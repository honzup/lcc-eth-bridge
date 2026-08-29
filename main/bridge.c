#include "bridge.h"
#include "gridconnect.h"
#include "board_config.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>

#define TAG         "bridge"
#define GC_PORT     12021       // OpenLCB/LCC GridConnect TCP port (JMRI connects here)
#define LCC_BITRATE 125000      // LCC CAN bus rate

static twai_node_handle_t s_node;
static QueueHandle_t      s_rx_q;      // CAN -> net
static SemaphoreHandle_t  s_lock;
static int                s_client = -1;  // single JMRI connection
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
    xQueueSendFromISR(s_rx_q, &f, &woken);
    return woken == pdTRUE;
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
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_client >= 0) (void)send(s_client, line, n, 0); // net_task reaps a dead fd
        xSemaphoreGive(s_lock);
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
            twai_frame_t tx = { .header.id = f.id, .header.ide = f.ext,
                                .buffer = f.data, .buffer_len = f.dlc };
            twai_node_transmit(s_node, &tx, pdMS_TO_TICKS(10));
        }
        // malformed line silently dropped (garbled/hostile input at the trust boundary)
    }
}

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
    s_rx_q = xQueueCreate(32, sizeof(can_frame_t));
    s_lock = xSemaphoreCreateMutex();

    twai_onchip_node_config_t cfg = {
        .io_cfg.tx           = BOARD_TWAI_TX_GPIO,
        .io_cfg.rx           = BOARD_TWAI_RX_GPIO,
        .bit_timing.bitrate  = LCC_BITRATE,
        .tx_queue_depth      = 16,
    };
    ESP_ERROR_CHECK(twai_new_node_onchip(&cfg, &s_node));
    twai_event_callbacks_t cbs = { .on_rx_done = on_rx_done };
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(s_node, &cbs, NULL));
    ESP_ERROR_CHECK(twai_node_enable(s_node));

    xTaskCreate(can_to_net_task, "can2net", 4096, NULL, 10, NULL);
    xTaskCreate(net_task,        "net",     4096, NULL,  9, NULL);
}
