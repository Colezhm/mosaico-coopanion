/** Board clock estimate from ping/pong pairs (NTP-style minimum-RTT filter).
 *
 * offset = boardTime - desktopTime. A single sample's error is bounded by
 * RTT/2 and Wi-Fi jitter makes most samples worse than the best one, so keep a
 * short window and trust the lowest-RTT sample in it. This also keeps the clock
 * usable when the board's modem sleeps between pings: one quick sample every
 * window is enough, instead of every sample having to be quick. */
export interface ClockSample { offset: number; rtt: number; at: number }
export interface ClockEstimate { ready: boolean; offset: number; rtt: number; samples: number }

export const CLOCK = Object.freeze({
  /** Samples kept; pings are every 2 s, so about 16 s of history. */
  window: 8,
  /** Oldest sample that may still be trusted. Crystal drift over this is < 1 ms. */
  maxAgeMs: 20_000,
  /** Worst accepted round trip: offset error <= 50 ms, inside the ±80 ms budget. */
  maxRttMs: 100,
});

export class ClockFilter {
  private samples: ClockSample[] = [];
  constructor(private readonly limits = CLOCK) {}

  reset(): void { this.samples = []; }

  /** sentAt/receivedAt: desktop monotonic ms; boardAt: board monotonic ms. */
  add(sentAt: number, boardAt: number, receivedAt: number): ClockEstimate {
    const rtt = receivedAt - sentAt;
    if (Number.isFinite(rtt) && rtt >= 0 && Number.isFinite(boardAt)) {
      this.samples.push({ offset: boardAt - (sentAt + receivedAt) / 2, rtt, at: receivedAt });
      if (this.samples.length > this.limits.window) this.samples.shift();
    }
    return this.estimate(receivedAt);
  }

  estimate(now: number): ClockEstimate {
    this.samples = this.samples.filter(s => now - s.at <= this.limits.maxAgeMs);
    let best: ClockSample | null = null;
    for (const s of this.samples) if (!best || s.rtt < best.rtt) best = s;
    if (!best) return { ready: false, offset: 0, rtt: Infinity, samples: 0 };
    return { ready: best.rtt <= this.limits.maxRttMs, offset: best.offset, rtt: best.rtt, samples: this.samples.length };
  }
}
