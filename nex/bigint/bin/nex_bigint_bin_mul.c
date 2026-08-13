/*
 * nex_bigint_bin_mul.c：bigint_bin_ty 乘法实现。
 *
 * 职责（设计文档 §4.2.4、§4.3）：schoolbook 朴素乘法与 Karatsuba 分治乘法，
 * 以及带方法选择的分派入口 bigint_bin_mul_ex / bigint_bin_mul。
 *
 * 算法分派：
 *   - AUTO：按肢数阈值自动选择（< 32 肢 schoolbook；32..TOOM_CUTOFF 切
 *     Karatsuba；≥ TOOM_CUTOFF 切 Toom-3；≥ NTT_CUTOFF 且长度和 ≤ 2^26
 *     切多模数 CRT NTT；浮点 FFT 实测仅窄带占优，不参与 AUTO，§4.3）；
 *   - SCHOOLBOOK / KARATSUBA：强制对应算法（params 同前）；
 *   - FLOAT_COMPLEX_FFT：强制浮点 FFT（chunk_bits 0/8/16，其余 INVALID）；
 *   - MULTI_MODULI_CRT_NTT：强制多模数 CRT NTT（mod_count 0 或 2，
 *     其余 UNSUPPORTED）；
 *   - 其余已登记算法（Toom-Cook、SS）当前版本未实现 →
 *     BIGINT_ERR_UNSUPPORTED_E；未知算法标签 → BIGINT_ERR_INVALID_E。
 *
 * 规范化不变式与别名约定同头文件。本文件所有内部函数仅处理幅值，
 * 符号由顶层分派统一设置；幅值中间量一律经 uint64_t 显式计算（§4.3）。
 */

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/fft/nex_fft.h"
#include "nex/nex_alloc.h"
#include "nex/ntt/nex_ntt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 自适应阈值：两操作数肢数均达到该值启用 Karatsuba；也作 Karatsuba 递归阈值 */
#define NEX_MUL_AUTO_CUTOFF 32U

/* FFT 乘法（设计文档 §4.3）：仅强制方法（mul_ex）；实测标量 double 下
 * 仅在 ~512² 肢窄带胜过 Karatsuba（~10%），AUTO 不采用（见 §4.3 记录） */
/* 16-bit 节的安全规模上限（长度和）：舍入误差保守界 < 0.5（见 mul_fft） */
#define NEX_MUL_FFT_B16_MAX_SUM 1024U

/* NTT 乘法（设计文档 §4.3）：AUTO 切换阈值，实测标定——Karatsuba 交叉点
 * 约 13K 肢（-O2，双 30-bit 模数 + Montgomery 模乘），取 2^14 留余量 */
#define NEX_MUL_NTT_CUTOFF 16384U
/* Toom-3 乘法：AUTO 切换阈值，实测标定——Karatsuba 交叉点约 600 肢，
 * 取 512 留余量（-O2） */
#define NEX_MUL_TOOM_CUTOFF 512U
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
    /* 变换长度须容纳两侧节数（每肢 2 节）与卷积长度 2s−1：零操作数时
       2s−1 < 2·max_len，取两者较大（修复零×单肢的拆节越界，ASan 捕获） */
    const size_t chunks_side = 2U * ((lhs->len > rhs->len) ? lhs->len : rhs->len);
    const size_t need = (2U * s - 1U > chunks_side) ? (2U * s - 1U) : chunks_side;
    uint32_t log2n = 0U;
    while ((log2n < 31U) && (((size_t)1U << log2n) < need)) {
        log2n++;
    }
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
/* 内部辅助：浮点复数 FFT 乘法（设计文档 §4.3）                         */
/* ------------------------------------------------------------------ */

/*
 * brief: 幅值拆 chunk_bits 位节并零填充至长度 n（交错 double，虚部恒 0）
 */
static void mul_fft_pack(double *out, size_t n, const bigint_bin_ty *v,
        uint32_t chunk_bits)
{
    const uint32_t per_limb = 32U / chunk_bits;
    const uint32_t mask = (1U << chunk_bits) - 1U;
    for (size_t i = 0U; i < 2U * n; i++) {
        out[i] = 0.0;
    }
    for (size_t i = 0U; i < v->len; i++) {
        uint32_t limb = v->limbs[i];
        for (uint32_t c = 0U; c < per_limb; c++) {
            out[2U * (per_limb * i + c)] = (double)(limb & mask);
            limb >>= chunk_bits;
        }
    }
}

/*
 * brief: 幅值乘 dst = |lhs| × |rhs|（浮点复数 FFT）
 * param: chunk_bits  0 = 自动（S ≤ 2^10 用 16 bit，否则 8 bit）；
 *                    8 / 16 强制
 * note: 舍入安全界（Higham FFT 误差界）：|error| ≈ 2·ε·(1+6·log2N)·C，
 *       C = L·(2^b−1)²。8-bit 节 N 可达 ~2^34 点；16-bit 节限 S ≤ 2^10
 *       （保守界，测试逐边界验证）。系数 < 2^53 故 (x+0.5) 截断即精确
 *       舍入；base-2^b 进位后按 32/b 个节拼一个肢。dst 与 src 不别名
 */
static bigint_err_ty mul_fft(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, uint32_t chunk_bits)
{
    const size_t s = lhs->len + rhs->len;
    if (s == 0U) {
        dst->len = 0U;
        mul_normalize(dst);
        return BIGINT_OK_E;
    }
    if (chunk_bits == 0U) {
        chunk_bits = (s <= NEX_MUL_FFT_B16_MAX_SUM) ? 16U : 8U;
    }
    const uint32_t per_limb = 32U / chunk_bits;
    const size_t l = per_limb * lhs->len;
    const size_t m = per_limb * rhs->len;
    /* 变换长度须容纳两侧节数与卷积长度 l+m−1：零操作数时 l+m−1 <
       单侧节数，取两者较大（修复零×单肢的拆节越界，ASan 捕获） */
    const size_t chunks_side = (l > m) ? l : m;
    const size_t need = (l + m - 1U > chunks_side) ? (l + m - 1U) : chunks_side;
    uint32_t log2n = 0U;
    while ((log2n < 31U) && (((size_t)1U << log2n) < need)) {
        log2n++;
    }
    if (log2n > 30U) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }
    const uint32_t n = 1U << log2n;

    double *a = (double *)malloc(2U * n * sizeof(double));
    double *b = (double *)malloc(2U * n * sizeof(double));
    if ((a == NULL) || (b == NULL)) {
        free(a);
        free(b);
        return BIGINT_ERR_OOM_E;
    }

    bigint_err_ty err = mul_ensure_cap(dst, s);
    if (err != BIGINT_OK_E) {
        free(a);
        free(b);
        return err;
    }

    mul_fft_pack(a, n, lhs, chunk_bits);
    mul_fft_pack(b, n, rhs, chunk_bits);
    fft_err_ty ferr = fft_forward(a, log2n);
    if (ferr == FFT_OK_E) {
        ferr = fft_forward(b, log2n);
    }
    if (ferr == FFT_OK_E) {
        ferr = fft_pointwise_mul(a, a, b, n);
    }
    if (ferr == FFT_OK_E) {
        ferr = fft_inverse(a, log2n);
    }
    if (ferr != FFT_OK_E) {
        free(a);
        free(b);
        return BIGINT_ERR_INVALID_E;  /* 参数已预校验，理论不可达 */
    }

    /* 精确舍入 + base-2^b 进位，直接落输出肢 */
    const uint32_t digits_total = (uint32_t)(l + m);
    const uint32_t mask = (1U << chunk_bits) - 1U;
    uint64_t carry = 0U;
    for (uint32_t k = 0U; k < digits_total; k++) {
        uint64_t coeff;
        if (k < (uint32_t)(l + m - 1U)) {
            coeff = (uint64_t)(long long)(a[2U * k] + 0.5);
        } else {
            coeff = 0U;  /* 末位仅承接进位 */
        }
        const uint64_t acc = coeff + carry;
        const uint32_t digit = (uint32_t)(acc & mask);
        carry = acc >> chunk_bits;
        const uint32_t limb_idx = k / per_limb;
        const uint32_t shift = chunk_bits * (k % per_limb);
        if (shift == 0U) {
            dst->limbs[limb_idx] = digit;
        } else {
            dst->limbs[limb_idx] |= digit << shift;
        }
    }
    dst->len = s;
    mul_normalize(dst);
    dst->sign = BIGINT_SIGN_POS_E;

    free(a);
    free(b);
    return BIGINT_OK_E;
}

/* 前置声明：由两操作数计算乘积符号（定义见方法分派段） */
static bigint_sign_ty product_sign(const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs);

/* ------------------------------------------------------------------ */
/* 内部辅助：带符号幅值运算（Toom-3 求值 / 插值用）                      */
/* ------------------------------------------------------------------ */

/*
 * brief: dst = |src| 幅值副本，符号保持（src 可为视图）
 */
static bigint_err_ty s_copy(bigint_bin_ty *dst, const bigint_bin_ty *src)
{
    bigint_err_ty err = mul_ensure_cap(dst, src->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (src->len > 0U) {
        memcpy(dst->limbs, src->limbs, src->len * sizeof(uint32_t));
    }
    dst->len = src->len;
    dst->sign = src->sign;
    return BIGINT_OK_E;
}

/*
 * brief: dst = lhs ± rhs（带符号；sub 为真时翻转 rhs 符号）
 * note: 源可为视图；结果非零时 sign ∈ {POS, NEG}
 */
static bigint_err_ty s_addsub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, bool sub)
{
    bigint_sign_ty rsign = rhs->sign;
    if (sub && (rsign != BIGINT_SIGN_ZERO_E)) {
        rsign = (rsign == BIGINT_SIGN_POS_E)
                ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }
    if (lhs->sign == BIGINT_SIGN_ZERO_E) {
        /* 0 ± rhs = ±rhs（sub 时取翻转后的 rsign） */
        bigint_err_ty err = s_copy(dst, rhs);
        if (err == BIGINT_OK_E) {
            dst->sign = rsign;
        }
        return err;
    }
    if (rsign == BIGINT_SIGN_ZERO_E) {
        return s_copy(dst, lhs);
    }
    if (lhs->sign == rsign) {
        bigint_err_ty err = mag_add(dst, lhs, rhs);
        if (err == BIGINT_OK_E) {
            dst->sign = lhs->sign;
        }
        return err;
    }
    const int c = bigint_bin_cmp_abs(lhs, rhs);
    if (c == 0) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    const bigint_bin_ty *big = (c > 0) ? lhs : rhs;
    const bigint_bin_ty *small = (c > 0) ? rhs : lhs;
    bigint_err_ty err = mag_sub(dst, big, small);
    if (err == BIGINT_OK_E) {
        dst->sign = (c > 0) ? lhs->sign : rsign;
    }
    return err;
}

/*
 * brief: dst = src × m（带符号，m 为 uint32）
 */
static bigint_err_ty s_mul_u32(bigint_bin_ty *dst, const bigint_bin_ty *src,
        uint32_t m)
{
    if ((m == 0U) || (src->sign == BIGINT_SIGN_ZERO_E)) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    bigint_err_ty err = mul_ensure_cap(dst, src->len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t carry = 0U;
    for (size_t i = 0U; i < src->len; i++) {
        const uint64_t cur = (uint64_t)src->limbs[i] * m + carry;
        dst->limbs[i] = (uint32_t)cur;
        carry = cur >> 32U;
    }
    dst->len = src->len;
    if (carry > 0U) {
        dst->limbs[src->len] = (uint32_t)carry;
        dst->len++;
    }
    dst->sign = src->sign;
    mul_normalize(dst);
    return BIGINT_OK_E;
}

/*
 * brief: dst = src / d（带符号，要求整除；d ≥ 1）
 * note: 幅值小除法，符号保持；调用方保证余数恰为零
 */
static bigint_err_ty s_div_u32(bigint_bin_ty *dst, const bigint_bin_ty *src,
        uint32_t d)
{
    if (src->sign == BIGINT_SIGN_ZERO_E) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    bigint_err_ty err = mul_ensure_cap(dst, src->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t rem = 0U;
    for (size_t i = src->len; i-- > 0U;) {
        const uint64_t cur = (rem << 32U) | src->limbs[i];
        dst->limbs[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    dst->len = src->len;
    dst->sign = src->sign;
    mul_normalize(dst);
    return BIGINT_OK_E;
}

/*
 * brief: 规范化视图：去除尾部零肢并同步符号（切片固定长度可能含前导零）
 * note: Toom 求值 / 比较要求操作数为规范化幅值（cmp_abs 先比 len）
 */
static void toom_view_norm(bigint_bin_ty *v)
{
    while ((v->len > 0U) && (v->limbs[v->len - 1U] == 0U)) {
        v->len--;
    }
    if (v->len == 0U) {
        v->sign = BIGINT_SIGN_ZERO_E;
    }
}

/* ------------------------------------------------------------------ */
/* 内部辅助：Toom-3 乘法（设计文档 §4.2.4）                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 幅值乘 dst = |lhs| × |rhs|（Toom-3，x = 2^(32m) 三点插值）
 * note: 拆分 a = a0+a1·x+a2·x²、b = b0+b1·x+b2·x²；在 x ∈ {0,1,−1,2,∞}
 *       求值后插值恢复 5 个系数；子乘积递归至 min(len) < cutoff 切
 *       Karatsuba。所有插值中间量为带符号整数（除以 2/6 恰整除）
 */
static bigint_err_ty mul_toom3(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, size_t cutoff)
{
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    const size_t m = (max_len + 2U) / 3U;  /* 每段肢数，3m ≥ max_len */

    /* 视图切片（sign 由长度决定；len 0 → ZERO） */
    bigint_bin_ty a0;
    bigint_bin_ty a1;
    bigint_bin_ty a2;
    bigint_bin_ty b0;
    bigint_bin_ty b1;
    bigint_bin_ty b2;
    a0.sign = BIGINT_SIGN_POS_E;
    a0.limbs = lhs->limbs;
    a0.len = (lhs->len < m) ? lhs->len : m;
    a0.cap = 0U;
    a1.sign = (lhs->len > m) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    a1.limbs = (lhs->len > m) ? (lhs->limbs + m) : lhs->limbs;
    a1.len = (lhs->len > 2U * m) ? m : ((lhs->len > m) ? lhs->len - m : 0U);
    a1.cap = 0U;
    a2.sign = (lhs->len > 2U * m) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    a2.limbs = (lhs->len > 2U * m) ? (lhs->limbs + 2U * m) : lhs->limbs;
    a2.len = (lhs->len > 2U * m) ? lhs->len - 2U * m : 0U;
    a2.cap = 0U;
    b0.sign = BIGINT_SIGN_POS_E;
    b0.limbs = rhs->limbs;
    b0.len = (rhs->len < m) ? rhs->len : m;
    b0.cap = 0U;
    b1.sign = (rhs->len > m) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    b1.limbs = (rhs->len > m) ? (rhs->limbs + m) : rhs->limbs;
    b1.len = (rhs->len > 2U * m) ? m : ((rhs->len > m) ? rhs->len - m : 0U);
    b1.cap = 0U;
    b2.sign = (rhs->len > 2U * m) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    b2.limbs = (rhs->len > 2U * m) ? (rhs->limbs + 2U * m) : rhs->limbs;
    b2.len = (rhs->len > 2U * m) ? rhs->len - 2U * m : 0U;
    b2.cap = 0U;
    /* 切片可能含前导零：规范化（比较与求值要求） */
    toom_view_norm(&a0);
    toom_view_norm(&a1);
    toom_view_norm(&a2);
    toom_view_norm(&b0);
    toom_view_norm(&b1);
    toom_view_norm(&b2);

    /* 临时对象池 */
    bigint_bin_ty t1;
    bigint_bin_ty t2;
    bigint_bin_ty av1;
    bigint_bin_ty avm1;
    bigint_bin_ty av2;
    bigint_bin_ty bv1;
    bigint_bin_ty bvm1;
    bigint_bin_ty bv2;
    bigint_bin_ty e0;
    bigint_bin_ty e1;
    bigint_bin_ty em;
    bigint_bin_ty e2;
    bigint_bin_ty e4;
    bigint_bin_ty c0;
    bigint_bin_ty c1;
    bigint_bin_ty c2;
    bigint_bin_ty c3;
    bigint_bin_ty c4;
    bigint_err_ty err = bigint_bin_init(&t1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&t2);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&av1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&avm1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&av2);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&bv1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&bvm1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&bv2);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&e0);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&e1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&em);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&e2);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&e4);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&c0);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&c1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&c2);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&c3);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&c4);
    if (err != BIGINT_OK_E) {
        /* 部分初始化的对象 free 安全 */
        bigint_bin_free(&c4);
        bigint_bin_free(&c3);
        bigint_bin_free(&c2);
        bigint_bin_free(&c1);
        bigint_bin_free(&c0);
        bigint_bin_free(&e4);
        bigint_bin_free(&e2);
        bigint_bin_free(&em);
        bigint_bin_free(&e1);
        bigint_bin_free(&e0);
        bigint_bin_free(&bv2);
        bigint_bin_free(&bvm1);
        bigint_bin_free(&bv1);
        bigint_bin_free(&av2);
        bigint_bin_free(&avm1);
        bigint_bin_free(&av1);
        bigint_bin_free(&t2);
        bigint_bin_free(&t1);
        return err;
    }

    /* 求值：a(0)=a0, a(1)=a0+a1+a2, a(−1)=a0−a1+a2, a(2)=a0+2a1+4a2 */
    if (err == BIGINT_OK_E) err = s_addsub(&av1, &a0, &a1, false);
    if (err == BIGINT_OK_E) err = s_addsub(&av1, &av1, &a2, false);
    if (err == BIGINT_OK_E) err = s_addsub(&avm1, &a0, &a2, false);
    if (err == BIGINT_OK_E) err = s_addsub(&avm1, &avm1, &a1, true);
    if (err == BIGINT_OK_E) err = s_mul_u32(&t1, &a1, 2U);
    if (err == BIGINT_OK_E) err = s_addsub(&av2, &a0, &t1, false);
    if (err == BIGINT_OK_E) err = s_mul_u32(&t2, &a2, 4U);
    if (err == BIGINT_OK_E) err = s_addsub(&av2, &av2, &t2, false);
    if (err == BIGINT_OK_E) err = s_addsub(&bv1, &b0, &b1, false);
    if (err == BIGINT_OK_E) err = s_addsub(&bv1, &bv1, &b2, false);
    if (err == BIGINT_OK_E) err = s_addsub(&bvm1, &b0, &b2, false);
    if (err == BIGINT_OK_E) err = s_addsub(&bvm1, &bvm1, &b1, true);
    if (err == BIGINT_OK_E) err = s_mul_u32(&t1, &b1, 2U);
    if (err == BIGINT_OK_E) err = s_addsub(&bv2, &b0, &t1, false);
    if (err == BIGINT_OK_E) err = s_mul_u32(&t2, &b2, 4U);
    if (err == BIGINT_OK_E) err = s_addsub(&bv2, &bv2, &t2, false);

    /* 5 个乘积（子递归：min(len) < cutoff 切 Karatsuba） */
    if (err == BIGINT_OK_E) {
        if (a0.len <= cutoff || b0.len <= cutoff) {
            err = mul_karatsuba(&e0, &a0, &b0, NEX_MUL_AUTO_CUTOFF);
        } else {
            err = mul_toom3(&e0, &a0, &b0, cutoff);
        }
    }
    if (err == BIGINT_OK_E) {
        if (av1.len <= cutoff || bv1.len <= cutoff) {
            err = mul_karatsuba(&e1, &av1, &bv1, NEX_MUL_AUTO_CUTOFF);
        } else {
            err = mul_toom3(&e1, &av1, &bv1, cutoff);
        }
    }
    if (err == BIGINT_OK_E) {
        if (avm1.len <= cutoff || bvm1.len <= cutoff) {
            err = mul_karatsuba(&em, &avm1, &bvm1, NEX_MUL_AUTO_CUTOFF);
        } else {
            err = mul_toom3(&em, &avm1, &bvm1, cutoff);
        }
    }
    if (err == BIGINT_OK_E) {
        if (av2.len <= cutoff || bv2.len <= cutoff) {
            err = mul_karatsuba(&e2, &av2, &bv2, NEX_MUL_AUTO_CUTOFF);
        } else {
            err = mul_toom3(&e2, &av2, &bv2, cutoff);
        }
    }
    if (err == BIGINT_OK_E) {
        if (a2.len <= cutoff || b2.len <= cutoff) {
            err = mul_karatsuba(&e4, &a2, &b2, NEX_MUL_AUTO_CUTOFF);
        } else {
            err = mul_toom3(&e4, &a2, &b2, cutoff);
        }
    }
    if (err == BIGINT_OK_E) e0.sign = product_sign(&a0, &b0);
    if (err == BIGINT_OK_E) e1.sign = product_sign(&av1, &bv1);
    if (err == BIGINT_OK_E) em.sign = product_sign(&avm1, &bvm1);
    if (err == BIGINT_OK_E) e2.sign = product_sign(&av2, &bv2);
    if (err == BIGINT_OK_E) e4.sign = product_sign(&a2, &b2);

    /* 插值（带符号）
       c0 = e0
       c4 = e4
       c2 = (e1+em)/2 − c0 − c4
       c3 = (e2 − c0 − 4c2 − 16c4 − e1 + em)/6
       c1 = (e1−em)/2 − c3 */
    if (err == BIGINT_OK_E) err = s_copy(&c0, &e0);
    if (err == BIGINT_OK_E) err = s_copy(&c4, &e4);
    if (err == BIGINT_OK_E) err = s_addsub(&t1, &e1, &em, false);
    if (err == BIGINT_OK_E) err = s_div_u32(&c2, &t1, 2U);
    if (err == BIGINT_OK_E) err = s_addsub(&c2, &c2, &c0, true);
    if (err == BIGINT_OK_E) err = s_addsub(&c2, &c2, &c4, true);
    if (err == BIGINT_OK_E) err = s_addsub(&t2, &e1, &em, true);
    if (err == BIGINT_OK_E) err = s_div_u32(&t1, &t2, 2U);
    if (err == BIGINT_OK_E) err = s_copy(&c1, &t1);
    if (err == BIGINT_OK_E) err = s_copy(&t2, &e2);
    if (err == BIGINT_OK_E) err = s_addsub(&t2, &t2, &c0, true);
    if (err == BIGINT_OK_E) err = s_mul_u32(&t1, &c2, 4U);
    if (err == BIGINT_OK_E) err = s_addsub(&t2, &t2, &t1, true);
    if (err == BIGINT_OK_E) err = s_mul_u32(&t1, &c4, 16U);
    if (err == BIGINT_OK_E) err = s_addsub(&t2, &t2, &t1, true);
    if (err == BIGINT_OK_E) err = s_addsub(&t2, &t2, &e1, true);
    if (err == BIGINT_OK_E) err = s_addsub(&t2, &t2, &em, false);
    if (err == BIGINT_OK_E) err = s_div_u32(&c3, &t2, 6U);
    if (err == BIGINT_OK_E) err = s_addsub(&c1, &c1, &c3, true);

    /* 组装：dst = c0 + c1·x + c2·x² + c3·x³ + c4·x⁴（系数均非负） */
    if (err == BIGINT_OK_E) {
        err = mul_ensure_cap(dst, lhs->len + rhs->len);
    }
    if (err == BIGINT_OK_E) {
        dst->len = lhs->len + rhs->len;
        memset(dst->limbs, 0, dst->len * sizeof(uint32_t));
        mag_add_at(dst, 0U, &c0);
        mag_add_at(dst, m, &c1);
        mag_add_at(dst, 2U * m, &c2);
        mag_add_at(dst, 3U * m, &c3);
        mag_add_at(dst, 4U * m, &c4);
        mul_normalize(dst);
        dst->sign = BIGINT_SIGN_POS_E;
    }

    bigint_bin_free(&c4);
    bigint_bin_free(&c3);
    bigint_bin_free(&c2);
    bigint_bin_free(&c1);
    bigint_bin_free(&c0);
    bigint_bin_free(&e4);
    bigint_bin_free(&e2);
    bigint_bin_free(&em);
    bigint_bin_free(&e1);
    bigint_bin_free(&e0);
    bigint_bin_free(&bv2);
    bigint_bin_free(&bvm1);
    bigint_bin_free(&bv1);
    bigint_bin_free(&av2);
    bigint_bin_free(&avm1);
    bigint_bin_free(&av1);
    bigint_bin_free(&t2);
    bigint_bin_free(&t1);
    return err;
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
        if (min_len >= NEX_MUL_TOOM_CUTOFF) {
            return mul_toom3(dst, lhs, rhs, NEX_MUL_TOOM_CUTOFF);
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
    case BIGINT_MUL_FLOAT_COMPLEX_FFT_E: {
        const uint32_t chunk_bits = (method != NULL)
                ? method->params.float_complex_fft.chunk_bits : 0U;
        if ((chunk_bits != 0U) && (chunk_bits != 8U) && (chunk_bits != 16U)) {
            return BIGINT_ERR_INVALID_E;
        }
        return mul_fft(dst, lhs, rhs, chunk_bits);
    }
    case BIGINT_MUL_MULTI_MODULI_CRT_NTT_E: {
        const uint32_t mod_count = (method != NULL)
                ? method->params.multi_moduli_crt_ntt.mod_count : 0U;
        if ((mod_count != 0U) && (mod_count != 2U)) {
            return BIGINT_ERR_UNSUPPORTED_E;  /* v1 仅支持默认双模数 */
        }
        return mul_ntt(dst, lhs, rhs);
    }
    case BIGINT_MUL_TOOM_COOK_E: {
        const uint32_t k = (method != NULL)
                ? method->params.toom_cook.k : 0U;
        if ((k != 0U) && (k != 3U)) {
            return BIGINT_ERR_UNSUPPORTED_E;  /* v1 仅支持 Toom-3 */
        }
        size_t cutoff = (method != NULL)
                ? method->params.toom_cook.cutoff : 0U;
        if (cutoff == 0U) {
            cutoff = NEX_MUL_TOOM_CUTOFF;
        }
        return mul_toom3(dst, lhs, rhs, cutoff);
    }
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