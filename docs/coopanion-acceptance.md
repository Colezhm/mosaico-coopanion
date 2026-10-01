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
| 原生连续录像与菜单 | 官方 GSP API 保存 27 秒、314 张带实际时间戳的连续帧；长按输入打开菜单，画面含返回电脑、静音、摸头与关闭按钮 | `native-actions.mp4` 和 `native-record/capture.json` 位于本机产物目录；录像采样约 12 fps，不代表设备渲染帧率 |
| 桌面 .app | 最终包在隔离数据目录启动成功，Core、桌宠 World、网页和 FunASR 连接成功；Mosaico 页面显示连接与位置，未连接时禁止传送 | ad hoc 签名，无 Apple 公证；未调用模型服务 |
| ESP-IDF | `20261001-142840-build-95579` 构建成功，6.4 秒，应用 2,254,000 字节 | 通用尺寸检查提示正常应用大于 factory；导出器将其放在 5.9375 MiB 的 ota_0，factory 保留较小的 Vibe Mode 固件 |
| 完整镜像 | 16,777,216 字节；分区 MD5、保留布局、基础组件哈希、资源边界校验通过 | 整个 NOR 会被覆盖；普通应用 BIN 不用于零地址初装 |
| 官方浏览器烧录 | 用户当次确认后写入完整 BIN；官方页面显示 100% 和 Firmware flashed successfully | 已释放浏览器串口；烧录完成不等于交互验收 |
| 物理设备首次启动 | 重插 USB 后实时 Iris 握手确认 coopanion 1.0.0、normal、stale=false、crash_count=0；真实截图显示“等待 Coo 来访” | 配对未完成，Wi-Fi 和跨屏交互待验收 |
| Vibe Mode | 同一 Device ID 成功从 normal 进入 recovery 0.1.4，新 Boot ID，具备 ota/system_update，crash_count=0；真实截图显示 USB Active | 返回 normal 的实时确认待完成 |
| 首次公开源码 | `Colezhm/mosaico-coopanion` 提交 `d61fc0c15185734e4d51a2696bd33555eda6eb0a` 的树与本地 `944a5cc` 完全一致；GitHub CI 通过 | 源码树 `967a9539cdd52e5c591c7dd5b5b66acc67142e7d`；构建产物与私有配对文件不在源码中 |

完整 BIN SHA-256：
`0b5700d8f28409b550bdcbf6b66866f804717a4ba1fbb37068b4e3b0e0ac6aa2`。
镜像覆盖 `0x000000–0xFFFFFF`，含旧 NVS、配对、系统元数据、资源缓存与崩溃记录；
不写 NAND。本次覆盖已按根 AGENTS.md 获得用户当次确认。

Mac arm64 ZIP SHA-256：
`796a50f077e5e485c7ae560d33551e2f9eec00b63b186537367fe724a5868b07`。
本机原始证据保存在 `artifacts/mosaico-coopanion/device-first-boot.{json,log,png}`、
`device-enter-vibe.log`、`device-vibe.json` 和 `device-vibe.png`；这些文件不公开设备身份。

尚未通过：真实 500 ms 间隔 ±80 ms、板端 ≥30 fps、IMU 反馈 ≤100 ms、真实语音识别和
扬声器自声、连续录音、真实低电量与运动校准、30 分钟稳定运行、同设备 normal → Vibe Mode → normal。
截图、编译和模拟器日志不能替代这些结果。Windows、Intel Mac、Linux 构建运行亦未实测。

重现命令、版本锁定、协议边界和已知功能限制见 [开发与使用](coopanion-implementation.md)。
