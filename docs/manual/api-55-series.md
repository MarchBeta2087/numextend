# series —— 幂级数求值（精确系数 + 全局尾界）


nex_series：幂级数求值的「精确系数 + 全局尾界」路线 A（设计文档 §15.6）。

以精确有理解的截断级数在数值点求值：系数为 `bigfrac`（精确），仅截断与
最终舍入引入误差。截断尾由尾界界定：
- 几何型 `A·q^n/(1−q)`（q = B·r）：有界系数（如二项式 `α ≥ −1`）；
- 比值型 `t_n/(1−q_n)`：阶乘型系数（exp / sin / cos）。
求值返回结果，并可经可选 `err` 输出收到**该截断的严格上界**。
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

**返回**：成功返回 BIGFRAC_OK_E；指针为 NULL 返回 BIGFRAC_ERR_INVALID_E； alpha < −1（系数无界）返回 BIGFRAC_ERR_UNSUPPORTED_E； 内存不足返回 BIGFRAC_ERR_OOM_E（A、B 不变）

### `nex_series_binom_order`

```c
bigfrac_err_ty nex_series_binom_order(size_t *out_n, const bigfrac_ty *alpha, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：二项式级数自适应阶：最小 n 使 (1+z)^alpha（|z| ≤ r）截断尾 ≤ eps

**返回**：成功返回 BIGFRAC_OK_E；参数非法或 r ≥ 1 返回 BIGFRAC_ERR_INVALID_E； alpha < −1 返回 BIGFRAC_ERR_UNSUPPORTED_E；内存不足返回 BIGFRAC_ERR_OOM_E（out_n 不变）

### `nex_series_binom_eval_float`

```c
bigfloat_err_ty nex_series_binom_eval_float(bigfloat_ty *out, bigfloat_ty *err_out, const bigfrac_ty *alpha, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 (1+z)^alpha（精确系数 + 全局尾界 + 终值舍入）

**参数**
- `out`：输出（正确舍入的精确部分和）
- `err_out`：可选，接收截断尾的严格上界；可为 NULL
- `alpha`：指数（≥ −1）
- `z`：自变量（有限且 |z| < 1）
- `eps`：目标截断误差（绝对，≥ 0）
- `ctx`：目标精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法、z 非有限、|z| ≥ 1 或 alpha < −1 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E

**备注**：总误差 ≤ eps（截断）+ 终值一次舍入

### `nex_series_binom_eval_decimal`

```c
bigdecimal_err_ty nex_series_binom_eval_decimal(bigdecimal_ty *out, bigdecimal_ty *err_out, const bigfrac_ty *alpha, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 (1+z)^alpha（语义同 eval_float）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 eval_float（错误码对应更换）

### `nex_series_exp_order`

```c
bigfrac_err_ty nex_series_exp_order(size_t *out_n, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：exp(z) = Σ z^k/k! 的自适应阶：最小 n 使截断尾（|z| ≤ r）≤ eps

**返回**：成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E； 超出迭代上限返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E

### `nex_series_cos_order`

```c
bigfrac_err_ty nex_series_cos_order(size_t *out_n, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：cos(z) = Σ (−1)^j z^{2j}/(2j)! 的自适应阶

**返回**：同 exp_order

### `nex_series_sin_order`

```c
bigfrac_err_ty nex_series_sin_order(size_t *out_n, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：sin(z) = Σ (−1)^j z^{2j+1}/(2j+1)! 的自适应阶

**返回**：同 exp_order

### `nex_series_exp_eval_float`

```c
bigfloat_err_ty nex_series_exp_eval_float(bigfloat_ty *out, bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 exp(z)（精确系数 + 比值尾界）

**参数**
- `out/err_out/z/eps/ctx`：语义同 binom_eval_float

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 z 非有限返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（out 不变）

**备注**：未做实参规约，建议 |z| ≲ 1；总误差 ≤ eps + 终值一次舍入

### `nex_series_cos_eval_float`

```c
bigfloat_err_ty nex_series_cos_eval_float(bigfloat_ty *out, bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 cos(z)（语义同 exp_eval_float）

**返回**：同 exp_eval_float

### `nex_series_sin_eval_float`

```c
bigfloat_err_ty nex_series_sin_eval_float(bigfloat_ty *out, bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 sin(z)（语义同 exp_eval_float）

**返回**：同 exp_eval_float

### `nex_series_exp_eval_decimal`

```c
bigdecimal_err_ty nex_series_exp_eval_decimal(bigdecimal_ty *out, bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 exp(z)（语义同 bigfloat 版）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）

### `nex_series_cos_eval_decimal`

```c
bigdecimal_err_ty nex_series_cos_eval_decimal(bigdecimal_ty *out, bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 cos(z)（语义同 bigfloat 版）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）

### `nex_series_sin_eval_decimal`

```c
bigdecimal_err_ty nex_series_sin_eval_decimal(bigdecimal_ty *out, bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 sin(z)（语义同 bigfloat 版）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）

### `nex_series_ln1p_order`

```c
bigfrac_err_ty nex_series_ln1p_order(size_t *out_n, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：ln(1+z) = Σ_{k≥1} (−1)^{k+1} z^k/k 的自适应阶（|z| ≤ r < 1）

**参数**
- `out_n`：输出阶；r 自变量幅度上界（需 r < 1）；eps 目标误差

**返回**：成功返回 BIGFRAC_OK_E；参数非法或 r ≥ 1 返回 BIGFRAC_ERR_INVALID_E； 超出迭代上限返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E

### `nex_series_ln1p_eval_float`

```c
bigfloat_err_ty nex_series_ln1p_eval_float(bigfloat_ty *out, bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 ln(1+z)（精确系数 + 比值尾界 + 可选误差输出）

**参数**
- `out/err_out/z/eps/ctx`：语义同 binom_eval_float

**返回**：成功返回 BIGFLOAT_OK_E；参数非法、z 非有限或 |z| ≥ 1 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（out 不变）

**备注**：未做实参规约，建议 |z| ≲ 1/2；总误差 ≤ eps + 终值一次舍入

### `nex_series_ln1p_eval_decimal`

```c
bigdecimal_err_ty nex_series_ln1p_eval_decimal(bigdecimal_ty *out, bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 ln(1+z)（语义同 bigfloat 版）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）

### `nex_series_atan_order`

```c
bigfrac_err_ty nex_series_atan_order(size_t *out_n, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：atan(z) = Σ_{k≥0} (−1)^k z^{2k+1}/(2k+1) 的自适应阶（|z| ≤ r < 1）

**返回**：同 ln1p_order

### `nex_series_atan_eval_float`

```c
bigfloat_err_ty nex_series_atan_eval_float(bigfloat_ty *out, bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 atan(z)（语义同 ln1p_eval_float）

**返回**：成功返回 BIGFLOAT_OK_E；其余同 ln1p_eval_float

### `nex_series_atan_eval_decimal`

```c
bigdecimal_err_ty nex_series_atan_eval_decimal(bigdecimal_ty *out, bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 atan(z)（语义同 ln1p_eval_decimal）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 ln1p_eval_float（错误码对应更换）

### `nex_series_pade_frac`

```c
bigfrac_err_ty nex_series_pade_frac(bigfrac_series_ty *num, bigfrac_series_ty *den, const bigfrac_series_ty *series, size_t m, size_t n);
```

**说明**：Padé 逼近 [m/n]：由幂级数 c_0+c_1 z+… 求 P/Q，f − P/Q = O(z^{m+n+1})

**参数**
- `num`：输出分子多项式（bigfrac_series，val=0，次数 ≤ m）
- `den`：输出分母多项式（bigfrac_series，val=0，Q(0)=1，次数 ≤ n）
- `series`：输入幂级数（要求 val = 0）
- `m,`：n   分子/分母次数

**返回**：成功返回 BIGFRAC_OK_E；参数非法或 series->val ≠ 0 或系统奇异返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E

**备注**：对 q 用 bigfrac 精确高斯消元；P/Q 系数精确

### `nex_series_pade_eval_frac`

```c
bigfrac_err_ty nex_series_pade_eval_frac(bigfrac_ty *out, const bigfrac_series_ty *num, const bigfrac_series_ty *den, const bigfrac_ty *z);
```

**说明**：Padé 有理逼近求值 out = P(z)/Q(z)（精确有理结果）

**返回**：成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E； Q(z) == 0 返回 BIGFRAC_ERR_DIV_ZERO_E；内存不足返回 OOM（out 不变）

### `nex_series_pade_eval_float`

```c
bigfloat_err_ty nex_series_pade_eval_float(bigfloat_ty *out, const bigfrac_series_ty *num, const bigfrac_series_ty *den, const bigfloat_ty *z, const bigfloat_ctx_ty *ctx);
```

**说明**：在 bigfloat 点求值 P(z)/Q(z)（精确部分和 + 终值一次舍入）

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 Q(z) == 0 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 OOM（out 不变）

### `nex_series_pade_eval_decimal`

```c
bigdecimal_err_ty nex_series_pade_eval_decimal(bigdecimal_ty *out, const bigfrac_series_ty *num, const bigfrac_series_ty *den, const bigdecimal_ty *z, const bigdecimal_ctx_ty *ctx);
```

**说明**：在 bigdecimal 点求值 P(z)/Q(z)（语义同 bigfloat 版）

**返回**：成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）

NEX_SERIES_H
