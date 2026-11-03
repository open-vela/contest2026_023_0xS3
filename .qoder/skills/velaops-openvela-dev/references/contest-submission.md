# 大赛提交与仓库状态

## 规则要点（2026 首届 openvela AI 硬件开发者大赛）

- **只在 GitHub 提交**，Gitee/GitCode 只是源码镜像。
- **主作品仓**：`open-vela/contest2026_023_0xS3`，默认分支 `dev-ai-contest-2026`。
  - 流程：fork 专属仓 → 开发 → PR 回专属仓 → 可自行 review 合入。
  - 作品代码、README、docs、`logs/`、介绍文档、≤5 分钟演示视频都放这里。
- **公共仓改动**（nuttx、nuttx-apps 等）：fork 公共仓，PR 到 `dev-ai-contest-2026`，由组委会 review；不要直接塞进专属仓，也不要用公共仓 PR 代替主作品提交。
- **CLA**：首次 PR 必须用报名 GitHub 账号签署 CLA；PR 会跑 `cla/signature`，签完在原 PR 评论 `/check-cla`。不是 DCO/Signed-off-by。
- **AI Coding 日志**：必须导出到 `logs/<github_login>/<date>/<tool>__<sid>.jsonl`。官方采集器只支持 Claude Code / opencode / Codex / Kiro；Qoder 不在列表，需和组委会确认导出方式。
- **时间**：作品提交 7/1–9/20；初评 9/21–9/30；决赛 11/7–11/8。截止后收回 push 权限，之后主要是获奖后的上游 PR。规则可能调整，提交前以官方文档为准。

## 仓库与分支

| 仓库 | 用途 | 当前状态 |
|---|---|---|
| `open-vela/contest2026_023_0xS3` | 主作品仓 | `dev-ai-contest-2026` 已合入业务代码 + 补丁/docs |
| `lingview/contest2026_023_0xS3` | fork/PR 源 | 已同步到上游最新 |
| `open-vela/nuttx` | 内核/板级 | 本地 `contest/pr-velaops`，未提交公共仓 PR |
| `open-vela/nuttx-apps` | 应用/libssh2 | 本地 `contest/pr-velaops`，未提交公共仓 PR |

## 当前队伍仓内容（2026-11-02）

```text
app/velaops/                 # 19 个业务源文件，manifest 映射到 apps/examples/velaops
patches/nuttx/               # 0001-velaops-esp32s3-board.patch
patches/apps/                # 0001-libssh2-mbedtls.patch
docs/BUILD.md                # 构建与烧录
docs/tools/apply_contest_patches.sh
logs/                        # 原样保留（hanbingniao 旧日志 + README + dev_reset.py）
archive/2026-09-20/          # 旧 app/proxy/patches/docs/quickapp/board
contest2026_023_0xS3.xml     # linkfile: app/velaops -> apps/examples/velaops
```

已合并提交：

```text
ae026233 docs: 增加补丁应用脚本与构建说明
755ca79b feat(patches): 增加libssh2与mbedTLS适配补丁
747e79b6 feat(patches): 增加ESP32-S3板级与公共改动补丁
515a4ac4 feat(app): 同步VelaOps板载SSH业务代码并归档旧实现
```

- PR #8（业务代码 + 归档）已 merged
- PR #9（补丁 + docs）已 merged
- `logs/` 未被修改；历史日志作者是 `hanbingniao`，本次新提交作者是 `lingview`

## 分支保护

`dev-ai-contest-2026` 当前规则：

- `protected: true`
- ruleset `cla-check`：必须通过 `cla/signature`，必须走 PR
- 只允许 **rebase merge**
- 0 个 required approve，但仍不允许直接 push/force push 改写

因此：

- 不要对 `dev-ai-contest-2026` 做 `git push --force`。
- 已合并的汇总提交不能无痛拆分；要拆需管理员/组委会临时放开保护。
- 追加改动走新分支 + 新 PR，再 rebase merge。

## 追加提交流程

```bash
cd /home/lingview/openvela/contest2026_023_0xS3.clone
git fetch origin dev-ai-contest-2026
git switch -c submit/<topic> origin/dev-ai-contest-2026
# 修改/加文件
git add <paths>
git commit -m "type(scope): 中文描述"
git push -u fork submit/<topic>

gh pr create --repo open-vela/contest2026_023_0xS3 \
  --base dev-ai-contest-2026 \
  --head lingview:submit/<topic> \
  --title "..." --body "..."

# 等 cla/signature 通过
gh pr merge <num> --repo open-vela/contest2026_023_0xS3 --rebase
```

- 合并方式只能用 `--rebase`；`--merge` 会报 “Merge commits are not allowed”。
- 合并后 fork 默认分支不会自动更新；需要 `gh repo sync lingview/contest2026_023_0xS3 --source open-vela/contest2026_023_0xS3 --branch dev-ai-contest-2026`。
- 不要删除 `logs/`，不要把旧实现从 archive 删除；比赛要求保留日志。

## 构建（队伍仓）

```bash
bash contest2026_023_0xS3/docs/tools/apply_contest_patches.sh

export PATH="$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
./build.sh boards/xtensa/esp32s3/esp32s3-eye/configs/ai_agent
```

## 已知缺口

- 补丁按 `nuttx/boards/xtensa/esp32s3/esp32s3-eye` 生成；如果队伍仓最终强制走 `vendor/espressif` 板级，需要再做 vendor 移植。
- 当前 `logs/` 只到 9/15；之后的 Qoder 会话日志没有官方格式，不能直接补。
- 大赛提交窗口已过；追加 PR 是否符合当前阶段要求，需要和组委会确认。

## 提交前检查

- `git status` 干净；没有 `.o/.built/.depend` 进仓。
- `git diff --check` 通过。
- 补丁在干净基线 `git apply --check` 通过。
- `logs/` 未变。
- PR 标题/描述说明本次范围和验证结果。
- CLA 已签；PR 的 `cla/signature` 为 pass。
