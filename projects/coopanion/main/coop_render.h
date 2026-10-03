// SPDX-License-Identifier: MIT
#pragma once
#include "coop_state.h"
#include <stddef.h>
typedef struct coop_render_t *coop_render_handle_t;
/** Canvas-local pixel rectangle with exclusive x2/y2; empty when x2 <= x1 or y2 <= y1. */
typedef struct {
    int x1, y1, x2, y2;
} coop_render_rect_t;
esp_err_t coop_render_create(coop_render_handle_t *out);
void coop_render_delete(coop_render_handle_t handle);
bool coop_render_atlas(coop_render_handle_t handle, const uint8_t *bytes, size_t size);
bool coop_atlas_validate(const uint8_t *bytes, size_t size);
void coop_render_draw(coop_render_handle_t handle, const coop_snapshot_t *state, uint16_t *pixels,
                      size_t stride, int x, int y, int width, int height);
/** Degrees the panel itself is rotated by (0, 90, 180, 270): the renderer then
 *  draws only the remaining orientation. Safe from any task; read every frame. */
void coop_render_set_screen_rotation(coop_render_handle_t handle, int degrees);
/** Conservative screen rectangle containing every non-background pixel that
 *  coop_render_draw() would produce for @p state. Everything outside is black. */
void coop_render_bounds(coop_render_handle_t handle, const coop_snapshot_t *state,
                        coop_render_rect_t *out);
static inline coop_render_rect_t coop_render_rect_union(coop_render_rect_t a, coop_render_rect_t b)
{
    if (a.x2 <= a.x1 || a.y2 <= a.y1)
        return b;
    if (b.x2 <= b.x1 || b.y2 <= b.y1)
        return a;
    return (coop_render_rect_t){a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1,
                                a.x2 > b.x2 ? a.x2 : b.x2, a.y2 > b.y2 ? a.y2 : b.y2};
}
