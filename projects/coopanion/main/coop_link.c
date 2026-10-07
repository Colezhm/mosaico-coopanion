// SPDX-License-Identifier: MIT
#include "coop_link.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "coop_wifi_device.h"
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
#include <stdatomic.h>

#define RX_LIMIT (32 * 1024)
#define TX_LIMIT 16384
/* A send that fails while the socket still reports connected is a busy client
 * lock, not a dead link: real transport errors abort the connection inside
 * esp_websocket_client and arrive as WEBSOCKET_EVENT_DISCONNECTED. */
#define TX_ATTEMPTS 3
#define TX_ATTEMPT_TIMEOUT_MS 400
#define SYSTEM_WIFI_PARTITION "sysmeta"
#define SYSTEM_WIFI_NAMESPACE "wifi"
typedef struct {
    char *text;
    size_t length;
    bool binary;
    unsigned generation;
} tx_item_t;
struct coop_link_t {
    coop_link_config_t cfg;
    esp_websocket_client_handle_t ws;
    QueueHandle_t tx;
    SemaphoreHandle_t mutex;
    TaskHandle_t worker;
    char *rx, *provision, *certificate;
    size_t rx_size, rx_expected;
    char session[48], uri[256], headers[100];
    uint32_t incoming, outgoing;
    atomic_bool stop, connected, low_latency;
    atomic_uint generation;
    bool started, power_save_applied, power_save_known;
    unsigned dropped;
};
static const char *str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
typedef struct {
    char ssid[33];
    char password[64];
} system_wifi_t;
static void clear_secret(void *data, size_t size)
{
    volatile uint8_t *p = data;
    while (size--)
        *p++ = 0;
}
static void clear_json_secrets(cJSON *j)
{
    static const char *const keys[] = {"token", "password"};
    for (unsigned i = 0; j && i < sizeof(keys) / sizeof(keys[0]); i++) {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(j, keys[i]);
        if (cJSON_IsString(v) && v->valuestring)
            clear_secret(v->valuestring, strlen(v->valuestring));
    }
}
/* Vibe Mode 0.1.4 stores Wi-Fi in retained sysmeta/wifi. Read it only on
 * explicit USB pairing opt-in; credentials never travel back to the host. */
static esp_err_t system_wifi_read(system_wifi_t *wifi)
{
    memset(wifi, 0, sizeof(*wifi));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition(SYSTEM_WIFI_PARTITION, SYSTEM_WIFI_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK)
        return err;
    size_t ssid_size = sizeof(wifi->ssid), password_size = sizeof(wifi->password);
    err = nvs_get_str(nvs, "ssid", wifi->ssid, &ssid_size);
    if (err == ESP_OK)
        err = nvs_get_str(nvs, "password", wifi->password, &password_size);
    nvs_close(nvs);
    if (err == ESP_OK && !wifi->ssid[0])
        err = ESP_ERR_INVALID_STATE;
    if (err != ESP_OK)
        clear_secret(wifi, sizeof(*wifi));
    return err;
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
/* Modem sleep saves battery while Coo idles, but adds a DTIM wake (often
 * 100-300 ms) to every downlink frame. The board asks for low latency whenever
 * timing matters (transfer, voice, asset sync, recent touch/button); the
 * desktop's minimum-RTT clock filter keeps the session usable in between. */
static void apply_power_save(coop_link_handle_t h)
{
    bool want_awake = atomic_load(&h->low_latency) || !COOP_LINK_IDLE_POWER_SAVE;
    if (h->power_save_known && h->power_save_applied == !want_awake)
        return;
    if (esp_wifi_set_ps(want_awake ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM) == ESP_OK) {
        h->power_save_applied = !want_awake;
        h->power_save_known = true;
    }
}
static void worker(void *arg)
{
    coop_link_handle_t h = arg;
    /* Wait for an address unless Wi-Fi already has one. */
    coop_wifi_status_t wifi;
    coop_wifi_status(&wifi);
    if (wifi.link != COOP_WIFI_CONNECTED)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!h->stop) {
        /* The desktop certificate is pinned (cert_pem is the only trust anchor),
         * and ESP-IDF's default mbedTLS build does not check certificate dates.
         * Wall time is only for logs, so SNTP runs in the background instead of
         * delaying a LAN-only board by up to 30 s on every boot. */
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
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
        apply_power_save(h);
        if (xQueueReceive(h->tx, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool sent = false;
            for (unsigned attempt = 0; attempt < TX_ATTEMPTS && !sent && !h->stop; attempt++) {
                if (!h->connected || !h->ws || item.generation != atomic_load(&h->generation) ||
                    !esp_websocket_client_is_connected(h->ws))
                    break;
                const TickType_t timeout = pdMS_TO_TICKS(TX_ATTEMPT_TIMEOUT_MS);
                int written = item.binary
                                  ? esp_websocket_client_send_bin(h->ws, item.text, item.length, timeout)
                                  : esp_websocket_client_send_text(h->ws, item.text, item.length, timeout);
                sent = written == (int)item.length;
            }
            /* Never report "offline" here: only WEBSOCKET_EVENT_DISCONNECTED owns
             * that state, otherwise a busy lock leaves the board stuck offline on a
             * live session until the next reconnect. */
            if (!sent && item.generation == atomic_load(&h->generation) && h->connected &&
                (++h->dropped & 15) == 1)
                ESP_LOGW("coo_link", "dropped %u frame(s) on a busy session", h->dropped);
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
    bool system_wifi = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "use_system_wifi"));
    bool valid = (system_wifi || (strlen(ssid) > 0 && strlen(ssid) <= 32 && strlen(password) <= 63)) &&
                 strlen(uri) < 256 && !strncmp(uri, "wss://", 6) && strlen(token) == 64 &&
                 strlen(cert) > 64 && strlen(cert) < 4096;
    if (!valid) {
        clear_json_secrets(j);
        cJSON_Delete(j);
        return ESP_ERR_INVALID_ARG;
    }
    if (system_wifi) {
        system_wifi_t wifi;
        esp_err_t err = system_wifi_read(&wifi);
        clear_secret(&wifi, sizeof(wifi));
        if (err != ESP_OK) {
            clear_json_secrets(j);
            cJSON_Delete(j);
            return err;
        }
        cJSON_DeleteItemFromObjectCaseSensitive(j, "ssid");
        cJSON_DeleteItemFromObjectCaseSensitive(j, "password");
    }
    char *text = cJSON_PrintUnformatted(j);
    clear_json_secrets(j);
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
    clear_secret(text, strlen(text));
    free(text);
    return err;
}
/* Reads the pairing: desktop address, token and pinned certificate, plus the
 * Wi-Fi it was paired with (unless that is Vibe Mode's own). */
static esp_err_t read_provision(coop_link_handle_t h, system_wifi_t *wifi)
{
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
    cJSON *j = err == ESP_OK ? cJSON_Parse(h->provision) : NULL;
    if (err == ESP_OK && !j)
        err = ESP_ERR_INVALID_ARG;
    if (j) {
        snprintf(h->uri, sizeof(h->uri), "%s", str(j, "uri"));
        snprintf(h->headers, sizeof(h->headers), "Authorization: Bearer %s\r\n", str(j, "token"));
        h->certificate = strdup(str(j, "certificate"));
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "use_system_wifi"))) {
            snprintf(wifi->ssid, sizeof(wifi->ssid), "%s", str(j, "ssid"));
            snprintf(wifi->password, sizeof(wifi->password), "%s", str(j, "password"));
        }
        clear_json_secrets(j);
        cJSON_Delete(j);
    }
    /* The provision copy holds the token and possibly a Wi-Fi password: wipe it
     * on every path, including read and parse failures. */
    clear_secret(h->provision, size);
    free(h->provision);
    h->provision = NULL;
    return err;
}
static void got_ip(void *ctx)
{
    coop_link_handle_t h = ctx;
    if (h->worker)
        xTaskNotifyGive(h->worker);
}
esp_err_t coop_link_start(coop_link_handle_t h)
{
    if (!h || h->started)
        return ESP_ERR_INVALID_ARG;
    /* Wi-Fi comes up even before pairing, so the board's Wi-Fi page works; the
     * paired network (or Vibe Mode's) is its read-only default. */
    system_wifi_t wifi = {0};
    esp_err_t err = read_provision(h, &wifi);
    if (!wifi.ssid[0]) {
        system_wifi_t vibe;
        if (system_wifi_read(&vibe) == ESP_OK)
            wifi = vibe;
        clear_secret(&vibe, sizeof(vibe));
    }
    esp_err_t wifi_err = coop_wifi_start(wifi.ssid[0] ? wifi.ssid : NULL, wifi.password, got_ip, h);
    clear_secret(&wifi, sizeof(wifi));
    if (wifi_err != ESP_OK)
        return wifi_err;
    if (err != ESP_OK)
        return err;
    if (!h->certificate)
        return ESP_ERR_NO_MEM;
    /* Start awake: pairing, clock sampling and the first asset sync all need
     * low latency. apply_power_save() owns the mode afterwards. */
    atomic_store(&h->low_latency, true);
    if (esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) {
        h->power_save_known = true;
        h->power_save_applied = false;
    }
    h->started = true;
    if (xTaskCreate(worker, "coo_link", 6144, h, 5, &h->worker) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
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
    /* Wi-Fi belongs to coop_wifi and outlives the link. */
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
void coop_link_set_low_latency(coop_link_handle_t h, bool low_latency)
{
    if (h)
        atomic_store(&h->low_latency, low_latency);
}
