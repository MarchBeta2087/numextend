# bench_math 夜间趋势基线

本目录存放 nightly `bench_math --preset ci` 的趋势基线：

- 文件名：`bench-math-ci.json`（历史命名；nightly artifact 里的原名是
  `build/bench/core.math.ci.json`，内容为 `bench_math --preset ci` 的 JSON Lines）；
- 生成环境：GitHub Actions **`ubuntu-24.04`（固定）** 上的
  `.github/workflows/benchmark.yml`（默认分支，当前为 `main`）；
  `ubuntu-latest` 正在向 Ubuntu 26 迁移，固定镜像避免基线漂移；
- **不要用本地/其他机器的结果充当基线**——绝对耗时不可跨环境比较。

## 更新流程

1. 等待一次成功的 nightly（或手动 `workflow_dispatch`）运行；
2. 从 run artifact `performance-report` 下载 `build/bench/core.math.ci.json`；
3. 确认该次运行未触发预算截断、无已知性能回归；
4. 以 `bench/baseline/bench-math-ci.json` 为名提交（保持 compare 脚本路径不变）；
5. 在 PR/提交说明中记录 runner、日期与更新原因。

建议节奏：每次 v1.x 发布、或有意改变算法/阈值后更新一次；不要每个 PR 都改。

## 阈值策略（先趋势、后门禁）

`scripts/bench-math-compare.py` 默认 **advisory**（永远退出 0）：

- 相对阈值 **±20%** 记为回归/改善；
- 绝对值 **< 10 µs**（`ms < 0.01` 或 `ops/s > 1e5`）的项视为低噪声，
  不参与门禁判断，但仍在报告中列出；
- 建议连续 **3 次** nightly 同方向超阈值再开 issue 排查，避免 runner 抖动误报；
- 稳定后再考虑启用 `--fail-over 1.5`（50%）作为可选门禁；
- 报告写入 GitHub Step Summary，并随 artifact 上传。

## 本地对照

```bash
# 统一入口（推荐）：先跑 ci 档，再对比
python3 scripts/bench.py --all --profile ci --no-build
python3 scripts/bench-math-compare.py \
  --current build/bench/core.math.ci.json \
  --baseline bench/baseline/bench-math-ci.json \
  --out bench-math-compare.md
```

直接跑 `scripts/bench-math.sh ci all 300` 时，当前文件在
`build-bench/bench-math-ci.json`，把 `--current` 指到那里即可。
