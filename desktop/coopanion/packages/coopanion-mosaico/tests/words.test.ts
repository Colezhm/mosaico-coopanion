import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { BOARD_FACES, boardWord, boardWords, lastFace } from '../src/words.ts';

const PET_WEB = new URL('../../cortico-world-desktop-pet/web/', import.meta.url);
const vocab = (pack: string) => (JSON.parse(readFileSync(new URL(`${pack}/figure.json`, PET_WEB), 'utf8')) as { vocab: { id: string; kind: string }[] }).vocab;
const clips = (JSON.parse(readFileSync(new URL('../web/whale-deepseek.bin.json', import.meta.url), 'utf8')) as { clips: { name: string }[] }).clips.map((c) => c.name);

describe('the board vocabulary', () => {
  it('has a board word for every word the built-in figure packs know', () => {
    for (const pack of ['coo', 'whale']) {
      const missing = vocab(pack).map((w) => w.id).filter((id) => boardWord(id) === null);
      expect(missing, pack).toEqual([]);
    }
  });

  it('maps only to faces the whale atlas carries', () => {
    for (const face of BOARD_FACES) if (face !== 'listening' && face !== 'sulking') expect(clips, face).toContain(face);
  });

  it('keeps board words, maps near ones and drops the rest', () => {
    expect(boardWords(['happy', 'wave', 'scared', 'nonsense', 3])).toEqual(['happy', 'hop', 'flustered']);
  });

  it('remembers the last expression as the board names it', () => {
    expect(lastFace(['wave', 'happy', 'jump', 'scared'], new Set(['happy', 'scared']))).toBe('flustered');
    expect(lastFace(['wave', 'jump'], new Set(['happy']))).toBeNull();
  });
});
