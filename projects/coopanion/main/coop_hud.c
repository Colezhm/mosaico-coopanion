// SPDX-License-Identifier: MIT
#include "coop_hud.h"
#include <stdio.h>
void coop_hud_status(const coop_snapshot_t *s, unsigned page, unsigned pages,
                     char out[COOP_HUD_STATUS_BYTES])
{
    out[0] = 0;
    if (!s || s->transferring)
        return;
    /* TTS is off for every build, so "muted" is not news; show what changes. */
    const char *state = s->listening            ? "倾听中"
                        : s->motion == COOP_THINK ? "思考中"
                        : s->motion == COOP_SPEAK ? "回复中"
                        : s->connected            ? "已连接"
                                                  : "离线";
    int n = snprintf(out, COOP_HUD_STATUS_BYTES, "%s  ·  %s%u%%", state,
                     s->battery < COOP_HUD_LOW_BATTERY ? "电量低 " : "", s->battery);
    if (pages > 1 && n > 0 && n < COOP_HUD_STATUS_BYTES)
        snprintf(out + n, COOP_HUD_STATUS_BYTES - n, "  ·  %u/%u", page + 1, pages);
}
