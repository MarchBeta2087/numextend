# 1. 简介与能力

Numextend 的目标是：在保持**纯 C99、零外部依赖**的前提下，提供一组**语义严格**
的任意精度数值类型——正确舍入、强异常安全（OOM 不破坏输出）、显式精度上下文。

## 1.1 能力总览

| 类型 | 主要能力 |
|------|----------|
| `bigint_bin` / `bigint_dec` | 加减乘除（带余）、幂、模幂、gcd、移位、按位运算、整数平方根、字符串 I/O（进制 2..36）、比较与符号；`bin ↔ dec` 互转 |
| `bigint_bin64` | 64 位肢整数核心（加减乘、除法；用例见头文件） |
| `bigfrac` | 精确有理数四则与约分、比较、与整数互转 |
| `bigfloat` / `bigdecimal` | 四则、`sqrt`、正确舍入、特殊值（±0/±∞/NaN）、分解/合成、字符串 I/O；**正确舍入超越函数** `atan`/`atan2`/`exp`/`log`/`sin`/`cos`/`tan`/`asin`/`acos`（球式 + Ziv）；`bigdecimal` 十进制 I/O 完全精确 |
| `bigcomplex_float` / `bigcomplex_decimal` | 加减乘除、共轭、模、辐角 `arg`、相等；超越函数 `exp`/`log`/`sqrt`/`sin`/`cos`/`tan`（分量运算） |
| `convert` | 七类型之间的精确/有损转换矩阵 |

## 1.2 设计取舍

1. **正确性优先**，其次可读性，再次性能；
2. **无隐式精度**：任何有损转换都必须显式传入目标精度上下文；
3. **强异常安全**：内存不足或非法参数时，输出参数保持调用前状态；
4. **不做**（v1.x 非目标）：SIMD/GPU/内部多线程（跨平台线程封装列为独立
   可选目标，不进核心）、次正规浮点、超大 `|x|`（> 2⁶³）三角函数的
   Payne–Hanek 规约（当前折半上限覆盖 |x| ≲ 2⁶³）。

## 1.3 基本用法

```c
#include "nex/bigint/bin/nex_bigint_bin.h"

bigint_bin_ty a, b, c;
bigint_bin_init(&a);
bigint_bin_init(&b);
bigint_bin_init(&c);
bigint_bin_from_u64(&a, 123456789U);
bigint_bin_from_u64(&b, 987654321U);
bigint_bin_mul(&c, &a, &b);       /* c = a × b，允许别名 */
/* ... 使用 c ... */
bigint_bin_free(&a);
bigint_bin_free(&b);
bigint_bin_free(&c);
```

每个值的生命周期遵循 `_init` → 运算 → `_free`；`_move` 可转移所有权，
`_copy` 深拷贝。

## 1.4 语言与标准兼容性

- **C**：默认以 C99 构建（项目承诺基准）；C11 / C17 / C23（及 C2y）实测通过。
  可用 `-DNEX_C_STANDARD=11/17/23` 切换构建标准做验证。
- **C++**：全部公开头文件带 `extern "C"` 守卫，C++11–C++26 可经 C ABI 直接
  链接消费，无需包装或单独绑定。

C++ 中使用示例：

```cpp
#include "nex/bigint/bin/nex_bigint_bin.h"

int main() {
    bigint_bin_ty a{};
    bigint_bin_init(&a);
    bigint_bin_from_u64(&a, 123456789u);
    /* ... */
    bigint_bin_free(&a);
    return 0;
}
```
