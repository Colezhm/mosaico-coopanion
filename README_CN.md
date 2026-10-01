# Mosaico × Coopanion

[English](README.md) · [开发与使用](docs/coopanion-implementation.md) · [验收记录](docs/coopanion-acceptance.md)

让同一个 Coo 在电脑与 ESP-Mosaico 之间传送。电脑保留 Coopanion 的对话、记忆、
FunASR 与电脑操作授权；板端独立运行 C 动画、IMU、触摸、情绪反馈和 Xiaole 中文语音。
独立配对 WSS 通道维护唯一身体归属，传送包含两个 500 ms 间隔。

**当前为开发候选版，真机验收尚待完成。** 烧录前阅读验收记录。
完整初装镜像会覆盖全部 16 MiB NOR，包含旧设置、配对与缓存；保留 Vibe Mode 布局和功能，
不代表保留原数据。实际覆盖需要针对具体镜像再次确认。

- 桌面端：`desktop/coopanion`，基于 Coopanion `78a46f83`，保留原 MIT 许可证。
- 板端和共享模拟器：`projects/coopanion`，固定 ESP-IDF、ESP32-S31、GSP 1.5.1。
- 构建、私有配对、双端预览和整包导出：[使用说明](docs/coopanion-implementation.md)。
- 原工作区文档：[中文](README_CN.upstream.md) / [English](README.upstream.md)。

首版仅集成 Coo，不含大肥鱼、摄像头和外接模块。依赖保留各自许可证，新增桌面与应用模块使用 MIT。
