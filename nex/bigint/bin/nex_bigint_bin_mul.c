/*
 * nex_bigint_bin_mul.c：bigint_bin_ty 乘法实现。
 *
 * 职责（设计文档 §4.2.4、§4.3）：schoolbook 朴素乘法与 Karatsuba 分治乘法，
 * 以及带方法选择的分派入口 bigint_bin_mul_ex / bigint_bin_mul。
 *
 * 算法分派：
 *   - AUTO：按肢数阈值自动选择（< 32 肢 schoolbook；32..NTT_CUTOFF 切
 *     Karatsuba；≥ NTT_CUTOFF 且长度和 ≤ 2^26 切多模数 CRT NTT）；
 *   - SCHOOLBOOK / KARATSUBA：强制对应算法（params 同前）；
 *   - MULTI_MODULI_CRT_NTT：强制多模数 CRT NTT（mod_count 0 或 2，
 *     其余 UNSUPPORTED）；
 *   - 其余已登记算法（Toom-Cook、FFT、SS）当前版本未实现 →
 *     BIGINT_ERR_UNSUPPORTED_E；未知算法标签 → BIGINT_ERR_INVALID_E。
 *
 * 规范化不变式与别名约定同头文件。本文件所有内部函数仅处理幅值，
 * 符号由顶层分派统一设置；幅值中间量一律经 uint64_t 显式计算（§4.3）。
 */

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_alloc.h"
#include "nex/ntt/nex_ntt.h"

#include <stdlib.h>
#include <string.h>

/* 自适应阈值：两操作数肢数均达到该值启用 Karatsuba；也作 Karatsuba 递归阈值 */
#define NEX_MUL_AUTO_CUTOFF 32U

/* NTT 乘法（设计文档 §4.3）：AUTO 切换阈值，实测标定——Karatsuba 交叉点
 * 约 13K 肢（-O2，双 30-bit 模数 + Montgomery 模乘），取 2^14 留余量 */
#define NEX_MUL_NTT_CUTOFF 16384U
/* 长度和上限：保证 log2n ≤ 27（内置模数最大 c）；系数上界亦自动满足（见 mul_ntt） */
#define NEX_MUL_NTT_MAX_SUM (1U << 26U)

/* ------------------------------------------------------------------ */
/* 内部辅助：容量与规范化（算法文件自包含，不依赖 bin.c 的 static 函数）      */
/* ------------------------------------------------------------------ */

/*
 * brief: 确保 val 的容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty mul_ensure_cap(bigint_bin_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)nex_realloc(val->limbs,
            new_cap * sizeof(uint32_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

/*
 * brief: 去除高位零肢；len 变为 0 时符号规范化为 ZERO
 */
static void mul_normalize(bigint_bin_ty *val)
{
    while ((val->len > 0U) && (val->limbs[val->len - 1U] == 0U)) {
        val->len--;
    }
    if (val->len == 0U) {
        val->sign = BIGINT_SIGN_ZERO_E;
    }
}

/* ------------------------------------------------------------------ */
/* 内部辅助：幅值加法 / 减法                                               */
/* ------------------------------------------------------------------ */

/*
 * brief: 幅值加 dst = |lhs| + |rhs|（dst 与 src 不别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 结果天然规范化；src 可为"视图"对象（cap == 0 亦可）
 */
static bigint_err_ty mag_add(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    const bigint_err_ty err = mul_ensure_cap(dst, max_len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    uint64_t carry = 0U;
    size_t idx = 0U;
    for (; idx < max_len; idx++) {
        const uint64_t a = (idx < lhs->len) ? lhs->limbs[idx] : 0U;
        const uint64_t b = (idx < rhs->len) ? rhs->limbs[idx] : 0U;
        const uint64_t sum = a + b + carry;
        dst->limbs[idx] = (uint32_t)sum;
        carry = sum >> 32U;
    }
    dst->len = max_len;
    if (carry > 0U) {
        dst->limbs[max_len] = (uint32_t)carry;
        dst->len = max_len + 1U;
    }
    mul_normalize(dst);
    return BIGINT_OK_E;
}

/*
 * brief: 幅值减 dst = |lhs| - |rhs|，要求 |lhs| >= |rhs|（dst 与 src 不别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 结果规范化为非负（数学上差非负）
 */
static bigint_err_ty mag_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    const bigint_err_ty err = mul_ensure_cap(dst, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }

    uint64_t borrow = 0U;
    size_t idx = 0U;
    for (; idx < lhs->len; idx++) {
        const uint64_t cur = lhs->limbs[idx];
        const uint64_t sub = (uint64_t)((idx < rhs->len) ? rhs->limbs[idx] : 0U)
                + borrow;
        if (cur >= sub) {
            dst->limbs[idx] = (uint32_t)(cur - sub);
            borrow = 0U;
        } else {
            dst->limbs[idx] = (uint32_t)((UINT64_C(1) << 32U) + cur - sub);
            borrow = 1U;
        }
    }
    dst->len = lhs->len;
    mul_normalize(dst);
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：schoolbook 幅值乘法                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 幅值乘 dst = |lhs| × |rhs|（dst 与 src 不别名；src 可为视图对象）
 * return: 恒为 BIGINT_OK_E；调用方须保证 dst 容量 >= lhs->len + rhs->len
 * note: lhs / rhs 任一为零时 dst 规范化为零
 */
static bigint_err_ty mul_schoolbook(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const bigint_bin_ty *narrow = lhs;
    const bigint_bin_ty *wide = rhs;
    if (narrow->len > wide->len) {
        const bigint_bin_ty *tmp = narrow;
        narrow = wide;
        wide = tmp;
    }

    const size_t n = narrow->len;
    const size_t m = wide->len;
    dst->len = n + m;
    memset(dst->limbs, 0, (n + m) * sizeof(uint32_t));

    for (size_t i = 0U; i < n; i++) {
        const uint32_t li = narrow->limbs[i];
        if (li == 0U) {
            continue;
        }
        uint64_t carry = 0U;
        size_t j = 0U;
        for (; j < m; j++) {
            const uint64_t cur = (uint64_t)li * wide->limbs[j]
                    + dst->limbs[i + j] + carry;
            dst->limbs[i + j] = (uint32_t)cur;
            carry = cur >> 32U;
        }
        size_t k = i + m;
        while ((carry > 0U) && (k < n + m)) {
            const uint64_t cur = (uint64_t)dst->limbs[k] + carry;
            dst->limbs[k] = (uint32_t)cur;
            carry = cur >> 32U;
            k++;
        }
    }
    mul_normalize(dst);
    return BIGINT_OK_E;
}

/*
 * brief: 把 src 加到 acc 的 off 起始处（带进位，就地）
 * param: acc  目标（len 预先置为容量上限，全零打底）
 * param: off  起始肢偏移
 * param: src  源幅值
 * note: 数学上 acc 足够大，进位不会越出 acc->len；if 为防御性护栏
 */
static bigint_err_ty mul_schoolbook_checked(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

static void mag_add_at(bigint_bin_ty *acc, size_t off,
        const bigint_bin_ty *src)
{
    if (src->len == 0U) {
        return;
    }
    uint64_t carry = 0U;
    size_t idx = 0U;
    for (; idx < src->len; idx++) {
        if (off + idx >= acc->len) {
            break;
        }
        const uint64_t sum = (uint64_t)acc->limbs[off + idx]
                + src->limbs[idx] + carry;
        acc->limbs[off + idx] = (uint32_t)sum;
        carry = sum >> 32U;
    }
    size_t k = off + idx;
    while ((carry > 0U) && (k < acc->len)) {
        const uint64_t sum = (uint64_t)acc->limbs[k] + carry;
        acc->limbs[k] = (uint32_t)sum;
        carry = sum >> 32U;
        k++;
    }
}

/* ------------------------------------------------------------------ */
/* 内部辅助：Karatsuba 幅值乘法                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 幅值乘 dst = |lhs| × |rhs|（Karatsuba 分治；dst 与 src 不别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 递归到 min(肢数) <= cutoff 时切换 schoolbook；lhs / rhs 可为视图对象
 */
static bigint_err_ty mul_karatsuba(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs, size_t cutoff)
{
    if (lhs->len < rhs->len) {
        const bigint_bin_ty *swap = lhs;
        lhs = rhs;
        rhs = swap;
    }
    if ((rhs->len <= cutoff) || (lhs->len == 0U) || (rhs->len == 0U)) {
        const bigint_err_ty err = mul_ensure_cap(dst, lhs->len + rhs->len);
        if (err != BIGINT_OK_E) {
            return err;
        }
        return mul_schoolbook(dst, lhs, rhs);
    }

    const size_t m = lhs->len / 2U;
    const size_t c_len = (rhs->len > m) ? rhs->len - m : 0U;

    /* 视图对象：只读引用于原操作数切片，不为它们分配或释放 */
    bigint_bin_ty a;  /* 高半（含最高肢，恒非零） */
    bigint_bin_ty b;  /* 低 m 肢 */
    bigint_bin_ty c;  /* rhs 的高半；rhs->len <= m 时为空 */
    bigint_bin_ty d;  /* rhs 的低半 */
    a.sign = BIGINT_SIGN_POS_E;
    a.limbs = lhs->limbs + m;
    a.len = lhs->len - m;
    a.cap = 0U;
    b.sign = BIGINT_SIGN_POS_E;
    b.limbs = lhs->limbs;
    b.len = m;
    b.cap = 0U;
    c.sign = (c_len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    c.limbs = (c_len > 0U) ? (rhs->limbs + m) : rhs->limbs;
    c.len = c_len;
    c.cap = 0U;
    d.sign = BIGINT_SIGN_POS_E;
    d.limbs = rhs->limbs;
    d.len = (rhs->len < m) ? rhs->len : m;
    d.cap = 0U;

    bigint_bin_ty z0;  /* b × d */
    bigint_bin_ty z1;  /* (a+b)(c+d) - z0 - z2 */
    bigint_bin_ty z2;  /* a × c */
    bigint_bin_ty s;   /* a + b */
    bigint_bin_ty t;   /* c + d */
    bigint_bin_ty acc; /* 三段拼合结果 */
    bigint_err_ty err = bigint_bin_init(&z0);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin_init(&z1);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&z0);
        return err;
    }
    err = bigint_bin_init(&z2);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&z0);
        bigint_bin_free(&z1);
        return err;
    }
    err = bigint_bin_init(&s);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&z0);
        bigint_bin_free(&z1);
        bigint_bin_free(&z2);
        return err;
    }
    err = bigint_bin_init(&t);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&z0);
        bigint_bin_free(&z1);
        bigint_bin_free(&z2);
        bigint_bin_free(&s);
        return err;
    }
    err = bigint_bin_init(&acc);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&z0);
        bigint_bin_free(&z1);
        bigint_bin_free(&z2);
        bigint_bin_free(&s);
        bigint_bin_free(&t);
        return err;
    }

    bigint_err_ty rc = BIGINT_OK_E;
    if (rc == BIGINT_OK_E) {
        rc = mul_karatsuba(&z2, &a, &c, cutoff);
    }
    if (rc == BIGINT_OK_E) {
        rc = mul_karatsuba(&z0, &b, &d, cutoff);
    }
    if (rc == BIGINT_OK_E) {
        rc = mag_add(&s, &a, &b);
    }
    if (rc == BIGINT_OK_E) {
        rc = mag_add(&t, &c, &d);
    }
    if (rc == BIGINT_OK_E) {
        rc = mul_karatsuba(&z1, &s, &t, cutoff);
    }
    if (rc == BIGINT_OK_E) {
        rc = mag_sub(&z1, &z1, &z0);
    }
    if (rc == BIGINT_OK_E) {
        rc = mag_sub(&z1, &z1, &z2);
    }
    if (rc == BIGINT_OK_E) {
        rc = mul_ensure_cap(&acc, lhs->len + rhs->len);
    }
    if (rc == BIGINT_OK_E) {
        acc.len = lhs->len + rhs->len;
        memset(acc.limbs, 0, acc.len * sizeof(uint32_t));
        mag_add_at(&acc, 0U, &z0);
        mag_add_at(&acc, m, &z1);
        mag_add_at(&acc, 2U * m, &z2);
        mul_normalize(&acc);
        acc.sign = BIGINT_SIGN_POS_E;
        bigint_bin_move(dst, &acc);
    }

    bigint_bin_free(&z0);
    bigint_bin_free(&z1);
    bigint_bin_free(&z2);
    bigint_bin_free(&s);
    bigint_bin_free(&t);
    if (rc != BIGINT_OK_E) {
        bigint_bin_free(&acc);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：多模数 CRT NTT 乘法（设计文档 §4.3）                        */
/* ------------------------------------------------------------------ */

/*
 * brief: NTT 长度对数 log2n = ceil(log2(2s−1))，s = 两操作数肢数和
 * note: 16-bit 分节（2 节/肢）下卷积长度 = 2(n+m) − 1 = 2s−1
 */
static uint32_t mul_ntt_log2n(size_t s)
{
    uint32_t log2n = 0U;
    while (((size_t)1U << log2n) < (2U * s - 1U)) {
        log2n++;
    }
    return log2n;
}

/*
 * brief: 选取两个 c ≥ log2n 的内置模数（设计文档 §4.3：每个模数 2^c ≥ N）
 * return: 找到返回 BIGINT_OK_E；内置表不足以覆盖变换长度返回
 *         BIGINT_ERR_UNSUPPORTED_E
 */
static bigint_err_ty mul_ntt_pick_mods(uint32_t log2n,
        const ntt_mod_ty *mods[2])
{
    size_t found = 0U;
    const size_t count = ntt_mod_count();
    for (size_t i = 0U; i < count; i++) {
        const ntt_mod_ty *mod = ntt_mod_get(i);
        if (mod->c >= log2n) {
            mods[found] = mod;
            found++;
            if (found == 2U) {
                return BIGINT_OK_E;
            }
        }
    }
    return BIGINT_ERR_UNSUPPORTED_E;
}

/*
 * brief: 幅值拆 16-bit 节并零填充至长度 n（2 节/肢，设计文档 §4.3 默认分节）
 */
static void mul_ntt_pack(uint32_t *out, size_t n, const bigint_bin_ty *v)
{
    for (size_t i = 0U; i < n; i++) {
        out[i] = 0U;
    }
    for (size_t i = 0U; i < v->len; i++) {
        const uint32_t limb = v->limbs[i];
        out[2U * i] = limb & 0xFFFFU;
        out[2U * i + 1U] = limb >> 16U;
    }
}

/*
 * brief: 幅值乘 dst = |lhs| × |rhs|（多模数 CRT NTT）
 * note: 双 30-bit 模数 + Garner 重构；要求 s = len(lhs)+len(rhs) ≤ 2^26
 *       （log2n ≤ 27）。系数上界 C ≤ 2·min(n,m)·(2^16−1)² ≤ 2^58 < Πp
 *       （内置最小两模数之积 ≈ 2^59.8），CRT 重构唯一且全程 uint64 无回绕。
 *       dst 与 src 不别名（调用方保证）；零操作数自然退化为零结果
 */
static bigint_err_ty mul_ntt(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    const size_t s = lhs->len + rhs->len;
    if (s == 0U) {
        /* 零 × 零：规范化为零 */
        dst->len = 0U;
        mul_normalize(dst);
        return BIGINT_OK_E;
    }
    if (s > NEX_MUL_NTT_MAX_SUM) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }
    const uint32_t log2n = mul_ntt_log2n(s);
    const uint32_t n = 1U << log2n;

    const ntt_mod_ty *mods[2];
    if (mul_ntt_pick_mods(log2n, mods) != BIGINT_OK_E) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }

    uint32_t *a = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *b = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *c1 = (uint32_t *)malloc(n * sizeof(uint32_t));
    if ((a == NULL) || (b == NULL) || (c1 == NULL)) {
        free(a);
        free(b);
        free(c1);
        return BIGINT_ERR_OOM_E;
    }

    bigint_err_ty err = mul_ensure_cap(dst, s);
    if (err != BIGINT_OK_E) {
        free(a);
        free(b);
        free(c1);
        return err;
    }

    ntt_err_ty nerr = NTT_OK_E;
    for (uint32_t m = 0U; m < 2U; m++) {
        const ntt_mod_ty *mod = mods[m];
        mul_ntt_pack(a, n, lhs);
        mul_ntt_pack(b, n, rhs);
        nerr = ntt_forward(a, log2n, mod);
        if (nerr == NTT_OK_E) {
            nerr = ntt_forward(b, log2n, mod);
        }
        if (nerr == NTT_OK_E) {
            nerr = ntt_pointwise_mul(a, a, b, n, mod);
        }
        if (nerr == NTT_OK_E) {
            nerr = ntt_inverse(a, log2n, mod);
        }
        if (nerr != NTT_OK_E) {
            break;
        }
        if (m == 0U) {
            memcpy(c1, a, n * sizeof(uint32_t));
        } else {
            /* Garner 重构（预计算 p1^{-1} mod p2，一次逆元全系数共享）
               + base-2^16 进位，直接落输出肢 */
            const uint32_t p2 = mods[1]->p;
            const uint32_t p1_inv = ntt_mod_inv(
                    (uint32_t)((uint64_t)mods[0]->p % p2), mods[1]);
            uint64_t carry = 0U;
            for (uint32_t k = 0U; k < 2U * s; k++) {
                uint64_t coeff;
                if (k < 2U * s - 1U) {
                    /* v = r1 + p1·t2，t2 = (r2 − r1)·p1^{-1} mod p2；
                       v < p1·p2 < 2^64，uint64 无回绕 */
                    const uint32_t r1 = c1[k];
                    const uint32_t r2 = a[k];
                    const uint32_t r1_mod = (uint32_t)((uint64_t)r1 % p2);
                    const uint32_t diff = (r2 >= r1_mod)
                            ? (r2 - r1_mod) : (r2 + p2 - r1_mod);
                    const uint32_t t2 = ntt_mod_mul(diff, p1_inv, mods[1]);
                    coeff = (uint64_t)r1 + (uint64_t)mods[0]->p * t2;
                } else {
                    coeff = 0U;  /* 末位仅承接进位 */
                }
                const uint64_t acc = coeff + carry;
                const uint32_t digit = (uint32_t)(acc & 0xFFFFU);
                carry = acc >> 16U;
                if ((k & 1U) == 0U) {
                    dst->limbs[k >> 1U] = digit;
                } else {
                    dst->limbs[k >> 1U] |= digit << 16U;
                }
            }
            dst->len = s;
            mul_normalize(dst);
            dst->sign = BIGINT_SIGN_POS_E;
        }
    }

    free(a);
    free(b);
    free(c1);
    if (nerr != NTT_OK_E) {
        /* 模数与 log2n 已预校验，此路径理论不可达；防御性映射 */
        return BIGINT_ERR_INVALID_E;
    }
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 方法分派与符号处理                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 由两操作数计算乘积符号；任一为零则整体为零
 */
static bigint_sign_ty product_sign(const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    if ((lhs->len == 0U) || (rhs->len == 0U)) {
        return BIGINT_SIGN_ZERO_E;
    }
    return (lhs->sign == rhs->sign) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_NEG_E;
}

/*
 * brief: 解析方法选择并执行幅值乘（不做别名规避；由调用方保证 dst 独立）
 */
static bigint_err_ty mul_dispatch(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs,
        const bigint_mul_method_ty *method)
{
    const bigint_mul_algo_ty algo = (method != NULL)
            ? method->algo : BIGINT_MUL_AUTO_E;
    switch (algo) {
    case BIGINT_MUL_AUTO_E: {
        const size_t min_len = (lhs->len < rhs->len) ? lhs->len : rhs->len;
        if ((min_len >= NEX_MUL_NTT_CUTOFF)
                && (lhs->len + rhs->len <= NEX_MUL_NTT_MAX_SUM)) {
            return mul_ntt(dst, lhs, rhs);
        }
        const size_t use_ka = ((lhs->len >= NEX_MUL_AUTO_CUTOFF)
                && (rhs->len >= NEX_MUL_AUTO_CUTOFF)) ? 1U : 0U;
        return (use_ka != 0U) ? mul_karatsuba(dst, lhs, rhs,
                NEX_MUL_AUTO_CUTOFF) : mul_schoolbook_checked(dst, lhs, rhs);
    }
    case BIGINT_MUL_SCHOOLBOOK_E:
        return mul_schoolbook_checked(dst, lhs, rhs);
    case BIGINT_MUL_KARATSUBA_E: {
        size_t cutoff = (method != NULL)
                ? method->params.karatsuba.cutoff : 0U;
        if (cutoff == 0U) {
            cutoff = NEX_MUL_AUTO_CUTOFF;
        }
        return mul_karatsuba(dst, lhs, rhs, cutoff);
    }
    case BIGINT_MUL_MULTI_MODULI_CRT_NTT_E: {
        const uint32_t mod_count = (method != NULL)
                ? method->params.multi_moduli_crt_ntt.mod_count : 0U;
        if ((mod_count != 0U) && (mod_count != 2U)) {
            return BIGINT_ERR_UNSUPPORTED_E;  /* v1 仅支持默认双模数 */
        }
        return mul_ntt(dst, lhs, rhs);
    }
    case BIGINT_MUL_TOOM_COOK_E:
    case BIGINT_MUL_FLOAT_COMPLEX_FFT_E:
    case BIGINT_MUL_SCHONHAGE_STRASSEN_E:
        return BIGINT_ERR_UNSUPPORTED_E;
    default:
        return BIGINT_ERR_INVALID_E;
    }
}

/*
 * brief: schoolbook 调用封装：先确保输出容量（含零操作数情形）
 */
static bigint_err_ty mul_schoolbook_checked(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const bigint_err_ty err = mul_ensure_cap(dst, lhs->len + rhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    return mul_schoolbook(dst, lhs, rhs);
}

/* ------------------------------------------------------------------ */
/* 对外 API                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 带方法选择的大整数乘法（详见头文件注释）
 */
bigint_err_ty bigint_bin_mul_ex(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, const bigint_mul_method_ty *method)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }

    bigint_bin_ty tmp;
    bigint_err_ty err = bigint_bin_init(&tmp);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = mul_dispatch(&tmp, lhs, rhs, method);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return err;
    }

    tmp.sign = product_sign(lhs, rhs);
    bigint_bin_move(dst, &tmp);
    return BIGINT_OK_E;
}

/*
 * brief: 乘法，等价于 { BIGINT_MUL_AUTO_E, {0} } 的 mul_ex
 */
bigint_err_ty bigint_bin_mul(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    return bigint_bin_mul_ex(dst, lhs, rhs, NULL);
}