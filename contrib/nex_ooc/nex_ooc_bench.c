/*
 * nex_ooc_bench：out-of-core 分块乘法基准（P1b-OOC M1/M2，issue #104）。
 *
 * 用法：
 *   nex_ooc_bench <digits> [block_bits] [--small <k>] [--scratch DIR] [--keep]
 *
 *   digits      操作数 a 的十进制位数（支持 1e9 / 1e10 写法）
 *   block_bits  块长 = 2^block_bits 肢；0 表示默认（2^23）
 *   --small k   令 b = k（单肢），并按 schoolbook 逐块校验结果（M1 验收）
 *   --scratch   临时文件目录
 *   --keep      保留临时文件
 *
 * 输出：块数、块对数、耗时、峰值 RSS、磁盘读/写量、结果肢数。
 * 操作数直接流式写盘，不在内存中持有完整操作数。
 */

#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#elif !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "nex_ooc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#endif

#define PATH_BUF 512U
#define GEN_CHUNK_LIMBS (1U << 20U)

static double now_sec(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static int have = 0;
    LARGE_INTEGER cnt;
    if (have == 0) {
        QueryPerformanceFrequency(&freq);
        have = 1;
    }
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (double)clock() / (double)CLOCKS_PER_SEC;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static size_t peak_rss_bytes(void)
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (size_t)pmc.PeakWorkingSetSize;
    }
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
        return (size_t)ru.ru_maxrss;
#else
        return (size_t)ru.ru_maxrss * 1024U;
#endif
    }
#endif
    return 0U;
}

static unsigned long current_pid(void)
{
#if defined(_WIN32)
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

static int ooc_seek(FILE *fp, uint64_t byte_off)
{
#if defined(_WIN32)
    return _fseeki64(fp, (__int64)byte_off, SEEK_SET);
#else
    return fseeko(fp, (off_t)byte_off, SEEK_SET);
#endif
}

static uint64_t g_rng = 0x13198A2E03707344ULL;

static uint32_t rnd32(void)
{
    g_rng ^= g_rng << 13U;
    g_rng ^= g_rng >> 7U;
    g_rng ^= g_rng << 17U;
    return (uint32_t)(g_rng >> 16);
}

static int host_is_little_endian(void)
{
    const uint16_t probe = 1U;
    return ((const unsigned char *)&probe)[0] == 1;
}

static void bswap32_inplace(uint32_t *buf, size_t limbs)
{
    size_t i;
    for (i = 0U; i < limbs; i++) {
        const uint32_t v = buf[i];
        buf[i] = (v >> 24) | ((v >> 8) & 0x0000FF00U)
                | ((v << 8) & 0x00FF0000U) | (v << 24);
    }
}

/* 生成 limbs 个随机肢并流式写盘（分块，不整块驻留） */
static int generate_file(const char *path, size_t limbs, int ones)
{
    FILE *fp = fopen(path, "wb");
    uint32_t *buf = NULL;
    size_t done = 0U;
    const int swap = !host_is_little_endian();
    if (fp == NULL) {
        return -1;
    }
    buf = (uint32_t *)malloc(GEN_CHUNK_LIMBS * sizeof(uint32_t));
    if (buf == NULL) {
        (void)fclose(fp);
        return -1;
    }
    while (done < limbs) {
        size_t n = limbs - done;
        size_t i;
        if (n > (size_t)GEN_CHUNK_LIMBS) {
            n = (size_t)GEN_CHUNK_LIMBS;
        }
        for (i = 0U; i < n; i++) {
            buf[i] = ones ? 0xFFFFFFFFU : rnd32();
        }
        if (swap) {
            bswap32_inplace(buf, n);
        }
        if (fwrite(buf, sizeof(uint32_t), n, fp) != n) {
            free(buf);
            buf = NULL;
            (void)fclose(fp);
            return -1;
        }
        done += n;
    }
    free(buf);
    buf = NULL;
    return (fclose(fp) == 0) ? 0 : -1;
}

/* 逐块校验 a·k：参考值为 schoolbook 单肢乘，内存 O(chunk) */
static int verify_small(const char *a_path, size_t a_limbs, uint32_t k,
        const char *res_path, uint64_t res_limbs)
{
    FILE *fa = fopen(a_path, "rb");
    FILE *fr = fopen(res_path, "rb");
    uint32_t *abuf = NULL;
    uint32_t *rbuf = NULL;
    size_t done = 0U;
    uint64_t carry = 0U;
    int bad = 0;
    const int swap = !host_is_little_endian();

    if ((fa == NULL) || (fr == NULL)) {
        if (fa != NULL) {
            (void)fclose(fa);
        }
        if (fr != NULL) {
            (void)fclose(fr);
        }
        return -1;
    }
    abuf = (uint32_t *)malloc(GEN_CHUNK_LIMBS * sizeof(uint32_t));
    rbuf = (uint32_t *)malloc(GEN_CHUNK_LIMBS * sizeof(uint32_t));
    if ((abuf == NULL) || (rbuf == NULL)) {
        free(rbuf);
        rbuf = NULL;
        free(abuf);
        abuf = NULL;
        (void)fclose(fr);
        (void)fclose(fa);
        return -1;
    }

    while (done < a_limbs) {
        size_t n = a_limbs - done;
        size_t i;
        if (n > (size_t)GEN_CHUNK_LIMBS) {
            n = (size_t)GEN_CHUNK_LIMBS;
        }
        if (fread(abuf, sizeof(uint32_t), n, fa) != n) {
            bad = 1;
            break;
        }
        if (swap) {
            bswap32_inplace(abuf, n);
        }
        for (i = 0U; i < n; i++) {
            const uint64_t t = (uint64_t)abuf[i] * (uint64_t)k + carry;
            abuf[i] = (uint32_t)t;
            carry = t >> 32;
        }
        if (ooc_seek(fr, (uint64_t)done * 4U) != 0) {
            bad = 1;
            break;
        }
        if (fread(rbuf, sizeof(uint32_t), n, fr) != n) {
            bad = 1;
            break;
        }
        if (swap) {
            bswap32_inplace(rbuf, n);
        }
        if (memcmp(abuf, rbuf, n * sizeof(uint32_t)) != 0) {
            for (i = 0U; i < n; i++) {
                if (abuf[i] != rbuf[i]) {
                    printf("  mismatch at limb %zu: got=%08x want=%08x\n",
                            done + i, (unsigned)rbuf[i], (unsigned)abuf[i]);
                    break;
                }
            }
            bad = 1;
            break;
        }
        done += n;
    }

    if ((bad == 0) && (carry != 0U) && (done == a_limbs)) {
        /* 末尾还有 1 肢进位 */
        uint32_t tail = 0U;
        if ((ooc_seek(fr, (uint64_t)done * 4U) != 0)
                || (fread(&tail, sizeof(uint32_t), 1U, fr) != 1U)) {
            bad = 1;
        } else {
            if (swap) {
                bswap32_inplace(&tail, 1U);
            }
            if ((uint64_t)tail != carry) {
                printf("  mismatch at final limb: got=%08x want=%08x\n",
                        (unsigned)tail, (unsigned)carry);
                bad = 1;
            }
        }
    }
    if ((bad == 0) && (res_limbs != (uint64_t)a_limbs
                + ((carry != 0U) ? 1U : 0U))) {
        printf("  limb count mismatch: got=%llu want=%zu\n",
                (unsigned long long)res_limbs,
                a_limbs + ((carry != 0U) ? 1U : 0U));
        bad = 1;
    }

    free(rbuf);
    rbuf = NULL;
    free(abuf);
    abuf = NULL;
    (void)fclose(fr);
    (void)fclose(fa);
    return bad;
}

/* 读入整个肢体文件到大整数（仅用于 --verify-direct 的中小规模校验） */
static int load_file_bin(const char *path, bigint_bin_ty *out)
{
    FILE *fp = fopen(path, "rb");
    uint64_t bytes = 0U;
    uint32_t *limbs = NULL;
    const int swap = !host_is_little_endian();
    if (fp == NULL) {
        return -1;
    }
#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        return -1;
    }
    {
        const __int64 pos = _ftelli64(fp);
        if (pos < 0) {
            (void)fclose(fp);
            return -1;
        }
        bytes = (uint64_t)pos;
    }
#else
    if (fseeko(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        return -1;
    }
    {
        const off_t pos = ftello(fp);
        if (pos < 0) {
            (void)fclose(fp);
            return -1;
        }
        bytes = (uint64_t)pos;
    }
#endif
    if (ooc_seek(fp, 0U) != 0) {
        (void)fclose(fp);
        return -1;
    }
    if (bytes == 0U) {
        (void)fclose(fp);
        return 0;
    }
    limbs = (uint32_t *)malloc((size_t)bytes);
    if (limbs == NULL) {
        (void)fclose(fp);
        return -1;
    }
    if (fread(limbs, 1U, (size_t)bytes, fp) != (size_t)bytes) {
        free(limbs);
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);
    if (swap) {
        bswap32_inplace(limbs, (size_t)(bytes / 4U));
    }
    bigint_bin_free(out);
    if (bigint_bin_init(out) != BIGINT_OK_E) {
        free(limbs);
        return -1;
    }
    out->limbs = limbs;
    out->len = (size_t)(bytes / 4U);
    out->cap = out->len;
    out->sign = BIGINT_SIGN_POS_E;
    while ((out->len > 0U) && (out->limbs[out->len - 1U] == 0U)) {
        out->len--;
    }
    return 0;
}

/* 与内存内 bigint_bin_mul（AUTO，大输入为 NTT）逐块比对 */
static int verify_direct(const char *a_path, const char *b_path,
        const char *res_path, uint64_t res_limbs)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
    FILE *fr = NULL;
    uint32_t *rbuf = NULL;
    uint64_t done = 0U;
    int bad = 0;
    const int swap = !host_is_little_endian();

    (void)bigint_bin_init(&a);
    (void)bigint_bin_init(&b);
    (void)bigint_bin_init(&c);
    if ((load_file_bin(a_path, &a) != 0) || (load_file_bin(b_path, &b) != 0)) {
        bad = -1;
        goto out;
    }
    if (bigint_bin_mul(&c, &a, &b) != BIGINT_OK_E) {
        bad = -1;
        goto out;
    }
    if ((uint64_t)c.len != res_limbs) {
        printf("  limb count mismatch: got=%llu want=%zu\n",
                (unsigned long long)res_limbs, c.len);
        bad = 1;
        goto out;
    }
    fr = fopen(res_path, "rb");
    rbuf = (uint32_t *)malloc(GEN_CHUNK_LIMBS * sizeof(uint32_t));
    if ((fr == NULL) || (rbuf == NULL)) {
        bad = -1;
        goto out;
    }
    while (done < res_limbs) {
        size_t n = (size_t)(res_limbs - done);
        if (n > (size_t)GEN_CHUNK_LIMBS) {
            n = (size_t)GEN_CHUNK_LIMBS;
        }
        if (fread(rbuf, sizeof(uint32_t), n, fr) != n) {
            bad = -1;
            break;
        }
        if (swap) {
            bswap32_inplace(rbuf, n);
        }
        if (memcmp(rbuf, c.limbs + done, n * sizeof(uint32_t)) != 0) {
            size_t i;
            for (i = 0U; i < n; i++) {
                if (rbuf[i] != c.limbs[done + i]) {
                    printf("  mismatch at limb %zu: got=%08x want=%08x\n",
                            (size_t)done + i, (unsigned)rbuf[i],
                            (unsigned)c.limbs[done + i]);
                    break;
                }
            }
            bad = 1;
            break;
        }
        done += n;
    }

out:
    free(rbuf);
    if (fr != NULL) {
        (void)fclose(fr);
    }
    bigint_bin_free(&c);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
    return bad;
}

int main(int argc, char **argv)
{
    double digits;
    size_t limbs;
    size_t block = 0U;
    uint32_t small = 0U;
    int use_small = 0;
    int use_direct = 0;
    int keep = 0;
    const char *scratch = NULL;
    char a_path[PATH_BUF];
    char b_path[PATH_BUF];
    char r_path[PATH_BUF];
    nex_ooc_opts_ty opts;
    nex_ooc_stats_ty stats;
    nex_ooc_err_ty err;
    double t0;
    double dt;
    size_t peak;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <digits> [block_bits] [--small k] "
                "[--verify-direct] [--scratch DIR] [--keep]\n", argv[0]);
        return 2;
    }
    digits = strtod(argv[1], NULL);
    if (!(digits > 0.0)) {
        fprintf(stderr, "invalid digits: %s\n", argv[1]);
        return 2;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--small") == 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "--small needs a value\n");
                return 2;
            }
            small = (uint32_t)strtoul(argv[++i], NULL, 10);
            use_small = 1;
        } else if (strcmp(argv[i], "--scratch") == 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "--scratch needs a directory\n");
                return 2;
            }
            scratch = argv[++i];
        } else if (strcmp(argv[i], "--keep") == 0) {
            keep = 1;
        } else if (strcmp(argv[i], "--verify-direct") == 0) {
            use_direct = 1;
        } else {
            const int bits = (int)strtol(argv[i], NULL, 10);
            if ((bits < 0) || (bits > 26)) {
                fprintf(stderr, "block_bits out of range: %s\n", argv[i]);
                return 2;
            }
            block = (bits == 0) ? 0U : ((size_t)1U << (unsigned)bits);
        }
    }

    limbs = (size_t)ceil(digits * log2(10.0) / 32.0);
    if (limbs == 0U) {
        limbs = 1U;
    }
    if (block == 0U) {
        block = (size_t)NEX_OOC_DEFAULT_BLOCK_LIMBS;
    }

    (void)snprintf(a_path, sizeof(a_path), "%s%snex_ooc_bench_a_%lu.bin",
            (scratch != NULL) ? scratch : "",
            ((scratch != NULL) && (scratch[0] != '\0')) ? "/" : "",
            current_pid());
    (void)snprintf(b_path, sizeof(b_path), "%s%snex_ooc_bench_b_%lu.bin",
            (scratch != NULL) ? scratch : "",
            ((scratch != NULL) && (scratch[0] != '\0')) ? "/" : "",
            current_pid());
    (void)snprintf(r_path, sizeof(r_path), "%s%snex_ooc_bench_r_%lu.bin",
            (scratch != NULL) ? scratch : "",
            ((scratch != NULL) && (scratch[0] != '\0')) ? "/" : "",
            current_pid());

    if (generate_file(a_path, limbs, 0) != 0) {
        fprintf(stderr, "failed to create %s\n", a_path);
        return 1;
    }
    if (use_small != 0) {
        if (generate_file(b_path, 1U, 0) != 0) {
            fprintf(stderr, "failed to create %s\n", b_path);
            return 1;
        }
        /* 把单肢乘数改成给定值 k（至少为 3，保证有进位） */
        {
            FILE *fp = fopen(b_path, "wb");
            uint32_t v = (small < 3U) ? 3U : small;
            if ((fp == NULL) || (fwrite(&v, sizeof(uint32_t), 1U, fp) != 1U)) {
                if (fp != NULL) {
                    (void)fclose(fp);
                }
                return 1;
            }
            (void)fclose(fp);
            small = v;
        }
    } else if (generate_file(b_path, limbs, 0) != 0) {
        fprintf(stderr, "failed to create %s\n", b_path);
        return 1;
    }

    (void)memset(&opts, 0, sizeof(opts));
    opts.block_limbs = block;
    opts.scratch_dir = scratch;
    opts.keep_temps = keep;
    (void)memset(&stats, 0, sizeof(stats));

    printf("digits≈%.6g  limbs=%zu (%.1f MB/operand)  block=%zu limbs "
            "(%.1f MB)  estimate peak %.1f MB\n",
            digits, limbs, (double)limbs * 4.0 / (1024.0 * 1024.0),
            block, (double)block * 4.0 / (1024.0 * 1024.0),
            (double)nex_ooc_estimate_peak_bytes(block) / (1024.0 * 1024.0));
    (void)fflush(stdout);

    t0 = now_sec();
    err = nex_ooc_mul_files(r_path, a_path, limbs, b_path,
            use_small ? 1U : limbs, &opts, &stats);
    dt = now_sec() - t0;
    peak = peak_rss_bytes();
    if (err != NEX_OOC_OK_E) {
        fprintf(stderr, "ooc mul failed: %s\n", nex_ooc_strerror(err));
        if (!keep) {
            (void)remove(r_path);
            (void)remove(b_path);
            (void)remove(a_path);
        }
        return 1;
    }

    printf("blocks a=%zu b=%zu  pairs=%zu  time=%.1f s (%.1f min)  "
            "peak RSS=%.1f MB\n",
            stats.blocks_a, stats.blocks_b, stats.block_pairs, dt, dt / 60.0,
            (double)peak / (1024.0 * 1024.0));
    printf("read=%.2f GB  written=%.2f GB  result=%llu limbs (%.2f GB)\n",
            (double)stats.bytes_read / 1e9,
            (double)stats.bytes_written / 1e9,
            (unsigned long long)stats.result_limbs,
            (double)stats.result_limbs * 4.0 / 1e9);

    if (use_small != 0) {
        const double tv0 = now_sec();
        const int bad = verify_small(a_path, limbs, small, r_path,
                stats.result_limbs);
        printf("verify a×%u (schoolbook, chunked): %s  %.1f s\n",
                (unsigned)small,
                (bad == 0) ? "bit-identical" : "MISMATCH", now_sec() - tv0);
        if (bad != 0) {
            if (!keep) {
                (void)remove(r_path);
                (void)remove(b_path);
                (void)remove(a_path);
            }
            return 1;
        }
    }

    if (use_direct != 0) {
        const double tv0 = now_sec();
        const int bad = verify_direct(a_path, b_path, r_path,
                stats.result_limbs);
        printf("verify a×b (in-memory AUTO/NTT): %s  %.1f s\n",
                (bad == 0) ? "bit-identical" : "MISMATCH", now_sec() - tv0);
        if (bad != 0) {
            if (!keep) {
                (void)remove(r_path);
                (void)remove(b_path);
                (void)remove(a_path);
            }
            return 1;
        }
    }

    if (!keep) {
        (void)remove(r_path);
        (void)remove(b_path);
        (void)remove(a_path);
    } else {
        printf("kept: %s %s %s\n", a_path, b_path, r_path);
    }
    return 0;
}
