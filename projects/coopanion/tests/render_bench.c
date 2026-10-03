// SPDX-License-Identifier: MIT
/* Host-only renderer timeline: drives the shared state machine through
 * arrival, idle, walking, jumping, tilting, falling, listening, emotions and
 * departure at the device's 33 ms tick, rendering every frame.
 *
 * usage: render_bench <atlas> [frames.rgb565]
 *   Prints the mean full-frame render time. With an output path, appends each
 *   frame so two renderer builds can be compared pixel by pixel.
 *
 * Every frame it also checks the dirty-rectangle contract used by the device:
 *   - nothing outside coop_render_bounds() differs from the black background;
 *   - repainting only the union of the previous and current bounds into a
 *     persistent framebuffer yields exactly the full redraw.
 */
#include "coop_render.h"
#include "coop_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define W 480
#define STRIDE (W * 2)

static void ignore(void *ctx, const coop_event_t *e)
{
    (void)ctx;
    (void)e;
}
static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    uint8_t *bytes = malloc((size_t)n);
    if (!bytes || fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(bytes);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return bytes;
}
static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

typedef struct {
    coop_render_handle_t render;
    coop_state_handle_t state;
    uint16_t *full, *partial;
    FILE *out;
    double spent, dirty_spent;
    unsigned frames, failures;
    unsigned long long dirty_pixels;
    coop_render_rect_t previous;
} bench_t;

static void frame(bench_t *b, uint64_t now)
{
    coop_state_tick(b->state, now);
    const coop_snapshot_t *s = coop_state_get(b->state);
    double start = seconds();
    coop_render_draw(b->render, s, b->full, STRIDE, 0, 0, W, W);
    b->spent += seconds() - start;
    b->frames++;
    if (b->out && fwrite(b->full, 2, W * W, b->out) != W * W)
        b->failures++;

    coop_render_rect_t now_rect;
    coop_render_bounds(b->render, s, &now_rect);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (b->full[y * W + x] &&
                (x < now_rect.x1 || x >= now_rect.x2 || y < now_rect.y1 || y >= now_rect.y2)) {
                if (b->failures++ < 5)
                    fprintf(stderr, "frame %u: pixel (%d,%d) outside bounds [%d,%d)-[%d,%d)\n",
                            b->frames, x, y, now_rect.x1, now_rect.y1, now_rect.x2, now_rect.y2);
                y = W;
                break;
            }
    coop_render_rect_t dirty = coop_render_rect_union(b->previous, now_rect);
    b->previous = now_rect;
    if (dirty.x2 > dirty.x1 && dirty.y2 > dirty.y1) {
        b->dirty_pixels += (unsigned long long)(dirty.x2 - dirty.x1) * (dirty.y2 - dirty.y1);
        start = seconds();
        coop_render_draw(b->render, s, b->partial + dirty.y1 * W + dirty.x1, STRIDE, dirty.x1,
                         dirty.y1, dirty.x2 - dirty.x1, dirty.y2 - dirty.y1);
        b->dirty_spent += seconds() - start;
    }
    if (memcmp(b->full, b->partial, W * W * 2) && b->failures++ < 5)
        fprintf(stderr, "frame %u: dirty repaint differs from full redraw\n", b->frames);
}
static void run(bench_t *b, uint64_t *now, uint64_t ms)
{
    for (uint64_t end = *now + ms; *now < end; *now += 33)
        frame(b, *now);
}
static void imu(bench_t *b, uint64_t *now, float ax, float ay, float az, float gx, float gy, float gz,
                uint64_t ms)
{
    for (uint64_t start = *now, end = *now + ms; *now < end; *now += 10) {
        coop_state_imu(b->state, ax, ay, az, gx, gy, gz, *now);
        if ((*now - start) % 30 == 0)
            frame(b, *now);
    }
}
static void tilt(bench_t *b, uint64_t *now, float ax, float ay, uint64_t ms)
{
    for (uint64_t end = *now + ms; *now < end; *now += 10) {
        coop_state_imu(b->state, ax, ay, 0, 0, 0, 0, *now);
        if (*now % 33 < 10)
            frame(b, *now);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s <atlas> [frames.rgb565]\n", argv[0]);
        return 2;
    }
    size_t size = 0;
    uint8_t *atlas = load(argv[1], &size);
    bench_t b = {.full = calloc(W * W, 2), .partial = calloc(W * W, 2)};
    const coop_state_config_t cfg = {.event = ignore, .seed = 7};
    if (!atlas || !b.full || !b.partial || coop_render_create(&b.render) != ESP_OK ||
        !coop_render_atlas(b.render, atlas, size) || coop_state_create(&cfg, &b.state) != ESP_OK)
        return 1;
    if (argc == 3 && !(b.out = fopen(argv[2], "wb")))
        return 1;

    uint64_t now = 1000;
    coop_state_tick(b.state, now);
    coop_state_connection(b.state, true);
    coop_state_battery(b.state, 83, false);
    run(&b, &now, 600); /* away: dormant portal */
    coop_state_transfer(b.state, "transfer_prepare", "t1", 1, 0, now);
    coop_state_transfer(b.state, "transfer_arrive", "t1", 1, now + 500, now);
    run(&b, &now, 2000);
    coop_state_presence(b.state, true, 1, false);
    run(&b, &now, 1500);
    coop_state_walk(b.state, .9f, false, now);
    run(&b, &now, 1500);
    coop_state_walk(b.state, .1f, true, now);
    run(&b, &now, 1200);
    coop_state_action(b.state, "jump", now);
    run(&b, &now, 800);
    static const char *const faces[] = {"happy", "flustered", "delighted", "cheeky", "sad", "love"};
    for (unsigned i = 0; i < sizeof(faces) / sizeof(faces[0]); i++) {
        coop_state_emotion(b.state, faces[i]);
        run(&b, &now, 400);
    }
    coop_state_button(b.state, true, now);
    coop_state_voice_level(b.state, .6f);
    run(&b, &now, 600);
    coop_state_button(b.state, false, now);
    run(&b, &now, 400);
    static const char *const actions[] = {"sit", "stand", "cry", "sulk", "fall"};
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
        coop_state_action(b.state, actions[i], now);
        run(&b, &now, 900);
    }
    tilt(&b, &now, 1, 0, 2000);  /* left edge */
    tilt(&b, &now, 0, -1, 2000); /* top edge */
    tilt(&b, &now, -1, 0, 2000); /* right edge */
    tilt(&b, &now, 0, 0, 2000);  /* upright again */
    /* Physical layer: toss and catch, shake, knocks, spin, face down, petting. */
    for (int i = 0; i < 3; i++) {
        imu(&b, &now, 0, 0, 1, 0, 0, 0, 400);
        imu(&b, &now, 0, 0, .05f, 0, 0, 0, 350);
        imu(&b, &now, 0, 0, 2.8f, 0, 0, 0, 20);
        imu(&b, &now, 0, 0, 1, 0, 0, 0, 1200);
    }
    for (uint64_t start = now, end = now + 1500; now < end; now += 10) {
        float swing = 1.2f * sinf((float)(now - start) / 1000.f * 6.2831853f * 5);
        coop_state_imu(b.state, swing, 0, 1, 0, 0, 0, now);
        if ((now - start) % 30 == 0)
            frame(&b, now);
    }
    imu(&b, &now, 0, 0, 1, 0, 0, 0, 2500);
    imu(&b, &now, .9f, 0, 1, 0, 0, 0, 20);
    imu(&b, &now, 0, 0, 1, 0, 0, 0, 200);
    imu(&b, &now, .9f, 0, 1, 0, 0, 0, 20);
    imu(&b, &now, 0, 0, 1, 0, 0, 0, 1500);
    imu(&b, &now, 0, 0, 1, 0, 0, 340, 2200);
    imu(&b, &now, 0, 0, 1, 0, 0, 0, 2500);
    imu(&b, &now, 0, 0, -1, 0, 0, 0, 2500);
    imu(&b, &now, 0, 0, 1, 0, 0, 0, 1500);
    coop_state_touch(b.state, true, now);
    run(&b, &now, 1800);
    /* Board-only: bubbles, then a carry and a fling into the wall. */
    coop_state_action(b.state, "bubbles", now);
    run(&b, &now, 1500);
    coop_state_pointer(b.state, 240, 330, true, COOP_PART_BODY, now);
    for (int i = 0; i < 20; i++) {
        now += 20;
        coop_state_pointer(b.state, 240, 330, true, COOP_PART_BODY, now);
        if (i % 2)
            frame(&b, now);
    }
    for (int i = 1; i <= 12; i++) {
        now += 20;
        coop_state_pointer(b.state, 240 - 6 * i, 330 - 16 * i, true, COOP_PART_NONE, now);
        frame(&b, now);
    }
    for (int i = 1; i <= 4; i++) {
        now += 16;
        coop_state_pointer(b.state, 168 + 50 * i, 140, true, COOP_PART_NONE, now);
    }
    coop_state_pointer(b.state, 368, 140, false, COOP_PART_NONE, now + 8);
    run(&b, &now, 4000);
    coop_state_menu(b.state, true);
    run(&b, &now, 300);
    coop_state_presence(b.state, false, 2, false);
    run(&b, &now, 1200);

    if (b.out)
        fclose(b.out);
    printf("%u frames: full redraw %.3f ms, dirty repaint %.3f ms over %.1f%% of the screen\n",
           b.frames, b.spent * 1000 / b.frames, b.dirty_spent * 1000 / b.frames,
           100.0 * b.dirty_pixels / ((double)b.frames * W * W));
    coop_state_delete(b.state);
    coop_render_delete(b.render);
    free(b.full);
    free(b.partial);
    free(atlas);
    if (b.failures)
        fprintf(stderr, "FAIL: %u dirty-rectangle violations\n", b.failures);
    return b.failures ? 1 : 0;
}
