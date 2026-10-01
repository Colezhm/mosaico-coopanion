// SPDX-License-Identifier: MIT
#include "coop_link.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdatomic.h>

#define RX_LIMIT (32 * 1024)
#define TX_LIMIT 16384
typedef struct {
    char *text;
    size_t length;
    bool binary;
    unsigned generation;
} tx_item_t;
struct coop_link_t {
    coop_link_config_t cfg;
    esp_websocket_client_handle_t ws;
    esp_netif_t *netif;
    QueueHandle_t tx;
    SemaphoreHandle_t mutex;
    TaskHandle_t worker;
    esp_event_handler_instance_t wifi_handler, ip_handler;
    char *rx, *provision, *certificate;
    size_t rx_size, rx_expected;
    char session[48], uri[256], headers[100];
    uint32_t incoming, outgoing;
    atomic_bool stop, connected;
    atomic_uint generation;
    bool started;
    wifi_config_t wifi;
};
static const char *str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)data;
    coop_link_handle_t h = arg;
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (!h->stop)
            esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (h->worker)
            xTaskNotifyGive(h->worker);
    }
}
static void receive(coop_link_handle_t h, cJSON *m)
{
    const char *type = str(m, "t"), *session = str(m, "session");
    cJSON *v = cJSON_GetObjectItem(m, "v"), *seq = cJSON_GetObjectItem(m, "seq");
    if (!cJSON_IsNumber(v) || v->valueint != 1 || !cJSON_IsNumber(seq) || seq->valuedouble < 1 ||
        seq->valuedouble > UINT32_MAX) {
        cJSON_Delete(m);
        return;
    }
    if (!strcmp(type, "hello")) {
        if (strlen(session) >= sizeof(h->session)) {
            cJSON_Delete(m);
            return;
        }
        xSemaphoreTake(h->mutex, portMAX_DELAY);
        snprintf(h->session, sizeof(h->session), "%s", session);
        h->incoming = (uint32_t)seq->valuedouble;
        h->outgoing = 0;
        xSemaphoreGive(h->mutex);
        cJSON_Delete(m);
        return;
    }
    if (strcmp(session, h->session) || (uint32_t)seq->valuedouble <= h->incoming) {
        cJSON_Delete(m);
        return;
    }
    h->incoming = (uint32_t)seq->valuedouble;
    if (!strcmp(type, "clock_ping")) {
        cJSON *reply = cJSON_CreateObject();
        cJSON_AddStringToObject(reply, "t", "clock_pong");
        cJSON *at = cJSON_GetObjectItem(m, "at");
        cJSON_AddNumberToObject(reply, "echo", cJSON_IsNumber(at) ? at->valuedouble : 0);
        cJSON_AddNumberToObject(reply, "at", esp_timer_get_time() / 1000.0);
        coop_link_send(h, reply);
        cJSON_Delete(m);
        return;
    }
    if (h->cfg.message)
        h->cfg.message(h->cfg.ctx, m);
    else
        cJSON_Delete(m);
}
static void ws_event(void *arg, esp_event_base_t base, int32_t event, void *event_data)
{
    (void)base;
    coop_link_handle_t h = arg;
    esp_websocket_event_data_t *d = event_data;
    if (event == WEBSOCKET_EVENT_CONNECTED) {
        xSemaphoreTake(h->mutex, portMAX_DELAY);
        atomic_fetch_add(&h->generation, 1);
        h->connected = true;
        h->session[0] = 0;
        xSemaphoreGive(h->mutex);
        h->rx_size = h->rx_expected = 0;
        if (h->cfg.connection)
            h->cfg.connection(h->cfg.ctx, true);
    } else if (event == WEBSOCKET_EVENT_DISCONNECTED) {
        xSemaphoreTake(h->mutex, portMAX_DELAY);
        h->connected = false;
        h->session[0] = 0;
        atomic_fetch_add(&h->generation, 1);
        xSemaphoreGive(h->mutex);
        if (h->cfg.connection)
            h->cfg.connection(h->cfg.ctx, false);
    } else if (event == WEBSOCKET_EVENT_DATA) {
        if (d->op_code != 1 && d->op_code != 0)
            return;
        if (d->payload_offset == 0) {
            h->rx_size = 0;
            h->rx_expected = d->payload_len;
        }
        if (d->payload_len < 0 || h->rx_expected >= RX_LIMIT ||
            d->payload_offset != (int)h->rx_size || d->data_len < 0 ||
            h->rx_size + (size_t)d->data_len > h->rx_expected) {
            h->rx_size = 0;
            return;
        }
        memcpy(h->rx + h->rx_size, d->data_ptr, d->data_len);
        h->rx_size += d->data_len;
        if (h->rx_size == h->rx_expected) {
            h->rx[h->rx_size] = 0;
            cJSON *m = cJSON_ParseWithLength(h->rx, h->rx_size);
            if (m)
                receive(h, m);
            h->rx_size = 0;
        }
    }
}
static void worker(void *arg)
{
    coop_link_handle_t h = arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!h->stop) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        /* TLS validity is checked; never disable verification to get past a bad clock. */
        for (int i = 0; i < 60 && time(NULL) < 1700000000 && !h->stop; i++)
            vTaskDelay(pdMS_TO_TICKS(500));
        esp_websocket_client_config_t c = {.uri = h->uri,
                                           .cert_pem = h->certificate,
                                           .headers = h->headers,
                                           .buffer_size = 4096,
                                           .task_stack = 6144,
                                           .network_timeout_ms = 3000,
                                           .reconnect_timeout_ms = 2000};
        h->ws = esp_websocket_client_init(&c);
        if (h->ws) {
            esp_websocket_register_events(h->ws, WEBSOCKET_EVENT_ANY, ws_event, h);
            esp_websocket_client_start(h->ws);
        }
    }
    tx_item_t item;
    while (!h->stop) {
        if (xQueueReceive(h->tx, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (h->connected && h->ws && item.generation == atomic_load(&h->generation)) {
                int sent = item.binary
                               ? esp_websocket_client_send_bin(h->ws, item.text, item.length,
                                                               pdMS_TO_TICKS(100))
                               : esp_websocket_client_send_text(h->ws, item.text, item.length,
                                                                pdMS_TO_TICKS(100));
                if (sent != (int)item.length && h->cfg.connection)
                    h->cfg.connection(h->cfg.ctx, false);
            }
            free(item.text);
        }
    }
    if (h->ws) {
        esp_websocket_client_stop(h->ws);
        esp_websocket_client_destroy(h->ws);
        h->ws = NULL;
    }
    h->worker = NULL;
    vTaskDelete(NULL);
}
esp_err_t coop_link_create(const coop_link_config_t *cfg, coop_link_handle_t *out)
{
    if (!cfg || !out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    coop_link_handle_t h = *out;
    h->cfg = *cfg;
    h->rx = malloc(RX_LIMIT);
    h->tx = xQueueCreate(24, sizeof(tx_item_t));
    h->mutex = xSemaphoreCreateMutex();
    if (!h->rx || !h->tx || !h->mutex) {
        coop_link_delete(h);
        *out = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t coop_link_provision(coop_link_handle_t h, const uint8_t *data, size_t size)
{
    if (!h || !data || size > 6144)
        return ESP_ERR_INVALID_ARG;
    cJSON *j = cJSON_ParseWithLength((const char *)data, size);
    if (!j)
        return ESP_ERR_INVALID_ARG;
    const char *ssid = str(j, "ssid"), *password = str(j, "password"), *uri = str(j, "uri"),
               *token = str(j, "token"), *cert = str(j, "certificate");
    bool valid = strlen(ssid) > 0 && strlen(ssid) <= 32 && strlen(password) <= 63 &&
                 strlen(uri) < 256 && !strncmp(uri, "wss://", 6) && strlen(token) == 64 &&
                 strlen(cert) > 64 && strlen(cert) < 4096;
    if (!valid) {
        cJSON_Delete(j);
        return ESP_ERR_INVALID_ARG;
    }
    char *text = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!text)
        return ESP_ERR_NO_MEM;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("coo_pair", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "config", text);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    free(text);
    return err;
}
esp_err_t coop_link_start(coop_link_handle_t h)
{
    if (!h || h->started)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    size_t size = 0;
    esp_err_t err = nvs_open("coo_pair", NVS_READONLY, &nvs);
    if (err != ESP_OK)
        return err;
    err = nvs_get_str(nvs, "config", NULL, &size);
    if (err != ESP_OK || size > 6144) {
        nvs_close(nvs);
        return ESP_ERR_INVALID_STATE;
    }
    h->provision = malloc(size);
    if (!h->provision) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_str(nvs, "config", h->provision, &size);
    nvs_close(nvs);
    if (err != ESP_OK)
        return err;
    cJSON *j = cJSON_Parse(h->provision);
    if (!j)
        return ESP_ERR_INVALID_ARG;
    snprintf(h->uri, sizeof(h->uri), "%s", str(j, "uri"));
    snprintf(h->headers, sizeof(h->headers), "Authorization: Bearer %s\r\n", str(j, "token"));
    h->certificate = strdup(str(j, "certificate"));
    strncpy((char *)h->wifi.sta.ssid, str(j, "ssid"), sizeof(h->wifi.sta.ssid));
    strncpy((char *)h->wifi.sta.password, str(j, "password"), sizeof(h->wifi.sta.password));
    cJSON_Delete(j);
    free(h->provision);
    h->provision = NULL;
    if (!h->certificate)
        return ESP_ERR_NO_MEM;
    err = esp_netif_init();
    if (err != ESP_OK)
        return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    h->netif = esp_netif_create_default_wifi_sta();
    if (!h->netif)
        return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
        return err;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, h, &h->wifi_handler));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, h, &h->ip_handler));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &h->wifi);
    h->started = true;
    if (xTaskCreate(worker, "coo_link", 6144, h, 5, &h->worker) != pdPASS)
        return ESP_ERR_NO_MEM;
    return esp_wifi_start();
}
bool coop_link_send(coop_link_handle_t h, cJSON *m)
{
    if (!m)
        return false;
    if (!h || !h->connected) {
        cJSON_Delete(m);
        return false;
    }
    if (xSemaphoreTake(h->mutex, 0) != pdTRUE) {
        cJSON_Delete(m);
        return false;
    }
    if (!h->connected || !h->session[0]) {
        xSemaphoreGive(h->mutex);
        cJSON_Delete(m);
        return false;
    }
    cJSON_AddNumberToObject(m, "v", 1);
    cJSON_AddStringToObject(m, "session", h->session);
    cJSON_AddNumberToObject(m, "seq", ++h->outgoing);
    char *text = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (!text) {
        xSemaphoreGive(h->mutex);
        return false;
    }
    tx_item_t item = {
        .text = text, .length = strlen(text), .generation = atomic_load(&h->generation)};
    if (item.length > TX_LIMIT || xQueueSend(h->tx, &item, 0) != pdTRUE) {
        xSemaphoreGive(h->mutex);
        free(text);
        return false;
    }
    xSemaphoreGive(h->mutex);
    return true;
}
bool coop_link_audio(coop_link_handle_t h, const void *data, size_t size)
{
    if (!h || !h->connected || !data || size > 1292)
        return false;
    tx_item_t item = {.text = malloc(size),
                      .length = size,
                      .binary = true,
                      .generation = atomic_load(&h->generation)};
    if (!item.text)
        return false;
    memcpy(item.text, data, size);
    if (xQueueSend(h->tx, &item, 0) != pdTRUE) {
        free(item.text);
        return false;
    }
    return true;
}
void coop_link_delete(coop_link_handle_t h)
{
    if (!h)
        return;
    h->stop = true;
    if (h->worker) {
        xTaskNotifyGive(h->worker);
        while (h->worker)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (h->started) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h->wifi_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h->ip_handler);
        esp_wifi_stop();
        esp_wifi_deinit();
    }
    if (h->netif)
        esp_netif_destroy_default_wifi(h->netif);
    if (h->tx) {
        tx_item_t item;
        while (xQueueReceive(h->tx, &item, 0) == pdTRUE)
            free(item.text);
        vQueueDelete(h->tx);
    }
    if (h->mutex)
        vSemaphoreDelete(h->mutex);
    free(h->certificate);
    free(h->provision);
    free(h->rx);
    free(h);
}
