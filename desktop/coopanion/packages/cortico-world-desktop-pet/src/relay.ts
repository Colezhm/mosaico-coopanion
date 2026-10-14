/**
 * A second place the pet's body can be: a board on the desk, a phone, another screen. An embedding app
 * lends one through `DesktopPetAssembly.relay`; the World then asks the relay first with every order
 * for the body, and the relay decides whether the pet page or its own device carries it.
 *
 * The World stays the one World the bot sees: same tools, same events, same Memory. The relay only
 * moves the body. It hears what the pet page reports before the World does (a transfer's own
 * handshake), and feeds back what its device heard and felt as if the pet page had sent it.
 *
 * Nothing here is required beyond `start`, `stop`, `send` and `here`; a relay that leaves out the
 * rest gets the World's own behaviour for them.
 */
import type { Logger, ToolDef, TriggerMode } from 'cortico/core/types.ts';
import type { PetSkin } from './config.ts';
import type { PageMessage } from './server.ts';

/** What the World lends a relay at start. */
export interface PetBodyPort {
  /** Sends to the pet page as the World itself does; false when no page is connected. */
  sendPage(msg: PageMessage): boolean;
  /** Input from the relay's device, taken as if the pet page sent it (touch, answer, confirmed, text, arrived, interrupted). */
  input(msg: PageMessage): void;
  /** Speech the relay's device heard and had transcribed, delivered like the computer's microphone (an open question takes it as the answer). */
  heard(text: string): void;
  /** Transcribes 16 kHz mono PCM16 with the voice engine in force, starting it when it is not running. */
  transcribe(pcm: Int16Array): Promise<{ text: string; error?: string | null }>;
  /** Voice input is switched on. */
  voiceEnabled(): boolean;
  /** The look in force, as saved. */
  skin(): PetSkin;
  /** The words of the body's vocabulary in force: expressions and motions, by id. */
  words(): readonly { id: string; kind: string }[];
  /** An event for the bot, from the World (`source` desktop-pet). */
  push(type: string, text: string, trigger: TriggerMode): void;
  /** The World's log. */
  log: Logger;
}

/** A console panel the relay adds to `world:desktop-pet`: its declaration and its methods. */
export interface PetRelayPanel {
  id: string;
  title: string;
  description: string;
  /** Methods the console may call with GET; the rest are POST. */
  getMethods?: string[];
  invoke(method: string, args: unknown[]): Promise<unknown>;
}

export interface PetBodyRelay {
  /** Called at World start, before the pet page server opens: a relay that knows the body is away can say so to the first page. */
  start(port: PetBodyPort): Promise<void>;
  stop(): Promise<void>;
  /**
   * An order for the body (say, ask, confirm, act, walk, walk-stop, listen, status, dialog…). `true` or `false`: the
   * relay delivered it or could not; `undefined`: the pet page takes it as usual.
   */
  send(msg: PageMessage): boolean | undefined;
  /** False while the body is elsewhere: the computer's microphone and talk key stay off and its audio is dropped. */
  here(): boolean;
  /** True while the body elsewhere can show what the bot does, though no pet page is connected (the World keeps its output tap). */
  reachable?(): boolean;
  /** A pet page message before the World reads it; true when it was the relay's own. */
  fromPage?(msg: PageMessage): boolean;
  pageConnected?(): void;
  pageDisconnected?(): void;
  /** What the pet page is told at connect (`init.relay`): `module`, the URL of a page module under `/relay/`, and whatever it needs. */
  page?(): Record<string, unknown> | null;
  /** Directory served at `/relay/`, for the page module. */
  pageDir?: string;
  /** Why `tool` could not run, when the relay knows better than "no pet window"; null for the World's own text. */
  unavailable?(tool: string): string | null;
  /** Before computer use acts on this screen: resolves once the body is back on it. */
  beforeComputer?(): Promise<void>;
  /** The turn computer use acted in ended. */
  computerDone?(): void;
  /** Tools the relay adds to the World (prefix them `pet_`). */
  tools?(): ToolDef[];
  /** Text for `{{pet.relay}}` in the environment prompt; empty to leave it out. */
  envPrompt?(): string;
  /** A touch kind the pet page does not know (a board's own: hug, tickle): the event's text for `user`, or null to drop it. */
  touchText?(kind: string, count: number, user: string): string | null;
  panel?: PetRelayPanel;
}
