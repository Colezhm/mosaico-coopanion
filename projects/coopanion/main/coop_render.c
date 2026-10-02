// SPDX-License-Identifier: MIT
#include "coop_render.h"
#include "coop_animation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define ATLAS_W 192
#define ATLAS_H 216
#define ATLAS_PIXELS (ATLAS_W * ATLAS_H)
/* Display scale per figure. Both keep the head below the three caption rows
 * (y <= 128) at rest; transient jumps may pass under the captions. */
#define COO_SCALE 1.5f
#define WHALE_SCALE 1.75f
#define GROUND_Y 430.f
/* Contact shadow: a dim teal ellipse on the OLED black, so the figure stands
 * on something. It narrows and fades with height above the ground. */
#define SHADOW_RX 50.f
#define SHADOW_RY 8.f
#define SHADOW_LIFT_FADE 140.f
/* While Coo is on the desktop the arrival portal idles at low intensity. */
/* Half the figure's width in atlas pixels. Lying on its side the figure pivots
 * at its feet; lifting by this much keeps the body on the ground, not under it. */
#define LYING_HALF_WIDTH 50.f
#define LYING_BODY_MID 70.f
#define AWAY_PORTAL_MIN .22f
#define AWAY_PORTAL_SWING .12f
/* The light map is authored around y=26 (the top-edge arrival portal). While
 * away, the dormant portal sits where Coo would stand instead. */
#define PORTAL_Y 26
#define AWAY_PORTAL_Y 330
struct coop_render_t {
    const uint8_t *atlas;
    size_t size;
    int pose;
    uint16_t *rgb;
    uint8_t *alpha, *light, *indices, *scratch;
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
    (*out)->indices = calloc(ATLAS_PIXELS,1);
    (*out)->scratch = calloc(ATLAS_PIXELS,1);
    (*out)->pose = -1;
    if (!(*out)->rgb || !(*out)->alpha || !(*out)->light || !(*out)->indices || !(*out)->scratch) {
        coop_render_delete(*out);
        *out = NULL;
        return ESP_ERR_NO_MEM;
    }
    /* A narrow, feathered oval near the edge, with a restrained downward halo.
     * Expensive geometry is precomputed once, never per animated pixel. */
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 480; x++) {
            float dx=(x-240)/88.f,dy=(y-26)/14.f;
            float r=sqrtf(dx*dx+dy*dy),ring=expf(-18*(r-1)*(r-1));
            float halo=expf(-3*dx*dx-(y-26)*(y-26)/1500.f);
            float core=expf(-4*r*r);
            (*out)->light[y * 480 + x] = (uint8_t)fminf(255,155*ring+45*halo+16*core);
        }
    return ESP_OK;
}
void coop_render_delete(coop_render_handle_t h)
{
    if (h) {
        free(h->rgb);
        free(h->alpha);
        free(h->light);
        free(h->indices);
        free(h->scratch);
        free(h);
    }
}
bool coop_atlas_validate(const uint8_t *p, size_t n)
{
    if(p && n>=4 && !memcmp(p,"COO2",4))return coop_animation_validate(p,n);
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
    /* COO1 has 14 faces; richer expressions fall back to the closest one. */
    static const char *const alias[][2] = {
        {"worried", "sad"},     {"furious", "angry"},   {"smug", "happy"},   {"pleading", "love"},
        {"curious", "surprised"}, {"excited", "happy"}, {"crying", "sad"},   {"pout", "angry"},
        {"sulking", "angry"},   {"flustered", "dragged"}, {"delighted", "happy"}, {"cheeky", "wink"}};
    for (unsigned a = 0; a < sizeof(alias) / sizeof(alias[0]); a++)
        if (!strcmp(s->expression, alias[a][0]))
            for (int i = 0; i < 14; i++)
                if (!strcmp(alias[a][1], names[i]))
                    return i;
    return 0;
}
static unsigned clip_for(const coop_snapshot_t *s)
{
    switch(s->motion){
    case COOP_WALK:return 14; case COOP_RUN:return 15;case COOP_SIT:return 13;
    case COOP_SLEEP:return 9;case COOP_JUMP:return 16;case COOP_SWAY:return 17;
    case COOP_STUMBLE:return 18;case COOP_FALL:return 19;case COOP_GETUP:return 20;
    case COOP_CRY:return 21;case COOP_SULK:return 22;case COOP_LISTEN:return 23;
    case COOP_THINK:return 12;case COOP_DEPART:return 25;case COOP_ARRIVE:return 26;
    case COOP_SETTLE:return 27;default:break;
    }
    static const char *const faces[]={"neutral","happy","wink","love","shy","surprised","angry","sad","sleepy","sleep","dizzy","dragged","thinking","sit"};
    static const char *const extra[]={"worried","furious","smug","pleading","curious","excited","crying","pout","flustered","delighted","cheeky"};
    if(!strcmp(s->expression,"sulking"))return 22;
    if(s->motion==COOP_SPEAK&&!strcmp(s->expression,"neutral"))return 24;
    for(unsigned i=0;i<sizeof(extra)/sizeof(extra[0]);i++)if(!strcmp(s->expression,extra[i]))return 32+i;
    for(unsigned i=0;i<14;i++)if(!strcmp(s->expression,faces[i]))return i;
    return 0;
}
static void decode(coop_render_handle_t h, int pose)
{
    if (!h->atlas || h->pose == pose)
        return;
    const uint8_t *p = h->atlas;
    if(p[3]=='2'){
        if(!coop_animation_decode(p,pose,h->pose,h->indices,h->scratch))return;
        const uint8_t *palette=coop_animation_palette(p);
        for(unsigned i=0;i<ATLAS_PIXELS;i++){unsigned at=h->indices[i]*3;h->rgb[i]=u16(palette+at);h->alpha[i]=palette[at+2];}
        h->pose=pose;return;
    }
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
static uint16_t sampled(coop_render_handle_t h, uint16_t background, int x, int y)
{
    if(x<0||y<0||x>=ATLAS_W||y>=ATLAS_H)return background;
    size_t at=(size_t)y*ATLAS_W+x;
    return blend(background,h->rgb[at],h->alpha[at]);
}
static uint16_t figure_pixel(coop_render_handle_t h, const coop_snapshot_t *s, uint16_t background,
                             float px, float py, bool whale)
{
    int ix = (int)floorf(px), iy = (int)floorf(py);
    if (ix < -1 || ix >= ATLAS_W || iy < -1 || iy >= ATLAS_H)
        return background;
    /* Coo's sulk pose hides the face region to read as a turned back. */
    if (!whale && s->motion == COOP_SULK && ix > 75 && ix < 132 && iy > 72 && iy < 129)
        return background;
    /* Bilinear filtering against this pixel's background: smooth edges for the
     * up-scaled sprite without a dark fringe from transparent texels. */
    unsigned fx = (unsigned)((px - ix) * 255), fy = (unsigned)((py - iy) * 255);
    uint16_t top = blend(sampled(h, background, ix, iy), sampled(h, background, ix + 1, iy), fx);
    uint16_t bottom =
        blend(sampled(h, background, ix, iy + 1), sampled(h, background, ix + 1, iy + 1), fx);
    uint16_t color = blend(top, bottom, fy);
    if (!whale && s->motion == COOP_CRY && iy > 114 && iy < 153) {
        int drop = 118 + ((int)(s->phase * 45) % 30);
        if ((abs(ix - 88) < 2 || abs(ix - 118) < 2) && abs(iy - drop) < 5)
            color = rgb565(105, 207, 247);
    }
    return color;
}
static uint16_t shadow_pixel(const coop_snapshot_t *s, float dx, float ly, float scale)
{
    if (!s->visible || s->motion == COOP_DEPART || s->motion == COOP_ARRIVE)
        return 0;
    float dy = ly - GROUND_Y;
    if (dy < -SHADOW_RY || dy > SHADOW_RY)
        return 0;
    float lift = fminf(1, fmaxf(0, -s->y / SHADOW_LIFT_FADE));
    float rx = SHADOW_RX * scale / COO_SCALE * (1 - .35f * lift);
    float r = (dx * dx) / (rx * rx) + (dy * dy) / (SHADOW_RY * SHADOW_RY);
    if (r >= 1)
        return 0;
    unsigned k = (unsigned)((1 - r) * (1 - r) * (1 - .7f * lift) * 255);
    return rgb565(k * 30 / 255, k * 84 / 255, k * 72 / 255);
}
void coop_render_draw(coop_render_handle_t h, const coop_snapshot_t *s, uint16_t *dst,
                      size_t stride, int x0, int y0, int w, int height)
{
    if (!h || !s || !dst)
        return;
    bool whale=h->atlas && h->atlas[3]=='2';
    decode(h,whale?coop_animation_frame(h->atlas,clip_for(s),s->phase):pose_for(s));
    const float angle = s->angle * .017453293f, c = cosf(angle), sn = sinf(angle),
                scale = whale ? WHALE_SCALE : COO_SCALE;
    float orientation=s->orientation*.017453293f,oc=cosf(orientation),os=sinf(orientation);
    /* On side edges, roam below the horizontal caption area. Blend the
     * supporting point during rotation so changing edges never teleports it. */
    float lateral = fabsf(os);
    float side_x = (os > 0 ? 240 : 130) + s->x * 110;
    const float cx = (70 + s->x * 340) * (1-lateral) + side_x * lateral;
    float lying = s->motion == COOP_FALL || s->motion == COOP_GETUP ? fabsf(sn) : 0;
    const float cy = GROUND_Y + s->y - lying * LYING_HALF_WIDTH * scale;
    /* The shadow follows the body's middle, which moves sideways as it tips over. */
    const float lying_shift = lying ? sn * LYING_BODY_MID * scale : 0;
    /* Idle portal while away: a slow breath, never a full-intensity arrival. */
    float glow = s->glow;
    if (!s->resident && !s->transferring && !s->visible)
        glow = AWAY_PORTAL_MIN + AWAY_PORTAL_SWING * (.5f + .5f * sinf(s->phase * 1.6f));
    bool away = !s->resident && !s->transferring && !s->visible;
    const float portal_cx = away ? 240 : cx;
    const int portal_dy = away ? AWAY_PORTAL_Y - PORTAL_Y : 0;
    for (int row = 0; row < height; row++) {
        uint16_t *line = (uint16_t *)((uint8_t *)dst + row * stride);
        int y = y0 + row;
        for (int col = 0; col < w; col++) {
            int x = x0 + col;
            float lx=240+(x-240)*oc+(y-240)*os,ly=240-(x-240)*os+(y-240)*oc;
            uint16_t color = shadow_pixel(s, lx - cx - lying_shift, ly, scale);
            int light_x=(int)(lx-portal_cx+240),light_y=(int)ly-portal_dy;
            if (glow > 0 && light_x >= 0 && light_x < 480 && light_y >= 0 && light_y < 480) {
                unsigned k = (unsigned)(glow * h->light[light_y * 480 + light_x]);
                if (k)
                    color = rgb565(k * 3 / 5, k, k * 9 / 10);
            }
            if (s->visible && h->atlas && (!s->transferring || ly>=26)) {
                float dx = lx - cx, dy = ly - cy;
                float px = (dx * c + dy * sn) / (scale * s->scale_x) + 96,
                      py = (-dx * sn + dy * c) / (scale * s->scale_y) + 182;
                color = figure_pixel(h, s, color, px, py, whale);
            }
            if (s->listening && y >= 170 && y < 176 && x >= 190 && x < 290) {
                color = x < 190 + (int)(100 * s->voice_level) ? rgb565(100, 239, 188)
                                                              : rgb565(20, 45, 37);
            }
            line[col] = color;
        }
    }
}
