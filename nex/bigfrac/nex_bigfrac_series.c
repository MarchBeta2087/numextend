#include "nex/bigfrac/nex_bigfrac.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "nex/nex_alloc.h"

/*
 * 截断幂级数 / Laurent 级数（系数为 bigfrac，精确；设计文档 §15，TODO §C2）。
 *
 * 内部约定：
 *   - 结果先算入局部 r，最后 swap 到 dst：允许别名且 OOM 时 dst 不变；
 *   - coeffs[0..len-1] 已初始化，[len..cap-1] 未初始化（push 时逐个 init）；
 *   - 每次运算后去尾零（series_trim）。
 */

static void series_swap(bigfrac_series_ty *a, bigfrac_series_ty *b) {
    bigfrac_series_ty t = *a;
    *a = *b;
    *b = t;
}

static bigfrac_err_ty series_push_zero(bigfrac_series_ty *s) {
    if (s->len == s->cap) {
        size_t new_cap = (s->cap == 0U) ? 4U : s->cap * 2U;
        bigfrac_ty *arr = (bigfrac_ty *)nex_realloc(s->coeffs,
                                                    new_cap * sizeof(bigfrac_ty));
        if (arr == NULL) {
            return BIGFRAC_ERR_OOM_E;
        }
        s->coeffs = arr;
        s->cap = new_cap;
    }
    bigfrac_err_ty err = bigfrac_init(&s->coeffs[s->len]);
    if (err != BIGFRAC_OK_E) {
        return err;
    }
    s->len++;
    return BIGFRAC_OK_E;
}

static void series_trim(bigfrac_series_ty *s) {
    while (s->len > 0U) {
        const bigint_bin_ty *num = bigfrac_num(&s->coeffs[s->len - 1U]);
        if (!bigint_bin_is_zero(num)) {
            break;
        }
        bigfrac_free(&s->coeffs[s->len - 1U]);
        s->len--;
    }
    if (s->len == 0U) {
        s->val = 0;
    }
}

static bool series_is_zero(const bigfrac_series_ty *s) {
    return s->len == 0U;
}

static const bigfrac_ty *series_coeff_at(const bigfrac_series_ty *s, int64_t e) {
    if (s->len == 0U || e < s->val) {
        return NULL;
    }
    int64_t off = e - s->val;
    if (off < 0 || (uint64_t)off >= (uint64_t)s->len) {
        return NULL;
    }
    return &s->coeffs[(size_t)off];
}

bigfrac_err_ty bigfrac_series_init(bigfrac_series_ty *s) {
    if (s == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    s->val = 0;
    s->coeffs = NULL;
    s->len = 0U;
    s->cap = 0U;
    return BIGFRAC_OK_E;
}

void bigfrac_series_free(bigfrac_series_ty *s) {
    if (s == NULL) {
        return;
    }
    for (size_t i = 0U; i < s->len; i++) {
        bigfrac_free(&s->coeffs[i]);
    }
    free(s->coeffs);
    s->coeffs = NULL;
    s->len = 0U;
    s->cap = 0U;
    s->val = 0;
}

bigfrac_err_ty bigfrac_series_from_array(bigfrac_series_ty *dst, int64_t val,
        const bigfrac_ty *coeffs, size_t len) {
    if (dst == NULL || (len > 0U && coeffs == NULL)) {
        return BIGFRAC_ERR_INVALID_E;
    }
    bigfrac_series_ty r;
    bigfrac_err_ty err = bigfrac_series_init(&r);
    if (err != BIGFRAC_OK_E) {
        return err;
    }
    for (size_t i = 0U; i < len && err == BIGFRAC_OK_E; i++) {
        err = series_push_zero(&r);
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_copy(&r.coeffs[i], &coeffs[i]);
        }
    }
    if (err == BIGFRAC_OK_E) {
        r.val = val;
        series_trim(&r);
        series_swap(&r, dst);
    }
    bigfrac_series_free(&r);
    return err;
}

bigfrac_err_ty bigfrac_series_copy(bigfrac_series_ty *dst,
        const bigfrac_series_ty *src) {
    if (dst == NULL || src == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    return bigfrac_series_from_array(dst, src->val, src->coeffs, src->len);
}

static bigfrac_err_ty series_truncate(bigfrac_series_ty *dst,
        const bigfrac_series_ty *src, size_t order) {
    size_t n = (src->len < order) ? src->len : order;
    return bigfrac_series_from_array(dst, src->val, src->coeffs, n);
}

static bigfrac_err_ty series_addsub(bigfrac_series_ty *dst,
        const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs,
        size_t order, bool subtract) {
    if (dst == NULL || lhs == NULL || rhs == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (series_is_zero(lhs)) {
        if (!subtract) {
            return series_truncate(dst, rhs, order);
        }
        /* 0 − rhs：取负后截断 */
        bigfrac_series_ty neg;
        bigfrac_err_ty e = bigfrac_series_copy(&neg, rhs);
        if (e != BIGFRAC_OK_E) {
            return e;
        }
        for (size_t i = 0U; i < neg.len; i++) {
            bigfrac_neg(&neg.coeffs[i]);
        }
        e = series_truncate(dst, &neg, order);
        bigfrac_series_free(&neg);
        return e;
    }
    if (series_is_zero(rhs)) {
        return series_truncate(dst, lhs, order);
    }

    bigfrac_series_ty r;
    bigfrac_err_ty err = bigfrac_series_init(&r);
    if (err != BIGFRAC_OK_E) {
        return err;
    }
    r.val = (lhs->val < rhs->val) ? lhs->val : rhs->val;

    bigfrac_ty zero;
    bigfrac_init(&zero);
    size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    for (size_t i = 0U; i < order && i < max_len && err == BIGFRAC_OK_E; i++) {
        int64_t e = r.val + (int64_t)i;
        const bigfrac_ty *a = series_coeff_at(lhs, e);
        const bigfrac_ty *b = series_coeff_at(rhs, e);
        if (a == NULL && b == NULL) {
            break;
        }
        err = series_push_zero(&r);
        if (err == BIGFRAC_OK_E) {
            const bigfrac_ty *aa = (a != NULL) ? a : &zero;
            const bigfrac_ty *bb = (b != NULL) ? b : &zero;
            err = subtract ? bigfrac_sub(&r.coeffs[i], aa, bb)
                           : bigfrac_add(&r.coeffs[i], aa, bb);
        }
    }
    if (err == BIGFRAC_OK_E) {
        series_trim(&r);
        series_swap(&r, dst);
    }
    bigfrac_free(&zero);
    bigfrac_series_free(&r);
    return err;
}

bigfrac_err_ty bigfrac_series_add(bigfrac_series_ty *dst,
        const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs,
        size_t order) {
    return series_addsub(dst, lhs, rhs, order, false);
}

bigfrac_err_ty bigfrac_series_sub(bigfrac_series_ty *dst,
        const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs,
        size_t order) {
    return series_addsub(dst, lhs, rhs, order, true);
}

bigfrac_err_ty bigfrac_series_mul(bigfrac_series_ty *dst,
        const bigfrac_series_ty *lhs, const bigfrac_series_ty *rhs,
        size_t order) {
    if (dst == NULL || lhs == NULL || rhs == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (series_is_zero(lhs) || series_is_zero(rhs) || order == 0U) {
        return bigfrac_series_from_array(dst, 0, NULL, 0U);
    }

    bigfrac_series_ty r;
    bigfrac_err_ty err = bigfrac_series_init(&r);
    if (err != BIGFRAC_OK_E) {
        return err;
    }
    r.val = lhs->val + rhs->val;

    size_t full = lhs->len + rhs->len - 1U;   /* 非零系数最大个数 */
    size_t n = (full < order) ? full : order;
    for (size_t i = 0U; i < n && err == BIGFRAC_OK_E; i++) {
        bigfrac_ty acc;
        bigfrac_init(&acc);
        size_t k0 = (i + 1U > rhs->len) ? (i + 1U - rhs->len) : 0U;
        for (size_t k = k0; k <= i && k < lhs->len && err == BIGFRAC_OK_E; k++) {
            size_t j = i - k;
            if (j >= rhs->len) {
                continue;
            }
            bigfrac_ty t;
            bigfrac_init(&t);
            err = bigfrac_mul(&t, &lhs->coeffs[k], &rhs->coeffs[j]);
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_add(&acc, &acc, &t);
            }
            bigfrac_free(&t);
        }
        if (err == BIGFRAC_OK_E) {
            err = series_push_zero(&r);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_copy(&r.coeffs[i], &acc);
        }
        bigfrac_free(&acc);
    }
    if (err == BIGFRAC_OK_E) {
        series_trim(&r);
        series_swap(&r, dst);
    }
    bigfrac_series_free(&r);
    return err;
}

bigfrac_err_ty bigfrac_series_inv(bigfrac_series_ty *dst,
        const bigfrac_series_ty *src, size_t order) {
    if (dst == NULL || src == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (series_is_zero(src) ||
            bigint_bin_is_zero(bigfrac_num(&src->coeffs[0]))) {
        return BIGFRAC_ERR_INVALID_E;   /* 含除零 */
    }
    if (order == 0U) {
        return bigfrac_series_from_array(dst, 0, NULL, 0U);
    }

    bigfrac_series_ty r;
    bigfrac_err_ty err = bigfrac_series_init(&r);
    if (err != BIGFRAC_OK_E) {
        return err;
    }
    r.val = -src->val;

    bigfrac_ty inv0;
    bigfrac_init(&inv0);
    err = bigfrac_copy(&inv0, &src->coeffs[0]);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_inv(&inv0);   /* 1/a0（就地） */
    }

    for (size_t i = 0U; i < order && err == BIGFRAC_OK_E; i++) {
        bigfrac_ty bn;
        bigfrac_init(&bn);
        if (i == 0U) {
            err = bigfrac_copy(&bn, &inv0);
        } else {
            bigfrac_ty acc;
            bigfrac_init(&acc);
            for (size_t k = 1U; k <= i && k < src->len && err == BIGFRAC_OK_E;
                    k++) {
                bigfrac_ty t;
                bigfrac_init(&t);
                err = bigfrac_mul(&t, &src->coeffs[k], &r.coeffs[i - k]);
                if (err == BIGFRAC_OK_E) {
                    err = bigfrac_add(&acc, &acc, &t);
                }
                bigfrac_free(&t);
            }
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_mul(&bn, &inv0, &acc);
            }
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_neg(&bn);
            }
            bigfrac_free(&acc);
        }
        if (err == BIGFRAC_OK_E) {
            err = series_push_zero(&r);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_copy(&r.coeffs[i], &bn);
        }
        bigfrac_free(&bn);
    }

    if (err == BIGFRAC_OK_E) {
        series_trim(&r);
        series_swap(&r, dst);
    }
    bigfrac_free(&inv0);
    bigfrac_series_free(&r);
    return err;
}

/* out = base^e（e ≥ 0），精确 */
static bigfrac_err_ty frac_pow_u(bigfrac_ty *out, const bigfrac_ty *base,
        uint64_t e) {
    bigfrac_ty acc;
    bigfrac_ty b;
    bigfrac_init(&acc);
    bigfrac_init(&b);
    bigfrac_err_ty err = bigfrac_from_str(&acc, "1", NULL);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(&b, base);
    }
    while (e > 0U && err == BIGFRAC_OK_E) {
        if ((e & 1U) != 0U) {
            err = bigfrac_mul(&acc, &acc, &b);
        }
        e >>= 1U;
        if (e > 0U && err == BIGFRAC_OK_E) {
            err = bigfrac_mul(&b, &b, &b);
        }
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(out, &acc);
    }
    bigfrac_free(&b);
    bigfrac_free(&acc);
    return err;
}

bigfrac_err_ty bigfrac_series_eval(bigfrac_ty *out,
        const bigfrac_series_ty *s, const bigfrac_ty *x) {
    if (out == NULL || s == NULL || x == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (series_is_zero(s)) {
        bigfrac_ty z;
        bigfrac_init(&z);
        bigfrac_err_ty e = bigfrac_copy(out, &z);
        bigfrac_free(&z);
        return e;
    }

    bigfrac_ty p;
    bigfrac_init(&p);
    bigfrac_err_ty err = bigfrac_poly_eval(&p, s->coeffs, s->len - 1U, x);
    if (err == BIGFRAC_OK_E && s->val != 0) {
        uint64_t e = (s->val < 0) ? (uint64_t)(-s->val) : (uint64_t)s->val;
        bigfrac_ty xp;
        bigfrac_init(&xp);
        err = frac_pow_u(&xp, x, e);
        if (err == BIGFRAC_OK_E && s->val > 0) {
            err = bigfrac_mul(&p, &p, &xp);
        } else if (err == BIGFRAC_OK_E) {
            if (bigint_bin_is_zero(bigfrac_num(x))) {
                err = BIGFRAC_ERR_INVALID_E;   /* x = 0 且负幂 */
            } else {
                err = bigfrac_div(&p, &p, &xp);
            }
        }
        bigfrac_free(&xp);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(out, &p);
    }
    bigfrac_free(&p);
    return err;
}
