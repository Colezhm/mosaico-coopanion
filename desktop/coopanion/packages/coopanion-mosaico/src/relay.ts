/**
 * Mosaico as the desktop pet's body relay (cortico-world-desktop-pet src/relay.ts): one Coo, two
 * screens. The presence journal says which screen owns the body; orders for the body go to the pet
 * page while the computer owns it and to the paired board while the board does. The board's touch,
 * voice and answers come back to the World as if the pet page had sent them.
 *
 * State lives in `<data>/desktop-pet/mosaico/`: `presence.json` (who owns the body, durable before
 * any acknowledgement), `emotion.json` (the face carried across a transfer) and `settings.json`.
 * Persona Memory is not touched; the emotion cache is the body's look, not the bot's mood.
 */
import { existsSync, mkdirSync, readFileSync, renameSync } from 'node:fs';
import { basename, join } from 'node:path';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';
import type { PageMessage, PetBodyPort, PetBodyRelay, PetRelayContext, PetRelayPanel } from 'cortico-world-desktop-pet';
import type { ToolDef } from 'cortico/core/types.ts';
import { PresenceCoordinator, type Body, type CoordinatorOptions, type Message, type Presence } from './coordinator.ts';
import { MosaicoTransport, type Pairing } from './transport.ts';
import { AssetSync } from './assets.ts';
import { loadSettings, patchSettings, saveSettings, writeDurable, type MosaicoSettings } from './settings.ts';
import { BOARD_FACES, boardWord, boardWords, lastFace } from './words.ts';
import { activatePersona, figureOf, personaNotice, WHALE_SEED, type Figure } from './persona.ts';
import { MOSAICO_PROMPT, TOUCH_TEXT, mosaicoTools } from './model-text.ts';

/** Presence, readiness and asset bookkeeping cadence. */
const TICK_MS = 100;
/** A board recording longer than this never sent voice_end; drop it. */
const VOICE_TIMEOUT_MS = 32_000;
/** 30 s of 16 kHz mono PCM16. */
const VOICE_MAX_BYTES = 960_000;
/** 100 ms of 16 kHz mono PCM16; anything shorter is a stray press. */
const VOICE_MIN_BYTES = 3200;
/** The secret holding the board's bearer token, in the deployment `.env`. */
export const TOKEN_SECRET = 'CORTICO_MOSAICO_TOKEN';
/** The page module, served by the pet server at /relay/. */
const PAGE_DIR = fileURLToPath(new URL('../web/', import.meta.url));

/** Seconds since 1970: above any transfer count, inside the board's uint32 epoch until 2106. */
export const recoveryEpoch = (): number => Math.floor(Date.now() / 1000);

const wantsWhale = (skin: unknown): boolean => figureOf(skin) === 'whale';

export interface MosaicoRelayOptions {
  /** The bot's workspace, whose CONSTITUTION.md is swapped per figure; omitted, personas are left alone. */
  workspaceDir?: string;
  /** Coo's first persona: the app's seed in the language in force. */
  cooSeed?: () => string;
}

export class MosaicoRelay implements PetBodyRelay {
  readonly pageDir = PAGE_DIR;
  readonly presence: PresenceCoordinator;
  private settings: MosaicoSettings;
  /** Settings as this process started with them: `enabled` and `pairingFile` apply at the next start. */
  private readonly started: MosaicoSettings;
  private port: PetBodyPort | null = null;
  private transport: MosaicoTransport | null = null;
  private timer: NodeJS.Timeout | null = null;
  private chunks: Buffer[] = [];
  private utterance = 0;
  private audioSeq = 0;
  private bytes = 0;
  private recording = false;
  private voiceGeneration = 0;
  private voiceStarted = 0;
  private speaking = false;
  private computerBusy = false;
  private thinking = false;
  /** What the board was last told of thinking. */
  private boardThinking = false;
  private error = '';
  private deviceReady = false;
  private capabilities: Message | null = null;
  private emotion = 'neutral';
  private skinKey = '';
  private readonly assets: AssetSync;
  private readonly dir: string;

  constructor(private readonly ctx: PetRelayContext, private readonly opts: MosaicoRelayOptions = {}) {
    this.dir = join(ctx.dataDir, 'mosaico');
    mkdirSync(this.dir, { recursive: true, mode: 0o700 });
    this.settings = loadSettings(this.dir, (ctx.cfg as unknown as Record<string, unknown>).mosaico);
    this.started = { ...this.settings };
    this.assets = new AssetSync(
      m => this.transport?.send(m) ?? false,
      m => this.port?.sendPage(m as PageMessage) ?? false,
      ready => {
        this.deviceReady = ready;
        if (ready) this.error = '';
        this.presence?.setDeviceReady(ready && !!this.transport?.ready);
      },
      text => this.fail(text),
    );
    this.emotion = this.loadEmotion();
    const journal = join(this.dir, 'presence.json');
    const options: CoordinatorOptions = {
      now: () => performance.now(),
      save: state => writeDurable(journal, JSON.stringify(state)),
      send: (to, msg) => {
        const message = msg.t === 'transfer_arrive' ? { ...msg, emotion: this.emotion } : msg;
        if (to === 'device') return this.transport?.send(message) ?? false;
        // The desktop page schedules from its own clock; hand it a relative delay.
        return this.port?.sendPage((typeof message.glowAt === 'number'
          ? { ...message, delayMs: Math.max(0, message.glowAt - performance.now()) }
          : message) as PageMessage) ?? false;
      },
      changed: () => {
        if (!this.settledOn('device')) this.cancelVoice();
        this.publishStatus();
      },
    };
    this.presence = this.openJournal(journal, options);
  }

  private get on(): boolean { return this.started.enabled; }

  /** True when the body rests on `body`: owned there and not mid-transfer. */
  settledOn(body: Body): boolean {
    return this.presence.owner === body && !this.presence.transferring;
  }

  /** An unreadable journal cannot say where the body is. It is kept aside and replaced by "desktop owns"
   * at an epoch above any counter the board can hold, so the board's next presence update hides its copy. */
  private openJournal(journal: string, options: CoordinatorOptions): PresenceCoordinator {
    try {
      const saved = existsSync(journal) ? JSON.parse(readFileSync(journal, 'utf8')) as Presence : undefined;
      return new PresenceCoordinator(options, saved);
    } catch (e) {
      const backup = `${journal}.corrupt-${Date.now()}`;
      renameSync(journal, backup);
      const presence = new PresenceCoordinator(options, { version: 1, owner: 'desktop', epoch: recoveryEpoch(), transfer: null });
      options.save(presence.snapshot());
      this.fail(`身体位置记录无法读取(${e instanceof Error ? e.message : String(e)}),已另存为 ${basename(backup)};身体归电脑所有,板端连接后会隐藏。`);
      return presence;
    }
  }

  /* ---------- lifecycle ---------- */

  async start(port: PetBodyPort): Promise<void> {
    this.port = port;
    this.skinKey = JSON.stringify(port.skin());
    this.syncPersona(false);
    this.timer = setInterval(() => this.tick(), TICK_MS);
    if (!this.on) return;
    if (!this.settings.pairingFile) { this.fail('还没有选择配对文件'); return; }
    try {
      const pairing = JSON.parse(readFileSync(this.settings.pairingFile, 'utf8')) as Pairing;
      // Runtime secrets come from the deployment, never from the package or public config.
      pairing.token = this.ctx.secret(TOKEN_SECRET) ?? '';
      this.transport = new MosaicoTransport(pairing, {
        connected: () => {
          this.capabilities = null;
          this.presence.connected('device', false);
          this.startAssets();
          this.publishStatus();
        },
        disconnected: () => {
          this.assets.cancel();
          this.deviceReady = false;
          this.cancelVoice();
          this.presence.disconnected('device');
          this.publishStatus();
        },
        message: msg => this.deviceMessage(msg),
        audio: (id, seq, pcm) => this.audio(id, seq, pcm),
        error: detail => this.fail(detail),
      });
      await this.transport.start();
    } catch (e) {
      this.transport = null;
      this.fail(`Mosaico 未启动: ${(e as Error).message}`);
    }
    this.publishStatus();
  }

  async stop(): Promise<void> {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
    this.cancelVoice();
    this.presence.stop();
    await this.transport?.stop();
    this.transport = null;
    this.port = null;
  }

  private tick(): void {
    try {
      this.checkSkin();
      if (!this.on) return;
      this.presence.setBusy(this.computerBusy || this.thinking || this.recording || this.speaking);
      this.presence.setDeviceReady(this.deviceReady && !!this.transport?.ready);
      this.publishStatus();
      this.presence.tick(this.settings.autonomous);
      this.assets.tick();
      if (this.recording && performance.now() - this.voiceStarted > VOICE_TIMEOUT_MS) {
        this.cancelVoice();
        this.transport?.send({ t: 'voice_error', text: '录音没有完整结束，请再说一次' });
      }
    } catch (e) {
      // Report each distinct failure once; the next tick retries.
      const detail = String(e);
      if (this.error !== detail) this.fail(detail);
    }
  }

  /* ---------- the relay contract ---------- */

  send(msg: PageMessage): boolean | undefined {
    if (!this.on) return undefined;
    this.noteOrder(msg);
    if (this.presence.transferring) return false;
    if (this.presence.owner === 'desktop') return undefined;
    const command = this.forBoard(msg);
    if (command === undefined) return false;
    if (command === null) return true;
    return this.transport?.send({ t: 'pet_command', command }) ?? false;
  }

  here(): boolean { return !this.on || this.settledOn('desktop'); }

  reachable(): boolean { return this.on && this.presence.owner === 'device' && this.presence.deviceOnline; }

  fromPage(msg: PageMessage): boolean {
    if (!this.on) return false;
    if (msg.t === 'asset_exported') { this.assets.exported(msg); return true; }
    if (msg.t.startsWith('transfer_')) {
      this.presence.receive('desktop', msg.t === 'transfer_hidden' ? { ...msg, at: performance.now() } : msg);
      return true;
    }
    if (msg.t === 'text' || msg.t === 'touch' || msg.t === 'answer') this.presence.activity();
    return false;
  }

  pageConnected(): void {
    if (!this.on) return;
    this.presence.connected('desktop');
    if (this.transport?.ready) this.startAssets();
  }

  pageDisconnected(): void { if (this.on) this.presence.disconnected('desktop'); }

  page(): Record<string, unknown> | null {
    if (!this.on) return null;
    const away = this.presence.owner !== 'desktop';
    return { module: '/relay/page.js', held: away || this.presence.transferring, hidden: away };
  }

  unavailable(tool: string): string | null {
    if (!this.on) return null;
    if (this.presence.transferring) return `[${tool} 没执行] 身体正在传送,落地后再做。`;
    if (this.presence.owner === 'device' && !this.presence.deviceOnline) return `[${tool} 没执行] 身体在 Mosaico 上,板子当前没有连接,看不到。`;
    return null;
  }

  async beforeComputer(): Promise<void> {
    if (!this.on) return;
    this.computerBusy = true;
    try { await this.transfer('desktop'); } catch (e) { this.computerBusy = false; throw e; }
  }

  computerDone(): void {
    if (!this.computerBusy) return;
    this.computerBusy = false;
    this.presence.activity();
  }

  tools(): ToolDef[] { return this.on ? mosaicoTools(this) : []; }

  envPrompt(): string { return this.on ? MOSAICO_PROMPT : ''; }

  touchText(kind: string, count: number, user: string): string | null {
    return TOUCH_TEXT[kind]?.(user, count) ?? null;
  }

  readonly panel: PetRelayPanel = {
    id: 'mosaico', title: 'Mosaico', description: '连接状态与跨屏传送。', getMethods: ['state'],
    invoke: async (method, args) => {
      if (method === 'state') return this.state();
      if (method === 'transfer' && (args[0] === 'desktop' || args[0] === 'device')) {
        if (!this.on) throw new Error('Mosaico 未启用');
        await this.transfer(args[0]);
        return this.state();
      }
      if (method === 'settings') {
        this.settings = patchSettings(this.settings, args[0]);
        saveSettings(this.dir, this.settings);
        return this.state();
      }
      throw new Error(`Mosaico 面板没有 ${method}`);
    },
  };

  /* ---------- state ---------- */

  state(): Record<string, unknown> {
    return {
      ...this.presence.snapshot(),
      enabled: this.on,
      settings: this.settings,
      restart: this.settings.enabled !== this.started.enabled || this.settings.pairingFile !== this.started.pairingFile,
      connected: this.presence.deviceOnline,
      ready: this.presence.canUseDevice,
      error: this.error,
      recording: this.recording,
      capabilities: this.capabilities,
    };
  }

  private statusKey = '';
  private publishStatus(): void {
    if (!this.on) return;
    const state = {
      owner: this.presence.owner,
      phase: this.presence.snapshot().transfer?.phase ?? 'idle',
      connected: this.presence.deviceOnline,
      ready: this.presence.canUseDevice,
    };
    const key = JSON.stringify(state);
    if (key === this.statusKey) return;
    this.statusKey = key;
    this.port?.push('desktop-pet.presence', `[身体状态] ${key}`, 'piggyback');
  }

  async transfer(to: Body): Promise<void> { await this.presence.request(to); }

  /* ---------- orders ---------- */

  private expressions(): Set<string> {
    return new Set((this.port?.words() ?? []).filter(w => w.kind === 'expression').map(w => w.id));
  }

  /** Remembers the face the body last made on the computer, to carry it across a transfer. */
  private noteOrder(msg: PageMessage): void {
    if (msg.t === 'status') this.thinking = !!msg.status;
    if (!this.settledOn('desktop')) return;
    const words = msg.t === 'act' ? (msg.actions as unknown[] ?? [])
      : msg.t === 'say' ? (msg.beats as Array<{ actions?: unknown[] }> ?? []).flatMap(b => b.actions ?? [])
      : [];
    const face = lastFace(words, this.expressions());
    if (face) this.rememberEmotion(face);
  }

  /** The board's command for `msg`: null when the board has nothing to do for it, undefined when it cannot take it. */
  private forBoard(msg: PageMessage): Record<string, unknown> | null | undefined {
    switch (msg.t) {
      case 'say': {
        const beats = (msg.beats as Array<{ text?: string; actions?: unknown[] }> ?? [])
          .map(b => ({ text: b.text ?? '', actions: boardWords(b.actions ?? []) }));
        return { t: 'say', id: msg.id, beats };
      }
      case 'act': {
        const actions = boardWords(msg.actions as unknown[] ?? []);
        return actions.length ? { t: 'act', id: msg.id, actions } : null;
      }
      case 'walk': case 'ask': case 'confirm': case 'listen': return msg;
      case 'status': {
        // the board shows thinking, not what the bot reads or writes; only a change is sent
        const on = !!msg.status && (msg.status as { kind?: unknown }).kind === 'think';
        if (on === this.boardThinking) return null;
        this.boardThinking = on;
        return { t: 'thinking', on };
      }
      case 'walk-stop': case 'ask-close': return null;
      // the app's own dialogs (the first-start guide) need the computer's screen
      default: return undefined;
    }
  }

  /* ---------- device ---------- */

  private startAssets(): void {
    const skin = this.port?.skin();
    if (wantsWhale(skin)) {
      this.assets.cancel();
      if (!this.capabilities) return; // The board advertises formats after clock synchronization.
      const formats = this.capabilities.assetFormats;
      if (!Array.isArray(formats) || !formats.includes('COO2')) {
        this.fail('大肥鱼需要 Mosaico 1.1.0 或更新固件，请先更新板端');
        return;
      }
    }
    this.assets.start(skin);
  }

  private checkSkin(): void {
    const skin = this.port?.skin();
    const key = JSON.stringify(skin);
    if (key === this.skinKey) return;
    this.skinKey = key;
    this.syncPersona(true);
    if (this.on && this.presence.deviceOnline) this.startAssets();
  }

  /** Gives the figure on screen its own self-description; tells the running session when it changed. */
  private syncPersona(announce: boolean): void {
    if (!this.opts.workspaceDir || !this.opts.cooSeed || !this.port) return;
    const figure: Figure = figureOf(this.port.skin());
    try {
      const from = activatePersona(this.opts.workspaceDir, figure, { coo: this.opts.cooSeed(), whale: WHALE_SEED });
      if (from && announce) this.port.push('desktop-pet.figure', personaNotice(from, figure), 'flush');
    } catch (e) {
      this.port.log.warn(`切换${figure}的自述失败:${(e as Error).message}`);
    }
  }

  private deviceMessage(msg: Message): void {
    if (msg.t === 'capabilities') {
      const first = !this.capabilities;
      this.capabilities = msg;
      // A whale skin waits for the board's formats before it can start syncing.
      if (first && wantsWhale(this.port?.skin())) this.startAssets();
      return;
    }
    if (this.assets.receive(msg)) return;
    if (msg.t.startsWith('transfer_')) { this.presence.receive('device', msg); return; }
    if (msg.t === 'battery') { this.presence.setBattery(Number(msg.percent)); return; }
    if (msg.t === 'summon') { void this.transfer('device').catch(e => this.report(e)); return; }
    if (msg.t === 'return') { void this.transfer('desktop').catch(e => this.report(e)); return; }
    if (!this.settledOn('device')) return;
    if (msg.t === 'pet_emotion') { this.rememberEmotion(msg.face); return; }
    this.presence.activity();
    switch (msg.t) {
      case 'voice_start':
        if (!this.port?.voiceEnabled()) {
          this.transport?.send({ t: 'voice_error', text: '请先开启电脑端语音输入' });
          return;
        }
        this.cancelVoice();
        this.utterance = Number(msg.utterance) >>> 0;
        this.recording = true;
        this.voiceStarted = performance.now();
        this.audioSeq = 0;
        return;
      case 'voice_end':
        if (!this.recording || Number(msg.utterance) !== this.utterance) return;
        if (msg.frames !== this.audioSeq) {
          this.cancelVoice();
          this.transport?.send({ t: 'voice_error', text: '录音中断，请再说一次' });
        } else void this.finishVoice(Number(msg.utterance));
        return;
      case 'voice_cancel':
        if (Number(msg.utterance) === this.utterance) this.cancelVoice();
        return;
      case 'speaking':
        this.speaking = msg.on === true;
        return;
      case 'touch': case 'answer': case 'confirmed': case 'arrived': case 'interrupted': case 'text':
        this.port?.input(msg as PageMessage);
    }
  }

  private audio(id: number, seq: number, pcm: Buffer): void {
    if (!this.recording || id !== this.utterance || !this.settledOn('device')) return;
    if (seq !== this.audioSeq || this.bytes + pcm.length > VOICE_MAX_BYTES) {
      this.cancelVoice();
      this.transport?.send({ t: 'voice_error', text: '录音中断，请再说一次' });
      return;
    }
    this.audioSeq++;
    this.bytes += pcm.length;
    this.chunks.push(Buffer.from(pcm));
  }

  private cancelVoice(): void {
    this.voiceGeneration++;
    this.recording = false;
    this.chunks = [];
    this.bytes = 0;
  }

  private async finishVoice(id: number): Promise<void> {
    if (!this.recording || id !== this.utterance) return;
    this.recording = false;
    const generation = this.voiceGeneration, bytes = Buffer.concat(this.chunks, this.bytes);
    this.chunks = [];
    this.bytes = 0;
    if (bytes.length < VOICE_MIN_BYTES) {
      this.transport?.send({ t: 'voice_error', text: '没听清，请按住再说一次' });
      return;
    }
    this.transport?.send({ t: 'pet_command', command: { t: 'listen', phase: 'transcribing' } });
    try {
      const pcm = new Int16Array(bytes.length / 2);
      for (let i = 0; i < pcm.length; i++) pcm[i] = bytes.readInt16LE(i * 2);
      const result = await this.port!.transcribe(pcm);
      // A newer recording, a transfer or a disconnect makes this result stale.
      if (generation !== this.voiceGeneration || this.presence.owner !== 'device' || !this.presence.deviceOnline) return;
      if (result.error) throw new Error(result.error);
      this.transport?.send({ t: 'pet_command', command: { t: 'listen', phase: 'heard', text: result.text } });
      if (result.text.trim()) this.port?.heard(result.text.trim());
    } catch (e) {
      if (generation === this.voiceGeneration) this.report(e);
    }
  }

  /** Records a failure and tells the host log. */
  private fail(detail: string): void {
    this.error = detail;
    this.port?.log.warn(`Mosaico: ${detail}`);
  }

  /** Records a failure and shows it on the board. */
  private report(e: unknown): void {
    this.error = e instanceof Error ? e.message : String(e);
    this.transport?.send({ t: 'voice_error', text: this.error });
  }

  private loadEmotion(): string {
    const file = join(this.dir, 'emotion.json');
    if (!existsSync(file)) return this.emotion;
    try {
      const saved = JSON.parse(readFileSync(file, 'utf8'));
      if (typeof saved.face === 'string') return saved.face;
    } catch { /* an unreadable mood only costs the remembered face */ }
    return this.emotion;
  }

  private rememberEmotion(face: unknown): void {
    const board = typeof face === 'string' ? boardWord(face) : null;
    if (!board || !BOARD_FACES.has(board) || board === this.emotion) return;
    this.emotion = board;
    try { writeDurable(join(this.dir, 'emotion.json'), JSON.stringify({ face: board })); } catch (e) { this.report(e); }
  }
}
