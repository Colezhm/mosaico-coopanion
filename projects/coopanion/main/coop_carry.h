// SPDX-License-Identifier: MIT
#pragma once
/* Picking the figure up with a finger and flinging it, on the board's screen.
 * While carried, the feet hang below the grab point and the body swings with
 * the finger's motion; released, it flies under real gravity (the board's tilt
 * bends the arc), bounces off the screen's sides and comes to rest on the
 * ground. Pure and host-testable; character frame in screen pixels. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { COOP_CARRY_NONE, COOP_CARRY_HELD, COOP_CARRY_FLYING } coop_carry_mode_t;

typedef enum {
    COOP_CARRY_WALL = 1 << 0,   /* hit a side or the top; see impact */
    COOP_CARRY_BOUNCE = 1 << 1, /* bounced off the ground; see impact */
    COOP_CARRY_LANDED = 1 << 2, /* at rest on the ground; see peak */
} coop_carry_event_t;

typedef struct {
    coop_carry_mode_t mode;
    float feet_x, feet_y; /* feet position: x across, y from the top of the screen */
    float vx, vy, angle, spin;
    float grab_dx, grab_dy; /* finger minus feet at the grab */
    float finger_vx;
    float impact, peak; /* last collision speed and the hardest of this flight, px/s */
    float height;       /* the figure's height, for the ceiling */
    float flight_s;
} coop_carry_t;

/** Starts carrying a figure standing with its feet at (@p feet_x, @p feet_y). */
void coop_carry_grab(coop_carry_t *c, float feet_x, float feet_y, float finger_x, float finger_y, float height);
/** Moves the grab point to the finger. */
void coop_carry_drag(coop_carry_t *c, float finger_x, float finger_y, float finger_vx);
/** Lets go with the finger's velocity (px/s). */
void coop_carry_release(coop_carry_t *c, float vx, float vy);
/** Advances by @p dt; (@p gx, @p gy) is the unit gravity direction. Returns coop_carry_event_t bits. */
unsigned coop_carry_step(coop_carry_t *c, float dt, float gx, float gy);
