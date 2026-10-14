# 更新日志

本文件记录 Numextend 的版本变更，格式参考
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

v1.0.0 之前的开发以设计文档的「修订记录」逐次跟踪（见
[`docs/design.md`](docs/design.md#修订记录)）。

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

[1.5.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.5.0
[1.4.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.4.0
[1.3.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.3.0
[1.2.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.2.0
[1.1.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.1.0
[1.0.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.0.0
