// SPDX-License-Identifier: MIT
#include "coop_fx.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f

/* Palette matched to the Coo art: white body, mint eyes, the atlas's own
 * accent red and tear blue. */
#define WHITE 0xffff
#define MINT 0x2eb3   /* #2fd59b */
#define YELLOW 0xfe8b /* #ffd25a */
#define BLUE 0x6e7e   /* #69cff7 */
#define PINK 0xfbd5   /* #ff7aa8 */
#define RED 0xfacb    /* #ff5a5a */

enum { CIRCLE, RING, CAPSULE, ARC, STAR, HEART, DROP };

static float clampf(float x, float a, float b)
{
    return fminf(b, fmaxf(a, x));
}
static float length2(float x, float y)
{
    return sqrtf(x * x + y * y);
}

static coop_fx_shape_t *add(coop_fx_frame_t *f, uint8_t type, uint16_t color, float alpha, float x, float y,
                            float extent)
{
    if (f->count >= COOP_FX_MAX_SHAPES || alpha <= 0)
        return NULL;
    coop_fx_shape_t *s = &f->shapes[f->count++];
    memset(s, 0, sizeof(*s));
    s->type = type;
    s->color = color;
    s->alpha = clampf(alpha, 0, 1);
    s->x = x;
    s->y = y;
    /* One pixel of antialiasing beyond the shape's own extent. */
    s->x0 = x - extent - 1;
    s->x1 = x + extent + 1;
    s->y0 = y - extent - 1;
    s->y1 = y + extent + 1;
    if (f->x0 > f->x1) {
        f->x0 = s->x0, f->y0 = s->y0, f->x1 = s->x1, f->y1 = s->y1;
    } else {
        f->x0 = fminf(f->x0, s->x0), f->y0 = fminf(f->y0, s->y0);
        f->x1 = fmaxf(f->x1, s->x1), f->y1 = fmaxf(f->y1, s->y1);
    }
    return s;
}
static void circle(coop_fx_frame_t *f, uint16_t color, float alpha, float x, float y, float r)
{
    coop_fx_shape_t *s = add(f, CIRCLE, color, alpha, x, y, r);
    if (s)
        s->p[0] = r;
}
static void ring(coop_fx_frame_t *f, uint16_t color, float alpha, float x, float y, float r, float th)
{
    coop_fx_shape_t *s = add(f, RING, color, alpha, x, y, r + th);
    if (s)
        s->p[0] = r, s->p[1] = th;
}
static void capsule(coop_fx_frame_t *f, uint16_t color, float alpha, float x0, float y0, float x1,
                    float y1, float r)
{
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    coop_fx_shape_t *s = add(f, CAPSULE, color, alpha, cx, cy, length2(x1 - x0, y1 - y0) / 2 + r);
    if (s) {
        s->p[0] = x0 - cx, s->p[1] = y0 - cy, s->p[2] = x1 - cx, s->p[3] = y1 - cy, s->p[4] = r;
    }
}
/* Ring segment from angle a0 to a1 (radians, +y down), stroke half-width th. */
static void arc(coop_fx_frame_t *f, uint16_t color, float alpha, float x, float y, float r, float th,
                float a0, float a1)
{
    coop_fx_shape_t *s = add(f, ARC, color, alpha, x, y, r + th);
    if (s)
        s->p[0] = r, s->p[1] = th, s->p[2] = a0, s->p[3] = a1;
}
static void star(coop_fx_frame_t *f, uint16_t color, float alpha, float x, float y, float r, float points,
                 float sharpness, float turn)
{
    coop_fx_shape_t *s = add(f, STAR, color, alpha, x, y, r);
    if (s)
        s->p[0] = r, s->p[1] = points, s->p[2] = sharpness, s->p[3] = turn;
}
static void heart(coop_fx_frame_t *f, uint16_t color, float alpha, float x, float y, float size)
{
    coop_fx_shape_t *s = add(f, HEART, color, alpha, x, y, size * .75f);
    if (s)
        s->p[0] = size;
}
static void drop(coop_fx_frame_t *f, uint16_t color, float alpha, float x, float y, float size)
{
    coop_fx_shape_t *s = add(f, DROP, color, alpha, x, y, size * 1.6f);
    if (s)
        s->p[0] = size;
}

/* Signed distances (negative inside), after Inigo Quilez's 2D primitives. */
static float sd_segment(float px, float py, float ax, float ay, float bx, float by)
{
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay + 1e-6f), 0, 1);
    return length2(pax - bax * h, pay - bay * h);
}
static float sd_star(float px, float py, float r, float n, float m, float turn)
{
    float c = cosf(turn), s = sinf(turn), x = px * c - py * s, y = px * s + py * c;
    float an = PI / n, en = PI / m;
    float acx = cosf(an), acy = sinf(an), ecx = cosf(en), ecy = sinf(en);
    float bn = fmodf(atan2f(x, -y) + 2 * PI, 2 * an) - an;
    float l = length2(x, y);
    x = l * cosf(bn);
    y = l * fabsf(sinf(bn));
    x -= r * acx;
    y -= r * acy;
    float k = clampf(-(x * ecx + y * ecy), 0, r * acy / ecy);
    x += ecx * k;
    y += ecy * k;
    return length2(x, y) * (x < 0 ? -1.f : 1.f);
}
static float sd_heart(float px, float py, float size)
{
    /* Unit heart: tip at (0,0), top lobes near y = 1, y up. */
    float x = fabsf(px) / size, y = -py / size + .5f;
    float d;
    if (y + x > 1)
        d = length2(x - .25f, y - .75f) - .35355339f;
    else {
        float m = .5f * fmaxf(x + y, 0);
        float d1 = (x * x + (y - 1) * (y - 1)), d2 = (x - m) * (x - m) + (y - m) * (y - m);
        d = sqrtf(fminf(d1, d2)) * (x - y < 0 ? -1.f : 1.f);
    }
    return d * size;
}
static float sd_drop(float px, float py, float size)
{
    /* Tear drop: round bottom of radius r1, point h above it. */
    float r1 = size * .55f, r2 = size * .08f, h = size * 1.1f;
    float x = fabsf(px), y = -(py - size * .45f);
    float b = (r1 - r2) / h, a = sqrtf(1 - b * b), k = -b * x + a * y;
    if (k < 0)
        return length2(x, y) - r1;
    if (k > a * h)
        return length2(x, y - h) - r2;
    return x * a + y * b - r1;
}
static float sd_arc(float px, float py, float r, float th, float a0, float a1)
{
    float angle = atan2f(py, px);
    while (angle < a0)
        angle += 2 * PI;
    if (angle <= a1)
        return fabsf(length2(px, py) - r) - th;
    float e0 = length2(px - r * cosf(a0), py - r * sinf(a0)), e1 = length2(px - r * cosf(a1), py - r * sinf(a1));
    return fminf(e0, e1) - th;
}

static uint16_t blend(uint16_t a, uint16_t b, unsigned opacity)
{
    unsigned inv = 255 - opacity;
    return (uint16_t)((((((a >> 11) & 31) * inv + ((b >> 11) & 31) * opacity) / 255) << 11) |
                      (((((a >> 5) & 63) * inv + ((b >> 5) & 63) * opacity) / 255) << 5) |
                      (((a & 31) * inv + (b & 31) * opacity) / 255));
}

uint16_t coop_fx_pixel(const coop_fx_frame_t *f, float x, float y, uint16_t color)
{
    if (!f->count || x < f->x0 || x > f->x1 || y < f->y0 || y > f->y1)
        return color;
    for (unsigned i = 0; i < f->count; i++) {
        const coop_fx_shape_t *s = &f->shapes[i];
        if (x < s->x0 || x > s->x1 || y < s->y0 || y > s->y1)
            continue;
        float px = x - s->x, py = y - s->y, d;
        switch (s->type) {
        case CIRCLE: d = length2(px, py) - s->p[0]; break;
        case RING: d = fabsf(length2(px, py) - s->p[0]) - s->p[1]; break;
        case CAPSULE: d = sd_segment(px, py, s->p[0], s->p[1], s->p[2], s->p[3]) - s->p[4]; break;
        case ARC: d = sd_arc(px, py, s->p[0], s->p[1], s->p[2], s->p[3]); break;
        case STAR: d = sd_star(px, py, s->p[0], s->p[1], s->p[2], s->p[3]); break;
        case HEART: d = sd_heart(px, py, s->p[0]); break;
        default: d = sd_drop(px, py, s->p[0]); break;
        }
        float coverage = clampf(.5f - d, 0, 1) * s->alpha;
        if (coverage > 0)
            color = blend(color, s->color, (unsigned)(coverage * 255));
    }
    return color;
}

/* Fade in over the first 120 ms. */
static float appear(float t)
{
    return clampf(t / .12f, 0, 1);
}
/* A pop: overshoots then settles to 1. */
static float pop(float t)
{
    return 1 + .35f * expf(-t * 9) * sinf(t * 26);
}

void coop_fx_bubbles(coop_fx_frame_t *f, const coop_bubbles_t *bubbles, bool whale)
{
    memset(f, 0, sizeof(*f));
    f->x0 = 1;
    f->x1 = 0;
    const uint16_t film = whale ? 0xaf3f /* #a8e4ff */ : 0x8f3a /* #8ee6d0 */;
    for (unsigned i = 0; i < COOP_BUBBLES; i++) {
        const coop_bubble_t *b = &bubbles->b[i];
        if (b->state == COOP_BUBBLE_FLOAT) {
            float a = clampf(b->age / .2f, 0, 1) * clampf((b->life - b->age) / .4f, 0, 1);
            circle(f, film, .12f * a, b->x, b->y, b->r);
            ring(f, film, .85f * a, b->x, b->y, b->r, 1.1f);
            /* A glint toward the light. */
            arc(f, WHITE, .9f * a, b->x, b->y, b->r * .62f, 1.1f, -PI * .85f, -PI * .55f);
        } else if (b->state == COOP_BUBBLE_POP) {
            float u = clampf(b->pop_t / .25f, 0, 1);
            ring(f, film, .9f * (1 - u), b->x, b->y, b->r * (1 + .9f * u), 1.2f * (1 - u) + .4f);
            for (int k = 0; k < 2; k++) {
                float angle = b->seed + k * PI, d = b->r * (1 + 1.6f * u);
                circle(f, WHITE, .8f * (1 - u), b->x + cosf(angle) * d, b->y + sinf(angle) * d, 1.6f);
            }
        }
    }
}

void coop_fx_build(coop_fx_frame_t *f, unsigned kind, float t, float hx, float hy, float size)
{
    memset(f, 0, sizeof(*f));
    f->x0 = 1;
    f->x1 = 0;
    if (t < 0)
        return;
    const float u = size / 1.5f; /* effects were drawn for Coo's 1.5x display scale */
    const float a = appear(t);
    switch (kind) {
    case COOP_FX_EXCLAIM: {
        float k = pop(t) * u, x = hx + 34 * u, y = hy - 4 * u;
        capsule(f, WHITE, a, x, y - 24 * k, x + 1 * k, y - 10 * k, 3.4f * k);
        circle(f, WHITE, a, x + 1.2f * k, y - 2 * k, 3.4f * k);
        break;
    }
    case COOP_FX_QUESTION: {
        float k = pop(t) * u, x = hx + 34 * u + 2 * sinf(t * 5) * u, y = hy - 12 * u;
        arc(f, MINT, a, x, y - 12 * k, 6.5f * k, 2.6f * k, -PI * 1.15f, PI * .45f);
        capsule(f, MINT, a, x + 1.5f * k, y - 5.8f * k, x, y + 1 * k, 2.6f * k);
        circle(f, MINT, a, x, y + 8 * k, 2.8f * k);
        break;
    }
    case COOP_FX_STARS:
        for (int i = 0; i < 3; i++) {
            float angle = t * 4.2f + i * 2 * PI / 3, depth = sinf(angle);
            float r = (5.5f + 1.5f * depth) * u, alpha = a * (.65f + .35f * depth);
            star(f, i == 1 ? WHITE : YELLOW, alpha, hx + cosf(angle) * 36 * u, hy - 6 * u + depth * 7 * u, r,
                 5, 2.6f, t * 3 + i);
        }
        break;
    case COOP_FX_SWEAT: {
        float cycle = fmodf(t, 1.1f), slide = clampf(cycle / .8f, 0, 1);
        float alpha = a * (1 - clampf((cycle - .8f) / .3f, 0, 1));
        drop(f, BLUE, alpha, hx + 38 * u, hy + (14 + 12 * slide) * u, 7 * u);
        break;
    }
    case COOP_FX_HEARTS:
    case COOP_FX_NOTES:
        for (int i = 0; i < 3; i++) {
            float local = t - i * .45f;
            if (local < 0)
                continue;
            float cycle = fmodf(local, 1.35f), rise = cycle / 1.35f;
            float alpha = a * clampf(cycle / .15f, 0, 1) * (1 - clampf((rise - .7f) / .3f, 0, 1));
            float side = (i % 2 ? 1.f : -1.f), x = hx + side * (18 + 8 * i) * u + 5 * sinf(local * 6) * u,
                  y = hy - (2 + 46 * rise) * u, k = (.75f + .35f * rise) * u;
            if (kind == COOP_FX_HEARTS)
                heart(f, PINK, alpha, x, y, 13 * k);
            else {
                circle(f, MINT, alpha, x, y + 4 * k, 3.6f * k);
                capsule(f, MINT, alpha, x + 3 * k, y + 4 * k, x + 3 * k, y - 8 * k, 1.3f * k);
                capsule(f, MINT, alpha, x + 3 * k, y - 8 * k, x + 8 * k, y - 4 * k, 1.3f * k);
            }
        }
        break;
    case COOP_FX_ZZZ:
        for (int i = 0; i < 3; i++) {
            float local = t - i * .6f;
            if (local < 0)
                continue;
            float cycle = fmodf(local, 1.8f), rise = cycle / 1.8f;
            float alpha = a * clampf(cycle / .2f, 0, 1) * (1 - clampf((rise - .65f) / .35f, 0, 1));
            float k = (4 + 3 * rise) * u, x = hx + (22 + 26 * rise) * u, y = hy - (4 + 40 * rise) * u;
            float w = 1.1f * u;
            capsule(f, WHITE, alpha, x - k, y - k, x + k, y - k, w);
            capsule(f, WHITE, alpha, x + k, y - k, x - k, y + k, w);
            capsule(f, WHITE, alpha, x - k, y + k, x + k, y + k, w);
            if (f->count > 9)
                break;
        }
        break;
    case COOP_FX_ANGER: {
        float k = (1 + .12f * sinf(t * 10)) * u, x = hx + 34 * u, y = hy - 2 * u;
        for (int q = 0; q < 4; q++) {
            float sx = q & 1 ? 1.f : -1.f, sy = q & 2 ? 1.f : -1.f;
            /* Each bracket faces the mark's center. */
            float start = atan2f(-sy, -sx) - PI / 4;
            arc(f, RED, a, x + sx * 7.5f * k, y + sy * 7.5f * k, 5.5f * k, 2.1f * k, start, start + PI / 2);
        }
        break;
    }
    case COOP_FX_SPARKLE:
        for (int i = 0; i < 4; i++) {
            float twinkle = .5f + .5f * sinf(t * 7 + i * 1.7f);
            float angle = i * PI / 2 + .6f, x = hx + cosf(angle) * 42 * u, y = hy + 6 * u + sinf(angle) * 28 * u;
            star(f, i % 2 ? WHITE : YELLOW, a * (.45f + .55f * twinkle), x, y, (6 + 5 * twinkle) * u, 4, 3.4f, 0);
        }
        break;
    default:
        break;
    }
}
