# nex_bigint_block


nex_bigint_block：contrib 层递归分块 NTT 乘法（P1b，issue #98）。

递归折半直到单块 max_len ≤ 2^26（直接 NTT 的 s ≤ 2^27）：
a = a0 + a1·B^L, b = b0 + b1·B^L
a·b = c0 + (sa·sb − c0 − c2)·B^L + c2·B^(2L)
其中 c0=a0·b0、c2=a1·b1、sa=a0+a1、sb=b0+b1；三个子积递归。
10^9 位约一层；10^10 位约四层（81 个基础积）。

限制：10^10 位的分块/SSA 内存模型约 64–114 GB；34 GB 机器会在
递归合并阶段 OOM（本实验实测）。更大规模需要 ≥256 GB 或 out-of-core。


### `nex_block_bin_mul`

```c
bigint_err_ty nex_block_bin_mul(bigint_bin_ty *dst, const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);
```

**说明**：分块 NTT 乘法：dst = lhs × rhs（突破直接 NTT 上限）

**参数**
- `dst`：目标（已初始化；允许别名）
- `lhs/rhs`：非负操作数

**返回**：BIGINT_OK_E；规模/内存不满足时返回错误码

**备注**：结果精确；1e9 位实测 2403 s / 6.24 GB；1e10 位需 ≥256 GB 或 OOC
