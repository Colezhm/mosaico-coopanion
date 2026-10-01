// SPDX-License-Identifier: MIT
#include "coop_render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define ATLAS_W 192
#define ATLAS_H 216
#define ATLAS_PIXELS (ATLAS_W * ATLAS_H)
struct coop_render_t {
    const uint8_t *atlas;
    size_t size;
    int pose;
    uint16_t *rgb;
    uint8_t *alpha, *light;
};
static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16);
}
static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
esp_err_t coop_render_create(coop_render_handle_t *out)
{
    if (!out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    (*out)->rgb = calloc(ATLAS_PIXELS, 2);
    (*out)->alpha = calloc(ATLAS_PIXELS, 1);
    (*out)->light = calloc(480 * 480, 1);
    (*out)->pose = -1;
    if (!(*out)->rgb || !(*out)->alpha || !(*out)->light) {
        coop_render_delete(*out);
        *out = NULL;
        return ESP_ERR_NO_MEM;
    }
    /* Expensive ray geometry is precomputed once, never per animated pixel. */
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 480; x++) {
            float dx = (x - 240) / 200.f, dy = (y - 22) / 340.f,
                  q = fmaxf(0, 1 - dx * dx - dy * dy);
            float ray = .65f + .35f * cosf(atan2f(y - 12, x - 240) * 18);
            (*out)->light[y * 480 + x] = (uint8_t)(90 * q * q * ray);
        }
    return ESP_OK;
}
void coop_render_delete(coop_render_handle_t h)
{
    if (h) {
        free(h->rgb);
        free(h->alpha);
        free(h->light);
        free(h);
    }
}
bool coop_atlas_validate(const uint8_t *p, size_t n)
{
    if (!p || n < 188 || memcmp(p, "COO1", 4) || u16(p + 4) != ATLAS_W || u16(p + 6) != ATLAS_H ||
        u16(p + 8) != 22)
        return false;
    for (int i = 0; i < 22; i++) {
        uint32_t o = u32(p + 12 + i * 8), len = u32(p + 16 + i * 8);
        if (o < 188 || o > n || len > n - o || len % 5)
            return false;
        size_t pixels = 0;
        for (size_t k = o; k < o + len; k += 5) {
            pixels += u16(p + k);
            if (pixels > ATLAS_PIXELS)
                return false;
        }
        if (pixels != ATLAS_PIXELS)
            return false;
    }
    return true;
}
bool coop_render_atlas(coop_render_handle_t h, const uint8_t *p, size_t n)
{
    if (!h || !coop_atlas_validate(p, n))
        return false;
    h->atlas = p;
    h->size = n;
    h->pose = -1;
    return true;
}
static int pose_for(const coop_snapshot_t *s)
{
    if (s->motion == COOP_WALK || s->motion == COOP_RUN)
        return 14 + ((int)(s->phase * 12) % 8);
    if (s->motion == COOP_SLEEP)
        return 9;
    if (s->motion == COOP_SIT)
        return 13;
    if (s->motion == COOP_CRY)
        return 7;
    if (s->motion == COOP_SULK)
        return 6;
    if (s->motion == COOP_FALL || s->motion == COOP_GETUP)
        return 10;
    if (s->motion == COOP_THINK)
        return 12;
    const char *names[] = {"neutral", "happy",  "wink",  "love",  "shy",     "surprised", "angry",
                           "sad",     "sleepy", "sleep", "dizzy", "dragged", "thinking",  "sit"};
    for (int i = 0; i < 14; i++)
        if (!strcmp(s->expression, names[i]))
            return i;
    return 0;
}
static void decode(coop_render_handle_t h, int pose)
{
    if (!h->atlas || h->pose == pose)
        return;
    const uint8_t *p = h->atlas;
    uint32_t o = u32(p + 12 + pose * 8), len = u32(p + 16 + pose * 8);
    size_t j = 0;
    for (size_t i = o; i < o + len; i += 5) {
        unsigned run = u16(p + i);
        uint16_t rgb = u16(p + i + 2);
        uint8_t a = p[i + 4];
        for (unsigned k = 0; k < run; k++, j++) {
            h->rgb[j] = rgb;
            h->alpha[j] = a;
        }
    }
    h->pose = pose;
}
static uint16_t blend(uint16_t a, uint16_t b, unsigned opacity)
{
    unsigned inv = 255 - opacity;
    return (uint16_t)((((((a >> 11) & 31) * inv + ((b >> 11) & 31) * opacity) / 255) << 11) |
                      (((((a >> 5) & 63) * inv + ((b >> 5) & 63) * opacity) / 255) << 5) |
                      (((a & 31) * inv + (b & 31) * opacity) / 255));
}
void coop_render_draw(coop_render_handle_t h, const coop_snapshot_t *s, uint16_t *dst,
                      size_t stride, int x0, int y0, int w, int height)
{
    if (!h || !s || !dst)
        return;
    decode(h, pose_for(s));
    const float angle = s->angle * .017453293f, c = cosf(angle), sn = sinf(angle), scale = 1.18f;
    const float cx = 70 + s->x * 340, cy = 430 + s->y;
    for (int row = 0; row < height; row++) {
        uint16_t *line = (uint16_t *)((uint8_t *)dst + row * stride);
        int y = y0 + row;
        for (int col = 0; col < w; col++) {
            int x = x0 + col;
            uint16_t color = 0;
            if (s->glow > 0 && x >= 0 && x < 480 && y >= 0 && y < 480) {
                unsigned k = (unsigned)(s->glow * h->light[y * 480 + x]);
                color = rgb565(k / 2, k, k * 9 / 10);
            }
            if (s->visible && h->atlas) {
                float dx = x - cx, dy = y - cy;
                float px = (dx * c + dy * sn) / (scale * s->scale_x) + 96,
                      py = (-dx * sn + dy * c) / (scale * s->scale_y) + 182;
                int ix = (int)floorf(px), iy = (int)floorf(py);
                if (ix >= 0 && ix < ATLAS_W && iy >= 0 && iy < ATLAS_H) {
                    size_t at = (size_t)iy * ATLAS_W + ix;
                    bool back =
                        s->motion == COOP_SULK && ix > 75 && ix < 132 && iy > 72 && iy < 129;
                    if (!back)
                        color = blend(color, h->rgb[at], h->alpha[at]);
                    if (s->motion == COOP_CRY && iy > 114 && iy < 153) {
                        int drop = 118 + ((int)(s->phase * 45) % 30);
                        if ((abs(ix - 88) < 2 || abs(ix - 118) < 2) && abs(iy - drop) < 5)
                            color = rgb565(105, 207, 247);
                    }
                }
            }
            if (s->listening && y >= 170 && y < 176 && x >= 190 && x < 290) {
                color = x < 190 + (int)(100 * s->voice_level) ? rgb565(100, 239, 188)
                                                              : rgb565(20, 45, 37);
            }
            line[col] = color;
        }
    }
}
