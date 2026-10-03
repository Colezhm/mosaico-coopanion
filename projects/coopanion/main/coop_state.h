// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#endif

typedef struct coop_state_t *coop_state_handle_t;
typedef enum {
    COOP_IDLE,
    COOP_WALK,
    COOP_RUN,
    COOP_SIT,
    COOP_SLEEP,
    COOP_JUMP,
    COOP_SWAY,
    COOP_STUMBLE,
    COOP_FALL,
    COOP_GETUP,
    COOP_CRY,
    COOP_SULK,
    COOP_LISTEN,
    COOP_THINK,
    COOP_SPEAK,
    COOP_SETTLE,
    COOP_DEPART,
    COOP_ARRIVE
} coop_motion_t;
typedef struct {
    bool resident, visible, connected, muted, menu, listening, transferring;
    coop_motion_t motion;
    float x, y, angle, scale_x, scale_y, gaze, glow, phase, gravity_x, gravity_y, voice_level;
    float orientation;
    /* Physical layer, character frame in screen px: spring sway, squash
     * (+ flattened), height floated while tossed, gaze (-1..1). */
    float body_dx, squash, air, look_x, look_y;
    /* Procedural eyes: blink 0 open .. 1 shut, eye_wide 1 normal; eye_mode 0
     * rings, 1 dizzy spirals. fx is a coop_fx_kind_t started fx_t seconds ago. */
    float blink, eye_wide, fx_t;
    uint8_t eye_mode, fx;
    uint8_t edge; /* 0 bottom, 1 left, 2 top, 3 right; screen gravity = (-ax, ay). */
    uint8_t battery;
    uint32_t epoch, utterance;
    uint64_t interacted_at; /* last local button/touch/menu input, ms */
    uint32_t body_color, eye_color;
    char expression[24], subtitle[2048], transfer_id[64];
} coop_snapshot_t;
typedef struct {
    const char *type;
    const char *text;
    uint32_t epoch, utterance;
    uint64_t at;
} coop_event_t;
/** Runs synchronously on the owning UI thread. Event strings are borrowed until return. */
typedef void (*coop_event_cb_t)(void *ctx, const coop_event_t *event);
typedef struct {
    coop_event_cb_t event;
    void *ctx;
    uint32_t seed;
} coop_state_config_t;
esp_err_t coop_state_create(const coop_state_config_t *config, coop_state_handle_t *out);
void coop_state_delete(coop_state_handle_t handle);
const coop_snapshot_t *coop_state_get(coop_state_handle_t handle);
bool coop_state_animation_busy(coop_state_handle_t handle);
void coop_state_tick(coop_state_handle_t handle, uint64_t now);
void coop_state_connection(coop_state_handle_t handle, bool connected);
void coop_state_presence(coop_state_handle_t handle, bool resident, uint32_t epoch,
                         bool in_transit);
void coop_state_restore(coop_state_handle_t handle, bool resident, bool visible, bool in_transit,
                        uint32_t epoch, const char *transfer_id, bool muted);
bool coop_state_transfer(coop_state_handle_t handle, const char *command, const char *id,
                         uint32_t epoch, uint64_t glow_at, uint64_t now);
void coop_state_imu(coop_state_handle_t handle, float ax, float ay, float az, float gx, float gy,
                    float gz, uint64_t now);
void coop_state_button(coop_state_handle_t handle, bool down, uint64_t now);
void coop_state_touch(coop_state_handle_t handle, bool petting, uint64_t now);
void coop_state_action(coop_state_handle_t handle, const char *action, uint64_t now);
void coop_state_walk(coop_state_handle_t handle, float target, bool run, uint64_t now);
void coop_state_say(coop_state_handle_t handle, const char *text, bool speak, uint64_t now);
void coop_state_battery(coop_state_handle_t handle, uint8_t percent, bool charging);
void coop_state_menu(coop_state_handle_t handle, bool open);
void coop_state_mute(coop_state_handle_t handle, bool mute);
void coop_state_colors(coop_state_handle_t handle, uint32_t body, uint32_t eyes);
/** Names the figure in system captions: Coo, or the DeepSeek whale. */
void coop_state_figure(coop_state_handle_t handle, bool whale);
void coop_state_voice_level(coop_state_handle_t handle, float level);
void coop_state_emotion(coop_state_handle_t handle, const char *face);
void coop_state_voice_result(coop_state_handle_t handle, const char *text, bool thinking,
                             uint64_t now);
