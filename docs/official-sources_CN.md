# 官方资料核对与阅读范围

[工作流](vibe-coding-workflow_CN.md) · [文档索引](README_CN.md)

核对日期：2026-10-01。本记录区分页面正文、源码依据和实际操作证据。“已读取”不代表硬件功能已经测试。

## 已审阅的指南与硬件页面

已读取下列 Guide 全部栏目，中英文共 16 个页面。首次启动、控制面板、对话和 Work 是 ChatCoding 页内章节。

| 栏目 | 英文 | 中文 |
| --- | --- | --- |
| 入门 | [Guide](https://mosaico.espressif.com/guide/) | [指南](https://mosaico.espressif.com/zh/guide/) |
| ChatCoding | [页面](https://mosaico.espressif.com/guide/chat-coding/) | [页面](https://mosaico.espressif.com/zh/guide/chat-coding/) |
| VibeCoding | [页面](https://mosaico.espressif.com/guide/vibe-coding/) | [页面](https://mosaico.espressif.com/zh/guide/vibe-coding/) |
| 点子中心 | [页面](https://mosaico.espressif.com/guide/idea-center/) | [页面](https://mosaico.espressif.com/zh/guide/idea-center/) |
| FAQ | [页面](https://mosaico.espressif.com/guide/faq/) | [页面](https://mosaico.espressif.com/zh/guide/faq/) |
| 术语 | [页面](https://mosaico.espressif.com/guide/glossary/) | [页面](https://mosaico.espressif.com/zh/guide/glossary/) |
| 安全 | [页面](https://mosaico.espressif.com/guide/safety/) | [页面](https://mosaico.espressif.com/zh/guide/safety/) |
| 相关资料 | [页面](https://mosaico.espressif.com/guide/hardware-docs/) | [页面](https://mosaico.espressif.com/zh/guide/hardware-docs/) |

硬件文档已读取[入口](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/index.html)及四篇中文正文：

- [CoreBoard V1.2](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide.html)
- [CoreBoard V1.0](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide_v1.0.html)
- [摄像头模块 V1.2 / V1.4](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide_camera.html)
- [多功能交互子板](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide_interact.html)

指南内链接的芯片完整数据手册、各原理图 PDF、光学规格书，以及外站 Claw 全套教程不是本轮逐页穷尽的材料。尚未选定目标功能；下一项目涉及相应电路、时序或光学能力时，必须继续打开对应版本文件核实。不能将本记录表述为“所有链接及所有附件均已读完”。

## 软件来源与固定版本

已核对 [esp-mosaico 组织](https://github.com/esp-mosaico)中的 Vibe、Utils、BSP、Claw 职责及相关说明；仓库正文优先通过 GitHub 插件读取，已下载的固定依赖直接读源码。

| 来源 | 本轮依据 | 负责内容 |
| --- | --- | --- |
| [Vibe](https://github.com/esp-mosaico/esp-mosaico-vibe) | `95469e63b621fc6ece3b0e313af8c1ba59dcd066` | 工作区、AGENTS、技能、CLI 入口、工程选择、设备模式与迁移 |
| [Utils](https://github.com/esp-mosaico/esp-mosaico-utils) | `fc63d43f3ac72aa54e701b07b07fbed07a16b3ca` | 工具、模板、GSP、Iris、应用集成、平台上传和保留固件 |
| [BSP](https://github.com/esp-mosaico/esp-mosaico-bsp) | `a2c985ad698b3f04a21c63e006b43e7e948bc1d7` | 板型识别、板载/子板硬件及示例 |
| [Claw](https://github.com/esp-mosaico/esp-mosaico-claw/tree/master) | 本轮读取 `master` README，未作为当前应用依赖 | 设备 Agent、Lua/skills、联网服务和出厂应用 |
| [Raylib Lite Engine](https://github.com/espressif2022/raylib-lite-engine) | 工作区固定 `ca3200a6b7f63e2bdea07851e19fdbd3a42eb742`，未初始化 | 游戏运行时和 Host 模拟器，选定游戏任务后再检查 |

重点核对的本地文档为根 README/AGENTS、`docs/` 中的工程创建、CLI、设备模式、Gateway、游戏、MCP 和迁移说明，以及 Utils 的 `docs/component-boundaries.md`、`mosaico-tools/docs/application-integration.md`、工具 README、Vibe Mode README、`product_contract.json`、评审包 manifest。关键结论同时核对了 BSP 板型检测、应用 CMake、更新包检查器和实际构建输出。

## 在线烧录与点子中心

- [独立在线烧录器](https://mosaico.espressif.com/firmware-update/)：已读页面、使用 Chrome 查看连接状态，并分析页面公开脚本的文件选择与写入行为。检查时默认完整镜像为 Claw v0.4.0；它不是当前 Hello World 应用。
- 点子中心已查看首页、公开分类/列表/排行导航、出厂固件详情及安装页；发布和反馈入口提示需要登录，未进入登录后表单。
- 代表性 [Claw 0.3.1 详情](https://mosaico-ideas.espressif.com/firmware/5807b118-10bb-4143-9f23-8d5c32306b78)明确区分 Iris 包和完整 BIN；[有线安装](https://mosaico-ideas.espressif.com/flash/project/5807b118-10bb-4143-9f23-8d5c32306b78?mode=wired)显示从 `0x0` 写入由 Iris 包合成的完整固件；[在线安装](https://mosaico-ideas.espressif.com/flash/project/5807b118-10bb-4143-9f23-8d5c32306b78?mode=online)使用 Vibe Mode 和 Device Code。
- 社区应用会持续新增。本轮没有逐一审计全部社区固件的源码、评论或二进制，也没有把某个页面自述的测试结果记为本机测试。
- 没有向点子中心上传、发布、评论或提交反馈。

原始 23 个 HTTP 页面快照和网页脚本缓存在本机忽略目录 `.tools/research/`。其中点子中心首页原始 HTML 是动态应用壳，其实际内容另外通过浏览器读取。可提交的[来源清单](research/official-sources-2026-10-01.json)仅保留 URL、校验和、阅读方式及边界，不复制整篇上游内容。

## 冲突与采用规则

| 发现 | 处理方式 |
| --- | --- |
| FAQ 说 Claw v0.4.0 未实现 USB；硬件指南泛称出厂固件具备 CDC | 区分具体固件与硬件能力，通过实际握手确认；不能因出现串口就宣称 Iris 可用 |
| Vibe 网页入门先进入 ROM；当前代码工作区默认使用保留 Vibe Mode / Iris | 以具体镜像、设备状态和用户选定路径决定，不将每次更新都当成恢复出厂 |
| BSP 示例包含 `idf.py flash monitor`；Vibe 工作区要求产品 CLI 和保留布局 | 当前应用遵循工作区契约；用户指定的在线烧录走经核对的完整镜像路径，不套用通用 flash 参数 |
| 同为 `.bin`，普通应用和完整镜像写入地址不同 | 检查镜像清单与分区，不靠扩展名判断；本机已确认通用 flasher_args 与正确应用 OTA 位置不同 |
| FAQ 允许相机热插入；安全指南及模块安装步骤要求断电/关闭输出 | 本工作流采用断电插拔，摄像头仅左槽 |
| Claw README 有圆屏描述；硬件指南和 BSP 使用 480 × 480 方屏 | 本工作区以方形 480 × 480 布局和真实屏幕验收 |
| 入门页与硬件页对充满指示灯颜色描述不同 | 不以颜色推定板型或电池状态；查看丝印、BSP 检测和电量计 |
| 应用 GSP 1.5.1，保留固件 GSP 1.4.0 | 两个版本域分别锁定，不盲目统一依赖 |
| 在线烧录器 Claw v0.4.0，点子中心代表条目 Claw 0.3.1 | 视为不同发布产物，分别核对来源、布局和版本，不互换 |

未来升级时优先核对产品契约和已固定源码，再核对对应版本硬件文档与实际设备。发现无法解释的冲突时，继续完成独立工作，暂停仅依赖该冲突的实现或设备写入。
