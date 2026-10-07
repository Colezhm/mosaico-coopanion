// SPDX-License-Identifier: MIT
#pragma once
/* The board's own Wi-Fi: remembers up to COOP_WIFI_SAVED networks, joins the
 * strongest one in range (so a board and a desktop on the same network find
 * each other through mDNS), and lets the person scan and join another network
 * from the hidden Wi-Fi page. The Vibe Mode network and a paired network are a
 * read-only default; they are never copied or written back.
 *
 * Implemented by main/coop_wifi.c on the device and pc/wifi_pc.c in the
 * simulator. Requests are queued and safe from any task; coop_wifi_status()
 * returns a consistent snapshot. Passwords never leave the board or appear in
 * the status. */
#include <stdbool.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
#include "coop_state.h" /* esp_err_t stand-in for host builds */
#endif

#define COOP_WIFI_SSID_BYTES 33
#define COOP_WIFI_PASSWORD_BYTES 65
#define COOP_WIFI_SAVED 5
#define COOP_WIFI_SCAN_MAX 16

typedef enum {
    COOP_WIFI_OFF,
    COOP_WIFI_CONNECTING,
    COOP_WIFI_CONNECTED,
    COOP_WIFI_FAILED, /* the last join did not work; see error */
} coop_wifi_link_t;

typedef struct {
    char ssid[COOP_WIFI_SSID_BYTES];
    int8_t rssi;
    bool secure, saved;
} coop_wifi_ap_t;

typedef struct {
    coop_wifi_link_t link;
    char ssid[COOP_WIFI_SSID_BYTES]; /* connected or being joined */
    char ip[16];
    int8_t rssi;
    bool scanning;
    bool current_saved; /* the connected network can be forgotten */
    uint32_t revision;  /* changes whenever anything below changes */
    unsigned ap_count;
    coop_wifi_ap_t aps[COOP_WIFI_SCAN_MAX];
    char error[64];
    uint32_t error_revision; /* bumps once per failed join; error explains it */
} coop_wifi_status_t;

/** Copies the current state. */
void coop_wifi_status(coop_wifi_status_t *out);
/** Starts a scan; results arrive in the status. */
esp_err_t coop_wifi_scan(void);
/** Joins @p ssid. A NULL password uses the saved one; an open network takes "".
 *  The network is saved once it connects. */
esp_err_t coop_wifi_join(const char *ssid, const char *password);
/** Forgets a saved network; leaving it if connected. */
esp_err_t coop_wifi_forget(const char *ssid);
