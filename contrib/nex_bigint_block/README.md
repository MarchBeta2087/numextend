# contrib/nex_bigint_block：分块 NTT 乘法（P1，issue #98）

默认不构建：

```bash
cmake -S . -B build-bignum -DNEX_ENABLE_BIGNUM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-bignum -j
./build-bignum/contrib/nex_bigint_block/nex_block_test          # 1e6/1e7 对拍
./build-bignum/contrib/nex_bigint_block/nex_block_bench 1e9 1   # 10 亿位
```

## 算法

把操作数按肢数折半：

```
a = a0 + a1·B^L,  b = b0 + b1·B^L
a·b = c0 + (sa·sb − c0 − c2)·B^L + c2·B^(2L)
c0 = a0·b0, c2 = a1·b1, sa = a0+a1, sb = b0+b1
```

三个半积仍走核心多模数 CRT NTT，因此半积 `s ≈ len/2` 可落在直接 NTT
上限 `sum(limbs) ≤ 2^27` 内；`10^9` 位（约 1.04e8 肢，半积 ~5.19e7 肢）
因此可行。更大规模（`10^10` 位）需要多级分块或 SSA。

## NTT 扩展（P1）

`nex/ntt` 内置模数新增：

| p | k | c | 根 |
|---|---:|---:|---:|
| 3489660929 | 13 | 28 | 3 |
| 3221225473 | 3 | 30 | 5 |

`NEX_MUL_NTT_MAX_SUM` 由 `2^26` 提升到 `2^27`。直接 NTT 需要两个模数的
`c ≥ log2n`；`n=2^28` 用 c=28 + c=30，`n=2^29` 只有单个 c=30，故 10^9 位
不能直接 NTT，必须走分块。

## 实测

见 issue #98 与 CHANGELOG；本目录的 `nex_block_bench` 输出耗时与 Peak RSS。
