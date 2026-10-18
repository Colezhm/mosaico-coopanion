/**
 * The board's character atlas, made in the pet page from the desktop's own drawing.
 *
 * - Coo: COO1, 22 poses of 192×216 RGB565+alpha run-length pixels, drawn with the Coo pack's
 *   figure (web/coo/coo.js) in the skin's palette and accessories, dark-theme colours (the board's
 *   screen is dark).
 * - The whale: COO2, the animation atlas made from the whale rig ahead of time (whale-deepseek.bin,
 *   host/export-whale.cjs of 0.1.0-mosaico.7); every scheme uses it until the board has its outfits.
 */
import { figure, normalizeSkin, skinVars, COO_CSS } from '/web/coo/coo.js';
import { FACES, STAND } from '/web/kit/body.js';
import { base64, validAnimationAtlas } from './atlas-codec.js';

export const COO_POSES = ['neutral', 'happy', 'wink', 'love', 'shy', 'surprised', 'angry', 'sad', 'sleepy', 'sleep', 'dizzy', 'dragged', 'thinking', 'sit', ...Array.from({ length: 8 }, (_, i) => `walk${i}`)];
const W = 192, H = 216, HEADER = 188, LIMIT = 900 * 1024;

export async function exportAtlas(skin) {
  if (skin?.figure === 'whale') return whaleAtlas();
  return cooAtlas(skin);
}

async function whaleAtlas() {
  const response = await fetch(new URL('./whale-deepseek.bin', import.meta.url));
  if (!response.ok) throw new Error('大肥鱼预编译动画资源缺失，请重新安装桌面端');
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (!validAnimationAtlas(bytes)) throw new Error('大肥鱼动画资源损坏');
  return base64(bytes);
}

/** The SVG of one pose, as the pet page would draw it, in logo units. */
export function cooPose(name, skin) {
  let legs = STAND.map((l) => [...l]), low = 0, t = 0;
  if (name.startsWith('walk')) {
    t = Number(name.slice(4)) / 8;
    for (let i = 0; i < 2; i++) {
      legs[i][2] += Math.sin(t * Math.PI * 2 + i * Math.PI) * 25;
      legs[i][3] -= Math.max(0, Math.cos(t * Math.PI * 2 + i * Math.PI)) * 18;
    }
  }
  if (name === 'sit' || name === 'sleep') { low = 30; legs = legs.map((l) => [l[0], l[1] + 30, l[2] + 18, 256]); }
  const face = (FACES[name] || FACES.neutral).f(t);
  const drawn = figure(face, { look: [0, 0], legs, low, t, blink: 0, acc: skin, swing: 0 });
  return `<svg xmlns="http://www.w3.org/2000/svg" data-theme="dark" width="${W}" height="${H}" viewBox="-32 -48 320 360" style="${skinVars(skin, true)}"><style>${COO_CSS}</style>${drawn}</svg>`;
}

async function cooAtlas(raw) {
  const skin = normalizeSkin(raw);
  const canvas = document.createElement('canvas');
  canvas.width = W; canvas.height = H;
  const ctx = canvas.getContext('2d', { willReadFrequently: true }), chunks = [];
  for (const name of COO_POSES) {
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(cooPose(name, skin));
    await img.decode();
    ctx.clearRect(0, 0, W, H);
    ctx.drawImage(img, 0, 0);
    chunks.push(runs(ctx.getImageData(0, 0, W, H).data));
  }
  const length = HEADER + chunks.reduce((sum, a) => sum + a.length, 0);
  if (length > LIMIT) throw new Error('造型资源超过板端容量');
  const bytes = new Uint8Array(length), view = new DataView(bytes.buffer);
  bytes.set([67, 79, 79, 49]); // COO1
  view.setUint16(4, W, true); view.setUint16(6, H, true); view.setUint16(8, COO_POSES.length, true);
  let offset = HEADER;
  chunks.forEach((a, i) => { view.setUint32(12 + i * 8, offset, true); view.setUint32(16 + i * 8, a.length, true); bytes.set(a, offset); offset += a.length; });
  return base64(bytes);
}

/** RGBA pixels as 5-byte runs: count (u16), RGB565 (u16), alpha (u8); a transparent pixel is all zero. */
function runs(pixels) {
  const out = [];
  let previous = -1, count = 0;
  const flush = () => { if (count) out.push(count & 255, count >> 8, previous & 255, (previous >> 8) & 255, previous >>> 16); };
  for (let i = 0; i < pixels.length; i += 4) {
    const a = pixels[i + 3], rgb = ((pixels[i] >> 3) << 11) | ((pixels[i + 1] >> 2) << 5) | (pixels[i + 2] >> 3), value = a ? rgb | (a << 16) : 0;
    if (value === previous && count < 65535) count++;
    else { flush(); previous = value; count = 1; }
  }
  flush();
  return new Uint8Array(out);
}
