import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: '컴퓨터 사용',
  enabled: 'Coo가 이 컴퓨터를 사용하도록 허용',
  control: '마우스와 키보드 허용',
  controlHint: '끄면 Coo는 화면을 보는 것만 할 수 있습니다.',
  permission: '먼저 물어볼 때',
  levels: { 'ask-each-turn': '매 턴', 'ask-before-acting': '조작 전', 'ask-once': '한 번', 'never-ask': '묻지 않음' },
  levelHints: {
    'ask-each-turn': '매 턴 처음 화면을 보거나 마우스와 키보드를 쓰기 전에 묻습니다.',
    'ask-before-acting': '화면 보기는 묻지 않고, 매 턴 처음 마우스와 키보드를 쓰기 전에 묻습니다.',
    'ask-once': '화면 보기는 묻지 않고, 조작 전에 한 번 물은 뒤 허용 유지 시간 동안 다시 묻지 않습니다.',
    'never-ask': '묻지 않습니다.',
  },
  grant: '허용 유지 시간',
  grantSuffix: '분',
  grantBad: (min: number, max: number) => `${min}부터 ${max}까지의 정수를 입력하세요`,
  more: '더 많은 설정은 고급 모드의 컴퓨터 사용 페이지에 있습니다.',
  saveFailed: (why: string) => `저장하지 못했습니다: ${why}`,
  enabledLabel: '컴퓨터 사용',
  controlLabel: '마우스와 키보드',
};
