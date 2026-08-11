# Numextend 维护手册

面向仓库所有者的操作手册：一次性初始化、日常开发、发版、热修复。
贡献者侧规范见 `.github/CONTRIBUTING.md`。

## 1. 分支模型

```
feature/xxx ──PR(squash)──▶ dev ──PR(merge)──▶ main ──tag──▶ Release
     ▲                        ▲                    ▲
     └──────── 热修复 hotfix/xxx ──────────────────┘
```

规则：

- `main` / `dev` **禁止直接 push**，一切变更经 `feature/xxx` + PR；
- `dev` 为集成分支，须始终保持可发布（CI 在每次 push 与 PR 上运行）；
- `main` 仅在发版点由 `dev` 合并而来，合并后打 tag 并发布 GitHub Release；
- 合并策略：feature → dev 用 **squash**（dev 历史线性）；dev → main 用
  **merge commit**（保留发版批次分组）。

## 2. 一次性初始化（首次发布前）

### 2.1 推送引导提交（保护规则生效前的唯一直接 push）

```bash
git push origin main     # 含 CI 的 dev 触发规则
git push origin dev      # 含社区设施与维护手册
```

### 2.2 GitHub 网页设置

1. **默认分支改为 `dev`**：Settings → Branches → Default branch。
   （新 PR 默认指向 dev，符合 feature→dev 流程）
2. **给 `main` 和 `dev` 各加一条分支保护**（Settings → Branches → Add rule）：
   - ☐ Require a pull request before merging
   - ☐ Require status checks to pass before merging（勾选 CI）
   - ☐ Require branches to be up to date（建议 dev 勾选）
   - ☐ Include administrators（禁止管理员旁路）
   - ✗ Allow force pushes / Allow deletions（保持关闭）
3. **开启自动合并**（可选，单人维护提速）：Settings → General →
   Allow auto-merge。
4. **确认 CI 首次运行**：Actions 页面上 3 平台工作流全绿。

## 3. 日常开发

```bash
git checkout dev && git pull
git checkout -b feature/<name> dev   # 从 dev 切
# ... 开发 + 本地测试 ...
git push -u origin feature/<name>    # 推功能分支
# GitHub 上开 PR：feature/<name> → dev（CI 自动跑）
# 合并用 squash；之后本地：
git checkout dev && git pull
```

本地验收线（提交前）：

```bash
ctest --test-dir build                # 26 项全绿
NEX_GOLDEN_SEED=1 ctest -R golden     # 额外种子（2 个）
NEX_GOLDEN_SEED=2 ctest -R golden
sh scripts/coverage.sh                # 覆盖率不低于现有基线（约 84%）
```

## 4. 发版流程（dev → main → tag → Release）

```bash
# ① 确认 dev 全绿、README/设计文档已同步
# ② 开 PR：dev → main，合并用 merge commit
# ③ 打 tag 并推送：
git checkout main && git pull
git tag v0.1.0
git push origin v0.1.0
# ④ GitHub Releases 页把 tag 转成 Release：
#    标题、变更说明（可从 git log main 摘录）、附件（源码 zip 自动）
# ⑤ 视需要把 main 合并回 dev（保持同步）：
git checkout dev && git pull
git merge main && git push
```

版本号语义（SemVer）：破坏性 API 变更 → 0.x 递增主位；新功能/新模块 →
次位；纯修复 → 修订位。发布前检查 `LICENSE` 版权年份/署名。

## 5. 热修复

```bash
git checkout -b hotfix/<name> main    # 从 main（或最近的 release tag）切
git push -u origin hotfix/<name>
# 开两个 PR：hotfix/<name> → main（合并后立即打 patch tag v0.1.1）
#            hotfix/<name> → dev（修复合回集成分支）
```

## 6. 常见操作速查

| 需求 | 命令 |
|------|------|
| 同步 dev 与 main | `git checkout dev && git merge main && git push` |
| 回滚 dev 上某 squash 提交 | `git revert <hash>`（再走 PR 合入） |
| 回滚已发版 main | `git revert` + patch 版本；不 rewrite 已推送历史 |
| 查看发布历史 | `git log main --oneline`、`git tag -n` |
| 本地强制验证 MSVC | `scripts/build-msvc-debug.bat` → `cd build-dbg\tests && runall.bat` |

## 7. 纪律提醒

- 不要 `--force` 推送 `main`/`dev`（保护规则已禁，管理员旁路也建议勾选禁止）；
- 合并到 `dev` 前本地至少跑一遍 ctest；发版前跑一遍三平台 CI；
- 语义变更必须同步 `docs/design.md` 修订记录与 §10 转换矩阵；
- 新前缀/新类型须登记 `coding_standard.md` 附录 A 缩写表。
