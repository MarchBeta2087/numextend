# Numextend 参考手册

Numextend（库前缀 `nex`）是一个**纯 C99、零依赖**的任意精度数值库，提供七种
数值类型与一张跨类型转换矩阵。

- **整数**：`bigint_bin`（二进制肢，基 2³²）、`bigint_dec`（十进制肢，基 10⁹）、
  `bigint_bin64`（64 位肢变体）。
- **有理数**：`bigfrac`（既约分数）。
- **浮点**：`bigfloat`（二进制）、`bigdecimal`（十进制），精度与指数范围可调，
  逐运算正确舍入。
- **复数**：`bigcomplex_float`、`bigcomplex_decimal`。
- **超越函数**：`nex/transcend` 提供正确舍入的 `atan`/`atan2`/`exp`/`log`/
  `sin`/`cos`/`tan`/`asin`/`acos`（`bigfloat`/`bigdecimal` 各一套）及复数超越
  函数 `exp`/`log`/`sqrt`/`sin`/`cos`/`tan`。

本手册面向**使用者**，说明每种类型的公开 API、语义与约定。
设计原理、算法与内部结构见仓库内 [`../design.md`](../design.md)。

> 兼容性：C99 为承诺基准，C11/C17/C23（及 C2y）实测通过；全部公开头文件带
> `extern "C"`，C++11–C++26 可经 C ABI 直接链接。

> 顺序：简介 → 通用约定 → 安装与使用 → 示例 → API 参考（按类型）。
> 生成的 API 章节直接取自公开头文件的注释，是本手册的事实来源。
