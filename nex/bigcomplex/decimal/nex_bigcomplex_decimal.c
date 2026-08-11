/*
 * nex_bigcomplex_decimal.c：bigcomplex_decimal_ty（实部/虚部均为
 * bigdecimal 的复数）实现。
 *
 * 与 nex_bigcomplex_float.c 平行（设计文档 §9）：乘法朴素四乘二加、
 * 除法共轭法、abs 经 sqrt(re² + im²)、arg v1 返回 UNSUPPORTED。
 * 复数不引入新的特殊值类别——特殊值语义完全由分量的七标志组合表达。
 */

#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

static bigcomplex_decimal_err_ty map_bd_err(bigdecimal_err_ty err)
{
    if (err == BIGDECIMAL_OK_E) {
        return BIGCOMPLEX_DECIMAL_OK_E;
    }
    if (err == BIGDECIMAL_ERR_OOM_E) {
        return BIGCOMPLEX_DECIMAL_ERR_OOM_E;
    }
    return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
}

/* 是否任一分量为 NaN（§9.3：任一分量 NaN → 整体 (NaN, NaN)） */
static bool any_nan(const bigcomplex_decimal_ty *z)
{
    return bigdecimal_is_nan(&z->re) || bigdecimal_is_nan(&z->im);
}

/* 将两个分量置为 NaN（零尾数，免分配） */
static void set_nan_components(bigcomplex_decimal_ty *dst)
{
    bigdecimal_free(&dst->re);
    bigdecimal_free(&dst->im);
    bigdecimal_init(&dst->re);
    bigdecimal_init(&dst->im);
    dst->re.flag = BIGDECIMAL_NAN_E;
    dst->im.flag = BIGDECIMAL_NAN_E;
}

/* ------------------------------------------------------------------ */
/* 生命周期与构造（§9.2）                                              */
/* ------------------------------------------------------------------ */

bigcomplex_decimal_err_ty bigcomplex_decimal_init(bigcomplex_decimal_ty *cpx)
{
    if (cpx == NULL) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    const bigdecimal_err_ty re = bigdecimal_init(&cpx->re);
    if (re != BIGDECIMAL_OK_E) {
        return map_bd_err(re);
    }
    const bigdecimal_err_ty im = bigdecimal_init(&cpx->im);
    if (im != BIGDECIMAL_OK_E) {
        bigdecimal_free(&cpx->re);
        return map_bd_err(im);
    }
    return BIGCOMPLEX_DECIMAL_OK_E;
}

void bigcomplex_decimal_free(bigcomplex_decimal_ty *cpx)
{
    if (cpx == NULL) {
        return;
    }
    bigdecimal_free(&cpx->re);
    bigdecimal_free(&cpx->im);
}

bigcomplex_decimal_err_ty bigcomplex_decimal_copy(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ty re_tmp;
    bigdecimal_ty im_tmp;
    bigdecimal_init(&re_tmp);
    bigdecimal_init(&im_tmp);
    bigdecimal_err_ty berr = bigdecimal_copy(&re_tmp, &src->re);
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&re_tmp);
        bigdecimal_free(&im_tmp);
        return map_bd_err(berr);
    }
    berr = bigdecimal_copy(&im_tmp, &src->im);
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&re_tmp);
        bigdecimal_free(&im_tmp);
        return map_bd_err(berr);
    }
    /* 全部成功后再覆盖 dst（强异常安全） */
    bigdecimal_free(&dst->re);
    bigdecimal_free(&dst->im);
    dst->re = re_tmp;
    dst->im = im_tmp;
    return BIGCOMPLEX_DECIMAL_OK_E;
}

bigcomplex_decimal_err_ty bigcomplex_decimal_from_parts(
        bigcomplex_decimal_ty *cpx, const bigdecimal_ty *re,
        const bigdecimal_ty *im)
{
    if ((cpx == NULL) || (re == NULL) || (im == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    return bigcomplex_decimal_copy(cpx,
            &(bigcomplex_decimal_ty){ .re = *re, .im = *im });
}

/* ------------------------------------------------------------------ */
/* 字符串 I/O（§9.2）                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: 从字符串解析复数
 * note: 接受 "a+bi" / "a-bi" / "a" / "bi"；完整字面量后的尾随字符按
 *       部分消费约定处理；"a±..." 的虚部缺 'i' 后缀 → 整字面量非法
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_from_str(
        bigcomplex_decimal_ty *cpx, const char *str,
        const bigdecimal_ctx_ty *ctx, const char **end)
{
    if ((cpx == NULL) || (str == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    if (end != NULL) {
        *end = str;  // 默认：未消费
    }

    bigdecimal_ty re;
    bigdecimal_ty im;
    bigdecimal_init(&re);
    bigdecimal_init(&im);

    const char *e1 = str;
    bigdecimal_err_ty berr = bigdecimal_from_str(&re, str, ctx, &e1);
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&re);
        bigdecimal_free(&im);
        return BIGCOMPLEX_DECIMAL_ERR_PARSE_E;
    }

    if (*e1 == 'i') {
        // "bi" 形式：解析得到的实部实际是虚部
        bigdecimal_ty tmp;
        bigdecimal_init(&tmp);
        bigdecimal_copy(&tmp, &re);
        bigdecimal_free(&re);
        bigdecimal_init(&re);  // re = +0
        im = tmp;
        if (end != NULL) {
            *end = e1 + 1;
        }
    } else if ((*e1 == '+') || (*e1 == '-')) {
        const char sign = *e1;
        const char *e2 = e1 + 1;
        berr = bigdecimal_from_str(&im, e2, ctx, &e2);
        if ((berr != BIGDECIMAL_OK_E) || (*e2 != 'i')) {
            bigdecimal_free(&re);
            bigdecimal_free(&im);
            return BIGCOMPLEX_DECIMAL_ERR_PARSE_E;  // 虚部缺失或缺 'i'
        }
        if (sign == '-') {
            bigdecimal_neg(&im);
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
    bigdecimal_free(&cpx->re);
    bigdecimal_free(&cpx->im);
    cpx->re = re;
    cpx->im = im;
    return BIGCOMPLEX_DECIMAL_OK_E;
}

/*
 * brief: 输出 "a+bi" 形式
 */
bigcomplex_decimal_err_ty bigcomplex_decimal_to_str(
        const bigcomplex_decimal_ty *cpx, bigdecimal_fmt_ty fmt,
        char *buf, size_t buf_len, size_t *needed)
{
    if (cpx == NULL) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    if (needed != NULL) {
        *needed = 0U;
    }

    /* 渲染两个分量（先查询长度再分配） */
    size_t re_need = 0U;
    size_t im_need = 0U;
    bigdecimal_err_ty berr = bigdecimal_to_str(&cpx->re, fmt, NULL, 0,
            &re_need);
    if (berr != BIGDECIMAL_OK_E) {
        return map_bd_err(berr);
    }
    berr = bigdecimal_to_str(&cpx->im, fmt, NULL, 0, &im_need);
    if (berr != BIGDECIMAL_OK_E) {
        return map_bd_err(berr);
    }
    char *re_str = (char *)malloc(re_need);
    char *im_str = (char *)malloc(im_need);
    if ((re_str == NULL) || (im_str == NULL)) {
        free(re_str);
        free(im_str);
        return BIGCOMPLEX_DECIMAL_ERR_OOM_E;
    }
    berr = bigdecimal_to_str(&cpx->re, fmt, re_str, re_need, NULL);
    if (berr != BIGDECIMAL_OK_E) {
        free(re_str);
        free(im_str);
        return map_bd_err(berr);
    }
    berr = bigdecimal_to_str(&cpx->im, fmt, im_str, im_need, NULL);
    if (berr != BIGDECIMAL_OK_E) {
        free(re_str);
        free(im_str);
        return map_bd_err(berr);
    }

    const size_t re_len = strlen(re_str);
    const size_t im_len = strlen(im_str);
    const bool re_zero = bigdecimal_is_zero(&cpx->re);
    const bool im_zero = bigdecimal_is_zero(&cpx->im);
    const bool im_neg = (cpx->im.flag == BIGDECIMAL_NEG_E)
            || (cpx->im.flag == BIGDECIMAL_NEG_ZERO_E)
            || (cpx->im.flag == BIGDECIMAL_NEG_INF_E);

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
        return BIGCOMPLEX_DECIMAL_OK_E;  // 仅查询
    }
    if (buf_len < need_total) {
        free(re_str);
        free(im_str);
        return BIGCOMPLEX_DECIMAL_ERR_OVERFLOW_E;
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
    return BIGCOMPLEX_DECIMAL_OK_E;
}

/* ------------------------------------------------------------------ */
/* 分量访问与一元运算（§9.2）                                          */
/* ------------------------------------------------------------------ */

bigcomplex_decimal_err_ty bigcomplex_decimal_conj(bigcomplex_decimal_ty *cpx)
{
    if (cpx == NULL) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    return map_bd_err(bigdecimal_neg(&cpx->im));
}

bigcomplex_decimal_err_ty bigcomplex_decimal_abs(bigdecimal_ty *dst,
        const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ty re2;
    bigdecimal_ty im2;
    bigdecimal_ty sum;
    bigdecimal_init(&re2);
    bigdecimal_init(&im2);
    bigdecimal_init(&sum);
    bigdecimal_err_ty berr = bigdecimal_mul(&re2, &src->re, &src->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&im2, &src->im, &src->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_add(&sum, &re2, &im2, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_sqrt(dst, &sum, ctx);
cleanup:
    bigdecimal_free(&re2);
    bigdecimal_free(&im2);
    bigdecimal_free(&sum);
    return map_bd_err(berr);
}

bigcomplex_decimal_err_ty bigcomplex_decimal_arg(bigdecimal_ty *dst,
        const bigcomplex_decimal_ty *src, const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    /* 依赖 atan，v1 未实现（§9.2） */
    return BIGCOMPLEX_DECIMAL_ERR_UNSUPPORTED_E;
}

/* ------------------------------------------------------------------ */
/* 四则（§9.2）                                                        */
/* ------------------------------------------------------------------ */

bigcomplex_decimal_err_ty bigcomplex_decimal_add(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_DECIMAL_OK_E;
    }
    bigdecimal_ty re;
    bigdecimal_ty im;
    bigdecimal_init(&re);
    bigdecimal_init(&im);
    bigdecimal_err_ty berr = bigdecimal_add(&re, &lhs->re, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_add(&im, &lhs->im, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    bigdecimal_free(&dst->re);
    bigdecimal_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGDECIMAL_OK_E;
cleanup:
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&re);
        bigdecimal_free(&im);
    }
    return map_bd_err(berr);
}

bigcomplex_decimal_err_ty bigcomplex_decimal_sub(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_DECIMAL_OK_E;
    }
    bigdecimal_ty re;
    bigdecimal_ty im;
    bigdecimal_init(&re);
    bigdecimal_init(&im);
    bigdecimal_err_ty berr = bigdecimal_sub(&re, &lhs->re, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_sub(&im, &lhs->im, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    bigdecimal_free(&dst->re);
    bigdecimal_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGDECIMAL_OK_E;
cleanup:
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&re);
        bigdecimal_free(&im);
    }
    return map_bd_err(berr);
}

bigcomplex_decimal_err_ty bigcomplex_decimal_mul(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_DECIMAL_OK_E;
    }
    // (a+bi)(c+di) = (ac−bd) + (bc+ad)i
    bigdecimal_ty ac;
    bigdecimal_ty bd;
    bigdecimal_ty bc;
    bigdecimal_ty ad;
    bigdecimal_ty re;
    bigdecimal_ty im;
    bigdecimal_init(&ac);
    bigdecimal_init(&bd);
    bigdecimal_init(&bc);
    bigdecimal_init(&ad);
    bigdecimal_init(&re);
    bigdecimal_init(&im);
    bigdecimal_err_ty berr = bigdecimal_mul(&ac, &lhs->re, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&bd, &lhs->im, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&bc, &lhs->im, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&ad, &lhs->re, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_sub(&re, &ac, &bd, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_add(&im, &bc, &ad, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    bigdecimal_free(&dst->re);
    bigdecimal_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGDECIMAL_OK_E;
cleanup:
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&ac);
        bigdecimal_free(&bd);
        bigdecimal_free(&bc);
        bigdecimal_free(&ad);
        bigdecimal_free(&re);
        bigdecimal_free(&im);
    }
    return map_bd_err(berr);
}

bigcomplex_decimal_err_ty bigcomplex_decimal_div(bigcomplex_decimal_ty *dst,
        const bigcomplex_decimal_ty *lhs, const bigcomplex_decimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (ctx == NULL)) {
        return BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
    }
    if (any_nan(lhs) || any_nan(rhs)) {
        set_nan_components(dst);
        return BIGCOMPLEX_DECIMAL_OK_E;
    }
    // 共轭法：(a+bi)/(c+di) = [(a+bi)(c−di)] / (c²+d²)
    //   re_num = ac + bd；im_num = bc − ad；den = c² + d²
    bigdecimal_ty ac;
    bigdecimal_ty bd;
    bigdecimal_ty bc;
    bigdecimal_ty ad;
    bigdecimal_ty cc;
    bigdecimal_ty dd;
    bigdecimal_ty re_num;
    bigdecimal_ty im_num;
    bigdecimal_ty den;
    bigdecimal_ty re;
    bigdecimal_ty im;
    bigdecimal_init(&ac);
    bigdecimal_init(&bd);
    bigdecimal_init(&bc);
    bigdecimal_init(&ad);
    bigdecimal_init(&cc);
    bigdecimal_init(&dd);
    bigdecimal_init(&re_num);
    bigdecimal_init(&im_num);
    bigdecimal_init(&den);
    bigdecimal_init(&re);
    bigdecimal_init(&im);
    bigdecimal_err_ty berr = bigdecimal_mul(&ac, &lhs->re, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&bd, &lhs->im, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&bc, &lhs->im, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&ad, &lhs->re, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&cc, &rhs->re, &rhs->re, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_mul(&dd, &rhs->im, &rhs->im, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_add(&re_num, &ac, &bd, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_sub(&im_num, &bc, &ad, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_add(&den, &cc, &dd, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_div(&re, &re_num, &den, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    berr = bigdecimal_div(&im, &im_num, &den, ctx);
    if (berr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }
    bigdecimal_free(&dst->re);
    bigdecimal_free(&dst->im);
    dst->re = re;
    dst->im = im;
    berr = BIGDECIMAL_OK_E;
cleanup:
    if (berr != BIGDECIMAL_OK_E) {
        bigdecimal_free(&ac);
        bigdecimal_free(&bd);
        bigdecimal_free(&bc);
        bigdecimal_free(&ad);
        bigdecimal_free(&cc);
        bigdecimal_free(&dd);
        bigdecimal_free(&re_num);
        bigdecimal_free(&im_num);
        bigdecimal_free(&den);
        bigdecimal_free(&re);
        bigdecimal_free(&im);
    }
    return map_bd_err(berr);
}

/* ------------------------------------------------------------------ */
/* 相等性（§9.2）                                                      */
/* ------------------------------------------------------------------ */

bool bigcomplex_decimal_eq(const bigcomplex_decimal_ty *lhs,
        const bigcomplex_decimal_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return false;
    }
    return bigdecimal_eq(&lhs->re, &rhs->re)
            && bigdecimal_eq(&lhs->im, &rhs->im);
}
