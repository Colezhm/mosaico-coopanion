// SPDX-License-Identifier: MIT
#pragma once
#include "coop_ui.h"
#include "cJSON.h"
bool coop_pc_link_start(void);
void coop_pc_link_poll(coop_ui_handle_t ui, uint64_t now);
void coop_pc_link_event(const coop_event_t *event);
void coop_pc_link_stop(void);
