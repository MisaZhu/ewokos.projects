/*
 * cl_platform.c - clGetPlatformIDs / clGetPlatformInfo.
 */
#include "internal.h"

cl_int clGetPlatformIDs(cl_uint num_entries, cl_platform_id *platforms,
                        cl_uint *num_platforms)
{
    struct _cl_platform_id *p;
    if ((num_entries == 0 && platforms != NULL) ||
        (platforms == NULL && num_platforms == NULL))
        return CL_INVALID_VALUE;
    p = cl_runtime_platform();
    if (platforms && num_entries >= 1)
        platforms[0] = (cl_platform_id)p;
    if (num_platforms)
        *num_platforms = 1;
    return CL_SUCCESS;
}

cl_int clGetPlatformInfo(cl_platform_id platform, cl_platform_info param_name,
                         size_t param_value_size, void *param_value,
                         size_t *param_value_size_ret)
{
    struct _cl_platform_id *p = cl_runtime_platform();
    if (platform != NULL && platform != (cl_platform_id)p)
        return CL_INVALID_PLATFORM;
    switch (param_name) {
    case CL_PLATFORM_PROFILE:
        return cl_get_info_str(p->profile, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_VERSION:
        return cl_get_info_str(p->version, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_NAME:
        return cl_get_info_str(p->name, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_VENDOR:
        return cl_get_info_str(p->vendor, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_EXTENSIONS:
        return cl_get_info_str(p->extensions, param_value_size, param_value, param_value_size_ret);
    case CL_PLATFORM_ICD_SUFFIX_KHR:
        return cl_get_info_str(p->icd_suffix, param_value_size, param_value, param_value_size_ret);
    default:
        return CL_INVALID_VALUE;
    }
}
