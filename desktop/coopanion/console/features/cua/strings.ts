import { pick } from '../../base/language.ts';
import { S as zhHant } from './strings.zh-Hant.ts';
import { S as ja } from './strings.ja.ts';
import { S as ko } from './strings.ko.ts';
import { S as fr } from './strings.fr.ts';
import { S as de } from './strings.de.ts';
import { S as es419 } from './strings.es-419.ts';
import { S as ptBR } from './strings.pt-BR.ts';
import { S as it } from './strings.it.ts';
import { S as ru } from './strings.ru.ts';

const zh = {
  title: '电脑操作',
  enabled: '让 Coo 操作这台电脑',
  control: '允许操作鼠标和键盘',
  controlHint: '关闭时只能查看屏幕。',
  permission: '询问时机',
  levels: { 'ask-each-turn': '每轮询问', 'ask-before-acting': '操作前询问', 'ask-once': '询问一次', 'never-ask': '从不询问' },
  levelHints: {
    'ask-each-turn': '每轮首次查看屏幕或操作鼠标键盘前询问。',
    'ask-before-acting': '查看屏幕不询问；每轮首次操作鼠标键盘前询问。',
    'ask-once': '查看屏幕不询问；操作前询问一次，有效期内不再询问。',
    'never-ask': '查看屏幕和操作都不询问。',
  },
  grant: '同意有效期',
  grantSuffix: '分钟',
  grantBad: (min: number, max: number) => `请填写 ${min} 到 ${max} 之间的整数`,
  more: '更多配置在高级模式的「电脑操作」页。',
  saveFailed: (why: string) => `保存失败：${why}`,
  enabledLabel: '使用电脑',
  controlLabel: '鼠标与键盘',
};

export const en: typeof zh = {
  title: 'Computer use',
  enabled: 'Let Coo use this computer',
  control: 'Allow the mouse and keyboard',
  controlHint: 'When off, Coo can only look at the screen.',
  permission: 'When to ask you',
  levels: { 'ask-each-turn': 'Every turn', 'ask-before-acting': 'Before acting', 'ask-once': 'Once', 'never-ask': 'Never' },
  levelHints: {
    'ask-each-turn': 'Asks every turn before first looking at the screen or using the mouse and keyboard.',
    'ask-before-acting': 'Looking is not asked; asks every turn before first using the mouse and keyboard.',
    'ask-once': 'Looking is not asked; asks once before acting, then not again while the yes lasts.',
    'never-ask': 'Never asks.',
  },
  grant: 'A yes lasts',
  grantSuffix: 'minutes',
  grantBad: (min: number, max: number) => `Enter a whole number from ${min} to ${max}`,
  more: 'More settings are on the Computer use page in the advanced mode.',
  saveFailed: (why: string) => `Not saved: ${why}`,
  enabledLabel: 'Use the computer',
  controlLabel: 'Mouse and keyboard',
};

export const S = pick({
  zh, en, 'zh-Hant': zhHant, ja, ko, fr, de, 'es-419': es419, 'pt-BR': ptBR, it, ru,
});
