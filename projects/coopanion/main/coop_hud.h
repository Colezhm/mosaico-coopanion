// SPDX-License-Identifier: MIT
#pragma once
#include "coop_state.h"
#include <stddef.h>
#define COOP_HUD_STATUS_BYTES 96
#define COOP_HUD_LOW_BATTERY 15
/** Formats the one-line status shown above the subtitle. Pure; shared by the
 * device UI, the native simulator and the host preview. */
void coop_hud_status(const coop_snapshot_t *s, unsigned page, unsigned pages,
                     char out[COOP_HUD_STATUS_BYTES]);
