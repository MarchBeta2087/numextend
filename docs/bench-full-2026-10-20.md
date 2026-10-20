# 完整性能实测（2026-10-20，nex + contrib）

> **单机快照，非 CI 基线、非产品承诺。** 绝对耗时依赖硬件、编译器与系统负载；
> 请勿跨机器直接比较，也不要把本页数字当作回归门禁（CI 用
> `bench/baseline/` 的 advisory 基线）。

## 环境

| 项 | 值 |
|---|---|
| 日期 | 2026-10-20 11:28–13:30 UTC（总 7308 s ≈ 2 h 02 min） |
| commit | `bbd051d`（`bench` 支持 `--suite/--suite-args` 时） |
| 操作系统 | Windows 11（10.0.26100） |
| CPU | 13th Gen Intel Core i5-13500H（16 逻辑核） |
| 内存 | 31.7 GB |
| GPU | NVIDIA GeForce RTX 4050 Laptop（6 GB 显存） |
| 编译器 | MinGW-W64 GCC 15.2.0（ucrt, posix-seh） |
| 构建 | Release（`-O2`），CMake preset：`bench` / `bignum` / `thread` / `opencl` |
| Python | 3.13.7（仅 runner） |

## 方法

- 入口：`scripts/bench.py`（`--profile full` + `--include-frozen`），只跑
  **full + frozen** 档，跳过 ci/quick 的小规模重复项。
- 全局预算 10800 s（3 h）；实际 7308 s，无 `skipped(budget)`、无 timeout。
- 每 100 ms 采样峰值 RSS；OpenCL 为可选项（本机有 GPU，正常执行）。
- 命令：

```sh
python3 scripts/bench.py --all --include-frozen --profile full --max-time 10800 \
  --exclude 'core.smoke' --exclude 'core.sweep.ci' --exclude 'core.math.ci' \
  --exclude 'core.sweep.deep' --exclude 'core.math.quick' \
  --exclude 'contrib.block.1e6' --exclude 'contrib.parallel.ntt' \
  --exclude 'contrib.opencl.madddot' --exclude 'contrib.opencl.matmul.512' \
  --exclude 'contrib.opencl.cpu.512' \
  --json build/bench/full.json --markdown-out build/bench/full.md \
  --html-out build/bench-full.html
```

产物：`build/bench-full.html`（主报告）、`build/bench-full-logs/`（每 suite
日志 HTML + index）、`build/bench/full.json`、`build/bench/*.log`。这些在
`build/` 下，不随仓库提交；需要归档时作为 issue/Release 附件。

## 结果

| suite | tier | 耗时 | 峰值 RSS | 说明 |
|---|---|---:|---:|---|
| `core.sweep.full` | full | 450.4 s | 291 MB | 全规模扫描（bigint/NTT/FFT/dec/float 等） |
| `core.math.full` | full | **5780.1 s** | 8 MB | 超越函数 / 常数 / 吞吐量全量 |
| `core.huge.1e8` | full | 178.4 s | 930 MB | 1e8 位直接 NTT（`bench_huge`） |
| `contrib.block.1e8` | full | 217.7 s | 924 MB | 1e8 位分块 NTT |
| `contrib.opencl.matmul.4k` | full | 337.4 s | **15,664 MB** | 4k×4k、60+60 并集（A/B 各 ~7 GB 走 RAM） |
| `contrib.ssa.1e6` | frozen | 33.2 s | 115 MB | SSA 原型 1e6 肢（冻结） |
| `contrib.ooc.1e7` | frozen | 6.5 s | 132 MB | OOC 1e7 位（冻结） |
| `contrib.ooc.1e8` | frozen | 303.9 s | 652 MB | OOC 1e8 位，4 个块对，读 0.18 GB / 写 0.10 GB |

合计 **7308 s（约 2 h 02 min）**，8/8 通过。

## 解读

- **数学专项** 5780 s 与 `docs/bench-math.md` 的历史全量 5791.8 s（WSL/GCC 13.3）
  基本一致，说明该路径的性能稳定；差异主要来自 OS/编译器。
- **1e8 位直接 NTT** 178.4 s / 930 MB，与历史 195.8 s 同量级（本次机器/编译器更快）。
- **OpenCL 4k×4k** 337.4 s / **15.7 GB 峰值**：时间与历史 327.1 s 接近；
  内存是硬门槛——A/B 各约 7 GB 在 RAM，tile 显存在预算内。
- **OOC** 用默认块长（2^23 肢）跑 4 个块对，303.9 s / 652 MB；OOC 的定位是
  “低内存换时间”，不要拿它和内存内 NTT 的绝对耗时直接比。
- **frozen（SSA/OOC）** 只是保留门槛数据的原型，不代表活跃功能（§K2）。

## 边界

- 单次运行、单机；Windows/MinGW，未做多次取最小值或温控归一。
- `core.math.full` 清单预算 5400 s，实际 5780 s：`bench_math` 在操作之间检查
  预算，长操作会小幅超出（非截断）。
- OpenCL 性能数仅本机 RTX 4050；CI 只有 PoCL CPU 正确性，**不可复现**，
  因此 P4 仍保持实验状态。
- 本页是**快照**，不参与文档漂移检查；新数据请新增日期页，不要改历史数字。

## 相关

- 工具与 suite 定义：`docs/benchmarking.md`、`bench/suites.json`
- 规划与结果讨论：issue #128
- 历史基线：`docs/bench-math.md`、`docs/performance.md`、`bench/baseline/`
