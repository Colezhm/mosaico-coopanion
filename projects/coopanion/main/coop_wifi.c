// SPDX-License-Identifier: MIT
/* Device Wi-Fi: one worker task owns every esp_wifi call and the saved list;
 * other tasks queue requests and read a mutex-guarded status snapshot. */
#include "coop_wifi.h"
#include "coop_wifi_device.h"
#include "coop_wifi_pick.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

#define TAG "coo_wifi"
#define NVS_NAMESPACE "coo_wifi"
#define NVS_LIST_KEY "list"
#define JOIN_TIMEOUT_MS 15000
#define RECONNECT_BASE_MS 500
#define ROAM_AFTER_FAILURES 4
#define USER_JOIN_ATTEMPTS 2
#define RSSI_REFRESH_MS 10000
#define SEARCH_EVERY_MS 30000
#define WORKER_STACK 6144 /* scan, known list and pick buffers nest on this stack */

typedef enum { CMD_BOOT, CMD_SCAN, CMD_JOIN, CMD_FORGET, CMD_DISCONNECTED, CMD_GOT_IP } cmd_kind_t;
typedef struct {
    cmd_kind_t kind;
    uint8_t reason;
    bool use_saved;
    char ssid[COOP_WIFI_SSID_BYTES];
    char password[COOP_WIFI_PASSWORD_BYTES];
    char ip[16];
} cmd_t;

static struct {
    QueueHandle_t queue;
    SemaphoreHandle_t mutex;
    coop_wifi_status_t status; /* guarded by mutex */
    /* Worker-owned. */
    coop_wifi_cred_t saved[COOP_WIFI_SAVED];
    unsigned saved_count;
    coop_wifi_cred_t fallback, current, previous;
    bool has_fallback, has_previous, user_join;
    unsigned failures;
    uint64_t join_started, rssi_at, search_at;
    coop_wifi_ip_cb_t on_ip;
    void *ctx;
    bool started;
} s;

static void wipe(void *data, size_t size)
{
    volatile uint8_t *p = data;
    while (size--)
        *p++ = 0;
}
static uint64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---------- status ---------- */

static void publish(void (*change)(coop_wifi_status_t *, const void *), const void *arg)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    change(&s.status, arg);
    s.status.revision++;
    xSemaphoreGive(s.mutex);
}
static bool is_saved(const char *ssid)
{
    for (unsigned i = 0; i < s.saved_count; i++)
        if (!strcmp(s.saved[i].ssid, ssid))
            return true;
    return false;
}
static void set_link(coop_wifi_status_t *st, const void *arg)
{
    const coop_wifi_link_t *link = arg;
    st->link = *link;
    snprintf(st->ssid, sizeof(st->ssid), "%s", s.current.ssid);
    st->current_saved = is_saved(s.current.ssid);
    if (*link != COOP_WIFI_CONNECTED)
        st->ip[0] = 0;
    if (*link == COOP_WIFI_CONNECTED)
        st->error[0] = 0;
    for (unsigned i = 0; i < st->ap_count; i++)
        st->aps[i].saved = is_saved(st->aps[i].ssid);
}
static void set_error(coop_wifi_status_t *st, const void *arg)
{
    snprintf(st->error, sizeof(st->error), "%s", (const char *)arg);
    st->error_revision++;
    st->link = COOP_WIFI_FAILED;
}
static void set_ip(coop_wifi_status_t *st, const void *arg)
{
    snprintf(st->ip, sizeof(st->ip), "%s", (const char *)arg);
}
static void set_rssi(coop_wifi_status_t *st, const void *arg)
{
    st->rssi = *(const int8_t *)arg;
}
static void set_scanning(coop_wifi_status_t *st, const void *arg)
{
    st->scanning = *(const bool *)arg;
}
typedef struct {
    const coop_wifi_ap_t *aps;
    unsigned count;
} scan_result_t;
static void set_scan(coop_wifi_status_t *st, const void *arg)
{
    const scan_result_t *r = arg;
    st->ap_count = r->count;
    memcpy(st->aps, r->aps, r->count * sizeof(r->aps[0]));
    st->scanning = false;
    st->current_saved = is_saved(s.current.ssid);
}

/* ---------- saved networks ---------- */

static void load_saved(void)
{
    nvs_handle_t nvs;
    s.saved_count = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return;
    size_t size = sizeof(s.saved);
    if (nvs_get_blob(nvs, NVS_LIST_KEY, s.saved, &size) == ESP_OK && size % sizeof(s.saved[0]) == 0)
        s.saved_count = size / sizeof(s.saved[0]);
    nvs_close(nvs);
    for (unsigned i = 0; i < s.saved_count; i++) {
        s.saved[i].ssid[COOP_WIFI_SSID_BYTES - 1] = 0;
        s.saved[i].password[COOP_WIFI_PASSWORD_BYTES - 1] = 0;
    }
}
static void store_saved(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
        return;
    esp_err_t err = s.saved_count ? nvs_set_blob(nvs, NVS_LIST_KEY, s.saved, s.saved_count * sizeof(s.saved[0]))
                                  : nvs_erase_key(nvs, NVS_LIST_KEY);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND)
        err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "saving the network list failed (%s)", esp_err_to_name(err));
}
/* Saved networks, most recent first, then the read-only default. */
static unsigned known(coop_wifi_cred_t *out)
{
    unsigned n = s.saved_count;
    memcpy(out, s.saved, n * sizeof(out[0]));
    if (s.has_fallback && !is_saved(s.fallback.ssid))
        out[n++] = s.fallback;
    return n;
}
static bool find_password(const char *ssid, char *password)
{
    coop_wifi_cred_t list[COOP_WIFI_SAVED + 1];
    unsigned n = known(list);
    bool found = false;
    for (unsigned i = 0; i < n; i++)
        if (!strcmp(list[i].ssid, ssid)) {
            memcpy(password, list[i].password, COOP_WIFI_PASSWORD_BYTES);
            found = true;
        }
    wipe(list, sizeof(list));
    return found;
}

/* ---------- radio ---------- */

static void connect_to(const coop_wifi_cred_t *cred)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, cred->ssid, strnlen(cred->ssid, sizeof(config.sta.ssid)));
    memcpy(config.sta.password, cred->password, strnlen(cred->password, sizeof(config.sta.password)));
    config.sta.threshold.authmode = cred->password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    if (&s.current != cred)
        s.current = *cred;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &config);
    wipe(&config, sizeof(config));
    s.join_started = now_ms();
    coop_wifi_link_t link = COOP_WIFI_CONNECTING;
    publish(set_link, &link);
    esp_err_t err = esp_wifi_connect();
    /* The network name stays out of the log; its length tells networks apart. */
    ESP_LOGI(TAG, "joining a %u-byte network (%s)%s%s", (unsigned)strlen(cred->ssid),
             cred->password[0] ? "secured" : "open", err == ESP_OK ? "" : ": ",
             err == ESP_OK ? "" : esp_err_to_name(err));
}
/* Try the current network again from a clean state: a driver still busy with
 * the previous attempt refuses a new connect. */
static void retry(void)
{
    esp_wifi_disconnect();
    s.join_started = now_ms();
    coop_wifi_link_t link = COOP_WIFI_CONNECTING;
    publish(set_link, &link);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
        ESP_LOGW(TAG, "retry failed to start (%s)", esp_err_to_name(err));
}
static unsigned scan(coop_wifi_ap_t *aps)
{
    bool on = true;
    publish(set_scanning, &on);
    uint16_t count = COOP_WIFI_SCAN_MAX;
    wifi_ap_record_t *records = calloc(COOP_WIFI_SCAN_MAX, sizeof(*records));
    unsigned n = 0;
    if (records && esp_wifi_scan_start(NULL, true) == ESP_OK &&
        esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
        for (unsigned i = 0; i < count && n < COOP_WIFI_SCAN_MAX; i++) {
            memset(&aps[n], 0, sizeof(aps[n]));
            snprintf(aps[n].ssid, sizeof(aps[n].ssid), "%s", (const char *)records[i].ssid);
            aps[n].rssi = records[i].rssi;
            aps[n].secure = records[i].authmode != WIFI_AUTH_OPEN;
            n++;
        }
    }
    free(records);
    coop_wifi_cred_t list[COOP_WIFI_SAVED + 1];
    unsigned k = known(list);
    n = coop_wifi_tidy(aps, n, list, k);
    wipe(list, sizeof(list));
    unsigned known_seen = 0;
    for (unsigned i = 0; i < n; i++)
        known_seen += aps[i].saved;
    ESP_LOGI(TAG, "scan: %u networks, %u of %u known in range", n, known_seen, k);
    scan_result_t result = {aps, n};
    publish(set_scan, &result);
    return n;
}
/* Join the best known network in range, avoiding @p except; true if one was
 * chosen. @p blind tries the most recent one even when unseen (it may hide its
 * name). */
static bool join_best(const char *except, bool blind)
{
    coop_wifi_ap_t aps[COOP_WIFI_SCAN_MAX];
    unsigned seen = scan(aps);
    coop_wifi_cred_t list[COOP_WIFI_SAVED + 1];
    unsigned k = known(list);
    if (except)
        for (unsigned i = 0; i < seen; i++)
            if (!strcmp(aps[i].ssid, except))
                aps[i].ssid[0] = 0;
    int pick = coop_wifi_pick(list, k, aps, seen);
    /* Nothing known in range: try the most recent anyway (it may hide its SSID). */
    if (pick < 0 && k && blind)
        pick = 0;
    if (pick >= 0)
        connect_to(&list[pick]);
    wipe(list, sizeof(list));
    return pick >= 0;
}
/* Leave the current attempt and join the best known network in range; with
 * none, stay idle and look again every SEARCH_EVERY_MS. */
static void search(const char *except)
{
    esp_wifi_disconnect();
    s.search_at = now_ms();
    if (join_best(except, false))
        return;
    coop_wifi_link_t link = COOP_WIFI_OFF;
    publish(set_link, &link);
    ESP_LOGI(TAG, "no known network in range; looking again in %u s", SEARCH_EVERY_MS / 1000);
}
static bool own_leave(uint8_t reason)
{
    return reason == WIFI_REASON_ASSOC_LEAVE || reason == WIFI_REASON_STA_LEAVING;
}
static bool auth_failure(uint8_t reason)
{
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_EXPIRE ||
           reason == WIFI_REASON_CONNECTION_FAIL;
}
/* A join the person asked for failed: say why and go back where we were. */
static void abandon_join(uint8_t reason)
{
    char message[64];
    snprintf(message, sizeof(message), "%s：%s", s.current.ssid,
             reason == WIFI_REASON_NO_AP_FOUND ? "找不到这个网络" : auth_failure(reason) ? "密码不对" : "连不上");
    s.user_join = false;
    publish(set_error, message);
    s.search_at = now_ms();
    if (s.has_previous) {
        coop_wifi_cred_t back = s.previous;
        s.has_previous = false;
        connect_to(&back);
        wipe(&back, sizeof(back));
    } else
        esp_wifi_disconnect();
}

static void handle(cmd_t *c)
{
    switch (c->kind) {
    case CMD_BOOT:
        s.search_at = now_ms();
        if (!join_best(NULL, true))
            ESP_LOGI(TAG, "no known network yet; use the Wi-Fi page");
        break;
    case CMD_SCAN: {
        /* The radio cannot scan while it is joining: pause the attempt. */
        bool joining = s.status.link == COOP_WIFI_CONNECTING;
        if (joining)
            esp_wifi_disconnect();
        coop_wifi_ap_t aps[COOP_WIFI_SCAN_MAX];
        scan(aps);
        if (joining)
            retry();
        break;
    }
    case CMD_JOIN: {
        coop_wifi_cred_t target = {0};
        snprintf(target.ssid, sizeof(target.ssid), "%s", c->ssid);
        if (c->use_saved) {
            if (!find_password(c->ssid, target.password)) {
                publish(set_error, "没有保存这个网络的密码");
                break;
            }
        } else
            snprintf(target.password, sizeof(target.password), "%s", c->password);
        if (s.status.link == COOP_WIFI_CONNECTED || s.status.link == COOP_WIFI_CONNECTING) {
            s.previous = s.current;
            s.has_previous = strcmp(s.current.ssid, target.ssid) != 0;
        }
        s.user_join = true;
        s.failures = 0;
        connect_to(&target);
        wipe(&target, sizeof(target));
        break;
    }
    case CMD_FORGET:
        s.saved_count = coop_wifi_drop(s.saved, s.saved_count, c->ssid);
        store_saved();
        if (!strcmp(s.current.ssid, c->ssid) && !(s.has_fallback && !strcmp(s.fallback.ssid, c->ssid)))
            search(c->ssid);
        else {
            coop_wifi_link_t link = s.status.link;
            publish(set_link, &link);
        }
        break;
    case CMD_GOT_IP: {
        ESP_LOGI(TAG, "connected, address %s", c->ip);
        s.failures = 0;
        if (s.user_join) {
            /* Saved only once it works. */
            s.saved_count = coop_wifi_remember(s.saved, s.saved_count, COOP_WIFI_SAVED, s.current.ssid,
                                               s.current.password);
            store_saved();
            s.user_join = false;
            s.has_previous = false;
            wipe(&s.previous, sizeof(s.previous));
        }
        coop_wifi_link_t link = COOP_WIFI_CONNECTED;
        publish(set_link, &link);
        publish(set_ip, c->ip);
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            publish(set_rssi, &ap.rssi);
        s.rssi_at = now_ms();
        if (s.on_ip)
            s.on_ip(s.ctx);
        break;
    }
    case CMD_DISCONNECTED:
        /* Our own leave while switching networks is not a failure. */
        if (own_leave(c->reason))
            break;
        s.failures++;
        ESP_LOGW(TAG, "join attempt %u failed (reason %u)", s.failures, c->reason);
        if (s.user_join && (auth_failure(c->reason) || c->reason == WIFI_REASON_NO_AP_FOUND ||
                            s.failures >= USER_JOIN_ATTEMPTS)) {
            abandon_join(c->reason);
            break;
        }
        if (!s.user_join && (c->reason == WIFI_REASON_NO_AP_FOUND || s.failures >= ROAM_AFTER_FAILURES)) {
            /* This network is gone: move to another known one in range. */
            s.failures = 0;
            search(c->reason == WIFI_REASON_NO_AP_FOUND ? NULL : s.current.ssid);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_BASE_MS * (s.failures < 6 ? s.failures : 6)));
        if (s.current.ssid[0])
            retry();
        break;
    }
}

static void worker(void *arg)
{
    (void)arg;
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s.queue, &c, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle(&c);
            wipe(&c, sizeof(c));
            continue;
        }
        uint64_t now = now_ms();
        coop_wifi_link_t link = s.status.link;
        if (link == COOP_WIFI_CONNECTING && now - s.join_started > JOIN_TIMEOUT_MS) {
            cmd_t timeout = {.kind = CMD_DISCONNECTED, .reason = WIFI_REASON_CONNECTION_FAIL};
            handle(&timeout);
        } else if ((link == COOP_WIFI_OFF || link == COOP_WIFI_FAILED) && now - s.search_at > SEARCH_EVERY_MS) {
            coop_wifi_cred_t list[COOP_WIFI_SAVED + 1];
            unsigned k = known(list);
            wipe(list, sizeof(list));
            if (k)
                search(NULL);
            else
                s.search_at = now;
        } else if (link == COOP_WIFI_CONNECTED && now - s.rssi_at > RSSI_REFRESH_MS) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
                publish(set_rssi, &ap.rssi);
            s.rssi_at = now;
        }
    }
}

static esp_err_t post(cmd_t *c)
{
    if (!s.started)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = xQueueSend(s.queue, c, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    wipe(c, sizeof(*c));
    return err;
}
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    cmd_t c = {0};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        c.kind = CMD_DISCONNECTED;
        c.reason = ((wifi_event_sta_disconnected_t *)data)->reason;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        c.kind = CMD_GOT_IP;
        snprintf(c.ip, sizeof(c.ip), IPSTR, IP2STR(&((ip_event_got_ip_t *)data)->ip_info.ip));
    } else
        return;
    post(&c);
}

/* ---------- public ---------- */

esp_err_t coop_wifi_start(const char *default_ssid, const char *default_password, coop_wifi_ip_cb_t on_ip,
                          void *ctx)
{
    if (s.started)
        return ESP_OK;
    s.mutex = xSemaphoreCreateMutex();
    s.queue = xQueueCreate(8, sizeof(cmd_t));
    if (!s.mutex || !s.queue)
        return ESP_ERR_NO_MEM;
    s.on_ip = on_ip;
    s.ctx = ctx;
    if (default_ssid && default_ssid[0]) {
        snprintf(s.fallback.ssid, sizeof(s.fallback.ssid), "%s", default_ssid);
        snprintf(s.fallback.password, sizeof(s.fallback.password), "%s", default_password ? default_password : "");
        s.has_fallback = true;
    }
    load_saved();
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE)
            err = ESP_OK;
    }
    if (err == ESP_OK && !esp_netif_create_default_wifi_sta())
        err = ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK)
        err = esp_wifi_init(&cfg);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL, NULL);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL);
    if (err == ESP_OK)
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK)
        err = esp_wifi_start();
    if (err != ESP_OK)
        return err;
    if (xTaskCreate(worker, "coo_wifi", WORKER_STACK, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    s.started = true;
    cmd_t boot = {.kind = CMD_BOOT};
    return post(&boot);
}

void coop_wifi_status(coop_wifi_status_t *out)
{
    if (!out)
        return;
    if (!s.started) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    *out = s.status;
    xSemaphoreGive(s.mutex);
}
esp_err_t coop_wifi_scan(void)
{
    cmd_t c = {.kind = CMD_SCAN};
    return post(&c);
}
esp_err_t coop_wifi_join(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) >= COOP_WIFI_SSID_BYTES ||
        (password && strlen(password) >= COOP_WIFI_PASSWORD_BYTES))
        return ESP_ERR_INVALID_ARG;
    cmd_t c = {.kind = CMD_JOIN, .use_saved = password == NULL};
    snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
    if (password)
        snprintf(c.password, sizeof(c.password), "%s", password);
    return post(&c);
}
esp_err_t coop_wifi_forget(const char *ssid)
{
    if (!ssid || !ssid[0] || strlen(ssid) >= COOP_WIFI_SSID_BYTES)
        return ESP_ERR_INVALID_ARG;
    cmd_t c = {.kind = CMD_FORGET};
    snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
    return post(&c);
}
