/*
 * nex_bigint_conv.c：bigint_bin ↔ bigint_dec 互转（设计文档 §4.2.6、§4.3）。
 *
 * 本单元是唯一同时包含 bin 与 dec 头文件的模块（§2.1 的唯一跨支线依赖）：
 *   - bin → dec：对 bin 幅值工作副本反复除以 10^9（单肢除法），余数即
 *     十进制肢，自低向高依次产出；
 *   - dec → bin：自高肢向低肢逐肢 "乘 10^9 加当前肢" 累加。
 * 两侧输出均保持规范化表示：最高有效肢非零，零 ⇔ len == 0 且 sign == ZERO。
 * 中间量一律经 uint64_t 显式计算（§4.3、§5.3），不依赖编译器溢出行为。
 */

#include "nex/bigint/nex_bigint_conv.h"

#include <stdlib.h>
#include <string.h>

/* 十进制肢基 10^9（小于 2^32，可作 bin 上的单肢乘数 / 除数） */
#define NEX_DEC_BASE UINT32_C(1000000000)

/* ------------------------------------------------------------------ */
/* 内部辅助：容量（自包含，不依赖 bin.c / dec.c 的 static 函数）            */
/* ------------------------------------------------------------------ */

/*
 * brief: 确保 dec 对象容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty conv_dec_ensure(bigint_dec_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)realloc(val->limbs,
            new_cap * sizeof(uint32_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

/*
 * brief: 确保 bin 对象容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty conv_bin_ensure(bigint_bin_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)realloc(val->limbs,
            new_cap * sizeof(uint32_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

/*
 * brief: bin 幅值就地乘 10^9 再加小整数：|val| = |val| × 10^9 + addend
 * param: addend 加数，小于 10^9（即源 dec 肢）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 乘积累加 ≤ (2^32−1) × 10^9 + (10^9−1) < 2^62，uint64_t 安全；
 *       失败时低位肢可能已改写，调用方须放弃该对象（仅用于临时量）
 */
static bigint_err_ty bin_mul_add_base(bigint_bin_ty *val, uint32_t addend)
{
    uint64_t carry = addend;
    for (size_t idx = 0U; idx < val->len; idx++) {
        const uint64_t cur = (uint64_t)val->limbs[idx] * (uint64_t)NEX_DEC_BASE
                + carry;
        val->limbs[idx] = (uint32_t)cur;
        carry = cur >> 32U;
    }
    while (carry > 0U) {
        const bigint_err_ty err = conv_bin_ensure(val, val->len + 1U);
        if (err != BIGINT_OK_E) {
            return err;
        }
        val->limbs[val->len] = (uint32_t)carry;
        carry >>= 32U;
        val->len++;
    }
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: bin 转 dec（反复除以 10^9，余数即十进制肢）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 最后一次非零除法的余数即最高有效肢，恒非零，结果天然规范化
 */
bigint_err_ty bigint_conv_bin_to_dec(bigint_dec_ty *dst,
        const bigint_bin_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (src->len == 0U) {
        return bigint_dec_from_u64(dst, 0U);
    }
    if (src->len > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *work = (uint32_t *)malloc(src->len * sizeof(uint32_t));
    if (work == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    memcpy(work, src->limbs, src->len * sizeof(uint32_t));
    size_t work_len = src->len;

    bigint_dec_ty tmp;
    (void)bigint_dec_init(&tmp);  // 栈对象非空，恒成功
    bigint_err_ty err = BIGINT_OK_E;
    while (work_len > 0U) {
        /* bin 幅值上的单肢除法：cur ≤ (10^9−1) × 2^32 + (2^32−1) < 2^62 */
        uint64_t rem = 0U;
        for (size_t idx = work_len; idx-- > 0U;) {
            const uint64_t cur = (rem << 32U) | work[idx];
            work[idx] = (uint32_t)(cur / (uint64_t)NEX_DEC_BASE);
            rem = cur % (uint64_t)NEX_DEC_BASE;
        }
        while ((work_len > 0U) && (work[work_len - 1U] == 0U)) {
            work_len--;
        }
        err = conv_dec_ensure(&tmp, tmp.len + 1U);
        if (err != BIGINT_OK_E) {
            break;
        }
        tmp.limbs[tmp.len] = (uint32_t)rem;
        tmp.len++;
    }
    free(work);
    work = NULL;
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&tmp);
        return err;
    }

    tmp.sign = src->sign;  // src 非零，tmp.len 恒 > 0
    bigint_dec_move(dst, &tmp);
    return BIGINT_OK_E;
}

/*
 * brief: dec 转 bin（自高肢向低肢逐肢乘 10^9 累加）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_conv_dec_to_bin(bigint_bin_ty *dst,
        const bigint_dec_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (src->len == 0U) {
        return bigint_bin_from_u64(dst, 0U);
    }

    bigint_bin_ty tmp;
    (void)bigint_bin_init(&tmp);  // 栈对象非空，恒成功
    bigint_err_ty err = BIGINT_OK_E;
    for (size_t idx = src->len; idx-- > 0U;) {
        err = bin_mul_add_base(&tmp, src->limbs[idx]);
        if (err != BIGINT_OK_E) {
            break;
        }
    }
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return err;
    }

    tmp.sign = src->sign;  // src 非零，tmp.len 恒 > 0
    bigint_bin_move(dst, &tmp);
    return BIGINT_OK_E;
}
