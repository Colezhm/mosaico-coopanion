// @vitest-environment jsdom
import { beforeEach, describe, expect, it } from 'vitest';
// @ts-expect-error the page module is browser JS without types
import { install } from '../web/page.js';

type Msg = Record<string, unknown> & { t: string };

/** The pet page's side of the relay contract (pet-app.js `useRelay`), recorded. */
function mount(state: Record<string, unknown> = {}) {
  document.body.innerHTML = '<div id="stage"><div id="figureLayer"></div><div id="bubble"></div></div>';
  const layer = document.querySelector<HTMLElement>('#figureLayer')!, bubble = document.querySelector<HTMLElement>('#bubble')!;
  const sent: Msg[] = [], holds: boolean[] = [], done: string[] = [];
  let take: (m: Msg) => boolean = () => false, draw: (now: number) => void = () => {};
  const body = { layout: { x: 400, facing: 1 }, place: (x: number) => { body.layout.x = x; }, do: (w: string) => done.push(w) };
  install({
    state, send: (m: Msg) => sent.push(m), stage: document.querySelector('#stage'), layer, ui: [bubble],
    body: () => body, bounds: () => ({ W: 1200, H: 800, floorY: 760, S: .42 }), hold: (on: boolean) => holds.push(on),
    onOrder: (fn: typeof take) => { take = fn; }, onFrame: (fn: typeof draw) => { draw = fn; },
  });
  return { layer, bubble, sent, holds, done, order: (m: Msg) => take(m), frame: (now: number) => draw(now) };
}

beforeEach(() => {
  // Node's own experimental localStorage global shadows jsdom's: a plain one in its place
  const items = new Map<string, string>();
  const store = { getItem: (k: string) => items.get(k) ?? null, setItem: (k: string, v: string) => { items.set(k, String(v)); }, removeItem: (k: string) => { items.delete(k); }, clear: () => items.clear() };
  Object.defineProperty(globalThis, 'localStorage', { value: store, configurable: true });
  globalThis.requestAnimationFrame = ((fn: FrameRequestCallback) => { fn(0); return 0; }) as typeof requestAnimationFrame;
});

describe("Mosaico's page module", () => {
  it('hides the body and its bubbles while the board owns it, and shows them when it is back', () => {
    const p = mount({ held: true, hidden: true });
    expect(p.layer.style.visibility).toBe('hidden');
    expect(p.order({ t: 'presence', owner: 'device', epoch: 3, transfer: null })).toBe(true);
    expect(p.layer.style.visibility).toBe('hidden');
    expect(p.bubble.style.visibility).toBe('hidden');
    expect(p.holds.at(-1)).toBe(true);
    p.order({ t: 'presence', owner: 'desktop', epoch: 4, transfer: null });
    expect(p.layer.style.visibility).toBe('');
    expect(p.holds.at(-1)).toBe(false);
  });

  it('sinks into the portal and acknowledges hidden only after the last frame', () => {
    const p = mount();
    p.order({ t: 'presence', owner: 'desktop', epoch: 1, transfer: null });
    const t0 = performance.now();
    p.order({ t: 'transfer_depart', id: 'transfer-2', epoch: 2 });
    expect(p.holds.at(-1)).toBe(true);
    p.frame(t0 + 500);
    expect(p.layer.style.transform).toMatch(/translate\(0px, [\d.]+px\)/);
    expect(p.sent.some((m) => m.t === 'transfer_hidden')).toBe(false);
    p.frame(t0 + 1000);
    expect(p.layer.style.visibility).toBe('hidden');
    expect(p.sent.find((m) => m.t === 'transfer_hidden')).toMatchObject({ id: 'transfer-2', epoch: 2 });
    // a repeated depart for the same transfer is answered at once
    p.order({ t: 'transfer_depart', id: 'transfer-2', epoch: 2 });
    expect(p.sent.filter((m) => m.t === 'transfer_hidden')).toHaveLength(2);
    expect(JSON.parse(localStorage.getItem('mosaico-presence')!)).toMatchObject({ hidden: true, id: 'transfer-2' });
  });

  it('rises where it left, carries the face across and acknowledges arrival', () => {
    const p = mount({ held: true, hidden: true });
    p.order({ t: 'presence', owner: 'desktop', epoch: 5, transfer: { id: 'transfer-5', epoch: 5, from: 'device', to: 'desktop', phase: 'arriving' } });
    const t0 = performance.now();
    p.order({ t: 'transfer_arrive', id: 'transfer-5', epoch: 5, delayMs: 0, emotion: 'happy' });
    p.frame(t0 + 100);
    expect(p.layer.style.visibility).toBe('hidden');
    p.frame(t0 + 600);
    expect(p.layer.style.visibility).toBe('');
    p.frame(t0 + 1200);
    expect(p.done).toEqual(['happy']);
    expect(p.sent.find((m) => m.t === 'transfer_arrived')).toMatchObject({ id: 'transfer-5' });
    expect(p.holds.at(-1)).toBe(false);
  });

  it('answers a status query with whether this page hid the body, and leaves other orders to the page', () => {
    const p = mount();
    expect(p.order({ t: 'transfer_status', transfer: { id: 'x', epoch: 9, from: 'desktop' } })).toBe(true);
    expect(p.sent.at(-1)).toEqual({ t: 'transfer_report', id: 'x', epoch: 9, hidden: false });
    expect(p.order({ t: 'say', id: 's', beats: [] })).toBe(false);
  });
});
