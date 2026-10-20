# contrib/nex_ssa：Schönhage–Strassen 原型（P1b-SSA，issue #102）

M1 原型已可运行：10⁶–10⁸ 位十进制乘法，与 NTT 逐位对拍通过（≤3×10⁶ 肢）。

## 算法

- `N` bit → `L = 2^k`，块数 `n = L/2`，块 bit `m = L/2`，`L²/4 ≥ N`。
- 环 1：`M1 = 2^L + 1`，根 `4`（阶 `L`）。
- 环 2：`M2 = 2^(L/2) + 1`，根 `2`（阶 `L`）。
  - 不用 `2^L − 1`：其 DFT 存在零因子（`1 − 2^(L/2)` 与模不互素），
    变换不可逆；`2^(L/2)+1` 与 `M1` 互素且 DFT 可逆。
- 两个环上做长度 `L` 的循环卷积，CRT 恢复精确 `c_l`：
  `M1·M2 ≈ 2^(1.5L) > n·2^(2m) = 2^(L+k−1)`。
- 按 base `2^m` 进位合并；环算术复用 `bigint_bin`。

## 参数/内存模型

`python3 scripts/ssa-parameters.py`（标准 3 数组模型，偏保守）：

| 十进制位 | L | 环 bit | 单数组 | 3 数组 |
|---:|---:|---:|---:|---:|
| 10⁸ | 65536 | 65536 | 512 MB | 1.7 GB |
| 10⁹ | 131072 | 131072 | 2.0 GB | 7.5 GB |
| 10¹⁰ | 524288 | 524288 | 32 GB | **111.5 GB** |

10¹⁰ 位即使 SSA 也需 ~100 GB（本原型双环峰值同量级），34 GB 不够；
需要 SSA + out-of-core 或 ≥128–256 GB。10¹² 位仍远超单机。

## 实测（Windows i5-13500H，Release/-O2，单线程）

| 肢数 | bit | 十进制位（约） | 时间 | 峰值 RSS |
|---:|---:|---:|---:|---:|
| 100,000 | 3.2e6 | 9.6e5 | 0.9–1.0 s | 12.5 MB |
| 1,000,000 | 3.2e7 | 9.6e6 | 36–74 s | 115 MB |
| 3,000,000 | 9.6e7 | 2.9e7 | 220–328 s | 424 MB |
| 10,400,000 | 3.33e8 | **1.0e8** | **1142–1318 s** | **1.66 GB** |

- 正确性：`nex_ssa_test` 覆盖 10–100,000 肢；另在 1,000,000 / 3,000,000 肢
  与 `bigint_bin_mul`（NTT）逐位比对通过。
- 对照：1e8 位直接 NTT 约 195.8 s / 930 MB；本原型慢约 5.8×，但验证了
  SSA 路径与内存趋势。后续优化方向：环内乘法批量/缓存 twiddle、减少
  bigint 分配、递归环乘法/并行。

## 构建

```sh
cmake -S . -B build -DNEX_ENABLE_SSA=ON -DNEX_MODULES=all -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/contrib/nex_ssa/nex_ssa_test
./build/contrib/nex_ssa/nex_ssa_bench 1000000
```

默认 `NEX_ENABLE_SSA=OFF`，不进核心 `bigint_bin` AUTO / amalgam / 子集。

## M1 结论

- 1e6–1e8 位 SSA 原型跑通；1e8 位 19–22 min / 1.66 GB。
- 10¹⁰ 位仍受内存限制（双环 ~100 GB），需 out-of-core。

## 决策：M2 / M3 不做（2026-10-20）

issue #102 停在 M1（原型存档）。理由与 out-of-core 一致：

- **1e9 位已被覆盖**：分块 NTT 2403 s / 6.24 GB、OOC 111 min / 2.7 GB。
- **M2 的目标没有增量**：原定 RSS <16 GB（比分块 NTT 实测 6.24 GB 更差）、
  时间「不劣于 2403 s」（只是打平）；而 M1 在 1e8 位已慢 5.8×，达标
  约需 6× 提速。
- **M3（10¹⁰ 位）**的前提（1e10 位有需求）已被否决，重启条件跟随 OOC
  的 1e10 决策（见 `contrib/nex_ooc/README.md` 与 `TODO.md` §K2）。

代码保留在 `contrib/nex_ssa`，默认 OFF，不排期。
