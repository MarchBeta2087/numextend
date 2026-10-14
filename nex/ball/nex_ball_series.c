#include "nex/ball/nex_ball.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "nex/nex_alloc.h"

/*
 * 球系数截断级数（§I，v1.5.0）：系数为 bigfloat_ball_ty，每个系数携带误差
 * 半径，四则/复合/求值全程由球运算传播半径，得到**严谨的数值级数**。
 *
 *   S(z) = Σ_{i=0}^{len-1} coeffs[i] · z^{val+i}
 *
 * 规范化：len == 0 表示零级数（val 归 0）；否则末项非零球（mid 与 rad 均 0
 * 视为零球）。约定同 bigfrac_series：结果先算入局部量再 swap，OOM 时输出不变。
 */

static void bseries_swap(bigfloat_ball_series_ty *a,
        bigfloat_ball_series_ty *b) {
    bigfloat_ball_series_ty t = *a;
    *a = *b;
    *b = t;
}

static bool ball_is_zero(const bigfloat_ball_ty *b) {
    return bigfloat_is_zero(&b->mid) && bigfloat_is_zero(&b->rad);
}

static bigfloat_err_ty bseries_push_zero(bigfloat_ball_series_ty *s) {
    if (s->len == s->cap) {
        size_t nc = (s->cap == 0U) ? 4U : s->cap * 2U;
        bigfloat_ball_ty *arr = (bigfloat_ball_ty *)nex_realloc(s->coeffs,
                nc * sizeof(bigfloat_ball_ty));
        if (arr == NULL) {
            return BIGFLOAT_ERR_OOM_E;
        }
        s->coeffs = arr;
        s->cap = nc;
    }
    bigfloat_err_ty err = bigfloat_ball_init(&s->coeffs[s->len]);
    if (err != BIGFLOAT_OK_E) {
        return err;
    }
    s->len++;
    return BIGFLOAT_OK_E;
}

static void bseries_trim(bigfloat_ball_series_ty *s) {
    while (s->len > 0U && ball_is_zero(&s->coeffs[s->len - 1U])) {
        bigfloat_ball_free(&s->coeffs[s->len - 1U]);
        s->len--;
    }
    if (s->len == 0U) {
        s->val = 0;
    }
}

static void bseries_truncate_len(bigfloat_ball_series_ty *s, size_t n) {
    while (s->len > n) {
        bigfloat_ball_free(&s->coeffs[s->len - 1U]);
        s->len--;
    }
    if (s->len == 0U) {
        s->val = 0;
    }
}

bigfloat_err_ty bigfloat_ball_series_init(bigfloat_ball_series_ty *s) {
    if (s == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    s->val = 0;
    s->coeffs = NULL;
    s->len = 0U;
    s->cap = 0U;
    return BIGFLOAT_OK_E;
}

void bigfloat_ball_series_free(bigfloat_ball_series_ty *s) {
    if (s == NULL) {
        return;
    }
    for (size_t i = 0U; i < s->len; i++) {
        bigfloat_ball_free(&s->coeffs[i]);
    }
    free(s->coeffs);
    s->coeffs = NULL;
    s->len = 0U;
    s->cap = 0U;
    s->val = 0;
}

bigfloat_err_ty bigfloat_ball_series_from_array(bigfloat_ball_series_ty *dst,
        int64_t val, const bigfloat_ball_ty *coeffs, size_t len) {
    if (dst == NULL || (len > 0U && coeffs == NULL)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ball_series_ty r;
    bigfloat_ball_series_init(&r);
    bigfloat_err_ty err = BIGFLOAT_OK_E;
    for (size_t i = 0U; i < len && err == BIGFLOAT_OK_E; i++) {
        err = bseries_push_zero(&r);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_copy(&r.coeffs[i], &coeffs[i]);
        }
    }
    if (err == BIGFLOAT_OK_E) {
        r.val = val;
        bseries_trim(&r);
        bseries_swap(&r, dst);
    }
    bigfloat_ball_series_free(&r);
    return err;
}

bigfloat_err_ty bigfloat_ball_series_copy(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *src) {
    if (dst == NULL || src == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    return bigfloat_ball_series_from_array(dst, src->val, src->coeffs,
                                           src->len);
}

static bigfloat_err_ty bseries_truncate(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *src, size_t order) {
    size_t n = (src->len < order) ? src->len : order;
    return bigfloat_ball_series_from_array(dst, src->val, src->coeffs, n);
}

static const bigfloat_ball_ty *bseries_at(
        const bigfloat_ball_series_ty *s, int64_t e) {
    if (s->len == 0U || e < s->val) {
        return NULL;
    }
    int64_t off = e - s->val;
    if (off < 0 || (uint64_t)off >= (uint64_t)s->len) {
        return NULL;
    }
    return &s->coeffs[(size_t)off];
}

static bigfloat_err_ty bseries_addsub(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *lhs,
        const bigfloat_ball_series_ty *rhs, size_t order, bool subtract,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || lhs == NULL || rhs == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (lhs->len == 0U) {
        return bseries_truncate(dst, rhs, order);
    }
    if (rhs->len == 0U) {
        return bseries_truncate(dst, lhs, order);
    }

    bigfloat_ball_series_ty r;
    bigfloat_ball_series_init(&r);
    r.val = (lhs->val < rhs->val) ? lhs->val : rhs->val;
    bigfloat_ball_ty zero;
    bigfloat_ball_init(&zero);
    bigfloat_err_ty err = BIGFLOAT_OK_E;
    size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    for (size_t i = 0U; i < order && i < max_len && err == BIGFLOAT_OK_E; i++) {
        int64_t e = r.val + (int64_t)i;
        const bigfloat_ball_ty *a = bseries_at(lhs, e);
        const bigfloat_ball_ty *b = bseries_at(rhs, e);
        if (a == NULL && b == NULL) {
            break;
        }
        err = bseries_push_zero(&r);
        if (err == BIGFLOAT_OK_E) {
            const bigfloat_ball_ty *aa = (a != NULL) ? a : &zero;
            const bigfloat_ball_ty *bb = (b != NULL) ? b : &zero;
            err = subtract ? bigfloat_ball_sub(&r.coeffs[i], aa, bb, ctx)
                           : bigfloat_ball_add(&r.coeffs[i], aa, bb, ctx);
        }
    }
    if (err == BIGFLOAT_OK_E) {
        bseries_trim(&r);
        bseries_swap(&r, dst);
    }
    bigfloat_ball_free(&zero);
    bigfloat_ball_series_free(&r);
    return err;
}

bigfloat_err_ty bigfloat_ball_series_add(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *lhs,
        const bigfloat_ball_series_ty *rhs, size_t order,
        const bigfloat_ctx_ty *ctx) {
    return bseries_addsub(dst, lhs, rhs, order, false, ctx);
}

bigfloat_err_ty bigfloat_ball_series_sub(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *lhs,
        const bigfloat_ball_series_ty *rhs, size_t order,
        const bigfloat_ctx_ty *ctx) {
    return bseries_addsub(dst, lhs, rhs, order, true, ctx);
}

bigfloat_err_ty bigfloat_ball_series_mul(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *lhs,
        const bigfloat_ball_series_ty *rhs, size_t order,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || lhs == NULL || rhs == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (lhs->len == 0U || rhs->len == 0U || order == 0U) {
        return bigfloat_ball_series_from_array(dst, 0, NULL, 0U);
    }
    bigfloat_ball_series_ty r;
    bigfloat_ball_series_init(&r);
    r.val = lhs->val + rhs->val;
    bigfloat_err_ty err = BIGFLOAT_OK_E;
    size_t full = lhs->len + rhs->len - 1U;
    size_t n = (full < order) ? full : order;
    for (size_t i = 0U; i < n && err == BIGFLOAT_OK_E; i++) {
        bigfloat_ball_ty acc;
        bigfloat_ball_init(&acc);
        size_t k0 = (i + 1U > rhs->len) ? (i + 1U - rhs->len) : 0U;
        for (size_t k = k0; k <= i && k < lhs->len && err == BIGFLOAT_OK_E;
                k++) {
            size_t j = i - k;
            if (j >= rhs->len) {
                continue;
            }
            bigfloat_ball_ty t;
            bigfloat_ball_init(&t);
            err = bigfloat_ball_mul(&t, &lhs->coeffs[k], &rhs->coeffs[j], ctx);
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_ball_add(&acc, &acc, &t, ctx);
            }
            bigfloat_ball_free(&t);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bseries_push_zero(&r);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_copy(&r.coeffs[i], &acc);
        }
        bigfloat_ball_free(&acc);
    }
    if (err == BIGFLOAT_OK_E) {
        bseries_trim(&r);
        bseries_swap(&r, dst);
    }
    bigfloat_ball_series_free(&r);
    return err;
}

bigfloat_err_ty bigfloat_ball_series_eval(bigfloat_ball_ty *out,
        const bigfloat_ball_series_ty *s, const bigfloat_ball_ty *x,
        const bigfloat_ctx_ty *ctx) {
    if (out == NULL || s == NULL || x == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (s->val < 0) {
        return BIGFLOAT_ERR_INVALID_E;   /* 仅支持 val ≥ 0 */
    }
    bigfloat_ball_ty acc;
    bigfloat_ball_init(&acc);
    bigfloat_err_ty err = BIGFLOAT_OK_E;
    if (s->len > 0U) {
        err = bigfloat_ball_copy(&acc, &s->coeffs[s->len - 1U]);
        for (size_t k = s->len - 1U; k-- > 0U && err == BIGFLOAT_OK_E; ) {
            bigfloat_ball_ty t;
            bigfloat_ball_init(&t);
            err = bigfloat_ball_mul(&t, &acc, x, ctx);
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_ball_add(&acc, &t, &s->coeffs[k], ctx);
            }
            bigfloat_ball_free(&t);
        }
        /* 乘以 x^{val} */
        bigfloat_ball_ty p;
        bigfloat_ball_init(&p);
        if (err == BIGFLOAT_OK_E) {
            bigfloat_ty one;
            bigfloat_ty zero;
            bigfloat_init(&one);
            bigfloat_init(&zero);
            (void)bigfloat_from_f64(&one, 1.0);
            err = bigfloat_ball_set(&p, &one, &zero, ctx);
            bigfloat_free(&zero);
            bigfloat_free(&one);
        }
        for (int64_t i = 0; i < s->val && err == BIGFLOAT_OK_E; i++) {
            err = bigfloat_ball_mul(&p, &p, x, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_mul(&acc, &acc, &p, ctx);
        }
        bigfloat_ball_free(&p);
    } else {
        bigfloat_ball_ty zero;
        bigfloat_ball_init(&zero);
        err = bigfloat_ball_copy(&acc, &zero);
        bigfloat_ball_free(&zero);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_copy(out, &acc);
    }
    bigfloat_ball_free(&acc);
    return err;
}

/* acc += scalar（加在 z^0 项），截断到 order */
static bigfloat_err_ty bseries_add_scalar(bigfloat_ball_series_ty *s,
        const bigfloat_ball_ty *scalar, size_t order,
        const bigfloat_ctx_ty *ctx) {
    if (s->len == 0U) {
        if (order == 0U) {
            return bigfloat_ball_series_from_array(s, 0, NULL, 0U);
        }
        return bigfloat_ball_series_from_array(s, 0, scalar, 1U);
    }
    if (s->val == 0) {
        bigfloat_err_ty err = bigfloat_ball_add(&s->coeffs[0], &s->coeffs[0],
                                                scalar, ctx);
        if (err == BIGFLOAT_OK_E) {
            bseries_trim(s);
            bseries_truncate_len(s, order);
        }
        return err;
    }
    /* val > 0：前插常数项 */
    size_t lead = (size_t)s->val;
    size_t total = lead + s->len;
    size_t new_len = (total < order) ? total : order;
    if (new_len == 0U) {
        return bigfloat_ball_series_from_array(s, 0, NULL, 0U);
    }
    bigfloat_ball_ty *arr = (bigfloat_ball_ty *)nex_malloc(
            new_len * sizeof(bigfloat_ball_ty));
    if (arr == NULL) {
        return BIGFLOAT_ERR_OOM_E;
    }
    for (size_t i = 0U; i < new_len; i++) {
        bigfloat_ball_init(&arr[i]);
    }
    bigfloat_err_ty err = bigfloat_ball_copy(&arr[0], scalar);
    for (size_t i = lead; i < new_len && err == BIGFLOAT_OK_E; i++) {
        err = bigfloat_ball_copy(&arr[i], &s->coeffs[i - lead]);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_series_from_array(s, 0, arr, new_len);
    }
    for (size_t i = 0U; i < new_len; i++) {
        bigfloat_ball_free(&arr[i]);
    }
    free(arr);
    return err;
}

bigfloat_err_ty bigfloat_ball_series_compose(bigfloat_ball_series_ty *dst,
        const bigfloat_ball_series_ty *f,
        const bigfloat_ball_series_ty *g, size_t order,
        const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || f == NULL || g == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (f->val != 0 || g->val < 0) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (order == 0U || f->len == 0U) {
        return bigfloat_ball_series_from_array(dst, 0, NULL, 0U);
    }
    bigfloat_ball_series_ty acc;
    bigfloat_ball_series_init(&acc);
    bigfloat_err_ty err = BIGFLOAT_OK_E;
    for (size_t k = f->len; k-- > 0U && err == BIGFLOAT_OK_E; ) {
        bigfloat_ball_series_ty prod;
        bigfloat_ball_series_init(&prod);
        err = bigfloat_ball_series_mul(&prod, &acc, g, order, ctx);
        if (err == BIGFLOAT_OK_E) {
            err = bseries_add_scalar(&prod, &f->coeffs[k], order, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            bigfloat_ball_series_free(&acc);
            acc = prod;
            bigfloat_ball_series_init(&prod);
        }
        bigfloat_ball_series_free(&prod);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_series_copy(dst, &acc);
    }
    bigfloat_ball_series_free(&acc);
    return err;
}
