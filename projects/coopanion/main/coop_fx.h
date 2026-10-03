// SPDX-License-Identifier: MIT
#pragma once
/* Procedural emote effects drawn around the character's head: dizzy stars,
 * sweat, exclamation and question marks, hearts, notes, sleep Zs, anger marks
 * and sparkles. Shared by every character and independent of its atlas, which
 * has no room for more frames.
 *
 * Shapes are signed-distance primitives with one-pixel antialiasing, built once
 * per frame in the character-local frame (screen pixels, +y down). */
#include <stdbool.h>
#include <stdint.h>
#include "coop_bubbles.h"

typedef enum {
    COOP_FX_NONE,
    COOP_FX_EXCLAIM,
    COOP_FX_QUESTION,
    COOP_FX_STARS,
    COOP_FX_SWEAT,
    COOP_FX_HEARTS,
    COOP_FX_NOTES,
    COOP_FX_ZZZ,
    COOP_FX_ANGER,
    COOP_FX_SPARKLE,
    COOP_FX_COUNT
} coop_fx_kind_t;

#define COOP_FX_MAX_SHAPES 28

typedef struct {
    uint8_t type;
    uint16_t color;
    float alpha;
    float x, y, p[5];
    float x0, y0, x1, y1; /* bounding box */
} coop_fx_shape_t;

typedef struct {
    unsigned count;
    coop_fx_shape_t shapes[COOP_FX_MAX_SHAPES];
    float x0, y0, x1, y1; /* union of the shapes' boxes; empty when x0 > x1 */
} coop_fx_frame_t;

/** Builds @p kind at @p t seconds after it started, anchored at the top of the
 *  head (@p head_x, @p head_y) and sized by @p size (the character's display scale). */
void coop_fx_build(coop_fx_frame_t *frame, unsigned kind, float t, float head_x, float head_y,
                   float size);
/** Builds the bubbles' shapes: soap-film mint for Coo, sea blue for the whale. */
void coop_fx_bubbles(coop_fx_frame_t *frame, const coop_bubbles_t *bubbles, bool whale);
/** Composites the effect over @p background at local point (@p x, @p y). */
uint16_t coop_fx_pixel(const coop_fx_frame_t *frame, float x, float y, uint16_t background);
