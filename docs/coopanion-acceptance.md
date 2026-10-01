<!-- Owner: desktop/coopanion/packages/*/tests/, projects/coopanion/tools/test_native.py, projects/coopanion/tools/export_browser_image.py -->

# Mosaico × Coopanion 验收记录

2026-10-01，Apple Silicon Mac。本版本是开发候选版，真机验收尚未完成。

| 项目 | 已观察结果 | 边界 |
|---|---|---|
| 桌宠 World | 包内 76 项通过、1 项平台条件跳过；类型检查通过 | 含 WSS、资源哈希、PCM 完整性、旧会话拒绝、启用时工具声明、部署隔离 |
| CUA World | 包内 25 项通过、类型检查通过 | 沿用原授权逻辑；未实际操作用户桌面 |
| 桌面应用 | 22 项测试通过；Node 和网页类型检查通过 | 模型服务未调用 |
| Cortina 第二级 | 两个包 `check:extension` 通过，含临时部署干装载 | 干装载不启动服务；不代表用户实例验收 |
| 传送状态机 | 重复召唤、超时、丢失回执、重连、持久化失败、重启恢复测试通过 | 无超时生成第二个身体的路径 |
| 原生 C | ASan/UBSan 的状态、渲染、脚本队列测试通过 | 编译器对 vendored cJSON 报 6 条弃用警告，未出现 sanitizer 错误 |
| 双端模拟器 | 原桌面网页经真实 WSS 与共享 C/GSP 后端完成双向传送；观察到摇晃、踉跄、摔倒、爬起、哭泣、生闷气、安抚、字幕、离线提示 | 音频和传感器输入模拟；不是硬件时延或音质证据 |
| 桌面 .app | 隔离数据目录启动成功，Core、桌宠 World 和网页连接成功 | ad hoc 签名，无 Apple 公证；最终包还须核对 |
| ESP-IDF | `20261001-142840-build-95579` 构建成功，6.4 秒，应用 2,254,000 字节 | 固定 ESP32-S31 SDK；1 条构建警告需结合原日志查看 |
| 完整镜像 | 16,777,216 字节；分区 MD5、保留布局、基础组件哈希、资源边界校验通过 | 整个 NOR 会被覆盖；普通应用 BIN 不用于零地址初装 |
| 物理设备 | 只读发现曾报告 ROM Download Mode | 尚未烧录，尚无目标应用的 Device ID / Boot ID / health 证据 |

完整 BIN SHA-256：
`0b5700d8f28409b550bdcbf6b66866f804717a4ba1fbb37068b4e3b0e0ac6aa2`。
镜像覆盖 `0x000000–0xFFFFFF`，含旧 NVS、配对、系统元数据、资源缓存与崩溃记录；
不写 NAND。覆盖前必须按根 AGENTS.md 取得当次确认。

尚未通过：真实 500 ms 间隔 ±80 ms、板端 ≥30 fps、IMU 反馈 ≤100 ms、真实语音识别和
扬声器自声、连续录音、真实低电量与运动校准、30 分钟稳定运行、同设备 normal → Vibe Mode → normal。
截图、编译和模拟器日志不能替代这些结果。Windows、Intel Mac、Linux 构建运行亦未实测。

重现命令、版本锁定、协议边界和已知功能限制见 [开发与使用](coopanion-implementation.md)。
