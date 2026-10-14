/**
 * 「Mosaico」: the paired board as a second place for Coo's body (packages/coopanion-mosaico). Reads and
 * drives the desktop pet World's `mosaico` panel (the relay's): where the body is, the board's link,
 * the two transfer buttons, and Mosaico's own settings. Turning Mosaico on or choosing another pairing
 * file applies at the next start of the app; the autonomous visits apply at once.
 *
 * Mosaico-owned (desktop/upstream/upstream.json `keep`); Chinese and English only.
 */
import { invokePanel } from '../../base/api.ts';
import type { Page, PageContext } from '../../base/page.ts';
import { intro } from '../intro.ts';

const PET_PAGE = 'world:desktop-pet';
const PANEL = 'mosaico';
const REFRESH_MS = 1000;

interface MosaicoState {
  enabled: boolean;
  owner: 'desktop' | 'device';
  transfer: { phase: string } | null;
  connected: boolean;
  ready: boolean;
  error: string;
  restart: boolean;
  settings: { enabled: boolean; pairingFile: string; autonomous: boolean };
}

const zh = !navigator.language.toLowerCase().startsWith('en');
const S = zh ? {
  nav: 'Mosaico', desc: '同一个 Coo,在电脑和 Mosaico 开发板之间传送身体。',
  status: '状态', link: '板子连接', where: '身体在哪里', linked: '已连接,可以传送', preparing: '已连接,正在准备角色资源', offline: '未连接',
  desktop: '电脑', device: 'Mosaico', moving: '传送中', go: '去 Mosaico', back: '回电脑', arrived: '已抵达',
  off: 'Mosaico 没有启用。用 USB 完成配对(projects/coopanion/tools/pair.py)后,在下面打开并选择配对文件,然后重启应用。',
  settings: '设置', enable: '启用 Mosaico', pairing: '配对文件(pairing.json 的完整路径)', save: '保存', autonomous: '空闲时自主前往 Mosaico',
  restart: '已保存。启用状态和配对文件在重启应用后生效。', saved: '已保存',
  talk: '和 Coo 说话', talkDesc: 'Coo 在电脑上时,短按板子的 AI 键召唤它。在 Mosaico 上按住 AI 键说话,松开发送;长按屏幕空白处打开菜单,长按顶部状态栏切换 Wi-Fi。',
} : {
  nav: 'Mosaico', desc: 'One Coo, its body moving between the computer and the Mosaico board.',
  status: 'Status', link: 'Board link', where: 'Where the body is', linked: 'Connected, ready to move', preparing: 'Connected, preparing the figure', offline: 'Not connected',
  desktop: 'Computer', device: 'Mosaico', moving: 'Moving', go: 'Go to Mosaico', back: 'Back to the computer', arrived: 'Arrived',
  off: 'Mosaico is off. Pair the board over USB (projects/coopanion/tools/pair.py), switch it on below with the pairing file, then restart the app.',
  settings: 'Settings', enable: 'Use Mosaico', pairing: 'Pairing file (full path of pairing.json)', save: 'Save', autonomous: 'Visit Mosaico on its own when idle',
  restart: 'Saved. Switching on and the pairing file apply after the app restarts.', saved: 'Saved',
  talk: 'Talking to Coo', talkDesc: 'While Coo is on the computer, a short press of the board\'s AI key calls it over. On Mosaico, hold the AI key to talk and let go to send; a long press on the screen opens the menu, and on the status bar switches Wi-Fi.',
};

async function mount(ctx: PageContext): Promise<void> {
  const { ui, root } = ctx;
  root.classList.add('home');
  root.append(intro(ui, S.nav, { desc: S.desc }));
  const call = <T>(method: string, args: unknown[] = []) => invokePanel<T>(PET_PAGE, PANEL, method, args, { signal: ctx.signal });

  const status = ui.sheet({ title: S.status, en: 'one Coo, two screens' });
  const link = ui.pill(S.offline, 'off'), where = ui.pill(S.desktop, 'plain');
  const go = ui.button(S.go, { variant: 'primary', size: 'sm' }), back = ui.button(S.back, { size: 'sm' });
  const buttons = ui.rowbar();
  buttons.append(go, back);
  const message = ui.msgline('');
  status.body.append(ui.kv([{ k: S.link, v: link }, { k: S.where, v: where }]), buttons, message);

  const settings = ui.sheet({ title: S.settings });
  const enable = ui.checkbox(S.enable);
  const pairing = ui.input({ cls: 'mono' });
  const save = ui.button(S.save, { size: 'sm' });
  const autonomous = ui.checkbox(S.autonomous, {
    onChange: (on) => void call<MosaicoState>('settings', [{ autonomous: on }]).then(render).catch(fail),
  });
  const saveRow = ui.rowbar();
  saveRow.append(save);
  settings.body.append(enable.el, ui.field(S.pairing, pairing), saveRow, autonomous.el);

  const talk = ui.sheet({ title: S.talk, desc: S.talkDesc });
  root.append(status.el, settings.el, talk.el);

  function fail(e: unknown): void { message.textContent = e instanceof Error ? e.message : String(e); message.classList.add('bad'); }
  let edited = false;
  pairing.addEventListener('input', () => { edited = true; });
  enable.input.addEventListener('change', () => { edited = true; });

  function render(s: MosaicoState): void {
    link.textContent = s.ready ? S.linked : s.connected ? S.preparing : S.offline;
    link.className = link.className.replace(/\b(on|off|plain)\b/g, '').trim() + ` ${s.ready ? 'on' : 'off'}`;
    where.textContent = s.transfer ? S.moving : s.owner === 'device' ? S.device : S.desktop;
    go.disabled = !s.enabled || !s.ready || !!s.transfer || s.owner === 'device';
    back.disabled = !s.enabled || !s.connected || !!s.transfer || s.owner !== 'device';
    if (!edited) { enable.setChecked(s.settings.enabled); pairing.value = s.settings.pairingFile; }
    autonomous.setChecked(s.settings.autonomous);
    message.classList.toggle('bad', !!s.error);
    message.textContent = !s.enabled ? S.off : s.restart ? S.restart : s.error;
  }
  const refresh = async () => { try { render(await call<MosaicoState>('state')); } catch (e) { fail(e); } };
  const transfer = async (to: 'desktop' | 'device') => {
    go.disabled = back.disabled = true;
    try { render(await call<MosaicoState>('transfer', [to])); ui.toast(S.arrived); } catch (e) { fail(e); }
  };
  go.addEventListener('click', () => void transfer('device'));
  back.addEventListener('click', () => void transfer('desktop'));
  save.addEventListener('click', async () => {
    try {
      const s = await call<MosaicoState>('settings', [{ enabled: enable.checked, pairingFile: pairing.value.trim() }]);
      edited = false;
      render(s);
      ui.toast(S.saved);
    } catch (e) { fail(e); }
  });
  await refresh();
  ctx.lifecycle.interval(() => void refresh(), REFRESH_MS);
}

export const mosaicoPage: Page = { route: 'mosaico', label: S.nav, icon: 'cpu', mount };
