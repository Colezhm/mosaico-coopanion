// SPDX-License-Identifier: MIT
#pragma once
/* Touch gestures on the board's own screen, an interaction the desktop pet
 * cannot offer: poking a body part, stroking the head, tickling, picking the
 * figure up and flinging it. Pure and host-testable; fed from the GSP pointer
 * observer on the render task (single owner).
 *
 * Coordinates are the character frame in screen pixels (+y down), as returned
 * by coop_render_locate(), so the recognizer is unaffected by screen rotation. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COOP_PART_NONE,
    COOP_PART_HEAD,
    COOP_PART_BODY,
    COOP_PART_FEET,
} coop_part_t;

typedef enum {
    COOP_TOUCH_NONE,
    COOP_TOUCH_POKE,       /* short tap on the figure; see part */
    COOP_TOUCH_TAP_EMPTY,  /* short tap beside the figure; see x, y */
    COOP_TOUCH_STROKE,     /* one sweep across the head */
    COOP_TOUCH_TICKLE,     /* rapid back-and-forth scribbling on the figure */
    COOP_TOUCH_GRAB,       /* picked up; see x, y and the grab offset */
    COOP_TOUCH_DRAG,       /* carried to x, y */
    COOP_TOUCH_RELEASE,    /* let go while carrying; see vx, vy */
} coop_touch_kind_t;

typedef struct {
    coop_touch_kind_t kind;
    coop_part_t part;
    float x, y, vx, vy;
} coop_touch_event_t;

typedef struct {
    bool down, on_figure, carrying, stroking;
    coop_part_t part;
    float x0, y0, x, y, last_x, last_y, vx, vy, run;
    int8_t dir;
    uint8_t reversals;
    uint64_t pressed_at, last_at, first_reversal_at;
} coop_touch_t;

void coop_touch_reset(coop_touch_t *t);
/** Feeds one pointer sample; @p part is what lies under the finger now. Returns
 *  the gesture this sample completes, if any (kind COOP_TOUCH_NONE otherwise). */
coop_touch_event_t coop_touch_sample(coop_touch_t *t, float x, float y, bool pressed,
                                     coop_part_t part, uint64_t now);
/** True while the current press belongs to the figure (GSP's own long-press
 *  menu and tap handling must stand aside). */
bool coop_touch_claimed(const coop_touch_t *t);
