// SPDX-License-Identifier: MIT
/* Host-only preview: drives the shared state machine and C renderer, then writes
 * one 480x480 RGB565 frame plus the HUD text the device would show. It never
 * replaces the GSP simulator; it lets art and layout changes be reviewed and
 * regression-checked without ESP-IDF or the GSP runtime.
 *
 * usage: preview_render <atlas> <scenario> <frame.rgb565> <hud.txt>
 */
#include "coop_hud.h"
#include "coop_render.h"
#include "coop_state.h"
#include "coop_subtitle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static void run(coop_state_handle_t s, uint64_t *now, uint64_t until)
{
    for (; *now < until; *now += 33)
        coop_state_tick(s, *now);
}
int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <atlas> <scenario> <frame.rgb565> <hud.txt>\n", argv[0]);
        return 2;
    }
    size_t size = 0;
    uint8_t *atlas = load(argv[1], &size);
    coop_render_handle_t render = NULL;
    coop_state_handle_t s = NULL;
    const coop_state_config_t cfg = {.event = ignore, .seed = 7};
    if (!atlas || coop_render_create(&render) != ESP_OK || !coop_render_atlas(render, atlas, size) ||
        coop_state_create(&cfg, &s) != ESP_OK)
        return 1;
    const char *scenario = argv[2];
    uint64_t now = 1000;
    coop_state_tick(s, now);
    coop_state_connection(s, true);
    coop_state_battery(s, 83, false);
    if (strcmp(scenario, "away") && strcmp(scenario, "away-offline")) {
        /* Arrive through the real transfer path so subtitles match the device. */
        coop_state_transfer(s, "transfer_prepare", "t1", 1, 0, now);
        coop_state_transfer(s, "transfer_arrive", "t1", 1, now + 500, now);
        run(s, &now, now + 2000);
        coop_state_presence(s, true, 1, false);
        /* Let a random idle wander finish so each scenario starts from rest. */
        for (int i = 0; i < 400 && coop_state_animation_busy(s); i++)
            run(s, &now, now + 33);
    }
    if (!strncmp(scenario, "edge-", 5)) {
        /* Constant gravity from the first sample anchors the board upright. */
        float ax = !strcmp(scenario, "edge-left") ? 1 : !strcmp(scenario, "edge-right") ? -1 : 0;
        float ay = !strcmp(scenario, "edge-top") ? -1 : 0;
        for (uint64_t t = now; t < now + 2500; t += 10) {
            coop_state_imu(s, ax, ay, 0, 0, 0, 0, t);
            coop_state_tick(s, t);
        }
        now += 2500;
        coop_state_say(s, "我来啦！按住 AI 键和我说话", false, now);
    }
    if (!strcmp(scenario, "idle")) {
        coop_state_say(s, "我来啦！按住 AI 键和我说话", false, now);
    } else if (!strcmp(scenario, "happy")) {
        coop_state_emotion(s, "happy");
        coop_state_say(s, "好啦，原谅你了。", false, now);
    } else if (!strcmp(scenario, "subtitle")) {
        coop_state_say(s,
                       "这是多行字幕的测试。轻轻摇晃只会播放摇晃动画，不会触发语音。倾斜时我会努力"
                       "站稳，摔倒后需要你摸摸头才能恢复心情。",
                       true, now);
    } else if (!strcmp(scenario, "offline")) {
        coop_state_connection(s, false);
    } else if (!strcmp(scenario, "reconnected")) {
        coop_state_connection(s, false);
        run(s, &now, now + 500);
        coop_state_connection(s, true);
    } else if (!strcmp(scenario, "jump")) {
        coop_state_action(s, "jump", now);
        run(s, &now, now + 350);
    } else if (!strcmp(scenario, "walk")) {
        coop_state_walk(s, .8f, false, now);
        run(s, &now, now + 600);
    } else if (!strcmp(scenario, "listen")) {
        coop_state_button(s, true, now);
        coop_state_voice_level(s, .55f);
        run(s, &now, now + 300);
    } else if (!strcmp(scenario, "fall") || !strcmp(scenario, "sulk") || !strcmp(scenario, "sit") ||
               !strcmp(scenario, "cry")) {
        coop_state_action(s, scenario, now);
        run(s, &now, now + 500);
    } else if (!strcmp(scenario, "arrive")) {
        coop_state_presence(s, false, 2, false);
        coop_state_transfer(s, "transfer_prepare", "t2", 3, 0, now);
        coop_state_transfer(s, "transfer_arrive", "t2", 3, now + 100, now);
        run(s, &now, now + 100 + 750);
    } else if (!strcmp(scenario, "away") || !strcmp(scenario, "away-offline")) {
        coop_state_presence(s, false, 2, false);
        if (!strcmp(scenario, "away-offline"))
            coop_state_connection(s, false);
        run(s, &now, now + 1700);
    }
    coop_state_tick(s, now);
    uint16_t *frame = calloc(480 * 480, 2);
    if (!frame)
        return 1;
    const coop_snapshot_t *v = coop_state_get(s);
    coop_render_draw(render, v, frame, 960, 0, 0, 480, 480);
    FILE *out = fopen(argv[3], "wb");
    if (!out || fwrite(frame, 2, 480 * 480, out) != 480 * 480)
        return 1;
    fclose(out);

    unsigned pages = coop_subtitle_pages(v->subtitle);
    char page[COOP_SUBTITLE_PAGE_BYTES], status[COOP_HUD_STATUS_BYTES];
    coop_subtitle_page(v->subtitle, 0, page);
    if (v->transferring)
        page[0] = 0;
    coop_hud_status(v, 0, pages, status);
    FILE *hud = fopen(argv[4], "w");
    if (!hud)
        return 1;
    fprintf(hud, "%d\n%s\n%s\n", v->edge, status, page);
    fclose(hud);
    free(frame);
    coop_state_delete(s);
    coop_render_delete(render);
    free(atlas);
    return 0;
}
