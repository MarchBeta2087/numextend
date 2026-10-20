/*
 * nex_opencl_abi.h：OpenCL 1.2 ABI 的最小子集（仅本实验用到的类型/常量）。
 *
 * 不包含 Khronos 头文件，也不链接 OpenCL 库：运行时用 dlopen/LoadLibrary
 * 动态解析入口。这样 Windows 不需要 SDK/import lib，Linux 只需要 ICD
 * loader（libOpenCL.so.1）。类型与常量取自 Khronos OpenCL 1.2 规范。
 */
#ifndef NEX_OPENCL_ABI_H
#define NEX_OPENCL_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef int64_t cl_long;
typedef uint64_t cl_bitfield;
typedef intptr_t cl_context_properties;
typedef cl_bitfield cl_device_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_bitfield cl_command_queue_properties;
typedef cl_bitfield cl_device_info;
typedef cl_bitfield cl_platform_info;
typedef cl_bitfield cl_program_build_info;
typedef cl_bitfield cl_kernel_work_group_info;

typedef struct _cl_platform_id *cl_platform_id;
typedef struct _cl_device_id *cl_device_id;
typedef struct _cl_context *cl_context;
typedef struct _cl_command_queue *cl_command_queue;
typedef struct _cl_mem *cl_mem;
typedef struct _cl_program *cl_program;
typedef struct _cl_kernel *cl_kernel;
typedef struct _cl_event *cl_event;

#define CL_SUCCESS 0
#define CL_DEVICE_NOT_FOUND -1

#define CL_DEVICE_TYPE_DEFAULT (1u << 0)
#define CL_DEVICE_TYPE_CPU (1u << 1)
#define CL_DEVICE_TYPE_GPU (1u << 2)
#define CL_DEVICE_TYPE_ALL 0xFFFFFFFFu

#define CL_PLATFORM_NAME 0x0902u
#define CL_PLATFORM_VENDOR 0x0903u
#define CL_PLATFORM_VERSION 0x0901u

#define CL_DEVICE_TYPE_INFO 0x1000u
#define CL_DEVICE_MAX_WORK_GROUP_SIZE 0x1004u
#define CL_DEVICE_MAX_WORK_ITEM_SIZES 0x1005u
#define CL_DEVICE_MAX_MEM_ALLOC_SIZE 0x1010u
#define CL_DEVICE_GLOBAL_MEM_SIZE 0x101Fu
#define CL_DEVICE_LOCAL_MEM_SIZE 0x1023u
#define CL_DEVICE_NAME 0x102Bu
#define CL_DEVICE_VENDOR 0x102Cu
#define CL_DEVICE_VERSION 0x102Fu

#define CL_MEM_READ_WRITE (1u << 0)
#define CL_MEM_WRITE_ONLY (1u << 1)
#define CL_MEM_READ_ONLY (1u << 2)
#define CL_MEM_USE_HOST_PTR (1u << 3)
#define CL_MEM_ALLOC_HOST_PTR (1u << 4)
#define CL_MEM_COPY_HOST_PTR (1u << 5)

#define CL_PROGRAM_BUILD_LOG 0x1183u

#define CL_TRUE 1
#define CL_FALSE 0

#ifdef __cplusplus
}
#endif

#endif /* NEX_OPENCL_ABI_H */
