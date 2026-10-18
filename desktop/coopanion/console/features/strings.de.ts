import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  // advanced/features/prompts/index.ts
  button: "Gespräch löschen und neu starten",
  title: "Aktuelles Gespräch löschen und neu starten?",
  body: "Speichert Änderungen dieser Seite, löscht dann Coos aktuellen Gesprächskontext und startet mit dem aktualisierten Systemprompt neu. Das Löschen lässt sich nicht rückgängig machen. Memory und Persona bleiben erhalten.",
  clearing: "Neues Gespräch wird gestartet…",
  cleared: "Neues Gespräch gestartet",
  // release.ts
  repoHint: 'Das Coopanion-Projekt auf GitHub öffnen',
  update: (latest: string) => `Coopanion ${latest} ist da: Update herunterladen`,
};
