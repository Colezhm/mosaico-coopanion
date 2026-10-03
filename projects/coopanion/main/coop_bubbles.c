// SPDX-License-Identifier: MIT
#include "coop_bubbles.h"
#include <math.h>
#include <string.h>

#define SCREEN_PX 480.f
#define MARGIN_PX 4.f
#define BUOYANCY_PX_S2 110.f /* rise against gravity */
#define DRAG_PER_S 1.2f
#define WOBBLE_PX_S 14.f
#define POP_S .25f
#define TOUCH_SLACK_PX 10.f /* fingers are wider than bubbles */

static float frand(uint32_t *seed, float a, float b)
{
    *seed = *seed * 1664525u + 1013904223u;
    return a + (b - a) * ((*seed >> 8) & 0xffff) / 65535.f;
}

void coop_bubbles_blow(coop_bubbles_t *bubbles, float x, float y, float dir, unsigned count, uint32_t *seed)
{
    for (unsigned i = 0; i < COOP_BUBBLES && count; i++) {
        coop_bubble_t *b = &bubbles->b[i];
        if (b->state != COOP_BUBBLE_FREE)
            continue;
        count--;
        memset(b, 0, sizeof(*b));
        b->state = COOP_BUBBLE_FLOAT;
        b->x = x;
        b->y = y;
        b->vx = dir * frand(seed, 60, 160);
        b->vy = -frand(seed, 10, 50);
        b->r = frand(seed, 11, 20);
        b->life = frand(seed, 6, 10);
        b->seed = frand(seed, 0, 6.2831853f);
    }
}

void coop_bubbles_step(coop_bubbles_t *bubbles, float dt, float gx, float gy)
{
    float drag = expf(-dt * DRAG_PER_S);
    for (unsigned i = 0; i < COOP_BUBBLES; i++) {
        coop_bubble_t *b = &bubbles->b[i];
        if (b->state == COOP_BUBBLE_POP) {
            b->pop_t += dt;
            if (b->pop_t >= POP_S)
                b->state = COOP_BUBBLE_FREE;
            continue;
        }
        if (b->state != COOP_BUBBLE_FLOAT)
            continue;
        b->age += dt;
        b->vx = (b->vx - gx * BUOYANCY_PX_S2 * dt) * drag;
        b->vy = (b->vy - gy * BUOYANCY_PX_S2 * dt) * drag;
        /* A sideways wobble, across the direction of rise. */
        float wobble = sinf(b->age * 4 + b->seed) * WOBBLE_PX_S * dt;
        b->x += b->vx * dt + gy * wobble;
        b->y += b->vy * dt - gx * wobble;
        bool outside = b->x < MARGIN_PX + b->r || b->x > SCREEN_PX - MARGIN_PX - b->r ||
                       b->y < MARGIN_PX + b->r || b->y > SCREEN_PX - MARGIN_PX - b->r;
        if (outside || b->age >= b->life) {
            b->state = COOP_BUBBLE_POP;
            b->pop_t = 0;
        }
    }
}

int coop_bubbles_pop_at(coop_bubbles_t *bubbles, float x, float y)
{
    int best = -1;
    float best_d = 0;
    for (unsigned i = 0; i < COOP_BUBBLES; i++) {
        coop_bubble_t *b = &bubbles->b[i];
        if (b->state != COOP_BUBBLE_FLOAT)
            continue;
        float d = hypotf(x - b->x, y - b->y);
        if (d <= b->r + TOUCH_SLACK_PX && (best < 0 || d < best_d)) {
            best = (int)i;
            best_d = d;
        }
    }
    if (best >= 0) {
        bubbles->b[best].state = COOP_BUBBLE_POP;
        bubbles->b[best].pop_t = 0;
    }
    return best;
}

int coop_bubbles_nearest(const coop_bubbles_t *bubbles, float x, float y)
{
    int best = -1;
    float best_d = 0;
    for (unsigned i = 0; i < COOP_BUBBLES; i++) {
        const coop_bubble_t *b = &bubbles->b[i];
        if (b->state != COOP_BUBBLE_FLOAT)
            continue;
        float d = hypotf(x - b->x, y - b->y);
        if (best < 0 || d < best_d) {
            best = (int)i;
            best_d = d;
        }
    }
    return best;
}

bool coop_bubbles_active(const coop_bubbles_t *bubbles)
{
    for (unsigned i = 0; i < COOP_BUBBLES; i++)
        if (bubbles->b[i].state != COOP_BUBBLE_FREE)
            return true;
    return false;
}
