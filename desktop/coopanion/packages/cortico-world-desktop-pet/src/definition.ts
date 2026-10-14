import { join } from 'node:path';
import type { Language } from 'cortico/core/language.ts';
import { runtimesRoot, modelsRoot } from 'cortico/paths.ts';
import type { WorldDefinition } from 'cortico/world.ts';
import { DESKTOP_PET_DEFAULTS, DESKTOP_PET_ID, type DesktopPetConfigSection, type PetSkin } from './config.ts';
import { DesktopPetWorld, modelsDirFor, type PetBotControls } from './world.ts';
import type { DescribeTool } from './status.ts';
import type { ModelLanguage } from './model-text.ts';
import type { Answer } from './cua/engine-ipc.ts';
import type { PetBodyRelay } from './relay.ts';
import { petText } from './i18n/index.ts';

/** The console keeps the bot's avatar here, in the deployment directory. */
const AVATAR_FILE = 'avatar.png';

export interface DesktopPetAssembly {
  /** Run controls for the menu header; see `PetBotControls`. */
  controls?: PetBotControls;
  /** Called with each World instance Core creates, for an app that calls `confirm` on it. */
  onCreate?(world: DesktopPetWorld): void;
  /** Called after the dressing page saves a look, with that look as saved. */
  onSkin?(skin: PetSkin): void;
  /** More directories of installed figure packs, besides `figures/` in the data directory. */
  packRoots?(): string[];
  /** Called after the bot changed its own settings (`pet_set`). */
  onBotChange?(): void;
  /** What the status bubble shows for a tool call; see `DescribeTool`. */
  describeTool?: DescribeTool;
  /** The language of what the bot reads from this World, read at each use; Chinese when absent. */
  modelLanguage?(): ModelLanguage;
  /** The language the bot is to talk to the person in; see `DesktopPetWorldOptions.replyLanguage`. */
  replyLanguage?(): string | null;
  /** The app language, what the person reads; see `DesktopPetWorldOptions.language`. */
  language?(): Language;
  /** Called with each answer to computer use's question in the pet's bubble. */
  onCuaAnswer?(answer: Answer): void;
  /**
   * Another place the body can be (src/relay.ts), made for each World instance Core creates. `dataDir` is the
   * deployment's own directory for the World (`<data>/desktop-pet`), for the relay's state.
   */
  relay?(ctx: PetRelayContext): PetBodyRelay | null;
}

/** What a relay is made with. */
export interface PetRelayContext {
  dataDir: string;
  /** The deployment's secrets (`.env`). */
  secret(name: string): string;
  /** The World's live config section. */
  cfg: DesktopPetConfigSection;
}

/** The definition, with what an embedding app lends the World. */
export function desktopPetDefinition(assembly: DesktopPetAssembly = {}): WorldDefinition<DesktopPetConfigSection> {
  return {
    id: DESKTOP_PET_ID,
    // read when Core assembles the Worlds, in the app language of that moment
    get label() { return petText(assembly.language?.()).console.label; },
    defaults: () => structuredClone(DESKTOP_PET_DEFAULTS),
    // ctx.cfg is the live `worlds.desktop-pet` section: hot keys are read at use
    create: (ctx) => {
      const world = new DesktopPetWorld({
        cfg: ctx.cfg,
        timezone: ctx.timezone,
        persist: (patch) => {
          ctx.persist(patch);
          // the whole look as it now is: a patch may carry only the part that changed
          if (patch.skin) assembly.onSkin?.(ctx.cfg.skin);
        },
        runtimesRoot,
        modelsDir: () => modelsDirFor(modelsRoot()),
        botName: ctx.botName,
        avatarFile: join(ctx.botDir, AVATAR_FILE),
        controls: assembly.controls,
        packRoots: () => [join(ctx.dataDir, 'figures'), ...assembly.packRoots?.() ?? []],
        packDir: () => join(ctx.dataDir, 'figures'),
        onBotChange: () => assembly.onBotChange?.(),
        describeTool: assembly.describeTool,
        modelLanguage: () => assembly.modelLanguage?.() ?? 'zh',
        replyLanguage: () => assembly.replyLanguage?.() ?? null,
        language: () => assembly.language?.() ?? 'zh',
        onCuaAnswer: (answer) => assembly.onCuaAnswer?.(answer),
        relay: assembly.relay?.({ dataDir: join(ctx.dataDir, DESKTOP_PET_ID), secret: (name) => ctx.secret(name), cfg: ctx.cfg }) ?? undefined,
      });
      assembly.onCreate?.(world);
      return world;
    },
  };
}

export const DESKTOP_PET = desktopPetDefinition();
