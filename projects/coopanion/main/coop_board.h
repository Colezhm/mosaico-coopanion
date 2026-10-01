// SPDX-License-Identifier: MIT
#pragma once
#include "coop_ui.h"
typedef struct coop_board_t *coop_board_handle_t;
esp_err_t coop_board_create(coop_board_handle_t *out);
esp_err_t coop_board_attach(coop_board_handle_t handle, esp_gsp_handle_t gsp);
