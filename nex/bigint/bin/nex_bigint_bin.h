#ifndef NEX_BIGINT_BIN_H
#define NEX_BIGINT_BIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/nex_bigint_common.h"

/*
 * bigint_bin_ty：二进制肢（基 2^32）任意精度整数（设计文档 §4）。
 *
 * 数值 = sign × Σ(limbs[i] × 2^(32·i))，limbs 小端序。
 * 规范化不变式（所有公开 API 的输出均满足）：
 *   - 最高有效肢 limbs[len-1] != 0；
 *   - 零的唯一表示为 len == 0 且 sign == BIGINT_SIGN_ZERO_E
 *     （此时 limbs 可为 NULL 或保留容量）。
 *
 * 通用约定：
 *   - 所有算术函数允许 dst 与任一源操作数别名（aliasing）；
 *   - 任何因 OOM 或非法参数失败的运算，输出参数保持调用前状态；
 *   - 负数位运算按二进制补码的无限符号扩展解释（与 Python 一致），
 *     例如 -1 & x == x。
 */

typedef struct {
    bigint_sign_ty sign;  // 三标志：正 / 负 / 零
    uint32_t *limbs;      // 肢数组，小端序，基 2^32
    size_t len;           // 当前有效肢数
    size_t cap;           // 已分配容量（肢数）
} bigint_bin_ty;

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化大整数为零
 * param: val 需要初始化的大整数变量
 * return: 成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_bin_init(bigint_bin_ty *val);

/*
 * brief: 初始化大整数为零并预分配容量
 * param: val 需要初始化的大整数变量
 * param: cap 预分配的肢容量；为 0 时等价于 bigint_bin_init
 * return: 成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E
 */
bigint_err_ty bigint_bin_init_cap(bigint_bin_ty *val, size_t cap);

/*
 * brief: 释放大整数占用的内存
 * param: val 需要释放的大整数变量；可为 NULL 或零初始化对象（安全无操作）
 */
void bigint_bin_free(bigint_bin_ty *val);

/*
 * brief: 深拷贝大整数
 * param: dst 目标对象（已初始化）
 * param: src 源对象
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_copy(bigint_bin_ty *dst, const bigint_bin_ty *src);

/*
 * brief: 移动大整数，dst 接管 src 的资源
 * param: dst 目标对象（已初始化，其原有资源被释放）
 * param: src 源对象，调用后被重置为零（len == 0，cap == 0，limbs == NULL）
 */
void bigint_bin_move(bigint_bin_ty *dst, bigint_bin_ty *src);

/*
 * brief: 将容量收缩至当前有效肢数
 * param: val 目标对象
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: len == 0 时释放全部容量
 */
bigint_err_ty bigint_bin_shrink(bigint_bin_ty *val);

/* ------------------------------------------------------------------ */
/* 与基本类型互转                                                       */
/* ------------------------------------------------------------------ */

/*
 * brief: 以 uint64_t 赋值
 * param: val 目标对象（已初始化）
 * param: value 源数值
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
bigint_err_ty bigint_bin_from_u64(bigint_bin_ty *val, uint64_t value);

/*
 * brief: 以 int64_t 赋值
 * param: val 目标对象（已初始化）
 * param: value 源数值
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
bigint_err_ty bigint_bin_from_i64(bigint_bin_ty *val, int64_t value);

/*
 * brief: 转为 uint64_t
 * param: val 源对象
 * param: out 输出数值
 * return: 成功返回 BIGINT_OK_E；负数或超出 uint64_t 范围返回
 *         BIGINT_ERR_OVERFLOW_E（out 不被修改）
 */
bigint_err_ty bigint_bin_to_u64(const bigint_bin_ty *val, uint64_t *out);

/*
 * brief: 转为 int64_t
 * param: val 源对象
 * param: out 输出数值
 * return: 成功返回 BIGINT_OK_E；超出 int64_t 范围返回
 *         BIGINT_ERR_OVERFLOW_E（out 不被修改）
 */
bigint_err_ty bigint_bin_to_i64(const bigint_bin_ty *val, int64_t *out);

/*
 * brief: 从字符串解析，支持进制 2..36（数字 0-9a-z，大小写均可）
 * param: val 目标对象（已初始化）
 * param: str 源字符串，可选前导 '-'；base 为 16 时允许 "0x"/"0X" 前缀，
 *        base 为 2 时允许 "0b"/"0B" 前缀；不跳过空白
 * param: base 进制，取值 2..36
 * param: end 若非 NULL，返回首个未消费字符位置
 * return: 成功返回 BIGINT_OK_E；首字符即非法（无可消费前缀）返回
 *         BIGINT_ERR_PARSE_E 且 val 不变；base 越界返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: 部分消费容错——尽可能多地消费合法数字（设计文档 §11）
 */
bigint_err_ty bigint_bin_from_str(bigint_bin_ty *val, const char *str,
        uint32_t base, const char **end);

/*
 * brief: 转为字符串，支持进制 2..36（数字 0-9a-z 小写；负数带 '-' 前缀）
 * param: val 源对象
 * param: base 进制，取值 2..36
 * param: buf 输出缓冲区；可为 NULL 仅查询所需长度
 * param: buf_len 缓冲区长度
 * param: needed 若非 NULL，传出含 '\0' 的所需长度
 * return: 成功返回 BIGINT_OK_E；buf 不足返回 BIGINT_ERR_OVERFLOW_E
 *         （buf 内容未定义，needed 仍被写出）；base 越界返回
 *         BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_bin_to_str(const bigint_bin_ty *val, uint32_t base,
        char *buf, size_t buf_len, size_t *needed);

/* ------------------------------------------------------------------ */
/* 比较与断言                                                           */
/* ------------------------------------------------------------------ */

/*
 * brief: 比较两个大整数
 * return: lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正
 */
int bigint_bin_cmp(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

/*
 * brief: 比较两个大整数的绝对值
 * return: |lhs| < |rhs| 为负，相等为 0，|lhs| > |rhs| 为正
 */
int bigint_bin_cmp_abs(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

/*
 * brief: 读取符号标志
 */
bigint_sign_ty bigint_bin_sign(const bigint_bin_ty *val);

/*
 * brief: 判断是否为零
 */
bool bigint_bin_is_zero(const bigint_bin_ty *val);

/* ------------------------------------------------------------------ */
/* 四则与幂                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: dst 允许与 lhs / rhs 别名
 */
bigint_err_ty bigint_bin_add(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 减法 dst = lhs - rhs
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: dst 允许与 lhs / rhs 别名
 */
bigint_err_ty bigint_bin_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 乘法 dst = lhs × rhs，按肢数阈值自动选择算法
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 等价于 method 为 { BIGINT_MUL_AUTO_E, {0} } 的 mul_ex；允许别名
 */
bigint_err_ty bigint_bin_mul(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 带方法选择的大整数乘法
 * param: dst 目标对象，允许与源操作数别名
 * param: method 方法选择；为 NULL 等价于 AUTO
 * return: algo 非法或与 params 不匹配返回 BIGINT_ERR_INVALID_E；
 *         algo 已登记但当前版本未实现返回 BIGINT_ERR_UNSUPPORTED_E；
 *         内存不足返回 BIGINT_ERR_OOM_E；成功返回 BIGINT_OK_E
 */
bigint_err_ty bigint_bin_mul_ex(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, const bigint_mul_method_ty *method);

/*
 * brief: 带余除法，lhs = quot × rhs + rem（截断除法，与 C99 整数除法语义相同）
 * param: quot 商，可为 NULL 表示不接收
 * param: rem 余数，可为 NULL 表示不接收；符号与被除数一致
 * return: 除数为零返回 BIGINT_ERR_DIV_ZERO_E（quot / rem 不被修改）；
 *         内存不足返回 BIGINT_ERR_OOM_E；成功返回 BIGINT_OK_E
 * note: quot / rem 允许与 lhs / rhs 别名
 */
bigint_err_ty bigint_bin_div_rem(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

/*
 * brief: 就地取负（零不变）
 * return: 恒为 BIGINT_OK_E
 */
bigint_err_ty bigint_bin_neg(bigint_bin_ty *val);

/*
 * brief: 就地取绝对值
 * return: 恒为 BIGINT_OK_E
 */
bigint_err_ty bigint_bin_abs(bigint_bin_ty *val);

/*
 * brief: 幂 dst = base^exp
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 约定 0^0 = 1；dst 允许与 base 别名
 */
bigint_err_ty bigint_bin_pow(bigint_bin_ty *dst, const bigint_bin_ty *base,
        uint64_t exp);

/*
 * brief: 模幂 dst = base^exp mod mod，结果取值 [0, mod)
 * return: mod 为零返回 BIGINT_ERR_DIV_ZERO_E；exp 为负或 mod 为负返回
 *         BIGINT_ERR_INVALID_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: dst 允许与任一源操作数别名
 */
bigint_err_ty bigint_bin_pow_mod(bigint_bin_ty *dst, const bigint_bin_ty *base,
        const bigint_bin_ty *exp, const bigint_bin_ty *mod);

/*
 * brief: 最大公因数 dst = gcd(|lhs|, |rhs|)，结果恒非负（二进制 GCD，
 *        供 bigfrac 约分使用，设计文档 §4.3）
 * param: dst 目标对象（已初始化）
 * param: lhs 左操作数
 * param: rhs 右操作数
 * return: 成功返回 BIGINT_OK_E；任一参数为 NULL 返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 约定 gcd(0, 0) = 0；dst 允许与 lhs / rhs 别名
 */
bigint_err_ty bigint_bin_gcd(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/* ------------------------------------------------------------------ */
/* 位运算与杂项（bin 专有；负数按补码无限符号扩展解释）                     */
/* ------------------------------------------------------------------ */

/*
 * brief: 算术左移 dst = src × 2^bits（符号随 src）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_shl(bigint_bin_ty *dst, const bigint_bin_ty *src,
        size_t bits);

/*
 * brief: 算术右移 dst = floor(src / 2^bits)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 负数为向下取整（floor）语义，如 -5 >> 1 = -3（与 Python 一致）
 */
bigint_err_ty bigint_bin_shr(bigint_bin_ty *dst, const bigint_bin_ty *src,
        size_t bits);

/*
 * brief: 按位与（负数按补码无限符号扩展，如 -1 & x == x）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_bit_and(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 按位或（负数按补码无限符号扩展，如 -1 | x == -1）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_bit_or(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 按位异或（负数按补码无限符号扩展）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_bit_xor(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/*
 * brief: 测试第 bit 位（从 0 计，最低有效位为第 0 位；补码语义，
 *        如 -1 的任意位均为 1）
 */
bool bigint_bin_bit_test(const bigint_bin_ty *val, size_t bit);

/*
 * brief: 设置/清除第 bit 位（补码语义，如 bit_set(-1, 5, false) 得 -33）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
bigint_err_ty bigint_bin_bit_set(bigint_bin_ty *val, size_t bit, bool value);

/*
 * brief: 幅值的位长（最高有效位位置 + 1；零为 0）
 */
size_t bigint_bin_bit_len(const bigint_bin_ty *val);

/*
 * brief: 幅值中 1 的个数（按 |val| 计数，与符号无关）
 */
size_t bigint_bin_popcount(const bigint_bin_ty *val);

/*
 * brief: 整数平方根（向下取整）：out = floor(sqrt(val))
 * note: Newton 迭代，负数返回 BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_bin_sqrt(bigint_bin_ty *out, const bigint_bin_ty *val);

#endif /* NEX_BIGINT_BIN_H */
