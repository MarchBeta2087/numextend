# 4. 示例

本章给出各类型的短示例；完整演示见仓库 `demo/demo.c`。

## 4.1 大整数：阶乘与十进制 I/O

```c
bigint_bin_ty f, i;
bigint_bin_init(&f);
bigint_bin_init(&i);
bigint_bin_from_u64(&f, 1);
for (unsigned k = 1; k <= 100; k++) {
    bigint_bin_from_u64(&i, k);
    bigint_bin_mul(&f, &f, &i);      /* f *= i */
}
/* f = 100!，可经 bigint_bin_to_str 输出 */
```

## 4.2 带方法选择的乘法

```c
bigint_mul_method_ty m;
m.algo = BIGINT_MUL_TOOM_COOK_E;      /* 或 NTT / FFT / AUTO */
bigint_bin_mul_ex(&r, &a, &b, &m);
```

`AUTO`（默认）按规模在 schoolbook / Karatsuba / Toom-3 / NTT 之间分派。

## 4.3 精确有理数

```c
bigfrac_ty x, y, s;
bigfrac_init(&x); bigfrac_init(&y); bigfrac_init(&s);
bigfrac_from_str(&x, "1/2", NULL);
bigfrac_from_str(&y, "1/3", NULL);
bigfrac_add(&s, &x, &y, NULL);        /* s = 5/6（自动约分） */
```

## 4.4 可调精度浮点与正确舍入

```c
bigfloat_ctx_ty ctx;
bigfloat_ctx_make(&ctx, 256, 61, BIGFLOAT_ROUND_NEAREST_EVEN_E);
bigfloat_ty a, b, c;
bigfloat_init(&a); bigfloat_init(&b); bigfloat_init(&c);
bigfloat_from_str(&a, "1.5", &ctx, NULL);
bigfloat_from_str(&b, "3", &ctx, NULL);
bigfloat_div(&c, &a, &b, &ctx);       /* 0.5，正确舍入 */
bigfloat_sqrt(&c, &b, &ctx);          /* √3 */
```

精度（尾数位数）与舍入模式由 `ctx` 显式指定；`bigdecimal` 接口对称。

## 4.5 复数

```c
bigcomplex_float_ty z, w, p;
bigcomplex_float_init(&z);
bigcomplex_float_init(&w);
bigcomplex_float_init(&p);
bigcomplex_float_from_str(&z, "3+4i", &ctx, NULL);
bigcomplex_float_from_str(&w, "3-4i", &ctx, NULL);
bigcomplex_float_mul(&p, &z, &w, &ctx);   /* 25 + 0i */
```

## 4.6 超越函数

```c
#include "nex/transcend/nex_transcend.h"

/* 正确舍入到 ctx（球式严谨误差界 + Ziv） */
bigfloat_atan(&c, &a, &ctx);          /* atan */
bigfloat_log(&c, &a, &ctx);           /* ln（自然对数） */
bigfloat_sin(&c, &a, &ctx);           /* sin / cos / tan */
bigfloat_exp(&c, &a, &ctx);           /* exp */
bigfloat_asin(&c, &a, &ctx);          /* asin / acos（|a| ≤ 1） */

/* 复数辐角与复数超越函数 */
bigfloat_ty arg;
bigcomplex_float_arg(&arg, &z, &ctx);         /* arg(z) = atan2(im, re) */
bigcomplex_float_ty e;
bigcomplex_float_exp(&e, &z, &ctx);           /* exp(z) */
```

`bigdecimal` 与 `bigcomplex_decimal` 接口对称；三角/反三角支持 `|x| ≲ 2⁶³`。

## 4.7 跨类型转换

```c
#include "nex/convert/nex_convert.h"

/* 精确：bigint_bin → bigfrac */
nex_convert_bin_to_frac(&frac, &bi);
/* 有损（需显式目标精度 ctx）：bigfrac → bigdecimal */
nex_convert_frac_to_decimal(&dec, &frac, &dec_ctx);
```

完整转换矩阵见 API 参考的 `convert` 章节。

> 以上示例均为 C。C++ 中调用同一套 API（无需包装）见 §3.6。
