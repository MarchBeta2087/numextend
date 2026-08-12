/*
 * nex_ntt.c：数论变换（NTT）核心实现（设计文档 §4.3）。
 *
 * 职责：内置模数表与校验、DIF 正变换 / DIT 逆变换（正逆配对免位反转）、
 * 频域点乘、单系数 Garner CRT 重构。只操作系数数组，不依赖 bigint 类型；
 * 无动态分配，全部函数无全局可变状态（§3.3 线程安全）。
 *
 * 正确性要点（随实现维护）：
 *   - 模乘 / 模幂中间量一律经 uint64_t（C99 无 128 位扩展）；
 *   - 蝶形加减：u、v < p 时 u+v 可能回绕 uint32（内置表含 p > 2^31 的
 *     模数），求和走 uint64 中间量再单次减 p；u−v 的补数分支 u+p−v
 *     在 u < v 时 < p，无需回绕处理；
 *   - 位反转：DIF 正变换输出位反转序、DIT 逆变换消费位反转序，配对后
 *     整体输入输出均自然序，无需位反转表（设计文档 §4.3"位反转与蝶形"）；
 *   - 模数表全部元素经 ntt_mod_validate 校验（质数 + 根阶），c 覆盖
 *     21..27，见设计文档 §4.3 素数表。
 */

#include "nex/ntt/nex_ntt.h"

#include <stddef.h>
#include <stdint.h>

/* 内置模数表：设计文档 §4.3，c 覆盖 21..27，全部经 ntt_mod_validate 校验 */
static const ntt_mod_ty ntt_builtin_mods[] = {
    { 1004535809U, 479U, 21U, 702606812U },
    { 985661441U, 235U, 22U, 79986183U },
    { 998244353U, 119U, 23U, 15311432U },
    { 754974721U, 45U, 24U, 739831874U },
    { 1224736769U, 73U, 24U, 1098543633U },
    { 167772161U, 5U, 25U, 243U },
    { 1107296257U, 33U, 25U, 1087287097U },
    { 469762049U, 7U, 26U, 2187U },
    { 1811939329U, 27U, 26U, 72705542U },
    { 2013265921U, 15U, 27U, 440564289U },
    { 2281701377U, 17U, 27U, 129140163U },
};

#define NTT_BUILTIN_MOD_COUNT \
        (sizeof(ntt_builtin_mods) / sizeof(ntt_builtin_mods[0]))

/* ------------------------------------------------------------------ */
/* 内部辅助：模运算                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 模幂 base^exp mod mod（任意 uint32 模数，含合数；供 MR 与模幂复用）
 */
static uint32_t ntt_pow_u32(uint32_t base, uint32_t exp, uint32_t mod)
{
    uint64_t result = 1U;
    uint64_t factor = base;
    while (exp > 0U) {
        if ((exp & 1U) != 0U) {
            result = result * factor % mod;
        }
        factor = factor * factor % mod;
        exp >>= 1U;
    }
    return (uint32_t)result;
}

/*
 * brief: Miller-Rabin 质数判定，基数 {2,3,5,7,11} 对全部 uint32 确定性
 *        （最小强伪素数 2,152,302,898,747 > 2^32，设计文档 §4.3）
 * return: n 为质数返回 1，否则 0
 */
static int ntt_is_prime_u32(uint32_t n)
{
    if (n < 2U) {
        return 0;
    }
    /* 小基数试除：同时覆盖 n ≤ 11 的全部情形 */
    static const uint32_t small_bases[] = { 2U, 3U, 5U, 7U, 11U };
    for (size_t i = 0U; i < 5U; i++) {
        if (n % small_bases[i] == 0U) {
            return (n == small_bases[i]);
        }
    }
    /* n−1 = d·2^s，d 奇 */
    uint32_t d = n - 1U;
    uint32_t s = 0U;
    while ((d & 1U) == 0U) {
        d >>= 1U;
        s++;
    }
    for (size_t i = 0U; i < 5U; i++) {
        uint32_t x = ntt_pow_u32(small_bases[i], d, n);
        if ((x == 1U) || (x == n - 1U)) {
            continue;
        }
        uint32_t composite = 1U;
        for (uint32_t r = 1U; r < s; r++) {
            x = (uint32_t)((uint64_t)x * x % n);
            if (x == n - 1U) {
                composite = 0U;
                break;
            }
        }
        if (composite != 0U) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 对外 API：模数表与校验                                              */
/* ------------------------------------------------------------------ */

size_t ntt_mod_count(void)
{
    return NTT_BUILTIN_MOD_COUNT;
}

const ntt_mod_ty *ntt_mod_get(size_t idx)
{
    if (idx >= NTT_BUILTIN_MOD_COUNT) {
        return NULL;
    }
    return &ntt_builtin_mods[idx];
}

ntt_err_ty ntt_mod_validate(const ntt_mod_ty *mod)
{
    if (mod == NULL) {
        return NTT_ERR_INVALID_E;
    }
    /* p = k·2^c + 1 的形态约束：k 奇、c ∈ [1, 31] */
    if ((mod->c == 0U) || (mod->c >= 32U) || (mod->k == 0U)
            || ((mod->k & 1U) == 0U)) {
        return NTT_ERR_MODULUS_E;
    }
    if (((uint64_t)mod->k << mod->c) != ((uint64_t)mod->p - 1U)) {
        return NTT_ERR_MODULUS_E;
    }
    if (!ntt_is_prime_u32(mod->p)) {
        return NTT_ERR_MODULUS_E;
    }
    if ((mod->w == 0U) || (mod->w >= mod->p)) {
        return NTT_ERR_MODULUS_E;
    }
    /* 根阶：w^(2^(c−1)) ≡ −1 (mod p)，p 为素数时等价于阶恰为 2^c */
    if (ntt_pow_u32(mod->w, 1U << (mod->c - 1U), mod->p) != mod->p - 1U) {
        return NTT_ERR_MODULUS_E;
    }
    return NTT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API：模运算                                                    */
/* ------------------------------------------------------------------ */

uint32_t ntt_mod_mul(uint32_t a, uint32_t b, const ntt_mod_ty *mod)
{
    return (uint32_t)((uint64_t)a * b % mod->p);
}

uint32_t ntt_mod_pow(uint32_t base, uint32_t exp, const ntt_mod_ty *mod)
{
    return ntt_pow_u32(base, exp, mod->p);
}

uint32_t ntt_mod_inv(uint32_t a, const ntt_mod_ty *mod)
{
    return ntt_pow_u32(a, mod->p - 2U, mod->p);
}

/* ------------------------------------------------------------------ */
/* 对外 API：变换                                                      */
/* ------------------------------------------------------------------ */

/*
 * brief: 蝶形求和（模 p 约简），u、v < p
 * note: u+v < 2p 可能回绕 uint32（内置表含 p > 2^31 的模数），故经
 *       uint64 中间量后单次减 p
 */
static uint32_t ntt_butterfly_sum(uint32_t u, uint32_t v, uint32_t p)
{
    const uint64_t uv = (uint64_t)u + v;
    return (uint32_t)(uv - ((uv >= p) ? (uint64_t)p : 0U));
}

/*
 * brief: 蝶形求差（模 p 约简），u、v < p
 * note: 补数分支 u+p−v 在 u < v 时 < p，无回绕
 */
static uint32_t ntt_butterfly_diff(uint32_t u, uint32_t v, uint32_t p)
{
    return (u >= v) ? (u - v) : (u + (p - v));
}

ntt_err_ty ntt_forward(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod)
{
    if ((coeffs == NULL) || (mod == NULL) || (mod->c >= 32U)
            || (log2n >= 32U) || (log2n > mod->c)) {
        return NTT_ERR_INVALID_E;
    }
    const uint32_t p = mod->p;
    const uint32_t n = 1U << log2n;
    /* 本原 N 次根 w_N = w^(2^(c−log2n)) */
    const uint32_t root = ntt_pow_u32(mod->w, 1U << (mod->c - log2n), p);

    for (uint32_t s = log2n; s >= 1U; s--) {
        const uint32_t len = 1U << s;
        const uint32_t half = len >> 1U;
        /* 本阶段本原 len 次根 wlen = w_N^(N/len) = w^(2^(c−s)) */
        const uint32_t wlen = ntt_pow_u32(root, n / len, p);
        uint32_t w = 1U;
        for (uint32_t k = 0U; k < half; k++) {
            for (uint32_t j = k; j < n; j += len) {
                const uint32_t u = coeffs[j];
                const uint32_t v = coeffs[j + half];
                coeffs[j] = ntt_butterfly_sum(u, v, p);
                coeffs[j + half] = ntt_mod_mul(ntt_butterfly_diff(u, v, p),
                        w, mod);
            }
            w = ntt_mod_mul(w, wlen, mod);
        }
    }
    return NTT_OK_E;
}

ntt_err_ty ntt_inverse(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod)
{
    if ((coeffs == NULL) || (mod == NULL) || (mod->c >= 32U)
            || (log2n >= 32U) || (log2n > mod->c)) {
        return NTT_ERR_INVALID_E;
    }
    const uint32_t p = mod->p;
    const uint32_t n = 1U << log2n;
    /* 逆变换用根的逆：w_N^-1 = (w^(2^(c−log2n)))^-1 */
    const uint32_t root_inv = ntt_mod_inv(
            ntt_pow_u32(mod->w, 1U << (mod->c - log2n), p), mod);

    for (uint32_t s = 1U; s <= log2n; s++) {
        const uint32_t len = 1U << s;
        const uint32_t half = len >> 1U;
        /* 本阶段 wlen = (w_N^-1)^(N/len) = w^(−(2^(c−s))) */
        const uint32_t wlen = ntt_pow_u32(root_inv, n / len, p);
        uint32_t w = 1U;
        for (uint32_t k = 0U; k < half; k++) {
            for (uint32_t j = k; j < n; j += len) {
                const uint32_t u = coeffs[j];
                const uint32_t v = ntt_mod_mul(coeffs[j + half], w, mod);
                coeffs[j] = ntt_butterfly_sum(u, v, p);
                coeffs[j + half] = ntt_butterfly_diff(u, v, p);
            }
            w = ntt_mod_mul(w, wlen, mod);
        }
    }
    /* 归一化：全体乘以 N^-1 mod p */
    const uint32_t n_inv = ntt_mod_inv(n, mod);
    for (uint32_t i = 0U; i < n; i++) {
        coeffs[i] = ntt_mod_mul(coeffs[i], n_inv, mod);
    }
    return NTT_OK_E;
}

ntt_err_ty ntt_pointwise_mul(uint32_t *dst, const uint32_t *lhs,
        const uint32_t *rhs, size_t len, const ntt_mod_ty *mod)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || (mod == NULL)) {
        return NTT_ERR_INVALID_E;
    }
    for (size_t i = 0U; i < len; i++) {
        dst[i] = ntt_mod_mul(lhs[i], rhs[i], mod);
    }
    return NTT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API：CRT 重构                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: Garner 递推：v = r1 + p1·t2 + p1·p2·t3 + …
 * note: 每一步 acc·t_i ≤ acc·(p_i−1) < Πp < 2^64，uint64 全程无回绕
 *       （契约：模数个数满足 Πp < 2^64）
 */
uint64_t ntt_crt_reconstruct_one(const uint32_t *residues,
        const ntt_mod_ty *const *mods, size_t mod_count)
{
    uint64_t v = (uint64_t)residues[0];
    uint64_t acc = 1U;  /* 进入第 i 轮前 acc = p0·…·p_{i−2}；初始为空积 */
    for (size_t i = 1U; i < mod_count; i++) {
        acc *= mods[i - 1U]->p;  /* 现在 acc = p0·…·p_{i−1} */
        const uint32_t pi = mods[i]->p;
        /* diff = (r_i − v) mod p_i；else 分支 r_i < vmod 保证不溢出 */
        const uint32_t vmod = (uint32_t)(v % pi);
        const uint32_t diff = (residues[i] >= vmod)
                ? (residues[i] - vmod) : (residues[i] + pi - vmod);
        const uint32_t acc_inv = ntt_mod_inv((uint32_t)(acc % pi), mods[i]);
        const uint32_t ti = ntt_mod_mul(diff, acc_inv, mods[i]);
        v += acc * ti;
    }
    return v;
}
