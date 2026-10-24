/*
 * cl_program.c - program object lifecycle and clBuildProgram.  The runtime
 * stores the concatenated source and, on build, asks each target device's
 * backend to compile it (CPU: parse to the OpenCL-C AST the interpreter runs;
 * GPU: transpile + compile to a device blob).  Per-device build status, log
 * and binary type are recorded so clGetProgramBuildInfo reports them even for
 * a failed build.
 */
#include "internal.h"
#include <stdlib.h>
#include <stdio.h>

static struct _cl_program_build *find_build(struct _cl_program *p,
                                            struct _cl_device_id *dev)
{
    cl_uint i;
    for (i = 0; i < p->num_devices; i++)
        if (p->build[i].device == dev)
            return &p->build[i];
    return NULL;
}

static void free_builds(struct _cl_program *p)
{
    cl_uint i;
    for (i = 0; i < p->num_devices; i++) {
        if (p->build[i].exec && p->build[i].device && p->build[i].device->backend)
            p->build[i].device->backend->free_exec(p->build[i].exec);
        p->build[i].exec = NULL;
    }
}

cl_program clCreateProgramWithSource(cl_context context, cl_uint count,
                                     const char **strings, const size_t *lengths,
                                     cl_int *errcode_ret)
{
    struct _cl_program *p;
    size_t total = 0;
    cl_uint i, di;
    char *src;
    size_t off = 0;

    if (!context) { if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT; return NULL; }
    if (count == 0 || !strings) { if (errcode_ret) *errcode_ret = CL_INVALID_VALUE; return NULL; }
    for (i = 0; i < count; i++) {
        if (!strings[i]) { if (errcode_ret) *errcode_ret = CL_INVALID_VALUE; return NULL; }
        total += (lengths && lengths[i]) ? lengths[i] : strlen(strings[i]);
    }

    src = (char *)malloc(total + 1);
    if (!src) { if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY; return NULL; }
    for (i = 0; i < count; i++) {
        size_t n = (lengths && lengths[i]) ? lengths[i] : strlen(strings[i]);
        memcpy(src + off, strings[i], n);
        off += n;
    }
    src[off] = '\0';

    p = (struct _cl_program *)calloc(1, sizeof(*p));
    if (!p) { free(src); if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY; return NULL; }
    p->refcount = 1;
    p->context = context;
    p->source = src;
    p->num_devices = context->num_devices;
    for (di = 0; di < context->num_devices && di < CL_MAX_PROGRAM_DEVICES; di++) {
        p->devices[di] = context->devices[di];
        p->build[di].device = context->devices[di];
        p->build[di].status = CL_BUILD_NONE;
        p->build[di].binary_type = CL_PROGRAM_BINARY_TYPE_NONE;
    }
    p->num_devices = di;
    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_program)p;
}

cl_program clCreateProgramWithBinary(cl_context context, cl_uint num_devices,
                                     const cl_device_id *device_list, const size_t *lengths,
                                     const unsigned char **binaries, cl_int *binary_status,
                                     cl_int *errcode_ret)
{
    (void)lengths; (void)binaries;
    if (binary_status) {
        cl_uint i;
        for (i = 0; i < num_devices; i++)
            binary_status[i] = CL_INVALID_VALUE;
    }
    if (errcode_ret) *errcode_ret = CL_INVALID_BINARY;
    (void)context; (void)device_list;
    return NULL; /* no binary (SPIR/ISA) ingestion in this runtime */
}

cl_program clCreateProgramWithBuiltInKernels(cl_context context, cl_uint num_devices,
                                             const cl_device_id *device_list,
                                             const char *kernel_names, cl_int *errcode_ret)
{
    (void)context; (void)num_devices; (void)device_list; (void)kernel_names;
    if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
    return NULL; /* no built-in kernels */
}

cl_int clRetainProgram(cl_program program)
{
    if (!program) return CL_INVALID_PROGRAM;
    program->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseProgram(cl_program program)
{
    if (!program) return CL_INVALID_PROGRAM;
    if (--program->refcount <= 0) {
        free_builds(program);
        free(program->source);
        free(program->kernel_names);
        clReleaseContext(program->context);
        free(program);
    }
    return CL_SUCCESS;
}

cl_int clBuildProgram(cl_program program, cl_uint num_devices,
                      const cl_device_id *device_list, const char *options,
                      void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data)
{
    struct _cl_program *p = (struct _cl_program *)program;
    cl_int overall = CL_SUCCESS;
    cl_uint i;

    if (!p) return CL_INVALID_PROGRAM;
    if ((device_list == NULL && num_devices != 0) ||
        (device_list != NULL && num_devices == 0))
        return CL_INVALID_VALUE;

    /* a rebuild frees any previous exec handles */
    free_builds(p);

    if (device_list) {
        for (i = 0; i < num_devices; i++) {
            struct _cl_device_id *dev = (struct _cl_device_id *)device_list[i];
            struct _cl_program_build *b = find_build(p, dev);
            if (!b) { overall = CL_INVALID_DEVICE; continue; }
            b->status = CL_BUILD_IN_PROGRESS;
            snprintf(b->options, sizeof(b->options), "%s", options ? options : "");
            b->log[0] = '\0';
            if (dev->backend->build(p, dev, p->source, options, &b->status,
                                    b->log, sizeof(b->log)) != 0 ||
                b->status != CL_BUILD_SUCCESS) {
                b->status = CL_BUILD_ERROR;
                overall = CL_BUILD_PROGRAM_FAILURE;
            }
        }
    } else {
        for (i = 0; i < p->num_devices; i++) {
            struct _cl_device_id *dev = p->devices[i];
            struct _cl_program_build *b = &p->build[i];
            b->status = CL_BUILD_IN_PROGRESS;
            snprintf(b->options, sizeof(b->options), "%s", options ? options : "");
            b->log[0] = '\0';
            if (dev->backend->build(p, dev, p->source, options, &b->status,
                                    b->log, sizeof(b->log)) != 0 ||
                b->status != CL_BUILD_SUCCESS) {
                b->status = CL_BUILD_ERROR;
                overall = CL_BUILD_PROGRAM_FAILURE;
            }
        }
    }

    if (pfn_notify)
        pfn_notify((cl_program)p, user_data);
    return overall;
}

cl_int clCompileProgram(cl_program program, cl_uint num_devices, const cl_device_id *device_list,
                        const char *options, cl_uint num_input_headers, const cl_program *input_headers,
                        const char **header_include_names, void (CL_CALLBACK *pfn_notify)(cl_program, void *),
                        void *user_data)
{
    (void)num_input_headers; (void)input_headers; (void)header_include_names;
    /* separate compilation is folded into a full build in this runtime */
    return clBuildProgram(program, num_devices, device_list, options, pfn_notify, user_data);
}

cl_program clLinkProgram(cl_context context, cl_uint num_devices, const cl_device_id *device_list,
                         const char *options, cl_uint num_input_programs, const cl_program *input_programs,
                         void (CL_CALLBACK *pfn_notify)(cl_program, void *), void *user_data,
                         cl_int *errcode_ret)
{
    (void)context; (void)num_devices; (void)device_list; (void)options;
    (void)num_input_programs; (void)input_programs; (void)pfn_notify; (void)user_data;
    if (errcode_ret) *errcode_ret = CL_LINK_PROGRAM_FAILURE;
    return NULL; /* no separate-compilation/link step */
}

cl_int clUnloadCompiler(void) { return CL_SUCCESS; }
cl_int clUnloadPlatformCompiler(cl_platform_id platform) { (void)platform; return CL_SUCCESS; }

#define INFO_P_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_int clGetProgramInfo(cl_program program, cl_program_info param_name,
                        size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    struct _cl_program *p = (struct _cl_program *)program;
    if (!p) return CL_INVALID_PROGRAM;
    switch (param_name) {
    case CL_PROGRAM_REFERENCE_COUNT: INFO_P_SCALAR(cl_uint, p->refcount);
    case CL_PROGRAM_NUM_DEVICES:     INFO_P_SCALAR(cl_uint, p->num_devices);
    case CL_PROGRAM_CONTEXT: {
        cl_context c = (cl_context)p->context;
        return cl_get_info(&c, sizeof(c), param_value_size, param_value, param_value_size_ret);
    }
    case CL_PROGRAM_DEVICES:
        return cl_get_info(p->devices, sizeof(cl_device_id) * p->num_devices,
                           param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_SOURCE:
        return cl_get_info_str(p->source ? p->source : "", param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_BINARY_SIZES: {
        size_t sizes[CL_MAX_PROGRAM_DEVICES];
        cl_uint i;
        for (i = 0; i < p->num_devices; i++) sizes[i] = 0;
        return cl_get_info(sizes, sizeof(size_t) * p->num_devices, param_value_size, param_value, param_value_size_ret);
    }
    case CL_PROGRAM_BINARIES:
        return CL_SUCCESS; /* caller-provided slots left untouched (no binaries) */
    case CL_PROGRAM_KERNEL_NAMES:
        return cl_get_info_str(p->kernel_names ? p->kernel_names : "", param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_NUM_KERNELS: {
        cl_uint n = 0;
        if (p->kernel_names && p->kernel_names[0]) {
            const char *s = p->kernel_names;
            n = 1;
            for (; *s; s++) if (*s == ';') n++;
        }
        INFO_P_SCALAR(size_t, n);
    }
    default:
        return CL_INVALID_VALUE;
    }
}

cl_int clGetProgramBuildInfo(cl_program program, cl_device_id device, cl_program_build_info param_name,
                             size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    struct _cl_program *p = (struct _cl_program *)program;
    struct _cl_program_build *b;
    if (!p) return CL_INVALID_PROGRAM;
    b = find_build(p, (struct _cl_device_id *)device);
    if (!b) return CL_INVALID_DEVICE;
    switch (param_name) {
    case CL_PROGRAM_BUILD_STATUS:     INFO_P_SCALAR(cl_build_status, b->status);
    case CL_PROGRAM_BINARY_TYPE:      INFO_P_SCALAR(cl_program_binary_type, b->binary_type);
    case CL_PROGRAM_BUILD_OPTIONS:    return cl_get_info_str(b->options, param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_BUILD_LOG:        return cl_get_info_str(b->log, param_value_size, param_value, param_value_size_ret);
    case CL_PROGRAM_BUILD_GLOBAL_VARIABLE_TOTAL_SIZE: INFO_P_SCALAR(size_t, 0);
    default:
        return CL_INVALID_VALUE;
    }
}
