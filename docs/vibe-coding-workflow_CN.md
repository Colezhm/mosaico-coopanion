# ESP-Mosaico Vibe Coding 工作流

[文档索引](README_CN.md) · [官方资料核对](official-sources_CN.md) · [本机验证记录](validation-baseline_CN.md)

核对日期：2026-10-01。适用于本工作区固定版本；升级 SDK、子模块、模拟器或烧录网站后，重新核对受影响步骤。

用户已确定：目前先准备环境和流程，尚未选择具体应用。以后新项目使用用户 GitHub 账号下的**公开仓库**，创建前先确认仓库名。开发板已经接入，CoreBoard 版本待确认。

## 1. 把想法变成可验证的功能

先用简短说明对齐用户要完成的事、主要交互、输入输出、离线行为和成功条件。已有明确要求直接执行；只询问会改变实现或硬件选择的缺失信息。UI 尚未确定时，提供渲染原型供用户确认，复用已经确认的设计。

为每个目标功能填写下表，查到所属官方文档、组件源码和对应示例后再实现 API 调用：

| 功能 | 验收行为 | 官方依据及版本 | 使用组件/API | 硬件、总线和资源约束 | 模拟器测试 | 真机测试 |
| --- | --- | --- | --- | --- | --- | --- |
| 例如触屏计数 | 每次点击只加一，重进页面按约定保留或清零 | 固定 utils 的 Hello World / GSP 文档 | 模板的共享 C UI 控制器 | 480 × 480、字体与 UI 资源分区 | 点击、连续点击、状态显示 | 触摸精度与实际屏幕 |
| 待选功能 | 明确可观察结果和异常分支 | 链接 + commit/组件版本 | 核对真实符号及返回值 | 板型、插槽、冲突、容量、耗电 | 能覆盖的部分 | 硬件独有部分 |

硬件事实以相应 CoreBoard/子板用户指南、固定 BSP 实现和实际检测交叉确认。组件选择优先使用项目已提供的 ESP Component Registry / ESP Pilot；不可用时读官方源码和组件文档，记录未验证的服务。点子中心用于查找范例；社区上传内容本身不能证明目标硬件兼容。

ChatCoding 是设备上 ESP-Claw 的对话/技能流程；本工作流默认开发 PC 构建的原生固件。只有目标明确需要 Claw、云模型、语音服务或网络 API 时，才配置对应依赖与凭据。

## 2. 锁定环境和硬件约束

| 项目 | 当前约束 |
| --- | --- |
| 芯片 | `esp32s31`，不能用 S3/C3 工程替代 |
| ESP-IDF | 精确提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` |
| Python | 上游最低 3.10；本机已验证 3.14.5，IDF 与 Iris 环境分开 |
| 普通 GSP 应用 | ESP-GSP / Simulator 1.5.1，GSPC 0.6.1 |
| 内置 Vibe Mode | 当前评审包 0.1.4；其 GSP 1.4.0 / GSPC 0.5.0 独立，不随应用一起升级 |
| 硬件版本 | 查看 CoreBoard 丝印，或使用 BSP 的硬件版本检测/日志；ROM 的 chip revision 不是板卡版本 |
| 工作区 | 整个仓库连同固定子模块可迁移；不只复制一个应用文件夹 |

本机终端入口：

```sh
source .tools/activate.sh
python mosaico.py doctor
```

`.tools/` 是本机忽略目录，不能作为新克隆的安装依赖。其他机器按[环境技能](../.agents/skills/espressif-env-setup/SKILL.md)安装固定 SDK，再按[构建技能](../.agents/skills/idf-low-noise-build/SKILL.md)检查。保留已有 SDK，检查实际 `git -C "$IDF_PATH" rev-parse HEAD`，不能仅看“6.2”版本号。

涉及硬件时至少检查以下约束：

- V1.0 和 V1.2 的 LCD 复位/时钟引脚互换；V1.2 拆分内外 I2C，取消可编程橙色 LED 和独立 Codec 电源控制。调用 BSP 的板型适配接口，检测失败必须报告，不猜 GPIO。
- 摄像头只接左槽。模块 V1.2 是 OV3640，V1.4 是 SC101IOT；模块版本与 CoreBoard 版本分别记录。按安全指南断电插拔。
- 摄像头占用 GPIO33/34，左侧 USB Serial/JTAG 不可同时使用；GPIO14 改作 DVP 后暂停左槽 EEPROM 访问。使用组件的申请/释放接口，取帧后归还，重启相机前归还所有帧。
- 右槽 GPIO37/40/49/52/54 与音频 I2S 复用，使用麦克风/喇叭时不能再分配给扩展设备。
- 16 MiB NOR、16 MiB PSRAM、128 MiB NAND 是不同存储空间，不能合并计算固件容量；真实分区表、堆余量、帧缓冲和任务栈分别检查。
- USB-C 在 Vibe Mode 中由 ESP-Iris 使用。不要套用其他板型的 USB 驱动或让浏览器、串口监视器和 Gateway 同时占用连接。

## 3. 创建与开发

普通应用使用官方模板；游戏使用 BSP 示例和 Raylib Lite Engine，进入游戏任务时才初始化引擎。

```sh
# 在整个工作区根目录执行；将 my_app 换成选定的应用标识。
git submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp
python mosaico.py project init my_app --dry-run
python mosaico.py project init my_app
```

应用放在 `projects/`，使用相对依赖路径。不要把 Vibe Mode 固件或内部测试夹具当作应用模板。遵循[根规则](../AGENTS.md)、[应用规则](../projects/AGENTS.md)及本次任务相关技能；API、资源生命周期和失败处理以所属组件为准。

普通应用必须保留前 2 MiB 系统区域及 Vibe Mode：保留 `esp_mosaico_app_recovery`、`iris_ota_support_start()`、`CONFIG_ESP_IRIS_OTA_DEFAULT_VIA_RECOVERY=y`，并在 `project()` 前包含 `mosaico_idf_project.cmake`。产品身份、板型、布局及 Recovery ABI 必须匹配[公共集成契约](../submodule/esp-mosaico-utils/mosaico-tools/docs/application-integration.md)。

每次改动以验收行为为单位，处理初始化失败、资源释放、断网和缺少子板等分支。不要为了让更新通过而擅自改分区，或删除保留固件。

## 4. 在官方模拟器验证

```sh
python mosaico.py project sim --project projects/my_app --interactive
```

GSP 应运行与设备共用的原生 C UI、控制器和资源。游戏运行官方 Host 模拟器及共用游戏逻辑。按对应[UI 技能](../.agents/skills/mosaico-ui/SKILL.md)、[GSP 技能](../.agents/skills/gsp-sim/SKILL.md)或[游戏技能](../.agents/skills/mosaico-game-development/SKILL.md)执行。

验收至少覆盖目标功能的初始状态、触摸/按键、页面切换、计时器、重复操作及失败状态，检查 480 × 480 画面中的裁切、中文字体、资源缺失和反馈。保存交互步骤、日志、运行版本和关键截图；修复后重跑受影响流程。截图只能证明画面，必须有操作后的状态变化证据。

模拟器无法证明真实传感器、音频、相机、无线连接、电池续航和实际触摸时延。把这些列为真机验收项；仅对硬件独有问题或模拟器未覆盖的行为采用真机优先，并写明缺口。

## 5. 构建和准备可审查固件

```sh
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/my_app doctor
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/my_app build
# 离线打包，不触碰开发板
idf.py -C projects/my_app system-update-bundle
```

检查实际镜像大小和目标分区容量、UI/资源是否齐全、产品契约、版本以及 SHA-256。保存构建结果和源 commit、SDK SHA、子模块 SHA、组件解析版本。编译成功不等于设备验证通过。

| 文件 | 用途 | 能否直接交给独立官方在线烧录器 |
| --- | --- | --- |
| `my_app.bin` | 普通应用单镜像 | 否 |
| `my_app-system-update.irisfw` | 带 manifest、分区表、应用和资源的 Iris 包 | 否；用于 Iris/点子中心 |
| 经验证的完整 `.bin` | 包含与应用匹配的引导、分区、维护固件、启动选择和资源，按整机地址布局 | 可以，必须确认该文件确实以 Flash `0x0` 为起点 |

**禁止把普通构建的 `flasher_args.json` 直接用于合并后烧录。** 本次 Hello World 的通用参数把应用映射到 `0x20000`，该位置属于保留 Vibe Mode；官方 System Update 包则正确映射到 `ota_0` 的 `0x210000`。普通应用的 bootloader 也不能替换评审过的保留 bootloader。

当前固定 CLI 已实测能生成并检查 `.irisfw`；其公开命令没有本次验证过的本地完整 BIN 导出入口。官方点子中心的 Iris 项目详情提供“下载完整 BIN”，有线安装页说明由 Iris 包合成完整固件。使用该路径时核对平台 Header/基础固件与本项目契约；平台包不能仅凭应用版本相同就当作本地同一产物。

后续每个项目必须在烧录前交付实际完整 BIN 和镜像清单。如果不经点子中心取得完整镜像，就需要针对该项目另外完成合并工具、启动选择及整包校验；目前不能把这项未完成的工作当作已有自动化能力。没有合格完整镜像时，先报告这一具体缺口；用户同意改用官方 Iris CLI 才执行下面的更新路径。向点子中心上传或发布是独立外部操作，不由 GitHub 推送授权自动涵盖。

## 6. 连接、烧录和真机验收

### 用户指定的官方在线烧录器

入口：[mosaico.espressif.com/firmware-update](https://mosaico.espressif.com/firmware-update/)。使用支持 Web Serial 的桌面 Chrome/Edge 和 USB 数据线。

1. 固定本次目标板、应用版本、完整镜像路径、大小和 SHA-256，展示将覆盖的固件/设置范围；保存能够读取的必要日志及用户数据。页面明示会清除设备数据，需要在实际覆盖前取得确认。尚未选择应用时，只检查连接。
2. 核对 Gateway/串口归属，结束本任务的占用；已有其他写入未完成时不能开始浏览器写入。
3. 需要进入 ROM 模式时由用户操作：关机，按住 BOOT，再按电源开机。黑屏正常。AI 键进入的 Vibe Mode 是另一种模式。
4. 点击 Connect，选择本次目标设备；核对 ESP32-S31 和 Flash 容量。浏览器授权设备选择窗或实物按键无法由工具操作时，由用户完成这一小步，随后继续。
5. 选择 **Local firmware**，载入第 5 步已验证的完整 `.bin`，核对页面显示的文件名。页面默认的 Claw 出厂固件仅用于用户明确要求恢复相应出厂固件的任务。
6. 确认具体镜像和清除范围后才点击 Flash firmware 并处理确认框。保持供电和连接，保存网页日志；失败时保留结果，不盲目连续重刷。
7. 按页面指引重启并释放浏览器串口，进入下面的真机验收。网页进度 100% 或“完成”不能代替目标应用启动验证。

已检查的网站脚本从 `0x0` 写入所选文件，文件选择层主要检查 `.bin` 后缀和非空，不能替我们验证保留布局、资源或正确应用。网站内部还容忍一种进度已达 100% 的末尾超时，因此必须独立检查重启后的行为。

### 官方 Iris 更新路径

适用于用户选择该路径、且有兼容 Vibe Mode 的设备。命令负责布局检查、切换、更新和重连：

```sh
python mosaico.py iris status --all --json
python mosaico.py iris list --project projects/my_app --details --json
# 新应用、分区变化或外部资源变化
python mosaico.py iris system-update --project projects/my_app
# 仅代码变化，完整分区表和资源均相同才用：
python mosaico.py iris app-update --project projects/my_app
```

首次初始化/恢复使用 `python mosaico.py recover`，它会写基础固件；进入已有 Vibe Mode 用 `iris test enter-recovery`。不要为了看状态就运行 `recover`。具体选择和设备标识规则见[CLI 文档](mosaico-cli_CN.md)与[Gateway 文档](project-gateway_CN.md)。

点子中心另有两条路径：其“在线烧录”使用 Vibe Mode、网络和 Device Code；其“有线烧录”使用 ROM 串口和合成完整 BIN。这些名称不能与独立在线烧录器的操作混同。

### 真机通过条件

- 核对实时设备身份和目标固件版本/ELF 哈希；Iris 应用还要核对 role、board、layout、ABI 和健康状态。每次实际重启产生新的 Boot ID，设备身份持续一致。
- 按第 1 步的功能验收表执行实际屏幕、触摸、按键和涉及的外设测试，验证异常分支、冷启动及再次重启。
- 保留 Vibe Mode 的应用完成 normal → Vibe Mode → normal 往返。对明确不接入应用侧 Iris 的特殊固件，记录替代证据和不能完成的检查，不能称 Iris 验收通过。
- 通过 `mosaico.py iris device-status --json`、`operation-status`、`logs --snapshot` 和 `screenshot` 保存适用证据。设备截图直接检查保存的图片，不拿 Gateway 网页截图冒充设备屏幕。
- 记录 PASS / FAIL / 未测及理由。上传完成、ROM 连接成功、发现串口、模拟器通过均不是整机验收通过。

## 7. Commit、创建公开仓库和推送

新项目先向用户提出具体仓库名，例如“建议 `<project-name>`，公开，位于你的 GitHub 账号下”，**得到名称确认后才创建**。用户已经确定公开可见性，不反复询问该偏好。还没有目标功能时不提前创建空仓库。

保留整个可重建工作区及固定子模块；官方来源保留为 `upstream`，新建用户仓库作为 `origin`。仅在创建用户仓库时完成这一远程调整，不能向 `esp-mosaico/esp-mosaico-vibe` 推送用户应用。使用 `codex/` 前缀的工作分支；空的新仓库可将其推为 `main`，既有仓库按既有分支规则操作，避免覆盖已有历史。

提交范围包含应用源码、资源源文件、构建配置、必要说明、测试及经过清理的验证记录。按官方忽略规则排除构建目录、SDK、缓存和本机状态；另检查凭据、Wi-Fi 密码、设备 token、私有截图、绝对路径和不应公开的日志。保留上游许可证、第三方许可和来源说明。固件二进制作为明确版本的交付物记录哈希，不用它代替源码。

流程顺序：

1. `git status` 与 diff 审查，只 stage 本任务文件；使用用户已有 Git 身份，不修改全局身份。
2. 对实际变更执行相关验证，记录仍未测的硬件功能，完成 commit。
3. 在用户账号创建名称已确认的公开仓库；通过 GitHub 插件可用能力、已认证 CLI 或浏览器完成，缺少登录时仅交接登录步骤。
4. 核对 remote 指向用户仓库后 push。核对远程分支 SHA 与本地 commit 一致，并确认源码、子模块和文档可见。
5. 交付仓库链接、commit、固件版本/哈希、模拟器证据、真机证据和未测项。创建 PR 时附上 PR，不把“本地 commit”说成“已经推送”。

发布到点子中心按用户另行要求执行：官方 `project upload` 只创建/更新草稿，不能把 draft 当成公开发布或审核通过。

## 新项目的完成标准

需求和技术依据可追溯；实现符合固定版本及板卡约束；官方模拟器的相关交互通过；指定烧录路径实际完成；目标固件在同一设备通过真实功能与重启验收；用户确认名称的公开 GitHub 仓库已有匹配 commit。任何一项未完成，都在交付中明确标出。
