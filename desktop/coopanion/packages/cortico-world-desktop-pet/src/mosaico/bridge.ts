import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync, openSync, closeSync, fsyncSync } from 'node:fs';
import { basename, join } from 'node:path';
import { performance } from 'node:perf_hooks';
import { PresenceCoordinator, type Body, type CoordinatorOptions, type Message, type Presence } from './coordinator.ts';
import { MosaicoTransport, type Pairing } from './transport.ts';
import { AssetSync } from './assets.ts';
import { VOCAB } from '../script.ts';

/** Faces the board can show and carry across a transfer: every scripted expression plus the rig's own states. */
const BOARD_FACES = new Set([...VOCAB.filter(v => v.kind === 'expression').map(v => v.id), 'sleep', 'dizzy', 'dragged']);

/** Presence, readiness and asset bookkeeping cadence. */
const TICK_MS = 100;
/** A board recording longer than this never sent voice_end; drop it. */
const VOICE_TIMEOUT_MS = 32_000;
/** 30 s of 16 kHz mono PCM16. */
const VOICE_MAX_BYTES = 960_000;
/** 100 ms of 16 kHz mono PCM16; anything shorter is a stray press. */
const VOICE_MIN_BYTES = 3200;

/** Seconds since 1970: above any transfer count, inside the board's uint32 epoch until 2106. */
export const recoveryEpoch = (): number => Math.floor(Date.now() / 1000);

const wantsWhale = (skin: unknown): boolean =>
  !!skin && typeof skin === 'object' && 'figure' in skin && skin.figure === 'whale';

/** Replaces @p file atomically and durably: a crash leaves the old or the new content, never a torn file. */
function writeDurable(file: string, data: string): void {
  writeFileSync(`${file}.tmp`, data, { mode: 0o600 });
  const handle = openSync(`${file}.tmp`, 'r');
  try { fsyncSync(handle); } finally { closeSync(handle); }
  renameSync(`${file}.tmp`, file);
  if (process.platform !== 'win32') {
    const dir = openSync(join(file, '..'), 'r');
    try { fsyncSync(dir); } finally { closeSync(dir); }
  }
}

export interface BridgeHooks {
  sendDesktop(msg: Message): boolean;
  input(msg: Message): void;
  speech(text: string): void;
  transcribe(pcm: Int16Array): Promise<{ text: string; error?: string | null }>;
  voiceEnabled(): boolean;
  skin(): unknown;
  error(detail: string): void;
  status?(state: { owner: Body; phase: string; connected: boolean; ready: boolean }): void;
}

export class MosaicoBridge {
  readonly presence: PresenceCoordinator;
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
  private desktopBusy = false;
  private error = '';
  private deviceReady = false;
  private capabilities: Message | null = null;
  private emotion = 'neutral';
  private statusKey = '';
  private readonly assets: AssetSync;

  constructor(private readonly directory: string, private readonly hooks: BridgeHooks, private readonly autonomous: () => boolean) {
    this.assets = new AssetSync(
      m => this.transport?.send(m) ?? false,
      m => hooks.sendDesktop(m),
      ready => {
        this.deviceReady = ready;
        if (ready) this.error = '';
        this.presence?.setDeviceReady(ready && !!this.transport?.ready);
      },
      text => this.fail(text),
    );
    mkdirSync(directory, { recursive: true, mode: 0o700 });
    this.emotion = this.loadEmotion();
    const journal = join(directory, 'presence.json');
    const options: CoordinatorOptions = {
      now: () => performance.now(),
      save: state => writeDurable(journal, JSON.stringify(state)),
      send: (to, msg) => {
        const message = msg.t === 'transfer_arrive' ? { ...msg, emotion: this.emotion } : msg;
        if (to === 'device') return this.transport?.send(message) ?? false;
        // The desktop page schedules from its own clock; hand it a relative delay.
        return hooks.sendDesktop(typeof message.glowAt === 'number'
          ? { ...message, delayMs: Math.max(0, message.glowAt - performance.now()) }
          : message);
      },
      changed: () => {
        if (!this.settledOn('device')) this.cancelVoice();
        this.publishStatus();
      },
    };
    this.presence = this.openJournal(journal, options);
  }

  /** True when the body rests on @p body: owned there and not mid-transfer. */
  settledOn(body: Body): boolean {
    return this.presence.owner === body && !this.presence.transferring;
  }

  /** An unreadable journal cannot say where the body is. It is kept aside and
   * replaced by "desktop owns" at an epoch above any counter the board can hold,
   * so the board's next presence update hides its copy instead of ignoring it. */
  private openJournal(journal: string, options: CoordinatorOptions): PresenceCoordinator {
    try {
      const saved = existsSync(journal) ? JSON.parse(readFileSync(journal, 'utf8')) as Presence : undefined;
      return new PresenceCoordinator(options, saved);
    } catch (e) {
      const backup = `${journal}.corrupt-${Date.now()}`;
      renameSync(journal, backup);
      const presence = new PresenceCoordinator(options, { version: 1, owner: 'desktop', epoch: recoveryEpoch(), transfer: null });
      options.save(presence.snapshot());
      this.fail(`身体位置记录无法读取(${e instanceof Error ? e.message : String(e)}),已另存为 ${basename(backup)};Coo 归电脑所有,板端连接后会隐藏。`);
      return presence;
    }
  }

  async start(pairingFile: string, token: string): Promise<void> {
    const pairing = JSON.parse(readFileSync(pairingFile, 'utf8')) as Pairing;
    // Runtime secrets come from the host's deployment, never from the package or public config.
    pairing.token = token;
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
    this.publishStatus();
    this.timer = setInterval(() => this.tick(), TICK_MS);
  }

  private tick(): void {
    try {
      this.presence.setBusy(this.computerBusy || this.desktopBusy || this.recording || this.speaking);
      this.presence.setDeviceReady(this.deviceReady && this.transport!.ready);
      this.publishStatus();
      this.presence.tick(this.autonomous());
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

  state(): Record<string, unknown> {
    return {
      ...this.presence.snapshot(),
      connected: this.presence.deviceOnline,
      ready: this.presence.canUseDevice,
      error: this.error,
      recording: this.recording,
      capabilities: this.capabilities,
    };
  }

  private publishStatus(): void {
    const state = {
      owner: this.presence.owner,
      phase: this.presence.snapshot().transfer?.phase ?? 'idle',
      connected: this.presence.deviceOnline,
      ready: this.presence.canUseDevice,
    };
    const key = JSON.stringify(state);
    if (key !== this.statusKey) {
      this.statusKey = key;
      this.hooks.status?.(state);
    }
  }

  desktopConnected(): void {
    this.presence.connected('desktop');
    if (this.transport?.ready) this.startAssets();
  }

  desktopDisconnected(): void { this.presence.disconnected('desktop'); }

  /** Handles a message from the desktop page; returns true when it was Mosaico's to consume. */
  desktopMessage(msg: Message): boolean {
    if (msg.t === 'pet_emotion') {
      if (this.settledOn('desktop')) this.rememberEmotion(msg.face);
      return true;
    }
    if (msg.t === 'asset_exported') {
      this.assets.exported(msg);
      return true;
    }
    if (msg.t.startsWith('transfer_')) {
      this.presence.receive('desktop', msg.t === 'transfer_hidden' ? { ...msg, at: performance.now() } : msg);
      return true;
    }
    if (['text', 'touch', 'answer'].includes(msg.t)) this.presence.activity();
    return false;
  }

  setBusy(busy: boolean): void {
    this.desktopBusy = busy;
    if (busy) this.presence.activity();
  }

  private startAssets(): void {
    const skin = this.hooks.skin();
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

  syncSkin(): void { if (this.presence.deviceOnline) this.startAssets(); }

  send(msg: Message): boolean {
    if (this.presence.transferring) return false;
    if (this.presence.owner === 'desktop') return this.hooks.sendDesktop(msg);
    if (msg.t === 'thinking') this.setBusy(msg.on === true);
    return this.transport?.send({ t: 'pet_command', command: msg }) ?? false;
  }

  async transfer(to: Body): Promise<void> { await this.presence.request(to); }

  async beforeComputer(): Promise<void> {
    this.computerBusy = true;
    try { await this.transfer('desktop'); } catch (e) { this.computerBusy = false; throw e; }
  }

  computerFinished(): void {
    this.computerBusy = false;
    this.presence.activity();
  }

  private deviceMessage(msg: Message): void {
    if (msg.t === 'capabilities') {
      const first = !this.capabilities;
      this.capabilities = msg;
      // A whale skin waits for the board's formats before it can start syncing.
      if (first && wantsWhale(this.hooks.skin())) this.startAssets();
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
        if (!this.hooks.voiceEnabled()) {
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
        this.hooks.input(msg);
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
      const result = await this.hooks.transcribe(pcm);
      // A newer recording, a transfer or a disconnect makes this result stale.
      if (generation !== this.voiceGeneration || this.presence.owner !== 'device' || !this.presence.deviceOnline) return;
      if (result.error) throw new Error(result.error);
      this.transport?.send({ t: 'pet_command', command: { t: 'listen', phase: 'heard', text: result.text } });
      if (result.text.trim()) this.hooks.speech(result.text.trim());
    } catch (e) {
      if (generation === this.voiceGeneration) this.report(e);
    }
  }

  /** Records a failure and tells the host log. */
  private fail(detail: string): void {
    this.error = detail;
    this.hooks.error(detail);
  }

  /** Records a failure and shows it on the board. */
  private report(e: unknown): void {
    this.error = e instanceof Error ? e.message : String(e);
    this.transport?.send({ t: 'voice_error', text: this.error });
  }

  private loadEmotion(): string {
    const file = join(this.directory, 'emotion.json');
    if (!existsSync(file)) return this.emotion;
    try {
      const saved = JSON.parse(readFileSync(file, 'utf8'));
      if (typeof saved.face === 'string') return saved.face;
    } catch { /* an unreadable mood only costs the remembered face */ }
    return this.emotion;
  }

  private rememberEmotion(face: unknown): void {
    if (typeof face !== 'string' || !BOARD_FACES.has(face) || face === this.emotion) return;
    this.emotion = face;
    try { writeDurable(join(this.directory, 'emotion.json'), JSON.stringify({ face })); } catch (e) { this.report(e); }
  }

  async stop(): Promise<void> {
    if (this.timer) clearInterval(this.timer);
    this.cancelVoice();
    this.presence.stop();
    await this.transport?.stop();
  }
}
