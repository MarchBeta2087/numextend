# TODO（v1.0 必要工作）

> **活文档**：只记录"发布 v1.0 前**必须**完成的"工作。性能优化、功能扩展等
> 非必要方向不作承诺，集中列在文末并指向 `docs/design.md` §13（完整路线图）。
> 最后更新：2026-10-13（v1.0 筹备中；dev @ `60a7410`）

---

## A. API 契约与版本化

- [x] `project(numextend VERSION 1.0.0)`，并生成 `nex_version.h`（`NEX_VERSION_*`）；
      安装到用户可见位置随 B1 一并做。
- [x] 稳定性政策写入 `docs/design.md` §1.4：1.x API 只增不改、同主版本 ABI 稳定
      （SONAME）、公开结构体布局冻结。
- [ ] 公开头文件逐符号审计：语义、错误码、别名规则、复杂度、OOM、线程安全。
- [x] `bigcomplex_*_arg` 移出 v1 公开面（依赖 `atan`，作为 1.x 增量）。

## B. 可消费性 / 打包

- [ ] CMake `install()` + `export()`，支持 `find_package(nex CONFIG)`
      （当前只能源码内嵌 `add_subdirectory`）。
- [ ] 可选 `BUILD_SHARED_LIBS` 构建；MSVC 导出符号（当前仅 STATIC）。
- [ ] 一个**独立下游示例工程**，证明安装后可被消费。
- [ ] 打 tag `v1.0.0` + GitHub Release 说明 + `CHANGELOG.md`。

## C. 正确性与可移植性证据

- [ ] CI 矩阵：{GCC, Clang} × {Linux, macOS} + MSVC，`-O0` 与 `-O2` 都测。
- [ ] **ASan + UBSan job**（把 `MEMO.md` 里的手动 MSVC ASan 固化进 CI）。
- [ ] MSVC 全量测试与 GCC 对齐（确认 golden 注册、`ctest -C Release`）。
- [ ] 覆盖率门槛（`scripts/coverage.sh` 已有）。
- [ ] 端序：显式声明"仅小端"，或补大端正确性验证。

## D. 文档与发布

- [ ] `docs/design.md` 去掉"草案"标记、定稿 v1.0、修订记录收口。
- [ ] 集中一节"已知限制与非目标"（无 SIMD/GPU/线程、无次正规、超越函数仅 sqrt…）。
- [ ] `README.md` 面向使用者（安装、最小示例、能力矩阵）。
- [ ] 每个公开运算的 -O2 性能基线写入文档。

---

## 已否定（不实现，理由见设计文档修订记录）

标量浮点 FFT 接入 AUTO（v0.10/v0.17）· 十进制快速乘法（v0.13）·
Montgomery 模幂（v0.16）· Newton 整数除法（v0.16）· NTT 三模数（v0.9）·
十进制 FFT 每肢 4 节 × 256 方案（v0.7）。

## 已知技术债 / 风险

- **构建优化级别**：默认曾为 -O0（无 `CMAKE_BUILD_TYPE`）→ 已修（PR #13）；
  基准必须与基线同优化级别，否则算法交叉点会颠倒。
- **16-bit FFT 舍入安全界**：min_len ≈ 2048 附近逼近 0.5（3072 肢实测出错）；
  8-bit 节已验证至 65536 肢安全。
- **`bigint_dec` 无亚二次除法**：仅性能问题，不影响语义正确性。
- **未规范化视图比较**是反复出现的 bug 模式（BZ 缺陷根因）。
- **值相关缺陷**：稀疏值（2^k±1）、10 的幂等需定向用例 + 外部黄金对拍。

## 非必要方向（不承诺 v1.0，完整见 `docs/design.md` §13）

- **性能**：半 gcd、dec 侧亚二次除法、浮点 FFT 的 SIMD 与 AUTO、Ryū 式十进制
  输出、NTT/Toom 阈值微调、`bigint_bin_sqrt` 提速。
- **功能**：bin64 字符串 I/O 与互转、次正规浮点数、自定义分配器钩子、
  超越函数（exp/log/sin/cos/atan）、复数 Karatsuba 三乘式、Smith 稳定除法。

---

## 流程与约定

- **分支**：`feature/*` / `chore/*` → PR → `dev` → `main`；PR 触发三平台 CI
  （linux `Release` / macos `Debug` / windows `Release`）。
- **性能**：用 -O2 构建；`bench_sweep`（交错采样）做规模扫描。
- **测试**：`ctest --test-dir build`（33 项，含 Python 黄金对拍）。
- **文档**：算法与决策入 `docs/design.md`；本文件只维护"必要工作"状态。
