import { describe, expect, it } from 'vitest';
import { ClockFilter } from '../src/mosaico/clock.ts';

describe('Mosaico clock filter', () => {
  it('uses the lowest-RTT sample in the window', () => {
    const clock = new ClockFilter();
    // Board clock = desktop + 5000 ms. A 20 ms round trip is symmetric.
    expect(clock.add(0, 5010, 20)).toMatchObject({ ready: true, offset: 5000, rtt: 20 });
    // A 300 ms modem-sleep sample with skewed delay does not move the estimate.
    expect(clock.add(2000, 7280, 2300)).toMatchObject({ ready: true, offset: 5000, rtt: 20 });
  });
  it('is not ready until a quick sample arrives, and expires old samples', () => {
    const clock = new ClockFilter();
    expect(clock.add(0, 5150, 300).ready).toBe(false);
    expect(clock.add(2000, 7040, 2080)).toMatchObject({ ready: true, offset: 5000 });
    expect(clock.estimate(2080 + 20_001).ready).toBe(false);
  });
  it('keeps a bounded window and ignores invalid samples', () => {
    const clock = new ClockFilter();
    clock.add(0, 5010, 20);
    for (let i = 1; i <= 8; i++) clock.add(i * 2000, i * 2000 + 5100, i * 2000 + 200);
    expect(clock.estimate(16_200)).toMatchObject({ ready: false, samples: 8 });
    expect(clock.add(20_000, Number.NaN, 20_010).samples).toBe(8);
    clock.reset();
    expect(clock.estimate(0).samples).toBe(0);
  });
});
