// SPDX-License-Identifier: MIT
#include "coop_render.h"
#include "coop_animation.h"
#include "coop_fx.h"
#include <math.h>
#include <stdatomic.h>
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
    /* Inclusive bounds of the decoded pose's non-transparent texels and of the
     * non-zero light map; min > max when empty. Used to skip work and to report
     * the dirty rectangle, never to change a pixel's value. */
    int opaque_x0, opaque_y0, opaque_x1, opaque_y1;
    int light_x0, light_y0, light_x1, light_y1;
    /* Coo's ring-eye poses have their eyes removed from the decoded texels and
     * drawn procedurally instead, centered at (COO_EYE_LX/RX, eye_y). */
    bool eyes;
    float eye_y, eye_wide;
    /* Emote effect of the current frame; render-task only, rebuilt by plan(). */
    coop_fx_frame_t fx;
    /* Written by the task that rotates the panel while rendering is paused. */
    atomic_int screen_rotation;
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
    coop_render_handle_t h = *out;
    h->light_x0 = h->light_y0 = 480;
    h->light_x1 = h->light_y1 = -1;
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 480; x++)
            if (h->light[y * 480 + x]) {
                h->light_x0 = x < h->light_x0 ? x : h->light_x0;
                h->light_x1 = x > h->light_x1 ? x : h->light_x1;
                h->light_y0 = y < h->light_y0 ? y : h->light_y0;
                h->light_y1 = y > h->light_y1 ? y : h->light_y1;
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
void coop_render_set_screen_rotation(coop_render_handle_t h, int degrees)
{
    if (h)
        atomic_store(&h->screen_rotation, degrees);
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
    /* Spiral eyes are drawn on the plain ring-eye face. */
    if (s->eye_mode == 1 && s->motion != COOP_WALK && s->motion != COOP_RUN && s->motion != COOP_SIT)
        return 0;
    /* Steps follow the distance covered: 1.5 cycles/s walking, 2.25 running. */
    if (s->motion == COOP_WALK || s->motion == COOP_RUN)
        return 14 + ((int)(s->gait * (s->motion == COOP_RUN ? 18 : 12)) % 8);
    if (s->motion == COOP_SLEEP)
        return 9;
    if (s->motion == COOP_SIT)
        return 13;
    if (s->motion == COOP_CRY)
        return 7;
    /* His back: the plain ring, face features hidden in figure_pixel; the
     * anger mark comes from the effect layer. */
    if (s->motion == COOP_SULK)
        return 0;
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
static void opaque_bounds(coop_render_handle_t h)
{
    h->opaque_x0 = ATLAS_W;
    h->opaque_y0 = ATLAS_H;
    h->opaque_x1 = h->opaque_y1 = -1;
    for (int y = 0; y < ATLAS_H; y++)
        for (int x = 0; x < ATLAS_W; x++)
            if (h->alpha[y * ATLAS_W + x]) {
                h->opaque_x0 = x < h->opaque_x0 ? x : h->opaque_x0;
                h->opaque_x1 = x > h->opaque_x1 ? x : h->opaque_x1;
                h->opaque_y0 = y < h->opaque_y0 ? y : h->opaque_y0;
                h->opaque_y1 = y > h->opaque_y1 ? y : h->opaque_y1;
            }
}
/* Coo's ring eyes: mint rings around transparent pupils, measured from the
 * COO1 atlas. Neutral, surprised, sit and the eight walk poses use them. */
#define COO_EYE_COLOR 0x2eb3
#define COO_EYE_LX 87.f
#define COO_EYE_RX 117.f
#define COO_EYE_Y 99.f
#define COO_SIT_EYE_Y 117.f
#define COO_EYE_OUTER 12.7f
#define COO_EYE_INNER 6.4f
#define COO_POSE_SURPRISED 5
#define COO_POSE_SIT 13
#define COO_RING_X 96.f
#define COO_RING_Y 106.f
#define COO_FACE_R 38.f
#define COO_POSE_ANGRY 6
#define COO_POSE_DRAGGED 11
#define COO_POSE_WALK0 14
#define COO_POSE_WALK7 21
static void procedural_eyes(coop_render_handle_t h, int pose)
{
    h->eyes = pose == 0 || pose == COO_POSE_SURPRISED || pose == COO_POSE_SIT ||
              (pose >= COO_POSE_WALK0 && pose <= COO_POSE_WALK7);
    if (!h->eyes)
        return;
    h->eye_y = pose == COO_POSE_SIT ? COO_SIT_EYE_Y : COO_EYE_Y;
    h->eye_wide = pose == COO_POSE_SURPRISED ? 1.18f : 1;
    /* Clear the baked eyes; nothing else in that box is mint. */
    for (int y = (int)h->eye_y - 18; y <= (int)h->eye_y + 18; y++)
        for (int x = 66; x <= 138; x++) {
            size_t at = (size_t)y * ATLAS_W + x;
            if (h->rgb[at] == COO_EYE_COLOR)
                h->alpha[at] = 0;
        }
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
        h->pose=pose;
        h->eyes=false;
        opaque_bounds(h);
        return;
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
    procedural_eyes(h, pose);
    opaque_bounds(h);
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
static unsigned alpha_at(coop_render_handle_t h, int x, int y)
{
    return x < 0 || y < 0 || x >= ATLAS_W || y >= ATLAS_H ? 0 : h->alpha[(size_t)y * ATLAS_W + x];
}
/* True when the 2x2 texel block at (x, y) lies inside the atlas and all four
 * texels share one color and opacity. */
static bool uniform_quad(coop_render_handle_t h, int x, int y)
{
    if (x < 0 || y < 0 || x + 1 >= ATLAS_W || y + 1 >= ATLAS_H)
        return false;
    size_t at = (size_t)y * ATLAS_W + x;
    const uint16_t *rgb = h->rgb + at;
    const uint8_t *alpha = h->alpha + at;
    return rgb[0] == rgb[1] && rgb[0] == rgb[ATLAS_W] && rgb[0] == rgb[ATLAS_W + 1] &&
           alpha[0] == alpha[1] && alpha[0] == alpha[ATLAS_W] && alpha[0] == alpha[ATLAS_W + 1];
}
/* Coo's crying pose draws tear drops in these atlas columns and rows. */
#define TEAR_X0 87
#define TEAR_X1 119
#define TEAR_Y0 115
#define TEAR_Y1 152
/* A local-space (pre-orientation) box; empty when x0 > x1. */
typedef struct {
    float x0, y0, x1, y1;
} box_t;
static const box_t EMPTY_BOX = {1, 1, 0, 0};
static bool inside(const box_t *b, float x, float y)
{
    return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1;
}
static void box_add(box_t *b, float x, float y)
{
    if (b->x0 > b->x1) {
        *b = (box_t){x, y, x, y};
        return;
    }
    b->x0 = fminf(b->x0, x);
    b->x1 = fmaxf(b->x1, x);
    b->y0 = fminf(b->y0, y);
    b->y1 = fmaxf(b->y1, y);
}
static box_t box_pad(box_t b, float pad)
{
    return b.x0 > b.x1 ? b : (box_t){b.x0 - pad, b.y0 - pad, b.x1 + pad, b.y1 + pad};
}

/* Per-frame placement shared by drawing and dirty-rectangle bounds. Every
 * value matches what the per-pixel code used to recompute, so pixels are
 * unchanged; the boxes are conservative supersets of each layer's footprint. */
typedef struct {
    bool whale, shadow, figure, away;
    float c, sn, scale, sx, sy, oc, os, cx, cy, lying_shift, glow, portal_cx;
    float lift, shadow_rx;
    int portal_dy;
    box_t shadow_box, light_box, figure_box, fx_box;
    /* Procedural eyes, atlas coordinates. */
    bool eyes;
    uint8_t eye_mode;
    float eye_shift_x, eye_y, eye_outer, eye_inner, pupil_x, pupil_y, eye_open, eye_spin, eye_aa;
    float eye_x0, eye_y0, eye_x1, eye_y1;
} frame_t;

static float eye_distance(const frame_t *f, float px, float py)
{
    float ex = px - ((px < 102 ? COO_EYE_LX : COO_EYE_RX) + f->eye_shift_x), ey = py - f->eye_y;
    if (f->eye_mode == 1) {
        /* Dizzy: a two-turn Archimedean spiral that keeps turning. */
        float r = sqrtf(ex * ex + ey * ey), turn = atan2f(ey, ex) + f->eye_spin;
        const float pitch = COO_EYE_OUTER / (2.2f * 6.2831853f);
        float n = roundf((r / pitch - turn) / 6.2831853f);
        return fmaxf(fabsf(r - pitch * (turn + 6.2831853f * n)) - 1.9f, r - COO_EYE_OUTER);
    }
    if (f->eye_open < .22f) {
        /* Shut: a short lid line. */
        float w = f->eye_outer * .8f, x = fmaxf(fabsf(ex) - w, 0);
        return sqrtf(x * x + ey * ey) - 2.3f;
    }
    ey /= f->eye_open;
    float outer = sqrtf(ex * ex + ey * ey) - f->eye_outer;
    float hx = ex - f->pupil_x, hy = ey - f->pupil_y;
    float hole = sqrtf(hx * hx + hy * hy) - f->eye_inner;
    return fmaxf(outer, -hole) * f->eye_open;
}
static uint16_t figure_pixel(coop_render_handle_t h, const coop_snapshot_t *s, const frame_t *f,
                             uint16_t background, float px, float py, bool whale)
{
    int ix = (int)floorf(px), iy = (int)floorf(py);
    if (ix < -1 || ix >= ATLAS_W || iy < -1 || iy >= ATLAS_H)
        return background;
    /* Coo's sulk pose turns his back: hide every face feature inside the ring
     * (inner radius 39.5 around (96, 106)) and keep the ring whole. */
    if (!whale && s->motion == COOP_SULK) {
        float fx = px - COO_RING_X, fy = py - COO_RING_Y;
        if (fx * fx + fy * fy < COO_FACE_R * COO_FACE_R)
            return background;
    }
    uint16_t color = background;
    /* Four transparent texels blend to exactly the background; most of the
     * sprite's rectangle is transparent, so skip the filter there. */
    if (alpha_at(h, ix, iy) | alpha_at(h, ix + 1, iy) | alpha_at(h, ix, iy + 1) |
        alpha_at(h, ix + 1, iy + 1)) {
        if (!whale && uniform_quad(h, ix, iy)) {
            /* Blending equal colors returns that color, so one blend gives the
             * filtered result inside Coo's flat-colored areas. The whale's
             * shaded art rarely has such blocks; checking would only cost time. */
            size_t at = (size_t)iy * ATLAS_W + ix;
            color = blend(background, h->rgb[at], h->alpha[at]);
        } else {
            /* Bilinear filtering against this pixel's background: smooth edges for
             * the up-scaled sprite without a dark fringe from transparent texels. */
            unsigned fx = (unsigned)((px - ix) * 255), fy = (unsigned)((py - iy) * 255);
            uint16_t top =
                blend(sampled(h, background, ix, iy), sampled(h, background, ix + 1, iy), fx);
            uint16_t bottom = blend(sampled(h, background, ix, iy + 1),
                                    sampled(h, background, ix + 1, iy + 1), fx);
            color = blend(top, bottom, fy);
        }
    }
    if (f->eyes && px >= f->eye_x0 && px <= f->eye_x1 && py >= f->eye_y0 && py <= f->eye_y1) {
        float coverage = .5f - eye_distance(f, px, py) * f->eye_aa;
        if (coverage > 0)
            color = blend(color, COO_EYE_COLOR, coverage >= 1 ? 255 : (unsigned)(coverage * 255));
    }
    if (!whale && s->motion == COOP_CRY && iy > 114 && iy < 153) {
        int drop = 118 + ((int)(s->phase * 45) % 30);
        if ((abs(ix - 88) < 2 || abs(ix - 118) < 2) && abs(iy - drop) < 5)
            color = rgb565(105, 207, 247);
    }
    return color;
}


static void plan(coop_render_handle_t h, const coop_snapshot_t *s, frame_t *f)
{
    f->whale = h->atlas && h->atlas[3] == '2';
    float clock = s->motion == COOP_WALK || s->motion == COOP_RUN ? s->gait : s->phase;
    decode(h, f->whale ? coop_animation_frame(h->atlas, clip_for(s), clock) : pose_for(s));
    /* The spring sway leans the body a little, as if it pivots on its feet. */
    const float angle = (s->angle + s->body_dx * .45f) * .017453293f;
    f->c = cosf(angle);
    f->sn = sinf(angle);
    f->scale = f->whale ? WHALE_SCALE : COO_SCALE;
    /* Squash keeps the volume roughly constant. */
    /* The art faces right; facing -1 mirrors it. */
    f->sx = s->scale_x * (s->facing < 0 ? -1.f : 1.f) * (1 + .5f * s->squash);
    f->sy = s->scale_y * (1 - s->squash);
    /* The panel may already be rotated to the standing edge; draw the rest. */
    float orientation=(s->orientation-(float)atomic_load(&h->screen_rotation))*.017453293f;
    f->oc = cosf(orientation);
    f->os = sinf(orientation);
    /* On side edges, roam below the horizontal caption area. Blend the
     * supporting point during rotation so changing edges never teleports it. */
    float lateral = fabsf(f->os);
    float side_x = (f->os > 0 ? 240 : 130) + s->x * 110;
    f->cx = (70 + s->x * 340) * (1-lateral) + side_x * lateral + s->body_dx;
    float lying = s->motion == COOP_FALL || s->motion == COOP_GETUP ? fabsf(f->sn) : 0;
    f->cy = GROUND_Y + s->y - s->air - lying * LYING_HALF_WIDTH * f->scale;
    /* The shadow follows the body's middle, which moves sideways as it tips over. */
    f->lying_shift = lying ? f->sn * LYING_BODY_MID * f->scale : 0;
    /* Idle portal while away: a slow breath, never a full-intensity arrival. */
    f->away = !s->resident && !s->transferring && !s->visible;
    f->glow = f->away ? AWAY_PORTAL_MIN + AWAY_PORTAL_SWING * (.5f + .5f * sinf(s->phase * 1.6f))
                      : s->glow;
    f->portal_cx = f->away ? 240 : f->cx;
    f->portal_dy = f->away ? AWAY_PORTAL_Y - PORTAL_Y : 0;

    f->shadow = s->visible && s->motion != COOP_DEPART && s->motion != COOP_ARRIVE;
    f->lift = fminf(1, fmaxf(0, -(s->y - s->air) / SHADOW_LIFT_FADE));
    f->shadow_rx = SHADOW_RX * f->scale / COO_SCALE * (1 - .35f * f->lift);
    f->shadow_box = EMPTY_BOX;
    if (f->shadow) {
        float mid = f->cx + f->lying_shift;
        f->shadow_box = box_pad((box_t){mid - f->shadow_rx, GROUND_Y - SHADOW_RY,
                                        mid + f->shadow_rx, GROUND_Y + SHADOW_RY}, 1);
    }

    /* light_x = (int)(lx - portal_cx + 240) truncates toward zero, so pad. */
    f->light_box = EMPTY_BOX;
    if (f->glow > 0 && h->light_x0 <= h->light_x1)
        f->light_box = box_pad((box_t){h->light_x0 + f->portal_cx - 240, h->light_y0 + f->portal_dy,
                                       h->light_x1 + 1 + f->portal_cx - 240,
                                       h->light_y1 + 1 + f->portal_dy}, 1);

    f->figure = s->visible && h->atlas;
    f->figure_box = EMPTY_BOX;
    if (f->figure) {
        float x0 = h->opaque_x0 - 1, y0 = h->opaque_y0 - 1, x1 = h->opaque_x1 + 1,
              y1 = h->opaque_y1 + 1;
        f->eyes = !f->whale && h->eyes;
        if (f->eyes) {
            /* Wider than this and the two rings touch. */
            float wide = fminf(1.22f, h->eye_wide * (s->eye_wide > 0 ? s->eye_wide : 1));
            f->eye_mode = s->eye_mode;
            f->eye_outer = COO_EYE_OUTER * wide;
            f->eye_inner = COO_EYE_INNER * (wide > 1 ? 1.05f : 1);
            /* Gaze is in screen space; mirrored art needs it mirrored back. */
            float mirror = f->sx < 0 ? -1.f : 1.f;
            f->eye_shift_x = s->look_x * 2.5f * mirror;
            f->eye_y = h->eye_y + s->look_y * 2.2f;
            f->pupil_x = s->look_x * 3.4f * mirror;
            f->pupil_y = s->look_y * 2.6f;
            f->eye_open = fmaxf(.05f, 1 - s->blink);
            f->eye_spin = s->phase * 6;
            f->eye_aa = f->scale * fminf(fabsf(f->sx), fabsf(f->sy));
            float reach = f->eye_outer + 4;
            f->eye_x0 = COO_EYE_LX + f->eye_shift_x - reach;
            f->eye_x1 = COO_EYE_RX + f->eye_shift_x + reach;
            f->eye_y0 = f->eye_y - reach;
            f->eye_y1 = f->eye_y + reach;
            x0 = fminf(x0, floorf(f->eye_x0));
            y0 = fminf(y0, floorf(f->eye_y0));
            x1 = fmaxf(x1, ceilf(f->eye_x1));
            y1 = fmaxf(y1, ceilf(f->eye_y1));
        }
        if (!f->whale && s->motion == COOP_CRY) {
            x0 = fminf(x0, TEAR_X0);
            y0 = fminf(y0, TEAR_Y0);
            x1 = fmaxf(x1, TEAR_X1 + 1);
            y1 = fmaxf(y1, TEAR_Y1 + 1);
        }
        if (x0 <= x1 && y0 <= y1) {
            /* Map the atlas rectangle back through the sprite transform. */
            const float atlas_x[2] = {x0, x1}, atlas_y[2] = {y0, y1};
            for (int i = 0; i < 4; i++) {
                float u = (atlas_x[i & 1] - 96) * f->scale * f->sx,
                      v = (atlas_y[i >> 1] - 182) * f->scale * f->sy;
                box_add(&f->figure_box, f->cx + u * f->c - v * f->sn, f->cy + u * f->sn + v * f->c);
            }
            f->figure_box = box_pad(f->figure_box, 1);
        }
    }

    /* Emote effects float around the top of the head, upright in the
     * character frame whatever the body's lean. */
    f->fx_box = EMPTY_BOX;
    h->fx.count = 0;
    /* Some faces already carry their mark in the art: both figures' surprised
     * faces an exclamation, Coo's angry face an anger mark and his flustered
     * face a sweat drop. Never draw a second one. */
    bool baked = (s->fx == COOP_FX_EXCLAIM && !strcmp(s->expression, "surprised")) ||
                 (!f->whale && s->fx == COOP_FX_ANGER && h->pose == COO_POSE_ANGRY) ||
                 (!f->whale && s->fx == COOP_FX_SWEAT && h->pose == COO_POSE_DRAGGED);
    if (f->figure && s->fx && !baked && h->opaque_y0 <= h->opaque_y1) {
        float v = (h->opaque_y0 - 182) * f->scale * f->sy;
        coop_fx_build(&h->fx, s->fx, s->fx_t, f->cx - v * f->sn, f->cy + v * f->c, f->scale);
        if (h->fx.count)
            f->fx_box = (box_t){h->fx.x0, h->fx.y0, h->fx.x1, h->fx.y1};
    }
}

static uint16_t shadow_pixel(const frame_t *f, float dx, float ly)
{
    float dy = ly - GROUND_Y;
    if (dy < -SHADOW_RY || dy > SHADOW_RY)
        return 0;
    float rx = f->shadow_rx;
    float r = (dx * dx) / (rx * rx) + (dy * dy) / (SHADOW_RY * SHADOW_RY);
    if (r >= 1)
        return 0;
    unsigned k = (unsigned)((1 - r) * (1 - r) * (1 - .7f * f->lift) * 255);
    return rgb565(k * 30 / 255, k * 84 / 255, k * 72 / 255);
}

/* Listening level meter, drawn in screen space over the canvas. */
#define METER_X0 190
#define METER_X1 290
#define METER_Y0 170
#define METER_Y1 176

void coop_render_bounds(coop_render_handle_t h, const coop_snapshot_t *s, coop_render_rect_t *out)
{
    *out = (coop_render_rect_t){0};
    if (!h || !s || !out)
        return;
    frame_t f;
    plan(h, s, &f);
    const box_t *boxes[] = {&f.shadow_box, &f.light_box, &f.figure_box, &f.fx_box};
    box_t screen = EMPTY_BOX;
    for (unsigned i = 0; i < sizeof(boxes) / sizeof(boxes[0]); i++) {
        const box_t *b = boxes[i];
        if (b->x0 > b->x1)
            continue;
        /* Inverse of the per-pixel orientation transform. */
        for (int k = 0; k < 4; k++) {
            float lx = (k & 1 ? b->x1 : b->x0) - 240, ly = (k & 2 ? b->y1 : b->y0) - 240;
            box_add(&screen, 240 + lx * f.oc - ly * f.os, 240 + lx * f.os + ly * f.oc);
        }
    }
    if (s->listening) {
        box_add(&screen, METER_X0, METER_Y0);
        box_add(&screen, METER_X1, METER_Y1);
    }
    if (screen.x0 > screen.x1)
        return;
    int x1 = (int)floorf(screen.x0) - 1, y1 = (int)floorf(screen.y0) - 1,
        x2 = (int)ceilf(screen.x1) + 2, y2 = (int)ceilf(screen.y1) + 2;
    *out = (coop_render_rect_t){x1 < 0 ? 0 : x1, y1 < 0 ? 0 : y1, x2 > 480 ? 480 : x2,
                                y2 > 480 ? 480 : y2};
    if (out->x2 <= out->x1 || out->y2 <= out->y1)
        *out = (coop_render_rect_t){0};
}

void coop_render_draw(coop_render_handle_t h, const coop_snapshot_t *s, uint16_t *dst,
                      size_t stride, int x0, int y0, int w, int height)
{
    if (!h || !s || !dst)
        return;
    frame_t f;
    plan(h, s, &f);
    const float oc = f.oc, os = f.os;
    for (int row = 0; row < height; row++) {
        uint16_t *line = (uint16_t *)((uint8_t *)dst + row * stride);
        int y = y0 + row;
        for (int col = 0; col < w; col++) {
            int x = x0 + col;
            float lx=240+(x-240)*oc+(y-240)*os,ly=240-(x-240)*os+(y-240)*oc;
            uint16_t color = 0;
            if (inside(&f.shadow_box, lx, ly))
                color = shadow_pixel(&f, lx - f.cx - f.lying_shift, ly);
            if (inside(&f.light_box, lx, ly)) {
                int light_x=(int)(lx-f.portal_cx+240),light_y=(int)ly-f.portal_dy;
                if (light_x >= 0 && light_x < 480 && light_y >= 0 && light_y < 480) {
                    unsigned k = (unsigned)(f.glow * h->light[light_y * 480 + light_x]);
                    if (k)
                        color = rgb565(k * 3 / 5, k, k * 9 / 10);
                }
            }
            if (inside(&f.figure_box, lx, ly) && (!s->transferring || ly>=26)) {
                float dx = lx - f.cx, dy = ly - f.cy;
                float px = (dx * f.c + dy * f.sn) / (f.scale * f.sx) + 96,
                      py = (-dx * f.sn + dy * f.c) / (f.scale * f.sy) + 182;
                color = figure_pixel(h, s, &f, color, px, py, f.whale);
            }
            if (inside(&f.fx_box, lx, ly))
                color = coop_fx_pixel(&h->fx, lx, ly, color);
            if (s->listening && y >= METER_Y0 && y < METER_Y1 && x >= METER_X0 && x < METER_X1) {
                color = x < METER_X0 + (int)(100 * s->voice_level) ? rgb565(100, 239, 188)
                                                                   : rgb565(20, 45, 37);
            }
            line[col] = color;
        }
    }
}
