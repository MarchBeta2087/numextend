/*
 * nex_opencl.c：contrib OpenCL 批量大整数（issue #98 P3，M1）。
 *
 * 设计要点：
 *   - 动态加载 OpenCL（Windows OpenCL.dll / Linux libOpenCL.so.1），
 *     不要求 SDK、不链接 import lib；
 *   - 数据用定长 32-bit limb 数组；kernel 内拆成 16-bit digit 做乘加，
 *     避免 32-bit 列和溢出，同时保持传输量最小（104 limb = 416 B/数）；
 *   - 两阶段：colsum 求无进位列和，carry 逐元素串行进位；
 *   - dot 在 colsum 后做一次设备端归约，再一次进位。
 *
 * 线程安全：上下文不共享；每个 ctx 自带 queue/buffer，库内无全局状态。
 */

#include "nex_opencl.h"
#include "nex_opencl_abi.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE nex_cl_lib_ty;
#define NEX_CL_CALL __stdcall
static nex_cl_lib_ty nex_cl_open(void)
{
    return LoadLibraryA("OpenCL.dll");
}
static void nex_cl_close(nex_cl_lib_ty lib)
{
    FreeLibrary(lib);
}
static void *nex_cl_sym(nex_cl_lib_ty lib, const char *name)
{
    return (void *)(uintptr_t)GetProcAddress(lib, name);
}
#else
#include <dlfcn.h>
typedef void *nex_cl_lib_ty;
#define NEX_CL_CALL
static nex_cl_lib_ty nex_cl_open(void)
{
    void *lib = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        lib = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
    }
    return lib;
}
static void nex_cl_close(nex_cl_lib_ty lib)
{
    dlclose(lib);
}
static void *nex_cl_sym(nex_cl_lib_ty lib, const char *name)
{
    return dlsym(lib, name);
}
#endif

/* ------------------------------------------------------------------ */
/* OpenCL 入口                                                         */
/* ------------------------------------------------------------------ */

typedef cl_int (NEX_CL_CALL *fn_get_platform_ids)(cl_uint, cl_platform_id *, cl_uint *);
typedef cl_int (NEX_CL_CALL *fn_get_platform_info)(cl_platform_id, cl_platform_info,
        size_t, void *, size_t *);
typedef cl_int (NEX_CL_CALL *fn_get_device_ids)(cl_platform_id, cl_device_type,
        cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (NEX_CL_CALL *fn_get_device_info)(cl_device_id, cl_device_info,
        size_t, void *, size_t *);
typedef cl_context (NEX_CL_CALL *fn_create_context)(const cl_context_properties *,
        cl_uint, const cl_device_id *, void *, void *, cl_int *);
typedef cl_int (NEX_CL_CALL *fn_release_context)(cl_context);
typedef cl_command_queue (NEX_CL_CALL *fn_create_queue)(cl_context, cl_device_id,
        cl_command_queue_properties, cl_int *);
typedef cl_int (NEX_CL_CALL *fn_release_queue)(cl_command_queue);
typedef cl_mem (NEX_CL_CALL *fn_create_buffer)(cl_context, cl_mem_flags,
        size_t, void *, cl_int *);
typedef cl_int (NEX_CL_CALL *fn_release_mem)(cl_mem);
typedef cl_program (NEX_CL_CALL *fn_create_program)(cl_context, cl_uint,
        const char **, const size_t *, cl_int *);
typedef cl_int (NEX_CL_CALL *fn_build_program)(cl_program, cl_uint,
        const cl_device_id *, const char *, void *, void *);
typedef cl_int (NEX_CL_CALL *fn_get_build_info)(cl_program, cl_device_id,
        cl_program_build_info, size_t, void *, size_t *);
typedef cl_int (NEX_CL_CALL *fn_release_program)(cl_program);
typedef cl_kernel (NEX_CL_CALL *fn_create_kernel)(cl_program, const char *, cl_int *);
typedef cl_int (NEX_CL_CALL *fn_set_kernel_arg)(cl_kernel, cl_uint, size_t, const void *);
typedef cl_int (NEX_CL_CALL *fn_release_kernel)(cl_kernel);
typedef cl_int (NEX_CL_CALL *fn_write_buffer)(cl_command_queue, cl_mem, cl_uint,
        size_t, size_t, const void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (NEX_CL_CALL *fn_read_buffer)(cl_command_queue, cl_mem, cl_uint,
        size_t, size_t, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (NEX_CL_CALL *fn_ndrange)(cl_command_queue, cl_kernel, cl_uint,
        const size_t *, const size_t *, const size_t *, cl_uint,
        const cl_event *, cl_event *);
typedef cl_int (NEX_CL_CALL *fn_finish)(cl_command_queue);

typedef struct {
    nex_cl_lib_ty lib;
    fn_get_platform_ids get_platform_ids;
    fn_get_platform_info get_platform_info;
    fn_get_device_ids get_device_ids;
    fn_get_device_info get_device_info;
    fn_create_context create_context;
    fn_release_context release_context;
    fn_create_queue create_queue;
    fn_release_queue release_queue;
    fn_create_buffer create_buffer;
    fn_release_mem release_mem;
    fn_create_program create_program;
    fn_build_program build_program;
    fn_get_build_info get_build_info;
    fn_release_program release_program;
    fn_create_kernel create_kernel;
    fn_set_kernel_arg set_kernel_arg;
    fn_release_kernel release_kernel;
    fn_write_buffer write_buffer;
    fn_read_buffer read_buffer;
    fn_ndrange ndrange;
    fn_finish finish;
} nex_cl_api_ty;

struct nex_opencl_ctx {
    nex_cl_api_ty api;
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel k_colsum;
    cl_kernel k_carry;
    cl_kernel k_dotreduce;
    cl_kernel k_carry1;
    cl_kernel k_matmul;
    size_t limbs;
    size_t digits;      /* 2*limbs（16-bit digit 数） */
    size_t out_digits;  /* 4*limbs+1 */
    size_t out_limbs;   /* 2*limbs+1 */
    size_t max_wg;
    size_t max_item[3];
    cl_mem b_a;
    cl_mem b_b;
    cl_mem b_c;
    cl_mem b_zero;
    cl_mem b_sums;
    cl_mem b_out;
    cl_mem b_dot_sums;
    cl_mem b_dot_out;
    cl_mem b_mat_a;
    cl_mem b_mat_b;
    cl_mem b_mat_c;
    size_t cap_a;
    size_t cap_b;
    size_t cap_c;
    size_t cap_zero;
    size_t cap_sums;
    size_t cap_out;
    size_t cap_dot_sums;
    size_t cap_dot_out;
    size_t cap_mat_a;
    size_t cap_mat_b;
    size_t cap_mat_c;
    char device_name[256];
    char err[512];
};

static int failf(nex_opencl_ctx_ty *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(ctx->err, sizeof(ctx->err), fmt, ap);
    va_end(ap);
    return -1;
}

#define LOAD_SYM(field, sym) do { \
    void *sym_ptr_ = nex_cl_sym(ctx->api.lib, #sym); \
    if (sym_ptr_ == NULL || sizeof(sym_ptr_) != sizeof(ctx->api.field)) { \
        return failf(ctx, "缺少 OpenCL 符号 %s", #sym); \
    } \
    memcpy(&ctx->api.field, &sym_ptr_, sizeof(sym_ptr_)); \
} while (0)

static int load_api(nex_opencl_ctx_ty *ctx)
{
    ctx->api.lib = nex_cl_open();
    if (ctx->api.lib == NULL) {
        return failf(ctx, "找不到 OpenCL 运行库（OpenCL.dll / libOpenCL.so.1）");
    }
    LOAD_SYM(get_platform_ids, clGetPlatformIDs);
    LOAD_SYM(get_platform_info, clGetPlatformInfo);
    LOAD_SYM(get_device_ids, clGetDeviceIDs);
    LOAD_SYM(get_device_info, clGetDeviceInfo);
    LOAD_SYM(create_context, clCreateContext);
    LOAD_SYM(release_context, clReleaseContext);
    LOAD_SYM(create_queue, clCreateCommandQueue);
    LOAD_SYM(release_queue, clReleaseCommandQueue);
    LOAD_SYM(create_buffer, clCreateBuffer);
    LOAD_SYM(release_mem, clReleaseMemObject);
    LOAD_SYM(create_program, clCreateProgramWithSource);
    LOAD_SYM(build_program, clBuildProgram);
    LOAD_SYM(get_build_info, clGetProgramBuildInfo);
    LOAD_SYM(release_program, clReleaseProgram);
    LOAD_SYM(create_kernel, clCreateKernel);
    LOAD_SYM(set_kernel_arg, clSetKernelArg);
    LOAD_SYM(release_kernel, clReleaseKernel);
    LOAD_SYM(write_buffer, clEnqueueWriteBuffer);
    LOAD_SYM(read_buffer, clEnqueueReadBuffer);
    LOAD_SYM(ndrange, clEnqueueNDRangeKernel);
    LOAD_SYM(finish, clFinish);
    return 0;
}

#undef LOAD_SYM

static int device_name_of(nex_opencl_ctx_ty *ctx, cl_device_id dev,
        char *buf, size_t cap)
{
    if (ctx->api.get_device_info(dev, CL_DEVICE_NAME, cap, buf, NULL) != CL_SUCCESS) {
        return -1;
    }
    buf[cap - 1u] = '\0';
    return 0;
}

static int try_devices(nex_opencl_ctx_ty *ctx, cl_platform_id plat,
        cl_device_type type, const char *want, cl_device_id *found)
{
    cl_device_id devs[8];
    cl_uint nd = 0;
    cl_int rc = ctx->api.get_device_ids(plat, type, 8, devs, &nd);
    if (rc != CL_SUCCESS) {
        return 0;
    }
    for (cl_uint i = 0; i < nd; i++) {
        char name[256] = {0};
        if (device_name_of(ctx, devs[i], name, sizeof(name)) != 0) {
            continue;
        }
        if ((want != NULL) && (strstr(name, want) == NULL)) {
            continue;
        }
        *found = devs[i];
        return 1;
    }
    return 0;
}

static int choose_device(nex_opencl_ctx_ty *ctx)
{
    cl_platform_id plats[16];
    cl_uint np = 0;
    if (ctx->api.get_platform_ids(16, plats, &np) != CL_SUCCESS || np == 0u) {
        return failf(ctx, "没有 OpenCL 平台");
    }
    const char *want = getenv("NEX_OPENCL_DEVICE");
    cl_device_id gpu = NULL;
    cl_device_id cpu = NULL;
    cl_platform_id gpu_plat = NULL;
    cl_platform_id cpu_plat = NULL;

    for (cl_uint i = 0; i < np; i++) {
        cl_device_id d = NULL;
        if (want != NULL) {
            if (try_devices(ctx, plats[i], CL_DEVICE_TYPE_ALL, want, &d)) {
                ctx->platform = plats[i];
                ctx->device = d;
                return 0;
            }
        } else {
            if (gpu == NULL && try_devices(ctx, plats[i], CL_DEVICE_TYPE_GPU, NULL, &d)) {
                gpu = d;
                gpu_plat = plats[i];
            }
            d = NULL;
            if (cpu == NULL && try_devices(ctx, plats[i], CL_DEVICE_TYPE_CPU, NULL, &d)) {
                cpu = d;
                cpu_plat = plats[i];
            }
        }
    }
    if (want != NULL) {
        return failf(ctx, "没有匹配 NEX_OPENCL_DEVICE=%s 的设备", want);
    }
    if (gpu != NULL) {
        ctx->platform = gpu_plat;
        ctx->device = gpu;
        return 0;
    }
    if (cpu != NULL) {
        ctx->platform = cpu_plat;
        ctx->device = cpu;
        return 0;
    }
    return failf(ctx, "没有可用的 OpenCL 设备（GPU/CPU）");
}

/* ------------------------------------------------------------------ */
/* kernel 源码（按 limbs 特化）                                         */
/* ------------------------------------------------------------------ */

static const char *const NEX_CL_KERNEL_BODY =
"__kernel void colsum(__global const uint *a, __global const uint *b,\n"
"                     __global const uint *c, __global ulong *sums,\n"
"                     uint n, int use_c)\n"
"{\n"
"    uint e = get_global_id(0);\n"
"    uint k = get_global_id(1);\n"
"    if (e >= n || k >= OUTDIGITS) return;\n"
"    ulong acc = 0;\n"
"    uint i0 = (k >= DIGITS) ? (k - DIGITS + 1u) : 0u;\n"
"    uint i1 = (k < DIGITS) ? k : (DIGITS - 1u);\n"
"    __global const uint *ae = a + (size_t)e * LIMBS;\n"
"    __global const uint *be = b + (size_t)e * LIMBS;\n"
"    for (uint i = i0; i <= i1; ++i) {\n"
"        uint j = k - i;\n"
"        uint ad = (ae[i >> 1] >> ((i & 1u) * 16u)) & 0xFFFFu;\n"
"        uint bd = (be[j >> 1] >> ((j & 1u) * 16u)) & 0xFFFFu;\n"
"        acc += (ulong)ad * (ulong)bd;\n"
"    }\n"
"    if (use_c && k < DIGITS) {\n"
"        uint cd = (c[(size_t)e * LIMBS + (k >> 1)] >> ((k & 1u) * 16u)) & 0xFFFFu;\n"
"        acc += (ulong)cd;\n"
"    }\n"
"    sums[(size_t)e * OUTDIGITS + k] = acc;\n"
"}\n"
"__kernel void carry(__global const ulong *sums, __global uint *out, uint n)\n"
"{\n"
"    uint e = get_global_id(0);\n"
"    if (e >= n) return;\n"
"    ulong carry = 0;\n"
"    for (uint k = 0; k < OUTDIGITS; ++k) {\n"
"        ulong t = sums[(size_t)e * OUTDIGITS + k] + carry;\n"
"        uint d = (uint)(t & 0xFFFFu);\n"
"        carry = t >> 16;\n"
"        uint p = k >> 1;\n"
"        if ((k & 1u) == 0u) out[(size_t)e * OUTLIMBS + p] = d;\n"
"        else out[(size_t)e * OUTLIMBS + p] |= (d << 16);\n"
"    }\n"
"}\n"
"__kernel void dotreduce(__global const ulong *sums, __global ulong *acc, uint n)\n"
"{\n"
"    uint k = get_global_id(0);\n"
"    if (k >= OUTDIGITS) return;\n"
"    ulong s = 0;\n"
"    for (uint e = 0; e < n; ++e) s += sums[(size_t)e * OUTDIGITS + k];\n"
"    acc[k] = s;\n"
"}\n"
"__kernel void carry1(__global const ulong *acc, __global uint *out)\n"
"{\n"
"    ulong carry = 0;\n"
"    for (uint k = 0; k < OUTDIGITS; ++k) {\n"
"        ulong t = acc[k] + carry;\n"
"        uint d = (uint)(t & 0xFFFFu);\n"
"        carry = t >> 16;\n"
"        uint p = k >> 1;\n"
"        if ((k & 1u) == 0u) out[p] = d;\n"
"        else out[p] |= (d << 16);\n"
"    }\n"
"}\n"
"__kernel __attribute__((reqd_work_group_size(64,1,1)))\n"
"void matmul_sub(__global const uint *A, __global const uint *B,\n"
"                __global uint *C, uint nout, uint k, uint ncols)\n"
"{\n"
"    uint idx = get_global_id(0) / 64u;\n"
"    if (idx >= nout) return;\n"
"    uint row = idx / ncols;\n"
"    uint col = idx - row * ncols;\n"
"    uint tid = get_local_id(0);\n"
"    __local ulong acc[OUTDIGITS];\n"
"    __local uint la[LIMBS];\n"
"    __local uint lb[LIMBS];\n"
"    for (uint q = tid; q < OUTDIGITS; q += 64u) acc[q] = 0;\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    for (uint p = 0; p < k; ++p) {\n"
"        __global const uint *ap = A + ((size_t)row * k + p) * LIMBS;\n"
"        __global const uint *bp = B + ((size_t)p * ncols + col) * LIMBS;\n"
"        for (uint i = tid; i < LIMBS; i += 64u) la[i] = ap[i];\n"
"        for (uint i = tid; i < LIMBS; i += 64u) lb[i] = bp[i];\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"        for (uint q = tid; q < OUTDIGITS; q += 64u) {\n"
"            ulong s = 0;\n"
"            uint i0 = (q >= DIGITS) ? (q - DIGITS + 1u) : 0u;\n"
"            uint i1 = (q < DIGITS) ? q : (DIGITS - 1u);\n"
"            for (uint i = i0; i <= i1; ++i) {\n"
"                uint j = q - i;\n"
"                uint ad = (la[i >> 1] >> ((i & 1u) * 16u)) & 0xFFFFu;\n"
"                uint bd = (lb[j >> 1] >> ((j & 1u) * 16u)) & 0xFFFFu;\n"
"                s += (ulong)ad * (ulong)bd;\n"
"            }\n"
"            acc[q] += s;\n"
"        }\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"    if (tid == 0u) {\n"
"        ulong carry = 0;\n"
"        for (uint q = 0; q < OUTDIGITS; ++q) {\n"
"            ulong t = acc[q] + carry;\n"
"            uint d = (uint)(t & 0xFFFFu);\n"
"            carry = t >> 16;\n"
"            uint limb = q >> 1;\n"
"            if ((q & 1u) == 0u) C[(size_t)idx * OUTLIMBS + limb] = d;\n"
"            else C[(size_t)idx * OUTLIMBS + limb] |= (d << 16);\n"
"        }\n"
"    }\n"
"}\n";

static int build_kernels(nex_opencl_ctx_ty *ctx)
{
    char defs[256];
    int dn = snprintf(defs, sizeof(defs),
            "#define LIMBS %zu\n"
            "#define DIGITS %zu\n"
            "#define OUTDIGITS %zu\n"
            "#define OUTLIMBS %zu\n",
            ctx->limbs, ctx->digits, ctx->out_digits, ctx->out_limbs);
    if (dn <= 0 || (size_t)dn >= sizeof(defs)) {
        return failf(ctx, "kernel 宏生成失败");
    }
    size_t src_size = (size_t)dn + strlen(NEX_CL_KERNEL_BODY) + 1u;
    char *src = (char *)malloc(src_size);
    if (src == NULL) {
        return failf(ctx, "OOM（kernel 源码）");
    }
    memcpy(src, defs, (size_t)dn);
    memcpy(src + dn, NEX_CL_KERNEL_BODY, strlen(NEX_CL_KERNEL_BODY) + 1u);

    cl_int rc = CL_SUCCESS;
    const char *src_ptr = src;
    ctx->program = ctx->api.create_program(ctx->context, 1, &src_ptr, NULL, &rc);
    free(src);
    if (ctx->program == NULL) {
        return failf(ctx, "clCreateProgramWithSource 失败 (%d)", (int)rc);
    }
    rc = ctx->api.build_program(ctx->program, 1, &ctx->device, "", NULL, NULL);
    if (rc != CL_SUCCESS) {
        size_t log_size = 0;
        (void)ctx->api.get_build_info(ctx->program, ctx->device,
                CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char *log = (char *)malloc(log_size + 1u);
        if (log != NULL) {
            (void)ctx->api.get_build_info(ctx->program, ctx->device,
                    CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
            log[log_size] = '\0';
            (void)failf(ctx, "kernel 构建失败 (%d):\n%s", (int)rc, log);
            free(log);
        } else {
            (void)failf(ctx, "kernel 构建失败 (%d)", (int)rc);
        }
        return -1;
    }
    ctx->k_colsum = ctx->api.create_kernel(ctx->program, "colsum", &rc);
    ctx->k_carry = ctx->api.create_kernel(ctx->program, "carry", &rc);
    ctx->k_dotreduce = ctx->api.create_kernel(ctx->program, "dotreduce", &rc);
    ctx->k_carry1 = ctx->api.create_kernel(ctx->program, "carry1", &rc);
    ctx->k_matmul = ctx->api.create_kernel(ctx->program, "matmul_sub", &rc);
    if (ctx->k_colsum == NULL || ctx->k_carry == NULL ||
            ctx->k_dotreduce == NULL || ctx->k_carry1 == NULL ||
            ctx->k_matmul == NULL) {
        return failf(ctx, "clCreateKernel 失败 (%d)", (int)rc);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 生命周期与 buffer 复用                                               */
/* ------------------------------------------------------------------ */

static void release_buffers(nex_opencl_ctx_ty *ctx)
{
    cl_mem *bufs[] = {
        &ctx->b_a, &ctx->b_b, &ctx->b_c, &ctx->b_zero,
        &ctx->b_sums, &ctx->b_out, &ctx->b_dot_sums, &ctx->b_dot_out,
        &ctx->b_mat_a, &ctx->b_mat_b, &ctx->b_mat_c
    };
    for (size_t i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++) {
        if (*bufs[i] != NULL) {
            (void)ctx->api.release_mem(*bufs[i]);
            *bufs[i] = NULL;
        }
    }
}

void nex_opencl_destroy(nex_opencl_ctx_ty *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->k_carry1 != NULL) {
        (void)ctx->api.release_kernel(ctx->k_carry1);
    }
    if (ctx->k_matmul != NULL) {
        (void)ctx->api.release_kernel(ctx->k_matmul);
    }
    if (ctx->k_dotreduce != NULL) {
        (void)ctx->api.release_kernel(ctx->k_dotreduce);
    }
    if (ctx->k_carry != NULL) {
        (void)ctx->api.release_kernel(ctx->k_carry);
    }
    if (ctx->k_colsum != NULL) {
        (void)ctx->api.release_kernel(ctx->k_colsum);
    }
    if (ctx->program != NULL) {
        (void)ctx->api.release_program(ctx->program);
    }
    release_buffers(ctx);
    if (ctx->queue != NULL) {
        (void)ctx->api.release_queue(ctx->queue);
    }
    if (ctx->context != NULL) {
        (void)ctx->api.release_context(ctx->context);
    }
    if (ctx->api.lib != NULL) {
        nex_cl_close(ctx->api.lib);
    }
    free(ctx);
}

static int ensure_buffer(nex_opencl_ctx_ty *ctx, cl_mem *buf, size_t *cap,
        size_t bytes, cl_mem_flags flags)
{
    if ((*buf != NULL) && (*cap >= bytes)) {
        return 0;
    }
    if (*buf != NULL) {
        (void)ctx->api.release_mem(*buf);
        *buf = NULL;
        *cap = 0;
    }
    cl_int rc = CL_SUCCESS;
    *buf = ctx->api.create_buffer(ctx->context, flags, bytes, NULL, &rc);
    if (*buf == NULL) {
        return failf(ctx, "clCreateBuffer(%zu B) 失败 (%d)", bytes, (int)rc);
    }
    *cap = bytes;
    return 0;
}

static int ensure_common_buffers(nex_opencl_ctx_ty *ctx, size_t n)
{
    const size_t a_bytes = n * ctx->limbs * sizeof(uint32_t);
    const size_t sums_bytes = n * ctx->out_digits * sizeof(cl_ulong);
    const size_t out_bytes = n * ctx->out_limbs * sizeof(uint32_t);
    if (ensure_buffer(ctx, &ctx->b_a, &ctx->cap_a, a_bytes, CL_MEM_READ_ONLY) != 0 ||
            ensure_buffer(ctx, &ctx->b_b, &ctx->cap_b, a_bytes, CL_MEM_READ_ONLY) != 0 ||
            ensure_buffer(ctx, &ctx->b_c, &ctx->cap_c, a_bytes, CL_MEM_READ_ONLY) != 0) {
        return -1;
    }
    if (ensure_buffer(ctx, &ctx->b_zero, &ctx->cap_zero, sizeof(uint32_t),
            CL_MEM_READ_ONLY) != 0) {
        return -1;
    }
    if (ensure_buffer(ctx, &ctx->b_sums, &ctx->cap_sums, sums_bytes,
            CL_MEM_READ_WRITE) != 0 ||
            ensure_buffer(ctx, &ctx->b_out, &ctx->cap_out, out_bytes,
            CL_MEM_READ_WRITE) != 0) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                            */
/* ------------------------------------------------------------------ */

nex_opencl_ctx_ty *nex_opencl_create(size_t limbs, char *err, size_t err_size)
{
    if (err != NULL && err_size > 0u) {
        err[0] = '\0';
    }
    if (limbs == 0u || limbs > (SIZE_MAX / 8u)) {
        if (err != NULL && err_size > 0u) {
            (void)snprintf(err, err_size, "limbs 非法");
        }
        return NULL;
    }
    nex_opencl_ctx_ty *ctx = (nex_opencl_ctx_ty *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        if (err != NULL && err_size > 0u) {
            (void)snprintf(err, err_size, "OOM");
        }
        return NULL;
    }
    ctx->limbs = limbs;
    ctx->digits = limbs * 2u;
    ctx->out_digits = limbs * 4u + 1u;
    ctx->out_limbs = limbs * 2u + 1u;

    if (load_api(ctx) != 0 || choose_device(ctx) != 0) {
        goto fail;
    }
    if (device_name_of(ctx, ctx->device, ctx->device_name,
            sizeof(ctx->device_name)) != 0) {
        (void)snprintf(ctx->device_name, sizeof(ctx->device_name), "(unknown)");
    }

    cl_int rc = CL_SUCCESS;
    (void)ctx->api.get_device_info(ctx->device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
            sizeof(ctx->max_wg), &ctx->max_wg, NULL);
    (void)ctx->api.get_device_info(ctx->device, CL_DEVICE_MAX_WORK_ITEM_SIZES,
            sizeof(ctx->max_item), ctx->max_item, NULL);
    if (ctx->max_wg == 0u) {
        ctx->max_wg = 1u;
    }
    for (size_t i = 0; i < 3u; i++) {
        if (ctx->max_item[i] == 0u) {
            ctx->max_item[i] = 1u;
        }
    }

    ctx->context = ctx->api.create_context(NULL, 1, &ctx->device, NULL, NULL, &rc);
    if (ctx->context == NULL) {
        (void)failf(ctx, "clCreateContext 失败 (%d)", (int)rc);
        goto fail;
    }
    ctx->queue = ctx->api.create_queue(ctx->context, ctx->device, 0, &rc);
    if (ctx->queue == NULL) {
        (void)failf(ctx, "clCreateCommandQueue 失败 (%d)", (int)rc);
        goto fail;
    }
    if (build_kernels(ctx) != 0) {
        goto fail;
    }
    return ctx;

fail:
    if (err != NULL && err_size > 0u) {
        (void)snprintf(err, err_size, "%s", ctx->err);
    }
    nex_opencl_destroy(ctx);
    return NULL;
}

const char *nex_opencl_device_name(const nex_opencl_ctx_ty *ctx)
{
    return (ctx != NULL) ? ctx->device_name : "";
}

const char *nex_opencl_error(const nex_opencl_ctx_ty *ctx)
{
    return (ctx != NULL) ? ctx->err : "";
}

size_t nex_opencl_limbs(const nex_opencl_ctx_ty *ctx)
{
    return (ctx != NULL) ? ctx->limbs : 0u;
}

size_t nex_opencl_out_limbs(const nex_opencl_ctx_ty *ctx)
{
    return (ctx != NULL) ? ctx->out_limbs : 0u;
}

static size_t round_up(size_t v, size_t m)
{
    return ((v + m - 1u) / m) * m;
}

static int set_arg(nex_opencl_ctx_ty *ctx, cl_kernel k, cl_uint index,
        size_t size, const void *ptr)
{
    cl_int rc = ctx->api.set_kernel_arg(k, index, size, ptr);
    if (rc != CL_SUCCESS) {
        return failf(ctx, "clSetKernelArg(%u) 失败 (%d)", (unsigned)index, (int)rc);
    }
    return 0;
}

#define SET_ARG(c, k, i, size, ptr) do {     if (set_arg((c), (k), (i), (size), (ptr)) != 0) {         return -1;     } } while (0)

static int run_kernel(nex_opencl_ctx_ty *ctx, cl_kernel k, cl_uint dim,
        const size_t *global, const size_t *local)
{
    cl_int rc = ctx->api.ndrange(ctx->queue, k, dim, NULL, global, local, 0, NULL, NULL);
    if (rc != CL_SUCCESS) {
        return failf(ctx, "clEnqueueNDRangeKernel 失败 (%d)", (int)rc);
    }
    return 0;
}

int nex_opencl_madd(nex_opencl_ctx_ty *ctx,
        const uint32_t *a, const uint32_t *b, const uint32_t *c,
        uint32_t *out, size_t n)
{
    if (ctx == NULL || a == NULL || b == NULL || c == NULL || out == NULL) {
        return -1;
    }
    if (n == 0u) {
        return 0;
    }
    if (n > 0xFFFFFFFFu) {
        return failf(ctx, "n 超出 uint 范围");
    }
    if (ensure_common_buffers(ctx, n) != 0) {
        return -1;
    }
    const size_t a_bytes = n * ctx->limbs * sizeof(uint32_t);
    const size_t out_bytes = n * ctx->out_limbs * sizeof(uint32_t);
    cl_int rc = ctx->api.write_buffer(ctx->queue, ctx->b_a, CL_FALSE, 0,
            a_bytes, a, 0, NULL, NULL);
    if (rc == CL_SUCCESS) {
        rc = ctx->api.write_buffer(ctx->queue, ctx->b_b, CL_FALSE, 0, a_bytes, b,
                0, NULL, NULL);
    }
    if (rc == CL_SUCCESS) {
        rc = ctx->api.write_buffer(ctx->queue, ctx->b_c, CL_FALSE, 0, a_bytes, c,
                0, NULL, NULL);
    }
    if (rc != CL_SUCCESS) {
        return failf(ctx, "上传输入失败 (%d)", (int)rc);
    }

    cl_uint nu = (cl_uint)n;
    const int use_c = 1;
    SET_ARG(ctx, ctx->k_colsum, 0, sizeof(cl_mem), &ctx->b_a);
    SET_ARG(ctx, ctx->k_colsum, 1, sizeof(cl_mem), &ctx->b_b);
    SET_ARG(ctx, ctx->k_colsum, 2, sizeof(cl_mem), &ctx->b_c);
    SET_ARG(ctx, ctx->k_colsum, 3, sizeof(cl_mem), &ctx->b_sums);
    SET_ARG(ctx, ctx->k_colsum, 4, sizeof(cl_uint), &nu);
    SET_ARG(ctx, ctx->k_colsum, 5, sizeof(int), &use_c);

    size_t lx = ctx->max_item[1];
    if (lx > 32u) {
        lx = 32u;
    }
    if (lx > ctx->max_wg) {
        lx = ctx->max_wg;
    }
    if (lx == 0u) {
        lx = 1u;
    }
    const size_t g1[2] = { n, round_up(ctx->out_digits, lx) };
    const size_t l1[2] = { 1u, lx };
    if (run_kernel(ctx, ctx->k_colsum, 2, g1, l1) != 0) {
        return -1;
    }

    size_t lc = ctx->max_item[0];
    if (lc > 64u) {
        lc = 64u;
    }
    if (lc > ctx->max_wg) {
        lc = ctx->max_wg;
    }
    if (lc == 0u) {
        lc = 1u;
    }
    const size_t g2[1] = { round_up(n, lc) };
    const size_t l2[1] = { lc };
    SET_ARG(ctx, ctx->k_carry, 0, sizeof(cl_mem), &ctx->b_sums);
    SET_ARG(ctx, ctx->k_carry, 1, sizeof(cl_mem), &ctx->b_out);
    SET_ARG(ctx, ctx->k_carry, 2, sizeof(cl_uint), &nu);
    if (run_kernel(ctx, ctx->k_carry, 1, g2, l2) != 0) {
        return -1;
    }

    rc = ctx->api.read_buffer(ctx->queue, ctx->b_out, CL_TRUE, 0, out_bytes,
            out, 0, NULL, NULL);
    if (rc != CL_SUCCESS) {
        return failf(ctx, "读取结果失败 (%d)", (int)rc);
    }
    return 0;
}

int nex_opencl_dot(nex_opencl_ctx_ty *ctx,
        const uint32_t *a, const uint32_t *b,
        uint32_t *out, size_t n)
{
    if (ctx == NULL || a == NULL || b == NULL || out == NULL) {
        return -1;
    }
    if (n > 0xFFFFFFFFu) {
        return failf(ctx, "n 超出 uint 范围（dot 上限 2^32）");
    }
    if (n == 0u) {
        memset(out, 0, ctx->out_limbs * sizeof(uint32_t));
        return 0;
    }
    if (ensure_common_buffers(ctx, n) != 0) {
        return -1;
    }
    const size_t a_bytes = n * ctx->limbs * sizeof(uint32_t);
    cl_int rc = ctx->api.write_buffer(ctx->queue, ctx->b_a, CL_FALSE, 0,
            a_bytes, a, 0, NULL, NULL);
    if (rc == CL_SUCCESS) {
        rc = ctx->api.write_buffer(ctx->queue, ctx->b_b, CL_FALSE, 0, a_bytes, b,
                0, NULL, NULL);
    }
    if (rc != CL_SUCCESS) {
        return failf(ctx, "上传输入失败 (%d)", (int)rc);
    }

    cl_uint nu = (cl_uint)n;
    const int use_c = 0;
    SET_ARG(ctx, ctx->k_colsum, 0, sizeof(cl_mem), &ctx->b_a);
    SET_ARG(ctx, ctx->k_colsum, 1, sizeof(cl_mem), &ctx->b_b);
    SET_ARG(ctx, ctx->k_colsum, 2, sizeof(cl_mem), &ctx->b_zero);
    SET_ARG(ctx, ctx->k_colsum, 3, sizeof(cl_mem), &ctx->b_sums);
    SET_ARG(ctx, ctx->k_colsum, 4, sizeof(cl_uint), &nu);
    SET_ARG(ctx, ctx->k_colsum, 5, sizeof(int), &use_c);

    size_t lx = ctx->max_item[1];
    if (lx > 32u) {
        lx = 32u;
    }
    if (lx > ctx->max_wg) {
        lx = ctx->max_wg;
    }
    if (lx == 0u) {
        lx = 1u;
    }
    const size_t g1[2] = { n, round_up(ctx->out_digits, lx) };
    const size_t l1[2] = { 1u, lx };
    if (run_kernel(ctx, ctx->k_colsum, 2, g1, l1) != 0) {
        return -1;
    }

    /* 归约：out_digits 个 64-bit 累加器 */
    const size_t dot_sums_bytes = ctx->out_digits * sizeof(cl_ulong);
    const size_t dot_out_bytes = ctx->out_limbs * sizeof(uint32_t);
    if (ensure_buffer(ctx, &ctx->b_dot_sums, &ctx->cap_dot_sums, dot_sums_bytes,
            CL_MEM_READ_WRITE) != 0 ||
            ensure_buffer(ctx, &ctx->b_dot_out, &ctx->cap_dot_out, dot_out_bytes,
            CL_MEM_READ_WRITE) != 0) {
        return -1;
    }
    size_t ld = ctx->max_item[0];
    if (ld > 64u) {
        ld = 64u;
    }
    if (ld > ctx->max_wg) {
        ld = ctx->max_wg;
    }
    if (ld == 0u) {
        ld = 1u;
    }
    const size_t g2[1] = { round_up(ctx->out_digits, ld) };
    const size_t l2[1] = { ld };
    SET_ARG(ctx, ctx->k_dotreduce, 0, sizeof(cl_mem), &ctx->b_sums);
    SET_ARG(ctx, ctx->k_dotreduce, 1, sizeof(cl_mem), &ctx->b_dot_sums);
    SET_ARG(ctx, ctx->k_dotreduce, 2, sizeof(cl_uint), &nu);
    if (run_kernel(ctx, ctx->k_dotreduce, 1, g2, l2) != 0) {
        return -1;
    }
    const size_t g3[1] = { 1u };
    const size_t l3[1] = { 1u };
    SET_ARG(ctx, ctx->k_carry1, 0, sizeof(cl_mem), &ctx->b_dot_sums);
    SET_ARG(ctx, ctx->k_carry1, 1, sizeof(cl_mem), &ctx->b_dot_out);
    if (run_kernel(ctx, ctx->k_carry1, 1, g3, l3) != 0) {
        return -1;
    }
    rc = ctx->api.read_buffer(ctx->queue, ctx->b_dot_out, CL_TRUE, 0,
            dot_out_bytes, out, 0, NULL, NULL);
    if (rc != CL_SUCCESS) {
        return failf(ctx, "读取 dot 结果失败 (%d)", (int)rc);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 子矩阵乘                                                            */
/* ------------------------------------------------------------------ */

static int idx_contiguous(const uint32_t *idx, size_t start, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (idx[start + i] != (uint32_t)(start + i)) {
            return 0;
        }
    }
    return 1;
}

static void pack_a_rows(const uint32_t *A, size_t lda, const uint32_t *rows,
        size_t r0, size_t nr, size_t k, size_t limbs, uint32_t *out)
{
    if (idx_contiguous(rows, r0, nr)) {
        memcpy(out, A + r0 * lda, nr * k * limbs * sizeof(uint32_t));
        return;
    }
    for (size_t i = 0; i < nr; i++) {
        memcpy(out + i * k * limbs,
                A + (size_t)rows[r0 + i] * lda,
                k * limbs * sizeof(uint32_t));
    }
}

static void pack_b_cols(const uint32_t *B, size_t ldb, const uint32_t *cols,
        size_t c0, size_t nc, size_t k, size_t limbs, uint32_t *out)
{
    if (idx_contiguous(cols, c0, nc)) {
        for (size_t p = 0; p < k; p++) {
            memcpy(out + p * nc * limbs,
                    B + p * ldb + c0 * limbs,
                    nc * limbs * sizeof(uint32_t));
        }
        return;
    }
    for (size_t p = 0; p < k; p++) {
        for (size_t j = 0; j < nc; j++) {
            memcpy(out + (p * nc + j) * limbs,
                    B + p * ldb + (size_t)cols[c0 + j] * limbs,
                    limbs * sizeof(uint32_t));
        }
    }
}

int nex_opencl_matmul_subset(nex_opencl_ctx_ty *ctx,
        const uint32_t *A, size_t lda, const uint32_t *B, size_t ldb,
        const uint32_t *rows, size_t nrows,
        const uint32_t *cols, size_t ncols,
        uint32_t *C, size_t ldc, size_t k)
{
    if (ctx == NULL || A == NULL || B == NULL || rows == NULL || cols == NULL
            || C == NULL || nrows == 0u || ncols == 0u || k == 0u) {
        return -1;
    }
    if (k > 0xFFFFFFFFu || ncols > 0xFFFFFFFFu) {
        return failf(ctx, "k/ncols 超出 uint 范围");
    }
    if (nrows > (SIZE_MAX / ncols)) {
        return failf(ctx, "nrows*ncols 溢出");
    }
    if ((double)k * (double)ctx->digits * 65535.0 * 65535.0 >= 1.8e19) {
        return failf(ctx, "k 过大，64-bit 累加可能溢出");
    }

    const size_t row_bytes = k * ctx->limbs * sizeof(uint32_t);
    const size_t col_bytes = k * ctx->limbs * sizeof(uint32_t);
    const size_t cell_bytes = ctx->out_limbs * sizeof(uint32_t);
    const size_t budget = 2500000000u; /* 2.5 GB，给 4 GB 显存留余量 */

    size_t rt = nrows;
    size_t ct = ncols;
    const char *tile_env = getenv("NEX_OPENCL_MATMUL_TILE");
    size_t max_tile = (tile_env != NULL) ? (size_t)strtoull(tile_env, NULL, 10) : 0u;
    if (max_tile > 0u) {
        if (rt > max_tile) {
            rt = max_tile;
        }
        if (ct > max_tile) {
            ct = max_tile;
        }
    }
    while ((rt > 1u || ct > 1u) &&
            (rt * row_bytes + ct * col_bytes + rt * ct * cell_bytes > budget)) {
        if (rt >= ct) {
            rt = (rt + 1u) / 2u;
        } else {
            ct = (ct + 1u) / 2u;
        }
    }

    uint32_t *a_pack = (uint32_t *)malloc(rt * k * ctx->limbs * sizeof(uint32_t));
    uint32_t *b_pack = (uint32_t *)malloc(k * ct * ctx->limbs * sizeof(uint32_t));
    uint32_t *c_pack = (uint32_t *)malloc(rt * ct * ctx->out_limbs * sizeof(uint32_t));
    if (a_pack == NULL || b_pack == NULL || c_pack == NULL) {
        free(a_pack);
        free(b_pack);
        free(c_pack);
        return failf(ctx, "OOM（matmul 主机缓冲）");
    }
    if (ensure_buffer(ctx, &ctx->b_mat_a, &ctx->cap_mat_a,
                rt * row_bytes, CL_MEM_READ_ONLY) != 0 ||
            ensure_buffer(ctx, &ctx->b_mat_b, &ctx->cap_mat_b,
                ct * col_bytes, CL_MEM_READ_ONLY) != 0 ||
            ensure_buffer(ctx, &ctx->b_mat_c, &ctx->cap_mat_c,
                rt * ct * cell_bytes, CL_MEM_READ_WRITE) != 0) {
        free(a_pack);
        free(b_pack);
        free(c_pack);
        return -1;
    }

    const size_t lwork = 64u;
    const int rows_outer = (nrows <= ncols);
    cl_int rc = CL_SUCCESS;

    for (size_t r0 = 0; r0 < nrows; r0 += rt) {
        if (!rows_outer) {
            continue;
        }
        const size_t nr = (r0 + rt > nrows) ? (nrows - r0) : rt;
        pack_a_rows(A, lda, rows, r0, nr, k, ctx->limbs, a_pack);
        rc = ctx->api.write_buffer(ctx->queue, ctx->b_mat_a, CL_FALSE, 0,
                nr * row_bytes, a_pack, 0, NULL, NULL);
        if (rc != CL_SUCCESS) {
            free(a_pack);
            free(b_pack);
            free(c_pack);
            return failf(ctx, "上传 A tile 失败 (%d)", (int)rc);
        }
        for (size_t c0 = 0; c0 < ncols; c0 += ct) {
            const size_t nc = (c0 + ct > ncols) ? (ncols - c0) : ct;
            pack_b_cols(B, ldb, cols, c0, nc, k, ctx->limbs, b_pack);
            rc = ctx->api.write_buffer(ctx->queue, ctx->b_mat_b, CL_FALSE, 0,
                    nc * col_bytes, b_pack, 0, NULL, NULL);
            if (rc != CL_SUCCESS) {
                free(a_pack);
                free(b_pack);
                free(c_pack);
                return failf(ctx, "上传 B tile 失败 (%d)", (int)rc);
            }
            cl_uint kk = (cl_uint)k;
            cl_uint ncc = (cl_uint)nc;
            cl_uint nout = (cl_uint)(nr * nc);
            SET_ARG(ctx, ctx->k_matmul, 0, sizeof(cl_mem), &ctx->b_mat_a);
            SET_ARG(ctx, ctx->k_matmul, 1, sizeof(cl_mem), &ctx->b_mat_b);
            SET_ARG(ctx, ctx->k_matmul, 2, sizeof(cl_mem), &ctx->b_mat_c);
            SET_ARG(ctx, ctx->k_matmul, 3, sizeof(cl_uint), &nout);
            SET_ARG(ctx, ctx->k_matmul, 4, sizeof(cl_uint), &kk);
            SET_ARG(ctx, ctx->k_matmul, 5, sizeof(cl_uint), &ncc);
            const size_t global[1] = { nr * nc * 64u };
            const size_t local[1] = { lwork };
            if (run_kernel(ctx, ctx->k_matmul, 1, global, local) != 0) {
                free(a_pack);
                free(b_pack);
                free(c_pack);
                return -1;
            }
            rc = ctx->api.read_buffer(ctx->queue, ctx->b_mat_c, CL_TRUE, 0,
                    nr * nc * cell_bytes, c_pack, 0, NULL, NULL);
            if (rc != CL_SUCCESS) {
                free(a_pack);
                free(b_pack);
                free(c_pack);
                return failf(ctx, "读取 C tile 失败 (%d)", (int)rc);
            }
            for (size_t i = 0; i < nr; i++) {
                memcpy(C + (r0 + i) * ldc + c0 * ctx->out_limbs,
                        c_pack + i * nc * ctx->out_limbs,
                        nc * ctx->out_limbs * sizeof(uint32_t));
            }
        }
    }

    if (!rows_outer) {
        for (size_t c0 = 0; c0 < ncols; c0 += ct) {
            const size_t nc = (c0 + ct > ncols) ? (ncols - c0) : ct;
            pack_b_cols(B, ldb, cols, c0, nc, k, ctx->limbs, b_pack);
            rc = ctx->api.write_buffer(ctx->queue, ctx->b_mat_b, CL_FALSE, 0,
                    nc * col_bytes, b_pack, 0, NULL, NULL);
            if (rc != CL_SUCCESS) {
                free(a_pack);
                free(b_pack);
                free(c_pack);
                return failf(ctx, "上传 B tile 失败 (%d)", (int)rc);
            }
            for (size_t r0 = 0; r0 < nrows; r0 += rt) {
                const size_t nr = (r0 + rt > nrows) ? (nrows - r0) : rt;
                pack_a_rows(A, lda, rows, r0, nr, k, ctx->limbs, a_pack);
                rc = ctx->api.write_buffer(ctx->queue, ctx->b_mat_a, CL_FALSE, 0,
                        nr * row_bytes, a_pack, 0, NULL, NULL);
                if (rc != CL_SUCCESS) {
                    free(a_pack);
                    free(b_pack);
                    free(c_pack);
                    return failf(ctx, "上传 A tile 失败 (%d)", (int)rc);
                }
                cl_uint kk = (cl_uint)k;
                cl_uint ncc = (cl_uint)nc;
                cl_uint nout = (cl_uint)(nr * nc);
                SET_ARG(ctx, ctx->k_matmul, 0, sizeof(cl_mem), &ctx->b_mat_a);
                SET_ARG(ctx, ctx->k_matmul, 1, sizeof(cl_mem), &ctx->b_mat_b);
                SET_ARG(ctx, ctx->k_matmul, 2, sizeof(cl_mem), &ctx->b_mat_c);
                SET_ARG(ctx, ctx->k_matmul, 3, sizeof(cl_uint), &nout);
                SET_ARG(ctx, ctx->k_matmul, 4, sizeof(cl_uint), &kk);
                SET_ARG(ctx, ctx->k_matmul, 5, sizeof(cl_uint), &ncc);
                const size_t global[1] = { nr * nc * 64u };
                const size_t local[1] = { lwork };
                if (run_kernel(ctx, ctx->k_matmul, 1, global, local) != 0) {
                    free(a_pack);
                    free(b_pack);
                    free(c_pack);
                    return -1;
                }
                rc = ctx->api.read_buffer(ctx->queue, ctx->b_mat_c, CL_TRUE, 0,
                        nr * nc * cell_bytes, c_pack, 0, NULL, NULL);
                if (rc != CL_SUCCESS) {
                    free(a_pack);
                    free(b_pack);
                    free(c_pack);
                    return failf(ctx, "读取 C tile 失败 (%d)", (int)rc);
                }
                for (size_t i = 0; i < nr; i++) {
                    memcpy(C + (r0 + i) * ldc + c0 * ctx->out_limbs,
                            c_pack + i * nc * ctx->out_limbs,
                            nc * ctx->out_limbs * sizeof(uint32_t));
                }
            }
        }
    }

    free(a_pack);
    free(b_pack);
    free(c_pack);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 便捷转换                                                            */
/* ------------------------------------------------------------------ */

int nex_opencl_pack_bin(const bigint_bin_ty *v, uint32_t *out, size_t limbs)
{
    if (v == NULL || out == NULL || limbs == 0u) {
        return -1;
    }
    if (v->sign == BIGINT_SIGN_NEG_E || v->len > limbs) {
        return -1;
    }
    memset(out, 0, limbs * sizeof(uint32_t));
    if (v->len > 0u) {
        memcpy(out, v->limbs, v->len * sizeof(uint32_t));
    }
    return 0;
}

int nex_opencl_unpack_bin(const uint32_t *limbs, size_t count, bigint_bin_ty *v)
{
    if (limbs == NULL || v == NULL) {
        return -1;
    }
    size_t n = count;
    while (n > 0u && limbs[n - 1u] == 0u) {
        n--;
    }
    if (n > v->cap) {
        bigint_bin_free(v);
        if (bigint_bin_init_cap(v, n) != BIGINT_OK_E) {
            return -1;
        }
    }
    if (n == 0u) {
        v->len = 0u;
        v->sign = BIGINT_SIGN_ZERO_E;
        return 0;
    }
    memcpy(v->limbs, limbs, n * sizeof(uint32_t));
    v->len = n;
    v->sign = BIGINT_SIGN_POS_E;
    return 0;
}
