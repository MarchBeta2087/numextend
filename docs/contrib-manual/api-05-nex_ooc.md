# nex_ooc


nex_ooc：out-of-core 分块乘法原型（P1b-OOC，issue #104）。

把操作数视为「肢文件」（小端 32-bit 肢序列，无头），按块长 L 切分：

a = Σ a_i·B^(iL)，b = Σ b_j·B^(jL)，B = 2^32
a·b = Σ_k B^(kL)·( Σ_{i+j=k} a_i·b_j )

输出按 k 升序生成：块 k 的累加器只保留 2L+2 肢，算完立即写盘，高位
作为进位带入块 k+1。因此**峰值内存 O(L)，与操作数规模无关**，代价是
每个 a_i / b_j 块被重复读取（块对总数 na·nb，I/O 约 O(n·nb)）。

与内存内算法（`bigint_bin_mul` / `contrib/nex_bigint_block`）逐位一致；
块对乘法直接复用核心 AUTO 分派（块内为 Karatsuba/Toom-3/CRT NTT）。

限制：M1 只用单线程顺序 I/O，未做块缓存、并行块对、块级 FFT；这些是
issue #104 的 M3 项。10^10 位的块数约 32（L=2^25 时），I/O 量约
数百 GB，需 M2 实测磁盘带宽。


### `type`

**说明**：错误码：本 contrib 模块自带 I/O 错误，避免污染核心 bigint_err_ty

```c
typedef enum {
NEX_OOC_OK_E = 0,
NEX_OOC_ERR_INVALID_E,
```

### `NEX_OOC_ERR_OOM_E,`

```c
NEX_OOC_ERR_OOM_E,
```

**说明**：空指针 / 非法块长 / 打不开文件

### `NEX_OOC_ERR_IO_E,`

```c
NEX_OOC_ERR_IO_E,
```

**说明**：内存不足

### `NEX_OOC_ERR_UNSUPPORTED_E`

```c
NEX_OOC_ERR_UNSUPPORTED_E
```

**说明**：读/写/定位/截断失败

### `} nex_ooc_err_ty;`

```c
} nex_ooc_err_ty;
```

**说明**：块对乘法返回 UNSUPPORTED（规模越界）

```c
typedef struct {
size_t block_limbs;
```

### `const char *scratch_dir;`

```c
const char *scratch_dir;
```

**说明**：块长（肢）；0 → NEX_OOC_DEFAULT_BLOCK_LIMBS

### `int keep_temps;`

```c
int keep_temps;
```

**说明**：临时文件目录；NULL → 当前目录

### `} nex_ooc_opts_ty;`

```c
} nex_ooc_opts_ty;
```

**说明**：非零：保留临时文件（调试用）

```c
typedef struct {
size_t block_limbs;
```

### `size_t blocks_a;`

```c
size_t blocks_a;
```

**说明**：实际使用的块长

### `size_t blocks_b;`

```c
size_t blocks_b;
```

**说明**：a 的块数

### `size_t block_pairs;`

```c
size_t block_pairs;
```

**说明**：b 的块数

### `uint64_t bytes_read;`

```c
uint64_t bytes_read;
```

**说明**：实际执行的块对乘法数

### `uint64_t bytes_written;`

```c
uint64_t bytes_written;
```

**说明**：从 a/b 读入的字节数

### `uint64_t result_limbs;`

```c
uint64_t result_limbs;
```

**说明**：写入结果的字节数（含尾部裁剪前的冗余）

### `double seconds;`

```c
double seconds;
```

**说明**：结果肢数（裁剪后）

### `} nex_ooc_stats_ty;`

```c
} nex_ooc_stats_ty;
```

**说明**：挂钟时间

### `nex_ooc_mul_files`

```c
nex_ooc_err_ty nex_ooc_mul_files(const char *dst_path, const char *a_path, size_t a_limbs, const char *b_path, size_t b_limbs, const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats);
```

**说明**：文件到文件的乘法（操作数为纯肢体文件，不含符号/长度头）

**参数**
- `dst_path`：结果文件路径（覆盖写；小端 32-bit 肢体，无头）
- `a_path`：/ a_limbs 操作数 a 的路径与肢数
- `b_path`：/ b_limbs 操作数 b 的路径与肢数
- `opts`：选项，可为 NULL
- `stats`：统计输出，可为 NULL

**返回**：NEX_OOC_OK_E 成功；否则见 nex_ooc_err_ty

**备注**：结果取幅值（符号由调用方处理）；任一操作数为零时生成空文件

### `nex_ooc_bin_mul`

```c
nex_ooc_err_ty nex_ooc_bin_mul(bigint_bin_ty *dst, const bigint_bin_ty *lhs, const bigint_bin_ty *rhs, const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats);
```

**说明**：内存大整数乘法，内部经临时文件走 out-of-core 路径

**参数**
- `dst`：结果；lhs / rhs 操作数（允许别名，符号按常规规则）
- `opts`：/ stats 同上

**返回**：NEX_OOC_OK_E 成功；否则见 nex_ooc_err_ty

**备注**：仅用于验证与中小规模使用；大输入本身已在内存中，省不了内存

### `nex_ooc_estimate_peak_bytes`

```c
size_t nex_ooc_estimate_peak_bytes(size_t block_limbs);
```

**说明**：峰值内存的经验估计（字节），用于选定块长

**参数**
- `block_limbs`：块长；0 表示默认块长

**返回**：估计的峰值字节数（工作缓冲区 + NTT 内部估算）

### `nex_ooc_strerror`

```c
const char *nex_ooc_strerror(nex_ooc_err_ty err);
```

**说明**：错误码名称（用于日志/基准输出）
