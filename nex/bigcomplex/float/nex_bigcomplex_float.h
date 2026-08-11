#ifndef NEX_BIGCOMPLEX_FLOAT_H
#define NEX_BIGCOMPLEX_FLOAT_H

#include <stdbool.h>
#include <stddef.h>

#include "nex/bigfloat/nex_bigfloat.h"

/*
 * bigcomplex_float_ty：实部/虚部均为 bigfloat_ty 的复数（设计文档 §9）。
 *
 * 复数本身不新增标志；特殊值语义完全由分量的七标志组合表达（§9.3）。
 * API 与 bigcomplex_decimal_ty 平行（十进制版见 decimal/ 子目录）。
 */

typedef struct {
    bigfloat_ty re;  // 实部
    bigfloat_ty im;  // 虚部
} bigcomplex_float_ty;

/* bigcomplex（float 版）错误码 */
typedef enum {
    BIGCOMPLEX_FLOAT_OK_E = 0,          // 成功
    BIGCOMPLEX_FLOAT_ERR_OOM_E,         // 内存分配失败
    BIGCOMPLEX_FLOAT_ERR_INVALID_E,     // 非法参数
    BIGCOMPLEX_FLOAT_ERR_OVERFLOW_E,    // 输出缓冲不足等
    BIGCOMPLEX_FLOAT_ERR_PARSE_E,       // 字符串解析失败
    BIGCOMPLEX_FLOAT_ERR_UNSUPPORTED_E  // 功能未实现（如 arg，v1 可选）
} bigcomplex_float_err_ty;

/* ------------------------------------------------------------------ */
/* 生命周期与构造（§9.2）                                              */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化为 0 + 0i
 * return: 成功返回 BIGCOMPLEX_FLOAT_OK_E；cpx 为 NULL 返回 INVALID
 */
bigcomplex_float_err_ty bigcomplex_float_init(bigcomplex_float_ty *cpx);

/*
 * brief: 释放复数占用的内存（两个分量各自释放）
 * param: cpx 目标对象；可为 NULL（安全无操作）
 */
void bigcomplex_float_free(bigcomplex_float_ty *cpx);

/*
 * brief: 深拷贝复数
 * return: 成功返回 OK；内存不足返回 OOM（dst 不变）
 */
bigcomplex_float_err_ty bigcomplex_float_copy(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *src);

/*
 * brief: 从实部/虚部构造复数（深拷贝分量）
 * return: 成功返回 OK；任一参数为 NULL 返回 INVALID（cpx 不变）
 */
bigcomplex_float_err_ty bigcomplex_float_from_parts(bigcomplex_float_ty *cpx,
        const bigfloat_ty *re, const bigfloat_ty *im);

/*
 * brief: 从字符串解析复数
 * param: str 源字符串，接受 "a+bi"、"a-bi"、"a"、"bi" 形式
 *        （分量本身支持十进制小数/科学计数/"inf"/"nan"）
 * param: end 若非 NULL，返回首个未消费字符位置（部分消费容错）
 * return: 成功返回 OK；首字符即非法或虚部缺 'i' 后缀返回 PARSE
 *         （cpx 不变）；内存不足返回 OOM（cpx 不变）
 * note: "a+..." 形式要求虚部以 'i' 结尾，否则整字面量非法（PARSE）；
 *       完整字面量后的尾随字符按部分消费约定处理
 */
bigcomplex_float_err_ty bigcomplex_float_from_str(bigcomplex_float_ty *cpx,
        const char *str, const bigfloat_ctx_ty *ctx, const char **end);

/*
 * brief: 输出 "a+bi" 形式（分量按 bigfloat_to_str 的最短往返表示）
 * param: max_digits 分量的有效数字位数上限；0 表示不限制（透传分量）
 * return: 成功返回 OK；buf 不足返回 OVERFLOW（needed 仍被写出）
 * note: 输出规则：虚部为 +0 时仅输出实部；实部为 +0 时仅输出虚部 + "i"；
 *       否则 "a±|b|i"（虚部负号用连接符 '-' 表达）
 */
bigcomplex_float_err_ty bigcomplex_float_to_str(const bigcomplex_float_ty *cpx,
        size_t max_digits, char *buf, size_t buf_len, size_t *needed);

/* ------------------------------------------------------------------ */
/* 分量访问与一元运算（§9.2）                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 共轭 cpx = conj(cpx)（虚部取负；NaN 虚部不变）
 */
bigcomplex_float_err_ty bigcomplex_float_conj(bigcomplex_float_ty *cpx);

/*
 * brief: 模 |src| = sqrt(re² + im²)（分量运算，中间量可能过早溢出，
 *        缩放算法列为后续方向，§9.2）
 * return: 成功返回 OK；内存不足返回 OOM（dst 不变）
 */
bigcomplex_float_err_ty bigcomplex_float_abs(bigfloat_ty *dst,
        const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);

/*
 * brief: 辐角 arg(src)（需要 atan，v1 未实现）
 * return: 恒返回 BIGCOMPLEX_FLOAT_ERR_UNSUPPORTED_E（dst 不变）
 */
bigcomplex_float_err_ty bigcomplex_float_arg(bigfloat_ty *dst,
        const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx);

/* ------------------------------------------------------------------ */
/* 四则（§9.2）                                                        */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs（分量加法）
 */
bigcomplex_float_err_ty bigcomplex_float_add(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 减法 dst = lhs − rhs（分量减法）
 */
bigcomplex_float_err_ty bigcomplex_float_sub(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 乘法 dst = lhs × rhs
 * note: (a+bi)(c+di) = (ac−bd) + (bc+ad)i；v1 朴素四乘二加
 *       （Karatsuba 三乘式列为后续，§9.2）
 */
bigcomplex_float_err_ty bigcomplex_float_mul(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 除法 dst = lhs / rhs
 * note: 共轭法 (a+bi)/(c+di) = (a+bi)(c−di)/(c²+d²)；中间量可能过早
 *       溢出/下溢（如 ∞ 分量经中间乘法产生 NaN），Smith 缩放算法列为
 *       后续方向（§9.2），v1 按"分量独立求值后组合"的朴素语义
 */
bigcomplex_float_err_ty bigcomplex_float_div(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx);

/* ------------------------------------------------------------------ */
/* 相等性（复数无序，不提供 cmp，§9.2）                                */
/* ------------------------------------------------------------------ */

/*
 * brief: 相等判断（分量各自 eq；任一分量 NaN 则 false）
 */
bool bigcomplex_float_eq(const bigcomplex_float_ty *lhs,
        const bigcomplex_float_ty *rhs);

#endif /* NEX_BIGCOMPLEX_FLOAT_H */
