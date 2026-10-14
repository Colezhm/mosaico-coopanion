<!-- Owner: desktop/upstream/ -->

# 桌面端与上游 Coopanion

`desktop/coopanion` = 上游 [Pal-AI-Lab/Coopanion](https://github.com/Pal-AI-Lab/Coopanion) 的某个发布
（`upstream.json` 里钉住的提交）+ 这里的补丁 + Mosaico 自有文件。Mosaico 的功能代码不改上游文件，
全部在 `packages/coopanion-mosaico/`，经桌宠 World 的「身体转接」接口（`PetBodyRelay`）接入。

| 组成 | 位置 | 同步时 |
|---|---|---|
| 上游源码 | `desktop/coopanion/` 除下两行外的全部 | 整体替换成钉住的提交 |
| Mosaico 自有文件 | `upstream.json` 的 `keep`：`packages/coopanion-mosaico/`、`console/features/mosaico/`、`installer/package-lock.json` | 不动 |
| 补丁 | `patches/*.patch`，按文件名顺序 | 替换后重新打上 |

现有补丁：

| 补丁 | 内容 | 是否打算提给上游 |
|---|---|---|
| `0001-desktop-pet-body-relay` | 桌宠 World 的身体转接接口（`src/relay.ts`）及测试、文档 | 是，草稿见 `PR-body-relay.md` |
| `0002-mosaico-app` | 应用接上 Mosaico、人格按形象切换；应用名、appId、版本、发布地址改为 Mosaico 分支；遥测默认关闭；打包带上 Mosaico 包和锁定的运行时依赖 | 否，Mosaico 分支自己的 |
| `0003-desktop-pet-talk-key-keeps-mic-closed` | 说话键读不到时麦克风保持关闭，不再改为一直收音 | 是，草稿见 `PR-talk-key.md` |

上游合并了 0001 和 0003 之后，`patches/` 只剩 0002（约 40 行，品牌与装配）。

## 升级到上游新版本

在仓库根目录：

```sh
python desktop/upstream/sync.py --ref v0.1.27
cd desktop/coopanion && pnpm install
```

脚本取上游该标签，替换 `desktop/coopanion`，把 `vendor/cortico` 子模块钉到上游那一版用的提交，
打上补丁，并改写 `upstream.json`。然后跑 `docs/coopanion-implementation.md`「构建」一节的全部检查，
包括 `pnpm run test:worlds`（含 Mosaico 包的测试）。上游改了运行时依赖时，移走
`desktop/coopanion/installer/package-lock.json` 再打包，让它按新依赖重新生成。

## 补丁打不上时

脚本会在冲突的文件旁留下 `*.rej` 并以非零退出。在一个新目录里做出「上游 + 补丁」的 git 提交栈来解决：

```sh
python desktop/upstream/sync.py --stack /tmp/coopanion-stack --ref v0.1.27   # git am -3,停在冲突处
cd /tmp/coopanion-stack       # 解决冲突后 git am --continue
git format-patch v0.1.27 -o /tmp/patches-new
```

把新导出的补丁按原文件名放回 `patches/`，再运行一次不带 `--stack` 的同步。修改某个补丁的内容也用同一方法：
在提交栈里改，`git commit --amend` 或 fixup 到对应提交，再导出。

## 许可证

上游自 2026-10-01（`76bda83`）起为 AGPL-3.0-or-later，向上游提交需签 CLA（上游 `CLA.md`）。
基于 v0.1.26 的桌面端整体按 AGPL 分发，源码公开在本仓库即满足；Mosaico 自写的模块
（`packages/coopanion-mosaico/`、`console/features/mosaico/`、补丁里的新内容）另按 MIT 授权，与 AGPL 兼容。
