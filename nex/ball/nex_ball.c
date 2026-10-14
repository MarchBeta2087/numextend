#include "nex/ball/nex_ball.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 球式算术（路线 B 第一阶段，设计文档 §15.6）。
 *
 * 以 [mid − rad, mid + rad] 表示区间；四则运算在端点间用定向舍入求严格包含
 * 的区间，再由 from_interval 归一为「中心 + 半径」。半径余量覆盖中心舍入：
 *   rad = max(|mid−lo|, |hi−mid|) + ulp(rad0) + (m_hi − m_lo)
 * 其中 m_lo/m_hi 为 (lo+hi)/2 的向下/向上舍入，度量中心的表示不确定性。
 */

static bigfloat_err_ty dir_ctx(bigfloat_ctx_ty *out,
        const bigfloat_ctx_ty *base, bigfloat_round_ty round) {
    return bigfloat_ctx_make(out, base->mant_bits, base->exp_bits, round);
}

static bigfloat_err_ty bf_abs(bigfloat_ty *out, const bigfloat_ty *x) {
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bigfloat_err_ty err = bigfloat_copy(out, x);
    if (err == BIGFLOAT_OK_E && bigfloat_cmp(x, &zero) < 0) {
        err = bigfloat_neg(out);
    }
    bigfloat_free(&zero);
    return err;
}

/* out = ulp(x) = 2^{e − p}（x 为正常值时）；x 为 0/特殊值时为 0 */
static bigfloat_err_ty bf_ulp(bigfloat_ty *out, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty mant;
    bigint_bin_init(&mant);
    int64_t e = 0;
    bigfloat_flag_ty fl = BIGFLOAT_POS_ZERO_E;
    bigfloat_err_ty err = bigfloat_decompose(x, &mant, &e, &fl);
    if (err == BIGFLOAT_OK_E) {
        if (fl == BIGFLOAT_POS_E || fl == BIGFLOAT_NEG_E) {
            bigint_bin_ty one;
            bigint_bin_init(&one);
            bigint_bin_from_u64(&one, 1U);
            err = bigfloat_compose(out, &one, e - (int64_t)ctx->mant_bits,
                                   BIGFLOAT_POS_E, ctx);
            bigint_bin_free(&one);
        } else {
            err = bigfloat_from_f64(out, 0.0);
        }
    }
    bigint_bin_free(&mant);
    return err;
}

static bool bf_finite(const bigfloat_ty *x) {
    return !bigfloat_is_nan(x) && !bigfloat_is_inf(x);
}

static bool ball_valid(const bigfloat_ball_ty *b) {
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bool ok = b != NULL && bf_finite(&b->mid) && bf_finite(&b->rad) &&
              bigfloat_cmp(&b->rad, &zero) >= 0;
    bigfloat_free(&zero);
    return ok;
}

/* 由区间 [lo, hi] 构造 (mid, rad)，保证覆盖；lo ≤ hi */
static bigfloat_err_ty from_interval(bigfloat_ball_ty *dst,
        const bigfloat_ty *lo, const bigfloat_ty *hi,
        const bigfloat_ctx_ty *near) {
    bigfloat_ctx_ty dn;
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = dir_ctx(&dn, near, BIGFLOAT_ROUND_TOWARD_NEG_E);
    if (err == BIGFLOAT_OK_E) {
        err = dir_ctx(&up, near, BIGFLOAT_ROUND_TOWARD_POS_E);
    }

    bigfloat_ty two;
    bigfloat_ty s_lo;
    bigfloat_ty s_hi;
    bigfloat_ty sum;
    bigfloat_ty mid;
    bigfloat_ty m_lo;
    bigfloat_ty m_hi;
    bigfloat_ty unc;
    bigfloat_ty d1;
    bigfloat_ty d2;
    bigfloat_ty m1;
    bigfloat_ty m2;
    bigfloat_ty rad;
    bigfloat_ty u;
    bigfloat_init(&two);
    bigfloat_init(&s_lo);
    bigfloat_init(&s_hi);
    bigfloat_init(&sum);
    bigfloat_init(&mid);
    bigfloat_init(&m_lo);
    bigfloat_init(&m_hi);
    bigfloat_init(&unc);
    bigfloat_init(&d1);
    bigfloat_init(&d2);
    bigfloat_init(&m1);
    bigfloat_init(&m2);
    bigfloat_init(&rad);
    bigfloat_init(&u);

    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&two, 2.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&s_lo, lo, hi, &dn);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&s_hi, lo, hi, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&sum, lo, hi, near);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_div(&mid, &sum, &two, near);      /* /2 精确 */
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_div(&m_lo, &s_lo, &two, near);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_div(&m_hi, &s_hi, &two, near);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&unc, &m_hi, &m_lo, &up);     /* 中心不确定性 */
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&d1, &mid, lo, near);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&d2, hi, &mid, near);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bf_abs(&m1, &d1);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bf_abs(&m2, &d2);
    }
    if (err == BIGFLOAT_OK_E) {
        err = (bigfloat_cmp(&m1, &m2) >= 0) ? bigfloat_copy(&rad, &m1)
                                            : bigfloat_copy(&rad, &m2);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bf_ulp(&u, &rad, near);                    /* 覆盖减法舍入 */
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&rad, &rad, &u, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&rad, &rad, &unc, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(&dst->mid, &mid);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(&dst->rad, &rad);
    }

    bigfloat_free(&u);
    bigfloat_free(&rad);
    bigfloat_free(&m2);
    bigfloat_free(&m1);
    bigfloat_free(&d2);
    bigfloat_free(&d1);
    bigfloat_free(&unc);
    bigfloat_free(&m_hi);
    bigfloat_free(&m_lo);
    bigfloat_free(&mid);
    bigfloat_free(&sum);
    bigfloat_free(&s_hi);
    bigfloat_free(&s_lo);
    bigfloat_free(&two);
    return err;
}

/* lo = mid − rad（向下），hi = mid + rad（向上） */
static bigfloat_err_ty ball_bounds(bigfloat_ty *lo, bigfloat_ty *hi,
        const bigfloat_ball_ty *b, const bigfloat_ctx_ty *near) {
    bigfloat_ctx_ty dn;
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = dir_ctx(&dn, near, BIGFLOAT_ROUND_TOWARD_NEG_E);
    if (err == BIGFLOAT_OK_E) {
        err = dir_ctx(&up, near, BIGFLOAT_ROUND_TOWARD_POS_E);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(lo, &b->mid, &b->rad, &dn);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(hi, &b->mid, &b->rad, &up);
    }
    return err;
}

bigfloat_err_ty bigfloat_ball_init(bigfloat_ball_ty *b) {
    if (b == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_err_ty err = bigfloat_init(&b->mid);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_init(&b->rad);
    }
    return err;
}

void bigfloat_ball_free(bigfloat_ball_ty *b) {
    if (b == NULL) {
        return;
    }
    bigfloat_free(&b->mid);
    bigfloat_free(&b->rad);
}

bigfloat_err_ty bigfloat_ball_copy(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *src) {
    if (dst == NULL || src == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ball_ty r;
    bigfloat_ball_init(&r);
    bigfloat_err_ty err = bigfloat_copy(&r.mid, &src->mid);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(&r.rad, &src->rad);
    }
    if (err == BIGFLOAT_OK_E) {
        bigfloat_ball_free(dst);
        *dst = r;
        return BIGFLOAT_OK_E;
    }
    bigfloat_ball_free(&r);
    return err;
}

bigfloat_err_ty bigfloat_ball_set(bigfloat_ball_ty *dst,
        const bigfloat_ty *mid, const bigfloat_ty *rad,
        const bigfloat_ctx_ty *ctx) {
    (void)ctx;
    if (dst == NULL || mid == NULL || rad == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bool ok = bf_finite(mid) && bf_finite(rad) &&
              bigfloat_cmp(rad, &zero) >= 0;
    bigfloat_free(&zero);
    if (!ok) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ball_ty r;
    bigfloat_ball_init(&r);
    bigfloat_err_ty err = bigfloat_copy(&r.mid, mid);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(&r.rad, rad);
    }
    if (err == BIGFLOAT_OK_E) {
        bigfloat_ball_free(dst);
        *dst = r;
        return BIGFLOAT_OK_E;
    }
    bigfloat_ball_free(&r);
    return err;
}

bool bigfloat_ball_contains(const bigfloat_ball_ty *b, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx) {
    if (!ball_valid(b) || x == NULL || !bf_finite(x)) {
        return false;
    }
    bigfloat_ctx_ty dn;
    bigfloat_ctx_ty up;
    if (dir_ctx(&dn, ctx, BIGFLOAT_ROUND_TOWARD_NEG_E) != BIGFLOAT_OK_E ||
            dir_ctx(&up, ctx, BIGFLOAT_ROUND_TOWARD_POS_E) != BIGFLOAT_OK_E) {
        return false;
    }
    bigfloat_ty lo_in;
    bigfloat_ty hi_in;
    bigfloat_init(&lo_in);
    bigfloat_init(&hi_in);
    bool result = false;
    /* 向内舍入：lo_in ≥ 真下界、hi_in ≤ 真上界 → 无假阳性 */
    if (bigfloat_sub(&lo_in, &b->mid, &b->rad, &up) == BIGFLOAT_OK_E &&
            bigfloat_add(&hi_in, &b->mid, &b->rad, &dn) == BIGFLOAT_OK_E) {
        result = bigfloat_cmp(x, &lo_in) >= 0 &&
                 bigfloat_cmp(x, &hi_in) <= 0;
    }
    bigfloat_free(&hi_in);
    bigfloat_free(&lo_in);
    return result;
}

bigfloat_err_ty bigfloat_ball_add(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !ball_valid(a) || !ball_valid(b)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ctx_ty dn;
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = dir_ctx(&dn, ctx, BIGFLOAT_ROUND_TOWARD_NEG_E);
    if (err == BIGFLOAT_OK_E) {
        err = dir_ctx(&up, ctx, BIGFLOAT_ROUND_TOWARD_POS_E);
    }
    bigfloat_ty a_lo;
    bigfloat_ty a_hi;
    bigfloat_ty b_lo;
    bigfloat_ty b_hi;
    bigfloat_ty lo;
    bigfloat_ty hi;
    bigfloat_init(&a_lo);
    bigfloat_init(&a_hi);
    bigfloat_init(&b_lo);
    bigfloat_init(&b_hi);
    bigfloat_init(&lo);
    bigfloat_init(&hi);
    if (err == BIGFLOAT_OK_E) {
        err = ball_bounds(&a_lo, &a_hi, a, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&lo, &a_lo, &b_lo, &dn);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&hi, &a_hi, &b_hi, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = from_interval(dst, &lo, &hi, ctx);
    }
    bigfloat_free(&hi);
    bigfloat_free(&lo);
    bigfloat_free(&b_hi);
    bigfloat_free(&b_lo);
    bigfloat_free(&a_hi);
    bigfloat_free(&a_lo);
    return err;
}

bigfloat_err_ty bigfloat_ball_sub(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !ball_valid(a) || !ball_valid(b)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ctx_ty dn;
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = dir_ctx(&dn, ctx, BIGFLOAT_ROUND_TOWARD_NEG_E);
    if (err == BIGFLOAT_OK_E) {
        err = dir_ctx(&up, ctx, BIGFLOAT_ROUND_TOWARD_POS_E);
    }
    bigfloat_ty a_lo;
    bigfloat_ty a_hi;
    bigfloat_ty b_lo;
    bigfloat_ty b_hi;
    bigfloat_ty lo;
    bigfloat_ty hi;
    bigfloat_init(&a_lo);
    bigfloat_init(&a_hi);
    bigfloat_init(&b_lo);
    bigfloat_init(&b_hi);
    bigfloat_init(&lo);
    bigfloat_init(&hi);
    if (err == BIGFLOAT_OK_E) {
        err = ball_bounds(&a_lo, &a_hi, a, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&lo, &a_lo, &b_hi, &dn);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&hi, &a_hi, &b_lo, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = from_interval(dst, &lo, &hi, ctx);
    }
    bigfloat_free(&hi);
    bigfloat_free(&lo);
    bigfloat_free(&b_hi);
    bigfloat_free(&b_lo);
    bigfloat_free(&a_hi);
    bigfloat_free(&a_lo);
    return err;
}

/* 4 个乘积：按向下取 min、按向上取 max */
static bigfloat_err_ty ball_mul_extreme(bigfloat_ty *lo, bigfloat_ty *hi,
        const bigfloat_ty *a_lo, const bigfloat_ty *a_hi,
        const bigfloat_ty *b_lo, const bigfloat_ty *b_hi,
        const bigfloat_ctx_ty *near, bool quotient) {
    bigfloat_ctx_ty dn;
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = dir_ctx(&dn, near, BIGFLOAT_ROUND_TOWARD_NEG_E);
    if (err == BIGFLOAT_OK_E) {
        err = dir_ctx(&up, near, BIGFLOAT_ROUND_TOWARD_POS_E);
    }
    const bigfloat_ty *av[2] = {a_lo, a_hi};
    const bigfloat_ty *bv[2] = {b_lo, b_hi};
    bool first = true;
    for (int i = 0; i < 2 && err == BIGFLOAT_OK_E; i++) {
        for (int j = 0; j < 2 && err == BIGFLOAT_OK_E; j++) {
            bigfloat_ty p_dn;
            bigfloat_ty p_up;
            bigfloat_init(&p_dn);
            bigfloat_init(&p_up);
            if (quotient) {
                err = bigfloat_div(&p_dn, av[i], bv[j], &dn);
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_div(&p_up, av[i], bv[j], &up);
                }
            } else {
                err = bigfloat_mul(&p_dn, av[i], bv[j], &dn);
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_mul(&p_up, av[i], bv[j], &up);
                }
            }
            if (err == BIGFLOAT_OK_E && (first || bigfloat_cmp(&p_dn, lo) < 0)) {
                err = bigfloat_copy(lo, &p_dn);
            }
            if (err == BIGFLOAT_OK_E && (first || bigfloat_cmp(&p_up, hi) > 0)) {
                err = bigfloat_copy(hi, &p_up);
            }
            first = false;
            bigfloat_free(&p_up);
            bigfloat_free(&p_dn);
        }
    }
    return err;
}

bigfloat_err_ty bigfloat_ball_mul(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !ball_valid(a) || !ball_valid(b)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ty a_lo;
    bigfloat_ty a_hi;
    bigfloat_ty b_lo;
    bigfloat_ty b_hi;
    bigfloat_ty lo;
    bigfloat_ty hi;
    bigfloat_init(&a_lo);
    bigfloat_init(&a_hi);
    bigfloat_init(&b_lo);
    bigfloat_init(&b_hi);
    bigfloat_init(&lo);
    bigfloat_init(&hi);
    bigfloat_err_ty err = ball_bounds(&a_lo, &a_hi, a, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = ball_mul_extreme(&lo, &hi, &a_lo, &a_hi, &b_lo, &b_hi, ctx,
                               false);
    }
    if (err == BIGFLOAT_OK_E) {
        err = from_interval(dst, &lo, &hi, ctx);
    }
    bigfloat_free(&hi);
    bigfloat_free(&lo);
    bigfloat_free(&b_hi);
    bigfloat_free(&b_lo);
    bigfloat_free(&a_hi);
    bigfloat_free(&a_lo);
    return err;
}

bigfloat_err_ty bigfloat_ball_div(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *a, const bigfloat_ball_ty *b,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !ball_valid(a) || !ball_valid(b)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ty a_lo;
    bigfloat_ty a_hi;
    bigfloat_ty b_lo;
    bigfloat_ty b_hi;
    bigfloat_ty lo;
    bigfloat_ty hi;
    bigfloat_ty zero;
    bigfloat_init(&a_lo);
    bigfloat_init(&a_hi);
    bigfloat_init(&b_lo);
    bigfloat_init(&b_hi);
    bigfloat_init(&lo);
    bigfloat_init(&hi);
    bigfloat_init(&zero);

    bigfloat_err_ty err = ball_bounds(&a_lo, &a_hi, a, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    /* 分母区间不得含 0 */
    if (err == BIGFLOAT_OK_E &&
            !(bigfloat_cmp(&b_lo, &zero) > 0 ||
              bigfloat_cmp(&b_hi, &zero) < 0)) {
        err = BIGFLOAT_ERR_INVALID_E;
    }
    if (err == BIGFLOAT_OK_E) {
        err = ball_mul_extreme(&lo, &hi, &a_lo, &a_hi, &b_lo, &b_hi, ctx,
                               true);
    }
    if (err == BIGFLOAT_OK_E) {
        err = from_interval(dst, &lo, &hi, ctx);
    }
    bigfloat_free(&zero);
    bigfloat_free(&hi);
    bigfloat_free(&lo);
    bigfloat_free(&b_hi);
    bigfloat_free(&b_lo);
    bigfloat_free(&a_hi);
    bigfloat_free(&a_lo);
    return err;
}

/* ================================================================== */
/* bigdecimal 球式（镜像 bigfloat 版；十进制定向舍入）                  */
/* ================================================================== */

static bigdecimal_err_ty dec_dir_ctx(bigdecimal_ctx_ty *out,
        const bigdecimal_ctx_ty *base, bigdecimal_round_ty round) {
    return bigdecimal_ctx_make(out, base->mant_digits, base->exp_digits,
                               round);
}

static bigdecimal_err_ty dec_abs(bigdecimal_ty *out, const bigdecimal_ty *x) {
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = bigdecimal_copy(out, x);
    if (err == BIGDECIMAL_OK_E && bigdecimal_cmp(x, &zero) < 0) {
        err = bigdecimal_neg(out);
    }
    bigdecimal_free(&zero);
    return err;
}

/* out = ulp(x) = 10^{e − p}（x 为正常值）；x 为 0/特殊值时为 0 */
static bigdecimal_err_ty dec_ulp(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    bigint_dec_ty mant;
    bigint_dec_init(&mant);
    int64_t e = 0;
    bigdecimal_flag_ty fl = BIGDECIMAL_POS_ZERO_E;
    bigdecimal_err_ty err = bigdecimal_decompose(x, &mant, &e, &fl);
    if (err == BIGDECIMAL_OK_E) {
        if (fl == BIGDECIMAL_POS_E || fl == BIGDECIMAL_NEG_E) {
            bigint_dec_ty one;
            bigint_dec_init(&one);
            bigint_dec_from_u64(&one, 1U);
            err = bigdecimal_compose(out, &one, e - (int64_t)ctx->mant_digits,
                                     BIGDECIMAL_POS_E, ctx);
            bigint_dec_free(&one);
        } else {
            bigdecimal_ty zero;
            bigdecimal_init(&zero);
            err = bigdecimal_copy(out, &zero);
            bigdecimal_free(&zero);
        }
    }
    bigint_dec_free(&mant);
    return err;
}

static bool dec_finite(const bigdecimal_ty *x) {
    return !bigdecimal_is_nan(x) && !bigdecimal_is_inf(x);
}

static bool dec_ball_valid(const bigdecimal_ball_ty *b) {
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bool ok = b != NULL && dec_finite(&b->mid) && dec_finite(&b->rad) &&
              bigdecimal_cmp(&b->rad, &zero) >= 0;
    bigdecimal_free(&zero);
    return ok;
}

static bigdecimal_err_ty dec_from_interval(bigdecimal_ball_ty *dst,
        const bigdecimal_ty *lo, const bigdecimal_ty *hi,
        const bigdecimal_ctx_ty *near) {
    bigdecimal_ctx_ty dn;
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = dec_dir_ctx(&dn, near, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_dir_ctx(&up, near, BIGDECIMAL_ROUND_TOWARD_POS_E);
    }
    bigdecimal_ty two;
    bigdecimal_ty s_lo;
    bigdecimal_ty s_hi;
    bigdecimal_ty sum;
    bigdecimal_ty mid;
    bigdecimal_ty m_lo;
    bigdecimal_ty m_hi;
    bigdecimal_ty unc;
    bigdecimal_ty d1;
    bigdecimal_ty d2;
    bigdecimal_ty m1;
    bigdecimal_ty m2;
    bigdecimal_ty rad;
    bigdecimal_ty u;
    bigdecimal_init(&two);
    bigdecimal_init(&s_lo);
    bigdecimal_init(&s_hi);
    bigdecimal_init(&sum);
    bigdecimal_init(&mid);
    bigdecimal_init(&m_lo);
    bigdecimal_init(&m_hi);
    bigdecimal_init(&unc);
    bigdecimal_init(&d1);
    bigdecimal_init(&d2);
    bigdecimal_init(&m1);
    bigdecimal_init(&m2);
    bigdecimal_init(&rad);
    bigdecimal_init(&u);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&two, "2", near, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&s_lo, lo, hi, &dn);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&s_hi, lo, hi, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&sum, lo, hi, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_div(&mid, &sum, &two, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_div(&m_lo, &s_lo, &two, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_div(&m_hi, &s_hi, &two, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(&unc, &m_hi, &m_lo, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(&d1, &mid, lo, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(&d2, hi, &mid, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_abs(&m1, &d1);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_abs(&m2, &d2);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = (bigdecimal_cmp(&m1, &m2) >= 0) ? bigdecimal_copy(&rad, &m1)
                                              : bigdecimal_copy(&rad, &m2);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ulp(&u, &rad, near);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&rad, &rad, &u, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&rad, &rad, &unc, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&dst->mid, &mid);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&dst->rad, &rad);
    }
    bigdecimal_free(&u);
    bigdecimal_free(&rad);
    bigdecimal_free(&m2);
    bigdecimal_free(&m1);
    bigdecimal_free(&d2);
    bigdecimal_free(&d1);
    bigdecimal_free(&unc);
    bigdecimal_free(&m_hi);
    bigdecimal_free(&m_lo);
    bigdecimal_free(&mid);
    bigdecimal_free(&sum);
    bigdecimal_free(&s_hi);
    bigdecimal_free(&s_lo);
    bigdecimal_free(&two);
    return err;
}

static bigdecimal_err_ty dec_ball_bounds(bigdecimal_ty *lo,
        bigdecimal_ty *hi, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *near) {
    bigdecimal_ctx_ty dn;
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = dec_dir_ctx(&dn, near, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_dir_ctx(&up, near, BIGDECIMAL_ROUND_TOWARD_POS_E);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(lo, &b->mid, &b->rad, &dn);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(hi, &b->mid, &b->rad, &up);
    }
    return err;
}

bigdecimal_err_ty bigdecimal_ball_init(bigdecimal_ball_ty *b) {
    if (b == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_err_ty err = bigdecimal_init(&b->mid);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_init(&b->rad);
    }
    return err;
}

void bigdecimal_ball_free(bigdecimal_ball_ty *b) {
    if (b == NULL) {
        return;
    }
    bigdecimal_free(&b->mid);
    bigdecimal_free(&b->rad);
}

bigdecimal_err_ty bigdecimal_ball_copy(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *src) {
    if (dst == NULL || src == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_ty r;
    bigdecimal_ball_init(&r);
    bigdecimal_err_ty err = bigdecimal_copy(&r.mid, &src->mid);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&r.rad, &src->rad);
    }
    if (err == BIGDECIMAL_OK_E) {
        bigdecimal_ball_free(dst);
        *dst = r;
        return BIGDECIMAL_OK_E;
    }
    bigdecimal_ball_free(&r);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_set(bigdecimal_ball_ty *dst,
        const bigdecimal_ty *mid, const bigdecimal_ty *rad,
        const bigdecimal_ctx_ty *ctx) {
    (void)ctx;
    if (dst == NULL || mid == NULL || rad == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bool ok = dec_finite(mid) && dec_finite(rad) &&
              bigdecimal_cmp(rad, &zero) >= 0;
    bigdecimal_free(&zero);
    if (!ok) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_ty r;
    bigdecimal_ball_init(&r);
    bigdecimal_err_ty err = bigdecimal_copy(&r.mid, mid);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&r.rad, rad);
    }
    if (err == BIGDECIMAL_OK_E) {
        bigdecimal_ball_free(dst);
        *dst = r;
        return BIGDECIMAL_OK_E;
    }
    bigdecimal_ball_free(&r);
    return err;
}

bool bigdecimal_ball_contains(const bigdecimal_ball_ty *b,
        const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx) {
    if (!dec_ball_valid(b) || x == NULL || !dec_finite(x)) {
        return false;
    }
    bigdecimal_ctx_ty dn;
    bigdecimal_ctx_ty up;
    if (dec_dir_ctx(&dn, ctx, BIGDECIMAL_ROUND_TOWARD_NEG_E) !=
            BIGDECIMAL_OK_E ||
            dec_dir_ctx(&up, ctx, BIGDECIMAL_ROUND_TOWARD_POS_E) !=
            BIGDECIMAL_OK_E) {
        return false;
    }
    bigdecimal_ty lo_in;
    bigdecimal_ty hi_in;
    bigdecimal_init(&lo_in);
    bigdecimal_init(&hi_in);
    bool result = false;
    if (bigdecimal_sub(&lo_in, &b->mid, &b->rad, &up) == BIGDECIMAL_OK_E &&
            bigdecimal_add(&hi_in, &b->mid, &b->rad, &dn) ==
            BIGDECIMAL_OK_E) {
        result = bigdecimal_cmp(x, &lo_in) >= 0 &&
                 bigdecimal_cmp(x, &hi_in) <= 0;
    }
    bigdecimal_free(&hi_in);
    bigdecimal_free(&lo_in);
    return result;
}

bigdecimal_err_ty bigdecimal_ball_add(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !dec_ball_valid(a) || !dec_ball_valid(b)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ctx_ty dn;
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = dec_dir_ctx(&dn, ctx, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_dir_ctx(&up, ctx, BIGDECIMAL_ROUND_TOWARD_POS_E);
    }
    bigdecimal_ty a_lo;
    bigdecimal_ty a_hi;
    bigdecimal_ty b_lo;
    bigdecimal_ty b_hi;
    bigdecimal_ty lo;
    bigdecimal_ty hi;
    bigdecimal_init(&a_lo);
    bigdecimal_init(&a_hi);
    bigdecimal_init(&b_lo);
    bigdecimal_init(&b_hi);
    bigdecimal_init(&lo);
    bigdecimal_init(&hi);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_bounds(&a_lo, &a_hi, a, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&lo, &a_lo, &b_lo, &dn);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&hi, &a_hi, &b_hi, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_from_interval(dst, &lo, &hi, ctx);
    }
    bigdecimal_free(&hi);
    bigdecimal_free(&lo);
    bigdecimal_free(&b_hi);
    bigdecimal_free(&b_lo);
    bigdecimal_free(&a_hi);
    bigdecimal_free(&a_lo);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_sub(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !dec_ball_valid(a) || !dec_ball_valid(b)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ctx_ty dn;
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = dec_dir_ctx(&dn, ctx, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_dir_ctx(&up, ctx, BIGDECIMAL_ROUND_TOWARD_POS_E);
    }
    bigdecimal_ty a_lo;
    bigdecimal_ty a_hi;
    bigdecimal_ty b_lo;
    bigdecimal_ty b_hi;
    bigdecimal_ty lo;
    bigdecimal_ty hi;
    bigdecimal_init(&a_lo);
    bigdecimal_init(&a_hi);
    bigdecimal_init(&b_lo);
    bigdecimal_init(&b_hi);
    bigdecimal_init(&lo);
    bigdecimal_init(&hi);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_bounds(&a_lo, &a_hi, a, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(&lo, &a_lo, &b_hi, &dn);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(&hi, &a_hi, &b_lo, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_from_interval(dst, &lo, &hi, ctx);
    }
    bigdecimal_free(&hi);
    bigdecimal_free(&lo);
    bigdecimal_free(&b_hi);
    bigdecimal_free(&b_lo);
    bigdecimal_free(&a_hi);
    bigdecimal_free(&a_lo);
    return err;
}

static bigdecimal_err_ty dec_ball_mul_extreme(bigdecimal_ty *lo,
        bigdecimal_ty *hi, const bigdecimal_ty *a_lo, const bigdecimal_ty *a_hi,
        const bigdecimal_ty *b_lo, const bigdecimal_ty *b_hi,
        const bigdecimal_ctx_ty *near, bool quotient) {
    bigdecimal_ctx_ty dn;
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = dec_dir_ctx(&dn, near, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_dir_ctx(&up, near, BIGDECIMAL_ROUND_TOWARD_POS_E);
    }
    const bigdecimal_ty *av[2] = {a_lo, a_hi};
    const bigdecimal_ty *bv[2] = {b_lo, b_hi};
    bool first = true;
    for (int i = 0; i < 2 && err == BIGDECIMAL_OK_E; i++) {
        for (int j = 0; j < 2 && err == BIGDECIMAL_OK_E; j++) {
            bigdecimal_ty p_dn;
            bigdecimal_ty p_up;
            bigdecimal_init(&p_dn);
            bigdecimal_init(&p_up);
            if (quotient) {
                err = bigdecimal_div(&p_dn, av[i], bv[j], &dn);
                if (err == BIGDECIMAL_OK_E) {
                    err = bigdecimal_div(&p_up, av[i], bv[j], &up);
                }
            } else {
                err = bigdecimal_mul(&p_dn, av[i], bv[j], &dn);
                if (err == BIGDECIMAL_OK_E) {
                    err = bigdecimal_mul(&p_up, av[i], bv[j], &up);
                }
            }
            if (err == BIGDECIMAL_OK_E &&
                    (first || bigdecimal_cmp(&p_dn, lo) < 0)) {
                err = bigdecimal_copy(lo, &p_dn);
            }
            if (err == BIGDECIMAL_OK_E &&
                    (first || bigdecimal_cmp(&p_up, hi) > 0)) {
                err = bigdecimal_copy(hi, &p_up);
            }
            first = false;
            bigdecimal_free(&p_up);
            bigdecimal_free(&p_dn);
        }
    }
    return err;
}

bigdecimal_err_ty bigdecimal_ball_mul(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !dec_ball_valid(a) || !dec_ball_valid(b)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ty a_lo;
    bigdecimal_ty a_hi;
    bigdecimal_ty b_lo;
    bigdecimal_ty b_hi;
    bigdecimal_ty lo;
    bigdecimal_ty hi;
    bigdecimal_init(&a_lo);
    bigdecimal_init(&a_hi);
    bigdecimal_init(&b_lo);
    bigdecimal_init(&b_hi);
    bigdecimal_init(&lo);
    bigdecimal_init(&hi);
    bigdecimal_err_ty err = dec_ball_bounds(&a_lo, &a_hi, a, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_mul_extreme(&lo, &hi, &a_lo, &a_hi, &b_lo, &b_hi, ctx,
                                   false);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_from_interval(dst, &lo, &hi, ctx);
    }
    bigdecimal_free(&hi);
    bigdecimal_free(&lo);
    bigdecimal_free(&b_hi);
    bigdecimal_free(&b_lo);
    bigdecimal_free(&a_hi);
    bigdecimal_free(&a_lo);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_div(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *a, const bigdecimal_ball_ty *b,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || a == NULL || b == NULL || ctx == NULL ||
            !dec_ball_valid(a) || !dec_ball_valid(b)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ty a_lo;
    bigdecimal_ty a_hi;
    bigdecimal_ty b_lo;
    bigdecimal_ty b_hi;
    bigdecimal_ty lo;
    bigdecimal_ty hi;
    bigdecimal_ty zero;
    bigdecimal_init(&a_lo);
    bigdecimal_init(&a_hi);
    bigdecimal_init(&b_lo);
    bigdecimal_init(&b_hi);
    bigdecimal_init(&lo);
    bigdecimal_init(&hi);
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = dec_ball_bounds(&a_lo, &a_hi, a, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_bounds(&b_lo, &b_hi, b, ctx);
    }
    if (err == BIGDECIMAL_OK_E &&
            !(bigdecimal_cmp(&b_lo, &zero) > 0 ||
              bigdecimal_cmp(&b_hi, &zero) < 0)) {
        err = BIGDECIMAL_ERR_INVALID_E;
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_ball_mul_extreme(&lo, &hi, &a_lo, &a_hi, &b_lo, &b_hi, ctx,
                                   true);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = dec_from_interval(dst, &lo, &hi, ctx);
    }
    bigdecimal_free(&zero);
    bigdecimal_free(&hi);
    bigdecimal_free(&lo);
    bigdecimal_free(&b_hi);
    bigdecimal_free(&b_lo);
    bigdecimal_free(&a_hi);
    bigdecimal_free(&a_lo);
    return err;
}
