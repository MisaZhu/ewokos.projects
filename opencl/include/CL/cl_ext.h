/*
 * cl_ext.h - vendor/KHR extension tokens and prototypes for OpenCL 1.2.
 * Only the extensions the EwokOS runtime advertises (or recognises for source
 * compatibility) are declared; unknown extensions simply resolve to no
 * function pointer via clGetExtensionFunctionAddressForPlatform.
 */
#ifndef __OPENCL_CL_EXT_H
#define __OPENCL_CL_EXT_H

#include <CL/cl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* cl_khr_icd */
#define CL_PLATFORM_ICD_SUFFIX_KHR          0x0920
#define CL_PLATFORM_NOT_FOUND_KHR           -1001
typedef cl_uint cl_platform_icd_suffix;

/* cl_khr_fp64 / cl_khr_fp16 */
#define CL_DEVICE_HALF_FP_CONFIG            0x1033

/* cl_khr_int64_base_atomics / extended_atomics */
#define CL_DEVICE_MAX_ATOMIC_ORDER          0x10AA

/* cl_khr_byte_addressable_store already core in 1.1+ */

/* cl_khr_global_int32_base_atomics extension function */
cl_int CL_API_ENTRY clIcdGetPlatformIDsKHR(cl_uint num_entries,
                                           cl_platform_id *platforms,
                                           cl_uint *num_platforms);

#ifdef __cplusplus
}
#endif

#endif /* __OPENCL_CL_EXT_H */
