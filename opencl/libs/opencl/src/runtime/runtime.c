/*
 * runtime.c - the libOpenCL platform/device registry and the shared helpers
 * every API translation unit uses (clGet*Info marshalling, the profiling clock,
 * buffer residency, event lifecycle primitives).
 *
 * There is exactly one platform.  It always registers a CPU device (the
 * software OpenCL-C subset interpreter, present on every machine); it also
 * registers a GPU device when the build includes the GPU backend AND that
 * backend's probe finds usable hardware (raspi5's V3D compute path today).
 * A machine without GPU support therefore exposes CPU-only OpenCL with no code
 * divergence.
 */
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>

#include <ewoksys/kernel_tic.h>

/* GPU device registration is compiled in only when the machine exports the
 * 3D/compute path (Makefile defines OPENCL_GPU_BACKEND under PGL_G3D=yes).
 * cl_gpu_probe() lives in src/gpu/gpu_device.c and returns non-zero when a
 * usable V3D compute device is present. */
#ifdef OPENCL_GPU_BACKEND
extern int cl_gpu_probe(void);
extern void cl_gpu_device_init(struct _cl_device_id *dev);
#endif

#define CL_PLATFORM_PROFILE_STR   "FULL_PROFILE"
#define CL_PLATFORM_VERSION_STR   "OpenCL 1.2 EwokOS"
#define CL_PLATFORM_NAME_STR      "EwokOS OpenCL"
#define CL_PLATFORM_VENDOR_STR    "EwokOS"
#define CL_PLATFORM_EXT_STR       "cl_khr_icd cl_khr_global_int32_base_atomics cl_khr_byte_addressable_store"

/* ---- device descriptors -------------------------------------------------- */
static struct _cl_device_id g_cpu_dev;
#ifdef OPENCL_GPU_BACKEND
static struct _cl_device_id g_gpu_dev;
#endif

static struct _cl_platform_id g_platform;
static int g_initialised;

static void init_cpu_device(struct _cl_device_id *d)
{
    memset(d, 0, sizeof(*d));
    d->type = CL_DEVICE_TYPE_CPU;
    d->platform = &g_platform;
    d->backend = &cl_backend_cpu;

    d->name = "EwokOS CPU";
    d->vendor = "EwokOS";
    d->driver_version = "1.2";
    d->profile = "FULL_PROFILE";
    d->version = "OpenCL 1.2 EwokOS";
    d->opencl_c_version = "OpenCL C 1.2";
    d->extensions = "cl_khr_byte_addressable_store cl_khr_global_int32_base_atomics";
    d->built_in_kernels = "";

    d->vendor_id = 0;
    d->max_compute_units = 4;
    d->max_work_item_dimensions = 3;
    d->max_work_group_size = 256;
    d->max_work_item_sizes[0] = 256;
    d->max_work_item_sizes[1] = 256;
    d->max_work_item_sizes[2] = 256;
    d->max_clock_frequency = 1000;
    d->address_bits = (cl_uint)(sizeof(void *) * 8);
    d->max_mem_alloc_size = 256ull * 1024 * 1024;
    d->global_mem_size = 512ull * 1024 * 1024;
    d->local_mem_size = 64 * 1024;
    d->max_constant_buffer_size = 64 * 1024;
    d->max_constant_args = CL_MAX_KERNEL_ARGS;
    d->max_parameter_size = 1024;
    d->mem_base_addr_align = 1024;
    d->profiling_timer_resolution = 1;
    d->image_support = CL_TRUE;
    d->compiler_available = CL_TRUE;
    d->linker_available = CL_TRUE;
    d->endian_little = CL_TRUE;
    d->available = CL_TRUE;
    d->host_unified_memory = CL_TRUE;
    d->single_fp_config = CL_FP_INF_NAN | CL_FP_ROUND_TO_NEAREST |
                          CL_FP_ROUND_TO_ZERO | CL_FP_ROUND_TO_INF |
                          CL_FP_FMA | CL_FP_DENORM;
    d->double_fp_config = CL_FP_INF_NAN | CL_FP_ROUND_TO_NEAREST |
                          CL_FP_ROUND_TO_ZERO | CL_FP_ROUND_TO_INF |
                          CL_FP_FMA | CL_FP_DENORM;
    d->global_mem_cache_type = CL_READ_WRITE_CACHE;
    d->global_mem_cacheline_size = 64;
    d->global_mem_cache_size = 32 * 1024;
    d->local_mem_type = CL_GLOBAL;
    d->exec_capabilities = CL_EXEC_KERNEL | CL_EXEC_NATIVE_KERNEL;
    d->queue_properties = CL_QUEUE_PROFILING_ENABLE;
    d->printf_buffer_size = 64 * 1024;
}

static void runtime_init(void)
{
    if (g_initialised)
        return;
    g_initialised = 1;

    g_platform.profile = CL_PLATFORM_PROFILE_STR;
    g_platform.version = CL_PLATFORM_VERSION_STR;
    g_platform.name = CL_PLATFORM_NAME_STR;
    g_platform.vendor = CL_PLATFORM_VENDOR_STR;
    g_platform.extensions = CL_PLATFORM_EXT_STR;
    g_platform.icd_suffix = "EwokOS";

    init_cpu_device(&g_cpu_dev);
    g_cpu_dev.index = 0;
    g_platform.devices[g_platform.num_devices++] = &g_cpu_dev;

#ifdef OPENCL_GPU_BACKEND
    if (cl_gpu_probe()) {
        memset(&g_gpu_dev, 0, sizeof(g_gpu_dev));
        g_gpu_dev.type = CL_DEVICE_TYPE_GPU;
        g_gpu_dev.platform = &g_platform;
        g_gpu_dev.backend = &cl_backend_gpu;
        g_gpu_dev.index = (int)g_platform.num_devices;
        cl_gpu_device_init(&g_gpu_dev);
        g_platform.devices[g_platform.num_devices++] = &g_gpu_dev;
    }
#endif
}

struct _cl_platform_id *cl_runtime_platform(void)
{
    runtime_init();
    return &g_platform;
}

cl_uint cl_runtime_num_devices(void)
{
    runtime_init();
    return g_platform.num_devices;
}

struct _cl_device_id *cl_runtime_device(cl_uint i)
{
    runtime_init();
    if (i >= g_platform.num_devices)
        return NULL;
    return g_platform.devices[i];
}

int cl_device_index(struct _cl_device_id *dev)
{
    cl_uint i;
    runtime_init();
    for (i = 0; i < g_platform.num_devices; i++)
        if (g_platform.devices[i] == dev)
            return (int)i;
    return -1;
}

int cl_valid_device(struct _cl_device_id *dev)
{
    return cl_device_index(dev) >= 0;
}

int cl_context_has_device(struct _cl_context *ctx, struct _cl_device_id *dev)
{
    cl_uint i;
    if (!ctx || !dev)
        return 0;
    for (i = 0; i < ctx->num_devices; i++)
        if (ctx->devices[i] == dev)
            return 1;
    return 0;
}

/* ---- clGet*Info marshalling --------------------------------------------- */
cl_int cl_get_info(const void *src, size_t src_size, size_t param_value_size,
                   void *param_value, size_t *param_value_size_ret)
{
    if (param_value_size_ret)
        *param_value_size_ret = src_size;
    if (param_value) {
        if (param_value_size < src_size)
            return CL_INVALID_VALUE;
        memcpy(param_value, src, src_size);
    }
    return CL_SUCCESS;
}

cl_int cl_get_info_str(const char *s, size_t param_value_size,
                       void *param_value, size_t *param_value_size_ret)
{
    size_t n;
    if (!s)
        s = "";
    n = strlen(s) + 1;
    return cl_get_info(s, n, param_value_size, param_value, param_value_size_ret);
}

/* ---- profiling clock ----------------------------------------------------- */
cl_ulong cl_now_ns(void)
{
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0) {
        /* fall back to the tick clock scaled to ns */
        uint32_t sec = 0;
        uint64_t usec = 0;
        kernel_tic(&sec, &usec);
        ns = (uint64_t)sec * 1000000000ull + usec * 1000ull;
    }
    return (cl_ulong)ns;
}

/* ---- buffer residency ---------------------------------------------------- */
void *cl_mem_device_ptr(struct _cl_mem *m)
{
    if (!m)
        return NULL;
    if (m->parent)
        return (char *)cl_mem_device_ptr(m->parent) + m->origin;
    return m->host_ptr;
}
