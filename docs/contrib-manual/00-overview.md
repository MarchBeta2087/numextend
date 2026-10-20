# contrib 参考手册

> **非核心 API**：以下模块默认不构建，不进入 `libnex` / 子集闭包 /
> amalgam（design §14、TODO §F1）。启用方式见各模块 `flag` 列。
> 机器可读索引：[`docs/contrib-modules.md`](../contrib-modules.md)；
> 各模块的算法、参数与实测见 `contrib/<模块>/README.md`。

| 模块 | 依赖 | 开关 | preset | 状态 |
|---|---|---|---|---|
| `nex_thread` | nex_alloc | `NEX_ENABLE_THREAD` | `thread` | 可用（TSan CI） |
| `nex_parallel` | nex_thread, ntt, bigint_bin | `NEX_ENABLE_THREAD` | `thread` | 可用（并行 NTT / 批量向量） |
| `nex_bigint_block` | bigint_bin, ntt, fft | `NEX_ENABLE_BIGNUM` | `bignum` | 可用（1e9 位 2403 s / 6.24 GB） |
| `nex_ssa` | bigint_bin, ntt | `NEX_ENABLE_SSA` | `bignum` | 冻结（M1 原型，不再排期） |
| `nex_ooc` | bigint_bin, ntt | `NEX_ENABLE_BIGNUM` | `bignum` | 冻结（M1 原型，实用上限 1e9 位级） |
| `nex_opencl` | bigint_bin | `NEX_ENABLE_OPENCL` | `opencl` | M1–M3（GPU 10.3×，P4 保持实验） |
