#ifndef NEX_BALL_H
#define NEX_BALL_H

#include <stdbool.h>

#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfloat/nex_bigfloat.h"

/*
 * nex_ball：球式（中心 + 误差半径）算术，路线 B 第一阶段（设计文档 §15.6）。
 *
 * 区间语义：bigfloat_ball_ty 表示 [mid − rad, mid + rad]（rad ≥ 0）。
 * 四则运算用**定向舍入**在区间端点间进行，结果球严格包含真实区间；
 * 半径传播复用需要时可与路线 A 的尾界叠加。
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * bigfloat_ball_ty：中心 mid 与半径 rad（rad ≥ 0）构成的闭区间
 */
typedef struct {
    bigfloat_ty mid;  // 中心
    bigfloat_ty rad;  // 半径（≥ 0）
} bigfloat_ball_ty;

/*
 * brief: 初始化为 [0, 0]
 * return: 成功返回 BIGFLOAT_OK_E；b 为 NULL 返回 BIGFLOAT_ERR_INVALID_E
 */
bigfloat_err_ty bigfloat_ball_init(bigfloat_ball_ty *b);

/*
 * brief: 释放（b 可为 NULL）
 */
void bigfloat_ball_free(bigfloat_ball_ty *b);

/*
 * brief: 深拷贝
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 */
bigfloat_err_ty bigfloat_ball_copy(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *src);

/*
 * brief: 设为 [mid − rad, mid + rad]
 * param: mid/rad 中心与半径（rad 须 ≥ 0；为 ±0 视为 0）
 * return: 成功返回 BIGFLOAT_OK_E；参数非法或 rad < 0 返回
 *         BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 */
bigfloat_err_ty bigfloat_ball_set(bigfloat_ball_ty *dst,
        const bigfloat_ty *mid, const bigfloat_ty *rad,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 判断 x 是否在球内（无假阳性：两端点按向内方向舍入）
 * return: x ∈ [mid − rad, mid + rad] 为 true；NaN 或参数非法为 false
 */
bool bigfloat_ball_contains(const bigfloat_ball_ty *b, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 加法 dst = a + b（区间和）
 * return: 成功返回 BIGFLOAT_OK_E；参数非法或 a、b 含 NaN/±∞ 返回
 *         BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（dst 不变）
 */
bigfloat_err_ty bigfloat_ball_add(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 减法 dst = a - b（区间差）
 * return: 同 add
 */
bigfloat_err_ty bigfloat_ball_sub(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 乘 dst = a × b（区间积）
 * return: 同 add
 */
bigfloat_err_ty bigfloat_ball_mul(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 除 dst = a / b（区间商）
 * return: 成功返回 BIGFLOAT_OK_E；b 的区间含 0 返回 BIGFLOAT_ERR_INVALID_E；
 *         其余同 add
 */
bigfloat_err_ty bigfloat_ball_div(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx);

/*
 * bigdecimal_ball_ty：bigdecimal 版球（中心 + 半径，rad ≥ 0）
 */
typedef struct {
    bigdecimal_ty mid;  // 中心
    bigdecimal_ty rad;  // 半径（≥ 0）
} bigdecimal_ball_ty;

/*
 * brief: 初始化为 [0, 0]
 * return: 成功返回 BIGDECIMAL_OK_E；b 为 NULL 返回 BIGDECIMAL_ERR_INVALID_E
 */
bigdecimal_err_ty bigdecimal_ball_init(bigdecimal_ball_ty *b);

/*
 * brief: 释放（b 可为 NULL）
 */
void bigdecimal_ball_free(bigdecimal_ball_ty *b);

/*
 * brief: 深拷贝
 * return: 成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）
 */
bigdecimal_err_ty bigdecimal_ball_copy(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *src);

/*
 * brief: 设为 [mid − rad, mid + rad]（rad ≤ 0 非法）
 * return: 成功返回 BIGDECIMAL_OK_E；参数非法或 rad < 0 返回
 *         BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）
 */
bigdecimal_err_ty bigdecimal_ball_set(bigdecimal_ball_ty *dst,
        const bigdecimal_ty *mid, const bigdecimal_ty *rad,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 判断 x 是否在球内（向内舍入，无假阳性）
 */
bool bigdecimal_ball_contains(const bigdecimal_ball_ty *b,
        const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx);

/*
 * brief: 加法 dst = a + b（区间和）
 * return: 成功返回 BIGDECIMAL_OK_E；参数非法或含 NaN/±∞ 返回
 *         BIGDECIMAL_ERR_INVALID_E；内存不足返回 BIGDECIMAL_ERR_OOM_E（dst 不变）
 */
bigdecimal_err_ty bigdecimal_ball_add(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 减法 dst = a - b（区间差）
 * return: 同 add
 */
bigdecimal_err_ty bigdecimal_ball_sub(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 乘法 dst = a × b（区间积）
 * return: 同 add
 */
bigdecimal_err_ty bigdecimal_ball_mul(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 除法 dst = a / b（区间商）
 * return: 成功返回 BIGDECIMAL_OK_E；b 的区间含 0 返回
 *         BIGDECIMAL_ERR_INVALID_E；其余同 add
 */
bigdecimal_err_ty bigdecimal_ball_div(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NEX_BALL_H */
