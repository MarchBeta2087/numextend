# 模块参考（自动生成）

> 由 `scripts/modules.py gen-docs` 生成（TODO §B5），数据源为
> `cmake/nex_modules.txt` 与公开头文件。请勿手改；
> 运行 `sh scripts/docs.sh` 重新生成。

## 1. 子集构建与依赖闭包

构建选项 `NEX_MODULES`（默认 `all`）接受模块名列表，CMake 按
**依赖闭包**自动补齐：只编译闭包内源文件、只安装闭包内公开头。
依赖允许成环（`bigint_bin ↔ bigint_conv`——bin 的十进制字符串
快速路径调用 conv）。查看某子集：
`python scripts/modules.py closure <mod...>`；某子集的保留/移除
清单：`python scripts/modules.py docs <mod...>`。

## 2. 模块与依赖

### `nex_alloc`

- 直接依赖：—
- 源文件：`nex/nex_alloc.c`
- 公开头：`nex/nex_alloc.h`
- 公开函数（2）：
  - `nex_malloc`
  - `nex_realloc`

### `fft`

- 直接依赖：—
- 源文件：`nex/fft/nex_fft.c`
- 公开头：`nex/fft/nex_fft.h`
- 公开函数（3）：
  - `fft_forward`
  - `fft_inverse`
  - `fft_pointwise_mul`

### `ntt`

- 直接依赖：—
- 源文件：`nex/ntt/nex_ntt.c`
- 公开头：`nex/ntt/nex_ntt.h`
- 公开函数（10）：
  - `ntt_crt_reconstruct_one`
  - `ntt_forward`
  - `ntt_inverse`
  - `ntt_mod_count`
  - `ntt_mod_get`
  - `ntt_mod_inv`
  - `ntt_mod_mul`
  - `ntt_mod_pow`
  - `ntt_mod_validate`
  - `ntt_pointwise_mul`

### `bigint_common`

- 直接依赖：—
- 源文件：（仅头）
- 公开头：`nex/bigint/nex_bigint_common.h`
- 公开函数（0）：

### `bigint_bin`

- 直接依赖：`bigint_common`, `bigint_conv`, `fft`, `ntt`, `nex_alloc`
- 源文件：`nex/bigint/bin/nex_bigint_bin.c`, `nex/bigint/bin/nex_bigint_bin_mul.c`, `nex/bigint/bin/nex_bigint_bin_div.c`, `nex/bigint/bin/nex_bigint_bin_gcd.c`
- 公开头：`nex/bigint/bin/nex_bigint_bin.h`
- 公开函数（36）：
  - `bigint_bin_abs`
  - `bigint_bin_add`
  - `bigint_bin_bit_and`
  - `bigint_bin_bit_len`
  - `bigint_bin_bit_or`
  - `bigint_bin_bit_set`
  - `bigint_bin_bit_test`
  - `bigint_bin_bit_xor`
  - `bigint_bin_cmp`
  - `bigint_bin_cmp_abs`
  - `bigint_bin_copy`
  - `bigint_bin_div_rem`
  - `bigint_bin_free`
  - `bigint_bin_from_i64`
  - `bigint_bin_from_str`
  - `bigint_bin_from_u64`
  - `bigint_bin_gcd`
  - `bigint_bin_init`
  - `bigint_bin_init_cap`
  - `bigint_bin_is_zero`
  - `bigint_bin_move`
  - `bigint_bin_mul`
  - `bigint_bin_mul_ex`
  - `bigint_bin_neg`
  - `bigint_bin_popcount`
  - `bigint_bin_pow`
  - `bigint_bin_pow_mod`
  - `bigint_bin_shl`
  - `bigint_bin_shr`
  - `bigint_bin_shrink`
  - `bigint_bin_sign`
  - `bigint_bin_sqrt`
  - `bigint_bin_sub`
  - `bigint_bin_to_i64`
  - `bigint_bin_to_str`
  - `bigint_bin_to_u64`

### `bigint_dec`

- 直接依赖：`bigint_common`, `nex_alloc`
- 源文件：`nex/bigint/dec/nex_bigint_dec.c`, `nex/bigint/dec/nex_bigint_dec_mul.c`, `nex/bigint/dec/nex_bigint_dec_div.c`
- 公开头：`nex/bigint/dec/nex_bigint_dec.h`
- 公开函数（28）：
  - `bigint_dec_abs`
  - `bigint_dec_add`
  - `bigint_dec_cmp`
  - `bigint_dec_cmp_abs`
  - `bigint_dec_copy`
  - `bigint_dec_digit_len`
  - `bigint_dec_div_pow10`
  - `bigint_dec_div_rem`
  - `bigint_dec_free`
  - `bigint_dec_from_i64`
  - `bigint_dec_from_str`
  - `bigint_dec_from_u64`
  - `bigint_dec_init`
  - `bigint_dec_init_cap`
  - `bigint_dec_is_zero`
  - `bigint_dec_move`
  - `bigint_dec_mul`
  - `bigint_dec_mul_ex`
  - `bigint_dec_mul_pow10`
  - `bigint_dec_neg`
  - `bigint_dec_pow`
  - `bigint_dec_pow_mod`
  - `bigint_dec_shrink`
  - `bigint_dec_sign`
  - `bigint_dec_sub`
  - `bigint_dec_to_i64`
  - `bigint_dec_to_str`
  - `bigint_dec_to_u64`

### `bigint_conv`

- 直接依赖：`bigint_common`, `bigint_bin`, `bigint_dec`, `nex_alloc`
- 源文件：`nex/bigint/nex_bigint_conv.c`
- 公开头：`nex/bigint/nex_bigint_conv.h`
- 公开函数（2）：
  - `bigint_conv_bin_to_dec`
  - `bigint_conv_dec_to_bin`

### `bigint_bin64`

- 直接依赖：`bigint_common`, `bigint_bin`, `nex_alloc`
- 源文件：`nex/bigint/bin64/nex_bigint_bin64.c`
- 公开头：`nex/bigint/bin64/nex_bigint_bin64.h`
- 公开函数（15）：
  - `bigint_bin64_add`
  - `bigint_bin64_cmp`
  - `bigint_bin64_copy`
  - `bigint_bin64_div_rem`
  - `bigint_bin64_free`
  - `bigint_bin64_from_u64`
  - `bigint_bin64_init`
  - `bigint_bin64_init_cap`
  - `bigint_bin64_is_zero`
  - `bigint_bin64_move`
  - `bigint_bin64_mul`
  - `bigint_bin64_shrink`
  - `bigint_bin64_sub`
  - `bigint_bin64_to_u64`
  - `nex_u128_mul`

### `bigfrac`

- 直接依赖：`bigint_common`, `bigint_bin`
- 源文件：`nex/bigfrac/nex_bigfrac.c`
- 公开头：`nex/bigfrac/nex_bigfrac.h`
- 公开函数（14）：
  - `bigfrac_add`
  - `bigfrac_cmp`
  - `bigfrac_copy`
  - `bigfrac_den`
  - `bigfrac_div`
  - `bigfrac_free`
  - `bigfrac_from_ints`
  - `bigfrac_from_str`
  - `bigfrac_init`
  - `bigfrac_inv`
  - `bigfrac_mul`
  - `bigfrac_neg`
  - `bigfrac_num`
  - `bigfrac_sub`

### `bigfloat`

- 直接依赖：`bigint_common`, `bigint_bin`, `nex_alloc`
- 源文件：`nex/bigfloat/nex_bigfloat.c`, `nex/bigfloat/nex_bigfloat_str.c`
- 公开头：`nex/bigfloat/nex_bigfloat.h`
- 公开函数（26）：
  - `bigfloat_add`
  - `bigfloat_cmp`
  - `bigfloat_compose`
  - `bigfloat_copy`
  - `bigfloat_ctx_binary128`
  - `bigfloat_ctx_binary32`
  - `bigfloat_ctx_binary64`
  - `bigfloat_ctx_make`
  - `bigfloat_decompose`
  - `bigfloat_div`
  - `bigfloat_eq`
  - `bigfloat_free`
  - `bigfloat_from_bigint`
  - `bigfloat_from_f64`
  - `bigfloat_from_str`
  - `bigfloat_init`
  - `bigfloat_is_inf`
  - `bigfloat_is_nan`
  - `bigfloat_is_normal`
  - `bigfloat_is_zero`
  - `bigfloat_mul`
  - `bigfloat_neg`
  - `bigfloat_sqrt`
  - `bigfloat_sub`
  - `bigfloat_to_f64`
  - `bigfloat_to_str`

### `bigdecimal`

- 直接依赖：`bigint_common`, `bigint_bin`, `bigint_dec`, `bigint_conv`, `nex_alloc`
- 源文件：`nex/bigdecimal/nex_bigdecimal.c`, `nex/bigdecimal/nex_bigdecimal_str.c`
- 公开头：`nex/bigdecimal/nex_bigdecimal.h`
- 公开函数（24）：
  - `bigdecimal_add`
  - `bigdecimal_cmp`
  - `bigdecimal_compose`
  - `bigdecimal_copy`
  - `bigdecimal_ctx_decimal128`
  - `bigdecimal_ctx_decimal32`
  - `bigdecimal_ctx_decimal64`
  - `bigdecimal_ctx_make`
  - `bigdecimal_decompose`
  - `bigdecimal_div`
  - `bigdecimal_eq`
  - `bigdecimal_free`
  - `bigdecimal_from_bigint`
  - `bigdecimal_from_str`
  - `bigdecimal_init`
  - `bigdecimal_is_inf`
  - `bigdecimal_is_nan`
  - `bigdecimal_is_normal`
  - `bigdecimal_is_zero`
  - `bigdecimal_mul`
  - `bigdecimal_neg`
  - `bigdecimal_sqrt`
  - `bigdecimal_sub`
  - `bigdecimal_to_str`

### `bigcomplex_float`

- 直接依赖：`bigint_common`, `bigint_bin`, `bigfloat`, `nex_alloc`
- 源文件：`nex/bigcomplex/float/nex_bigcomplex_float.c`
- 公开头：`nex/bigcomplex/float/nex_bigcomplex_float.h`
- 公开函数（13）：
  - `bigcomplex_float_abs`
  - `bigcomplex_float_add`
  - `bigcomplex_float_conj`
  - `bigcomplex_float_copy`
  - `bigcomplex_float_div`
  - `bigcomplex_float_eq`
  - `bigcomplex_float_free`
  - `bigcomplex_float_from_parts`
  - `bigcomplex_float_from_str`
  - `bigcomplex_float_init`
  - `bigcomplex_float_mul`
  - `bigcomplex_float_sub`
  - `bigcomplex_float_to_str`

### `bigcomplex_decimal`

- 直接依赖：`bigdecimal`, `nex_alloc`
- 源文件：`nex/bigcomplex/decimal/nex_bigcomplex_decimal.c`
- 公开头：`nex/bigcomplex/decimal/nex_bigcomplex_decimal.h`
- 公开函数（13）：
  - `bigcomplex_decimal_abs`
  - `bigcomplex_decimal_add`
  - `bigcomplex_decimal_conj`
  - `bigcomplex_decimal_copy`
  - `bigcomplex_decimal_div`
  - `bigcomplex_decimal_eq`
  - `bigcomplex_decimal_free`
  - `bigcomplex_decimal_from_parts`
  - `bigcomplex_decimal_from_str`
  - `bigcomplex_decimal_init`
  - `bigcomplex_decimal_mul`
  - `bigcomplex_decimal_sub`
  - `bigcomplex_decimal_to_str`

### `convert`

- 直接依赖：`nex_alloc`, `bigint_bin`, `bigint_dec`, `bigint_conv`, `bigfrac`, `bigfloat`, `bigdecimal`, `bigcomplex_float`, `bigcomplex_decimal`
- 源文件：`nex/convert/nex_convert.c`
- 公开头：`nex/convert/nex_convert.h`
- 公开函数（17）：
  - `nex_convert_bin_to_frac`
  - `nex_convert_cpx_decimal_to_decimal`
  - `nex_convert_cpx_float_to_float`
  - `nex_convert_dec_to_float`
  - `nex_convert_dec_to_frac`
  - `nex_convert_decimal_to_bin`
  - `nex_convert_decimal_to_dec`
  - `nex_convert_decimal_to_float`
  - `nex_convert_decimal_to_frac`
  - `nex_convert_float_to_bin`
  - `nex_convert_float_to_dec`
  - `nex_convert_float_to_decimal`
  - `nex_convert_float_to_frac`
  - `nex_convert_frac_to_bin`
  - `nex_convert_frac_to_dec`
  - `nex_convert_frac_to_decimal`
  - `nex_convert_frac_to_float`
