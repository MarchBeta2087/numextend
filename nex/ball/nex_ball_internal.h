#ifndef NEX_BALL_INTERNAL_H
#define NEX_BALL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"

/*
 * nex_ball 内部接口（设计文档 §16.2）：高精度 π 与 Payne–Hanek 规约。
 *
 * 仅供 nex/ball 内部使用，不属公开 API。
 */

/* π 定点位数上限（同时也是单次规约可处理的最大 |x| 量级） */
#define NEX_BALL_PH_MAX_PI_BITS ((size_t)1U << 20)

/*
 * Payne–Hanek 规约结果：|r| = r·2^{-k}，且 x ≈ q·π/2 + r，
 * 其中 q mod 4 = quad（已把 x 的符号折叠进象限与 r 的符号）。
 */
typedef struct {
    bigint_bin_ty r;   // 无符号尾数（|r| = r·2^{-k}）
    size_t k;          // 二进制小数位数（k ≥ 1）
    unsigned quad;     // 象限 q mod 4
    unsigned neg;      // r < 0 时为 1
    int64_t err_exp;   // 规约误差上界：|Δr| ≤ 2^{err_exp}（不含 r 的舍入）
} nex_ball_ph_result_ty;

/*
 * brief: 初始化 / 释放规约结果
 */
void nex_ball_ph_result_init(nex_ball_ph_result_ty *res);
void nex_ball_ph_result_free(nex_ball_ph_result_ty *res);

/*
 * brief: 计算 floor(π · 2^bits)
 * param: out  输出大整数（已初始化）
 * param: bits 目标二进制位数；上限 NEX_BALL_PH_MAX_PI_BITS（默认 2^20）
 * return: 成功返回 BIGINT_OK_E；bits 超限或参数非法返回
 *         BIGINT_ERR_INVALID_E；内存不足返回 BIGINT_ERR_OOM_E（out 不变）
 * note: Chudnovsky 级数 + 二分拆分（每项约 47 bit），内部保留 96 bit 保护位；
 *       截断误差 < 2^{-(bits+150)}，远低于输出最低位
 */
bigint_err_ty nex_ball_pi_fixed_internal(bigint_bin_ty *out, size_t bits);

/*
 * brief: Payne–Hanek 规约 |x| = num / den（num、den 均 > 0）
 * param: res   输出规约结果（|x| 的符号由 sign 给出）
 * param: num   分子（非负）
 * param: den   分母（正）
 * param: sign  1 或 -1
 * param: pbits 工作精度（位），决定 π 精度与 r 的二进制小数位
 * return: 成功返回 BIGINT_OK_E；参数非法或所需 π 位数超过
 *         NEX_BALL_PH_MAX_PI_BITS 返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E（res 不变）
 * note: 要求 |x| 足够大（调用方仅在折半将超限时使用）；结果 r ∈ [−π/4, π/4]，
 *       误差上界见 res->err_exp（由 2/π 截断、π/2 截断与商截断合成）
 */
bigint_err_ty nex_ball_ph_reduce_internal(nex_ball_ph_result_ty *res,
        const bigint_bin_ty *num, const bigint_bin_ty *den, int sign,
        size_t pbits);

#endif /* NEX_BALL_INTERNAL_H */
