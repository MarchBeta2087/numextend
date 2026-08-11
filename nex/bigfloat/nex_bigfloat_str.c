/*
 * nex_bigfloat_str.c：bigfloat_ty 十进制字符串 I/O。
 *
 * 职责（设计文档 §7.4 / §7.5）：bigfloat_from_str（十进制小数 / 科学
 * 计数法 / "inf" / "nan" 解析，按 ctx 正确舍入）、bigfloat_to_str
 * （最短往返输出）、内部共享 nex_bf_from_decimal（十进制精确有理数 →
 * bigfloat）与 nex_bf_dec_round（最近舍入到 n 位十进制有效数字）。
 *
 * 算法要点（§7.5）：
 *   - 解析：十进制数字累积为精确有理数 decimal_mant × 10^exp10，经
 *     nex_bf_from_decimal 一次性除以 10^k（商保留 mant_bits + 2 位
 *     保护位，余数置粘位）舍入，全程无双重舍入；
 *   - 输出：从 1 位有效数字起递增生成候选（nex_bf_dec_round），试解析
 *     回并精确比较（bigfloat_cmp），首个往返成功的候选即最短。to_str
 *     无 ctx 参数，往返验证实现为"候选串的精确值等于 src"——解析验证
 *     用精度足够大的上下文，保证候选串（≤ n_limit 位十进制）按任何
 *     不低于 src 表示精度的 ctx 解析都恢复 src；
 *   - dec_round：十进制科学指数先以 double 估算、再经 10 的幂整数比较
 *     修正（至多 2 次）；舍入到 n 位采用 round-half-up（往返验证兜底，
 *     ties 方向不影响最短性正确性）；
 *   - 输出格式约定（%g 风格，见 to_str 注释）：科学计数当 dec_exp > 21
 *     或 dec_exp ≤ −5，否则定点。
 *
 * 部分消费约定（§11）：from_str 尽可能多地消费合法前缀，经 end 传出
 * 停止位置；首字符即非法返回 BIGFLOAT_ERR_PARSE_E 且 dst 不变。
 */

#include "nex/bigfloat/nex_bigfloat_internal.h"
#include "nex/nex_alloc.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * brief: 校验精度上下文（与 nex_bigfloat.c 的 valid_ctx 一致）
 */
static bool valid_ctx(const bigfloat_ctx_ty *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    if ((ctx->mant_bits < 2U) || (ctx->exp_bits < 1U)
            || (ctx->exp_bits > BIGFLOAT_MAX_EXP_BITS)) {
        return false;
    }
    if ((ctx->round < BIGFLOAT_ROUND_NEAREST_EVEN_E)
            || (ctx->round > BIGFLOAT_ROUND_AWAY_ZERO_E)) {
        return false;
    }
    return true;
}

/*
 * brief: bigint 错误码映射为 bigfloat 错误码（设计文档 §3.1）
 */
static bigfloat_err_ty map_bigint_err(bigint_err_ty err)
{
    if (err == BIGINT_OK_E) {
        return BIGFLOAT_OK_E;
    }
    if (err == BIGINT_ERR_OOM_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    return BIGFLOAT_ERR_INVALID_E;
}

/*
 * brief: 将 dst 置为指定特殊值（±0 / ±∞ / NaN）
 */
static void set_special(bigfloat_ty *dst, bigfloat_flag_ty flag)
{
    bigfloat_free(dst);
    dst->flag = flag;
    dst->exp = 0;
}

/*
 * brief: 比较 m × 2^e 与 10 的幂 p10（两者均为正）
 * return: lhs < rhs 为负，相等为 0，lhs > rhs 为正
 * note: 先用位长快速判定（区间不重叠时免分配），重叠时精确比较；
 *       内存不足防御性返回 0（调用方修正循环提前终止，结果仍正确）
 */
static int cmp_value_pow10(const bigint_bin_ty *m, int64_t e,
        const bigint_bin_ty *p10)
{
    const int64_t blm = (int64_t)bigint_bin_bit_len(m);
    const int64_t blp = (int64_t)bigint_bin_bit_len(p10);
    const int64_t vbl = blm + e;  // e 正常范围（round_pack / from_f64 输出），不溢出
    if (vbl > blp + 1) {
        return 1;  // value ≥ 2^(vbl−1) ≥ 2^(blp+1) > p10
    }
    if (vbl < blp - 1) {
        return -1;  // value < 2^vbl ≤ 2^(blp−1) ≤ p10
    }

    bigint_bin_ty tmp;
    bigint_err_ty berr = bigint_bin_init(&tmp);
    if (berr != BIGINT_OK_E) {
        return 0;
    }
    int result = 0;
    if (e >= 0) {
        berr = bigint_bin_shl(&tmp, m, (size_t)e);
        if (berr == BIGINT_OK_E) {
            result = bigint_bin_cmp(&tmp, p10);
        }
    } else {
        berr = bigint_bin_shl(&tmp, p10, (size_t)(-e));
        if (berr == BIGINT_OK_E) {
            result = bigint_bin_cmp(m, &tmp);
        }
    }
    bigint_bin_free(&tmp);
    return result;
}

/*
 * brief: 比较 value = m × 2^e 与 10^de（de 可为负；m、value 均 > 0）
 * return: value < 10^de 为负，相等为 0，大于为正；OOM 防御性返回 0
 * note: de ≥ 0 走 cmp_value_pow10；de < 0 时 10^de = 1/10^|de|，
 *       value ≥ 10^de ⟺ m × 2^e × 10^|de| ≥ 1：
 *       - e ≥ 0 时 m × 2^e ≥ 1 > 10^de 恒成立 → 返回 1；
 *       - e < 0 时两边同乘 2^(−e) 得整数比较 m × 10^|de| vs 2^(−e)。
 *       避免对负指数做 bigint_bin_pow（会因 (uint64_t)de 巨大而爆炸）。
 */
static int cmp_value_pow10_signed(const bigint_bin_ty *m, int64_t e,
        int64_t de, const bigint_bin_ty *ten)
{
    if (de >= 0) {
        bigint_bin_ty p10;
        bigint_err_ty berr = bigint_bin_init(&p10);
        if (berr != BIGINT_OK_E) {
            return 0;
        }
        berr = bigint_bin_pow(&p10, ten, (uint64_t)de);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&p10);
            return 0;
        }
        const int result = cmp_value_pow10(m, e, &p10);
        bigint_bin_free(&p10);
        return result;
    }
    if (e >= 0) {
        return 1;  // value ≥ 1 > 10^de（m ≥ 1、2^e ≥ 1、10^|de| ≥ 1）
    }
    // e < 0：m × 10^|de| vs 2^(−e)
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_err_ty berr = bigint_bin_init(&a);
    if (berr != BIGINT_OK_E) {
        return 0;
    }
    berr = bigint_bin_init(&b);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&a);
        return 0;
    }
    berr = bigint_bin_pow(&a, ten, (uint64_t)(-de));
    if (berr == BIGINT_OK_E) {
        berr = bigint_bin_mul(&a, &a, m);
    }
    if (berr == BIGINT_OK_E) {
        berr = bigint_bin_from_u64(&b, 1U);
    }
    if (berr == BIGINT_OK_E) {
        berr = bigint_bin_shl(&b, &b, (size_t)(-e));
    }
    int result = 0;
    if (berr == BIGINT_OK_E) {
        result = bigint_bin_cmp(&a, &b);
    }
    bigint_bin_free(&a);
    bigint_bin_free(&b);
    return result;
}

/*
 * brief: 将整数 bigint 转十进制串（内部辅助，复用 bigint_bin_to_str）
 * param: val   源整数（非负）
 * param: buf   输出缓冲（长度 ≥ need）
 * param: need  传入缓冲长度；传出含 '\0' 所需长度（可 NULL）
 * return: 成功返回 BIGINT_OK_E；缓冲不足返回 BIGINT_ERR_OVERFLOW_E
 */
static bigint_err_ty to_dec_str(const bigint_bin_ty *val, char *buf,
        size_t buf_len, size_t *need)
{
    return bigint_bin_to_str(val, 10U, buf, buf_len, need);
}

/* ------------------------------------------------------------------ */
/* 十进制精确有理数 → bigfloat（内部共享）                              */
/* ------------------------------------------------------------------ */

/*
 * brief: 将十进制值 value = ±decimal_mant × 10^exp10 按 ctx 正确舍入为
 *        bigfloat（from_str 的内部共享实现）
 * note: 正确舍入策略：
 *       - exp10 ≥ 0：精确整数（乘 10^exp10 后 round_pack，无粘位）；
 *       - exp10 < 0：除以 10^k（k = −exp10）。先算 10^k 的位长，再缩放
 *         使商位长 = mant_bits + 2（保护位），余数非零置粘位——商位长
 *         大于 mant_bits，满足 nex_bf_round_pack 的粘位契约（正确舍入）。
 */
bigfloat_err_ty nex_bf_from_decimal(bigfloat_ty *dst,
        const bigint_bin_ty *decimal_mant, int64_t exp10, bool negative,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (decimal_mant == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigint_bin_is_zero(decimal_mant)) {
        set_special(dst, negative ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        return BIGFLOAT_OK_E;
    }
    const int sign = negative ? -1 : 1;

    bigint_bin_ty ten;
    bigint_err_ty berr = bigint_bin_init(&ten);
    if (berr != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_from_u64(&ten, 10U);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        return map_bigint_err(berr);
    }

    if (exp10 >= 0) {
        // 精确整数路径
        bigint_bin_ty pow10;
        bigint_bin_ty raw;
        berr = bigint_bin_init(&pow10);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&ten);
            return BIGFLOAT_ERR_OOM_E;
        }
        berr = bigint_bin_init(&raw);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&ten);
            bigint_bin_free(&pow10);
            return BIGFLOAT_ERR_OOM_E;
        }
        berr = bigint_bin_pow(&pow10, &ten, (uint64_t)exp10);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&ten);
            bigint_bin_free(&pow10);
            bigint_bin_free(&raw);
            return map_bigint_err(berr);
        }
        berr = bigint_bin_mul(&raw, decimal_mant, &pow10);
        bigint_bin_free(&ten);
        bigint_bin_free(&pow10);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&raw);
            return map_bigint_err(berr);
        }
        {
            const bigfloat_err_ty ferr = nex_bf_round_pack(dst, &raw, 0,
                    false, sign, ctx);
            bigint_bin_free(&raw);
            return ferr;
        }
    }

    // exp10 < 0：除以 10^k，k = −exp10
    const uint64_t k = (uint64_t)(-(exp10 + 1)) + 1U;
    bigint_bin_ty pow10;
    berr = bigint_bin_init(&pow10);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_pow(&pow10, &ten, k);
    bigint_bin_free(&ten);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&pow10);
        return map_bigint_err(berr);
    }

    // 缩放使商位长 = mant_bits + 2：s = (p + 2) − blm + blp
    const size_t blm = bigint_bin_bit_len(decimal_mant);
    const size_t blp = bigint_bin_bit_len(&pow10);
    const int64_t s = (int64_t)ctx->mant_bits + 2 - (int64_t)blm + (int64_t)blp;

    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_ty quot;
    bigint_bin_ty rem;
    berr = bigint_bin_init(&num);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&pow10);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&den);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&pow10);
        bigint_bin_free(&num);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&quot);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&pow10);
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&rem);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&pow10);
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        bigint_bin_free(&quot);
        return BIGFLOAT_ERR_OOM_E;
    }

    if (s > 0) {
        berr = bigint_bin_shl(&num, decimal_mant, (size_t)s);
        if (berr != BIGINT_OK_E) {
            goto dec_cleanup;
        }
        berr = bigint_bin_copy(&den, &pow10);
        if (berr != BIGINT_OK_E) {
            goto dec_cleanup;
        }
    } else if (s < 0) {
        berr = bigint_bin_copy(&num, decimal_mant);
        if (berr != BIGINT_OK_E) {
            goto dec_cleanup;
        }
        berr = bigint_bin_shl(&den, &pow10, (size_t)(-s));
        if (berr != BIGINT_OK_E) {
            goto dec_cleanup;
        }
    } else {
        berr = bigint_bin_copy(&num, decimal_mant);
        if (berr != BIGINT_OK_E) {
            goto dec_cleanup;
        }
        berr = bigint_bin_copy(&den, &pow10);
        if (berr != BIGINT_OK_E) {
            goto dec_cleanup;
        }
    }
    berr = bigint_bin_div_rem(&quot, &rem, &num, &den);
    if (berr != BIGINT_OK_E) {
        goto dec_cleanup;
    }
    {
        const bool sticky = !bigint_bin_is_zero(&rem);
        const bigfloat_err_ty ferr = nex_bf_round_pack(dst, &quot, -s,
                sticky, sign, ctx);
        berr = (ferr == BIGFLOAT_OK_E) ? BIGINT_OK_E
                : (ferr == BIGFLOAT_ERR_OOM_E) ? BIGINT_ERR_OOM_E
                : BIGINT_ERR_INVALID_E;
    }

dec_cleanup:
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    bigint_bin_free(&quot);
    bigint_bin_free(&rem);
    bigint_bin_free(&pow10);
    return map_bigint_err(berr);
}

/* ------------------------------------------------------------------ */
/* 字符串解析（§7.4）                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: 从字符串解析浮点数
 * note: 支持 "123" / "12.34" / "1e-5" / "-.5" 等十进制小数与科学计数
 *       （可选前导 '-' / '+'，'e' / 'E' 指数）、"inf"、"nan"（可选
 *       前导 '-'）；不跳过空白；部分消费容错（见文件头注释）
 */
bigfloat_err_ty bigfloat_from_str(bigfloat_ty *dst, const char *str,
        const bigfloat_ctx_ty *ctx, const char **end)
{
    if ((dst == NULL) || (str == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
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
        set_special(dst, negative ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E);
        if (end != NULL) {
            *end = p + 3;
        }
        return BIGFLOAT_OK_E;
    }
    if ((p[0] == 'n') && (p[1] == 'a') && (p[2] == 'n')) {
        set_special(dst, BIGFLOAT_NAN_E);
        if (end != NULL) {
            *end = p + 3;
        }
        return BIGFLOAT_OK_E;
    }

    // 数字部分（整数 + 可选小数）
    bigint_bin_ty acc;
    bigint_err_ty berr = bigint_bin_init(&acc);
    if (berr != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    bigint_bin_ty ten;
    berr = bigint_bin_init(&ten);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&acc);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_from_u64(&ten, 10U);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&acc);
        bigint_bin_free(&ten);
        return map_bigint_err(berr);
    }

    bool any_digit = false;
    bool in_frac = false;
    uint64_t frac_digits = 0U;
    while (true) {
        const char ch = *p;
        if ((ch >= '0') && (ch <= '9')) {
            // acc = acc × 10 + digit
            berr = bigint_bin_mul(&acc, &acc, &ten);
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&acc);
                bigint_bin_free(&ten);
                return map_bigint_err(berr);
            }
            berr = bigint_bin_from_u64(&ten, (uint64_t)(ch - '0'));
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&acc);
                bigint_bin_free(&ten);
                return map_bigint_err(berr);
            }
            berr = bigint_bin_add(&acc, &acc, &ten);
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&acc);
                bigint_bin_free(&ten);
                return map_bigint_err(berr);
            }
            berr = bigint_bin_from_u64(&ten, 10U);
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&acc);
                bigint_bin_free(&ten);
                return map_bigint_err(berr);
            }
            any_digit = true;
            if (in_frac && (frac_digits < UINT64_MAX)) {
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
        bigint_bin_free(&acc);
        bigint_bin_free(&ten);
        return BIGFLOAT_ERR_PARSE_E;  // 首字符（或符号后）即非法，dst 不变
    }

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
        exp10 = INT64_MIN;  // 小数位数远超 int64 → 结果极小 → 下溢饱和
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

    const bigfloat_err_ty ferr = nex_bf_from_decimal(dst, &acc, exp10,
            negative, ctx);
    bigint_bin_free(&acc);
    bigint_bin_free(&ten);
    return ferr;
}

/* ------------------------------------------------------------------ */
/* 最近舍入到 n 位十进制有效数字（内部共享）                            */
/* ------------------------------------------------------------------ */

/*
 * brief: 将 src（正常值）最近舍入到 n 位十进制有效数字
 * param: src        源浮点数（必须为正常值）
 * param: n          有效数字位数（≥ 1）
 * param: digits     输出缓冲，接收恰好 n 位数字（不含小数点 / 符号 /
 *                   指数，无前导零）
 * param: digits_len digits 缓冲长度（必须 ≥ n + 1，含 '\0'）
 * param: needed     若非 NULL，传出含 '\0' 的所需长度
 * param: dec_exp    若非 NULL，传出十进制科学指数（value = digits 的
 *                   n 位整数 × 10^(dec_exp − n)，即小数点位于第 1 位
 *                   数字之后时指数为 dec_exp − 1）
 * return: 成功返回 BIGFLOAT_OK_E；digits 不足返回 BIGFLOAT_ERR_OVERFLOW_E
 *         （needed 写出）；src 非有限值返回 BIGFLOAT_ERR_INVALID_E
 * note: 舍入采用 round-half-up；正确性由 to_str 的往返验证兜底
 */
bigfloat_err_ty nex_bf_dec_round(const bigfloat_ty *src, size_t n,
        char *digits, size_t digits_len, size_t *needed, int64_t *dec_exp)
{
    if (needed != NULL) {
        *needed = n + 1U;  // 先写出所需长度（含 '\0'）
    }
    if ((src == NULL) || (digits == NULL) || !bigfloat_is_normal(src)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (n < 1U) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (digits_len < n + 1U) {
        return BIGFLOAT_ERR_OVERFLOW_E;
    }

    const bigint_bin_ty *m = &src->mant;
    const int64_t e = src->exp;
    const size_t bl = bigint_bin_bit_len(m);

    bigint_bin_ty ten;
    bigint_err_ty berr = bigint_bin_init(&ten);
    if (berr != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_from_u64(&ten, 10U);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        return map_bigint_err(berr);
    }

    // 1. 十进制科学指数：估算（double）+ 整数比较修正
    //    value = m × 2^e，log10(value) ≈ (bl − 1 + e) × log10(2)
    const double approx = ((double)bl - 1.0 + (double)e) * 0.301029995663981198;
    int64_t de = (int64_t)floor(approx) + 1;

    bigint_bin_ty p10;
    berr = bigint_bin_init(&p10);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        return BIGFLOAT_ERR_OOM_E;
    }

    // 修正：value ≥ 10^de → de++（正常情况估算误差 ≤ 2；de 可为负，
    // cmp_value_pow10_signed 内部避免负指数 pow）。上限 64 防 OOM 防御
    // 返回 0 时无限递增——若仍不收敛说明指数极端到无法表示（后续运算
    // 会以 OOM 快速失败，to_str 的往返验证兜底报错）。
    int corr = 0;
    while (corr < 64) {
        const int c = cmp_value_pow10_signed(m, e, de, &ten);
        if (c < 0) {
            break;  // value < 10^de，估算成立
        }
        de++;  // value ≥ 10^de（含 OOM 防御返回 0 的保守处理）
        corr++;
    }
    // 修正：value < 10^(de−1) → de−−（de ≤ 1 时不可能有下溢方向修正）
    while (de > 1) {
        berr = bigint_bin_pow(&p10, &ten, (uint64_t)(de - 1));
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&ten);
            bigint_bin_free(&p10);
            return map_bigint_err(berr);
        }
        if (cmp_value_pow10(m, e, &p10) < 0) {
            de--;
            continue;
        }
        break;
    }

    // 2. 舍入到 n 位：R = value × 10^(n − de) = 分子 / 分母
    //    分子 = m × 10^max(t,0) × 2^max(e,0)，分母 = 10^max(−t,0) × 2^max(−e,0)
    //    t = n − de
    const int64_t t = (int64_t)n - de;
    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_ty quot;
    bigint_bin_ty rem;
    berr = bigint_bin_init(&num);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        bigint_bin_free(&p10);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&den);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        bigint_bin_free(&p10);
        bigint_bin_free(&num);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&quot);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        bigint_bin_free(&p10);
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&rem);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&ten);
        bigint_bin_free(&p10);
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        bigint_bin_free(&quot);
        return BIGFLOAT_ERR_OOM_E;
    }

    if (t >= 0) {
        berr = bigint_bin_pow(&p10, &ten, (uint64_t)t);
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
        berr = bigint_bin_mul(&num, m, &p10);
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
        berr = bigint_bin_from_u64(&den, 1U);  // 分母 = 1（此前未赋值，保持 0 → 除零）
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
    } else {
        berr = bigint_bin_pow(&p10, &ten, (uint64_t)(-t));
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
        berr = bigint_bin_copy(&den, &p10);
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
        berr = bigint_bin_copy(&num, m);
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
    }
    if (e >= 0) {
        berr = bigint_bin_shl(&num, &num, (size_t)e);
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
    } else {
        berr = bigint_bin_shl(&den, &den, (size_t)(-e));
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
    }
    berr = bigint_bin_div_rem(&quot, &rem, &num, &den);
    if (berr != BIGINT_OK_E) {
        goto round_cleanup;
    }

    // 舍入 round-half-up：余数 × 2 ≥ 分母 → 进位
    if (!bigint_bin_is_zero(&rem)) {
        berr = bigint_bin_shl(&rem, &rem, 1U);
        if (berr != BIGINT_OK_E) {
            goto round_cleanup;
        }
        if (bigint_bin_cmp(&rem, &den) >= 0) {
            bigint_bin_ty one;
            berr = bigint_bin_init(&one);
            if (berr != BIGINT_OK_E) {
                goto round_cleanup;
            }
            berr = bigint_bin_from_u64(&one, UINT64_C(1));
            if (berr == BIGINT_OK_E) {
                berr = bigint_bin_add(&quot, &quot, &one);
            }
            bigint_bin_free(&one);
            if (berr != BIGINT_OK_E) {
                goto round_cleanup;
            }
        }
    }

    // 3. quot ∈ [10^(n−1), 10^n]：转十进制，取前 n 位
    {
        char *tmp_buf = (char *)nex_malloc(n + 2U);
        if (tmp_buf == NULL) {
            berr = BIGINT_ERR_OOM_E;
            goto round_cleanup;
        }
        size_t need = 0U;
        berr = to_dec_str(&quot, tmp_buf, n + 2U, &need);
        if (berr != BIGINT_OK_E) {
            free(tmp_buf);
            goto round_cleanup;
        }
        const size_t str_len = need - 1U;  // 不含 '\0'
        if (str_len == n + 1U) {
            // 进位到 10^n：digits = "1" + (n−1) 个 '0'，指数 +1
            digits[0] = '1';
            for (size_t idx = 1U; idx < n; idx++) {
                digits[idx] = '0';
            }
            de++;
        } else if (str_len == n) {
            memcpy(digits, tmp_buf, n);
        } else {
            free(tmp_buf);
            berr = BIGINT_ERR_INVALID_E;  // 防御：位长不变量被破坏
            goto round_cleanup;
        }
        digits[n] = '\0';
        free(tmp_buf);
    }
    if (dec_exp != NULL) {
        *dec_exp = de;
    }
    berr = BIGINT_OK_E;

round_cleanup:
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    bigint_bin_free(&quot);
    bigint_bin_free(&rem);
    bigint_bin_free(&p10);
    bigint_bin_free(&ten);
    return map_bigint_err(berr);
}

/* ------------------------------------------------------------------ */
/* 最短往返输出（§7.4）                                                */
/* ------------------------------------------------------------------ */

/*
 * brief: 将 n 位有效数字与科学指数格式化为十进制串
 * param: digits  n 位数字（首位非零，无小数点）
 * param: n       有效数字位数
 * param: de      科学指数（value ≈ digits × 10^(de − n)）
 * param: out     输出缓冲
 * param: out_len 缓冲长度
 * param: needed  若非 NULL，传出含 '\0' 所需长度
 * return: 成功返回 BIGFLOAT_OK_E；缓冲不足返回 BIGFLOAT_ERR_OVERFLOW_E
 * note: 格式约定：dec_exp（= de）> 21 或 ≤ −5 用科学计数
 *       "d[.ddd]e±X"；否则定点（整数部分位数 = de，de ≤ 0 时
 *       "0.00…ddd"）
 */
static bigfloat_err_ty format_digits(const char *digits, size_t n, int64_t de,
        char *out, size_t out_len, size_t *needed)
{
    // 计算所需长度
    size_t len;
    bool scientific = (de > 21) || (de <= -5);
    if (scientific) {
        // d[.ddd]e±X：1 + (n>1 ? n : 0) + 2 + 指数位数
        const int64_t ev = de - 1;
        uint64_t mag = (ev < 0) ? (uint64_t)(-(ev + 1)) + 1U : (uint64_t)ev;
        size_t exp_digits = 1U;
        while (mag >= 10U) {
            mag /= 10U;
            exp_digits++;
        }
        len = 1U + ((n > 1U) ? (1U + (n - 1U)) : 0U) + 2U + exp_digits;
    } else if (de >= 1) {
        const size_t int_digits = (size_t)de;
        len = int_digits;
        if (de < (int64_t)n) {
            len += 1U + (n - int_digits);  // '.' + 小数部分
        }
    } else {
        len = 2U + (size_t)(-de) + n;  // "0." + 前导零 + digits
    }
    if (needed != NULL) {
        *needed = len + 1U;
    }
    if (out == NULL) {
        return BIGFLOAT_OK_E;  // 仅查询
    }
    if (out_len < len + 1U) {
        return BIGFLOAT_ERR_OVERFLOW_E;
    }

    size_t pos = 0U;
    if (scientific) {
        out[pos++] = digits[0];
        if (n > 1U) {
            out[pos++] = '.';
            for (size_t idx = 1U; idx < n; idx++) {
                out[pos++] = digits[idx];
            }
        }
        out[pos++] = 'e';
        const int64_t ev = de - 1;
        if (ev < 0) {
            out[pos++] = '-';
        } else {
            out[pos++] = '+';
        }
        uint64_t mag = (ev < 0) ? (uint64_t)(-(ev + 1)) + 1U : (uint64_t)ev;
        char tmp[24];
        size_t tl = 0U;
        do {
            tmp[tl++] = (char)('0' + (mag % 10U));
            mag /= 10U;
        } while (mag > 0U);
        while (tl > 0U) {
            out[pos++] = tmp[--tl];
        }
    } else if (de >= 1) {
        const size_t int_digits = (size_t)de;
        size_t idx = 0U;
        for (; (idx < int_digits) && (idx < n); idx++) {
            out[pos++] = digits[idx];
        }
        for (; idx < int_digits; idx++) {
            out[pos++] = '0';  // 整数部分补零
        }
        if (de < (int64_t)n) {
            out[pos++] = '.';
            for (; idx < n; idx++) {
                out[pos++] = digits[idx];
            }
        }
    } else {
        out[pos++] = '0';
        out[pos++] = '.';
        const size_t zeros = (size_t)(-de);
        for (size_t idx = 0U; idx < zeros; idx++) {
            out[pos++] = '0';
        }
        for (size_t idx = 0U; idx < n; idx++) {
            out[pos++] = digits[idx];
        }
    }
    out[pos] = '\0';
    return BIGFLOAT_OK_E;
}

/*
 * brief: 输出"最短且能按相同 ctx 往返"的十进制表示
 * note: 无 ctx 参数，往返验证实现为"候选串的精确值等于 src"：候选串用
 *       nex_bf_dec_round 生成（n 位有效数字，n 从 1 递增），再用精度
 *       足够大的验证上下文解析回，与 src 精确比较（bigfloat_cmp）。
 *       max_digits 限制有效数字位数上限；为 0 表示不限制。格式约定
 *       （%g 风格）：dec_exp > 21 或 dec_exp ≤ −5 用科学计数，否则定点
 */
bigfloat_err_ty bigfloat_to_str(const bigfloat_ty *src, size_t max_digits,
        char *buf, size_t buf_len, size_t *needed)
{
    if (src == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    // 特殊值固定输出
    const char *fixed = NULL;
    switch (src->flag) {
        case BIGFLOAT_POS_ZERO_E:
            fixed = "0";
            break;
        case BIGFLOAT_NEG_ZERO_E:
            fixed = "-0";
            break;
        case BIGFLOAT_POS_INF_E:
            fixed = "inf";
            break;
        case BIGFLOAT_NEG_INF_E:
            fixed = "-inf";
            break;
        case BIGFLOAT_NAN_E:
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
            return BIGFLOAT_OK_E;  // 仅查询
        }
        if (buf_len < need) {
            return BIGFLOAT_ERR_OVERFLOW_E;
        }
        memcpy(buf, fixed, need);
        return BIGFLOAT_OK_E;
    }

    // 正常值：最短往返
    const size_t bl = bigint_bin_bit_len(&src->mant);
    size_t n_limit = max_digits;
    if (n_limit == 0U) {
        // 无限制：最短位数上界 ≈ 尾数位长 × log10(2) + 裕量
        n_limit = bl / 3U + 64U;
    }
    if (n_limit < 1U) {
        n_limit = 1U;
    }

    // 往返验证上下文：按 src 自身精度（mant_bits = bl）最近偶解析候选串，
    // 解析结果必须与 src 完全相等。这正是头文件承诺的"按相同 ctx 往返"：
    // 正常值尾数恒为 bl 位（round_pack / from_f64 规范化输出），bl 即产生
    // src 的精度。不能用"高精度解析后精确相等"——极端指数值（如 1e100、
    // 5e-324）的精确十进制展开可达数百位，远超任何合理候选上限，而最短
    // 往返表示位数仅需 ≈ bl·log10(2) + 1（binary64 至多 17 位）。
    bigfloat_ctx_ty vctx;
    vctx.mant_bits = bl;
    vctx.exp_bits = BIGFLOAT_MAX_EXP_BITS;
    vctx.round = BIGFLOAT_ROUND_NEAREST_EVEN_E;

    // 输出缓冲（格式化串最大长度 ≈ 有效位数 + 指数部分）
    const size_t out_cap = n_limit + 256U;
    char *out_buf = (char *)nex_malloc(out_cap);
    if (out_buf == NULL) {
        return BIGFLOAT_ERR_OOM_E;
    }
    char *digits = (char *)nex_malloc(n_limit + 2U);
    if (digits == NULL) {
        free(out_buf);
        return BIGFLOAT_ERR_OOM_E;
    }

    bigfloat_err_ty result = BIGFLOAT_ERR_INVALID_E;
    bool hard_error = false;  // dec_round / format 硬错误（区别于"未找到往返"）
    size_t best_n = 1U;
    for (size_t n = 1U; n <= n_limit; n++) {
        int64_t de = 0;
        const bigfloat_err_ty derr = nex_bf_dec_round(src, n, digits,
                n_limit + 2U, NULL, &de);
        if (derr != BIGFLOAT_OK_E) {
            result = derr;
            hard_error = true;
            break;
        }
        size_t need = 0U;
        const bigfloat_err_ty ferr = format_digits(digits, n, de, out_buf,
                out_cap, &need);
        if (ferr != BIGFLOAT_OK_E) {
            result = ferr;
            hard_error = true;
            break;
        }
        // 负号：负值候选串须带 '-'（dec_round / format_digits 只处理幅值）
        if (src->flag == BIGFLOAT_NEG_E) {
            memmove(out_buf + 1, out_buf, strlen(out_buf) + 1U);
            out_buf[0] = '-';
        }
        // 往返验证：解析回（按 src 精度，最近偶）并精确比较
        bigfloat_ty parsed;
        bigfloat_init(&parsed);
        const bigfloat_err_ty perr = bigfloat_from_str(&parsed, out_buf,
                &vctx, NULL);
        const bool roundtrip = (perr == BIGFLOAT_OK_E)
                && (bigfloat_cmp(&parsed, src) == 0);
        bigfloat_free(&parsed);
        if (roundtrip) {
            result = BIGFLOAT_OK_E;
            best_n = n;
            break;
        }
        best_n = n;  // 未往返成功：暂存，max_digits 限制下退而求其次
    }

    // 输出（best_n 位候选；max_digits 低于最短位数时可能非最短）。
    // 无硬错误即输出：无限制时必为最短往返；受 max_digits 限制时
    // 退而求其次输出受限精度的最佳近似。
    if (!hard_error) {
        int64_t de = 0;
        const bigfloat_err_ty derr = nex_bf_dec_round(src, best_n, digits,
                n_limit + 2U, NULL, &de);
        if (derr != BIGFLOAT_OK_E) {
            result = derr;
        } else {
            size_t need = 0U;
            result = format_digits(digits, best_n, de, out_buf, out_cap,
                    &need);
            if (result == BIGFLOAT_OK_E) {
                if (src->flag == BIGFLOAT_NEG_E) {
                    memmove(out_buf + 1, out_buf, strlen(out_buf) + 1U);
                    out_buf[0] = '-';
                }
                const size_t need_total = strlen(out_buf) + 1U;
                if (needed != NULL) {
                    *needed = need_total;
                }
                if (buf == NULL) {
                    result = BIGFLOAT_OK_E;  // 仅查询
                } else if (buf_len < need_total) {
                    result = BIGFLOAT_ERR_OVERFLOW_E;
                } else {
                    memcpy(buf, out_buf, need_total);
                }
            }
        }
    }

    free(out_buf);
    free(digits);
    return result;
}
