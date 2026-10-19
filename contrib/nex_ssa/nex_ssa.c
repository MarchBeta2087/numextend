/*
 * nex_ssa：Schönhage–Strassen 原型（contrib 实验，issue #102）。
 *
 * 结构：
 *   1. 把 a/b 拆成 n 个 m bit 块（n = m = 2^(k-1)，L = 2n = 2m）；
 *   2. 在环 Z/(2^L+1) 与 Z/(2^L−1) 上分别做长度 L 的循环卷积；
 *   3. 两个环的系数用 CRT 恢复精确 c_l（c_l < n·2^(2m) < 2^(2L)）；
 *   4. 按 base 2^m 进位合并。
 *
 * 仅用于原型与基准；不进入核心 AUTO。为便于验证，环算术使用
 * bigint_bin，FFT 为迭代 Cooley–Tukey。
 */

#include "nex_ssa.h"

#include <stdlib.h>
#include <string.h>

#define NEX_SSA_MIN_K 6u /* L >= 64, m >= 32 */

typedef struct {
    bigint_bin_ty mod;
    size_t L;      /* 变换长度 / 根的阶（bit） */
    size_t MB;     /* 模数 2^MB ± 1 的 MB（bit） */
    size_t W;      /* MB / 32 */
    uint32_t root; /* 2 或 4，阶为 L */
    bool plus;     /* true: 2^MB+1；false: 2^MB−1 */
} ssa_ring_ty;

static bigint_err_ty ssa_ring_init(ssa_ring_ty *r, size_t L, size_t MB,
        bool plus, uint32_t root)
{
    bigint_bin_ty one;
    bigint_err_ty e;
    r->L = L;
    r->MB = MB;
    r->W = MB / 32u;
    r->plus = plus;
    r->root = root;
    e = bigint_bin_init(&r->mod);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&one);
    if (e != BIGINT_OK_E) { bigint_bin_free(&r->mod); return e; }
    e = bigint_bin_from_u64(&one, 1);
    if (e == BIGINT_OK_E) e = bigint_bin_shl(&r->mod, &one, MB);
    if (e == BIGINT_OK_E) {
        if (plus) e = bigint_bin_add(&r->mod, &r->mod, &one);
        else e = bigint_bin_sub(&r->mod, &r->mod, &one);
    }
    bigint_bin_free(&one);
    return e;
}

static void ssa_ring_free(ssa_ring_ty *r)
{
    bigint_bin_free(&r->mod);
}

/* 由肢数组构造非负 bigint；调用者保证 limbs 已无前导零。 */
static void ssa_set_limbs(bigint_bin_ty *v, const uint32_t *limbs, size_t len)
{
    while (len > 0 && limbs[len - 1] == 0) len--;
    if (len == 0) {
        v->sign = BIGINT_SIGN_ZERO_E;
        v->len = 0;
        return;
    }
    memcpy(v->limbs, limbs, len * sizeof(uint32_t));
    v->len = len;
    v->sign = BIGINT_SIGN_POS_E;
}

/*
 * 把 p（< 2^(2L)）规约到 mod = 2^L ± 1。
 * p 为低 L bit 与高 L bit 的拼接：p = lo + hi·2^L。
 */
static bigint_err_ty ssa_ring_reduce(bigint_bin_ty *dst,
        const bigint_bin_ty *p, const ssa_ring_ty *r)
{
    bigint_bin_ty lo, hi, val;
    size_t lo_len, hi_len;
    bigint_err_ty e;

    if (bigint_bin_is_zero(p)) {
        bigint_bin_move(dst, (bigint_bin_ty *)p);
        return BIGINT_OK_E;
    }
    lo_len = p->len < r->W ? p->len : r->W;
    hi_len = p->len > r->W ? p->len - r->W : 0;
    e = bigint_bin_init_cap(&lo, lo_len);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init_cap(&hi, hi_len);
    if (e != BIGINT_OK_E) { bigint_bin_free(&lo); return e; }
    e = bigint_bin_init(&val);
    if (e != BIGINT_OK_E) { bigint_bin_free(&lo); bigint_bin_free(&hi); return e; }

    ssa_set_limbs(&lo, p->limbs, lo_len);
    ssa_set_limbs(&hi, p->limbs + r->W, hi_len);

    if (r->plus) {
        e = bigint_bin_sub(&val, &lo, &hi);
        if (e == BIGINT_OK_E && val.sign == BIGINT_SIGN_NEG_E)
            e = bigint_bin_add(&val, &val, &r->mod);
        if (e == BIGINT_OK_E && bigint_bin_cmp(&val, &r->mod) >= 0)
            e = bigint_bin_sub(&val, &val, &r->mod);
    } else {
        e = bigint_bin_add(&val, &lo, &hi);
        if (e == BIGINT_OK_E && bigint_bin_cmp(&val, &r->mod) >= 0)
            e = bigint_bin_sub(&val, &val, &r->mod);
        if (e == BIGINT_OK_E && bigint_bin_cmp(&val, &r->mod) >= 0)
            e = bigint_bin_sub(&val, &val, &r->mod);
    }
    bigint_bin_free(&lo);
    bigint_bin_free(&hi);
    if (e != BIGINT_OK_E) { bigint_bin_free(&val); return e; }
    bigint_bin_move(dst, &val);
    return BIGINT_OK_E;
}

static bigint_err_ty ssa_add(bigint_bin_ty *dst, const bigint_bin_ty *a,
        const bigint_bin_ty *b, const ssa_ring_ty *r)
{
    bigint_err_ty e = bigint_bin_add(dst, a, b);
    if (e == BIGINT_OK_E && bigint_bin_cmp(dst, &r->mod) >= 0)
        e = bigint_bin_sub(dst, dst, &r->mod);
    return e;
}

static bigint_err_ty ssa_sub(bigint_bin_ty *dst, const bigint_bin_ty *a,
        const bigint_bin_ty *b, const ssa_ring_ty *r)
{
    bigint_err_ty e = bigint_bin_sub(dst, a, b);
    if (e == BIGINT_OK_E && dst->sign == BIGINT_SIGN_NEG_E)
        e = bigint_bin_add(dst, dst, &r->mod);
    return e;
}

static bigint_err_ty ssa_mul(bigint_bin_ty *dst, const bigint_bin_ty *a,
        const bigint_bin_ty *b, const ssa_ring_ty *r)
{
    bigint_bin_ty p;
    bigint_err_ty e;
    if (bigint_bin_is_zero(a) || bigint_bin_is_zero(b)) {
        bigint_bin_free(dst);
        return bigint_bin_init(dst);
    }
    e = bigint_bin_init(&p);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_mul(&p, a, b);
    if (e == BIGINT_OK_E) e = ssa_ring_reduce(dst, &p, r);
    bigint_bin_free(&p);
    return e;
}

static bigint_err_ty ssa_pow(bigint_bin_ty *dst, const bigint_bin_ty *base,
        size_t exp, const ssa_ring_ty *r)
{
    bigint_bin_ty res, b;
    bigint_err_ty e;
    e = bigint_bin_init(&res);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&b);
    if (e != BIGINT_OK_E) { bigint_bin_free(&res); return e; }
    e = bigint_bin_copy(&b, base);
    if (e != BIGINT_OK_E) { bigint_bin_free(&b); bigint_bin_free(&res); return e; }
    e = bigint_bin_from_u64(&res, 1);
    while (e == BIGINT_OK_E && exp != 0) {
        if (exp & 1u) e = ssa_mul(&res, &res, &b, r);
        exp >>= 1;
        if (exp != 0) e = ssa_mul(&b, &b, &b, r);
    }
    bigint_bin_free(&b);
    if (e != BIGINT_OK_E) { bigint_bin_free(&res); return e; }
    bigint_bin_move(dst, &res);
    return BIGINT_OK_E;
}

/* inv2 = (M+1)/2；inv4 = (3M+1)/4（M 为奇数）。 */
static bigint_err_ty ssa_inv_small(bigint_bin_ty *dst, const ssa_ring_ty *r,
        uint32_t k)
{
    bigint_bin_ty t, one;
    bigint_err_ty e;
    e = bigint_bin_init(&t);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&one);
    if (e != BIGINT_OK_E) { bigint_bin_free(&t); return e; }
    e = bigint_bin_from_u64(&one, 1);
    if (e == BIGINT_OK_E) e = bigint_bin_from_u64(&t, (uint64_t)(k - 1));
    if (e == BIGINT_OK_E) e = bigint_bin_mul(&t, &t, &r->mod);
    if (e == BIGINT_OK_E) e = bigint_bin_add(&t, &t, &one);
    if (e == BIGINT_OK_E) e = bigint_bin_shr(dst, &t, (size_t)(k == 2 ? 1 : 2));
    bigint_bin_free(&t);
    bigint_bin_free(&one);
    return e;
}

/*
 * L^{-1} mod M：
 *   M = 2^L+1 时，L·(M − 2^(L−k)) ≡ 1；
 *   M = 2^L−1 时，L·2^(L−k) ≡ 1。
 */
static bigint_err_ty ssa_inv_len(bigint_bin_ty *dst, const ssa_ring_ty *r,
        size_t k)
{
    bigint_bin_ty t, one;
    bigint_err_ty e;
    e = bigint_bin_init(&t);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&one);
    if (e != BIGINT_OK_E) { bigint_bin_free(&t); return e; }
    e = bigint_bin_from_u64(&one, 1);
    if (e == BIGINT_OK_E) e = bigint_bin_shl(&t, &one, r->MB - k);
    if (e == BIGINT_OK_E && r->plus)
        e = bigint_bin_sub(dst, &r->mod, &t);
    else if (e == BIGINT_OK_E)
        bigint_bin_move(dst, &t);
    bigint_bin_free(&t);
    bigint_bin_free(&one);
    return e;
}

static void ssa_bit_reverse(bigint_bin_ty *a, size_t n)
{
    size_t i, j = 0, bit;
    for (i = 1; i < n; i++) {
        bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            bigint_bin_ty t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }
}

static bigint_err_ty ssa_fft(bigint_bin_ty *a, size_t L, size_t k,
        const ssa_ring_ty *r, bool inverse)
{
    bigint_bin_ty root, wlen, w, t, v;
    size_t len, i, j, half;
    bigint_err_ty e;
    (void)k;

    e = bigint_bin_init(&root);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&wlen);
    if (e != BIGINT_OK_E) { bigint_bin_free(&root); return e; }
    e = bigint_bin_init(&w);
    if (e != BIGINT_OK_E) { bigint_bin_free(&root); bigint_bin_free(&wlen); return e; }
    e = bigint_bin_init(&t);
    if (e != BIGINT_OK_E) { bigint_bin_free(&root); bigint_bin_free(&wlen); bigint_bin_free(&w); return e; }
    e = bigint_bin_init(&v);
    if (e != BIGINT_OK_E) {
        bigint_bin_free(&root); bigint_bin_free(&wlen); bigint_bin_free(&w);
        bigint_bin_free(&t); return e;
    }

    if (inverse) e = ssa_inv_small(&root, r, r->root);
    else e = bigint_bin_from_u64(&root, r->root);
    ssa_bit_reverse(a, L);
    for (len = 2; e == BIGINT_OK_E && len <= L; len <<= 1) {
        e = ssa_pow(&wlen, &root, L / len, r);
        if (e != BIGINT_OK_E) break;
        half = len >> 1;
        for (i = 0; e == BIGINT_OK_E && i < L; i += len) {
            e = bigint_bin_from_u64(&w, 1);
            for (j = 0; e == BIGINT_OK_E && j < half; j++) {
                e = ssa_mul(&v, &a[i + j + half], &w, r);
                if (e != BIGINT_OK_E) break;
                e = ssa_sub(&t, &a[i + j], &v, r);
                if (e == BIGINT_OK_E) e = ssa_add(&a[i + j], &a[i + j], &v, r);
                if (e == BIGINT_OK_E) bigint_bin_move(&a[i + j + half], &t);
                if (e == BIGINT_OK_E) e = ssa_mul(&w, &w, &wlen, r);
            }
        }
    }
    bigint_bin_free(&root);
    bigint_bin_free(&wlen);
    bigint_bin_free(&w);
    bigint_bin_free(&t);
    bigint_bin_free(&v);
    return e;
}

static void ssa_free_array(bigint_bin_ty *a, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) bigint_bin_free(&a[i]);
    free(a);
}

/* 在某个环上做长度 L 的循环卷积，输出前 2n−1 个系数。 */
static bigint_err_ty ssa_conv(bigint_bin_ty **out, size_t *out_len,
        const bigint_bin_ty *pieces_a, const bigint_bin_ty *pieces_b,
        size_t n, size_t L, size_t k, size_t W, size_t MB, bool plus,
        uint32_t root)
{
    ssa_ring_ty r;
    bigint_bin_ty *a = NULL, *b = NULL, inv;
    bigint_err_ty e;
    size_t i, clen = 2 * n - 1;

    *out = NULL;
    *out_len = 0;
    e = ssa_ring_init(&r, L, MB, plus, root);
    if (e != BIGINT_OK_E) return e;
    a = (bigint_bin_ty *)calloc(L, sizeof(*a));
    b = (bigint_bin_ty *)calloc(L, sizeof(*b));
    if (!a || !b) { e = BIGINT_ERR_OOM_E; goto done; }
    for (i = 0; i < L; i++) {
        e = bigint_bin_init(&a[i]);
        if (e != BIGINT_OK_E) goto done;
        e = bigint_bin_init(&b[i]);
        if (e != BIGINT_OK_E) goto done;
    }
    for (i = 0; i < n; i++) {
        e = bigint_bin_copy(&a[i], &pieces_a[i]);
        if (e != BIGINT_OK_E) goto done;
        e = bigint_bin_copy(&b[i], &pieces_b[i]);
        if (e != BIGINT_OK_E) goto done;
    }
    (void)W;

    e = ssa_fft(a, L, k, &r, false);
    if (e == BIGINT_OK_E) e = ssa_fft(b, L, k, &r, false);
    for (i = 0; e == BIGINT_OK_E && i < L; i++)
        e = ssa_mul(&a[i], &a[i], &b[i], &r);
    if (e == BIGINT_OK_E) e = ssa_fft(a, L, k, &r, true);
    if (e == BIGINT_OK_E) {
        e = bigint_bin_init(&inv);
        if (e == BIGINT_OK_E) {
            e = ssa_inv_len(&inv, &r, k);
            for (i = 0; e == BIGINT_OK_E && i < L; i++)
                e = ssa_mul(&a[i], &a[i], &inv, &r);
            bigint_bin_free(&inv);
        }
    }
    if (e != BIGINT_OK_E) goto done;

    *out = (bigint_bin_ty *)calloc(clen, sizeof(**out));
    if (!*out) { e = BIGINT_ERR_OOM_E; goto done; }
    for (i = 0; i < clen; i++) {
        e = bigint_bin_init(&(*out)[i]);
        if (e != BIGINT_OK_E) {
            size_t j;
            for (j = 0; j < i; j++) bigint_bin_free(&(*out)[j]);
            free(*out);
            *out = NULL;
            goto done;
        }
    }
    for (i = 0; i < clen; i++) bigint_bin_move(&(*out)[i], &a[i]);
    *out_len = clen;

done:
    if (a) ssa_free_array(a, L);
    if (b) ssa_free_array(b, L);
    ssa_ring_free(&r);
    return e;
}

static bigint_err_ty ssa_make_pieces(bigint_bin_ty **out, size_t n, size_t W,
        const bigint_bin_ty *x)
{
    bigint_bin_ty *p;
    size_t i, off, len;
    bigint_err_ty e;
    p = (bigint_bin_ty *)calloc(n, sizeof(*p));
    if (!p) return BIGINT_ERR_OOM_E;
    for (i = 0; i < n; i++) {
        e = bigint_bin_init_cap(&p[i], W);
        if (e != BIGINT_OK_E) {
            for (i = 0; i < n; i++) bigint_bin_free(&p[i]);
            free(p);
            return e;
        }
        off = i * W;
        if (off < x->len) {
            len = x->len - off;
            if (len > W) len = W;
            memcpy(p[i].limbs, x->limbs + off, len * sizeof(uint32_t));
            p[i].len = len;
            p[i].sign = BIGINT_SIGN_POS_E;
            while (p[i].len > 0 && p[i].limbs[p[i].len - 1] == 0) p[i].len--;
            if (p[i].len == 0) p[i].sign = BIGINT_SIGN_ZERO_E;
        }
    }
    *out = p;
    return BIGINT_OK_E;
}

static bigint_err_ty ssa_crt_and_carry(bigint_bin_ty *dst,
        const bigint_bin_ty *cp, const bigint_bin_ty *cm, size_t clen,
        const ssa_ring_ty *rp, const ssa_ring_ty *rm, size_t n, size_t W)
{
    bigint_bin_ty inv2, diff, t, prod, c, rp_mod;
    uint32_t *tmp;
    size_t i, j, cap = 2 * n * W + 4, idx;
    uint64_t carry, sum;
    bigint_err_ty e;

    e = bigint_bin_init(&inv2);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&diff);
    if (e != BIGINT_OK_E) { bigint_bin_free(&inv2); return e; }
    e = bigint_bin_init(&t);
    if (e != BIGINT_OK_E) { bigint_bin_free(&inv2); bigint_bin_free(&diff); return e; }
    e = bigint_bin_init(&prod);
    if (e != BIGINT_OK_E) {
        bigint_bin_free(&inv2); bigint_bin_free(&diff); bigint_bin_free(&t);
        return e;
    }
    e = bigint_bin_init(&c);
    if (e != BIGINT_OK_E) {
        bigint_bin_free(&inv2); bigint_bin_free(&diff); bigint_bin_free(&t);
        bigint_bin_free(&prod); return e;
    }
    e = bigint_bin_init(&rp_mod);
    if (e != BIGINT_OK_E) {
        bigint_bin_free(&inv2); bigint_bin_free(&diff); bigint_bin_free(&t);
        bigint_bin_free(&prod); bigint_bin_free(&c); return e;
    }
    e = ssa_inv_small(&inv2, rm, 2); /* (M− + 1)/2 */
    if (e != BIGINT_OK_E) goto done;

    tmp = (uint32_t *)calloc(cap, sizeof(uint32_t));
    if (!tmp) { e = BIGINT_ERR_OOM_E; goto done; }

    for (i = 0; i < clen; i++) {
        /* cp 是模 M1 的余数，需完整归一到 M2（M2 可能远小于 M1）。 */
        e = bigint_bin_div_rem(NULL, &rp_mod, &cp[i], &rm->mod);
        if (e != BIGINT_OK_E) break;
        e = ssa_sub(&diff, &cm[i], &rp_mod, rm);
        if (e != BIGINT_OK_E) break;
        e = ssa_mul(&t, &diff, &inv2, rm);
        if (e != BIGINT_OK_E) break;
        e = bigint_bin_mul(&prod, &rp->mod, &t);
        if (e != BIGINT_OK_E) break;
        e = bigint_bin_add(&c, &cp[i], &prod);
        if (e != BIGINT_OK_E) break;

        carry = 0;
        for (j = 0; j < c.len; j++) {
            idx = i * W + j;
            if (idx >= cap) { e = BIGINT_ERR_OVERFLOW_E; break; }
            sum = (uint64_t)tmp[idx] + c.limbs[j] + carry;
            tmp[idx] = (uint32_t)sum;
            carry = sum >> 32;
        }
        idx = i * W + c.len;
        while (e == BIGINT_OK_E && carry != 0 && idx < cap) {
            sum = (uint64_t)tmp[idx] + carry;
            tmp[idx] = (uint32_t)sum;
            carry = sum >> 32;
            idx++;
        }
        if (e == BIGINT_OK_E && carry != 0) e = BIGINT_ERR_OVERFLOW_E;
        if (e != BIGINT_OK_E) break;
    }
    if (e == BIGINT_OK_E) {
        cap = 2 * n * W + 4;
        while (cap > 0 && tmp[cap - 1] == 0) cap--;
        e = bigint_bin_init_cap(dst, cap);
        if (e == BIGINT_OK_E) ssa_set_limbs(dst, tmp, cap);
    }
    free(tmp);
done:
    bigint_bin_free(&inv2);
    bigint_bin_free(&diff);
    bigint_bin_free(&t);
    bigint_bin_free(&prod);
    bigint_bin_free(&c);
    bigint_bin_free(&rp_mod);
    return e;
}

bigint_err_ty nex_ssa_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    bigint_bin_ty a, b, *pa = NULL, *pb = NULL, *cp = NULL, *cm = NULL;
    size_t N, k, L, n, W, clen_p = 0, clen_m = 0;
    ssa_ring_ty rp, rm;
    bool neg;
    bigint_err_ty e;

    if (!dst || !lhs || !rhs) return BIGINT_ERR_INVALID_E;
    if (bigint_bin_is_zero(lhs) || bigint_bin_is_zero(rhs)) {
        bigint_bin_free(dst);
        return bigint_bin_init(dst);
    }
    N = (lhs->len > rhs->len ? lhs->len : rhs->len) * 32u;
    if (N == 0) {
        bigint_bin_free(dst);
        return bigint_bin_init(dst);
    }
    /* 取最小 k，使 (2^(k−1))^2 >= N，即 L^2/4 >= N。 */
    k = NEX_SSA_MIN_K;
    while (((size_t)1 << (2 * k - 2)) < N && k < 30) k++;
    L = (size_t)1 << k;
    n = L / 2;
    W = L / 64; /* m = L/2 bit = W 肢 */
    if (W == 0 || k >= 30) return BIGINT_ERR_UNSUPPORTED_E;

    e = bigint_bin_init(&a);
    if (e != BIGINT_OK_E) return e;
    e = bigint_bin_init(&b);
    if (e != BIGINT_OK_E) { bigint_bin_free(&a); return e; }
    e = bigint_bin_copy(&a, lhs);
    if (e == BIGINT_OK_E) e = bigint_bin_copy(&b, rhs);
    if (e != BIGINT_OK_E) goto done;
    neg = false;
    if (a.sign == BIGINT_SIGN_NEG_E) { neg = !neg; bigint_bin_neg(&a); }
    if (b.sign == BIGINT_SIGN_NEG_E) { neg = !neg; bigint_bin_neg(&b); }

    e = ssa_make_pieces(&pa, n, W, &a);
    if (e == BIGINT_OK_E) e = ssa_make_pieces(&pb, n, W, &b);
    if (e != BIGINT_OK_E) goto done;

    e = ssa_conv(&cp, &clen_p, pa, pb, n, L, k, W, L, true, 4u);
    if (e == BIGINT_OK_E)
        e = ssa_conv(&cm, &clen_m, pa, pb, n, L, k, W, L / 2u, true, 2u);
    if (e != BIGINT_OK_E) goto done;

    e = ssa_ring_init(&rp, L, L, true, 4u);
    if (e != BIGINT_OK_E) goto done;
    e = ssa_ring_init(&rm, L, L / 2u, true, 2u);
    if (e != BIGINT_OK_E) { ssa_ring_free(&rp); goto done; }

    e = ssa_crt_and_carry(dst, cp, cm, clen_p, &rp, &rm, n, W);
    if (e == BIGINT_OK_E && neg) bigint_bin_neg(dst);

    ssa_ring_free(&rp);
    ssa_ring_free(&rm);
    for (size_t i = 0; i < clen_p; i++) bigint_bin_free(&cp[i]);
    free(cp); cp = NULL;
    for (size_t i = 0; i < clen_m; i++) bigint_bin_free(&cm[i]);
    free(cm); cm = NULL;

done:
    if (pa) ssa_free_array(pa, n);
    if (pb) ssa_free_array(pb, n);
    if (cp) { for (size_t i = 0; i < clen_p; i++) bigint_bin_free(&cp[i]); free(cp); }
    if (cm) { for (size_t i = 0; i < clen_m; i++) bigint_bin_free(&cm[i]); free(cm); }
    bigint_bin_free(&a);
    bigint_bin_free(&b);
    return e;
}
