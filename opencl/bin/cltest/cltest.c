/*
 * cltest.c - OpenCL interface test + CPU-vs-GPU speed comparison.  Enumerates
 * every device the platform registers (always the CPU interpreter device, plus
 * the GPU device on machines whose compute probe succeeds, e.g. raspi5's V3D
 * CSD), then runs three kernels from the documented OpenCL-C subset on EACH
 * device with correctness self-checks and timed repeat launches:
 *
 *   saxpy   y = a*x + y        (1-D NDRange, float4 vload/vstore + fma,
 *                               memory-bandwidth bound)
 *   matmul  C = A * B          (2-D NDRange, scalar arg + indexed loop,
 *                               compute bound)
 *   math    transcendental mix (sqrt/sin/cos/fma inner loop, ALU bound)
 *
 * Timings cover clEnqueueNDRangeKernel + clFinish only (the result read-back
 * happens once afterwards for verification), so the printed per-launch cost is
 * the device's steady-state dispatch cost on this platform: for the CPU device
 * that is the interpreter run, for the GPU device that includes the daemon
 * round-trip.  Results are compared against a host reference (all devices must
 * match), and a GPU-vs-CPU speedup summary is printed at the end.
 *
 * Beyond the three benchmark kernels, each device also runs an interface
 * COVERAGE phase exercising the rest of the OpenCL 1.2 API surface this
 * runtime implements: info queries, retain/release, sub-buffers, fill/copy/
 * rect transfer, map/unmap, images + samplers, events (user events, callbacks,
 * profiling), task/marker/barrier, migrate, compile/link and the documented
 * rejection paths (fission, binary ingestion, kernel-arg info).
 */
#include <CL/cl.h>
#include <ewoksys/kernel_tic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Workload sized so the FULL CPU-vs-GPU comparison (every benchmark runs a
 * warm-up + ITERS timed launches on EACH device) finishes in ~8s on raspi5,
 * well inside the 20s budget.  The CPU interpreter cost scales as: saxpy ~
 * work-items, matmul ~ N^3, math ~ N*INNER, so these are the knobs that matter.
 * Each workload stays large enough that a real GPU dispatch still shows a clear
 * speedup over the interpreter. */
#define SAXPY_N     (256 * 1024)    /* floats; multiple of 4            */
#define SAXPY_ITERS 2
#define MATMUL_N    48              /* square matrix side               */
#define MATMUL_ITERS 2
#define MATH_N      (8 * 1024)      /* work-items                       */
#define MATH_ITERS  2
#define MATH_INNER  32

static const char *SAXPY_SRC =
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

static const char *MATMUL_SRC =
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

static const char *MATH_SRC =
    "__kernel void mathmix(__global const float *in,\n"
    "                      __global float *out,\n"
    "                      int inner)\n"
    "{\n"
    "    int i = get_global_id(0);\n"
    "    float v = in[i];\n"
    "    float acc = 0.0f;\n"
    "    for (int k = 0; k < inner; k++) {\n"
    "        acc += sin(v) * cos(v) + sqrt(fabs(v) + 1.0f);\n"
    "        v = fma(acc, 0.001f, v);\n"
    "    }\n"
    "    out[i] = acc;\n"
    "}\n";

static uint64_t now_ns(void)
{
    uint64_t ns = 0;
    if (kernel_tic_nsec(&ns) != 0) {
        uint32_t sec = 0;
        uint64_t usec = 0;
        kernel_tic(&sec, &usec);
        ns = (uint64_t)sec * 1000000000ull + usec * 1000ull;
    }
    return ns;
}

/* ---- host references ------------------------------------------------------ */
/* saxpy launches k times into the same y buffer, so the expected final state
 * is y = a^k*x + (a^(k-1)+...+1)*y0, not a single application. */
static void ref_saxpy(float a, const float *x, const float *y0, float *y, int n, int k)
{
    int i, t;
    for (i = 0; i < n; i++) {
        float v = y0[i];
        for (t = 0; t < k; t++) v = a * x[i] + v;
        y[i] = v;
    }
}

static void ref_matmul(const float *A, const float *B, float *C, int n)
{
    int i, j, k;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            float e = 0.0f;
            for (k = 0; k < n; k++) e += A[i * n + k] * B[k * n + j];
            C[i * n + j] = e;
        }
}

static void ref_math(const float *in, float *out, int n, int inner)
{
    int i, k;
    for (i = 0; i < n; i++) {
        float v = in[i], acc = 0.0f;
        for (k = 0; k < inner; k++) {
            acc += (float)(sin(v) * cos(v) + sqrt(fabs(v) + 1.0));
            v = acc * 0.001f + v;
        }
        out[i] = acc;
    }
}

static int check(const char *tag, const float *got, const float *want,
                 int n, float tol)
{
    int i, bad = 0;
    for (i = 0; i < n; i++) {
        float d = (float)fabs(got[i] - want[i]);
        float s = (float)fabs(want[i]);
        if (d > tol && d > tol * s) {
            if (bad < 3)
                printf("  %s: mismatch[%d] got %f want %f\n", tag, i, got[i], want[i]);
            bad++;
        }
    }
    if (bad) printf("  %s: VERIFY FAIL (%d/%d)\n", tag, bad, n);
    return bad;
}

/* ---- per-device benchmark results, kept for the final summary ------------- */
#define MAX_DEV 4
static struct {
    char name[64];
    int  is_gpu;
    double saxpy_ms, matmul_ms, math_ms;   /* per launch; <0 = not run */
} g_res[MAX_DEV];
static int g_nres;

struct bench_ctx {
    cl_context ctx;
    cl_command_queue q;
};

/* Run one kernel `iters` times (plus one untimed warm-up), verifying the
 * final output against `want`.  Returns the mean timed launch cost in ms, or
 * -1.0 on failure. */
static double run_bench(struct bench_ctx *bc, cl_program prog, const char *kname,
                        const char *tag, int iters,
                        void (*set_args)(cl_kernel, void *), void *args_cb_data,
                        size_t dims, const size_t *gws, const size_t *lws,
                        cl_mem out_buf, size_t out_bytes,
                        const float *want, int out_n, float tol)
{
    cl_int err;
    cl_kernel k;
    uint64_t t0, t1;
    double total = 0.0;
    float *back;
    int i, bad;

    k = clCreateKernel(prog, kname, &err);
    if (!k) { printf("  %s: clCreateKernel %d\n", tag, err); return -1.0; }
    set_args(k, args_cb_data);

    err = clEnqueueNDRangeKernel(bc->q, k, dims, NULL, gws, lws, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        printf("  %s: ndrange %d\n", tag, err);
        clReleaseKernel(k);
        return -1.0;
    }
    clFinish(bc->q);

    t0 = now_ns();
    for (i = 0; i < iters; i++) {
        clEnqueueNDRangeKernel(bc->q, k, dims, NULL, gws, lws, 0, NULL, NULL);
        clFinish(bc->q);
    }
    t1 = now_ns();
    total = (double)(t1 - t0) / 1e6 / iters;

    back = (float *)malloc(out_bytes);
    clEnqueueReadBuffer(bc->q, out_buf, CL_TRUE, 0, out_bytes, back, 0, NULL, NULL);
    clFinish(bc->q);
    bad = check(tag, back, want, out_n, tol);
    free(back);
    clReleaseKernel(k);
    return bad ? -1.0 : total;
}

/* ================= interface coverage phase =============================== */
/* One-work-item kernel for the clEnqueueTask / event paths (GPU subset too). */
static const char *TASK_SRC =
    "__kernel void task(__global float *out, float v)\n"
    "{\n"
    "    out[get_global_id(0)] = v;\n"
    "}\n";

struct cov { int pass, fail; const char *dev; };
static volatile int g_cb_fired;
static int g_dtor_fired;

static void CL_CALLBACK cov_event_cb(cl_event e, cl_int status, void *ud)
{
    (void)e; (void)status; (void)ud;
    g_cb_fired = 1;
}
static void CL_CALLBACK cov_dtor_cb(cl_mem m, void *ud)
{
    (void)m; (void)ud;
    g_dtor_fired = 1;
}

#define COV_OK(call) do { \
    cl_int _e = (call); \
    if (_e == CL_SUCCESS) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s: %s -> %d\n", c->dev, #call, _e); fflush(stdout); } \
} while (0)
#define COV_ERR(call, want) do { \
    cl_int _e = (call); \
    if (_e == (want)) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s: %s -> %d (want %d)\n", c->dev, #call, _e, (int)(want)); fflush(stdout); } \
} while (0)
#define COV_TRUE(cond) do { \
    if (cond) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s: %s\n", c->dev, #cond); fflush(stdout); } \
} while (0)
#define COV_PTR(p, call) do { \
    (p) = (call); \
    if ((p) != NULL) c->pass++; \
    else { c->fail++; printf("  COV FAIL %s: %s -> NULL\n", c->dev, #call); fflush(stdout); } \
} while (0)

static void cov_platform(struct cov *c, cl_platform_id pl)
{
    char prof[64] = "", vend[64] = "", ext[256] = "";
    size_t sz = 0;
    COV_OK(clGetPlatformInfo(pl, CL_PLATFORM_PROFILE, sizeof(prof), prof, &sz));
    COV_TRUE(sz > 0);
    COV_OK(clGetPlatformInfo(pl, CL_PLATFORM_VENDOR, sizeof(vend), vend, NULL));
    COV_OK(clGetPlatformInfo(pl, CL_PLATFORM_EXTENSIONS, sizeof(ext), ext, NULL));
    COV_ERR(clGetPlatformInfo(pl, CL_PLATFORM_NAME, 2, prof, NULL), CL_INVALID_VALUE);
    COV_TRUE(clGetExtensionFunctionAddressForPlatform(pl, "clGetGLContextInfoKHR") != NULL);
    COV_TRUE(clGetExtensionFunctionAddress("clGetGLContextInfoKHR") != NULL);
    COV_OK(clUnloadCompiler());
    COV_OK(clUnloadPlatformCompiler(pl));
}

static void cov_device(struct cov *c, cl_platform_id pl, cl_device_id dev)
{
    cl_device_partition_property props[3];
    cl_device_id sub;
    cl_uint nd = 0, vend = 0;
    char ext[256] = "";
    size_t wg = 0;

    COV_OK(clGetDeviceIDs(pl, CL_DEVICE_TYPE_ALL, 0, NULL, &nd));
    COV_TRUE(nd > 0);
    COV_OK(clGetDeviceInfo(dev, CL_DEVICE_VENDOR_ID, sizeof(vend), &vend, NULL));
    COV_OK(clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, sizeof(ext), ext, NULL));
    COV_OK(clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(wg), &wg, NULL));
    COV_TRUE(wg > 0);
    COV_OK(clRetainDevice(dev));
    COV_OK(clReleaseDevice(dev));
    /* documented rejection: this runtime implements no device fission.
     * 0x1086 = CL_DEVICE_PARTITION_EQUALLY (the token is not in cl.h). */
    props[0] = 0x1086; props[1] = 2; props[2] = 0;
    COV_ERR(clCreateSubDevices(dev, props, 1, &sub, NULL), CL_INVALID_VALUE);
}

static void cov_context_queue(struct cov *c, cl_device_id dev, cl_context ctx,
                              cl_command_queue q)
{
    cl_device_type dtype = 0;
    cl_context c2 = NULL;
    cl_command_queue q2 = NULL;
    cl_uint n = 0, rc = 0;
    cl_command_queue_properties props = 0, old = 0;
    cl_device_id got = NULL;

    clGetDeviceInfo(dev, CL_DEVICE_TYPE, sizeof(dtype), &dtype, NULL);
    COV_PTR(c2, clCreateContextFromType(NULL, dtype, NULL, NULL, NULL));
    if (c2) {
        COV_OK(clGetContextInfo(c2, CL_CONTEXT_NUM_DEVICES, sizeof(n), &n, NULL));
        COV_TRUE(n >= 1);
        COV_OK(clRetainContext(c2));
        COV_OK(clGetContextInfo(c2, CL_CONTEXT_REFERENCE_COUNT, sizeof(rc), &rc, NULL));
        COV_TRUE(rc == 2);
        COV_OK(clReleaseContext(c2));
        COV_OK(clReleaseContext(c2));
    }
    COV_OK(clGetContextInfo(ctx, CL_CONTEXT_DEVICES, sizeof(got), &got, NULL));
    COV_TRUE(got == dev);

    COV_PTR(q2, clCreateCommandQueue(ctx, dev, CL_QUEUE_PROFILING_ENABLE, NULL));
    if (q2) {
        COV_OK(clGetCommandQueueInfo(q2, CL_QUEUE_PROPERTIES, sizeof(props), &props, NULL));
        COV_TRUE((props & CL_QUEUE_PROFILING_ENABLE) != 0);
        COV_OK(clGetCommandQueueInfo(q2, CL_QUEUE_DEVICE, sizeof(got), &got, NULL));
        COV_TRUE(got == dev);
        COV_OK(clRetainCommandQueue(q2));
        COV_OK(clReleaseCommandQueue(q2));
        COV_OK(clReleaseCommandQueue(q2));
    }
    COV_OK(clGetCommandQueueInfo(q, CL_QUEUE_CONTEXT, sizeof(c2), &c2, NULL));
    COV_TRUE(c2 == ctx);
    COV_OK(clSetCommandQueueProperty(q, CL_QUEUE_PROFILING_ENABLE, 0, &old));
    COV_OK(clSetCommandQueueProperty(q, CL_QUEUE_PROFILING_ENABLE,
                                     CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &old));
}

static void cov_buffer(struct cov *c, cl_context ctx, cl_command_queue q)
{
    cl_mem b = NULL, b2 = NULL, sub = NULL, dm = NULL;
    cl_buffer_region region;
    cl_int data[16], back[16], patt;
    cl_uint rc = 0, mapc = 0;
    cl_mem_object_type type = 0;
    cl_mem assoc = NULL;
    size_t msz = 0, off = 0;
    void *map = NULL;
    int i;

    for (i = 0; i < 16; i++) data[i] = i;
    COV_PTR(b, clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                              16 * sizeof(cl_int), data, NULL));
    if (!b) return;
    COV_OK(clGetMemObjectInfo(b, CL_MEM_TYPE, sizeof(type), &type, NULL));
    COV_TRUE(type == CL_MEM_OBJECT_BUFFER);
    COV_OK(clGetMemObjectInfo(b, CL_MEM_SIZE, sizeof(msz), &msz, NULL));
    COV_TRUE(msz == 16 * sizeof(cl_int));
    COV_OK(clRetainMemObject(b));
    COV_OK(clGetMemObjectInfo(b, CL_MEM_REFERENCE_COUNT, sizeof(rc), &rc, NULL));
    COV_TRUE(rc == 2);
    COV_OK(clReleaseMemObject(b));

    /* write / read back a modified copy */
    for (i = 0; i < 16; i++) back[i] = 100 + i;
    COV_OK(clEnqueueWriteBuffer(q, b, CL_TRUE, 0, 16 * sizeof(cl_int), back, 0, NULL, NULL));
    memset(data, 0, sizeof(data));
    COV_OK(clEnqueueReadBuffer(q, b, CL_TRUE, 0, 16 * sizeof(cl_int), data, 0, NULL, NULL));
    COV_TRUE(memcmp(data, back, sizeof(data)) == 0);

    /* fill with an int pattern */
    patt = 0x5A5A5A5A;
    COV_OK(clEnqueueFillBuffer(q, b, &patt, sizeof(patt), 0, 16 * sizeof(cl_int), 0, NULL, NULL));
    COV_OK(clFinish(q));
    memset(data, 0, sizeof(data));
    COV_OK(clEnqueueReadBuffer(q, b, CL_TRUE, 0, 16 * sizeof(cl_int), data, 0, NULL, NULL));
    COV_TRUE(data[0] == patt && data[15] == patt);

    /* copy buffer -> buffer */
    COV_PTR(b2, clCreateBuffer(ctx, CL_MEM_READ_WRITE, 16 * sizeof(cl_int), NULL, NULL));
    if (b2) {
        COV_OK(clEnqueueCopyBuffer(q, b, b2, 0, 0, 16 * sizeof(cl_int), 0, NULL, NULL));
        COV_OK(clFinish(q));
        memset(back, 0, sizeof(back));
        COV_OK(clEnqueueReadBuffer(q, b2, CL_TRUE, 0, 16 * sizeof(cl_int), back, 0, NULL, NULL));
        COV_TRUE(back[0] == patt && back[15] == patt);
        clReleaseMemObject(b2);
    }

    /* rect write/read/copy on a 8x8 float buffer.  Per spec the region and
     * both row pitches are in BYTES: copy a 4(float)x4(row) corner, i.e.
     * 16 bytes per row, host rows packed (16 B), buffer rows at 8*4 = 32 B. */
    COV_PTR(b2, clCreateBuffer(ctx, CL_MEM_READ_WRITE, 64 * sizeof(float), NULL, NULL));
    if (b2) {
        size_t boff[3] = { 0, 0, 0 }, breg[3] = { 4 * sizeof(float), 4, 1 };
        float src[16], dst[16];
        size_t hb, rb;
        for (i = 0; i < 16; i++) src[i] = (float)i;
        hb = 8 * sizeof(float); rb = 4 * sizeof(float);
        COV_OK(clEnqueueWriteBufferRect(q, b2, CL_TRUE, boff, boff, breg,
                                        hb, 0, rb, 0, src, 0, NULL, NULL));
        memset(dst, 0, sizeof(dst));
        COV_OK(clEnqueueReadBufferRect(q, b2, CL_TRUE, boff, boff, breg,
                                       hb, 0, rb, 0, dst, 0, NULL, NULL));
        COV_TRUE(memcmp(src, dst, sizeof(src)) == 0);
        COV_OK(clEnqueueCopyBufferRect(q, b2, b2, boff, boff, breg,
                                       hb, 0, hb, 0, 0, NULL, NULL));
        COV_OK(clFinish(q));
        clReleaseMemObject(b2);
    }

    /* sub-buffer: ints [4..12) of the parent */
    region.origin = 4 * sizeof(cl_int);
    region.size = 8 * sizeof(cl_int);
    COV_PTR(sub, clCreateSubBuffer(b, CL_MEM_READ_WRITE, CL_BUFFER_CREATE_TYPE_REGION,
                                   &region, NULL));
    if (sub) {
        COV_OK(clGetMemObjectInfo(sub, CL_MEM_ASSOCIATED_MEMOBJECT, sizeof(assoc), &assoc, NULL));
        COV_TRUE(assoc == b);
        COV_OK(clGetMemObjectInfo(sub, CL_MEM_OFFSET, sizeof(off), &off, NULL));
        COV_TRUE(off == region.origin);
        memset(back, 0, sizeof(back));
        COV_OK(clEnqueueReadBuffer(q, sub, CL_TRUE, 0, region.size, back, 0, NULL, NULL));
        COV_TRUE(back[0] == patt);
        back[0] = 77;
        COV_OK(clEnqueueWriteBuffer(q, sub, CL_TRUE, 0, sizeof(cl_int), back, 0, NULL, NULL));
        memset(data, 0, sizeof(data));
        COV_OK(clEnqueueReadBuffer(q, b, CL_TRUE, region.origin, sizeof(cl_int), data, 0, NULL, NULL));
        COV_TRUE(data[0] == 77);
        clReleaseMemObject(sub);
    }

    /* map / unmap */
    map = clEnqueueMapBuffer(q, b, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0,
                             16 * sizeof(cl_int), 0, NULL, NULL, NULL);
    COV_TRUE(map != NULL);
    if (map) {
        ((cl_int *)map)[0] = 4242;
        COV_OK(clGetMemObjectInfo(b, CL_MEM_MAP_COUNT, sizeof(mapc), &mapc, NULL));
        COV_TRUE(mapc >= 1);
        COV_OK(clEnqueueUnmapMemObject(q, b, map, 0, NULL, NULL));
        COV_OK(clFinish(q));
        memset(data, 0, sizeof(data));
        COV_OK(clEnqueueReadBuffer(q, b, CL_TRUE, 0, sizeof(cl_int), data, 0, NULL, NULL));
        COV_TRUE(data[0] == 4242);
    }

    /* migrate (host-unified memory: a validated no-op is acceptable) */
    COV_OK(clEnqueueMigrateMemObjects(q, 1, &b, 0, 0, NULL, NULL));
    COV_OK(clFinish(q));

    /* destructor callback fires on the last release */
    g_dtor_fired = 0;
    COV_PTR(dm, clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(cl_int), NULL, NULL));
    if (dm) {
        COV_OK(clSetMemObjectDestructorCallback(dm, cov_dtor_cb, NULL));
        clReleaseMemObject(dm);
        COV_TRUE(g_dtor_fired == 1);
    }

    clReleaseMemObject(b);
}

static void cov_image(struct cov *c, cl_context ctx, cl_command_queue q)
{
    cl_image_format fmt, sup[8], rf;
    cl_image_desc desc;
    cl_mem img = NULL, img2 = NULL, buf = NULL;
    unsigned char px[4 * 4 * 4], back[4 * 4 * 4];
    unsigned char white[4] = { 255, 255, 255, 255 };
    cl_uint nfmt = 0;
    size_t w = 0, elem = 0, rp = 0, sz = 0;
    cl_sampler smp = NULL;
    cl_bool norm = CL_TRUE;
    cl_addressing_mode am = 0;
    cl_filter_mode fm = 0;
    size_t origin[3] = { 0, 0, 0 }, region[3] = { 4, 4, 1 };
    int i;

    COV_OK(clGetSupportedImageFormats(ctx, CL_MEM_READ_WRITE, CL_MEM_OBJECT_IMAGE2D,
                                      8, sup, &nfmt));
    COV_TRUE(nfmt > 0);

    fmt.image_channel_order = CL_RGBA;
    fmt.image_channel_data_type = CL_UNORM_INT8;
    memset(&desc, 0, sizeof(desc));
    desc.image_type = CL_MEM_OBJECT_IMAGE2D;
    desc.image_width = 4;
    desc.image_height = 4;
    COV_PTR(img, clCreateImage(ctx, CL_MEM_READ_WRITE, &fmt, &desc, NULL, NULL));
    if (!img) return;
    COV_OK(clGetImageInfo(img, CL_IMAGE_WIDTH, sizeof(w), &w, NULL));
    COV_TRUE(w == 4);
    COV_OK(clGetImageInfo(img, CL_IMAGE_ELEMENT_SIZE, sizeof(elem), &elem, NULL));
    COV_TRUE(elem == 4);
    COV_OK(clGetImageInfo(img, CL_IMAGE_ROW_PITCH, sizeof(rp), &rp, NULL));
    COV_TRUE(rp == 16);
    COV_OK(clGetImageInfo(img, CL_IMAGE_FORMAT, sizeof(rf), &rf, NULL));
    COV_TRUE(rf.image_channel_order == CL_RGBA);
    COV_OK(clGetMemObjectInfo(img, CL_MEM_SIZE, sizeof(sz), &sz, NULL));
    COV_TRUE(sz == 64);

    for (i = 0; i < 64; i++) px[i] = (unsigned char)i;
    COV_OK(clEnqueueWriteImage(q, img, CL_TRUE, origin, region, 0, 0, px, 0, NULL, NULL));
    memset(back, 0, sizeof(back));
    COV_OK(clEnqueueReadImage(q, img, CL_TRUE, origin, region, 0, 0, back, 0, NULL, NULL));
    COV_TRUE(memcmp(px, back, 64) == 0);

    COV_PTR(img2, clCreateImage2D(ctx, CL_MEM_READ_WRITE, &fmt, 4, 4, 0, NULL, NULL));
    if (img2) {
        COV_OK(clEnqueueCopyImage(q, img, img2, origin, origin, region, 0, NULL, NULL));
        COV_OK(clFinish(q));
        memset(back, 0, sizeof(back));
        COV_OK(clEnqueueReadImage(q, img2, CL_TRUE, origin, region, 0, 0, back, 0, NULL, NULL));
        COV_TRUE(memcmp(px, back, 64) == 0);
        COV_OK(clEnqueueFillImage(q, img2, white, origin, region, 0, NULL, NULL));
        COV_OK(clFinish(q));
        memset(back, 0, sizeof(back));
        COV_OK(clEnqueueReadImage(q, img2, CL_TRUE, origin, region, 0, 0, back, 0, NULL, NULL));
        COV_TRUE(back[0] == 255 && back[1] == 255 && back[63] == 255);
        clReleaseMemObject(img2);
    }

    /* image <-> buffer round trip */
    COV_PTR(buf, clCreateBuffer(ctx, CL_MEM_READ_WRITE, 64, NULL, NULL));
    if (buf) {
        COV_OK(clEnqueueCopyImageToBuffer(q, img, buf, origin, region, 0, 0, NULL, NULL));
        COV_OK(clFinish(q));
        memset(back, 0, sizeof(back));
        COV_OK(clEnqueueReadBuffer(q, buf, CL_TRUE, 0, 64, back, 0, NULL, NULL));
        COV_TRUE(memcmp(px, back, 64) == 0);
        COV_OK(clEnqueueFillBuffer(q, buf, white, 1, 0, 64, 0, NULL, NULL));
        COV_OK(clEnqueueCopyBufferToImage(q, buf, img, 0, origin, region, 0, NULL, NULL));
        COV_OK(clFinish(q));
        memset(back, 0, sizeof(back));
        COV_OK(clEnqueueReadImage(q, img, CL_TRUE, origin, region, 0, 0, back, 0, NULL, NULL));
        COV_TRUE(back[0] == 255 && back[63] == 255);
        /* mapped image view */
        {
            void *m = clEnqueueMapImage(q, img, CL_TRUE, CL_MAP_READ, origin, region,
                                        &rp, NULL, 0, NULL, NULL, NULL);
            COV_TRUE(m != NULL);
            if (m) {
                COV_TRUE(((unsigned char *)m)[0] == 255);
                COV_OK(clEnqueueUnmapMemObject(q, img, m, 0, NULL, NULL));
                COV_OK(clFinish(q));
            }
        }
        clReleaseMemObject(buf);
    }

    /* sampler create + info + refcount */
    COV_PTR(smp, clCreateSampler(ctx, CL_FALSE, CL_ADDRESS_CLAMP_TO_EDGE,
                                 CL_FILTER_NEAREST, NULL));
    if (smp) {
        COV_OK(clGetSamplerInfo(smp, CL_SAMPLER_NORMALIZED_COORDS, sizeof(norm), &norm, NULL));
        COV_TRUE(norm == CL_FALSE);
        COV_OK(clGetSamplerInfo(smp, CL_SAMPLER_ADDRESSING_MODE, sizeof(am), &am, NULL));
        COV_TRUE(am == CL_ADDRESS_CLAMP_TO_EDGE);
        COV_OK(clGetSamplerInfo(smp, CL_SAMPLER_FILTER_MODE, sizeof(fm), &fm, NULL));
        COV_TRUE(fm == CL_FILTER_NEAREST);
        COV_OK(clRetainSampler(smp));
        COV_OK(clReleaseSampler(smp));
        COV_OK(clReleaseSampler(smp));
    }

    clReleaseMemObject(img);
}

/* ---- saxpy --------------------------------------------------------------- */
struct saxpy_args { cl_mem bx, by; float a; };
static void saxpy_set_args(cl_kernel k, void *d)
{
    struct saxpy_args *s = (struct saxpy_args *)d;
    clSetKernelArg(k, 0, sizeof(float), &s->a);
    clSetKernelArg(k, 1, sizeof(cl_mem), &s->bx);
    clSetKernelArg(k, 2, sizeof(cl_mem), &s->by);
}

/* ---- matmul -------------------------------------------------------------- */
struct matmul_args { cl_mem bA, bB, bC; int n; };
static void matmul_set_args(cl_kernel k, void *d)
{
    struct matmul_args *m = (struct matmul_args *)d;
    clSetKernelArg(k, 0, sizeof(cl_mem), &m->bA);
    clSetKernelArg(k, 1, sizeof(cl_mem), &m->bB);
    clSetKernelArg(k, 2, sizeof(cl_mem), &m->bC);
    clSetKernelArg(k, 3, sizeof(int), &m->n);
}

/* ---- mathmix ------------------------------------------------------------- */
struct math_args { cl_mem bin, bout; int inner; };
static void math_set_args(cl_kernel k, void *d)
{
    struct math_args *m = (struct math_args *)d;
    clSetKernelArg(k, 0, sizeof(cl_mem), &m->bin);
    clSetKernelArg(k, 1, sizeof(cl_mem), &m->bout);
    clSetKernelArg(k, 2, sizeof(int), &m->inner);
}

static void cov_kernel_program(struct cov *c, cl_context ctx, cl_device_id dev,
                               cl_command_queue q)
{
    const char *srcs[2];
    size_t lens[2];
    cl_program p = NULL, p2 = NULL, lnk = NULL;
    cl_kernel ks[4];
    cl_kernel k = NULL;
    cl_uint nk = 0, nargs = 0, rc = 0, nd = 0;
    char names[128] = "", fname[64] = "", log[256] = "", srcback[512];
    size_t srcsz = 0;
    cl_build_status st = 0;
    cl_program_binary_type bt = 0;
    size_t kwg = 0;
    cl_mem b = NULL;
    float v = 7.0f, got = 0.0f;
    int err2;

    (void)q;
    srcs[0] = SAXPY_SRC; lens[0] = strlen(SAXPY_SRC);
    srcs[1] = TASK_SRC;  lens[1] = strlen(TASK_SRC);
    COV_PTR(p, clCreateProgramWithSource(ctx, 2, srcs, lens, NULL));
    if (!p) return;
    COV_OK(clBuildProgram(p, 1, &dev, "-cl-fast-relaxed-math", NULL, NULL));

    COV_OK(clGetProgramInfo(p, CL_PROGRAM_REFERENCE_COUNT, sizeof(rc), &rc, NULL));
    COV_TRUE(rc == 1);
    COV_OK(clGetProgramInfo(p, CL_PROGRAM_NUM_DEVICES, sizeof(nd), &nd, NULL));
    COV_TRUE(nd == 1);
    COV_OK(clGetProgramInfo(p, CL_PROGRAM_SOURCE, 0, NULL, &srcsz));
    COV_TRUE(srcsz == lens[0] + lens[1] + 1);
    memset(srcback, 0, sizeof(srcback));
    COV_OK(clGetProgramInfo(p, CL_PROGRAM_SOURCE, sizeof(srcback), srcback, NULL));
    COV_TRUE(strlen(srcback) == lens[0] + lens[1]);
    COV_OK(clGetProgramInfo(p, CL_PROGRAM_NUM_KERNELS, sizeof(nk), &nk, NULL));
    COV_TRUE(nk == 2);
    COV_OK(clGetProgramInfo(p, CL_PROGRAM_KERNEL_NAMES, sizeof(names), names, NULL));
    COV_TRUE(strstr(names, "saxpy") != NULL && strstr(names, "task") != NULL);
    COV_OK(clGetProgramBuildInfo(p, dev, CL_PROGRAM_BUILD_STATUS, sizeof(st), &st, NULL));
    COV_TRUE(st == CL_BUILD_SUCCESS);
    COV_OK(clGetProgramBuildInfo(p, dev, CL_PROGRAM_BINARY_TYPE, sizeof(bt), &bt, NULL));
    COV_TRUE(bt == CL_PROGRAM_BINARY_TYPE_EXECUTABLE);
    COV_OK(clGetProgramBuildInfo(p, dev, CL_PROGRAM_BUILD_OPTIONS, sizeof(log), log, NULL));
    COV_TRUE(strcmp(log, "-cl-fast-relaxed-math") == 0);
    COV_OK(clGetProgramBuildInfo(p, dev, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL));

    /* clCreateKernelsInProgram: count query, then materialise */
    COV_OK(clCreateKernelsInProgram(p, 0, NULL, &nk));
    COV_TRUE(nk == 2);
    memset(ks, 0, sizeof(ks));
    COV_OK(clCreateKernelsInProgram(p, 2, ks, NULL));
    COV_TRUE(ks[0] != NULL && ks[1] != NULL);
    if (ks[0]) {
        COV_OK(clGetKernelInfo(ks[0], CL_KERNEL_FUNCTION_NAME, sizeof(fname), fname, NULL));
        COV_TRUE(strlen(fname) > 0);
        COV_OK(clGetKernelInfo(ks[0], CL_KERNEL_NUM_ARGS, sizeof(nargs), &nargs, NULL));
        COV_TRUE(nargs >= 2);
        COV_OK(clGetKernelInfo(ks[0], CL_KERNEL_REFERENCE_COUNT, sizeof(rc), &rc, NULL));
        COV_TRUE(rc == 1);
        COV_OK(clRetainKernel(ks[0]));
        COV_OK(clReleaseKernel(ks[0]));
        /* documented rejection: no -cl-kernel-arg-info in this runtime */
        COV_ERR(clGetKernelArgInfo(ks[0], 0, CL_KERNEL_ARG_NAME, 0, NULL, NULL),
                CL_KERNEL_ARG_INFO_NOT_AVAILABLE);
    }
    if (ks[0]) clReleaseKernel(ks[0]);
    if (ks[1]) clReleaseKernel(ks[1]);

    /* task kernel: clEnqueueTask runs exactly one work-item */
    COV_PTR(k, clCreateKernel(p, "task", NULL));
    if (k) {
        size_t one = 1;
        COV_OK(clGetKernelWorkGroupInfo(k, dev, CL_KERNEL_WORK_GROUP_SIZE,
                                        sizeof(kwg), &kwg, NULL));
        COV_TRUE(kwg > 0);
        COV_PTR(b, clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(float), NULL, NULL));
        if (b) {
            COV_OK(clSetKernelArg(k, 0, sizeof(cl_mem), &b));
            COV_OK(clSetKernelArg(k, 1, sizeof(float), &v));
            COV_OK(clEnqueueTask(q, k, 0, NULL, NULL));
            COV_OK(clFinish(q));
            COV_OK(clEnqueueReadBuffer(q, b, CL_TRUE, 0, sizeof(float), &got, 0, NULL, NULL));
            printf("  task readback got=%g expected=7.0\n", got);
            COV_TRUE(fabsf(got - 7.0f) < 1e-6f);
            /* single-item NDRange through the same kernel */
            got = 0.0f;
            v = 3.0f;
            COV_OK(clSetKernelArg(k, 1, sizeof(float), &v));
            COV_OK(clEnqueueNDRangeKernel(q, k, 1, NULL, &one, &one, 0, NULL, NULL));
            COV_OK(clFinish(q));
            COV_OK(clEnqueueReadBuffer(q, b, CL_TRUE, 0, sizeof(float), &got, 0, NULL, NULL));
            printf("  task readback got=%g expected=3.0\n", got);
            COV_TRUE(fabsf(got - 3.0f) < 1e-6f);
            clReleaseMemObject(b);
        }
        clReleaseKernel(k);
    }
    COV_OK(clRetainProgram(p));
    COV_OK(clReleaseProgram(p));

    /* compile folds into build; link is a documented rejection */
    COV_PTR(p2, clCreateProgramWithSource(ctx, 1, &TASK_SRC, &lens[1], NULL));
    if (p2) {
        COV_OK(clCompileProgram(p2, 1, &dev, NULL, 0, NULL, NULL, NULL, NULL));
        lnk = clLinkProgram(ctx, 1, &dev, NULL, 1, &p2, NULL, NULL, &err2);
        COV_TRUE(lnk == NULL && err2 == CL_LINK_PROGRAM_FAILURE);
        clReleaseProgram(p2);
    }
    /* documented rejections: no binary ingestion, no built-in kernels */
    {
        const unsigned char *bin = (const unsigned char *)"x";
        size_t bl = 1;
        cl_int bst = 0;
        cl_program pb = clCreateProgramWithBinary(ctx, 1, &dev, &bl, &bin, &bst, &err2);
        COV_TRUE(pb == NULL && err2 == CL_INVALID_BINARY && bst == CL_INVALID_VALUE);
        pb = clCreateProgramWithBuiltInKernels(ctx, 1, &dev, "foo", &err2);
        COV_TRUE(pb == NULL && err2 == CL_INVALID_VALUE);
    }
    clReleaseProgram(p);
}

static void cov_events(struct cov *c, cl_context ctx, cl_device_id dev,
                       cl_program prog)
{
    cl_command_queue pq = NULL;
    cl_event ev = NULL, mev = NULL, uev = NULL, dev2ev = NULL;
    cl_int status = 0;
    cl_command_queue cq = NULL;
    cl_command_type ct = 0;
    cl_ulong t0 = 0, t1 = 0, tq = 0, ts = 0;
    size_t one = 1;

    COV_PTR(pq, clCreateCommandQueue(ctx, dev, CL_QUEUE_PROFILING_ENABLE, NULL));
    if (!pq) return;

    /* marker / barrier, new and deprecated spellings */
    COV_OK(clEnqueueMarkerWithWaitList(pq, 0, NULL, &mev));
    COV_TRUE(mev != NULL);
    COV_OK(clEnqueueBarrierWithWaitList(pq, 0, NULL, NULL));
    COV_OK(clFinish(pq));
    if (mev) {
        COV_OK(clGetEventInfo(mev, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(status), &status, NULL));
        COV_TRUE(status == CL_COMPLETE);
        COV_OK(clRetainEvent(mev));
        COV_OK(clReleaseEvent(mev));
        COV_OK(clReleaseEvent(mev));
    }
    COV_OK(clEnqueueMarker(pq, &dev2ev));
    COV_OK(clEnqueueBarrier(pq));
    COV_OK(clFinish(pq));
    if (dev2ev) {
        COV_OK(clWaitForEvents(1, &dev2ev));
        COV_OK(clEnqueueWaitForEvents(pq, 1, &dev2ev));
        COV_OK(clFinish(pq));
        clReleaseEvent(dev2ev);
    }

    /* user event gates a real launch; callback must fire on completion */
    {
        cl_kernel k = clCreateKernel(prog, "task", NULL);
        cl_mem b = NULL;
        float v = 1.0f, got = 0.0f;
        if (k)
            COV_PTR(b, clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(float), NULL, NULL));
        if (k && b) {
            g_cb_fired = 0;
            COV_PTR(uev, clCreateUserEvent(ctx, NULL));
            if (uev) {
                COV_OK(clGetEventInfo(uev, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                      sizeof(status), &status, NULL));
                COV_TRUE(status == CL_SUBMITTED);
                COV_OK(clSetKernelArg(k, 0, sizeof(cl_mem), &b));
                COV_OK(clSetKernelArg(k, 1, sizeof(float), &v));
                COV_OK(clEnqueueTask(pq, k, 1, &uev, &ev));
                COV_TRUE(ev != NULL);
                if (ev) {
                    COV_OK(clSetEventCallback(ev, CL_COMPLETE, cov_event_cb, NULL));
                    COV_OK(clGetEventInfo(ev, CL_EVENT_COMMAND_QUEUE,
                                          sizeof(cq), &cq, NULL));
                    COV_TRUE(cq == (cl_command_queue)pq);
                    COV_OK(clGetEventInfo(ev, CL_EVENT_COMMAND_TYPE, sizeof(ct), &ct, NULL));
                    COV_TRUE(ct == CL_COMMAND_TASK);
                    COV_OK(clSetUserEventStatus(uev, CL_COMPLETE));
                    COV_OK(clWaitForEvents(1, &ev));
                    COV_OK(clGetEventInfo(ev, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                          sizeof(status), &status, NULL));
                    COV_TRUE(status == CL_COMPLETE);
                    COV_TRUE(g_cb_fired == 1);
                    /* profiling timestamps on the completed task */
                    COV_OK(clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_QUEUED,
                                                   sizeof(tq), &tq, NULL));
                    COV_OK(clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_SUBMIT,
                                                   sizeof(ts), &ts, NULL));
                    COV_OK(clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_START,
                                                   sizeof(t0), &t0, NULL));
                    COV_OK(clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_END,
                                                   sizeof(t1), &t1, NULL));
                    COV_TRUE(tq > 0 && t1 >= t0 && t0 > 0);
                    clReleaseEvent(ev);
                }
                clReleaseEvent(uev);
            }
            COV_OK(clEnqueueReadBuffer(pq, b, CL_TRUE, 0, sizeof(float), &got, 0, NULL, NULL));
            COV_TRUE(fabsf(got - 1.0f) < 1e-6f);
            clReleaseMemObject(b);
        }
        if (k) clReleaseKernel(k);
    }

    /* clFlush + a plain NDRange event on the profiling queue */
    {
        cl_kernel k = clCreateKernel(prog, "task", NULL);
        cl_mem b = NULL;
        float v = 2.0f;
        if (k)
            COV_PTR(b, clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(float), NULL, NULL));
        if (k && b) {
            COV_OK(clSetKernelArg(k, 0, sizeof(cl_mem), &b));
            COV_OK(clSetKernelArg(k, 1, sizeof(float), &v));
            COV_OK(clEnqueueNDRangeKernel(pq, k, 1, NULL, &one, &one, 0, NULL, &ev));
            COV_OK(clFlush(pq));
            COV_OK(clFinish(pq));
            if (ev) clReleaseEvent(ev);
            clReleaseMemObject(b);
        }
        if (k) clReleaseKernel(k);
    }

    clReleaseCommandQueue(pq);
}

/* Run the whole coverage phase on one device.  `prog` is the benchmark program
 * (saxpy/matmul/mathmix, already built for this device). */
static int run_coverage(cl_context ctx, cl_command_queue q, cl_device_id dev,
                        cl_platform_id pl, cl_program prog, const char *dname)
{
    struct cov c;
    c.pass = 0; c.fail = 0; c.dev = dname;

    cov_platform(&c, pl);
    cov_device(&c, pl, dev);
    cov_context_queue(&c, dev, ctx, q);
    cov_buffer(&c, ctx, q);
    cov_image(&c, ctx, q);
    cov_kernel_program(&c, ctx, dev, q);
    cov_events(&c, ctx, dev, prog);

    printf("  coverage: %d/%d API checks passed\n", c.pass, c.pass + c.fail);
    fflush(stdout);
    return c.fail;
}

int main(void)
{
    cl_platform_id platform = NULL;
    cl_device_id devices[MAX_DEV];
    cl_uint nplat = 0, ndev = 0, di;
    int fails = 0;

    setvbuf(stdout, NULL, _IONBF, 0);   /* live output over the ssh pipe */

    if (clGetPlatformIDs(1, &platform, &nplat) != CL_SUCCESS || nplat == 0) {
        printf("cltest: no OpenCL platform\n");
        return 1;
    }
    {
        char pname[128] = "", pver[128] = "";
        clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(pname) - 1, pname, NULL);
        clGetPlatformInfo(platform, CL_PLATFORM_VERSION, sizeof(pver) - 1, pver, NULL);
        printf("cltest: platform %s (%s)\n", pname, pver);
    }
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, MAX_DEV, devices, &ndev) != CL_SUCCESS || ndev == 0) {
        printf("cltest: no OpenCL device\n");
        return 1;
    }
    printf("cltest: %u device(s)\n", ndev);

    /* shared host input data (identical for every device) */
    float *sx = (float *)malloc(SAXPY_N * sizeof(float));
    float *sy0 = (float *)malloc(SAXPY_N * sizeof(float));
    float *sy_ref = (float *)malloc(SAXPY_N * sizeof(float));
    float *mA = (float *)malloc(MATMUL_N * MATMUL_N * sizeof(float));
    float *mB = (float *)malloc(MATMUL_N * MATMUL_N * sizeof(float));
    float *mC_ref = (float *)malloc(MATMUL_N * MATMUL_N * sizeof(float));
    float *tin = (float *)malloc(MATH_N * sizeof(float));
    float *tout_ref = (float *)malloc(MATH_N * sizeof(float));
    int i;
    float a = 2.5f;
    for (i = 0; i < SAXPY_N; i++) {
        sx[i] = (float)(i % 1000) * 0.01f;
        sy0[i] = (float)(SAXPY_N - i) * 0.25f;
    }
    ref_saxpy(a, sx, sy0, sy_ref, SAXPY_N, SAXPY_ITERS + 1);  /* + warm-up */
    for (i = 0; i < MATMUL_N * MATMUL_N; i++) {
        mA[i] = (float)((i * 7) % 13) * 0.5f - 3.0f;
        mB[i] = (float)((i * 5) % 11) * 0.25f - 1.0f;
    }
    ref_matmul(mA, mB, mC_ref, MATMUL_N);
    for (i = 0; i < MATH_N; i++)
        tin[i] = (float)((i * 37) % 101) * 0.125f - 6.0f;
    ref_math(tin, tout_ref, MATH_N, MATH_INNER);

    for (di = 0; di < ndev; di++) {
        cl_device_id device = devices[di];
        cl_device_type dtype = 0;
        cl_uint units = 0, mhz = 0;
        char dname[64] = "";
        cl_int err = CL_SUCCESS;
        cl_context ctx;
        cl_command_queue q;
        struct bench_ctx bc;
        cl_program prog;
        uint64_t tb0, tb1;
        double build_ms;

        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(dname) - 1, dname, NULL);
        clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(dtype), &dtype, NULL);
        clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(units), &units, NULL);
        clGetDeviceInfo(device, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(mhz), &mhz, NULL);
        printf("\n=== device %u: %s (%s, %u CU @ %u MHz) ===\n", di, dname,
               (dtype & CL_DEVICE_TYPE_GPU) ? "GPU" :
               (dtype & CL_DEVICE_TYPE_CPU) ? "CPU" : "OTHER", units, mhz);

        if (g_nres < MAX_DEV) {
            snprintf(g_res[g_nres].name, sizeof(g_res[0].name), "%s", dname);
            g_res[g_nres].is_gpu = (dtype & CL_DEVICE_TYPE_GPU) ? 1 : 0;
            g_res[g_nres].saxpy_ms = g_res[g_nres].matmul_ms = g_res[g_nres].math_ms = -1.0;
        }

        ctx = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
        if (!ctx) { printf("  clCreateContext %d\n", err); fails++; continue; }
        q = clCreateCommandQueue(ctx, device, 0, &err);
        if (!q) { printf("  clCreateCommandQueue %d\n", err); fails++; continue; }
        bc.ctx = ctx;
        bc.q = q;

        /* one build over all three sources, timed: on the GPU device this is
         * the transpile + on-device mesa compile + upload path */
        {
            const char *srcs[3];
            size_t lens[3];
            srcs[0] = SAXPY_SRC;  lens[0] = strlen(SAXPY_SRC);
            srcs[1] = MATMUL_SRC; lens[1] = strlen(MATMUL_SRC);
            srcs[2] = MATH_SRC;   lens[2] = strlen(MATH_SRC);
            prog = clCreateProgramWithSource(bc.ctx, 3, srcs, lens, &err);
            tb0 = now_ns();
            err = clBuildProgram(prog, 1, &device, NULL, NULL, NULL);
            tb1 = now_ns();
            build_ms = (double)(tb1 - tb0) / 1e6;
            if (err != CL_SUCCESS) {
                char log[4096];
                clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
                printf("  build failed (%.1f ms):\n%s\n", build_ms, log);
                fails++;
                continue;
            }
            printf("  build: %.1f ms\n", build_ms);
            fflush(stdout);
        }

        /* --- saxpy --- */
        {
            struct saxpy_args sa;
            size_t gws = SAXPY_N / 4, lws = 64;
            double ms;
            sa.a = a;
            sa.bx = clCreateBuffer(bc.ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   SAXPY_N * sizeof(float), sx, &err);
            sa.by = clCreateBuffer(bc.ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                   SAXPY_N * sizeof(float), sy0, &err);
            ms = run_bench(&bc, prog, "saxpy", "saxpy", SAXPY_ITERS,
                           saxpy_set_args, &sa, 1, &gws, &lws,
                           sa.by, SAXPY_N * sizeof(float), sy_ref, SAXPY_N, 1e-2f);
            printf("  saxpy  %8.2f ms/launch  (%dK float4 fma)\n", ms, SAXPY_N / 1024);
            fflush(stdout);
            if (g_nres < MAX_DEV) g_res[g_nres].saxpy_ms = ms;
            if (ms < 0) fails++;
            clReleaseMemObject(sa.bx); clReleaseMemObject(sa.by);
        }

        /* --- matmul --- */
        {
            struct matmul_args ma;
            size_t gws[2] = { MATMUL_N, MATMUL_N }, lws[2] = { 8, 8 };
            double ms;
            ma.n = MATMUL_N;
            ma.bA = clCreateBuffer(bc.ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   MATMUL_N * MATMUL_N * sizeof(float), mA, &err);
            ma.bB = clCreateBuffer(bc.ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   MATMUL_N * MATMUL_N * sizeof(float), mB, &err);
            ma.bC = clCreateBuffer(bc.ctx, CL_MEM_WRITE_ONLY,
                                   MATMUL_N * MATMUL_N * sizeof(float), NULL, &err);
            ms = run_bench(&bc, prog, "matmul", "matmul", MATMUL_ITERS,
                           matmul_set_args, &ma, 2, gws, lws,
                           ma.bC, MATMUL_N * MATMUL_N * sizeof(float),
                           mC_ref, MATMUL_N * MATMUL_N, 1e-2f);
            printf("  matmul %8.2f ms/launch  (%dx%d, %.1f MFLOP)\n",
                   ms, MATMUL_N, MATMUL_N, 2.0 * MATMUL_N * MATMUL_N * MATMUL_N / 1e6);
            fflush(stdout);
            if (g_nres < MAX_DEV) g_res[g_nres].matmul_ms = ms;
            if (ms < 0) fails++;
            clReleaseMemObject(ma.bA); clReleaseMemObject(ma.bB); clReleaseMemObject(ma.bC);
        }

        /* --- mathmix --- */
        {
            struct math_args ta;
            size_t gws = MATH_N, lws = 64;
            double ms;
            ta.inner = MATH_INNER;
            ta.bin = clCreateBuffer(bc.ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                    MATH_N * sizeof(float), tin, &err);
            ta.bout = clCreateBuffer(bc.ctx, CL_MEM_WRITE_ONLY,
                                     MATH_N * sizeof(float), NULL, &err);
            ms = run_bench(&bc, prog, "mathmix", "math", MATH_ITERS,
                           math_set_args, &ta, 1, &gws, &lws,
                           ta.bout, MATH_N * sizeof(float), tout_ref, MATH_N, 1e-2f);
            printf("  math   %8.2f ms/launch  (%dK items x %d ops)\n",
                   ms, MATH_N / 1024, MATH_INNER);
            fflush(stdout);
            if (g_nres < MAX_DEV) g_res[g_nres].math_ms = ms;
            if (ms < 0) fails++;
            clReleaseMemObject(ta.bin); clReleaseMemObject(ta.bout);
        }

        /* --- interface coverage (everything the benchmarks do not touch) --- */
        fails += run_coverage(ctx, q, device, platform, prog, dname);

        clReleaseProgram(prog);
        clReleaseCommandQueue(bc.q);
        clReleaseContext(bc.ctx);
        g_nres++;
    }

    /* ---- CPU-vs-GPU summary ---- */
    {
        int ci = -1, gi = -1, j;
        for (j = 0; j < g_nres; j++) {
            if (g_res[j].is_gpu && gi < 0) gi = j;
            if (!g_res[j].is_gpu && ci < 0) ci = j;
        }
        printf("\n=== summary (ms/launch, lower is better) ===\n");
        printf("%-10s %12s %12s %10s\n", "bench", "CPU", "GPU", "speedup");
        if (ci >= 0 && gi >= 0) {
            const char *tags[3] = { "saxpy", "matmul", "math" };
            double cv[3], gv[3];
            int b;
            cv[0] = g_res[ci].saxpy_ms; gv[0] = g_res[gi].saxpy_ms;
            cv[1] = g_res[ci].matmul_ms; gv[1] = g_res[gi].matmul_ms;
            cv[2] = g_res[ci].math_ms;   gv[2] = g_res[gi].math_ms;
            for (b = 0; b < 3; b++) {
                char cbuf[16], gbuf[16];
                snprintf(cbuf, sizeof(cbuf), "%.2f", cv[b]);
                snprintf(gbuf, sizeof(gbuf), "%.2f", gv[b]);
                if (cv[b] > 0 && gv[b] > 0)
                    printf("%-10s %12s %12s %9.1fx\n", tags[b], cbuf, gbuf, cv[b] / gv[b]);
                else
                    printf("%-10s %12s %12s %10s\n", tags[b],
                           cv[b] > 0 ? cbuf : "n/a", gv[b] > 0 ? gbuf : "n/a", "-");
            }
        } else {
            printf("(no CPU+GPU device pair on this machine; per-device numbers above)\n");
            for (j = 0; j < g_nres; j++)
                printf("  %-16s saxpy %.2f ms  matmul %.2f ms  math %.2f ms\n",
                       g_res[j].name, g_res[j].saxpy_ms, g_res[j].matmul_ms, g_res[j].math_ms);
        }
    }

    free(sx); free(sy0); free(sy_ref);
    free(mA); free(mB); free(mC_ref);
    free(tin); free(tout_ref);
    printf("\ncltest %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
