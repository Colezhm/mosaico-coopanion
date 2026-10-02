// SPDX-License-Identifier: MIT
#include "coop_receipt.h"
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint64_t now, drawing_until;
    unsigned calls, timeouts;
    bool broken;
} fake_t;
static uint64_t clock_ms(void *ctx) { return ((fake_t *)ctx)->now; }
static void wait_ms(void *ctx, uint32_t ms) { ((fake_t *)ctx)->now += ms; }
static coop_fence_result_t flush(void *ctx, uint32_t timeout)
{
    fake_t *f = ctx;
    f->calls++;
    if (f->broken) return COOP_FENCE_FAILED;
    if (f->now < f->drawing_until) return COOP_FENCE_BUSY;
    if (f->timeouts) { f->timeouts--; f->now += timeout; return COOP_FENCE_TIMEOUT; }
    f->now += 25; /* The fence returns only after the next rendered frame. */
    return COOP_FENCE_OK;
}
int main(void)
{
    fake_t f = {.now = 100, .drawing_until = 171};
    coop_receipt_config_t cfg = {.flush = flush, .now = clock_ms, .wait = wait_ms, .ctx = &f};
    uint64_t at = 99;
    unsigned attempts;
    assert(coop_receipt_wait(&cfg, 1500, &at, &attempts) == COOP_FENCE_OK);
    assert(at == 205 && attempts == 9); /* No premature receipt while drawing. */
    f = (fake_t){.drawing_until = 2000};
    assert(coop_receipt_wait(&cfg, 1500, &at, &attempts) == COOP_FENCE_TIMEOUT);
    assert(f.now == 1500 && at == 0 && attempts == 150); /* Bounded, no fabricated completion. */
    f = (fake_t){.timeouts = 1};
    assert(coop_receipt_wait(&cfg, 1500, &at, &attempts) == COOP_FENCE_OK);
    assert(at == 1035 && attempts == 2);
    f = (fake_t){.timeouts = 3};
    assert(coop_receipt_wait(&cfg, 1500, &at, &attempts) == COOP_FENCE_TIMEOUT);
    assert(f.now == 1500 && at == 0 && attempts == 2); /* Queue admission shares the deadline. */
    f = (fake_t){.broken = true};
    assert(coop_receipt_wait(&cfg, 1500, &at, &attempts) == COOP_FENCE_FAILED);
    assert(f.calls == 1 && at == 0);
    puts("Render receipt busy/timeout/failure injection: PASS");
}
