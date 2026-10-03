// SPDX-License-Identifier: MIT
#include "coop_touch.h"
#include <math.h>
#include <string.h>

/* A tap stays within this radius and ends this soon. */
#define TAP_RADIUS_PX 12.f
#define TAP_MS 400
/* Holding still this long, or moving this far, picks the figure up. */
#define GRAB_HOLD_MS 350
#define GRAB_MOVE_PX 14.f
/* A stroke is a mostly horizontal sweep of this length across the head. */
#define STROKE_START_PX 10.f
#define STROKE_PX 40.f
#define SWEEP_JITTER_PX 2.f
/* Tickling: this many direction reversals inside the window. */
#define TICKLE_REVERSALS 4
#define TICKLE_WINDOW_MS 1200

void coop_touch_reset(coop_touch_t *t)
{
    memset(t, 0, sizeof(*t));
}

bool coop_touch_claimed(const coop_touch_t *t)
{
    return t->down && t->on_figure;
}

static coop_touch_event_t event(coop_touch_kind_t kind, coop_part_t part, float x, float y, float vx, float vy)
{
    return (coop_touch_event_t){kind, part, x, y, vx, vy};
}

coop_touch_event_t coop_touch_sample(coop_touch_t *t, float x, float y, bool pressed, coop_part_t part,
                                     uint64_t now)
{
    coop_touch_event_t none = {COOP_TOUCH_NONE, COOP_PART_NONE, 0, 0, 0, 0};
    if (pressed && !t->down) {
        coop_touch_reset(t);
        t->down = true;
        t->on_figure = part != COOP_PART_NONE;
        t->part = part;
        t->x0 = t->x = t->last_x = x;
        t->y0 = t->y = t->last_y = y;
        t->pressed_at = t->last_at = now;
        return none;
    }
    if (!t->down)
        return none;

    if (!pressed) {
        t->down = false;
        float moved = hypotf(t->x - t->x0, t->y - t->y0);
        bool quick = now - t->pressed_at <= TAP_MS && moved < TAP_RADIUS_PX;
        if (t->carrying)
            return event(COOP_TOUCH_RELEASE, t->part, t->x, t->y, t->vx, t->vy);
        if (quick && t->on_figure && !t->stroking)
            return event(COOP_TOUCH_POKE, t->part, t->x0, t->y0, 0, 0);
        if (quick && !t->on_figure)
            return event(COOP_TOUCH_TAP_EMPTY, COOP_PART_NONE, t->x0, t->y0, 0, 0);
        return none;
    }

    /* Moving (or holding): update the smoothed velocity in px/s. */
    float dt = (now - t->last_at) / 1000.f;
    float dx = x - t->last_x, dy = y - t->last_y;
    if (dt > 0) {
        t->vx += (dx / dt - t->vx) * .5f;
        t->vy += (dy / dt - t->vy) * .5f;
    }
    t->x = x;
    t->y = y;
    t->last_x = x;
    t->last_y = y;
    t->last_at = now;
    if (!t->on_figure)
        return none;
    if (t->carrying)
        return event(COOP_TOUCH_DRAG, t->part, x, y, t->vx, t->vy);

    float mx = x - t->x0, my = y - t->y0, moved = hypotf(mx, my);
    if (!t->stroking && fabsf(mx) > STROKE_START_PX && fabsf(mx) > 1.5f * fabsf(my))
        t->stroking = true;
    if (t->stroking) {
        if (fabsf(dx) < SWEEP_JITTER_PX)
            return none;
        int8_t dir = dx > 0 ? 1 : -1;
        if (t->dir && dir != t->dir) {
            if (!t->reversals || now - t->first_reversal_at > TICKLE_WINDOW_MS) {
                t->reversals = 0;
                t->first_reversal_at = now;
            }
            t->run = 0;
            if (++t->reversals >= TICKLE_REVERSALS) {
                t->reversals = 0;
                t->dir = dir;
                return event(COOP_TOUCH_TICKLE, t->part, x, y, 0, 0);
            }
        }
        t->dir = dir;
        float before = t->run;
        t->run += fabsf(dx);
        /* One stroke per sweep, counted when it is long enough. */
        if (t->part == COOP_PART_HEAD && before < STROKE_PX && t->run >= STROKE_PX)
            return event(COOP_TOUCH_STROKE, t->part, x, y, (float)dir, 0);
        return none;
    }
    if (moved > GRAB_MOVE_PX || (now - t->pressed_at >= GRAB_HOLD_MS && moved < TAP_RADIUS_PX)) {
        t->carrying = true;
        return event(COOP_TOUCH_GRAB, t->part, x, y, 0, 0);
    }
    return none;
}
