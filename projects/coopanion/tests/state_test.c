// SPDX-License-Identifier: MIT
#include "coop_state.h"
#include "coop_render.h"
#include "coop_script.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned hidden, arrived, voice_start, voice_end, voice_cancel, summon, say;
    uint64_t hidden_at, arrived_at, glow_at;
} events_t;
static void event(void *ctx, const coop_event_t *e)
{
    events_t *events = ctx;
    if (!strcmp(e->type, "transfer_hidden")) {
        events->hidden++;
        events->hidden_at = e->at;
    }
    if (!strcmp(e->type, "transfer_arrived")) {
        events->arrived++;
        events->arrived_at = e->at;
    }
    if (!strcmp(e->type, "glow_started"))
        events->glow_at = e->at;
    if (!strcmp(e->type, "voice_start"))
        events->voice_start++;
    if (!strcmp(e->type, "voice_end"))
        events->voice_end++;
    if (!strcmp(e->type, "voice_cancel"))
        events->voice_cancel++;
    if (!strcmp(e->type, "summon"))
        events->summon++;
    if (!strcmp(e->type, "say"))
        events->say++;
}
static coop_state_handle_t create(events_t *e)
{
    coop_state_handle_t h = NULL;
    const coop_state_config_t cfg = {.event = event, .ctx = e, .seed = 4};
    assert(coop_state_create(&cfg, &h) == ESP_OK);
    return h;
}
static void transfer_test(void)
{
    events_t e = {0};
    coop_state_handle_t h = create(&e);
    coop_state_presence(h, true, 1, false);
    assert(coop_state_transfer(h, "transfer_depart", "out", 2, 0, 1000));
    coop_state_tick(h, 1949);
    assert(coop_state_get(h)->visible && e.hidden == 0);
    coop_state_tick(h, 1950);
    assert(!coop_state_get(h)->visible && e.hidden == 1 && e.hidden_at == 1950);
    coop_state_tick(h, 2200);
    assert(e.hidden == 1);
    assert(!coop_state_transfer(h, "transfer_arrive", "stale", 1, 2300, 2300));
    assert(!coop_state_transfer(h, "bad_command", "bad", 100, 2300, 2300));
    assert(coop_state_get(h)->epoch == 2);
    assert(coop_state_transfer(h, "transfer_arrive", "in", 3, 3000, 2500));
    coop_state_tick(h, 2999);
    assert(!coop_state_get(h)->visible && coop_state_get(h)->glow == 0);
    coop_state_tick(h, 3000);
    assert(e.glow_at == 3000 && !coop_state_get(h)->visible);
    coop_state_tick(h, 3499);
    assert(!coop_state_get(h)->visible);
    coop_state_tick(h, 3500);
    assert(coop_state_get(h)->visible && coop_state_get(h)->y < -400);
    coop_state_tick(h, 4100);
    assert(coop_state_get(h)->y == 0 && e.arrived == 0);
    coop_state_tick(h, 4300);
    assert(e.arrived == 1 && e.arrived_at == 4300 && !coop_state_get(h)->transferring);
    coop_state_tick(h, 4400);
    assert(e.arrived == 1);
    coop_state_delete(h);
}
static void input_test(void)
{
    events_t e = {0};
    coop_state_handle_t h = create(&e);
    coop_state_connection(h, true);
    coop_state_button(h, true, 1000);
    coop_state_button(h, false, 1200);
    assert(e.summon == 1 && e.voice_start == 0);
    coop_state_presence(h, true, 1, false);
    coop_state_button(h, true, 2000);
    assert(e.voice_start == 1);
    coop_state_button(h, true, 2200);
    assert(e.voice_start == 1);
    coop_state_button(h, false, 2600);
    assert(e.voice_end == 1);
    coop_state_button(h, true, 3000);
    coop_state_tick(h, 33000);
    assert(e.voice_end == 2 && !coop_state_get(h)->listening);
    coop_state_button(h, true, 34000);
    coop_state_connection(h, false);
    assert(e.voice_cancel == 1 && !coop_state_get(h)->listening);
    coop_state_button(h, true, 35000);
    assert(e.voice_start == 3 && e.say == 1);
    coop_state_delete(h);
}
static void motion_test(void)
{
    events_t e = {0};
    coop_state_handle_t h = create(&e);
    coop_state_presence(h, true, 1, false);
    coop_state_connection(h, true);
    coop_state_imu(h, 0, 0, 1, 0, 0, 0, 2000);
    coop_state_button(h, true, 2100);
    coop_state_imu(h, 3, 0, 1, 400, 0, 0, 2200);
    assert(coop_state_get(h)->motion == COOP_FALL && e.voice_cancel == 1);
    coop_state_action(h, "happy", 2300);
    assert(coop_state_get(h)->motion == COOP_FALL);
    for (uint64_t t = 2210; t <= 4200; t += 10) {
        coop_state_imu(h, 0, 0, 1, 0, 0, 0, t);
        coop_state_tick(h, t);
    }
    assert(coop_state_get(h)->motion == COOP_GETUP);
    coop_state_action(h, "jump", 4210);
    assert(coop_state_get(h)->motion == COOP_GETUP);
    for (uint64_t t = 4210; t <= 5500; t += 10) {
        coop_state_imu(h, 0, 0, 1, 0, 0, 0, t);
        coop_state_tick(h, t);
    }
    assert(coop_state_get(h)->motion == COOP_CRY);
    coop_state_touch(h, true, 5510);
    assert(coop_state_get(h)->motion == COOP_IDLE);
    coop_state_restore(h, false, false, true, 9, "restart", false);
    assert(!coop_state_get(h)->visible && coop_state_get(h)->epoch == 9);
    coop_state_presence(h, true, 8, false);
    assert(!coop_state_get(h)->visible);
    coop_state_delete(h);
}
static void atlas_test(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *bytes = malloc(size);
    assert(fread(bytes, 1, size, f) == (size_t)size);
    fclose(f);
    assert(coop_atlas_validate(bytes, size));
    assert(!coop_atlas_validate(bytes, 187));
    uint8_t old = bytes[12];
    bytes[12] = 0;
    assert(!coop_atlas_validate(bytes, size));
    bytes[12] = old;
    coop_render_handle_t r;
    assert(coop_render_create(&r) == ESP_OK);
    assert(coop_render_atlas(r, bytes, size));
    events_t e = {0};
    coop_state_handle_t h = create(&e);
    coop_state_presence(h, true, 1, false);
    uint16_t *frame = calloc(480 * 480, 2);
    coop_render_draw(r, coop_state_get(h), frame, 960, 0, 0, 480, 480);
    unsigned lit = 0;
    for (int i = 0; i < 480 * 480; i++)
        lit += frame[i] != 0;
    assert(lit > 1000 && lit < 50000);
    coop_state_action(h, "fall", 2000);
    coop_state_tick(h, 2300);
    coop_render_draw(r, coop_state_get(h), frame, 960, 0, 0, 480, 480);
    free(frame);
    coop_render_delete(r);
    coop_state_delete(h);
    free(bytes);
}
int main(int argc, char **argv)
{
    events_t queue_events = {0};
    coop_state_handle_t qs = create(&queue_events);
    coop_script_handle_t script = NULL;
    assert(coop_script_create(&script) == ESP_OK);
    coop_state_presence(qs, true, 1, false);
    cJSON *commands = cJSON_Parse("{\"t\":\"act\",\"actions\":[\"hop\",\"sit\"]}");
    assert(coop_script_push(script, commands));
    cJSON_Delete(commands);
    coop_script_poll(script, qs, 2000, false);
    assert(coop_state_get(qs)->motion == COOP_JUMP);
    coop_script_poll(script, qs, 2200, false);
    assert(coop_state_get(qs)->motion == COOP_JUMP);
    coop_script_poll(script, qs, 3600, false);
    assert(coop_state_get(qs)->motion == COOP_SIT);
    coop_script_poll(script, qs, 5200, false);
    commands =
        cJSON_Parse("{\"t\":\"say\",\"beats\":[{\"text\":\"第一句\"},{\"text\":\"第二句\"}]}");
    assert(coop_script_push(script, commands));
    cJSON_Delete(commands);
    coop_script_poll(script, qs, 5300, false);
    assert(queue_events.say == 1);
    coop_script_poll(script, qs, 7000, true);
    assert(queue_events.say == 1);
    coop_script_poll(script, qs, 7100, false);
    coop_script_poll(script, qs, 7200, false);
    assert(queue_events.say == 2);
    coop_script_delete(script);
    coop_state_delete(qs);
    assert(argc == 2);
    transfer_test();
    input_test();
    motion_test();
    atlas_test(argv[1]);
    puts("PASS: transfer timing, stale messages, button arbitration, IMU recovery, restart "
         "snapshot and atlas bounds");
    return 0;
}
