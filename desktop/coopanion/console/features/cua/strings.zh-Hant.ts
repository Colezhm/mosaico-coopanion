import type { en } from './strings.ts';

export const S: Partial<typeof en> = {
  title: '電腦操作',
  enabled: '讓 Coo 操作這台電腦',
  control: '允許操作滑鼠和鍵盤',
  controlHint: '關閉時只能查看螢幕。',
  permission: '詢問時機',
  levels: { 'ask-each-turn': '每輪詢問', 'ask-before-acting': '操作前詢問', 'ask-once': '詢問一次', 'never-ask': '從不詢問' },
  levelHints: {
    'ask-each-turn': '每輪首次查看螢幕或操作滑鼠鍵盤前詢問。',
    'ask-before-acting': '查看螢幕不詢問；每輪首次操作滑鼠鍵盤前詢問。',
    'ask-once': '查看螢幕不詢問；操作前詢問一次，有效期內不再詢問。',
    'never-ask': '查看螢幕和操作都不詢問。',
  },
  grant: '同意有效期',
  grantSuffix: '分鐘',
  grantBad: (min: number, max: number) => `請填寫 ${min} 到 ${max} 之間的整數`,
  more: '更多設定在進階模式的「電腦操作」頁。',
  saveFailed: (why: string) => `沒有儲存：${why}`,
  enabledLabel: '使用電腦',
  controlLabel: '滑鼠與鍵盤',
};
