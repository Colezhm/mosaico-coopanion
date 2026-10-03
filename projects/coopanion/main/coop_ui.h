// SPDX-License-Identifier: MIT
#pragma once
#include "coop_state.h"
#include "esp_gsp.h"
#include <stddef.h>
typedef struct coop_ui_t *coop_ui_handle_t;
typedef struct {
    uint64_t (*now)(void *ctx);
    void (*poll)(void *ctx, coop_state_handle_t state);
    coop_event_cb_t event;
    void *ctx;
    const uint8_t *atlas;
    size_t atlas_size;
} coop_ui_config_t;
esp_err_t coop_ui_create(esp_gsp_handle_t gsp, const coop_ui_config_t *config,
                         coop_ui_handle_t *out);
void coop_ui_delete(coop_ui_handle_t handle);
coop_state_handle_t coop_ui_state(coop_ui_handle_t handle);
void coop_ui_question(coop_ui_handle_t handle, const char *id, const char *question,
                      const char *const options[], int count, bool confirmation);
/** The panel now shows the UI rotated by @p degrees; any task, applied next frame. */
void coop_ui_set_screen_rotation(coop_ui_handle_t handle, int degrees);
bool coop_ui_atlas(coop_ui_handle_t handle, const uint8_t *bytes, size_t size);
