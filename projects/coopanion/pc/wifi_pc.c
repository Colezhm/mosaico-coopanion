// SPDX-License-Identifier: MIT
/* Simulated Wi-Fi for the native preview: a few fictional networks, a scan
 * that takes a moment, and joins that succeed with the password "12345678"
 * (or on an open network) and fail otherwise. Lets the Wi-Fi and password
 * pages be exercised in the official simulator without a radio. */
#include "coop_wifi.h"
#include "coop_wifi_pick.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCAN_MS 1200
#define JOIN_MS 1500
#define GOOD_PASSWORD "12345678"

static const coop_wifi_ap_t AIR[] = {
    {"Coo-Home", -48, true, false},
    {"办公室-5G", -61, true, false},
    {"Phone Hotspot", -70, true, false},
    {"咖啡店免费WiFi", -77, false, false},
};

static struct {
    bool ready;
    coop_wifi_status_t status;
    coop_wifi_cred_t saved[COOP_WIFI_SAVED], joining, previous;
    unsigned saved_count;
    uint64_t scan_done_at, join_done_at;
} s;

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static bool saved(const char *ssid)
{
    for (unsigned i = 0; i < s.saved_count; i++)
        if (!strcmp(s.saved[i].ssid, ssid))
            return true;
    return false;
}
static void finish_scan(void)
{
    unsigned n = sizeof(AIR) / sizeof(AIR[0]);
    memcpy(s.status.aps, AIR, sizeof(AIR));
    s.status.ap_count = coop_wifi_tidy(s.status.aps, n, s.saved, s.saved_count);
    s.status.scanning = false;
}
static void connected(const coop_wifi_cred_t *c)
{
    s.status.link = COOP_WIFI_CONNECTED;
    snprintf(s.status.ssid, sizeof(s.status.ssid), "%s", c->ssid);
    snprintf(s.status.ip, sizeof(s.status.ip), "192.0.2.10");
    s.status.rssi = -50;
    s.status.error[0] = 0;
    s.status.current_saved = saved(c->ssid);
}
static void ensure(void)
{
    if (s.ready)
        return;
    s.ready = true;
    snprintf(s.saved[0].ssid, sizeof(s.saved[0].ssid), "Coo-Home");
    snprintf(s.saved[0].password, sizeof(s.saved[0].password), GOOD_PASSWORD);
    s.saved_count = 1;
    connected(&s.saved[0]);
    finish_scan();
}
static void advance(void)
{
    ensure();
    uint64_t now = now_ms();
    bool changed = false;
    if (s.status.scanning && now >= s.scan_done_at) {
        finish_scan();
        changed = true;
    }
    if (s.status.link == COOP_WIFI_CONNECTING && now >= s.join_done_at) {
        bool open = false;
        for (unsigned i = 0; i < sizeof(AIR) / sizeof(AIR[0]); i++)
            if (!strcmp(AIR[i].ssid, s.joining.ssid))
                open = !AIR[i].secure;
        if (open || !strcmp(s.joining.password, GOOD_PASSWORD)) {
            s.saved_count = coop_wifi_remember(s.saved, s.saved_count, COOP_WIFI_SAVED, s.joining.ssid,
                                               s.joining.password);
            connected(&s.joining);
        } else {
            /* Back on the previous network, with the reason kept. */
            connected(&s.previous);
            snprintf(s.status.error, sizeof(s.status.error), "%s：密码不对", s.joining.ssid);
            s.status.error_revision++;
        }
        for (unsigned i = 0; i < s.status.ap_count; i++)
            s.status.aps[i].saved = saved(s.status.aps[i].ssid);
        changed = true;
    }
    if (changed)
        s.status.revision++;
}

void coop_wifi_status(coop_wifi_status_t *out)
{
    advance();
    *out = s.status;
}
esp_err_t coop_wifi_scan(void)
{
    advance();
    s.status.scanning = true;
    s.scan_done_at = now_ms() + SCAN_MS;
    s.status.revision++;
    return ESP_OK;
}
esp_err_t coop_wifi_join(const char *ssid, const char *password)
{
    advance();
    if (!ssid || !ssid[0])
        return ESP_ERR_INVALID_ARG;
    snprintf(s.previous.ssid, sizeof(s.previous.ssid), "%s", s.status.ssid);
    memset(&s.joining, 0, sizeof(s.joining));
    snprintf(s.joining.ssid, sizeof(s.joining.ssid), "%s", ssid);
    if (password)
        snprintf(s.joining.password, sizeof(s.joining.password), "%s", password);
    else
        for (unsigned i = 0; i < s.saved_count; i++)
            if (!strcmp(s.saved[i].ssid, ssid))
                snprintf(s.joining.password, sizeof(s.joining.password), "%s", s.saved[i].password);
    s.status.link = COOP_WIFI_CONNECTING;
    snprintf(s.status.ssid, sizeof(s.status.ssid), "%s", ssid);
    s.status.ip[0] = 0;
    s.join_done_at = now_ms() + JOIN_MS;
    s.status.revision++;
    return ESP_OK;
}
esp_err_t coop_wifi_forget(const char *ssid)
{
    advance();
    s.saved_count = coop_wifi_drop(s.saved, s.saved_count, ssid);
    if (!strcmp(s.status.ssid, ssid)) {
        s.status.link = COOP_WIFI_OFF;
        s.status.ssid[0] = 0;
        s.status.ip[0] = 0;
    }
    s.status.current_saved = saved(s.status.ssid);
    for (unsigned i = 0; i < s.status.ap_count; i++)
        s.status.aps[i].saved = saved(s.status.aps[i].ssid);
    s.status.revision++;
    return ESP_OK;
}
