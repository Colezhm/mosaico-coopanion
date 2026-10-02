// SPDX-License-Identifier: MIT
#include "coop_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "coop_subtitle.h"

#define SWAY_G .12f
#define STUMBLE_G .60f
#define STUMBLE_DPS 200.f
#define STUMBLE_HOLD_MS 140
#define FALL_COS .573576f /* 55 degrees from the last settled gravity vector. */
#define FALL_HOLD_MS 180
#define EDGE_HOLD_MS 450
#define REACTION_COOLDOWN_MS 15000

struct coop_state_t {
    coop_snapshot_t view;
    coop_state_config_t config;
    uint64_t entered, now, previous, stable_since, last_motion, last_speech, next_idle;
    uint64_t voice_start, glow_at, disturbance_at, tilt_since;
    float grav[3], anchor[3], target, turn_from, turn_to;
    uint64_t imu_at, moderate_since, edge_since, reading_until;
    unsigned candidate_edge;
    bool pending_fall, pending_depart, soothing;
    uint32_t seed, disturbances;
    bool imu_started, pressed, summoned_press, hidden, transfer_done, glow_sent, inverted_latched;
};
bool coop_state_animation_busy(coop_state_handle_t h)
{
    if (!h) return false;
    switch (h->view.motion) {
    case COOP_WALK: case COOP_RUN: case COOP_JUMP: case COOP_SWAY:
    case COOP_STUMBLE: case COOP_FALL: case COOP_GETUP: case COOP_CRY:
    case COOP_SULK: case COOP_SPEAK: case COOP_SETTLE:
    case COOP_ARRIVE: case COOP_DEPART: return true;
    default: return false;
    }
}
static float clampf(float x, float a, float b)
{
    return fminf(b, fmaxf(a, x));
}
static float smooth(float x)
{
    x = clampf(x, 0, 1);
    return x * x * (3 - 2 * x);
}
static void emit(coop_state_handle_t h, const char *type, const char *text)
{
    if (!h->config.event)
        return;
    const coop_event_t e = {.type = type,
                            .text = text,
                            .epoch = h->view.epoch,
                            .utterance = h->view.utterance,
                            .at = h->now};
    h->config.event(h->config.ctx, &e);
}
static void motion(coop_state_handle_t h, coop_motion_t m)
{
    h->view.motion = m;
    h->entered = h->now;
    h->view.phase = 0;
    h->view.scale_x = h->view.scale_y = 1;
    h->view.angle = 0;
    h->view.y = 0;
}
static uint32_t random_next(coop_state_handle_t h)
{
    h->seed = h->seed * 1664525u + 1013904223u;
    return h->seed;
}
static void cancel_voice(coop_state_handle_t h)
{
    if (h->view.listening) {
        h->view.listening = false;
        emit(h, "voice_cancel", "");
    }
}
esp_err_t coop_state_create(const coop_state_config_t *config, coop_state_handle_t *out)
{
    if (!config || !out)
        return ESP_ERR_INVALID_ARG;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return ESP_ERR_NO_MEM;
    coop_state_handle_t h = *out;
    h->config = *config;
    h->seed = config->seed ? config->seed : 1;
    h->view.x = .5f;
    h->view.scale_x = h->view.scale_y = 1;
    h->view.battery = 100;
    h->view.muted = true; /* TTS suspended for this release; microphone stays available. */
    h->view.body_color = 0xffffff;
    h->view.eye_color = 0x2fd59b;
    h->hidden = true;
    strcpy(h->view.expression, "neutral");
    strcpy(h->view.subtitle, "等待 Coo 来访");
    return ESP_OK;
}
void coop_state_delete(coop_state_handle_t h)
{
    free(h);
}
void coop_state_restore(coop_state_handle_t h, bool resident, bool visible, bool in_transit,
                        uint32_t epoch, const char *id, bool muted)
{
    if (!h || !id || strlen(id) >= sizeof(h->view.transfer_id))
        return;
    h->view.resident = resident;
    h->view.visible = visible;
    h->view.transferring = in_transit;
    if (!resident)
        h->view.visible = false;
    h->view.epoch = epoch;
    (void)muted;
    h->view.muted = true;
    h->hidden = !h->view.visible;
    h->transfer_done = resident && visible && !in_transit;
    strcpy(h->view.transfer_id, id);
}
const coop_snapshot_t *coop_state_get(coop_state_handle_t h)
{
    return h ? &h->view : NULL;
}
void coop_state_connection(coop_state_handle_t h, bool connected)
{
    if (!h)
        return;
    h->view.connected = connected;
    if (!connected) {
        cancel_voice(h);
        h->pressed = false;
        if (h->view.resident && !h->view.transferring)
            strcpy(h->view.subtitle, "电脑未连接，陪你玩一会儿");
    }
}
void coop_state_presence(coop_state_handle_t h, bool resident, uint32_t epoch, bool in_transit)
{
    if (!h || epoch < h->view.epoch)
        return;
    bool was_resident = h->view.resident;
    h->view.epoch = epoch;
    h->view.resident = resident;
    h->view.transferring = in_transit;
    if (!in_transit) {
        h->pending_depart = false;
        h->view.visible = resident;
        h->view.glow = 0;
        h->hidden = !resident;
        if (!resident || !was_resident || h->view.motion == COOP_DEPART || h->view.motion == COOP_ARRIVE)
            motion(h, COOP_IDLE);
    }
    if (!resident || in_transit)
        cancel_voice(h);
    emit(h, "persist", "");
}
bool coop_state_transfer(coop_state_handle_t h, const char *command, const char *id, uint32_t epoch,
                         uint64_t glow_at, uint64_t now)
{
    if (!command || (strcmp(command, "transfer_status") && strcmp(command, "transfer_prepare") &&
                     strcmp(command, "transfer_depart") && strcmp(command, "transfer_arrive")))
        return false;
    if (!h || !id || strlen(id) >= sizeof(h->view.transfer_id) || epoch < h->view.epoch)
        return false;
    h->now = now;
    if (!strcmp(command, "transfer_status")) {
        emit(h, h->hidden ? "report_hidden" : "report_visible", id);
        return true;
    }
    if (epoch == h->view.epoch && !strcmp(id, h->view.transfer_id)) {
        if (!strcmp(command, "transfer_depart") && h->hidden) {
            emit(h, "transfer_hidden", id);
            return true;
        }
        if (!strcmp(command, "transfer_arrive") && h->transfer_done) {
            emit(h, "transfer_arrived", id);
            return true;
        }
        if ((!strcmp(command, "transfer_depart") && h->view.motion == COOP_DEPART) ||
            (!strcmp(command, "transfer_arrive") && h->view.motion == COOP_ARRIVE))
            return true;
    }
    h->view.epoch = epoch;
    strcpy(h->view.transfer_id, id);
    h->view.transferring = true;
    cancel_voice(h);
    if (!strcmp(command, "transfer_prepare")) {
        h->transfer_done = false;
        emit(h, "transfer_ready", id);
    } else if (!strcmp(command, "transfer_depart")) {
        if (coop_state_animation_busy(h)) {
            h->pending_depart = true;
            emit(h, "persist", "");
            return true;
        }
        motion(h, COOP_DEPART);
        h->hidden = false;
        h->transfer_done = false;
        emit(h, "audio_stop", "");
    } else if (!strcmp(command, "transfer_arrive")) {
        h->glow_at = glow_at;
        h->view.resident = true;
        h->view.visible = false;
        h->transfer_done = false;
        h->glow_sent = false;
        motion(h, COOP_ARRIVE);
    } else
        return false;
    emit(h, "persist", "");
    return true;
}
static void feedback(coop_state_handle_t h, const char *text)
{
    strncpy(h->view.subtitle, text, sizeof(h->view.subtitle) - 1);
    if (!h->last_speech || h->now - h->last_speech >= REACTION_COOLDOWN_MS) {
        h->last_speech = h->now;
        if (!h->view.muted)
            emit(h, "say", text);
    }
}
static void anchor_gravity(coop_state_handle_t h)
{
    float n = sqrtf(h->grav[0]*h->grav[0]+h->grav[1]*h->grav[1]+h->grav[2]*h->grav[2]);
    if (n > .5f)
        for (unsigned i=0;i<3;i++) h->anchor[i]=h->grav[i]/n;
}
static void fall(coop_state_handle_t h)
{
    h->pending_fall = false;
    h->inverted_latched = true;
    cancel_voice(h);
    emit(h, "audio_stop", "");
    motion(h, COOP_FALL);
    h->last_motion = h->now;
    if (h->now - h->disturbance_at > 30000) h->disturbances = 0;
    h->disturbances++;
    h->disturbance_at = h->now;
    feedback(h, "哎呀，太斜啦！让我站稳。");
    emit(h, "vibrate", "");
    /* A physical fall is local feedback; never misreport a small tilt as a user throw. */
}
static void settle_edge(coop_state_handle_t h)
{
    h->turn_from=h->view.orientation;
    float delta=(float)h->candidate_edge*90-h->turn_from;
    while(delta>180)delta-=360;
    while(delta< -180)delta+=360;
    h->turn_to=h->turn_from+delta;
    h->view.edge=(uint8_t)h->candidate_edge;
    motion(h,COOP_SETTLE);
}
void coop_state_imu(coop_state_handle_t h, float ax, float ay, float az, float gx, float gy,
                    float gz, uint64_t now)
{
    if (!h || !isfinite(ax) || !isfinite(ay) || !isfinite(az) || !isfinite(gx) || !isfinite(gy) ||
        !isfinite(gz))
        return;
    h->now = now;
    if (!h->imu_started) {
        h->grav[0] = ax;
        h->grav[1] = ay;
        h->grav[2] = az;
        h->imu_started = true;
        anchor_gravity(h);
    }
    float dt=h->imu_at ? clampf((now-h->imu_at)/1000.f,.001f,.1f):.01f;
    h->imu_at=now;
    float alpha=1-expf(-dt/.18f);
    h->grav[0] += alpha * (ax - h->grav[0]);
    h->grav[1] += alpha * (ay - h->grav[1]);
    h->grav[2] += alpha * (az - h->grav[2]);
    float dx = ax - h->grav[0], dy = ay - h->grav[1], dz = az - h->grav[2];
    float force = sqrtf(dx * dx + dy * dy + dz * dz), spin = sqrtf(gx * gx + gy * gy + gz * gz);
    h->view.gravity_x = h->grav[0];
    h->view.gravity_y = h->grav[1];
    h->view.gaze = clampf(-h->grav[0] * 8, -8, 8);
    if (force < .18f && spin < 35) {
        if (!h->stable_since)
            h->stable_since = now;
    } else
        h->stable_since = 0;
    /* Sensor coordinates to display: +ax points left, +ay points down.
     * When lying nearly flat the projected gravity is ambiguous: retain the edge. */
    float sx=-h->grav[0], sy=h->grav[1], projection=sqrtf(sx*sx+sy*sy);
    unsigned edge=h->view.edge;
    if (projection>.45f) {
        float current = edge==0?sy:edge==1?-sx:edge==2?-sy:sx;
        unsigned next=fabsf(sx)>fabsf(sy)?(sx<0?1:3):(sy<0?2:0);
        float best=fmaxf(fabsf(sx),fabsf(sy));
        if(best>current+.18f)edge=next;
    }
    if(edge!=h->candidate_edge){h->candidate_edge=edge;h->edge_since=now;}
    float norm=sqrtf(h->grav[0]*h->grav[0]+h->grav[1]*h->grav[1]+h->grav[2]*h->grav[2]);
    float dot=norm>.5f?(h->grav[0]*h->anchor[0]+h->grav[1]*h->anchor[1]+h->grav[2]*h->anchor[2])/norm:1;
    if(dot<FALL_COS && force<.5f) {
        if (!h->tilt_since)
            h->tilt_since = now;
    } else if(dot>.72f) {
        h->tilt_since = 0;
        h->inverted_latched = false;
    }
    bool moderate=force>STUMBLE_G || spin>STUMBLE_DPS;
    if(moderate){if(!h->moderate_since)h->moderate_since=now;}else h->moderate_since=0;
    if (!h->view.resident || h->view.transferring) return;
    if (!h->inverted_latched && h->tilt_since && now-h->tilt_since>=FALL_HOLD_MS)
        h->pending_fall=true;
    /* Coalesce one pending fall; no sensor, touch, script or reply replaces an animation. */
    if (coop_state_animation_busy(h)) return;
    if(h->pending_fall){fall(h);return;}
    if(h->candidate_edge!=h->view.edge && h->stable_since && now-h->edge_since>=EDGE_HOLD_MS){
        settle_edge(h);return;
    }
    if (now - h->last_motion < 2000) return;
    if (h->moderate_since && now-h->moderate_since>=STUMBLE_HOLD_MS) {
        if (h->view.listening) {
            cancel_voice(h);
            strcpy(h->view.subtitle, "晃得没听清，请再说一次");
        }
        motion(h, COOP_STUMBLE);
        h->last_motion = now;
        emit(h, "vibrate", "");
        feedback(h, "慢一点，我的小短腿跟不上啦。");
    } else if (force > SWAY_G && !moderate &&
               (h->view.motion == COOP_IDLE || h->view.motion == COOP_SIT || h->view.motion == COOP_SLEEP))
        motion(h, COOP_SWAY);
}
void coop_state_tick(coop_state_handle_t h, uint64_t now)
{
    if (!h)
        return;
    h->now = now;
    float dt = h->previous ? clampf((now - h->previous) / 1000.f, 0, .05f) : 0;
    h->previous = now;
    float t = (now - h->entered) / 1000.f;
    h->view.phase = t;
    if (h->view.listening && now - h->voice_start >= 30000)
        coop_state_button(h, false, now);
    switch (h->view.motion) {
    case COOP_DEPART:
        h->view.glow = smooth(t / .3f);
        if (t > .3f) {
            float u = smooth((t - .3f) / .65f);
            h->view.y = -470 * u;
            h->view.scale_x = 1 - .25f * u;
        }
        if (t >= .95f && !h->hidden) {
            h->hidden = true;
            h->view.visible = false;
            h->view.resident = false;
            h->view.glow = 0;
            emit(h, "persist", "");
            emit(h, "transfer_hidden", h->view.transfer_id);
        }
        break;
    case COOP_ARRIVE: {
        int64_t elapsed = (int64_t)now - (int64_t)h->glow_at;
        if (elapsed < 0) {
            h->view.glow = 0;
            break;
        }
        h->view.glow = smooth(elapsed / 250.f);
        if (!h->glow_sent) {
            h->glow_sent = true;
            emit(h, "glow_started", h->view.transfer_id);
        }
        if (elapsed >= 500) {
            if (h->hidden)
                emit(h, "body_entered", h->view.transfer_id);
            h->view.visible = true;
            h->hidden = false;
            float u = clampf((elapsed - 500) / 600.f, 0, 1);
            h->view.y = -470 * (1 - u * u);
        }
        if (elapsed >= 1100) {
            h->view.glow = 1 - smooth((elapsed - 1100) / 200.f);
            h->view.scale_y = 1 - .18f * sinf(clampf((elapsed - 1100) / 200.f, 0, 1) * 3.14159f);
        }
        if (elapsed >= 1300 && !h->transfer_done) {
            h->transfer_done = true;
            h->view.transferring = false;
            h->view.visible = true;
            h->view.glow = 0;
            motion(h, COOP_IDLE);
            strcpy(h->view.subtitle, "我来啦！按住 AI 键和我说话");
            emit(h, "persist", "");
            emit(h, "vibrate", "");
            emit(h, "transfer_arrived", h->view.transfer_id);
        }
        break;
    }
    case COOP_WALK:
    case COOP_RUN: {
        float d = h->target - h->view.x, step = (h->view.motion == COOP_RUN ? .35f : .13f) * dt;
        h->view.x += clampf(d, -step, step);
        h->view.y = -fabsf(sinf(t * 12)) * 6;
        if (fabsf(d) < .01f) {
            motion(h, COOP_IDLE);
            emit(h, "arrived", "");
        }
        break;
    }
    case COOP_JUMP:
        h->view.y = -80 * sinf(clampf(t / .7f, 0, 1) * 3.14159f);
        if (t > .7f)
            motion(h, COOP_IDLE);
        break;
    case COOP_SWAY:
        h->view.angle = 12 * sinf(t * 12) * expf(-t * 2);
        if (t > 1.2f)
            motion(h, COOP_IDLE);
        break;
    case COOP_STUMBLE:
        h->view.angle = 25 * sinf(t * 14) * expf(-t);
        if (t > 1.5f) {
            motion(h, COOP_IDLE);
            strcpy(h->view.expression, "angry");
        }
        break;
    case COOP_FALL:
        h->view.angle = 85 * smooth(t / .35f);
        h->view.y = 30 * smooth(t / .35f);
        if (t > .8f && h->stable_since && now - h->stable_since >= 500)
            motion(h, COOP_GETUP);
        break;
    case COOP_GETUP:
        h->view.angle = 85 * (1 - smooth(t / 1.2f));
        h->view.y = 30 * (1 - smooth(t / 1.2f));
        if (t > 1.2f) {
            anchor_gravity(h);
            h->tilt_since=0;
            h->pending_fall=false;
            motion(h, h->disturbances >= 3 ? COOP_SULK : COOP_CRY);
            feedback(h, h->disturbances >= 3 ? "哼，我要背对你三秒钟。"
                                             : "呜，我摔倒啦，摸摸我就好了。");
        }
        break;
    case COOP_CRY:
        strcpy(h->view.expression, "sad");
        if (t > 3) {
            motion(h, COOP_IDLE);
            strcpy(h->view.expression, "neutral");
        }
        break;
    case COOP_SULK:
        strcpy(h->view.expression, "angry");
        h->view.scale_x = -1;
        if (t > 4) {
            motion(h, COOP_IDLE);
            strcpy(h->view.expression, "neutral");
        }
        break;
    case COOP_IDLE:
        h->view.angle = clampf(-h->grav[0] * 12, -15, 15);
        if (h->view.resident && !h->view.transferring && !h->pending_fall && !h->pending_depart && now > h->next_idle) {
            h->next_idle = now + 5000 + random_next(h) % 10000;
            if (h->view.battery < 15)
                motion(h, COOP_SLEEP);
            else if (random_next(h) % 3 == 0) {
                h->target = .15f + (random_next(h) % 700) / 1000.f;
                motion(h, COOP_WALK);
            }
        }
        break;
    case COOP_SPEAK:
        if (now >= h->reading_until)
            motion(h, COOP_IDLE);
        break;
    case COOP_SETTLE:
        h->view.orientation=h->turn_from+(h->turn_to-h->turn_from)*smooth(t/.65f);
        if(t>=.65f){h->view.orientation=h->view.edge*90.f;anchor_gravity(h);motion(h,COOP_IDLE);}
        break;
    default:
        break;
    }
    if (!coop_state_animation_busy(h)) {
        if(h->pending_depart){h->pending_depart=false;h->hidden=false;h->transfer_done=false;motion(h,COOP_DEPART);emit(h,"audio_stop","");}
        else if(h->pending_fall&&!h->view.transferring)fall(h);
        else if(h->soothing){h->soothing=false;h->disturbances=0;strcpy(h->view.expression,"happy");feedback(h,"好啦，原谅你了。");}
    }
}
void coop_state_button(coop_state_handle_t h, bool down, uint64_t now)
{
    if (!h || h->pressed == down)
        return;
    h->now = now;
    h->pressed = down;
    if (h->view.transferring)
        return;
    if (down) {
        h->summoned_press = !h->view.resident;
        if (h->summoned_press)
            return;
        if (!h->view.connected) {
            feedback(h, "电脑未连接");
            return;
        }
        if (coop_state_animation_busy(h) && h->view.motion != COOP_SPEAK)
            return;
        emit(h, "audio_stop", "");
        h->view.utterance++;
        h->view.listening = true;
        h->voice_start = now;
        motion(h, COOP_LISTEN);
        strcpy(h->view.subtitle, "在听，松开 AI 键发送");
        emit(h, "voice_start", "");
    } else if (h->summoned_press) {
        h->summoned_press = false;
        emit(h, "summon", "");
    } else if (h->view.listening) {
        h->view.listening = false;
        motion(h, COOP_THINK);
        strcpy(h->view.subtitle, "正在听懂你的话…");
        emit(h, "voice_end", "");
    }
}
void coop_state_touch(coop_state_handle_t h, bool petting, uint64_t now)
{
    if (!h || !h->view.resident || h->view.transferring)
        return;
    h->now = now;
    if (coop_state_animation_busy(h)) {
        if(petting)h->soothing=true;
        return;
    }
    if (petting) {
        h->disturbances = 0;
        motion(h, COOP_IDLE);
        strcpy(h->view.expression, "happy");
        feedback(h, "好啦，原谅你了。");
    } else {
        coop_state_action(h, "hop", now);
    }
    emit(h, "touch", petting ? "petting" : "poke");
}
void coop_state_action(coop_state_handle_t h, const char *a, uint64_t now)
{
    if (!h || !a || h->view.transferring || coop_state_animation_busy(h))
        return;
    h->now = now;
    if (!strcmp(a, "walk") || !strcmp(a, "run")) {
        coop_state_walk(h, h->view.x > .5f ? .2f : .8f, !strcmp(a, "run"), now);
        return;
    }
    if (!strcmp(a, "jump") || !strcmp(a, "hop"))
        motion(h, COOP_JUMP);
    else if (!strcmp(a, "sit"))
        motion(h, COOP_SIT);
    else if (!strcmp(a, "sleep"))
        motion(h, COOP_SLEEP);
    else if (!strcmp(a, "stand"))
        motion(h, COOP_IDLE);
    else if (!strcmp(a, "sulk") || !strcmp(a, "turn"))
        motion(h, COOP_SULK);
    else if (!strcmp(a, "cry"))
        motion(h, COOP_CRY);
    else if (!strcmp(a, "stumble")) {
        motion(h, COOP_STUMBLE);
        feedback(h, "慢一点，我的小短腿跟不上啦。");
    }
    else if (!strcmp(a, "fall")) {
        cancel_voice(h);
        motion(h, COOP_FALL);
    } else if (!strcmp(a, "sway") || !strcmp(a, "shake") || !strcmp(a, "nod") ||
               !strcmp(a, "spin") || !strcmp(a, "dizzy") || !strcmp(a, "look"))
        motion(h, COOP_SWAY);
    else {
        strncpy(h->view.expression, a, sizeof(h->view.expression) - 1);
    }
}
void coop_state_walk(coop_state_handle_t h, float target, bool run, uint64_t now)
{
    if (!h || h->view.transferring || coop_state_animation_busy(h) ||
        h->view.listening)
        return;
    h->now = now;
    h->target = clampf(target, .12f, .88f);
    motion(h, run ? COOP_RUN : COOP_WALK);
}
void coop_state_say(coop_state_handle_t h, const char *text, bool speak, uint64_t now)
{
    if (!h || !text)
        return;
    h->now = now;
    size_t length=strlen(text);
    if(length>=sizeof(h->view.subtitle)){
        length=sizeof(h->view.subtitle)-1;
        while(length&&((unsigned char)text[length]&0xc0)==0x80)length--;
    }
    memcpy(h->view.subtitle,text,length);h->view.subtitle[length]=0;
    if (speak) {
        h->reading_until=now+coop_subtitle_pages(h->view.subtitle)*5000;
        if (!coop_state_animation_busy(h) && !h->view.transferring &&
            !h->view.listening)
            motion(h, COOP_SPEAK);
        /* Text feedback remains available while board TTS is suspended. */
    }
}
void coop_state_battery(coop_state_handle_t h, uint8_t percent, bool charging)
{
    if (!h)
        return;
    h->view.battery = percent > 100 ? 100 : percent;
    if (charging && h->view.motion == COOP_SLEEP)
        motion(h, COOP_IDLE);
}
void coop_state_menu(coop_state_handle_t h, bool open)
{
    if (h && !h->view.transferring)
        h->view.menu = open;
}
void coop_state_mute(coop_state_handle_t h, bool mute)
{
    if (h) {
        (void)mute;
        h->view.muted = true;
        emit(h, "audio_stop", "");
        strcpy(h->view.subtitle,"语音输出暂时关闭，仍可按住 AI 键说话");
        emit(h, "persist", "");
    }
}
void coop_state_colors(coop_state_handle_t h, uint32_t body, uint32_t eyes)
{
    if (h) {
        h->view.body_color = body;
        h->view.eye_color = eyes;
    }
}
void coop_state_voice_level(coop_state_handle_t h, float level)
{
    if (h)
        h->view.voice_level = clampf(level, 0, 1);
}
void coop_state_emotion(coop_state_handle_t h, const char *face)
{
    if (h && face && !coop_state_animation_busy(h) && strlen(face) < sizeof(h->view.expression))
        strcpy(h->view.expression, face);
}
void coop_state_voice_result(coop_state_handle_t h, const char *text, bool thinking, uint64_t now)
{
    if (!h)
        return;
    h->now = now;
    cancel_voice(h);
    h->pressed = false;
    if (!h->view.transferring && !coop_state_animation_busy(h))
        motion(h, thinking ? COOP_THINK : COOP_IDLE);
    coop_state_say(h, text, false, now);
}
