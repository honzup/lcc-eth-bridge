#include "bridge.h"
#include "gridconnect.h"
#include "board_config.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_log.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us — pace multi-frame CAN-TX bursts
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <sys/time.h>
#include <string.h>

#define TAG         "bridge"
#define GC_PORT     12021       // OpenLCB/LCC GridConnect TCP port (JMRI connects here)
#define LCC_BITRATE 125000      // LCC CAN bus rate
// Inter-frame pacing (us) applied ONLY during a multi-frame burst (more frames
// already queued). A datagram's CAN frames are otherwise delivered back-to-back
// faster than an OpenMRNLite node services its TWAI RX, so it silently drops a
// MIDDLE frame -> the sequence-number-less reassembler stitches survivors ->
// truncated CDI writes (all fleet nodes; proven 2026-09-03: 4-frame writes
// mangle, 2-frame chunks land clean). Lone frames (events) are NOT paced.
#define CAN_TX_PACE_US 2500
#define TX_Q_LEN    1024         // s_tx_q depth (net->CAN). Deep enough to absorb JMRI TCP write
                                 // bursts that the slow 125kbit CAN drains behind (was 32 -> a
                                 // multi-frame CDI write overflowed it and silently dropped frames,
                                 // corrupting the write; ~16KB @ sizeof(can_frame_t), trivial on P4).
#define RX_Q_LEN    32           // s_rx_q depth (CAN->net); unchanged, named for its stats getter

static twai_node_handle_t s_node;
static QueueHandle_t      s_rx_q;      // CAN -> net
static QueueHandle_t      s_tx_q;      // net -> CAN (by value; owns the frame across transmit)
static volatile uint32_t  s_tx_drops = 0;  // frames dropped when s_tx_q is full (backpressure)
static volatile uint32_t  s_tx_q_max = 0;  // peak s_tx_q occupancy seen since boot (observability)
static volatile uint32_t  s_rx_drops = 0;  // frames dropped when s_rx_q is full (ISR couldn't enqueue)
static volatile uint32_t  s_rx_q_max = 0;  // peak s_rx_q occupancy seen since boot (observability)
static SemaphoreHandle_t  s_lock;
static int                s_client = -1;  // single JMRI connection
static TaskHandle_t       s_recover_task = NULL;  // notified from ISR on BUS_OFF
// ponytail: single client + one global lock. Fan out to an fd-list if you ever
// need more than one JMRI/tool attached at once.

// ---- TWAI RX (ISR) -> queue ----
static IRAM_ATTR bool on_rx_done(twai_node_handle_t h,
                                 const twai_rx_done_event_data_t *ev, void *ctx) {
    (void)ev; (void)ctx;
    uint8_t tmp[8];
    twai_frame_t rx = { .buffer = tmp, .buffer_len = sizeof tmp };
    if (twai_node_receive_from_isr(h, &rx) != ESP_OK) return false;
    can_frame_t f;
    f.id  = rx.header.id;
    f.ext = rx.header.ide;
    f.rtr = rx.header.rtr;                       // if this field name differs in your IDF, adjust
    f.dlc = twaifd_dlc2len(rx.header.dlc);
    if (f.dlc > 8) f.dlc = 8;
    memcpy(f.data, tmp, f.dlc);
    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_rx_q, &f, &woken) != pdTRUE) s_rx_drops++;
    UBaseType_t w = uxQueueMessagesWaitingFromISR(s_rx_q);
    if (w > s_rx_q_max) s_rx_q_max = w;
    return woken == pdTRUE;
}

// ---- TWAI state change (ISR): defer BUS_OFF recovery to a task ----
static IRAM_ATTR bool on_state_change(twai_node_handle_t h,
                                      const twai_state_change_event_data_t *e, void *ctx) {
    (void)h; (void)ctx;
    if (e->new_sta != TWAI_ERROR_BUS_OFF) return false;
    BaseType_t hpw = pdFALSE;
    vTaskNotifyGiveFromISR(s_recover_task, &hpw);
    return hpw == pdTRUE;
}

static void recover_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGW(TAG, "CAN bus-off -> recovering");
        esp_err_t err = twai_node_recover(s_node);
        ESP_LOGW(TAG, "twai_node_recover: %s", esp_err_to_name(err));
    }
}

// ---- CAN -> net ----
static void can_to_net_task(void *arg) {
    (void)arg;
    can_frame_t f;
    char line[32];
    for (;;) {
        if (xQueueReceive(s_rx_q, &f, portMAX_DELAY) != pdTRUE) continue;
        int n = gc_encode(&f, line, sizeof line);
        if (n < 0) continue;
#ifdef LCC_SELFTEST
        ESP_LOGI(TAG, "SELFTEST RX  %s", line);   // frame looped back to us
#endif
        // ponytail: keep the lock across send() rather than copy-fd-then-send.
        // A bare copy would race net_task closing/reusing the fd (send to a stale
        // or reassigned descriptor). The send is bounded instead by SO_SNDTIMEO
        // (set in net_task), so the hold is capped at that timeout, not forever.
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_client >= 0) (void)send(s_client, line, n, 0); // net_task reaps a dead fd
        xSemaphoreGive(s_lock);
    }
}

// ---- net -> CAN transmit task ----
// Owns each frame by value for the whole twai_node_transmit() call. The node
// driver stores the caller's twai_frame_t pointer (and its .buffer) and reads it
// later from the TX-done ISR, so the frame MUST outlive the call — a stack local
// in feed_byte did not, causing a use-after-free / LoadProhibited panic.
static void can_tx_task(void *arg) {
    (void)arg;
    can_frame_t f;
    for (;;) {
        if (xQueueReceive(s_tx_q, &f, portMAX_DELAY) != pdTRUE) continue;
        twai_frame_t tx = { .header.id = f.id, .header.ide = f.ext,
                            .buffer = f.data, .buffer_len = f.dlc };
        twai_node_transmit(s_node, &tx, portMAX_DELAY); // this task owns f for the whole call
        // Pace multi-frame datagram bursts so the receiving node's TWAI RX keeps
        // up. Only when more frames are already queued (i.e. mid-datagram) — a
        // lone frame (a normal event) is sent immediately, no added latency.
        if (uxQueueMessagesWaiting(s_tx_q) > 0)
            esp_rom_delay_us(CAN_TX_PACE_US);
    }
}

// ---- net -> CAN: assemble ';'-terminated GridConnect lines, transmit each ----
static void feed_byte(char c) {
    static char buf[40];
    static int  len = 0;
    static bool in  = false;
    if (c == ':') { in = true; len = 0; buf[len++] = c; return; }
    if (!in) return;
    if (len < (int)sizeof buf - 1) buf[len++] = c;
    if (c == ';') {
        buf[len] = '\0';
        in = false;
        can_frame_t f;
        if (gc_decode(buf, &f) == 0) {
            // Non-blocking hand-off to can_tx_task: the TCP reader must never block
            // on CAN. A full queue drops the frame (OpenLCB retries) — drop-count
            // backpressure rather than stalling the whole net path.
            if (xQueueSend(s_tx_q, &f, 0) != pdTRUE) s_tx_drops++;
            uint32_t occ = (uint32_t)uxQueueMessagesWaiting(s_tx_q);
            if (occ > s_tx_q_max) s_tx_q_max = occ;
        }
        // malformed line silently dropped (garbled/hostile input at the trust boundary)
    }
}

#ifdef LCC_SELFTEST
// Solo bring-up: transmit a test frame every second. With enable_self_test the
// TX needs no ACK (no second node), and enable_loopback returns it to on_rx_done,
// so you should see a matching SELFTEST RX line for every SELFTEST TX.
static void selftest_task(void *arg) {
    (void)arg;
    uint8_t data[8] = {0xDE,0xAD,0xBE,0xEF,0x00,0x00,0x00,0x00};
    uint32_t seq = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        data[7] = (uint8_t)seq;
        twai_frame_t tx = { .header.id = 0x0AAAAAAA, .header.ide = true,
                            .buffer = data, .buffer_len = sizeof data };
        esp_err_t e = twai_node_transmit(s_node, &tx, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "SELFTEST TX  id=0x0AAAAAAA seq=%lu -> %s",
                 (unsigned long)seq++, esp_err_to_name(e));
    }
}
#endif

static void net_task(void *arg) {
    (void)arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(GC_PORT),
                             .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(srv, (struct sockaddr *)&a, sizeof a) < 0 || listen(srv, 1) < 0) {
        ESP_LOGE(TAG, "listen on :%d failed", GC_PORT);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "GridConnect server on :%d", GC_PORT);
    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;
        // Harden the client socket: low latency for tiny GridConnect lines, detect
        // a silently-dropped peer, and bound a stalled send() (this timeout is what
        // caps can_to_net_task's lock hold above).
        int one = 1;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY,  &one, sizeof one);
        setsockopt(c, SOL_SOCKET,  SO_KEEPALIVE, &one, sizeof one);
        struct timeval snd_to = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(c, SOL_SOCKET,  SO_SNDTIMEO,  &snd_to, sizeof snd_to);
        ESP_LOGI(TAG, "JMRI connected");
        xSemaphoreTake(s_lock, portMAX_DELAY); s_client = c; xSemaphoreGive(s_lock);
        char rb[128];
        for (;;) {
            int n = recv(c, rb, sizeof rb, 0);
            if (n <= 0) break;
            for (int i = 0; i < n; ++i) feed_byte(rb[i]);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY); s_client = -1; xSemaphoreGive(s_lock);
        close(c);
        ESP_LOGI(TAG, "JMRI disconnected");
    }
}

void bridge_start(void) {
    s_rx_q = xQueueCreate(RX_Q_LEN, sizeof(can_frame_t));
    s_tx_q = xQueueCreate(TX_Q_LEN, sizeof(can_frame_t));
    s_lock = xSemaphoreCreateMutex();

    twai_onchip_node_config_t cfg = {
        .io_cfg.tx           = BOARD_TWAI_TX_GPIO,
        .io_cfg.rx           = BOARD_TWAI_RX_GPIO,
        .bit_timing.bitrate  = LCC_BITRATE,
        .tx_queue_depth      = 16,
    };
#ifdef LCC_SELFTEST
    cfg.flags.enable_self_test = true;   // TX completes without an ACK (no 2nd node)
    cfg.flags.enable_loopback  = true;   // receive our own transmitted frames
#endif
    ESP_ERROR_CHECK(twai_new_node_onchip(&cfg, &s_node));

    // Accept-all filter (mask=0 → every bit "don't care"). LCC is all 29-bit
    // extended IDs. Without this the node receives nothing — bus→JMRI would be dead.
    twai_mask_filter_config_t fc = { .id = 0, .mask = 0, .is_ext = true };
    ESP_ERROR_CHECK(twai_node_config_mask_filter(s_node, 0, &fc));

    // Create the recovery task before registering callbacks so s_recover_task is
    // valid the moment the state-change ISR could fire.
    xTaskCreate(recover_task, "can_recover", 3072, NULL, 11, &s_recover_task);

    twai_event_callbacks_t cbs = { .on_rx_done = on_rx_done,
                                   .on_state_change = on_state_change };
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(s_node, &cbs, NULL));
    ESP_ERROR_CHECK(twai_node_enable(s_node));

    xTaskCreate(can_to_net_task, "can2net", 4096, NULL, 10, NULL);
    xTaskCreate(can_tx_task,     "can_tx",  4096, NULL,  9, NULL);
    xTaskCreate(net_task,        "net",     4096, NULL,  9, NULL);
#ifdef LCC_SELFTEST
    ESP_LOGW(TAG, "SELF-TEST MODE: no-ACK + loopback, TX every 1s");
    xTaskCreate(selftest_task,   "selftest", 3072, NULL, 8, NULL);
#endif
}

// ---- Observability getters (read-only; called from ota.c's status page) ----

uint32_t bridge_tx_drops(void) { return s_tx_drops; }

void bridge_tx_q_stats(uint32_t *now, uint32_t *max, uint32_t *size) {
    if (now)  *now  = (uint32_t)uxQueueMessagesWaiting(s_tx_q);
    if (max)  *max  = s_tx_q_max;
    if (size) *size = TX_Q_LEN;
}

void bridge_rx_q_stats(uint32_t *drops, uint32_t *now, uint32_t *max, uint32_t *size) {
    if (drops) *drops = s_rx_drops;
    if (now)   *now   = (uint32_t)uxQueueMessagesWaiting(s_rx_q);
    if (max)   *max   = s_rx_q_max;
    if (size)  *size  = RX_Q_LEN;
}

bool bridge_can_health(bridge_can_health_t *h) {
    if (!s_node) return false;   // called before bridge_start() has run
    twai_node_status_t st;
    twai_node_record_t rec;
    if (twai_node_get_info(s_node, &st, &rec) != ESP_OK) return false;
    h->state           = (int)st.state;
    h->tx_error_count  = st.tx_error_count;
    h->rx_error_count  = st.rx_error_count;
    h->bus_err_num     = rec.bus_err_num;
    return true;
}
