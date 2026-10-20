# Numextend

[![CI](https://github.com/MarchBeta2087/numextend/actions/workflows/ci.yml/badge.svg)](https://github.com/MarchBeta2087/numextend/actions/workflows/ci.yml)

任意精度数值计算库（C99，零依赖），覆盖整数、有理数、二进制/十进制浮点与复数，
带类型间精确/有损转换矩阵。设计文档见 [`docs/design.md`](docs/design.md)（§4–§10
为七类数值语义与转换矩阵规范）；待办与路线图见 [`TODO.md`](TODO.md)；
**开发者参考手册见 [`docs/manual.html`](docs/manual.html)**（源码 `docs/manual/`，
由 `sh scripts/docs.sh` 从头文件生成）；版本变更见 [`CHANGELOG.md`](CHANGELOG.md)。

## 模块（`nex/`）

| 模块 | 类型 | 语义 | 构建于 |
|------|------|------|--------|
| `bigint/bin` | `bigint_bin_ty` | 基 2³² 任意精度整数（运算快） | — |
| `bigint/dec` | `bigint_dec_ty` | 基 10⁹ 任意精度整数（十进制 I/O 快） | — |
| `bigint/bin64` | `bigint_bin64_ty` | 基 2⁶⁴ 变体：加/减/乘/除、字符串 I/O、与 bin/dec 互转 | — |
| `bigfrac` | `bigfrac_ty` | 精确有理数（既约分数） | bigint_bin |
| `bigfloat` | `bigfloat_ty` | 可调精度二进制浮点（IEEE 754 推广） | bigint_bin |
| `bigdecimal` | `bigdecimal_ty` | 可调精度十进制浮点（IEEE 754 十进制推广） | bigint_dec |
| `bigcomplex/float` | `bigcomplex_float_ty` | 复数（实虚部为 bigfloat） | bigfloat |
| `bigcomplex/decimal` | `bigcomplex_decimal_ty` | 复数（实虚部为 bigdecimal） | bigdecimal |
| `convert` | `nex_convert_*` | 跨类型转换矩阵（§10） | 全部 |
| `nex/transcend` | `bigfloat_*` / `bigdecimal_*` / `bigcomplex_*` | 正确舍入超越函数：`atan`/`atan2`/`exp`/`log`/`sin`/`cos`/`tan`/`asin`/`acos` 与复数超越（球式 + Ziv，§16）；超大 `|x|` 的三角经 Payne–Hanek 规约（§16.2） | bigfloat, bigdecimal, ball |

## 构建与测试

```sh
cmake -S . -B build && cmake --build build      # 库 + 测试 + demo
ctest --test-dir build                          # 全部测试（GCC）

# 或使用 CMake 预设（需 CMake ≥3.19 + Ninja），产物统一在 build/<preset>：
cmake --preset default && cmake --build build/default
cmake --preset opencl  && cmake --build build/opencl    # 其余见 docs/maintenance.md §8
python3 scripts/clean.py --list                 # 构建/产物清理（默认 dry-run）
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

测试共 42 项（CTest）：22 组模块/功能单元测试 + vs-double fuzz（bigfloat 与
硬件 double/float 逐位对照）+ tie 舍入边界构造 + OOM 注入（12 运算 × 64
注入点，验证强异常安全）+ 16 项 Python/mpmath 精确参考黄金对拍（含多种子）
+ C++ consumer。

覆盖率：`sh scripts/coverage.sh [阈值]`（gcovr，默认 80%；CI `linux-coverage`
强制该阈值）。公开 API 审计：`sh scripts/api_audit.sh`（头文件自包含 +
声明/定义一致性；CI 每次运行）。

黄金对拍参考实现位于 `tests/golden_*.py`（`fractions.Fraction` / `decimal` 精确
复刻各类型语义），随机种子可配置：`NEX_GOLDEN_SEED=... ctest -R golden`。

## 子集构建（可选）

默认构建全部模块。只需部分模块时可经 `NEX_MODULES` 裁剪（按
`cmake/nex_modules.txt` 的依赖闭包自动补齐，文件级子集）：

```sh
cmake -S . -B build -DNEX_MODULES="bigint_bin,bigfrac"   # 只保留整数/有理数
cmake --build build
```

`convert`（跨类型转换）依赖全部类型；未选模块不编译、不安装、也不导出符号。
子集构建不提供完整测试与 ABI 承诺。模块总览（依赖 / 公开函数）见
[`docs/modules.md`](docs/modules.md)；清单与闭包可用
`python scripts/modules.py list` / `closure <mod...>` / `docs <mod...>` 查看。

> **体积说明**：静态链接下 linker 本就只拉取可达的 `.o` 成员，所以子集减小的是
> **归档 / 分发体积与构建面**，最终可执行文件通常**不变**；共享库场景子集可直接
> 减小 `.so`/`.dll`。如需函数级裁剪，可对库加 `-ffunction-sections
> -fdata-sections` 并以 `-Wl,--gc-sections` 链接（ELF 有效；PE/MinGW 基本无效）。

## 单文件组合（可选）

`-DNEX_AMALGAM=ON` 时先由 `scripts/amalgamate.py` 生成 `nex_amalgam.c/.h`
（剥离内部 `#include`、跨文件 `static` 改名），再以单个翻译单元构建库：

```sh
cmake -S . -B build -DNEX_AMALGAM=ON
cmake --build build && ctest --test-dir build   # 全量测试通过 → 与分文件等价
```

也可直接使用生成物：`cc app.c nex_amalgam.c -I<amalgam_dir>`。

## 典型场景（子集 / 单文件）

- **嵌入式 / 固件**：只需整数与有理数 →
  `-DNEX_MODULES="bigint_bin,bigfrac"`，去掉浮点/复数/跨类型转换，缩小归档与
  代码审查面。
- **十进制财务计算**：只需精确十进制 → `-DNEX_MODULES="bigint_dec,bigdecimal"`
  （`bigdecimal` 精确十进制展开与 I/O，无二进制浮点）。
- **专用工具（哈希 / 编码 / 加密）**：只需整数核心 →
  `-DNEX_MODULES="bigint_bin"`（注意整数层因 bin↔conv 依赖环会连带 dec/conv）。
- **无 CMake / 单文件分发**：`python scripts/amalgamate.py --out dist` 得到
  `nex_amalgam.c/.h`，直接 `cc app.c nex_amalgam.c`，或拖入单个工程 / 竞赛 /
  教学示例。完整示例见 [`examples/amalgam/`](examples/amalgam/)。
- **共享库体积敏感**：用子集生成小的 `.so`/`.dll`（共享库不会自动剔除未用
  符号，子集是直接有效手段）。

场景 1–3 可直接用 [`examples/downstream/`](examples/downstream/) 对子集
`find_package` 消费（该示例仅用 `bigint_bin`）；CI `linux-subset-install` 验证
子集安装与消费链路。

## 性能

Release（-O2）实测节选（Intel i5-13500H / GCC 15.2.0；完整报告与规模扫描见
[`docs/performance.md`](docs/performance.md)）：

| 运算 | 规模 | 耗时 |
|------|------|------|
| `bigint_bin` 乘法 | 10⁴ 位十进制 | 0.35 ms |
| `bigint_bin` NTT 乘法 | 32 万 bit | 45.7 ms |
| `bigint_bin` 除法 | 10⁴ 位十进制 | 0.36 ms |
| `bin→dec` / `dec→bin` | 10⁵ 位十进制 | 50 / 18.6 ms |
| `bigfrac` 加法（含 GCD） | 10⁴ 位十进制 | 18.5 ms |
| `bigfloat` 万位精度 mul / div / sqrt | 33220 bit | 0.31 / 1.46 / 22.2 ms |
| `bigdecimal` 万位精度 mul / sqrt | 10000 位 | 1.04 / 26.6 ms |

复现（规模扫描）：`sh scripts/bench.sh [输出文件] [预算秒]`（Release 构建 +
`bench_sweep` 深度扫描；默认输出 `docs/performance.md`）。表中为固定规模冒烟
数据；规模扫描的覆盖范围受预算限制，完整曲线以报告为准。

数学专项基准（`bench_math`，分段可选）：

```sh
sh scripts/bench-math.sh full all 7200     # 全量，预算 2 小时
sh scripts/bench-math.sh quick trans 600   # 只测超越函数，10 分钟
sh scripts/bench-math.sh ci all 300        # CI 缩减档
```

部分：`trans`（所有公开超越函数 × 多种精度）、`const`（π/e/ζ(3)/γ 的多种
表达式 × 多种精度）、`throughput`（各规模运算的 ops/s）。`--json` 输出供
夜间趋势收集。完整结果见 [`docs/bench-math.md`](docs/bench-math.md)。

统一入口（推荐）：`scripts/bench.py` 可任意组合 nex 模块与 `contrib`，并带
时间/内存预算：

```sh
python3 scripts/bench.py --list                        # suite 清单
python3 scripts/bench.py --profile ci                  # CI 档，约 2–5 分钟
python3 scripts/bench.py --modules bigint_bin,ntt      # 只跑相关模块
python3 scripts/bench.py --all --include-frozen --profile quick   # 本地 20 分钟
```

详见 [`docs/benchmarking.md`](docs/benchmarking.md) 与自动生成的
[`docs/benchmark-suites.md`](docs/benchmark-suites.md)。

本机 full 档实测（WSL2 / Ubuntu 24.04 / GCC 13.3.0 / i5-13500H，-O2；
预算 7200 s；v1.7 加入 32768 bit 吞吐档后约 98 min，此前为 96.5 min）：

| 运算 | 精度 | 耗时 |
|------|------|------|
| `bigfloat_exp` | 1024 / 4096 / 16384 bit | 2.2 / 57 / 1289 ms |
| `bigfloat_log` | 1024 / 4096 / 16384 bit | 10.3 / 298 / 7599 ms |
| `bigfloat_sin` | 1024 / 4096 / 16384 bit | 3.0 / 81 / 1660 ms |
| π（`4·atan(1)`）/ Machin | 4096 bit | 189 / 131 ms |
| e（`exp(1)`）/ 指数级数 | 4096 bit | 75 / 45 ms |
| ζ(3)（Apéry）/ γ（Brent–McMillan） | 4096 bit | 89 / 510 ms |
| `bigfloat_log` | 65536 bit | 221 s |

绝对耗时依赖机器与编译器；CI 另提供每日基准 artifact
（`.github/workflows/benchmark.yml`）。

## 演示（`demo/`）

`demo/demo.c` 演示七大类型：Fibonacci / 阶乘 / 精确有理数 / Gauss-Legendre 计算 π
（binary128 与 decimal128）/ 复数恒等式 / double↔decimal 精确互转。

## 设计要点

- 运算结果**正确舍入**（保护位 + 粘位，无双重舍入）；超越函数（`atan`/`atan2`/`exp`/
  `log`/`sin`/`cos`/`tan`/`asin`/`acos` 及复数超越）同样正确舍入（球式严谨
  误差界 + Ziv，§16）；`to_str` 最短往返（bigfloat）或精确展开（bigdecimal）；
- 所有有损转换必须显式传入目标精度上下文（无隐式默认精度）；
- 规范 §3.1 / §11：OOM 时输出参数保持调用前状态（强异常安全）；
- 内部统一分配器 `nex/nex_alloc`（OOM 注入测试入口，未来自定义分配器钩子）。

## 兼容性

| 语言 | 支持范围 | 说明 |
|------|----------|------|
| C | C99 为承诺基准；C11 / C17 / C23（及 C2y）实测通过 | 默认 `NEX_C_STANDARD=99`；可用 `-DNEX_C_STANDARD=11/17/23` 验证新标准 |
| C++ | C++11 / C++14 / C++17 / C++20 / C++23 / C++26 | 公开头文件均带 `extern "C"`，可直接 `find_package(nex)` 后以 C ABI 链接，无需手动包装 |

CI 覆盖 C11/C17/C23 与 C++11/C17/C23 矩阵（见 `.github/workflows/ci.yml`）；
[`tests/test_cpp_consumer.cpp`](tests/test_cpp_consumer.cpp) 是 C++ 互操作回归测试。

## 许可

见 [LICENSE](LICENSE)。

## 贡献

欢迎贡献！请先阅读 [`CONTRIBUTING.md`](.github/CONTRIBUTING.md)——它规定了
分支流程（feature → dev → main）与"完成"的定义（单元测试、黄金对拍、边界/
鲁棒测试、文档同步），PR/Issue 请使用仓库内模板。

参与本项目即表示同意遵守[行为准则](CODE_OF_CONDUCT.md)；安全漏洞请勿公开
提交 issue，按[安全政策](SECURITY.md)私密报告。
