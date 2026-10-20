# 性能测试指南

统一入口是 `scripts/bench.py` + 清单 `bench/suites.json`。本文只讲**怎么跑**；
具体数字在生成物里（`docs/performance.md`、`docs/bench-math.md`、
`bench/baseline/`），不在本文复述，避免过期。

## 1. 快速开始

```sh
python3 scripts/bench.py --list                    # 有哪些 suite
python3 scripts/bench.py --profile ci              # CI 档，约 2–5 分钟
python3 scripts/bench.py --profile quick           # 本地快速档，默认 20 分钟
python3 scripts/bench.py --modules bigint_bin,ntt  # 只跑相关模块
python3 scripts/bench.py --profile quick \
    --json build/bench/quick.json --markdown-out build/bench/quick.md \
    --html-out build/bench-report.html
```

首次运行会按需构建（`cmake --preset <preset>` → `build/<preset>`）。

## 2. 概念

| 概念 | 含义 |
|---|---|
| **suite** | 一条可执行的基准（目标 + 参数 + 预算），清单里的最小单位 |
| **modules** | suite 关联的 nex 模块（名字取自 `cmake/nex_modules.txt`），多对多 |
| **contrib** | suite 关联的 `contrib/` 实验（`nex_opencl` / `nex_parallel` / …） |
| **tier** | `ci` ⊂ `quick` ⊂ `full`；`frozen` 默认不跑（SSA/OOC） |
| **preset** | 构建配置，对应 `CMakePresets.json` 与 `build/<preset>` |

模块只是**标签**：`ntt` 相关的 suite 也会出现在 `bigint_bin` 的选择结果里
（乘法走 NTT）。未知模块名会直接报错并列出可选项。

## 3. 选择与组合

```sh
--modules A,B        # 任一匹配即选中（并集）
--contrib X,Y        # 只选指定 contrib 的 suite
--all                # 忽略模块过滤，选中档位内全部
--exclude 'core.*'   # 按 suite 名 glob 排除（可重复）
--include-frozen     # 加入 SSA/OOC（冻结项）
--suite NAME         # 只跑指定 suite（可重复；绕过 profile 档位，frozen 仍需 --include-frozen）
--suite-args NAME=ARGS  # 给某个 suite 追加参数（可重复）
--dry-run            # 只显示将运行什么、参数与预估耗时
```

`nex` 与 `contrib` 可以任意混合；`--all --include-frozen` 即“全量”。

**给单个 suite 追加参数**（保留清单里的默认参数，后出现的同名 flag 生效，
`${json}` 占位符仍有效）：

```sh
# 只跑数学 full，但只测 trans 段、预算 30 分钟
python3 scripts/bench.py --suite core.math.full     --suite-args 'core.math.full=--sections trans --budget 1800'

# 只跑 OpenCL 4k，并放宽内存上限
python3 scripts/bench.py --suite contrib.opencl.matmul.4k     --max-rss-mb 24576
```

`--dry-run` 会打印每个 suite 的最终 `args`，可先确认再跑。

## 4. 预算、时间与内存

| 档位 | 全局预算 | 典型用途 |
|---|---:|---|
| `ci` | 300 s | CI/PR 前的快速回归 |
| `quick` | 1200 s（20 min） | 本地快速档（默认） |
| `full` | 7200 s | 发版前/夜间完整档 |

- **时间**：全局 deadline + 单 suite `timeout_s`。剩余预算不足时，后续 suite
  标记 `skipped(budget)`（不算失败）。`--max-time` 覆盖档位预算。
- **预算是上限，不是目标**：各 suite 内部还有 `--budget`（如
  `core.sweep.deep` 的 900 s），实际通常明显更短；以 `--dry-run` 的预估和
  实际输出为准，不要假设一定会跑满档位时长。
- **内存**：每 100 ms 采样峰值 RSS（Linux `VmHWM`、Windows
  `PeakWorkingSetSize`、macOS `ps`），超过 `rss_mb`（或 `--max-rss-mb`，取小）
  终止进程并记 `rss` 失败。POSIX 另设 `RLIMIT_AS = 2×` 预算作为硬安全网。
- **optional**：需要设备/大内存的 suite（OpenCL、4k×4k）标 `optional`，
  失败只记 `skip(optional)`，不改变退出码。

退出码：`0` 正常（含预算跳过/optional 失败）；`1` 有非 optional 失败
（`fail` / `timeout` / `rss`）。

## 5. 构建与目录

suite 的 `preset` 决定构建配置：

| preset | 用途 |
|---|---|
| `bench` | 核心基准（`bench` / `bench_sweep` / `bench_math` / `bench_huge`） |
| `bignum` | `contrib/nex_bigint_block`、`nex_ssa`、`nex_ooc` |
| `thread` | `contrib/nex_parallel`、`nex_thread` |
| `opencl` / `opencl-cpu` | `contrib/nex_opencl`（GPU / 真实 CPU 基线） |

- 默认按需构建；`--no-build` 使用现成产物。
- 目录统一为 `build/<preset>`（见 `CMakePresets.json`，需要 CMake ≥3.19 +
  Ninja）。清理：`python3 scripts/clean.py --builds --presets`。
- 二进制缺失时 runner 会先构建；`--no-build` 下直接报错。

## 6. 输出

- 控制台：逐 suite 状态 + 汇总表（状态 / 耗时 / 峰值 RSS）。
- `--json FILE`：`{"profile","budget_s","results":[...]}`，每项含
  `suite/status/seconds/peak_rss_mb/rc/log/json/reason`。
- `--markdown-out FILE`：结果表，可直接贴 PR/issue。
- `--html-out FILE`：自包含 HTML 报告（摘要卡片、环境、suite 表），
  适合本地归档或作为 CI artifact；默认同时生成 `<报告名>-logs/`
  （每 suite 日志 HTML + `index.html`），主报告“日志”列直接链接。
- `--html-logs DIR`：自定义日志 HTML 目录。
- `--render-json FILE`：不重新运行，仅用已有结果 JSON 重新渲染报告
  与日志（适合补生成 HTML / 改样式）。
- 单 suite 的 stdout/stderr：`build/bench/<suite>.log`。
- `--json` 标记的 suite（`bench_math`）额外生成
  `build/bench/<suite>.json`（供趋势对比）。

## 7. 基线与趋势

- 数学专项：`scripts/bench-math-compare.py` 对比
  `bench/baseline/bench-math-ci.json`（advisory ±20%，`--fail-over` 预留门禁）。
- 更新基线：从 CI artifact 下载 `core.math.ci.json`，按
  `bench/baseline/README.md` 提交。
- 基准必须与对比基线**同优化级别**（Release），否则结论会反转。

## 8. CI 行为

`benchmark.yml`（每日 04:00 UTC 或手动）运行 `--profile ci`，上传：

- `bench-ci.json` / `bench-ci.md`（runner 汇总）
- `build/bench/*.log`、`build/bench/core.math.ci.json`
- `bench-math-compare.md`

PR 不跑性能门禁（runner 噪声大）；`docs` job 只校验
`docs/benchmark-suites.md` 与清单一致（漂移即失败）。

## 9. 新增一个 suite

1. 在 `bench/suites.json` 增加条目（`name` 唯一；`modules`/`contrib` 必须是
   已知名字；给出 `preset`/`target`/`args`）。
2. 设定 `tier`、`est_s`、`timeout_s`、`rss_mb`；设备相关项加 `optional`。
3. 需要机器可读结果时加 `"json": true`，参数里用 `${json}` 占位。
4. 验收：单次耗时 < `timeout_s`；输出确定性（固定种子）；报告峰值 RSS；
   与现有基线同优化级别。
5. `python3 scripts/bench.py --list --markdown > docs/benchmark-suites.md`
   （CI 会校验）。

## 10. 排障

| 现象 | 处理 |
|---|---|
| `cmake --preset` 失败 | 需要 CMake ≥3.19 + Ninja；或改用 `cmake -B build/xxx` 并 `--build-root` |
| OpenCL suite `skip(optional)` | 无设备/驱动；`NEX_OPENCL_DEVICE` 指定设备，或只跑 `--modules bigint_bin` |
| `rss` 失败 | 调高该 suite `rss_mb` 或 `--max-rss-mb`；看 `build/bench/<suite>.log` |
| `timeout` | 提高 `--max-time` 或该 suite `timeout_s`；CI 档不应放长基准 |
| 数字波动大 | 关闭后台任务；固定 CPU 频率；用 `full` 档并对比 baseline |
| 磁盘被构建目录占满 | `python3 scripts/clean.py --builds --installs --yes` |

## 11. 结果归档

| 内容 | 位置 |
|---|---|
| 完整单机实测（快照） | `docs/bench-full-2026-10-20.md` |
| 数学专项解读 | `docs/bench-math.md` |
| 核心性能报告 | `docs/performance.md`（`scripts/bench.sh` 生成） |
| 夜间趋势基线 | `bench/baseline/*.json` |
| suite 清单（自动生成） | `docs/benchmark-suites.md` |
