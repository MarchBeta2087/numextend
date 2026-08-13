/*
 * nex_bigint_bin.c：bigint_bin_ty（基 2^32 二进制肢大整数）核心实现。
 *
 * 职责（设计文档 §2.2、§4）：生命周期、基本类型互转、字符串 I/O、比较与断言、
 * 加减与一元运算、位运算（负数按补码无限符号扩展，与 Python 一致）。
 * 乘法 / 除法 / 幂由 nex_bigint_bin_mul.c / nex_bigint_bin_div.c 实现。
 *
 * 规范化不变式（§3.4）：最高有效肢非零；零 ⇔ len == 0 且 sign == ZERO_E。
 * 进位 / 借位一律经 uint64_t 显式中间量（§4.3）。
 */

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/nex_bigint_conv.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

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
static bigint_err_ty ensure_cap(bigint_bin_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)nex_realloc(val->limbs, new_cap * sizeof(uint32_t));
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
static void normalize(bigint_bin_ty *val)
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
 */
static bigint_err_ty set_u64_mag(bigint_bin_ty *val, uint64_t mag,
        bigint_sign_ty sign)
{
    if (mag == 0U) {
        val->len = 0U;
        val->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    const bigint_err_ty err = ensure_cap(val, 2U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    val->limbs[0] = (uint32_t)mag;
    const uint32_t high = (uint32_t)(mag >> 32U);
    if (high > 0U) {
        val->limbs[1] = high;
        val->len = 2U;
    } else {
        val->len = 1U;
    }
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
static bigint_err_ty set_mag(bigint_bin_ty *dst, const bigint_bin_ty *src,
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
static bigint_err_ty mag_add(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    const bigint_bin_ty *big = lhs;
    const bigint_bin_ty *small = rhs;
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
        const uint64_t sum = (uint64_t)big->limbs[idx] + small->limbs[idx] + carry;
        dst->limbs[idx] = (uint32_t)sum;
        carry = sum >> 32U;
    }
    for (; idx < big->len; idx++) {
        const uint64_t sum = (uint64_t)big->limbs[idx] + carry;
        dst->limbs[idx] = (uint32_t)sum;
        carry = sum >> 32U;
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
static bigint_err_ty mag_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
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
            dst->limbs[idx] = (uint32_t)((UINT64_C(1) << 32U) + cur - sub);
            borrow = 1U;
        }
    }
    dst->len = lhs->len;
    return BIGINT_OK_E;
}

/*
 * brief: 幅值就地乘小整数：|val| *= factor（len 为 0 时无操作）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 失败时低位肢可能已改写，调用方须放弃该对象（仅用于临时量）
 */
static bigint_err_ty mul_small(bigint_bin_ty *val, uint32_t factor)
{
    uint64_t carry = 0U;
    for (size_t idx = 0U; idx < val->len; idx++) {
        const uint64_t cur = (uint64_t)val->limbs[idx] * (uint64_t)factor + carry;
        val->limbs[idx] = (uint32_t)cur;
        carry = cur >> 32U;
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
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 失败时低位肢可能已改写；调用方须放弃该对象或预留容量（shr 路径）
 */
static bigint_err_ty add_small(bigint_bin_ty *val, uint32_t addend)
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
        val->limbs[idx] = (uint32_t)sum;
        carry = sum >> 32U;
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
 * param: divisor 除数，必须非零
 * return: 余数（< divisor）
 * note: 商可能含高位零肢，由调用方修剪
 */
static uint32_t div_small(uint32_t *limbs, size_t len, uint32_t divisor)
{
    uint64_t rem = 0U;
    for (size_t idx = len; idx-- > 0U;) {
        const uint64_t cur = (rem << 32U) | limbs[idx];
        limbs[idx] = (uint32_t)(cur / (uint64_t)divisor);
        rem = cur % (uint64_t)divisor;
    }
    return (uint32_t)rem;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：字符串解析与输出                                            */
/* ------------------------------------------------------------------ */

/*
 * brief: 字符转数字值（0-9a-zA-Z → 0..35），非法字符返回 UINT32_MAX
 */
static uint32_t digit_value(char ch)
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

/*
 * brief: 数字值转小写字符（0..35 → 0-9a-z）；调用方保证取值合法
 */
static char digit_char(uint32_t value)
{
    if (value < 10U) {
        return (char)('0' + (int)value);
    }
    return (char)('a' + (int)value - 10);
}

/*
 * brief: 计算进制分组参数：返回不超过 UINT32_MAX 的最大 base^k，经 digits 传出 k
 * note: 一组 k 位数字的值 < base^k，可用 uint32_t 累加后与单肢乘除配合
 */
static uint32_t base_group(uint32_t base, uint32_t *digits)
{
    uint64_t mul = base;
    uint32_t count = 1U;
    while (mul * (uint64_t)base <= (uint64_t)UINT32_MAX) {
        mul *= (uint64_t)base;
        count++;
    }
    *digits = count;
    return (uint32_t)mul;
}

/*
 * brief: 跳过 base 对应的可选前缀（base 16 的 "0x"/"0X"，base 2 的 "0b"/"0B"）
 * return: 跳过前缀后的位置；无前缀时返回原位置
 */
static const char *str_skip_prefix(const char *str, uint32_t base)
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

/*
 * brief: 将不足一组的 acc_digits 位已解析数字（值 acc）并入 tmp：
 *        tmp = tmp * base^acc_digits + acc
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 */
static bigint_err_ty flush_group(bigint_bin_ty *tmp, uint32_t base, uint64_t acc,
        uint32_t acc_digits)
{
    if (acc_digits == 0U) {
        return BIGINT_OK_E;
    }
    uint64_t mul = 1U;
    for (uint32_t idx = 0U; idx < acc_digits; idx++) {
        mul *= (uint64_t)base;
    }
    const bigint_err_ty err = mul_small(tmp, (uint32_t)mul);
    if (err != BIGINT_OK_E) {
        return err;
    }
    return add_small(tmp, (uint32_t)acc);
}

/* ------------------------------------------------------------------ */
/* 内部辅助：十进制快速 I/O（设计文档 §13 方向落地，经转换单元跨支线）  */
/* ------------------------------------------------------------------ */

/*
 * brief: 单肢十进制位数（1..9），用于十进制输出位数估算
 */
static size_t bin_limb_digit_count(uint32_t limb)
{
    size_t count = 1U;
    while (limb >= 10U) {
        limb /= 10U;
        count++;
    }
    return count;
}

/*
 * brief: 将单肢十进制写入 out；pad 为 true 时定长 9 位前导补零
 * return: 写入字符数
 * note: 与 dec 模块格式化语义一致（最高肢去前导零，其余肢补零）
 */
static size_t bin_write_limb(char *out, uint32_t limb, bool pad)
{
    char tmp[9];
    for (size_t idx = 9U; idx-- > 0U;) {
        tmp[idx] = (char)('0' + (int)(limb % 10U));
        limb /= 10U;
    }
    if (pad) {
        memcpy(out, tmp, 9U);
        return 9U;
    }
    size_t first = 0U;
    while ((first < 8U) && (tmp[first] == '0')) {
        first++;
    }
    const size_t count = 9U - first;
    memcpy(out, tmp + first, count);
    return count;
}

/*
 * brief: 十进制快速输出（base 10 专用）：bin→dec 分治转换后逐肢格式化
 * param: out     输出缓冲（无 '\0' 结尾，仅数字字符）
 * param: out_cap 输出缓冲容量
 * param: count_out 传出数字字符数
 * note: 替代 digits_generic 的 O(n²) 反复除 10^9；转换经转换单元（§2.1）
 */
static bigint_err_ty digits_dec_fast(const bigint_bin_ty *val, char *out,
        size_t out_cap, size_t *count_out)
{
    bigint_dec_ty dec;
    (void)bigint_dec_init(&dec);  // 栈对象非空，恒成功
    bigint_err_ty err = bigint_conv_bin_to_dec(&dec, val);
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&dec);
        return err;
    }

    const size_t digit_count = (dec.len - 1U) * 9U
            + bin_limb_digit_count(dec.limbs[dec.len - 1U]);
    if (digit_count + 1U > out_cap) {
        bigint_dec_free(&dec);
        return BIGINT_ERR_OOM_E;  // 理论不可达（digit_cap = len·32+1）
    }
    size_t pos = bin_write_limb(out, dec.limbs[dec.len - 1U], false);
    for (size_t idx = dec.len - 1U; idx-- > 0U;) {
        pos += bin_write_limb(out + pos, dec.limbs[idx], true);
    }
    *count_out = digit_count;
    bigint_dec_free(&dec);
    return BIGINT_OK_E;
}

/*
 * brief: 十进制快速解析（base 10 专用）：数字串按 9 位分组直接解析为
 *        dec 肢（O(n)），再经分治 dec→bin 转入 bin 幅值
 * param: tmp   累积对象（调用方初始化为零）
 * param: cur   起始位置
 * param: stop  传出首个未消费字符位置；无合法数字时返回
 *        BIGINT_ERR_PARSE_E（stop 指向 cur）
 */
static bigint_err_ty parse_dec_fast(bigint_bin_ty *tmp, const char *cur,
        const char **stop)
{
    /* 定位合法数字跨度 [cur, scan)（'0'..'9'） */
    const char *scan = cur;
    while (digit_value(*scan) < 10U) {
        scan++;
    }
    *stop = scan;
    if (scan == cur) {
        return BIGINT_ERR_PARSE_E;
    }

    const size_t ndigits = (size_t)(scan - cur);
    const size_t nlimbs = (ndigits + 8U) / 9U;
    bigint_dec_ty dec;
    bigint_err_ty err = bigint_dec_init_cap(&dec, nlimbs);
    if (err != BIGINT_OK_E) {
        return err;
    }
    /* 自右向左每 9 位一组解析为 dec 肢（小端：最低位组在 limb[0]） */
    size_t idx = 0U;
    const char *g_end = scan;
    while (g_end > cur) {
        const char *g_start = ((size_t)(g_end - cur) >= 9U) ? g_end - 9 : cur;
        uint32_t group = 0U;
        for (const char *p = g_start; p < g_end; p++) {
            group = group * 10U + digit_value(*p);
        }
        dec.limbs[idx] = group;
        idx++;
        g_end = g_start;
    }
    dec.len = idx;
    while ((dec.len > 0U) && (dec.limbs[dec.len - 1U] == 0U)) {
        dec.len--;
    }
    dec.sign = (dec.len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;

    err = bigint_conv_dec_to_bin(tmp, &dec);
    bigint_dec_free(&dec);
    return err;
}

/*
 * brief: 从 cur 起尽可能多地消费 base 进制数字并入 tmp 的幅值
 * param: tmp  累积对象（调用方初始化为零，符号字段不参与）
 * param: cur  起始位置
 * param: base 进制（2..36）
 * param: stop 传出首个未消费字符位置
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 一个数字都未消费不视为错误（stop == cur），由调用方判定
 */
static bigint_err_ty parse_digits(bigint_bin_ty *tmp, const char *cur,
        uint32_t base, const char **stop)
{
    uint32_t group_digits = 0U;
    const uint32_t group_mul = base_group(base, &group_digits);

    bigint_err_ty err = BIGINT_OK_E;
    uint64_t acc = 0U;
    uint32_t acc_digits = 0U;
    const char *scan = cur;
    for (;;) {
        const uint32_t digit = digit_value(*scan);
        if (digit >= base) {
            break;  // 含 UINT32_MAX（非数字字符）
        }
        acc = acc * (uint64_t)base + digit;
        acc_digits++;
        scan++;
        if (acc_digits == group_digits) {
            err = mul_small(tmp, group_mul);
            if (err == BIGINT_OK_E) {
                err = add_small(tmp, (uint32_t)acc);
            }
            if (err != BIGINT_OK_E) {
                break;
            }
            acc = 0U;
            acc_digits = 0U;
        }
    }
    if (err == BIGINT_OK_E) {
        err = flush_group(tmp, base, acc, acc_digits);
    }
    *stop = scan;
    return err;
}

/*
 * brief: 返回 2 的幂进制 base 的每数字位数 log2(base)；调用方保证 base 为 2 的幂
 */
static uint32_t u32_log2(uint32_t base)
{
    uint32_t bits = 0U;
    while (base > 1U) {
        bits++;
        base >>= 1U;
    }
    return bits;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：补码物化与回转（负数位运算语义）                                */
/* ------------------------------------------------------------------ */

/* 内部按位操作选择 */
typedef enum {
    BIT_OP_AND_E = 0,  // 按位与
    BIT_OP_OR_E,       // 按位或
    BIT_OP_XOR_E       // 按位异或
} bit_op_ty;

/*
 * brief: 将 val 的定长补码表示写入 out[0..out_len)（符号扩展填满）
 * param: val     源对象
 * param: out     输出数组
 * param: out_len 输出长度，必须 > val->len
 * note: 负数 = 幅值按位取反加一；正数 / 零高位补 0
 */
static void tc_materialize(const bigint_bin_ty *val, uint32_t *out, size_t out_len)
{
    size_t idx = 0U;
    for (; idx < val->len; idx++) {
        out[idx] = val->limbs[idx];
    }
    for (; idx < out_len; idx++) {
        out[idx] = 0U;
    }
    if (val->sign == BIGINT_SIGN_NEG_E) {
        uint64_t carry = 1U;  // 取反加一
        for (idx = 0U; idx < out_len; idx++) {
            const uint64_t cur = (uint64_t)(~out[idx]) + carry;
            out[idx] = (uint32_t)cur;
            carry = cur >> 32U;
        }
    }
}

/*
 * brief: 将 tc_len 肢的定长补码数组解释为整数，转为符号-幅值写入 dst 并规范化
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
static bigint_err_ty tc_to_bigint(bigint_bin_ty *dst, const uint32_t *tc,
        size_t tc_len)
{
    const bigint_err_ty err = ensure_cap(dst, tc_len);
    if (err != BIGINT_OK_E) {
        return err;
    }

    const bool is_neg = (tc[tc_len - 1U] & UINT32_C(0x80000000)) != 0U;
    if (is_neg) {
        uint64_t carry = 1U;  // 幅值 = 取反加一
        for (size_t idx = 0U; idx < tc_len; idx++) {
            const uint64_t cur = (uint64_t)(~tc[idx]) + carry;
            dst->limbs[idx] = (uint32_t)cur;
            carry = cur >> 32U;
        }
        dst->len = tc_len;
        dst->sign = BIGINT_SIGN_NEG_E;
    } else {
        memcpy(dst->limbs, tc, tc_len * sizeof(uint32_t));
        dst->len = tc_len;
        dst->sign = BIGINT_SIGN_POS_E;
    }
    normalize(dst);
    return BIGINT_OK_E;
}

/*
 * brief: 补码无限符号扩展语义的逐肢按位运算核心；别名安全
 *        （结果先写入栈上临时对象，成功后 move 回 dst，失败时 dst 不变）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E；
 *         op 非法返回 BIGINT_ERR_INVALID_E
 * note: 补码长度取 max(len) + 1 肢：符号扩展肢保证结果符号正确且幅值可容纳
 */
static bigint_err_ty bit_op_core(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, bit_op_ty op)
{
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    const size_t tc_len = max_len + 1U;
    if (tc_len > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *tc_lhs = (uint32_t *)nex_malloc(tc_len * sizeof(uint32_t));
    if (tc_lhs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    uint32_t *tc_rhs = (uint32_t *)nex_malloc(tc_len * sizeof(uint32_t));
    if (tc_rhs == NULL) {
        free(tc_lhs);
        tc_lhs = NULL;
        return BIGINT_ERR_OOM_E;
    }

    tc_materialize(lhs, tc_lhs, tc_len);
    tc_materialize(rhs, tc_rhs, tc_len);
    for (size_t idx = 0U; idx < tc_len; idx++) {
        switch (op) {
        case BIT_OP_AND_E:
            tc_lhs[idx] &= tc_rhs[idx];
            break;
        case BIT_OP_OR_E:
            tc_lhs[idx] |= tc_rhs[idx];
            break;
        case BIT_OP_XOR_E:
            tc_lhs[idx] ^= tc_rhs[idx];
            break;
        default:  // 不可达：op 非登记值，按错误处理（规范 §9.1）
            free(tc_lhs);
            tc_lhs = NULL;
            free(tc_rhs);
            tc_rhs = NULL;
            return BIGINT_ERR_INVALID_E;
        }
    }

    bigint_bin_ty tmp;
    (void)bigint_bin_init(&tmp);  // 栈对象非空，恒成功
    const bigint_err_ty err = tc_to_bigint(&tmp, tc_lhs, tc_len);
    free(tc_lhs);
    tc_lhs = NULL;
    free(tc_rhs);
    tc_rhs = NULL;
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return err;
    }
    bigint_bin_move(dst, &tmp);
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：移位与加减核心                                               */
/* ------------------------------------------------------------------ */

/*
 * brief: 判断算术右移 bits = 32 * ls + bs 时被移出位中是否有 1
 * note: 调用方保证 ls < src->len；用于负数的 floor 修正
 */
static bool shr_sticky(const bigint_bin_ty *src, size_t ls, uint32_t bs)
{
    for (size_t idx = 0U; idx < ls; idx++) {
        if (src->limbs[idx] != 0U) {
            return true;
        }
    }
    if (bs > 0U) {
        const uint32_t low_mask = (UINT32_C(1) << bs) - 1U;
        if ((src->limbs[ls] & low_mask) != 0U) {
            return true;
        }
    }
    return false;
}

/*
 * brief: 逻辑右移：dst[0..len-ls) = src >> (32 * ls + bs)；允许 dst == src
 * note: 自低向高写，读下标恒大于等于写下标，原地安全；结果高肢可能为零
 */
static void shr_limbs(uint32_t *dst, const uint32_t *src, size_t len, size_t ls,
        uint32_t bs)
{
    const size_t out_len = len - ls;
    if (bs == 0U) {
        memmove(dst, src + ls, out_len * sizeof(uint32_t));
        return;
    }
    for (size_t idx = 0U; idx < out_len; idx++) {
        const uint32_t low = src[idx + ls];
        const uint32_t high = (idx + ls + 1U < len) ? src[idx + ls + 1U] : 0U;
        dst[idx] = (low >> bs) | (high << (32U - bs));
    }
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
static bigint_err_ty add_sub_core(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, bool is_sub)
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

    const int cmp = bigint_bin_cmp_abs(lhs, rhs);
    if (cmp == 0) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    const bigint_bin_ty *big = (cmp > 0) ? lhs : rhs;
    const bigint_bin_ty *small = (cmp > 0) ? rhs : lhs;
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
static bigint_err_ty add_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs, bool is_sub)
{
    if ((dst == lhs) || (dst == rhs)) {
        bigint_bin_ty tmp;
        (void)bigint_bin_init(&tmp);  // 栈对象非空，恒成功
        const bigint_err_ty err = add_sub_core(&tmp, lhs, rhs, is_sub);
        if (err != BIGINT_OK_E) {
            bigint_bin_free(&tmp);
            return err;
        }
        bigint_bin_move(dst, &tmp);
        return BIGINT_OK_E;
    }
    return add_sub_core(dst, lhs, rhs, is_sub);
}

/* ------------------------------------------------------------------ */
/* 内部辅助：字符串输出                                                   */
/* ------------------------------------------------------------------ */

/*
 * brief: 按 2^bits_per_digit 进制做位提取，正序（最高位在前）写入 out
 * param: val            源对象（非零）
 * param: bits_per_digit 每数字位数（1..5）
 * param: out            输出缓冲区，容量须 >= 幅值位长
 * return: 数字个数
 */
static size_t digits_pow2(const bigint_bin_ty *val, uint32_t bits_per_digit,
        char *out)
{
    const size_t total_bits = bigint_bin_bit_len(val);
    const size_t count = (total_bits + bits_per_digit - 1U) / bits_per_digit;
    const uint32_t mask = (UINT32_C(1) << bits_per_digit) - 1U;
    for (size_t idx = 0U; idx < count; idx++) {
        // 第 idx 个数字（自高位计）的最低位在幅值中的位置
        const size_t pos = (count - 1U - idx) * bits_per_digit;
        const size_t limb = pos / 32U;
        const uint32_t off = (uint32_t)(pos % 32U);
        uint32_t digit = val->limbs[limb] >> off;
        if ((off + bits_per_digit > 32U) && (limb + 1U < val->len)) {
            digit |= val->limbs[limb + 1U] << (32U - off);
        }
        out[idx] = digit_char(digit & mask);
    }
    return count;
}

/*
 * brief: 非 2 的幂进制输出：对幅值副本反复 div_small(base^k) 按组产出数字，
 *        正序写入 out，经 count_out 传出数字个数
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 圈复杂度约 11，源自分组 / 最高组去前导零的分支（规范 §5.4）
 */
static bigint_err_ty digits_generic(const bigint_bin_ty *val, uint32_t base,
        char *out, size_t *count_out)
{
    uint32_t group_digits = 0U;
    const uint32_t group_div = base_group(base, &group_digits);

    uint32_t *work = (uint32_t *)nex_malloc(val->len * sizeof(uint32_t));
    if (work == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    memcpy(work, val->limbs, val->len * sizeof(uint32_t));

    size_t work_len = val->len;
    size_t count = 0U;
    while (work_len > 0U) {
        uint32_t rem = div_small(work, work_len, group_div);
        while ((work_len > 0U) && (work[work_len - 1U] == 0U)) {
            work_len--;
        }
        if (work_len > 0U) {
            // 非最高组：不足 group_digits 位时前导补零
            for (uint32_t idx = 0U; idx < group_digits; idx++) {
                out[count] = digit_char(rem % base);
                count++;
                rem /= base;
            }
        } else {
            // 最高组：不补零
            while (rem > 0U) {
                out[count] = digit_char(rem % base);
                count++;
                rem /= base;
            }
        }
    }
    free(work);
    work = NULL;

    // 以上按低位组先行产出（逆序），翻转为正序
    for (size_t idx = 0U; idx < count / 2U; idx++) {
        const char tmp_ch = out[idx];
        out[idx] = out[count - 1U - idx];
        out[count - 1U - idx] = tmp_ch;
    }
    *count_out = count;
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                              */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化大整数为零
 * param: val 需要初始化的大整数变量
 * return: 成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E
 */
bigint_err_ty bigint_bin_init(bigint_bin_ty *val)
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
 * param: cap 预分配的肢容量；为 0 时等价于 bigint_bin_init
 * return: 成功返回 BIGINT_OK_E；val 为 NULL 返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E
 */
bigint_err_ty bigint_bin_init_cap(bigint_bin_ty *val, size_t cap)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    (void)bigint_bin_init(val);  // val 已判空，恒成功
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
void bigint_bin_free(bigint_bin_ty *val)
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
bigint_err_ty bigint_bin_copy(bigint_bin_ty *dst, const bigint_bin_ty *src)
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
void bigint_bin_move(bigint_bin_ty *dst, bigint_bin_ty *src)
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
bigint_err_ty bigint_bin_shrink(bigint_bin_ty *val)
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
bigint_err_ty bigint_bin_from_u64(bigint_bin_ty *val, uint64_t value)
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
bigint_err_ty bigint_bin_from_i64(bigint_bin_ty *val, int64_t value)
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
bigint_err_ty bigint_bin_to_u64(const bigint_bin_ty *val, uint64_t *out)
{
    if ((val == NULL) || (out == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->sign == BIGINT_SIGN_NEG_E) {
        return BIGINT_ERR_OVERFLOW_E;
    }
    if (val->len > 2U) {
        return BIGINT_ERR_OVERFLOW_E;
    }

    uint64_t result = 0U;
    if (val->len > 0U) {
        result = val->limbs[0];
    }
    if (val->len > 1U) {
        result |= (uint64_t)val->limbs[1] << 32U;
    }
    *out = result;
    return BIGINT_OK_E;
}

/*
 * brief: 转为 int64_t
 * return: 成功返回 BIGINT_OK_E；超出范围返回 BIGINT_ERR_OVERFLOW_E
 *         （out 不被修改）
 */
bigint_err_ty bigint_bin_to_i64(const bigint_bin_ty *val, int64_t *out)
{
    if ((val == NULL) || (out == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len > 2U) {
        return BIGINT_ERR_OVERFLOW_E;
    }

    uint64_t mag = 0U;
    if (val->len > 0U) {
        mag = val->limbs[0];
    }
    if (val->len > 1U) {
        mag |= (uint64_t)val->limbs[1] << 32U;
    }

    switch (val->sign) {
    case BIGINT_SIGN_POS_E:
        if (mag > (uint64_t)INT64_MAX) {
            return BIGINT_ERR_OVERFLOW_E;
        }
        *out = (int64_t)mag;
        break;
    case BIGINT_SIGN_NEG_E:
        if (mag > (UINT64_C(1) << 63U)) {
            return BIGINT_ERR_OVERFLOW_E;
        }
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
 * brief: 从字符串解析，支持进制 2..36；可选前导 '-' 与 base 对应前缀
 * return: 成功返回 BIGINT_OK_E；首字符即非法返回 BIGINT_ERR_PARSE_E 且
 *         val 不变；base 越界返回 BIGINT_ERR_INVALID_E；内存不足返回
 *         BIGINT_ERR_OOM_E（val 不变）
 * note: 部分消费容错；圈复杂度约 12，源自参数校验与错误归并分支（规范 §5.4）
 */
bigint_err_ty bigint_bin_from_str(bigint_bin_ty *val, const char *str,
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
    cur = str_skip_prefix(cur, base);

    bigint_bin_ty tmp;
    (void)bigint_bin_init(&tmp);  // 栈对象非空，恒成功
    const char *stop = cur;
    bigint_err_ty err;
    if (base == 10U) {
        err = parse_dec_fast(&tmp, cur, &stop);  // O(n) 快速路径
    } else {
        err = parse_digits(&tmp, cur, base, &stop);
    }
    if ((err == BIGINT_OK_E) && (stop == cur)) {
        err = BIGINT_ERR_PARSE_E;  // 符号 / 前缀后没有任何合法数字
    }
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        if (end != NULL) {
            *end = str;  // 失败视同未消费
        }
        return err;
    }

    if (tmp.len > 0U) {
        tmp.sign = is_neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }
    bigint_bin_move(val, &tmp);
    if (end != NULL) {
        *end = stop;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 转为字符串，支持进制 2..36（小写数字；负数带 '-' 前缀；零输出 "0"）
 * return: 成功返回 BIGINT_OK_E；buf 为 NULL 表示仅查询所需长度（needed 被写出，
 *         含 '\0'，仍返回 OK）；buf 非 NULL 但不足返回 BIGINT_ERR_OVERFLOW_E；
 *         base 越界返回 BIGINT_ERR_INVALID_E
 * note: 圈复杂度约 13，源自进制分派与长度 / 符号分支，逻辑线性（规范 §5.4）
 */
bigint_err_ty bigint_bin_to_str(const bigint_bin_ty *val, uint32_t base,
        char *buf, size_t buf_len, size_t *needed)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((base < 2U) || (base > 36U)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len > (SIZE_MAX - 1U) / 32U) {
        return BIGINT_ERR_OOM_E;
    }

    // 任意进制下数字个数不超过幅值位长（base 2 达到上界）
    const size_t digit_cap = (val->len > 0U) ? (val->len * 32U + 1U) : 2U;
    char *digits = (char *)nex_malloc(digit_cap);
    if (digits == NULL) {
        return BIGINT_ERR_OOM_E;
    }

    size_t digit_count = 0U;
    bigint_err_ty err = BIGINT_OK_E;
    if (val->len == 0U) {
        digits[0] = '0';
        digit_count = 1U;
    } else if (base == 10U) {
        err = digits_dec_fast(val, digits, digit_cap, &digit_count);  // O(n)
    } else if ((base & (base - 1U)) == 0U) {
        digit_count = digits_pow2(val, u32_log2(base), digits);
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
            memcpy(buf + pos, digits, digit_count);
            buf[total - 1U] = '\0';
        }
    }
    free(digits);
    digits = NULL;
    return err;
}

/* ------------------------------------------------------------------ */
/* 比较与断言                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 比较两个大整数
 * return: lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正
 */
int bigint_bin_cmp(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const int lhs_order = sign_order(lhs->sign);
    const int rhs_order = sign_order(rhs->sign);
    if (lhs_order != rhs_order) {
        return (lhs_order < rhs_order) ? -1 : 1;
    }
    if (lhs_order == 0) {
        return 0;
    }
    const int mag_cmp = bigint_bin_cmp_abs(lhs, rhs);
    return (lhs_order > 0) ? mag_cmp : -mag_cmp;
}

/*
 * brief: 比较两个大整数的绝对值
 * return: |lhs| < |rhs| 为负，相等为 0，|lhs| > |rhs| 为正
 */
int bigint_bin_cmp_abs(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
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
bigint_sign_ty bigint_bin_sign(const bigint_bin_ty *val)
{
    return val->sign;
}

/*
 * brief: 判断是否为零
 */
bool bigint_bin_is_zero(const bigint_bin_ty *val)
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
bigint_err_ty bigint_bin_add(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
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
bigint_err_ty bigint_bin_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
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
bigint_err_ty bigint_bin_neg(bigint_bin_ty *val)
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
bigint_err_ty bigint_bin_abs(bigint_bin_ty *val)
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
/* 位运算与杂项（负数按补码无限符号扩展解释）                                 */
/* ------------------------------------------------------------------ */

/*
 * brief: 算术左移 dst = src * 2^bits（符号随 src）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 自高向低写、读下标恒小于写下标，dst == src 原地安全
 */
bigint_err_ty bigint_bin_shl(bigint_bin_ty *dst, const bigint_bin_ty *src,
        size_t bits)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (src->len == 0U) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    const size_t limb_shift = bits / 32U;
    const uint32_t bit_shift = (uint32_t)(bits % 32U);
    const size_t extra = (bit_shift > 0U) ? 1U : 0U;
    const bigint_err_ty err = ensure_cap(dst, src->len + limb_shift + extra);
    if (err != BIGINT_OK_E) {
        return err;
    }

    if (bit_shift == 0U) {
        memmove(dst->limbs + limb_shift, src->limbs, src->len * sizeof(uint32_t));
        dst->len = src->len + limb_shift;
    } else {
        const uint32_t top = src->limbs[src->len - 1U] >> (32U - bit_shift);
        for (size_t idx = src->len - 1U; idx > 0U; idx--) {
            dst->limbs[idx + limb_shift] = (src->limbs[idx] << bit_shift)
                    | (src->limbs[idx - 1U] >> (32U - bit_shift));
        }
        dst->limbs[limb_shift] = src->limbs[0] << bit_shift;
        dst->len = src->len + limb_shift;
        if (top > 0U) {
            dst->limbs[dst->len] = top;
            dst->len++;
        }
    }
    if (limb_shift > 0U) {
        memset(dst->limbs, 0, limb_shift * sizeof(uint32_t));
    }
    dst->sign = src->sign;
    return BIGINT_OK_E;
}

/*
 * brief: 算术右移 dst = floor(src / 2^bits)；负数为向下取整语义
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 负数且移出位含 1 时 |结果| = (|src| >> bits) + 1，故预留 +1 肢容量
 */
bigint_err_ty bigint_bin_shr(bigint_bin_ty *dst, const bigint_bin_ty *src,
        size_t bits)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (src->len == 0U) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    const size_t limb_shift = bits / 32U;
    const uint32_t bit_shift = (uint32_t)(bits % 32U);
    if (limb_shift >= src->len) {
        // 全部移出：正数得 0；负数 floor 语义得 -1
        if (src->sign == BIGINT_SIGN_NEG_E) {
            const bigint_err_ty err = ensure_cap(dst, 1U);
            if (err != BIGINT_OK_E) {
                return err;
            }
            dst->limbs[0] = 1U;
            dst->len = 1U;
            dst->sign = BIGINT_SIGN_NEG_E;
        } else {
            dst->len = 0U;
            dst->sign = BIGINT_SIGN_ZERO_E;
        }
        return BIGINT_OK_E;
    }

    // 预留 +1 肢：负数有移出位时 +1 可能进位，保证后续 add_small 不失败
    const bigint_err_ty err = ensure_cap(dst, src->len - limb_shift + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    const bool sticky = shr_sticky(src, limb_shift, bit_shift);
    shr_limbs(dst->limbs, src->limbs, src->len, limb_shift, bit_shift);
    dst->len = src->len - limb_shift;
    if ((src->sign == BIGINT_SIGN_NEG_E) && sticky) {
        // 容量已预留，add_small 不会失败；仍检查以遵循规范 §8.6
        const bigint_err_ty add_err = add_small(dst, 1U);
        if (add_err != BIGINT_OK_E) {
            return add_err;
        }
    }
    normalize(dst);
    if (dst->len > 0U) {
        dst->sign = src->sign;
    }
    return BIGINT_OK_E;
}

/*
 * brief: 按位与（负数按补码无限符号扩展，如 -1 & x == x）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_bit_and(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    return bit_op_core(dst, lhs, rhs, BIT_OP_AND_E);
}

/*
 * brief: 按位或（负数按补码无限符号扩展，如 -1 | x == -1）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_bit_or(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    return bit_op_core(dst, lhs, rhs, BIT_OP_OR_E);
}

/*
 * brief: 按位异或（负数按补码无限符号扩展）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_bit_xor(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    return bit_op_core(dst, lhs, rhs, BIT_OP_XOR_E);
}

/*
 * brief: 测试第 bit 位（补码语义，如 -1 的任意位均为 1）
 * param: val 源对象（NULL 时防御性返回 false）
 * param: bit 位序号，从 0 计
 * return: 该位为 1 返回 true
 * note: 负数第 i 位 = (|val| - 1) 第 i 位取反（-m 的补码 = ~(m - 1)）
 */
bool bigint_bin_bit_test(const bigint_bin_ty *val, size_t bit)
{
    if ((val == NULL) || (val->sign == BIGINT_SIGN_ZERO_E)) {
        return false;
    }

    const size_t limb = bit / 32U;
    const uint32_t off = (uint32_t)(bit % 32U);
    if (val->sign == BIGINT_SIGN_POS_E) {
        if (limb >= val->len) {
            return false;
        }
        return ((val->limbs[limb] >> off) & 1U) != 0U;
    }

    // 负数：求 |val| - 1 的第 bit 位后取反
    if (limb >= val->len) {
        return true;  // |val| - 1 该位为 0，取反为 1
    }
    size_t lowest = 0U;  // 最低非零肢下标；val 非零，必然存在
    while (val->limbs[lowest] == 0U) {
        lowest++;
    }
    uint32_t word = 0U;
    if (limb < lowest) {
        word = UINT32_MAX;  // 减一借位使低于 lowest 的肢全为 1
    } else if (limb == lowest) {
        word = val->limbs[lowest] - 1U;
    } else {
        word = val->limbs[limb];
    }
    return ((word >> off) & 1U) == 0U;
}

/*
 * brief: 设置 / 清除第 bit 位（补码语义，如 bit_set(-1, 5, false) 得 -33）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: 物化定长补码后改位再转回符号-幅值；+1 肢容纳符号扩展
 */
bigint_err_ty bigint_bin_bit_set(bigint_bin_ty *val, size_t bit, bool value)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }

    const size_t need = bit / 32U + 1U;
    size_t tc_len = (val->len > need) ? val->len : need;
    tc_len += 1U;
    if (tc_len > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *tc = (uint32_t *)nex_malloc(tc_len * sizeof(uint32_t));
    if (tc == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    tc_materialize(val, tc, tc_len);

    const size_t limb = bit / 32U;
    const uint32_t mask = UINT32_C(1) << (bit % 32U);
    if (value) {
        tc[limb] |= mask;
    } else {
        tc[limb] &= ~mask;
    }

    bigint_bin_ty tmp;
    (void)bigint_bin_init(&tmp);  // 栈对象非空，恒成功
    const bigint_err_ty err = tc_to_bigint(&tmp, tc, tc_len);
    free(tc);
    tc = NULL;
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return err;
    }
    bigint_bin_move(val, &tmp);
    return BIGINT_OK_E;
}

/*
 * brief: 幅值的位长（最高有效位位置 + 1；零为 0）
 */
size_t bigint_bin_bit_len(const bigint_bin_ty *val)
{
    if ((val == NULL) || (val->len == 0U)) {
        return 0U;
    }
    size_t bits = (val->len - 1U) * 32U;
    uint32_t top = val->limbs[val->len - 1U];
    while (top != 0U) {
        bits++;
        top >>= 1U;
    }
    return bits;
}

/*
 * brief: 幅值中 1 的个数（按 |val| 计数，与符号无关）
 */
size_t bigint_bin_popcount(const bigint_bin_ty *val)
{
    if (val == NULL) {
        return 0U;
    }
    size_t count = 0U;
    for (size_t idx = 0U; idx < val->len; idx++) {
        uint32_t limb = val->limbs[idx];
        while (limb != 0U) {
            limb &= limb - 1U;  // 清除最低位的 1
            count++;
        }
    }
    return count;
}
