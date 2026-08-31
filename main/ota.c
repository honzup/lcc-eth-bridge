#include "ota.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ota"

// ponytail: fixed 4 KB chunk buffer on the stack; plenty for LAN push, no
// heap churn. Bump if you ever want to trade RAM for fewer recv() calls.
#define OTA_CHUNK 4096

#ifdef OTA_TOKEN
// Constant-time-ish compare: no early-out on first mismatch, so timing does
// not leak the token length/prefix. Both are NUL-terminated C strings.
static bool token_ok(const char *got) {
    const char *want = OTA_TOKEN;
    size_t lg = strlen(got), lw = strlen(want);
    if (lw == 0 || lg == 0) return false;   // empty token/key never authenticates
    unsigned diff = (unsigned)(lg ^ lw);
    for (size_t i = 0; i < lg; i++) {
        diff |= (unsigned char)(got[i] ^ want[i % lw]);
    }
    return diff == 0;
}

static esp_err_t update_post(httpd_req_t *req) {
    // Authenticate FIRST — nothing touches flash until the token checks out.
    char query[128], key[96];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "key", key, sizeof(key)) != ESP_OK ||
        !token_ok(key)) {
        ESP_LOGW(TAG, "OTA rejected: missing/bad token");   // never log the value
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "unauthorized\n");
        return ESP_OK;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "no ota partition\n");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "OTA start -> %s (%d bytes incoming)", part->label, req->content_len);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "ota_begin failed\n");
        return ESP_OK;
    }

    char *buf = malloc(OTA_CHUNK);
    if (!buf) { esp_ota_abort(handle); httpd_resp_send_500(req); return ESP_OK; }

    int remaining = req->content_len, total = 0;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < OTA_CHUNK ? remaining : OTA_CHUNK);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { err = ESP_FAIL; break; }
        err = esp_ota_write(handle, buf, r);
        if (err != ESP_OK) break;
        remaining -= r;
        total += r;
    }
    free(buf);

    if (err == ESP_OK) err = esp_ota_end(handle);
    else               esp_ota_abort(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed after %d bytes: %s", total, esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "ota write/end failed\n");
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "set_boot failed\n");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA ok (%d bytes) -> %s, rebooting", total, part->label);
    httpd_resp_sendstr(req, "OK, rebooting\n");
    vTaskDelay(pdMS_TO_TICKS(500));   // let the response flush before we reset
    esp_restart();
    return ESP_OK;
}
#endif // OTA_TOKEN

static esp_err_t status_get(httpd_req_t *req) {
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_app_desc_t *app = esp_app_get_description();
    char body[256];
    int n = snprintf(body, sizeof(body),
        "lcc-eth-bridge OTA\n"
        "running partition: %s\n"
        "project: %s\n"
        "version: %s\n"
        "idf: %s\n",
        run ? run->label : "?", app->project_name, app->version, esp_get_idf_version());
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, body, n);
    return ESP_OK;
}

void ota_server_start(void) {
#ifndef OTA_TOKEN
    // Safe default: without a build-time token we never open the push endpoint.
    ESP_LOGW(TAG, "OTA server disabled: build with -DOTA_TOKEN=\"...\" to enable push-OTA");
    (void)status_get;
    return;
#else
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    cfg.recv_wait_timeout = 20;   // seconds; large image bodies stream slowly

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    httpd_uri_t u_update = { .uri = "/update", .method = HTTP_POST, .handler = update_post };
    httpd_uri_t u_status = { .uri = "/",       .method = HTTP_GET,  .handler = status_get };
    httpd_register_uri_handler(srv, &u_update);
    httpd_register_uri_handler(srv, &u_status);
    ESP_LOGI(TAG, "OTA server up: GET / (status), POST /update?key=... (push, token required)");
#endif
}
