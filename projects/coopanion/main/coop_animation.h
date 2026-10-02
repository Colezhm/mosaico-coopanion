// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define COOP_ANIMATION_PIXELS (192 * 216)
#define COOP_ASSET_MAX (1000 * 1024)
bool coop_animation_validate(const uint8_t *p, size_t n);
/* Call only after validation; all frames are bounded to the same canvas. */
int coop_animation_frame(const uint8_t *p, unsigned clip_id, float seconds);
bool coop_animation_decode(const uint8_t *p, int frame, int previous,
                           uint8_t *indices, uint8_t *scratch);
const uint8_t *coop_animation_palette(const uint8_t *p);
