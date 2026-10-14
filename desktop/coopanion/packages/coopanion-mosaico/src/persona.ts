/**
 * One persona per figure. Cortico puts the workspace's CONSTITUTION.md into the system prefix of
 * every new session; this keeps a copy for each figure under `personas/` and swaps the active file
 * when the figure changes, so Coo and the DeepSeek whale never share a self-description and each
 * keeps the edits made while it was active (a nickname, habits the person asked for).
 *
 * Coo's first persona is the app's own seed (core/seed/CONSTITUTION.md); the whale's is ours.
 */
import { copyFileSync, existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

export type Figure = 'coo' | 'whale';

export const PERSONA_FILE = 'CONSTITUTION.md';
const PERSONAS_DIR = 'personas';
/** Which figure CONSTITUTION.md currently belongs to. Missing means Coo, the seeded persona. */
const ACTIVE_FILE = '.active-figure';
export const WHALE_SEED = fileURLToPath(new URL('./personas/whale.md', import.meta.url));

export const figureOf = (skin: unknown): Figure =>
  skin && typeof skin === 'object' && (skin as { figure?: unknown }).figure === 'whale' ? 'whale' : 'coo';

export const FIGURE_NAMES: Record<Figure, string> = { coo: 'Coo', whale: '大肥鱼' };

function writeAtomic(file: string, text: string): void {
  writeFileSync(`${file}.tmp`, text);
  renameSync(`${file}.tmp`, file);
}

/**
 * Makes CONSTITUTION.md the persona of `figure`; `seeds` gives each figure's first persona. Returns
 * the figure it replaced, or null when it already belonged to `figure` (or there is no workspace yet).
 */
export function activatePersona(workspace: string, figure: Figure, seeds: Record<Figure, string>): Figure | null {
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
  writeAtomic(active, readFileSync(existsSync(saved) ? saved : seeds[figure], 'utf8'));
  writeAtomic(marker, `${figure}\n`);
  return current;
}

/** What the bot is told when its persona changed with the figure. */
export function personaNotice(from: Figure, to: Figure): string {
  const name = FIGURE_NAMES[to], was = FIGURE_NAMES[from];
  return `[形象] 你的自述 CONSTITUTION.md 已换成${name}的版本,下一个 session 起完整生效;在那之前就以${name}的名字、性子和口癖说话。${was}的自述原样存在 personas/${from}.md,换回来时会恢复。`;
}
