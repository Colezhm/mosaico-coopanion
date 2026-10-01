/** Single owner, durable cross-screen hand-off. No timeout may create a second body. */
export type Body = 'desktop' | 'device';
export type Phase = 'idle' | 'preparing' | 'departing' | 'arriving' | 'recovering';
export interface Transfer { id: string; epoch: number; from: Body; to: Body; phase: Phase }
export interface Presence { version: 1; owner: Body; epoch: number; transfer: Transfer | null }
export interface Message { t: string; [key: string]: unknown }
export interface CoordinatorOptions {
  now(): number;
  send(to: Body, message: Message): boolean;
  save(state: Presence): void;
  changed?(state: Presence): void;
  random?(): number;
}
export const TIMING = Object.freeze({ open: 300, depart: 650, gap: 500, glow: 500, arrive: 600, settle: 200 });
const copy = (s: Presence): Presence => structuredClone(s);

export class PresenceCoordinator {
  private state: Presence;
  private durable: Presence;
  private online: Record<Body, boolean> = { desktop: false, device: false };
  private ready = false;
  private deadline = 0;
  private waiters: Array<{ resolve(): void; reject(error: Error): void }> = [];
  private lastActivity: number;
  private lastAttempt: number;
  private lastTransfer = -Infinity;
  private blocked = false;
  private battery = 100;

  constructor(private readonly opts: CoordinatorOptions, saved?: Presence) {
    this.state = saved ? copy(saved) : { version: 1, owner: 'desktop', epoch: 0, transfer: null };
    if (this.state.version !== 1 || !['desktop', 'device'].includes(this.state.owner) || !Number.isSafeInteger(this.state.epoch)||this.state.epoch<0) throw new Error('Invalid presence journal');
    const tr=this.state.transfer;
    if(tr&&(typeof tr.id!=='string'||!tr.id||tr.id.length>63||tr.epoch!==this.state.epoch||!['desktop','device'].includes(tr.from)||!['desktop','device'].includes(tr.to)||tr.from===tr.to||!['preparing','departing','arriving','recovering'].includes(tr.phase)))throw new Error('Invalid transfer journal');
    if (this.state.transfer) this.state.transfer.phase = 'recovering';
    this.durable=copy(this.state);
    this.lastActivity = this.lastAttempt = opts.now();
  }
  snapshot(): Presence { return copy(this.state); }
  get owner(): Body { return this.state.owner; }
  get transferring(): boolean { return this.state.transfer !== null; }
  get deviceOnline(): boolean { return this.online.device; }
  get canUseDevice(): boolean { return this.online.device && this.ready; }
  private persist(): void {
    try{this.opts.save(copy(this.state));this.durable=copy(this.state);}
    catch(error){this.state=copy(this.durable);throw error;}
    this.opts.changed?.(copy(this.state));
  }
  private publish(): void {
    const message = { t: 'presence', ...this.snapshot() };
    this.opts.send('desktop', message); this.opts.send('device', message);
  }
  connected(body: Body, ready = true): void {
    this.online[body] = true;
    if (body === 'device') this.ready = ready;
    this.opts.send(body, { t: 'presence', ...this.snapshot() });
    if (this.transferring) this.queryTransfer();
  }
  disconnected(body: Body): void {
    this.online[body] = false;
    if (body === 'device') this.ready = false;
    const tr = this.state.transfer;
    if (!tr) return;
    if (tr.phase === 'preparing') this.cancel('连接中断，未开始传送');
    else this.recover('传送中断，等待设备重连核对');
  }
  setDeviceReady(ready: boolean): void { const changed=ready!==this.ready;this.ready = ready;if(changed&&ready&&this.state.transfer?.phase==='recovering')this.queryTransfer(); }
  activity(): void { this.lastActivity = this.opts.now(); }
  setBusy(busy: boolean): void { this.blocked = busy; if (busy) this.activity(); }
  setBattery(percent: number): void { if (Number.isFinite(percent)) this.battery = percent; }

  async request(to: Body): Promise<void> {
    this.activity();
    if (this.state.transfer) {
      if (this.state.transfer.to !== to) throw new Error('已有另一方向的传送正在进行');
      if (this.state.transfer.phase === 'recovering') throw new Error('传送待恢复，请先连接两端');
      return new Promise((resolve, reject) => this.waiters.push({ resolve, reject }));
    }
    if (to === this.state.owner) return;
    if (!this.online.desktop || !this.canUseDevice) throw new Error('两端尚未就绪：请检查连接、资源和时间同步');
    this.state.epoch++;
    const id = `transfer-${this.state.epoch}`;
    this.state.transfer = { id, epoch: this.state.epoch, from: this.state.owner, to, phase: 'preparing' };
    this.persist();
    const result = new Promise<void>((resolve, reject) => this.waiters.push({ resolve, reject }));
    this.deadline = this.opts.now() + 5000;
    this.publish();
    if (!this.opts.send(to, { t: 'transfer_prepare', ...this.state.transfer })) this.cancel('目标设备未连接');
    return result;
  }

  receive(body: Body, msg: Message): void {
    const tr = this.state.transfer;
    if (!tr || msg.id !== tr.id || msg.epoch !== tr.epoch) return;
    if (msg.t === 'transfer_ready' && body === tr.to && tr.phase === 'preparing') {
      tr.phase = 'departing'; this.persist(); this.deadline = this.opts.now() + 5000;
      this.publish();
      if (!this.opts.send(tr.from, { t: 'transfer_depart', ...tr, timing: TIMING })) this.recover('源端离线');
    } else if (msg.t === 'transfer_hidden' && body === tr.from && tr.phase === 'departing') {
      // Source persists hidden before acknowledging. Destination is now the only possible owner.
      this.state.owner = tr.to; tr.phase = 'arriving'; this.persist();
      this.deadline = this.opts.now() + 7000;
      const hiddenAt = typeof msg.at === 'number' && Number.isFinite(msg.at) ? Math.min(this.opts.now(), msg.at) : this.opts.now();
      this.opts.send(tr.to, { t: 'transfer_arrive', ...tr, glowAt: hiddenAt + TIMING.gap, timing: TIMING });
    } else if (msg.t === 'transfer_arrived' && body === tr.to && tr.phase === 'arriving') {
      this.finish();
    } else if (msg.t === 'transfer_report' && tr.phase === 'recovering') {
      // Reconciliation never guesses from a missing acknowledgement. Source must prove hidden.
      if (body === tr.from && msg.hidden === true && this.online[tr.to] && this.canUseDevice) {
        this.state.owner = tr.to; tr.phase = 'arriving'; this.persist(); this.deadline = this.opts.now() + 7000;
        this.opts.send(tr.to, { t: 'transfer_arrive', ...tr, glowAt: this.opts.now() + TIMING.gap, timing: TIMING });
      } else if (body === tr.from && msg.hidden === false && this.state.owner === tr.from && this.online[tr.to]) {
        // A source that has never hidden may safely abort; target was never granted ownership.
        this.cancel('传送已恢复到出发位置');
      }
    }
  }
  private finish(): void {
    this.state.transfer = null; this.lastTransfer = this.opts.now(); this.persist(); this.publish();
    for (const waiter of this.waiters.splice(0)) waiter.resolve();
  }
  private cancel(reason: string): void {
    this.state.transfer = null; this.persist(); this.publish();
    for (const waiter of this.waiters.splice(0)) waiter.reject(new Error(reason));
  }
  private recover(reason: string): void {
    if (this.state.transfer) { this.state.transfer.phase = 'recovering'; this.persist(); this.publish(); }
    this.queryTransfer();
    for (const waiter of this.waiters.splice(0)) waiter.reject(new Error(reason));
  }
  private queryTransfer(): void {
    for(const body of ['desktop','device'] as const)if(this.online[body])this.opts.send(body,{t:'transfer_status',transfer:this.state.transfer});
  }
  tick(autonomy: boolean): void {
    const now = this.opts.now(), tr = this.state.transfer;
    if (tr && tr.phase !== 'recovering' && now >= this.deadline) {
      if (tr.phase === 'preparing') this.cancel('目标准备超时'); else this.recover('传送回执超时，等待核对');
    }
    if (now - this.lastAttempt < 60_000) return;
    this.lastAttempt = now;
    if (!autonomy || this.blocked || this.transferring || this.owner !== 'desktop' || !this.canUseDevice || !this.online.desktop || this.battery < 15) return;
    if (now - this.lastActivity < 120_000 || now - this.lastTransfer < 600_000) return;
    if ((this.opts.random ?? Math.random)() < .1) void this.request('device').catch(() => {});
  }
  stop(): void { for (const waiter of this.waiters.splice(0)) waiter.reject(new Error('扩展已停止')); }
}
