// SPDX-License-Identifier: MIT
#pragma once
/* The hidden Wi-Fi pages (long-press the status line): the current network,
 * a scan list, forgetting, and a password page with the scene's keyboard.
 * Render-task only; part of coop_ui. */
#include "coop_wifi.h"
#include "esp_gsp.h"
#include <stdbool.h>

typedef struct {
    int page; /* 0 closed, 1 networks, 2 password */
    esp_gsp_list_t list;
    coop_wifi_status_t status;
    uint32_t seen_revision;
    bool dirty, show_password;
    char selected[COOP_WIFI_SSID_BYTES];
    char password[COOP_WIFI_PASSWORD_BYTES];
    char note[96];
    char joining[COOP_WIFI_SSID_BYTES]; /* the join the person asked for */
    uint32_t seen_error;
} coop_wifi_ui_t;

/** Binds the network list; call once after the scene is up. */
void coop_wifi_ui_init(coop_wifi_ui_t *w, esp_gsp_handle_t gsp);
/** Opens the Wi-Fi page and starts a scan. */
void coop_wifi_ui_show(coop_wifi_ui_t *w, esp_gsp_handle_t gsp);
/** Handles the pages' own events; returns true when @p e belonged to them. */
bool coop_wifi_ui_event(coop_wifi_ui_t *w, esp_gsp_handle_t gsp, const esp_gsp_event_t *e);
/** Follows the Wi-Fi status while a page is open; returns true when the
 *  pages changed (opened, closed or new content). */
bool coop_wifi_ui_tick(coop_wifi_ui_t *w, esp_gsp_handle_t gsp);
static inline bool coop_wifi_ui_open(const coop_wifi_ui_t *w)
{
    return w->page != 0;
}
