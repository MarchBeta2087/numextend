# transcend —— 超越函数（正确舍入）


nex_transcend：数值类型的超越函数（设计文档 §13 #8 / §D）。

实现方式：内部用球式（路线 B）做实参规约与严谨误差界，再以 **Ziv 策略**
提升工作精度直至结果在目标 ctx 下**正确舍入**。首个函数为 `atan`（复数
辐角 `arg` 依赖它）。


### `bigfloat_atan`

```c
bigfloat_err_ty bigfloat_atan(bigfloat_ty *out, const bigfloat_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：反正切 out = atan(x)，按 ctx 正确舍入

**参数**
- `out`：输出；x 自变量（有限值）；ctx 目标精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 x 非有限返回 BIGFLOAT_ERR_INVALID_E； 内存不足返回 BIGFLOAT_ERR_OOM_E（out 不变）

**备注**：实参规约 + 级数 + 球式严格误差界 + Ziv 提升精度保证正确舍入

### `bigfloat_atan2`

```c
bigfloat_err_ty bigfloat_atan2(bigfloat_ty *out, const bigfloat_ty *y, const bigfloat_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：双参反正切 out = atan2(y, x) = arg(x + iy)，按 ctx 正确舍入

**参数**
- `out`：输出；y, x 坐标（有限值）；ctx 目标精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 y/x 非有限返回 INVALID； 内存不足返回 OOM（out 不变）

**备注**：主值 ∈ (−π, π]；球式半角规约 + Ziv 正确舍入

### `bigfloat_exp`

```c
bigfloat_err_ty bigfloat_exp(bigfloat_ty *out, const bigfloat_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：指数 out = exp(x)，按 ctx 正确舍入

**参数**
- `out`：输出；x 自变量（有限值）；ctx 目标精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 x 非有限返回 INVALID； 内存不足返回 OOM（out 不变）

**备注**：球式折半 + 路线 A 级数 + k 次球平方 + Ziv 正确舍入

### `bigfloat_log`

```c
bigfloat_err_ty bigfloat_log(bigfloat_ty *out, const bigfloat_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：自然对数 out = log(x) = ln(x)，按 ctx 正确舍入

**参数**
- `out`：输出；x 自变量（有限值，x > 0）；ctx 目标精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法、x 非有限或 x ≤ 0 返回 INVALID； 内存不足返回 OOM（out 不变）

**备注**：球式迭代平方根规约 + `ln1p` 级数 + Ziv 正确舍入

### `bigdecimal_atan`

```c
bigdecimal_err_ty bigdecimal_atan(bigdecimal_ty *out, const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：反正切 out = atan(x)，按 ctx 正确舍入（十进制版）

**参数**
- `out`：输出；x 自变量（有限值）；ctx 目标精度上下文

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法或 x 非有限返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（out 不变）

**备注**：实参规约 + 级数 + 球式严格误差界 + Ziv 提升精度保证正确舍入

### `bigdecimal_atan2`

```c
bigdecimal_err_ty bigdecimal_atan2(bigdecimal_ty *out, const bigdecimal_ty *y, const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：双参反正切 out = atan2(y, x) = arg(x + iy)，按 ctx 正确舍入（十进制版）

**参数**
- `out`：输出；y, x 坐标（有限值）；ctx 目标精度上下文

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法或 y/x 非有限返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 OOM（out 不变）

**备注**：主值 ∈ (−π, π]；球式半角规约 + Ziv 正确舍入

### `bigdecimal_exp`

```c
bigdecimal_err_ty bigdecimal_exp(bigdecimal_ty *out, const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：指数 out = exp(x)，按 ctx 正确舍入（十进制版）

**参数**
- `out`：输出；x 自变量（有限值）；ctx 目标精度上下文

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法或 x 非有限返回 INVALID； 内存不足返回 OOM（out 不变）

**备注**：球式折半 + 路线 A 级数 + k 次球平方 + Ziv 正确舍入

### `bigdecimal_log`

```c
bigdecimal_err_ty bigdecimal_log(bigdecimal_ty *out, const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：自然对数 out = log(x) = ln(x)，按 ctx 正确舍入（十进制版）

**参数**
- `out`：输出；x 自变量（有限值，x > 0）；ctx 目标精度上下文

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法、x 非有限或 x ≤ 0 返回 INVALID； 内存不足返回 OOM（out 不变）

**备注**：球式迭代平方根规约 + `ln1p` 级数 + Ziv 正确舍入

NEX_TRANSCEND_H
