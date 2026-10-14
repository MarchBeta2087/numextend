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

## 截断幂级数 / Laurent 级数（设计文档 §15）

## bigfrac_series_ty：系数为 bigfrac_ty 的截断级数  S(z) = Σ_{i=0}^{len-1} coeffs[i] · z^{val+i}  val 为赋值 valuation（最低次幂指数，可为负 → Laurent）；coeffs 升幂排列。 规范化：len == 0 表示零级数（val 归 0）；否则末项 coeffs[len-1] != 0。

```c
typedef struct {
int64_t val;         // 最低次幂指数，可为负（Laurent）
bigfrac_ty *coeffs;  // coeffs[i] 对应 z^{val+i}
size_t len;          // 有效系数个数
size_t cap;          // 已分配容量
} bigfrac_series_ty;
```

### `bigfrac_series_init`

```c
bigfrac_err_ty bigfrac_series_init(bigfrac_series_ty *s);
```

**说明**：初始化为零级数

**返回**：成功返回 BIGFRAC_OK_E；s 为 NULL 返回 BIGFRAC_ERR_INVALID_E

### `bigfrac_series_from_array`

```c
bigfrac_err_ty bigfrac_series_from_array(bigfrac_series_ty *dst, int64_t val, const bigfrac_ty *coeffs, size_t len);
```

**说明**：由系数数组构造（深拷贝，去尾零）

**参数**
- `dst`：输出（已初始化）
- `val`：最低次幂指数
- `coeffs`：系数数组，coeffs[i] 对应 z^{val+i}；len 为 0 时可为 NULL
- `len`：系数个数

**返回**：成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：全零输入归一为零级数（len = 0，val = 0）

### `bigfrac_series_free`

```c
void bigfrac_series_free(bigfrac_series_ty *s);
```

**说明**：释放级数（s 可为 NULL 或零初始化对象）

### `bigfrac_series_copy`

```c
bigfrac_err_ty bigfrac_series_copy(bigfrac_series_ty *dst, const bigfrac_series_ty *src);
```

**说明**：深拷贝

**返回**：成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

### `bigfrac_series_add`

```c
bigfrac_err_ty bigfrac_series_add(bigfrac_series_ty *dst, const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs, size_t order);
```

**说明**：加法 dst = lhs + rhs，保留最低 order 项

**返回**：成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：结果 val = min(lhs->val, rhs->val)；允许别名；order = 0 得零级数

### `bigfrac_series_sub`

```c
bigfrac_err_ty bigfrac_series_sub(bigfrac_series_ty *dst, const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs, size_t order);
```

**说明**：减法 dst = lhs - rhs，保留最低 order 项

**返回**：成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：结果 val = min(lhs->val, rhs->val)；允许别名；order = 0 得零级数

### `bigfrac_series_mul`

```c
bigfrac_err_ty bigfrac_series_mul(bigfrac_series_ty *dst, const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs, size_t order);
```

**说明**：乘：dst = lhs × rhs（截断），保留最低 order 项

**返回**：同 add

**备注**：结果 val = lhs->val + rhs->val；允许别名

### `bigfrac_series_inv`

```c
bigfrac_err_ty bigfrac_series_inv(bigfrac_series_ty *dst, const bigfrac_series_ty *src, size_t order);
```

**说明**：倒数 dst = 1 / src，保留最低 order 项

**返回**：src 为空或首项为零返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：结果 val = -src->val（首项非零的“单位级数”），支持 Laurent

### `bigfrac_series_eval`

```c
bigfrac_err_ty bigfrac_series_eval(bigfrac_ty *out, const bigfrac_series_ty *s, const bigfrac_ty *x);
```

**说明**：求值 out = Σ_{i=0}^{len-1} coeffs[i] · x^{val+i}

**返回**：参数非法或（val < 0 时）x == 0 返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）

**备注**：精确；val < 0（Laurent）按除以 x^{|val|} 实现

### `bigfrac_binom`

```c
bigfrac_err_ty bigfrac_binom(bigfrac_ty *out, const bigfrac_ty *alpha, size_t k);
```

**说明**：广义二项式系数 C(alpha, k) = alpha(alpha−1)···(alpha−k+1)/k!

**参数**
- `out`：输出（已初始化）
- `alpha`：上指标（任意有理数）
- `k`：下指标

**返回**：成功返回 BIGFRAC_OK_E；指针为 NULL 返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）

**备注**：k = 0 返回 1；alpha 为非负整数且 k > alpha 时返回 0

### `bigfrac_series_binom`

```c
bigfrac_err_ty bigfrac_series_binom(bigfrac_series_ty *dst, const bigfrac_ty *alpha, size_t order);
```

**说明**：截断二项式级数 (1+z)^alpha = Σ_{k=0}^{order-1} C(alpha,k)·z^k

**参数**
- `dst`：输出（已初始化）
- `alpha`：指数（任意有理数）
- `order`：保留项数

**返回**：成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）

**备注**：精确；order = 0 得零级数；alpha 为非负整数 n 时为 n 次多项式

### `bigfrac_series_tail_geometric`

```c
bigfrac_err_ty bigfrac_series_tail_geometric(bigfrac_ty *out, const bigfrac_ty *A, const bigfrac_ty *B, const bigfrac_ty *r, size_t n);
```

**说明**：几何型全局尾界：若 |a_k| ≤ A·B^k（∀k）、|z| ≤ r 且 B·r < 1， 则 |Σ_{k≥n} a_k z^k| ≤ A·(B·r)^n / (1 − B·r)

**参数**
- `out`：输出上界（≥ 0）
- `A`：系数幅度因子（≥ 0）
- `B`：系数增长因子（≥ 0）
- `r`：自变量幅度上界（≥ 0）
- `n`：截断阶（保留 k < n 的项）

**返回**：成功返回 BIGFRAC_OK_E；指针为 NULL 返回 BIGFRAC_ERR_INVALID_E； B·r ≥ 1 或任一参数为负返回 BIGFRAC_ERR_INVALID_E； 内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）

**备注**：精确有理数上界；用于“精确系数 + 全局尾界”路线（design §15.6）

### `bigfrac_series_order_geometric`

```c
bigfrac_err_ty bigfrac_series_order_geometric(size_t *out, const bigfrac_ty *A, const bigfrac_ty *B, const bigfrac_ty *r, const bigfrac_ty *eps);
```

**说明**：自适应截断阶：最小的 n 使 tail_geometric(A,B,r,n) ≤ eps

**参数**
- `out`：输出阶 n
- `A,`：B, r 同 tail_geometric
- `eps`：目标绝对误差上界（≥ 0）

**返回**：成功返回 BIGFRAC_OK_E；参数非法或 B·r ≥ 1 返回 BIGFRAC_ERR_INVALID_E；超出内部迭代上限（q 极接近 1）返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）

**备注**：精确比较 A·(B·r)^n ≤ eps·(1−B·r) 逐步确定 n；上限 1 000 000 阶

### `bigfrac_series_tail_ratio`

```c
bigfrac_err_ty bigfrac_series_tail_ratio(bigfrac_ty *out, const bigfrac_ty *first_omitted, const bigfrac_ty *q);
```

**说明**：比值型尾界：若自首个略去项起相邻项幅度比 ≤ q（q < 1），则 尾部之和 ≤ first_omitted / (1 − q)

**参数**
- `out`：输出上界（≥ 0）
- `first_omitted`：首个略去项的幅度（≥ 0）
- `q`：相邻项幅度比上界（0 ≤ q < 1）

**返回**：成功返回 BIGFRAC_OK_E；指针为 NULL / 参数为负 / q ≥ 1 返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）

**备注**：适合阶乘型系数（exp/sin/cos），比几何型界更紧；用于 design §15.6 路线 A

NEX_BIGFRAC_H
