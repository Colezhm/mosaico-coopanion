// SPDX-License-Identifier: MIT
#include "coop_receipt.h"
#include <stddef.h>

coop_fence_result_t coop_receipt_wait(const coop_receipt_config_t *cfg,
                                     uint32_t budget_ms, uint64_t *completed_at,
                                     unsigned *attempts)
{
    if (!cfg || !cfg->flush || !cfg->now || !cfg->wait || !completed_at || !attempts)
        return COOP_FENCE_FAILED;
    *attempts = 0;
    *completed_at = 0;
    const uint64_t started = cfg->now(cfg->ctx);
    for (;;) {
        uint64_t elapsed = cfg->now(cfg->ctx) - started;
        if (elapsed >= budget_ms)
            return COOP_FENCE_TIMEOUT;
        uint32_t remaining = budget_ms - (uint32_t)elapsed;
        (*attempts)++;
        coop_fence_result_t result = cfg->flush(cfg->ctx, remaining < 1000 ? remaining : 1000);
        if (result == COOP_FENCE_OK) {
            *completed_at = cfg->now(cfg->ctx);
            return COOP_FENCE_OK;
        }
        if (result == COOP_FENCE_FAILED)
            return result;
        /* GSP 1.5.1 rejects flush while a Canvas draw callback is active,
         * including a draw on another task. Do not discard that receipt or
         * spin at render priority; let the current frame finish first. */
        elapsed = cfg->now(cfg->ctx) - started;
        if (elapsed >= budget_ms)
            return COOP_FENCE_TIMEOUT;
        remaining = budget_ms - (uint32_t)elapsed;
        cfg->wait(cfg->ctx, remaining < 10 ? remaining : 10);
    }
}
