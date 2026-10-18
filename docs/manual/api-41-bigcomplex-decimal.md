# bigcomplex_decimal —— 十进制浮点复数


bigcomplex_decimal_ty：实部/虚部均为 bigdecimal_ty 的复数（设计文档 §9）。

与 bigcomplex_float_ty 平行（float 版见 float/ 子目录）：复数本身不新增
标志，特殊值语义完全由分量的七标志组合表达（§9.3）。


```c
typedef struct {
bigdecimal_ty re;  // 实部
bigdecimal_ty im;  // 虚部
} bigcomplex_decimal_ty;
```

bigcomplex（decimal 版）错误码

```c
typedef enum {
BIGCOMPLEX_DECIMAL_OK_E = 0,          // 成功
BIGCOMPLEX_DECIMAL_ERR_OOM_E,         // 内存分配失败
BIGCOMPLEX_DECIMAL_ERR_INVALID_E,     // 非法参数
BIGCOMPLEX_DECIMAL_ERR_OVERFLOW_E,    // 输出缓冲不足等
BIGCOMPLEX_DECIMAL_ERR_PARSE_E,       // 字符串解析失败
BIGCOMPLEX_DECIMAL_ERR_UNSUPPORTED_E  // 已登记但当前版本未实现的功能
} bigcomplex_decimal_err_ty;
```

## 生命周期与构造（§9.2）

### `bigcomplex_decimal_init`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_init(bigcomplex_decimal_ty *cpx);
```

**说明**：初始化为 0 + 0i

**返回**：成功返回 BIGCOMPLEX_DECIMAL_OK_E；cpx 为 NULL 返回 INVALID

### `bigcomplex_decimal_free`

```c
void bigcomplex_decimal_free(bigcomplex_decimal_ty *cpx);
```

**说明**：释放复数占用的内存（两个分量各自释放）

**参数**
- `cpx`：目标对象；可为 NULL（安全无操作）

### `bigcomplex_decimal_copy`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_copy(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src);
```

**说明**：深拷贝复数

**返回**：成功返回 OK；内存不足返回 OOM（dst 不变）

### `bigcomplex_decimal_from_parts`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_from_parts( bigcomplex_decimal_ty *cpx, const bigdecimal_ty *re, const bigdecimal_ty *im);
```

**说明**：从实部/虚部构造复数（深拷贝分量）

**返回**：成功返回 OK；任一参数为 NULL 返回 INVALID（cpx 不变）

### `bigcomplex_decimal_from_str`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_from_str( bigcomplex_decimal_ty *cpx, const char *str, const bigdecimal_ctx_ty *ctx, const char **end);
```

**说明**：从字符串解析复数

**参数**
- `str`：源字符串，接受 "a+bi"、"a-bi"、"a"、"bi" 形式 （分量本身支持十进制小数/科学计数/"inf"/"nan"）
- `end`：若非 NULL，返回首个未消费字符位置（部分消费容错）

**返回**：成功返回 OK；首字符即非法或虚部缺 'i' 后缀返回 PARSE （cpx 不变）；内存不足返回 OOM（cpx 不变）

**备注**："a+..." 形式要求虚部以 'i' 结尾，否则整字面量非法（PARSE）； 完整字面量后的尾随字符按部分消费约定处理

### `bigcomplex_decimal_to_str`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_to_str( const bigcomplex_decimal_ty *cpx, bigdecimal_fmt_ty fmt, char *buf, size_t buf_len, size_t *needed);
```

**说明**：输出 "a+bi" 形式（分量按 bigdecimal_to_str 精确展开）

**参数**
- `fmt`：分量输出格式（定点 / 科学计数，透传分量）

**返回**：成功返回 OK；buf 不足返回 OVERFLOW（needed 仍被写出）

**备注**：输出规则同 float 版：虚部为 +0 时仅输出实部；实部为 +0 时仅 输出虚部 + "i"；否则 "a±|b|i"

## 分量访问与一元运算（§9.2）

### `bigcomplex_decimal_conj`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_conj(bigcomplex_decimal_ty *cpx);
```

**说明**：共轭 cpx = conj(cpx)（虚部取负；NaN 虚部不变）

### `bigcomplex_decimal_abs`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_abs(bigdecimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：模 |src| = sqrt(re² + im²)（分量运算，中间量可能过早溢出， 缩放算法列为后续方向，§9.2）

### `bigcomplex_decimal_arg`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_arg(bigdecimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：辐角 arg(src) = atan2(im, re)，主值 ∈ (−π, π]（正确舍入，§9.2/§16）

**返回**：成功返回 OK（任一分量为 NaN 时 dst 置 NaN）；参数非法或含 ±∞ 返回 INVALID；内存不足返回 OOM（dst 不变）

**备注**：经 bigdecimal_atan2（球式半角规约 + Ziv）；负实轴（im=0, re<0）取 +π

## 超越函数（§D5）：分量运算，各步按 ctx 舍入

### `bigcomplex_decimal_exp`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_exp(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：复指数 exp(z) = e^re·(cos im + i sin im)（十进制版）

**返回**：成功返回 OK（NaN 分量传播为 (NaN,NaN)）；参数非法/含 ±∞ 返回 INVALID；内存不足返回 OOM（dst 不变）

### `bigcomplex_decimal_log`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_log(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：复对数 log(z) = log|z| + i·arg(z)（主值；z ≠ 0，十进制版）

**返回**：同 exp；z = 0 返回 INVALID

### `bigcomplex_decimal_sqrt`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_sqrt(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：复平方根 sqrt(z)（主值，右半平面，十进制版）

**返回**：同 exp

### `bigcomplex_decimal_sin`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_sin(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：复正弦 sin(z) = sin re·cosh im + i·cos re·sinh im（十进制版）

**返回**：同 exp（|im| 过大时 e^{|im|} 可能溢出）

### `bigcomplex_decimal_cos`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_cos(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：复余弦 cos(z) = cos re·cosh im − i·sin re·sinh im（十进制版）

**返回**：同 sin

### `bigcomplex_decimal_tan`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_tan(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);
```

**说明**：复正切 tan(z) = sin(z)/cos(z)（十进制版）

**返回**：同 exp；cos(z) = 0（极点）返回 INVALID

## 四则（§9.2）

### `bigcomplex_decimal_add`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_add(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：加法 dst = lhs + rhs（分量加法）

### `bigcomplex_decimal_sub`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_sub(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：减法 dst = lhs − rhs（分量减法）

### `bigcomplex_decimal_mul`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_mul(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：乘法 dst = lhs × rhs（分量四乘二加，§9.2）

### `bigcomplex_decimal_div`

```c
bigcomplex_decimal_err_ty bigcomplex_decimal_div(bigcomplex_decimal_ty *dst, const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs, const bigdecimal_ctx_ty *ctx);
```

**说明**：除法 dst = lhs / rhs

**备注**：有限输入用 Smith 稳定缩放，避免 c²+d² 越过指数范围；任一分量为 ±∞ 时回退共轭法；各步按 ctx 正确舍入（§9.2）

## 相等性（复数无序，不提供 cmp，§9.2）

### `bigcomplex_decimal_eq`

```c
bool bigcomplex_decimal_eq(const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs);
```

**说明**：相等判断（分量各自 eq；任一分量 NaN 则 false）

NEX_BIGCOMPLEX_DECIMAL_H
