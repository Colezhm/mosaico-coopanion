// SPDX-License-Identifier: MIT
#pragma once
#include "coop_state.h"
#include "cJSON.h"
typedef struct coop_script_t *coop_script_handle_t;
esp_err_t coop_script_create(coop_script_handle_t *out);
void coop_script_delete(coop_script_handle_t handle);
/** UI-thread owned. Copies bounded say/act messages; rejects a full queue. */
bool coop_script_push(coop_script_handle_t handle, const cJSON *command);
void coop_script_poll(coop_script_handle_t handle, coop_state_handle_t state, uint64_t now,
                      bool audio_busy);
