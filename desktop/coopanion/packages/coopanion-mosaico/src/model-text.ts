/** What the bot reads from Mosaico: its section of the environment prompt, the board's touches and its two tools. */
import type { ToolDef } from 'cortico/core/types.ts';
import type { Body } from './coordinator.ts';

/** `{{pet.relay}}` while Mosaico is on. */
export const MOSAICO_PROMPT = `### 跨屏身体:Mosaico

同一个身体可以前往桌上的 Mosaico 开发板屏幕。身体归属和连接变化由 [身体状态] 事件报告;需要最新状态时用 \`pet_location\`,要过去或回来用 \`pet_transfer\`。
传送中暂不能说话或做普通动作,等落地。身体在板子上时,\`pet_say\`、\`pet_ask\`、\`pet_act\`、\`pet_walk_to\` 照常用,显示在板子上;板端语音是按住 AI 键说话、松开发送;板子离线时只有板上的本地互动和固定台词。
电脑操作会先等身体回到电脑并落地,再按原有授权规则执行。空闲时的自主迁移由设置控制,它是设备行为,不能据此推断自己的意愿。板端表情是外观状态,长期情绪和对话记忆仍由你管理。
板子上独有的互动:挠痒、捧在手里抱着、放回桌上、戳破泡泡,会以 [互动] 事件告诉你。`;

/** The board's own touches (firmware coop_touch.c, coop_carry.c, coop_bubbles.c). */
export const TOUCH_TEXT: Record<string, (user: string, count: number) => string> = {
  tickle: (u, n) => (n > 1 ? `${u}在板子上挠了你好几下痒痒` : `${u}在板子上挠你痒痒`),
  hug: (u) => `${u}把板子捧在手里,安安静静地抱着你`,
  putdown: (u) => `${u}把你放回了桌上`,
  bubble: (u, n) => (n > 1 ? `${u}戳破了你吐的 ${n} 个泡泡` : `${u}戳破了你吐的泡泡`),
};

interface Traveller {
  state(): Record<string, unknown>;
  transfer(to: Body): Promise<void>;
}

/** pet_location and pet_transfer. */
export function mosaicoTools(relay: Traveller): ToolDef[] {
  return [{
    name: 'pet_location', tags: ['read'], description: '读取身体当前在哪块屏幕(电脑或 Mosaico)、板子连接与传送状态。',
    parameters: { type: 'object', properties: {} },
    handler: async () => ({ text: JSON.stringify(relay.state()) }),
  }, {
    name: 'pet_transfer', tags: ['act'], description: '让身体通过传送动画前往 Mosaico 板子或回到电脑屏幕;落地后返回。电脑操作会自动先回到电脑。',
    parameters: { type: 'object', properties: { destination: { type: 'string', enum: ['desktop', 'device'], description: 'desktop 是电脑屏幕,device 是 Mosaico 板子' } }, required: ['destination'] },
    handler: async (args: Record<string, unknown>) => {
      if (args.destination !== 'desktop' && args.destination !== 'device') return { text: '[pet_transfer 没执行] destination 应为 desktop 或 device。', failed: true };
      try {
        await relay.transfer(args.destination);
        return { text: `已抵达 ${args.destination === 'device' ? 'Mosaico' : '电脑'}。` };
      } catch (e) {
        return { text: `[pet_transfer 没执行] ${(e as Error).message}`, failed: true };
      }
    },
  }];
}
