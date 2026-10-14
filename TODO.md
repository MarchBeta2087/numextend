# TODO（v1.1 及以后路线图）

> **活文档**：v1.0.0 – v1.4.0 已发布，本文件记录 **v1.4 之后的规划**。只收录已评估、
> 可执行的方向；未决/否定的候选集中列在文末。优先级见 §0。
> 最后更新：2026-10-14（v1.4.0：§G 已知技术债收口）

---

## 0. 优先级总览

| 优先级 | 方向 | 目标 | 依据 |
|--------|------|------|------|
| **P0** | C++ 互操作 + C 标准矩阵 | v1.1（低风险，尽快） | 实测：13/14 公开头缺 `extern "C"`，C++ 链接失败；C11–C2y 可编过但无 CI |
| **P1** | 子集化 + 单文件组合 | v1.2 | CMake 源列表显式、模块依赖单向；amalgamation 有 44 处跨文件 `static` 重名待解 |
| **P2** | 秦九韶 / 幂级数 / Padé | v1.3 | 精确有理数 `bigfrac` 天然适配；属超越函数前置基础设施 |
| **P3** | 超越函数（分阶段） | v1.x | `docs/design.md` §13 #8 已列；正确舍入工作量最大 |
| **P4** | 无穷列表（重新设计后决策） | 探索 | C99 无生成器，惰性抽象与本库句柄式 API 不契合 |
| **X** | 跨平台多线程封装 | 隔离评估 | §14 明确「内部多线程」为非目标；建议独立模块/仓库 |

依赖关系：`P0 → P1`（子集/合并需先固定公开面与兼容性）；`P2 → P3`（超越函数复用求值与级数工具）。

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
        （折半 + 球平方 / half-angle）；测试 `tests/test_ball.c`。待续：更大 |x|。
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
      （原为 [0,2]）。待续：超大 |x|（> 2⁶³）的 Payne–Hanek 规约（需高精度 π）。
- [x] **D4 `asin` / `acos` / `atan`**：✅ `bigfloat_asin`/`acos` 与
      `bigdecimal_asin`/`acos` 正确舍入（球式 `asin = atan(x/√(1−x²))`、`acos =
      π/2 − asin`；`atan` 见 D1）；特判 `acos(1)=0` 避免对称球跨 0 时 Ziv 不收敛。
- [ ] **D5 复数超越函数**（`bigcomplex_float` / `bigcomplex_decimal`）。
- [ ] **D6 正确舍入**：保护位 + 粘位、无双重舍入；补 §7/§9 语义规范。

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
- [ ] **F2 若实现**：`pthread` + Win32 双后端（不依赖 C11 `<threads.h>`：
  MSVC 支持不全、MinGW 视 winpthreads）；调用方驱动（parallel-for / 任务队列）
  + 确定性归约原语（固定分块、固定合并顺序）。
- [ ] **F3 CI**：独立 job + TSan（与 ASan job 分开；MSVC 无 TSan），不并入
  现有平台矩阵的默认构建。
- [ ] **F4 确定性**：并行归约须可复现；收益先量化（NTT 仅 ≥ 16384 肢启用）。

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

**非目标（除非 §F 明确独立化）：**

SIMD / GPU / 内部多线程 · 超越函数完整正确舍入前的中间态发布 ·
次正规浮点（§7.3）· 公开惰性无穷列表抽象（§E）· `NEX_MINIMAL` 预设
（依赖环使“最小”不成立）· 单头 STB `NEX_IMPLEMENTATION` 风格
（二文件 amalgam 已覆盖；如有需求再做）。

---

## I. v1.5.0 规划（候选）

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

## 流程与约定

- **分支**：`feature/*` / `chore/*` → PR → `dev` → `main`；PR 触发多平台 CI。
- **性能**：用 `-O2` 构建；`bench_sweep`（交错采样）做规模扫描。**基准必须与
  对比基线同优化级别**——不同 level 会颠倒算法交叉点（如 FFT 与 Toom-3）。
- **测试**：`ctest --test-dir build`（当前 41 项，含 Python/mpmath 黄金对拍）；
  平台矩阵含 32 位（`-m32 -msse2 -mfpmath=sse`）与大端（s390x + QEMU）。
- **文档**：算法与决策入 `docs/design.md`；API 手册由 `sh scripts/docs.sh` 生成
  （API 页取自头文件 + Pandoc 合成 `docs/manual.html`），手写页随能力更新；
  发版前按 `docs/maintenance.md` §4.1 清单同步。本文件只维护规划状态与优先级。
- **验收门槛**：每个 P1+ 方向落地时同步补 §A 提及的 CI/文档/黄金对拍。
