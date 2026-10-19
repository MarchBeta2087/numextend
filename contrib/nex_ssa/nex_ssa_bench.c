#include "nex_ssa.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

static size_t peak_rss_kb(void)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (size_t)(pmc.PeakWorkingSetSize / 1024u);
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) return (size_t)ru.ru_maxrss;
#endif
    return 0;
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint32_t rnd32(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

static double now_sec(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static int run(size_t limbs)
{
    bigint_bin_ty a, b, c;
    size_t i;
    double t0, t1;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&c);
    if (bigint_bin_init_cap(&a, limbs) != BIGINT_OK_E) return 1;
    if (bigint_bin_init_cap(&b, limbs) != BIGINT_OK_E) return 1;
    for (i = 0; i < limbs; i++) a.limbs[i] = rnd32();
    for (i = 0; i < limbs; i++) b.limbs[i] = rnd32();
    a.limbs[0] |= 1u; b.limbs[0] |= 1u;
    a.len = limbs; a.sign = BIGINT_SIGN_POS_E;
    b.len = limbs; b.sign = BIGINT_SIGN_POS_E;

    t0 = now_sec();
    if (nex_ssa_mul(&c, &a, &b) != BIGINT_OK_E) {
        fprintf(stderr, "ssa failed at %zu limbs\n", limbs);
        return 1;
    }
    t1 = now_sec();
    printf("%zu limbs (%zu bit): %.3f s, out %zu limbs, peak RSS %.1f MB\n",
            limbs, limbs * 32u, t1 - t0, c.len,
            (double)peak_rss_kb() / 1024.0);
    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
    return 0;
}

int main(int argc, char **argv)
{
    size_t limbs;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <limbs>\n", argv[0]);
        return 2;
    }
    limbs = (size_t)strtoull(argv[1], NULL, 10);
    return run(limbs);
}
