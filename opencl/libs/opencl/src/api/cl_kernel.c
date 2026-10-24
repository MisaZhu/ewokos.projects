/*
 * cl_kernel.c - kernel object lifecycle and argument binding.  A kernel object
 * is created against a successfully-built program for one device; the backend's
 * prepare_kernel() resolves the named entry point and the argument count.
 * clSetKernelArg records each argument (buffer/image handle, __local size, or
 * scalar bytes) which the backend reads back at dispatch time.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static struct _cl_program_build *first_success_build(struct _cl_program *p)
{
    cl_uint i;
    for (i = 0; i < p->num_devices; i++)
        if (p->build[i].status == CL_BUILD_SUCCESS && p->build[i].exec)
            return &p->build[i];
    return NULL;
}

cl_kernel clCreateKernel(cl_program program, const char *kernel_name, cl_int *errcode_ret)
{
    struct _cl_program *p = (struct _cl_program *)program;
    struct _cl_kernel *k;
    struct _cl_program_build *b;
    cl_int rc;

    if (!p) { if (errcode_ret) *errcode_ret = CL_INVALID_PROGRAM; return NULL; }
    if (!kernel_name) { if (errcode_ret) *errcode_ret = CL_INVALID_VALUE; return NULL; }

    b = first_success_build(p);
    if (!b) { if (errcode_ret) *errcode_ret = CL_INVALID_PROGRAM_EXECUTABLE; return NULL; }

    k = (struct _cl_kernel *)calloc(1, sizeof(*k));
    if (!k) { if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY; return NULL; }
    k->refcount = 1;
    k->program = p;
    k->context = p->context;
    k->build = b;
    k->name = (char *)malloc(strlen(kernel_name) + 1);
    if (!k->name) { free(k); if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY; return NULL; }
    strcpy(k->name, kernel_name);
    clRetainProgram((cl_program)p);
    clRetainContext(p->context);

    rc = b->device->backend->prepare_kernel(k);
    if (rc != CL_SUCCESS) {
        free(k->name);
        clReleaseContext(p->context);
        clReleaseProgram((cl_program)p);
        free(k);
        if (errcode_ret) *errcode_ret = (rc == CL_INVALID_KERNEL_NAME) ? CL_INVALID_KERNEL_NAME : rc;
        return NULL;
    }
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_kernel)k;
}

cl_int clCreateKernelsInProgram(cl_program program, cl_uint num_kernels,
                                cl_kernel *kernels, cl_uint *num_kernels_ret)
{
    struct _cl_program *p = (struct _cl_program *)program;
    cl_uint total = 0, i = 0;
    char *dup, *save;

    if (!p) return CL_INVALID_PROGRAM;
    if (!first_success_build(p)) return CL_INVALID_PROGRAM_EXECUTABLE;
    if (!p->kernel_names || !p->kernel_names[0]) {
        if (num_kernels_ret) *num_kernels_ret = 0;
        return CL_SUCCESS;
    }

    /* count names */
    total = 1;
    { const char *s = p->kernel_names; for (; *s; s++) if (*s == ';') total++; }

    if (num_kernels && num_kernels != total) return CL_INVALID_VALUE;
    if (num_kernels_ret) *num_kernels_ret = total;
    if (!kernels) return CL_SUCCESS;

    dup = (char *)malloc(strlen(p->kernel_names) + 1);
    if (!dup) return CL_OUT_OF_HOST_MEMORY;
    strcpy(dup, p->kernel_names);
    save = dup;
    while (*save && i < total) {
        char *semi = strchr(save, ';');
        if (semi) *semi = '\0';
        if (save[0]) {
            kernels[i] = clCreateKernel(program, save, NULL);
            if (!kernels[i]) { free(dup); return CL_INVALID_KERNEL_NAME; }
            i++;
        }
        if (!semi) break;
        save = semi + 1;
    }
    free(dup);
    return CL_SUCCESS;
}

cl_int clRetainKernel(cl_kernel kernel)
{
    if (!kernel) return CL_INVALID_KERNEL;
    kernel->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseKernel(cl_kernel kernel)
{
    if (!kernel) return CL_INVALID_KERNEL;
    if (--kernel->refcount <= 0) {
        if (kernel->build && kernel->build->device && kernel->build->device->backend)
            kernel->build->device->backend->release_kernel(kernel);
        free(kernel->name);
        clReleaseContext(kernel->context);
        clReleaseProgram((cl_program)kernel->program);
        free(kernel);
    }
    return CL_SUCCESS;
}

cl_int clSetKernelArg(cl_kernel kernel, cl_uint arg_index, size_t arg_size,
                      const void *arg_value)
{
    struct _cl_kernel *k = (struct _cl_kernel *)kernel;
    cl_kernel_arg *a;

    if (!k) return CL_INVALID_KERNEL;
    if (arg_index >= k->num_args || arg_index >= CL_MAX_KERNEL_ARGS)
        return CL_INVALID_ARG_INDEX;

    a = &k->args[arg_index];
    memset(a, 0, sizeof(*a));

    /* __local: arg_value is NULL, arg_size is the per-work-group byte count */
    if (arg_value == NULL) {
        if (arg_size == 0) return CL_INVALID_ARG_SIZE;
        a->is_local = 1;
        a->size = arg_size;
        a->set = 1;
        return CL_SUCCESS;
    }

    /* buffer/image/sampler: arg_value points to a cl_mem (pointer-sized) */
    if (arg_size == sizeof(cl_mem)) {
        cl_mem m = *(const cl_mem *)arg_value;
        if (m == NULL) {
            /* a NULL buffer argument is legal; bind as a null mem */
            a->is_mem = 1;
            a->mem = NULL;
            a->size = sizeof(cl_mem);
            a->set = 1;
            return CL_SUCCESS;
        }
        a->is_mem = 1;
        a->mem = (struct _cl_mem *)m;
        a->size = sizeof(cl_mem);
        a->set = 1;
        return CL_SUCCESS;
    }

    /* scalar passed by value */
    if (arg_size == 0 || arg_size > sizeof(a->value))
        return CL_INVALID_ARG_SIZE;
    memcpy(a->value, arg_value, arg_size);
    a->size = arg_size;
    a->is_mem = 0;
    a->is_local = 0;
    a->set = 1;
    return CL_SUCCESS;
}

#define INFO_K_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_int clGetKernelInfo(cl_kernel kernel, cl_kernel_info param_name,
                       size_t param_value_size, void *param_value,
                       size_t *param_value_size_ret)
{
    struct _cl_kernel *k = (struct _cl_kernel *)kernel;
    if (!k) return CL_INVALID_KERNEL;
    switch (param_name) {
    case CL_KERNEL_FUNCTION_NAME:
        return cl_get_info_str(k->name, param_value_size, param_value, param_value_size_ret);
    case CL_KERNEL_NUM_ARGS:
        INFO_K_SCALAR(cl_uint, k->num_args);
    case CL_KERNEL_REFERENCE_COUNT:
        INFO_K_SCALAR(cl_uint, k->refcount);
    case CL_KERNEL_CONTEXT: {
        cl_context c = (cl_context)k->context;
        return cl_get_info(&c, sizeof(c), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_PROGRAM: {
        cl_program p = (cl_program)k->program;
        return cl_get_info(&p, sizeof(p), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_ATTRIBUTES:
        return cl_get_info_str("", param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}

cl_int clGetKernelArgInfo(cl_kernel kernel, cl_uint arg_indx, cl_kernel_arg_info param_name,
                          size_t param_value_size, void *param_value, size_t *param_value_size_ret)
{
    (void)kernel; (void)arg_indx; (void)param_name;
    (void)param_value_size; (void)param_value; (void)param_value_size_ret;
    /* the subset compiler was not invoked with -cl-kernel-arg-info */
    return CL_KERNEL_ARG_INFO_NOT_AVAILABLE;
}

cl_int clGetKernelWorkGroupInfo(cl_kernel kernel, cl_device_id device,
                                cl_kernel_work_group_info param_name,
                                size_t param_value_size, void *param_value,
                                size_t *param_value_size_ret)
{
    struct _cl_kernel *k = (struct _cl_kernel *)kernel;
    struct _cl_device_id *dev = (struct _cl_device_id *)device;
    if (!k) return CL_INVALID_KERNEL;
    if (!dev) dev = k->build ? k->build->device : NULL;
    if (!dev) return CL_INVALID_DEVICE;

    switch (param_name) {
    case CL_KERNEL_WORK_GROUP_SIZE:
        INFO_K_SCALAR(size_t, dev->max_work_group_size);
    case CL_KERNEL_COMPILE_WORK_GROUP_SIZE: {
        size_t wg[3] = { 0, 0, 0 };
        return cl_get_info(wg, sizeof(wg), param_value_size, param_value, param_value_size_ret);
    }
    case CL_KERNEL_LOCAL_MEM_SIZE:
        INFO_K_SCALAR(cl_ulong, 0);
    case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
        INFO_K_SCALAR(size_t, 1);
    case CL_KERNEL_PRIVATE_MEM_SIZE:
        INFO_K_SCALAR(cl_ulong, 0);
    case CL_KERNEL_GLOBAL_WORK_SIZE: {
        size_t gw[3] = { dev->max_work_item_sizes[0], 1, 1 };
        return cl_get_info(gw, sizeof(gw), param_value_size, param_value, param_value_size_ret);
    }
    default:
        return CL_INVALID_VALUE;
    }
}
