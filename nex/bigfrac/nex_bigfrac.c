/*
 * nex_bigfrac.c：bigfrac_ty（任意精度有理数，既约分数）实现（设计文档 §6）。
 *
 * 职责：生命周期、构造（from_ints / from_str）、四则与一元运算（自动约分、
 * 交叉约分）、比较与访问器。
 *
 * 规范化不变式（§3.4）：
 *   - den > 0（符号由 num 承担）；
 *   - gcd(|num|, den) == 1（既约）；
 *   - 零的唯一表示为 0/1。
 *
 * 强异常安全（§11）：所有失败路径输出参数保持调用前状态——先计算到临时
 * 对象，成功后再整体移交（frac_move）。
 *
 * 算法要点（§6.3）：
 *   - 加 / 减：先按 g = gcd(den_l, den_r) 缩分母（b1 = b/g、d1 = d/g），
 *     num = a·d1 ± c·b1，den = b1·d（= lcm），最后全约分兜底；
 *   - 乘 / 除：交叉约分后再相乘，抑制中间结果肢数膨胀，最后全约分兜底；
 *   - 约分：二进制 GCD（bigint_bin_gcd），仅需移位与减法。
 *
 * 待办（依赖 bigfloat / bigdecimal 模块，落地后补充）：
 *   - bigfrac_to_bigfloat / bigfrac_to_bigdecimal（§6.2 有损转换）。
 */

#include "nex/bigfrac/nex_bigfrac.h"

#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: bigint 层错误码映射到 bigfrac 层（§3.1 同名分类一一映射；
 *        无同名分类的 OVERFLOW / UNSUPPORTED 等映射为 INVALID）
 * param: err bigint 层错误码
 * return: 对应的 bigfrac 层错误码
 */
static bigfrac_err_ty map_err(bigint_err_ty err)
{
    switch (err) {
    case BIGINT_OK_E:
        return BIGFRAC_OK_E;
    case BIGINT_ERR_OOM_E:
        return BIGFRAC_ERR_OOM_E;
    case BIGINT_ERR_INVALID_E:
        return BIGFRAC_ERR_INVALID_E;
    case BIGINT_ERR_DIV_ZERO_E:
        return BIGFRAC_ERR_DIV_ZERO_E;
    case BIGINT_ERR_PARSE_E:
        return BIGFRAC_ERR_PARSE_E;
    default:
        return BIGFRAC_ERR_INVALID_E;
    }
}

/*
 * brief: 全约分并归一符号：g = gcd(|num|, |den|) 后 num /= g、den /= g，
 *        若 den 为负则 num、den 同取负（符号转移到分子）
 * param: frac 目标对象（调用方保证其 num / den 已初始化）
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（frac 不变）
 * note: 结果为零时归一为 0/1（gcd(0, d) = |d| 使之自然成立）
 */
static bigfrac_err_ty frac_reduce(bigfrac_ty *frac)
{
    bigint_bin_ty g;
    bigint_bin_ty q;
    (void)bigint_bin_init(&g);  // 栈对象非空，恒成功
    (void)bigint_bin_init(&q);

    bigfrac_err_ty err = map_err(bigint_bin_gcd(&g, &frac->num, &frac->den));
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&q, NULL, &frac->num, &g));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&frac->den, NULL, &frac->den, &g));
    }
    if (err == BIGFRAC_OK_E) {
        bigint_bin_move(&frac->num, &q);
        if (bigint_bin_sign(&frac->den) == BIGINT_SIGN_NEG_E) {
            err = map_err(bigint_bin_neg(&frac->num));
            if (err == BIGFRAC_OK_E) {
                err = map_err(bigint_bin_neg(&frac->den));
            }
        }
    }

    bigint_bin_free(&g);
    bigint_bin_free(&q);
    return err;
}

/*
 * brief: 整体移交：dst 接管 src 的资源（dst 原有资源被释放）
 * param: dst 目标对象（已初始化）
 * param: src 源对象，调用后重置为零初始化状态
 */
static void frac_move(bigfrac_ty *dst, bigfrac_ty *src)
{
    bigfrac_free(dst);
    bigint_bin_move(&dst->num, &src->num);
    bigint_bin_move(&dst->den, &src->den);
}

/* ------------------------------------------------------------------ */
/* 内部实现：四则（输出到 out，out 与 lhs / rhs 可别名）                    */
/* ------------------------------------------------------------------ */

/*
 * brief: out = lhs ± rhs（subtract 为 true 时取差）；结果自动约分
 * param: out  目标对象（已初始化）
 * param: lhs  左操作数
 * param: rhs  右操作数
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）
 * note: 内部临时量失败时 out 保持调用前状态
 */
static bigfrac_err_ty frac_addsub(bigfrac_ty *out, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs, bool subtract)
{
    bigint_bin_ty g;
    bigint_bin_ty b1;
    bigint_bin_ty d1;
    bigint_bin_ty t;
    bigint_bin_ty num;
    bigint_bin_ty den;
    (void)bigint_bin_init(&g);
    (void)bigint_bin_init(&b1);
    (void)bigint_bin_init(&d1);
    (void)bigint_bin_init(&t);
    (void)bigint_bin_init(&num);
    (void)bigint_bin_init(&den);

    bigfrac_err_ty err = map_err(bigint_bin_gcd(&g, &lhs->den, &rhs->den));
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&b1, NULL, &lhs->den, &g));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&d1, NULL, &rhs->den, &g));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&den, &b1, &rhs->den));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&num, &lhs->num, &d1));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&t, &rhs->num, &b1));
    }
    if (err == BIGFRAC_OK_E) {
        if (subtract) {
            err = map_err(bigint_bin_sub(&num, &num, &t));
        } else {
            err = map_err(bigint_bin_add(&num, &num, &t));
        }
    }
    if (err == BIGFRAC_OK_E) {
        bigint_bin_move(&out->num, &num);
        bigint_bin_move(&out->den, &den);
        err = frac_reduce(out);
        if (err != BIGFRAC_OK_E) {
            bigfrac_free(out);        // 约分失败：丢弃部分结果
            (void)bigfrac_init(out);  // 恢复为零初始化，调用方可安全 free
        }
    }

    bigint_bin_free(&g);
    bigint_bin_free(&b1);
    bigint_bin_free(&d1);
    bigint_bin_free(&t);
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    return err;
}

/*
 * brief: out = lhs × rhs；交叉约分后相乘，结果自动约分
 * param: out  目标对象（已初始化）
 * param: lhs  左操作数
 * param: rhs  右操作数
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）
 * note: g1 = gcd(num_l, den_r)、g2 = gcd(num_r, den_l)，先约再乘（§6.3）
 */
static bigfrac_err_ty frac_mul(bigfrac_ty *out, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs)
{
    bigint_bin_ty g1;
    bigint_bin_ty g2;
    bigint_bin_ty a1;
    bigint_bin_ty c1;
    bigint_bin_ty b2;
    bigint_bin_ty d2;
    bigint_bin_ty num;
    bigint_bin_ty den;
    (void)bigint_bin_init(&g1);
    (void)bigint_bin_init(&g2);
    (void)bigint_bin_init(&a1);
    (void)bigint_bin_init(&c1);
    (void)bigint_bin_init(&b2);
    (void)bigint_bin_init(&d2);
    (void)bigint_bin_init(&num);
    (void)bigint_bin_init(&den);

    bigfrac_err_ty err = map_err(bigint_bin_gcd(&g1, &lhs->num, &rhs->den));
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_gcd(&g2, &rhs->num, &lhs->den));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&a1, NULL, &lhs->num, &g1));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&d2, NULL, &rhs->den, &g1));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&c1, NULL, &rhs->num, &g2));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&b2, NULL, &lhs->den, &g2));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&num, &a1, &c1));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&den, &b2, &d2));
    }
    if (err == BIGFRAC_OK_E) {
        bigint_bin_move(&out->num, &num);
        bigint_bin_move(&out->den, &den);
        err = frac_reduce(out);
        if (err != BIGFRAC_OK_E) {
            bigfrac_free(out);
            (void)bigfrac_init(out);
        }
    }

    bigint_bin_free(&g1);
    bigint_bin_free(&g2);
    bigint_bin_free(&a1);
    bigint_bin_free(&c1);
    bigint_bin_free(&b2);
    bigint_bin_free(&d2);
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    return err;
}

/*
 * brief: out = lhs ÷ rhs；交叉约分后相乘，结果自动约分
 * param: out  目标对象（已初始化）
 * param: lhs  左操作数
 * param: rhs  右操作数，为零返回 BIGFRAC_ERR_DIV_ZERO_E（out 不变）
 * return: 成功返回 BIGFRAC_OK_E；rhs 为零返回 BIGFRAC_ERR_DIV_ZERO_E；
 *         内存不足返回 BIGFRAC_ERR_OOM_E（out 不变）
 * note: g1 = gcd(num_l, num_r)、g2 = gcd(den_l, den_r)，先约再乘（§6.3）
 */
static bigfrac_err_ty frac_div(bigfrac_ty *out, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs)
{
    if (bigint_bin_is_zero(&rhs->num)) {
        return BIGFRAC_ERR_DIV_ZERO_E;
    }

    bigint_bin_ty g1;
    bigint_bin_ty g2;
    bigint_bin_ty a1;
    bigint_bin_ty c1;
    bigint_bin_ty b2;
    bigint_bin_ty d2;
    bigint_bin_ty num;
    bigint_bin_ty den;
    (void)bigint_bin_init(&g1);
    (void)bigint_bin_init(&g2);
    (void)bigint_bin_init(&a1);
    (void)bigint_bin_init(&c1);
    (void)bigint_bin_init(&b2);
    (void)bigint_bin_init(&d2);
    (void)bigint_bin_init(&num);
    (void)bigint_bin_init(&den);

    bigfrac_err_ty err = map_err(bigint_bin_gcd(&g1, &lhs->num, &rhs->num));
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_gcd(&g2, &lhs->den, &rhs->den));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&a1, NULL, &lhs->num, &g1));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&c1, NULL, &rhs->num, &g1));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&b2, NULL, &lhs->den, &g2));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_div_rem(&d2, NULL, &rhs->den, &g2));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&num, &a1, &d2));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&den, &b2, &c1));
    }
    if (err == BIGFRAC_OK_E) {
        bigint_bin_move(&out->num, &num);
        bigint_bin_move(&out->den, &den);
        err = frac_reduce(out);
        if (err != BIGFRAC_OK_E) {
            bigfrac_free(out);
            (void)bigfrac_init(out);
        }
    }

    bigint_bin_free(&g1);
    bigint_bin_free(&g2);
    bigint_bin_free(&a1);
    bigint_bin_free(&c1);
    bigint_bin_free(&b2);
    bigint_bin_free(&d2);
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    return err;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化为零（规范表示 0/1）
 * return: 成功返回 BIGFRAC_OK_E；frac 为 NULL 返回 BIGFRAC_ERR_INVALID_E；
 *         内存不足返回 BIGFRAC_ERR_OOM_E
 */
bigfrac_err_ty bigfrac_init(bigfrac_ty *frac)
{
    if (frac == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    (void)bigint_bin_init(&frac->num);  // 非空，恒成功
    (void)bigint_bin_init(&frac->den);
    return map_err(bigint_bin_from_u64(&frac->den, 1U));
}

/*
 * brief: 释放有理数占用的内存；容忍 NULL 与零初始化对象
 */
void bigfrac_free(bigfrac_ty *frac)
{
    if (frac == NULL) {
        return;
    }
    bigint_bin_free(&frac->num);
    bigint_bin_free(&frac->den);
}

/*
 * brief: 深拷贝有理数
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）
 */
bigfrac_err_ty bigfrac_copy(bigfrac_ty *dst, const bigfrac_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (dst == src) {
        return BIGFRAC_OK_E;
    }

    bigfrac_ty tmp;
    bigfrac_err_ty err = bigfrac_init(&tmp);
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_copy(&tmp.num, &src->num));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_copy(&tmp.den, &src->den));
    }
    if (err == BIGFRAC_OK_E) {
        frac_move(dst, &tmp);
    }
    bigfrac_free(&tmp);
    return err;
}

/* ------------------------------------------------------------------ */
/* 构造                                                                */
/* ------------------------------------------------------------------ */

/*
 * brief: 以分子分母构造有理数，自动约分并归一符号
 * return: 成功返回 BIGFRAC_OK_E；den 为零返回 BIGFRAC_ERR_DIV_ZERO_E
 *         （frac 不变）；内存不足返回 BIGFRAC_ERR_OOM_E（frac 不变）
 */
bigfrac_err_ty bigfrac_from_ints(bigfrac_ty *frac, const bigint_bin_ty *num,
        const bigint_bin_ty *den)
{
    if ((frac == NULL) || (num == NULL) || (den == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (bigint_bin_is_zero(den)) {
        return BIGFRAC_ERR_DIV_ZERO_E;
    }

    /* 先计算到临时对象：a = |num| / g，b = |den| / g，g = gcd(|num|, |den|) */
    bigfrac_ty tmp;
    bigfrac_err_ty err = bigfrac_init(&tmp);
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_copy(&tmp.num, num));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_copy(&tmp.den, den));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_abs(&tmp.num));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_abs(&tmp.den));
    }
    if (err == BIGFRAC_OK_E) {
        err = frac_reduce(&tmp);
    }
    if (err == BIGFRAC_OK_E) {
        const bool is_neg = (bigint_bin_sign(num) == BIGINT_SIGN_NEG_E)
                != (bigint_bin_sign(den) == BIGINT_SIGN_NEG_E);
        if (is_neg) {
            err = map_err(bigint_bin_neg(&tmp.num));
        }
    }
    if (err == BIGFRAC_OK_E) {
        frac_move(frac, &tmp);
    }
    bigfrac_free(&tmp);
    return err;
}

/*
 * brief: 从字符串解析有理数（整数 / 分数 / 十进制小数，精确化简）
 * return: 成功返回 BIGFRAC_OK_E；首字符即非法返回 BIGFRAC_ERR_PARSE_E 且
 *         frac 不变；"p/0" 返回 BIGFRAC_ERR_DIV_ZERO_E；内存不足返回
 *         BIGFRAC_ERR_OOM_E（frac 不变）
 * note: 部分消费容错（§11）；圈复杂度约 13，源自三形态解析与错误归并
 *       分支，逻辑线性（规范 §5.4）
 */
bigfrac_err_ty bigfrac_from_str(bigfrac_ty *frac, const char *str,
        const char **end)
{
    if ((frac == NULL) || (str == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (end != NULL) {
        *end = str;  // 失败视同未消费
    }

    /* 文法：[sign] digits ['.' digits] | [sign] digits '/' digits */
    const char *cur = str;
    bool is_neg = false;
    if (*cur == '-') {
        is_neg = true;
        cur++;
    }

    /* 整数部分（可为空，如 ".5"） */
    const char *int_start = cur;
    while ((*cur >= '0') && (*cur <= '9')) {
        cur++;
    }
    const size_t int_len = (size_t)(cur - int_start);

    /* 小数部分（可为空，如 "5."） */
    const char *frac_start = NULL;
    size_t frac_len = 0U;
    if (*cur == '.') {
        frac_start = cur + 1;
        cur++;
        while ((*cur >= '0') && (*cur <= '9')) {
            cur++;
        }
        frac_len = (size_t)(cur - frac_start);
    }

    /* 分数部分（仅当无小数部分时尝试；分母缺失视为整数，如 "12/"） */
    const char *den_start = NULL;
    size_t den_len = 0U;
    const char *slash = NULL;
    if ((frac_len == 0U) && (*cur == '/')) {
        slash = cur;
        den_start = cur + 1;
        cur++;
        while ((*cur >= '0') && (*cur <= '9')) {
            cur++;
        }
        den_len = (size_t)(cur - den_start);
    }
    const bool has_frac_part = (slash != NULL) && (den_len > 0U)
            && (int_len > 0U);
    const char *stop = (has_frac_part || (slash == NULL)) ? cur : slash;

    if ((int_len == 0U) && (frac_len == 0U)) {
        return BIGFRAC_ERR_PARSE_E;  // 仅符号或无任何数字
    }
    if (end != NULL) {
        *end = stop;
    }

    if (has_frac_part) {
        /* 分子分母为原串中的连续数字段（其后为 '/' 或串尾） */
        bigint_bin_ty num;
        bigint_bin_ty den;
        (void)bigint_bin_init(&num);
        (void)bigint_bin_init(&den);
        bigfrac_err_ty err = map_err(
                bigint_bin_from_str(&num, int_start, 10, NULL));
        if (err == BIGFRAC_OK_E) {
            err = map_err(bigint_bin_from_str(&den, den_start, 10, NULL));
        }
        if (err == BIGFRAC_OK_E) {
            if (is_neg) {
                err = map_err(bigint_bin_neg(&num));  // 零取负仍为零
            }
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_from_ints(frac, &num, &den);
        }
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        return err;
    }

    /* 十进制：num = int_val·10^flen + frac_val，den = 10^flen */
    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_ty ten;
    bigint_bin_ty tmp;
    (void)bigint_bin_init(&num);
    (void)bigint_bin_init(&den);
    (void)bigint_bin_init(&ten);
    (void)bigint_bin_init(&tmp);

    bigfrac_err_ty err = BIGFRAC_OK_E;
    if (int_len > 0U) {
        err = map_err(bigint_bin_from_str(&num, int_start, 10, NULL));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_from_u64(&ten, 10U));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_pow(&den, &ten, (uint64_t)frac_len));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_mul(&num, &num, &den));
    }
    if ((err == BIGFRAC_OK_E) && (frac_len > 0U)) {
        err = map_err(bigint_bin_from_str(&tmp, frac_start, 10, NULL));
        if (err == BIGFRAC_OK_E) {
            err = map_err(bigint_bin_add(&num, &num, &tmp));
        }
    }
    if ((err == BIGFRAC_OK_E) && is_neg) {
        err = map_err(bigint_bin_neg(&num));  // 零取负仍为零
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_from_ints(frac, &num, &den);
    }

    bigint_bin_free(&num);
    bigint_bin_free(&den);
    bigint_bin_free(&ten);
    bigint_bin_free(&tmp);
    return err;
}

/* ------------------------------------------------------------------ */
/* 算术                                                                */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）
 */
bigfrac_err_ty bigfrac_add(bigfrac_ty *dst, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty tmp;
    bigfrac_err_ty err = bigfrac_init(&tmp);
    if (err == BIGFRAC_OK_E) {
        err = frac_addsub(&tmp, lhs, rhs, false);
    }
    if (err == BIGFRAC_OK_E) {
        frac_move(dst, &tmp);
    }
    bigfrac_free(&tmp);
    return err;
}

/*
 * brief: 减法 dst = lhs - rhs
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）
 */
bigfrac_err_ty bigfrac_sub(bigfrac_ty *dst, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty tmp;
    bigfrac_err_ty err = bigfrac_init(&tmp);
    if (err == BIGFRAC_OK_E) {
        err = frac_addsub(&tmp, lhs, rhs, true);
    }
    if (err == BIGFRAC_OK_E) {
        frac_move(dst, &tmp);
    }
    bigfrac_free(&tmp);
    return err;
}

/*
 * brief: 乘法 dst = lhs × rhs
 * return: 成功返回 BIGFRAC_OK_E；内存不足返回 BIGFRAC_ERR_OOM_E（dst 不变）
 */
bigfrac_err_ty bigfrac_mul(bigfrac_ty *dst, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty tmp;
    bigfrac_err_ty err = bigfrac_init(&tmp);
    if (err == BIGFRAC_OK_E) {
        err = frac_mul(&tmp, lhs, rhs);
    }
    if (err == BIGFRAC_OK_E) {
        frac_move(dst, &tmp);
    }
    bigfrac_free(&tmp);
    return err;
}

/*
 * brief: 除法 dst = lhs ÷ rhs
 * return: rhs 为零返回 BIGFRAC_ERR_DIV_ZERO_E（dst 不变）；内存不足返回
 *         BIGFRAC_ERR_OOM_E（dst 不变）
 */
bigfrac_err_ty bigfrac_div(bigfrac_ty *dst, const bigfrac_ty *lhs,
        const bigfrac_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty tmp;
    bigfrac_err_ty err = bigfrac_init(&tmp);
    if (err == BIGFRAC_OK_E) {
        err = frac_div(&tmp, lhs, rhs);
    }
    if (err == BIGFRAC_OK_E) {
        frac_move(dst, &tmp);
    }
    bigfrac_free(&tmp);
    return err;
}

/*
 * brief: 就地取负（零不变）
 * return: 恒为 BIGFRAC_OK_E；frac 为 NULL 返回 BIGFRAC_ERR_INVALID_E
 */
bigfrac_err_ty bigfrac_neg(bigfrac_ty *frac)
{
    if (frac == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    return map_err(bigint_bin_neg(&frac->num));
}

/*
 * brief: 就地取倒数 frac = den / num（符号保持在分子上）
 * return: frac 为零返回 BIGFRAC_ERR_DIV_ZERO_E（frac 不变）；内存不足返回
 *         BIGFRAC_ERR_OOM_E（frac 不变）；frac 为 NULL 返回 BIGFRAC_ERR_INVALID_E
 */
bigfrac_err_ty bigfrac_inv(bigfrac_ty *frac)
{
    if (frac == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (bigint_bin_is_zero(&frac->num)) {
        return BIGFRAC_ERR_DIV_ZERO_E;
    }

    /* num' = sign(num)·den，den' = |num|：负号转移到新分子 */
    bigint_bin_ty new_num;
    bigint_bin_ty new_den;
    (void)bigint_bin_init(&new_num);
    (void)bigint_bin_init(&new_den);

    bigfrac_err_ty err = map_err(bigint_bin_copy(&new_num, &frac->den));
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_copy(&new_den, &frac->num));
    }
    if (err == BIGFRAC_OK_E) {
        err = map_err(bigint_bin_abs(&new_den));
    }
    if (err == BIGFRAC_OK_E) {
        if (bigint_bin_sign(&frac->num) == BIGINT_SIGN_NEG_E) {
            err = map_err(bigint_bin_neg(&new_num));
        }
    }
    if (err == BIGFRAC_OK_E) {
        bigint_bin_move(&frac->num, &new_num);
        bigint_bin_move(&frac->den, &new_den);
    }

    bigint_bin_free(&new_num);
    bigint_bin_free(&new_den);
    return err;
}

/* ------------------------------------------------------------------ */
/* 比较与访问                                                           */
/* ------------------------------------------------------------------ */

/*
 * brief: 比较两个有理数
 * return: lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正
 * note: 符号捷径优先；同号非零时交叉相乘 num_l·den_r vs num_r·den_l（§6.3）
 */
int bigfrac_cmp(const bigfrac_ty *lhs, const bigfrac_ty *rhs)
{
    const bigint_sign_ty lhs_sign = bigint_bin_sign(&lhs->num);
    const bigint_sign_ty rhs_sign = bigint_bin_sign(&rhs->num);
    if (lhs_sign != rhs_sign) {
        if (lhs_sign == BIGINT_SIGN_NEG_E) {
            return -1;  // 负 < 非负
        }
        if (rhs_sign == BIGINT_SIGN_NEG_E) {
            return 1;   // 非负 > 负
        }
        return (lhs_sign == BIGINT_SIGN_ZERO_E) ? -1 : 1;  // 零 vs 正
    }
    if (lhs_sign == BIGINT_SIGN_ZERO_E) {
        return 0;  // 双方均为零
    }

    bigint_bin_ty lhs_cross;
    bigint_bin_ty rhs_cross;
    (void)bigint_bin_init(&lhs_cross);
    (void)bigint_bin_init(&rhs_cross);
    (void)bigint_bin_mul(&lhs_cross, &lhs->num, &rhs->den);
    (void)bigint_bin_mul(&rhs_cross, &rhs->num, &lhs->den);
    const int result = bigint_bin_cmp(&lhs_cross, &rhs_cross);
    bigint_bin_free(&lhs_cross);
    bigint_bin_free(&rhs_cross);
    return result;
}

/*
 * brief: 读取分子（只读指针；frac 为 NULL 返回 NULL）
 */
const bigint_bin_ty *bigfrac_num(const bigfrac_ty *frac)
{
    if (frac == NULL) {
        return NULL;
    }
    return &frac->num;
}

/*
 * brief: 读取分母（只读指针，恒为正；frac 为 NULL 返回 NULL）
 */
const bigint_bin_ty *bigfrac_den(const bigfrac_ty *frac)
{
    if (frac == NULL) {
        return NULL;
    }
    return &frac->den;
}
