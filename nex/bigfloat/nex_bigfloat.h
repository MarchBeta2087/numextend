#ifndef NEX_BIGFLOAT_H
#define NEX_BIGFLOAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"

/*
 * bigfloat_ty：可调精度二进制浮点数（设计文档 §7）。
 *
 * 语义模型：
 *   - 正常值：value = (−1)^sign × mant × 2^exp，mant 为正整数且最高位恒 1
 *     （IEEE 754 隐藏位显式化），exp 为二进制指数；
 *   - 特殊值：+0 / −0 / +∞ / −∞ / NaN，由 flag 独立表达；
 *   - mant 的符号字段恒为 BIGINT_SIGN_POS_E（正常值）或 BIGINT_SIGN_ZERO_E
 *     （特殊值），符号完全由 flag 表达，避免双重符号源；
 *   - 正常值表示唯一：给定"最高位恒 1"规范化要求，数值到 (mant, exp) 是双射。
 *
 * 常量约定：
 *   - BIGFLOAT_MAX_EXP_BITS 限制 exp_bits 上界，防止 ctx 构造时指数范围溢出。
 */

/* 七标志：±0 / 正常值 / ±∞ / NaN */
typedef enum {
    BIGFLOAT_POS_ZERO_E = 0,  // +0
    BIGFLOAT_NEG_ZERO_E,      // −0
    BIGFLOAT_POS_E,           // 正（正常值）
    BIGFLOAT_NEG_E,           // 负（正常值）
    BIGFLOAT_POS_INF_E,       // 正无穷
    BIGFLOAT_NEG_INF_E,       // 负无穷
    BIGFLOAT_NAN_E            // 非数
} bigfloat_flag_ty;

/* 五种舍入模式 */
typedef enum {
    BIGFLOAT_ROUND_NEAREST_EVEN_E = 0,  // 最近舍入，平局取偶（默认，同 IEEE 754）
    BIGFLOAT_ROUND_TOWARD_ZERO_E,       // 向零截断
    BIGFLOAT_ROUND_TOWARD_POS_E,        // 向 +∞
    BIGFLOAT_ROUND_TOWARD_NEG_E,        // 向 −∞
    BIGFLOAT_ROUND_AWAY_ZERO_E          // 远离零
} bigfloat_round_ty;

/* bigfloat 层错误码 */
typedef enum {
    BIGFLOAT_OK_E = 0,          // 成功
    BIGFLOAT_ERR_OOM_E,         // 内存分配失败
    BIGFLOAT_ERR_INVALID_E,     // 非法参数（空指针、零长度、非法进制等）
    BIGFLOAT_ERR_OVERFLOW_E,    // 结果超出目标类型表示范围（如 to_f64 溢出）
    BIGFLOAT_ERR_PARSE_E        // 字符串解析失败
} bigfloat_err_ty;

/* 精度上下文：决定结果的舍入与范围 */
typedef struct {
    size_t mant_bits;        // 尾数精度（位），对应 IEEE 754 的 p；必须 ≥ 2
    size_t exp_bits;         // 指数字段长度（位），决定可表示范围
    bigfloat_round_ty round; // 舍入模式
} bigfloat_ctx_ty;

/* 可调精度二进制浮点数 */
typedef struct {
    bigfloat_flag_ty flag;  // 七标志
    bigint_bin_ty mant;     // 尾数幅值；仅 POS/NEG（正常值）时有效，最高位恒 1
    int64_t exp;            // 二进制指数；仅正常值时有效
} bigfloat_ty;

/* exp_bits 上界：2^61 远大于任何合理指数范围，且保证 emin 计算不溢出 int64 */
#define BIGFLOAT_MAX_EXP_BITS ((size_t)61)

/* ------------------------------------------------------------------ */
/* 生命周期与上下文                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化为 +0
 * return: 成功返回 BIGFLOAT_OK_E；val 为 NULL 返回 BIGFLOAT_ERR_INVALID_E
 */
bigfloat_err_ty bigfloat_init(bigfloat_ty *val);

/*
 * brief: 释放浮点数占用的内存（mant 的内部 bigint）
 * param: val 目标对象；可为 NULL（安全无操作）
 */
void bigfloat_free(bigfloat_ty *val);

/*
 * brief: 深拷贝浮点数
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 */
bigfloat_err_ty bigfloat_copy(bigfloat_ty *dst, const bigfloat_ty *src);

/*
 * brief: 构造精度上下文
 * param: mant_bits 尾数精度（位），必须 ≥ 2
 * param: exp_bits  指数字段长度（位），决定可表示指数范围
 *                  [emin, emax] = [−2^(exp_bits−1), 2^(exp_bits−1) − 1]
 * param: round     舍入模式
 * return: 成功返回 BIGFLOAT_OK_E；任意参数为 NULL 返回 BIGFLOAT_ERR_INVALID_E
 *         （dst 不变）；mant_bits < 2 或 exp_bits == 0 或 exp_bits > 61
 *         返回 BIGFLOAT_ERR_INVALID_E（dst 不变）
 */
bigfloat_err_ty bigfloat_ctx_make(bigfloat_ctx_ty *dst, size_t mant_bits,
        size_t exp_bits, bigfloat_round_ty round);

/*
 * brief: 常用预设上下文：IEEE 754 单精度（binary32）
 */
bigfloat_ctx_ty bigfloat_ctx_binary32(void);

/*
 * brief: 常用预设上下文：IEEE 754 双精度（binary64）
 */
bigfloat_ctx_ty bigfloat_ctx_binary64(void);

/*
 * brief: 常用预设上下文：IEEE 754 四倍精度（binary128）
 */
bigfloat_ctx_ty bigfloat_ctx_binary128(void);

/* ------------------------------------------------------------------ */
/* 分类断言（纯标志判断，不失败）                                      */
/* ------------------------------------------------------------------ */

bool bigfloat_is_zero(const bigfloat_ty *val);
bool bigfloat_is_inf(const bigfloat_ty *val);
bool bigfloat_is_nan(const bigfloat_ty *val);
bool bigfloat_is_normal(const bigfloat_ty *val);  // POS_E 或 NEG_E

/* ------------------------------------------------------------------ */
/* 构造与转换                                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 从大整数构造浮点数，按 ctx 舍入；精确值可表示时无损
 * return: 成功返回 BIGFLOAT_OK_E；src 为 NULL 返回 BIGFLOAT_ERR_INVALID_E
 *         （dst 不变）；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 */
bigfloat_err_ty bigfloat_from_bigint(bigfloat_ty *dst, const bigint_bin_ty *src,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 从 double 构造浮点数，总是精确
 * return: 成功返回 BIGFLOAT_OK_E；dst 为 NULL 返回 BIGFLOAT_ERR_INVALID_E；
 *         内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 */
bigfloat_err_ty bigfloat_from_f64(bigfloat_ty *dst, double value);

/*
 * brief: 转为 double
 * param: out 输出数值
 * return: 成功返回 BIGFLOAT_OK_E；超范围时 out 为 ±HUGE_VAL 并返回
 *         BIGFLOAT_ERR_OVERFLOW_E（out 仍被写入）；参数为 NULL 返回
 *         BIGFLOAT_ERR_INVALID_E
 */
bigfloat_err_ty bigfloat_to_f64(const bigfloat_ty *src, double *out);

/*
 * brief: 从字符串解析浮点数
 * param: str 源字符串，接受十进制小数/科学计数法、"inf"、"nan"
 *         （可选前导 '-'）；不跳过空白
 * param: end 若非 NULL，返回首个未消费字符位置（部分消费容错）
 * return: 成功返回 BIGFLOAT_OK_E；首字符即非法返回 BIGFLOAT_ERR_PARSE_E
 *         且 dst 不变；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 * note: 按 ctx 正确舍入；"0x..." 十六进制浮点不在 v1 范围
 */
bigfloat_err_ty bigfloat_from_str(bigfloat_ty *dst, const char *str,
        const bigfloat_ctx_ty *ctx, const char **end);

/*
 * brief: 输出"最短且能按相同 ctx 往返"的十进制表示
 * param: max_digits 限制有效数字位数上限；为 0 表示不限制
 * param: buf 输出缓冲区；可为 NULL 仅查询所需长度
 * param: needed 若非 NULL，传出含 '\0' 的所需长度
 * return: 成功返回 BIGFLOAT_OK_E；buf 不足返回 BIGFLOAT_ERR_OVERFLOW_E
 *         （needed 仍被写出）
 * note: 输出格式为十进制（是否含小数点由值决定）；可为整数形式，
 *       例如 1e100 输出 "1e+100" 或 "1" 及指数形式
 */
bigfloat_err_ty bigfloat_to_str(const bigfloat_ty *src, size_t max_digits,
        char *buf, size_t buf_len, size_t *needed);

/* ------------------------------------------------------------------ */
/* 算术（特殊值传播见设计 §11；有限结果的舍入与溢出见 §7.3）           */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs
 */
bigfloat_err_ty bigfloat_add(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);

/*
 * brief: 减法 dst = lhs − rhs
 */
bigfloat_err_ty bigfloat_sub(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);

/*
 * brief: 乘法 dst = lhs × rhs
 */
bigfloat_err_ty bigfloat_mul(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);

/*
 * brief: 除法 dst = lhs / rhs
 */
bigfloat_err_ty bigfloat_div(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx);

/*
 * brief: 平方根 dst = sqrt(src)；负数产生 NaN
 */
bigfloat_err_ty bigfloat_sqrt(bigfloat_ty *dst, const bigfloat_ty *src,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 就地取负（翻转符号标志，含 ±0、±∞；NaN 不变）
 */
bigfloat_err_ty bigfloat_neg(bigfloat_ty *val);

/* ------------------------------------------------------------------ */
/* 比较：+0 == −0；NaN 与任何值（含自身）比较均"不相等"，             */
/* cmp 遇 NaN 返回约定值 2                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 三路比较
 * return: lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正；
 *         任一操作数为 NaN 返回 2
 */
int bigfloat_cmp(const bigfloat_ty *lhs, const bigfloat_ty *rhs);

/*
 * brief: 相等判断（+0 == −0 为真；NaN 与任何值均不相等）
 */
bool bigfloat_eq(const bigfloat_ty *lhs, const bigfloat_ty *rhs);

/* ------------------------------------------------------------------ */
/* 分解与合成（frexp/ldexp 式）                                        */
/* ------------------------------------------------------------------ */

/*
 * brief: 分解浮点数为尾数、指数与标志
 * param: mant 传出尾数幅值（正常值时最高位恒 1；特殊值传出 0）
 * param: exp  传出二进制指数；特殊值传出 0
 * param: flag 传出七标志；可为 NULL 表示不接收
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 *         （输出参数不变）
 */
bigfloat_err_ty bigfloat_decompose(const bigfloat_ty *src, bigint_bin_ty *mant,
        int64_t *exp, bigfloat_flag_ty *flag);

/*
 * brief: 合成浮点数，按 ctx 对尾数舍入/规范化
 * param: mant 尾数幅值（可为任意非负整数；零则结果为 ±0，符号由 flag 决定）
 * param: exp  二进制指数
 * param: flag 七标志
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 *         （dst 不变）
 * note: 若 flag 为特殊值，mant / exp 被忽略，结果为对应特殊值
 */
bigfloat_err_ty bigfloat_compose(bigfloat_ty *dst, const bigint_bin_ty *mant,
        int64_t exp, bigfloat_flag_ty flag, const bigfloat_ctx_ty *ctx);

#endif /* NEX_BIGFLOAT_H */