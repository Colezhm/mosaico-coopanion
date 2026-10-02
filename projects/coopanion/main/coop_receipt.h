// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

typedef enum {
    COOP_FENCE_OK, COOP_FENCE_BUSY, COOP_FENCE_TIMEOUT, COOP_FENCE_FAILED
} coop_fence_result_t;

/** Called only by the I/O task. wait must yield, and now must be monotonic. */
typedef struct {
    coop_fence_result_t (*flush)(void *ctx, uint32_t timeout_ms);
    uint64_t (*now)(void *ctx);
    void (*wait)(void *ctx, uint32_t delay_ms);
    void *ctx;
} coop_receipt_config_t;

/** Retry a rejected/expired render fence within one total deadline. A receipt
 * must not be sent unless this returns OK; completed_at is then render time,
 * not the earlier state-machine time. No network or state ownership is changed. */
coop_fence_result_t coop_receipt_wait(const coop_receipt_config_t *config,
                                     uint32_t budget_ms, uint64_t *completed_at,
                                     unsigned *attempts);
