/*
 * nex_bigdecimal_str.c：bigdecimal_ty 十进制字符串 I/O。
 *
 * 职责（设计文档 §8.2）：bigdecimal_from_str（十进制小数 / 科学计数法 /
 * "inf" / "nan" 解析，按 ctx 正确舍入）、bigdecimal_to_str（精确输出，
 * 定点 / 科学计数两种格式）。
 *
 * 算法要点（§8.2 / §8.3）：
 *   - 解析：十进制数字累积为精确整数（bigint_dec），有效位数不超过
 *     ctx->mant_digits 时完全精确；超长时经 nex_dec_round_pack 一次舍入
 *     （无双重舍入）；
 *   - 输出：value = mant × 10^exp 直接展开（无舍入、无往返验证）——
 *     mant 为十进制整数，展开即精确表示。
 *
 * 部分消费约定（§11）：from_str 尽可能多地消费合法前缀，经 end 传出
 * 停止位置；首字符即非法返回 BIGDECIMAL_ERR_PARSE_E 且 dst 不变。
 */

#include "nex/bigdecimal/nex_bigdecimal_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 校验精度上下文（与 nex_bigdecimal.c 的 valid_ctx 一致）
 */
static bool valid_ctx(const bigdecimal_ctx_ty *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    if ((ctx->mant_digits < 1U)
            || (ctx->exp_digits < 1U)
            || (ctx->exp_digits > BIGDECIMAL_MAX_EXP_DIGITS)) {
        return false;
    }
    if ((ctx->round < BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            || (ctx->round > BIGDECIMAL_ROUND_AWAY_ZERO_E)) {
        return false;
    }
    return true;
}

/*
 * brief: bigint 错误码映射为 bigdecimal 错误码（设计文档 §3.1）
 */
static bigdecimal_err_ty map_bigint_err(bigint_err_ty err)
{
    if (err == BIGINT_OK_E) {
        return BIGDECIMAL_OK_E;
    }
    if (err == BIGINT_ERR_OOM_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    return BIGDECIMAL_ERR_INVALID_E;
}

/*
 * brief: 将 dst 置为指定特殊值（±0 / ±∞ / NaN）
 */
static void set_special(bigdecimal_ty *dst, bigdecimal_flag_ty flag)
{
    bigdecimal_free(dst);
    dst->flag = flag;
    dst->exp = 0;
}

/* ------------------------------------------------------------------ */
/* 字符串解析（§8.2）                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: 从字符串解析浮点数
 * note: 支持 "123" / "12.34" / "1e-5" / "-.5" 等十进制小数与科学计数
 *       （可选前导 '-' / '+'，'e' / 'E' 指数）、"inf"、"nan"（可选
 *       前导 '-'）；不跳过空白；部分消费容错（见文件头注释）
 */
bigdecimal_err_ty bigdecimal_from_str(bigdecimal_ty *dst, const char *str,
        const bigdecimal_ctx_ty *ctx, const char **end)
{
    if ((dst == NULL) || (str == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (end != NULL) {
        *end = str;  // 默认：未消费
    }

    const char *p = str;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        p++;
        if (end != NULL) {
            *end = p;
        }
    } else if (*p == '+') {
        p++;
        if (end != NULL) {
            *end = p;
        }
    }

    // 特殊值
    if ((p[0] == 'i') && (p[1] == 'n') && (p[2] == 'f')) {
        set_special(dst, negative ? BIGDECIMAL_NEG_INF_E
                : BIGDECIMAL_POS_INF_E);
        if (end != NULL) {
            *end = p + 3;
        }
        return BIGDECIMAL_OK_E;
    }
    if ((p[0] == 'n') && (p[1] == 'a') && (p[2] == 'n')) {
        set_special(dst, BIGDECIMAL_NAN_E);
        if (end != NULL) {
            *end = p + 3;
        }
        return BIGDECIMAL_OK_E;
    }

    // 数字部分（整数 + 可选小数）：收集数字字符到缓冲
    const size_t str_len = strlen(str);
    char *digits = (char *)malloc(str_len + 1U);
    if (digits == NULL) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    size_t ndigits = 0U;
    size_t frac_digits = 0U;
    bool in_frac = false;
    bool any_digit = false;
    while (true) {
        const char ch = *p;
        if ((ch >= '0') && (ch <= '9')) {
            digits[ndigits++] = ch;
            any_digit = true;
            if (in_frac) {
                frac_digits++;
            }
            p++;
            if (end != NULL) {
                *end = p;
            }
        } else if ((ch == '.') && !in_frac) {
            in_frac = true;
            p++;
            if (end != NULL) {
                *end = p;
            }
        } else {
            break;
        }
    }
    if (!any_digit) {
        free(digits);
        return BIGDECIMAL_ERR_PARSE_E;  // 首字符（或符号后）即非法，dst 不变
    }
    digits[ndigits] = '\0';

    // 指数部分（'e' / 'E' 后必须至少一位数字，否则不消费）
    int64_t exp_part = 0;
    if ((*p == 'e') || (*p == 'E')) {
        const char *save = p;
        p++;
        bool exp_neg = false;
        if (*p == '-') {
            exp_neg = true;
            p++;
        } else if (*p == '+') {
            p++;
        }
        bool any_exp = false;
        int64_t exp_val = 0;
        while ((*p >= '0') && (*p <= '9')) {
            any_exp = true;
            if (exp_val > (INT64_MAX - 9) / 10) {
                exp_val = INT64_MAX;  // 饱和
            } else {
                exp_val = exp_val * 10 + (int64_t)(*p - '0');
            }
            p++;
        }
        if (!any_exp) {
            p = save;  // 指数部分不消费
        } else {
            if (exp_neg) {
                exp_part = (exp_val == INT64_MAX) ? INT64_MIN : -exp_val;
            } else {
                exp_part = exp_val;
            }
            if (end != NULL) {
                *end = p;  // 指数已消费：end 指向其后首个未消费字符
            }
        }
    }

    // exp10 = exp_part − frac_digits（饱和到 int64）
    int64_t exp10;
    if (frac_digits > (uint64_t)INT64_MAX) {
        exp10 = INT64_MIN;
    } else {
        const int64_t fd = (int64_t)frac_digits;
        if ((exp_part > 0) && (exp_part > INT64_MAX - fd)) {
            exp10 = INT64_MAX;
        } else if ((exp_part < 0) && (exp_part < INT64_MIN + fd)) {
            exp10 = INT64_MIN;
        } else {
            exp10 = exp_part - fd;
        }
    }

    // 构建十进制整数尾数（digits 缓冲为纯数字串）
    bigint_dec_ty mant;
    bigint_err_ty berr = bigint_dec_init(&mant);
    if (berr != BIGINT_OK_E) {
        free(digits);
        return map_bigint_err(berr);
    }
    berr = bigint_dec_from_str(&mant, digits, 10, NULL);
    free(digits);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&mant);
        return map_bigint_err(berr);
    }

    const bigdecimal_err_ty ferr = nex_dec_round_pack(dst, &mant, exp10,
            false, negative ? -1 : 1, ctx);
    bigint_dec_free(&mant);
    return ferr;
}

/* ------------------------------------------------------------------ */
/* 精确输出（§8.2）                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 输出十进制表示（value = mant × 10^exp 直接展开）
 * note: 定点格式（FMT_FIXED）：如 "12345.67"、"0.00123"、"12300"；
 *       科学计数（FMT_SCIENTIFIC）："d[.ddd]e±X"（X = exp + 位数 − 1）
 */
bigdecimal_err_ty bigdecimal_to_str(const bigdecimal_ty *src,
        bigdecimal_fmt_ty fmt, char *buf, size_t buf_len, size_t *needed)
{
    if (src == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if ((fmt != BIGDECIMAL_FMT_FIXED_E)
            && (fmt != BIGDECIMAL_FMT_SCIENTIFIC_E)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    // 特殊值固定输出
    const char *fixed = NULL;
    switch (src->flag) {
        case BIGDECIMAL_POS_ZERO_E:
            fixed = "0";
            break;
        case BIGDECIMAL_NEG_ZERO_E:
            fixed = "-0";
            break;
        case BIGDECIMAL_POS_INF_E:
            fixed = "inf";
            break;
        case BIGDECIMAL_NEG_INF_E:
            fixed = "-inf";
            break;
        case BIGDECIMAL_NAN_E:
            fixed = "nan";
            break;
        default:
            break;
    }
    if (fixed != NULL) {
        const size_t need = strlen(fixed) + 1U;
        if (needed != NULL) {
            *needed = need;
        }
        if (buf == NULL) {
            return BIGDECIMAL_OK_E;  // 仅查询
        }
        if (buf_len < need) {
            return BIGDECIMAL_ERR_OVERFLOW_E;
        }
        memcpy(buf, fixed, need);
        return BIGDECIMAL_OK_E;
    }

    // 正常值：mant × 10^exp 精确展开
    const bool negative = (src->flag == BIGDECIMAL_NEG_E);
    const int64_t e = src->exp;

    size_t mant_need = 0;
    if (bigint_dec_to_str(&src->mant, 10, NULL, 0, &mant_need)
            != BIGINT_OK_E) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    char *mant_str = (char *)malloc(mant_need);
    if (mant_str == NULL) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    if (bigint_dec_to_str(&src->mant, 10, mant_str, mant_need, &mant_need)
            != BIGINT_OK_E) {
        free(mant_str);
        return BIGDECIMAL_ERR_INVALID_E;
    }
    const size_t dl = mant_need - 1U;  // 不含 '\0'

    size_t len = 0;
    bool scientific = (fmt == BIGDECIMAL_FMT_SCIENTIFIC_E);
    int64_t sci_exp = 0;
    if (scientific) {
        // d[.ddd]e±X：X = e + dl − 1
        sci_exp = e + (int64_t)dl - 1;
        uint64_t mag = (sci_exp < 0)
                ? (uint64_t)(-(sci_exp + 1)) + 1U : (uint64_t)sci_exp;
        size_t exp_digits_len = 1U;
        while (mag >= 10U) {
            mag /= 10U;
            exp_digits_len++;
        }
        len = 1U + ((dl > 1U) ? (1U + (dl - 1U)) : 0U) + 2U + exp_digits_len;
    } else if (e >= 0) {
        // mant 后补 e 个零
        if ((uint64_t)e > (size_t)-1 - dl) {
            free(mant_str);
            return BIGDECIMAL_ERR_OVERFLOW_E;  // 输出超可寻址长度
        }
        len = dl + (size_t)e;
    } else {
        // 小数点左移 |e| 位
        const uint64_t nd = (uint64_t)(-(e + 1)) + 1U;  // |e| ≥ 1
        if (nd >= dl) {
            // "0." + 前导零 + 数字
            len = 2U + (size_t)(nd - dl) + dl;
        } else {
            len = dl + 1U;
        }
    }
    if (negative) {
        len += 1U;  // '-' 前缀
    }
    const size_t need_total = len + 1U;  // 含 '\0'
    if (needed != NULL) {
        *needed = need_total;
    }
    if (buf == NULL) {
        free(mant_str);
        return BIGDECIMAL_OK_E;  // 仅查询
    }
    if (buf_len < need_total) {
        free(mant_str);
        return BIGDECIMAL_ERR_OVERFLOW_E;
    }

    size_t pos = 0U;
    if (negative) {
        buf[pos++] = '-';
    }
    if (scientific) {
        buf[pos++] = mant_str[0];
        if (dl > 1U) {
            buf[pos++] = '.';
            for (size_t i = 1U; i < dl; i++) {
                buf[pos++] = mant_str[i];
            }
        }
        buf[pos++] = 'e';
        if (sci_exp < 0) {
            buf[pos++] = '-';
        } else {
            buf[pos++] = '+';
        }
        uint64_t mag = (sci_exp < 0)
                ? (uint64_t)(-(sci_exp + 1)) + 1U : (uint64_t)sci_exp;
        char tmp[24];
        size_t tl = 0U;
        do {
            tmp[tl++] = (char)('0' + (mag % 10U));
            mag /= 10U;
        } while (mag > 0U);
        while (tl > 0U) {
            buf[pos++] = tmp[--tl];
        }
    } else if (e >= 0) {
        for (size_t i = 0U; i < dl; i++) {
            buf[pos++] = mant_str[i];
        }
        for (size_t i = 0U; i < (size_t)e; i++) {
            buf[pos++] = '0';
        }
    } else {
        const uint64_t nd = (uint64_t)(-(e + 1)) + 1U;  // |e| ≥ 1
        if (nd >= dl) {
            buf[pos++] = '0';
            buf[pos++] = '.';
            for (uint64_t i = 0U; i < nd - dl; i++) {
                buf[pos++] = '0';
            }
            for (size_t i = 0U; i < dl; i++) {
                buf[pos++] = mant_str[i];
            }
        } else {
            for (size_t i = 0U; i < dl - (size_t)nd; i++) {
                buf[pos++] = mant_str[i];
            }
            buf[pos++] = '.';
            for (size_t i = dl - (size_t)nd; i < dl; i++) {
                buf[pos++] = mant_str[i];
            }
        }
    }
    buf[pos] = '\0';
    free(mant_str);
    return BIGDECIMAL_OK_E;
}
