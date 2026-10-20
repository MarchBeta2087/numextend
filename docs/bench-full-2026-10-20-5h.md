# 完整性能实测（2026-10-20，5 h 预算，18 suite）

> **单机快照，非 CI 基线、非产品承诺。** 绝对耗时依赖硬件、编译器与系统负载；
> 请勿跨机器直接比较，也不要把本页数字当作回归门禁（CI 用
> `bench/baseline/` 的 advisory 基线）。
>
> 本页是 **all-tier 全量**（ci + quick + full + frozen，共 18 个 suite）。
> 同日只跑 full + frozen 的 8 项快照见
> [`bench-full-2026-10-20.md`](bench-full-2026-10-20.md)。

## 环境

| 项 | 值 |
|---|---|
| 日期 | 2026-10-20 15:07–17:13 UTC（总 7553 s ≈ 2 h 06 min） |
| commit | `97a48c2`（运行时工作区含未提交的 `full=18000` / optional `skip_rc` 改动） |
| 操作系统 | Windows 11（10.0.26100） |
| CPU | 13th Gen Intel Core i5-13500H（16 逻辑核） |
| 内存 | 31.7 GB |
| GPU | NVIDIA GeForce RTX 4050 Laptop（6141 MiB 显存） |
| 编译器 | MinGW-W64 GCC 15.2.0（ucrt, posix-seh） |
| CMake | 4.1.0 |
| 构建 | Release（`-O2`），CMake preset：`bench` / `bignum` / `thread` / `opencl` / `opencl-cpu` |
| Python | 3.13.7（仅 runner） |

## 方法

- 入口：`scripts/bench.py --all --include-frozen --profile full`。
  `--profile full` 的 tier 集合是 `ci + quick + full`，再加 `--include-frozen`
  后为全部 18 个 suite；未使用 `--exclude` 缩减范围。
- 全局预算 18000 s（5 h）。实际墙钟 7553 s；无 `skipped(budget)`、无
  `timeout`、无 `rss`，OpenCL 3 项实际执行，无 `skip(optional)`。
- optional 语义：只有产物缺失或程序返回 `skip_rc=77`（无 OpenCL 设备/驱动）
  才记 `skip(optional)`；`timeout` / `rss` / 其他非零退出码计失败。
- 每 100 ms 采样峰值 RSS；OpenCL 4k 的 RSS 上限为 20480 MB。
- 运行期间每 15 min 检查一次进程与进度；巡检记录在
  `build/bench/full-5h.monitor.log`（`build/` 不入库）。
- 命令：

```sh
python scripts/bench.py --all --include-frozen --profile full \
  --json build/bench/full-5h.json \
  --markdown-out build/bench/full-5h.md \
  --html-out build/bench-full-5h.html
```

产物：`build/bench-full-5h.html`（主报告）、`build/bench-full-5h-logs/`
（18 个 suite 日志 HTML + index）、`build/bench/full-5h.json`、
`build/bench/full-5h.md` 与原始日志。归档 zip 见
[`bench/artifacts/2026-10-20-full-5h.zip`](../bench/artifacts/2026-10-20-full-5h.zip)。

## 结果

| suite | tier | 耗时 s | 峰值 MB | 说明 |
|---|---|---:|---:|---|
| `core.smoke` | ci | 0.7 | 6 | 固定万位规模冒烟 |
| `core.sweep.ci` | ci | 39.8 | 6 | 小规模全项扫描 |
| `core.math.ci` | ci | 72.3 | 4 | 数学专项 CI 缩减档 |
| `core.sweep.deep` | quick | 279.0 | 148 | deep 规模扫描 |
| `core.math.quick` | quick | 92.0 | 5 | 数学专项 quick 档 |
| `contrib.block.1e6` | quick | 0.4 | 10 | 1e6 位分块 NTT |
| `contrib.parallel.ntt` | quick | 6.5 | 23 | 16k–64k 肢并行 NTT |
| `contrib.opencl.madddot` | quick | 2.5 | 177 | 10k×104 肢 madd/dot（GPU） |
| `contrib.opencl.matmul.512` | quick | 2.2 | 484 | 512³ 子矩阵乘（GPU） |
| `contrib.opencl.cpu.512` | quick | 9.7 | 225 | 512³ 子矩阵乘（16 线程 CPU） |
| `core.sweep.full` | full | 457.1 | 293 | 全规模扫描 |
| `core.math.full` | full | **5627.0** | 9 | 数学专项全量 |
| `core.huge.1e8` | full | 130.0 | 927 | 1e8 位直接 NTT |
| `contrib.block.1e8` | full | 142.5 | 927 | 1e8 位分块 NTT |
| `contrib.opencl.matmul.4k` | full | 337.2 | **15,664** | 4k×4k 60+60 子矩阵乘（GPU） |
| `contrib.ssa.1e6` | frozen | 33.7 | 116 | SSA 原型 1e6 肢 |
| `contrib.ooc.1e7` | frozen | 6.8 | 131 | OOC 1e7 位 |
| `contrib.ooc.1e8` | frozen | 311.9 | 652 | OOC 1e8 位 |

suite 耗时合计 **7551 s**，墙钟 **7553 s（2 h 05 min 53 s）**；**18/18 通过，
0 跳过，0 失败**。数学专项占总时间约 **74.5%**。

## 与 2026-10-20 的 8 项快照对比

| suite | 8 项快照 s | 本次 s | Δ |
|---|---:|---:|---:|
| `core.sweep.full` | 450.4 | 457.1 | +6.7 |
| `core.math.full` | 5780.1 | 5627.0 | **-153.1** |
| `core.huge.1e8` | 178.4 | 130.0 | **-48.5** |
| `contrib.block.1e8` | 217.7 | 142.5 | **-75.2** |
| `contrib.opencl.matmul.4k` | 337.4 | 337.2 | -0.2 |
| `contrib.ssa.1e6` | 33.2 | 33.7 | +0.4 |
| `contrib.ooc.1e7` | 6.5 | 6.8 | +0.3 |
| `contrib.ooc.1e8` | 303.9 | 311.9 | +8.0 |

两次都是单次运行，差异主要来自缓存、后台负载与温控；**不构成回归结论**。
OpenCL 4k 的耗时与峰值 RSS（15,664 MB）基本不变。

## 解读

- **数学专项** 5627 s，与历史 5780 / 5791.8 s 同量级；仍是全量耗时主体。
- **1e8 位直接 NTT** 130.0 s / 927 MB；**分块 NTT** 142.5 s / 927 MB。
  在直接 NTT 可用的规模内，分块仍不占优，符合此前结论。
- **OpenCL 4k×4k** 337.2 s / **15.7 GB**：时间是稳定的，内存是硬门槛；
  A/B 各约 7 GB 走 RAM，机器需 ≥24 GB 才能跑。
- **OOC 1e8** 311.9 s / 652 MB：低内存换时间，不要与内存内 NTT 的绝对耗时
  直接比较。
- **frozen（SSA/OOC）** 只是保留门槛数据的原型，不代表活跃功能。
- **5 h 是上限，不是目标**：当前 18 项 workload 在 2 h 06 min 内跑完；
  要真正跑满 5 h，需要新增更重的 suite（例如 1e9 位分块）或扩大现有
  suite 的内部规模。

## 边界

- 单次运行、单机；Windows/MinGW，未做多次取最小值或温控归一。
- `core.math.full` 清单预算 5400 s，实际 5627 s：`bench_math` 在操作之间
  检查预算，长操作会小幅超出（非截断）。
- `core.sweep.full` 的 `est_s=3600` 明显保守，实际 457 s；以实际输出为准。
- OpenCL 性能数仅本机 RTX 4050；CI 只有 PoCL CPU 正确性，**不可复现**。
- 本页是**快照**，不参与文档漂移检查；新数据请新增日期页，不要改历史数字。

## 相关

- 工具与 suite 定义：`docs/benchmarking.md`、`bench/suites.json`
- 同日 full + frozen 8 项快照：`docs/bench-full-2026-10-20.md`
- 归档 zip：`bench/artifacts/2026-10-20-full-5h.zip`
- 规划与结果讨论：issue #128
