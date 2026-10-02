import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync, openSync, closeSync, fsyncSync } from 'node:fs';
import { basename, join } from 'node:path';
import { performance } from 'node:perf_hooks';
import { PresenceCoordinator, type Body, type CoordinatorOptions, type Message, type Presence } from './coordinator.ts';
import { MosaicoTransport, type Pairing } from './transport.ts';
import { AssetSync } from './assets.ts';

/** Seconds since 1970: above any transfer count, inside the board's uint32 epoch until 2106. */
export const recoveryEpoch = (): number => Math.floor(Date.now() / 1000);
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
  private capabilities:Message|null=null;
  private emotion='neutral';
  private statusKey = '';
  private readonly assets: AssetSync;

  constructor(private readonly directory: string, private readonly hooks: BridgeHooks, private readonly autonomous: () => boolean) {
    this.assets=new AssetSync(m=>this.transport?.send(m)??false,m=>hooks.sendDesktop(m),ready=>{this.deviceReady=ready;if(ready)this.error='';this.presence?.setDeviceReady(ready&&!!this.transport?.ready);},text=>{this.error=text;hooks.error(text);});
    mkdirSync(directory, { recursive: true, mode: 0o700 });
    const emotionFile=join(directory,'emotion.json');
    if(existsSync(emotionFile)){try{const e=JSON.parse(readFileSync(emotionFile,'utf8'));if(typeof e.face==='string')this.emotion=e.face;}catch{}}
    const journal = join(directory, 'presence.json');
    const options: CoordinatorOptions = {
      now: () => performance.now(),
      save: state => {
        writeFileSync(`${journal}.tmp`, JSON.stringify(state), { mode: 0o600 });
        const file=openSync(`${journal}.tmp`,'r');try{fsyncSync(file);}finally{closeSync(file);}
        renameSync(`${journal}.tmp`, journal);
        if(process.platform!=='win32'){const dir=openSync(directory,'r');try{fsyncSync(dir);}finally{closeSync(dir);}}
      },
      send: (to, msg) => {const message=msg.t==='transfer_arrive'?{...msg,emotion:this.emotion}:msg;return to === 'desktop' ? hooks.sendDesktop(typeof message.glowAt === 'number' ? { ...message, delayMs: Math.max(0, message.glowAt - performance.now()) } : message) : this.transport?.send(message) ?? false;},
      changed: () => { if (this.presence.transferring || this.presence.owner !== 'device') this.cancelVoice(); this.publishStatus(); },
    };
    this.presence = this.openJournal(journal, options);
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
      this.error = `身体位置记录无法读取(${e instanceof Error ? e.message : String(e)}),已另存为 ${basename(backup)};Coo 归电脑所有,板端连接后会隐藏。`;
      this.hooks.error(this.error);
      return presence;
    }
  }
  async start(pairingFile: string, token: string): Promise<void> {
    const pairing = JSON.parse(readFileSync(pairingFile, 'utf8')) as Pairing;
    // Runtime secrets come from the host's deployment, never from the package or public config.
    pairing.token = token;
    this.transport = new MosaicoTransport(pairing, {
      connected: () => { this.capabilities=null;this.presence.connected('device', false); this.startAssets(); this.publishStatus(); },
      disconnected: () => { this.assets.cancel(); this.deviceReady = false; this.cancelVoice(); this.presence.disconnected('device'); this.publishStatus(); },
      message: msg => this.deviceMessage(msg),
      audio: (id, seq, pcm) => this.audio(id, seq, pcm),
      error: detail => { this.error = detail; this.hooks.error(detail); },
    });
    await this.transport.start();
    this.publishStatus();
    this.timer = setInterval(() => {
      try {
      this.presence.setBusy(this.computerBusy || this.desktopBusy || this.recording || this.speaking);
      this.presence.setDeviceReady(this.deviceReady && this.transport!.ready);
      this.publishStatus();
      this.presence.tick(this.autonomous());
      this.assets.tick();
      if (this.recording && performance.now() - this.voiceStarted > 32_000) {this.cancelVoice();this.transport?.send({t:'voice_error',text:'录音没有完整结束，请再说一次'});}
      }catch(e){const detail=String(e);if(this.error!==detail){this.error=detail;this.hooks.error(detail);}}
    }, 100);
  }
  state(): Record<string, unknown> { return { ...this.presence.snapshot(), connected: this.presence.deviceOnline, ready: this.presence.canUseDevice, error: this.error, recording: this.recording,capabilities:this.capabilities }; }
  private publishStatus(): void {
    const state = { owner: this.presence.owner, phase: this.presence.snapshot().transfer?.phase ?? 'idle', connected: this.presence.deviceOnline, ready: this.presence.canUseDevice };
    const key = JSON.stringify(state);
    if (key !== this.statusKey) { this.statusKey = key; this.hooks.status?.(state); }
  }
  desktopConnected(): void { this.presence.connected('desktop');if(this.transport?.ready)this.startAssets(); }
  desktopDisconnected(): void { this.presence.disconnected('desktop'); }
  desktopMessage(msg: Message): boolean {
    if(msg.t==='pet_emotion'){if(this.presence.owner==='desktop'&&!this.presence.transferring)this.rememberEmotion(msg.face);return true;}
    if(msg.t==='asset_exported'){this.assets.exported(msg);return true;}
    if (msg.t.startsWith('transfer_')) { this.presence.receive('desktop', msg.t === 'transfer_hidden' ? { ...msg, at: performance.now() } : msg); return true; }
    if (['text', 'touch', 'answer'].includes(msg.t)) this.presence.activity();
    return false;
  }
  setBusy(busy: boolean): void { this.desktopBusy = busy; if (busy) this.presence.activity(); }
  private startAssets():void {
    const skin=this.hooks.skin();
    if(skin&&typeof skin==='object'&&'figure' in skin&&skin.figure==='whale'){
      this.assets.cancel();
      if(!this.capabilities)return; // The board advertises formats after clock synchronization.
      if(!Array.isArray(this.capabilities.assetFormats)||!this.capabilities.assetFormats.includes('COO2')){
        this.error='大肥鱼需要 Mosaico 1.1.0 或更新固件，请先更新板端';this.hooks.error(this.error);return;
      }
    }
    this.assets.start(skin);
  }
  syncSkin(): void { if(this.presence.deviceOnline)this.startAssets(); }
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
  computerFinished(): void { this.computerBusy = false; this.presence.activity(); }
  private deviceMessage(msg: Message): void {
    if(msg.t==='capabilities'){const first=!this.capabilities;this.capabilities=msg;const skin=this.hooks.skin();if(first&&skin&&typeof skin==='object'&&'figure' in skin&&skin.figure==='whale')this.startAssets();return;}
    if(this.assets.receive(msg))return;
    if (msg.t.startsWith('transfer_')) { this.presence.receive('device', msg); return; }
    if (msg.t === 'battery') { this.presence.setBattery(Number(msg.percent)); return; }
    if (msg.t === 'summon') { void this.transfer('device').catch(e => this.report(e)); return; }
    if (msg.t === 'return') { void this.transfer('desktop').catch(e => this.report(e)); return; }
    if (this.presence.owner !== 'device' || this.presence.transferring) return;
    if(msg.t==='pet_emotion'){this.rememberEmotion(msg.face);return;}
    this.presence.activity();
    if (msg.t === 'voice_start') {
      if (!this.hooks.voiceEnabled()) { this.transport?.send({ t: 'voice_error', text: '请先开启电脑端语音输入' }); return; }
      this.cancelVoice(); this.utterance = Number(msg.utterance) >>> 0;
      this.recording = true; this.voiceStarted = performance.now(); this.audioSeq = 0;
    } else if (msg.t === 'voice_end') {
      if(!this.recording || Number(msg.utterance)!==this.utterance)return;
      if(msg.frames!==this.audioSeq){this.cancelVoice();this.transport?.send({t:'voice_error',text:'录音中断，请再说一次'});}
      else void this.finishVoice(Number(msg.utterance));
    }
    else if (msg.t === 'voice_cancel' && Number(msg.utterance)===this.utterance) this.cancelVoice();
    else if (msg.t === 'speaking') this.speaking = msg.on === true;
    else if (['touch', 'answer', 'confirmed', 'arrived', 'interrupted', 'text'].includes(msg.t)) this.hooks.input(msg);
  }
  private audio(id: number, seq: number, pcm: Buffer): void {
    if (!this.recording || id !== this.utterance || this.presence.owner !== 'device' || this.presence.transferring) return;
    if (seq !== this.audioSeq || this.bytes + pcm.length > 960_000) { this.cancelVoice(); this.transport?.send({ t: 'voice_error', text: '录音中断，请再说一次' }); return; }
    this.audioSeq++; this.bytes += pcm.length; this.chunks.push(Buffer.from(pcm));
  }
  private cancelVoice(): void { this.voiceGeneration++; this.recording = false; this.chunks = []; this.bytes = 0; }
  private async finishVoice(id: number): Promise<void> {
    if (!this.recording || id !== this.utterance) return;
    this.recording = false;
    const generation = this.voiceGeneration, bytes = Buffer.concat(this.chunks, this.bytes);
    this.chunks = []; this.bytes = 0;
    if (bytes.length < 3200) { this.transport?.send({ t: 'voice_error', text: '没听清，请按住再说一次' }); return; }
    this.transport?.send({ t: 'pet_command', command: { t: 'listen', phase: 'transcribing' } });
    try {
      const pcm = new Int16Array(bytes.length / 2);
      for (let i = 0; i < pcm.length; i++) pcm[i] = bytes.readInt16LE(i * 2);
      const result = await this.hooks.transcribe(pcm);
      if (generation !== this.voiceGeneration || this.presence.owner !== 'device' || !this.presence.deviceOnline) return;
      if (result.error) throw new Error(result.error);
      this.transport?.send({ t: 'pet_command', command: { t: 'listen', phase: 'heard', text: result.text } });
      if (result.text.trim()) this.hooks.speech(result.text.trim());
    } catch (e) { if (generation === this.voiceGeneration) this.report(e); }
  }
  private report(e: unknown): void { this.error = e instanceof Error ? e.message : String(e); this.transport?.send({ t: 'voice_error', text: this.error }); }
  private rememberEmotion(face:unknown):void {
    if(typeof face!=='string'||!['neutral','happy','wink','love','shy','surprised','angry','sad','sleepy','sleep','dizzy','dragged','thinking'].includes(face)||face===this.emotion)return;
    this.emotion=face;
    try{const file=join(this.directory,'emotion.json');writeFileSync(file+'.tmp',JSON.stringify({face}),{mode:0o600});renameSync(file+'.tmp',file);}catch(e){this.report(e);}
  }
  async stop(): Promise<void> { if (this.timer) clearInterval(this.timer); this.cancelVoice(); this.presence.stop(); await this.transport?.stop(); }
}
