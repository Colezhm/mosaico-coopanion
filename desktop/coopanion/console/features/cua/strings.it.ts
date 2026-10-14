import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: 'Controllo del computer',
  enabled: 'Lascia che Coo usi questo computer',
  control: 'Consenti mouse e tastiera',
  controlHint: 'Se disattivato, Coo può solo guardare lo schermo.',
  permission: 'Quando chiederti',
  levels: { 'ask-each-turn': 'Ogni turno', 'ask-before-acting': 'Prima di agire', 'ask-once': 'Una volta', 'never-ask': 'Mai' },
  levelHints: {
    'ask-each-turn': 'Chiede a ogni turno prima di guardare lo schermo o usare mouse e tastiera per la prima volta.',
    'ask-before-acting': 'Per guardare non chiede; chiede a ogni turno prima di usare mouse e tastiera per la prima volta.',
    'ask-once': 'Per guardare non chiede; chiede una volta prima di agire, poi non più finché vale il sì.',
    'never-ask': 'Non chiede mai.',
  },
  grant: 'Un sì vale',
  grantSuffix: 'minuti',
  grantBad: (min: number, max: number) => `Inserisci un numero intero da ${min} a ${max}`,
  more: 'Altre impostazioni sono nella pagina Controllo del computer, in modalità avanzata.',
  saveFailed: (why: string) => `Non salvato: ${why}`,
  enabledLabel: 'Usare il computer',
  controlLabel: 'Mouse e tastiera',
};
