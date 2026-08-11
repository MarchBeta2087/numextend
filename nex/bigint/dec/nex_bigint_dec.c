/*
 * nex_bigint_dec.c：bigint_dec_ty（基 10^9 十进制肢大整数）核心实现。
 *
 * 职责（设计文档 §2.2、§5）：生命周期、基本类型互转、十进制字符串 I/O、
 * 比较与断言、加减与一元运算、十进制移位（mul_pow10 / div_pow10）与
 * 十进制位数度量。乘法 / 除法 / 幂由 nex_bigint_dec_mul.c /
 * nex_bigint_dec_div.c 实现。
 *
 * 规范化不变式（§3.4）：最高有效肢非零（取值 1..10^9−1）；零 ⇔
 * len == 0 且 sign == ZERO_E。
 *
 * 基 10^9 的 uint64_t 累加安全性论证（§5.3）：
 *   - 加减：两肢与进位之和 ≤ 2·10^9 − 1 < 2^31，显式 uint64_t 计算无溢出；
 *   - 单肢乘加：(10^9−1)^2 + (10^9−1) + (10^9−1) = 10^18 − 10^9 < 2^60；
 *   - 单肢除的两肢组合：高位 × 10^9 + 低位 ≤ 10^18 − 1 < 2^60。
 * 进位 / 借位一律经 uint64_t 显式中间量（§4.3），不依赖编译器溢出行为。
 */

#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

/* 十进制肢基：每肢恰好 9 位十进制数字 */
#define NEX_DEC_BASE UINT32_C(1000000000)

/* 每肢十进制位数 */
#define NEX_DEC_DIGITS 9U

/* ------------------------------------------------------------------ */
/* 内部辅助：容量与规范化                                                */
/* ------------------------------------------------------------------ */

/*
 * brief: 确保 val 的容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * param: val    目标对象
 * param: needed 需要的肢数
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: cap 恒满足 cap <= SIZE_MAX / 4（分配不变式），故 2 * cap 不溢出
 */
static bigint_err_ty ensure_cap(bigint_dec_ty *val, size_t needed)
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
 * brief: 去除高位零肢；len 变为 0 时将符号规范化为 BIGINT_SIGN_ZERO_E
 * param: val 目标对象
 */
static void normalize(bigint_dec_ty *val)
{
    while ((val->len > 0U) && (val->limbs[val->len - 1U] == 0U)) {
        val->len--;
    }
    if (val->len == 0U) {
        val->sign = BIGINT_SIGN_ZERO_E;
    }
}

/*
 * brief: 赋值 val = sign * mag；mag 为 0 时规范化为零（忽略 sign）
 * param: val  目标对象
 * param: mag  幅值
 * param: sign 符号
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: uint64_t 最大约 1.8×10^19 < 10^27，至多 3 肢
 */
static bigint_err_ty set_u64_mag(bigint_dec_ty *val, uint64_t mag,
        bigint_sign_ty sign)
{
    if (mag == 0U) {
        val->len = 0U;
        val->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    const bigint_err_ty err = ensure_cap(val, 3U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    size_t len = 0U;
    while (mag > 0U) {
        val->limbs[len] = (uint32_t)(mag % (uint64_t)NEX_DEC_BASE);
        mag /= (uint64_t)NEX_DEC_BASE;
        len++;
    }
    val->len = len;
    val->sign = sign;
    return BIGINT_OK_E;
}

/*
 * brief: 赋值 dst = sign * |src|（dst 与 src 不别名）；src 为零时 dst 为零
 * param: dst  目标对象
 * param: src  源对象
 * param: sign 期望符号
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
static bigint_err_ty set_mag(bigint_dec_ty *dst, const bigint_dec_ty *src,
        bigint_sign_ty sign)
{
    const bigint_err_ty err = ensure_cap(dst, src->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (src->len > 0U) {
        memcpy(dst->limbs, src->limbs, src->len * sizeof(uint32_t));
    }
    dst->len = src->len;
    dst->sign = (src->len > 0U) ? sign : BIGINT_SIGN_ZERO_E;
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：幅值加减与小整数乘除（均不触碰符号，由调用方负责）                */
/* ------------------------------------------------------------------ */

/*
 * brief: 幅值加 dst = |lhs| + |rhs|（不别名）；结果天然规范化
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
static bigint_err_ty mag_add(bigint_dec_ty *dst, const bigint_dec_ty *lhs,
        const bigint_dec_ty *rhs)
{
    const bigint_dec_ty *big = lhs;
    const bigint_dec_ty *small = rhs;
    if (big->len < small->len) {
        big = rhs;
        small = lhs;
    }

    const bigint_err_ty err = ensure_cap(dst, big->len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    uint64_t carry = 0U;
    size_t idx = 0U;
    for (; idx < small->len; idx++) {
        uint64_t sum = (uint64_t)big->limbs[idx] + small->limbs[idx] + carry;
        if (sum >= (uint64_t)NEX_DEC_BASE) {
            sum -= (uint64_t)NEX_DEC_BASE;
            carry = 1U;
        } else {
            carry = 0U;
        }
        dst->limbs[idx] = (uint32_t)sum;
    }
    for (; idx < big->len; idx++) {
        uint64_t sum = (uint64_t)big->limbs[idx] + carry;
        if (sum >= (uint64_t)NEX_DEC_BASE) {
            sum -= (uint64_t)NEX_DEC_BASE;
            carry = 1U;
        } else {
            carry = 0U;
        }
        dst->limbs[idx] = (uint32_t)sum;
    }
    if (carry > 0U) {
        dst->limbs[big->len] = (uint32_t)carry;
        dst->len = big->len + 1U;
    } else {
        dst->len = big->len;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 幅值减 dst = |lhs| - |rhs|，要求 |lhs| >= |rhs|（不别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 结果可能含高位零肢，由调用方 normalize
 */
static bigint_err_ty mag_sub(bigint_dec_ty *dst, const bigint_dec_ty *lhs,
        const bigint_dec_ty *rhs)
{
    const bigint_err_ty err = ensure_cap(dst, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }

    uint64_t borrow = 0U;
    for (size_t idx = 0U; idx < lhs->len; idx++) {
        const uint64_t cur = lhs->limbs[idx];
        const uint64_t sub = (uint64_t)((idx < rhs->len) ? rhs->limbs[idx] : 0U)
                + borrow;
        if (cur >= sub) {
            dst->limbs[idx] = (uint32_t)(cur - sub);
            borrow = 0U;
        } else {
            dst->limbs[idx] = (uint32_t)((uint64_t)NEX_DEC_BASE + cur - sub);
            borrow = 1U;
        }
    }
    dst->len = lhs->len;
    return BIGINT_OK_E;
}

/*
 * brief: 幅值就地乘小整数：|val| *= factor（len 为 0 时无操作）
 * param: factor 乘数，不超过 10^9
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 失败时低位肢可能已改写，调用方须放弃该对象（仅用于临时量）；
 *       或预先确保 len + 1 容量，则本函数不会失败（mul_pow10 路径）
 */
static bigint_err_ty mul_small(bigint_dec_ty *val, uint32_t factor)
{
    uint64_t carry = 0U;
    for (size_t idx = 0U; idx < val->len; idx++) {
        // cur < 10^9 × 10^9 + 10^9 = 10^18 < 2^60，uint64_t 安全（§5.3）
        const uint64_t cur = (uint64_t)val->limbs[idx] * (uint64_t)factor
                + carry;
        val->limbs[idx] = (uint32_t)(cur % (uint64_t)NEX_DEC_BASE);
        carry = cur / (uint64_t)NEX_DEC_BASE;
    }
    if (carry > 0U) {
        const bigint_err_ty err = ensure_cap(val, val->len + 1U);
        if (err != BIGINT_OK_E) {
            return err;
        }
        val->limbs[val->len] = (uint32_t)carry;
        val->len++;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 幅值就地加小整数：|val| += addend（addend 为 0 时无操作）
 * param: addend 加数，小于 10^9
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 失败时低位肢可能已改写，调用方须放弃该对象（仅用于临时量）
 */
static bigint_err_ty add_small(bigint_dec_ty *val, uint32_t addend)
{
    if (addend == 0U) {
        return BIGINT_OK_E;
    }
    if (val->len == 0U) {
        const bigint_err_ty err = ensure_cap(val, 1U);
        if (err != BIGINT_OK_E) {
            return err;
        }
        val->limbs[0] = addend;
        val->len = 1U;
        return BIGINT_OK_E;
    }

    uint64_t carry = addend;
    size_t idx = 0U;
    while ((carry > 0U) && (idx < val->len)) {
        const uint64_t sum = (uint64_t)val->limbs[idx] + carry;
        if (sum >= (uint64_t)NEX_DEC_BASE) {
            val->limbs[idx] = (uint32_t)(sum - (uint64_t)NEX_DEC_BASE);
            carry = 1U;
        } else {
            val->limbs[idx] = (uint32_t)sum;
            carry = 0U;
        }
        idx++;
    }
    if (carry > 0U) {
        const bigint_err_ty err = ensure_cap(val, val->len + 1U);
        if (err != BIGINT_OK_E) {
            return err;
        }
        val->limbs[val->len] = (uint32_t)carry;
        val->len++;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 幅值就地除以单肢除数：limbs[0..len) /= divisor，返回余数
 * param: limbs   肢数组（小端序），就地被改写为商
 * param: len     有效肢数
 * param: divisor 除数，必须非零且不超过 10^9
 * return: 余数（< divisor）
 * note: 两肢组合 rem × 10^9 + limbs[idx] ≤ 10^18 − 1 < 2^60（§5.3）；
 *       商可能含高位零肢，由调用方修剪
 */
static uint32_t div_small(uint32_t *limbs, size_t len, uint32_t divisor)
{
    uint64_t rem = 0U;
    for (size_t idx = len; idx-- > 0U;) {
        const uint64_t cur = rem * (uint64_t)NEX_DEC_BASE + limbs[idx];
        limbs[idx] = (uint32_t)(cur / (uint64_t)divisor);
        rem = cur % (uint64_t)divisor;
    }
    return (uint32_t)rem;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：十进制字符串解析与输出                                        */
/* ------------------------------------------------------------------ */

/*
 * brief: 单肢（1..999999999）的十进制位数
 */
static size_t limb_digit_count(uint32_t limb)
{
    size_t count = 1U;
    while (limb >= 10U) {
        limb /= 10U;
        count++;
    }
    return count;
}

/*
 * brief: 将单肢的十进制表示写入 out；pad 为 true 时定长 9 位前导补零
 * return: 写入的字符数
 */
static size_t write_limb_digits(char *out, uint32_t limb, bool pad)
{
    char tmp[NEX_DEC_DIGITS];
    for (size_t idx = NEX_DEC_DIGITS; idx-- > 0U;) {
        tmp[idx] = (char)('0' + (int)(limb % 10U));
        limb /= 10U;
    }
    if (pad) {
        memcpy(out, tmp, NEX_DEC_DIGITS);
        return NEX_DEC_DIGITS;
    }
    size_t first = 0U;
    while ((first < NEX_DEC_DIGITS - 1U) && (tmp[first] == '0')) {
        first++;
    }
    const size_t count = NEX_DEC_DIGITS - first;
    memcpy(out, tmp + first, count);
    return count;
}

/*
 * brief: 10^exp（exp ∈ 0..8），用于十进制移位的余数段
 */
static uint32_t pow10_small(uint32_t exp)
{
    uint32_t value = 1U;
    for (uint32_t idx = 0U; idx < exp; idx++) {
        value *= 10U;
    }
    return value;
}

/*
 * brief: 符号的全序秩：负 < 零 < 正
 */
static int sign_order(bigint_sign_ty sign)
{
    switch (sign) {
    case BIGINT_SIGN_NEG_E:
        return -1;
    case BIGINT_SIGN_POS_E:
        return 1;
    default:  // BIGINT_SIGN_ZERO_E
        return 0;
    }
}

/*
 * brief: 加减核心 dst = lhs + rhs（is_sub 时为 lhs - rhs）；要求不别名
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 圈复杂度约 12，源自符号组合与错误检查分支，无嵌套逻辑（规范 §5.4）
 */
static bigint_err_ty add_sub_core(bigint_dec_ty *dst, const bigint_dec_ty *lhs,
        const bigint_dec_ty *rhs, bool is_sub)
{
    bigint_sign_ty rhs_sign = rhs->sign;
    if (is_sub && (rhs_sign != BIGINT_SIGN_ZERO_E)) {
        rhs_sign = (rhs_sign == BIGINT_SIGN_POS_E)
                ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }

    if (lhs->sign == BIGINT_SIGN_ZERO_E) {
        return set_mag(dst, rhs, rhs_sign);
    }
    if (rhs_sign == BIGINT_SIGN_ZERO_E) {
        return set_mag(dst, lhs, lhs->sign);
    }

    bigint_err_ty err = BIGINT_OK_E;
    if (lhs->sign == rhs_sign) {
        err = mag_add(dst, lhs, rhs);
        if (err != BIGINT_OK_E) {
            return err;
        }
        dst->sign = lhs->sign;  // 同号相加结果非零
        return BIGINT_OK_E;
    }

    const int cmp = bigint_dec_cmp_abs(lhs, rhs);
    if (cmp == 0) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    const bigint_dec_ty *big = (cmp > 0) ? lhs : rhs;
    const bigint_dec_ty *small = (cmp > 0) ? rhs : lhs;
    err = mag_sub(dst, big, small);
    if (err != BIGINT_OK_E) {
        return err;
    }
    normalize(dst);
    dst->sign = (cmp > 0) ? lhs->sign : rhs_sign;  // cmp != 0，结果非零
    return BIGINT_OK_E;
}

/*
 * brief: 别名安全包装：dst 与源别名时先算到栈上临时对象，成功后 move 回 dst
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
static bigint_err_ty add_sub(bigint_dec_ty *dst, const bigint_dec_ty *lhs,
        const bigint_dec_ty *rhs, bool is_sub)
{
    if ((dst == lhs) || (dst == rhs)) {
        bigint_dec_ty tmp;
        (void)bigint_dec_init(&tmp);  // 栈对象非空，恒成功
        const bigint_err_ty err = add_sub_core(&tmp, lhs, rhs, is_sub);
        if (err != BIGINT_OK_E) {
            bigint_dec_free(&tmp);
            return err;
        }
        bigint_dec_move(dst, &tmp);
        return BIGINT_OK_E;
    }
    return add_sub_core(dst, lhs, rhs, is_sub);
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                              */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化大整数为零
 * param: val 需要初始化的大整数变量
 * return: 成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_dec_init(bigint_dec_ty *val)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    val->sign = BIGINT_SIGN_ZERO_E;
    val->limbs = NULL;
    val->len = 0U;
    val->cap = 0U;
    return BIGINT_OK_E;
}

/*
 * brief: 初始化大整数为零并预分配容量
 * param: val 需要初始化的大整数变量
 * param: cap 预分配的肢容量；为 0 时等价于 bigint_dec_init
 * return: 成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E
 */
bigint_err_ty bigint_dec_init_cap(bigint_dec_ty *val, size_t cap)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    (void)bigint_dec_init(val);  // val 已判空，恒成功
    if (cap == 0U) {
        return BIGINT_OK_E;
    }
    if (cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    val->limbs = (uint32_t *)nex_malloc(cap * sizeof(uint32_t));
    if (val->limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->cap = cap;
    return BIGINT_OK_E;
}

/*
 * brief: 释放大整数占用的内存
 * param: val 需要释放的大整数变量；可为 NULL 或零初始化对象（安全无操作）
 */
void bigint_dec_free(bigint_dec_ty *val)
{
    if (val == NULL) {
        return;
    }
    free(val->limbs);
    val->limbs = NULL;
    val->sign = BIGINT_SIGN_ZERO_E;
    val->len = 0U;
    val->cap = 0U;
}

/*
 * brief: 深拷贝大整数
 * param: dst 目标对象（已初始化）
 * param: src 源对象
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_dec_copy(bigint_dec_ty *dst, const bigint_dec_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (dst == src) {
        return BIGINT_OK_E;
    }
    return set_mag(dst, src, src->sign);
}

/*
 * brief: 移动大整数，dst 接管 src 的资源
 * param: dst 目标对象（已初始化，其原有资源被释放）
 * param: src 源对象，调用后被重置为零
 */
void bigint_dec_move(bigint_dec_ty *dst, bigint_dec_ty *src)
{
    if ((dst == NULL) || (src == NULL) || (dst == src)) {
        return;
    }
    free(dst->limbs);
    dst->limbs = NULL;
    dst->sign = src->sign;
    dst->limbs = src->limbs;
    dst->len = src->len;
    dst->cap = src->cap;
    src->sign = BIGINT_SIGN_ZERO_E;
    src->limbs = NULL;
    src->len = 0U;
    src->cap = 0U;
}

/*
 * brief: 将容量收缩至当前有效肢数
 * param: val 目标对象
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: len == 0 时释放全部容量
 */
bigint_err_ty bigint_dec_shrink(bigint_dec_ty *val)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len == 0U) {
        free(val->limbs);
        val->limbs = NULL;
        val->cap = 0U;
        return BIGINT_OK_E;
    }
    if (val->len == val->cap) {
        return BIGINT_OK_E;
    }

    uint32_t *new_limbs = (uint32_t *)nex_realloc(val->limbs,
            val->len * sizeof(uint32_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;  // 原缓冲区仍有效，val 不变
    }
    val->limbs = new_limbs;
    val->cap = val->len;
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 与基本类型互转                                                         */
/* ------------------------------------------------------------------ */

/*
 * brief: 以 uint64_t 赋值
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
bigint_err_ty bigint_dec_from_u64(bigint_dec_ty *val, uint64_t value)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    return set_u64_mag(val, value, BIGINT_SIGN_POS_E);
}

/*
 * brief: 以 int64_t 赋值
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
bigint_err_ty bigint_dec_from_i64(bigint_dec_ty *val, int64_t value)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }

    uint64_t mag = 0U;
    bigint_sign_ty sign = BIGINT_SIGN_POS_E;
    if (value < 0) {
        mag = (uint64_t)(-(value + 1)) + 1U;  // 避免 INT64_MIN 取负溢出
        sign = BIGINT_SIGN_NEG_E;
    } else {
        mag = (uint64_t)value;
    }
    return set_u64_mag(val, mag, sign);
}

/*
 * brief: 转为 uint64_t
 * return: 成功返回 BIGINT_OK_E；负数或超出范围返回 BIGINT_ERR_OVERFLOW_E
 *         （out 不被修改）
 */
bigint_err_ty bigint_dec_to_u64(const bigint_dec_ty *val, uint64_t *out)
{
    if ((val == NULL) || (out == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->sign == BIGINT_SIGN_NEG_E) {
        return BIGINT_ERR_OVERFLOW_E;
    }

    uint64_t result = 0U;
    for (size_t idx = val->len; idx-- > 0U;) {
        // 溢出判定：result × 10^9 + limb ≤ UINT64_MAX
        if (result > (UINT64_MAX - val->limbs[idx]) / (uint64_t)NEX_DEC_BASE) {
            return BIGINT_ERR_OVERFLOW_E;
        }
        result = result * (uint64_t)NEX_DEC_BASE + val->limbs[idx];
    }
    *out = result;
    return BIGINT_OK_E;
}

/*
 * brief: 转为 int64_t
 * return: 成功返回 BIGINT_OK_E；超出范围返回 BIGINT_ERR_OVERFLOW_E
 *         （out 不被修改）
 */
bigint_err_ty bigint_dec_to_i64(const bigint_dec_ty *val, int64_t *out)
{
    if ((val == NULL) || (out == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }

    // 负数允许幅值达 2^63（INT64_MIN）
    const uint64_t limit = (val->sign == BIGINT_SIGN_NEG_E)
            ? (UINT64_C(1) << 63U) : (uint64_t)INT64_MAX;
    uint64_t mag = 0U;
    for (size_t idx = val->len; idx-- > 0U;) {
        if (mag > (limit - val->limbs[idx]) / (uint64_t)NEX_DEC_BASE) {
            return BIGINT_ERR_OVERFLOW_E;
        }
        mag = mag * (uint64_t)NEX_DEC_BASE + val->limbs[idx];
    }

    switch (val->sign) {
    case BIGINT_SIGN_POS_E:
        *out = (int64_t)mag;
        break;
    case BIGINT_SIGN_NEG_E:
        if (mag == (UINT64_C(1) << 63U)) {
            *out = INT64_MIN;
        } else {
            *out = -(int64_t)mag;
        }
        break;
    default:  // BIGINT_SIGN_ZERO_E
        *out = 0;
        break;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 从十进制字符串解析；可选前导 '-'
 * return: 成功返回 BIGINT_OK_E；首字符即非法返回 BIGINT_ERR_PARSE_E 且
 *         val 不变；base 非 10 返回 BIGINT_ERR_INVALID_E；内存不足返回
 *         BIGINT_ERR_OOM_E（val 不变）
 * note: 部分消费容错；首组 digit_count % 9 位，其后每 9 位一组经
 *       mul_small(10^9) + add_small 并入，OOM 时放弃临时对象
 */
bigint_err_ty bigint_dec_from_str(bigint_dec_ty *val, const char *str,
        uint32_t base, const char **end)
{
    if ((val == NULL) || (str == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (base != 10U) {
        return BIGINT_ERR_INVALID_E;
    }

    const char *cur = str;
    bool is_neg = false;
    if (*cur == '-') {
        is_neg = true;
        cur++;
    }
    const char *digits_begin = cur;
    while ((*cur >= '0') && (*cur <= '9')) {
        cur++;
    }
    const size_t digit_count = (size_t)(cur - digits_begin);
    if (digit_count == 0U) {
        if (end != NULL) {
            *end = str;  // 失败视同未消费
        }
        return BIGINT_ERR_PARSE_E;
    }

    bigint_dec_ty tmp;
    (void)bigint_dec_init(&tmp);  // 栈对象非空，恒成功
    bigint_err_ty err = ensure_cap(&tmp, (digit_count + 8U) / 9U);
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&tmp);
        if (end != NULL) {
            *end = str;
        }
        return err;
    }

    // 首组：digit_count % 9 位（为 0 时 9 位）
    size_t group = digit_count % 9U;
    if (group == 0U) {
        group = 9U;
    }
    size_t pos = 0U;
    while (pos < digit_count) {
        uint32_t acc = 0U;
        for (size_t idx = 0U; idx < group; idx++) {
            acc = acc * 10U + (uint32_t)(digits_begin[pos] - '0');
            pos++;
        }
        if (pos == group) {
            // 首组直接落肢，保留规范化（全零输入 len 保持 0）
            if (acc > 0U) {
                tmp.limbs[0] = acc;
                tmp.len = 1U;
            }
        } else {
            err = mul_small(&tmp, NEX_DEC_BASE);
            if (err == BIGINT_OK_E) {
                err = add_small(&tmp, acc);
            }
            if (err != BIGINT_OK_E) {
                break;
            }
        }
        group = 9U;
    }
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&tmp);
        if (end != NULL) {
            *end = str;
        }
        return err;
    }

    if (tmp.len > 0U) {
        tmp.sign = is_neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }
    bigint_dec_move(val, &tmp);
    if (end != NULL) {
        *end = cur;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 转为十进制字符串（负数带 '-' 前缀；零输出 "0"）
 * return: 成功返回 BIGINT_OK_E；buf 为 NULL 表示仅查询所需长度（needed 被
 *         写出，含 '\0'，仍返回 OK）；buf 非 NULL 但不足返回
 *         BIGINT_ERR_OVERFLOW_E；base 非 10 返回 BIGINT_ERR_INVALID_E
 * note: 逐肢格式化，O(n)（§5.2）
 */
bigint_err_ty bigint_dec_to_str(const bigint_dec_ty *val, uint32_t base,
        char *buf, size_t buf_len, size_t *needed)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if (base != 10U) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len > (SIZE_MAX - 2U) / 9U) {
        return BIGINT_ERR_OOM_E;  // 位数计算溢出护栏（分配不变式下不可达）
    }

    size_t digit_count = 1U;  // 零输出 "0"
    if (val->len > 0U) {
        digit_count = (val->len - 1U) * NEX_DEC_DIGITS
                + limb_digit_count(val->limbs[val->len - 1U]);
    }
    const size_t sign_len = (val->sign == BIGINT_SIGN_NEG_E) ? 1U : 0U;
    const size_t total = sign_len + digit_count + 1U;
    if (needed != NULL) {
        *needed = total;
    }
    if (buf == NULL) {
        return BIGINT_OK_E;
    }
    if (buf_len < total) {
        return BIGINT_ERR_OVERFLOW_E;
    }

    size_t pos = 0U;
    if (sign_len > 0U) {
        buf[pos] = '-';
        pos++;
    }
    if (val->len == 0U) {
        buf[pos] = '0';
        pos++;
    } else {
        // 首肢去前导零，其余肢定长 9 位补零
        pos += write_limb_digits(buf + pos, val->limbs[val->len - 1U], false);
        for (size_t idx = val->len - 1U; idx-- > 0U;) {
            pos += write_limb_digits(buf + pos, val->limbs[idx], true);
        }
    }
    buf[pos] = '\0';
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 比较与断言                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 比较两个大整数
 * return: lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正
 */
int bigint_dec_cmp(const bigint_dec_ty *lhs, const bigint_dec_ty *rhs)
{
    const int lhs_order = sign_order(lhs->sign);
    const int rhs_order = sign_order(rhs->sign);
    if (lhs_order != rhs_order) {
        return (lhs_order < rhs_order) ? -1 : 1;
    }
    if (lhs_order == 0) {
        return 0;
    }
    const int mag_cmp = bigint_dec_cmp_abs(lhs, rhs);
    return (lhs_order > 0) ? mag_cmp : -mag_cmp;
}

/*
 * brief: 比较两个大整数的绝对值
 * return: |lhs| < |rhs| 为负，相等为 0，|lhs| > |rhs| 为正
 */
int bigint_dec_cmp_abs(const bigint_dec_ty *lhs, const bigint_dec_ty *rhs)
{
    if (lhs->len != rhs->len) {
        return (lhs->len < rhs->len) ? -1 : 1;
    }
    for (size_t idx = lhs->len; idx-- > 0U;) {
        if (lhs->limbs[idx] != rhs->limbs[idx]) {
            return (lhs->limbs[idx] < rhs->limbs[idx]) ? -1 : 1;
        }
    }
    return 0;
}

/*
 * brief: 读取符号标志
 */
bigint_sign_ty bigint_dec_sign(const bigint_dec_ty *val)
{
    return val->sign;
}

/*
 * brief: 判断是否为零
 */
bool bigint_dec_is_zero(const bigint_dec_ty *val)
{
    return val->sign == BIGINT_SIGN_ZERO_E;
}

/* ------------------------------------------------------------------ */
/* 加减与一元运算                                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs；dst 允许与 lhs / rhs 别名
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_dec_add(bigint_dec_ty *dst, const bigint_dec_ty *lhs,
        const bigint_dec_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    return add_sub(dst, lhs, rhs, false);
}

/*
 * brief: 减法 dst = lhs - rhs；dst 允许与 lhs / rhs 别名
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_dec_sub(bigint_dec_ty *dst, const bigint_dec_ty *lhs,
        const bigint_dec_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    return add_sub(dst, lhs, rhs, true);
}

/*
 * brief: 就地取负（零不变）
 * return: 恒为 BIGINT_OK_E；val 为 NULL 时防御性返回 BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_dec_neg(bigint_dec_ty *val)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->sign == BIGINT_SIGN_POS_E) {
        val->sign = BIGINT_SIGN_NEG_E;
    } else if (val->sign == BIGINT_SIGN_NEG_E) {
        val->sign = BIGINT_SIGN_POS_E;
    } else {
        // 零不变
    }
    return BIGINT_OK_E;
}

/*
 * brief: 就地取绝对值
 * return: 恒为 BIGINT_OK_E；val 为 NULL 时防御性返回 BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_dec_abs(bigint_dec_ty *val)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->sign == BIGINT_SIGN_NEG_E) {
        val->sign = BIGINT_SIGN_POS_E;
    }
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 十进制移位与度量（dec 专有）                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 就地乘以 10^digits（符号不变；零不变）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: 先按 9 整肢移动，余数（0..8）走单肢乘；容量一次性预留，
 *       预留成功后 mul_small 不再分配，保证失败时 val 不变
 */
bigint_err_ty bigint_dec_mul_pow10(bigint_dec_ty *val, size_t digits)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((digits == 0U) || (val->len == 0U)) {
        return BIGINT_OK_E;
    }

    const size_t limb_shift = digits / NEX_DEC_DIGITS;
    const uint32_t rem = (uint32_t)(digits % NEX_DEC_DIGITS);
    if ((limb_shift > SIZE_MAX - val->len - 1U)
            || (val->len + limb_shift + 1U > SIZE_MAX / sizeof(uint32_t))) {
        return BIGINT_ERR_OOM_E;  // 肢数上界护栏，防止加回绕
    }

    const bigint_err_ty err = ensure_cap(val, val->len + limb_shift + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    if (limb_shift > 0U) {
        memmove(val->limbs + limb_shift, val->limbs,
                val->len * sizeof(uint32_t));
        memset(val->limbs, 0, limb_shift * sizeof(uint32_t));
        val->len += limb_shift;
    }
    if (rem > 0U) {
        // 容量已预留（mul_small 至多再长 1 肢），恒成功；仍检查以遵循规范 §8.6
        const bigint_err_ty mul_err = mul_small(val, pow10_small(rem));
        if (mul_err != BIGINT_OK_E) {
            return mul_err;
        }
    }
    return BIGINT_OK_E;
}

/*
 * brief: 就地截断除以 10^digits（向零取整）
 * return: 恒为 BIGINT_OK_E（不分配内存）；val 为 NULL 时防御性返回
 *         BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_dec_div_pow10(bigint_dec_ty *val, size_t digits)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((digits == 0U) || (val->len == 0U)) {
        return BIGINT_OK_E;
    }

    const size_t limb_shift = digits / NEX_DEC_DIGITS;
    const uint32_t rem = (uint32_t)(digits % NEX_DEC_DIGITS);
    if (limb_shift >= val->len) {
        val->len = 0U;
        val->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    if (limb_shift > 0U) {
        memmove(val->limbs, val->limbs + limb_shift,
                (val->len - limb_shift) * sizeof(uint32_t));
        val->len -= limb_shift;
    }
    if (rem > 0U) {
        (void)div_small(val->limbs, val->len, pow10_small(rem));
    }
    normalize(val);  // 结果为零时符号一并规范化
    return BIGINT_OK_E;
}

/*
 * brief: 幅值的十进制位数（零为 0）
 */
size_t bigint_dec_digit_len(const bigint_dec_ty *val)
{
    if ((val == NULL) || (val->len == 0U)) {
        return 0U;
    }
    return (val->len - 1U) * NEX_DEC_DIGITS
            + limb_digit_count(val->limbs[val->len - 1U]);
}
