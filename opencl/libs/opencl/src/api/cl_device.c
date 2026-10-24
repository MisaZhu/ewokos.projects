/*
 * cl_device.c - clGetDeviceIDs / clGetDeviceInfo and the 1.2 device
 * partition/retain entry points.  clGetDeviceInfo is one large param switch;
 * the INFO_* macros keep the size/ret marshalling uniform (cl_get_info).
 */
#include "internal.h"

cl_int clGetDeviceIDs(cl_platform_id platform, cl_device_type device_type,
                      cl_uint num_entries, cl_device_id *devices,
                      cl_uint *num_devices)
{
    struct _cl_platform_id *p = cl_runtime_platform();
    cl_uint n = cl_runtime_num_devices();
    cl_uint matched = 0;
    cl_uint i;

    if (platform != NULL && platform != (cl_platform_id)p)
        return CL_INVALID_PLATFORM;
    if ((num_entries == 0 && devices != NULL) ||
        (devices == NULL && num_devices == NULL))
        return CL_INVALID_VALUE;
    if (device_type == 0 ||
        (device_type & ~(CL_DEVICE_TYPE_DEFAULT | CL_DEVICE_TYPE_CPU |
                         CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_ACCELERATOR |
                         CL_DEVICE_TYPE_CUSTOM | CL_DEVICE_TYPE_ALL)))
        return CL_INVALID_DEVICE_TYPE;

    for (i = 0; i < n; i++) {
        struct _cl_device_id *d = cl_runtime_device(i);
        int want;
        if (device_type == CL_DEVICE_TYPE_ALL)
            want = 1;
        else if (device_type & CL_DEVICE_TYPE_DEFAULT)
            want = (d->type == CL_DEVICE_TYPE_CPU); /* default device = CPU */
        else
            want = (d->type & device_type) != 0;
        if (!want)
            continue;
        if (devices && matched < num_entries)
            devices[matched] = (cl_device_id)d;
        matched++;
    }
    if (num_devices)
        *num_devices = matched;
    if (matched == 0)
        return CL_DEVICE_NOT_FOUND;
    return CL_SUCCESS;
}

#define INFO_STR(field) \
    return cl_get_info_str(d->field, param_value_size, param_value, param_value_size_ret)
#define INFO_SCALAR(ctype, field) do { \
    ctype _v = (ctype)d->field; \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)
#define INFO_CONST(ctype, val) do { \
    ctype _v = (ctype)(val); \
    return cl_get_info(&_v, sizeof(_v), param_value_size, param_value, param_value_size_ret); \
} while (0)

cl_int clGetDeviceInfo(cl_device_id device, cl_device_info param_name,
                       size_t param_value_size, void *param_value,
                       size_t *param_value_size_ret)
{
    struct _cl_device_id *d = (struct _cl_device_id *)device;
    if (!cl_valid_device(d))
        return CL_INVALID_DEVICE;

    switch (param_name) {
    case CL_DEVICE_TYPE:                    INFO_SCALAR(cl_device_type, type);
    case CL_DEVICE_VENDOR_ID:               INFO_SCALAR(cl_uint, vendor_id);
    case CL_DEVICE_MAX_COMPUTE_UNITS:       INFO_SCALAR(cl_uint, max_compute_units);
    case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:INFO_SCALAR(cl_uint, max_work_item_dimensions);
    case CL_DEVICE_MAX_WORK_GROUP_SIZE:     INFO_SCALAR(size_t, max_work_group_size);
    case CL_DEVICE_MAX_WORK_ITEM_SIZES:
        return cl_get_info(d->max_work_item_sizes, sizeof(d->max_work_item_sizes),
                           param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_MAX_CLOCK_FREQUENCY:     INFO_SCALAR(cl_uint, max_clock_frequency);
    case CL_DEVICE_ADDRESS_BITS:            INFO_SCALAR(cl_uint, address_bits);
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE:      INFO_SCALAR(cl_ulong, max_mem_alloc_size);
    case CL_DEVICE_GLOBAL_MEM_SIZE:         INFO_SCALAR(cl_ulong, global_mem_size);
    case CL_DEVICE_LOCAL_MEM_SIZE:          INFO_SCALAR(cl_ulong, local_mem_size);
    case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE:INFO_SCALAR(cl_ulong, max_constant_buffer_size);
    case CL_DEVICE_MAX_CONSTANT_ARGS:       INFO_SCALAR(cl_uint, max_constant_args);
    case CL_DEVICE_MAX_PARAMETER_SIZE:      INFO_SCALAR(size_t, max_parameter_size);
    case CL_DEVICE_MEM_BASE_ADDR_ALIGN:     INFO_SCALAR(cl_uint, mem_base_addr_align);
    case CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE:INFO_CONST(cl_uint, 128);
    case CL_DEVICE_SINGLE_FP_CONFIG:        INFO_SCALAR(cl_device_fp_config, single_fp_config);
    case CL_DEVICE_DOUBLE_FP_CONFIG:        INFO_SCALAR(cl_device_fp_config, double_fp_config);
    case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:   INFO_SCALAR(cl_device_mem_cache_type, global_mem_cache_type);
    case CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE:INFO_SCALAR(cl_uint, global_mem_cacheline_size);
    case CL_DEVICE_GLOBAL_MEM_CACHE_SIZE:   INFO_SCALAR(cl_ulong, global_mem_cache_size);
    case CL_DEVICE_LOCAL_MEM_TYPE:          INFO_SCALAR(cl_device_local_mem_type, local_mem_type);
    case CL_DEVICE_ERROR_CORRECTION_SUPPORT:INFO_CONST(cl_bool, CL_FALSE);
    case CL_DEVICE_PROFILING_TIMER_RESOLUTION:INFO_SCALAR(size_t, profiling_timer_resolution);
    case CL_DEVICE_ENDIAN_LITTLE:           INFO_SCALAR(cl_bool, endian_little);
    case CL_DEVICE_AVAILABLE:               INFO_SCALAR(cl_bool, available);
    case CL_DEVICE_COMPILER_AVAILABLE:      INFO_SCALAR(cl_bool, compiler_available);
    case CL_DEVICE_LINKER_AVAILABLE:        INFO_SCALAR(cl_bool, linker_available);
    case CL_DEVICE_EXECUTION_CAPABILITIES:  INFO_SCALAR(cl_device_exec_capabilities, exec_capabilities);
    case CL_DEVICE_QUEUE_PROPERTIES:        INFO_SCALAR(cl_command_queue_properties, queue_properties);
    case CL_DEVICE_PLATFORM: {
        cl_platform_id p = (cl_platform_id)d->platform;
        return cl_get_info(&p, sizeof(p), param_value_size, param_value, param_value_size_ret);
    }
    case CL_DEVICE_NAME:                    INFO_STR(name);
    case CL_DEVICE_VENDOR:                  INFO_STR(vendor);
    case CL_DRIVER_VERSION:                 INFO_STR(driver_version);
    case CL_DEVICE_PROFILE:                 INFO_STR(profile);
    case CL_DEVICE_VERSION:                 INFO_STR(version);
    case CL_DEVICE_OPENCL_C_VERSION:        INFO_STR(opencl_c_version);
    case CL_DEVICE_EXTENSIONS:              INFO_STR(extensions);
    case CL_DEVICE_BUILT_IN_KERNELS:        INFO_STR(built_in_kernels);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR:   INFO_CONST(cl_uint, 4);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT:  INFO_CONST(cl_uint, 4);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT:    INFO_CONST(cl_uint, 4);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG:   INFO_CONST(cl_uint, 2);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT:  INFO_CONST(cl_uint, 4);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE: INFO_CONST(cl_uint, 2);
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF:   INFO_CONST(cl_uint, 4);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR:      INFO_CONST(cl_uint, 4);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT:     INFO_CONST(cl_uint, 4);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_INT:       INFO_CONST(cl_uint, 4);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG:      INFO_CONST(cl_uint, 2);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT:     INFO_CONST(cl_uint, 4);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE:    INFO_CONST(cl_uint, 2);
    case CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF:      INFO_CONST(cl_uint, 4);
    case CL_DEVICE_MAX_READ_IMAGE_ARGS:     INFO_CONST(cl_uint, 128);
    case CL_DEVICE_MAX_WRITE_IMAGE_ARGS:    INFO_CONST(cl_uint, 8);
    case CL_DEVICE_IMAGE2D_MAX_WIDTH:       INFO_CONST(size_t, 8192);
    case CL_DEVICE_IMAGE2D_MAX_HEIGHT:      INFO_CONST(size_t, 8192);
    case CL_DEVICE_IMAGE3D_MAX_WIDTH:       INFO_CONST(size_t, 2048);
    case CL_DEVICE_IMAGE3D_MAX_HEIGHT:      INFO_CONST(size_t, 2048);
    case CL_DEVICE_IMAGE3D_MAX_DEPTH:       INFO_CONST(size_t, 2048);
    case CL_DEVICE_IMAGE_MAX_BUFFER_SIZE:   INFO_CONST(size_t, 65536);
    case CL_DEVICE_IMAGE_MAX_ARRAY_SIZE:    INFO_CONST(size_t, 256);
    case CL_DEVICE_IMAGE_SUPPORT:           INFO_SCALAR(cl_bool, image_support);
    case CL_DEVICE_MAX_SAMPLERS:            INFO_CONST(cl_uint, 16);
    case CL_DEVICE_HOST_UNIFIED_MEMORY:     INFO_SCALAR(cl_bool, host_unified_memory);
    case CL_DEVICE_PRINTF_BUFFER_SIZE:      INFO_SCALAR(size_t, printf_buffer_size);
    case CL_DEVICE_PREFERRED_INTEROP_USER_SYNC: INFO_CONST(cl_bool, CL_TRUE);
    case CL_DEVICE_PARENT_DEVICE: {
        cl_device_id none = NULL;
        return cl_get_info(&none, sizeof(none), param_value_size, param_value, param_value_size_ret);
    }
    case CL_DEVICE_PARTITION_MAX_SUB_DEVICES: INFO_CONST(cl_uint, 0);
    case CL_DEVICE_REFERENCE_COUNT:         INFO_CONST(cl_uint, 1);
    case CL_DEVICE_PARTITION_PROPERTIES:
    case CL_DEVICE_PARTITION_TYPE:
        return cl_get_info("", 0, param_value_size, param_value, param_value_size_ret);
    case CL_DEVICE_PARTITION_AFFINITY_DOMAIN: INFO_CONST(cl_device_partition_property, 0);
    default:
        return CL_INVALID_VALUE;
    }
}

cl_int clCreateSubDevices(cl_device_id in_device,
                          const cl_device_partition_property *properties,
                          cl_uint num_devices, cl_device_id *out_devices,
                          cl_uint *num_devices_ret)
{
    (void)num_devices; (void)out_devices; (void)num_devices_ret;
    if (!cl_valid_device((struct _cl_device_id *)in_device))
        return CL_INVALID_DEVICE;
    if (!properties)
        return CL_INVALID_VALUE;
    /* device fission is not supported by this runtime */
    return CL_INVALID_VALUE;
}

cl_int clRetainDevice(cl_device_id device)
{
    if (!cl_valid_device((struct _cl_device_id *)device))
        return CL_INVALID_DEVICE;
    return CL_SUCCESS; /* root devices have an immutable refcount */
}

cl_int clReleaseDevice(cl_device_id device)
{
    if (!cl_valid_device((struct _cl_device_id *)device))
        return CL_INVALID_DEVICE;
    return CL_SUCCESS;
}
