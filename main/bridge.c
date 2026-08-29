#include "bridge.h"
#include "gridconnect.h"
#include "board_config.h"
#include "driver/twai.h"        // legacy TWAI driver (IDF 5.4: node-based esp_twai is 5.5+)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>

#define TAG         "bridge"
#define GC_PORT     12021       // OpenLCB/LCC GridConnect TCP port (JMRI connects here)

static SemaphoreHandle_t s_lock;
static int               s_client = -1;  // single JMRI connection
// ponytail: single client + one global lock. Fan out to an fd-list if you ever
// need more than one JMRI/tool attached at once.

static void send_line(const char *line, int n) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_client >= 0) (void)send(s_client, line, n, 0); // net_task reaps a dead fd
    xSemaphoreGive(s_lock);
}

// ---- CAN -> net: blocking receive loop (legacy driver has no ISR callback) ----
static void can_rx_task(void *arg) {
    (void)arg;
    twai_message_t m;
    char line[32];
    for (;;) {
        if (twai_receive(&m, portMAX_DELAY) != ESP_OK) continue;
        can_frame_t f;
        f.id  = m.identifier;
        f.ext = m.extd;
        f.rtr = m.rtr;
        f.dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
        memcpy(f.data, m.data, f.dlc);
        int n = gc_encode(&f, line, sizeof line);
        if (n > 0) send_line(line, n);
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
            twai_message_t m = {0};
            m.identifier       = f.id;
            m.extd             = f.ext;
            m.rtr              = f.rtr;
            m.data_length_code = f.dlc;
            memcpy(m.data, f.data, f.dlc);
            twai_transmit(&m, pdMS_TO_TICKS(10));
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
    s_lock = xSemaphoreCreateMutex();

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        BOARD_TWAI_TX_GPIO, BOARD_TWAI_RX_GPIO, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_125KBITS();   // LCC bus rate
    twai_filter_config_t  fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &fcfg));
    ESP_ERROR_CHECK(twai_start());

    xTaskCreate(can_rx_task, "can_rx", 4096, NULL, 10, NULL);
    xTaskCreate(net_task,    "net",    4096, NULL,  9, NULL);
}
