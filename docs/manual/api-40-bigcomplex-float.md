# bigcomplex_float —— 二进制浮点复数


bigcomplex_float_ty：实部/虚部均为 bigfloat_ty 的复数（设计文档 §9）。

复数本身不新增标志；特殊值语义完全由分量的七标志组合表达（§9.3）。
API 与 bigcomplex_decimal_ty 平行（十进制版见 decimal/ 子目录）。


```c
typedef struct {
bigfloat_ty re;  // 实部
bigfloat_ty im;  // 虚部
} bigcomplex_float_ty;
```

bigcomplex（float 版）错误码

```c
typedef enum {
BIGCOMPLEX_FLOAT_OK_E = 0,          // 成功
BIGCOMPLEX_FLOAT_ERR_OOM_E,         // 内存分配失败
BIGCOMPLEX_FLOAT_ERR_INVALID_E,     // 非法参数
BIGCOMPLEX_FLOAT_ERR_OVERFLOW_E,    // 输出缓冲不足等
BIGCOMPLEX_FLOAT_ERR_PARSE_E,       // 字符串解析失败
BIGCOMPLEX_FLOAT_ERR_UNSUPPORTED_E  // 已登记但当前版本未实现的功能
} bigcomplex_float_err_ty;
```

## 生命周期与构造（§9.2）

### `bigcomplex_float_init`

```c
bigcomplex_float_err_ty bigcomplex_float_init(bigcomplex_float_ty *cpx);
```

**说明**：初始化为 0 + 0i

**返回**：成功返回 BIGCOMPLEX_FLOAT_OK_E；cpx 为 NULL 返回 INVALID

### `bigcomplex_float_free`

```c
void bigcomplex_float_free(bigcomplex_float_ty *cpx);
```

**说明**：释放复数占用的内存（两个分量各自释放）

**参数**
- `cpx`：目标对象；可为 NULL（安全无操作）

### `bigcomplex_float_copy`

```c
bigcomplex_float_err_ty bigcomplex_float_copy(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src);
```

**说明**：深拷贝复数

**返回**：成功返回 OK；内存不足返回 OOM（dst 不变）

### `bigcomplex_float_from_parts`

```c
bigcomplex_float_err_ty bigcomplex_float_from_parts(bigcomplex_float_ty *cpx, const bigfloat_ty *re, const bigfloat_ty *im);
```

**说明**：从实部/虚部构造复数（深拷贝分量）

**返回**：成功返回 OK；任一参数为 NULL 返回 INVALID（cpx 不变）

### `bigcomplex_float_from_str`

```c
bigcomplex_float_err_ty bigcomplex_float_from_str(bigcomplex_float_ty *cpx, const char *str, const bigfloat_ctx_ty *ctx, const char **end);
```

**说明**：从字符串解析复数

**参数**
- `str`：源字符串，接受 "a+bi"、"a-bi"、"a"、"bi" 形式 （分量本身支持十进制小数/科学计数/"inf"/"nan"）
- `end`：若非 NULL，返回首个未消费字符位置（部分消费容错）

**返回**：成功返回 OK；首字符即非法或虚部缺 'i' 后缀返回 PARSE （cpx 不变）；内存不足返回 OOM（cpx 不变）

**备注**："a+..." 形式要求虚部以 'i' 结尾，否则整字面量非法（PARSE）； 完整字面量后的尾随字符按部分消费约定处理

### `bigcomplex_float_to_str`

```c
bigcomplex_float_err_ty bigcomplex_float_to_str(const bigcomplex_float_ty *cpx, size_t max_digits, char *buf, size_t buf_len, size_t *needed);
```

**说明**：输出 "a+bi" 形式（分量按 bigfloat_to_str 的最短往返表示）

**参数**
- `max_digits`：分量的有效数字位数上限；0 表示不限制（透传分量）

**返回**：成功返回 OK；buf 不足返回 OVERFLOW（needed 仍被写出）

**备注**：输出规则：虚部为 +0 时仅输出实部；实部为 +0 时仅输出虚部 + "i"； 否则 "a±|b|i"（虚部负号用连接符 '-' 表达）

## 分量访问与一元运算（§9.2）

### `bigcomplex_float_conj`

```c
bigcomplex_float_err_ty bigcomplex_float_conj(bigcomplex_float_ty *cpx);
```

**说明**：共轭 cpx = conj(cpx)（虚部取负；NaN 虚部不变）

### `bigcomplex_float_abs`

```c
bigcomplex_float_err_ty bigcomplex_float_abs(bigfloat_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：模 |src| = sqrt(re² + im²)（分量运算，中间量可能过早溢出， 缩放算法列为后续方向，§9.2）

**返回**：成功返回 OK；内存不足返回 OOM（dst 不变）

### `bigcomplex_float_arg`

```c
bigcomplex_float_err_ty bigcomplex_float_arg(bigfloat_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：辐角 arg(src) = atan2(im, re)，主值 ∈ (−π, π]（正确舍入，§9.2/§16）

**参数**
- `dst`：输出（bigfloat）；src 输入复数；ctx 精度上下文

**返回**：成功返回 OK（任一分量为 NaN 时 dst 置 NaN）；参数非法或含 ±∞ 返回 INVALID；内存不足返回 OOM（dst 不变）

**备注**：经 bigfloat_atan2（球式半角规约 + Ziv）；负实轴（im=0, re<0）取 +π

## 超越函数（§D5）：分量运算，各步按 ctx 舍入

### `bigcomplex_float_exp`

```c
bigcomplex_float_err_ty bigcomplex_float_exp(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：复指数 exp(z) = e^re·(cos im + i sin im)

**返回**：成功返回 OK（NaN 分量传播为 (NaN,NaN)）；参数非法/含 ±∞ 返回 INVALID；内存不足返回 OOM（dst 不变）

### `bigcomplex_float_log`

```c
bigcomplex_float_err_ty bigcomplex_float_log(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：复对数 log(z) = log|z| + i·arg(z)（主值；z ≠ 0）

**返回**：同 exp；z = 0 返回 INVALID

### `bigcomplex_float_sqrt`

```c
bigcomplex_float_err_ty bigcomplex_float_sqrt(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：复平方根 sqrt(z)（主值，右半平面）

**返回**：同 exp

### `bigcomplex_float_sin`

```c
bigcomplex_float_err_ty bigcomplex_float_sin(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：复正弦 sin(z) = sin re·cosh im + i·cos re·sinh im

**返回**：同 exp（|im| 过大时 e^{|im|} 可能溢出）

### `bigcomplex_float_cos`

```c
bigcomplex_float_err_ty bigcomplex_float_cos(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：复余弦 cos(z) = cos re·cosh im − i·sin re·sinh im

**返回**：同 sin

### `bigcomplex_float_tan`

```c
bigcomplex_float_err_ty bigcomplex_float_tan(bigcomplex_float_ty *dst, const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);
```

**说明**：复正切 tan(z) = sin(z)/cos(z)

**返回**：同 exp；cos(z) = 0（极点）返回 INVALID

## 四则（§9.2）

### `bigcomplex_float_add`

```c
bigcomplex_float_err_ty bigcomplex_float_add(bigcomplex_float_ty *dst, const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs, const bigfloat_ctx_ty *ctx);
```

**说明**：加法 dst = lhs + rhs（分量加法）

### `bigcomplex_float_sub`

```c
bigcomplex_float_err_ty bigcomplex_float_sub(bigcomplex_float_ty *dst, const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs, const bigfloat_ctx_ty *ctx);
```

**说明**：减法 dst = lhs − rhs（分量减法）

### `bigcomplex_float_mul`

```c
bigcomplex_float_err_ty bigcomplex_float_mul(bigcomplex_float_ty *dst, const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs, const bigfloat_ctx_ty *ctx);
```

**说明**：乘法 dst = lhs × rhs

**备注**：(a+bi)(c+di) = (ac−bd) + (bc+ad)i；默认朴素四乘二加； 可选 Karatsuba 三乘式（`NEX_COMPLEX_KARATSUBA`，默认关闭，§9.2）

### `bigcomplex_float_div`

```c
bigcomplex_float_err_ty bigcomplex_float_div(bigcomplex_float_ty *dst, const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs, const bigfloat_ctx_ty *ctx);
```

**说明**：除法 dst = lhs / rhs

**备注**：有限输入用 Smith 稳定缩放（|c|≥|d| → r=d/c，否则 r=c/d），避免 c²+d² 过早溢出/下溢；任一分量为 ±∞ 时回退共轭法以维持特殊值语义。 各步按 ctx 正确舍入（§9.2），v1 按"分量独立求值后组合"的朴素语义

## 相等性（复数无序，不提供 cmp，§9.2）

### `bigcomplex_float_eq`

```c
bool bigcomplex_float_eq(const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs);
```

**说明**：相等判断（分量各自 eq；任一分量 NaN 则 false）

NEX_BIGCOMPLEX_FLOAT_H
