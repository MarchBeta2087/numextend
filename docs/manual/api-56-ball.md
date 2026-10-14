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

NEX_BALL_H
