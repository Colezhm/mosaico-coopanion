// SPDX-License-Identifier: MIT
#pragma once
/* Bubbles the figure blows on the board: they rise against real gravity, drift
 * and wobble, and pop when tapped or when they reach the edge of the screen.
 * Pure and host-testable; positions are the character frame in screen pixels. */
#include <stdbool.h>
#include <stdint.h>

#define COOP_BUBBLES 8

typedef enum { COOP_BUBBLE_FREE, COOP_BUBBLE_FLOAT, COOP_BUBBLE_POP } coop_bubble_state_t;

typedef struct {
    float x, y, vx, vy, r, age, life, seed, pop_t;
    uint8_t state;
} coop_bubble_t;

typedef struct {
    coop_bubble_t b[COOP_BUBBLES];
} coop_bubbles_t;

/** Blows up to @p count bubbles from (@p x, @p y) toward @p dir (+1 right, -1 left). */
void coop_bubbles_blow(coop_bubbles_t *bubbles, float x, float y, float dir, unsigned count, uint32_t *seed);
/** Advances by @p dt seconds; (@p gx, @p gy) is the unit direction of gravity. */
void coop_bubbles_step(coop_bubbles_t *bubbles, float dt, float gx, float gy);
/** Pops a floating bubble under (@p x, @p y); returns its index or -1. */
int coop_bubbles_pop_at(coop_bubbles_t *bubbles, float x, float y);
/** Index of the floating bubble nearest (@p x, @p y), or -1 when none float. */
int coop_bubbles_nearest(const coop_bubbles_t *bubbles, float x, float y);
bool coop_bubbles_active(const coop_bubbles_t *bubbles);
