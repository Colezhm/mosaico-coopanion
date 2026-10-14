/**
 * The board's vocabulary. Firmware plays motions by name (coop_state.c `coop_state_action`) and
 * shows any other word as a face from the atlas (web/whale-deepseek.bin.json, the Coo atlas'
 * poses), falling back to neutral. Words the desktop figure packs know but the board does not
 * map to the nearest board word here, so the firmware needs no change when a pack adds words.
 */

/** Words the firmware plays as motions. */
const BOARD_MOTIONS = new Set([
  'walk', 'run', 'jump', 'hop', 'sit', 'sleep', 'stand', 'bubbles', 'sulk', 'turn', 'cry', 'stumble', 'fall',
  'sway', 'shake', 'nod', 'spin', 'dizzy', 'look',
]);

/** Faces the board's atlases carry. */
export const BOARD_FACES = new Set([
  'neutral', 'happy', 'wink', 'love', 'shy', 'surprised', 'angry', 'sad', 'sleepy', 'sleep', 'dizzy', 'dragged',
  'thinking', 'listening', 'worried', 'furious', 'smug', 'pleading', 'curious', 'excited', 'crying', 'pout',
  'flustered', 'delighted', 'cheeky', 'sulking',
]);

/** Desktop words the board has no clip for, and the board word closest to them. */
const NEAREST: Record<string, string> = {
  determined: 'smug',
  scared: 'flustered',
  confused: 'curious',
  wave: 'hop',
  flap: 'hop',
  bow: 'nod',
  shiver: 'shake',
  dance: 'spin',
};

/** The board's word for `word`, or null when the board has nothing like it. */
export function boardWord(word: string): string | null {
  if (BOARD_MOTIONS.has(word) || BOARD_FACES.has(word)) return word;
  return NEAREST[word] ?? null;
}

/** `words` in the board's vocabulary, dropping what it has nothing like. */
export function boardWords(words: readonly unknown[]): string[] {
  const out: string[] = [];
  for (const w of words) {
    const b = typeof w === 'string' ? boardWord(w) : null;
    if (b) out.push(b);
  }
  return out;
}

/** The face the board should keep after these words: the last expression among them, as the board names it. */
export function lastFace(words: readonly unknown[], expressions: ReadonlySet<string>): string | null {
  for (let i = words.length - 1; i >= 0; i--) {
    const w = words[i];
    if (typeof w !== 'string' || !expressions.has(w)) continue;
    const b = boardWord(w);
    if (b && BOARD_FACES.has(b)) return b;
  }
  return null;
}
