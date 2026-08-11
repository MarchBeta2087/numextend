/*
 * nex_bigcomplex_float.c：bigcomplex_float_ty（实部/虚部均为 bigfloat
 * 的复数）实现。
 *
 * 职责（设计文档 §9）：生命周期与构造、字符串 I/O、共轭/模/辐角、
 * 四则、相等性。复数不引入新的特殊值类别——特殊值语义完全由分量的
 * 七标志组合表达（§9.3）。
 *
 * 实现要点（§9.2）：
 *   - 乘法用朴素四乘二加；(a+bi)(c+di) = (ac−bd) + (bc+ad)i；
 *   - 除法用共轭法 (a+bi)(c−di)/(c²+d²)——中间量可能过早溢出/下溢
 *     （如 ∞ 分量经中间乘法产生 NaN），Smith 缩放算法列为后续方向；
 *   - abs 经 sqrt(re² + im²)，同样的溢出注意事项适用；
 *   - arg 依赖 atan，v1 返回 UNSUPPORTED（未引入超越函数）。
 */

#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

static bigcomplex_float_err_ty map_bf_err(bigfloat_err_ty err)
{
    if (err == BIGFLOAT_OK_E) {
        return BIGCOMPLEX_FLOAT_OK_E;
    }
    if (err == BIGFLOAT_ERR_OOM_E) {
        return BIGCOMPLEX_FLOAT_ERR_OOM_E;
    }
    return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
}

/* 是否任一分量为 NaN（§9.3：任一分量 NaN → 整体 (NaN, NaN)） */
static bool any_nan(const bigcomplex_float_ty *z)
{
    return bigfloat_is_nan(&z->re) || bigfloat_is_nan(&z->im);
}

/* 将两个分量置为 NaN（零尾数，免分配） */
static void set_nan_components(bigcomplex_float_ty *dst)
{
    bigfloat_free(&dst->re);
    bigfloat_free(&dst->im);
    bigfloat_init(&dst->re);
    bigfloat_init(&dst->im);
    dst->re.flag = BIGFLOAT_NAN_E;
    dst->im.flag = BIGFLOAT_NAN_E;
}

/* ------------------------------------------------------------------ */
/* 生命周期与构造（§9.2）                                              */
/* ------------------------------------------------------------------ */

bigcomplex_float_err_ty bigcomplex_float_init(bigcomplex_float_ty *cpx)
{
    if (cpx == NULL) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    const bigfloat_err_ty re = bigfloat_init(&cpx->re);
    if (re != BIGFLOAT_OK_E) {
        return map_bf_err(re);
    }
    const bigfloat_err_ty im = bigfloat_init(&cpx->im);
    if (im != BIGFLOAT_OK_E) {
        bigfloat_free(&cpx->re);
        return map_bf_err(im);
    }
    return BIGCOMPLEX_FLOAT_OK_E;
}

void bigcomplex_float_free(bigcomplex_float_ty *cpx)
{
    if (cpx == NULL) {
        return;
    }
    bigfloat_free(&cpx->re);
    bigfloat_free(&cpx->im);
}

bigcomplex_float_err_ty bigcomplex_float_copy(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    bigfloat_ty re_tmp;
    bigfloat_ty im_tmp;
    bigfloat_init(&re_tmp);
    bigfloat_init(&im_tmp);
    bigfloat_err_ty berr = bigfloat_copy(&re_tmp, &src->re);
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&re_tmp);
        bigfloat_free(&im_tmp);
        return map_bf_err(berr);
    }
    berr = bigfloat_copy(&im_tmp, &src->im);
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&re_tmp);
        bigfloat_free(&im_tmp);
        return map_bf_err(berr);
    }
    /* 全部成功后再覆盖 dst（强异常安全） */
    bigfloat_free(&dst->re);
    bigfloat_free(&dst->im);
    dst->re = re_tmp;
    dst->im = im_tmp;
    return BIGCOMPLEX_FLOAT_OK_E;
}

bigcomplex_float_err_ty bigcomplex_float_from_parts(bigcomplex_float_ty *cpx,
        const bigfloat_ty *re, const bigfloat_ty *im)
{
    if ((cpx == NULL) || (re == NULL) || (im == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    return bigcomplex_float_copy(cpx,
            &(bigcomplex_float_ty){ .re = *re, .im = *im });
}

/* ------------------------------------------------------------------ */
/* 字符串 I/O（§9.2）                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: 从字符串解析复数
 * note: 接受 "a+bi" / "a-bi" / "a" / "bi"；完整字面量后的尾随字符按
 *       部分消费约定处理；"a±..." 的虚部缺 'i' 后缀 → 整字面量非法
 */
bigcomplex_float_err_ty bigcomplex_float_from_str(bigcomplex_float_ty *cpx,
        const char *str, const bigfloat_ctx_ty *ctx, const char **end)
{
    if ((cpx == NULL) || (str == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    if (end != NULL) {
        *end = str;  // 默认：未消费
    }

    bigfloat_ty re;
    bigfloat_ty im;
    bigfloat_init(&re);
    bigfloat_init(&im);

    const char *e1 = str;
    bigfloat_err_ty berr = bigfloat_from_str(&re, str, ctx, &e1);
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&re);
        bigfloat_free(&im);
        return BIGCOMPLEX_FLOAT_ERR_PARSE_E;
    }

    if (*e1 == 'i') {
        // "bi" 形式：解析得到的实部实际是虚部
        bigfloat_ty tmp;
        bigfloat_init(&tmp);
        bigfloat_copy(&tmp, &re);
        bigfloat_free(&re);
        bigfloat_init(&re);  // re = +0
        im = tmp;
        if (end != NULL) {
            *end = e1 + 1;
        }
    } else if ((*e1 == '+') || (*e1 == '-')) {
        const char sign = *e1;
        const char *e2 = e1 + 1;
        berr = bigfloat_from_str(&im, e2, ctx, &e2);
        if ((berr != BIGFLOAT_OK_E) || (*e2 != 'i')) {
            bigfloat_free(&re);
            bigfloat_free(&im);
            return BIGCOMPLEX_FLOAT_ERR_PARSE_E;  // 虚部缺失或缺 'i'
        }
        if (sign == '-') {
            bigfloat_neg(&im);
        }
        if (end != NULL) {
            *end = e2 + 1;
        }
    } else {
        // 仅实部：im = +0（已初始化）
        if (end != NULL) {
            *end = e1;
        }
    }

    /* 组装（强异常安全：全部成功后再覆盖 cpx） */
    bigfloat_free(&cpx->re);
    bigfloat_free(&cpx->im);
    cpx->re = re;
    cpx->im = im;
    return BIGCOMPLEX_FLOAT_OK_E;
}

/*
 * brief: 输出 "a+bi" 形式
 */
bigcomplex_float_err_ty bigcomplex_float_to_str(const bigcomplex_float_ty *cpx,
        size_t max_digits, char *buf, size_t buf_len, size_t *needed)
{
    if (cpx == NULL) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    if (needed != NULL) {
        *needed = 0U;
    }

    /* 渲染两个分量（先查询长度再分配） */
    size_t re_need = 0U;
    size_t im_need = 0U;
    bigfloat_err_ty berr = bigfloat_to_str(&cpx->re, max_digits, NULL, 0,
            &re_need);
    if (berr != BIGFLOAT_OK_E) {
        return map_bf_err(berr);
    }
    berr = bigfloat_to_str(&cpx->im, max_digits, NULL, 0, &im_need);
    if (berr != BIGFLOAT_OK_E) {
        return map_bf_err(berr);
    }
    char *re_str = (char *)nex_malloc(re_need);
    char *im_str = (char *)nex_malloc(im_need);
    if ((re_str == NULL) || (im_str == NULL)) {
        free(re_str);
        free(im_str);
        return BIGCOMPLEX_FLOAT_ERR_OOM_E;
    }
    berr = bigfloat_to_str(&cpx->re, max_digits, re_str, re_need, NULL);
    if (berr != BIGFLOAT_OK_E) {
        free(re_str);
        free(im_str);
        return map_bf_err(berr);
    }
    berr = bigfloat_to_str(&cpx->im, max_digits, im_str, im_need, NULL);
    if (berr != BIGFLOAT_OK_E) {
        free(re_str);
        free(im_str);
        return map_bf_err(berr);
    }

    const size_t re_len = strlen(re_str);
    const size_t im_len = strlen(im_str);
    const bool re_zero = bigfloat_is_zero(&cpx->re);
    const bool im_zero = bigfloat_is_zero(&cpx->im);
    const bool im_neg = (cpx->im.flag == BIGFLOAT_NEG_E)
            || (cpx->im.flag == BIGFLOAT_NEG_ZERO_E)
            || (cpx->im.flag == BIGFLOAT_NEG_INF_E);

    size_t total = 0U;
    if (im_zero) {
        total = re_len;
    } else if (re_zero) {
        total = im_len + 1U;  // im + 'i'
    } else {
        total = re_len + 1U + im_len + 1U;  // re + 连接符 + |im| + 'i'
    }
    const size_t need_total = total + 1U;  // 含 '\0'
    if (needed != NULL) {
        *needed = need_total;
    }
    if (buf == NULL) {
        free(re_str);
        free(im_str);
        return BIGCOMPLEX_FLOAT_OK_E;  // 仅查询
    }
    if (buf_len < need_total) {
        free(re_str);
        free(im_str);
        return BIGCOMPLEX_FLOAT_ERR_OVERFLOW_E;
    }

    size_t pos = 0U;
    if (im_zero) {
        memcpy(buf, re_str, re_len);
        pos = re_len;
    } else if (re_zero) {
        memcpy(buf, im_str, im_len);
        pos = im_len;
        buf[pos++] = 'i';
    } else {
        memcpy(buf, re_str, re_len);
        pos = re_len;
        if (im_neg) {
            buf[pos++] = '-';
            /* 虚部串以 '-' 开头（含 -inf 等），跳过符号字符 */
            memcpy(buf + pos, im_str + 1, im_len - 1U);
            pos += im_len - 1U;
        } else {
            buf[pos++] = '+';
            memcpy(buf + pos, im_str, im_len);
            pos += im_len;
        }
        buf[pos++] = 'i';
    }
    buf[pos] = '\0';
    free(re_str);
    free(im_str);
    return BIGCOMPLEX_FLOAT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 分量访问与一元运算（§9.2）                                          */
/* ------------------------------------------------------------------ */

bigcomplex_float_err_ty bigcomplex_float_conj(bigcomplex_float_ty *cpx)
{
    if (cpx == NULL) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    return map_bf_err(bigfloat_neg(&cpx->im));
}

bigcomplex_float_err_ty bigcomplex_float_abs(bigfloat_ty *dst,
        const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    bigfloat_ty re2;
    bigfloat_ty im2;
    bigfloat_ty sum;
    bigfloat_init(&re2);
    bigfloat_init(&im2);
    bigfloat_init(&sum);
    bigfloat_err_ty berr = bigfloat_mul(&re2, &src->re, &src->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&im2, &src->im, &src->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_add(&sum, &re2, &im2, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_sqrt(dst, &sum, ctx);
cleanup:
    bigfloat_free(&re2);
    bigfloat_free(&im2);
    bigfloat_free(&sum);
    return map_bf_err(berr);
}

bigcomplex_float_err_ty bigcomplex_float_arg(bigfloat_ty *dst,
        const bigcomplex_float_ty *src, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    /* 依赖 atan，v1 未实现（§9.2） */
    return BIGCOMPLEX_FLOAT_ERR_UNSUPPORTED_E;
}

/* ------------------------------------------------------------------ */
/* 四则（§9.2）                                                        */
/* ------------------------------------------------------------------ */

bigcomplex_float_err_ty bigcomplex_float_add(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_FLOAT_OK_E;
    }
    bigfloat_ty re;
    bigfloat_ty im;
    bigfloat_init(&re);
    bigfloat_init(&im);
    bigfloat_err_ty berr = bigfloat_add(&re, &lhs->re, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_add(&im, &lhs->im, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    bigfloat_free(&dst->re);
    bigfloat_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGFLOAT_OK_E;
cleanup:
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&re);
        bigfloat_free(&im);
    }
    return map_bf_err(berr);
}

bigcomplex_float_err_ty bigcomplex_float_sub(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_FLOAT_OK_E;
    }
    bigfloat_ty re;
    bigfloat_ty im;
    bigfloat_init(&re);
    bigfloat_init(&im);
    bigfloat_err_ty berr = bigfloat_sub(&re, &lhs->re, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_sub(&im, &lhs->im, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    bigfloat_free(&dst->re);
    bigfloat_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGFLOAT_OK_E;
cleanup:
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&re);
        bigfloat_free(&im);
    }
    return map_bf_err(berr);
}

bigcomplex_float_err_ty bigcomplex_float_mul(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_FLOAT_OK_E;
    }
    // (a+bi)(c+di) = (ac−bd) + (bc+ad)i
    bigfloat_ty ac;
    bigfloat_ty bd;
    bigfloat_ty bc;
    bigfloat_ty ad;
    bigfloat_ty re;
    bigfloat_ty im;
    bigfloat_init(&ac);
    bigfloat_init(&bd);
    bigfloat_init(&bc);
    bigfloat_init(&ad);
    bigfloat_init(&re);
    bigfloat_init(&im);
    bigfloat_err_ty berr = bigfloat_mul(&ac, &lhs->re, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&bd, &lhs->im, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&bc, &lhs->im, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&ad, &lhs->re, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_sub(&re, &ac, &bd, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_add(&im, &bc, &ad, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    bigfloat_free(&dst->re);
    bigfloat_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGFLOAT_OK_E;
cleanup:
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&ac);
        bigfloat_free(&bd);
        bigfloat_free(&bc);
        bigfloat_free(&ad);
        bigfloat_free(&re);
        bigfloat_free(&im);
    }
    return map_bf_err(berr);
}

bigcomplex_float_err_ty bigcomplex_float_div(bigcomplex_float_ty *dst,
        const bigcomplex_float_ty *lhs, const bigcomplex_float_ty *rhs,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_FLOAT_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_FLOAT_OK_E;
    }
    // 共轭法：(a+bi)/(c+di) = [(a+bi)(c−di)] / (c²+d²)
    //   re_num = ac + bd；im_num = bc − ad；den = c² + d²
    bigfloat_ty ac;
    bigfloat_ty bd;
    bigfloat_ty bc;
    bigfloat_ty ad;
    bigfloat_ty cc;
    bigfloat_ty dd;
    bigfloat_ty re_num;
    bigfloat_ty im_num;
    bigfloat_ty den;
    bigfloat_ty re;
    bigfloat_ty im;
    bigfloat_init(&ac);
    bigfloat_init(&bd);
    bigfloat_init(&bc);
    bigfloat_init(&ad);
    bigfloat_init(&cc);
    bigfloat_init(&dd);
    bigfloat_init(&re_num);
    bigfloat_init(&im_num);
    bigfloat_init(&den);
    bigfloat_init(&re);
    bigfloat_init(&im);
    bigfloat_err_ty berr = bigfloat_mul(&ac, &lhs->re, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&bd, &lhs->im, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&bc, &lhs->im, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&ad, &lhs->re, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&cc, &rhs->re, &rhs->re, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_mul(&dd, &rhs->im, &rhs->im, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_add(&re_num, &ac, &bd, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_sub(&im_num, &bc, &ad, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_add(&den, &cc, &dd, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_div(&re, &re_num, &den, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    berr = bigfloat_div(&im, &im_num, &den, ctx);
    if (berr != BIGFLOAT_OK_E) {
        goto cleanup;
    }
    bigfloat_free(&dst->re);
    bigfloat_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGFLOAT_OK_E;
cleanup:
    if (berr != BIGFLOAT_OK_E) {
        bigfloat_free(&ac);
        bigfloat_free(&bd);
        bigfloat_free(&bc);
        bigfloat_free(&ad);
        bigfloat_free(&cc);
        bigfloat_free(&dd);
        bigfloat_free(&re_num);
        bigfloat_free(&im_num);
        bigfloat_free(&den);
        bigfloat_free(&re);
        bigfloat_free(&im);
    }
    return map_bf_err(berr);
}

/* ------------------------------------------------------------------ */
/* 相等性（§9.2）                                                      */
/* ------------------------------------------------------------------ */

bool bigcomplex_float_eq(const bigcomplex_float_ty *lhs,
        const bigcomplex_float_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return false;
    }
    return bigfloat_eq(&lhs->re, &rhs->re) && bigfloat_eq(&lhs->im, &rhs->im);
}
