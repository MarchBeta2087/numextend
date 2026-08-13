# Numextend 设计文档

> 版本：v0.12（草案）
> 适用代码：`nex/` 目录下全部 C 源代码
> 关联文档：`coding_standard.md` v1.4（编码规范，本文档中的所有命名均遵循之）
> 许可证：MIT

---

## 目录

- [Numextend 设计文档](#numextend-设计文档)
  - [目录](#目录)
  - [1. 引言](#1-引言)
    - [1.1 项目定位](#11-项目定位)
    - [1.2 类型总览](#12-类型总览)
    - [1.3 目标与非目标](#13-目标与非目标)
  - [2. 总体架构](#2-总体架构)
    - [2.1 模块分层与依赖](#21-模块分层与依赖)
    - [2.2 目录结构与文件命名](#22-目录结构与文件命名)
    - [2.3 模块交互规则](#23-模块交互规则)
  - [3. 通用约定](#3-通用约定)
    - [3.1 错误码](#31-错误码)
    - [3.2 内存管理策略](#32-内存管理策略)
    - [3.3 线程安全](#33-线程安全)
    - [3.4 规范化不变式总表](#34-规范化不变式总表)
  - [4. bigint_bin_ty：二进制肢大整数](#4-bigint_bin_ty二进制肢大整数)
    - [4.1 数据结构](#41-数据结构)
    - [4.2 对外 API](#42-对外-api)
    - [4.3 内部算法](#43-内部算法)
  - [5. bigint_dec_ty：十进制肢大整数](#5-bigint_dec_ty十进制肢大整数)
    - [5.1 数据结构](#51-数据结构)
    - [5.2 对外 API](#52-对外-api)
    - [5.3 内部算法](#53-内部算法)
  - [6. bigfrac_ty：大有理数](#6-bigfrac_ty大有理数)
    - [6.1 数据结构](#61-数据结构)
    - [6.2 对外 API](#62-对外-api)
    - [6.3 内部算法](#63-内部算法)
  - [7. bigfloat_ty：可调精度二进制浮点数](#7-bigfloat_ty可调精度二进制浮点数)
    - [7.1 语义模型](#71-语义模型)
    - [7.2 数据结构](#72-数据结构)
    - [7.3 精度上下文](#73-精度上下文)
    - [7.4 对外 API](#74-对外-api)
    - [7.5 内部算法](#75-内部算法)
  - [8. bigdecimal_ty：可调精度十进制浮点数](#8-bigdecimal_ty可调精度十进制浮点数)
    - [8.1 语义模型与数据结构](#81-语义模型与数据结构)
    - [8.2 对外 API](#82-对外-api)
    - [8.3 内部算法](#83-内部算法)
  - [9. 大复数：bigcomplex_float_ty 与 bigcomplex_decimal_ty](#9-大复数bigcomplex_float_ty-与-bigcomplex_decimal_ty)
    - [9.1 数据结构](#91-数据结构)
    - [9.2 对外 API](#92-对外-api)
    - [9.3 特殊值传播](#93-特殊值传播)
  - [10. 类型间转换矩阵](#10-类型间转换矩阵)
  - [11. 错误处理与边界语义](#11-错误处理与边界语义)
  - [12. 测试与验证策略](#12-测试与验证策略)
  - [13. 性能目标与后续方向](#13-性能目标与后续方向)
  - [附录 A：缩写与后缀登记增补建议](#附录-a缩写与后缀登记增补建议)
  - [修订记录](#修订记录)

---

## 1. 引言

### 1.1 项目定位

Numextend（库前缀 `nex`）是一个纯 C99 编写的任意精度数值库，不依赖任何第三方
组件（仅使用 C 标准库）。项目将在未完成状态下以 MIT 许可证开源至 GitHub，因此
本文档同时承担两项职责：

- 对内：作为实现阶段的契约，所有模块的数据结构、API 签名与语义以本文档为准；
- 对外：作为开源读者理解库设计的第一手资料。

### 1.2 类型总览

本库实现以下七种类型：

| 类型 | 模块前缀 | 语义 | 标志 |
|------|----------|------|------|
| `bigint_bin_ty` | `bigint_bin_` | 二进制肢（基 2^32）任意精度整数 | 正 / 负 / 零（3 个） |
| `bigint_dec_ty` | `bigint_dec_` | 十进制肢（基 10^9）任意精度整数 | 正 / 负 / 零（3 个） |
| `bigfrac_ty` | `bigfrac_` | 任意精度有理数（既约分数） | 符号由分子承担 |
| `bigfloat_ty` | `bigfloat_` | 类 IEEE 754 二进制浮点，尾数与指数长度可调 | 7 个（见 §7.1） |
| `bigdecimal_ty` | `bigdecimal_` | 类 IEEE 754 十进制浮点，尾数与指数长度可调 | 7 个（见 §8.1） |
| `bigcomplex_float_ty` | `bigcomplex_float_` | 实部、虚部均为 `bigfloat_ty` 的复数 | 由分量决定 |
| `bigcomplex_decimal_ty` | `bigcomplex_decimal_` | 实部、虚部均为 `bigdecimal_ty` 的复数 | 由分量决定 |

浮点七标志：正（正常）、负（正常）、正无穷、负无穷、NaN、+0、−0。

### 1.3 目标与非目标

目标：

- 纯 C99（`-std=c99`），通过 `-Wall -Wextra -Wpedantic` 无警告编译；
- 无外部依赖，仅使用 C 标准库；
- 正确性优先，其次是清晰可读的算法实现，再次是性能；
- 全部命名、格式遵循 `coding_standard.md` v1.4。

非目标（v1 明确不做）：

- 汇编级优化、SIMD、平台特化代码；
- GPU  offload 与内部多线程并行；
- 超越函数（sin/cos/log/exp 等）的完整实现（仅保留 sqrt；其余列为后续方向）；
- 次正规（subnormal）浮点数（见 §7.3 的取舍说明）。

---

## 2. 总体架构

### 2.1 模块分层与依赖

```text
第 2 层   bigcomplex_float_ty        bigcomplex_decimal_ty
              │                            │
第 1 层   bigfloat_ty ──────┐        bigdecimal_ty
              │             │              │
              │        bigfrac_ty          │
              │             │              │
第 0 层   bigint_bin_ty ────┘        bigint_dec_ty
           （基 2^32，运算快）        （基 10^9，十进制 I/O 快）
```

依赖规则：

- `bigint_bin` 与 `bigint_dec` 互不依赖，是同一抽象（任意精度整数）的两种存储实现；
- `bigfrac`、`bigfloat` 构建于 `bigint_bin` 之上（二进制运算效率最高）；
- `bigdecimal` 构建于 `bigint_dec` 之上（十进制语义与十进制 I/O 天然契合）；
- `bigcomplex_float` / `bigcomplex_decimal` 仅是对应浮点对的组合，不引入新的数值语义；
- 上层模块只能通过下层模块的公开头文件交互，禁止跨层包含 `static` 实现细节；
- `bigint_bin ↔ bigint_dec` 的互转是唯一的跨支线依赖，由专门的转换单元承担
  （见 §4.2.5），该单元同时包含两个模块的头文件，位于 `nex/bigint/` 层级。
- bin / dec 的乘法分派可共享只读公共算法模块 `nex/ntt/`（NTT 核心，仅操作
  系数数组、不依赖 bigint 类型，见 §4.3）；该共享不构成 bin ↔ dec 互依赖。

### 2.2 目录结构与文件命名

目录骨架（已存在，深度 ≤ 4，符合规范 §2.1）：

```text
nex/
├── bigint/
│   ├── nex_bigint_common.h          # 共享：符号枚举、错误码枚举
│   ├── nex_bigint_conv.h / .c       # bin ↔ dec 互转
│   ├── bin/
│   │   └── nex_bigint_bin.h / .c    # 可拆分为多个 .c（见下）
│   └── dec/
│       └── nex_bigint_dec.h / .c
├── fft/
│   └── nex_fft.h / .c               # 浮点复数 FFT 核心（bin 乘法共用，§4.3）
├── ntt/
│   └── nex_ntt.h / .c               # NTT 核心（bin / dec 乘法共用，§4.3）
├── bigfrac/
│   └── nex_bigfrac.h / .c
├── bigfloat/
│   └── nex_bigfloat.h / .c
├── bigdecimal/
│   └── nex_bigdecimal.h / .c
└── bigcomplex/
    ├── float/
    │   └── nex_bigcomplex_float.h / .c
    └── decimal/
        └── nex_bigcomplex_decimal.h / .c
```

约定：

- 头文件命名：`nex_<模块路径>.h`，include guard 为 `NEX_<模块路径>_H`（规范 §11）；
- 全部头文件路径长度 ≤ 52 字符（最长为
  `nex/bigcomplex/decimal/nex_bigcomplex_decimal.h`，47 字符）；
- 单个 `.c` 文件建议不超过 2000 行（规范 §2.4）。当实现超出时按职责拆分，
  例如 `bigint/bin/` 下可拆为 `nex_bigint_bin.c`（生命周期与加减）、
  `nex_bigint_bin_mul.c`（乘法）、`nex_bigint_bin_div.c`（除法）等，
  拆分文件名同样登记于规范附录 A；
- 每个头文件自包含（规范 §11.3），include 顺序遵循规范 §11.4。

### 2.3 模块交互规则

- 上层对象**包含**下层对象（值语义组合），不持有指向下层对象的指针；
- 上层函数返回上层错误码枚举；下层错误在边界处映射为上层错误码（见 §3.1）；
- 任何模块不得绕过公开 API 直接读写下层结构体字段（结构体虽在头文件中可见，
  字段直读仅限本模块内部与本文档明确允许的场合）。

---

## 3. 通用约定

### 3.1 错误码

每个模块定义独立的错误码枚举，成员以 `模块_..._E` 命名（规范 §10.3）。
`bigint` 层的错误码由 bin / dec 共用，定义于 `nex/bigint/nex_bigint_common.h`：

```c99
typedef enum {
    BIGINT_OK_E = 0,          // 成功
    BIGINT_ERR_OOM_E,         // 内存分配失败
    BIGINT_ERR_INVALID_E,     // 非法参数（空指针、零长度、非法进制等）
    BIGINT_ERR_DIV_ZERO_E,    // 除数为零
    BIGINT_ERR_OVERFLOW_E,    // 结果超出目标类型表示范围（如转 uint64_t 溢出）
    BIGINT_ERR_PARSE_E,       // 字符串解析失败
    BIGINT_ERR_UNSUPPORTED_E  // 功能已预留但当前版本未实现（如未落地的乘法方法）
} bigint_err_ty;
```

上层模块镜像同样的分类并加自身前缀，例如：

```c99
typedef enum {
    BIGFLOAT_OK_E = 0,
    BIGFLOAT_ERR_OOM_E,
    BIGFLOAT_ERR_INVALID_E,
    BIGFLOAT_ERR_DIV_ZERO_E,   // 整数语义除零；浮点语义除零产生 ±∞，见 §11
    BIGFLOAT_ERR_PARSE_E
} bigfloat_err_ty;
```

映射规则：上层调用下层失败时，在同名分类间一一映射（`BIGINT_ERR_OOM_E` →
`BIGFLOAT_ERR_OOM_E`）；无同名分类时映射为 `..._ERR_INVALID_E` 并在注释中说明。
`..._ERR_UNSUPPORTED_E` 同样逐层同名映射（上层模块需自行定义同名成员）。
浮点运算中的"除以零"不属于错误，按 §11 的边界语义产生 ±∞ 或 NaN。

所有返回错误码的函数，调用方必须检查返回值（规范 §8.6）。

### 3.2 内存管理策略

- 所有动态存储采用 `len` / `cap` 模型：`len` 为当前有效肢数，`cap` 为已分配容量；
- 扩容策略为倍增（`cap` 不足时扩至 `max(2 * cap, needed)`），收缩不自动进行，
  提供显式 `shrink_to_fit` 类接口；
- `malloc` / `calloc` / `realloc` 返回值必须判空（规范 §7.2）；
- `free` 后立即置 `NULL`，`free` 一个已 `free` 或未初始化的对象不是合法操作，
  但 `free(NULL)` 风格的"对零初始化对象调用 free"必须安全（规范 §7.3 的延伸：
  建议调用方以 `{0}` 或对应 `init` 函数初始化）；
- v1 只使用标准分配器；自定义分配器钩子（每对象持有 alloc/free 函数指针）列为
  后续方向（§13），不在 v1 接口中预留参数，以避免签名污染；
- 函数内多分配点的错误路径使用规范 §8.7 允许的单出口 `goto cleanup;` 清理。

### 3.3 线程安全

- 库对象不含可变共享状态，不使用全局可变变量；
- 不同线程操作**不同**对象是安全的；同一线程对同一对象的写操作与任何其他操作
  必须外部同步（规范 §13）；
- 库内部不加锁，因此规范 §13.2 的锁序登记表为空。

### 3.4 规范化不变式总表

每个类型都有唯一的规范化表示（canonical form），所有公开 API 的输出均保证规范化，
比较函数可以依赖不变式直接逐字段比较：

| 类型 | 不变式 |
|------|--------|
| `bigint_bin_ty` | 最高有效肢非零；零 ⇔ `len == 0` 且 `sign == BIGINT_SIGN_ZERO_E` |
| `bigint_dec_ty` | 同上（最高有效肢非零，取值范围 1..10^9−1） |
| `bigfrac_ty` | `den > 0`；`gcd(\|num\|, den) == 1`；零表示为 `0/1` |
| `bigfloat_ty` | 正常值：尾数最高位恒为 1（显式隐藏位）；±0 / ±∞ / NaN 时尾数与指数字段无意义 |
| `bigdecimal_ty` | 正常值：尾数最高有效肢非零且个位非 0（无尾随十进制零） |
| 复数类型 | 无额外不变式，规范性交由分量保证 |

---

## 4. bigint_bin_ty：二进制肢大整数

### 4.1 数据结构

定义于 `nex/bigint/bin/nex_bigint_bin.h`。符号枚举与错误码为 bin / dec 共用，
定义于 `nex/bigint/nex_bigint_common.h`：

```c99
/* nex_bigint_common.h */
typedef enum {
    BIGINT_SIGN_ZERO_E = 0,  // 零
    BIGINT_SIGN_POS_E,       // 正
    BIGINT_SIGN_NEG_E        // 负
} bigint_sign_ty;
```

```c99
/* nex_bigint_bin.h */
typedef struct {
    bigint_sign_ty sign;  // 三标志：正 / 负 / 零
    uint32_t *limbs;      // 肢数组，小端序，基 2^32
    size_t len;           // 当前有效肢数
    size_t cap;           // 已分配容量（肢数）
} bigint_bin_ty;
```

语义与不变式：

- 数值 = sign × Σ(limbs[i] × 2^(32·i))；
- 规范化：最高有效肢 `limbs[len-1] != 0`；零的唯一表示为 `len == 0` 且
  `sign == BIGINT_SIGN_ZERO_E`（此时 `limbs` 可为 NULL 或保留容量）；
- 选择 32 位肢而非 64 位肢：肢乘积累加放入 `uint64_t` 即可，无需 `__int128`
  等非 C99 扩展（规范 §1.1）。64 位肢列为后续性能方向（§13）。

### 4.2 对外 API

函数名遵循 `模块_动作_对象`（规范 §4.4）。按职责分类列出代表签名，
完整清单以实现头文件为准。

#### 4.2.1 生命周期

```c99
bigint_err_ty bigint_bin_init(bigint_bin_ty *val);                 // 初始化为零
bigint_err_ty bigint_bin_init_cap(bigint_bin_ty *val, size_t cap); // 预分配容量
void          bigint_bin_free(bigint_bin_ty *val);                 // 释放，容忍零初始化对象
bigint_err_ty bigint_bin_copy(bigint_bin_ty *dst, const bigint_bin_ty *src);
void          bigint_bin_move(bigint_bin_ty *dst, bigint_bin_ty *src); // 接管资源
bigint_err_ty bigint_bin_shrink(bigint_bin_ty *val);             // 收缩至 len
```

#### 4.2.2 与基本类型互转

```c99
bigint_err_ty bigint_bin_from_u64(bigint_bin_ty *val, uint64_t value);
bigint_err_ty bigint_bin_from_i64(bigint_bin_ty *val, int64_t value);
bigint_err_ty bigint_bin_to_u64(const bigint_bin_ty *val, uint64_t *out); // 溢出返回
                                                        // BIGINT_ERR_OVERFLOW_E
bigint_err_ty bigint_bin_to_i64(const bigint_bin_ty *val, int64_t *out);

/*
 * brief: 从字符串解析，支持进制 2..36，可选前导 '-' 与 "0x"/"0b" 前缀
 * param: end 若非 NULL，返回首个未消费字符位置（部分消费容错，见 §11）
 */
bigint_err_ty bigint_bin_from_str(bigint_bin_ty *val, const char *str,
        uint32_t base, const char **end);

/*
 * brief: 转为字符串。buf 不足时返回 BIGINT_ERR_OVERFLOW_E 并经 needed 传出所需长度
 */
bigint_err_ty bigint_bin_to_str(const bigint_bin_ty *val, uint32_t base,
        char *buf, size_t buf_len, size_t *needed);
```

#### 4.2.3 比较与断言

```c99
int           bigint_bin_cmp(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);
int           bigint_bin_cmp_abs(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);
bigint_sign_ty bigint_bin_sign(const bigint_bin_ty *val);
bool          bigint_bin_is_zero(const bigint_bin_ty *val);
```

`cmp` 返回值约定：lhs<rhs 为负，lhs==rhs 为 0，lhs>rhs 为正。

#### 4.2.4 四则与幂

所有算术函数允许 `dst` 与任一源操作数别名（aliasing），实现须先处理别名情形。

```c99
bigint_err_ty bigint_bin_add(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);
bigint_err_ty bigint_bin_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);
bigint_err_ty bigint_bin_mul(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 带余除法，quot / rem 任一可为 NULL（表示不接收该结果）
 * return: 除数为零返回 BIGINT_ERR_DIV_ZERO_E
 * note: 余数符号与被除数一致（截断除法，与 C99 整数除法语义相同）
 */
bigint_err_ty bigint_bin_div_rem(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

bigint_err_ty bigint_bin_neg(bigint_bin_ty *val);          // 就地取负
bigint_err_ty bigint_bin_abs(bigint_bin_ty *val);          // 就地取绝对值
bigint_err_ty bigint_bin_pow(bigint_bin_ty *dst, const bigint_bin_ty *base,
        uint64_t exp);
bigint_err_ty bigint_bin_pow_mod(bigint_bin_ty *dst, const bigint_bin_ty *base,
        const bigint_bin_ty *exp, const bigint_bin_ty *mod);

/*
 * brief: 最大公因数 dst = gcd(|lhs|, |rhs|)，结果恒非负
 * return: 内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 约定 gcd(0, 0) = 0；dst 允许与 lhs / rhs 别名
 */
bigint_err_ty bigint_bin_gcd(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);
```

**乘法方法选择**：乘法另提供带方法选择的扩展入口 `bigint_bin_mul_ex`，供差分测试
（§12）、阈值标定与后续算法扩展使用。相关类型定义于 `nex_bigint_common.h`
（bin / dec 共用算法族）：

```c99
/* nex_bigint_common.h */
typedef enum {
    BIGINT_MUL_AUTO_E = 0,                    // 按肢数阈值自动分派（默认）
    /* 1xxx：亚二次分治类 */
    BIGINT_MUL_SCHOOLBOOK_E = 1001,
    BIGINT_MUL_KARATSUBA_E = 1002,
    BIGINT_MUL_TOOM_COOK_E = 1003,            // v1 未实现 → UNSUPPORTED
    /* 2xxx：变换类 */
    BIGINT_MUL_FLOAT_COMPLEX_FFT_E = 2001,    // v1 未实现 → UNSUPPORTED
    BIGINT_MUL_MULTI_MODULI_CRT_NTT_E = 2002, // 多模数 CRT NTT（v1 已实现，双模数）
    /* 3xxx */
    BIGINT_MUL_SCHONHAGE_STRASSEN_E = 3001    // v1 未实现 → UNSUPPORTED
} bigint_mul_algo_ty;

typedef union {
    struct { uint32_t reserved; } schoolbook;             // 无参数（C99 无空结构体）
    struct { size_t cutoff; } karatsuba;                  // 切换阈值（肢数），0 = 库默认
    struct { uint32_t k; size_t cutoff; } toom_cook;      // Toom-k 的 k 与阈值
    struct { uint32_t chunk_bits; } float_complex_fft;    // 分节位数，0 = 默认 8
    struct { uint32_t mod_count; } multi_moduli_crt_ntt;  // 模数个数，0 = 库默认
    struct { uint32_t reserved; } schonhage_strassen;
} bigint_mul_params_ty;

typedef struct {
    bigint_mul_algo_ty algo;     // 算法标签
    bigint_mul_params_ty params; // 与 algo 对应的参数；不适用时忽略
} bigint_mul_method_ty;
```

```c99
/* nex_bigint_bin.h */
bigint_err_ty bigint_bin_mul(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);   // 等价于 method = { BIGINT_MUL_AUTO_E, {0} }

/*
 * brief: 带方法选择的大整数乘法
 * return: algo 非法或 params 与 algo 不匹配 → BIGINT_ERR_INVALID_E；
 *         algo 已登记但当前版本未实现 → BIGINT_ERR_UNSUPPORTED_E
 * note: method 为 NULL 等价于 AUTO；dst 允许与源操作数别名
 */
bigint_err_ty bigint_bin_mul_ex(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, const bigint_mul_method_ty *method);
```

枚举编号分段规则：0 为 AUTO，1xxx 为亚二次分治类，2xxx 为变换类，3xxx 为
Schönhage-Strassen 类；后续新增方法按族续号，不复用已占用编号。

#### 4.2.5 位运算与杂项（bin 专有）

```c99
bigint_err_ty bigint_bin_shl(bigint_bin_ty *dst, const bigint_bin_ty *src, size_t bits);
bigint_err_ty bigint_bin_shr(bigint_bin_ty *dst, const bigint_bin_ty *src, size_t bits);
bigint_err_ty bigint_bin_bit_and(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);
bigint_err_ty bigint_bin_bit_or(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);
bigint_err_ty bigint_bin_bit_xor(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);
bool          bigint_bin_bit_test(const bigint_bin_ty *val, size_t bit);
bigint_err_ty bigint_bin_bit_set(bigint_bin_ty *val, size_t bit, bool value);
size_t        bigint_bin_bit_len(const bigint_bin_ty *val);   // 最高有效位位置+1
size_t        bigint_bin_popcount(const bigint_bin_ty *val);  // 幅值中 1 的个数
```

负数位运算语义：按**二进制补码的无限符号扩展**解释（与 Python 一致），
例如 `-1 & x == x`。该约定写入头文件注释，避免实现歧义。

#### 4.2.6 与 bigint_dec 互转

定义于 `nex/bigint/nex_bigint_conv.h`（唯一同时包含两个模块头文件的单元）：

```c99
bigint_err_ty bigint_conv_bin_to_dec(bigint_dec_ty *dst, const bigint_bin_ty *src);
bigint_err_ty bigint_conv_dec_to_bin(bigint_bin_ty *dst, const bigint_dec_ty *src);
```

### 4.3 内部算法

| 操作 | v1 算法 | 复杂度 | 后续方向 |
|------|---------|--------|----------|
| 加 / 减 | 逐肢带进位扫描 | O(n) | — |
| 乘法 | schoolbook；肢数 ≥ 32 切换 Karatsuba；≥ 16384 切换多模数 CRT NTT（阈值实测标定，见 §4.3）；`mul_ex` 可强制选择各算法（FFT 已实现但不参与 AUTO） | O(n²) / O(n^1.585) / O(n log n) | Toom-3；FFT 的 SIMD 化与 AUTO 启用、分治基数转换（见 §13） |
| 除法 | Knuth《TAOCP》卷 2 Algorithm D（规范化 + 试商修正） | O(n·m) | 递归除法 |
| 平方 | 专用 schoolbook 平方（利用对称性减半乘累加） | O(n²/2) | 随乘法升级 |
| 模幂 | 平方-乘，滑动窗口列为后续 | O(log e 次模乘） | Montgomery 约减 |
| bin→dec | 分治（2 的幂切半 + 平方链表），小规模回退反复除 10^9 | O(n^1.585) | decimal 快速乘法（转 bin → NTT → 转回） |
| dec→bin | 分治（对称），小规模回退逐肢乘 10^9 | O(n^1.585) | decimal 快速乘法（转 bin → NTT → 转回） |
| gcd（供 bigfrac 使用，公开 API `bigint_bin_gcd`） | 二进制 GCD | O(n²) | Lehmer / 半 GCD |

实现要点：

- 乘法、除法拆分为独立 `.c` 文件（见 §2.2），Karatsuba 的递归在临时缓冲区上
  进行，缓冲区一次分配、全程复用，避免递归中的反复 `malloc`；
- 所有内部辅助函数为 `static`（规范 §8.4）；
- 进位/借位一律通过 `uint64_t` 中间量显式计算，禁止依赖编译器溢出行为。

变换类乘法（浮点复数 FFT 与多模数 CRT NTT）的分节约定（对应算法落地时生效）。

记号：每肢（bin 为 2^32、dec 为 10^9）拆为若干**节**（chunk），节基为 B；
单侧总节数 L = 每肢节数 × 肢数 n，卷积长度 2L−1，FFT/NTT 长度 N 取下一位
2 的幂（≥ 2L）。卷积系数上界 C = L·(B−1)²（每个输出位置至多 L 个乘积项）。

**浮点复数 FFT**（`BIGINT_MUL_FLOAT_COMPLEX_FFT_E`）：

- 二进制（基 2^32）：默认每肢 4 节 × 8 bit（B = 256，可经
  `params.float_complex_fft.chunk_bits` 覆盖）。拆节即 limb 的字节视图，
  零成本；C ≈ 4n·(2^8−1)²，double 的 53 位尾数下安全余量极大（N 可达
  ~2^34 点）。增大 chunk_bits 可缩短变换长度（16 bit 时 N = 4n），但安全
  规模上限随之下降（16 bit 时约 N < 2^18），默认取 8 bit 以覆盖全部规模，
  完整舍入安全界的论证随实现写入注释；
- 十进制（基 10^9）：默认每肢 3 节 × 基 1000（B = 10³，C ≈ 3n·(10³−1)²，
  N = 8n）。**不采用** 每肢 4 节 × 基 256 方案（B = 256，C ≈ 4n·255²，
  N 同为 8n——两方案变换长度相同，因 6n−1 与 8n−1 的下一位 2 的幂均为 8n）。
  弃用理由：① 基 256 方案卷积结果须经一次 bin→dec 基数转换才能还原为
  十进制肢（10^9 不是 2 的幂，十进制肢与字节不对齐；v1 的 O(n²) 朴素转换
  会抵消 FFT 的收益），而 base-1000 每 3 节恰好拼一个十进制肢，零转换；
  ② 基 256 方案的最高节受限于 0..59（59 < 10^9/256³ < 60），为非均匀节，
  增加正确性脚枪；③ 基 256 的系数上界虽小约 3.5 bit，但 base-1000 的精度
  余量（N 可达 ~2^30 点）对任何现实规模均绰绰有余。字节对齐拆节零成本的
  优势仅对二进制成立（limb 即字节数组），十进制拆/装为 O(n) 的 div/mod，
  相对 O(n log n) 变换可忽略；故二/十进制不共享分节规则。
- **落地状态（v0.10）**：二进制 FFT 已实现（`nex/fft/`，强制方法
  `BIGINT_MUL_FLOAT_COMPLEX_FFT_E`，节位宽 0/8/16，0 = 自动：长度和
  ≤ 2^10 用 16 bit、否则 8 bit）。实测（标量 double，-O2）：16-bit 节
  仅在 ~512² 肢窄带胜过 Karatsuba（~10%），8-bit 节（N = 8n）几乎处处
  落败，故 **AUTO 不采用 FFT**，保留为强制方法与 SIMD 后续方向（§13）。
  十进制 FFT 未实现（还原需分治基数转换，见 §13），本节十进制约定
  保留待其落地。

**多模数 CRT NTT**（`BIGINT_MUL_MULTI_MODULI_CRT_NTT_E`）：

- 模运算无舍入误差，**不套用** 8-bit 规则：节位宽 b 由模数容量反推。
  设所选模数之积 Πp ≈ 2^W，须满足 C = L·(2^b)² < Πp（系数小于模积时 CRT
  重构唯一），且变换长度 N ≤ 2^c（c 见模数结构体）；
- 默认分节：二进制每肢 2 节 × 16 bit（L = 2n、N = 4n，两个 30-bit 模数
  覆盖 n ≤ 2^27）；十进制每肢 2 节 × 15 bit（2·2^15 > 10^9，L = 2n、
  N = 4n，两个 30-bit 模数覆盖 n ≤ 2^29）。固定 8 bit 分节（L = 4n、
  N = 8n）会使变换长度无谓放大 2 倍，故不作 FFT/NTT 共同规则；
- `params.multi_moduli_crt_ntt.mod_count` 控制模数个数（0 或 2；其余
  UNSUPPORTED）。**三模数不实现**：双模数容量已覆盖 n ≤ 2^27 肢
  （512 Mbit 操作数），远超现实输入；三模数需 128 位中间量（C99 无
  __int128，结构体模拟可行但无使用场景）且每操作慢 1.5 倍。128 位
  乘法留待 §13"64 位肢变体"一并实现；
- **实现要点（Phase 2 落地）**：蝶形乘法走 Montgomery 模乘（R = 2^32，
  模数常数 m' = −p^{-1} mod 2^32 每次变换现算，Newton 5 轮），蝶形全程
  无除法；Garner 重构的常数 p1^{-1} mod p2 每次乘法预计算一次、全部
  系数共享；AUTO 分派阈值实测标定为 16384 肢（Karatsuba 交叉点约
  13K 肢，-O2 双模数），见 §4.3 算法表；
- 模数一律取形如 p = k·2^c + 1 的素数（k 奇），定义内部结构体 `ntt_mod_ty`
  （不属公开 API）：

```c99
typedef struct {
    uint32_t p;  /* 素数，p = k·2^c + 1 */
    uint32_t k;  /* 奇部，k = (p−1) / 2^c */
    uint32_t c;  /* p−1 中 2 的最大幂指数（2^c || p−1，= 最大变换长度对数） */
    uint32_t w;  /* 2^c 次本原单位根：w = g^k mod p，满足 w^(2^(c−1)) ≡ −1 (mod p) */
} ntt_mod_ty;
```

  其中 g 为模 p 的原根；长度 N 的根 = w^(2^c / N)（N 为 2 的幂且 N ≤ 2^c）。
  内置模数表为 `const` 数组（下表全部经单元测试逐项校验），c 覆盖 21..27；
  选取模数组合须满足：每个模数 2^c ≥ N，且 Πp > C：

| p | k | c | g（原根） |
|---|---|---|-----------|
| 167772161 | 5 | 25 | 3 |
| 469762049 | 7 | 26 | 3 |
| 754974721 | 45 | 24 | 11 |
| 985661441 | 235 | 22 | 3 |
| 998244353 | 119 | 23 | 3 |
| 1004535809 | 479 | 21 | 3 |
| 1107296257 | 33 | 25 | 10 |
| 1224736769 | 73 | 24 | 3 |
| 1811939329 | 27 | 26 | 13 |
| 2013265921 | 15 | 27 | 31 |
| 2281701377 | 17 | 27 | 3 |

- **模数有效性校验**：合数模下 Z/mZ 单位群非循环，不存在 2^c 阶根，
  radix-2 NTT 无正确性保证，故**合数一律不可用**。内置表在单元测试中
  验证；若将来支持用户自供模数，对外入口对每个模数做运行时校验：
  ① 质数性——Miller-Rabin，基数 {2,3,5,7,11} 对全部 uint32 为确定性
  （最小强伪素数 2,152,302,898,747 > 2^32）；② 根阶——w^(2^(c−1)) ≡ −1
  (mod p)。校验为纯函数、无缓存、不引入全局状态（§3.3），单次成本约
  数十次模幂，相对 O(n log n) 变换可忽略；合数返回 `BIGINT_ERR_INVALID_E`。

**位反转与蝶形**：正变换用 DIF（频域抽取，Gentleman–Sande）蝶形、逆变换
用 DIT（时域抽取，Cooley–Tukey）蝶形，正逆配对后输入输出均自然序，
**位反转被完全消除**（无需置乱遍历与旋转因子表翻转）。若实现期为便于
调试需要显式位反转，提供 `ntt_bitrev(uint32_t *rev, uint32_t log2n)`：
由**调用方提供缓冲区**并就地填充反转置换表（显式内存、无全局状态、
线程安全）；仅允许 `const` 的 256 字节字节反转查找表（编译期常量，
不违反 §3.3），**禁止**按 N 缓存的全局表。

**模块归属**：NTT 仅操作系数数组（`uint32_t *` + 长度），不依赖 bin/dec
类型，作为公共算法模块置于 `nex/ntt/`（`nex_ntt.h` / `nex_ntt.c`，深度 2），
由 bin / dec 的乘法分派共同调用；§2.1 的"跨支线依赖唯一性"随之放宽为
"bin ↔ dec 互不依赖，可共享只读公共算法模块"（已随 v0.8 落地）。

---

## 5. bigint_dec_ty：十进制肢大整数

### 5.1 数据结构

定义于 `nex/bigint/dec/nex_bigint_dec.h`，与 bin 同构：

```c99
typedef struct {
    bigint_sign_ty sign;  // 三标志：正 / 负 / 零（与 bin 共用枚举）
    uint32_t *limbs;      // 肢数组，小端序，基 10^9（每肢取值 0..999999999）
    size_t len;
    size_t cap;
} bigint_dec_ty;
```

设计动机：

- 十进制字符串 I/O 为 O(n)（每肢恰好对应 9 位十进制数字，仅首肢需去前导零），
  适合十进制输入输出密集的场景；
- 直接作为 `bigdecimal_ty` 的尾数存储（§8）；
- 与 bin 实现互为参照，可用互转做交叉验证（§12）。

### 5.2 对外 API

API 与 `bigint_bin` 对齐（`bigint_dec_` 前缀），差异如下：

- **不提供**位运算一族（`bit_and` 等）与 `bit_len`/`popcount`——十进制基下
  位语义不自然，调用方应先转 bin；
- 乘法同样提供方法选择扩展入口 `bigint_dec_mul_ex`，签名与 `bigint_bin_mul_ex`
  同型，复用 `bigint_mul_method_ty`（见 §4.2.4）；
- 新增十进制特有的移位与度量：

```c99
bigint_err_ty bigint_dec_mul_pow10(bigint_dec_ty *val, size_t digits); // 乘以 10^digits
bigint_err_ty bigint_dec_div_pow10(bigint_dec_ty *val, size_t digits); // 截断除以 10^digits
size_t        bigint_dec_digit_len(const bigint_dec_ty *val);          // 十进制位数
```

`mul_pow10` / `div_pow10` 是 `bigdecimal` 指数调整的核心原语：
`digits` 先按 9 取整做整肢移动，余数（0..8）走单肢乘除。

### 5.3 内部算法

算法族与 bin 相同（schoolbook + Karatsuba、Knuth D），仅基不同。乘法方法类型与
分派策略和 bin 一致（§4.2.4）：v1 实现 schoolbook 与 Karatsuba，其余已预留方法
同样返回 `BIGINT_ERR_UNSUPPORTED_E`。基 10^9 的正确性论证（写入实现注释）：

- 单肢乘法：`(10^9 − 1)² = 10^18 − 2·10^9 + 1 < 2^60`，与既有进位
  （< 10^9）及累加项相加后仍远小于 `2^64`，`uint64_t` 累加安全；
- 试商：Knuth Algorithm D 要求基 b 满足"两肢组合 / 一肢"的商估计，
  以 `uint64_t` 保存两肢（高位 × 10^9 + 低位 ≤ 10^18 − 1 < 2^60）同样安全；
- 进制转换：bin↔dec 见 §4.3；十进制字符串 I/O 为逐肢格式化，无需转换。

---

## 6. bigfrac_ty：大有理数

### 6.1 数据结构

定义于 `nex/bigfrac/nex_bigfrac.h`：

```c99
typedef struct {
    bigint_bin_ty num;  // 分子，符号由 num 承担
    bigint_bin_ty den;  // 分母，恒为正（sign == BIGINT_SIGN_POS_E）
} bigfrac_ty;
```

不变式（所有公开 API 的输出均满足）：

- `den > 0`；
- `gcd(|num|, den) == 1`（既约）；
- 零的唯一表示为 `0/1`。

选择 `bigint_bin` 而非 `bigint_dec` 作为分量：有理数运算以乘除与 gcd 为主，
二进制基效率更高；十进制输入在解析边界一次性转换为 bin。

### 6.2 对外 API

```c99
/* 生命周期 */
bigfrac_err_ty bigfrac_init(bigfrac_ty *frac);
void           bigfrac_free(bigfrac_ty *frac);
bigfrac_err_ty bigfrac_copy(bigfrac_ty *dst, const bigfrac_ty *src);

/* 构造 */
bigfrac_err_ty bigfrac_from_ints(bigfrac_ty *frac, const bigint_bin_ty *num,
        const bigint_bin_ty *den);   // den 为零返回 BIGFRAC_ERR_DIV_ZERO_E；自动约分
bigfrac_err_ty bigfrac_from_str(bigfrac_ty *frac, const char *str, const char **end);
        // 接受 "p/q"、"p"、十进制小数（如 "-12.340"，精确化简）

/* 转换（有损，需目标类型精度上下文，见 §7.3 / §8.1） */
bigfrac_err_ty bigfrac_to_bigfloat(bigfloat_ty *dst, const bigfrac_ty *src,
        const bigfloat_ctx_ty *ctx);
bigfrac_err_ty bigfrac_to_bigdecimal(bigdecimal_ty *dst, const bigfrac_ty *src,
        const bigdecimal_ctx_ty *ctx);

/* 算术（结果自动约分） */
bigfrac_err_ty bigfrac_add(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
bigfrac_err_ty bigfrac_sub(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
bigfrac_err_ty bigfrac_mul(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
bigfrac_err_ty bigfrac_div(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
bigfrac_err_ty bigfrac_neg(bigfrac_ty *frac);
bigfrac_err_ty bigfrac_inv(bigfrac_ty *frac);  // 倒数；零返回 BIGFRAC_ERR_DIV_ZERO_E

/* 比较与访问 */
int            bigfrac_cmp(const bigfrac_ty *lhs, const bigfrac_ty *rhs);
const bigint_bin_ty *bigfrac_num(const bigfrac_ty *frac);
const bigint_bin_ty *bigfrac_den(const bigfrac_ty *frac);
```

### 6.3 内部算法

- **自动约分**：每次算术输出前执行 gcd 约分，保证不变式；gcd 使用二进制 GCD
  （仅需移位与减法，避免昂贵的十进制基除法），Lehmer 加速列为后续；
- **交叉约分**：乘法 `a/b × c/d` 先约 `gcd(a, d)` 与 `gcd(c, b)` 再相乘，
  除法同理，显著抑制中间结果的肢数膨胀；
- **加减法**：`a/b ± c/d = (a·d ± c·b) / (b·d)`，先约 `g = gcd(b, d)`，
  以 `b/g`、`d/g` 缩小乘积规模；
- **比较**：既约前提下 `a/b ≷ c/d` 等价于 `a·d ≷ c·b`，两数既约且同号时可先
  按 `bit_len` 差估算以避免大数相乘（列为实现期优化，不影响语义）。

---

## 7. bigfloat_ty：可调精度二进制浮点数

### 7.1 语义模型

`bigfloat_ty` 模仿 IEEE 754 二进制浮点的结构，但**尾数长度与指数长度不再固化**
（IEEE 754 中 double 为 52+11 位），而是由每次运算传入的精度上下文（§7.3）
动态指定：

- 正常值：value = (−1)^sign × mant × 2^exp，其中 `mant` 为正整数（最高位恒 1，
  即把 IEEE 754 的隐藏位显式化），`exp` 为二进制指数；
- 特殊值：+∞、−∞、NaN、+0、−0 由标志枚举直接表达，共七个标志：

```c99
typedef enum {
    BIGFLOAT_POS_ZERO_E = 0,  // +0
    BIGFLOAT_NEG_ZERO_E,      // −0
    BIGFLOAT_POS_E,           // 正（正常值）
    BIGFLOAT_NEG_E,           // 负（正常值）
    BIGFLOAT_POS_INF_E,       // 正无穷
    BIGFLOAT_NEG_INF_E,       // 负无穷
    BIGFLOAT_NAN_E            // 非数
} bigfloat_flag_ty;
```

±0 不设为"正常值 + 空尾数"，而是独立标志：这使 classify 成为纯标志判断，
且与 IEEE 754 中 +0 ≠ −0（尽管 +0 == −0 成立）的语义对齐（见 §11）。

### 7.2 数据结构

定义于 `nex/bigfloat/nex_bigfloat.h`：

```c99
typedef struct {
    bigfloat_flag_ty flag;  // 七标志
    bigint_bin_ty mant;     // 尾数幅值；仅 POS/NEG（正常值）时有效，
                            // 规范化：最高位恒为 1，且不含符号（幅值）
    int64_t exp;            // 二进制指数；仅正常值时有效
} bigfloat_ty;
```

说明：

- `mant` 的符号字段恒为 `BIGINT_SIGN_POS_E`（正常值）或 `BIGINT_SIGN_ZERO_E`
  （特殊值），符号完全由 `flag` 表达，避免双重符号源；
- ±0 / ±∞ / NaN 时 `mant` 与 `exp` 无意义，实现置零并忽略，读写均不得依赖之；
- 正常值表示唯一：给定 `mant` 最高位为 1 的规范化要求，数值到 `(mant, exp)`
  的映射是双射。

### 7.3 精度上下文

每次产生新值的运算都接收一个上下文，决定结果的舍入与范围：

```c99
typedef enum {
    BIGFLOAT_ROUND_NEAREST_EVEN_E = 0,  // 最近舍入，平局取偶（默认，同 IEEE 754）
    BIGFLOAT_ROUND_TOWARD_ZERO_E,       // 向零截断
    BIGFLOAT_ROUND_TOWARD_POS_E,        // 向 +∞
    BIGFLOAT_ROUND_TOWARD_NEG_E,        // 向 −∞
    BIGFLOAT_ROUND_AWAY_ZERO_E          // 远离零
} bigfloat_round_ty;

typedef struct {
    size_t mant_bits;        // 尾数精度（位），对应 IEEE 754 的 p；必须 ≥ 2
    size_t exp_bits;         // 指数字段长度（位），决定可表示范围
    bigfloat_round_ty round; // 舍入模式
} bigfloat_ctx_ty;
```

语义：

- **舍入**：运算先以 `mant_bits + 3` 个保护位计算，再按 `round` 舍入到
  `mant_bits` 位；保护位位数是正确舍入（不含双重舍入误差）的充分条件，
  其证明写入实现注释；
- **范围**：可表示指数区间为 `[emin, emax] = [−2^(exp_bits−1), 2^(exp_bits−1) − 1]`
  （对称二补语义）。结果指数 > `emax` → 按舍入方向产生 ±∞ 或最大有限值
  （最近舍入产生 ±∞）；结果指数 < `emin` → **flush-to-zero**，产生 ±0，
  符号按舍入方向决定（向 −∞ 舍入的负下溢产生 −0）；
- **不做次正规数**：v1 明确取舍。次正规要求尾数非规范化存储，会破坏
  "最高位恒 1" 不变式并使所有运算分支化；flush-to-zero 简单且语义清晰，
  该决定连同影响（`emin` 附近精度突降）记录于此，次正规支持列为 §13 后续方向；
- 提供常用预设构造：`bigfloat_ctx_binary32()`、`bigfloat_ctx_binary64()`、
  `bigfloat_ctx_binary128()`，分别对应 IEEE 754 单/双/四倍精度的**尾数位长 p**
  （24 / 53 / 113）；**指数范围**则统一采用上文对称公式
  `[−2^(exp_bits−1), 2^(exp_bits−1)−1]`（binary64 → [−1024, 1023]，对应值域
  [2^52·2^−1024, 2^53·2^1023) ≈ [2^−972, 2^1076)），与 IEEE 754 的偏置
  指数值域（binary64 正常数 [2^−1074, 2^1024)）**不一致**——低端更高、
  高端更宽，且无次正规。与硬件浮点对照测试须限制在两者值域的重叠区。

### 7.4 对外 API

```c99
/* 生命周期与上下文 */
bigfloat_err_ty bigfloat_init(bigfloat_ty *val);      // 初始化为 +0
void            bigfloat_free(bigfloat_ty *val);
bigfloat_err_ty bigfloat_copy(bigfloat_ty *dst, const bigfloat_ty *src);
bigfloat_ctx_ty bigfloat_ctx_make(size_t mant_bits, size_t exp_bits,
        bigfloat_round_ty round);

/* 分类断言（纯标志判断，不失败） */
bool bigfloat_is_zero(const bigfloat_ty *val);
bool bigfloat_is_inf(const bigfloat_ty *val);
bool bigfloat_is_nan(const bigfloat_ty *val);
bool bigfloat_is_normal(const bigfloat_ty *val);  // POS_E 或 NEG_E

/* 构造与转换 */
bigfloat_err_ty bigfloat_from_bigint(bigfloat_ty *dst, const bigint_bin_ty *src,
        const bigfloat_ctx_ty *ctx);   // 按 ctx 舍入，精确值可表示时无损
bigfloat_err_ty bigfloat_from_f64(bigfloat_ty *dst, double value);  // 总是精确
bigfloat_err_ty bigfloat_to_f64(const bigfloat_ty *src, double *out);
        // 超范围时 out 为 ±HUGE_VAL 并返回 BIGFLOAT_ERR_OVERFLOW_E
bigfloat_err_ty bigfloat_from_str(bigfloat_ty *dst, const char *str,
        const bigfloat_ctx_ty *ctx, const char **end);
        // 接受十进制小数/科学计数法、"inf"、"nan"，按 ctx 正确舍入
bigfloat_err_ty bigfloat_to_str(const bigfloat_ty *src, size_t max_digits,
        char *buf, size_t buf_len, size_t *needed);
        // 输出"最短且能按相同 ctx 往返"的十进制表示；max_digits 限制位数

/* 算术（特殊值传播见 §11；有限结果的舍入与溢出见 §7.3） */
bigfloat_err_ty bigfloat_add(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);
bigfloat_err_ty bigfloat_sub(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);
bigfloat_err_ty bigfloat_mul(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);
bigfloat_err_ty bigfloat_div(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);
bigfloat_err_ty bigfloat_sqrt(bigfloat_ty *dst, const bigfloat_ty *src,
        const bigfloat_ctx_ty *ctx);   // 负数产生 NaN
bigfloat_err_ty bigfloat_neg(bigfloat_ty *val);  // 翻转符号标志，含 ±0、±∞；NaN 不变

/* 比较：+0 == −0；NaN 与任何值（含自身）比较均"不相等"，cmp 遇 NaN 返回约定值 2 */
int bigfloat_cmp(const bigfloat_ty *lhs, const bigfloat_ty *rhs);
bool bigfloat_eq(const bigfloat_ty *lhs, const bigfloat_ty *rhs);

/* 分解与合成（frexp/ldexp 式） */
bigfloat_err_ty bigfloat_decompose(const bigfloat_ty *src, bigint_bin_ty *mant,
        int64_t *exp, bigfloat_flag_ty *flag);
bigfloat_err_ty bigfloat_compose(bigfloat_ty *dst, const bigint_bin_ty *mant,
        int64_t exp, bigfloat_flag_ty flag, const bigfloat_ctx_ty *ctx);
```

### 7.5 内部算法

- **加/减**：按指数差对齐（小指数一方右移），移位时保留粘位（sticky bit：
  被移出位的或），然后整数加减、重新规范化、按 ctx 舍入。指数差超过
  `mant_bits + 3` 时可直接判定结果（小的一方退化为粘位），避免无意义的大移位；
- **乘法**：尾数整数乘（走 `bigint_bin_mul`），规范化并舍入；
- **除法**：将被除数左移至商达到 `mant_bits + 3` 位，整数除法得商与余数，
  余数非零置粘位，然后舍入。Newton 迭代除法列为后续；
- **sqrt**：整数平方根（移位试商法）作用于缩放后的尾数，得到舍入所需的
  保护位与粘位；Newton 迭代列为后续；
- **十进制输入**：十进制小数解析为 `bigfrac` 式精确有理数（分子分母为
  bigint，分母为 10^k），再做一次带保护位的除法并舍入——保证正确舍入的
  充分条件是中间精度 ≥ `mant_bits + 输入位数相关界`，实现取宽松上界并注释依据；
- **十进制输出**：`to_str` 采用"逐步增加有效位数、试转回并比较"的往返策略
  （类似 Grisu/Ryū 的效果，实现更简单）：从 1 位开始递增，直到输出串按同一
  ctx 解析回原值。正确性由往返测试保证，性能列为后续优化。

---

## 8. bigdecimal_ty：可调精度十进制浮点数

### 8.1 语义模型与数据结构

`bigdecimal_ty` 是 `bigfloat_ty` 的十进制对偶，语义对齐 IEEE 754 十进制浮点
（decimal64/128 的推广），尾数与指数长度同样由上下文指定。定义于
`nex/bigdecimal/nex_bigdecimal.h`：

```c99
typedef enum {
    BIGDECIMAL_POS_ZERO_E = 0,
    BIGDECIMAL_NEG_ZERO_E,
    BIGDECIMAL_POS_E,
    BIGDECIMAL_NEG_E,
    BIGDECIMAL_POS_INF_E,
    BIGDECIMAL_NEG_INF_E,
    BIGDECIMAL_NAN_E
} bigdecimal_flag_ty;

typedef enum {
    BIGDECIMAL_ROUND_NEAREST_EVEN_E = 0,
    BIGDECIMAL_ROUND_TOWARD_ZERO_E,
    BIGDECIMAL_ROUND_TOWARD_POS_E,
    BIGDECIMAL_ROUND_TOWARD_NEG_E,
    BIGDECIMAL_ROUND_AWAY_ZERO_E
} bigdecimal_round_ty;

typedef struct {
    size_t mant_digits;          // 十进制有效位数，必须 ≥ 1
    size_t exp_digits;           // 指数字段的十进制位数，决定范围 ±(10^exp_digits − 1)
    bigdecimal_round_ty round;
} bigdecimal_ctx_ty;

typedef struct {
    bigdecimal_flag_ty flag;  // 七标志
    bigint_dec_ty mant;       // 尾数幅值（十进制整数）；仅正常值有效，
                              // 规范化：个位非 0（无尾随十进制零）
    int64_t exp;              // 十进制指数，value = ±mant × 10^exp
} bigdecimal_ty;
```

与 bigfloat 的关键差异：

- 规范化条件是"无尾随十进制零"（而非最高位恒 1），例如 `1.20` 一律存为
  `12 × 10^−1`；这保证表示唯一、比较可依赖字段相等；
- 指数范围 `[−(10^exp_digits − 1), 10^exp_digits − 1]`（对称十进制语义，
  与 exp_bits 的二补语义平行）；
- 十进制字符串 I/O **完全精确**，无需 bigfloat 的往返算法；
- 溢出/下溢处理（→±∞ / flush-to-zero）与 §7.3 相同。

### 8.2 对外 API

与 §7.4 一一对应（`bigdecimal_` 前缀、`bigdecimal_ctx_ty`），差异：

- `bigdecimal_from_str` 为精确解析（仅超长有效位需按 ctx 舍入）；
- `bigdecimal_to_str` 精确输出，支持定点与科学计数两种格式标志参数；
- 不提供 `from_f64`（二进制浮点到十进制的"精确"转换会产生长尾数，语义易误解）；
  需要时经 `bigfloat → string → bigdecimal` 显式转换，路径清晰且有损点明确；
- 预设构造：`bigdecimal_ctx_decimal32/64/128()`（对齐 IEEE 754 十进制格式）。

### 8.3 内部算法

- 加/减：按 `exp` 差用 `bigint_dec_mul_pow10` 对齐（十进制移位为 O(n)，
  这是相对 bigfloat 二进制移位的对称优势），整数加减后去除尾随零规范化，
  按 `mant_digits` 舍入（舍入位与粘位直接从十进制肢读取，无需位操作）；
- 乘/除：尾数走 `bigint_dec_mul` / 带保护位的整数除法，与 §7.5 平行；
- sqrt 与十进制的关系不特殊，同样走移位试商整数平方根；
- 指数调整（`mul_pow10` 整肢移动）使绝大多数十进制缩放为 O(n) 内存搬移。

---

## 9. 大复数：bigcomplex_float_ty 与 bigcomplex_decimal_ty

### 9.1 数据结构

定义于 `nex/bigcomplex/float/nex_bigcomplex_float.h` 与
`nex/bigcomplex/decimal/nex_bigcomplex_decimal.h`：

```c99
typedef struct {
    bigfloat_ty re;  // 实部
    bigfloat_ty im;  // 虚部
} bigcomplex_float_ty;

typedef struct {
    bigdecimal_ty re;
    bigdecimal_ty im;
} bigcomplex_decimal_ty;
```

复数本身不新增标志；特殊值语义完全由分量的七标志组合表达。两者 API 结构相同，
以下以 `bigcomplex_float_` 前缀示例（decimal 版平行替换前缀与上下文类型）。

### 9.2 对外 API

```c99
/* 生命周期与构造 */
bigcomplex_float_err_ty bigcomplex_float_init(bigcomplex_float_ty *cpx); // 0 + 0i
void                  bigcomplex_float_free(bigcomplex_float_ty *cpx);
bigcomplex_float_err_ty bigcomplex_float_copy(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *src);
bigcomplex_float_err_ty bigcomplex_float_from_parts(bigcomplex_float_ty *cpx,
        const bigfloat_ty *re, const bigfloat_ty *im);
bigcomplex_float_err_ty bigcomplex_float_from_str(bigcomplex_float_ty *cpx,
        const char *str, const bigfloat_ctx_ty *ctx, const char **end);
        // 接受 "a+bi"、"a-bi"、"a"、"bi" 形式
bigcomplex_float_err_ty bigcomplex_float_to_str(const bigcomplex_float_ty *cpx,
        size_t max_digits, char *buf, size_t buf_len, size_t *needed);

/* 分量访问与一元运算 */
bigcomplex_float_err_ty bigcomplex_float_conj(bigcomplex_float_ty *cpx);  // 共轭
bigcomplex_float_err_ty bigcomplex_float_abs(bigfloat_ty *dst,
        const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);      // |z|
bigcomplex_float_err_ty bigcomplex_float_arg(bigfloat_ty *dst,
        const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
        // 辐角；需要 atan，列为 v1 可选实现，未实现时返回
        // BIGCOMPLEX_FLOAT_ERR_UNSUPPORTED_E

/* 四则 */
bigcomplex_float_err_ty bigcomplex_float_add(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx);
bigcomplex_float_err_ty bigcomplex_float_sub(...);   // 同型
bigcomplex_float_err_ty bigcomplex_float_mul(...);   // 同型
bigcomplex_float_err_ty bigcomplex_float_div(...);   // 同型

/* 相等性（复数无序，不提供 cmp） */
bool bigcomplex_float_eq(const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs);
```

实现要点：

- 乘法 `(a+bi)(c+di)` v1 用朴素四乘二加（Karatsuba 三乘式列为后续）；
- 除法 v1 用共轭法 `(a+bi)/(c+di) = (a+bi)(c−di)/(c²+d²)`；该方法在中间量
  上可能过早溢出/下溢，Smith 缩放算法列为后续方向并在文档与注释中注明取舍；
- `abs` 经 `sqrt(re² + im²)` 实现，同样的溢出注意事项适用；
- `arg` 依赖 atan，v1 允许返回 `..._ERR_UNSUPPORTED_E`，避免引入未经设计的
  超越函数实现。

### 9.3 特殊值传播

分量含特殊值时的运算结果规则（与 C99 `<complex.h>` 的 Annex G 思路一致，
但做简化：不区分虚无穷等细分情形）：

| 情形 | add/sub | mul | div |
|------|---------|-----|-----|
| 任一分量为 NaN | 结果 (NaN, NaN) | 结果 (NaN, NaN) | 结果 (NaN, NaN) |
| 有限 ± ∞ 混合（如 ∞ − ∞、0 × ∞） | 产生 NaN | 产生 NaN | 产生 NaN |
| 一方含 ∞，运算良定义（如 ∞ + 有限、有限 / ∞） | 按实部虚部分别的浮点规则 | 按分量规则组合 | 结果趋向 0 或按分量规则 |
| ±0 | 遵循 §11 的零符号规则 | 同左 | 同左 |

完整真值表以实现头文件注释为准；原则是"先把分量当独立浮点数按 §11 规则求值，
再按代数公式组合"，不引入复数特有的额外特殊值类别。

---

## 10. 类型间转换矩阵

图例：✓ 精确无损；R 精确值可表示时无损，否则按目标 ctx 舍入；
O 可能溢出 / 超范围（返回错误或产生 ±∞）；— 不提供直接接口。

| 源 \ 目标 | bigint_bin | bigint_dec | bigfrac | bigfloat | bigdecimal |
|-----------|------------|------------|---------|----------|------------|
| bigint_bin | — | ✓（转换单元） | ✓（den=1） | R / O | R / O |
| bigint_dec | ✓（转换单元） | — | ✓（经 bin） | R / O | R / O |
| bigfrac | 仅当 den==1（否则错误） | 仅当 den==1 | — | R / O | R / O |
| bigfloat | 仅整数值且有限（否则错误） | 经 bin | ✓（mant×2^exp 化简为分数） | — | R / O（经字符串或精确化） |
| bigdecimal | 仅整数值且有限 | ✓（mant×10^exp 展开） | ✓（精确分数） | R / O | — |
| 复数 → 实数类型 | — | — | — | 仅当 im 为 ±0（取 re） | 同左 |

原则：

- 所有有损转换都必须显式传入目标精度上下文，不提供"默认精度"的隐式有损转换，
  使每一处精度损失在调用点可见；
- 逆向转换（浮点 → 整数/分数）仅在接受精确值时成功，否则返回
  `..._ERR_INVALID_E`，不做静默截断；需要截断语义的调用方显式组合
  `to_bigfrac` + 取整操作；
- `bigfloat ↔ bigdecimal` 不共享内部表示，转换经过精确中间值（有理数）进行，
  保证单次舍入。

## 11. 错误处理与边界语义

**整数 / 有理数**：

- 除以零（含 `div_rem`、`pow_mod` 模为零、`bigfrac_from_ints` 分母为零、
  `bigfrac_inv(0)`）：返回 `..._ERR_DIV_ZERO_E`，输出参数不被修改；
- 任何因 OOM 或非法参数失败的运算，输出参数保持调用前状态（强异常安全保证）。

**浮点**（不返回错误，按 IEEE 754 思路产生特殊值）：

| 运算 | 结果 |
|------|------|
| 有限非零 / ±0 | ±∞（符号为操作数符号之积） |
| ±0 / ±0、±∞ / ±∞ | NaN |
| ±∞ − ±∞（同号无穷相减） | NaN |
| ±0 × ±∞ | NaN |
| sqrt(负数，含 −∞) | NaN |
| sqrt(−0) | −0 |
| NaN 参与的任何运算 | NaN（传播） |
| 有限值上溢 | 最近舍入 → ±∞；向零/向内舍入 → 最大有限值 |
| 有限值下溢 | flush-to-zero，符号按舍入方向（§7.3） |

**±0 符号规则**（对齐 IEEE 754）：

- `+0 == −0` 比较为真，但 `bigfloat_decompose` 可区分二者；
- `x + (−x) = +0`（最近舍入下；向 −∞ 舍入时为 −0）；
- 乘除的零符号 = 操作数符号之积；
- `to_str(+0)` 输出 `"0"`，`to_str(−0)` 输出 `"-0"`。

**字符串解析容错**：所有 `from_str` 遵循部分消费约定——尽可能多地消费合法前缀，
经 `end` 传出停止位置；完全无法解析（首字符即非法）返回 `..._ERR_PARSE_E` 且
输出参数不变。溢出到目标类型的合法前缀（如超长整数转 u64）按相应错误码处理。

## 12. 测试与验证策略

- **单元测试**：按模块组织于 `tests/`（目录与构建待定，不属本文档范围），
  每个公开函数至少覆盖：正常路径、每个错误码、边界值（0、±1、肢进位边界
  如 2^32−1 与 2^32、10^9−1 与 10^9）；
- **交叉验证**：`bigint_bin` 与 `bigint_dec` 是同一抽象的两种实现，
  随机用例经互转后比较结果（加法/乘法/除法/字符串输出）必须一致——
  这是本架构自带的廉价差分测试；
- **黄金对拍**：用 Python（`int`、`fractions.Fraction`、`decimal`、
  `mpmath`）生成随机用例与期望结果，C 侧比对；
- **浮点专项**：
  - 预设 ctx（binary32/64/128）下与硬件 `float`/`double`/`long double` 对照；
  - 舍入边界：构造距舍入点恰 1 ulp 半的用例，验证最近偶规则；
  - 特殊值真值表逐项覆盖（§9.3、§11）；
  - `to_str`/`from_str` 往返一致性；
- **内存正确性**：全流程在 ASan / Valgrind 下运行，错误路径注入失败分配
  （malloc 失败注入）验证清理路径；
- **不变式断言**：调试构建中每个公开 API 出口断言 §3.4 的规范化不变式。

## 13. 性能目标与后续方向

v1 性能目标（参考量级，非硬指标）：万位十进制整数乘法 < 10ms（schoolbook 可达成），
Karatsuba 切换阈值经实测标定（初定 32 肢）。

已识别的后续方向（均不改变 v1 语义，仅优化或扩展）：

1. Toom-3 / 多模 NTT 乘法（分节约定、模数表与位反转策略见 §4.3；NTT
   已实现）、递归除法、Newton 迭代除法与开方；
2. Montgomery 模幂、Lehmer/半 gcd；
3. 64 位肢变体（需处理 `uint64_t × uint64_t → 128 位`，受 C99 限制，
   可经编译器探测条件编译）；
4. 十进制输出的 Ryū 式快速算法；
5. 次正规浮点数支持（需放松"最高位恒 1"不变式，影响面大，单独设计评审）；
6. 自定义分配器钩子；
7. 复数 Karatsuba 三乘式、Smith 稳定除法；
8. 超越函数（atan 落地后解锁 `arg`，另有 exp/log/sin/cos）；
9. 浮点 FFT 的 SIMD 化与 AUTO 启用（标量 double 下仅 ~512² 肢窄带占优，
   见 §4.3；SIMD 可使中小规模反超 Karatsuba）；
10. 分治基数转换 bin↔dec（已落地）与 decimal 快速乘法（经"转 bin → FFT/NTT → 转回"，替代直接的十进制 FFT/NTT）；十进制字符串 I/O 已接入快速路径（bigint_bin base 10，v0.12）。

---

## 附录 A：缩写与后缀登记增补建议

以下条目已并入 `coding_standard.md` 附录 A（v1.2 / v1.3 / v1.4）：

| 缩写/后缀 | 全称 | 含义 | 用途示例 |
|-----------|------|------|----------|
| `bin` | binary | 二进制子模块缩写 | `bigint_bin_*` |
| `frac` | fraction | 有理数（分数）模块前缀 | `bigfrac_add` |
| `ctx` | context | 精度上下文 | `bigfloat_ctx_ty` |
| `mant` | mantissa | 尾数 | `mant_bits` |
| `exp` | exponent | 指数 | `exp_bits` |
| `re` / `im` | real / imag | 复数实部 / 虚部 | `bigcomplex_float_ty.re` |
| `inf` / `nan` | infinity / NaN | 无穷 / 非数 | `BIGFLOAT_POS_INF_E` |
| `conv` | conversion | 类型转换单元 | `nex_bigint_conv.h` |
| `convert` | cross-type conversion | 跨类型转换单元（§10 矩阵） | `nex_convert_*` |
| `u64` / `i64` / `f64` | uint64 / int64 / float64 | 定宽基本类型后缀 | `bigint_bin_from_u64` |
| `shl` / `shr` | shift left / right | 移位 | `bigint_bin_shl` |
| `popcount` | population count | 置位计数 | `bigint_bin_popcount` |
| `abs` | absolute value | 绝对值 | `bigint_bin_abs` |
| `arg` | argument | 复数辐角 | `bigcomplex_float_arg` |
| `bd` | bigint decimal | `bigint_dec` 实例惯用名（本规范示例沿用） | `bigint_dec_init(bd, len)` |
| `bigcomplex` | big complex | 大复数模块前缀 | `bigcomplex_float_add` |
| `bigdecimal` | big decimal | 十进制浮点模块前缀 | `bigdecimal_from_str` |
| `bigfloat` | big float | 二进制浮点模块前缀 | `bigfloat_add` |
| `buf` | buffer | 字符缓冲区 | `bigint_bin_to_str` |
| `cmp` | compare | 比较 | `bigint_bin_cmp` |
| `conj` | conjugate | 共轭 | `bigcomplex_float_conj` |
| `cpx` | complex | 复数实例惯用名 | `bigcomplex_float_init(cpx)` |
| `den` | denominator | 分母 | `bigfrac_ty.den` |
| `div` | divide | 除法 | `bigint_bin_div_rem` |
| `dst` | destination | 输出/目标参数惯用名 | `bigint_bin_add(dst, ...)` |
| `eq` | equal | 相等判断 | `bigfloat_eq` |
| `err` | error | 错误码枚举中段 | `BIGINT_ERR_OOM_E` |
| `init` | initialize | 初始化 | `bigint_bin_init` |
| `inv` | inverse | 倒数 | `bigfrac_inv` |
| `lhs` / `rhs` | left/right-hand side | 二元运算左/右操作数惯用名 | `bigint_bin_add(dst, lhs, rhs)` |
| `mod` | modulus | 模 | `bigint_bin_pow_mod` |
| `mul` | multiply | 乘法 | `bigint_bin_mul` |
| `neg` | negate | 取负 | `bigint_bin_neg` |
| `num` | numerator | 分子 | `bigfrac_ty.num` |
| `pow` | power | 幂 | `bigint_bin_pow` |
| `quot` | quotient | 商 | `bigint_bin_div_rem(quot, rem, ...)` |
| `rem` | remainder | 余数 | `bigint_bin_div_rem` |
| `sqrt` | square root | 平方根 | `bigfloat_sqrt` |
| `src` | source | 源参数惯用名 | `bigint_bin_copy(dst, src)` |
| `str` | string | 字符串 | `bigint_bin_from_str` |
| `sub` | subtract | 减法 | `bigint_bin_sub` |
| `val` | value | 一元/就地操作数惯用名 | `bigint_bin_neg(val)` |
| `algo` | algorithm | 算法标签 | `bigint_mul_algo_ty` |
| `crt` | Chinese remainder theorem | 中国剩余定理 | `BIGINT_MUL_MULTI_MODULI_CRT_NTT_E` |
| `ex` | extended | 扩展入口后缀 | `bigint_bin_mul_ex` |
| `fft` | fast Fourier transform | 快速傅里叶变换 | `BIGINT_MUL_FLOAT_COMPLEX_FFT_E` |
| `ntt` | number theoretic transform | 数论变换 | `BIGINT_MUL_MULTI_MODULI_CRT_NTT_E` |
| `param` | parameter | 参数 | `bigint_mul_params_ty` |

## 修订记录

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v0.1 | 2026-08-05 | 初版：七大类型设计、模块架构、通用约定、转换矩阵、边界语义、测试策略 |
| v0.2 | 2026-08-10 | 新增乘法方法选择机制（`bigint_mul_method_ty` / `bigint_bin_mul_ex` / `bigint_dec_mul_ex`）；`bigint_err_ty` 新增 `BIGINT_ERR_UNSUPPORTED_E`；补变换类乘法分节约定（FFT 默认 4×8 bit，NTT 按模数推导） |
| v0.3 | 2026-08-11 | 新增公开 API `bigint_bin_gcd`（二进制 GCD，`gcd(0, 0) = 0`，结果恒非负），供 bigfrac 约分使用 |
| v0.4 | 2026-08-11 | §8 bigdecimal 模块落地：规范化的“无尾随零”语义（`12 × 10^−1`）、对称指数范围 ±(10^exp_digits−1)、精确字符串 I/O（定点/科学计数两种格式）、十进制逐位试商整数开方；§7.3 澄清预设指数范围与 IEEE 的差异 |
| v0.5 | 2026-08-11 | §9 bigcomplex 落地（float / decimal 两版）：分量组合语义（乘法朴素四乘二加、除法共轭法、abs 经 sqrt(re²+im²)）、任一分量 NaN → 整体 (NaN, NaN)（§9.3）、"a±bi" 字面量解析与输出、arg 返回 UNSUPPORTED（依赖 atan，v1 未实现） |
| v0.6 | 2026-08-11 | §10 转换矩阵落地：新增专用转换单元 `nex/convert/`（nex_convert_*，沿袭 nex_bigint_conv 的跨模块胶水层先例）。精确互转（bigint↔bigfrac、浮点→bigfrac）、逆向精确转换（浮点→整数仅接受整数值，否则 INVALID 不截断）、有损互转（显式 ctx、经精确有理数中间值带保护位除法一次舍入，float↔decimal 走 mant×5^k/10^k 精确表示）；复数→实数（im 为 ±0 取 re）。附录 A 注册 `convert` 缩写 |
| v0.7 | 2026-08-12 | §4.3 变换类乘法分节约定细化：十进制 FFT 定稿为每肢 3 节 × base 1000（弃用 4 节 × 256：两方案变换长度同为 N = 8n，但 256 方案需 bin→dec 基数转换还原且最高节为非均匀 0..59）；NTT 分节按模数容量推导并定默认（bin 16 bit、dec 15 bit，均 2 节/肢、N = 4n）；新增 NTT 模数结构体 `ntt_mod_ty`（p/k/c/w）、内置素数表（11 个 k·2^c+1 素数，c 21..27，均经校验）与确定性校验方案（MR 基数 {2,3,5,7,11} + 根阶验证，合数返回 INVALID）；位反转：DIF/DIT 配对消除，`ntt_bitrev` 由调用方供缓冲区；NTT 模块归属建议 `nex/ntt/`（开放问题） |
| v0.8 | 2026-08-12 | §4.3 模块归属定案：NTT 核心落地为 `nex/ntt/`（`nex_ntt.h/.c`，仅操作系数数组、无动态分配），§2.1 依赖规则放宽为"bin ↔ dec 互不依赖，可共享只读公共算法模块"，§2.2 目录树增补 `nex/ntt/`；随 NTT 核心实现（Phase 1：模数表 + 校验、DIF/DIT 变换、点乘、Garner CRT 重构）一并交付 |
| v0.9 | 2026-08-12 | §4.3 NTT 乘法落地（Phase 2）：`bigint_bin` 集成（16-bit 分节、双模数选择、Garner 常数预计算 + base-2^16 进位还原）；Montgomery 模乘内部化（蝶形无除法，§13 方向落地）；AUTO 阈值实测标定 16384 肢（Karatsuba 交叉点约 13K 肢，-O2）；黄金对拍新增 `mul_ntt` 命令 |
| v0.10 | 2026-08-12 | §4.3 浮点复数 FFT 落地（Phase 3）：`nex/fft/` 核心（DIF/DIT 免位反转）+ `bigint_bin` 集成（强制方法，节位宽 0/8/16）；实测标量 double 下 16-bit 节仅 ~512² 肢窄带胜过 Karatsuba（~10%），8-bit 节处处落败，故 AUTO 不采用 FFT（SIMD 化列入 §13）；NTT 三模数决策：不实现（无场景 + 128 位中间量，容量已覆盖 512 Mbit），128 位乘法留待 64 位肢工作；§13 增补 FFT SIMD 化与分治基数转换方向；黄金对拍新增 `mul_fft` 命令 |
| v0.11 | 2026-08-12 | §4.3 分治基数转换落地（§13 #10）：`bigint_conv_bin_to_dec` / `bigint_conv_dec_to_bin` 改为分治（2 的幂切半 + 平方链表，T(n) = 2T(n/2) + M(n)），阈值按方向独立实测标定（bin→dec 256 肢、dec→bin 2048 肢起分治胜出；8192 肢分别快 3.6 倍 / 2 倍）；修复分治基例未设 sign 导致低半丢失的缺陷；黄金对拍补齐 `c_b2d`/`c_d2b` 大数用例（此前无覆盖）；新增 `test_bigint_conv` 单元测试 |
| v0.12 | 2026-08-12 | §4.3 十进制字符串 I/O 接入快速路径：`bigint_bin_from_str` / `bigint_bin_to_str`（base 10）改为"9 位分组直析 dec 肢 → 分治 dec→bin" / "分治 bin→dec → 逐肢格式化"（经转换单元跨支线，§2.1），替代 O(n²) 的 parse_digits / digits_generic（20 万位 from_str ~60ms）；测试补盲：新增 `test_bigint_str`（往返、9 位分组边界、10^k 邻域、部分消费与错误语义、大小查询），`test_oom` 增加 2 万位串的 from_str / to_str OOM 注入扫描（覆盖分治转换分配点），golden 的 `c_b2d`/`c_d2b` 补充 2 万位与 10^k 边界用例 |
