/*
 * matmul.c - C = A * B for square NxN single-precision matrices, using a 2-D
 * NDRange (one work-item per output element).  Exercises get_global_id(0/1),
 * a scalar int kernel argument, an indexed for-loop and float accumulation --
 * all part of the documented OpenCL-C subset.  Self-checks against a host
 * triple-loop reference and returns non-zero on any mismatch.
 */
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define N 64

static const char *KERNEL_SRC =
    "__kernel void matmul(__global const float *A,\n"
    "                     __global const float *B,\n"
    "                     __global float *C,\n"
    "                     int n)\n"
    "{\n"
    "    int col = get_global_id(0);\n"
    "    int row = get_global_id(1);\n"
    "    if (row < n && col < n) {\n"
    "        float sum = 0.0f;\n"
    "        for (int k = 0; k < n; k++) {\n"
    "            sum += A[row * n + k] * B[k * n + col];\n"
    "        }\n"
    "        C[row * n + col] = sum;\n"
    "    }\n"
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
    cl_mem bA, bB, bC;
    float *A, *B, *C;
    size_t gws[2] = { N, N }, lws[2] = { 8, 8 };
    int n = N;
    int i, j, kk, bad = 0;

    if (clGetPlatformIDs(1, &platform, &nplat) != CL_SUCCESS || nplat == 0) {
        printf("matmul: no OpenCL platform\n"); return 1;
    }
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, &ndev) != CL_SUCCESS || ndev == 0) {
        printf("matmul: no OpenCL device\n"); return 1;
    }

    ctx = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (!ctx) { printf("matmul: clCreateContext %d\n", err); return 1; }
    q = clCreateCommandQueue(ctx, device, 0, &err);
    prog = clCreateProgramWithSource(ctx, 1, &KERNEL_SRC, NULL, &err);
    if (clBuildProgram(prog, 1, &device, NULL, NULL, NULL) != CL_SUCCESS) {
        char log[4096];
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("matmul: build failed:\n%s\n", log);
        return 1;
    }
    k = clCreateKernel(prog, "matmul", &err);
    if (!k) { printf("matmul: clCreateKernel %d\n", err); return 1; }

    A = (float *)malloc(N * N * sizeof(float));
    B = (float *)malloc(N * N * sizeof(float));
    C = (float *)malloc(N * N * sizeof(float));
    for (i = 0; i < N * N; i++) {
        A[i] = (float)((i * 7) % 13) * 0.5f - 3.0f;
        B[i] = (float)((i * 5) % 11) * 0.25f - 1.0f;
        C[i] = 0.0f;
    }

    bA = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * N * sizeof(float), A, &err);
    bB = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * N * sizeof(float), B, &err);
    bC = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, N * N * sizeof(float), NULL, &err);

    clSetKernelArg(k, 0, sizeof(cl_mem), &bA);
    clSetKernelArg(k, 1, sizeof(cl_mem), &bB);
    clSetKernelArg(k, 2, sizeof(cl_mem), &bC);
    clSetKernelArg(k, 3, sizeof(int), &n);

    err = clEnqueueNDRangeKernel(q, k, 2, NULL, gws, lws, 0, NULL, NULL);
    if (err != CL_SUCCESS) { printf("matmul: ndrange %d\n", err); return 1; }
    clFinish(q);
    clEnqueueReadBuffer(q, bC, CL_TRUE, 0, N * N * sizeof(float), C, 0, NULL, NULL);
    clFinish(q);

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            float e = 0.0f;
            for (kk = 0; kk < N; kk++) e += A[i * N + kk] * B[kk * N + j];
            if (fabsf(C[i * N + j] - e) > 1e-2f) {
                if (bad < 5)
                    printf("matmul: mismatch[%d,%d] got %f want %f\n", i, j, C[i * N + j], e);
                bad++;
            }
        }
    }
    if (bad)
        printf("matmul FAIL (%d/%d)\n", bad, N * N);
    else
        printf("matmul PASS (%dx%d)\n", N, N);

    clReleaseMemObject(bA); clReleaseMemObject(bB); clReleaseMemObject(bC);
    clReleaseKernel(k); clReleaseProgram(prog);
    clReleaseCommandQueue(q); clReleaseContext(ctx);
    free(A); free(B); free(C);
    return bad ? 1 : 0;
}
