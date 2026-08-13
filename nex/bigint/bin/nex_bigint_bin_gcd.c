/*
 * nex_bigint_bin_gcd.c：bigint_bin_ty 最大公因数实现。
 *
 * 职责（设计文档 §4.2.4、§4.3）：二进制 GCD（Stein 算法）+ 大输入前导的
 * Lehmer 两步约减（§13 方向），供 bigfrac 约分（§6.3）使用。语义：
 * dst = gcd(|lhs|, |rhs|)，结果恒非负，并约定 gcd(0, 0) = 0。
 *
 * Lehmer（Knuth 4.5.2）：对顶 64 位做欧几里得并累积续分式矩阵
 * （det 恒 ±1，故 gcd 保持），矩阵作用于大数完成多步约减；验证 b' < b
 * 不满足时回退一次完整除法。所有中间量为有符号 64 位，矩阵条目经
 * 溢出护栏（q·|v| 与 |u|+q|v| 均限 < 2^63，续分式相邻条目异号保证
 * 幅值相加界成立）。
 *
 * 实现只复用公开 API（拷贝、比较、减法、移位、位测试、除法），全程在
 * 临时对象上运算，成功后才经 move 写入 dst，保证失败时 dst 不变的
 * 强错误保证。输入的符号在入口处经 abs 剥离。
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
 * brief: 一步 Lehmer 约减：(a, b) → (a', b')，保持 gcd 且 b' < b
 * note: 顶 64 位欧几里得累积续分式矩阵（det ±1）；验证失败回退完整除法
 *       一步（b, a mod b）。要求 a ≥ b > 0 且 b 超过单肢
 */
static bigint_err_ty gcd_lehmer_step(bigint_bin_ty *a, bigint_bin_ty *b)
{
    const size_t blen_a = bigint_bin_bit_len(a);
    if (blen_a <= 64U) {
        /* 两者都 ≤ 64 位：一次性欧几里得收尾（b 置零终止入口循环） */
        uint64_t x = 0U;
        uint64_t y = 0U;
        (void)bigint_bin_to_u64(a, &x);
        (void)bigint_bin_to_u64(b, &y);
        while (y != 0U) {
            const uint64_t r = x % y;
            x = y;
            y = r;
        }
        bigint_err_ty err = bigint_bin_from_u64(a, x);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_from_u64(b, 0U);
        }
        return err;
    }

    const size_t s = blen_a - 64U;
    uint64_t x = 0U;
    uint64_t y = 0U;
    bigint_bin_ty hi;
    (void)bigint_bin_init(&hi);
    (void)bigint_bin_shr(&hi, a, s);
    (void)bigint_bin_to_u64(&hi, &x);
    (void)bigint_bin_shr(&hi, b, s);
    (void)bigint_bin_to_u64(&hi, &y);
    bigint_bin_free(&hi);
    if (y == 0U) {
        /* 位长差 ≥ 64：完整除法一步（商过大，矩阵无法容纳）
           (a, b) = (b, a mod b)，a 须换为旧 b */
        bigint_bin_ty old_b;
        bigint_bin_ty rem;
        (void)bigint_bin_init(&old_b);
        (void)bigint_bin_init(&rem);
        bigint_err_ty err = bigint_bin_copy(&old_b, b);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_div_rem(NULL, &rem, a, b);
        }
        if (err == BIGINT_OK_E) {
            bigint_bin_move(a, &old_b);
            bigint_bin_move(b, &rem);
        } else {
            bigint_bin_free(&old_b);
            bigint_bin_free(&rem);
        }
        return err;
    }

    /* 顶 64 位欧几里得 + 续分式矩阵（int64 条目 + 溢出护栏） */
    int64_t u1 = 1;
    int64_t u2 = 0;
    int64_t v1 = 0;
    int64_t v2 = 1;
    while (1) {
        const uint64_t q = x / y;
        const uint64_t r = x - q * y;
        if ((r == 0U) || (r < (UINT64_C(1) << 32))) {
            break;  /* 余数进入单肢：最后商可疑，本步不含 */
        }
        if (q > (uint64_t)INT64_MAX) {
            break;  /* 商过大（理论不可达），保守停止 */
        }
        const int64_t qs = (int64_t)q;
        const int64_t av1 = (v1 < 0) ? -v1 : v1;
        const int64_t av2 = (v2 < 0) ? -v2 : v2;
        const int64_t au1 = (u1 < 0) ? -u1 : u1;
        const int64_t au2 = (u2 < 0) ? -u2 : u2;
        if ((av1 > INT64_MAX / qs) || (av2 > INT64_MAX / qs)) {
            break;
        }
        /* 相邻条目异号：|新条目| = |u| + q·|v|，幅值相加防溢出 */
        if ((au1 > INT64_MAX - qs * av1) || (au2 > INT64_MAX - qs * av2)) {
            break;
        }
        const int64_t nu1 = v1;
        const int64_t nu2 = v2;
        const int64_t nv1 = u1 - qs * v1;
        const int64_t nv2 = u2 - qs * v2;
        u1 = nu1;
        u2 = nu2;
        v1 = nv1;
        v2 = nv2;
        x = y;
        y = r;
    }

    /* 应用矩阵：(a', b') = (|u1·a + u2·b|, |v1·a + v2·b|)；
       续分式结构保证 u1·u2 ≤ 0、v1·v2 ≤ 0 */
    bigint_bin_ty p1;
    bigint_bin_ty p2;
    bigint_bin_ty q1;
    bigint_bin_ty q2;
    bigint_bin_ty t;
    bigint_bin_ty a2;
    bigint_bin_ty b2;
    (void)bigint_bin_init(&p1);
    (void)bigint_bin_init(&p2);
    (void)bigint_bin_init(&q1);
    (void)bigint_bin_init(&q2);
    (void)bigint_bin_init(&t);
    (void)bigint_bin_init(&a2);
    (void)bigint_bin_init(&b2);
    bigint_err_ty err = bigint_bin_from_u64(&t, (uint64_t)((u1 < 0) ? -u1 : u1));
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&p1, a, &t);
    if (err == BIGINT_OK_E) err = bigint_bin_from_u64(&t, (uint64_t)((u2 < 0) ? -u2 : u2));
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&p2, b, &t);
    if (err == BIGINT_OK_E) err = bigint_bin_from_u64(&t, (uint64_t)((v1 < 0) ? -v1 : v1));
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&q1, a, &t);
    if (err == BIGINT_OK_E) err = bigint_bin_from_u64(&t, (uint64_t)((v2 < 0) ? -v2 : v2));
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&q2, b, &t);
    if (err == BIGINT_OK_E) err = bigint_bin_sub(&a2, &p1, &p2);
    if (err == BIGINT_OK_E) err = bigint_bin_sub(&b2, &q1, &q2);
    if (err == BIGINT_OK_E) {
        bigint_bin_abs(&a2);
        bigint_bin_abs(&b2);
    }
    if ((err == BIGINT_OK_E) && (bigint_bin_is_zero(&b2)
            || (bigint_bin_cmp(&b2, b) >= 0)
            || (bigint_bin_cmp(&a2, &b2) < 0))) {
        /* 矩阵未产生有效约减（或顺序不对）：回退完整除法一步 */
        bigint_bin_ty rem;
        (void)bigint_bin_init(&rem);
        err = bigint_bin_div_rem(NULL, &rem, a, b);
        if (err == BIGINT_OK_E) {
            bigint_bin_move(&a2, b);
            bigint_bin_move(&b2, &rem);
        } else {
            bigint_bin_free(&rem);
        }
    }
    if (err == BIGINT_OK_E) {
        bigint_bin_move(a, &a2);
        bigint_bin_move(b, &b2);
        if (bigint_bin_cmp(a, b) < 0) {
            gcd_swap(a, b);
        }
    }
    bigint_bin_free(&b2);
    bigint_bin_free(&a2);
    bigint_bin_free(&t);
    bigint_bin_free(&q2);
    bigint_bin_free(&q1);
    bigint_bin_free(&p2);
    bigint_bin_free(&p1);
    return err;
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
            if (bigint_bin_cmp(&a, &b) < 0) {
                gcd_swap(&a, &b);
            }
            /* Lehmer 前导：两数都超单肢时用矩阵多步约减（§13 方向） */
            while ((err == BIGINT_OK_E)
                    && (bigint_bin_bit_len(&b) > 32U)) {
                err = gcd_lehmer_step(&a, &b);
            }
            if ((err == BIGINT_OK_E) && !bigint_bin_is_zero(&b)) {
                err = gcd_core(&a, &b);
            }
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
