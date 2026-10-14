# bigfrac —— 任意精度有理数


bigfrac_ty：任意精度有理数（既约分数）（设计文档 §6）。

规范化不变式（所有公开 API 的输出均满足，§3.4）：
- den > 0（符号由分子 num 承担）；
- gcd(|num|, den) == 1（既约）；
- 零的唯一表示为 0/1。

选择 bigint_bin 而非 bigint_dec 作为分量（§6.1）：有理数运算以乘除与
gcd 为主，二进制基效率更高；十进制输入在解析边界一次性转换为 bin。

通用约定：
- 所有算术函数允许 dst 与任一源操作数别名（aliasing）；
- 任何因 OOM 或非法参数失败的运算，输出参数保持调用前状态（§11）。

待办（依赖 bigfloat / bigdecimal 模块，落地后补充）：
- bigfrac_to_bigfloat / bigfrac_to_bigdecimal（§6.2 有损转换）。


```c
typedef struct {
bigint_bin_ty num;  // 分子，符号由 num 承担
bigint_bin_ty den;  // 分母，恒为正（sign == BIGINT_SIGN_POS_E）
} bigfrac_ty;
```

bigfrac 层错误码（§3.1：与 bigint 层同名分类一一映射）。 BIGFRAC_ERR_UNSUPPORTED_E 为上层模块预留（to_bigfloat 等落地后使用）。

```c
typedef enum {
BIGFRAC_OK_E = 0,          // 成功
BIGFRAC_ERR_OOM_E,         // 内存分配失败
BIGFRAC_ERR_INVALID_E,     // 非法参数（空指针等）
BIGFRAC_ERR_DIV_ZERO_E,    // 除以零（from_ints 分母为零、div 除数为零、inv(0)）
BIGFRAC_ERR_PARSE_E,       // 字符串解析失败（首字符即非法，无可消费前缀）
BIGFRAC_ERR_UNSUPPORTED_E  // 功能已预留但当前版本未实现
} bigfrac_err_ty;
```

## 生命周期

### `bigfrac_init`

```c
bigfrac_err_ty bigfrac_init(bigfrac_ty *frac);
```

**说明**：初始化为零（规范表示 0/1）

**参数**
- `frac`：需要初始化的有理数变量

**返回**：成功返回 BIGFRAC_OK_E；frac 为 NULL 返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E

### `bigfrac_free`

```c
void bigfrac_free(bigfrac_ty *frac);
```

**说明**：释放有理数占用的内存

**参数**
- `frac`：需要释放的有理数变量；可为 NULL 或零初始化对象（安全无操作）

### `bigfrac_copy`

```c
bigfrac_err_ty bigfrac_copy(bigfrac_ty *dst, const bigfrac_ty *src);
```

**说明**：深拷贝有理数

**参数**
- `dst`：目标对象（已初始化）
- `src`：源对象

**返回**：成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

## 构造

### `bigfrac_from_ints`

```c
bigfrac_err_ty bigfrac_from_ints(bigfrac_ty *frac, const bigint_bin_ty *num, const bigint_bin_ty *den);
```

**说明**：以分子分母构造有理数 frac = num / den，自动约分并归一符号

**参数**
- `frac`：目标对象（已初始化）
- `num`：分子（可为负或零）
- `den`：分母，零返回 BIGFRAC_ERR_DIV_ZERO_E（frac 不变）；负分母 自动将符号转移到分子

**返回**：成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（frac 不变）

### `bigfrac_from_str`

```c
bigfrac_err_ty bigfrac_from_str(bigfrac_ty *frac, const char *str, const char **end);
```

**说明**：从字符串解析有理数（仅十进制）

**参数**
- `frac`：目标对象（已初始化）
- `str`：源字符串，可选前导 '-'；不跳过空白
- `end`：若非 NULL，返回首个未消费字符位置

**返回**：成功返回 BIGFRAC_OK_E；首字符即非法（无可消费前缀）返回 BIGFRAC_ERR_PARSE_E 且 frac 不变；"p/0" 返回 BIGFRAC_ERR_DIV_ZERO_E； 内存不足返回 BIGFRAC_ERR_OOM_E（frac 不变）

**备注**：接受三种形式（均为精确表示，自动约分）： - 整数    "p"（如 "12"） - 分数    "p/q"（如 "-3/7"） - 十进制  "12.340"、".5"、"5."（如 "-12.340" → -617/50） 部分消费容错——尽可能多地消费合法前缀（设计文档 §11）， 如 "12abc" 解析为 12 且 end 指向 'a'；"12/" 解析为 12 且 end 指向 '/'。

## 算术（结果自动约分）

### `bigfrac_add`

```c
bigfrac_err_ty bigfrac_add(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
```

**说明**：加法 dst = lhs + rhs

**返回**：成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：先按 gcd(den_l, den_r) 缩分母再全约分（§6.3）；dst 允许别名

### `bigfrac_sub`

```c
bigfrac_err_ty bigfrac_sub(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
```

**说明**：减法 dst = lhs - rhs

**返回**：成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：同 add；dst 允许别名

### `bigfrac_mul`

```c
bigfrac_err_ty bigfrac_mul(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
```

**说明**：乘法 dst = lhs × rhs

**返回**：成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：交叉约分 gcd(num_l, den_r) 与 gcd(num_r, den_l) 后相乘（§6.3）； dst 允许别名

### `bigfrac_div`

```c
bigfrac_err_ty bigfrac_div(bigfrac_ty *dst, const bigfrac_ty *lhs, const bigfrac_ty *rhs);
```

**说明**：除法 dst = lhs ÷ rhs

**返回**：rhs 为零返回 BIGFRAC_ERR_DIV_ZERO_E（dst 不变）；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：交叉约分 gcd(num_l, num_r) 与 gcd(den_l, den_r) 后相乘（§6.3）； dst 允许别名

### `bigfrac_neg`

```c
bigfrac_err_ty bigfrac_neg(bigfrac_ty *frac);
```

**说明**：就地取负（零不变）

**返回**：恒为 BIGFRAC_OK_E；frac 为 NULL 返回 BIGFRAC_ERR_INVALID_E

### `bigfrac_inv`

```c
bigfrac_err_ty bigfrac_inv(bigfrac_ty *frac);
```

**说明**：就地取倒数 frac = den / num（符号保持在分子上）

**返回**：frac 为零返回 BIGFRAC_ERR_DIV_ZERO_E（frac 不变）；内存不足返回 BIGFRAC_ERR_OOM_E（frac 不变）；frac 为 NULL 返回 BIGFRAC_ERR_INVALID_E

## 比较与访问

### `bigfrac_cmp`

```c
int bigfrac_cmp(const bigfrac_ty *lhs, const bigfrac_ty *rhs);
```

**说明**：比较两个有理数

**返回**：lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正

**备注**：既约前提下等价于比较 num_l·den_r 与 num_r·den_l（§6.3）； 内部可能分配临时内存，极端内存不足时结果未定义（接口无错误通道）

### `bigfrac_num`

```c
const bigint_bin_ty *bigfrac_num(const bigfrac_ty *frac);
```

**说明**：读取分子（只读指针，指向 frac 内部；frac 为 NULL 返回 NULL）

### `bigfrac_den`

```c
const bigint_bin_ty *bigfrac_den(const bigfrac_ty *frac);
```

**说明**：读取分母（只读指针，恒为正；frac 为 NULL 返回 NULL）

### `bigfrac_poly_eval`

```c
bigfrac_err_ty bigfrac_poly_eval(bigfrac_ty *out, const bigfrac_ty *coeffs, size_t degree, const bigfrac_ty *x);
```

**说明**：多项式求值（秦九韶 / Horner 法）：out = Σ_{i=0}^{degree} coeffs[i]·x^i

**参数**
- `out`：输出对象
- `coeffs`：系数数组，共 degree+1 项，coeffs[i] 为 x^i 的系数
- `degree`：最高次幂（coeffs 长度须为 degree+1）
- `x`：自变量

**返回**：成功返回 BIGFRAC_OK_E；任一指针为 NULL 返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）

**备注**：精确有理数运算，无舍入；out 允许与 x 或 coeffs 元素别名

NEX_BIGFRAC_H
