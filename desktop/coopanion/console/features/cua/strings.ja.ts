import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: 'コンピューター操作',
  enabled: 'Coo にこのコンピューターを使わせる',
  control: 'マウスとキーボードを許可',
  controlHint: 'オフのときは画面を見ることしかできません。',
  permission: 'いつ確認するか',
  levels: { 'ask-each-turn': '毎ターン', 'ask-before-acting': '操作の前', 'ask-once': '一度だけ', 'never-ask': '確認しない' },
  levelHints: {
    'ask-each-turn': '毎ターン、最初に画面を見る前またはマウスとキーボードを使う前に確認します。',
    'ask-before-acting': '画面を見るときは確認しません。毎ターン、最初にマウスとキーボードを使う前に確認します。',
    'ask-once': '画面を見るときは確認しません。操作の前に一度だけ確認し、有効時間内は再確認しません。',
    'never-ask': '確認しません。',
  },
  grant: '許可の有効時間',
  grantSuffix: '分',
  grantBad: (min: number, max: number) => `${min} から ${max} までの整数を入力してください`,
  more: 'その他の設定は詳細モードの「コンピューター操作」ページにあります。',
  saveFailed: (why: string) => `保存できませんでした：${why}`,
  enabledLabel: 'コンピューターの使用',
  controlLabel: 'マウスとキーボード',
};
