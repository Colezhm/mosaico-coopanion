import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { DEFAULT_SETTINGS, loadSettings, patchSettings, saveSettings } from '../src/settings.ts';

describe('Mosaico settings', () => {
  it('reads the legacy config section once and keeps its own file from then on', () => {
    const dir = mkdtempSync(join(tmpdir(), 'mosaico-settings-'));
    const legacy = { enabled: true, pairingFile: '/p/pairing.json', autonomous: false };
    expect(loadSettings(dir, legacy)).toEqual(legacy);
    expect(JSON.parse(readFileSync(join(dir, 'settings.json'), 'utf8'))).toEqual(legacy);
    saveSettings(dir, { ...legacy, autonomous: true });
    expect(loadSettings(dir, { enabled: false }).autonomous).toBe(true);
  });

  it('starts switched off without a legacy section and takes only well-typed fields', () => {
    const dir = mkdtempSync(join(tmpdir(), 'mosaico-settings-'));
    expect(loadSettings(dir, undefined)).toEqual(DEFAULT_SETTINGS);
    expect(patchSettings(DEFAULT_SETTINGS, { enabled: 'yes', pairingFile: '/x', extra: 1 })).toEqual({ ...DEFAULT_SETTINGS, pairingFile: '/x' });
  });
});
