/*
 * nex_bigint_bin_gcd.c：bigint_bin_ty 最大公因数实现。
 *
 * 职责（设计文档 §4.2.4、§4.3）：二进制 GCD（Stein 算法），供 bigfrac
 * 约分（§6.3）使用。语义：dst = gcd(|lhs|, |rhs|)，结果恒非负，并约定
 * gcd(0, 0) = 0。
 *
 * 实现只复用公开 API（拷贝、比较、减法、移位、位测试），全程在临时对象
 * 上运算，成功后才经 move 写入 dst，保证失败时 dst 不变的强错误保证。
 * 输入的符号在入口处经 abs 剥离，循环体内只处理非负幅值。
 */

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_alloc.h"

/*
 * brief: 统计 val 幅值最低有效位以下的连续零位个数（trailing zeros）
 * note: 要求 val 非零（由调用方保证）；非零幅值必有置位位，故循环必有界
 */
static size_t trailing_zeros(const bigint_bin_ty *val)
{
    size_t count = 0U;
    while (!bigint_bin_bit_test(val, count)) {
        count++;
    }
    return count;
}

/*
 * brief: 交换两个大整数对象的内部表示（浅交换，资源所有权随结构体移动）
 */
static void gcd_swap(bigint_bin_ty *lhs, bigint_bin_ty *rhs)
{
    const bigint_bin_ty tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

/*
 * brief: 二进制 GCD 主循环：a = gcd(a, b)，要求 a、b 均为正
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 *         （a / b 内容未定义，由调用方负责释放）
 */
static bigint_err_ty gcd_core(bigint_bin_ty *a, bigint_bin_ty *b)
{
    /* 提出公因子 2^shift：gcd(a, b) = 2^shift × gcd(a >> tz_a, b >> tz_b) */
    const size_t tz_a = trailing_zeros(a);
    const size_t tz_b = trailing_zeros(b);
    const size_t shift = (tz_a < tz_b) ? tz_a : tz_b;

    bigint_err_ty err = bigint_bin_shr(a, a, tz_a);
    if (err == BIGINT_OK_E) {
        err = bigint_bin_shr(b, b, tz_b);
    }

    /*
     * 此后 a 恒为奇数；b - a 为偶数，去尾零后恢复奇数。
     * b 严格递减，循环必有穷。
     */
    while ((err == BIGINT_OK_E) && !bigint_bin_is_zero(b)) {
        if (bigint_bin_cmp(a, b) > 0) {
            gcd_swap(a, b);
        }
        err = bigint_bin_sub(b, b, a);
        if ((err == BIGINT_OK_E) && !bigint_bin_is_zero(b)) {
            err = bigint_bin_shr(b, b, trailing_zeros(b));
        }
    }

    if (err == BIGINT_OK_E) {
        err = bigint_bin_shl(a, a, shift);
    }
    return err;
}


bigint_err_ty bigint_bin_gcd(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }

    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_err_ty err = bigint_bin_init(&a);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin_init(&b);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&a);
        return err;
    }

    /* 全程操作 |lhs| 与 |rhs| 的副本，dst 仅在成功路径末端被改写 */
    err = bigint_bin_copy(&a, lhs);
    if (err == BIGINT_OK_E) {
        err = bigint_bin_copy(&b, rhs);
    }

    if (err == BIGINT_OK_E) {
        (void)bigint_bin_abs(&a);
        (void)bigint_bin_abs(&b);
        if (bigint_bin_is_zero(&a)) {
            gcd_swap(&a, &b);   /* gcd(0, b) = |b|，含 gcd(0, 0) = 0 */
        } else if (!bigint_bin_is_zero(&b)) {
            err = gcd_core(&a, &b);
        }
        /* 剩余情形 b == 0：结果即 a（= |lhs|） */
    }

    if (err == BIGINT_OK_E) {
        bigint_bin_move(dst, &a);   /* dst 接管结果，a 被重置为零 */
    } else {
        bigint_bin_free(&a);
    }
    bigint_bin_free(&b);
    return err;
}
