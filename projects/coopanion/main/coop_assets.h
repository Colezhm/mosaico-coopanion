// SPDX-License-Identifier: MIT
#pragma once
#include "coop_link.h"
typedef struct coop_assets_t *coop_assets_handle_t;
typedef struct {
    /* Takes ownership of bytes. Called on the resource worker; UI must apply it
     * before sending asset_ready back to the host. */
    void (*complete)(void *ctx, uint8_t *bytes, size_t size, const char *id, const char *hash);
    void *ctx;
    coop_link_handle_t link;
} coop_assets_config_t;
esp_err_t coop_assets_create(const coop_assets_config_t *config, coop_assets_handle_t *out);
void coop_assets_message(coop_assets_handle_t handle, cJSON *message);
