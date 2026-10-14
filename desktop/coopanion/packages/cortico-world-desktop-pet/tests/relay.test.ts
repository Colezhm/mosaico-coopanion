import { afterEach, describe, expect, it } from 'vitest';
import { mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DESKTOP_PET_DEFAULTS, type DesktopPetConfigSection } from '../src/config.ts';
import { desktopPetDefinition } from '../src/definition.ts';
import { DesktopPetWorld } from '../src/world.ts';
import type { PetBodyPort, PetBodyRelay } from '../src/relay.ts';
import type { PageMessage } from '../src/server.ts';
import type { ToolDef } from 'cortico/core/types.ts';
import { FakeHost } from './helpers/fake-host.ts';
import { FakePage } from './helpers/page.ts';

const ctx = { role: 'main', log: new FakeHost().log };

/** A relay whose body is on a board while `away`: it records what it carries and what it was asked. */
class BoardRelay implements PetBodyRelay {
  away = false;
  carried: PageMessage[] = [];
  fromPageSeen: PageMessage[] = [];
  port: PetBodyPort | null = null;
  connects = 0;
  computer = 0;
  done = 0;
  pageDir?: string;
  async start(port: PetBodyPort): Promise<void> { this.port = port; }
  async stop(): Promise<void> { this.port = null; }
  send(msg: PageMessage): boolean | undefined {
    if (!this.away) return undefined;
    this.carried.push(msg);
    return true;
  }
  here(): boolean { return !this.away; }
  reachable(): boolean { return this.away; }
  fromPage(msg: PageMessage): boolean {
    if (!msg.t.startsWith('board_')) return false;
    this.fromPageSeen.push(msg);
    return true;
  }
  pageConnected(): void { this.connects++; }
  page(): Record<string, unknown> { return { module: '/relay/page.js', held: this.away, hidden: this.away }; }
  unavailable(tool: string): string | null { return this.away ? `[${tool}] on the board, offline` : null; }
  async beforeComputer(): Promise<void> { this.computer++; this.away = false; }
  computerDone(): void { this.done++; }
  tools(): ToolDef[] {
    return [{ name: 'pet_location', tags: ['read'], description: 'where', parameters: { type: 'object', properties: {} }, handler: async () => ({ text: this.away ? 'board' : 'desktop' }) }];
  }
  envPrompt(): string { return 'BODY CAN TRAVEL'; }
  touchText(kind: string, count: number, user: string): string | null { return kind === 'hug' ? `${user} hugged you ${count}x` : null; }
  panel = { id: 'board', title: 'Board', description: 'the board', getMethods: ['state'], invoke: async (method: string) => ({ method, away: this.away }) };
}

function makeWorld(relay: PetBodyRelay, patch: (c: DesktopPetConfigSection) => void = () => {}) {
  const cfg = structuredClone(DESKTOP_PET_DEFAULTS);
  cfg.enabled = true;
  cfg.port = 0;
  cfg.window.enabled = false;
  cfg.asr.enabled = false;
  patch(cfg);
  const dir = mkdtempSync(join(tmpdir(), 'pet-relay-'));
  const world = new DesktopPetWorld({
    cfg, timezone: 'Asia/Shanghai', persist: (p) => { Object.assign(cfg, p); },
    runtimesRoot: () => join(dir, 'runtimes'), modelsDir: () => join(dir, 'models'), relay,
  });
  return { world, cfg };
}

const origin = (w: DesktopPetWorld) => w.petUrl.replace(/\/pet$/, '');
const tool = (w: DesktopPetWorld, name: string) => w.tools().find((t) => t.name === name)!;
/** A tool's receipt as an outcome (handlers may also answer with plain text). */
const outcome = (r: string | { text: string; failed?: boolean }) => (typeof r === 'string' ? { text: r } : r);

let cleanup: Array<() => Promise<void>> = [];
afterEach(async () => {
  for (const fn of cleanup.reverse()) await fn();
  cleanup = [];
});

async function mounted(relay: BoardRelay, patch?: (c: DesktopPetConfigSection) => void) {
  const made = makeWorld(relay, patch);
  const host = new FakeHost();
  await made.world.start(host);
  cleanup.push(() => made.world.stop());
  return { ...made, host };
}

describe('body relay', () => {
  it('lets the pet page carry the body while the relay leaves it here', async () => {
    const relay = new BoardRelay();
    const { world } = await mounted(relay);
    expect(relay.port).not.toBeNull();
    const page = await FakePage.open(origin(world));
    cleanup.push(() => page.close());
    expect(page.init.relay).toEqual({ module: '/relay/page.js', held: false, hidden: false });
    await expect.poll(() => relay.connects).toBe(1);
    const said = outcome(await tool(world, 'pet_say').handler({ script: 'hello' }, ctx));
    expect(said.failed).toBeFalsy();
    expect((await page.next((m) => m.t === 'say')).beats).toBeTruthy();
    expect(relay.carried).toEqual([]);
  });

  it('routes orders to the relay while the body is away, and says why a tool cannot run', async () => {
    const relay = new BoardRelay();
    relay.away = true;
    const { world } = await mounted(relay);
    expect(world.outputTap()).toBeTruthy();
    const said = outcome(await tool(world, 'pet_say').handler({ script: 'hello' }, ctx));
    expect(said.failed).toBeFalsy();
    expect(relay.carried.map((m) => m.t)).toContain('say');
    relay.send = () => false;
    const failed = await tool(world, 'pet_act').handler({ actions: ['jump'] }, ctx);
    expect(failed).toEqual({ text: '[pet_act] on the board, offline', failed: true });
  });

  it('takes the relay page messages before the World and feeds device input as page input', async () => {
    const relay = new BoardRelay();
    const { world, host } = await mounted(relay);
    const page = await FakePage.open(origin(world));
    cleanup.push(() => page.close());
    page.send({ t: 'board_ready', id: 'x' });
    await expect.poll(() => relay.fromPageSeen.length).toBe(1);
    relay.port!.input({ t: 'text', text: 'typed on the board' });
    await expect.poll(() => host.events.length).toBe(1);
    expect(host.events[0]!.text).toContain('typed on the board');
  });

  it('delivers heard speech, as the answer to an open question first', async () => {
    const relay = new BoardRelay();
    const { world, host } = await mounted(relay);
    const page = await FakePage.open(origin(world));
    cleanup.push(() => page.close());
    relay.port!.heard('good morning');
    await expect.poll(() => host.events.length).toBe(1);
    expect(host.events[0]!.type).toBe('desktop-pet.speech');
    await tool(world, 'pet_ask').handler({ question: 'Tea?', options: ['yes', 'no'] }, ctx);
    relay.port!.heard('green tea please');
    await expect.poll(() => host.events.length).toBe(2);
    expect(host.events[1]!.type).toBe('desktop-pet.answer');
    expect(host.events[1]!.text).toContain('green tea please');
  });

  it('adds its touch kinds, tools, prompt and panel', async () => {
    const relay = new BoardRelay();
    const { world, host } = await mounted(relay, (c) => { c.user = 'Sam'; });
    expect(world.tools().map((t) => t.name)).toContain('pet_location');
    expect((await world.envPromptVars())['pet.relay']).toBe('BODY CAN TRAVEL');
    const decl = world.console();
    expect(decl.panels!.map((p) => p.id)).toContain('board');
    expect(await decl.invoke!('board', 'state', [])).toEqual({ method: 'state', away: false });
    relay.port!.input({ t: 'touch', kind: 'hug' });
    await expect.poll(() => host.events.length, { timeout: 5000 }).toBe(1);
    expect(host.events[0]!.text).toContain('Sam hugged you 1x');
  });

  it('keeps the computer microphone closed while the body is away', async () => {
    const relay = new BoardRelay();
    relay.away = true;
    const { world } = await mounted(relay);
    const page = await FakePage.open(origin(world));
    cleanup.push(() => page.close());
    expect(page.init.mic).toBe(false);
    expect(page.init.relay).toEqual({ module: '/relay/page.js', held: true, hidden: true });
  });

  it('serves the relay page module, and nothing outside its directory', async () => {
    const relay = new BoardRelay();
    const dir = mkdtempSync(join(tmpdir(), 'pet-relay-page-'));
    mkdirSync(join(dir, 'sub'));
    writeFileSync(join(dir, 'page.js'), 'export function install() {}');
    relay.pageDir = dir;
    const { world } = await mounted(relay);
    const ok = await fetch(`${origin(world)}/relay/page.js`);
    expect(ok.status).toBe(200);
    expect(ok.headers.get('content-type')).toContain('javascript');
    expect(await ok.text()).toContain('install');
    expect((await fetch(`${origin(world)}/relay/..%2F..%2Fetc%2Fpasswd`)).status).toBe(403);
    expect((await fetch(`${origin(world)}/relay/missing.js`)).status).toBe(404);
  });

  it('is made for each World instance from the assembly', () => {
    const made: string[] = [];
    const def = desktopPetDefinition({ relay: ({ dataDir }) => { made.push(dataDir); return null; } });
    const cfg = structuredClone(DESKTOP_PET_DEFAULTS);
    const ctxFor = (dataDir: string) => ({
      cfg, timezone: 'Asia/Shanghai', botName: 'Coo', botDir: dataDir, packageDir: dataDir, dataDir, repoRoot: dataDir,
      secret: () => '', storeSecret: () => {}, persist: () => {}, restart: async () => {},
    });
    const dir = mkdtempSync(join(tmpdir(), 'pet-relay-def-'));
    def.create(ctxFor(dir) as never);
    expect(made).toEqual([join(dir, 'desktop-pet')]);
  });
});
