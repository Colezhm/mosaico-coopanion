// SPDX-License-Identifier: MIT
#include "coop_wifi_pick.h"
#include <string.h>

/* Prefer the most recently used network unless another is clearly stronger. */
#define STRONGER_DB 8

static int known_index(const coop_wifi_cred_t *known, unsigned count, const char *ssid)
{
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(known[i].ssid, ssid))
            return (int)i;
    return -1;
}

int coop_wifi_pick(const coop_wifi_cred_t *known, unsigned known_count, const coop_wifi_ap_t *seen,
                   unsigned seen_count)
{
    int best = -1, best_rssi = -128;
    for (unsigned i = 0; i < known_count; i++) {
        int rssi = -128;
        bool present = false;
        for (unsigned j = 0; j < seen_count; j++)
            if (!strcmp(seen[j].ssid, known[i].ssid) && seen[j].rssi > rssi) {
                rssi = seen[j].rssi;
                present = true;
            }
        if (!present)
            continue;
        /* known is most recent first: a later entry must beat the choice clearly. */
        if (best < 0 || rssi > best_rssi + STRONGER_DB) {
            best = (int)i;
            best_rssi = rssi;
        }
    }
    return best;
}

unsigned coop_wifi_tidy(coop_wifi_ap_t *aps, unsigned count, const coop_wifi_cred_t *known,
                        unsigned known_count)
{
    unsigned n = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!aps[i].ssid[0])
            continue;
        int dup = -1;
        for (unsigned j = 0; j < n; j++)
            if (!strcmp(aps[j].ssid, aps[i].ssid))
                dup = (int)j;
        if (dup >= 0) {
            if (aps[i].rssi > aps[dup].rssi)
                aps[dup] = aps[i];
            continue;
        }
        aps[n++] = aps[i];
    }
    for (unsigned i = 0; i < n; i++)
        aps[i].saved = known_index(known, known_count, aps[i].ssid) >= 0;
    /* Insertion sort: saved first, then strongest. */
    for (unsigned i = 1; i < n; i++) {
        coop_wifi_ap_t v = aps[i];
        unsigned j = i;
        while (j > 0 && (v.saved > aps[j - 1].saved || (v.saved == aps[j - 1].saved && v.rssi > aps[j - 1].rssi))) {
            aps[j] = aps[j - 1];
            j--;
        }
        aps[j] = v;
    }
    return n;
}

unsigned coop_wifi_remember(coop_wifi_cred_t *list, unsigned count, unsigned capacity, const char *ssid,
                            const char *password)
{
    coop_wifi_cred_t entry = {0};
    strncpy(entry.ssid, ssid, sizeof(entry.ssid) - 1);
    strncpy(entry.password, password ? password : "", sizeof(entry.password) - 1);
    count = coop_wifi_drop(list, count, ssid);
    if (count >= capacity)
        count = capacity - 1;
    memmove(&list[1], &list[0], count * sizeof(list[0]));
    list[0] = entry;
    memset(&entry, 0, sizeof(entry));
    return count + 1;
}

unsigned coop_wifi_drop(coop_wifi_cred_t *list, unsigned count, const char *ssid)
{
    int at = known_index(list, count, ssid);
    if (at < 0)
        return count;
    memset(&list[at], 0, sizeof(list[at]));
    memmove(&list[at], &list[at + 1], (count - at - 1) * sizeof(list[0]));
    memset(&list[count - 1], 0, sizeof(list[0]));
    return count - 1;
}

const char *coop_wifi_strength(int8_t rssi)
{
    return rssi >= -60 ? "信号强" : rssi >= -72 ? "信号中" : "信号弱";
}
