/*
 * cl_sampler.c - sampler objects used by the image read/write builtins on the
 * CPU device.  A sampler records the coordinate-normalisation, addressing and
 * filtering modes; the CPU image builtins consult it when sampling.
 */
#include "internal.h"
#include <stdlib.h>

#define INFO_S_SCALAR(ctype, expr) do { \
    ctype _v = (ctype)(expr); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_sampler clCreateSampler(cl_context context, cl_bool normalized_coords,
                           cl_addressing_mode addressing_mode, cl_filter_mode filter_mode,
                           cl_int *errcode_ret)
{
    struct _cl_sampler *s;
    if (!context) { if (errcode_ret) *errcode_ret = CL_INVALID_CONTEXT; return NULL; }
    switch (addressing_mode) {
    case CL_ADDRESS_NONE: case CL_ADDRESS_CLAMP_TO_EDGE: case CL_ADDRESS_CLAMP:
    case CL_ADDRESS_REPEAT: case CL_ADDRESS_MIRRORED_REPEAT:
        break;
    default:
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }
    if (filter_mode != CL_FILTER_NEAREST && filter_mode != CL_FILTER_LINEAR) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }
    /* REPEAT / MIRRORED_REPEAT require normalised coordinates */
    if ((addressing_mode == CL_ADDRESS_REPEAT || addressing_mode == CL_ADDRESS_MIRRORED_REPEAT)
        && normalized_coords != CL_TRUE) {
        if (errcode_ret) *errcode_ret = CL_INVALID_VALUE;
        return NULL;
    }

    s = (struct _cl_sampler *)calloc(1, sizeof(*s));
    if (!s) { if (errcode_ret) *errcode_ret = CL_OUT_OF_HOST_MEMORY; return NULL; }
    s->refcount = 1;
    s->context = context;
    s->normalized_coords = normalized_coords;
    s->addressing_mode = addressing_mode;
    s->filter_mode = filter_mode;
    clRetainContext(context);
    if (errcode_ret) *errcode_ret = CL_SUCCESS;
    return (cl_sampler)s;
}

cl_int clRetainSampler(cl_sampler sampler)
{
    if (!sampler) return CL_INVALID_SAMPLER;
    sampler->refcount++;
    return CL_SUCCESS;
}

cl_int clReleaseSampler(cl_sampler sampler)
{
    if (!sampler) return CL_INVALID_SAMPLER;
    if (--sampler->refcount <= 0) {
        clReleaseContext(sampler->context);
        free(sampler);
    }
    return CL_SUCCESS;
}

cl_int clGetSamplerInfo(cl_sampler sampler, cl_sampler_info param_name,
                        size_t param_value_size, void *param_value,
                        size_t *param_value_size_ret)
{
    struct _cl_sampler *s = (struct _cl_sampler *)sampler;
    if (!s) return CL_INVALID_SAMPLER;
    switch (param_name) {
    case CL_SAMPLER_REFERENCE_COUNT:
        INFO_S_SCALAR(cl_uint, s->refcount);
    case CL_SAMPLER_CONTEXT: {
        cl_context c = (cl_context)s->context;
        return cl_get_info(&c, sizeof(c), param_value_size, param_value, param_value_size_ret);
    }
    case CL_SAMPLER_NORMALIZED_COORDS:
        INFO_S_SCALAR(cl_bool, s->normalized_coords);
    case CL_SAMPLER_ADDRESSING_MODE:
        INFO_S_SCALAR(cl_addressing_mode, s->addressing_mode);
    case CL_SAMPLER_FILTER_MODE:
        INFO_S_SCALAR(cl_filter_mode, s->filter_mode);
    default:
        return CL_INVALID_VALUE;
    }
}
