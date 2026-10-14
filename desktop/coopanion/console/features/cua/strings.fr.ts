import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: "Contrôle de l'ordinateur",
  enabled: 'Laisser Coo utiliser cet ordinateur',
  control: 'Autoriser la souris et le clavier',
  controlHint: "Désactivé, Coo peut seulement regarder l'écran.",
  permission: 'Quand vous demander',
  levels: { 'ask-each-turn': 'À chaque tour', 'ask-before-acting': "Avant d'agir", 'ask-once': 'Une fois', 'never-ask': 'Jamais' },
  levelHints: {
    'ask-each-turn': "Demande à chaque tour avant de regarder l'écran ou d'utiliser la souris et le clavier pour la première fois.",
    'ask-before-acting': "Regarder se fait sans demander ; demande à chaque tour avant d'utiliser la souris et le clavier pour la première fois.",
    'ask-once': "Regarder se fait sans demander ; demande une fois avant d'agir, puis plus tant que le oui reste valable.",
    'never-ask': 'Ne demande jamais.',
  },
  grant: 'Un oui vaut',
  grantSuffix: 'minutes',
  grantBad: (min: number, max: number) => `Saisissez un nombre entier de ${min} à ${max}`,
  more: "D'autres réglages se trouvent sur la page Contrôle de l'ordinateur du mode avancé.",
  saveFailed: (why: string) => `Non enregistré : ${why}`,
  enabledLabel: "Utiliser l'ordinateur",
  controlLabel: 'Souris et clavier',
};
