# bigint_conv —— bin ↔ dec 互转


bigint_conv：bigint_bin ↔ bigint_dec 互转（设计文档 §4.2.6），以及
bigint_bin64 与两者的等值互转。
本单元是唯一同时包含 bin / bin64 / dec 头文件的模块（§2.1 的跨支线依赖）。
各侧输出均保持规范化表示（含符号与零的唯一表示）。


### `bigint_conv_bin_to_dec`

```c
bigint_err_ty bigint_conv_bin_to_dec(bigint_dec_ty *dst, const bigint_bin_ty *src);
```

**说明**：bin 转 dec（基 2^32 → 基 10^9）

**参数**
- `dst`：目标对象（已初始化）
- `src`：源对象

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：分治（2 的幂切半 + 平方链表），小规模回退朴素反复除 10^9； 大输入 O(n^1.585)（设计文档 §13 #10）

### `bigint_conv_dec_to_bin`

```c
bigint_err_ty bigint_conv_dec_to_bin(bigint_bin_ty *dst, const bigint_dec_ty *src);
```

**说明**：dec 转 bin（基 10^9 → 基 2^32）

**参数**
- `dst`：目标对象（已初始化）
- `src`：源对象

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：分治（对称于 bin→dec），小规模回退朴素逐肢乘 10^9； 大输入 O(n^1.585)（设计文档 §13 #10）

### `bigint_conv_bin64_to_bin`

```c
bigint_err_ty bigint_conv_bin64_to_bin(bigint_bin_ty *dst, const bigint_bin64_ty *src);
```

**说明**：bin64 转 bin（基 2^64 → 基 2^32，按半字拆分）

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：线性时间；结果保持规范化

### `bigint_conv_bin_to_bin64`

```c
bigint_err_ty bigint_conv_bin_to_bin64(bigint_bin64_ty *dst, const bigint_bin_ty *src);
```

**说明**：bin 转 bin64（基 2^32 → 基 2^64，按半字合并）

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：线性时间；结果保持规范化

### `bigint_conv_bin64_to_dec`

```c
bigint_err_ty bigint_conv_bin64_to_dec(bigint_dec_ty *dst, const bigint_bin64_ty *src);
```

**说明**：bin64 转 dec（经 bin 中转，复用分治基数转换）

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：中间 bin 与 bin64 肢数相当，额外开销为线性

### `bigint_conv_dec_to_bin64`

```c
bigint_err_ty bigint_conv_dec_to_bin64(bigint_bin64_ty *dst, const bigint_dec_ty *src);
```

**说明**：dec 转 bin64（经 bin 中转，复用分治基数转换）

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：中间 bin 与 bin64 肢数相当，额外开销为线性

NEX_BIGINT_CONV_H
