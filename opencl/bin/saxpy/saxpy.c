/*
 * saxpy.c - y = a*x + y.  Exercises a scalar kernel argument (the float `a`)
 * and OpenCL-C float4 vector arithmetic (vload4/vstore4 + fma), both part of the
 * documented subset.  Self-checks against a host reference.
 */
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define N 1024   /* multiple of 4 */

static const char *KERNEL_SRC =
    "__kernel void saxpy(float a,\n"
    "                    __global const float *x,\n"
    "                    __global float *y)\n"
    "{\n"
    "    int i = get_global_id(0);\n"
    "    float4 vx = vload4(i, x);\n"
    "    float4 vy = vload4(i, y);\n"
    "    vy = fma(a, vx, vy);\n"
    "    vstore4(vy, i, y);\n"
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
    cl_mem bx, by;
    float *x, *y, a = 2.5f;
    size_t gws = N / 4, lws = 16;
    int i, bad = 0;

    if (clGetPlatformIDs(1, &platform, &nplat) != CL_SUCCESS || nplat == 0) {
        printf("saxpy: no OpenCL platform\n"); return 1;
    }
    /* Prefer the GPU device (raspi5's V3D compute path); fall back to the CPU
     * device the platform always exposes when no GPU is present. */
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, &ndev) != CL_SUCCESS || ndev == 0) {
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, &ndev) != CL_SUCCESS || ndev == 0) {
            printf("saxpy: no OpenCL device\n"); return 1;
        }
    }
    {
        char dname[128];
        dname[0] = '\0';
        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dname) - 1, dname, NULL);
        printf("saxpy: device = %s\n", dname);
    }
    ctx = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    q = clCreateCommandQueue(ctx, device, 0, &err);
    prog = clCreateProgramWithSource(ctx, 1, &KERNEL_SRC, NULL, &err);
    if (clBuildProgram(prog, 1, &device, NULL, NULL, NULL) != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("saxpy: build failed:\n%s\n", log);
        return 1;
    }
    k = clCreateKernel(prog, "saxpy", &err);
    if (!k) { printf("saxpy: clCreateKernel %d\n", err); return 1; }

    x = (float *)malloc(N * sizeof(float));
    y = (float *)malloc(N * sizeof(float));
    for (i = 0; i < N; i++) { x[i] = (float)i; y[i] = (float)(N - i) * 0.25f; }

    bx = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * sizeof(float), x, &err);
    by = clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, N * sizeof(float), y, &err);

    clSetKernelArg(k, 0, sizeof(float), &a);
    clSetKernelArg(k, 1, sizeof(cl_mem), &bx);
    clSetKernelArg(k, 2, sizeof(cl_mem), &by);

    err = clEnqueueNDRangeKernel(q, k, 1, NULL, &gws, &lws, 0, NULL, NULL);
    if (err != CL_SUCCESS) { printf("saxpy: ndrange %d\n", err); return 1; }
    clFinish(q);
    clEnqueueReadBuffer(q, by, CL_TRUE, 0, N * sizeof(float), y, 0, NULL, NULL);
    clFinish(q);

    for (i = 0; i < N; i++) {
        float e = a * (float)i + (float)(N - i) * 0.25f;
        if (fabsf(y[i] - e) > 1e-2f) {
            if (bad < 5) printf("saxpy: mismatch[%d] got %f want %f\n", i, y[i], e);
            bad++;
        }
    }
    if (bad)
        printf("saxpy FAIL (%d/%d)\n", bad, N);
    else
        printf("saxpy PASS (%d elements)\n", N);

    clReleaseMemObject(bx); clReleaseMemObject(by);
    clReleaseKernel(k); clReleaseProgram(prog);
    clReleaseCommandQueue(q); clReleaseContext(ctx);
    free(x); free(y);
    return bad ? 1 : 0;
}
