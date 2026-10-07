// SPDX-License-Identifier: MIT
#include "coop_state.h"
#include "coop_render.h"
#include "coop_script.h"
#include "coop_subtitle.h"
#include "coop_fx.h"
#include "coop_touch.h"
#include "coop_wifi_pick.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned hidden, arrived, voice_start, voice_end, voice_cancel, summon, say, touch, vibrate, haptic;
    char last_haptic[16], last_touch[16];
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
    if (!strcmp(e->type, "touch")) {
        events->touch++;
        snprintf(events->last_touch, sizeof(events->last_touch), "%s", e->text ? e->text : "");
    }
    if (!strcmp(e->type, "haptic")) {
        events->haptic++;
        snprintf(events->last_haptic, sizeof(events->last_haptic), "%s", e->text ? e->text : "");
    }
    if (!strcmp(e->type, "vibrate")) events->vibrate++;
}
static coop_state_handle_t create(events_t *e)
{
    coop_state_handle_t h = NULL;
    const coop_state_config_t cfg = {.event = event, .ctx = e, .seed = 4};
    assert(coop_state_create(&cfg, &h) == ESP_OK);
    return h;
}
/* ---------- physical layer ---------- */

/* Constant IMU reading at 100 Hz for @p ms with UI ticks every 30 ms. */
static void feed(coop_state_handle_t h, uint64_t *now, float ax, float ay, float az, float gz, uint64_t ms)
{
    for (uint64_t start = *now, end = *now + ms; *now < end; *now += 10) {
        coop_state_imu(h, ax, ay, az, 0, 0, gz, *now);
        if ((*now - start) % 30 == 0)
            coop_state_tick(h, *now);
    }
}
static void toss_and_catch(coop_state_handle_t h, uint64_t *now, float impact)
{
    feed(h, now, 0, 0, 1, 0, 400);
    feed(h, now, 0, 0, .05f, 0, 300);
    feed(h, now, 0, 0, impact, 0, 20);
    feed(h, now, 0, 0, 1, 0, 300);
}
static void shake_board(coop_state_handle_t h, uint64_t *now, float g, uint64_t ms)
{
    for (uint64_t start = *now, end = *now + ms; *now < end; *now += 10) {
        coop_state_imu(h, g * sinf((float)(*now - start) / 1000.f * 6.2831853f * 5), 0, 1, 0, 0, 0, *now);
        if ((*now - start) % 30 == 0)
            coop_state_tick(h, *now);
    }
}
static coop_state_handle_t resident(events_t *e, uint64_t *now)
{
    coop_state_handle_t h = create(e);
    coop_state_presence(h, true, 1, false);
    coop_state_connection(h, true);
    coop_state_tick(h, *now);
    feed(h, now, 0, 0, 1, 0, 500);
    return h;
}
static void physical_test(void)
{
    events_t e = {0};
    uint64_t now = 1000;
    coop_state_handle_t h = resident(&e, &now);

    /* Tossed: floats with wide eyes; caught hard: a playful landing that buzzes. */
    feed(h, &now, 0, 0, 1, 0, 400);
    feed(h, &now, 0, 0, .05f, 0, 300);
    assert(coop_state_get(h)->air > 20 && !strcmp(coop_state_get(h)->expression, "surprised"));
    assert(coop_state_get(h)->eye_wide > 1 && coop_state_get(h)->fx == COOP_FX_EXCLAIM);
    assert(coop_state_get(h)->motion != COOP_FALL && coop_state_get(h)->motion != COOP_STUMBLE);
    unsigned buzz = e.vibrate;
    feed(h, &now, 0, 0, 2.6f, 0, 20);
    feed(h, &now, 0, 0, 1, 0, 300);
    assert(e.vibrate == buzz + 1);
    assert(!strcmp(coop_state_get(h)->expression, "delighted") && coop_state_get(h)->fx == COOP_FX_SPARKLE);
    assert(strstr(coop_state_get(h)->subtitle, "再来一次"));
    feed(h, &now, 0, 0, 1, 0, 800);
    assert(coop_state_get(h)->air == 0);

    /* Tolerance: repeated tosses escalate to a sulk with an anger mark. */
    for (int i = 0; i < 6; i++)
        toss_and_catch(h, &now, 3);
    assert(coop_state_get(h)->motion == COOP_SULK && coop_state_get(h)->fx == COOP_FX_ANGER);
    /* Petted mid-sulk: forgiven, with hearts, once the sulk ends. */
    coop_state_touch(h, true, now);
    for (int i = 0; i < 200 && coop_state_get(h)->motion == COOP_SULK; i++)
        feed(h, &now, 0, 0, 1, 0, 30);
    feed(h, &now, 0, 0, 1, 0, 60);
    assert(coop_state_get(h)->motion != COOP_SULK && coop_state_get(h)->fx == COOP_FX_HEARTS);
    assert(strstr(coop_state_get(h)->subtitle, "原谅"));
    feed(h, &now, 0, 0, 1, 0, 2000);
    /* Petting and a calm minute restore the playful mood. */
    coop_state_touch(h, true, now);
    assert(coop_state_get(h)->fx == COOP_FX_HEARTS);
    feed(h, &now, 0, 0, 1, 0, 30000);
    toss_and_catch(h, &now, 2.2f);
    assert(!strcmp(coop_state_get(h)->expression, "delighted"));
    coop_state_delete(h);

    /* Shaking giggles with notes, then wobbles; never a stumble while shaking. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    shake_board(h, &now, 1.1f, 900);
    assert(coop_state_get(h)->fx == COOP_FX_NOTES && coop_state_get(h)->motion != COOP_STUMBLE);
    feed(h, &now, 0, 0, 1, 0, 1800);
    assert(strstr(coop_state_get(h)->subtitle, "好痒"));
    coop_state_delete(h);

    /* Turning onto another edge is not a shake, however abrupt. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    feed(h, &now, 1, 0, 0, 0, 3000);
    assert(coop_state_get(h)->fx != COOP_FX_NOTES && coop_state_get(h)->fx != COOP_FX_STARS);
    assert(!strstr(coop_state_get(h)->subtitle, "好痒") && !strstr(coop_state_get(h)->subtitle, "晕"));
    coop_state_delete(h);

    /* A knock on the left side: look left with a question mark; two knocks hop. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    feed(h, &now, .9f, 0, 1, 0, 20);
    feed(h, &now, 0, 0, 1, 0, 200);
    assert(coop_state_get(h)->fx == COOP_FX_QUESTION && coop_state_get(h)->look_x < -.5f);
    feed(h, &now, .9f, 0, 1, 0, 20);
    feed(h, &now, 0, 0, 1, 0, 60);
    assert(coop_state_get(h)->motion == COOP_JUMP);
    feed(h, &now, 0, 0, 1, 0, 2000);
    /* The bump of a screen touch is not a knock. */
    coop_state_touch(h, false, now);
    feed(h, &now, 0, 0, 1, 0, 800);
    coop_state_touch(h, false, now);
    unsigned fx = coop_state_get(h)->fx;
    feed(h, &now, .9f, 0, 1, 0, 20);
    feed(h, &now, 0, 0, 1, 0, 100);
    assert(coop_state_get(h)->fx == fx);
    coop_state_delete(h);

    /* Spinning flat on the table: dizzy spiral eyes, not a tumble. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    feed(h, &now, 0, 0, 1, 320, 1900);
    assert(coop_state_get(h)->eye_mode == 1 && strstr(coop_state_get(h)->subtitle, "转晕"));
    assert(coop_state_get(h)->motion != COOP_STUMBLE && e.vibrate == 0);
    coop_state_delete(h);

    /* Face down: sleeps instead of falling; face up: wakes asking. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    for (int i = 0; i < 150; i++) {
        feed(h, &now, 0, 0, -1, 0, 10);
        assert(coop_state_get(h)->motion != COOP_FALL);
    }
    feed(h, &now, 0, 0, -1, 0, 1500);
    assert(coop_state_get(h)->motion == COOP_SLEEP && coop_state_get(h)->fx == COOP_FX_ZZZ);
    feed(h, &now, 0, 0, 1, 0, 600);
    /* Being flipped back over may wobble him; he is awake either way. */
    assert(coop_state_get(h)->motion != COOP_SLEEP && strstr(coop_state_get(h)->subtitle, "天亮"));
    assert(e.vibrate == 0);
    coop_state_delete(h);

    /* A reply on screen survives physical captions until it is old. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    coop_state_say(h, "这是刚收到的回复", false, now);
    toss_and_catch(h, &now, 2.2f);
    assert(!strcmp(coop_state_get(h)->subtitle, "这是刚收到的回复"));
    feed(h, &now, 0, 0, 1, 0, 9000);
    toss_and_catch(h, &now, 2.2f);
    assert(strcmp(coop_state_get(h)->subtitle, "这是刚收到的回复"));
    coop_state_delete(h);

    /* A push sways the body on its spring, which settles again. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    feed(h, &now, -.6f, 0, 1, 0, 150);
    assert(fabsf(coop_state_get(h)->body_dx) > 3);
    feed(h, &now, 0, 0, 1, 0, 2500);
    assert(fabsf(coop_state_get(h)->body_dx) < .5f && fabsf(coop_state_get(h)->squash) < .02f);

    /* The AI key is heard during any animation but a transfer or a turn. */
    feed(h, &now, .5f, 0, 1, 0, 20);
    feed(h, &now, 0, 0, 1, 0, 60);
    assert(coop_state_animation_busy(h));
    unsigned starts = e.voice_start;
    coop_state_button(h, true, now);
    assert(coop_state_get(h)->listening && coop_state_get(h)->motion == COOP_LISTEN && e.voice_start == starts + 1);
    coop_state_button(h, false, now + 400);
    coop_state_voice_result(h, "好的", false, now + 900);
    feed(h, &now, 0, 0, 1, 0, 3000);

    /* Left alone: drowsy, then dozing with Zs; a touch wakes him. */
    for (int i = 0; i < 200; i++)
        feed(h, &now, 0, 0, 1, 0, 1000);
    assert(coop_state_get(h)->motion == COOP_SLEEP && coop_state_get(h)->fx == COOP_FX_ZZZ);
    coop_state_touch(h, true, now);
    coop_state_tick(h, now + 30);
    assert(coop_state_get(h)->motion != COOP_SLEEP && coop_state_get(h)->fx != COOP_FX_ZZZ);
    coop_state_delete(h);
}

static void walk_test(void)
{
    events_t e = {0};
    uint64_t now = 1000;
    coop_state_handle_t h = resident(&e, &now);
    /* Walking left turns him to face left; the stride sets the pace. */
    coop_state_walk(h, .15f, false, now);
    uint64_t start = now;
    float gait0 = coop_state_get(h)->gait;
    while (coop_state_get(h)->motion == COOP_WALK && now - start < 6000) {
        feed(h, &now, 0, 0, 1, 0, 30);
        if (coop_state_get(h)->x < .45f)
            assert(coop_state_get(h)->facing < 0);
    }
    /* 0.35 of the ground is 119 px: about 1.1 s at Coo's 108 px/s plus easing. */
    assert(coop_state_get(h)->motion != COOP_WALK && now - start >= 1000 && now - start <= 2600);
    assert(fabsf(coop_state_get(h)->x - .15f) < .01f && coop_state_get(h)->facing < 0);
    assert(coop_state_get(h)->gait - gait0 > .8f);
    coop_state_walk(h, .85f, true, now);
    feed(h, &now, 0, 0, 1, 0, 300);
    assert(coop_state_get(h)->facing > 0);
    coop_state_delete(h);

    /* Left to himself he sits down sometimes, and gets up again on his own. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    uint64_t sat = 0, stood = 0;
    for (int i = 0; i < 4000 && !stood; i++) {
        coop_state_imu(h, 0, 0, 1, 0, 0, 0, now);
        coop_state_tick(h, now);
        now += 30;
        if (!sat && coop_state_get(h)->motion == COOP_SIT)
            sat = now;
        if (sat && coop_state_get(h)->motion != COOP_SIT)
            stood = now;
    }
    assert(sat && stood && stood - sat >= 5900 && stood - sat <= 9100);
    /* A sit that was asked for lasts. */
    coop_state_action(h, "sit", now);
    for (int i = 0; i < 400; i++) {
        coop_state_tick(h, now);
        now += 30;
    }
    assert(coop_state_get(h)->motion == COOP_SIT);
    coop_state_delete(h);
}
/* ---------- board-only interactions ---------- */

static coop_touch_kind_t swipe(coop_touch_t *t, float x0, float y, float x1, coop_part_t part, uint64_t *now, unsigned *count,
                               coop_touch_kind_t want)
{
    coop_touch_kind_t last = COOP_TOUCH_NONE;
    float step = x1 > x0 ? 5 : -5;
    for (float x = x0; (step > 0 ? x <= x1 : x >= x1); x += step) {
        coop_touch_event_t e = coop_touch_sample(t, x, y, true, part, *now);
        *now += 16;
        if (e.kind != COOP_TOUCH_NONE)
            last = e.kind;
        if (e.kind == want)
            ++*count;
    }
    return last;
}
static void touch_test(void)
{
    coop_touch_t t;
    coop_touch_reset(&t);
    uint64_t now = 0;
    unsigned n = 0;
    /* A quick tap on the head is a poke there. */
    coop_touch_sample(&t, 100, 100, true, COOP_PART_HEAD, now);
    coop_touch_event_t e = coop_touch_sample(&t, 101, 100, false, COOP_PART_HEAD, now + 120);
    assert(e.kind == COOP_TOUCH_POKE && e.part == COOP_PART_HEAD && !coop_touch_claimed(&t));
    /* A sweep across the head is one stroke. */
    now = 1000;
    coop_touch_sample(&t, 200, 150, true, COOP_PART_HEAD, now);
    swipe(&t, 200, 150, 270, COOP_PART_HEAD, &now, &n, COOP_TOUCH_STROKE);
    coop_touch_sample(&t, 270, 150, false, COOP_PART_HEAD, now);
    assert(n == 1);
    /* Fast back-and-forth is tickling. */
    now = 3000;
    n = 0;
    coop_touch_sample(&t, 200, 200, true, COOP_PART_BODY, now);
    for (int i = 0; i < 8; i++)
        swipe(&t, i & 1 ? 230 : 200, 200, i & 1 ? 200 : 230, COOP_PART_BODY, &now, &n, COOP_TOUCH_TICKLE);
    coop_touch_sample(&t, 200, 200, false, COOP_PART_BODY, now);
    assert(n >= 1);
    /* Holding still picks up; moving carries; letting go flings with the finger's speed. */
    now = 6000;
    coop_touch_sample(&t, 240, 330, true, COOP_PART_BODY, now);
    assert(coop_touch_claimed(&t));
    coop_touch_kind_t k = COOP_TOUCH_NONE;
    for (int i = 0; i < 25 && k != COOP_TOUCH_GRAB; i++) {
        now += 20;
        k = coop_touch_sample(&t, 240, 330, true, COOP_PART_BODY, now).kind;
    }
    assert(k == COOP_TOUCH_GRAB && now - 6000 >= 350);
    for (int i = 1; i <= 5; i++) {
        now += 16;
        e = coop_touch_sample(&t, 240 + 20 * i, 330 - 4 * i, true, COOP_PART_NONE, now);
        assert(e.kind == COOP_TOUCH_DRAG);
    }
    e = coop_touch_sample(&t, 340, 310, false, COOP_PART_NONE, now + 10);
    assert(e.kind == COOP_TOUCH_RELEASE && e.vx > 600);
    /* A tap beside the figure is not a poke. */
    coop_touch_sample(&t, 30, 30, true, COOP_PART_NONE, now + 100);
    assert(coop_touch_sample(&t, 30, 30, false, COOP_PART_NONE, now + 180).kind == COOP_TOUCH_TAP_EMPTY);
}
/* Feeds a still board while touching; returns once motion leaves @p from or time runs out. */
static void settle_motion(coop_state_handle_t h, uint64_t *now, coop_motion_t from, uint64_t limit)
{
    for (uint64_t end = *now + limit; coop_state_get(h)->motion == from && *now < end;)
        feed(h, now, 0, 0, 1, 0, 30);
}
static void interaction_test(void)
{
    events_t e = {0};
    uint64_t now = 1000;
    coop_state_handle_t h = resident(&e, &now);
    /* Poking the head: a curious look and a tick. */
    coop_state_pointer(h, 240, 260, true, COOP_PART_HEAD, now);
    coop_state_pointer(h, 240, 260, false, COOP_PART_HEAD, now + 80);
    feed(h, &now, 0, 0, 1, 0, 60);
    assert(coop_state_get(h)->fx == COOP_FX_QUESTION && !strcmp(e.last_haptic, "tick") && !strcmp(e.last_touch, "poke"));
    feed(h, &now, 0, 0, 1, 0, 1500);
    /* Three strokes across the head: content, hearts, a purr. */
    for (int i = 0; i < 3; i++) {
        coop_state_pointer(h, 200, 260, true, COOP_PART_HEAD, now);
        for (float x = 200; x <= 270; x += 5) {
            now += 16;
            coop_state_pointer(h, x, 260, true, COOP_PART_HEAD, now);
        }
        coop_state_pointer(h, 270, 260, false, COOP_PART_HEAD, now);
        feed(h, &now, 0, 0, 1, 0, 120);
    }
    assert(!strcmp(coop_state_get(h)->expression, "love") && coop_state_get(h)->fx == COOP_FX_HEARTS);
    assert(strstr(coop_state_get(h)->subtitle, "好舒服") && !strcmp(e.last_haptic, "purr"));
    feed(h, &now, 0, 0, 1, 0, 3000);
    /* Picked up, carried high, flung sideways: flies, bounces, lands. */
    coop_state_pointer(h, 240, 330, true, COOP_PART_BODY, now);
    for (int i = 0; i < 25 && coop_state_get(h)->motion != COOP_CARRY; i++) {
        now += 20;
        coop_state_pointer(h, 240, 330, true, COOP_PART_BODY, now);
    }
    assert(coop_state_get(h)->motion == COOP_CARRY && coop_state_pointer_claimed(h));
    for (int i = 1; i <= 10; i++) {
        now += 20;
        coop_state_pointer(h, 240, 330 - 18 * i, true, COOP_PART_NONE, now);
    }
    assert(coop_state_get(h)->y < -150);
    unsigned buzz = e.haptic;
    for (int i = 1; i <= 4; i++) {
        now += 16;
        coop_state_pointer(h, 240 + 40 * i, 150, true, COOP_PART_NONE, now);
    }
    coop_state_pointer(h, 400, 150, false, COOP_PART_NONE, now + 8);
    assert(coop_state_get(h)->motion == COOP_THROWN);
    settle_motion(h, &now, COOP_THROWN, 6000);
    assert(coop_state_get(h)->motion != COOP_THROWN && fabsf(coop_state_get(h)->y) < .5f);
    assert(e.haptic > buzz && (!strcmp(e.last_touch, "thrown") || !strcmp(e.last_touch, "drop")));
    feed(h, &now, 0, 0, 1, 0, 3000);
    /* Bubbles: blown on request, popped by a tap with a tick. */
    coop_state_action(h, "bubbles", now);
    feed(h, &now, 0, 0, 1, 0, 300);
    const coop_bubbles_t *b = &coop_state_get(h)->bubbles;
    int target = -1;
    for (int i = 0; i < COOP_BUBBLES; i++)
        if (b->b[i].state == COOP_BUBBLE_FLOAT)
            target = i;
    assert(target >= 0);
    float bx = b->b[target].x, by = b->b[target].y;
    coop_state_pointer(h, bx, by, true, COOP_PART_NONE, now);
    assert(coop_state_get(h)->bubbles.b[target].state == COOP_BUBBLE_POP && !strcmp(e.last_touch, "bubble"));
    coop_state_pointer(h, bx, by, false, COOP_PART_NONE, now + 60);
    assert(!coop_state_pointer_claimed(h));
    feed(h, &now, 0, 0, 1, 0, 12000);
    assert(!coop_bubbles_active(&coop_state_get(h)->bubbles)); /* the rest float off and pop */
    coop_state_delete(h);

    /* Held quietly in the hands: snuggles with a heartbeat; put down: notices. */
    memset(&e, 0, sizeof(e));
    now = 1000;
    h = resident(&e, &now);
    feed(h, &now, 0, 0, 1, 0, 20000); /* on the table: never a hug */
    assert(strcmp(e.last_touch, "hug"));
    unsigned beats = 0;
    for (uint64_t end = now + 16000; now < end && strcmp(e.last_touch, "hug"); now += 10) {
        float jitter = (now / 10) % 2 ? 2.f : -2.f;
        coop_state_imu(h, 0, 0, 1, jitter, -jitter, jitter, now);
        if (now % 30 == 0)
            coop_state_tick(h, now);
    }
    assert(!strcmp(e.last_touch, "hug") && strstr(coop_state_get(h)->subtitle, "暖和"));
    for (uint64_t end = now + 4000; now < end; now += 10) {
        float jitter = (now / 10) % 2 ? 2.f : -2.f;
        coop_state_imu(h, 0, 0, 1, jitter, -jitter, jitter, now);
        if (now % 30 == 0) {
            coop_state_tick(h, now);
            beats += !strcmp(e.last_haptic, "heartbeat");
        }
    }
    assert(beats > 0 && !strcmp(coop_state_get(h)->expression, "love"));
    feed(h, &now, 0, 0, 1, 0, 3000);
    assert(!strcmp(e.last_touch, "putdown") && strstr(coop_state_get(h)->subtitle, "放下我啦"));
    coop_state_delete(h);
}
static void wifi_pick_test(void)
{
    coop_wifi_cred_t known[COOP_WIFI_SAVED] = {{"Home", "pw-home"}, {"Office", "pw-office"}};
    coop_wifi_ap_t seen[] = {{"Office", -65, true, false}, {"Home", -70, true, false}, {"Cafe", -40, false, false}};
    /* The most recent network wins unless another is clearly stronger. */
    assert(coop_wifi_pick(known, 2, seen, 3) == 0);
    seen[0].rssi = -55;
    assert(coop_wifi_pick(known, 2, seen, 3) == 1);
    assert(coop_wifi_pick(known, 2, seen + 2, 1) == -1);
    /* Tidy: strongest per SSID, hidden ones dropped, saved first, then signal. */
    coop_wifi_ap_t aps[] = {{"Cafe", -40, false, false}, {"Home", -80, true, false}, {"", -30, true, false},
                            {"Home", -65, true, false}, {"Office", -75, true, false}};
    unsigned n = coop_wifi_tidy(aps, 5, known, 2);
    assert(n == 3 && !strcmp(aps[0].ssid, "Home") && aps[0].rssi == -65 && aps[0].saved);
    assert(!strcmp(aps[1].ssid, "Office") && aps[1].saved && !strcmp(aps[2].ssid, "Cafe") && !aps[2].saved);
    /* Remember moves to the front and keeps at most the capacity; drop removes. */
    unsigned count = 2;
    count = coop_wifi_remember(known, count, COOP_WIFI_SAVED, "Office", "new");
    assert(count == 2 && !strcmp(known[0].ssid, "Office") && !strcmp(known[0].password, "new"));
    const char *more[] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; i++)
        count = coop_wifi_remember(known, count, COOP_WIFI_SAVED, more[i], "x");
    assert(count == COOP_WIFI_SAVED && !strcmp(known[0].ssid, "D") && !strcmp(known[4].ssid, "Office"));
    count = coop_wifi_drop(known, count, "B");
    assert(count == 4 && strcmp(known[1].ssid, "B") && strcmp(known[2].ssid, "B"));
    assert(!strcmp(coop_wifi_strength(-50), "信号强") && !strcmp(coop_wifi_strength(-80), "信号弱"));
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
    coop_state_battery(h, 1, false);
    coop_state_tick(h, 4400);
    assert(e.arrived == 1);
    coop_state_action(h, "cry", 4500);
    assert(coop_state_transfer(h, "transfer_depart", "queued", 4, 0, 4600));
    assert(coop_state_get(h)->motion == COOP_CRY);
    /* Cancellation while waiting for a clip must never hide the source later. */
    coop_state_presence(h, true, 4, false);
    assert(coop_state_get(h)->motion == COOP_CRY);
    coop_state_tick(h, 8000);
    coop_state_tick(h, 9000);
    assert(coop_state_get(h)->visible && e.hidden == 1);
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
    assert(e.voice_start == 3 && e.say == 0);
    coop_state_delete(h);
}
static void motion_test(void)
{
    events_t e = {0};
    coop_state_handle_t h = create(&e);
    coop_state_presence(h, true, 1, false);
    coop_state_connection(h, true);
    coop_state_imu(h, 0, 0, 1, 0, 0, 0, 2000);
    coop_state_imu(h, .4f, 0, 1, 20, 0, 0, 2100);
    assert(coop_state_get(h)->motion==COOP_SWAY);
    coop_state_action(h,"stumble",2200);
    assert(coop_state_get(h)->motion==COOP_SWAY);
    assert(e.say==0 && e.touch==0 && e.vibrate==0);
    /* Gravity turning to the left edge interrupts the sway and turns at once:
     * never a fall toward the old ground first. */
    uint64_t turned = 0;
    for (uint64_t t = 2210; t <= 3400; t += 10) {
        coop_state_imu(h, .9063f, 0, .4226f, 0, 0, 0, t);
        coop_state_tick(h, t);
        assert(coop_state_get(h)->motion != COOP_FALL);
        if (!turned && coop_state_get(h)->motion == COOP_SETTLE)
            turned = t;
    }
    assert(turned && turned - 2210 <= 400);
    assert(coop_state_get(h)->edge == 1 && fabsf(coop_state_get(h)->orientation - 90) < .1f);
    assert(e.vibrate == 1); /* one buzz when he lands on the new ground */
    for (uint64_t t = 3410; t <= 6000; t += 10) {
        coop_state_imu(h, .9063f, 0, .4226f, 0, 0, 0, t);
        coop_state_tick(h, t);
    }
    assert(e.vibrate == 1 && coop_state_get(h)->edge == 1);
    coop_state_delete(h);

    /* A steep slope that is not yet another edge: he loses balance once. */
    memset(&e, 0, sizeof(e));
    h = create(&e);
    coop_state_presence(h, true, 1, false);
    coop_state_connection(h, true);
    for (uint64_t t = 1000; t <= 3000; t += 10) {
        coop_state_imu(h, 0, 1, 0, 0, 0, 0, t);
        coop_state_tick(h, t);
    }
    bool fell = false;
    for (uint64_t t = 3010; t <= 9000; t += 10) {
        coop_state_imu(h, .66f, .66f, .35f, 0, 0, 0, t);
        coop_state_tick(h, t);
        fell |= coop_state_get(h)->motion == COOP_FALL;
    }
    assert(fell && coop_state_get(h)->edge == 0);
    coop_state_action(h, "jump", 9010);
    unsigned buzz = e.vibrate;
    for (uint64_t t = 9020; t <= 14000; t += 10) {
        coop_state_imu(h, .66f, .66f, .35f, 0, 0, 0, t);
        coop_state_tick(h, t);
    }
    assert(e.vibrate == buzz); /* holding the slope does not trip him again */
    coop_state_restore(h, false, false, true, 9, "restart", false);
    assert(!coop_state_get(h)->visible && coop_state_get(h)->epoch == 9);
    coop_state_presence(h, true, 8, false);
    assert(!coop_state_get(h)->visible);
    coop_state_delete(h);
}
static void stumble_and_edges_test(void)
{
    events_t e={0};coop_state_handle_t h=create(&e);
    coop_state_presence(h,true,1,false);coop_state_battery(h,1,false);coop_state_imu(h,0,0,1,0,0,0,2000);
    for(uint64_t t=2010;t<=2300;t+=10){coop_state_imu(h,0,0,1,220,0,0,t);coop_state_tick(h,t);}
    assert(coop_state_get(h)->motion==COOP_STUMBLE);
    assert(strstr(coop_state_get(h)->subtitle,"小短腿"));
    assert(e.vibrate==1 && e.touch==0 && e.say==0);
    coop_state_delete(h);
    for(unsigned edge=0;edge<4;edge++){
        h=create(&e);coop_state_presence(h,true,1,false);coop_state_battery(h,1,false);
        coop_state_imu(h,0,0,1,0,0,0,1000);
        float ax=edge==1?1:edge==3?-1:0,ay=edge==0?1:edge==2?-1:0;
        for(uint64_t t=1010;t<=18000;t+=10){coop_state_imu(h,ax,ay,0,0,0,0,t);coop_state_tick(h,t);}
        assert(coop_state_get(h)->edge==edge);
        assert(fabsf(coop_state_get(h)->orientation-edge*90.f)<.1f);
        coop_state_transfer(h,"transfer_depart","edge-out",2,0,19000);
        coop_state_tick(h,19350);
        assert(coop_state_get(h)->edge==edge && coop_state_get(h)->glow>0);
        coop_state_delete(h);
    }
}
static void subtitle_test(void)
{
    const char *text="这是一段超过一行的中文回复，用来验证自动换行以及长回复分页，不会越过屏幕边界，也不会在一个汉字中间截断。第二页仍然应该完整保留后面的文字。";
    unsigned pages=coop_subtitle_pages(text);assert(pages>=2);
    char page[COOP_SUBTITLE_PAGE_BYTES],joined[512]={0};size_t n=0;
    for(unsigned p=0;p<pages;p++){
        coop_subtitle_page(text,p,page);unsigned lines=1;
        for(const char *c=page;*c;c++){if(*c=='\n')lines++;else joined[n++]=*c;}
        assert(lines<=3);
    }
    assert(!strcmp(joined,text));
    /* Every page concatenates back to the input; no line starts with closing
     * punctuation; pages prefer a sentence end on their last line. */
    const char *samples[]={
        "这是多行字幕的测试。轻轻摇晃只会播放摇晃动画，不会触发语音。倾斜时我会努力站稳，摔倒后需要你摸摸头才能恢复心情。",
        "一二三四五六七八九十一二三四五六七八，下一行不能以逗号开头。",
        "Hello there, this caption mixes English words with 中文 so that words wrap at spaces instead of splitting letters apart.",
        "第一句。\n第二句在新行。"};
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++){
        const char *t=samples[i];char all[1024]={0};size_t m=0;
        unsigned count=coop_subtitle_pages(t);
        for(unsigned p=0;p<count;p++){
            coop_subtitle_page(t,p,page);
            assert(page[0]);
            const char *line=page;
            for(;;){
                assert(strncmp(line,"，",3)&&strncmp(line,"。",3)&&strncmp(line,"！",3));
                const char *next=strchr(line,'\n');if(!next)break;line=next+1;
            }
            for(const char *c=page;*c;c++)if(*c!='\n')all[m++]=*c;
        }
        char expect[1024]={0};size_t k=0;for(const char *c=t;*c;c++)if(*c!='\n')expect[k++]=*c;
        assert(!strcmp(all,expect));
    }
    /* Page 1 of the first sample ends on a full stop, not mid-word. */
    coop_subtitle_page(samples[0],0,page);
    size_t len=strlen(page);
    assert(len>=3&&(!strcmp(page+len-3,"。")||!strcmp(page+len-3,"，")));
    /* ASCII words are not split across lines. */
    coop_subtitle_page(samples[2],0,page);
    for(const char *c=page;(c=strchr(c,'\n'));c++)
        assert(c[-1]==' '||(unsigned char)c[-1]>=0x80||c[1]==' ');
    /* A word adjoining Chinese text also stays whole at the line boundary. */
    const char *adjoining[]={
        "字幕分页测试：轻轻摇晃，慢慢倾斜。English words stay together.",
        "一二三四五六七八九十一二三四五六七English"};
    for(unsigned i=0;i<sizeof(adjoining)/sizeof(adjoining[0]);i++){
        bool found=false;
        for(unsigned p=0;p<coop_subtitle_pages(adjoining[i]);p++){
            coop_subtitle_page(adjoining[i],p,page);
            if(strstr(page,"English"))found=true;
        }
        assert(found);
    }
    assert(coop_subtitle_pages("")==1);
}
static void caption_test(void)
{
    events_t e = {0};
    coop_state_handle_t h = create(&e);
    coop_state_connection(h, true);
    assert(!strcmp(coop_state_get(h)->subtitle, "等待 Coo 来访"));
    coop_state_presence(h, true, 1, false);
    coop_state_connection(h, false);
    assert(strstr(coop_state_get(h)->subtitle, "电脑未连接"));
    /* Reconnecting must not leave an "offline" caption under a "connected" status. */
    coop_state_connection(h, true);
    assert(!strstr(coop_state_get(h)->subtitle, "未连接"));
    /* Content captions survive a reconnect. */
    coop_state_say(h, "这是回复", false, 100);
    coop_state_connection(h, false);
    coop_state_connection(h, true);
    assert(!strstr(coop_state_get(h)->subtitle, "未连接"));
    coop_state_say(h, "这是回复", false, 200);
    coop_state_tick(h, 300);
    /* Leaving replaces the reply with the away caption, and it tracks the link. */
    assert(coop_state_transfer(h, "transfer_depart", "out", 2, 0, 1000));
    for (uint64_t t = 1000; t <= 2000; t += 33)
        coop_state_tick(h, t);
    coop_state_presence(h, false, 2, false);
    assert(strstr(coop_state_get(h)->subtitle, "Coo 在电脑上"));
    coop_state_connection(h, false);
    assert(strstr(coop_state_get(h)->subtitle, "暂时过不来"));
    coop_state_connection(h, true);
    assert(strstr(coop_state_get(h)->subtitle, "Coo 在电脑上"));
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
    assert(coop_state_get(qs)->motion == COOP_JUMP); /* elapsed script delay is not animation completion */
    coop_state_tick(qs,3600);
    coop_script_poll(script,qs,3610,false);
    assert(coop_state_get(qs)->motion == COOP_SIT);
    coop_script_poll(script, qs, 5200, false);
    commands =
        cJSON_Parse("{\"t\":\"say\",\"beats\":[{\"text\":\"第一句\"},{\"text\":\"第二句\"}]}");
    assert(coop_script_push(script, commands));
    cJSON_Delete(commands);
    coop_script_poll(script, qs, 5300, false);
    assert(queue_events.say == 0);
    coop_script_poll(script, qs, 7000, true);
    assert(queue_events.say == 0);
    coop_script_poll(script, qs, 7100, false);
    coop_script_poll(script, qs, 7200, false);
    assert(queue_events.say == 0);
    assert(!strcmp(coop_state_get(qs)->subtitle,"第一句"));
    coop_state_tick(qs,10400);
    coop_script_poll(script,qs,10410,false);
    coop_script_poll(script,qs,10420,false);
    assert(!strcmp(coop_state_get(qs)->subtitle,"第二句"));
    coop_script_delete(script);
    coop_state_delete(qs);
    assert(argc >= 2);
    transfer_test();
    input_test();
    motion_test();
    stumble_and_edges_test();
    physical_test();
    walk_test();
    touch_test();
    interaction_test();
    wifi_pick_test();
    subtitle_test();
    caption_test();
    atlas_test(argv[1]);
    for(int i=2;i<argc;i++)atlas_test(argv[i]);
    puts("PASS: transfer timing, stale messages, button arbitration, IMU recovery, restart "
         "snapshot, physical reactions and atlas bounds");
    return 0;
}
