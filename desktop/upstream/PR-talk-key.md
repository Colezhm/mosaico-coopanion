<!-- 提交到 Pal-AI-Lab/Coopanion 的 PR 草稿,对应 patches/0003-desktop-pet-talk-key-keeps-mic-closed.patch。提交前需签上游 CLA。 -->

# fix(desktop-pet): 说话键不可用时麦克风保持关闭,不再改成一直收音

## 问题

读不到说话键时(最常见的是 macOS 上 .app 没有「输入监控」权限,更新后新版本也不认旧授权),
`micMode()` 把 hold / toggle 改成 `always`。使用者以为自己要按键才收音,麦克风却一直开着:
我们的一份事件记录里,房间里的环境声被转写成 25 条「没。」发给了 bot,其他人的说话也被当成使用者的话。

## 改动

- `micMode()` 返回配置的模式,不再因为说话键不可用而改成 `always`;于是麦克风保持关闭。
- 提示行仍写明说话键为什么不可用(`voice.fallback`,11 种语言改为「麦克风先关着」的意思),日志同步改写。
- `tests/voice.test.ts` 原来断言「改为一直收音」,改为断言模式不变、麦克风关闭、不产生事件。

使用者想要一直收音,仍可以在语音设置里选「一直听」。
