// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
typedef struct coop_link_t *coop_link_handle_t;
typedef struct {
    /* Called on the transport task. Callback takes ownership of JSON. */
    void (*message)(void *ctx, cJSON *message);
    void (*connection)(void *ctx, bool connected);
    void *ctx;
} coop_link_config_t;
esp_err_t coop_link_create(const coop_link_config_t *config, coop_link_handle_t *out);
esp_err_t coop_link_start(coop_link_handle_t handle);
void coop_link_delete(coop_link_handle_t handle);
bool coop_link_send(coop_link_handle_t handle, cJSON *message);
bool coop_link_audio(coop_link_handle_t handle, const void *bytes, size_t size);
esp_err_t coop_link_provision(coop_link_handle_t handle, const uint8_t *json, size_t size);
