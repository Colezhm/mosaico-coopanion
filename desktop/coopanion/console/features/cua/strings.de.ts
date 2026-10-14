import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: 'Computersteuerung',
  enabled: 'Coo diesen Computer benutzen lassen',
  control: 'Maus und Tastatur erlauben',
  controlHint: 'Wenn aus, kann Coo nur auf den Bildschirm schauen.',
  permission: 'Wann du gefragt wirst',
  levels: { 'ask-each-turn': 'Jede Runde', 'ask-before-acting': 'Vor dem Handeln', 'ask-once': 'Einmal', 'never-ask': 'Nie' },
  levelHints: {
    'ask-each-turn': 'Fragt jede Runde, bevor es zum ersten Mal auf den Bildschirm schaut oder Maus und Tastatur benutzt.',
    'ask-before-acting': 'Schauen ohne Nachfrage; fragt jede Runde, bevor es zum ersten Mal Maus und Tastatur benutzt.',
    'ask-once': 'Schauen ohne Nachfrage; fragt einmal vor dem Handeln, danach nicht mehr, solange das Ja gilt.',
    'never-ask': 'Fragt nie.',
  },
  grant: 'Ein Ja gilt',
  grantSuffix: 'Minuten',
  grantBad: (min: number, max: number) => `Gib eine ganze Zahl von ${min} bis ${max} ein`,
  more: 'Weitere Einstellungen findest du im erweiterten Modus auf der Seite Computersteuerung.',
  saveFailed: (why: string) => `Nicht gespeichert: ${why}`,
  enabledLabel: 'Computer verwenden',
  controlLabel: 'Maus und Tastatur',
};
