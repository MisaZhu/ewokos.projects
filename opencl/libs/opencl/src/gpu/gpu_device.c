/*
 * gpu_device.c - the GPU device backend (cl_backend_gpu) for machines whose
 * bsp_cl exposes a compute path (raspi5's V3D CSD today).  It reuses the same
 * clc front-end parse the CPU device runs, then per kernel:
 *
 *   1. transpiles the parsed AST to a GLSL ES 3.10 compute shader
 *      (clc_to_glsl.c) covering the GPU subset;
 *   2. compiles + links it with the on-device mesa backend
 *      (pglc_compile_compute_raw) and copies the uniform reflection;
 *   3. serializes the compute stage into the daemon's wire blob
 *      (pglc_program_serialize) and uploads it once (cl_program_upload), so the
 *      QPU is never re-sent per launch;
 *   4. at dispatch, packs the scalar args + the host global offset/size into the
 *      reflected uniform blob by name, gathers the __global buffers as SSBO
 *      canvases in binding order, and runs one NDRange (cl_kernel_dispatch).
 *
 * Everything outside the GPU subset is DECLINED, never mis-compiled: a declined
 * kernel (an unsupported construct, a compiler rejection, or a negative daemon
 * result) runs transparently on the CPU backend, so registering the GPU device
 * can only ever add speed, never change results.  Dispatch failures are local
 * to that launch: later kernels and launches continue to attempt the GPU.
 *
 * Global-id model (see clc_to_glsl.h): the shader bakes local_size = (256,1,1)
 * and this file dispatches ceil(gsz0/256) x gsz1 x gsz2 groups of 256 items,
 * so gl_GlobalInvocationID.x sweeps dim0 (a partial tail group is clipped by the
 * emitted guard) and .y/.z map 1:1 onto dim1/dim2 -- get_global_id(d) is exact
 * for every d.  The host's own local_work_size only gates the runtime's
 * validation; it does not affect the result because a GPU kernel may not read
 * any work-group builtin (those are declined to the CPU).
 */
#include "internal.h"
#include "clc/clc.h"
#include "clc_to_glsl.h"
#include "clclient.h"

#include <pglcompiler.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Per-kernel GPU state, hung off kernel->exec_kernel.  `fn` is the parsed
 * kernel so a declined launch can hand the very same AST to the CPU backend. */
typedef struct gpu_kernel {
    clc_function *fn;             /* parsed kernel (CPU-fallback handle)      */
    int32_t       handle;         /* uploaded program handle (< 0 = none)     */
    int           cpu_fallback;   /* 1: run this kernel on the CPU device     */
    clc_glsl_info info;           /* transpiled arg -> SSBO/uniform map       */
    uint32_t      unif_dwords;    /* span of the per-dispatch uniform blob    */
    struct pglc_uniform_binding *bindings;  /* copied uniform reflection      */
    uint32_t      nbindings;
} gpu_kernel;

/* ---- device probe + descriptor ------------------------------------------- */

/* Non-zero when a g2d daemon with a supported compute GPU is reachable.  Also
 * latches the on-device compiler's codegen target to the live silicon (the
 * version is cached, so this is cheap on every call). */
int cl_gpu_probe(void)
{
    return clclient_gpu_version() != 0u;
}

/* Fill the GPU device descriptor.  runtime.c has already memset the struct and
 * set type/platform/backend/index, so this must NOT clear them again -- it only
 * fills the clGetDeviceInfo-reported capabilities. */
void cl_gpu_device_init(struct _cl_device_id *d)
{
    d->name = "EwokOS GPU";
    d->vendor = "EwokOS";
    d->driver_version = "1.2";
    d->profile = "FULL_PROFILE";
    d->version = "OpenCL 1.2 EwokOS";
    d->opencl_c_version = "OpenCL C 1.2";
    d->extensions = "cl_khr_byte_addressable_store cl_khr_global_int32_base_atomics";
    d->built_in_kernels = "";

    d->vendor_id = 0;
    d->max_compute_units = 4;
    d->max_work_item_dimensions = 3;
    d->max_work_group_size = 256;
    d->max_work_item_sizes[0] = 256;
    d->max_work_item_sizes[1] = 256;
    d->max_work_item_sizes[2] = 256;
    d->max_clock_frequency = 800;
    /* the V3D CSD addresses an SSBO by its 32-bit physical base */
    d->address_bits = 32;
    d->max_mem_alloc_size = 256ull * 1024 * 1024;
    d->global_mem_size = 512ull * 1024 * 1024;
    d->local_mem_size = 64 * 1024;
    d->max_constant_buffer_size = 64 * 1024;
    d->max_constant_args = CL_MAX_KERNEL_ARGS;
    d->max_parameter_size = 1024;
    d->mem_base_addr_align = 1024;
    d->profiling_timer_resolution = 1;
    d->image_support = CL_TRUE;
    d->compiler_available = CL_TRUE;
    d->linker_available = CL_TRUE;
    d->endian_little = CL_TRUE;
    d->available = CL_TRUE;
    d->host_unified_memory = CL_FALSE;
    d->single_fp_config = CL_FP_INF_NAN | CL_FP_ROUND_TO_NEAREST |
                          CL_FP_ROUND_TO_ZERO | CL_FP_ROUND_TO_INF |
                          CL_FP_FMA | CL_FP_DENORM;
    d->double_fp_config = 0;   /* no double on the V3D CSD */
    d->global_mem_cache_type = CL_READ_WRITE_CACHE;
    d->global_mem_cacheline_size = 64;
    d->global_mem_cache_size = 32 * 1024;
    d->local_mem_type = CL_LOCAL;
    d->exec_capabilities = CL_EXEC_KERNEL;
    d->queue_properties = CL_QUEUE_PROFILING_ENABLE;
    d->printf_buffer_size = 0;
}

/* ---- small helpers (mirror cpu_device.c) --------------------------------- */
static struct _cl_program_build *gpu_find_build(struct _cl_program *p,
                                                struct _cl_device_id *dev)
{
    cl_uint i;
    for (i = 0; i < p->num_devices; i++)
        if (p->build[i].device == dev)
            return &p->build[i];
    return NULL;
}

/* Rebuild the ';' joined kernel-name list after a successful parse. */
static void gpu_refresh_kernel_names(struct _cl_program *p, clc_program *cp)
{
    int n = clc_program_num_kernels(cp);
    size_t total = 1;
    int i;
    char *s;
    size_t off = 0;

    if (n <= 0) {
        free(p->kernel_names);
        p->kernel_names = NULL;
        return;
    }
    for (i = 0; i < n; i++)
        total += strlen(clc_program_kernel(cp, i)->name) + 1;
    s = (char *)malloc(total);
    if (!s)
        return;
    s[0] = '\0';
    for (i = 0; i < n; i++) {
        const char *nm = clc_program_kernel(cp, i)->name;
        if (i) s[off++] = ';';
        memcpy(s + off, nm, strlen(nm));
        off += strlen(nm);
    }
    s[off] = '\0';
    free(p->kernel_names);
    p->kernel_names = s;
}

/* ---- backend: build ------------------------------------------------------ */
/* Parse with the shared front-end and stash the bare clc_program as the build's
 * exec handle (exactly what the CPU build stores).  Per-kernel transpile +
 * mesa compile is deferred to gpu_prepare_kernel, so a program mixing GPU-able
 * and declined kernels still builds successfully and clCreateKernel binds. */
static int gpu_build(struct _cl_program *prog, struct _cl_device_id *dev,
                     const char *source, const char *options,
                     cl_build_status *build_status, char *log, size_t logsz)
{
    clc_program *cp;
    struct _cl_program_build *b;
    (void)options;

    if (log && logsz)
        log[0] = '\0';

    cp = clc_parse(source, log, logsz);
    if (!cp) {
        *build_status = CL_BUILD_ERROR;
        if (log && logsz && log[0] == '\0')
            snprintf(log, logsz, "error: failed to compile OpenCL-C source\n");
        return -1;
    }

    b = gpu_find_build(prog, dev);
    if (!b) {
        clc_program_free(cp);
        *build_status = CL_BUILD_ERROR;
        return -1;
    }

    b->exec = cp;
    b->binary_type = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
    gpu_refresh_kernel_names(prog, cp);
    /* prog->frontend is intentionally left untouched: it is write-only (the CPU
     * backend sets it, nothing reads it), so a GPU-only context is safe. */

    *build_status = CL_BUILD_SUCCESS;
    return 0;
}

static void gpu_free_exec(void *exec)
{
    if (exec)
        clc_program_free((clc_program *)exec);
}

/* ---- uniform packing ----------------------------------------------------- */
/* Bit-pack one scalar by-value kernel arg into a u32 uniform dword (a float
 * rides as its raw IEEE bits, which is what the mesa uniform stream expects). */
static uint32_t pack_scalar_u32(const cl_kernel_arg *arg, clc_base base)
{
    uint32_t u = 0;
    switch (base) {
    case CLC_FLOAT: {
        float f = 0.0f;
        memcpy(&f, arg->value, sizeof(f));
        memcpy(&u, &f, sizeof(u));
        break;
    }
    case CLC_INT: {
        int32_t v = 0;
        memcpy(&v, arg->value, sizeof(v));
        u = (uint32_t)v;
        break;
    }
    case CLC_UINT: {
        uint32_t v = 0;
        memcpy(&v, arg->value, sizeof(v));
        u = v;
        break;
    }
    default:
        break;
    }
    return u;
}

/* ---- backend: kernel prepare --------------------------------------------- */
/* Resolve the named kernel, then try to put it on the GPU.  Preparation failures
 * set cpu_fallback and STILL return CL_SUCCESS, so clCreateKernel succeeds and
 * the launch transparently replays on the CPU backend.  Only a genuinely absent
 * kernel name is a hard error. */
static int gpu_prepare_kernel(struct _cl_kernel *kernel)
{
    clc_program *cp;
    clc_function *fn;
    gpu_kernel *gk;
    char *glsl;
    char err[256];
    struct pglc_program res;
    struct pglc_blob_opts opts;
    uint32_t blobsz;
    void *blob;
    int32_t handle;

    if (!kernel || !kernel->build)
        return CL_INVALID_KERNEL;
    cp = (clc_program *)kernel->build->exec;
    if (!cp)
        return CL_INVALID_PROGRAM_EXECUTABLE;
    fn = clc_program_find_kernel(cp, kernel->name);
    if (!fn)
        return CL_INVALID_KERNEL_NAME;

    gk = (gpu_kernel *)calloc(1, sizeof(*gk));
    if (!gk)
        return CL_OUT_OF_HOST_MEMORY;
    gk->fn = fn;
    gk->handle = -1;

    kernel->exec_kernel = gk;
    kernel->num_args = (cl_uint)fn->nparams;

    /* 1. transpile to GLSL ES 3.10 compute (NULL => outside the GPU subset). */
    glsl = clc_to_glsl(cp, fn, &gk->info, err, sizeof(err));
    if (!glsl) {
        gk->cpu_fallback = 1;
        return CL_SUCCESS;
    }

    /* 2. compile + link with the on-device mesa backend. */
    memset(&res, 0, sizeof(res));
    if (!pglc_compile_compute_raw(glsl, &res) || !res.success ||
        res.stage[PGLC_STAGE_COMPUTE].code_words == 0) {
        const char *gp = glsl;
        int gln = 1;
        fprintf(stderr, "opencl-gpu: kernel '%s' pglc compile failed: %.400s\n",
                kernel->name, res.log[0] ? res.log : "(no log)");
        fprintf(stderr, "---- generated GLSL for '%s' ----\n", kernel->name);
        while (gp && *gp) {
            const char *nl = strchr(gp, '\n');
            fprintf(stderr, "%3d: %.*s\n", gln++,
                    nl ? (int)(nl - gp) : (int)strlen(gp), gp);
            if (!nl) break;
            gp = nl + 1;
        }
        fprintf(stderr, "---- end GLSL ----\n");
        free(glsl);
        pglc_program_free(&res);
        gk->cpu_fallback = 1;
        return CL_SUCCESS;
    }
    free(glsl);

    /* copy the uniform reflection (name -> blob dword offset) for dispatch. */
    {
        struct pglc_stage *cs = &res.stage[PGLC_STAGE_COMPUTE];
        gk->unif_dwords = cs->uniform_blob_dwords;
        gk->nbindings   = cs->num_uniform_bindings;
        if (gk->nbindings && cs->uniform_bindings) {
            size_t nb = (size_t)gk->nbindings * sizeof(struct pglc_uniform_binding);
            gk->bindings = (struct pglc_uniform_binding *)malloc(nb);
            if (!gk->bindings) {
                pglc_program_free(&res);
                gk->nbindings = 0;
                gk->cpu_fallback = 1;
                return CL_SUCCESS;
            }
            memcpy(gk->bindings, cs->uniform_bindings, nb);
        }
    }

    /* 3. serialize the compute stage into the daemon's wire blob (size query,
     *    then fill) and upload it once. */
    memset(&opts, 0, sizeof(opts));
    opts.geom_kind = 0;   /* plain compute program (no geometry-raster kind) */
    opts.geom_tpw  = 0;
    blobsz = pglc_program_serialize(&res, NULL, &opts, NULL, 0);
    if (blobsz == 0) {
        fprintf(stderr, "opencl-gpu: kernel '%s' serialize size query returned 0\n",
                kernel->name);
        pglc_program_free(&res);
        gk->cpu_fallback = 1;
        return CL_SUCCESS;
    }
    blob = malloc(blobsz);
    if (!blob) {
        fprintf(stderr, "opencl-gpu: kernel '%s' oom allocating %u byte blob\n",
                kernel->name, blobsz);
        pglc_program_free(&res);
        gk->cpu_fallback = 1;
        return CL_SUCCESS;
    }
    if (pglc_program_serialize(&res, NULL, &opts, blob, blobsz) == 0) {
        fprintf(stderr, "opencl-gpu: kernel '%s' serialize fill failed\n",
                kernel->name);
        free(blob);
        pglc_program_free(&res);
        gk->cpu_fallback = 1;
        return CL_SUCCESS;
    }

    handle = cl_program_upload(blob, blobsz);
    free(blob);
    pglc_program_free(&res);

    if (handle < 0) {
        fprintf(stderr, "opencl-gpu: kernel '%s' upload failed rc=%d (blob %u bytes)\n",
                kernel->name, (int)handle, blobsz);
        gk->cpu_fallback = 1;
        return CL_SUCCESS;
    }

    gk->handle = handle;
    return CL_SUCCESS;
}

/* ---- backend: ndrange ---------------------------------------------------- */
/* Replay the launch on the CPU backend.  cpu_ndrange reads fn from
 * kernel->exec_kernel and the program from kernel->build->exec (the bare
 * clc_program gpu_build stored), so temporarily pointing exec_kernel at the
 * parsed kernel is all that is needed; it is restored afterwards. */
static int gpu_run_cpu(struct _cl_command_queue *queue, struct _cl_kernel *kernel,
                       gpu_kernel *gk, cl_uint work_dim,
                       const size_t *global_work_offset,
                       const size_t *global_work_size,
                       const size_t *local_work_size)
{
    void *save = kernel->exec_kernel;
    int rc;

    kernel->exec_kernel = gk->fn;
    rc = cl_backend_cpu.ndrange(queue, kernel, work_dim,
                                global_work_offset, global_work_size, local_work_size);
    kernel->exec_kernel = save;
    return rc;
}

static int gpu_ndrange(struct _cl_command_queue *queue, struct _cl_kernel *kernel,
                       cl_uint work_dim, const size_t *global_work_offset,
                       const size_t *global_work_size, const size_t *local_work_size)
{
    gpu_kernel *gk = (gpu_kernel *)kernel->exec_kernel;
    uint32_t gsz[3], goff[3], wg[3], wg_size;
    uint32_t *unif = NULL;
    uint32_t  num_unif;
    void     *arg_host[CLCLIENT_MAX_ARGS];
    uint32_t  arg_size[CLCLIENT_MAX_ARGS];
    int       arg_shm[CLCLIENT_MAX_ARGS];
    uint32_t  nargs = 0;
    int       lsx;
    cl_uint   d;
    uint32_t  b;
    int       i;
    int32_t   rc;

    if (!gk)
        return CL_INVALID_KERNEL;

    if (gk->cpu_fallback || gk->handle < 0)
        return gpu_run_cpu(queue, kernel, gk, work_dim,
                           global_work_offset, global_work_size, local_work_size);

    /* normalise the global offset/size to 3 dims (unused: size 1, offset 0). */
    for (d = 0; d < 3; d++) {
        goff[d] = (d < work_dim && global_work_offset) ? (uint32_t)global_work_offset[d] : 0u;
        gsz[d]  = (d < work_dim) ? (uint32_t)global_work_size[d] : 1u;
        if (gsz[d] == 0) gsz[d] = 1u;
    }

    /* the shader baked local_size = (lsx,1,1): dim0 is swept by
     * gl_GlobalInvocationID.x across ceil(gsz0/lsx) groups (tail clipped by the
     * emitted guard); dim1/dim2 map 1:1 onto wgID.y/.z. */
    lsx = gk->info.local_size_x > 0 ? gk->info.local_size_x : CLC_GLSL_LOCAL_SIZE_X;
    wg[0]   = (gsz[0] + (uint32_t)lsx - 1u) / (uint32_t)lsx;
    wg[1]   = gsz[1];
    wg[2]   = gsz[2];
    wg_size = (uint32_t)lsx;

    /* pack the reflected uniform blob by name: the fixed global offset/size
     * uniforms plus each scalar by-value arg ("s<i>").  Unreferenced uniforms
     * were dead-stripped by the linker, so they simply are not in the table. */
    num_unif = gk->unif_dwords;
    if (num_unif) {
        unif = (uint32_t *)calloc(num_unif, sizeof(uint32_t));
        if (!unif)
            return gpu_run_cpu(queue, kernel, gk, work_dim,
                               global_work_offset, global_work_size, local_work_size);
        for (b = 0; b < gk->nbindings; b++) {
            struct pglc_uniform_binding *ub = &gk->bindings[b];
            uint32_t off = ub->dword_offset;
            uint32_t val = 0;
            int handled = 0;

            if (off >= num_unif)
                continue;
            if (!strncmp(ub->name, "u_goff", 6) && ub->name[6] >= '0' && ub->name[6] <= '2') {
                val = goff[ub->name[6] - '0'];
                handled = 1;
            } else if (!strncmp(ub->name, "u_gsz", 5) && ub->name[5] >= '0' && ub->name[5] <= '2') {
                val = gsz[ub->name[5] - '0'];
                handled = 1;
            } else if (ub->name[0] == 's' && ub->name[1] >= '0' && ub->name[1] <= '9') {
                i = atoi(ub->name + 1);
                if (i >= 0 && i < gk->info.nargs && i < CL_MAX_KERNEL_ARGS &&
                    gk->info.args[i].kind == CLC_GLSL_ARG_UNIFORM) {
                    cl_kernel_arg *arg = &kernel->args[i];
                    if (arg->set && !arg->is_mem) {
                        val = pack_scalar_u32(arg, gk->info.args[i].type.base);
                        handled = 1;
                    }
                }
            }
            if (handled)
                unif[off] = val;
        }
    }

    /* Gather __global buffers in binding order.  Only a zero-origin buffer can
     * use its contig-shm id directly; sub-buffer offsets need staged storage. */
    memset(arg_host, 0, sizeof(arg_host));
    memset(arg_size, 0, sizeof(arg_size));
    memset(arg_shm, 0, sizeof(arg_shm));
    for (i = 0; i < gk->info.nargs && i < CL_MAX_KERNEL_ARGS; i++) {
        clc_glsl_arg  *a   = &gk->info.args[i];
        cl_kernel_arg *arg = &kernel->args[i];
        int binding;

        if (a->kind != CLC_GLSL_ARG_SSBO)
            continue;
        binding = a->binding;
        if (binding < 0 || binding >= (int)CLCLIENT_MAX_ARGS)
            continue;
        if (arg->set && arg->is_mem && arg->mem) {
            struct _cl_mem *mem = arg->mem;
            struct _cl_mem *base = mem->parent ? mem->parent : mem;

            arg_host[binding] = cl_mem_device_ptr(mem);
            arg_size[binding] = (uint32_t)mem->size;
            if (mem->origin == 0 && base->gfx_shm > 0)
                arg_shm[binding] = base->gfx_shm;
        }
        if ((uint32_t)binding + 1u > nargs)
            nargs = (uint32_t)binding + 1u;
    }

    rc = cl_kernel_dispatch(gk->handle, wg, wg_size,
                            arg_host, arg_size, arg_shm, nargs, unif, num_unif);
    free(unif);

    if (rc == CLCLIENT_OK)
        return CL_SUCCESS;
    /* Ordinary failures may retry the GPU later.  After a terminal direct-job
     * fault clclient keeps subsequent direct launches on this safe CPU path. */
    return gpu_run_cpu(queue, kernel, gk, work_dim,
                       global_work_offset, global_work_size, local_work_size);
}

/* ---- backend: kernel release --------------------------------------------- */
static void gpu_release_kernel(struct _cl_kernel *kernel)
{
    gpu_kernel *gk;

    if (!kernel)
        return;
    gk = (gpu_kernel *)kernel->exec_kernel;
    if (gk) {
        if (gk->handle >= 0)
            cl_program_free(gk->handle);
        free(gk->bindings);
        free(gk);
    }
    kernel->exec_kernel = NULL;
}

/* ---- the vtable ---------------------------------------------------------- */
const cl_device_backend cl_backend_gpu = {
    "gpu",
    gpu_build,
    gpu_free_exec,
    gpu_prepare_kernel,
    gpu_ndrange,
    gpu_release_kernel
};
