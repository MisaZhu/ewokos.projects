/*
 * clinfo.c - a compact clinfo: enumerates every platform and device exposed by
 * libOpenCL and prints the subset of 1.2 platform/device queries that matter
 * for confirming the runtime's advertised capabilities (CPU everywhere; GPU on
 * raspi5 once Phase 2 lands).  Read-only: creates no context, compiles nothing.
 */
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *type_name(cl_device_type t)
{
    switch (t) {
    case CL_DEVICE_TYPE_CPU:         return "CPU";
    case CL_DEVICE_TYPE_GPU:         return "GPU";
    case CL_DEVICE_TYPE_ACCELERATOR: return "ACCELERATOR";
    case CL_DEVICE_TYPE_CUSTOM:      return "CUSTOM";
    default:                         return "OTHER";
    }
}

static void dump_device(cl_device_id d)
{
    cl_device_type type = 0;
    cl_uint u = 0;
    size_t s = 0;
    cl_ulong ul = 0;
    cl_bool b = CL_FALSE;
    size_t wis[3] = { 0, 0, 0 };

    clGetDeviceInfo(d, CL_DEVICE_TYPE, sizeof(type), &type, NULL);
    printf("  Device Type                    %s\n", type_name(type));

    if (clGetDeviceInfo(d, CL_DEVICE_NAME, 0, NULL, &s) == CL_SUCCESS && s > 0) {
        char *nm = (char *)malloc(s);
        if (nm) {
            clGetDeviceInfo(d, CL_DEVICE_NAME, s, nm, NULL);
            printf("  %-32s %s\n", "Device Name", nm);
            free(nm);
        }
    }
    if (clGetDeviceInfo(d, CL_DEVICE_VENDOR, 0, NULL, &s) == CL_SUCCESS && s > 0) {
        char *vd = (char *)malloc(s);
        if (vd) {
            clGetDeviceInfo(d, CL_DEVICE_VENDOR, s, vd, NULL);
            printf("  %-32s %s\n", "Device Vendor", vd);
            free(vd);
        }
    }
    if (clGetDeviceInfo(d, CL_DEVICE_VERSION, 0, NULL, &s) == CL_SUCCESS && s > 0) {
        char *vv = (char *)malloc(s);
        if (vv) {
            clGetDeviceInfo(d, CL_DEVICE_VERSION, s, vv, NULL);
            printf("  %-32s %s\n", "Device Version", vv);
            free(vv);
        }
    }
    if (clGetDeviceInfo(d, CL_DEVICE_OPENCL_C_VERSION, 0, NULL, &s) == CL_SUCCESS && s > 0) {
        char *cv = (char *)malloc(s);
        if (cv) {
            clGetDeviceInfo(d, CL_DEVICE_OPENCL_C_VERSION, s, cv, NULL);
            printf("  %-32s %s\n", "OpenCL C Version", cv);
            free(cv);
        }
    }

    clGetDeviceInfo(d, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(u), &u, NULL);
    printf("  %-32s %u\n", "Max compute units", u);
    clGetDeviceInfo(d, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(u), &u, NULL);
    printf("  %-32s %u\n", "Max work-item dimensions", u);
    if (clGetDeviceInfo(d, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(wis), wis, NULL) == CL_SUCCESS)
        printf("  %-32s %zu x %zu x %zu\n", "Max work-item sizes", wis[0], wis[1], wis[2]);
    clGetDeviceInfo(d, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(s), &s, NULL);
    printf("  %-32s %zu\n", "Max work-group size", s);
    clGetDeviceInfo(d, CL_DEVICE_ADDRESS_BITS, sizeof(u), &u, NULL);
    printf("  %-32s %u\n", "Address bits", u);
    clGetDeviceInfo(d, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(ul), &ul, NULL);
    printf("  %-32s %llu\n", "Max mem alloc size", (unsigned long long)ul);
    clGetDeviceInfo(d, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(ul), &ul, NULL);
    printf("  %-32s %llu\n", "Global mem size", (unsigned long long)ul);
    clGetDeviceInfo(d, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(ul), &ul, NULL);
    printf("  %-32s %llu\n", "Local mem size", (unsigned long long)ul);
    clGetDeviceInfo(d, CL_DEVICE_MAX_CONSTANT_ARGS, sizeof(u), &u, NULL);
    printf("  %-32s %u\n", "Max constant args", u);
    clGetDeviceInfo(d, CL_DEVICE_IMAGE_SUPPORT, sizeof(b), &b, NULL);
    printf("  %-32s %s\n", "Image support", b ? "yes" : "no");
    clGetDeviceInfo(d, CL_DEVICE_COMPILER_AVAILABLE, sizeof(b), &b, NULL);
    printf("  %-32s %s\n", "Compiler available", b ? "yes" : "no");
    clGetDeviceInfo(d, CL_DEVICE_ENDIAN_LITTLE, sizeof(b), &b, NULL);
    printf("  %-32s %s\n", "Little endian", b ? "yes" : "no");
    clGetDeviceInfo(d, CL_DEVICE_AVAILABLE, sizeof(b), &b, NULL);
    printf("  %-32s %s\n", "Device available", b ? "yes" : "no");
}

int main(void)
{
    cl_platform_id platforms[4];
    cl_uint nplat = 0, i, j;

    if (clGetPlatformIDs(4, platforms, &nplat) != CL_SUCCESS || nplat == 0) {
        printf("clinfo: no OpenCL platforms found\n");
        return 1;
    }
    printf("Number of platforms: %u\n\n", nplat);

    for (i = 0; i < nplat; i++) {
        cl_device_id devices[8];
        cl_uint ndev = 0;
        size_t s = 0;
        char buf[512];

        printf("Platform %u\n", i);
        if (clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(buf), buf, &s) == CL_SUCCESS)
            printf("  %-32s %s\n", "Platform Name", buf);
        if (clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(buf), buf, &s) == CL_SUCCESS)
            printf("  %-32s %s\n", "Platform Vendor", buf);
        if (clGetPlatformInfo(platforms[i], CL_PLATFORM_VERSION, sizeof(buf), buf, &s) == CL_SUCCESS)
            printf("  %-32s %s\n", "Platform Version", buf);
        if (clGetPlatformInfo(platforms[i], CL_PLATFORM_PROFILE, sizeof(buf), buf, &s) == CL_SUCCESS)
            printf("  %-32s %s\n", "Platform Profile", buf);

        if (clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 8, devices, &ndev) != CL_SUCCESS || ndev == 0) {
            printf("  (no devices)\n\n");
            continue;
        }
        printf("  Number of devices: %u\n", ndev);
        for (j = 0; j < ndev; j++) {
            printf("  Device %u:\n", j);
            dump_device(devices[j]);
        }
        printf("\n");
    }
    return 0;
}
