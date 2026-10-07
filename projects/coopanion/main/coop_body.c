// SPDX-License-Identifier: MIT
#include "coop_body.h"
#include <math.h>
#include <string.h>

/* Secondary motion: a damped spring holding the body over its feet. A 1 g push
 * moves it about 10 px; it swings back at 3.2 Hz and settles within a second. */
#define SPRING_HZ 3.2f
#define SPRING_DAMPING .32f
#define INERTIA_PX_PER_G 4200.f
#define NORMAL_GAIN .6f /* pushes along the screen normal squash instead of swaying */
#define MAX_DX 24.f
#define MAX_DY_DOWN 14.f
#define MAX_DY_UP 18.f
#define MAX_STEP .01f

/* Free fall: total acceleration well below 1 g long enough to rule out a jolt. */
#define FREEFALL_G .35f
#define FREEFALL_MS 70
#define LAND_G 1.6f
#define AIR_HEIGHT 36.f
#define AIR_RISE_PER_S 160.f

/* A knock: a short in-plane spike out of quiet. Screen touches push along the
 * normal, so a spike dominated by the normal axis is not a knock. */
#define TAP_G .55f
#define TAP_QUIET_G .15f
#define TAP_QUIET_MS 150
#define TAP_MAX_MS 70
#define DOUBLE_TAP_MS 450

/* Shaking: low-passed energy of the linear acceleration. */
#define SHAKE_TAU_S .4f
#define SHAKE_FLOOR_G .18f
#define SHAKE_RANGE_G .9f
#define SHAKE_START .25f
#define SHAKE_START_MS 250
#define SHAKE_STOP .1f
#define SHAKE_STOP_MS 400
#define SHAKE_SWING_G .3f    /* a swing counts once it passes this */
#define SHAKE_REVERSALS 2.f
#define REVERSAL_TAU_S .7f

/* Spinning flat on a table: rotation about the screen normal. */
#define SPIN_DPS 50.f
#define SPIN_DIZZY_DEG 540.f
#define SPIN_QUIET_MS 800

#define FACE_DOWN_Z -.7f
#define FACE_UP_Z -.3f
#define FACE_DOWN_MS 700

/* Held versus resting, from the tremor left after removing the gyro bias. */
#define BIAS_TAU_S 2.f
#define TREMOR_TAU_S .3f
#define TABLE_DPS .35f
#define TABLE_G .006f
#define HAND_MAX_DPS 15.f
#define HAND_MAX_G .08f
#define HELD_MS 8000
#define PUT_DOWN_MS 1000
#define HAND_GAP_MS 500

static float clampf(float x, float a, float b)
{
    return fminf(b, fmaxf(a, x));
}

void coop_body_reset(coop_body_t *b)
{
    memset(b, 0, sizeof(*b));
}

static void spring(coop_body_t *b, float fx, float fy, float dt)
{
    const float w = 2 * 3.14159265f * SPRING_HZ, k = w * w, c = 2 * SPRING_DAMPING * w;
    while (dt > 0) {
        float step = fminf(dt, MAX_STEP);
        dt -= step;
        b->vx += (fx - k * b->dx - c * b->vx) * step;
        b->vy += (fy - k * b->dy - c * b->vy) * step;
        b->dx += b->vx * step;
        b->dy += b->vy * step;
        /* The body is planted on its feet: hitting the limit stops it there. */
        if (fabsf(b->dx) > MAX_DX) {
            b->dx = copysignf(MAX_DX, b->dx);
            b->vx = 0;
        }
        if (b->dy > MAX_DY_DOWN || b->dy < -MAX_DY_UP) {
            b->dy = clampf(b->dy, -MAX_DY_UP, MAX_DY_DOWN);
            b->vy = 0;
        }
    }
}

static void gaze(coop_body_t *b, float dt, uint64_t now)
{
    float tx = b->slope * .8f, ty = b->airborne ? -.7f : .15f;
    if (now < b->look_until) {
        tx = b->look_hold_x;
        ty = b->look_hold_y;
    }
    /* Eyes move fast but not instantly: about 80 ms to cover most of the way. */
    float k = 1 - expf(-dt / .08f);
    b->look_x += (clampf(tx, -1, 1) - b->look_x) * k;
    b->look_y += (clampf(ty, -1, 1) - b->look_y) * k;
}

static void float_air(coop_body_t *b, float dt)
{
    if (b->airborne)
        b->air = fminf(AIR_HEIGHT, b->air + AIR_RISE_PER_S * dt);
    else if (b->air > 0) {
        /* Drop back under cartoon gravity. */
        b->air_v += 2400 * dt;
        b->air = fmaxf(0, b->air - b->air_v * dt);
        if (b->air == 0)
            b->air_v = 0;
    }
}

void coop_body_relax(coop_body_t *b, float dt, uint64_t now)
{
    spring(b, 0, 0, dt);
    float_air(b, dt);
    gaze(b, dt, now);
}

void coop_body_kick(coop_body_t *b, float vx, float vy)
{
    b->vx += vx;
    b->vy += vy;
}

void coop_body_look(coop_body_t *b, float x, float y, uint32_t ms, uint64_t now)
{
    b->look_hold_x = x;
    b->look_hold_y = y;
    b->look_until = now + ms;
}

float coop_body_squash(const coop_body_t *b)
{
    return clampf(b->dy / 40.f, -.3f, .3f);
}

unsigned coop_body_sample(coop_body_t *b, const float a[3], const float g[3], const float gravity[3],
                          float orientation, float dt, uint64_t now)
{
    unsigned events = 0;
    float o = orientation * .017453293f, oc = cosf(o), os = sinf(o);
    /* Screen gravity is (-ax, ay); the same mapping turns the linear part into
     * the pseudo-gravity the body feels while the board accelerates. */
    float sx = -(a[0] - gravity[0]), sy = a[1] - gravity[1], nz = a[2] - gravity[2];
    float lx = sx * oc + sy * os, ly = -sx * os + sy * oc;
    float gx = -gravity[0], gy = gravity[1];
    b->slope = clampf(gx * oc + gy * os, -1, 1);
    float total = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float plane = sqrtf(lx * lx + ly * ly), linear = sqrtf(plane * plane + nz * nz);

    /* Free fall and landing. */
    if (total < FREEFALL_G) {
        if (!b->low_g_since)
            b->low_g_since = now;
        if (!b->airborne && now - b->low_g_since >= FREEFALL_MS) {
            b->airborne = true;
            b->airborne_since = now;
            b->shaking = false;
            b->energy = 0;
            events |= COOP_BODY_TOSS;
        }
    } else {
        b->low_g_since = 0;
        if (b->airborne && total > LAND_G) {
            b->airborne = false;
            b->land_g = total;
            b->air_v = 0;
            events |= COOP_BODY_LAND;
            /* The impact presses the body flat; the spring bounces it back. */
            b->vy += 120 * fminf(total, 4.f);
        } else if (b->airborne && now - b->airborne_since > 1500) {
            b->airborne = false; /* caught gently, or the reading recovered without an impact */
            b->land_g = total;
        }
    }

    if (!b->airborne && !b->low_g_since) {
        spring(b, lx * INERTIA_PX_PER_G, (ly + nz * NORMAL_GAIN) * INERTIA_PX_PER_G, dt);
        /* Shaking energy. */
        float k = 1 - expf(-dt / SHAKE_TAU_S);
        b->energy += (linear * linear - b->energy) * k;
        b->shake = clampf((sqrtf(b->energy) - SHAKE_FLOOR_G) / SHAKE_RANGE_G, 0, 1);
        float swing = fabsf(lx) > fabsf(ly) ? lx : ly;
        if (fabsf(nz) > fabsf(swing))
            swing = nz;
        b->reversals *= expf(-dt / REVERSAL_TAU_S);
        if (fabsf(swing) > SHAKE_SWING_G) {
            int8_t sign = swing > 0 ? 1 : -1;
            if (b->last_sign && sign != b->last_sign)
                b->reversals += 1;
            b->last_sign = sign;
        }
        if (!b->shaking) {
            if (b->shake > SHAKE_START && b->reversals >= SHAKE_REVERSALS) {
                if (!b->shake_since)
                    b->shake_since = now;
                if (now - b->shake_since >= SHAKE_START_MS) {
                    b->shaking = true;
                    b->shake_peak = b->shake;
                    b->calm_since = 0;
                    events |= COOP_BODY_SHAKE;
                }
            } else
                b->shake_since = 0;
        } else {
            b->shake_peak = fmaxf(b->shake_peak, b->shake);
            if (b->shake < SHAKE_STOP) {
                if (!b->calm_since)
                    b->calm_since = now;
                if (now - b->calm_since >= SHAKE_STOP_MS) {
                    b->shaking = false;
                    b->shake_since = 0;
                    events |= COOP_BODY_SHAKE_END;
                }
            } else
                b->calm_since = 0;
        }

        /* Knocks: a brief in-plane spike that starts from quiet. */
        if (!b->in_tap) {
            if (plane > TAP_G && fabsf(nz) < plane * 1.2f && b->quiet_since &&
                now - b->quiet_since >= TAP_QUIET_MS && !b->shaking) {
                b->in_tap = true;
                b->tap_at = now;
                b->tap_peak = plane;
                b->tap_peak_x = lx;
            }
        } else {
            if (plane > b->tap_peak) {
                b->tap_peak = plane;
                b->tap_peak_x = lx;
            }
            if (plane < TAP_QUIET_G * 1.3f) {
                b->in_tap = false;
                if (now - b->tap_at <= TAP_MAX_MS) {
                    /* The board is pushed away from the knock; the knock came from
                     * the side the felt pseudo-gravity points to. */
                    b->tap_x = b->tap_peak_x < 0 ? -1.f : 1.f;
                    events |= COOP_BODY_TAP;
                    if (b->last_tap_at && now - b->last_tap_at <= DOUBLE_TAP_MS) {
                        events |= COOP_BODY_DOUBLE_TAP;
                        b->last_tap_at = 0;
                    } else
                        b->last_tap_at = now;
                }
            } else if (now - b->tap_at > TAP_MAX_MS)
                b->in_tap = false; /* too long for a knock: a push or a shake */
        }
        if (plane < TAP_QUIET_G) {
            if (!b->quiet_since)
                b->quiet_since = now;
        } else if (!b->in_tap)
            b->quiet_since = 0;
    }

    /* Spinning about the screen normal. */
    if (fabsf(g[2]) > SPIN_DPS) {
        b->spin += g[2] * dt;
        b->spin_quiet_since = 0;
        coop_body_look(b, clampf(-g[2] / 220.f, -1, 1), 0, 200, now);
        if (fabsf(b->spin) >= SPIN_DIZZY_DEG) {
            b->spin = 0;
            events |= COOP_BODY_SPUN;
        }
    } else {
        if (!b->spin_quiet_since)
            b->spin_quiet_since = now;
        if (now - b->spin_quiet_since >= SPIN_QUIET_MS)
            b->spin = 0;
    }

    /* Face down on the table. */
    if (!b->face_down && gravity[2] < FACE_DOWN_Z) {
        if (!b->face_since)
            b->face_since = now;
        if (now - b->face_since >= FACE_DOWN_MS) {
            b->face_down = true;
            events |= COOP_BODY_FACE_DOWN;
        }
    } else if (gravity[2] >= FACE_DOWN_Z)
        b->face_since = 0;
    if (b->face_down && gravity[2] > FACE_UP_Z) {
        b->face_down = false;
        events |= COOP_BODY_FACE_UP;
    }

    /* Tremor: deviation of the angular rate from its slow mean, and the
     * linear acceleration's level, both smoothed over about half a second. */
    float kb = 1 - expf(-dt / BIAS_TAU_S), kt = 1 - expf(-dt / TREMOR_TAU_S), dev = 0;
    for (int i = 0; i < 3; i++) {
        b->gyro_bias[i] += (g[i] - b->gyro_bias[i]) * kb;
        dev += (g[i] - b->gyro_bias[i]) * (g[i] - b->gyro_bias[i]);
    }
    b->gyro_var += (dev - b->gyro_var) * kt;
    b->accel_var += (linear * linear - b->accel_var) * kt;
    b->tremor_dps = sqrtf(b->gyro_var);
    b->tremor_g = sqrtf(b->accel_var);
    bool table = b->tremor_dps < TABLE_DPS && b->tremor_g < TABLE_G;
    bool hand = !table && b->tremor_dps < HAND_MAX_DPS && b->tremor_g < HAND_MAX_G && !b->shaking &&
                !b->airborne && !b->face_down;
    if (hand) {
        b->unsteady_since = 0;
        if (!b->hand_since)
            b->hand_since = now;
        if (!b->held && now - b->hand_since >= HELD_MS) {
            b->held = true;
            events |= COOP_BODY_HELD;
        }
    } else if (!table) {
        /* Rough handling ends a hug without a "put down". */
        if (!b->unsteady_since)
            b->unsteady_since = now;
        if (now - b->unsteady_since > HAND_GAP_MS) {
            b->hand_since = 0;
            b->held = false;
        }
    }
    if (table) {
        if (!b->table_since)
            b->table_since = now;
        b->hand_since = 0;
        if (b->held && now - b->table_since >= PUT_DOWN_MS) {
            b->held = false;
            events |= COOP_BODY_PUT_DOWN;
        }
    } else
        b->table_since = 0;

    float_air(b, dt);
    gaze(b, dt, now);
    return events;
}
