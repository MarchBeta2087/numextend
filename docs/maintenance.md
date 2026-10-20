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
ctest --test-dir build                # 42 项全绿
NEX_GOLDEN_SEED=1 ctest -R golden     # 额外种子（2 个）
NEX_GOLDEN_SEED=2 ctest -R golden
sh scripts/coverage.sh                # 不低于 CI 门槛 80%（约 89%，见 design §1.5）
```

### 3.1 夜间基准与趋势基线

- `.github/workflows/benchmark.yml` 每日 04:00 UTC 在**默认分支**（当前为
  `main`）运行统一入口 `python3 scripts/bench.py --all --profile ci`（内含
  `bench_math --preset ci`），产物为 `performance-report` artifact（含
  `bench-ci.json/md`、`build/bench/core.math.ci.json` 与各 suite 日志）。
- 基线文件：`bench/baseline/bench-math-ci.json`，必须来自同一 runner
  （固定 `ubuntu-24.04`，避免 `ubuntu-latest` 迁移漂移）；**不要**用
  本地/其他机器的结果。
- 对比：`scripts/bench-math-compare.py` 默认 **advisory**（退出 0）：
  ±20% 阈值，绝对值 < 10 µs 的项视为低噪声；报告写入 Step Summary
  并随 artifact 上传。连续 3 次同方向超阈值再排查。
- 基线更新流程见 `bench/baseline/README.md`；建议每次 v1.x 发布或有意
  改算法/阈值后更新一次。
- 注意：定时 workflow 跑在默认分支，而 §2.2 期望默认分支为 `dev`；二者
  尚未统一——若改为 `dev`，基线需在 `dev` 上重新播种。

## 4. 发版流程（dev → main → tag → Release）

```bash
# ① 确认 dev 全绿、文档已同步（见 §4.1 清单）
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

版本号语义（SemVer）：破坏性 API 变更 → 递增主位；新功能/新模块 →
递增次位；纯修复 → 递增修订位。发布前检查 `LICENSE` 版权年份/署名。

### 4.1 文档同步清单（发版前必做）

逐项确认后再开 dev → main 的 PR：

- [ ] `CHANGELOG.md`：新增 `[x.y.z] - 日期` 段（Keep a Changelog），补版本链接；
- [ ] `docs/design.md`：修订记录新增版本行；§10 转换矩阵 / 语义 / 附录 A 缩写表
      随变更更新；
- [ ] `README.md`：能力矩阵、兼容性表、性能数字；
- [ ] `docs/manual/`：手写页（`00-index` / `01-introduction` / `02-conventions` /
      `03-install` / `04-examples`）随能力与用法更新；
- [ ] **重新生成手册**：`sh scripts/docs.sh`（从公开头文件生成 API 页 + Pandoc
      合成 `docs/manual.html`），提交生成物；CI `docs` job 会以
      `git diff --exit-code -- docs/manual` 校验一致性；
- [ ] `coding_standard.md`：规则变更记修订记录；新前缀登记附录 A；
- [ ] `TODO.md`：勾选完成项、更新规划状态；
- [ ] `.github/CONTRIBUTING.md`：“完成”定义若变化同步更新。

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
| 用预设构建 | `cmake --preset opencl && cmake --build build/opencl`（需 CMake ≥3.19 + Ninja） |
| 清理构建/产物 | `python3 scripts/clean.py --builds --installs`（默认 dry-run；加 `--yes` 删除） |
| 查过期构建目录 | `python3 scripts/clean.py --builds --stale` |

## 7. 纪律提醒

- 不要 `--force` 推送 `main`/`dev`（保护规则已禁，管理员旁路也建议勾选禁止）；
- 合并到 `dev` 前本地至少跑一遍 ctest；发版前跑一遍三平台 CI；
- 语义变更必须同步 `docs/design.md` 修订记录与 §10 转换矩阵；
- 新前缀/新类型须登记 `coding_standard.md` 附录 A 缩写表；
- 发版前必须执行 §4.1 文档同步清单；`sh scripts/docs.sh` 重新生成后
  `git diff` 应仅包含本次有意的变更（CI `docs` job 会拦截漂移）。

## 8. 本地开发环境与基准纪律

- **构建目录**：推荐 `cmake --preset <name>` → `build/<preset>`（`default` /
  `debug` / `asan` / `tsan` / `cov` / `clang` / `opencl` / `opencl-cpu` /
  `thread` / `bignum` / `fuzz` / `bench` / `amalgam`；需 CMake ≥3.19 + Ninja，
  见 `CMakePresets.json`）。不用预设时 `cmake -B build/xxx` 亦可（最低 CMake
  仍为 3.16）。历史根目录 `build-*` 用 `python3 scripts/clean.py --builds
  --legacy` 清理（默认 dry-run，`--yes` 才删除；`--stale` 只列 CMakeCache
  源目录与当前仓库不一致的目录）。`scripts/build-msvc-debug.bat` 仍生成
  `build-dbg/`（MSVC Debug + CRT 调试堆）。
- **文档生成**：`sh scripts/docs.sh` 生成核心 API 页、contrib API 页、`docs/modules.md`、
  `docs/benchmark-suites.md`、`docs/contrib-modules.md` 与 `docs/manual.html`；
  HTML 由 CI 固定的 **pandoc 3.8.2.1** 生成（其他版本可能产生字节差异），
  `docs` job 会校验 `docs/manual.html` 与源 Markdown 同步。
- **默认优化级别**：`CMakeLists.txt` 在未指定 `CMAKE_BUILD_TYPE` 时默认
  `RelWithDebInfo`（`-O2 -g`）。历史/手工构建目录若显式配成空 build type 会是
  `-O0`（性能差数倍）。
- **基准纪律**：任何性能对比（`bench_sweep` / `bench_math`）都必须与对比基线
  **同优化级别**；否则会得出相反结论（例：`-O0` 下 8-bit FFT 看似胜 Toom-3，
  `-O2` 下全程落败）。
- **数学专项基准**：`sh scripts/bench-math.sh full all 7200`（本地全量，目标
  约 2 小时）；`trans`/`const`/`throughput` 可单独选择；CI 用 `ci` 预设
  （`--budget 300`）并上传 JSON 供趋势比较。
- **测试/黄金对拍**：`ctest --test-dir build`（含 Python/mpmath 黄金对拍）；
  大数黄金对拍脚本运行前设 `sys.set_int_max_str_digits(...)`。
- **工具链**：以环境变量 `CC`/`CXX` 或 `-DCMAKE_C_COMPILER` 指定；Windows 下
  MinGW/MSVC 路径按本机配置，**不写入仓库**。
- **过程教训（速记）**：
  - 值相关缺陷（稀疏值 `2^k±1`、`10` 的幂）是随机测试盲区 → 定向用例 + 外部
    黄金对拍；
  - 内部未规范化视图的**长度比较**是反复出现的 bug 模式 → 先裁剪前导零再比较
    （详见 `docs/design.md` §3.4）。
