import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  // advanced/features/prompts/index.ts
  button: "Effacer la conversation et recommencer",
  title: "Effacer la conversation actuelle et recommencer ?",
  body: "Enregistre les modifications de cette page, puis efface le contexte de la conversation actuelle de Coo et recommence avec le prompt système actualisé. L’effacement est irréversible. Memory et la persona sont conservés.",
  clearing: "Démarrage de la conversation…",
  cleared: "Nouvelle conversation démarrée",
  // release.ts
  repoHint: 'Ouvrir le projet Coopanion sur GitHub',
  update: (latest: string) => `Coopanion ${latest} est disponible : téléchargez la mise à jour`,
};
