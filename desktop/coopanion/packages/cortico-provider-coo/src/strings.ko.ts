import type { en } from './strings.ts';

export const cooText: Partial<typeof en> = {
  description: (names: readonly string[]) => `모듈 하나로 ${names.join(', ')}에 연결합니다. 기본 URL로 어느 서비스인지, 어떤 프로토콜을 쓰는지 판단합니다. 추론은 네 단계로 조절할 수 있습니다.`,
  tiers: { off: '추론 안 함', low: '추론 · 빠름', high: '추론 · 표준', max: '추론 · 최대' },
  protocol: '프로토콜',
  protocolHint: '비워 두면 목록에 있는 서비스는 자체 프로토콜을, 그 밖의 URL은 responses를 씁니다. responses: POST <URL>/responses, chat: POST <URL>/chat/completions, anthropic: Messages API이며 URL은 /v1 앞부분까지, gemini: Gemini API이며 URL은 /models 앞부분까지 입력합니다.',
  badProtocol: (protocols: string) => `프로토콜은 ${protocols} 중 하나여야 합니다`,
  chatgpt: {
    panel: 'ChatGPT 계정',
    description: 'ChatGPT 계정으로 로그인하며, 요청은 해당 계정의 ChatGPT 요금제 사용량에 포함됩니다.',
    signedOut: 'ChatGPT에 로그인하지 않았습니다',
    returnToApp: '로그인 결과는 앱에 표시됩니다. 이 탭은 닫아도 됩니다.',
    reasons: { access_denied: '접근이 허용되지 않았습니다', plan_not_granted: '이 계정에서 ChatGPT 요금제 사용이 허용되지 않았습니다', timeout: '브라우저 로그인 시간이 초과되었습니다' },
  },
};
