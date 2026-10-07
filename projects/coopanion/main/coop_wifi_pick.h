// SPDX-License-Identifier: MIT
#pragma once
/* Pure network choice for the board's Wi-Fi: which known network to join and
 * how to present a scan. Host-testable. */
#include "coop_wifi.h"

typedef struct {
    char ssid[COOP_WIFI_SSID_BYTES];
    char password[COOP_WIFI_PASSWORD_BYTES];
} coop_wifi_cred_t;

/** Index into @p known (most recent first) of the network to join given what a
 *  scan saw: the strongest known network in range, a stronger signal winning
 *  over recency only by more than 8 dB. -1 when none is in range. */
int coop_wifi_pick(const coop_wifi_cred_t *known, unsigned known_count, const coop_wifi_ap_t *seen,
                   unsigned seen_count);
/** Keeps the strongest entry per SSID, drops hidden ones, marks saved ones and
 *  orders saved first, then by signal. Returns the new count. */
unsigned coop_wifi_tidy(coop_wifi_ap_t *aps, unsigned count, const coop_wifi_cred_t *known,
                        unsigned known_count);
/** Moves @p ssid to the front of @p list with @p password (adding it, dropping
 *  the oldest past @p capacity). Returns the new count. */
unsigned coop_wifi_remember(coop_wifi_cred_t *list, unsigned count, unsigned capacity, const char *ssid,
                            const char *password);
/** Removes @p ssid from @p list; returns the new count. */
unsigned coop_wifi_drop(coop_wifi_cred_t *list, unsigned count, const char *ssid);
/** A short signal description: 强 / 中 / 弱. */
const char *coop_wifi_strength(int8_t rssi);
