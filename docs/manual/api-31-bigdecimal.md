# bigdecimal —— 可调精度十进制浮点


bigdecimal_ty：可调精度十进制浮点数（设计文档 §8）。

语义模型：
- 正常值：value = (−1)^sign × mant × 10^exp，mant 为正整数且
**个位非 0**（规范化：无尾随十进制零），exp 为十进制指数；
- 特殊值：+0 / −0 / +∞ / −∞ / NaN，由 flag 独立表达；
- 表示唯一：给定"无尾随零"规范化要求，数值到 (mant, exp) 是双射
（与 bigfloat 的"最高位恒 1"平行，§8.1）。

常量约定：
- BIGDECIMAL_MAX_EXP_DIGITS 限制 exp_digits 上界，保证
±(10^exp_digits − 1) 不溢出 int64（10^18 − 1 < INT64_MAX）。


七标志：±0 / 正常值 / ±∞ / NaN

```c
typedef enum {
BIGDECIMAL_POS_ZERO_E = 0,  // +0
BIGDECIMAL_NEG_ZERO_E,      // −0
BIGDECIMAL_POS_E,           // 正（正常值）
BIGDECIMAL_NEG_E,           // 负（正常值）
BIGDECIMAL_POS_INF_E,       // 正无穷
BIGDECIMAL_NEG_INF_E,       // 负无穷
BIGDECIMAL_NAN_E            // 非数
} bigdecimal_flag_ty;
```

五种舍入模式

```c
typedef enum {
BIGDECIMAL_ROUND_NEAREST_EVEN_E = 0,  // 最近舍入，平局取偶（默认，同 IEEE 754）
BIGDECIMAL_ROUND_TOWARD_ZERO_E,       // 向零截断
BIGDECIMAL_ROUND_TOWARD_POS_E,        // 向 +∞
BIGDECIMAL_ROUND_TOWARD_NEG_E,        // 向 −∞
BIGDECIMAL_ROUND_AWAY_ZERO_E          // 远离零
} bigdecimal_round_ty;
```

to_str 输出格式

```c
typedef enum {
BIGDECIMAL_FMT_FIXED_E = 0,      // 定点：如 "12.34"、"0.00123"
BIGDECIMAL_FMT_SCIENTIFIC_E      // 科学计数：如 "1.234e+1"、"1e+100"
} bigdecimal_fmt_ty;
```

bigdecimal 层错误码

```c
typedef enum {
BIGDECIMAL_OK_E = 0,          // 成功
BIGDECIMAL_ERR_OOM_E,         // 内存分配失败
BIGDECIMAL_ERR_INVALID_E,     // 非法参数（空指针、非法进制等）
BIGDECIMAL_ERR_OVERFLOW_E,    // 输出缓冲不足等
BIGDECIMAL_ERR_PARSE_E        // 字符串解析失败
} bigdecimal_err_ty;
```

精度上下文：决定结果的舍入与范围

```c
typedef struct {
size_t mant_digits;        // 十进制有效位数，必须 ≥ 1（对应 IEEE 754 十进制格式的 p）
size_t exp_digits;         // 指数字段的十进制位数，决定范围 ±(10^exp_digits − 1)
bigdecimal_round_ty round; // 舍入模式
} bigdecimal_ctx_ty;
```

可调精度十进制浮点数

```c
typedef struct {
bigdecimal_flag_ty flag;  // 七标志
bigint_dec_ty mant;       // 尾数幅值（十进制整数）；仅正常值有效，
// 规范化：个位非 0（无尾随零）
int64_t exp;              // 十进制指数；仅正常值时有效
} bigdecimal_ty;
```

exp_digits 上界：10^18 − 1 < INT64_MAX，保证 emin 计算不溢出 int64

## 生命周期与上下文

### `bigdecimal_init`

```c
bigdecimal_err_ty bigdecimal_init(bigdecimal_ty *val);
```

**说明**：初始化为 +0

**返回**：成功返回 BIGDECIMAL_OK_E；val 为 NULL 返回 BIGDECIMAL_ERR_INVALID_E

### `bigdecimal_free`

```c
void bigdecimal_free(bigdecimal_ty *val);
```

**说明**：释放浮点数占用的内存（mant 的内部 bigint）

**参数**
- `val`：目标对象；可为 NULL（安全无操作）

### `bigdecimal_copy`

```c
bigdecimal_err_ty bigdecimal_copy(bigdecimal_ty *dst, const bigdecimal_ty *src);
```

**说明**：深拷贝浮点数

**返回**：成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_ctx_make`

```c
bigdecimal_err_ty bigdecimal_ctx_make(bigdecimal_ctx_ty *dst, size_t mant_digits, size_t exp_digits, bigdecimal_round_ty round);
```

**说明**：构造精度上下文

**参数**
- `mant_digits`：十进制有效位数，必须 ≥ 1
- `exp_digits`：指数字段的十进制位数，决定可表示指数范围 [emin, emax] = [−(10^exp_digits − 1), 10^exp_digits − 1]
- `round`：舍入模式

**返回**：成功返回 BIGDECIMAL_OK_E；任意参数为 NULL 返回 BIGDECIMAL_ERR_INVALID_E（dst 不变）；mant_digits < 1 或 exp_digits == 0 或 exp_digits > BIGDECIMAL_MAX_EXP_DIGITS 返回 BIGDECIMAL_ERR_INVALID_E（dst 不变）

### `bigdecimal_ctx_decimal32`

```c
bigdecimal_ctx_ty bigdecimal_ctx_decimal32(void);
```

**说明**：常用预设上下文：IEEE 754 decimal32（7 位有效数字，指数 ±99）

### `bigdecimal_ctx_decimal64`

```c
bigdecimal_ctx_ty bigdecimal_ctx_decimal64(void);
```

**说明**：常用预设上下文：IEEE 754 decimal64（16 位有效数字，指数 ±999）

### `bigdecimal_ctx_decimal128`

```c
bigdecimal_ctx_ty bigdecimal_ctx_decimal128(void);
```

**说明**：常用预设上下文：IEEE 754 decimal128（34 位有效数字，指数 ±9999）

## 分类断言（纯标志判断，不失败）

### `bigdecimal_is_zero`

```c
bool bigdecimal_is_zero(const bigdecimal_ty *val);
```

**说明**：判断是否为零（+0 或 −0）

### `bigdecimal_is_inf`

```c
bool bigdecimal_is_inf(const bigdecimal_ty *val);
```

**说明**：判断是否为无穷（+∞ 或 −∞）

### `bigdecimal_is_nan`

```c
bool bigdecimal_is_nan(const bigdecimal_ty *val);
```

**说明**：判断是否为 NaN

### `bigdecimal_is_normal`

```c
bool bigdecimal_is_normal(const bigdecimal_ty *val);
```

**说明**：判断是否为正常值（POS_E 或 NEG_E）

## 构造与转换

### `bigdecimal_from_bigint`

```c
bigdecimal_err_ty bigdecimal_from_bigint(bigdecimal_ty *dst, const bigint_dec_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：从大整数（十进制肢）构造浮点数，按 ctx 舍入；精确值可表示时无损

**返回**：成功返回 BIGDECIMAL_OK_E；src 为 NULL 返回 BIGDECIMAL_ERR_INVALID_E（dst 不变）；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

### `bigdecimal_from_str`

```c
bigdecimal_err_ty bigdecimal_from_str(bigdecimal_ty *dst, const char *str, const bigdecimal_ctx_ty *ctx, const char **end);
```

**说明**：从字符串解析浮点数（精确解析）

**参数**
- `str`：源字符串，接受十进制小数/科学计数法、"inf"、"nan" （可选前导 '-'/'+'）；不跳过空白
- `end`：若非 NULL，返回首个未消费字符位置（部分消费容错）

**返回**：成功返回 BIGDECIMAL_OK_E；首字符即非法返回 BIGDECIMAL_ERR_PARSE_E 且 dst 不变；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）

**备注**：有效数字不超过 ctx->mant_digits 时完全精确；超长时按 ctx 正确 舍入（一次舍入，无双重舍入）。"0x..." 十六进制不在 v1 范围

### `bigdecimal_to_str`

```c
bigdecimal_err_ty bigdecimal_to_str(const bigdecimal_ty *src, bigdecimal_fmt_ty fmt, char *buf, size_t buf_len, size_t *needed);
```

**说明**：精确输出十进制表示（value = mant × 10^exp 直接展开，无舍入）

**参数**
- `fmt`：输出格式：定点（BIGDECIMAL_FMT_FIXED_E）或科学计数 （BIGDECIMAL_FMT_SCIENTIFIC_E："d[.ddd]e±X"）
- `buf`：输出缓冲区；可为 NULL 仅查询所需长度
- `needed`：若非 NULL，传出含 '\0' 的所需长度

**返回**：成功返回 BIGDECIMAL_OK_E；buf 不足返回 BIGDECIMAL_ERR_OVERFLOW_E（needed 仍被写出）

**备注**：特殊值输出 "0" / "-0" / "inf" / "-inf" / "nan"（与 fmt 无关）

## 算术（特殊值传播见 §11；有限结果的舍入与溢出见 §7.3 / §8.1）

### `bigdecimal_add`

```c
bigdecimal_err_ty bigdecimal_add(bigdecimal_ty *dst, const bigdecimal_ty *lhs, const bigdecimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：加法 dst = lhs + rhs

### `bigdecimal_sub`

```c
bigdecimal_err_ty bigdecimal_sub(bigdecimal_ty *dst, const bigdecimal_ty *lhs, const bigdecimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：减法 dst = lhs − rhs

### `bigdecimal_mul`

```c
bigdecimal_err_ty bigdecimal_mul(bigdecimal_ty *dst, const bigdecimal_ty *lhs, const bigdecimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：乘法 dst = lhs × rhs

### `bigdecimal_div`

```c
bigdecimal_err_ty bigdecimal_div(bigdecimal_ty *dst, const bigdecimal_ty *lhs, const bigdecimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：除法 dst = lhs / rhs

### `bigdecimal_sqrt`

```c
bigdecimal_err_ty bigdecimal_sqrt(bigdecimal_ty *dst, const bigdecimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：平方根 dst = sqrt(src)；负数产生 NaN

### `bigdecimal_neg`

```c
bigdecimal_err_ty bigdecimal_neg(bigdecimal_ty *val);
```

**说明**：就地取负（翻转符号标志，含 ±0、±∞；NaN 不变）

## 比较：+0 == −0；NaN 与任何值（含自身）比较均"不相等"，

cmp 遇 NaN 返回约定值 2

### `bigdecimal_cmp`

```c
int bigdecimal_cmp(const bigdecimal_ty *lhs, const bigdecimal_ty *rhs);
```

**说明**：三路比较

**返回**：lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正； 任一操作数为 NaN 返回 2

### `bigdecimal_eq`

```c
bool bigdecimal_eq(const bigdecimal_ty *lhs, const bigdecimal_ty *rhs);
```

**说明**：相等判断（+0 == −0 为真；NaN 与任何值均不相等）

## 分解与合成

### `bigdecimal_decompose`

```c
bigdecimal_err_ty bigdecimal_decompose(const bigdecimal_ty *src, bigint_dec_ty *mant, int64_t *exp, bigdecimal_flag_ty *flag);
```

**说明**：分解浮点数为尾数、指数与标志

**参数**
- `mant`：传出尾数幅值（正常值时个位非 0；特殊值传出 0）
- `exp`：传出十进制指数；特殊值传出 0
- `flag`：传出七标志；可为 NULL 表示不接收

**返回**：成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E （输出参数不变）

### `bigdecimal_compose`

```c
bigdecimal_err_ty bigdecimal_compose(bigdecimal_ty *dst, const bigint_dec_ty *mant, int64_t exp, bigdecimal_flag_ty flag, const bigdecimal_ctx_ty *ctx);
```

**说明**：合成浮点数，按 ctx 对尾数舍入/规范化

**参数**
- `mant`：尾数幅值（可为任意非负整数；零则结果为 ±0，符号由 flag 决定）
- `exp`：十进制指数
- `flag`：七标志

**返回**：成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E （dst 不变）

**备注**：若 flag 为特殊值，mant / exp 被忽略，结果为对应特殊值

### `bigdecimal_poly_eval`

```c
bigdecimal_err_ty bigdecimal_poly_eval(bigdecimal_ty *out, const bigdecimal_ty *coeffs, size_t degree, const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);
```

**说明**：多项式求值（秦九韶 / Horner 法）：out = Σ_{i=0}^{degree} coeffs[i]·x^i

**参数**
- `out`：输出对象
- `coeffs`：系数数组，共 degree+1 项，coeffs[i] 为 x^i 的系数
- `degree`：最高次幂（coeffs 长度须为 degree+1）
- `x`：自变量
- `ctx`：精度上下文（每步乘加按此正确舍入）

**返回**：成功返回 BIGDECIMAL_OK_E；任一指针为 NULL 返回 BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（out 不变）

**备注**：整体为 Horner 递推结果（逐次正确舍入），非对多项式值的单次正确舍入

NEX_BIGDECIMAL_H
