# numextend 开发备忘录（2026-08-13 夜）

> 供次日醒来后快速接续。分支已推送远端，未合并。

---

## 1. Git 状态

- **当前分支**：`feature/dec-toom3-recdiv`
- **远端**：`git@github.com:MarchBeta2087/numextend.git`（已推送）
- **本分支新增提交**（`7f0b28d..812cd0a`，共 4 个，全部在远端）：

| 提交 | 内容 |
|------|------|
| `7f0b28d` | dec 侧 Toom-3 落地；BZ 递归除法实现（当时因稀疏除数缺陷**暂不自动分派**） |
| `22b741a` | **BZ 缺陷定位并修复，恢复自动分派**（v0.16） |
| `190d339` | Newton 开方落地；Newton 除法实验路径（v0.16 续） |
| `812cd0a` | Montgomery 模幂（实测无收益，条件编译隐藏）；bin64 除法落地（v0.16 续） |

- **待办**：天亮后用户合并本分支（GitHub PR）→ 本地同步 `dev`。

---

## 2. 本次会话三条工作完成情况

### 2.1 BZ 稀疏除数缺陷（✅ 已修复并启用）

- **根因**（系统性二分调试定位，精确到一行）：
  `bz_mag_cmp` 对未规范化视图（前导零）按**长度**比较。稀疏除数（如 2^17000+1）移位后中间全零 → a12 视图顶肢为 0 但实际值 > b → 误判走 3a 分支 → 破坏 d2n1n2 前提（a12 < b1·B^n）→ 商溢出截断、余数下溢为全 1。
- **修复**：`bz_mag_cmp` 比较前先裁剪前导零。
- **性能优化**：商累加改块直写（预分配 (t−1)n+1 肢，每块 memcpy；qi 为罕见 n+1 肢时回退累加）。a/b=256（除数 1000 肢）**830ms vs Knuth 1059ms（快 22%）**；a/b=64 快 7%。
- **验证**：稀疏/全 1/2 的幂/负数/非平衡随机黄金对拍全过；GCC 33/33 + MSVC 32/32。
- **教训**：视图（未规范化）上的长度比较是反复出现的坑；临时 `test_mag_view` 曾误判 mag_divmod 有 bug（实为测试未规范化比较的误报，mag_divmod 本身正确）。

### 2.2 Newton 迭代除法/开方（✅ 开方落地；除法为实验路径）

- **开方** `bigint_bin_sqrt`：整数平方根（向下取整），Newton 迭代 x ← (x + a/x) >> 1，初值 2^ceil(blen/2)。负数返回 INVALID。golden driver 新增 `sqrt` 命令；单元测试覆盖完美平方/2 的幂边界/大数不变量 s² ≤ a < (s+1)²；Python `math.isqrt` 对拍 29 例全过（含 8 万 bit）。
- **除法** `newton_div_rem`（`NEX_DIV_NEWTON_PATH` 条件编译，默认关闭）：
  - 整数倒数迭代 v ← v + floor(v·(B^t − bs·v)/B^t)，t = m+n（商肢数+除数肢数）使单次商估计误差 < 1，双向修正。
  - 调试中修复两个 bug：① 负除数符号（shl 后 bs 保留 NEG 导致 t 为负、d 巨大、v 过冲死循环）→ 强制幅值符号；② v 精度只 2n 时商误差可达 123 肢 → 精度提到 m+n。
  - **性能结论**：全精度迭代效率不如 BZ（未用截断乘法），不参与 AUTO。未来可用截断乘法（GMP 风格）优化后再评估。

### 2.3 Montgomery 模幂 + bin64 扩展（✅ 完成；半 gcd 未做）

- **Montgomery 模幂**（`NEX_POW_MOD_MONT` 条件编译，默认关闭）：
  - REDC 逐肢归约（m' = −m⁻¹ mod 2^32，Newton 迭代 5 次）+ 平方-乘在 Montgomery 域。
  - 正确性经黄金对拍验证（RSA 风格大模数 + 随机）。
  - **实测无收益**：模数 1024/2048 bit 时比标准路径（mul + Knuth D）慢 ~5%（REDC 朴素 n² 不如已优化的 Knuth D）。按基准驱动决策不启用——与 FFT、decimal 快速乘法同模式的**负面结论**，已记录于设计文档 v0.16。
- **bin64 除法** `bigint_bin64_div_rem`：转换包装复用 32 位肢 bin 除法（拆/合 limb → `bigint_bin_div_rem`，自动获得 BZ + Knuth D）。测试覆盖基本/除零/|lhs|<|rhs|/负数截断 + LCG 随机不变量（商与已知构造值完全相等）。
- **半 gcd 未实现**：续分式矩阵递归的符号/界验证复杂、现有 Lehmer gcd 已落地（1024 肢 5.2ms vs 二进制 34ms）性能充分、无迫切场景。文档 §13 #2 保持为后续方向。

---

## 3. 设计文档版本

- `docs/design.md` **v0.16（草案）**，修订记录含 v0.14（Toom-3+Lehmer）、v0.15（dec Toom-3 + BZ 初版）、v0.16（BZ 修复 + Newton + Montgomery/bin64）。
- 本次三个负面/实验决策均已文档化：BZ 稀疏缺陷根因、Newton 除法全精度慢、Montgomery 无收益。

---

## 4. 次日接续计划（建议顺序）

1. **合并** `feature/dec-toom3-recdiv`（GitHub PR；用户操作）→ 本地 `dev` 同步。
2. 可选验证：合并后重跑 `ctest --test-dir build`（GCC）+ `build-dbg`（MSVC）确认无回归。
3. 后续方向（§13 路线图）：
   - **半 gcd**（若要做：参考 Stehlé-Zimmermann hgcd / GMP hgcd2；建议先写 Python 参考实现对拍）
   - bin64 字符串 I/O 与互转（from_str/to_str）
   - dec 侧 BZ/Newton 除法
   - FFT SIMD、Ryū 十进制输出、超越函数
4. 注意：`NEX_DIV_NEWTON_PATH` 与 `NEX_POW_MOD_MONT` 是两个默认关闭的条件编译开关（代码保留、不参与 AUTO）。

---

## 5. 环境速查

- **MinGW GCC**（不在 PATH，必须全路径）：
  `C:/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r1/mingw64/bin/gcc.exe`
- **构建**：`build/`（ninja + GCC，`cmake --build build`）；`build-dbg/`（MSVC Debug，`cmake --build build-dbg`）
- **测试**：`ctest --test-dir build`（33 项）；MSVC：`build-dbg` 下 `ctest -C Debug`（32 项）
- **MSVC ASan**：手动 `build/asan/asan_test.bat`（需 vcvars64，cl 编译 + /fsanitize=address）
- **黄金对拍**：`tests/golden_driver.c` + Python 脚本（`golden_bigint_bin.py` 等）；驱动输入缓冲已扩至 1M（大数用）
- **大数 Python 注意**：`sys.set_int_max_str_digits(400000)` 在超长整数前设置
- **性能基线**（-O2）：BZ a/b=256 830ms vs Knuth 1059ms；Lehmer gcd 1024 肢 5.2ms；bin→dec 十万位 58.8ms；万位 dec mul 0.35ms

---

## 6. 过程教训（已总结，供回顾）

- **值相关缺陷随机测试是盲区**：稀疏值（2^k±1）/10 的幂等定向用例 + 黄金对拍（外部参考）是发现关键。
- **未规范化视图比较是反复出现的 bug 模式**（`sign` 不设、视图未裁剪、长度比较误判）。
- **调试工具链**：C 内嵌 NEX_BZ_DBG 打印 + Python 镜像参考实现逐层对拍 + MSVC ASan 定位越界，是本次 BZ 缺陷定位的三大支柱。
- **heredoc 写补丁脚本的中文/反斜杠转义问题反复踩坑**：优先用 `write` 工具写脚本文件（再执行），避免 bash heredoc 处理 `\n`/`\u` 转义。
