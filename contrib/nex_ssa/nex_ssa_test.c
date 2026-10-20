#include "nex_ssa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state = 0x123456789abcdefULL;
static uint32_t rnd32(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

static void fill_random(bigint_bin_ty *v, size_t limbs)
{
    size_t i;
    bigint_bin_free(v);
    if (bigint_bin_init_cap(v, limbs) != BIGINT_OK_E) exit(2);
    for (i = 0; i < limbs; i++) v->limbs[i] = rnd32();
    v->len = limbs;
    v->sign = BIGINT_SIGN_POS_E;
    while (v->len > 0 && v->limbs[v->len - 1] == 0) v->len--;
    if (v->len == 0) {
        v->limbs[0] = 1;
        v->len = 1;
    }
}

static int check(size_t limbs)
{
    bigint_bin_ty a, b, got, want;
    int rc = 1;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&got);
    bigint_bin_init(&want);
    fill_random(&a, limbs);
    fill_random(&b, limbs);
    if (bigint_bin_mul(&want, &a, &b) != BIGINT_OK_E) goto out;
    if (nex_ssa_mul(&got, &a, &b) != BIGINT_OK_E) {
        fprintf(stderr, "ssa failed at %zu limbs\n", limbs);
        goto out;
    }
    if (bigint_bin_cmp(&got, &want) != 0) {
        fprintf(stderr, "mismatch at %zu limbs\n", limbs);
        goto out;
    }
    printf("ok %zu limbs (%zu bit)\n", limbs, limbs * 32u);
    rc = 0;
out:
    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&got);
    bigint_bin_free(&want);
    return rc;
}

int main(int argc, char **argv)
{
    static const size_t sizes[] = { 10, 100, 500, 2000, 10000, 50000, 100000 };
    size_t i;
    (void)argc;
    (void)argv;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if (check(sizes[i]) != 0) return 1;
    }
    printf("all ssa tests passed\n");
    return 0;
}
