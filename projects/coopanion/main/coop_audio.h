// SPDX-License-Identifier: MIT
#pragma once
#include "coop_link.h"
typedef struct coop_audio_t *coop_audio_handle_t;
esp_err_t coop_audio_create(coop_link_handle_t link, coop_audio_handle_t *out);
void coop_audio_say(coop_audio_handle_t handle, const char *text);
void coop_audio_stop(coop_audio_handle_t handle);
void coop_audio_record(coop_audio_handle_t handle, uint32_t utterance);
void coop_audio_release(coop_audio_handle_t handle, bool cancel);
float coop_audio_level(coop_audio_handle_t handle);
bool coop_audio_busy(coop_audio_handle_t handle);
