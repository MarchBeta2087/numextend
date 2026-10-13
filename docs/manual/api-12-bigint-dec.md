# bigint_dec —— 十进制肢任意精度整数


bigint_dec_ty：十进制肢（基 10^9）任意精度整数（设计文档 §5）。

数值 = sign × Σ(limbs[i] × 10^(9·i))，limbs 小端序，每肢取值 0..999999999。
规范化不变式（所有公开 API 的输出均满足）：
- 最高有效肢 limbs[len-1] != 0（取值 1..999999999）；
- 零的唯一表示为 len == 0 且 sign == BIGINT_SIGN_ZERO_E
（此时 limbs 可为 NULL 或保留容量）。

与 bigint_bin 的差异（§5.2）：
- 不提供位运算一族与 bit_len / popcount（位语义请转 bin）；
- 字符串 I/O 仅支持十进制（base 必须为 10，其余返回
BIGINT_ERR_INVALID_E）；
- 新增十进制移位与度量：mul_pow10 / div_pow10 / digit_len。

通用约定：
- 所有算术函数允许 dst 与任一源操作数别名（aliasing）；
- 任何因 OOM 或非法参数失败的运算，输出参数保持调用前状态。


```c
typedef struct {
bigint_sign_ty sign;  // 三标志：正 / 负 / 零
uint32_t *limbs;      // 肢数组，小端序，基 10^9（每肢 0..999999999）
size_t len;           // 当前有效肢数
size_t cap;           // 已分配容量（肢数）
} bigint_dec_ty;
```

## 生命周期

### `bigint_dec_init`

```c
bigint_err_ty bigint_dec_init(bigint_dec_ty *val);
```

**说明**：初始化大整数为零

**参数**
- `val`：需要初始化的大整数变量

**返回**：成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E

### `bigint_dec_init_cap`

```c
bigint_err_ty bigint_dec_init_cap(bigint_dec_ty *val, size_t cap);
```

**说明**：初始化大整数为零并预分配容量

**参数**
- `val`：需要初始化的大整数变量
- `cap`：预分配的肢容量；为 0 时等价于 bigint_dec_init

**返回**：成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E； 内存不足返回 BIGINT_ERR_OOM_E

### `bigint_dec_free`

```c
void bigint_dec_free(bigint_dec_ty *val);
```

**说明**：释放大整数占用的内存

**参数**
- `val`：需要释放的大整数变量；可为 NULL 或零初始化对象（安全无操作）

### `bigint_dec_copy`

```c
bigint_err_ty bigint_dec_copy(bigint_dec_ty *dst, const bigint_dec_ty *src);
```

**说明**：深拷贝大整数

**参数**
- `dst`：目标对象（已初始化）
- `src`：源对象

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

### `bigint_dec_move`

```c
void bigint_dec_move(bigint_dec_ty *dst, bigint_dec_ty *src);
```

**说明**：移动大整数，dst 接管 src 的资源

**参数**
- `dst`：目标对象（已初始化，其原有资源被释放）
- `src`：源对象，调用后被重置为零（len == 0，cap == 0，limbs == NULL）

### `bigint_dec_shrink`

```c
bigint_err_ty bigint_dec_shrink(bigint_dec_ty *val);
```

**说明**：将容量收缩至当前有效肢数

**参数**
- `val`：目标对象

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）

**备注**：len == 0 时释放全部容量

## 与基本类型互转

### `bigint_dec_from_u64`

```c
bigint_err_ty bigint_dec_from_u64(bigint_dec_ty *val, uint64_t value);
```

**说明**：以 uint64_t 赋值

**参数**
- `val`：目标对象（已初始化）
- `value`：源数值

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）

### `bigint_dec_from_i64`

```c
bigint_err_ty bigint_dec_from_i64(bigint_dec_ty *val, int64_t value);
```

**说明**：以 int64_t 赋值

**参数**
- `val`：目标对象（已初始化）
- `value`：源数值

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）

### `bigint_dec_to_u64`

```c
bigint_err_ty bigint_dec_to_u64(const bigint_dec_ty *val, uint64_t *out);
```

**说明**：转为 uint64_t

**参数**
- `val`：源对象
- `out`：输出数值

**返回**：成功返回 BIGINT_OK_E；负数或超出 uint64_t 范围返回 BIGINT_ERR_OVERFLOW_E（out 不被修改）

### `bigint_dec_to_i64`

```c
bigint_err_ty bigint_dec_to_i64(const bigint_dec_ty *val, int64_t *out);
```

**说明**：转为 int64_t

**参数**
- `val`：源对象
- `out`：输出数值

**返回**：成功返回 BIGINT_OK_E；超出 int64_t 范围返回 BIGINT_ERR_OVERFLOW_E（out 不被修改）

### `bigint_dec_from_str`

```c
bigint_err_ty bigint_dec_from_str(bigint_dec_ty *val, const char *str, uint32_t base, const char **end);
```

**说明**：从十进制字符串解析（数字 0-9）

**参数**
- `val`：目标对象（已初始化）
- `str`：源字符串，可选前导 '-'；不跳过空白
- `base`：进制，仅接受 10；其余取值返回 BIGINT_ERR_INVALID_E
- `end`：若非 NULL，返回首个未消费字符位置

**返回**：成功返回 BIGINT_OK_E；首字符即非法（无可消费前缀）返回 BIGINT_ERR_PARSE_E 且 val 不变；base 非 10 返回 BIGINT_ERR_INVALID_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）

**备注**：部分消费容错——尽可能多地消费合法数字（设计文档 §11）

### `bigint_dec_to_str`

```c
bigint_err_ty bigint_dec_to_str(const bigint_dec_ty *val, uint32_t base, char *buf, size_t buf_len, size_t *needed);
```

**说明**：转为十进制字符串（负数带 '-' 前缀）

**参数**
- `val`：源对象
- `base`：进制，仅接受 10；其余取值返回 BIGINT_ERR_INVALID_E
- `buf`：输出缓冲区；可为 NULL 仅查询所需长度
- `buf_len`：缓冲区长度
- `needed`：若非 NULL，传出含 '\0' 的所需长度

**返回**：成功返回 BIGINT_OK_E；buf 不足返回 BIGINT_ERR_OVERFLOW_E （buf 内容未定义，needed 仍被写出）；base 非 10 返回 BIGINT_ERR_INVALID_E

**备注**：每肢恰好 9 位十进制数字（首肢去前导零），O(n) 格式化（§5.2）

## 比较与断言

### `bigint_dec_cmp`

```c
int bigint_dec_cmp(const bigint_dec_ty *lhs, const bigint_dec_ty *rhs);
```

**说明**：比较两个大整数

**返回**：lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正

### `bigint_dec_cmp_abs`

```c
int bigint_dec_cmp_abs(const bigint_dec_ty *lhs, const bigint_dec_ty *rhs);
```

**说明**：比较两个大整数的绝对值

**返回**：|lhs| < |rhs| 为负，相等为 0，|lhs| > |rhs| 为正

### `bigint_dec_sign`

```c
bigint_sign_ty bigint_dec_sign(const bigint_dec_ty *val);
```

**说明**：读取符号标志

### `bigint_dec_is_zero`

```c
bool bigint_dec_is_zero(const bigint_dec_ty *val);
```

**说明**：判断是否为零

## 四则与幂

### `bigint_dec_add`

```c
bigint_err_ty bigint_dec_add(bigint_dec_ty *dst, const bigint_dec_ty *lhs, const bigint_dec_ty *rhs);
```

**说明**：加法 dst = lhs + rhs

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：dst 允许与 lhs / rhs 别名

### `bigint_dec_sub`

```c
bigint_err_ty bigint_dec_sub(bigint_dec_ty *dst, const bigint_dec_ty *lhs, const bigint_dec_ty *rhs);
```

**说明**：减法 dst = lhs - rhs

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：dst 允许与 lhs / rhs 别名

### `bigint_dec_mul`

```c
bigint_err_ty bigint_dec_mul(bigint_dec_ty *dst, const bigint_dec_ty *lhs, const bigint_dec_ty *rhs);
```

**说明**：乘法 dst = lhs × rhs，按肢数阈值自动选择算法

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：等价于 method 为 { BIGINT_MUL_AUTO_E, {0} } 的 mul_ex；允许别名

### `bigint_dec_mul_ex`

```c
bigint_err_ty bigint_dec_mul_ex(bigint_dec_ty *dst, const bigint_dec_ty *lhs, const bigint_dec_ty *rhs, const bigint_mul_method_ty *method);
```

**说明**：带方法选择的大整数乘法

**参数**
- `dst`：目标对象，允许与源操作数别名
- `method`：方法选择；为 NULL 等价于 AUTO

**返回**：algo 非法或与 params 不匹配返回 BIGINT_ERR_INVALID_E； algo 已登记但当前版本未实现返回 BIGINT_ERR_UNSUPPORTED_E； 内存不足返回 BIGINT_ERR_OOM_E；成功返回 BIGINT_OK_E

### `bigint_dec_div_rem`

```c
bigint_err_ty bigint_dec_div_rem(bigint_dec_ty *quot, bigint_dec_ty *rem, const bigint_dec_ty *lhs, const bigint_dec_ty *rhs);
```

**说明**：带余除法，lhs = quot × rhs + rem（截断除法，与 C99 整数除法语义相同）

**参数**
- `quot`：商，可为 NULL 表示不接收
- `rem`：余数，可为 NULL 表示不接收；符号与被除数一致

**返回**：除数为零返回 BIGINT_ERR_DIV_ZERO_E（quot / rem 不被修改）； 内存不足返回 BIGINT_ERR_OOM_E；成功返回 BIGINT_OK_E

**备注**：quot / rem 允许与 lhs / rhs 别名

### `bigint_dec_neg`

```c
bigint_err_ty bigint_dec_neg(bigint_dec_ty *val);
```

**说明**：就地取负（零不变）

**返回**：恒为 BIGINT_OK_E

### `bigint_dec_abs`

```c
bigint_err_ty bigint_dec_abs(bigint_dec_ty *val);
```

**说明**：就地取绝对值

**返回**：恒为 BIGINT_OK_E

### `bigint_dec_pow`

```c
bigint_err_ty bigint_dec_pow(bigint_dec_ty *dst, const bigint_dec_ty *base, uint64_t exp);
```

**说明**：幂 dst = base^exp

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：约定 0^0 = 1；dst 允许与 base 别名

### `bigint_dec_pow_mod`

```c
bigint_err_ty bigint_dec_pow_mod(bigint_dec_ty *dst, const bigint_dec_ty *base, const bigint_dec_ty *exp, const bigint_dec_ty *mod);
```

**说明**：模幂 dst = base^exp mod mod，结果取值 [0, mod)

**返回**：mod 为零返回 BIGINT_ERR_DIV_ZERO_E；exp 为负或 mod 为负返回 BIGINT_ERR_INVALID_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）

**备注**：dst 允许与任一源操作数别名

## 十进制移位与度量（dec 专有，bigdecimal 指数调整的核心原语，§5.2）

### `bigint_dec_mul_pow10`

```c
bigint_err_ty bigint_dec_mul_pow10(bigint_dec_ty *val, size_t digits);
```

**说明**：就地乘以 10^digits（符号不变；零不变）

**参数**
- `val`：目标对象
- `digits`：十进制位数，先按 9 整肢移动，余数（0..8）走单肢乘

**返回**：成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）

### `bigint_dec_div_pow10`

```c
bigint_err_ty bigint_dec_div_pow10(bigint_dec_ty *val, size_t digits);
```

**说明**：就地截断除以 10^digits（向零取整；结果为零时符号规范化为零）

**参数**
- `val`：目标对象
- `digits`：十进制位数，先按 9 整肢移动，余数（0..8）走单肢除

**返回**：恒为 BIGINT_OK_E（不分配内存）

### `bigint_dec_digit_len`

```c
size_t bigint_dec_digit_len(const bigint_dec_ty *val);
```

**说明**：幅值的十进制位数（零为 0，与 bigint_bin_bit_len 的零语义对齐）

NEX_BIGINT_DEC_H
