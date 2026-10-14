# 更新日志

本文件记录 Numextend 的版本变更，格式参考
[Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

v1.0.0 之前的开发以设计文档的「修订记录」逐次跟踪（见
[`docs/design.md`](docs/design.md#修订记录)）。

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

[1.1.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.1.0
[1.0.0]: https://github.com/MarchBeta2087/numextend/releases/tag/v1.0.0
