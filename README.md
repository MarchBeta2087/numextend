# Numextend

[![CI](https://github.com/MarchBeta2087/numextend/actions/workflows/ci.yml/badge.svg)](https://github.com/MarchBeta2087/numextend/actions/workflows/ci.yml)

任意精度数值计算库（C99，零依赖），覆盖整数、有理数、二进制/十进制浮点与复数，
带类型间精确/有损转换矩阵。设计文档见 [`docs/design.md`](docs/design.md)（§4–§10
为七类数值语义与转换矩阵规范）；待办与路线图见 [`TODO.md`](TODO.md)。

## 模块（`nex/`）

| 模块 | 类型 | 语义 | 构建于 |
|------|------|------|--------|
| `bigint/bin` | `bigint_bin_ty` | 基 2³² 任意精度整数（运算快） | — |
| `bigint/dec` | `bigint_dec_ty` | 基 10⁹ 任意精度整数（十进制 I/O 快） | — |
| `bigfrac` | `bigfrac_ty` | 精确有理数（既约分数） | bigint_bin |
| `bigfloat` | `bigfloat_ty` | 可调精度二进制浮点（IEEE 754 推广） | bigint_bin |
| `bigdecimal` | `bigdecimal_ty` | 可调精度十进制浮点（IEEE 754 十进制推广） | bigint_dec |
| `bigcomplex/float` | `bigcomplex_float_ty` | 复数（实虚部为 bigfloat） | bigfloat |
| `bigcomplex/decimal` | `bigcomplex_decimal_ty` | 复数（实虚部为 bigdecimal） | bigdecimal |
| `convert` | `nex_convert_*` | 跨类型转换矩阵（§10） | 全部 |

## 构建与测试

```sh
cmake -S . -B build && cmake --build build      # 库 + 测试 + demo
ctest --test-dir build                          # 全部测试（GCC）
# sanitizer 构建（CI 亦用）：GCC/Clang 用 -DNEX_SANITIZE=address,undefined；
# MSVC 用 -DNEX_SANITIZE=address（MSVC 无 UBSan）
scripts/build-msvc-debug.bat                    # MSVC Debug + CRT 调试堆检查
# 之后：cd build-dbg\tests && runall.bat        # 0 泄漏 / 0 堆损坏验证
./build/demo.exe                                # 能力演示
```

## 安装与下游使用

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build --parallel
cmake --install build                            # 头文件 + 库 + CMake 包 + pkg-config
# 共享库：追加 -DBUILD_SHARED_LIBS=ON（SOVERSION = 主版本号，如 libnex.so.1）
```

下游工程（参见 `examples/downstream/`）：

```cmake
find_package(nex CONFIG REQUIRED)
target_link_libraries(app PRIVATE nex::nex)
```

也可用 pkg-config：`pkg-config --cflags --libs nex`。

测试共 33 项（CTest）：8 组模块单元测试 + vs-double fuzz（bigfloat 与硬件
double/float 逐位对照）+ tie 舍入边界构造 + OOM 注入（12 运算 × 64 注入点，
验证强异常安全）+ 8 组 Python 精确参考黄金对拍 + 多种子。

覆盖率：`sh scripts/coverage.sh [阈值]`（gcovr，默认 80%；CI `linux-coverage`
强制该阈值）。公开 API 审计：`sh scripts/api_audit.sh`（头文件自包含 +
声明/定义一致性；CI 每次运行）。

黄金对拍参考实现位于 `tests/golden_*.py`（`fractions.Fraction` / `decimal` 精确
复刻各类型语义），随机种子可配置：`NEX_GOLDEN_SEED=... ctest -R golden`。

## 性能

Release（-O2）实测节选（Intel i5-13500H / GCC 15.2.0；完整报告与规模扫描见
[`docs/performance.md`](docs/performance.md)）：

| 运算 | 规模 | 耗时 |
|------|------|------|
| `bigint_bin` 乘法 | 10⁴ 位十进制 | 0.32 ms |
| `bigint_bin` 除法 | 10⁴ / 0.5×10⁴ 位 | 0.64 ms |
| `bin→dec` / `dec→bin` | 10⁵ 位十进制 | 49 / 18 ms |
| `bigint_bin` 乘法 | 2¹⁶ 肢（≈ 63 万位） | 97 ms |
| `bigint_bin` 乘法 | 2²⁰ 肢（≈ 1000 万位） | 5.7 s |
| `bigfloat` 万位精度 sqrt | 33220 bit | 22 ms |
| `bigdecimal` 万位精度 sqrt | 10000 位 | 25 ms |

复现：`sh scripts/bench.sh [输出文件] [预算秒]`（Release 构建 + `bench_sweep`
深度扫描；默认输出 `docs/performance.md`）。绝对耗时依赖机器与编译器；
CI 另提供每日基准 artifact（`.github/workflows/benchmark.yml`）。

## 演示（`demo/`）

`demo/demo.c` 演示七大类型：Fibonacci / 阶乘 / 精确有理数 / Gauss-Legendre 计算 π
（binary128 与 decimal128）/ 复数恒等式 / double↔decimal 精确互转。

## 设计要点

- 运算结果**正确舍入**（保护位 + 粘位，无双重舍入）；`to_str` 最短往返
  （bigfloat）或精确展开（bigdecimal）；
- 所有有损转换必须显式传入目标精度上下文（无隐式默认精度）；
- 规范 §3.1 / §11：OOM 时输出参数保持调用前状态（强异常安全）；
- 内部统一分配器 `nex/nex_alloc`（OOM 注入测试入口，未来自定义分配器钩子）。

## 许可

见 [LICENSE](LICENSE)。

## 贡献

欢迎贡献！请先阅读 [`CONTRIBUTING.md`](.github/CONTRIBUTING.md)——它规定了
分支流程（feature → dev → main）与"完成"的定义（单元测试、黄金对拍、边界/
鲁棒测试、文档同步），PR/Issue 请使用仓库内模板。
