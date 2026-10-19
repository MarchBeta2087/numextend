/*
 * nex_ooc.c：out-of-core 分块乘法的 M1 骨架（P1b-OOC，issue #104）。
 *
 * 数据布局：操作数与结果都是「纯肢体文件」——小端 32-bit 肢序列，无头。
 *
 * 算法（见 nex_ooc.h 的说明）：
 *   输出块 k 的累加器 acc 只保留 2L+2 肢：
 *     acc = Σ_{i+j=k} a_i·b_j + carry(来自块 k−1)
 *   低 L 肢立即写盘，高位（≤ L+2 肢）作为进位进入块 k+1。
 * 因此峰值内存 O(L)：acc(2L+2) + pending(L+2) + a 块(L) + b 块(L)
 * + 块积(2L)，共约 7L 肢，另加块内乘法的 NTT 工作区。
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "nex_ooc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define OOC_PATH_MAX 512U

/* ------------------------------------------------------------------ */
/* 平台小工具                                                          */
/* ------------------------------------------------------------------ */

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

static double now_sec(void)
{
#if defined(_WIN32)
    LARGE_INTEGER freq;
    LARGE_INTEGER cnt;
    QueryPerformanceFrequency(&freq);
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

static unsigned long current_pid(void)
{
#if defined(_WIN32)
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

/* 64 位定位（Windows 用 _fseeki64；POSIX 用 fseeko） */
static int ooc_seek(FILE *fp, uint64_t byte_off)
{
#if defined(_WIN32)
    return _fseeki64(fp, (__int64)byte_off, SEEK_SET);
#else
    return fseeko(fp, (off_t)byte_off, SEEK_SET);
#endif
}

static int ooc_file_size(FILE *fp, uint64_t *out)
{
#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    {
        const __int64 pos = _ftelli64(fp);
        if (pos < 0) {
            return -1;
        }
        *out = (uint64_t)pos;
    }
#else
    if (fseeko(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    {
        const off_t pos = ftello(fp);
        if (pos < 0) {
            return -1;
        }
        *out = (uint64_t)pos;
    }
#endif
    return 0;
}

static int ooc_truncate(FILE *fp, uint64_t bytes)
{
    if (fflush(fp) != 0) {
        return -1;
    }
#if defined(_WIN32)
    return _chsize_s(_fileno(fp), (__int64)bytes) == 0 ? 0 : -1;
#else
    return ftruncate(fileno(fp), (off_t)bytes) == 0 ? 0 : -1;
#endif
}

/* ------------------------------------------------------------------ */
/* 肢体文件 I/O                                                        */
/* ------------------------------------------------------------------ */

static int ooc_read_limbs(FILE *fp, uint64_t limb_off, size_t limbs,
        uint32_t *buf, uint64_t *io_bytes)
{
    if (limbs == 0U) {
        return 0;
    }
    if (ooc_seek(fp, limb_off * 4U) != 0) {
        return -1;
    }
    if (fread(buf, sizeof(uint32_t), limbs, fp) != limbs) {
        return -1;
    }
    if (io_bytes != NULL) {
        *io_bytes += (uint64_t)limbs * 4U;
    }
    if (!host_is_little_endian()) {
        bswap32_inplace(buf, limbs);
    }
    return 0;
}

static int ooc_write_limbs(FILE *fp, uint32_t *buf, size_t limbs,
        uint64_t *io_bytes)
{
    if (limbs == 0U) {
        return 0;
    }
    if (!host_is_little_endian()) {
        bswap32_inplace(buf, limbs);
    }
    if (fwrite(buf, sizeof(uint32_t), limbs, fp) != limbs) {
        if (!host_is_little_endian()) {
            bswap32_inplace(buf, limbs);
        }
        return -1;
    }
    if (!host_is_little_endian()) {
        bswap32_inplace(buf, limbs);
    }
    if (io_bytes != NULL) {
        *io_bytes += (uint64_t)limbs * 4U;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static size_t min_size(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

/* 去掉高位零肢后的长度（模块内不变量：乘法输入先裁剪） */
static size_t trim_len(const uint32_t *buf, size_t limbs)
{
    while (limbs > 0U) {
        if (buf[limbs - 1U] != 0U) {
            break;
        }
        limbs--;
    }
    return limbs;
}

static nex_ooc_err_ty map_bigint_err(bigint_err_ty err)
{
    switch (err) {
    case BIGINT_OK_E:
        return NEX_OOC_OK_E;
    case BIGINT_ERR_OOM_E:
        return NEX_OOC_ERR_OOM_E;
    case BIGINT_ERR_UNSUPPORTED_E:
        return NEX_OOC_ERR_UNSUPPORTED_E;
    default:
        return NEX_OOC_ERR_INVALID_E;
    }
}

/* acc(不交叠于 p) += p，进位链向右传播 */
static void acc_add(uint32_t *acc, size_t acc_len,
        const uint32_t *p, size_t p_len)
{
    size_t i = 0U;
    uint64_t carry = 0U;
    for (i = 0U; i < p_len; i++) {
        const uint64_t t = (uint64_t)acc[i] + (uint64_t)p[i] + carry;
        acc[i] = (uint32_t)t;
        carry = t >> 32;
    }
    while ((carry != 0U) && (i < acc_len)) {
        const uint64_t t = (uint64_t)acc[i] + carry;
        acc[i] = (uint32_t)t;
        carry = t >> 32;
        i++;
    }
}

/* 从尾部反查最后一个非零肢，得到精确结果肢数 */
static uint64_t trim_file_tail(FILE *fp, uint64_t limbs, uint64_t *io_bytes)
{
    enum { CHUNK_LIMBS = 8192 };
    uint32_t buf[CHUNK_LIMBS];
    while (limbs > 0U) {
        const uint64_t take = (limbs < (uint64_t)CHUNK_LIMBS)
                ? limbs : (uint64_t)CHUNK_LIMBS;
        const uint64_t off = limbs - take;
        size_t i;
        if (ooc_read_limbs(fp, off, (size_t)take, buf, io_bytes) != 0) {
            return limbs; /* 读失败：保持原长度，由调用方判定 */
        }
        i = (size_t)take;
        while (i > 0U) {
            if (buf[i - 1U] != 0U) {
                return off + (uint64_t)i;
            }
            i--;
        }
        limbs = off;
    }
    return 0U;
}

static nex_ooc_err_ty make_temp_path(char *buf, size_t cap,
        const char *dir, unsigned long tag)
{
    static unsigned long g_counter = 0U;
    const char *sep = ((dir != NULL) && (dir[0] != '\0')) ? "/" : "";
    g_counter++;
    if (snprintf(buf, cap, "%s%snex_ooc_%lu_%lu.tmp",
            (dir != NULL) ? dir : "", sep,
            current_pid(), tag * 1000UL + g_counter) < 0) {
        return NEX_OOC_ERR_INVALID_E;
    }
    return NEX_OOC_OK_E;
}

static nex_ooc_err_ty spill_bin_to_file(const char *path,
        const bigint_bin_ty *val, uint64_t *io_bytes)
{
    FILE *fp = fopen(path, "wb");
    nex_ooc_err_ty err = NEX_OOC_OK_E;
    if (fp == NULL) {
        return NEX_OOC_ERR_IO_E;
    }
    if (val->len > 0U) {
        uint32_t *copy = (uint32_t *)malloc(val->len * sizeof(uint32_t));
        if (copy == NULL) {
            (void)fclose(fp);
            return NEX_OOC_ERR_OOM_E;
        }
        memcpy(copy, val->limbs, val->len * sizeof(uint32_t));
        if (ooc_write_limbs(fp, copy, val->len, io_bytes) != 0) {
            err = NEX_OOC_ERR_IO_E;
        }
        free(copy);
        copy = NULL;
    }
    if ((fclose(fp) != 0) && (err == NEX_OOC_OK_E)) {
        err = NEX_OOC_ERR_IO_E;
    }
    return err;
}

static nex_ooc_err_ty load_bin_from_file(const char *path, bigint_bin_ty *out,
        uint64_t *io_bytes)
{
    FILE *fp = fopen(path, "rb");
    uint64_t bytes = 0U;
    nex_ooc_err_ty err = NEX_OOC_OK_E;
    if (fp == NULL) {
        return NEX_OOC_ERR_IO_E;
    }
    if (ooc_file_size(fp, &bytes) != 0) {
        (void)fclose(fp);
        return NEX_OOC_ERR_IO_E;
    }
    if ((bytes % 4U) != 0U) {
        (void)fclose(fp);
        return NEX_OOC_ERR_IO_E;
    }
    bigint_bin_free(out);
    if (bigint_bin_init(out) != BIGINT_OK_E) {
        (void)fclose(fp);
        return NEX_OOC_ERR_OOM_E;
    }
    if (bytes == 0U) {
        (void)fclose(fp);
        return NEX_OOC_OK_E;
    }
    if (bigint_bin_init_cap(out, (size_t)(bytes / 4U)) != BIGINT_OK_E) {
        (void)fclose(fp);
        return NEX_OOC_ERR_OOM_E;
    }
    if (ooc_read_limbs(fp, 0U, (size_t)(bytes / 4U), out->limbs,
            io_bytes) != 0) {
        err = NEX_OOC_ERR_IO_E;
    } else {
        out->len = (size_t)(bytes / 4U);
        out->sign = BIGINT_SIGN_POS_E;
    }
    (void)fclose(fp);
    return err;
}

/* ------------------------------------------------------------------ */
/* 主流程                                                              */
/* ------------------------------------------------------------------ */

nex_ooc_err_ty nex_ooc_mul_files(const char *dst_path,
        const char *a_path, size_t a_limbs,
        const char *b_path, size_t b_limbs,
        const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats)
{
    nex_ooc_stats_ty local;
    size_t block;
    size_t na;
    size_t nb;
    size_t acc_len;
    uint32_t *acc = NULL;
    uint32_t *pending = NULL;
    uint32_t *abuf = NULL;
    uint32_t *bbuf = NULL;
    bigint_bin_ty prod;
    bigint_bin_ty pa;
    bigint_bin_ty pb;
    FILE *fa = NULL;
    FILE *fb = NULL;
    FILE *fd = NULL;
    nex_ooc_err_ty err = NEX_OOC_OK_E;
    size_t pending_len = 0U;
    uint64_t written_limbs = 0U;
    uint64_t exact_limbs = 0U;
    size_t k;
    const double t_start = now_sec();

    (void)bigint_bin_init(&prod);
    (void)memset(&local, 0, sizeof(local));

    if ((dst_path == NULL) || (a_path == NULL) || (b_path == NULL)) {
        return NEX_OOC_ERR_INVALID_E;
    }
    block = ((opts != NULL) && (opts->block_limbs != 0U))
            ? opts->block_limbs : (size_t)NEX_OOC_DEFAULT_BLOCK_LIMBS;
    if (block > (size_t)NEX_OOC_MAX_BLOCK_LIMBS) {
        block = (size_t)NEX_OOC_MAX_BLOCK_LIMBS;
    }
    local.block_limbs = block;

    /* 任一操作数为零：结果为空文件 */
    if ((a_limbs == 0U) || (b_limbs == 0U)) {
        fd = fopen(dst_path, "wb+");
        if (fd == NULL) {
            return NEX_OOC_ERR_IO_E;
        }
        if (fclose(fd) != 0) {
            return NEX_OOC_ERR_IO_E;
        }
        local.seconds = now_sec() - t_start;
        if (stats != NULL) {
            *stats = local;
        }
        return NEX_OOC_OK_E;
    }

    na = (a_limbs + block - 1U) / block;
    nb = (b_limbs + block - 1U) / block;
    local.blocks_a = na;
    local.blocks_b = nb;

    if ((block > (SIZE_MAX / 64U)) || (na > (SIZE_MAX / 2U))
            || (nb > (SIZE_MAX / 2U))) {
        return NEX_OOC_ERR_UNSUPPORTED_E;
    }
    acc_len = 2U * block + 2U;
    acc = (uint32_t *)malloc(acc_len * sizeof(uint32_t));
    pending = (uint32_t *)malloc((block + 2U) * sizeof(uint32_t));
    abuf = (uint32_t *)malloc(block * sizeof(uint32_t));
    bbuf = (uint32_t *)malloc(block * sizeof(uint32_t));
    fa = fopen(a_path, "rb");
    fb = fopen(b_path, "rb");
    fd = fopen(dst_path, "wb+");
    if ((acc == NULL) || (pending == NULL) || (abuf == NULL)
            || (bbuf == NULL) || (fa == NULL) || (fb == NULL) || (fd == NULL)) {
        err = ((fa == NULL) || (fb == NULL) || (fd == NULL))
                ? NEX_OOC_ERR_IO_E : NEX_OOC_ERR_OOM_E;
        goto cleanup;
    }

    /* 块 k 的累加：Σ_{i+j=k} a_i·b_j + 上一块的进位 */
    for (k = 0U; (k + 1U) < (na + nb); k++) {
        const size_t i_lo = (k >= nb) ? (k - nb + 1U) : 0U;
        const size_t i_hi = (k < na) ? k : (na - 1U);
        size_t i;

        (void)memset(acc, 0, acc_len * sizeof(uint32_t));
        if (pending_len > 0U) {
            memcpy(acc, pending, pending_len * sizeof(uint32_t));
            pending_len = 0U;
        }

        for (i = i_lo; i <= i_hi; i++) {
            const size_t j = k - i;
            const size_t a_off = i * block;
            const size_t b_off = j * block;
            size_t alen = min_size(block, a_limbs - a_off);
            size_t blen = min_size(block, b_limbs - b_off);

            if (ooc_read_limbs(fa, a_off, alen, abuf,
                        &local.bytes_read) != 0) {
                err = NEX_OOC_ERR_IO_E;
                goto cleanup;
            }
            if (ooc_read_limbs(fb, b_off, blen, bbuf,
                        &local.bytes_read) != 0) {
                err = NEX_OOC_ERR_IO_E;
                goto cleanup;
            }
            alen = trim_len(abuf, alen);
            blen = trim_len(bbuf, blen);
            if ((alen == 0U) || (blen == 0U)) {
                local.block_pairs++;
                continue;
            }
            pa.sign = BIGINT_SIGN_POS_E;
            pa.limbs = abuf;
            pa.len = alen;
            pa.cap = block;
            pb.sign = BIGINT_SIGN_POS_E;
            pb.limbs = bbuf;
            pb.len = blen;
            pb.cap = block;
            err = map_bigint_err(bigint_bin_mul(&prod, &pa, &pb));
            if (err != NEX_OOC_OK_E) {
                goto cleanup;
            }
            if (prod.len > acc_len) {
                err = NEX_OOC_ERR_UNSUPPORTED_E;
                goto cleanup;
            }
            acc_add(acc, acc_len, prod.limbs, prod.len);
            local.block_pairs++;
        }

        if (ooc_write_limbs(fd, acc, block, &local.bytes_written) != 0) {
            err = NEX_OOC_ERR_IO_E;
            goto cleanup;
        }
        written_limbs += (uint64_t)block;
        pending_len = trim_len(acc + block, acc_len - block);
        if (pending_len > 0U) {
            memcpy(pending, acc + block, pending_len * sizeof(uint32_t));
        }
    }

    /* 末尾进位：可能超过一块（进位 ≤ 2^(32·(L+1))） */
    while (pending_len > 0U) {
        const size_t n = min_size(pending_len, block);
        if (ooc_write_limbs(fd, pending, n, &local.bytes_written) != 0) {
            err = NEX_OOC_ERR_IO_E;
            goto cleanup;
        }
        written_limbs += (uint64_t)n;
        if (pending_len <= block) {
            pending_len = 0U;
            break;
        }
        memmove(pending, pending + block,
                (pending_len - block) * sizeof(uint32_t));
        pending_len = trim_len(pending, pending_len - block);
    }

    /* 裁剪高位零肢：结果不留前导零（规范化） */
    exact_limbs = trim_file_tail(fd, written_limbs, &local.bytes_read);
    if (exact_limbs != written_limbs) {
        if (ooc_truncate(fd, exact_limbs * 4U) != 0) {
            err = NEX_OOC_ERR_IO_E;
            goto cleanup;
        }
    }
    local.result_limbs = exact_limbs;

cleanup:
    if (fd != NULL) {
        if (fclose(fd) != 0) {
            if (err == NEX_OOC_OK_E) {
                err = NEX_OOC_ERR_IO_E;
            }
        }
    }
    if (fa != NULL) {
        (void)fclose(fa);
    }
    if (fb != NULL) {
        (void)fclose(fb);
    }
    bigint_bin_free(&prod);
    free(bbuf);
    bbuf = NULL;
    free(abuf);
    abuf = NULL;
    free(pending);
    pending = NULL;
    free(acc);
    acc = NULL;
    local.seconds = now_sec() - t_start;
    if (stats != NULL) {
        *stats = local;
    }
    return err;
}

nex_ooc_err_ty nex_ooc_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs,
        const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats)
{
    char pa[OOC_PATH_MAX];
    char pb[OOC_PATH_MAX];
    char pc[OOC_PATH_MAX];
    const char *dir = (opts != NULL) ? opts->scratch_dir : NULL;
    nex_ooc_err_ty err;
    bigint_bin_ty out;
    nex_ooc_stats_ty local;
    int created = 0;
    const int keep = (opts != NULL) && (opts->keep_temps != 0);

    (void)bigint_bin_init(&out);
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return NEX_OOC_ERR_INVALID_E;
    }
    err = make_temp_path(pa, sizeof(pa), dir, 1U);
    if (err != NEX_OOC_OK_E) {
        return err;
    }
    err = make_temp_path(pb, sizeof(pb), dir, 2U);
    if (err != NEX_OOC_OK_E) {
        return err;
    }
    err = make_temp_path(pc, sizeof(pc), dir, 3U);
    if (err != NEX_OOC_OK_E) {
        return err;
    }

    err = spill_bin_to_file(pa, lhs, NULL);
    if (err == NEX_OOC_OK_E) {
        created = 1;
        err = spill_bin_to_file(pb, rhs, NULL);
    }
    if (err == NEX_OOC_OK_E) {
        err = nex_ooc_mul_files(pc, pa, lhs->len, pb, rhs->len, opts, &local);
    }
    if (err == NEX_OOC_OK_E) {
        err = load_bin_from_file(pc, &out, NULL);
    }
    if (err == NEX_OOC_OK_E) {
        if (out.len == 0U) {
            out.sign = BIGINT_SIGN_ZERO_E;
        } else {
            const bool neg = (lhs->sign == BIGINT_SIGN_NEG_E)
                    != (rhs->sign == BIGINT_SIGN_NEG_E);
            out.sign = neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
        }
        bigint_bin_move(dst, &out);
        if (stats != NULL) {
            *stats = local;
        }
    } else {
        bigint_bin_free(&out);
    }

    if (!keep) {
        (void)remove(pc);
        if (created != 0) {
            (void)remove(pb);
        }
        (void)remove(pa);
    }
    return err;
}

size_t nex_ooc_estimate_peak_bytes(size_t block_limbs)
{
    const size_t block = (block_limbs == 0U)
            ? (size_t)NEX_OOC_DEFAULT_BLOCK_LIMBS : block_limbs;
    /* 缓冲区：acc(2L+2) + pending(L+2) + a(L) + b(L) + 块积(2L) ≈ 7L 肢；
     * 块内乘法（AUTO → 多模数 CRT NTT）的工作区按 16L 肢保守估计。 */
    return (block * 23U + 4U) * sizeof(uint32_t);
}

const char *nex_ooc_strerror(nex_ooc_err_ty err)
{
    switch (err) {
    case NEX_OOC_OK_E:
        return "ok";
    case NEX_OOC_ERR_INVALID_E:
        return "invalid argument";
    case NEX_OOC_ERR_OOM_E:
        return "out of memory";
    case NEX_OOC_ERR_IO_E:
        return "i/o error";
    case NEX_OOC_ERR_UNSUPPORTED_E:
        return "unsupported size";
    default:
        return "unknown error";
    }
}
