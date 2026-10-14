#include "nex/ball/nex_ball.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "nex/nex_alloc.h"

/*
 * bigdecimal 球系数截断级数（§I，v1.5.0；镜像 bigfloat 版）。
 *
 *   S(z) = Σ_{i=0}^{len-1} coeffs[i] · z^{val+i}
 *
 * 系数为 bigdecimal_ball_ty，运算全程由球传播半径。规范化与别名/异常安全
 * 约定同 bigfloat_ball_series。
 */

static void dbseries_swap(bigdecimal_ball_series_ty *a,
        bigdecimal_ball_series_ty *b) {
    bigdecimal_ball_series_ty t = *a;
    *a = *b;
    *b = t;
}

static bool dball_is_zero(const bigdecimal_ball_ty *b) {
    return bigdecimal_is_zero(&b->mid) && bigdecimal_is_zero(&b->rad);
}

static bigdecimal_err_ty dbseries_push_zero(bigdecimal_ball_series_ty *s) {
    if (s->len == s->cap) {
        size_t nc = (s->cap == 0U) ? 4U : s->cap * 2U;
        bigdecimal_ball_ty *arr = (bigdecimal_ball_ty *)nex_realloc(s->coeffs,
                nc * sizeof(bigdecimal_ball_ty));
        if (arr == NULL) {
            return BIGDECIMAL_ERR_OOM_E;
        }
        s->coeffs = arr;
        s->cap = nc;
    }
    bigdecimal_err_ty err = bigdecimal_ball_init(&s->coeffs[s->len]);
    if (err != BIGDECIMAL_OK_E) {
        return err;
    }
    s->len++;
    return BIGDECIMAL_OK_E;
}

static void dbseries_trim(bigdecimal_ball_series_ty *s) {
    while (s->len > 0U && dball_is_zero(&s->coeffs[s->len - 1U])) {
        bigdecimal_ball_free(&s->coeffs[s->len - 1U]);
        s->len--;
    }
    if (s->len == 0U) {
        s->val = 0;
    }
}

static void dbseries_truncate_len(bigdecimal_ball_series_ty *s, size_t n) {
    while (s->len > n) {
        bigdecimal_ball_free(&s->coeffs[s->len - 1U]);
        s->len--;
    }
    if (s->len == 0U) {
        s->val = 0;
    }
}

bigdecimal_err_ty bigdecimal_ball_series_init(bigdecimal_ball_series_ty *s) {
    if (s == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    s->val = 0;
    s->coeffs = NULL;
    s->len = 0U;
    s->cap = 0U;
    return BIGDECIMAL_OK_E;
}

void bigdecimal_ball_series_free(bigdecimal_ball_series_ty *s) {
    if (s == NULL) {
        return;
    }
    for (size_t i = 0U; i < s->len; i++) {
        bigdecimal_ball_free(&s->coeffs[i]);
    }
    free(s->coeffs);
    s->coeffs = NULL;
    s->len = 0U;
    s->cap = 0U;
    s->val = 0;
}

bigdecimal_err_ty bigdecimal_ball_series_from_array(
        bigdecimal_ball_series_ty *dst, int64_t val,
        const bigdecimal_ball_ty *coeffs, size_t len) {
    if (dst == NULL || (len > 0U && coeffs == NULL)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_series_ty r;
    bigdecimal_ball_series_init(&r);
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    for (size_t i = 0U; i < len && err == BIGDECIMAL_OK_E; i++) {
        err = dbseries_push_zero(&r);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_copy(&r.coeffs[i], &coeffs[i]);
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        r.val = val;
        dbseries_trim(&r);
        dbseries_swap(&r, dst);
    }
    bigdecimal_ball_series_free(&r);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_series_copy(bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *src) {
    if (dst == NULL || src == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    return bigdecimal_ball_series_from_array(dst, src->val, src->coeffs,
                                             src->len);
}

static bigdecimal_err_ty dbseries_truncate(bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *src, size_t order) {
    size_t n = (src->len < order) ? src->len : order;
    return bigdecimal_ball_series_from_array(dst, src->val, src->coeffs, n);
}

static const bigdecimal_ball_ty *dbseries_at(
        const bigdecimal_ball_series_ty *s, int64_t e) {
    if (s->len == 0U || e < s->val) {
        return NULL;
    }
    int64_t off = e - s->val;
    if (off < 0 || (uint64_t)off >= (uint64_t)s->len) {
        return NULL;
    }
    return &s->coeffs[(size_t)off];
}

static bigdecimal_err_ty dbseries_addsub(bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *lhs,
        const bigdecimal_ball_series_ty *rhs, size_t order, bool subtract,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || lhs == NULL || rhs == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (lhs->len == 0U) {
        return dbseries_truncate(dst, rhs, order);
    }
    if (rhs->len == 0U) {
        return dbseries_truncate(dst, lhs, order);
    }
    bigdecimal_ball_series_ty r;
    bigdecimal_ball_series_init(&r);
    r.val = (lhs->val < rhs->val) ? lhs->val : rhs->val;
    bigdecimal_ball_ty zero;
    bigdecimal_ball_init(&zero);
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    for (size_t i = 0U; i < order && i < max_len && err == BIGDECIMAL_OK_E;
            i++) {
        int64_t e = r.val + (int64_t)i;
        const bigdecimal_ball_ty *a = dbseries_at(lhs, e);
        const bigdecimal_ball_ty *b = dbseries_at(rhs, e);
        if (a == NULL && b == NULL) {
            break;
        }
        err = dbseries_push_zero(&r);
        if (err == BIGDECIMAL_OK_E) {
            const bigdecimal_ball_ty *aa = (a != NULL) ? a : &zero;
            const bigdecimal_ball_ty *bb = (b != NULL) ? b : &zero;
            err = subtract ? bigdecimal_ball_sub(&r.coeffs[i], aa, bb, ctx)
                           : bigdecimal_ball_add(&r.coeffs[i], aa, bb, ctx);
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        dbseries_trim(&r);
        dbseries_swap(&r, dst);
    }
    bigdecimal_ball_free(&zero);
    bigdecimal_ball_series_free(&r);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_series_add(bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *lhs,
        const bigdecimal_ball_series_ty *rhs, size_t order,
        const bigdecimal_ctx_ty *ctx) {
    return dbseries_addsub(dst, lhs, rhs, order, false, ctx);
}

bigdecimal_err_ty bigdecimal_ball_series_sub(bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *lhs,
        const bigdecimal_ball_series_ty *rhs, size_t order,
        const bigdecimal_ctx_ty *ctx) {
    return dbseries_addsub(dst, lhs, rhs, order, true, ctx);
}

bigdecimal_err_ty bigdecimal_ball_series_mul(bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *lhs,
        const bigdecimal_ball_series_ty *rhs, size_t order,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || lhs == NULL || rhs == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (lhs->len == 0U || rhs->len == 0U || order == 0U) {
        return bigdecimal_ball_series_from_array(dst, 0, NULL, 0U);
    }
    bigdecimal_ball_series_ty r;
    bigdecimal_ball_series_init(&r);
    r.val = lhs->val + rhs->val;
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    size_t full = lhs->len + rhs->len - 1U;
    size_t n = (full < order) ? full : order;
    for (size_t i = 0U; i < n && err == BIGDECIMAL_OK_E; i++) {
        bigdecimal_ball_ty acc;
        bigdecimal_ball_init(&acc);
        size_t k0 = (i + 1U > rhs->len) ? (i + 1U - rhs->len) : 0U;
        for (size_t k = k0; k <= i && k < lhs->len && err == BIGDECIMAL_OK_E;
                k++) {
            size_t j = i - k;
            if (j >= rhs->len) {
                continue;
            }
            bigdecimal_ball_ty t;
            bigdecimal_ball_init(&t);
            err = bigdecimal_ball_mul(&t, &lhs->coeffs[k], &rhs->coeffs[j],
                                      ctx);
            if (err == BIGDECIMAL_OK_E) {
                err = bigdecimal_ball_add(&acc, &acc, &t, ctx);
            }
            bigdecimal_ball_free(&t);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = dbseries_push_zero(&r);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_copy(&r.coeffs[i], &acc);
        }
        bigdecimal_ball_free(&acc);
    }
    if (err == BIGDECIMAL_OK_E) {
        dbseries_trim(&r);
        dbseries_swap(&r, dst);
    }
    bigdecimal_ball_series_free(&r);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_series_eval(bigdecimal_ball_ty *out,
        const bigdecimal_ball_series_ty *s, const bigdecimal_ball_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || s == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (s->val < 0) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_ty acc;
    bigdecimal_ball_init(&acc);
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    if (s->len > 0U) {
        err = bigdecimal_ball_copy(&acc, &s->coeffs[s->len - 1U]);
        for (size_t k = s->len - 1U; k-- > 0U && err == BIGDECIMAL_OK_E; ) {
            bigdecimal_ball_ty t;
            bigdecimal_ball_init(&t);
            err = bigdecimal_ball_mul(&t, &acc, x, ctx);
            if (err == BIGDECIMAL_OK_E) {
                err = bigdecimal_ball_add(&acc, &t, &s->coeffs[k], ctx);
            }
            bigdecimal_ball_free(&t);
        }
        bigdecimal_ball_ty p;
        bigdecimal_ball_init(&p);
        if (err == BIGDECIMAL_OK_E) {
            bigdecimal_ty one;
            bigdecimal_ty zero;
            bigdecimal_init(&one);
            bigdecimal_init(&zero);
            err = bigdecimal_from_str(&one, "1", ctx, NULL);
            if (err == BIGDECIMAL_OK_E) {
                err = bigdecimal_ball_set(&p, &one, &zero, ctx);
            }
            bigdecimal_free(&zero);
            bigdecimal_free(&one);
        }
        for (int64_t i = 0; i < s->val && err == BIGDECIMAL_OK_E; i++) {
            err = bigdecimal_ball_mul(&p, &p, x, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_mul(&acc, &acc, &p, ctx);
        }
        bigdecimal_ball_free(&p);
    } else {
        bigdecimal_ball_ty zero;
        bigdecimal_ball_init(&zero);
        err = bigdecimal_ball_copy(&acc, &zero);
        bigdecimal_ball_free(&zero);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_copy(out, &acc);
    }
    bigdecimal_ball_free(&acc);
    return err;
}

static bigdecimal_err_ty dbseries_add_scalar(bigdecimal_ball_series_ty *s,
        const bigdecimal_ball_ty *scalar, size_t order,
        const bigdecimal_ctx_ty *ctx) {
    if (s->len == 0U) {
        if (order == 0U) {
            return bigdecimal_ball_series_from_array(s, 0, NULL, 0U);
        }
        return bigdecimal_ball_series_from_array(s, 0, scalar, 1U);
    }
    if (s->val == 0) {
        bigdecimal_err_ty err = bigdecimal_ball_add(&s->coeffs[0],
                &s->coeffs[0], scalar, ctx);
        if (err == BIGDECIMAL_OK_E) {
            dbseries_trim(s);
            dbseries_truncate_len(s, order);
        }
        return err;
    }
    size_t lead = (size_t)s->val;
    size_t total = lead + s->len;
    size_t new_len = (total < order) ? total : order;
    if (new_len == 0U) {
        return bigdecimal_ball_series_from_array(s, 0, NULL, 0U);
    }
    bigdecimal_ball_ty *arr = (bigdecimal_ball_ty *)nex_malloc(
            new_len * sizeof(bigdecimal_ball_ty));
    if (arr == NULL) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    for (size_t i = 0U; i < new_len; i++) {
        bigdecimal_ball_init(&arr[i]);
    }
    bigdecimal_err_ty err = bigdecimal_ball_copy(&arr[0], scalar);
    for (size_t i = lead; i < new_len && err == BIGDECIMAL_OK_E; i++) {
        err = bigdecimal_ball_copy(&arr[i], &s->coeffs[i - lead]);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_series_from_array(s, 0, arr, new_len);
    }
    for (size_t i = 0U; i < new_len; i++) {
        bigdecimal_ball_free(&arr[i]);
    }
    free(arr);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_series_compose(
        bigdecimal_ball_series_ty *dst,
        const bigdecimal_ball_series_ty *f,
        const bigdecimal_ball_series_ty *g, size_t order,
        const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || f == NULL || g == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (f->val != 0 || g->val < 0) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (order == 0U || f->len == 0U) {
        return bigdecimal_ball_series_from_array(dst, 0, NULL, 0U);
    }
    bigdecimal_ball_series_ty acc;
    bigdecimal_ball_series_init(&acc);
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    for (size_t k = f->len; k-- > 0U && err == BIGDECIMAL_OK_E; ) {
        bigdecimal_ball_series_ty prod;
        bigdecimal_ball_series_init(&prod);
        err = bigdecimal_ball_series_mul(&prod, &acc, g, order, ctx);
        if (err == BIGDECIMAL_OK_E) {
            err = dbseries_add_scalar(&prod, &f->coeffs[k], order, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            bigdecimal_ball_series_free(&acc);
            acc = prod;
            bigdecimal_ball_series_init(&prod);
        }
        bigdecimal_ball_series_free(&prod);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_series_copy(dst, &acc);
    }
    bigdecimal_ball_series_free(&acc);
    return err;
}
