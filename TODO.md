# TODO（路线图与待办）

> **活文档**：只记录「做什么 / 优先级 / 状态」；算法细节与决策依据见 `docs/design.md`
> （§13 为路线图）。完成项打勾并注明版本；被否定的方向保留在「已否定」以免重复评估。
>
> 最后更新：2026-08-14（v0.17，dev @ `44d8d85`）

## 图例

- **P0** 近期应做（有明确收益或阻塞后续）
- **P1** 计划内
- **P2** 视需求 / 锦上添花
- `[ ]` 待办 · `[x]` 完成

---

## P0

- [ ] **半 gcd（half-gcd）** — 大数 gcd 仍为 O(n²)（-O2：8192 肢 ≈ 0.36 s；`bigfrac` 约分同受影响）。
  参考 Stehlé–Zimmermann hgcd / GMP `hgcd2`；建议先写 Python 参考实现对拍。设计 §4.3、§13 #2。
- [ ] **dec 侧亚二次除法**（Burnikel–Ziegler 或 Newton）— `bigint_dec` 目前仅 Knuth D（O(n²)）；
  `bigdecimal_div` 与 `nex_dec_isqrt`（现已转二进制）等受益。设计 §4.3、§13。
- [ ] **bin64 字符串 I/O 与互转** — `bigint_bin64` 仅有 `from_u64`/`to_u64`；
  缺 `from_str`/`to_str`（base 10/16）与 bin↔bin64、bin64↔dec 互转。设计 §13 #3。

## P1

- [ ] **浮点 FFT 的 SIMD 化与 AUTO 启用再评估** — 标量 double 下 8/16-bit 节均不优于
  Toom-3/NTT（v0.10 结论，v0.17 -O2 复测复核）；SIMD 化后需重新标定阈值。设计 §4.3、§13 #9。
- [ ] **十进制输出 Ryū 式快速算法** — `bigfloat`/`bigdecimal` 的 `to_str` 现走「逐步试转回」
  往返法，性能可优化。设计 §7.5、§13 #4。
- [ ] **次正规浮点数支持** — 需放松「最高位恒 1」不变式，影响面大，单独设计评审。设计 §7.3、§13 #5。
- [ ] **自定义分配器钩子** — 现固定 `nex_malloc`/`nex_realloc`/`free`；提供可选 allocator 钩子。设计 §13 #6。

## P2

- [ ] **复数 Karatsuba 三乘式** — 复数乘法现为 4 乘。设计 §13 #7。
- [ ] **Smith 稳定除法** — `bigcomplex` 除法的缩放稳定性。设计 §9.3、§13 #7。
- [ ] **超越函数** — `exp`/`log`/`sin`/`cos`/`atan`；`atan` 落地后解锁
  `bigcomplex_float_arg` / `bigcomplex_decimal_arg`（现返回 `UNSUPPORTED`）。设计 §13 #8。
- [ ] **bin 侧 NTT/Toom 阈值精调** — 边界 ~16384 肢附近 Toom 略优于 NTT（-O2 ≈ 19.9 vs 20.9 ms，
  差 ~5%）；可细扫后微调 `NEX_MUL_NTT_CUTOFF`。设计 §4.3。
- [ ] **`bigint_bin_sqrt` 提速** — 现每轮 Newton 迭代用全精度除法；可改截断/递推降低复杂度
  （当前万位精度 ≈ 22 ms，可接受）。设计 §7.5。

---

## 已否定（保留理由，勿重复评估）

- [x] **标量浮点 FFT 接入 AUTO** — -O2 下 8-bit 节全尺寸不占优、16-bit 节精度不安全
  （min_len 3072 肢即出错）。v0.10、v0.17。
- [x] **十进制快速乘法（转 bin→NTT→转回）** — 1 万..50 万位全面落后 4–7×。v0.13。
- [x] **Montgomery 模幂** — 实测无收益（比标准路径慢 ~5%）；`NEX_POW_MOD_MONT` 条件编译隐藏。v0.16。
- [x] **Newton 整数除法** — 全精度迭代不如 BZ；`NEX_DIV_NEWTON_PATH` 条件编译隐藏。v0.16。
- [x] **NTT 三模数** — 双模数已覆盖 n ≤ 2^27 肢（512 Mbit），无使用场景。v0.9。
- [x] **十进制 FFT 每肢 4 节 × 256 方案** — 需基数转换且最高节非均匀，无收益。v0.7。

---

## 已知技术债 / 风险

- **构建优化级别**：`build/` 默认曾为 -O0（无 `CMAKE_BUILD_TYPE`）→ 已修（`CMakeLists.txt`
  默认 `RelWithDebInfo`，见 PR #13）。**基准必须与基线同优化级别**，否则算法交叉点会颠倒
  （FFT 复测曾被 -O0 误导）。
- **16-bit FFT 舍入安全界**：min_len ≈ 2048 附近逼近 0.5（3072 肢实测出错）；
  8-bit 节已验证至 65536 肢安全。
- **`bigint_dec` 无亚二次除法**：`bigdecimal` sqrt 依赖 `bigint_conv` 转二进制复用 bin Newton。
- **未规范化视图比较**是反复出现的 bug 模式（BZ 缺陷根因：对前导零视图按长度比较）。
- **值相关缺陷**：稀疏值（2^k±1）、10 的幂等需定向用例 + 外部（Python / GMP）黄金对拍。

---

## 流程与约定

- **分支**：`feature/*` / `chore/*` → PR → `dev` → `main`；PR 触发三平台 CI
  （linux `Release` / macos `Debug` / windows `Release`）。
- **性能**：用 -O2 构建（默认 `build/` 或 `build-rel/`）；`bench_sweep`（交错采样）做规模扫描。
- **测试**：`ctest --test-dir build`（33 项，含 Python 黄金对拍）。
- **文档**：算法与决策入 `docs/design.md`（含版本修订记录）；本文件只维护状态与优先级。
