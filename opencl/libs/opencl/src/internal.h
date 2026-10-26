/*
 * internal.h - libOpenCL runtime object model, shared by every src/api and
 * src/backend translation unit.  Not installed; applications see only the
 * public CL headers.
 *
 * Design:
 *   - one cl_platform (the EwokOS OpenCL platform) owning a small fixed set of
 *     cl_device: a CPU device that is always present, plus a GPU device on
 *     machines whose bsp_cl reports hardware support (raspi5 today).
 *   - each device carries a cl_device_backend vtable.  The runtime core is
 *     device-agnostic: it owns object lifetime, refcounts, events, queues and
 *     memory residency, and delegates program compilation and kernel execution
 *     to the selected backend (cpu/ or gpu/).
 *   - memory objects own a host-resident store; the GPU backend additionally
 *     keeps a device-resident mirror (Phase 2).  Buffers are the primary
 *     object; images add a pixel layout.
 */
#ifndef OPENCL_INTERNAL_H
#define OPENCL_INTERNAL_H

#include <CL/cl.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* forward decls of the OpenCL-C front-end program representation (clc.h) */
struct clc_program;

#define CL_MAX_DEVICES          4
#define CL_MAX_KERNEL_ARGS      32
#define CL_MAX_PROGRAM_DEVICES  CL_MAX_DEVICES
#define CL_LOG_SIZE             8192
#define CL_MAX_WORK_DIM         3

/* ---- device backend vtable ---------------------------------------------- */
struct _cl_device_id;
struct _cl_program;
struct _cl_kernel;
struct _cl_command_queue;

typedef struct cl_device_backend {
    const char *name;

    /* Compile `source` for this device.  On success returns a backend-owned
     * program handle stored in prog->exec[dev_index] and writes 0 into
     * build_status; on failure sets build_status = CL_BUILD_ERROR and fills
     * log (NUL-terminated, at most logsz-1 chars). */
    int (*build)(struct _cl_program *prog, struct _cl_device_id *dev,
                 const char *source, const char *options,
                 cl_build_status *build_status, char *log, size_t logsz);

    /* Release the backend program handle for one device. */
    void (*free_exec)(void *exec);

    /* Prepare a kernel object (resolve arg layout) after clCreateKernel.
     * Returns 0 on success. */
    int (*prepare_kernel)(struct _cl_kernel *kernel);

    /* Execute one enqueued NDRange (or single work-item task) dispatch.  The
     * runtime has already validated dimensions, resolved the work-group size
     * and made every buffer arg device-resident.  Returns a CL_* code. */
    int (*ndrange)(struct _cl_command_queue *queue, struct _cl_kernel *kernel,
                   cl_uint work_dim,
                   const size_t *global_work_offset,
                   const size_t *global_work_size,
                   const size_t *local_work_size);

    void (*release_kernel)(struct _cl_kernel *kernel);
} cl_device_backend;

/* the two backends (cpu/cpu_device.c, gpu/gpu_device.c) */
extern const cl_device_backend cl_backend_cpu;
extern const cl_device_backend cl_backend_gpu;

/* ---- platform ------------------------------------------------------------ */
struct _cl_platform_id {
    const char *profile;
    const char *version;
    const char *name;
    const char *vendor;
    const char *extensions;
    const char *icd_suffix;

    cl_uint                 num_devices;
    struct _cl_device_id   *devices[CL_MAX_DEVICES];
};

/* ---- device -------------------------------------------------------------- */
struct _cl_device_id {
    cl_device_type              type;
    struct _cl_platform_id     *platform;
    const cl_device_backend    *backend;

    const char *name;
    const char *vendor;
    const char *driver_version;
    const char *profile;
    const char *version;
    const char *opencl_c_version;
    const char *extensions;
    const char *built_in_kernels;

    cl_uint     vendor_id;
    cl_uint     max_compute_units;
    cl_uint     max_work_item_dimensions;
    size_t      max_work_group_size;
    size_t      max_work_item_sizes[3];
    cl_uint     max_clock_frequency;
    cl_uint     address_bits;
    cl_ulong    max_mem_alloc_size;
    cl_ulong    global_mem_size;
    cl_ulong    local_mem_size;
    cl_ulong    max_constant_buffer_size;
    cl_uint     max_constant_args;
    cl_uint     max_parameter_size;
    cl_uint     mem_base_addr_align;
    cl_uint     profiling_timer_resolution;
    cl_bool     image_support;
    cl_bool     compiler_available;
    cl_bool     linker_available;
    cl_bool     endian_little;
    cl_bool     available;
    cl_bool     host_unified_memory;
    cl_device_fp_config         single_fp_config;
    cl_device_fp_config         double_fp_config;
    cl_device_mem_cache_type    global_mem_cache_type;
    cl_uint                     global_mem_cacheline_size;
    cl_ulong                    global_mem_cache_size;
    cl_device_local_mem_type    local_mem_type;
    cl_device_exec_capabilities exec_capabilities;
    cl_command_queue_properties queue_properties;
    cl_uint     printf_buffer_size;

    /* per-device program exec handles are stored on the program, indexed by
     * this device's slot in the platform device list. */
    int         index;
    void       *backend_data;
};

/* ---- context ------------------------------------------------------------- */
struct _cl_context {
    cl_int                  refcount;
    cl_uint                 num_devices;
    struct _cl_device_id   *devices[CL_MAX_DEVICES];
    cl_context_properties   props[8];
    cl_uint                 num_props;
};

/* ---- command queue ------------------------------------------------------- */
struct _cl_command_queue {
    cl_int                          refcount;
    struct _cl_context             *context;
    struct _cl_device_id           *device;
    cl_command_queue_properties     properties;
};

/* ---- memory object ------------------------------------------------------- */
struct _cl_mem {
    cl_int                  refcount;
    cl_mem_object_type      type;
    cl_mem_flags            flags;
    struct _cl_context     *context;
    size_t                  size;

    /* CPU and GPU share this store.  GPU-context buffers prefer contig shm so
     * dispatch can use them directly; other buffers retain staging fallback. */
    void                   *host_ptr;
    int                     owns_ptr;
    int                     gfx_shm;       /* >0: GPU-addressable contig shm */

    /* sub-buffer linkage */
    struct _cl_mem         *parent;
    size_t                  origin;

    /* image geometry (type == IMAGE*) */
    cl_image_format         format;
    size_t                  width, height, depth;
    size_t                  row_pitch, slice_pitch;
    size_t                  elem_size;

    cl_uint                 map_count;

    /* GPU-side residency handle (backend-owned; NULL on CPU-only) */
    void                   *gpu_resident;
};

/* ---- program ------------------------------------------------------------- */
struct _cl_program_build {
    struct _cl_device_id   *device;
    cl_build_status         status;
    cl_program_binary_type  binary_type;
    void                   *exec;               /* backend program handle   */
    char                    options[256];
    char                    log[CL_LOG_SIZE];
};

struct _cl_program {
    cl_int                          refcount;
    struct _cl_context             *context;
    cl_uint                         num_devices;
    struct _cl_device_id           *devices[CL_MAX_PROGRAM_DEVICES];
    char                           *source;      /* owned, NUL-terminated   */
    struct clc_program             *frontend;    /* parsed AST (shared)     */
    struct _cl_program_build        build[CL_MAX_PROGRAM_DEVICES];
    char                           *kernel_names;/* ';' joined (owned)      */
};

/* ---- kernel argument ----------------------------------------------------- */
typedef struct cl_kernel_arg {
    int                 set;
    int                 is_mem;      /* 1: cl_mem buffer/image, 0: scalar   */
    int                 is_local;    /* 1: __local (arg_size = bytes)       */
    struct _cl_mem     *mem;         /* when is_mem                          */
    size_t              size;
    unsigned char       value[128];  /* scalar bytes, or local size          */
} cl_kernel_arg;

/* ---- kernel -------------------------------------------------------------- */
struct _cl_kernel {
    cl_int                          refcount;
    struct _cl_program             *program;
    struct _cl_context             *context;
    char                           *name;          /* owned                 */
    struct _cl_program_build       *build;         /* resolved at create    */
    void                           *exec_kernel;   /* backend kernel handle */
    cl_uint                         num_args;
    cl_kernel_arg                   args[CL_MAX_KERNEL_ARGS];
};

/* ---- sampler ------------------------------------------------------------- */
struct _cl_sampler {
    cl_int                  refcount;
    struct _cl_context     *context;
    cl_bool                 normalized_coords;
    cl_addressing_mode      addressing_mode;
    cl_filter_mode          filter_mode;
};

/* ---- event --------------------------------------------------------------- */
typedef struct cl_event_callback {
    cl_int  trigger;   /* CL_SUBMITTED / CL_RUNNING / CL_COMPLETE */
    void (*fn)(cl_event, cl_int, void *);
    void   *user_data;
} cl_event_callback;

struct _cl_event {
    cl_int                          refcount;
    struct _cl_context             *context;
    struct _cl_command_queue       *queue;   /* NULL for user events        */
    cl_command_type                 type;
    cl_int                          status;  /* CL_QUEUED/RUNNING/COMPLETE/<0 */
    /* profiling timestamps (ns since an epoch) */
    cl_ulong                        t_queued, t_submit, t_start, t_end;
    cl_event_callback               callbacks[4];
    cl_uint                         num_callbacks;
};

/* ---- shared runtime helpers (the runtime .c files) ----------------------- */

/* Retrieve the singleton platform (initialised on first use). */
struct _cl_platform_id *cl_runtime_platform(void);

/* Number of registered devices and access by index. */
cl_uint cl_runtime_num_devices(void);
struct _cl_device_id *cl_runtime_device(cl_uint i);

/* Find a device's slot in the platform list (used to index program->build). */
int cl_device_index(struct _cl_device_id *dev);

/* Argument validation helpers. */
int cl_valid_device(struct _cl_device_id *dev);
int cl_context_has_device(struct _cl_context *ctx, struct _cl_device_id *dev);

/* Event lifecycle used by the enqueue paths. */
struct _cl_event *cl_event_create(struct _cl_command_queue *queue,
                                  cl_command_type type);
void cl_event_set_status(struct _cl_event *ev, cl_int status);
int  cl_event_wait_list(cl_uint n, const cl_event *list);
void cl_event_stamp(struct _cl_event *ev, int which);

/* Copy param_value with the standard clGet*Info size/ret semantics. */
cl_int cl_get_info(const void *src, size_t src_size, size_t param_value_size,
                   void *param_value, size_t *param_value_size_ret);
/* String variant (copies NUL-terminated string, size includes terminator). */
cl_int cl_get_info_str(const char *s, size_t param_value_size,
                       void *param_value, size_t *param_value_size_ret);

/* Monotonic nanosecond clock for profiling. */
cl_ulong cl_now_ns(void);

/* Resolve a buffer arg's device-visible base pointer (host store, honoring
 * sub-buffer origin). */
void *cl_mem_device_ptr(struct _cl_mem *m);

#ifdef __cplusplus
}
#endif

#endif /* OPENCL_INTERNAL_H */
