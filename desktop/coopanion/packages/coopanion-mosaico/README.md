# coopanion-mosaico

把配对的 Mosaico 开发板当作桌宠身体的第二个去处：同一个 Coo，在电脑屏幕和板子屏幕之间传送。
它是桌宠 World 的身体转接（`cortico-world-desktop-pet` 的 `src/relay.ts`），由应用在
`core/companion.ts` 里交给 `desktopPetDefinition({ relay })`；不改上游任何源码。

| 文件 | 职责 |
|---|---|
| `src/relay.ts` | `MosaicoRelay`：转接契约的实现；身体归属、指令去向、板端输入与语音、资源同步、状态事件 |
| `src/coordinator.ts` | 单一归属、可持久化的传送事务（`presence.json`），任何超时都不会造出第二个身体 |
| `src/transport.ts`、`discovery.ts`、`clock.ts` | 与板子的 TLS WebSocket、mDNS/局域网发现、时钟同步 |
| `src/assets.ts` | 角色资源分块同步与校验（COO1 / COO2） |
| `src/words.ts` | 桌面形象包的词 → 板子认得的词；新词映射到最接近的板端动作，固件不必跟着改 |
| `src/settings.ts` | 自己的设置文件 `<data>/desktop-pet/mosaico/settings.json`；首次启动从旧版的 `worlds.desktop-pet.mosaico` 迁移 |
| `src/persona.ts`、`src/personas/whale.md` | 每个形象一份自述（CONSTITUTION.md），切换形象时换；Coo 的初始自述用应用自带的种子 |
| `src/model-text.ts` | `{{pet.relay}}` 环境提示词、板端独有触摸的事件文字、`pet_location` / `pet_transfer` 工具 |
| `web/page.js` | 桌宠页上的一半传送：传送门动画、隐藏与显示身体、按帧回执 |
| `web/assets.js` | 在桌宠页里把 Coo 按当前配色画成板端图集；大肥鱼用预编译的 `whale-deepseek.bin` |

设置窗口的「Mosaico」页在 `console/features/mosaico/`（应用的文件）。

## 行为边界

- 身体在板子上时，`pet_say`、`pet_ask`、`pet_act`、`pet_walk_to` 照常可用，显示在板上；应用自己的引导对话需要电脑屏幕，
  此时返回不可用。电脑操作会先把身体叫回电脑。
- 身体不在电脑上时电脑麦克风和说话键都不收音；板端按住 AI 键录音，由电脑当前的识别引擎转写。
- 启用与配对文件在下次启动应用时生效；空闲自主前往立即生效。
- 状态只写在部署数据目录下，不写 Persona Memory；板端表情缓存只是外观。

## 测试

```sh
pnpm --dir packages/coopanion-mosaico test       # 或在 desktop/coopanion 运行 pnpm run test:worlds
pnpm --dir packages/coopanion-mosaico typecheck
```

`tests/link.test.ts` 用本机自签证书起真实 WSS，走完资源同步、录音转写和指令转发；`tests/page.test.ts` 在 jsdom 里跑页面模块的传送动画与回执。
