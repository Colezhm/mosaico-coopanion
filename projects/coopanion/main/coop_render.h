// SPDX-License-Identifier: MIT
#pragma once
#include "coop_state.h"
#include <stddef.h>
typedef struct coop_render_t *coop_render_handle_t;
esp_err_t coop_render_create(coop_render_handle_t *out);
void coop_render_delete(coop_render_handle_t handle);
bool coop_render_atlas(coop_render_handle_t handle, const uint8_t *bytes, size_t size);
bool coop_atlas_validate(const uint8_t *bytes, size_t size);
void coop_render_draw(coop_render_handle_t handle, const coop_snapshot_t *state, uint16_t *pixels,
                      size_t stride, int x, int y, int width, int height);
