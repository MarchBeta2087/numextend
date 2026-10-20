# contrib 模块索引（自动生成）

> 由 `python3 scripts/modules.py gen-contrib-docs` 生成；数据源为
> [`contrib/modules.txt`](../contrib/modules.txt)。请勿手改。
> 这些模块**不进入**核心 `libnex` / `nex_modules.txt` / 子集闭包 /
> amalgam（design §14、TODO §F1）。

| 模块 | 目录 | 依赖 | 开关 | preset | 外部库 | 状态 |
|---|---|---|---|---|---|---|
| `nex_thread` | [`contrib/nex_thread`](../contrib/nex_thread/README.md) | `nex_alloc` | `NEX_ENABLE_THREAD` | `thread` | Threads | 可用（TSan CI） |
| `nex_parallel` | [`contrib/nex_parallel`](../contrib/nex_parallel/README.md) | `nex_thread`, `ntt`, `bigint_bin` | `NEX_ENABLE_THREAD` | `thread` | Threads | 可用（并行 NTT / 批量向量） |
| `nex_bigint_block` | [`contrib/nex_bigint_block`](../contrib/nex_bigint_block/README.md) | `bigint_bin`, `ntt`, `fft` | `NEX_ENABLE_BIGNUM` | `bignum` | — | 可用（1e9 位 2403 s / 6.24 GB） |
| `nex_ssa` | [`contrib/nex_ssa`](../contrib/nex_ssa/README.md) | `bigint_bin`, `ntt` | `NEX_ENABLE_SSA` | `bignum` | — | 冻结（M1 原型，不再排期） |
| `nex_ooc` | [`contrib/nex_ooc`](../contrib/nex_ooc/README.md) | `bigint_bin`, `ntt` | `NEX_ENABLE_BIGNUM` | `bignum` | — | 冻结（M1 原型，实用上限 1e9 位级） |
| `nex_opencl` | [`contrib/nex_opencl`](../contrib/nex_opencl/README.md) | `bigint_bin` | `NEX_ENABLE_OPENCL` | `opencl` | dl | M1–M3（GPU 10.3×，P4 保持实验） |
