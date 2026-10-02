<!-- Owner: desktop/coopanion/packages/*/tests/, projects/coopanion/tools/test_native.py, projects/coopanion/tools/export_browser_image.py -->

# Mosaico × Coopanion 验收记录

2026-10-02，Apple Silicon Mac。本版本是开发候选版，真机验收尚未完成。

## 1.2.0 / 桌面 0.1.0-mosaico.4 候选（Mac 验证完成，未上板）

| 项目 | 已观察结果 | 边界 |
|---|---|---|
| 桌面第一、二级验证 | frozen-lockfile 安装成功；应用 22 项、桌宠 93 项、CUA 25 项通过，桌宠另有 1 项平台跳过；World 合计 118 项通过、1 项跳过；Node、World、网页、两包类型检查、console 构建和两个 `check:extension` 全部通过 | 扩展为隔离干装载，未调用模型、实际电脑任务或替换用户部署 |
| 共享 C 与预览 | 状态、传送、渲染回执故障注入、ASan/UBSan 全部通过；20 个场景 × Coo/大肥鱼，共 40 个场景通过 | Apple Clang 对 vendored cJSON 有 6 条已知 `sprintf` 弃用警告，无 sanitizer 错误；不能代替 FreeRTOS/真机 |
| 官方原生模拟器 | GSP 1.5.1、共享 C 后端，保存 16 张实际截图；真实长按及点击验证半透明菜单「回到电脑 / 坐下歇会↔站起来 / 摸摸头 / 收起」；状态行无「静音」，本轮所需字形完整 | 是实际 GSP 字体和原生交互；物理触摸、IMU、音频和设备帧率未验收 |
| 连接与画面 | 实际测试 socket 断开再重连后显示「电脑连上啦」；电脑归属下显示 Coo/鲸鱼传送门；已加载提交的鲸鱼 atlas，`flustered`、`delighted`、`cheeky` 三表情可见 | 注入模拟连接状态，未完成本版本真实桌面与 USB 设备传送 |
| 字幕和桌宠回归修复 | 修复紧邻中文的英文单词在换行时被拆开，新增两种边界回归并实看分页；保留桌宠零、负值及非有限运动步长防护，两个行为测试通过 | 不改变原三行分页和原身体模型 |
| 固件与版本 | 固定 ESP-IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 构建 `20261002-143347-build-71659` 成功；应用描述符与 sdkconfig 均为 1.2.0；修复能力消息应用版本缓冲区编译截断错误；4 项 Python 配对/版本测试通过 | 一条预期 factory 尺寸警告；正常应用位于 `ota_0`，不是应用 BIN 整机烧录 |
| 资源与更新包 | 提交的 COO2 atlas 1,020,422 字节、39 clips/127 frames，低于 1,024,000 字节上限；官方 System Update 离线打包、组件哈希及布局检查通过 | UI 资源已变，必须 `iris system-update`；本地包未签名，未执行设备写入 |
| Electron 资源对比 | 隔离导出 1,019,134 字节，帧数/clip 数一致；RGB 平均绝对差 0.674，最大通道差的 p99 为 18；三张逐帧对比已目视检查 | 含量化和抗锯齿差异，未证明所有差异均仅来自抗锯齿；未替换提交的 Chromium atlas |
| Mac 候选交付 | `0.1.0-mosaico.4` arm64 `.app` 与 ZIP 构建成功，搬到交付目录后 `codesign --verify --deep --strict` 通过 | ad hoc 签名，无 Apple 公证；本轮未启动此最终 `.app`，原运行实例未替换 |
| 真机只读预检 | 复用已有 Iris gateway；正常运行 1.1.0，`stale=false`、`crash_count=0`，操作队列空闲 | 没有写入或重启；1.2.0 尚待当次具体设备更新确认，完整身份信息仅保存在本机 |

最终产物：

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `projects/coopanion/build/coopanion.bin` | 2,247,952 | `5677b8cbdee174d962e6716d634ec8a703891667219dbacc464da3f3fb84f2fb` |
| `mosaico-coopanion-1.2.0-system-update.irisfw` | 5,779,250 | `a9524760a193ef5702a6243ed2cd6993cbe3a87b867a32c21db62f22dbb4ca95` |
| `Coopanion-0.1.0-mosaico.4-mac-arm64.zip` | 166,454,478 | `e56aae530973a74a1bb34840677c2e7fbcde46c38e6fb6ba6dc2bad7d8389ff0` |

本机交付目录 `artifacts/mosaico-coopanion/1.2.0-handoff/`（Git 忽略）：
`validation/desktop-summary.json`、`final-desktop-summary.json`、`native-final.log`、
`python-tests.log`、`firmware-hashes.json`、`system-update-inspection.json`、
`deliverable-hashes.json`、`desktop-installer-build.log`、`atlas-comparison.json`；
`sim/report.json`、`sim/contact-sheet.png` 及 16 张原始截图；`desktop-macos/`。
配对凭据、完整设备标识和本机路径不进入新公开证据。

尚待真机：确认后保留数据升级到 1.2.0，核对同一设备身份、新 Boot ID、版本与应用 ELF 哈希，
并通过官方 CLI 取屏；两个实际跨设备 500±80 ms 间隔、modem-sleep 下的对时和传送、
真实录音/IMU/低电量、断线重连字幕和 30 分钟稳定性均未宣称通过。
GitHub CI 的实时结果以候选 PR 的检查页为准，本机通过不等同远端通过。

## 1.1.0 实机安装与 1.1.1 回执修复候选

| 项目 | 已观察结果 | 边界 |
|---|---|---|
| 保留数据升级 | 用户确认的 1.1.0 System Update 已通过官方 USB/Iris 完成；同一 Device ID 经 normal → Vibe Mode → normal，应用版本及 ELF 哈希验证成功 | 未执行整片擦除；分区布局、保留引导及 Vibe Mode 均未改变 |
| Wi-Fi 与配对 | 复用板内 Wi-Fi、原证书和配对密钥，将旧 IP 地址改为证书内的同一 Mac 局域网主机名 | 未绕过 TLS 校验；主机 DHCP 地址变化由局域网发现处理 |
| 桌面连接修复 | `0.1.0-mosaico.3` 已运行：兼容板端旧式 mDNS 单播回包；资源提交等待延长至 30 秒；时钟采样恢复不再清空设备能力 | 只放宽 NOR 提交等待，控制帧限流、低质量时钟禁迁入和过期会话检查保留 |
| 大肥鱼显示 | 原版蓝色 atlas 已同步、校验并缓存；Iris 直接截图确认实机角色显示 | 不等于正常传送完成；实际往返遇到渲染回执丢失，经过事务核对恢复，无手工重置归属 |
| 1.1.0 稳定性 | 配对后同一 Boot ID 的 30.8 分钟、58.3 分钟快照均无重启、crash_count=0；空闲 SPIRAM 保持 11,737,932 字节 | 是带传送重试的观测，不是全部动作/音频/性能的 30 分钟压力验收 |
| 1.1.1 修复 | GSP 1.5.1 在 Canvas draw 活跃时会拒绝 flush；I/O 回执改为共享总期限内让出调度重试，成功后记录渲染时刻；发送入队短暂忙碌也重试，仍检查持久化归属 | 无成功渲染证明仍不确认；失败有明确日志。新增 GSP 性能日志用于后续实测；尚未安装 1.1.1 |
| 本地验证 | 桌宠包 87 项通过、1 项平台跳过；类型检查及两个 Cortina 扩展检查通过；共享 C ASan/UBSan 与 Canvas 忙碌、超时、失败注入均通过 | 该缺陷涉及板端 GSP/FreeRTOS，主机注入测试不能代替新版实机往返 |
| 固件构建 | `20261002-111147-build-31544` 成功，1.1.1 应用 2,244,608 字节；与已安装 1.1.0 的分区表、UI、语音资源逐项哈希相同 | 一条预期 factory 尺寸警告；仅可按同布局 `iris app-update` 安装，不能把应用 BIN 用作整机镜像 |
| Mac 交付 | 最终 `.app` 已启动，`codesign --verify --deep --strict` 通过；导出 `-release-mac-arm64.zip` | ad hoc 签名，无 Apple 公证；产物目录中较早、不带 `-release-` 的 .3 ZIP 不是最终修复集合 |

1.1.1 应用 SHA-256：`400e6e17d7ebbf5a51f8d734efc07d4810e16e16d43b9342fe254c09bb54daec`。
Mac `Mosaico-Coopanion-0.1.0-mosaico.3-release-mac-arm64.zip`：167,737,677 字节，
SHA-256 `4c252d4c362d5919153377d7100435db89089aa34c895667ad7d3b812e82165c`。
1.1.1 的具体应用更新正在等待当次确认；此时实机仍为 1.1.0，最后核对归属为 desktop。

证据位于本机 `artifacts/mosaico-coopanion/`：`110-system-update.log`、
`110-installed-status.json`、`110-pair-hostname.log`、`110-whale-first.png/.json`、
`110-status-30min.json`、`111-pre-update-status.json`；配对信息和完整设备标识不进入公开仓库。
实际两个 500 ms 间隔、渲染 fps、IMU 延迟、真实录音及低电量仍待验收。

## 1.1.0 DeepSeek 大肥鱼迁移

| 项目 | 已观察结果 | 边界 |
|---|---|---|
| 原版形象与资源 | 复用原部件骨骼，36 段、121 个采样帧；新增 9 个脚本表情名，含专用背身图；COO2 967,993 字节，保留 1 MiB 缓存分区 | 原版是实时骨骼，没有固定 sprite 帧数；不是在板上运行整个 WebGL 模型 |
| 桌面回归 | 两个 World 共 105 项通过、1 项跳过；应用 22 项通过；World/Node/网页类型检查通过；console 构建及两个 Cortina `check:extension` 通过 | 没有修改 Core/Persona；本轮未调用模型或实际电脑任务 |
| 资源安全与状态 | JS 编解码、实际 atlas、错误长度/偏移/索引/基帧测试通过；共享 C ASan/UBSan 通过，包含新 atlas 解码、原 Coo、传送和 IMU 状态回归 | vendored cJSON 仍有 6 条已知 sprintf 弃用警告，无 sanitizer 错误 |
| 最终原生画面 | 官方 GSP 1.5.1 与共享 C 后端，131.951 秒、1275 张实际采样帧；9 个新表情、走跑跳、摇晃、踉跄、摔倒爬起、四边站立、倾听、分页字幕、门和 Coo↔鲸鱼资源切换 | 截图采样约 9.7 fps，不是设备渲染帧率；物理轴向和手感未测 |
| 双端与断线 | 实际桌面网页 → WSS → 原生后端完成大肥鱼同步及两个方向；断线后摸头出现开心表情，重连保留 device 归属；返回后 owner=desktop、epoch=2、transfer=null、ready=true | 是共享原生模拟器，非 USB/真机；完整录像中的旧 disconnect 输入无效，离线流程另行复测并记录 |
| 时序 | 最终连续录像两次顶部光效至身体进入回执为 493 ms、526 ms | 仅本机单调时钟观测；两个跨设备间隔仍需真机验收 |
| 固件 | 固定 ESP-IDF 构建 `20261002-085855-build-92364` 成功，应用描述符 1.1.0，2,243,184 字节；完整 BIN 与官方 System Update 导出成功 | 一条预期 factory 尺寸警告，正常应用位于 ota_0；没有写入设备 |
| Mac 构建 | `0.1.0-mosaico.2` Apple Silicon .app / ZIP 构建成功，`codesign --verify --deep --strict` 通过 | ad hoc 签名，无 Apple 公证；本轮运行验收使用隔离网页/WSS 联调，未启动最终 .app |
| 原有功能边界 | TTS 仍关闭，麦克风路径保留；运动阈值、动画锁、三行分页和四边重力逻辑不变 | 本轮未验证真实录音、扬声器、低电量、30 分钟稳定性或 normal→Vibe→normal |

本机证据目录 `artifacts/mosaico-coopanion/`：`native-whale-final/capture.json`、
`whale-native-actions.mp4`、`whale-native-report.json`、`whale-preview/events.jsonl`、
`whale-wss-report.json`、`whale-offline.json` 和 `whale-firmware-hashes.json`。
原始帧、测试配对密钥与构建产物均被 Git 忽略。

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `mosaico-coopanion-1.1.0-app.bin` | 2,243,184 | `b9b250a19d3d59ce2971549d2c686afae4b85ebe45fcdaeab8e56e832401c7ad` |
| `mosaico-coopanion-1.1.0-full.bin` | 16,777,216 | `fdea40878870c876800c8f97a5c1f9dbc7d53df2b25ea07e5fc049b1208bc716` |
| `mosaico-coopanion-1.1.0-system-update.irisfw` | 5,776,648 | `e3aa6a9c30d656d3415529668fa8c99b1ddb58f4d6be356c6804f5603b405d42` |
| `Mosaico-Coopanion-0.1.0-mosaico.2-mac-arm64.zip` | 167,734,327 | `323c3f5d3924d5d1a2ef4a9e8ebdc9701fffa1214d7dd4bb2653980713556157` |

以上为初次源码/模拟器交付记录；随后 1.1.0 已经用户确认安装，结果见上节。
美术出处、导出方式和其他配色限制见[大肥鱼说明](coopanion-whale.md)。

## 1.0.2 运动、画面和静音修正

| 项目 | 已观察结果 | 边界 |
|---|---|---|
| 状态机回归 | ASan/UBSan 通过：轻晃零对话/语音事件、持续扰动踉跄、55° 倾斜摔倒、同一倾角不重复、动作锁、四边重力、取消等待传送、UTF-8 分页、顺序脚本 | 真实 IMU 阈值、安装轴向和手感仍待板上校准 |
| 原生连续画面 | 官方 GSP 1.5.1 与共享 C 后端录制 55.001 秒、516 张实际帧；四条边均完成转向，包含摇晃→踉跄→摔倒→爬起→情绪反馈和双向门动画 | 约 9.4 fps 的截图采样不是设备渲染性能；此轮未驱动电脑端传送 |
| 字幕与菜单 | 三行均可见，长回复第二页完整保留；上边站立时字幕下移，侧边角色避让字幕；真实长按打开菜单，点击“语音已暂停”后仍为 muted=true | 字集仍为 GB2312；文本最多 2047 UTF-8 字节 |
| 暂停 TTS | 状态层不发 say，音频入口不排入文本，TTS 引擎不初始化；记录中无语音/对话事件，按键录音代码保留 | 物理扬声器静音与真实语音输入尚未复测 |
| 固件构建 | `20261002-061713-build-68000` 成功，31.5 秒，应用 2,239,888 字节 | 一条预期 factory 尺寸警告；正常应用仍位于 ota_0 |
| 资源更新包 | 官方 CMake `system-update-bundle` 成功，5,773,979 字节；相同分区表、应用、UI 与 Xiaole 资源 | 尚未安装；不能用应用 BIN 代替包含 UI 的更新 |
| USB 与真机 | 初次检查未发现 USB，随后恢复识别；实时 Iris 握手确认同一块板子处于 normal 1.0.1、stale=false、crash_count=0，新 Boot ID | 1.0.2 未写入，正在等待具体更新包的当次安装确认；四边轴向和真实手感尚未校准 |
| 公开源码 CI | `297d4302a7ff97ec3faefb50186047e6e274d41b` 的 CI `36995190298` 通过；源码树与本地构建提交一致 | 远端 CI 覆盖工作区和 USB 配对测试；共享 C/固件/视觉结果来自本机独立验收 |

本机证据：`artifacts/mosaico-coopanion/native-102-actions.mp4`、
`native-102/capture.json`、`102-native-report.json`、`102-menu-state.jsonl`、
`102-host-doctor.json`、`102-host-doctor-final.json`、`102-before-status.json` 和
`102-before-logs.jsonl`。模拟器顶部柔光到身体入场回执间隔为 519 ms；
两个跨设备 500 ms 间隔仍须真机测量。

1.0.2 System Update SHA-256：
`13bbbfee5f913088870bd23381a407e25b12663bf144502c2bf4d35c92b3cd0f`。
1.0.2 完整浏览器 BIN（16,777,216 字节）SHA-256：
`0924d08830a5fbb7df18ffd3c178bd175cbad23ee2a9252b44cb1f9aa9cdc5ff`。
两个文件均已导出，均未写入设备；原有获批准镜像保留。

## 1.0.0 / 1.0.1 基线记录

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
| Vibe Mode 往返 | 同一 Device ID 从 normal 进入 recovery 0.1.4，再经 Iris app-update 返回健康 normal；每次重启均取得新 Boot ID、crash_count=0，OTA image_state=2 | 软件进入会持久选择 factory，拔插不能代替应用更新返回；修正了之前的错误指引 |
| 保留 Wi-Fi 与配对 | 1.0.1 通过两块 USB RPC 完成证书与 token 配对，板内复用 Vibe 保存的 Wi-Fi；自动重启后 WSS、资源校验完成 | 密码未传回 Mac；首次单包超过 Iris 1024 字节上限的问题已修复，并增加载荷边界测试 |
| 真机双向传送 | 最终 Mac .app 将 Coo 迁入物理板子，事务结束 owner=device、epoch=1；Iris 截图显示 Coo；返回完成 owner=desktop、epoch=2、transfer=null、ready=true | Wi-Fi DTIM 省电曾造成时钟质量反复失效，现使用 WIFI_PS_NONE；时序误差与功耗尚未量化 |
| 1.0.1 修复验证 | 固件构建 `20261001-154951-build-20471` 成功，应用 2,255,904 字节；2 项 Python 配对分块与超限测试通过并纳入 CI | 设备应用描述符 SHA 与当前构建一致，正常运行无崩溃；不是 30 分钟稳定性结论 |
| 首次公开源码 | `Colezhm/mosaico-coopanion` 提交 `d61fc0c15185734e4d51a2696bd33555eda6eb0a` 的树与本地 `944a5cc` 完全一致；GitHub CI 通过 | 源码树 `967a9539cdd52e5c591c7dd5b5b66acc67142e7d`；构建产物与私有配对文件不在源码中 |

完整 BIN SHA-256：
`0b5700d8f28409b550bdcbf6b66866f804717a4ba1fbb37068b4e3b0e0ac6aa2`。
镜像覆盖 `0x000000–0xFFFFFF`，含旧 NVS、配对、系统元数据、资源缓存与崩溃记录；
不写 NAND。本次覆盖已按根 AGENTS.md 获得用户当次确认。

Mac arm64 ZIP SHA-256：
`796a50f077e5e485c7ae560d33551e2f9eec00b63b186537367fe724a5868b07`。
本机原始证据保存在 `artifacts/mosaico-coopanion/device-first-boot.{json,log,png}`、
`device-enter-vibe.log`、`device-vibe.json` 和 `device-vibe.png`；这些文件不公开设备身份。

1.0.1 增量更新仅写应用，保持完整分区表、Wi-Fi、配对及资源。
另行导出的 `mosaico-coopanion-1.0.1-full.bin` 为 16,777,216 字节，SHA-256
`317804deb1d7101c784372af861b1676dc2d7262ce9304268471356d86f7e6f6`；没有将它整盘写入设备，
原获批准的完整镜像仍保留。实时证据补充于 `device-return-application.log`、
`device-returned-ota-state.json`、`device-pair-101-chunks.log`、`device-101-final-status.json`、
`device-coo-resident.png`。

尚未通过：真实 500 ms 间隔 ±80 ms、板端 ≥30 fps、IMU 反馈 ≤100 ms、真实语音识别和
扬声器自声、连续录音、真实低电量与运动校准、30 分钟稳定运行。
截图、编译和模拟器日志不能替代这些结果。Windows、Intel Mac、Linux 构建运行亦未实测。

重现命令、版本锁定、协议边界和已知功能限制见 [开发与使用](coopanion-implementation.md)。
