# TODO（v1.1 及以后路线图）

> **活文档**：v1.0.0 – v1.4.0 已发布，本文件记录 **v1.4 之后的规划**。只收录已评估、
> 可执行的方向；未决/否定的候选集中列在文末。优先级见 §0。
> 最后更新：2026-10-20（v1.8.0 进行中：超大位宽乘法 §K 已立项跟踪）

---

## 0. 优先级总览

| 优先级 | 方向 | 目标 | 依据 |
|--------|------|------|------|
| **P0** | C++ 互操作 + C 标准矩阵 | v1.1（低风险，尽快） | 实测：13/14 公开头缺 `extern "C"`，C++ 链接失败；C11–C2y 可编过但无 CI |
| **P1** | 子集化 + 单文件组合 | v1.2 | CMake 源列表显式、模块依赖单向；amalgamation 有 44 处跨文件 `static` 重名待解 |
| **P2** | 秦九韶 / 幂级数 / Padé | v1.3 | 精确有理数 `bigfrac` 天然适配；属超越函数前置基础设施 |
| **P3** | 超越函数（分阶段） | v1.x | `docs/design.md` §13 #8 已列；正确舍入工作量最大 |
| **P3.5** | 超大实参三角（Payne–Hanek） | v1.6 ✅ | §16.2 唯一遗留正确性缺口；已落地 |
| **P4** | 无穷列表（重新设计后决策） | 探索 | C99 无生成器，惰性抽象与本库句柄式 API 不契合 |
| **X** | 跨平台多线程封装 | 隔离评估 | §14 明确「内部多线程」为非目标；建议独立模块/仓库 |
| **Y** | 超大位宽乘法（SSA / out-of-core） | 10⁹–10¹⁰ 位 | issue [#98](https://github.com/MarchBeta2087/numextend/issues/98) / [#102](https://github.com/MarchBeta2087/numextend/issues/102) / [#104](https://github.com/MarchBeta2087/numextend/issues/104)；实验均在 `contrib/`，不进核心 AUTO |

依赖关系：`P0 → P1`（子集/合并需先固定公开面与兼容性）；`P2 → P3`（超越函数复用求值与级数工具）。

> **命名注意**：本文件的 `P0–P4` 是**库自身路线图**编号（标准兼容 / 子集 / Padé /
> 超越函数 / 无穷列表）。issue #98 另有一套 `P0–P4`（P0 可行性、P1 大数乘法、
> P2 Lean 4 形式化、P3 OpenCL、P4 提升），两者**同名不同义**；后者统一记在 §K。

---

## A. P0 标准兼容与 C++ 互操作（v1.1）— ✅ 已完成

- [x] **A1 公开头文件补齐 `extern "C"` 守卫**
  - 已完成：13 个公开头 + `nex/nex_version.h.in` 全部加 `#ifdef __cplusplus /
    extern "C" {` 包裹（`nex/nex_alloc.h` 原已有）。
  - 验收：新增 `tests/test_cpp_consumer.cpp`（含全部公开头 + 真实运算 + 各模块
    符号解析），GCC 15.2 在 C++11/17/23/26 下编译、链接、运行全通过。
- [x] **A2 CI 增加 C++ consumer job**
  - 已完成：`.github/workflows/ci.yml` 新增 `linux-cpp`，矩阵 C++11/17/23，
    构建 `test_cpp_consumer` 并 `ctest -R cpp_consumer`。
- [x] **A3 CI 增加 C 标准矩阵**
  - 已完成：`CMakeLists.txt` 新增 `NEX_C_STANDARD` 缓存选项（默认 99；
    原先 `set(CMAKE_C_STANDARD 99)` 会遮蔽命令行 `-D`，故改为选项驱动）；
    CI 新增 `linux-c-std`，矩阵 C11/17/23，全量构建 + `ctest`。
  - 实测（本地 GCC 15.2）：C99/C11/C17/C23/C2y 全部无警告编译通过。
- [x] **A4 文档：兼容性矩阵**
  - 已完成：`README.md` 新增「兼容性」节；`docs/design.md` §1.3 目标补充
    C99 基准 + C11–C2y 实测 + C++11–26 经 C ABI 消费。
- [x] **A5 `CHANGELOG.md` 条目**：新增 `[Unreleased]` 节（C++ 互操作、C 标准矩阵）。

---

## B. P1 子集化与单文件组合（目标 v1.2）

### B1 模块依赖清单（机器可读）— ✅ 已完成

- [x] `cmake/nex_modules.txt`：每行 `<module>|<deps>|<sources>|<public_headers>`，
      依赖由各 `.c`/内部头的 `#include` 推导（允许成环，如
      `bigint_bin ↔ bigint_conv`）。供 CMake 与 `scripts/modules.py` 共用。
- [x] `scripts/modules.py check` 校验清单与源码树一致（每个 `.c` / 公开头恰好
      登记一次、依赖已知），已接入 `scripts/api_audit.sh`（CI 运行）。
- 实测依赖：`nex_alloc`/`fft`/`ntt` 为叶子；`bigint_bin`→conv（十进制字符串
  快速路径）故整数层闭包连带 dec/conv。

### B2 文件级子集（一期）— ✅ 已完成

- [x] CMake 选项 `-DNEX_MODULES=bigint_bin,bigfloat,...`（默认 `all`），按依赖
      闭包自动补齐源文件与公开头；只编译、只安装闭包内容。
- [x] 子集构建跳过全量测试/演示/基准，仅构建 `subset_smoke`（闭包含
      `bigint_bin` 时）；CI 新增 `linux-subset` 矩阵。
- [x] 验收：`bigint_bin,bigfrac` / `bigfloat` / `fft,ntt` / `convert` 均通过；
      `convert` 闭包正确排除无依赖的 `bigint_bin64`。
- 决策：`NEX_MINIMAL` 预设**移入 §H（非目标）**——依赖环使“最小”名不副实
  （`NEX_MODULES=bigint_bin` 闭包已含 dec/conv/fft/ntt）；`NEX_MODULES` 已足够。

### B3 函数级子集 — ⏸ 暂缓（不承诺），改为低成本替代

**评审结论（2026-10-14）**：

- **裁剪不减小最终可执行文件**：同口径静态链接，全量库与 `bigint_bin` 子集库
  产出**相同** text（90 068 B）；子集只减小归档/分发体积与构建面
  （230 KB → 111 KB → 9 KB 等）。
- **工具链 DCE 可近似函数级裁剪**：消费者用
  `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`（ELF 有效；
  PE/MinGW 基本无效），无需源码改动。
- **成本高**：需精确函数级调用图（文件粒度是伪稠密）+ 定义/调用点条件编译
  + 组合测试（2^203 不可测）+ ABI 政策调整（§1.4）。收益远不及成本。

**替代（低成本）**：

- [x] 文档化工具链 DCE 与“子集只减归档不减可执行”的事实（README / design §2.4）。
- [x] 场景示例：`examples/amalgam/`（单文件 + 十进制财务）；CI
      `linux-amalgam-example` / `linux-subset-install`。

（粗粒度特性宏经评估不现实——全库 text+data ≈ 155 KB，`fft`=1.5 KB、
`ntt`=4.6 KB，可选函数占比过小；决定不做，理由见 design §2.4/§13 #11/§14。）

### B4 单文件组合（amalgamation）— ✅ 已完成

- [x] `scripts/amalgamate.py` 生成 `nex_amalgam.c` / `nex_amalgam.h`：
      公开头 → 单头；内部头 + 源文件 → 单源；剥离内部 `#include "nex/..."`；
      跨文件重名 `static` 按文件前缀改名（实测 43 处）；版本宏内联。
- [x] CMake 选项 `-DNEX_AMALGAM=ON`（配置期生成，单 TU 构建）。
- [x] 验收：`linux-amalgam` CI 用**全量 CTest（34/34）**验证与分文件构建等价；
      子集 amalgam（`bigfloat`）亦通过 `subset_smoke`。
- 决策：可选单头（STB `NEX_IMPLEMENTATION` 风格）**移入 §H（非目标）**——二文件
  `nex_amalgam.c/.h` 已覆盖“拖入即用”；若出现明确需求，为约 30 行的生成器扩展
  （`scripts/amalgamate.py --single-header`），届时再做。

### B5 子集/合并文档生成 — ✅ 已完成

- [x] `scripts/modules.py gen-docs` → `docs/modules.md`（模块 / 依赖 / 公开函数）。
- [x] `scripts/modules.py docs <mod...>` 输出子集保留/移除清单（模块 / 文件 /
      函数）。
- [x] 并入 `scripts/docs.sh`；CI `docs` job 以
      `git diff --exit-code -- docs/manual docs/modules.md` 校验无漂移。

---

## C. P2 秦九韶 / 幂级数 / Padé 基础设施（目标 v1.3）— ✅ 已完成

- [x] **C1 秦九韶（Horner）多项式求值**
  - 已落地：`*_poly_eval` 覆盖 `bigint_bin`/`bigfrac`/`bigfloat`/`bigdecimal`；
    精确（整数/有理数）或逐次正确舍入（浮点，按 `ctx`）；out 允许别名，强异常安全。
  - 测试：`tests/test_poly.c`（`poly_horner`）。
- [x] **C2 幂级数**
  - [x] `bigfrac_series_ty` + `from_array`/`copy`/`add`/`sub`/`mul`（截断
        `order`）/`inv`（Laurent）/`eval`（含负幂）；测试 `tests/test_series.c`。
  - [x] 广义二项式系数 `bigfrac_binom` + 二项式级数 `bigfrac_series_binom`；
        测试 `tests/test_binom.c`；误差传播公式入 design §15.5。
  - [x] 路线 A 底层：几何型全局尾界 `bigfrac_series_tail_geometric` +
        自适应阶 `bigfrac_series_order_geometric`；测试 `tests/test_series_bound.c`；
        design §15.6。
  - [x] 路线 A 求值（`nex/series/`）：`nex_series_binom_model`（(A,B)）、
        `nex_series_binom_order`、`nex_series_binom_eval_float` / `_decimal`
        （bigfloat/bigdecimal 点自适应求值，精确部分和 + 终值一次舍入）；
        测试 `tests/test_series_eval.c`；design §15.6。
  - [x] 比值型尾界 + 阶乘核：`bigfrac_series_tail_ratio`；
        `nex_series_exp/cos/sin_order` 与 `_eval_float`/`_decimal`；求值新增可选
        `err_out` 输出截断尾上界（二项式版同步支持）；测试含十进制
        `exp(1)/cos(1)/sin(1)` 已知值对照。
  - [x] 逆幂核：`nex_series_ln1p_order`/`atan_order` 与
        `nex_series_{ln1p,atan}_eval_float`/`_decimal`；测试含 `ln(1.5)`、
        `atan(0.5)` 十进制已知值对照。
  - [x] 路线 B：`nex/ball/` 的 `bigfloat_ball_ty` / `bigdecimal_ball_ty` +
        `add/sub/mul/div/contains`（定向舍入严格区间）；实参规约
        `bigfloat_ball_exp/sin/cos` 与 `bigdecimal_ball_exp/sin/cos`
        （折半 + 球平方 / half-angle）；测试 `tests/test_ball.c`。v1.6 起
        sin/cos 的大 |x| 由 Payne–Hanek（§16.2）处理，log 用指数拆分（§16）。
  - [x] 数值类型（bigfloat/bigdecimal）严谨误差界 + 自适应截断（路线 A
        `*_order` + `err_out`；路线 B 球式传播）。
  - [x] 通用（球系数）数值级数 `bigfloat_ball_series` / `bigdecimal_ball_series`
        ——已在 §I（v1.5.0）落地（`nex/ball/nex_ball_series*.c`）。
  - [x] 复合与求导 / 积分：`bigfrac_series_deriv` / `integral`（前导零归一；
        `z^{−1}` 项积分报错）/ `compose`（秦九韶式）；测试入 `tests/test_series.c`。
- [x] **C3 Padé 逼近**：`nex_series_pade_frac`（bigfrac 高斯消元，[m/n] 系数
      精确；`exp` [2/2] `(12+6z+z²)/(12−6z+z²)`）与求值
      `nex_series_pade_eval_{frac,float,decimal}`（精确部分和 + 终值一次舍入）；
      测试入 `tests/test_series_eval.c`；design §15.7。
- [x] **C4 外部高精度黄金参考**：`tests/golden_series_driver.c` +
      `tests/golden_series.py`（**mpmath** 参考；缺失时 SKIP）；CI/CTest 新增
      `series_mpmath_golden`，覆盖 `exp/sin/cos/ln1p/atan` 的 64/128/256 bit。
- [x] **C5 规范**：`docs/design.md` 新增「§15 多项式与级数」（含 Horner 舍入协定
      与 Laurent/负幂级数设计）；级数误差界随 C2 细化。

---

## D. P3 超越函数（`design.md` §13 #8 落地，分阶段）

> 前置：C1–C5。每阶段须有**误差分析 + 外部黄金 + 定向边界用例**；
> `bigfloat` 与 `bigdecimal` 的规约各自实现。

- [x] **D1 `atan` / `atan2` / `arg`**：✅ `bigfloat_atan` + `bigdecimal_atan`
      （正确舍入：球式规约 + Ziv；含 `bigdecimal_ball_sqrt`/`bigdecimal_ball_atan`）；
      ✅ `bigfloat_atan2` / `bigdecimal_atan2`（直角分象限 + π 球，避大参数与
      灾难抵消）与复数辐角 `bigcomplex_float_arg` / `bigcomplex_decimal_arg`
      （重新引入，§9.2）；单元测试（含象限/轴/负实轴）+ 随机 mpmath 压测。
- [x] **D2 `exp` / `log` / `ln`**：✅ `bigfloat_exp` / `bigdecimal_exp`、
      `bigfloat_log` / `bigdecimal_log`（正确舍入；log 用球式迭代平方根规约
      `y=x^(1/2^k)` → `ln1p` 级数 → 2^k 球加法回放；球式 `ball_log` 公开）；
      测试 + mpmath 黄金 `exppub`/`exppubdec`/`logpub`/`logpubdec`。
- [x] **D3 `sin` / `cos` / `tan`**：✅ `bigfloat_*` / `bigdecimal_*` 正确舍入
      （球式折半 + half-angle 倍角 + Ziv；球式 `ball_tan` = sin/cos）；折半上限
      提到 64（覆盖 |x| ≲ 2⁶³）；修复 `sincos_reduced` 的 `one_ball` 半径错误
      （原为 [0,2]）。✅ v1.6 已落地：超大 |x|（> 2⁶³）改走 Payne–Hanek
      规约（Chudnovsky 二分拆分 π + 定点 2/π；float 与 decimal 两套，§16.2）。
- [x] **D4 `asin` / `acos` / `atan`**：✅ `bigfloat_asin`/`acos` 与
      `bigdecimal_asin`/`acos` 正确舍入（球式 `asin = atan(x/√(1−x²))`、`acos =
      π/2 − asin`；`atan` 见 D1）；特判 `acos(1)=0` 避免对称球跨 0 时 Ziv 不收敛。
- [x] **D5 复数超越函数**：✅ `bigcomplex_float_*` / `bigcomplex_decimal_*` 的
      `exp`/`log`/`sqrt`/`sin`/`cos`/`tan`（分量运算，各步按 `ctx` 舍入；
      `sinh`/`cosh` 由 `exp` 组合；NaN 分量传播、含 ±∞ 返回 INVALID）；
      单元测试 + 本机 mpmath 压测（float/decimal）。
- [x] **D6 正确舍入规范收口**：✅ design **§16.1** 汇总正确舍入语义与边界
      （算术：保护位 + 粘位、一次舍入、无双重舍入；超越：球式严谨界 + Ziv；
      复合：各步正确舍入；Ziv 终止/特判/范围/无次正规）；§7.3、§9.2 交叉引用。

---

## E. P4 无穷列表（已决策：不引入）

- [x] **E1 设计决策**：**不将惰性无穷列表作为公开抽象**。C99 无生成器/协程，
      惰性求值需状态机、`setjmp/longjmp` 或线程，与本库句柄式风格及规范 §13
      冲突；且对数值超越函数收益低（详见 `docs/design.md` §15.4）。
- [x] **E2**：惰性思想的**算法化**优先（自适应截断 + 尾界、Ziv、binary
      splitting、实参规约），均为有限循环，不依赖无穷列表。
- [x] **E3 决策记录**：已写入 `docs/design.md` §15.4（并预留修订记录）。

---

## F. X 跨平台多线程封装（隔离评估，默认不进核心）

- [x] **F1 决策**：作为**独立可选目标**（**不属 v1.5.0**）推进，**不进入核心
  `libnex`**、也不进入 `nex_modules.txt`（即不进 `all`、子集闭包与 amalgam），
  故核心「零依赖 / C99 / 全平台 / 单文件组合」契约不受影响；定位为**调用方
  驱动**的并行工具，不改变 §3.3「库内不加锁」（§14 将内部多线程列为非目标）。
  实现前须有具体消费方。
  - 形态：源码置于 `nex/` **之外**（如 `contrib/nex_thread/`）或独立仓库，配
    独立 CMake target（`find_package(Threads)`，`-DNEX_ENABLE_THREAD=ON` 显式
    启用）；这样不触动 `scripts/modules.py` 的「`nex/**` 全部登记」校验。
  - 若坚持置于 `nex/`：需扩展 modules.py/cmake 支持「optional 模块类」并从
    默认构建、子集与 amalgam 排除（否则 `modules.py check` 会失败）。
- [x] **F2 实验实现**：`contrib/nex_thread` 最小线程池（pthread + Win32
  `CONDITION_VARIABLE`，不依赖 C11 `<threads.h>`）；调用方驱动 `run` +
  唯一 index，固定分块由调用方给出；无全局状态/无内部锁。
- [ ] **F3 CI**：独立 job + TSan（与 ASan job 分开；MSVC 无 TSan），不并入
  现有平台矩阵的默认构建。（尚未接入）
- [x] **F4 确定性与收益量化**：两个消费方均与串行结果逐位一致；单次
  16384/32768/65536/131072 肢 NTT 乘法 16 线程 **2.50× / 3.11× / 3.58× /
  4.92×**（扁平任务图 + 并行 Garner + 分块进位；4M bit 达 4.9×）；
  批量 `a*b+c`（256–4096 bit，8k–100k 元素）6.5–7.7×。详见
  `contrib/README.md`。
  结论：保持 contrib 隔离，不开核心特例；单次乘法收益有限，批量场景
  才是线程池主场。

---

## G. 已知技术债 / 风险（v1.4.0 后）

- **`bigint_dec` 无亚二次除法**：✅ v1.5.0 已解决——移植 Burnikel-Ziegler 递归
  除法（`dbz_div_rem`；基 10⁹ 以 Knuth-D 归一化因子 d + 肢移位 pad 代替 bin 的
  位规约），除数 ≥ 64 肢且被除数 ≥ 2×除数自动分派；10⁵ 位 ≈ 45 ms（原 Knuth D
  ≈ 360 ms，约 8×）。见 §I。

### v1.4.0 已关闭

- **构建优化级别**：✅ 已解决——`CMakeLists.txt` 未指定 build type 时默认
  `RelWithDebInfo`（-O2 -g）；“基准须同优化级别”的纪律移入「流程与约定」。
- **16-bit FFT 舍入安全界**：✅ 16-bit 强制路径限
  `S ≤ NEX_MUL_FFT_B16_MAX_SUM`（1024），超限返回 `BIGINT_ERR_UNSUPPORTED_E`；
  8-bit 已验证至 65536 肢。测试 `test_fft_mul::test_fft16_cap`。
- **未规范化视图比较**：✅ `bz_mag_cmp` 先裁剪前导零；审计内部比较仅此一处
  依赖视图（其余依赖规范化不变式）；规则入 design §3.4。
- **值相关缺陷**：✅ `test_bigint_bin::test_sparse`（乘法/gcd 闭式）+ BZ 稀疏
  除法用例 + golden `10^k` 邻域。
- **大端 / 32 位平台**：✅ CI 覆盖——`linux-32bit`（`-m32 -msse2 -mfpmath=sse`）与
  `linux-bigendian`（s390x + QEMU；交叉构建跳过 Python 黄金测试）。（§1.3、§14）

---

## H. 已否定 / 非目标

**已否定（不实现，理由见设计文档修订记录）：**

标量浮点 FFT 接入 AUTO（v0.10/v0.17）· 十进制快速乘法（v0.13）·
Montgomery 模幂（v0.16）· Newton 整数除法（v0.16）· NTT 三模数（v0.9）·
十进制 FFT 每肢 4 节 × 256 方案（v0.7）。

**非目标（除非 §F / §K 明确独立化）：**

SIMD / GPU / 内部多线程 · 超越函数完整正确舍入前的中间态发布 ·
次正规浮点（§7.3）· 公开惰性无穷列表抽象（§E）· `NEX_MINIMAL` 预设
（依赖环使“最小”不成立）· 单头 STB `NEX_IMPLEMENTATION` 风格
（二文件 amalgam 已覆盖；如有需求再做）。

> GPU/OpenCL 的例外：issue #98 P3 只允许作为**独立可选目标**
> `contrib/nex_opencl` 存在（默认 OFF，不进 `libnex` / `nex_modules.txt` /
> 子集闭包 / amalgam，不开核心规范特例，见 §K）；核心仍无 SIMD/GPU 代码。

---

## I. v1.5.0 已落地（历史）

- **通用（球系数）数值级数**：✅ `bigfloat_ball_series` 与
  `bigdecimal_ball_series`（init/free/from_array/copy/add/sub/mul/eval/compose）
  均已落地；测试 `tests/test_ball.c`；design §15.8。
- **`bigint_dec` 亚二次除法**：✅ 已实现 Burnikel-Ziegler（`dbz_div_rem`；dec 基
  10⁹ 的 Knuth-D 归一化 + 肢移位规约，阈值 64 肢自动分派）；回归
  `tests/test_bigint_dec::test_bz`（分派边界/随机/稀疏/全 9/精确整除）；
  `bench_sweep` dec 除法扫描更新。实测 n=1024 肢 14→2.9 ms、2048 肢 8.2 ms、
  10⁵ 位 ≈ 45 ms（约 8×）。
- **退役一次性会话备忘 `MEMO.md`**（已在 chore #59 完成）：耐久内容并入
  `docs/maintenance.md` §8 与 `design §3.4/§G`。

---

## J. v1.6 进展（Payne–Hanek）

- [x] **J1 高精度 π**：`nex/ball/nex_ball_pi.c`（内部）Chudnovsky + 整数二分
      拆分，每项约 47 bit，96 bit 保护位；输出 `floor(π·2^p)`；位数上限
      `NEX_PH_MAX_PI_BITS = 2²⁰`。
- [x] **J2 float 规约**：`nex_ph_reduce_internal`（定点 `2/π` → 就近象限
      + `r ∈ [−π/4,π/4]`）；`bigfloat_ball_sin/cos/tan` 在 `|x| > 2⁶³` 时接入；
      误差界（2/π、π/2、商截断）以 `2^{err_exp}` 合成。
- [x] **J3 decimal 规约**：同一二进制内核 + `R·2^{-K} = (R·5^K)·10^{-K}`
      精确十进制换算；舍入按 `10^{d-p+1}` 计入。
- [x] **J4 宽区间**：输入球半径 ≥ 4（长度 > 2π）时 `sin`/`cos` 返回 `[−1,1]`。
- [x] **J5 测试**：`test_transcend`（2¹⁰⁰、1e30、1e100、正负号/象限）、
      `test_ball`（PH 路径、宽区间、十进制 1e30）；`golden_series.py` 新增
      2¹⁰⁰/2²⁰⁰/1e30/1e100/1e1000 的 mpmath 对拍，参考精度按实参量级自动提高。
- [x] **J6 文档**：design §16.1/§16.2/§16.3、§13 #8、§14；README 测试数与
      模块说明；`nex_ball.h` 过时注释（k > 20 → 64 / PH）。
- [x] **J7 `log` 上限评估与重构**：原 40 次开方上限的真实瓶颈是巨大指数下
      `ball_sqrt` 端点的精确对齐；改为**指数拆分 + 小实参规约**
      （`x = m·2^E` / `m·10^E`，`ln x = ln m + E·ln2` / `E·ln10`），
      `|ln x|` 不再受限，上限 16 仅用于 `m ∈ [1,2)`/`[1,10)`。
      测试：2^(2^40)、2^(2^56)、10^(6e10)（旧上限 40 会 INVALID）。
- [x] **J8 路线 A 高精度加速**：部分和与阶估计改为扩展精度浮点递推
      （`p + 48 + log2 n`，尾界向上舍入；decimal 复用 bigfloat 阶估计），
      去除精确有理公共分母。实测 exp 4096 bit 10.2 s → 63 ms，
      ln1p 1.64 s → 180 ms，`log(3)` 512 bit 11.0 s → 4.6 ms。
- [x] **J9 复数 Smith 稳定除法**：float/decimal 有限输入用 Smith 缩放，
      ±∞ 分量回退共轭法；新增极大/极小分量单元测试；黄金参考同步。
- [x] **J10 性能测试工具**：新增 `bench/bench_math.c` + `scripts/bench-math.sh`：
      - `trans`：所有公开超越函数（float/decimal/复数，含 `atan2`/`arg`）×
        多种精度；
      - `const`：π（4atan1/2asin1/acos(−1)/2acos0/Machin/Chudnovsky）、
        e（exp1/sqrt(exp2)/exp(.5)²/级数）、ζ(3)（Apéry float/decimal、
        Euler 变换低精度）、γ（Brent–McMillan n / 2n、decimal）；
      - `throughput`：各规模整数/浮点/十进制运算的 ops/s。
      分段可选（`--sections`），预设 full/quick/ci（默认预算 7200/600/300 s）；
      `--json` 输出 JSON Lines。CI 每日运行 `--preset ci --budget 300` 并上传
      artifact。全量本地预算 2 小时；本机实测 5791.8 s（约 96.5 min），
      结果与解读见 `docs/bench-math.md`。v1.7 起 `throughput` 的 full 档
      新增 **32768 bit**（实测该段 31 → 109 s，约 +1.3 min；full 总耗时仍
      在 2 h 预算内），32768 bit 下 `log` 仍最慢（约 55.5 s/次）。
      夜间趋势基线：`bench/baseline/` + `scripts/bench-math-compare.py`
      （advisory ±20%，低噪声 <10 µs；`--fail-over` 预留门禁；更新流程
      见 `bench/baseline/README.md`）。首个基线已从 dev dispatch
      `39184434034` 播种（Ubuntu 24.04 / 4 核 EPYC / GCC 13.3.0）。

---

## K. Y 超大位宽乘法（SSA / out-of-core，issue #98–#104）

目标：在单机上推到 10⁹–10¹⁰ 位十进制乘法。**当前全部是 `contrib/` 实验**，
默认 OFF，不进入核心 `bigint_bin` 的 AUTO 分派、amalgam 与子集闭包。
跟踪以 Issue 为准（[#98](https://github.com/MarchBeta2087/numextend/issues/98)
父项、[#102](https://github.com/MarchBeta2087/numextend/issues/102) SSA、
[#104](https://github.com/MarchBeta2087/numextend/issues/104) out-of-core）。

### K1 已完成（v1.8.0 进行中）

- [x] **Y1 P0 可行性与基准**（#98 / PR #99）：`scripts/bigint-feasibility.py`、
      `bench/bench_huge.c`；10⁶/10⁷/10⁸ 位直接 NTT = 0.29/7.5/195.8 s、
      Peak RSS 12.6/75.5/930 MB；原上限 `sum(limbs) ≤ 2^26`、`log2n ≤ 27`
      （约 3.2×10⁸ 位）。
- [x] **Y2 P1a 分块 NTT**（#98 / PR #100）：`nex/ntt` 内置模数扩展 c=28/30，
      直接上限 `sum(limbs) ≤ 2^27`、`log2n ≤ 28`；`contrib/nex_bigint_block`
      三分块（三个半积），10⁹ 位 **2403 s（40.1 min）/ 6.24 GB**。
- [x] **Y3 P1b 递归多级分块**（PR #101）：单块 `max_len ≤ 2^26` 递归折半；
      2×10⁹ 位 × 小乘数通过；10¹⁰ 位结构可行（~4 层、81 个基础积），
      但 34 GB 机器在合并阶段 OOM（模型 ~64 GB）。
- [x] **Y4 P1b-SSA M1 原型**（#102 / PR #103）：`contrib/nex_ssa`，
      1e6–1e8 位跑通；1e8 位 **1142–1318 s / 1.66 GB**，≤3×10⁶ 肢与 NTT
      逐位对拍通过。结论：10¹⁰ 位双环峰值仍 ~100 GB（标准模型 111.5 GB）。

### K2 未开始

- [ ] **Y5 SSA M2 递归环变换**（#102）：1e8–1e9 位，峰值 RSS 目标 <16 GB，
      时间不劣于分块 NTT 的 2403 s。
- [ ] **Y6 SSA M3 10¹⁰ 位**（#102）：34 GB 目标；若不够则记录实际门槛或转 Y7。
- [x] **Y7 OOC M1**（#104）：`contrib/nex_ooc`（肢体文件、块切分、
      磁盘进位链、规范化裁剪；随 `-DNEX_ENABLE_BIGNUM=ON` 构建）。
      峰值内存 O(块长)：1e9 位 ×3 = 0.8 s / **163.9 MB**（13 块，与
      schoolbook 逐位一致）；1e8 大×大 = 227 s / 651.5 MB（2×2 块，与
      内存 NTT 逐位一致）；1e7 大×大 = 19.9 s / 16.3 MB（8×8 块）。
      未用 mmap（`fread`/`fseeko` 64 位定位），块缓存/并行/模素数校验留 M3/M2。
      1e9×3 只验证进位链，不含 1e9 规模的块对 NTT（见 §K3 与 README）。
- [ ] **Y8 OOC M2**（#104）：按实测修订为两步 ——
      **M2a（保底）**：`L=2^26` 朴素分块（16 块 / 256 块对），外推 ~80 h、
      峰值 ~5–6 GB；结果用 `--verify-mod`（已实现，5 素数流式）+ 肢数检查。
      先补 `P(2^26)` 单块对实测（~20 min）闭环外推。
      **M2b（可选提速）**：块级 Karatsuba（16 块 → 4 层 → 81 叶子积）
      理论 ~3× → ~1 天；每层需把「块和」落盘。块级 FFT 并入 #102 SSA 决策。
- [ ] **Y9 OOC M3（可选）**（#104）：块级 FFT/Karatsuba 减少块对、并行块对
      （可复用 `contrib/nex_thread`）、进位前缀并行。
- [ ] **Y10 待决策**（#98）：硬件门槛（≥128–256 GB 单机 vs out-of-core）、
      Lean 4 验证边界与工具链、OpenCL 最低版本/设备范围、GPU CI、时间盒。

### K3 门槛与约束

- **总时间 ≈ `pairs × P(L)`**（`pairs = na·nb`，`P(L)` = 单块 L×L 耗时）。
  固定 n 时总时间随 nb 近似线性：实测 n = 1e7 位，nb = 1/2/4/8/16/32 →
  4.6/6.6/11.7/19.6/26.3/50.0 s，每次翻倍 ×1.34–1.9（支持
  `nb·n·log(n/nb)` 量级，且该模型偏保守）。
- 块数（此处曾写错，已更正）：1e10 位 = 1.0381e9 肢 → `L=2^23` 时
  nb = **124**（pairs 15,376）、`L=2^25` 时 nb = 31、`L=2^26` 时 nb = 16。
- 最大块长受直接 NTT 上限约束（`s ≤ 2^27` ⇒ `L ≤ 2^26`）；L=2^26 时
  34 GB 内 NTT 工作区约 6.2 GB（1e9 三分块实测）+ 自身缓冲 ≈ 8 GB，
  仍在预算内。
- 大 L 锚点（实测）：P(8.39M 肢)=56.8 s（1e8 大×大）、P(33.55M 肢)=415.6 s
  （1e9 大×大，6648.9 s / 16 块对，峰值 RSS 2.63 GB）⇒ L 翻 4 倍 P 涨 7.3 倍
  = `P ∝ L^1.44`（`L log L` 只预测 4.3 倍）。
- 1e10 外推：`L=2^23` → 242 h；`L=2^25` → 111 h；`L=2^26`（上限，16 块，
  峰值 ~5–6 GB）→ **~80 h（3.3 天）**。
- 结论修正：**"朴素分块在 34 GB 下不可行"不成立**——M2 可行，是「3 天级」
  而非「小时级」；M2 验收不设时间上限。故 **M2a = 先用 `L=2^26` 朴素分块
  跑通 1e10（保底）**，M2b = 块级 Karatsuba 提速（可选）；块级 FFT 应并入
  #102 的 SSA M3 决策，不在 OOC 线重复实现。
- 10¹²（万亿）位不在本路线承诺内（单操作数 ~415 GB，属 out-of-core/分布式研究）。
- 任何时候不改变「零依赖 / C99 / 单文件组合 / 库内不加锁」契约。
- 只有满足 ≥2× 且逐位一致、可复现、CI 可维护，才讨论进入正式支持面（#98 P4）。

---

## 流程与约定

- **分支**：`feature/*` / `chore/*` → PR → `dev` → `main`；PR 触发多平台 CI。
- **性能**：用 `-O2` 构建；`bench_sweep`（交错采样）做规模扫描。**基准必须与
  对比基线同优化级别**——不同 level 会颠倒算法交叉点（如 FFT 与 Toom-3）。
- **测试**：`ctest --test-dir build`（当前 42 项，含 Python/mpmath 黄金对拍）；
  平台矩阵含 32 位（`-m32 -msse2 -mfpmath=sse`）与大端（s390x + QEMU）。
- **文档**：算法与决策入 `docs/design.md`；API 手册由 `sh scripts/docs.sh` 生成
  （API 页取自头文件 + Pandoc 合成 `docs/manual.html`），手写页随能力更新；
  发版前按 `docs/maintenance.md` §4.1 清单同步。本文件只维护规划状态与优先级。
- **验收门槛**：每个 P1+ 方向落地时同步补 §A 提及的 CI/文档/黄金对拍。
