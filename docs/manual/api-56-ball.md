# ball —— 球式（中心 + 半径）算术


nex_ball：球式（中心 + 误差半径）算术，路线 B 第一阶段（设计文档 §15.6）。

区间语义：bigfloat_ball_ty 表示 [mid − rad, mid + rad]（rad ≥ 0）。
四则运算用**定向舍入**在区间端点间进行，结果球严格包含真实区间；
半径传播复用需要时可与路线 A 的尾界叠加。


bigfloat_ball_ty：中心 mid 与半径 rad（rad ≥ 0）构成的闭区间

```c
typedef struct {
bigfloat_ty mid;  // 中心
bigfloat_ty rad;  // 半径（≥ 0）
} bigfloat_ball_ty;
```

### `bigfloat_ball_init`

```c
bigfloat_err_ty bigfloat_ball_init(bigfloat_ball_ty *b);
```

**说明**：初始化为 [0, 0]

**返回**：成功返回 BIGFLOAT_OK_E；b 为 NULL 返回 BIGFLOAT_ERR_INVALID_E

### `bigfloat_ball_free`

```c
void bigfloat_ball_free(bigfloat_ball_ty *b);
```

**说明**：释放（b 可为 NULL）

### `bigfloat_ball_copy`

```c
bigfloat_err_ty bigfloat_ball_copy(bigfloat_ball_ty *dst, const bigfloat_ball_ty *src);
```

**说明**：深拷贝

**返回**：成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

### `bigfloat_ball_set`

```c
bigfloat_err_ty bigfloat_ball_set(bigfloat_ball_ty *dst, const bigfloat_ty *mid, const bigfloat_ty *rad, const bigfloat_ctx_ty *ctx);
```

**说明**：设为 [mid − rad, mid + rad]

**参数**
- `mid/rad`：中心与半径（rad 须 ≥ 0；为 ±0 视为 0）

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 rad < 0 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

### `bigfloat_ball_contains`

```c
bool bigfloat_ball_contains(const bigfloat_ball_ty *b, const bigfloat_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：判断 x 是否在球内（无假阳性：两端点按向内方向舍入）

**返回**：x ∈ [mid − rad, mid + rad] 为 true；NaN 或参数非法为 false

### `bigfloat_ball_add`

```c
bigfloat_err_ty bigfloat_ball_add(bigfloat_ball_ty *dst, const bigfloat_ball_ty *a, const bigfloat_ball_ty *b, const bigfloat_ctx_ty *ctx);
```

**说明**：加法 dst = a + b（区间和）

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 a、b 含 NaN/±∞ 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

### `bigfloat_ball_sub`

```c
bigfloat_err_ty bigfloat_ball_sub(bigfloat_ball_ty *dst, const bigfloat_ball_ty *a, const bigfloat_ball_ty *b, const bigfloat_ctx_ty *ctx);
```

**说明**：减法 dst = a - b（区间差）

**返回**：同 add

### `bigfloat_ball_mul`

```c
bigfloat_err_ty bigfloat_ball_mul(bigfloat_ball_ty *dst, const bigfloat_ball_ty *a, const bigfloat_ball_ty *b, const bigfloat_ctx_ty *ctx);
```

**说明**：乘 dst = a × b（区间积）

**返回**：同 add

### `bigfloat_ball_div`

```c
bigfloat_err_ty bigfloat_ball_div(bigfloat_ball_ty *dst, const bigfloat_ball_ty *a, const bigfloat_ball_ty *b, const bigfloat_ctx_ty *ctx);
```

**说明**：除 dst = a / b（区间商）

**返回**：成功返回 BIGFLOAT_OK_E；b 的区间含 0 返回 BIGFLOAT_ERR_INVALID_E； 其余同 add

### `bigfloat_ball_exp`

```c
bigfloat_err_ty bigfloat_ball_exp(bigfloat_ball_ty *dst, const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：球式 exp(x)：实参缩放 |x|/2^k ≤ 1/2 → 路线 A 级数得初始球 → 反复球平方

**参数**
- `dst`：输出；x 输入球；ctx 精度上下文

**返回**：成功返回 BIGFLOAT_OK_E；参数非法、含 NaN/±∞ 或 |x| 过大（k > 20） 返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E

**备注**：半径由球运算严格传播；总误差含截断与逐次舍入

### `bigfloat_ball_sin`

```c
bigfloat_err_ty bigfloat_ball_sin(bigfloat_ball_ty *dst, const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：球式 sin(x)（half-angle 倍角：sin2θ = 2 sinθ cosθ）

**返回**：同 exp（|x| 过大时 k > 20 返回 INVALID）

### `bigfloat_ball_cos`

```c
bigfloat_err_ty bigfloat_ball_cos(bigfloat_ball_ty *dst, const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：球式 cos(x)（half-angle 倍角：cos2θ = 2cos²θ − 1）

**返回**：同 exp

bigfloat_ball_series_ty：系数为 bigfloat_ball_ty 的截断级数（§I，v1.5.0）  S(z) = Σ_{i=0}^{len-1} coeffs[i] · z^{val+i}  每个系数携带误差半径，运算全程由球传播，得到严谨的数值级数。规范化： len == 0 为零级数（val 归 0）；否则末项为非零球。

```c
typedef struct {
int64_t val;                  // 最低次幂指数
bigfloat_ball_ty *coeffs;     // 球系数，coeffs[i] 对应 z^{val+i}
size_t len;                   // 有效系数个数
size_t cap;                   // 已分配容量
} bigfloat_ball_series_ty;
```

### `bigfloat_ball_series_init`

```c
bigfloat_err_ty bigfloat_ball_series_init(bigfloat_ball_series_ty *s);
```

**说明**：初始化为零级数

**返回**：成功返回 BIGFLOAT_OK_E；s 为 NULL 返回 BIGFLOAT_ERR_INVALID_E

### `bigfloat_ball_series_free`

```c
void bigfloat_ball_series_free(bigfloat_ball_series_ty *s);
```

**说明**：释放（s 可为 NULL）

### `bigfloat_ball_series_from_array`

```c
bigfloat_err_ty bigfloat_ball_series_from_array(bigfloat_ball_series_ty *dst, int64_t val, const bigfloat_ball_ty *coeffs, size_t len);
```

**说明**：由球系数数组构造（深拷贝，去尾零）

**返回**：成功返回 BIGFLOAT_OK_E；参数非法返回 BIGFLOAT_ERR_INVALID_E； 内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

### `bigfloat_ball_series_copy`

```c
bigfloat_err_ty bigfloat_ball_series_copy(bigfloat_ball_series_ty *dst, const bigfloat_ball_series_ty *src);
```

**说明**：深拷贝

**返回**：成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

### `bigfloat_ball_series_add`

```c
bigfloat_err_ty bigfloat_ball_series_add(bigfloat_ball_series_ty *dst, const bigfloat_ball_series_ty *lhs, const bigfloat_ball_series_ty *rhs, size_t order, const bigfloat_ctx_ty *ctx);
```

**说明**：加法 dst = lhs + rhs（截断到 order，半径由球运算严格传播）

**返回**：成功返回 BIGFLOAT_OK_E；参数非法返回 BIGFLOAT_ERR_INVALID_E； 内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

### `bigfloat_ball_series_sub`

```c
bigfloat_err_ty bigfloat_ball_series_sub(bigfloat_ball_series_ty *dst, const bigfloat_ball_series_ty *lhs, const bigfloat_ball_series_ty *rhs, size_t order, const bigfloat_ctx_ty *ctx);
```

**说明**：减法 dst = lhs - rhs（截断到 order）

**返回**：同 add

### `bigfloat_ball_series_mul`

```c
bigfloat_err_ty bigfloat_ball_series_mul(bigfloat_ball_series_ty *dst, const bigfloat_ball_series_ty *lhs, const bigfloat_ball_series_ty *rhs, size_t order, const bigfloat_ctx_ty *ctx);
```

**说明**：乘法 dst = lhs × rhs（截断到 order）

**返回**：同 add

### `bigfloat_ball_series_eval`

```c
bigfloat_err_ty bigfloat_ball_series_eval(bigfloat_ball_ty *out, const bigfloat_ball_series_ty *s, const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx);
```

**说明**：求值 out = Σ coeffs[i]·x^{val+i}（x 为球；要求 val ≥ 0）

**返回**：成功返回 BIGFLOAT_OK_E；参数非法或 val < 0 返回 BIGFLOAT_ERR_INVALID_E； 内存不足返回 BIGFLOAT_ERR_OOM_E（out 不变）

### `bigfloat_ball_series_compose`

```c
bigfloat_err_ty bigfloat_ball_series_compose(bigfloat_ball_series_ty *dst, const bigfloat_ball_series_ty *f, const bigfloat_ball_series_ty *g, size_t order, const bigfloat_ctx_ty *ctx);
```

**说明**：复合 dst = f∘g = f(g(z))，截断到 order（秦九韶式）

**参数**
- `f`：要求 val == 0；g 要求 val ≥ 0

**返回**：成功返回 BIGFLOAT_OK_E；参数非法返回 BIGFLOAT_ERR_INVALID_E； 内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）

bigdecimal_ball_ty：bigdecimal 版球（中心 + 半径，rad ≥ 0）

```c
typedef struct {
bigdecimal_ty mid;  // 中心
bigdecimal_ty rad;  // 半径（≥ 0）
} bigdecimal_ball_ty;
```

### `bigdecimal_ball_init`

```c
bigdecimal_err_ty bigdecimal_ball_init(bigdecimal_ball_ty *b);
```

**说明**：初始化为 [0, 0]

**返回**：成功返回 BIGDECIMAL_OK_E；b 为 NULL 返回 BIGDECIMAL_ERR_INVALID_E

### `bigdecimal_ball_free`

```c
void bigdecimal_ball_free(bigdecimal_ball_ty *b);
```

**说明**：释放（b 可为 NULL）

### `bigdecimal_ball_copy`

```c
bigdecimal_err_ty bigdecimal_ball_copy(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *src);
```

**说明**：深拷贝

**返回**：成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ball_set`

```c
bigdecimal_err_ty bigdecimal_ball_set(bigdecimal_ball_ty *dst, const bigdecimal_ty *mid, const bigdecimal_ty *rad, const bigdecimal_ctx_ty *ctx);
```

**说明**：设为 [mid − rad, mid + rad]（rad ≤ 0 非法）

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法或 rad < 0 返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ball_contains`

```c
bool bigdecimal_ball_contains(const bigdecimal_ball_ty *b, const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：判断 x 是否在球内（向内舍入，无假阳性）

### `bigdecimal_ball_add`

```c
bigdecimal_err_ty bigdecimal_ball_add(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b, const bigdecimal_ctx_ty *ctx);
```

**说明**：加法 dst = a + b（区间和）

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法或含 NaN/±∞ 返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ball_sub`

```c
bigdecimal_err_ty bigdecimal_ball_sub(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b, const bigdecimal_ctx_ty *ctx);
```

**说明**：减法 dst = a - b（区间差）

**返回**：同 add

### `bigdecimal_ball_mul`

```c
bigdecimal_err_ty bigdecimal_ball_mul(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b, const bigdecimal_ctx_ty *ctx);
```

**说明**：乘法 dst = a × b（区间积）

**返回**：同 add

### `bigdecimal_ball_div`

```c
bigdecimal_err_ty bigdecimal_ball_div(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b, const bigdecimal_ctx_ty *ctx);
```

**说明**：除法 dst = a / b（区间商）

**返回**：成功返回 BIGDECIMAL_OK_E；b 的区间含 0 返回 BIGDECIMAL_ERR_INVALID_E；其余同 add

### `bigdecimal_ball_exp`

```c
bigdecimal_err_ty bigdecimal_ball_exp(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：球式 exp(x)（实参规约；语义同 bigfloat 版）

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法/含 NaN/±∞/|x| 过大返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E

### `bigdecimal_ball_sin`

```c
bigdecimal_err_ty bigdecimal_ball_sin(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：球式 sin(x)（half-angle 倍角；语义同 bigfloat 版）

**返回**：同 exp

### `bigdecimal_ball_cos`

```c
bigdecimal_err_ty bigdecimal_ball_cos(bigdecimal_ball_ty *dst, const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：球式 cos(x)（half-angle 倍角；语义同 bigfloat 版）

**返回**：同 exp

bigdecimal_ball_series_ty：系数为 bigdecimal_ball_ty 的截断级数（§I） 语义同 bigfloat_ball_series_ty。

```c
typedef struct {
int64_t val;                   // 最低次幂指数
bigdecimal_ball_ty *coeffs;    // 球系数，coeffs[i] 对应 z^{val+i}
size_t len;                    // 有效系数个数
size_t cap;                    // 已分配容量
} bigdecimal_ball_series_ty;
```

### `bigdecimal_ball_series_init`

```c
bigdecimal_err_ty bigdecimal_ball_series_init(bigdecimal_ball_series_ty *s);
```

**说明**：初始化为零级数

**返回**：成功返回 BIGDECIMAL_OK_E；s 为 NULL 返回 BIGDECIMAL_ERR_INVALID_E

### `bigdecimal_ball_series_free`

```c
void bigdecimal_ball_series_free(bigdecimal_ball_series_ty *s);
```

**说明**：释放（s 可为 NULL）

### `bigdecimal_ball_series_from_array`

```c
bigdecimal_err_ty bigdecimal_ball_series_from_array( bigdecimal_ball_series_ty *dst, int64_t val, const bigdecimal_ball_ty *coeffs, size_t len);
```

**说明**：由球系数数组构造（深拷贝，去尾零）

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法返回 BIGDECIMAL_ERR_INVALID_E； 内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ball_series_copy`

```c
bigdecimal_err_ty bigdecimal_ball_series_copy(bigdecimal_ball_series_ty *dst, const bigdecimal_ball_series_ty *src);
```

**说明**：深拷贝

**返回**：成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ball_series_add`

```c
bigdecimal_err_ty bigdecimal_ball_series_add(bigdecimal_ball_series_ty *dst, const bigdecimal_ball_series_ty *lhs, const bigdecimal_ball_series_ty *rhs, size_t order, const bigdecimal_ctx_ty *ctx);
```

**说明**：加法 dst = lhs + rhs（截断到 order）

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法返回 BIGDECIMAL_ERR_INVALID_E； 内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ball_series_sub`

```c
bigdecimal_err_ty bigdecimal_ball_series_sub(bigdecimal_ball_series_ty *dst, const bigdecimal_ball_series_ty *lhs, const bigdecimal_ball_series_ty *rhs, size_t order, const bigdecimal_ctx_ty *ctx);
```

**说明**：减法 dst = lhs - rhs（截断到 order）

**返回**：同 add

### `bigdecimal_ball_series_mul`

```c
bigdecimal_err_ty bigdecimal_ball_series_mul(bigdecimal_ball_series_ty *dst, const bigdecimal_ball_series_ty *lhs, const bigdecimal_ball_series_ty *rhs, size_t order, const bigdecimal_ctx_ty *ctx);
```

**说明**：乘法 dst = lhs × rhs（截断到 order）

**返回**：同 add

### `bigdecimal_ball_series_eval`

```c
bigdecimal_err_ty bigdecimal_ball_series_eval(bigdecimal_ball_ty *out, const bigdecimal_ball_series_ty *s, const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：求值 out = Σ coeffs[i]·x^{val+i}（要求 val ≥ 0）

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法或 val < 0 返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（out 不变）

### `bigdecimal_ball_series_compose`

```c
bigdecimal_err_ty bigdecimal_ball_series_compose( bigdecimal_ball_series_ty *dst, const bigdecimal_ball_series_ty *f, const bigdecimal_ball_series_ty *g, size_t order, const bigdecimal_ctx_ty *ctx);
```

**说明**：复合 dst = f∘g = f(g(z))，截断到 order（秦九韶式）

**参数**
- `f`：要求 val == 0；g 要求 val ≥ 0

**返回**：成功返回 BIGDECIMAL_OK_E；参数非法返回 BIGDECIMAL_ERR_INVALID_E； 内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

NEX_BALL_H
