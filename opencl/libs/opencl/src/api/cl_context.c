/*
 * cl_context.c - clCreateContext / clCreateContextFromType and the context
 * info + refcount entry points.
 */
#include "internal.h"
#include <stdlib.h>

#define INFO_CTX_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

static cl_uint props_count(const cl_context_properties *properties)
{
    cl_uint n = 0;
    if (!properties)
        return 0;
    while (properties[n] != 0 && n < 6)
        n += 2;
    return n;
}

cl_context clCreateContext(const cl_context_properties *properties,
                           cl_uint num_devices, const cl_device_id *devices,
                           void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                           void *user_data, cl_int *errcode_ret)
{
    struct _cl_context *ctx;
    cl_uint i;
    cl_int err = CL_SUCCESS;

    (void)pfn_notify; (void)user_data;

    if (!devices || num_devices == 0) {
        err = CL_INVALID_VALUE;
        if (errcode_ret) *errcode_ret = err;
        return NULL;
    }
    for (i = 0; i < num_devices; i++) {
        if (!cl_valid_device((struct _cl_device_id *)devices[i])) {
            err = CL_INVALID_DEVICE;
            if (errcode_ret) *errcode_ret = err;
            return NULL;
        }
    }
    if (num_devices > CL_MAX_DEVICES)
        num_devices = CL_MAX_DEVICES;

    ctx = (struct _cl_context *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY;
        return NULL;
    }
    ctx->refcount = 1;
    ctx->num_devices = num_devices;
    for (i = 0; i < num_devices; i++)
        ctx->devices[i] = (struct _cl_device_id *)devices[i];
    if (properties) {
        cl_uint n = props_count(properties);
        cl_uint k;
        for (k = 0; k < n && ctx->num_props + 2 < 8; k += 2) {
            ctx->props[ctx->num_props++] = properties[k];
            ctx->props[ctx->num_props++] = properties[k + 1];
        }
        ctx->props[ctx->num_props] = 0;
    }
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_context)ctx;
}

cl_context clCreateContextFromType(const cl_context_properties *properties,
                                   cl_device_type device_type,
                                   void (CL_CALLBACK *pfn_notify)(const char *, const void *, size_t, void *),
                                   void *user_data, cl_int *errcode_ret)
{
    cl_device_id devs[CL_MAX_DEVICES];
    cl_uint n = 0;
    cl_int r = clGetDeviceIDs(NULL, device_type, CL_MAX_DEVICES, devs, &n);
    if (r != CL_SUCCESS || n == 0) {
        if (errcode_ret) *errcode_ret = (r == CL_SUCCESS ? CL_DEVICE_NOT_FOUND : r);
        return NULL;
    }
    return clCreateContext(properties, n, devs, pfn_notify, user_data, errcode_ret);
}

cl_int clRetainContext(cl_context context)
{
    if (!context)
        return CL_INVALID_CONTEXT;
    context->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseContext(cl_context context)
{
    if (!context)
        return CL_INVALID_CONTEXT;
    if (--context->refcount <= 0)
        free(context);
    return CL_SUCCESS;
}

cl_int clGetContextInfo(cl_context context, cl_context_info param_name,
                        size_t param_value_size, void *param_value,
                        size_t *param_value_size_ret)
{
    if (!context)
        return CL_INVALID_CONTEXT;
    switch (param_name) {
    case CL_CONTEXT_REFERENCE_COUNT:
        INFO_CTX_SCALAR(cl_uint, context->refcount);
    case CL_CONTEXT_NUM_DEVICES:
        INFO_CTX_SCALAR(cl_uint, context->num_devices);
    case CL_CONTEXT_DEVICES:
        return cl_get_info(context->devices,
                           sizeof(cl_device_id) * context->num_devices,
                           param_value_size, param_value, param_value_size_ret);
    case CL_CONTEXT_PROPERTIES: {
        size_t n = (context->num_props + 1) * sizeof(cl_context_properties);
        return cl_get_info(context->props, n, param_value_size, param_value, param_value_size_ret);
    }
    case CL_CONTEXT_INTEROP_USER_SYNC:
        INFO_CTX_SCALAR(cl_bool, CL_TRUE);
    default:
        return CL_INVALID_VALUE;
    }
}
