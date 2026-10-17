/**
 * Mosaico × Coopanion: the paired Mosaico board as a second place for the desktop pet's body.
 * The app lends it to the desktop pet World as its body relay:
 *
 *   desktopPetDefinition({ relay: (ctx) => createMosaicoRelay(ctx, { workspaceDir, cooSeed }), relayDescribe: MOSAICO_DESCRIBE })
 */
import type { PetRelayContext } from 'cortico-world-desktop-pet';
import { MosaicoRelay, type MosaicoRelayOptions } from './relay.ts';

export { MosaicoRelay, TOKEN_SECRET, type MosaicoRelayOptions } from './relay.ts';
export { DEFAULT_SETTINGS, type MosaicoSettings } from './settings.ts';
export { MOSAICO_DESCRIBE } from './model-text.ts';

export function createMosaicoRelay(ctx: PetRelayContext, opts: MosaicoRelayOptions = {}): MosaicoRelay {
  return new MosaicoRelay(ctx, opts);
}
