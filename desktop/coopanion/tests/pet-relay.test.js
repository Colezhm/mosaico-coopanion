import { readFileSync } from 'node:fs';
import { afterEach, describe, expect, it } from 'vitest';
import { JSDOM } from 'jsdom';
import { clamp, f, ICONS } from '../packages/cortico-world-desktop-pet/web/ui.js';
import { COO_CSS, mini, normalizeSkin, skinCss } from '../packages/cortico-world-desktop-pet/web/coo/coo.js';
import { t } from '../packages/cortico-world-desktop-pet/web/i18n.js';

const WEB = new URL('../packages/cortico-world-desktop-pet/web/', import.meta.url);
const source = readFileSync(new URL('pet-app.js', WEB), 'utf8').replace(/^import .*;$/gm, '');
const html = readFileSync(new URL('pet.html', WEB), 'utf8');
const pages = [];
afterEach(() => { for (const p of pages.splice(0)) p.close(); });

/** Run the actual page with a quiet socket and body; the test advances the page's frame clock. */
function page() {
  const dom = new JSDOM(html, { url: 'http://127.0.0.1/pet', runScripts: 'outside-only' });
  const win = dom.window;
  pages.push(win);
  const bodyChanges = [];
  const body = {
    layout: { busy: false, facing: 1, cursor: '', bubble: { x: 500, y: 400 } },
    set: (change) => bodyChanges.push(change), cue() {}, talk() {},
  };
  win.requestAnimationFrame = () => 0;
  win.WebSocket = class { readyState = 1; send() {} };
  win.deps = {
    applyTheme: (theme) => { win.document.documentElement.dataset.theme = theme; }, clamp, f, ICONS,
    COO_CSS, mini, normalizeSkin, skinCss,
    createSfx: () => ({ pop() {}, babble() {}, blub() {}, configure() {}, set() {}, listenStart() {}, listenEnd() {}, select() {}, tick() {} }),
    loadBody: async () => body, body,
    t, language: () => 'zh', useLanguage: async () => {},
    // the tab's stage (stage.js) is decoration the activity bubble does not depend on
    lightWall() {}, dressFloor() {}, modeButton: () => ({ show() {} }), glowColor: () => null, setGlow() {}, bindAppearance() {}, announcePainted() {},
  };
  win.eval(`const { applyTheme, clamp, f, ICONS, COO_CSS, mini, normalizeSkin, skinCss, createSfx, loadBody, t, language, useLanguage, lightWall, dressFloor, modeButton, glowColor, setGlow, bindAppearance, announcePainted } = window.deps;\n${source}\nbody = window.deps.body; window.page = { onOrder, openInput, closeBubble, relay, setInteractive, startMic, stopMic, step: (dt) => { T += dt; stepDialog(dt); stepListen(); stepStatus(); layout(); }, interactive: () => bubble.matches(UI_SELECTOR) };`);
  return { ...win.page, bubble: win.document.querySelector('#bubble'), bodyChanges, doc: win.document };
}

describe('the page side of a body relay', () => {
  it('hides the body from the first frame when init says the relay holds it away', () => {
    const p = page();
    p.onOrder({ t: 'init', relay: { held: true, hidden: true } });
    expect(p.doc.querySelector('#figureLayer').style.visibility).toBe('hidden');
    expect(p.relay.held).toBe(true);
  });

  it('leaves the page as it is without a relay', () => {
    const p = page();
    p.onOrder({ t: 'init', relay: null });
    expect(p.doc.querySelector('#figureLayer').style.visibility).toBe('');
    expect(p.relay.held).toBe(false);
  });

  it("lets the relay's page module take orders before the page does", () => {
    const p = page();
    const taken = [];
    p.relay.orders.push((m) => { if (m.t !== 'transfer_depart') return false; taken.push(m); return true; });
    p.onOrder({ t: 'transfer_depart', id: 'x' });
    p.onOrder({ t: 'say', id: 's', beats: [{ text: '你好', actions: [], anchors: [] }] });
    p.step(.01);
    expect(taken).toEqual([{ t: 'transfer_depart', id: 'x' }]);
    expect(p.bubble.classList.contains('say')).toBe(true);
  });
});
