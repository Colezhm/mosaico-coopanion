/**
 * One persona per figure. Cortico puts the workspace's CONSTITUTION.md into the
 * system prefix of every new session; this keeps a separate copy for each
 * figure under `personas/` and swaps the active file when the figure changes,
 * so Coo and the DeepSeek whale never share a self-description and each keeps
 * the edits made while it was active (a nickname, habits the person asked for).
 */
import { copyFileSync, existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

export type Figure = 'coo' | 'whale';

export const PERSONA_FILE = 'CONSTITUTION.md';
const PERSONAS_DIR = 'personas';
/** Which figure CONSTITUTION.md currently belongs to. Missing means Coo, the seeded persona. */
const ACTIVE_FILE = '.active-figure';
const SEED_DIR = fileURLToPath(new URL('./personas/', import.meta.url));

export const figureOf = (skin: unknown): Figure =>
  skin && typeof skin === 'object' && (skin as { figure?: unknown }).figure === 'whale' ? 'whale' : 'coo';

export const FIGURE_NAMES: Record<Figure, string> = { coo: 'Coo', whale: '大肥鱼' };

/** What the person sees on screen, for the World's environment prompt. */
export const FIGURE_BODIES: Record<Figure, string> = {
  coo: '一个小桌宠,C 形的身体,开口是嘴,两只 0 形的眼睛,底下两条短腿',
  whale: '一只 DeepSeek 大肥鱼:蓝色长发、戴女仆头饰、穿女仆装和白围裙的鲸鱼娘小桌宠,头侧有鲸鳍,身后拖着一条蓝色鲸鱼尾巴',
};

function writeAtomic(file: string, text: string): void {
  writeFileSync(`${file}.tmp`, text);
  renameSync(`${file}.tmp`, file);
}

/**
 * Makes CONSTITUTION.md the persona of @p figure. Returns the figure it replaced,
 * or null when it already belonged to @p figure (or there is no workspace yet).
 */
export function activatePersona(workspace: string, figure: Figure): Figure | null {
  const active = join(workspace, PERSONA_FILE);
  if (!existsSync(active)) return null;
  const dir = join(workspace, PERSONAS_DIR);
  mkdirSync(dir, { recursive: true });
  const marker = join(dir, ACTIVE_FILE);
  const current: Figure = existsSync(marker) && readFileSync(marker, 'utf8').trim() === 'whale' ? 'whale' : 'coo';
  if (current === figure) return null;
  // Keep the outgoing figure's persona with whatever it learned, then bring in the other one.
  copyFileSync(active, join(dir, `${current}.md`));
  const saved = join(dir, `${figure}.md`);
  writeAtomic(active, readFileSync(existsSync(saved) ? saved : join(SEED_DIR, `${figure}.md`), 'utf8'));
  writeAtomic(marker, `${figure}\n`);
  return current;
}
