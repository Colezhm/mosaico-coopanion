<!-- 提交到 Pal-AI-Lab/Coopanion 的 PR 草稿,对应 patches/0001-desktop-pet-body-relay.patch。提交前需签上游 CLA。 -->

# feat(desktop-pet): 身体转接,应用可以把桌宠的身体搬到别的设备上

## 做什么

给 `desktopPetDefinition` 加一个可选的 `relay(ctx)`:应用借给桌宠 World 一个 `PetBodyRelay`(`src/relay.ts`),
身体就可以去别的设备上——桌上的开发板、手机、另一台电脑的屏幕。bot 看到的仍是同一个桌宠 World:
同样的工具、事件和 Memory,转接只搬动身体。没有转接时行为完全不变。

我们在 [Mosaico × Coopanion](https://github.com/Colezhm/mosaico-coopanion) 里用它把 Coo 传送到一块
ESP32 开发板(触摸屏、IMU、马达)上。此前的做法是直接改 `world.ts`、`pet-app.js` 十几处,每次跟进上游都要重写;
有了这个接口,设备那一侧整个放在应用自己的包里,不碰桌宠的源码。

## 接口

- **指令**:发给身体的每条指令(`say`、`ask`、`confirm`、`act`、`walk`、`listen`、`status`、对话步骤)先交给
  `relay.send(msg)`:返回 `true` / `false` 表示转接已送达 / 送不到,`undefined` 时照常发给桌宠页。
  World 里原来的 `this.server.sendPet(...)` 改成一个 `sendBody(...)`,`hide` 仍直接发给页面。
- **身体在哪**:`here()` 为假时电脑的麦克风和说话键不收音;`reachable()` 为真时没有桌宠页也保留输出流。
- **输入**:`port.input(msg)` 把设备上的触摸、回答、打字当作桌宠页发来的处理;`port.heard(text)` 把设备听到的话
  像麦克风一样投递(有没答的提问时作为回答);`port.transcribe(pcm)` 用当前识别引擎转写设备录的音。
- **扩展点**:`tools()`、`envPrompt()`(新占位符 `{{pet.relay}}`,中英两份提示词各加一行)、`panel`(控制台面板)、
  `touchText()`(桌宠页没有的触摸种类)、`unavailable(tool)`(工具没执行的原因)、`beforeComputer()` / `computerDone()`
  (电脑操作先把身体叫回这块屏幕;`Cua` 加了一个可选的 `beforeAct`)。
- **页面一侧**:`page()` 的返回值作为 `init.relay` 交给桌宠页;`module` 是 `/relay/` 下的页面模块(`pageDir` 提供,
  服务端不允许越出该目录),页面 `import` 它并调用 `install(api)`,模块可以在身体离开和回来时做动画。
  `held` 为真时页面不推进身体、不接指针、不开麦克风,`hidden` 从第一帧起就不显示身体。

## 改动范围

`world.ts` 约 70 行(钩子与 port),`relay.ts` 新文件(只有类型),`server.ts` 一个 `/relay/` 静态路由,
`definition.ts` / `index.ts` 透传与导出,`pet-app.js` 约 30 行,`cua.ts` 一个可选回调;11 种语言各加一条
`pet.relay` 占位符说明;README「给内嵌应用」一节。

## 测试

- `packages/cortico-world-desktop-pet/tests/relay.test.ts`(8 个):不转接时照常走页面;转接时指令改道、工具失败原因;
  页面消息先给转接、设备输入当页面输入;听到的话与回答;工具、提示词、面板、触摸;身体不在时麦克风关闭;
  `/relay/` 静态文件与越界拒绝;装配为每个实例造转接。
- `tests/pet-relay.test.js`(3 个):`init.relay` 隐藏首帧、无转接时页面不变、页面模块先于页面处理指令。
- 现有测试全部通过:根目录 191、World 158(1 跳过),`check:extension` 通过。
