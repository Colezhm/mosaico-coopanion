import { createHash, randomUUID } from 'node:crypto';
import type { Message } from './coordinator.ts';
import { validAnimationAtlas, ATLAS_LIMIT } from '../../web/atlas-codec.js';

const RECEIPT_TIMEOUT_MS = 2000;
// NOR erase/write plus validation of the full whale atlas takes longer than a
// network receipt. Keep its transaction alive until durable readiness arrives.
const CACHE_COMMIT_TIMEOUT_MS = 30000;
const receiptTimeout = (m: Message) => m.t === 'asset_commit' ? CACHE_COMMIT_TIMEOUT_MS : RECEIPT_TIMEOUT_MS;
/** The desktop page renders and exports the atlas; a stuck page fails the sync. */
const EXPORT_TIMEOUT_MS = 120_000;
/** Pause between acknowledged chunks: at most 20 chunks per second. */
const CHUNK_INTERVAL_MS = 50;
const CHUNK_BYTES = 4096;
const MAX_RETRIES = 3;

/** COO1: 192x216 pixels, 22 run-length poses of 5-byte runs after a 188-byte header. */
const COO1 = Object.freeze({ width: 192, height: 216, poses: 22, header: 188, run: 5, limit: 900 * 1024 });

export function validAtlas(bytes: Buffer): boolean {
  if (bytes.toString('ascii', 0, 4) === 'COO2') return validAnimationAtlas(bytes);
  if (bytes.length < COO1.header || bytes.length > COO1.limit || bytes.toString('ascii', 0, 4) !== 'COO1' ||
      bytes.readUInt16LE(4) !== COO1.width || bytes.readUInt16LE(6) !== COO1.height || bytes.readUInt16LE(8) !== COO1.poses) return false;
  const area = COO1.width * COO1.height;
  for (let i = 0; i < COO1.poses; i++) {
    const offset = bytes.readUInt32LE(12 + i * 8), length = bytes.readUInt32LE(16 + i * 8);
    if (offset < COO1.header || offset + length > bytes.length || length % COO1.run) return false;
    let pixels = 0;
    for (let at = offset; at < offset + length; at += COO1.run) {
      pixels += bytes.readUInt16LE(at);
      if (pixels > area) return false;
    }
    if (pixels !== area) return false;
  }
  return true;
}

/** Stop-and-wait 4 KiB chunks at <= 20 Hz; controls and audio keep their own budget. */
export class AssetSync {
  private id = '';
  private bytes: Buffer | null = null;
  private hash = '';
  private offset = 0;
  private deadline = 0;
  private retry = 0;
  private pending: Message | null = null;

  constructor(
    private readonly send: (m: Message) => boolean,
    private readonly render: (m: Message) => boolean,
    private readonly ready: (value: boolean) => void,
    private readonly error: (text: string) => void,
    private readonly now = () => performance.now(),
  ) {}

  start(skin: unknown): void {
    this.cancel();
    if (skin && typeof skin === 'object' && 'figure' in skin && skin.figure && skin.figure !== 'coo' && skin.figure !== 'whale') {
      this.error('Mosaico 暂不支持此形象');
      return;
    }
    this.id = randomUUID();
    if (!this.render({ t: 'asset_export', id: this.id, skin })) {
      this.fail('桌面角色窗口尚未连接');
      return;
    }
    this.deadline = this.now() + EXPORT_TIMEOUT_MS;
  }

  cancel(): void {
    this.ready(false);
    this.id = '';
    this.bytes = null;
    this.pending = null;
    this.offset = 0;
    this.retry = 0;
    this.deadline = 0;
  }

  exported(m: Message): void {
    if (m.id !== this.id) return;
    if (typeof m.data !== 'string' || m.data.length > Math.ceil(ATLAS_LIMIT / 3) * 4) {
      this.fail(String(m.error || '角色资源导出失败'));
      return;
    }
    const data = Buffer.from(m.data, 'base64');
    if (!validAtlas(data)) {
      this.fail('角色资源格式校验失败');
      return;
    }
    this.bytes = data;
    this.hash = createHash('sha256').update(data).digest('hex');
    this.issue({ t: 'asset_offer', id: this.id, sha256: this.hash, size: data.length });
  }

  /** Handles a board asset_* message; returns false for any other message. */
  receive(m: Message): boolean {
    if (!m.t.startsWith('asset_')) return false;
    if (m.id !== this.id) return true;
    if (m.t === 'asset_ready' && m.sha256 === this.hash) {
      this.ready(true);
      this.pending = null;
      this.deadline = 0;
    } else if (m.t === 'asset_error') {
      this.fail(String(m.error || '板端资源同步失败'));
    } else if (m.t === 'asset_next' && this.bytes && Number.isSafeInteger(m.offset) &&
               Number(m.offset) >= 0 && Number(m.offset) <= this.bytes.length) {
      this.offset = Number(m.offset);
      this.pending = null;
      this.retry = 0;
      this.deadline = this.now() + CHUNK_INTERVAL_MS;
    }
    return true;
  }

  tick(): void {
    if (!this.id || !this.deadline || this.now() < this.deadline) return;
    if (this.pending) {
      if (++this.retry > MAX_RETRIES) {
        this.fail('资源同步超时');
        return;
      }
      this.send(this.pending);
      this.deadline = this.now() + receiptTimeout(this.pending);
      return;
    }
    if (!this.bytes) {
      this.fail('桌面资源导出超时');
      return;
    }
    if (this.offset === this.bytes.length) this.issue({ t: 'asset_commit', id: this.id, sha256: this.hash });
    else this.issue({
      t: 'asset_chunk', id: this.id, offset: this.offset,
      data: this.bytes.subarray(this.offset, this.offset + CHUNK_BYTES).toString('base64'),
    });
  }

  private issue(m: Message): void {
    this.pending = m;
    this.retry = 0;
    this.send(m);
    this.deadline = this.now() + receiptTimeout(m);
  }

  private fail(text: string): void {
    this.cancel();
    this.error(text);
  }
}
