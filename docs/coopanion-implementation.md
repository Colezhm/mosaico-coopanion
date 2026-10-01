<!-- Owner: desktop/coopanion/packages/cortico-world-desktop-pet/src/mosaico/, projects/coopanion/ -->

# Mosaico × Coopanion 开发与使用

同一个 Coo 在电脑与 Mosaico 之间交接身体，继续使用电脑上的角色身份、会话与记忆。
板端运行原生 C、GSP Canvas、传感器和 Xiaole 中文 TTS；电脑承担 FunASR、模型和电脑任务。
当前为待真机验收的开发候选版。验收进度见 [coopanion-acceptance.md](coopanion-acceptance.md)。

## 固定来源

| 组件 | 版本 |
|---|---|
| Coopanion（MIT，保留原 LICENSE） | `78a46f83d779836c182d8176234c93c26c02d539` |
| Cortico 子模块 | `9a1d562201413e6751a17f732d972ef19b86b466` |
| ESP-IDF / esp32s31 | `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` |
| BSP | `a2c985ad698b3f04a21c63e006b43e7e948bc1d7` |
| utils | `fc63d43f3ac72aa54e701b07b07fbed07a16b3ca` |
| GSP / Simulator / GSPC | `1.5.1` / `1.5.1` / `0.6.1` |
| ESP-SR / WebSocket client | `2.4.7` / `1.6.1` |
| Electron / pnpm | `44.4.4` / `11.5.0` |
| 保留 Vibe Mode | utils 评审包 `0.1.4` |
| 板端应用描述符版本 | `1.0.0`（开发候选固件，并非硬件验收结论） |

桌面扩展遵循 [Cortina](https://github.com/Pal-AI-Lab/Cortina/tree/454895173ebbac408f0840679d79738185a9e202)
的分层和三级验证流程。开发入口为 `desktop/coopanion/AGENTS.md`，实际接口以固定 Cortico 源码为准。
Mosaico 作为现有 `desktop-pet` World 的设备后端；Cortico Core、Persona 和 Memory 无修改。
身体归属、传送记录与表情缓存位于 `<部署 dataDir>/desktop-pet/mosaico/`；表情缓存不代表人格记忆。
`desktop-pet.presence` 以 piggyback 投递可核实的连接与身体状态。禁用 Mosaico 时不添加传送工具。

锁文件：桌面 `pnpm-lock.yaml`、安装包 `installer/package-lock.json`、板端 `dependencies.lock`。
Noto Sans SC 来自 Google Fonts `ofl/notosanssc`，用 fontTools 4.61.1 实例化为字重 450，
保留 SIL OFL。编译字集为 GB2312；生僻字可能显示缺字。Coo atlas 从原项目 SVG 导出。

## 构建

根目录初始化所需子模块，不更新到最新分支：

```sh
git submodule update --init --recursive submodule/esp-mosaico-utils submodule/esp-mosaico-bsp desktop/coopanion/vendor/cortico
cd desktop/coopanion
pnpm install --frozen-lockfile
pnpm run typecheck
pnpm run typecheck:worlds
pnpm run build:cortico
pnpm run typecheck:web
pnpm test
pnpm run test:worlds
pnpm --dir packages/cortico-world-desktop-pet typecheck
pnpm --dir packages/cortico-world-desktop-pet test
pnpm --dir packages/cortico-world-cua typecheck
pnpm --dir packages/cortico-world-cua test
pnpm --dir vendor/cortico check:extension ../../packages/cortico-world-desktop-pet
pnpm --dir vendor/cortico check:extension ../../packages/cortico-world-cua
pnpm exec tsx scripts/pack.ts --dir
```

Mac 产物：`desktop/coopanion/dist/mac-arm64/Mosaico Coopanion.app`。ad hoc 签名，无 Apple 公证。
其他平台保留上游构建逻辑，本次未实测。应用沿用 Coopanion 数据目录和单实例锁；
不要同时运行两个版本。测试可用 `CORTICO_COMPANION_DATA` 指定隔离目录。
新部署默认关闭遥测；已有用户保存的设置保持原值。

激活固定 ESP-IDF 后，在根目录：

```sh
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/coopanion doctor
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/coopanion build
python projects/coopanion/tools/test_native.py
python projects/coopanion/tools/export_browser_image.py --output artifacts/mosaico-coopanion/mosaico-coopanion-full.bin
```

新主机不能依赖本机忽略目录 `.tools/`。UI 源为 `ui/main.json`，可用 `tools/prepare_ui.py`
重建；默认 Coo 使用桌面包 `host/export-coo.cjs` 导出。运行时换装由网页导出，经 SHA-256
和像素边界校验后写入板端专用缓存，校验完成才开放迁入。

## 初装和配对

普通 `coopanion.bin` 不能从零地址烧录。完整导出器检查分区 MD5、评审基础组件 SHA-256、
组件边界与保留布局，包含引导程序、启动选择、Vibe Mode、应用、语音和 UI 资源。

| NOR 范围 | 内容 |
|---|---|
| `0x000000–0x1FFFFF` | 保留系统布局、Vibe Mode、系统元数据与崩溃槽 |
| `0x200000–0x20FFFF` | 应用 NVS：Wi-Fi、配对、归属、少量情绪 |
| `0x210000–0x7FFFFF` | 正常应用 ota_0 |
| `0x800000–0xAFFFFF` | Xiaole 中文语音 |
| `0xB00000–0xBFFFFF` | Coo 资源缓存 |
| `0xC00000–0xFFFFFF` | GSP UI 资源 |

**整包初装覆盖整个 16 MiB NOR，间隙写成 `0xFF`，既有设置、配对、系统元数据、缓存及
崩溃记录会丢失。保留的是布局及 Vibe Mode 功能，不是旧数据。** NAND 不由镜像写入，
应用只初始化现有映射，不格式化或占用用户扇区。实际覆盖前展示本次镜像路径、大小、
SHA-256、目标板与范围，并取得当次确认。
沿用 [官方浏览器烧录器](https://mosaico.espressif.com/firmware-update/)的 Local firmware；
不要选择默认 Claw 镜像。完成后通过 Iris 验证 normal → Vibe Mode → normal。

正常启动后，通过 USB/Iris 配置：

```sh
python mosaico.py iris status --all --json
python mosaico.py iris list --project projects/coopanion --details --json
# DEVICE_ID 来自实时 Iris 握手，不是串口名。
python projects/coopanion/tools/pair.py --device-id DEVICE_ID --directory /private/path/coo-pairing --deployment-dir /path/to/companion-deployment
```

工具交互询问 Wi-Fi 密码，不将密码放进进程参数或日志。`--prepare-only` 仅生成私有文件。
默认主机名 `coo-DEVICE_ID.local`、端口 19773。首次生成时可用 `--address 192.168.x.x`
将固定 IP 写入证书 SAN；更换 IP 应使用新配对目录重新配置，不能只修改连接地址。
RPC `0x434f/1` 保存后需要正常重启。TLS 要求正确时钟，当前通过 SNTP 设置，不绕过验证。
`--deployment-dir` 必须指向已有 `config.json` 的部署目录；工具仅合并
`CORTICO_MOSAICO_TOKEN` 到其 `.env`，保留其他密钥，拒绝覆盖不同的已有令牌。
省略此项时，令牌保存在私有配对目录的 `.env`，需要导入部署 `.env`。
World 只通过 `WorldContext.secret()` 读取令牌，`pairing.json` 不包含令牌；TLS 私钥仍在私有目录。
在桌面「Mosaico」→「配置」启用 Mosaico、选择生成的 `pairing.json` 后重启；可关闭自主迁移。
配对私钥、token 和 Wi-Fi 密码不得提交仓库。FunASR 模型仍从上游语音设置准备。

## 交互与实现边界

- 电脑状态短按 AI 键召唤，不同时录音；板端按住倾听、松开发送，16 kHz PCM16 单声道，最多 30 秒。
- 音频任务先关闭扬声器再开启麦克风，丢弃初始 80 ms 输入。断线、丢包、剧烈运动和过短录音不提交残缺指令。
- 100 Hz IMU；0.15g 摇晃、0.4g 踉跄、1.2g 或 300°/s 摔倒。稳定 500 ms 后爬起，阈值尚待实测校准。
- 触摸安抚、哭泣、转身生闷气与离线台词在本地运行，主动语音反馈最短间隔 15 秒。
- 长按屏幕打开返回/静音/摸头菜单。磁传感器接近互动、落地/碰撞震动、电池困倦和板型允许的 LED 已接入。
- 同一个电脑会话接收两端输入。表情随传送同步，板端 NVS 保存归属、静音及少量情绪。
- 电脑工具先等待返回落地，再进入原授权规则；任务完成留在电脑。闲置迁移采用方案规定的概率和冷却。

原桌宠服务仍只绑定回环。独立设备通道使用 TLS1.2+、配对 token、`v:1`、随机 session、
递增 seq 和传送 epoch/id。拒绝浏览器 Origin、错误 token 和第二台设备。
时钟 RTT 超过 100 ms 不允许新迁移；控制 150/s、资源 4 KiB 分块最多 20 Hz，音频另有
帧序号与 960000 字节总量上限。队列有界，资源写入不在渲染线程执行。

归属日志为权威：准备成功才离开，源端隐藏持久化且渲染完成才发回执；目标等 500 ms 开光，
再等 500 ms 入场。开始离开后故障进入 recovering；必须核对源端隐藏事实、资源和时钟后继续。
超时不会另造身体，重复召唤会合并。损坏的主机归属日志会阻止扩展启动，不要删除日志来恢复传送。

## 双端模拟器

第一终端（根目录、固件依赖已解析）：

```sh
python projects/coopanion/tools/pair.py --device-id simulator --directory artifacts/mosaico-coopanion/preview/pairing --prepare-only
export COOP_SIM_PORT=19774
export COOP_SIM_ASSET_PATH="$PWD/artifacts/mosaico-coopanion/preview/sim-atlas.bin"
python mosaico.py project sim --project projects/coopanion --interactive
```

第二终端：

```sh
cd desktop/coopanion
pnpm exec tsx scripts/mosaico-preview.ts
```

打开 `http://127.0.0.1:5199/`。真实桌面网页 + 真实 WSS + 共享 C 后端，
不调用模型、不打开真实麦克风、不执行电脑任务。事件写入
`artifacts/mosaico-coopanion/preview/events.jsonl`，测试密钥和状态被 Git 忽略。

## 尚需验收

真机帧率、IMU 延迟、两个 500 ms 间隔、自声抑制、中文发音、30 分钟稳定性、
Vibe Mode 往返及其他桌面平台未由模拟器或编译证明。板端普通动作采用简化姿态，
资源是从原 SVG 预渲染的 22 姿态 atlas，并非完整矢量骨骼系统。
字幕为 GB2312、单句缓冲 383 UTF-8 字节、最多三个回答选项；长句应分句。
同一时间支持一台板子；离线只保留本地交互和固定台词，没有自由模型对话。
安装包、模拟器、扩展干装载和真机各自的通过范围见验收记录。
