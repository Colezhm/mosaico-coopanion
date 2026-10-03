import { mkdtempSync, readFileSync, writeFileSync, appendFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { FIGURE_BODIES, activatePersona, figureOf } from '../src/persona.ts';

const COO = '# 我是谁\n\n我叫 Coo。\n\n(伙伴给我起了名字「柯笔」。)\n';

function workspace(): string {
  const dir = mkdtempSync(join(tmpdir(), 'persona-'));
  writeFileSync(join(dir, 'CONSTITUTION.md'), COO);
  return dir;
}
const active = (dir: string) => readFileSync(join(dir, 'CONSTITUTION.md'), 'utf8');

describe('one persona per figure', () => {
  it('switches to the whale and keeps Coo, nickname included, for later', () => {
    const dir = workspace();
    expect(activatePersona(dir, 'whale')).toBe('coo');
    expect(active(dir)).toContain('我叫大肥鱼');
    expect(active(dir)).not.toContain('Coo');
    expect(readFileSync(join(dir, 'personas/coo.md'), 'utf8')).toBe(COO);
  });

  it('round-trips each figure with the edits made while it was active', () => {
    const dir = workspace();
    activatePersona(dir, 'whale');
    appendFileSync(join(dir, 'CONSTITUTION.md'), '\n伙伴喜欢我叫他主人。\n');
    expect(activatePersona(dir, 'coo')).toBe('whale');
    expect(active(dir)).toBe(COO);
    expect(activatePersona(dir, 'whale')).toBe('coo');
    expect(active(dir)).toContain('伙伴喜欢我叫他主人');
  });

  it('does nothing when the figure already owns the persona or there is no workspace', () => {
    const dir = workspace();
    expect(activatePersona(dir, 'coo')).toBeNull();
    expect(active(dir)).toBe(COO);
    expect(activatePersona(join(dir, 'missing'), 'whale')).toBeNull();
  });

  it('reads the figure from the skin and describes each body differently', () => {
    expect(figureOf({ figure: 'whale' })).toBe('whale');
    expect(figureOf({ figure: 'coo' })).toBe('coo');
    expect(figureOf(null)).toBe('coo');
    expect(FIGURE_BODIES.whale).toContain('鲸');
    expect(FIGURE_BODIES.coo).toContain('C 形');
  });
});
