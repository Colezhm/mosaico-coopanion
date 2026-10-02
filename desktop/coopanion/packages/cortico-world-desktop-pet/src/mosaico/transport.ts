/** Paired TLS transport. The pet's loopback HTTP server is never exposed. */
import { createServer, type Server } from 'node:https';
import { timingSafeEqual, randomUUID } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import { WebSocketServer, type WebSocket } from 'ws';
import type { Message } from './coordinator.ts';
import { Bonjour, type ServiceConfig } from 'bonjour-service';
import { createDiscoverySocket } from './discovery.ts';
import { ClockFilter } from './clock.ts';

export interface Pairing { deviceId: string; token: string; certFile: string; keyFile: string; port: number; hostname?:string; advertise?:boolean }
export interface TransportHooks {
  connected(): void; disconnected(): void;
  message(msg: Message): void;
  audio(utterance: number, sequence: number, pcm: Buffer): void;
  error(message: string): void;
}
export class MosaicoTransport {
  private server: Server | null = null;
  private wss: WebSocketServer | null = null;
  private peer: WebSocket | null = null;
  private session = '';
  private seq = 0;
  private incoming = 0;
  private clockOffset = 0;
  private clockReady = false;
  private readonly clock = new ClockFilter();
  private announced = false;
  private pingAt = 0;
  private timer: NodeJS.Timeout | null = null;
  private lastSeen = 0;
  private burstAt = 0;
  private burstCount = 0;
  private audioBurstAt=0;
  private audioBurstCount=0;
  private bonjour:Bonjour|null=null;
  constructor(private readonly pairing: Pairing, private readonly hooks: TransportHooks) {}
  get ready(): boolean { return this.peer?.readyState === 1 && this.clockReady; }
  async start(): Promise<void> {
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(this.pairing.deviceId)) throw new Error('配对文件无效：deviceId 缺失或格式不对');
    if (!this.pairing.token) throw new Error('缺少 CORTICO_MOSAICO_TOKEN：运行 projects/coopanion/tools/pair.py 后把它写入部署的 .env');
    if (!/^[a-f0-9]{64}$/.test(this.pairing.token)) throw new Error('CORTICO_MOSAICO_TOKEN 格式不对：应为 64 位十六进制');
    this.server = createServer({ cert: readFileSync(this.pairing.certFile), key: readFileSync(this.pairing.keyFile), minVersion: 'TLSv1.2' }, (_req, res) => { res.writeHead(404); res.end(); });
    this.wss = new WebSocketServer({ noServer: true, maxPayload: 32 * 1024, perMessageDeflate: false });
    this.server.on('upgrade', (req, socket, head) => {
      const expected = Buffer.from(`Bearer ${this.pairing.token}`), actual = Buffer.from(req.headers.authorization ?? '');
      if (req.url !== '/mosaico/v1' || req.headers.origin || actual.length !== expected.length || !timingSafeEqual(actual, expected) || this.peer) { socket.destroy(); return; }
      this.wss!.handleUpgrade(req, socket, head, ws => this.accept(ws));
    });
    await new Promise<void>((resolve, reject) => { this.server!.once('error', reject); this.server!.listen(this.pairing.port, '0.0.0.0', () => { this.server!.off('error', reject); resolve(); }); });
    this.server.on('error', e => this.hooks.error(e.message));
    if(this.pairing.advertise!==false){
      const hostname=this.pairing.hostname||`coo-${this.pairing.deviceId}.local`;
      const reportError=(message:string)=>this.hooks.error(`局域网发现: ${message}`);
      let published=false;
      const socket=createDiscoverySocket(hostname,()=>published,reportError);
      // bonjour-service forwards these options to multicast-dns, which owns the socket.
      const options:Partial<ServiceConfig>&{socket:typeof socket}={socket};
      this.bonjour=new Bonjour(options,(error:Error)=>reportError(error.message));
      const service=this.bonjour.publish({name:`Mosaico Coo ${this.pairing.deviceId}`,type:'mosaico-coo',protocol:'tcp',port:this.pairing.port,host:hostname,txt:{device:this.pairing.deviceId,version:'1',path:'/mosaico/v1'}});
      service.once('up',()=>{published=true;});
    }
    this.timer = setInterval(() => {
      if (!this.peer) return;
      if (performance.now() - this.lastSeen > 6000) { this.peer.terminate(); return; }
      // Transfers pause once no quick sample remains in the window.
      if (this.clockReady && !this.clock.estimate(performance.now()).ready) {
        this.clockReady = false; this.send({ t: 'clock_quality', ready: false });
      }
      this.ping();
    }, 2000);
  }
  private accept(ws: WebSocket): void {
    this.peer = ws; this.session = randomUUID(); this.seq = this.incoming = 0; this.clockReady = false; this.clock.reset(); this.announced = false; this.lastSeen = performance.now();
    this.send({ t: 'hello', deviceId: this.pairing.deviceId });
    ws.on('message', (data, binary) => {
      const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data as ArrayBuffer);
      this.lastSeen = performance.now();
      if (binary) {
        if(performance.now()-this.audioBurstAt>1000){this.audioBurstAt=performance.now();this.audioBurstCount=0;}
        if(++this.audioBurstCount>80){ws.close(1008,'audio rate');return;}
        if (!this.clockReady || bytes.length < 14 || bytes.length > 1292 || bytes.toString('ascii', 0, 4) !== 'MPCM' || bytes.length % 2) return;
        this.hooks.audio(bytes.readUInt32LE(4), bytes.readUInt32LE(8), bytes.subarray(12)); return;
      }
      if (performance.now() - this.burstAt > 1000) { this.burstAt = performance.now(); this.burstCount = 0; }
      if (++this.burstCount > 150) { ws.close(1008, 'rate'); return; }
      try {
        const m = JSON.parse(bytes.toString()) as Message;
        if (m.v !== 1 || m.session !== this.session || !Number.isSafeInteger(m.seq) || Number(m.seq) <= this.incoming || typeof m.t !== 'string') return;
        this.incoming = Number(m.seq);
        if (m.t === 'clock_pong') {
          const now = performance.now();
          if (m.echo !== this.pingAt || typeof m.at !== 'number' || !Number.isFinite(m.at)) return;
          // Readiness and offset come from the quickest recent sample, so a slow
          // sample keeps the session ready while one within 100 ms is in the window.
          const clock = this.clock.add(this.pingAt, m.at, now);
          if (!clock.ready) { this.clockReady = false; this.send({ t: 'clock_quality', ready: false }); return; }
          this.clockOffset = clock.offset;
          this.clockReady = true;
          this.send({ t: 'clock_quality', ready: true, offset: this.clockOffset, rtt: clock.rtt });
          // A slow clock sample pauses transfer eligibility, not the WSS session.
          // Reannouncing here would discard capabilities that the board sends once
          // per connection and cancel an in-flight durable asset commit.
          if (!this.announced) { this.announced = true; this.hooks.connected(); }
          return;
        }
        if (typeof m.at === 'number') m.at -= this.clockOffset;
        this.hooks.message(m);
      } catch { /* malformed frames cannot change presence */ }
    });
    ws.on('error', e => this.hooks.error(e.message));
    ws.once('close', () => { if (this.peer === ws) { this.peer = null; this.clockReady = false; this.hooks.disconnected(); } });
    this.ping();
  }
  private ping(): void { this.pingAt = performance.now(); this.send({ t: 'clock_ping', at: this.pingAt }); }
  send(message: Message): boolean {
    if (!this.peer || this.peer.readyState !== 1 || this.peer.bufferedAmount > 128 * 1024) return false;
    const m: Message = { ...message, v: 1, session: this.session, seq: ++this.seq };
    if (typeof m.glowAt === 'number') m.glowAt += this.clockOffset;
    this.peer.send(JSON.stringify(m)); return true;
  }
  async stop(): Promise<void> {
    this.bonjour?.unpublishAll();this.bonjour?.destroy();this.bonjour=null;
    if (this.timer) clearInterval(this.timer); this.timer = null;
    this.peer?.terminate(); this.peer = null;
    this.wss?.close();
    if (this.server) await new Promise<void>(resolve => this.server!.close(() => resolve()));
    this.server = null;
  }
}
