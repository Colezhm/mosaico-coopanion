// SPDX-License-Identifier: MIT
#pragma once
/* Physical body layer: turns raw IMU samples into continuous secondary motion
 * (the body lags, wobbles and squashes as the board moves) and discrete
 * physical events (tossed, landed, knocked, shaken, spun, turned face down).
 *
 * Pure and host-testable. Character-local frame: +x right, +y down, in screen
 * pixels, already rotated by the display orientation. Single owner: the UI
 * task's coop_state instance. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COOP_BODY_TOSS = 1 << 0,       /* board left the hand: free fall began */
    COOP_BODY_LAND = 1 << 1,       /* impact after free fall; see land_g */
    COOP_BODY_TAP = 1 << 2,        /* a knock on the case; see tap_x */
    COOP_BODY_DOUBLE_TAP = 1 << 3, /* second knock soon after the first */
    COOP_BODY_SHAKE = 1 << 4,      /* sustained shaking began */
    COOP_BODY_SHAKE_END = 1 << 5,  /* shaking stopped; see shake_peak */
    COOP_BODY_SPUN = 1 << 6,       /* turned around the screen normal far enough to get dizzy */
    COOP_BODY_FACE_DOWN = 1 << 7,  /* screen turned toward the table */
    COOP_BODY_FACE_UP = 1 << 8,    /* screen visible again */
    COOP_BODY_HELD = 1 << 9,       /* held quietly in someone's hands for a while */
    COOP_BODY_PUT_DOWN = 1 << 10,  /* set down on something still after being held */
} coop_body_event_t;

typedef struct {
    /* Continuous secondary motion, consumed by the renderer. */
    float dx, dy;     /* spring offset of the body, px; +dy is pressed down */
    float vx, vy;     /* spring velocity, px/s */
    float air;        /* height floated while airborne, px (>= 0) */
    float look_x;     /* -1 left .. +1 right */
    float look_y;     /* -1 up .. +1 down */
    float slope;      /* sideways gravity in the character frame, -1 .. 1 */
    float shake;      /* shaking intensity, 0 .. 1 */
    /* Event details. */
    float land_g, shake_peak, tap_x;
    /* Detector state. */
    float energy, spin, air_v, look_hold_x, look_hold_y;
    uint64_t quiet_since, low_g_since, airborne_since, tap_at, last_tap_at, shake_since, calm_since, spin_quiet_since,
        face_since, look_until;
    float tap_peak, tap_peak_x;
    float reversals; /* decaying count of direction reversals: shaking oscillates, turning does not */
    /* Hand tremor: a board in the hands trembles slightly (around 0.5-10 deg/s
     * once the gyro's own bias is removed), one on a table shows only sensor noise. */
    float gyro_bias[3], gyro_var, accel_var, tremor_dps, tremor_g;
    uint64_t hand_since, table_since, unsteady_since;
    bool held;
    int8_t last_sign;
    bool airborne, shaking, face_down, in_tap;
} coop_body_t;

void coop_body_reset(coop_body_t *b);
/** Feeds one sample. @p a is acceleration (g) and @p g angular rate (deg/s), sensor
 *  frame; @p gravity the low-passed acceleration; @p orientation the display's
 *  rotation in degrees. Returns the coop_body_event_t bits raised by this sample. */
unsigned coop_body_sample(coop_body_t *b, const float a[3], const float g[3], const float gravity[3],
                          float orientation, float dt, uint64_t now);
/** Advances the spring and decays transient gaze without a sample (render tick). */
void coop_body_relax(coop_body_t *b, float dt, uint64_t now);
/** Adds a velocity kick to the spring, e.g. a landing squash or an expression bounce. */
void coop_body_kick(coop_body_t *b, float vx, float vy);
/** Holds the gaze toward (@p x, @p y) for @p ms, overriding the slope-driven look. */
void coop_body_look(coop_body_t *b, float x, float y, uint32_t ms, uint64_t now);
/** Squash factor derived from the spring: + flattened, - stretched, |s| <= .3. */
float coop_body_squash(const coop_body_t *b);
