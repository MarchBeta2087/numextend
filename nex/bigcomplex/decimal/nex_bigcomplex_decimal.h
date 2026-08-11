#ifndef NEX_BIGCOMPLEX_DECIMAL_H
#define NEX_BIGCOMPLEX_DECIMAL_H

#include <stdbool.h>
#include <stddef.h>

#include "nex/bigdecimal/nex_bigdecimal.h"

/*
 * bigcomplex_decimal_ty：实部/虚部均为 bigdecimal_ty 的复数（设计文档 §9）。
 *
 * 与 bigcomplex_float_ty 平行（float 版见 float/ 子目录）：复数本身不新增
 * 标志，特殊值语义完全由分量的七标志组合表达（§9.3）。
 */

typedef struct {
    bigdecimal_ty re;  // 实部
    bigdecimal_ty im;  // 虚部
} bigcomplex_decimal_ty;

/* bigcomplex（decimal 版）错误码 */
typedef enum {
    BIGCOMPLEX_DECIMAL_OK_E = 0,          // 成功
    BIGCOMPLEX_DECIMAL_ERR_OOM_E,         // 内存分配失败
    BIGCOMPLEX_DECIMAL_ERR_INVALID_E,     // 非法参数
    BIGCOMPLEX_DECIMAL_ERR_OVERFLOW_E,    // 输出缓冲不足等
    BIGCOMPLEX_DECIMAL_ERR_PARSE_E,       // 字符串解析失败
    BIGCOMPLEX_DECIMAL_ERR_UNSUPPORTED_E  // 功能未实现（如 arg，v1 可选）
} bigcomplex_decimal_err_ty;

/* ------------------------------------------------------------------ */
/* 生命周期与构造（§9.2）                                              */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化为 0 + 0i
 * return: 成功返回 BIGCOMPLEX_DECIMAL_OK_E；cpx 为 NULL 返回 INVALID
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_init(bigcomplex_decimal_ty *cpx);

/*
 * brief: 释放复数占用的内存（两个分量各自释放）
 * param: cpx 目标对象；可为 NULL（安全无操作）
 */
void bigcomplex_decimal_free(bigcomplex_decimal_ty *cpx);

/*
 * brief: 深拷贝复数
 * return: 成功返回 OK；内存不足返回 OOM（dst 不变）
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_copy(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *src);

/*
 * brief: 从实部/虚部构造复数（深拷贝分量）
 * return: 成功返回 OK；任一参数为 NULL 返回 INVALID（cpx 不变）
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_from_parts(
        bigcomplex_decimal_ty *cpx, const bigdecimal_ty *re,
        const bigdecimal_ty *im);

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
bigcomplex_decimal_err_ty bigcomplex_decimal_from_str(
        bigcomplex_decimal_ty *cpx, const char *str,
        const bigdecimal_ctx_ty *ctx, const char **end);

/*
 * brief: 输出 "a+bi" 形式（分量按 bigdecimal_to_str 精确展开）
 * param: fmt 分量输出格式（定点 / 科学计数，透传分量）
 * return: 成功返回 OK；buf 不足返回 OVERFLOW（needed 仍被写出）
 * note: 输出规则同 float 版：虚部为 +0 时仅输出实部；实部为 +0 时仅
 *       输出虚部 + "i"；否则 "a±|b|i"
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_to_str(
        const bigcomplex_decimal_ty *cpx, bigdecimal_fmt_ty fmt,
        char *buf, size_t buf_len, size_t *needed);

/* ------------------------------------------------------------------ */
/* 分量访问与一元运算（§9.2）                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 共轭 cpx = conj(cpx)（虚部取负；NaN 虚部不变）
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_conj(bigcomplex_decimal_ty *cpx);

/*
 * brief: 模 |src| = sqrt(re² + im²)（分量运算，中间量可能过早溢出，
 *        缩放算法列为后续方向，§9.2）
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_abs(bigdecimal_ty *dst,
        const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);

/*
 * brief: 辐角 arg(src)（需要 atan，v1 未实现）
 * return: 恒返回 BIGCOMPLEX_DECIMAL_ERR_UNSUPPORTED_E（dst 不变）
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_arg(bigdecimal_ty *dst,
        const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx);

/* ------------------------------------------------------------------ */
/* 四则（§9.2）                                                        */
/* ------------------------------------------------------------------ */

bigcomplex_decimal_err_ty bigcomplex_decimal_add(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx);

bigcomplex_decimal_err_ty bigcomplex_decimal_sub(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx);

bigcomplex_decimal_err_ty bigcomplex_decimal_mul(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx);

bigcomplex_decimal_err_ty bigcomplex_decimal_div(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx);

/* ------------------------------------------------------------------ */
/* 相等性（复数无序，不提供 cmp，§9.2）                                */
/* ------------------------------------------------------------------ */

bool bigcomplex_decimal_eq(const bigcomplex_decimal_ty *lhs,
        const bigcomplex_decimal_ty *rhs);

#endif /* NEX_BIGCOMPLEX_DECIMAL_H */
