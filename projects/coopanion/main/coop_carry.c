// SPDX-License-Identifier: MIT
#include "coop_carry.h"
#include <math.h>
#include <string.h>

#define GROUND_Y 430.f
#define SIDE_MIN 50.f
#define SIDE_MAX 430.f
#define TOP_MARGIN 8.f
#define GRAVITY_PX_S2 2200.f
#define MAX_THROW_PX_S 2500.f
#define WALL_RESTITUTION .55f
#define GROUND_RESTITUTION .32f
#define REST_PX_S 420.f      /* slower than this, a landing stays down */
#define SWING_PER_PX_S .03f  /* degrees of swing per px/s of finger motion */
#define SWING_MAX 35.f
#define SPIN_PER_PX_S .12f   /* tumbling in flight */
#define MAX_FLIGHT_S 6.f

static float clampf(float x, float a, float b)
{
    return fminf(b, fmaxf(a, x));
}

void coop_carry_grab(coop_carry_t *c, float feet_x, float feet_y, float finger_x, float finger_y, float height)
{
    memset(c, 0, sizeof(*c));
    c->mode = COOP_CARRY_HELD;
    c->feet_x = feet_x;
    c->feet_y = feet_y;
    c->grab_dx = finger_x - feet_x;
    c->grab_dy = finger_y - feet_y;
    c->height = height;
}

void coop_carry_drag(coop_carry_t *c, float finger_x, float finger_y, float finger_vx)
{
    if (c->mode != COOP_CARRY_HELD)
        return;
    c->feet_x = clampf(finger_x - c->grab_dx, SIDE_MIN, SIDE_MAX);
    c->feet_y = fminf(GROUND_Y, finger_y - c->grab_dy);
    c->finger_vx = finger_vx;
}

void coop_carry_release(coop_carry_t *c, float vx, float vy)
{
    if (c->mode != COOP_CARRY_HELD)
        return;
    float speed = hypotf(vx, vy);
    if (speed > MAX_THROW_PX_S) {
        vx *= MAX_THROW_PX_S / speed;
        vy *= MAX_THROW_PX_S / speed;
    }
    c->mode = COOP_CARRY_FLYING;
    c->vx = vx;
    c->vy = vy;
    c->peak = 0;
    c->flight_s = 0;
}

unsigned coop_carry_step(coop_carry_t *c, float dt, float gx, float gy)
{
    unsigned events = 0;
    if (c->mode == COOP_CARRY_HELD) {
        /* Hanging from the finger: swing against its motion, settle when still. */
        float target = clampf(-c->finger_vx * SWING_PER_PX_S + gx * 25, -SWING_MAX, SWING_MAX);
        c->angle += (target - c->angle) * (1 - expf(-dt * 8));
        c->finger_vx *= expf(-dt * 6);
        return 0;
    }
    if (c->mode != COOP_CARRY_FLYING)
        return 0;
    c->flight_s += dt;
    c->vx += gx * GRAVITY_PX_S2 * dt;
    c->vy += gy * GRAVITY_PX_S2 * dt;
    c->feet_x += c->vx * dt;
    c->feet_y += c->vy * dt;
    c->spin += c->vx * SPIN_PER_PX_S * dt;
    c->angle = c->spin;
    if (c->feet_x < SIDE_MIN || c->feet_x > SIDE_MAX) {
        c->impact = fabsf(c->vx);
        c->feet_x = clampf(c->feet_x, SIDE_MIN, SIDE_MAX);
        c->vx = -c->vx * WALL_RESTITUTION;
        events |= COOP_CARRY_WALL;
    }
    if (c->feet_y - c->height < TOP_MARGIN && c->vy < 0) {
        c->impact = fabsf(c->vy);
        c->feet_y = TOP_MARGIN + c->height;
        c->vy = -c->vy * WALL_RESTITUTION;
        events |= COOP_CARRY_WALL;
    }
    if (c->feet_y >= GROUND_Y && c->vy > 0) {
        c->impact = c->vy;
        c->feet_y = GROUND_Y;
        if (c->vy > REST_PX_S && c->flight_s < MAX_FLIGHT_S) {
            c->vy = -c->vy * GROUND_RESTITUTION;
            c->vx *= .7f;
            events |= COOP_CARRY_BOUNCE;
        } else {
            c->mode = COOP_CARRY_NONE;
            c->vx = c->vy = 0;
            c->angle = c->spin = 0;
            events |= COOP_CARRY_LANDED;
        }
    }
    if (events & (COOP_CARRY_WALL | COOP_CARRY_BOUNCE | COOP_CARRY_LANDED))
        c->peak = fmaxf(c->peak, c->impact);
    return events;
}
