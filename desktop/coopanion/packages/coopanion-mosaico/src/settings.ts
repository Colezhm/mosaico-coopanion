/**
 * Mosaico's settings, kept in its own file in the deployment (`<data>/desktop-pet/mosaico/settings.json`)
 * so no upstream config schema has to know them. Versions up to 0.1.0-mosaico.7 kept them in
 * `worlds.desktop-pet.mosaico`; the first start reads them from there once.
 */
import { closeSync, existsSync, fsyncSync, openSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

export interface MosaicoSettings {
  /** Use the paired board as a second place for the body. Applies at the next start. */
  enabled: boolean;
  /** The `pairing.json` that tools/pair.py made. Applies at the next start. */
  pairingFile: string;
  /** The body may go to the board by itself when nothing happens on the computer for a while. */
  autonomous: boolean;
}

export const DEFAULT_SETTINGS: MosaicoSettings = { enabled: false, pairingFile: '', autonomous: true };
const FILE = 'settings.json';

/** Replaces `file` atomically and durably: a crash leaves the old or the new content, never a torn file. */
export function writeDurable(file: string, data: string): void {
  writeFileSync(`${file}.tmp`, data, { mode: 0o600 });
  const handle = openSync(`${file}.tmp`, 'r');
  try { fsyncSync(handle); } finally { closeSync(handle); }
  renameSync(`${file}.tmp`, file);
  if (process.platform !== 'win32') {
    const dir = openSync(join(file, '..'), 'r');
    try { fsyncSync(dir); } finally { closeSync(dir); }
  }
}

function clean(raw: unknown): Partial<MosaicoSettings> {
  if (!raw || typeof raw !== 'object') return {};
  const r = raw as Record<string, unknown>;
  return {
    ...(typeof r.enabled === 'boolean' ? { enabled: r.enabled } : {}),
    ...(typeof r.pairingFile === 'string' ? { pairingFile: r.pairingFile } : {}),
    ...(typeof r.autonomous === 'boolean' ? { autonomous: r.autonomous } : {}),
  };
}

/** The saved settings; the legacy config section on first start, written to the file at once. */
export function loadSettings(dir: string, legacy: unknown): MosaicoSettings {
  const file = join(dir, FILE);
  if (existsSync(file)) return { ...DEFAULT_SETTINGS, ...clean(JSON.parse(readFileSync(file, 'utf8'))) };
  const settings = { ...DEFAULT_SETTINGS, ...clean(legacy) };
  saveSettings(dir, settings);
  return settings;
}

export function saveSettings(dir: string, settings: MosaicoSettings): void {
  writeDurable(join(dir, FILE), JSON.stringify(settings, null, 2));
}

/** `patch` applied to `settings`, taking only well-typed fields. */
export function patchSettings(settings: MosaicoSettings, patch: unknown): MosaicoSettings {
  return { ...settings, ...clean(patch) };
}
