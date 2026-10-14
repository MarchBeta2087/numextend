# series —— 幂级数求值（精确系数 + 全局尾界）


nex_series：幂级数求值的「精确系数 + 全局尾界」路线 A（设计文档 §15.6）。

以精确有理解的截断级数在数值点求值：系数为 `bigfrac`（精确），仅截断与
最终舍入引入误差；截断尾由几何型全局尾界 `A·q^n/(1−q)`（q = B·r）界定。
球式（中心 + 半径）路线 B 为后续方向。


### `nex_series_binom_model`

```c
bigfrac_err_ty nex_series_binom_model(bigfrac_ty *A, bigfrac_ty *B, const bigfrac_ty *alpha);
```

**说明**：二项式级数 (1+z)^alpha 的全局尾界模型 (A, B)：|C(alpha,k)| ≤ A·B^k

**参数**
- `A`：系数幅度上界（输出）
- `B`：系数增长因子（输出，此处恒为 1）
- `alpha`：指数

**返回**：成功返回 BIGFRAC_OK_E；指针为 NULL 返回 BIGFRAC_ERR_INVALID_E； alpha < −1（系数无界，需用比值型界）返回 BIGFRAC_ERR_UNSUPPORTED_E； 内存不足返回 BIGFRAC_ERR_OOM_E（A、B 不变）

**备注**：alpha ≥ −1 时 |C(alpha,k)| 在 k ≈ alpha 处取峰、其后单调下降， 故扫描前若干项即可得精确的 A（B = 1）

### `nex_series_binom_order`

```c
bigfrac_err_ty nex_series_binom_order(size_t *out_n, const bigfrac_ty *alpha, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：自适应截断阶：最小 n 使 (1+z)^alpha（|z| ≤ r）的截断尾 ≤ eps

**参数**
- `out_n`：输出阶 n
- `alpha`：指数
- `r`：自变量幅度上界（≥ 0，需 r < 1）
- `eps`：目标绝对误差上界（≥ 0）

**返回**：成功返回 BIGFRAC_OK_E；参数非法或 r ≥ 1 返回 BIGFRAC_ERR_INVALID_E； alpha < −1 返回 BIGFRAC_ERR_UNSUPPORTED_E；Q 极接近 1 超上限返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E（out_n 不变）

### `nex_series_binom_eval_float`

```c
bigfloat_err_ty nex_series_binom_eval_float(bigfloat_ty *out, const bigfrac_ty *alpha, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 (1+z)^alpha（路线 A：精确系数 + 全局尾界 + 终值舍入）

**参数**
- `out`：输出
- `alpha`：指数（≥ −1）
- `z`：自变量（有限且 |z| < 1）
- `eps`：目标截断误差（绝对，≥ 0）
- `ctx`：目标精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法、z 非有限或 |z| ≥ 1、或 alpha < −1 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E （out 不变）

**备注**：误差 ≤ eps（截断）+ 终值一次舍入误差；返回的 out 是精确部分和的 正确舍入

### `nex_series_binom_eval_decimal`

```c
bigdecimal_err_ty nex_series_binom_eval_decimal(bigdecimal_ty *out, const bigfrac_ty *alpha, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 (1+z)^alpha（路线 A，语义同 eval_float）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 eval_float（错误码对应更换）

NEX_SERIES_H
