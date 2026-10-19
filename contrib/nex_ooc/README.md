# contrib/nex_ooc：out-of-core 分块乘法原型（P1b-OOC，issue #104）

默认不构建；由 `-DNEX_ENABLE_BIGNUM=ON` 一并构建（与 `nex_bigint_block` 同组），
不进核心 `libnex`、`nex_modules.txt`、子集闭包与 amalgam。

```bash
cmake -S . -B build-bignum -DNEX_ENABLE_BIGNUM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-bignum -j
ctest --test-dir build-bignum -R nex_ooc_test --output-on-failure

# 10 亿位 × 小乘数，并用 schoolbook 参考逐位校验（峰值 RSS ≈ 164 MB）
./build-bignum/contrib/nex_ooc/nex_ooc_bench 1e9 23 --small 3

# 中小规模「大 × 大」，与内存内 AUTO（CRT NTT）逐位比对
./build-bignum/contrib/nex_ooc/nex_ooc_bench 1e7 17 --verify-direct
```

## 算法

操作数与结果都是**纯肢体文件**（小端 32-bit 肢序列，无头），块长 `L`：

```
a = Σ a_i·B^(iL)，b = Σ b_j·B^(jL)，B = 2^32
a·b = Σ_k B^(kL)·( Σ_{i+j=k} a_i·b_j )
```

按 `k` 升序生成输出块：块 `k` 的累加器只需 `2L+2` 肢，低 `L` 肢立即写盘，
高位（≤ `L+2` 肢）作为进位进入块 `k+1`。**峰值内存 O(L)，与操作数规模无关**。

- 块对乘法直接复用核心 AUTO 分派（块内为 Karatsuba / Toom-3 / CRT NTT）；
- 结果按「最后一个非零肢」裁剪（规范化），符号与零由 `nex_ooc_bin_mul`
  按常规规则处理；
- 只做顺序 I/O：每个 `a_i`/`b_j` 块被重复读取，读放大 = `min(na,nb)`。

```c
nex_ooc_err_ty nex_ooc_mul_files(const char *dst_path,
        const char *a_path, size_t a_limbs,
        const char *b_path, size_t b_limbs,
        const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats);
nex_ooc_err_ty nex_ooc_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs,
        const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats);
```

`opts.block_limbs`（默认 `2^23` 肢 = 32 MB）、`opts.scratch_dir`、
`opts.keep_temps`；`stats` 给出块数、块对数、磁盘读/写字节、结果肢数与耗时。

基准的两个校验开关：`--small k`（`b = k`，用分块 schoolbook 逐位比对，
内存 O(chunk)，可跑到 1e9 位）、`--verify-direct`（内存内完整乘一遍再逐块
比对，需容纳完整乘积：1e8 位约 1 GB、1e9 位约 5–7 GB）。

## 实测（Windows i5-13500H，Release/-O2，单线程）

| 规模 | 块长 | 块对 | 耗时 | 峰值 RSS | 校验 |
|---|---:|---:|---:|---:|---|
| 1e7 × 1e7 | 2^17（131k 肢） | 8×8 = 64 | 19.9 s | 16.3 MB | 与内存 NTT 逐位一致（+5.6 s） |
| 1e8 × 1e8 | 2^23（8.39M 肢） | 2×2 = 4 | 227.0 s | 651.5 MB | 与内存 NTT 逐位一致（+170.2 s） |
| 1e9 × 3 | 2^23（8.39M 肢） | 13×1 = 13 | 0.8 s | 163.9 MB | 与 schoolbook 逐位一致（+0.3 s） |

参考：内存内 1e8 位直接 NTT 195.8 s / 930 MB（`bench/bench_huge`），
1e9 位三分块 2403 s / 6.24 GB（`contrib/nex_bigint_block`）。

## M1 结论与 M2 的坑

1. **框架可行**：块切分 → 块积 → 磁盘进位链 → 规范化裁剪全部走通；
   1e9 位数（103,810,253 肢）在 164 MB 峰值下完成并逐位校验通过。
2. **朴素分块的时间代价是 O(nb) 倍 NTT**：总变换量约
   `nb·n·log(n/nb)`，而内存内递归折半是 `1.5·n·log n`。块数越多越慢：
   - `L = 2^23`、1e9 位 → 13 块 → 约 6–8 倍于内存算法的变换量；
   - `L = 2^23`、1e10 位 → 约 1240 块 → 时间到「百小时」量级，**不可行**；
   - 最大块长受直接 NTT 上限约束（`s ≤ 2^27` ⇒ `L ≤ 2^26`），1e10 位
     即便用最大块也要 16 块（而 34 GB 下 `L = 2^26` 的 NTT 工作区约 6 GB，
     仍在预算内）。
3. 因此 **M2（1e10 位 / 34 GB）不能只靠「朴素分块 + 落盘」**，需要在
   块层做乘法：块级 Karatsuba（3 块积代替 4）或块级 FFT/NTT（把块视作
   系数、再做一次变换），即 M3 的「减少块对」提前到 M2；或者接受 34 GB
   下 `L = 2^26` 的 16 块 + 数天运行时间。
4. 待补（M2/M3）：块缓存（把读放大从 `nb` 降到 1）、并行块对、模素数
   校验 `a·b mod p`、`mmap` 替代 `fread`。

## 测试覆盖（`nex_ooc_test`，18 组）

单块 / 块边界 / 块+1 / 非对称 / 1 肢块（进位链最长）/ 全 0xFF 触发长进位 /
中等规模 / 符号 / 零操作数 / 默认块长 / 非法参数 / 可重复性；每例与
`bigint_bin_mul` 逐位一致，并校验 `stats` 的块规划与结果肢数自洽。
