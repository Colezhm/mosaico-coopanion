import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: 'Uso de la computadora',
  enabled: 'Dejar que Coo use esta computadora',
  control: 'Permitir el mouse y el teclado',
  controlHint: 'Si está desactivado, Coo solo puede mirar la pantalla.',
  permission: 'Cuándo preguntarte',
  levels: { 'ask-each-turn': 'Cada turno', 'ask-before-acting': 'Antes de actuar', 'ask-once': 'Una vez', 'never-ask': 'Nunca' },
  levelHints: {
    'ask-each-turn': 'Pregunta en cada turno antes de mirar la pantalla o usar el mouse y el teclado por primera vez.',
    'ask-before-acting': 'Para mirar no pregunta; pregunta en cada turno antes de usar el mouse y el teclado por primera vez.',
    'ask-once': 'Para mirar no pregunta; pregunta una vez antes de actuar y no vuelve a preguntar mientras dure el sí.',
    'never-ask': 'Nunca pregunta.',
  },
  grant: 'Un sí dura',
  grantSuffix: 'minutos',
  grantBad: (min: number, max: number) => `Ingresa un número entero de ${min} a ${max}`,
  more: 'Hay más ajustes en la página Uso de la computadora del modo avanzado.',
  saveFailed: (why: string) => `No se guardó: ${why}`,
  enabledLabel: 'Usar la computadora',
  controlLabel: 'Mouse y teclado',
};
