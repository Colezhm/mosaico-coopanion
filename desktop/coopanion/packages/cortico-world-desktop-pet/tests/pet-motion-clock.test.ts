import {describe, expect, it} from 'vitest';
// @ts-expect-error pet-core is shipped as browser JavaScript without type declarations.
import {createPet} from '../web/pet-core.js';

function pet() {
  const element = () => ({setAttribute() {}, innerHTML: '', textContent: ''});
  const frames: unknown[] = [];
  const ctl = createPet({petG: element(), shadowEl: element(), fxG: element()}, {
    roam: 'off',
    sfx: new Proxy({}, {get: () => () => {}}),
    bounds: () => ({W: 600, H: 480, floorY: 450, S: .6}),
    figure: {draw(_element: unknown, _face: unknown, frame: unknown) {frames.push(frame);}},
  });
  return {ctl, frames};
}

function expectFinite(value: unknown) {
  if (typeof value === 'number') expect(Number.isFinite(value)).toBe(true);
  else if (Array.isArray(value)) value.forEach(expectFinite);
  else if (value && typeof value === 'object') Object.values(value).forEach(expectFinite);
}

describe('pet motion clock', () => {
  it('ignores duplicate, backwards and invalid ticks without poisoning later rig frames', () => {
    const {ctl, frames} = pet();
    ctl.step(1 / 60);
    for (const dt of [0, -1 / 60, Number.NaN, Infinity]) {
      const time = ctl.time;
      const state = structuredClone(ctl.pet);
      ctl.step(dt);
      expect(ctl.time).toBe(time);
      expect(ctl.pet).toEqual(state);
    }
    for (let i = 0; i < 180; i++) {ctl.step(1 / 60); ctl.render();}
    frames.forEach(expectFinite);
  });

  it('keeps motion finite after rendering frozen frames and then resuming movement', () => {
    const {ctl, frames} = pet();
    for (let i = 0; i < 60; i++) {ctl.step(1 / 60); ctl.render();}
    const time = ctl.time;
    for (let i = 0; i < 180; i++) ctl.render();
    expect(ctl.time).toBe(time);
    ctl.walkTo(180);
    for (let i = 0; i < 360; i++) {ctl.step(i % 7 ? 1 / 60 : 0); ctl.render();}
    expect(ctl.pet.mode).toBe('idle');
    expect(Math.abs(ctl.pet.x - 180)).toBeLessThan(1.5);
    frames.forEach(expectFinite);
  });
});
