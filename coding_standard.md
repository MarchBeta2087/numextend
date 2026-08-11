# Numextend 编码规范

> 版本：v1.4
> 适用范围：本项目全部 C 源代码（`.c` / `.h`）
> 说明：本规范为强制性要求，code review 以此为准。条款编号在本文档中唯一，评审意见可直接引用编号（如 "违反 §3.2"）。

---

## 目录

- [Numextend 编码规范](#numextend-编码规范)
  - [目录](#目录)
  - [1. 总则](#1-总则)
  - [2. 项目结构与文件](#2-项目结构与文件)
  - [3. 格式与排版](#3-格式与排版)
  - [4. 命名规则](#4-命名规则)
  - [5. 注释规范](#5-注释规范)
  - [6. 类型与变量](#6-类型与变量)
  - [7. 指针与内存](#7-指针与内存)
  - [8. 函数](#8-函数)
  - [9. 控制语句](#9-控制语句)
  - [10. 宏与枚举](#10-宏与枚举)
  - [11. 头文件与模块](#11-头文件与模块)
  - [12. 性能与安全](#12-性能与安全)
  - [13. 并发](#13-并发)
  - [附录 A：缩写与后缀登记表](#附录-a缩写与后缀登记表)
  - [附录 B：标准示例文件](#附录-b标准示例文件)
  - [修订记录](#修订记录)

---

## 1. 总则

| 编号 | 规则 |
|------|------|
| 1.1 | 本项目使用 **C99** 标准（`-std=c99`），不得使用编译器私有扩展，除非经评审批准并注明。 |
| 1.2 | 所有代码必须通过 `-Wall -Wextra -Wpedantic` 编译且无警告。 |
| 1.3 | 规范中未覆盖的情形，参考 MISRA C:2012 与 CERT C 的相应条款处理。 |

## 2. 项目结构与文件

| 编号 | 规则 |
|------|------|
| 2.1 | 项目文件目录深度最多为 **4 层**。 |
| 2.2 | 文件名（含路径与后缀）长度限制：头文件全名不超过 **52 字符**（含 `.h`）。模块路径可缩写，可读性优先，缩写必须登记到 [附录 A](#附录-a缩写与后缀登记表)。 |
| 2.3 | 源文件使用 UTF-8 编码、LF 换行符。 |
| 2.4 | 每个 `.c` 文件应有对应的职责说明，单个文件建议不超过 2000 行。 |

## 3. 格式与排版

| 编号 | 规则 |
|------|------|
| 3.1 | 使用 **4 空格缩进**，禁止使用 Tab。 |
| 3.2 | 行宽限制为 **96 字符**，一个汉字（全角字符）视作 2 个字符。 |
| 3.3 | 超长行换行时，续行相对于起始行**额外缩进 4 格**；函数参数换行可与首个参数对齐。 |
| 3.4 | 左大括号 `{` 紧接在控制语句后同一行末尾（K&R 风格）；右大括号 `}` 独占一行。 |
| 3.5 | 每行只定义**一个**变量，每行只写一条语句。 |
| 3.6 | 指针声明风格为 `int *p`：星号紧贴变量名、位于空格右侧。常量指针为 `const int *p`。 |
| 3.7 | 二元运算符两侧各加一个空格；逗号后加一个空格；`if`/`for`/`while` 关键字与 `(` 之间加一个空格。 |
| 3.8 | 函数定义之间空两行；逻辑段落之间空一行；禁止连续三个及以上空行。 |

```c99
/* 3.4 / 3.6 示例 */
if (err != BIGINT_OK_E) {
    const int *p = src->limbs;  // 只读遍历，禁止写入
    ...
}
```

## 4. 命名规则

| 编号 | 规则 |
|------|------|
| 4.1 | 变量名、函数名、结构体类型名：小写蛇形命名法（`snake_case`）。 |
| 4.2 | 宏常量名、枚举成员名：大写蛇形命名法（`UPPER_SNAKE_CASE`）。枚举成员名须加 `_E` 后缀。 |
| 4.3 | 结构体（及共用体）使用 `typedef` 定义，类型名加 `_ty` 后缀（见 §6.4）。 |
| 4.4 | 函数名必须加**模块前缀**，格式：`模块_动作_对象(...)`，如 `bigint_dec_init`。 |
| 4.5 | 长度上限：变量名、宏常量名、枚举成员名 ≤ **48** 字符；结构体类型名 ≤ **48** 字符（含 `_ty`）；函数名 ≤ **56** 字符。 |
| 4.6 | 禁止使用单字母变量名，循环计数器 `i`/`j`/`k` 除外；禁止以双下划线或"下划线+大写字母"开头（保留给实现）。 |
| 4.7 | 布尔语义的名字使用 `is_`/`has_`/`can_` 前缀，如 `is_empty`。 |

## 5. 注释规范

| 编号 | 规则 |
|------|------|
| 5.1 | 函数注释使用多行注释 `/* */`；变量及行内说明使用单行注释 `//`。 |
| 5.2 | 每个对外接口函数必须在头文件声明处附注释块，包含 `brief`、`param`、`return`；可选 `note`。内部（`static`）函数可精简为 `brief` 一段，但不得省略。 |
| 5.3 | 注释说明"为什么"和"契约"，不复述代码本身。代码修改时必须同步更新注释。 |
| 5.4 | 圈复杂度超过 10 的函数（见 §8.4），必须在注释中说明原因。 |

函数注释模板：

```c99
/*
 * brief: 初始化大整数
 * param: bd  需要初始化的大整数变量
 * param: len 设定的 uint32_t *limbs 的数组长度
 * return: 如果返回 len，则成功，否则失败
 * note: len 必须大于 0 且指针非空
 */
size_t bigint_dec_init(bigint_dec_ty *bd, size_t len);
```

## 6. 类型与变量

| 编号 | 规则 |
|------|------|
| 6.1 | 整数类型优先使用 `stdint.h` 的定宽类型（`uint32_t`、`size_t` 等），避免裸 `int`/`long` 表达位宽语义。 |
| 6.2 | **禁止隐式类型转换**，必须显式写出强制转换，如 `uint32_t v = (uint32_t)raw;`。 |
| 6.3 | 变量在使用前必须初始化；声明即初始化优先。 |
| 6.4 | 结构体/共用体使用 `typedef` 定义并以 `_ty` 结尾： |

```c99
typedef struct {
    uint32_t *limbs;  // 肢数组，按小端序存储
    size_t len;       // 当前肢数
    size_t cap;       // 已分配容量
} bigint_dec_ty;
```

| 6.5 | 禁止在函数中间声明变量以外的"goto 式"跳转初始化绕过；声明应尽量靠近首次使用处（C99 允许块内任意位置声明）。 |

## 7. 指针与内存

| 编号 | 规则 |
|------|------|
| 7.1 | **禁止使用 VLA（变长数组）**；动态数组一律使用 `malloc`/`calloc` 分配。 |
| 7.2 | `malloc`/`calloc`/`realloc` 的返回值**必须判空**后才可使用。 |
| 7.3 | `free` 之后立即将指针置为 `NULL`；每个分配点必须有明确对应的释放路径（含错误分支）。 |
| 7.4 | 指向内容不可变的输入指针参数必须加 `const`，形式为 `const int *p`。 |
| 7.5 | 禁止使用 `gets`、`strcpy`、`strcat`、`sprintf` 等无边界函数；输入读取使用 `fgets`，字符串操作使用 `snprintf`/`strncpy` 并保证 `'\0'` 结尾。 |
| 7.6 | 指针运算不得越出数组边界；禁止解引用未判空的入参指针（除非注释中明确契约并由断言保护）。 |

## 8. 函数

| 编号 | 规则 |
|------|------|
| 8.1 | 函数名加模块前缀（见 §4.4）。 |
| 8.2 | 单个函数不超过 **112 行**（空行与注释行不计入）。 |
| 8.3 | 圈复杂度不超过 **15**；超过 10 须在注释中说明理由（见 §5.4）。 |
| 8.4 | 文件内私有函数与变量必须声明为 `static`，不出现于头文件。 |
| 8.5 | 无参函数形参列表写 `(void)`，不允许空括号。 |
| 8.6 | 函数返回值表达错误语义时，调用方必须检查返回值；确需忽略时显式写 `(void)func(...)` 并注释原因。 |
| 8.7 | **禁止使用 `goto`**；唯一例外是函数内统一的错误清理出口（如 `goto cleanup;`），且只能向前跳转、不得构成循环。 |
| 8.8 | 参数数量建议不超过 5 个；超过时改用结构体聚合传参。 |

## 9. 控制语句

| 编号 | 规则 |
|------|------|
| 9.1 | `switch` 语句**必须包含 `default` 分支**；若 `default` 不可达，则在其中放置断言或错误处理。 |
| 9.2 | `case` 分支以 `break`、`return` 或 `/* fallthrough */` 注释结束，禁止无标注的贯通。 |
| 9.3 | `if`/`for`/`while` 即使只有一条语句也必须加大括号。 |
| 9.4 | 禁止在 `if` 条件内赋值（`=` 与 `==` 混淆风险）；确有必要时写为两行。 |
| 9.5 | 循环嵌套不超过 3 层；条件表达式中逻辑运算符不超过 3 个。 |

## 10. 宏与枚举

| 编号 | 规则 |
|------|------|
| 10.1 | 函数式宏的参数与整体必须加括号保护：`#define SQR(x) ((x) * (x))`。 |
| 10.2 | 能用 `enum` 或 `static const` 表达的不使用对象式宏；能用内联函数的不使用函数式宏。 |
| 10.3 | 枚举成员全大写蛇形 + `_E` 后缀（见 §4.2），错误码统一以 `模块_..._E` 命名： |

```c99
typedef enum {
    BIGINT_OK_E = 0,        // 成功
    BIGINT_ERR_OOM_E,       // 内存分配失败
    BIGINT_ERR_INVALID_E    // 非法参数
} bigint_err_ty;
```

| 10.4 | 多行宏使用 `do { ... } while (0)` 包裹。 |

## 11. 头文件与模块

| 编号 | 规则 |
|------|------|
| 11.1 | 头文件名采用"项目前缀 + 模块路径"命名，如 `proj_math_bigint.h`。 |
| 11.2 | 每个头文件使用 include guard，宏名与文件路径一一对应、全项目唯一：`#ifndef PROJ_MATH_BIGINT_H`。 |
| 11.3 | **对外接口头文件必须自包含**：即该头文件可被单独 `#include` 编译通过，所需类型自行包含对应头文件，不依赖包含顺序。 |
| 11.4 | `#include` 顺序：① 本模块自身头文件 → ② 系统/标准库头文件 → ③ 项目内其他头文件；组间空一行，组内按字母序排列。 |
| 11.5 | 头文件中只允许声明（`extern` 函数、`typedef`、宏），禁止定义非 `static` 变量和非内联函数。 |

```c99
#ifndef PROJ_MATH_BIGINT_H
#define PROJ_MATH_BIGINT_H

#include <stddef.h>
#include <stdint.h>

...

#endif /* PROJ_MATH_BIGINT_H */
```

## 12. 性能与安全

| 编号 | 规则 |
|------|------|
| 12.1 | **禁止在循环条件或循环体内重复调用 `strlen()` 等 O(n) 函数**；应在循环前缓存结果： |

```c99
/* 错误 */
for (size_t i = 0U; i < strlen(buf); i++) { ... }

/* 正确 */
const size_t buf_len = strlen(buf);  // 缓存长度，避免循环内 O(n) 调用
for (size_t i = 0U; i < buf_len; i++) { ... }
```

| 12.2 | 所有外部输入（文件、网络、用户）在使用前必须校验长度与取值范围。 |
| 12.3 | 数组下标、循环计数使用 `size_t` 时，注意与有符号类型比较产生的隐式转换（见 §6.2）。 |

## 13. 并发

| 编号 | 规则 |
|------|------|
| 13.1 | 多线程环境下的共享变量，所有读写必须由**显式同步原语**保护（互斥锁、读写锁、原子操作），不得依赖"实际上不会冲突"。 |
| 13.2 | 锁的获取顺序必须在模块注释中登记，全项目保持一致的锁序，避免死锁。 |
| 13.3 | 加锁区间尽量短，不得在持锁期间调用可能阻塞或回调外部的函数。 |

## 附录 A：缩写与后缀登记表

> 所有文件名缩写、模块前缀、自定义后缀必须登记于此，方可使用。

| 缩写/后缀 | 全称 | 含义 | 用途示例 |
|-----------|------|------|----------|
| `_ty` | type | typedef 定义的结构体/共用体类型后缀 | `bigint_dec_ty` |
| `_E` | enum | 枚举成员名后缀 | `BIGINT_OK_E` |
| `bigint` | big integer | 大整数模块前缀 | `bigint_dec_init` |
| `dec` | decimal | 十进制子模块缩写 | `bigint_dec_*` |
| `oom` | out of memory | 内存不足错误 | `BIGINT_ERR_OOM_E` |
| `len` | length | 长度 | `buf_len` |
| `cap` | capacity | 容量 | `cap` |
| `limb` | limb | 大整数存储单元 | `limbs` |
| `nex` | Numextend | Numextend 库 | `nex_bigint_bin.h` |
| `bin` | binary | 二进制子模块缩写 | `bigint_bin_*` |
| `bitrev` | bit reversal | 位反转置换 | `ntt_bitrev` |
| `conv` | conversion | 类型转换单元 | `nex_bigint_conv.h` |
| `ctx` | context | 精度上下文 | `bigfloat_ctx_ty` |
| `exp` | exponent | 指数 | `exp_bits` |
| `frac` | fraction | 有理数（分数）模块前缀 | `bigfrac_add` |
| `inf` / `nan` | infinity / NaN | 无穷 / 非数 | `BIGFLOAT_POS_INF_E` |
| `mant` | mantissa | 尾数 | `mant_bits` |
| `popcount` | population count | 置位计数 | `bigint_bin_popcount` |
| `re` / `im` | real / imag | 复数实部 / 虚部 | `bigcomplex_float_ty.re` |
| `shl` / `shr` | shift left / right | 移位 | `bigint_bin_shl` |
| `u64` / `i64` / `f64` | uint64 / int64 / float64 | 定宽基本类型后缀 | `bigint_bin_from_u64` |
| `abs` | absolute value | 绝对值 | `bigint_bin_abs` |
| `algo` | algorithm | 算法标签 | `bigint_mul_algo_ty` |
| `arg` | argument | 复数辐角 | `bigcomplex_float_arg` |
| `bd` | bigint decimal | `bigint_dec` 实例惯用名（本规范示例沿用） | `bigint_dec_init(bd, len)` |
| `bigcomplex` | big complex | 大复数模块前缀 | `bigcomplex_float_add` |
| `bigdecimal` | big decimal | 十进制浮点模块前缀 | `bigdecimal_from_str` |
| `bigfloat` | big float | 二进制浮点模块前缀 | `bigfloat_add` |
| `buf` | buffer | 字符缓冲区 | `bigint_bin_to_str` |
| `cmp` | compare | 比较 | `bigint_bin_cmp` |
| `conj` | conjugate | 共轭 | `bigcomplex_float_conj` |
| `cpx` | complex | 复数实例惯用名 | `bigcomplex_float_init(cpx)` |
| `crt` | Chinese remainder theorem | 中国剩余定理 | `BIGINT_MUL_MULTI_MODULI_CRT_NTT_E` |
| `den` | denominator | 分母 | `bigfrac_ty.den` |
| `dif` | decimation in frequency | 频域抽取（DIF 蝶形，正变换） | 注释用语 |
| `div` | divide | 除法 | `bigint_bin_div_rem` |
| `dit` | decimation in time | 时域抽取（DIT 蝶形，逆变换） | 注释用语 |
| `dst` | destination | 输出/目标参数惯用名 | `bigint_bin_add(dst, ...)` |
| `eq` | equal | 相等判断 | `bigfloat_eq` |
| `err` | error | 错误码枚举中段 | `BIGINT_ERR_OOM_E` |
| `ex` | extended | 扩展入口后缀 | `bigint_bin_mul_ex` |
| `fft` | fast Fourier transform | 快速傅里叶变换 | `BIGINT_MUL_FLOAT_COMPLEX_FFT_E` |
| `init` | initialize | 初始化 | `bigint_bin_init` |
| `inv` | inverse | 倒数 | `bigfrac_inv` |
| `lhs` / `rhs` | left/right-hand side | 二元运算左/右操作数惯用名 | `bigint_bin_add(dst, lhs, rhs)` |
| `mod` | modulus | 模 | `bigint_bin_pow_mod` |
| `mul` | multiply | 乘法 | `bigint_bin_mul` |
| `neg` | negate | 取负 | `bigint_bin_neg` |
| `ntt` | number theoretic transform | 数论变换 | `BIGINT_MUL_MULTI_MODULI_CRT_NTT_E` |
| `ntt_mod` | NTT modulus | NTT 模数结构体类型前缀 | `ntt_mod_ty` |
| `num` | numerator | 分子 | `bigfrac_ty.num` |
| `param` | parameter | 参数 | `bigint_mul_params_ty` |
| `pow` | power | 幂 | `bigint_bin_pow` |
| `quot` | quotient | 商 | `bigint_bin_div_rem(quot, rem, ...)` |
| `rem` | remainder | 余数 | `bigint_bin_div_rem` |
| `sqrt` | square root | 平方根 | `bigfloat_sqrt` |
| `src` | source | 源参数惯用名 | `bigint_bin_copy(dst, src)` |
| `str` | string | 字符串 | `bigint_bin_from_str` |
| `sub` | subtract | 减法 | `bigint_bin_sub` |
| `val` | value | 一元/就地操作数惯用名 | `bigint_bin_neg(val)` |
| `w` | root of unity | 单位根（NTT 模数结构体字段） | `ntt_mod_ty.w` |

（新增条目请在此追加，保持按字典序排列。）

## 附录 B：标准示例文件

以下示例完全符合本规范，可作为新文件的模板。

**`proj_math_bigint.h`**

```c99
#ifndef PROJ_MATH_BIGINT_H
#define PROJ_MATH_BIGINT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BIGINT_OK_E = 0,        // 成功
    BIGINT_ERR_OOM_E,       // 内存分配失败
    BIGINT_ERR_INVALID_E    // 非法参数
} bigint_err_ty;

typedef struct {
    uint32_t *limbs;  // 肢数组，按小端序存储
    size_t len;       // 当前肢数
    size_t cap;       // 已分配容量
} bigint_dec_ty;

/*
 * brief: 初始化大整数
 * param: bd  需要初始化的大整数变量
 * param: len 设定的 uint32_t *limbs 的数组长度
 * return: 成功返回 BIGINT_OK_E，失败返回错误码
 * note: len 必须大于 0 且 bd 非空
 */
bigint_err_ty bigint_dec_init(bigint_dec_ty *bd, size_t len);

/*
 * brief: 释放大整数占用的内存
 * param: bd 需要释放的大整数变量，可为 NULL
 */
void bigint_dec_free(bigint_dec_ty *bd);

#endif /* PROJ_MATH_BIGINT_H */
```

**`proj_math_bigint.c`**

```c99
#include "proj_math_bigint.h"

#include <stdlib.h>
#include <string.h>

/*
 * brief: 初始化大整数
 * param: bd  需要初始化的大整数变量
 * param: len 设定的 uint32_t *limbs 的数组长度
 * return: 成功返回 BIGINT_OK_E，失败返回错误码
 * note: len 必须大于 0 且 bd 非空
 */
bigint_err_ty bigint_dec_init(bigint_dec_ty *bd, size_t len)
{
    uint32_t *limbs = NULL;  // 新分配的肢数组

    if ((bd == NULL) || (len == 0U)) {
        return BIGINT_ERR_INVALID_E;
    }

    limbs = (uint32_t *)calloc(len, sizeof(uint32_t));
    if (limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }

    bd->limbs = limbs;
    bd->len = 0U;
    bd->cap = len;
    return BIGINT_OK_E;
}

/*
 * brief: 释放大整数占用的内存
 * param: bd 需要释放的大整数变量，可为 NULL
 */
void bigint_dec_free(bigint_dec_ty *bd)
{
    if (bd == NULL) {
        return;
    }

    free(bd->limbs);
    bd->limbs = NULL;  // free 后指针置空，防止悬垂引用
    bd->len = 0U;
    bd->cap = 0U;
}
```

---

## 修订记录

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v1.0 | — | 初版（22 条原始规则） |
| v1.1 | 2026-08-01 | 重构为 13 章；新增：编译警告要求（§1.2）、续行缩进（§3.3）、定宽整数（§6.1）、malloc 判空与 free 置空（§7.2/7.3）、static 约定（§8.4）、返回值检查（§8.6）、goto 限制（§8.7）、宏括号保护（§10.1）、include 顺序（§11.4）、锁序登记（§13.2）；补充附录 A（缩写登记表）与附录 B（示例文件） |
| v1.2 | 2026-08-05 | 附录 A 并入设计文档（`docs/design.md`）登记的缩写：`bin`/`conv`/`ctx`/`exp`/`frac`/`inf`/`nan`/`mant`/`popcount`/`re`/`im`/`shl`/`shr`/`u64`/`i64`/`f64` |
| v1.3 | 2026-08-05 | 附录 A 补登 30 条：`abs`/`arg`/`bd`/`bigcomplex`/`bigdecimal`/`bigfloat`/`buf`/`cmp`/`conj`/`cpx`/`den`/`div`/`dst`/`eq`/`err`/`init`/`inv`/`lhs`/`rhs`/`mod`/`mul`/`neg`/`num`/`pow`/`quot`/`rem`/`sqrt`/`src`/`str`/`sub`/`val` |
| v1.4 | 2026-08-10 | 附录 A 补登 6 条：`algo`/`crt`/`ex`/`fft`/`ntt`/`param`（配合设计文档 v0.2 乘法方法选择机制） |
| v1.5 | 2026-08-12 | 附录 A 补登 5 条：`bitrev`/`dif`/`dit`/`ntt_mod`/`w`（配合设计文档 v0.7 NTT 设计细化） |
