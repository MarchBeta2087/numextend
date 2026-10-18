/*
 * nex_bigint_bin64_str.c：64 位肢大整数字符串 I/O（进制 2..36）。
 *
 * 语义与 bigint_bin_from_str / bigint_bin_to_str 一致：可选 '-' 符号，
 * 16 进制允许 "0x"/"0X"、2 进制允许 "0b"/"0B" 前缀；解析部分消费容错；
 * 失败时输出对象不变。转换全程按幅值进行，符号在顶层统一设置。
 *
 * 算法：
 *   - 解析：按 base^k（不超过 2^64−1 的最大幂）分块累加，每块做一次
 *     “乘 group_mul + 加块值”；
 *   - 输出：2 的幂进制（2/4/8/16/32）按位切片 O(n)；其余进制用 uint64
 *     分块反复除法（每块 k 位）。
 */

#include "nex/bigint/bin64/nex_bigint_bin64.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

static const char B64_ALPHA[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

static bigint_err_ty s64_ensure(bigint_bin64_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }
    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint64_t)) {
        return BIGINT_ERR_OOM_E;
    }
    uint64_t *new_limbs = (uint64_t *)nex_realloc(val->limbs,
            new_cap * sizeof(uint64_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

static unsigned s64_clz(uint64_t x)
{
    unsigned bits = 0U;
    if (x == 0U) {
        return 64U;
    }
    while ((x & (UINT64_C(1) << 63U)) == 0U) {
        x <<= 1U;
        bits++;
    }
    return bits;
}

/* 就地乘 uint64（幅值，允许结果多一肢） */
static bigint_err_ty s64_mul_u64(bigint_bin64_ty *val, uint64_t m)
{
    if ((m == 0U) || (val->len == 0U)) {
        val->len = 0U;
        return BIGINT_OK_E;
    }
    if (m == 1U) {
        return BIGINT_OK_E;
    }
    const bigint_err_ty err = s64_ensure(val, val->len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t carry = 0U;
    for (size_t i = 0U; i < val->len; i++) {
        const nex_u128_ty p = nex_u128_mul(val->limbs[i], m);
        const uint64_t lo = p.lo + carry;
        carry = p.hi + ((lo < p.lo) ? 1U : 0U);
        val->limbs[i] = lo;
    }
    if (carry > 0U) {
        val->limbs[val->len] = carry;
        val->len++;
    }
    return BIGINT_OK_E;
}

/* 就地加 uint64（幅值） */
static bigint_err_ty s64_add_u64(bigint_bin64_ty *val, uint64_t a)
{
    if (a == 0U) {
        return BIGINT_OK_E;
    }
    const bigint_err_ty err = s64_ensure(val, val->len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t carry = a;
    for (size_t i = 0U; (i < val->len) && (carry != 0U); i++) {
        const uint64_t sum = val->limbs[i] + carry;
        carry = (sum < val->limbs[i]) ? 1U : 0U;
        val->limbs[i] = sum;
    }
    if (carry > 0U) {
        val->limbs[val->len] = carry;
        val->len++;
    }
    return BIGINT_OK_E;
}

/* 最大 base^k（≤ UINT64_MAX）：返回 base^k 并传出 k */
static uint64_t s64_group(uint32_t base, uint32_t *digits)
{
    uint64_t mul = 1U;
    uint32_t n = 0U;
    while (mul <= UINT64_MAX / (uint64_t)base) {
        mul *= (uint64_t)base;
        n++;
    }
    *digits = n;
    return mul;
}

static uint32_t s64_digit_value(char ch)
{
    if ((ch >= '0') && (ch <= '9')) {
        return (uint32_t)(ch - '0');
    }
    if ((ch >= 'a') && (ch <= 'z')) {
        return (uint32_t)(ch - 'a') + 10U;
    }
    if ((ch >= 'A') && (ch <= 'Z')) {
        return (uint32_t)(ch - 'A') + 10U;
    }
    return UINT32_MAX;
}

static const char *s64_skip_prefix(const char *str, uint32_t base)
{
    if (str[0] != '0') {
        return str;
    }
    if ((base == 16U) && ((str[1] == 'x') || (str[1] == 'X'))) {
        return str + 2;
    }
    if ((base == 2U) && ((str[1] == 'b') || (str[1] == 'B'))) {
        return str + 2;
    }
    return str;
}

/* ------------------------------------------------------------------ */
/* 解析                                                                 */
/* ------------------------------------------------------------------ */

bigint_err_ty bigint_bin64_from_str(bigint_bin64_ty *val, const char *str,
        uint32_t base, const char **end)
{
    if ((val == NULL) || (str == NULL) || (base < 2U) || (base > 36U)) {
        return BIGINT_ERR_INVALID_E;
    }

    const char *cur = str;
    bool is_neg = false;
    if (*cur == '-') {
        is_neg = true;
        cur++;
    }
    cur = s64_skip_prefix(cur, base);

    uint32_t group_digits = 0U;
    const uint64_t group_mul = s64_group(base, &group_digits);

    bigint_bin64_ty tmp;
    (void)bigint_bin64_init(&tmp);
    uint64_t acc = 0U;
    uint32_t acc_digits = 0U;
    const char *scan = cur;
    bigint_err_ty err = BIGINT_OK_E;
    for (;;) {
        const uint32_t digit = s64_digit_value(*scan);
        if (digit >= base) {
            break;
        }
        acc = acc * (uint64_t)base + digit;
        acc_digits++;
        scan++;
        if (acc_digits == group_digits) {
            err = s64_mul_u64(&tmp, group_mul);
            if (err == BIGINT_OK_E) {
                err = s64_add_u64(&tmp, acc);
            }
            if (err != BIGINT_OK_E) {
                break;
            }
            acc = 0U;
            acc_digits = 0U;
        }
    }
    if ((err == BIGINT_OK_E) && (scan == cur)) {
        err = BIGINT_ERR_PARSE_E;  /* 符号 / 前缀后没有任何合法数字 */
    }
    if ((err == BIGINT_OK_E) && (acc_digits > 0U)) {
        uint64_t pow = 1U;
        for (uint32_t i = 0U; i < acc_digits; i++) {
            pow *= (uint64_t)base;
        }
        err = s64_mul_u64(&tmp, pow);
        if (err == BIGINT_OK_E) {
            err = s64_add_u64(&tmp, acc);
        }
    }
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&tmp);
        if (end != NULL) {
            *end = str;  /* 失败视同未消费 */
        }
        return err;
    }

    if (tmp.len > 0U) {
        tmp.sign = is_neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }
    bigint_bin64_move(val, &tmp);
    if (end != NULL) {
        *end = scan;
    }
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 输出                                                                 */
/* ------------------------------------------------------------------ */

/* 2 的幂进制：按位切片（低位在前，与 digits_generic 一致，稍后统一反转） */
static size_t digits_pow2(const bigint_bin64_ty *val, unsigned bits, char *out)
{
    const size_t bitlen = (val->len - 1U) * 64U
            + (64U - s64_clz(val->limbs[val->len - 1U]));
    const size_t count = bitlen / bits + ((bitlen % bits) != 0U ? 1U : 0U);
    const uint64_t mask = (UINT64_C(1) << bits) - 1U;
    for (size_t k = 0U; k < count; k++) {
        const size_t pos = k * bits;
        const size_t idx = pos >> 6U;
        const unsigned off = (unsigned)(pos & 63U);
        uint64_t w = val->limbs[idx] >> off;
        if ((off + bits) > 64U) {
            if ((idx + 1U) < val->len) {
                w |= val->limbs[idx + 1U] << (64U - off);
            }
        }
        out[k] = B64_ALPHA[(size_t)(w & mask)];
    }
    return count;
}

/* 一般进制：反复除以 base^k，每块输出 k 位（低位在前，稍后反转） */
static bigint_err_ty digits_generic(const bigint_bin64_ty *val, uint32_t base,
        char *out, size_t *count)
{
    uint32_t group_digits = 0U;
    const uint64_t group_mul = s64_group(base, &group_digits);

    bigint_bin64_ty work;
    (void)bigint_bin64_init(&work);
    bigint_err_ty err = bigint_bin64_copy(&work, val);
    if (err != BIGINT_OK_E) {
        return err;
    }
    work.sign = BIGINT_SIGN_POS_E;
    size_t dc = 0U;
    while (work.len > 0U) {
        uint64_t r = 0U;
        size_t i = work.len;
        while (i > 0U) {
            i--;
            work.limbs[i] = nex_u128_divmod(r, work.limbs[i], group_mul, &r);
        }
        while ((work.len > 0U) && (work.limbs[work.len - 1U] == 0U)) {
            work.len--;
        }
        if (work.len == 0U) {
            /* 最高块：只输出有效位，避免为对齐补前导零 */
            do {
                out[dc++] = B64_ALPHA[(size_t)(r % (uint64_t)base)];
                r /= (uint64_t)base;
            } while (r != 0U);
        } else {
            for (uint32_t k = 0U; k < group_digits; k++) {
                out[dc++] = B64_ALPHA[(size_t)(r % (uint64_t)base)];
                r /= (uint64_t)base;
            }
        }
    }
    bigint_bin64_free(&work);

    /* 最高块的前导零在后（反转后即前导），裁掉但保留至少一位 */
    while ((dc > 1U) && (out[dc - 1U] == '0')) {
        dc--;
    }
    *count = dc;
    return BIGINT_OK_E;
}

bigint_err_ty bigint_bin64_to_str(const bigint_bin64_ty *val, uint32_t base,
        char *buf, size_t buf_len, size_t *needed)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((base < 2U) || (base > 36U)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len > (SIZE_MAX - 1U) / 64U) {
        return BIGINT_ERR_OOM_E;
    }

    /* 任意进制下数字个数不超过幅值位长（base 2 达到上界） */
    const size_t digit_cap = (val->len > 0U) ? (val->len * 64U + 1U) : 2U;
    char *digits = (char *)nex_malloc(digit_cap);
    if (digits == NULL) {
        return BIGINT_ERR_OOM_E;
    }

    size_t digit_count = 0U;
    bigint_err_ty err = BIGINT_OK_E;
    if (val->len == 0U) {
        digits[0] = '0';
        digit_count = 1U;
    } else if ((base & (base - 1U)) == 0U) {
        unsigned bits = 0U;
        for (uint32_t b = base; b > 1U; b >>= 1U) {
            bits++;
        }
        digit_count = digits_pow2(val, bits, digits);
    } else {
        err = digits_generic(val, base, digits, &digit_count);
    }

    if (err == BIGINT_OK_E) {
        const size_t sign_len = (val->sign == BIGINT_SIGN_NEG_E) ? 1U : 0U;
        const size_t total = sign_len + digit_count + 1U;
        if (needed != NULL) {
            *needed = total;
        }
        if ((buf != NULL) && (buf_len < total)) {
            err = BIGINT_ERR_OVERFLOW_E;
        } else if (buf != NULL) {
            size_t pos = 0U;
            if (sign_len > 0U) {
                buf[0] = '-';
                pos = 1U;
            }
            for (size_t i = 0U; i < digit_count; i++) {
                buf[pos + i] = digits[digit_count - 1U - i];
            }
            buf[total - 1U] = '\0';
        }
    }
    free(digits);
    return err;
}
