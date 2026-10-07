// SPDX-License-Identifier: MIT
#pragma once
/* Device-only start of the board's Wi-Fi (see coop_wifi.h). */
#include "coop_wifi.h"

typedef void (*coop_wifi_ip_cb_t)(void *ctx);

/** Brings Wi-Fi up and joins the best known network. @p default_ssid (the
 *  paired or Vibe Mode network) is a read-only fallback and may be NULL.
 *  @p on_ip runs on the Wi-Fi task whenever an address is obtained. */
esp_err_t coop_wifi_start(const char *default_ssid, const char *default_password, coop_wifi_ip_cb_t on_ip,
                          void *ctx);
