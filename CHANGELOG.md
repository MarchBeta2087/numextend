# 更新日志

本文件记录 Numextend 的版本变更，格式参考
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

v1.0.0 之前的开发以设计文档的「修订记录」逐次跟踪（见
[`docs/design.md`](docs/design.md#修订记录)）。

## [Unreleased]

## [1.8.0] - 2026-10-20

### 基准

- **P0 大整数可行性工具（issue #98）**：新增
  `scripts/bigint-feasibility.py`（十进制位 → bit/肢/变换长度/内存/相对
  工作量）与 `bench/bench_huge.c`（强制多模数 CRT NTT，报告耗时与 Peak
  RSS）。实测 10⁶/10⁷/10⁸ 位：0.29 s / 7.5 s / 195.8 s，峰值 RSS
  12.6 / 75.5 / 930 MB；CRT 代理约 0.15%。当前 NTT 上限约 3.2×10⁸
  位（`sum(limbs) ≤ 2^26`、`log2n ≤ 27`），10⁹ 位需更大模数或 SSA。
- **P1 分块 NTT（issue #98）**：`nex/ntt` 内置模数扩展到 c=28/30，
  直接 NTT 上限提升到 `sum(limbs) ≤ 2^27`、`log2n ≤ 28`；新增
  `contrib/nex_bigint_block`（`-DNEX_ENABLE_BIGNUM=ON`）用三个半积
  分块突破直接上限。10⁸ 位分块 359.8 s vs 直接 195.8 s（直接可用时
  分块不占优）；10⁹ 位（半积 s≈1.04e8）实测 **2403 s（40.1 min）、
  Peak RSS 6.24 GB**，结果肢数 207,620,506。P1b 将分块改为**递归**
  （单块 max_len≤2^26），1e9/2e9 位用“大数 × 小乘数”与 schoolbook
  对拍通过（11.6 s / 21.8 s）；10¹⁰ 位在 34 GB 机器合并阶段 OOM
  （模型 64 GB 分块 / 114 GB SSA），需 ≥256 GB 或 out-of-core。

### 实验（`contrib/`，默认 OFF，不进核心）

- **P3 OpenCL M1（issue #98）**：新增 `contrib/nex_opencl`
  （`-DNEX_ENABLE_OPENCL=ON`，默认 OFF；动态加载 OpenCL.dll /
  libOpenCL.so.1，不需要 SDK）。`nex_opencl_madd` / `nex_opencl_dot`
  针对 104 limb（≈1000 位十进制）特化；10k 元素在 RTX 4050 上端到端
  （含传输）**madd ≈6.9 ms / dot ≈4.6 ms**，与 CPU 参考及 `bigint_bin`
  逐位一致；对比 16 线程 `parallel_vector` 的 14.85 ms 约 **2.1×**。
  M2 增加子矩阵乘 `nex_opencl_matmul_subset`：4k×4k、1000 位、取
  60 行 + 60 列并集（487,920 输出、2.0×10⁹ 次大数乘法）在 RTX 4050 上
  **327.1 s（5.45 min）/ 6.11×10⁶ big-MADD/s**，抽样与 `bigint_bin`
  逐位一致；A/B 各 6.98 GB 走 RAM，tile 显存 ≤2.5 GB。CPU 16 线程
  **实测** 3385.1 s（56.4 min，`--cpu --threads 16`，同一 schoolbook
  参考实现；GPU **10.3×**），此前的 ~22.5 min 是完美线性外推、偏低 2.5×。
  M3 增加 CSR/CSC 稀疏路径与 `--density d`：只遍历非零索引交集，
  d=0.5/0.1/0.01/0.005 时 88.7/4.77/0.52/0.36 s；按实测基准折算
  交叉点 d≈0.01–0.02（更低密度 CPU 更快，d<1 的 CPU 列为投影），
  抽样逐位一致。
  修复：稀疏路径把 packed 缓冲以 `CL_FALSE`（非阻塞）入队后立即 `free`，
  实现可延迟读取主机内存（PoCL CPU 即如此），属 UB；改为阻塞写后
  `linux-opencl` 恢复通过（NVIDIA 立即拷贝，未暴露该问题）。
- **P1b-SSA M1 原型（issue #102，PR #103）**：新增 `contrib/nex_ssa`
  （Schönhage–Strassen，`2^L+1` / `2^(L/2)+1` 双环 + CRT）。双环而非
  `2^L−1`：后者 DFT 存在零因子（`1−2^(L/2)` 与模不互素、变换不可逆）。
  实测 1e6–1e8 位：1e8 位 **1142–1318 s / Peak RSS 1.66 GB**（较直接
  NTT 的 195.8 s 慢约 5.8×）；1e6 / 3e6 肢与 `bigint_bin_mul`（NTT）
  逐位对拍通过。结论：10¹⁰ 位双环峰值仍约 **100 GB**（标准模型
  111.5 GB），34 GB 不够，下一步转 out-of-core（issue #104）。
- **P1b-OOC M1 骨架（issue #104）**：新增 `contrib/nex_ooc`（随
  `-DNEX_ENABLE_BIGNUM=ON` 构建）。操作数与结果是「纯肢体文件」，块长 L
  可配（默认 2^23 肢）；输出块 k 的累加器只保留 2L+2 肢，低 L 肢写盘、
  高位作为进位入块 k+1，因此**峰值内存 O(L)、与规模无关**；块对乘法
  直接复用核心 AUTO。实测：1e7×1e7（8×8 块）19.9 s / 16.3 MB；
  1e8×1e8（2×2 块）**227.0 s / 651.5 MB**，与内存内 NTT 逐位一致；
  **1e9 位**（13 块）×3 **0.8 s / 163.9 MB**，与 schoolbook 逐位一致。
  新增 `--verify-mod`（5 素数流式校验：(a mod p)·(b mod p) ≡ (a·b) mod p，
  内存 O(chunk)，任意规模可用；1e9 位无法用 `--verify-direct`，因为需要
  s = 2^29 超出直接 NTT 上限）。
  实测补：**1e9×1e9（L=2^25，4×4 块）= 6648.9 / 6992.8 s（两次，同输入，
  波动 5%）/ 峰值 RSS 2692 MB / 读 3.43 GB / 写 0.94 GB**，结果
  207,620,506 肢（与内存三分块算法一致）；第二次带上 `--verify-mod`：
  **5 素数流式校验通过**。另直测 L=2^26 单块对：**1092.8 s / 峰值 4611 MB**。
  结论：框架可行。总时间 ≈ `pairs × P(L)`，随块数 nb 近似线性
  （实测 n=1e7 位，nb=1/2/4/8/16/32 → 4.6/6.6/11.7/19.6/26.3/50.0 s）；
  1e10 位对应 nb = 124（`L=2^23`）/ 31（`L=2^25`）/ 16（`L=2^26`）。
  `P(L)` 在大 L 段超线性（P(8.39M)=56.8 s vs P(33.55M)=415.6 s ⇒
  `P ∝ L^1.44`）。**此前"百小时/不可行"的说法基于错误的块数（1240 应为
  124），已更正**：1e10 —— `L=2^23` 242 h、`L=2^25` 111 h、`L=2^26`
  （16 块 / 256 块对，**时间与峰值内存都用同块长实测值算**）
  **77.7 h（3.2 天）/ ~4.6 GB**。固定块长时 `time ∝ n²`（块对 ∝ 块数²），
  故 1e10 位的代价是「3 天级」。**决策：M2 不做**——普通 PC 跑 3 天无实用
  意义，且 1e10 位无需求方；测量保留为门槛数据，重启条件见 issue #104。
  当前实用上限为 1e9 位级（~1–2 h、≤4.6 GB）。

### 评估与决策

- **超大位宽乘法收口（2026-10-20，#98 / #102 / #104）**：SSA M2/M3
  （递归环变换、10¹⁰ 位）与 OOC M2/M3（1e10 位、块级提速）**一并冻结**。
  依据：1e9 位已由分块 NTT（2403 s / 6.24 GB）与 OOC（111 min / 2.7 GB）
  覆盖；SSA M2 原目标（RSS <16 GB、不劣于 2403 s）相对现状没有增量，
  且 1e10 位无需求方。`contrib/nex_ssa` 与 `contrib/nex_ooc` 保留为原型与
  门槛数据，重启条件见 `TODO.md` §K2 / 各自 README。
- **#98 P2–P4 立项门槛**：`TODO.md` 新增 §K4，要求 Lean 4 形式化、
  OpenCL、进核心 AUTO 在启动前先定义验证边界、toolchain/设备范围、
  GPU CI 与验收指标（≥2× 且逐位一致）；当前无候选满足 P4。
- **P2 形式化路线变更（2026-10-20，#98）**：**取消 Lean 4** 参考模型与
  完整形式化。依据：核心 33k 行、7 个数值域，难点在 NTT/CRT/浮点误差界，
  Mathlib 无现成库；且没有 C→Lean 精化链路时定理无法为 C 背书。改为
  **先 C（CBMC 有界验证 limb 层）后 D（模糊测试 + 不变量断言）**；
  C、D 均已落地（`verify/cbmc`、`tests/fuzz` + `NEX_DEBUG_INVARIANTS`）；
  重启 Lean 的条件（合规/审计消费方 + 精化链路）写入 §K4。

### 测试与 CI

- **contrib 参考手册**：`gen_api_doc.py --contrib` 从
  `contrib/modules.txt` 的公开头生成 `docs/contrib-manual/`（7 页 + 总览），
  `docs.sh` 用同一份 CSS/pandoc 合成 `docs/contrib-manual.html`；纳入 docs CI
  漂移检查。补齐 `nex_opencl` / `nex_parallel` / `nex_ssa` / `nex_bigint_block`
  的函数注释（生成器零告警）。
- **bench 参数覆盖**：`scripts/bench.py` 新增 `--suite NAME`（只跑指定
  suite，绕过 profile 档位）与 `--suite-args NAME=ARGS`（向该 suite 追加参数，
  保留默认值；后出现的同名 flag 生效，`${json}` 仍有效）。`--dry-run`
  现在打印每个 suite 的最终参数。文档见 `docs/benchmarking.md` §3。
- **性能报告日志 HTML**：`--html-out` 现在默认生成
  `<报告名>-logs/`（每 suite 日志 HTML + `index.html`），主报告
  “日志”列直接可点；新增 `--html-logs DIR` 与 `--render-json FILE`
  （不重跑，仅从结果 JSON 重新渲染）。结果 JSON 增加 `elapsed_s`。
- **HTML 性能报告**：`scripts/bench.py` 新增 `--html-out`
  （自包含 HTML：摘要卡片、环境、suite 表与日志路径），与
  `--json`/`--markdown-out` 共用同一份结果。
- **HTML 测试报告**：新增 `scripts/test-report.py`：运行 CTest
  （`--output-junit`）并生成自包含 HTML（摘要卡片、环境/构建信息、失败
  详情、全部用例表）；退出码与 ctest 一致。用法见 README/maintenance §6。
- **文档漂移加固**：`docs` CI 现在固定 pandoc **3.8.2.1** 生成并校验
  `docs/manual.html`，同时运行 `scripts/check-docs.py`（校验 Markdown 中
  引用的仓库路径与 CMake preset 名，跳过生成物/占位符）。
- **contrib 模块索引**：新增 `contrib/modules.txt`（8 字段：deps/sources/
  headers/flag/libs/preset/status）与 `modules.py check-contrib` /
  `gen-contrib-docs`。校验每个 contrib 目录与公开头恰好登记一次、依赖与
  preset 有效；生成 `docs/contrib-modules.md` 并纳入 `docs` CI 漂移检查。
  核心 `cmake/nex_modules.txt` 保持不含 contrib（契约不变）。
- **统一性能测试入口**：新增 `scripts/bench.py` + `bench/suites.json`，可按
  nex 模块与 `contrib` 任意组合，统一时间/内存预算（ci=300 s / quick=1200 s /
  full=7200 s；100 ms 峰值 RSS 采样 + POSIX `RLIMIT_AS` 安全网），输出
  JSON/Markdown 与逐 suite 日志；`benchmark.yml` 改用它。文档见
  `docs/benchmarking.md`，suite 清单 `docs/benchmark-suites.md` 自动生成并
  由 `docs` CI 校验漂移。
- **构建目录治理**：新增 `CMakePresets.json`（13 个配置，产物统一到
  `build/<preset>`；需 CMake ≥3.19 + Ninja，普通 `-B` 仍兼容 3.16）与
  `scripts/clean.py`（默认 dry-run；`--builds/--installs/--coverage/
  --artifacts/--pycache/--stale`、`--keep`、`--json`；WSL `/mnt/c` 源路径归一，
  避免误判 stale）。本机 46 个遗留 `build*` + 安装树约 395 MB 可一次性回收。
- **contrib 大数实验 CI**：新增 `linux-bignum`，用 `-DNEX_ENABLE_BIGNUM=ON`
  `-DNEX_ENABLE_SSA=ON` 构建并运行 `block_ntt_selfcheck` / `nex_ssa_test` /
  `nex_ooc_test`（约 30 s），避免实验代码与核心漂移。
- **OpenCL 实验 CI**：新增 `linux-opencl`，安装 `ocl-icd-opencl-dev` +
  `pocl-opencl-icd`，在 PoCL CPU device 上跑 `nex_opencl_test`
  （`NEX_OPENCL_REQUIRE=1`）；GPU 性能数由本机 RTX 4050 记录。
- **limb 层有界验证（P2-C，#98）**：新增 `verify/cbmc` 与 CI job
  `linux-cbmc`（apt 安装 CBMC）。40 个目标覆盖 `nex/bigint/bin` 的
  加/减/比较（功能对拍、等长/不等长、零操作数、`dst==lhs` 别名）与
  乘法（无 UB/规范化/符号、常数乘子 0–3）；肢数固定 0..3 使循环边界为
  常量，`--unwinding-assertions` 保证展开充分。本机约 30 s。变异测试
  （尾随进位 `>>32`→`>>33`）确认 `1x3` 目标能抓到仅不等长才执行的路径。
- **属性/差分模糊测试（P2-D，#98）**：新增 `tests/fuzz/fuzz_nex.c`（单入口
  6 模式：NTT 往返/卷积、CRT 回代、dec↔bin 往返、bigfrac 既约与代数恒等式、
  bigfloat 规范化/指数范围/幂等、ball 包含性）。CI job `linux-fuzz`
  （clang + libFuzzer + ASan/UBSan + `NEX_DEBUG_INVARIANTS`，120 s 预算）。
  本机约 3.9k exec/s；注入 NTT 归一化错误后 200 次内可复现失败。
- **内部不变量断言（P2-D）**：新增 `nex/nex_debug_internal.h` 与 CMake 选项
  `NEX_DEBUG_INVARIANTS`（默认 OFF）；在 `normalize` / `mag_add` / `mag_sub` /
  `mul_small` / `frac_reduce` 断言进位/借位上界与规范化。`linux-sanitizer`
  与 `linux-fuzz` 开启。
- **线程池 TSan（TODO §F F3）**：新增 CI job `linux-thread-tsan`
  （`-DNEX_ENABLE_THREAD=ON -DNEX_SANITIZE=thread`），只跑 `parallel_ntt`
  相关测试；与 ASan job 分离，MSVC 不覆盖。测试命令用
  `setarch $(uname -m) -R` 关闭 ASLR（高熵 ASLR 会触发 TSan 的
  "unexpected memory mapping"）；本地 WSL（GCC 13.3）已验证通过。

## [1.7.0] - 2026-10-19

### 新增

- **`bigint_bin64` 原生除法、字符串 I/O 与互转（issue #82，v1.7）**：
  - `bigint_bin64_div_rem` 改为原生 64 位肢 Knuth Algorithm D，不再对所有
    输入经 32 位肢 bin 转换。128/64 试商在 GCC/Clang 用 `__int128`，
    其他编译器（含 MSVC）走以 2^32 为基的专用 4 字 ÷ 2 字 Knuth D
    （`nex_u128_divmod`，可用 `NEX_BIN64_FORCE_PORTABLE` 强制）。
  - 超大输入（除数 ≥ 1024 肢且被除数 ≥ 2×除数）自动回退 32 位肢 bin 的
    Burnikel-Ziegler，避免 O(n²) 在大位宽下反超。
  - 实测（-O2，MinGW x86-64，被除数 2n / 除数 n 肢）：n = 16/64/256/512
    时较原转换包装快 **2.15× / 2.28× / 2.06× / 1.65×**；n ≥ 1024 回退 BZ。
  - 新增 `bigint_bin64_from_str` / `bigint_bin64_to_str`（进制 2..36，
    可选 `-` 符号，16/2 进制前缀；2 的幂进制按位切片 O(n)，其余按
    uint64 分块；语义与 `bigint_bin` 一致）。
  - 新增 `bigint_conv_bin64_to_bin` / `bigint_conv_bin_to_bin64` /
    `bigint_conv_bin64_to_dec` / `bigint_conv_dec_to_bin64`
    （bin64 ↔ dec 经 bin 中转，复用分治基数转换）。
  - 测试：除法与 bin 交叉验证、对抗模式（全 1 除数、2 的幂、精确倍数、
    超过 BZ 阈值的大输入）、字符串全进制往返与错误语义、互转往返；
    ASan 与 `NEX_BIN64_FORCE_PORTABLE` 两套构建均通过。

### 基准

- **夜间趋势基线与对比策略（J10 剩余项）**：新增
  `scripts/bench-math-compare.py` 与 `bench/baseline/`。nightly
  `bench_math --preset ci` 与同 runner 基线比较，输出 Markdown 报告到
  Step Summary / artifact；默认 **advisory**（±20%，<10 µs 低噪声），
  连续 3 次同方向异常再排查；`--fail-over` 预留后续门禁。基线更新
  流程见 `bench/baseline/README.md`，维护手册 §3.1 同步。首个基线已从
  2026-10-19 的 dev `workflow_dispatch`（run `39184434034`，71.9 s，
  172 条）播种；benchmark workflow 固定 `ubuntu-24.04` 以防
  `ubuntu-latest` 迁移导致基线漂移。

### 性能

- **`bench_math` full 吞吐量新增 32768 bit 档（issue #82）**：评估加入
  该档后的预算占用——`throughput` 段 31 → 109 s（约 +1.3 min），full
  总耗时约 96.5 → 98 min，仍低于 7200 s 预算；32768 bit 下 `log` 约
  55.5 s/次，仍是最慢项。`docs/bench-math.md` 注明该报告为加入前快照。
- **`bigfloat_to_str` 最短往返输出加速（issue #82，Ryū 式目标）**：
  往返性质对有效位数 n 单调（n 位成功 ⇒ n+1 位必成功），改为先试
  上界再**单调二分**最小 n，每次候选仍经 `nex_bf_dec_round` 生成、
  按同一 ctx 解析回并 `bigfloat_cmp` 精确比较（保持原正确性契约）。
  代价由 O(n) 次“除法 + 解析”降为 O(log n) 次：8192 bit
  **1180 → 11.8 ms（100×）**，4096 bit 197 → 3.7 ms，1024 bit
  9.1 → 0.46 ms。黄金/单元测试全部通过；未引入 64/128 位定点表
  （其前提是固定精度）。
- **放宽 Burnikel-Ziegler 分派（issue #82）**：原条件为“被除数 ≥ 2×除数”
  （商肢数 m ≥ 除数肢数 n），导致 m ∈ [n/32, n) 的商仍走 O(m·n) 的
  Knuth D，而 BZ 的 O(M(n)) 已可摊薄。新条件（除数 ≥ 64 肢）：
  `m ≥ 96 且 m ≥ n/32`。Release 交错实测（4096 肢除数）：
  m=n/4 时 5.6→1.9 ms，m=n/2 时 11.3→3.0 ms；m ≥ n 原已走 BZ。
  `m < 96` 仍走 Knuth D，避免小商时 BZ 的常数开销。阈值可由
  `NEX_DIV_BZ_MIN_QUOT` / `NEX_DIV_BZ_QUOT_DIV` 覆盖用于对照。
  测试新增 m≈n/4 的 BZ 分派区间用例。

### 评估与决策

- **contrib 线程池与并行 NTT（TODO §F 消费者实验，默认不构建）**：
  新增 `-DNEX_ENABLE_THREAD=ON` 实验目标：最小线程池（pthread / Win32
  `CONDITION_VARIABLE`）+ 并行双模数 CRT NTT 乘法 + 批量
  `a[i]*b[i]+c[i]` 示例。结果与串行逐位一致。16 硬件线程实测：
  - 单次 NTT 乘法（16k/32k/64k 肢）：**2.5× / 2.45× / 2.8×**；
  - 批量运算（256 bit×100k / 1024 bit×30k / 4096 bit×8k）：
    **6.55× / 7.02× / 7.73×**。
  结论：保持 `contrib/` 隔离（不进核心/amalgam/子集），**不开核心规范
  特例**；单次乘法收益有限，批量场景才是线程池主场。TSan/CI 尚未接入。
  后续优化：扁平任务图（每 stage 一次 barrier，4 个缓冲区一起调度）+
  并行 Garner 系数重构 + 分块进位（4 系数/块，128-bit 块归约），单次
  16k/32k/64k/131k 肢提升到 **2.50× / 3.11× / 3.58× / 4.92×**
  （此前 2.53/2.37/2.80×；4M bit 达 4.9×）。详见 `contrib/README.md`。

- **Newton 迭代除法（issue #82）**：将 `NEX_DIV_NEWTON_PATH` 的旧实现
  （每轮在全精度上迭代）重写为**精度倍增倒数**（p → 2p，总代价
  O(M(P))）+ 商修正，并修正便携 128/64 试商。实测（-O2）：
  - 商 1 肢（4096 肢除数）：Newton 0.039 ms，Knuth D 0.012 ms
    ——Knuth 为 O(m·n)，商小时已是线性；
  - 商 ≈ 2n：Newton 31.6 ms，BZ 5.7 ms；
  - `bigfloat_div` 262144 bit：Newton 19.3 ms，默认路径 16.7 ms。
  各规模均不占优，**默认关闭**（保留 `NEX_DIV_NEWTON_PATH` 供后续
  实验；启用后全部 42 项测试与除法不变量 fuzz 通过）。
- **半 GCD（issue #82）**：新增矩阵递归原型（`NEX_GCD_HGCD_PATH`）：
  高位半段递归 + 矩阵应用 + 一次精确除法 + 二次递归，基例阈值
  512 bit，高位矩阵无效时回退精确欧几里得。单元测试与三组黄金对拍
  （含 3000 位十进制随机 gcd）均通过，但 bigint 矩阵的分配与乘法
  常数过大：64 肢 0.97 ms vs Lehmer 0.066 ms（约 15×慢），8192 肢
  2.15 s vs 0.29 s（约 7×慢）。**默认关闭**，保留供低层（无分配）
  矩阵实现时复用；Lehmer 仍是默认路径。
- **复数 Karatsuba 三乘式（issue #82）**：float/decimal 复数乘法新增
  三乘路径（`k1=(a+b)(c+d)`、`k2=ac`、`k3=bd`），仅用于有限非零
  分量；零/±∞ 仍走四乘以保持特殊值语义。正确性由黄金对拍（三乘参考）
  与单元测试验证。性能中性：4096 bit 10.86→10.50 µs（~3%），
  16384/32768 bit 反而慢 1–2%——多出的 3 次加法/分配抵消了省下的一次
  乘法。**默认关闭**（`NEX_COMPLEX_KARATSUBA`）；启用后舍入路径与四乘
  不同（可差 ~1 ulp），黄金参考需同步。

## [1.6.0] - 2026-10-18

### 新增

- **Payne–Hanek 实参规约（TODO §J，design §16.2）**：`sin`/`cos`/`tan` 在
  `|x| > 2⁶³` 时不再返回 `INVALID`，改用固定点 `2/π` 规约到
  `r ∈ [−π/4, π/4]` 并按象限还原。
  - 新增内部单元 `nex/ball/nex_ball_pi.c`：Chudnovsky 公式 + 整数二分拆分
    计算 `floor(π·2^p)`（每项约 47 bit，96 bit 保护位），定点位数上限
    `NEX_PH_MAX_PI_BITS = 2²⁰`。
  - float 与 decimal 两套接入：decimal 用 `R·2^{-K} = (R·5^K)·10^{-K}`
    精确换算，避免有损中间转换；半径由 `2/π` 截断、`π/2` 截断与商截断的
    误差上界合成。
  - 输入球半径 ≥ 4（区间长度 > 2π）时 `sin`/`cos` 直接返回 `[−1,1]`。
- **`log` 指数拆分（TODO §J7）**：`x = m·2^E`（decimal 为 `m·10^E`），
  `ln x = ln m + E·ln 2`（或 `E·ln 10`），小实参仍用迭代平方根 + `ln1p`
  级数。解除原 40 次开方上限，覆盖全部可表示输入；同时避免巨大指数下
  `ball_sqrt` 端点精确对齐的开销/内存爆炸。
- **路线 A 高精度加速（TODO §J8）**：`nex_series_*_eval_*` 的部分和与阶估计
  改为扩展精度浮点递推（`p + 48 + log2 n` 工作位，尾界向上舍入；decimal
  经 `|z|` 向上转 bigfloat 复用 float 阶估计），不再构造精确有理公共分母。
  实测（-O2）：`exp` 4096 bit 10.2 s → 63 ms，`ln1p` 1.64 s → 180 ms，
  `log(3)` 512 bit 11.0 s → 4.6 ms。精确 `*_order` 公共 API 保留。
- **复数 Smith 稳定除法（TODO §J9）**：`bigcomplex_float_div` /
  `bigcomplex_decimal_div` 对有限输入采用 Smith 缩放，避免 `c²+d²`
  溢出/下溢；含 ±∞ 分量时回退共轭法。新增极大/极小分量单元测试，黄金参考
  同步为 Smith 语义。
- **数学专项基准（TODO §J10）**：新增 `bench/bench_math.c` 与
  `scripts/bench-math.sh`，分三段可选：
  - `trans`：所有公开超越函数（float/decimal/复数，含 `atan2`/`arg`）× 多种精度；
  - `const`：π / e / ζ(3) / γ 的多种表达式 × 多种精度（含 Apéry、Euler 变换、
    Brent–McMillan、Chudnovsky），首次运行自检；
  - `throughput`：各规模整数/浮点/十进制运算的 ops/s。
  预设 full/quick/ci（默认预算 7200/600/300 s），支持 `--sections` 与 `--json`；
  CI 每日运行缩减档并上传 artifact。`bench_sweep` 同时新增“超越函数 / 级数 /
  复数除法”精度扫描。完整全量结果见 `docs/bench-math.md`。
- 测试：`test_transcend` 增加 2¹⁰⁰（float）与 1e30/1e100（decimal）用例；
  `test_ball` 覆盖 PH 路径、宽区间与十进制 1e30；`golden_series.py` 新增
  2¹⁰⁰/2²⁰⁰/1e30/1e100/1e1000 的 mpmath 对拍，并按实参量级自动提高参考精度。

### 修复

- **`bigfloat_from_bigint` 负输入**：`nex_bf_round_pack` 未对带符号尾数取幅值，
  负整数构造会破坏「尾数为正」不变式（影响 `log` 的指数项等）；已与 decimal
  版对齐，先取幅值再舍入。
- 球式 `exp`/`sin`/`cos` 头注释中的过时上限（`k > 20` → 64），与实现一致。

### 文档

- `docs/design.md`：§16.2 改写为已落地的 Payne–Hanek 设计（算法、误差界、
  float/decimal 路径、上限），新增 §16.3 后续；同步 §13 #8、§14、修订记录。
- `README.md`：测试数 33 → 42，模块说明补充 PH。
- `TODO.md`：新增 §J（v1.6 进展），D3 收口，优先级表与测试数同步；
  J7（log）、J8（路线 A）、J9（Smith）收口，J10 记录性能测试待定方案。
- `docs/design.md`：§9.2 Smith 除法、§13 #7、§15.6 路线 A 加速、
  §16 `log` 指数拆分与范围、§16.3 后续、修订记录同步。
- 公开头注释（`nex_series.h`、`bigcomplex_{float,decimal}.h`）同步。

---

## [1.5.0] - 2026-10-15

### 新增

- **球系数截断级数**（TODO §I）：`bigfloat_ball_series_ty` /
  `bigdecimal_ball_series_ty` + `init`/`free`/`from_array`/`copy`/`add`/`sub`/
  `mul`/`eval`/`compose`——系数为球，四则/复合/求值全程由球传播半径。
- `bench_sweep` 新增 `bigint_dec` 除法扫描（为 §I 的亚二次除法决策提供数据）。
- **超越函数 `bigfloat_atan`（TODO §D）**：正确舍入（球式实参规约 + 严谨误差界
  + Ziv 提升精度）；新模块 `nex/transcend/`；球式 `sqrt`/`atan`；测试 +
  mpmath 黄金对拍。**`bigdecimal_atan` 同步落地**（`nex/transcend/nex_transcend_dec.c`；
  另增 `bigdecimal_ball_sqrt`/`bigdecimal_ball_atan`；倍角用球加法避 `2^k` 非精确）；
  `golden_series_driver`/`.py` 新增 `atanpubdec`。
- **`bigint_dec` 亚二次除法**（TODO §I）：Burnikel-Ziegler 递归除法
  （`dbz_div_rem`），dec 基 10⁹ 以 Knuth-D 归一化因子 + 肢移位代替 bin 的整位
  规约；除数 ≥ 64 肢且被除数 ≥ 2×除数自动分派。回归
  `tests/test_bigint_dec::test_bz`（分派边界/随机/稀疏/全 9/精确整除）；10⁵ 位
  ≈ 45 ms（原 Knuth D ≈ 360 ms，约 8×）。
- **复数辐角 `arg` 重新引入**（TODO §D1）：`bigcomplex_float_arg` /
  `bigcomplex_decimal_arg` 经新公开的 `bigfloat_atan2` / `bigdecimal_atan2`
  正确舍入实现（直角分象限 `atan(y/x)`，`x<0` 用 `sign(y)·(π−atan(|y|/|x|))`，
  `t>1` 用 `π/2−atan(1/t)` 归约；π、π/2 由球式 `4·atan(1)`/`2·atan(1)` 严格
  得到）。单元测试含象限/轴/负实轴；随机 mpmath 压测 float 84 例 / decimal
  81 例全过。
- **指数 `exp`（TODO §D2）**：`bigfloat_exp` / `bigdecimal_exp` 正确舍入（球式
  折半 + 路线 A 级数 + k 次球平方 + Ziv）；`golden_series` 新增 `exppub` /
  `exppubdec`（mpmath 对拍）。
- **自然对数 `log`（TODO §D2）**：`bigfloat_log` / `bigdecimal_log` 正确舍入；
  球式 `ball_log` 用**迭代平方根规约**（`y = x^(1/2^k)` 使 `|y−1| ≤ 1/8`）+
  `ln1p` 级数 + 2^k 球加法回放，float/decimal 统一；`golden_series` 新增
  `logpub` / `logpubdec`。
- **三角 `sin` / `cos` / `tan`（TODO §D3）**：`bigfloat_*` / `bigdecimal_*` 正确
  舍入（球式折半 + half-angle 倍角 + Ziv）；球式 `ball_tan` = sin/cos；折半上限
  20→64（覆盖 |x| ≲ 2⁶³）；`golden_series` 新增 `sinpub`/`cospub`/`tanpub` 与
  十进制版。
- **反三角 `asin` / `acos`（TODO §D4）**：`bigfloat_asin/acos` 与
  `bigdecimal_asin/acos` 正确舍入（球式 `asin = atan(x/√(1−x²))`、`acos = π/2 −
  asin`）；特判 `acos(1)=0`（对称球跨 0 时 Ziv 不收敛）；`golden_series` 新增
  `asinpub`/`acospub` 与十进制版。
- **复数超越函数（TODO §D5）**：`bigcomplex_float_*` / `bigcomplex_decimal_*` 的
  `exp`/`log`/`sqrt`/`sin`/`cos`/`tan`（分量运算，各步按 `ctx` 舍入；
  `sinh`/`cosh` 由 `exp` 组合；`sqrt` 用稳定式避负实轴抵消；NaN 传播、含 ±∞
  返回 INVALID）；单元测试 + mpmath 压测。

### 文档

- **正确舍入语义规范收口（TODO §D6）**：design **§16.1** 汇总算术（保护位 + 粘位、
  一次舍入、无双重舍入）、超越（球式严谨界 + Ziv）、复合（各步正确舍入）与
  Ziv 终止边界/范围限制/无次正规；§7.3、§9.2 交叉引用。
- **设计文档过时项清理**：修正 `bigint_mul_algo_ty` 枚举注释（Toom-3 / 浮点 FFT /
  多模数 NTT 已实现，非“v1 未实现”；含公开头与生成手册）；§1.3 非目标、§13 #2/#8、
  §14 表（超越已实现、大端/32 位已验证）、§15.3/§15.6 实现状态同步。

### 修复

- **球式 `sincos_reduced` 的 `one_ball` 半径错误**：原 `ball_set(&one_ball, &one,
  &one, ...)` 使 `one_ball = [0,2]`（半径 1），导致 k≥2 折半时 sin/cos 球半径
  暴涨（x=1 时 rad≈1），正确舍入包装无法收敛。改为半径 0。

## [1.4.0] - 2026-10-14

### 修复

- **16-bit FFT 强制路径安全上限**：`mul_fft` 在强制 `chunk_bits = 16` 且长度和
  `S > NEX_MUL_FFT_B16_MAX_SUM`（1024）时返回 `BIGINT_ERR_UNSUPPORTED_E`
  （此前仅自动路径校验，强制路径可越界）。测试 `test_fft_mul::test_fft16_cap`。

### 新增

- **平台 CI**：`linux-32bit`（`-m32 -msse2 -mfpmath=sse`）与 `linux-bigendian`
  （s390x 交叉 + QEMU）；交叉构建跳过 Python 黄金测试。
- **测试**：`test_bigint_bin::test_sparse`（乘法与 gcd 闭式恒等式）。

### 文档

- `docs/design.md` §3.4「内部视图与比较规则」；§1.3/§14 平台 CI；§4.3 16-bit 上限。
- `TODO.md` §G 收口（v1.4.0 关闭项与延后项）。

## [1.3.0] - 2026-10-14

### 新增

- **多项式求值（秦九韶 / Horner，TODO §C1）**：`bigint_bin_poly_eval` /
  `bigfrac_poly_eval` / `bigfloat_poly_eval` / `bigdecimal_poly_eval`，计算
  `Σ coeffs[i]·x^i`；精确或逐次正确舍入，允许别名、强异常安全。
- `docs/design.md` §15「多项式与级数」：Horner 舍入协定 + 截断幂级数/Laurent
  （负幂）设计（TODO §C5）。
- 测试 `tests/test_poly.c`（`poly_horner`）。
- **截断幂级数 / Laurent（`bigfrac` 精确，TODO §C2）**：`bigfrac_series_ty`
  （`val` / `coeffs` / `len` / `cap`）+ `from_array` / `copy` / `add` / `sub` /
  `mul`（截断 `order`）/ `inv`（支持 Laurent）/ `eval`（含负幂）。精确、允许
  别名、OOM 时输出不变；测试 `tests/test_series.c`。
- **广义二项式系数与二项式级数（TODO §C2）**：`bigfrac_binom`（任意有理上
  指标）与 `bigfrac_series_binom`（`(1+z)^n` 的精确截断）；`design §15.5` 记
  录 `(c+ε)^n` 幂展开与误差传播/尾界公式。测试 `tests/test_binom.c`。
- **级数误差路线 A（TODO §C2）**：几何型全局尾界
  `bigfrac_series_tail_geometric`（`|a_k|≤A·B^k`、`|z|≤r`、`q=Br<1` 时
  `≤A·qⁿ/(1−q)`）与自适应阶 `bigfrac_series_order_geometric`；`design §15.6`
  规划路线 A（精确系数 + 全局尾界）与路线 B（球式）。测试 `tests/test_series_bound.c`。
- **路线 A 数值求值（TODO §C2）**：新模块 `nex/series/`——`nex_series_binom_model`
  （(A,B) 模型）、`nex_series_binom_order`（自适应阶）、
  `nex_series_binom_eval_float` / `_decimal`（在 bigfloat/bigdecimal 点对
  `(1+z)^α` 自适应截断求值：精确部分和 + 终值一次正确舍入）。测试
  `tests/test_series_eval.c`。
- **路线 A 续：比值尾界 + exp/cos/sin（TODO §C2）**：`bigfrac_series_tail_ratio`
  （阶乘型系数，`t_n/(1−q_n)`）；`nex_series_exp/cos/sin_order` 与
  `_eval_float`/`_decimal`；求值新增可选 `err_out`（截断尾严格上界），二项式版
  同步支持。测试新增十进制 `exp(1)/cos(1)/sin(1)` 已知值对照。
- **逆幂核 `ln(1+z)` / `atan(z)`（TODO §C2）**：`nex_series_ln1p_order` /
  `atan_order` 与 `nex_series_{ln1p,atan}_eval_float` / `_decimal`（常数比值
  尾界 `r^step/(1−r^step)`，含可选误差输出）；测试含 `ln(1.5)`、`atan(0.5)`
  十进制已知值对照。
- **路线 B 第一阶段：球式算术（TODO §C2）**：新模块 `nex/ball/`——
  `bigfloat_ball_ty`（`mid`/`rad`，区间 `[mid−rad, mid+rad]`）+ `add/sub/mul/div`
  （端点间定向舍入求严格包含区间 + 中心化半径余量）、`contains`（向内舍入，
  无假阳性）。测试 `tests/test_ball.c`。
- **路线 B：`bigdecimal` 球式（TODO §C2）**：`bigdecimal_ball_ty` +
  `init/free/copy/set/contains/add/sub/mul/div`（十进制定向舍入严格区间）；测试
  覆盖 `[1,1]+[2,2]`、区间乘/除/减法与除零区间。
- **路线 B 实参规约（TODO §C2）**：`bigfloat_ball_exp/sin/cos`——折半（2 的幂
  精确缩放）→ 路线 A 级数得初始球 → `exp` 反复球平方 / `sin/cos` half-angle
  倍角；半径严格传播。测试含 `exp(1)≈e`、`exp(2)≈e²`、`sin(1)`、`cos(1)`、
  过大 |x| 报错。
- **路线 B：`bigdecimal` 球式实参规约（TODO §C2）**：`bigdecimal_ball_exp/sin/cos`
  （折半 + 球平方 / half-angle；eps 用 `10^{−(p+10)}`）。
- **C3 Padé 精确版（TODO §C3）**：`nex_series_pade_frac`——[m/n] Padé 逼近，
  bigfrac 精确高斯消元；`exp` 的 [2/2] 为 `(12+6z+z²)/(12−6z+z²)`。
- **C3 续 Padé 求值（TODO §C3）**：`nex_series_pade_eval_{frac,float,decimal}`
  （精确部分和 + 终值一次舍入）。
- **C4 mpmath 黄金参考（TODO §C4）**：`tests/golden_series_driver.c` +
  `tests/golden_series.py`（mpmath 独立参考；缺失时 SKIP）；CTest 新增
  `series_mpmath_golden`（`exp/sin/cos/ln1p/atan` × 64/128/256 bit）。
- **C2 剩余：级数求导/积分/复合（TODO §C2）**：`bigfrac_series_deriv` /
  `integral`（逐项，前导零归一；`z^{−1}` 项积分报 `INVALID`）与
  `bigfrac_series_compose`（秦九韶式，`f.val = 0`、`g.val ≥ 0`）。

## [1.2.0] - 2026-10-14

### 新增

- **文件级子集构建**（TODO §B1/B2）：机器可读模块清单 `cmake/nex_modules.txt`
  （模块 / 直接依赖 / 源文件 / 公开头）+ 依赖闭包；新增 `NEX_MODULES` 构建选项
  （默认 `all`），可只编译、只安装所选模块及其闭包。
- `scripts/modules.py`（清单校验 / 闭包查询）并接入 `api_audit.sh`；CI 新增
  `linux-subset` 子集构建矩阵；新增 `tests/subset_smoke.c`。
- **单文件组合**（TODO §B4）：`scripts/amalgamate.py` 生成 `nex_amalgam.c/.h`
  （去内部 `#include`、跨文件 `static` 按文件前缀改名）；新增 CMake 选项
  `NEX_AMALGAM`（配置期生成），CI 新增 `linux-amalgam` 用**全量 CTest** 验证
  与分文件构建等价。
- **子集/模块文档**（TODO §B5）：`scripts/modules.py gen-docs` 生成
  `docs/modules.md`（模块 / 依赖 / 公开函数），`docs <mod...>` 输出保留/移除
  清单；并入 `scripts/docs.sh`，CI `docs` job 校验无漂移。
- **场景示例**：`examples/amalgam/`（单文件组合 + 十进制财务求和）；CI 新增
  `linux-amalgam-example`（无 CMake 直接 `cc`）与 `linux-subset-install`
  （子集安装 + `examples/downstream` 消费）。
- **函数级子集**（TODO §B3）经评审**暂缓**：粗粒度特性宏亦不做（可选功能占比过
  小，见下）；替代为文档化工具链 DCE。

### 文档 / 说明

- **函数级子集**（TODO §B3）经评审**暂缓**：成本高、收益低。子集只减小
  归档/分发体积与构建面，**不减小最终可执行文件**；函数级可由工具链 DCE
  （`-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`）近似。详见
  README「体积说明」与 `docs/design.md` §2.4 / §13 #11。

## [1.1.0] - 2026-10-14

### 新增

- **C++ 互操作**：全部公开头文件补齐 `extern "C"` 守卫，C++11–C++26 可经
  C ABI 直接链接消费；新增 `tests/test_cpp_consumer.cpp` 回归测试。
- **C 标准矩阵**：新增 `NEX_C_STANDARD` 构建选项（默认仍为 C99），支持以
  C11/C17/C23（及 C2y）验证；CI 新增 C++ 与 C 标准矩阵 job。

### 文档

- `README.md` 与 `docs/design.md` §1.3 增补「标准兼容性」说明。

## [1.0.0] - 2026-10-14

首个正式发布：七种任意精度数值类型与跨类型转换矩阵，纯 C99、零外部依赖。

### 新增

- **类型**：`bigint_bin`（基 2³²）、`bigint_dec`（基 10⁹）、`bigint_bin64`
  （64 位肢变体）、`bigfrac`（有理数）、`bigfloat` / `bigdecimal`（可调精度
  二进制/十进制浮点，逐运算正确舍入）、`bigcomplex_float` /
  `bigcomplex_decimal`（复数）。
- **转换**：`bigint_conv`（bin ↔ dec）与 `convert`（七类型转换矩阵）。
- **算法**：schoolbook / Karatsuba / Toom-3 / 多模数 CRT NTT 乘法；
  Burnikel–Ziegler 递归除法；Lehmer gcd；Newton 整数开方；浮点复数 FFT
  （强制路径）；分治基数转换 bin ↔ dec；十进制字符串 I/O 快速路径。
- **构建与集成**：CMake `find_package(nex CONFIG)`、pkg-config、静态与共享
  库（`SOVERSION = 主版本号`）、`BUILD_SHARED_LIBS`。
- **文档**：参考手册 [`docs/manual.html`](docs/manual.html)（源 `docs/manual/`，
  API 章节由公开头文件生成）、设计文档 [`docs/design.md`](docs/design.md)、
  性能报告 [`docs/performance.md`](docs/performance.md)。
- **测试与 CI**：单元测试 + Python 精确参考黄金对拍 + fuzz + OOM 注入 +
  多种子；三平台（Linux / macOS / Windows）、GCC / Clang / MSVC、
  ASan / UBSan、覆盖率门槛、API 审计、文档同步校验。

### 说明

- **API / ABI**：1.x 线内 API 向后兼容；同一主版本内 ABI 稳定
  （公开结构体布局冻结，见设计文档「API/ABI 稳定性政策」）。
- **非目标**：SIMD / GPU / 内部多线程、超越函数（仅保留 `sqrt`）、次正规浮点、
  复数辐角 `arg`（依赖 `atan`）。详见设计文档「已知限制与非目标」。

[Unreleased]: https://github.com/MarchBeta2087/numextend/compare/v1.8.0...HEAD
[1.8.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.8.0
[1.7.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.7.0
[1.6.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.6.0
[1.5.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.5.0
[1.4.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.4.0
[1.3.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.3.0
[1.2.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.2.0
[1.1.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.1.0
[1.0.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.0.0
