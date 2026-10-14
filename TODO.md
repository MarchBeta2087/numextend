# TODO（v1.1 及以后路线图）

> **活文档**：v1.0.0（2026-10-14）与 v1.1.0（C++ 互操作 + C 标准矩阵）已落地，
> 本文件记录 **v1.1 之后的规划**。只收录已评估、可执行的方向；未决/否定的候选
> 集中列在文末。优先级见 §0。
> 最后更新：2026-10-14（v1.1.0：§A 完成）

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

### B1 模块依赖清单（机器可读）

- [ ] 建立 `tools/modules.toml`（或 JSON）：每个源文件/头文件的**公开符号**与
      **内部依赖**，作为子集与文档生成的单一来源。
- [ ] 脚本校验：每个模块的依赖闭包完整、无悬空符号。
- 依据：`nex/bigint/bin`、`nex/bigint/dec`、`nex/fft`、`nex/ntt` 为叶子；
  `bigfrac`/`bigfloat`→`bigint_bin`，`bigdecimal`→`bigint_dec`，
  `bigcomplex`→`bigfloat`/`bigdecimal`，`convert`→几乎全部。

### B2 文件级子集（一期）

- [ ] CMake 选项 `-DNEX_MODULES=bigint_bin,bigfloat,...`，自动带入依赖闭包，
      只编译选中模块并只导出其符号。
- [ ] 可选：`NEX_MINIMAL` 预设（仅 `bigint_bin` + `nex_alloc`）。
- 验收：至少一个真子集构建通过其自身的 CTest 子集；`api_audit.sh` 对子集重跑。

### B3 函数级子集（二期，需评审）

- [ ] 基于 B1 做**调用图闭包**裁剪；处理跨模块依赖（如
      `bigcomplex_*_abs`→`sqrt`，删 `sqrt` 必须拒绝或连带裁剪）。
- [ ] 复用现有条件编译先例：`NEX_DIV_NEWTON_PATH`、`NEX_POW_MOD_MONT`、
      `NEX_BIN64_FORCE_PORTABLE`。
- 风险/门槛：函数级 ABI 与 §1.4 稳定性政策的关系须先写明；维护成本高，
  未定案前不进入实现。

### B4 单文件组合（amalgamation）

- [ ] 生成 `nex_amalgam.c` / `nex_amalgam.h`（单 TU）。
- **已知硬障碍（实测）**：202 个 `static` 定义、141 个唯一名、**44 个跨文件重名**
  （`valid_ctx`×4、`mag_add`/`mag_sub`×4、`mul_schoolbook`、`map_bigint_err` …）。
  单 TU 直接拼接会「重复定义」。
- [ ] 必备步骤：`static` 符号改名（按文件名加前缀）、内部 `#include` 剥离、
      拼接拓扑排序、宏/`static const` 表冲突检测、`NEX_*` 配置宏透传。
- [ ] 可选单头（STB `NEX_IMPLEMENTATION` 风格）由 amalgamation 派生；先评估
      是否值得改变消费模型。
- 验收：amalgam 构建产物通过**全量** CTest（与分文件构建等价）。

### B5 子集/合并文档生成

- [ ] 扩展 `scripts/api_symbols.py` / `scripts/gen_api_doc.py`：输出子集库的
      **保留函数/文件**与**移除函数/文件**清单。
- [ ] CI 校验生成物与实现同步（`git diff --exit-code`，沿用 `docs` job 模式）。

---

## C. P2 秦九韶 / 幂级数 / Padé 基础设施（目标 v1.3）

- [ ] **C1 秦九韶（Horner）多项式求值**
  - 覆盖 `bigint_bin`/`bigfrac`/`bigfloat`/`bigdecimal`；统一误差/舍入协定。
- [ ] **C2 幂级数**（截断阶数 + 严谨误差界；`bigfrac` 先做精确系数版）。
- [ ] **C3 Padé 逼近**（有理函数逼近，作为 `exp`/`atan` 等的可选核）。
- [ ] **C4 外部高精度黄金参考**：测试期引入 `mpmath`（或 MPFR），
      运行期保持零依赖；扩展 `tests/golden_*.py` 框架。
- [ ] **C5 规范**：`docs/design.md` 新增「多项式与级数」章节，明确正确舍入与
      误差界要求。

---

## D. P3 超越函数（`design.md` §13 #8 落地，分阶段）

> 前置：C1–C5。每阶段须有**误差分析 + 外部黄金 + 定向边界用例**；
> `bigfloat` 与 `bigdecimal` 的规约各自实现。

- [ ] **D1 `atan`**：落地后重新引入复数辐角 `bigcomplex_*_arg`（v1 已移出公开面）。
- [ ] **D2 `exp` / `log` / `ln`**（二进制 + 十进制各一套）。
- [ ] **D3 `sin` / `cos` / `tan`**：含**大参数 Payne–Hanek 规约**（需高精度 π）。
- [ ] **D4 `asin` / `acos` / `atan`**。
- [ ] **D5 复数超越函数**（`bigcomplex_float` / `bigcomplex_decimal`）。
- [ ] **D6 正确舍入**：保护位 + 粘位、无双重舍入；补 §7/§9 语义规范。

---

## E. P4 无穷列表（决策先行）

- [ ] **E1 设计决策**：显式迭代器 / 回调 vs 惰性求值抽象。
  - 结论倾向：C99 无生成器/协程，惰性求值（迭代式）需状态机、`setjmp/longjmp`
    或线程，与本库「显式 `init`/`free` 句柄」风格及规范 §13 冲突；
    **不将惰性无穷列表作为公开抽象**，仅允许作为内部实现细节。
- [ ] **E2 若保留**：非迭代式（闭式/记忆化）＋ 子列表截取优先；迭代式最后评估。
- [ ] **E3 决策记录**：无论采纳或否定，均写入 `docs/design.md` 修订记录。

---

## F. X 跨平台多线程封装（隔离评估，默认不进核心）

- [ ] **F1 决策**：作为**独立可选模块或独立仓库**，不进入核心 `libnex` ABI。
  理由：§14 将「内部多线程」列为非目标，§3.3 承诺「库内部不加锁、线程安全由
  调用方保证」；纳入核心会破坏契约并引入锁序登记（§13.2 目前为空）。
- [ ] **F2 若推进**：`pthread` + Win32 双后端（不依赖 C11 `<threads.h>`：
  MSVC 支持不全、MinGW 视 winpthreads），CMake `find_package(Threads)`。
- [ ] **F3 CI**：新增 TSan job（**与 ASan job 分开**；MSVC 无 TSan）。
- [ ] **F4 确定性**：并行归约的求和/舍入顺序需可复现；收益须实测
  （NTT 仅 ≥ 16384 肢启用，并行前先量化收益）。

---

## G. 保留：已知技术债 / 风险（沿用 v1.0）

- **构建优化级别**：默认 `-O0` 曾被修复；基准必须与基线同优化级别，
  否则算法交叉点会颠倒。
- **16-bit FFT 舍入安全界**：min_len ≈ 2048 附近逼近 0.5（3072 肢实测出错）；
  8-bit 节已验证至 65536 肢安全。
- **`bigint_dec` 无亚二次除法**：仅性能问题，不影响语义正确性。
- **未规范化视图比较**是反复出现的 bug 模式（BZ 缺陷根因）。
- **值相关缺陷**：稀疏值（2^k±1）、10 的幂等需定向用例 + 外部黄金对拍。
- **大端平台 / 32 位平台**：未验证（§1.3、§14）。

---

## H. 已否定 / 非目标

**已否定（不实现，理由见设计文档修订记录）：**

标量浮点 FFT 接入 AUTO（v0.10/v0.17）· 十进制快速乘法（v0.13）·
Montgomery 模幂（v0.16）· Newton 整数除法（v0.16）· NTT 三模数（v0.9）·
十进制 FFT 每肢 4 节 × 256 方案（v0.7）。

**非目标（除非 §F 明确独立化）：**

SIMD / GPU / 内部多线程 · 超越函数完整正确舍入前的中间态发布 ·
次正规浮点（§7.3）· 公开惰性无穷列表抽象（§E）。

---

## 流程与约定

- **分支**：`feature/*` / `chore/*` → PR → `dev` → `main`；PR 触发多平台 CI。
- **性能**：用 `-O2` 构建；`bench_sweep`（交错采样）做规模扫描。
- **测试**：`ctest --test-dir build`（当前 33 项，含 Python 黄金对拍）。
- **文档**：算法与决策入 `docs/design.md`；API 手册由 `sh scripts/docs.sh` 生成
  （API 页取自头文件 + Pandoc 合成 `docs/manual.html`），手写页随能力更新；
  发版前按 `docs/maintenance.md` §4.1 清单同步。本文件只维护规划状态与优先级。
- **验收门槛**：每个 P1+ 方向落地时同步补 §A 提及的 CI/文档/黄金对拍。
