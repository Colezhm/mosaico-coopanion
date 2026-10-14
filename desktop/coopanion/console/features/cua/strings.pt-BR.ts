import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: 'Uso do computador',
  enabled: 'Deixar Coo usar este computador',
  control: 'Permitir o mouse e o teclado',
  controlHint: 'Desligado, Coo só pode olhar a tela.',
  permission: 'Quando perguntar a você',
  levels: { 'ask-each-turn': 'Todo turno', 'ask-before-acting': 'Antes de agir', 'ask-once': 'Uma vez', 'never-ask': 'Nunca' },
  levelHints: {
    'ask-each-turn': 'Pergunta a cada turno antes de olhar a tela ou usar o mouse e o teclado pela primeira vez.',
    'ask-before-acting': 'Para olhar não pergunta; pergunta a cada turno antes de usar o mouse e o teclado pela primeira vez.',
    'ask-once': 'Para olhar não pergunta; pergunta uma vez antes de agir e não pergunta de novo enquanto o sim durar.',
    'never-ask': 'Nunca pergunta.',
  },
  grant: 'Um sim dura',
  grantSuffix: 'minutos',
  grantBad: (min: number, max: number) => `Digite um número inteiro de ${min} a ${max}`,
  more: 'Mais configurações na página Uso do computador do modo avançado.',
  saveFailed: (why: string) => `Não foi salvo: ${why}`,
  enabledLabel: 'Usar o computador',
  controlLabel: 'Mouse e teclado',
};
