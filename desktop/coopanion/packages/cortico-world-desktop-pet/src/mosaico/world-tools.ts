import type { ToolDef } from 'cortico/core/types.ts';
import type { MosaicoBridge } from './bridge.ts';

/** The model's view of the cross-screen body, added to the desktop pet World only when Mosaico is enabled. */
export const MOSAICO_PROMPT = '同一个身体可以前往 Mosaico 屏幕。身体归属和连接变化由 [身体状态] 事件报告；需要最新状态时使用 pet_location。传送中暂不能说话或做普通动作，等待落地。板端语音使用按住 AI 键倾听、松开发送；离线只有本地互动和固定台词。电脑工具会先等待身体返回电脑并落地，再按原有授权规则执行。空闲迁移由已启用的设置控制；它是设备行为，不能据此推断自己的意愿。板端表情是外观状态，长期情绪和对话记忆仍由你管理。';

const NOT_STARTED = 'Mosaico 尚未启动';

/** pet_location and pet_transfer; @p bridge returns null until the bridge has started. */
export function mosaicoTools(bridge: () => MosaicoBridge | null): ToolDef[] {
  return [{
    name: 'pet_location', tags: ['read'], description: '读取 Coo 当前身体位置、设备连接与传送状态。',
    parameters: { type: 'object', properties: {} },
    handler: async () => {
      const b = bridge();
      return b ? { text: JSON.stringify(b.state()) } : { text: NOT_STARTED, failed: true };
    },
  }, {
    name: 'pet_transfer', tags: ['act'], description: '让同一个 Coo 通过传送动画前往 Mosaico 或返回电脑；等待落地后返回。电脑操作会自动先返回桌面。',
    parameters: { type: 'object', properties: { destination: { type: 'string', enum: ['desktop', 'device'] } }, required: ['destination'] },
    handler: async (args: Record<string, unknown>) => {
      if (args.destination !== 'desktop' && args.destination !== 'device') return { text: '无效目的地', failed: true };
      const b = bridge();
      if (!b) return { text: NOT_STARTED, failed: true };
      try {
        await b.transfer(args.destination);
        return { text: `已抵达 ${args.destination}` };
      } catch (e) {
        return { text: (e as Error).message, failed: true };
      }
    },
  }];
}

/** Console panel 'mosaico': state() and transfer(destination). Returns undefined for other methods. */
export async function invokeMosaicoPanel(bridge: MosaicoBridge | null, method: string, args: unknown[]): Promise<unknown> {
  if (method === 'state') return bridge?.state() ?? { enabled: false };
  if (method === 'transfer' && (args[0] === 'desktop' || args[0] === 'device')) {
    if (!bridge) throw new Error('请先启用 Mosaico 扩展');
    await bridge.transfer(args[0]);
    return bridge.state();
  }
  return undefined;
}
