/*
 * vector_add.c - the canonical OpenCL smoke test: c[i] = a[i] + b[i] over a
 * 1-D NDRange.  Prefers the GPU device (raspi5's V3D compute path) and falls
 * back to the CPU device the platform always exposes, then self-checks against
 * a host reference, returning non-zero on any mismatch so the build's test step
 * can gate on it.
 */
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define N 1024

static const char *KERNEL_SRC =
    "__kernel void vadd(__global const float *a,\n"
    "                   __global const float *b,\n"
    "                   __global float *c)\n"
    "{\n"
    "    int i = get_global_id(0);\n"
    "    c[i] = a[i] + b[i];\n"
    "}\n";

int main(void)
{
    cl_platform_id platform = NULL;
    cl_device_id device = NULL;
    cl_uint nplat = 0, ndev = 0;
    cl_int err = CL_SUCCESS;
    cl_context ctx;
    cl_command_queue q;
    cl_program prog;
    cl_kernel k;
    cl_mem ba, bb, bc;
    float *a, *b, *c;
    size_t gws = N, lws = 64;
    int i, bad = 0;

    if (clGetPlatformIDs(1, &platform, &nplat) != CL_SUCCESS || nplat == 0) {
        printf("vector_add: no OpenCL platform\n");
        return 1;
    }
    /* Prefer the GPU device (raspi5's V3D compute path); fall back to the CPU
     * device the platform always exposes when no GPU is present. */
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, &ndev) != CL_SUCCESS || ndev == 0) {
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, &ndev) != CL_SUCCESS || ndev == 0) {
            printf("vector_add: no OpenCL device\n");
            return 1;
        }
    }
    {
        char dname[128];
        dname[0] = '\0';
        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dname) - 1, dname, NULL);
        printf("vector_add: device = %s\n", dname);
    }

    ctx = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (!ctx) { printf("vector_add: clCreateContext %d\n", err); return 1; }
    q = clCreateCommandQueue(ctx, device, 0, &err);
    prog = clCreateProgramWithSource(ctx, 1, &KERNEL_SRC, NULL, &err);
    if (clBuildProgram(prog, 1, &device, NULL, NULL, NULL) != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("vector_add: build failed:\n%s\n", log);
        return 1;
    }
    k = clCreateKernel(prog, "vadd", &err);
    if (!k) { printf("vector_add: clCreateKernel %d\n", err); return 1; }

    a = (float *)malloc(N * sizeof(float));
    b = (float *)malloc(N * sizeof(float));
    c = (float *)malloc(N * sizeof(float));
    for (i = 0; i < N; i++) { a[i] = (float)i * 0.5f; b[i] = (float)i * 2.0f; c[i] = 0.0f; }

    ba = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * sizeof(float), a, &err);
    bb = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * sizeof(float), b, &err);
    bc = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, N * sizeof(float), NULL, &err);

    clSetKernelArg(k, 0, sizeof(cl_mem), &ba);
    clSetKernelArg(k, 1, sizeof(cl_mem), &bb);
    clSetKernelArg(k, 2, sizeof(cl_mem), &bc);

    err = clEnqueueNDRangeKernel(q, k, 1, NULL, &gws, &lws, 0, NULL, NULL);
    if (err != CL_SUCCESS) { printf("vector_add: ndrange %d\n", err); return 1; }
    clFinish(q);
    clEnqueueReadBuffer(q, bc, CL_TRUE, 0, N * sizeof(float), c, 0, NULL, NULL);
    clFinish(q);

    for (i = 0; i < N; i++) {
        float e = a[i] + b[i];
        if (fabsf(c[i] - e) > 1e-4f) {
            if (bad < 5) printf("vector_add: mismatch[%d] got %f want %f\n", i, c[i], e);
            bad++;
        }
    }
    if (bad)
        printf("vector_add FAIL (%d/%d)\n", bad, N);
    else
        printf("vector_add PASS (%d elements)\n", N);

    clReleaseMemObject(ba); clReleaseMemObject(bb); clReleaseMemObject(bc);
    clReleaseKernel(k); clReleaseProgram(prog);
    clReleaseCommandQueue(q); clReleaseContext(ctx);
    free(a); free(b); free(c);
    return bad ? 1 : 0;
}
