/*
 * cpu_device.c - the CPU device backend (cl_backend_cpu).  It compiles OpenCL-C
 * source to the front-end AST (clc_parse) and executes kernels with the
 * tree-walking interpreter (clc_run_kernel), scheduling the NDRange one
 * work-item at a time in global-id order.
 *
 * Execution model
 *   - Serial: work-items of a work-group run to completion in order, so
 *     barrier() is a no-op fence (clc_builtin_impl.c) and __local memory is a
 *     single per-work-group block that every work-item of that group shares.
 *   - Every buffer/local argument is bound as a real host pointer into the
 *     cl_mem host store / the work-group local block, so the interpreter's
 *     pointer arithmetic addresses genuine memory.
 *
 * This is Phase 1 of the plan; the GPU backend (cl_backend_gpu) is compiled in
 * separately under OPENCL_GPU_BACKEND (Phase 2).
 */
#include "internal.h"
#include "clc/clc.h"
#include "clc/clc_interp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- small helpers ------------------------------------------------------- */
static int scalar_size(clc_type t)
{
    clc_type s = t;
    s.vec = 1;
    s.pointer = 0;
    {
        int n = clc_type_size(&s);
        return n > 0 ? n : 1;
    }
}

static struct _cl_program_build *cpu_find_build(struct _cl_program *p,
                                                struct _cl_device_id *dev)
{
    cl_uint i;
    for (i = 0; i < p->num_devices; i++)
        if (p->build[i].device == dev)
            return &p->build[i];
    return NULL;
}

/* Rebuild the ';' joined kernel-name list after a successful parse. */
static void cpu_refresh_kernel_names(struct _cl_program *p, clc_program *cp)
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
static int cpu_build(struct _cl_program *prog, struct _cl_device_id *dev,
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

    b = cpu_find_build(prog, dev);
    if (!b) {
        clc_program_free(cp);
        *build_status = CL_BUILD_ERROR;
        return -1;
    }

    b->exec = cp;
    b->binary_type = CL_PROGRAM_BINARY_TYPE_EXECUTABLE;
    prog->frontend = cp;
    cpu_refresh_kernel_names(prog, cp);

    *build_status = CL_BUILD_SUCCESS;
    return 0;
}

static void cpu_free_exec(void *exec)
{
    if (exec)
        clc_program_free((clc_program *)exec);
}

/* ---- backend: kernel prepare --------------------------------------------- */
static int cpu_prepare_kernel(struct _cl_kernel *kernel)
{
    clc_program *cp;
    clc_function *fn;

    if (!kernel || !kernel->build)
        return CL_INVALID_KERNEL;
    cp = (clc_program *)kernel->build->exec;
    if (!cp)
        return CL_INVALID_PROGRAM_EXECUTABLE;
    fn = clc_program_find_kernel(cp, kernel->name);
    if (!fn)
        return CL_INVALID_KERNEL_NAME;

    kernel->exec_kernel = fn;
    kernel->num_args = (cl_uint)fn->nparams;
    return CL_SUCCESS;
}

static void cpu_release_kernel(struct _cl_kernel *kernel)
{
    if (kernel)
        kernel->exec_kernel = NULL;
}

/* ---- argument binding ---------------------------------------------------- */
/* Compute the total per-work-group local block size: statically-declared
 * __local bytes plus every __local kernel argument's size. */
static size_t cpu_local_block_size(struct _cl_kernel *kernel, clc_function *fn)
{
    size_t sz = (size_t)(fn->local_mem_size > 0 ? fn->local_mem_size : 0);
    cl_uint i;
    for (i = 0; i < kernel->num_args && i < CL_MAX_KERNEL_ARGS; i++)
        if (kernel->args[i].is_local)
            sz += kernel->args[i].size;
    return sz;
}

/* Bind one kernel argument into a clc_value given the work-group local block.
 * local_cursor advances for each __local argument (placed after the static
 * __local region). */
static void cpu_bind_arg(clc_value *v, clc_type ptype, cl_kernel_arg *arg,
                         unsigned char *local_base, size_t static_local,
                         size_t *local_cursor)
{
    memset(v, 0, sizeof(*v));
    v->type = ptype;

    if (arg->is_local) {
        void *p = local_base + static_local + *local_cursor;
        *local_cursor += arg->size;
        v->i[0] = (long long)(uintptr_t)p;
        return;
    }
    if (arg->is_mem) {
        void *p = cl_mem_device_ptr(arg->mem);
        v->i[0] = (long long)(uintptr_t)p;
        return;
    }

    /* scalar / small aggregate passed by value */
    if (clc_type_is_float(&ptype)) {
        double d = 0.0;
        int lanes = ptype.vec > 1 ? ptype.vec : 1;
        int l;
        for (l = 0; l < lanes && l < CLC_MAX_VEC; l++) {
            const unsigned char *bp = arg->value + (size_t)l * scalar_size(ptype);
            if (ptype.base == CLC_DOUBLE) { double t; memcpy(&t, bp, 8); d = t; }
            else { float t = 0; memcpy(&t, bp, 4); d = t; }
            v->f[l] = d;
            v->i[l] = (long long)d;
        }
    } else if (ptype.pointer) {
        uintptr_t p = 0;
        memcpy(&p, arg->value, sizeof(p) < arg->size ? sizeof(p) : arg->size);
        v->i[0] = (long long)p;
    } else {
        int es = scalar_size(ptype);
        int lanes = ptype.vec > 1 ? ptype.vec : 1;
        int l;
        for (l = 0; l < lanes && l < CLC_MAX_VEC; l++) {
            const unsigned char *bp = arg->value + (size_t)l * es;
            long long iv = 0;
            switch (es) {
            case 1: iv = clc_type_is_signed(&ptype) ? (long long)(*(const signed char *)bp)
                                                    : (long long)(*(const unsigned char *)bp); break;
            case 2: iv = clc_type_is_signed(&ptype) ? (long long)(*(const short *)bp)
                                                    : (long long)(*(const unsigned short *)bp); break;
            case 4: iv = clc_type_is_signed(&ptype) ? (long long)(*(const int *)bp)
                                                    : (long long)(*(const unsigned int *)bp); break;
            default: memcpy(&iv, bp, 8); break;
            }
            v->i[l] = iv;
            v->f[l] = (double)iv;
        }
    }
}

/* ---- backend: ndrange ---------------------------------------------------- */
static void cpu_flush_printf(char *buf, size_t len)
{
    if (buf && len) {
        fwrite(buf, 1, len, stdout);
        fflush(stdout);
    }
}

static int cpu_ndrange(struct _cl_command_queue *queue, struct _cl_kernel *kernel,
                       cl_uint work_dim, const size_t *global_work_offset,
                       const size_t *global_work_size, const size_t *local_work_size)
{
    clc_function *fn = (clc_function *)kernel->exec_kernel;
    clc_exec_ctx ctx;
    size_t gwo[3], gws[3], lws[3], ngrp[3];
    size_t gx, gy, gz, lx, ly, lz;
    unsigned char *local_block = NULL;
    size_t static_local, block_size;
    char *printf_buf = NULL;
    size_t printf_len = 0;
    cl_uint d;
    int rc = CL_SUCCESS;
    (void)queue;

    if (!fn)
        return CL_INVALID_KERNEL;

    /* normalise dimensions (unused dims are size 1, offset 0) */
    for (d = 0; d < 3; d++) {
        gwo[d] = (d < work_dim && global_work_offset) ? global_work_offset[d] : 0;
        gws[d] = (d < work_dim) ? global_work_size[d] : 1;
        if (gws[d] == 0) gws[d] = 1;
        if (local_work_size && d < work_dim && local_work_size[d] > 0) {
            lws[d] = local_work_size[d];
        } else {
            /* choose a divisor of gws[d], capped so the group stays <= 256 */
            size_t cand = gws[d] > 64 ? 64 : gws[d];
            while (cand > 1 && (gws[d] % cand) != 0)
                cand--;
            lws[d] = cand ? cand : 1;
        }
        if (d >= work_dim)
            lws[d] = 1;
        ngrp[d] = (gws[d] + lws[d] - 1) / lws[d];
    }

    static_local = (size_t)(fn->local_mem_size > 0 ? fn->local_mem_size : 0);
    block_size = cpu_local_block_size(kernel, fn);

    printf_buf = (char *)malloc(64 * 1024);

    memset(&ctx, 0, sizeof(ctx));
    ctx.prog = (clc_program *)kernel->build->exec;
    ctx.work_dim = work_dim;
    for (d = 0; d < 3; d++) {
        ctx.global_offset[d] = gwo[d];
        ctx.global_size[d] = gws[d];
        ctx.local_size[d] = lws[d];
        ctx.num_groups[d] = ngrp[d];
    }
    ctx.printf_buf = printf_buf;
    ctx.printf_cap = printf_buf ? (64 * 1024) : 0;
    ctx.printf_len = printf_buf ? &printf_len : NULL;

    for (gz = 0; gz < ngrp[2]; gz++) {
    for (gy = 0; gy < ngrp[1]; gy++) {
    for (gx = 0; gx < ngrp[0]; gx++) {
        size_t local_cursor = 0;
        clc_value params[CLC_MAX_PARAMS];
        cl_uint np = (cl_uint)fn->nparams;
        cl_uint i;

        /* fresh, zeroed per-work-group local block */
        if (block_size) {
            local_block = (unsigned char *)calloc(1, block_size);
            if (!local_block) { rc = CL_OUT_OF_HOST_MEMORY; goto done; }
        }
        ctx.local_mem = local_block;
        ctx.group_id[0] = gx; ctx.group_id[1] = gy; ctx.group_id[2] = gz;

        /* bind arguments (pointers into mem / local block; scalars by value) */
        for (i = 0; i < np && i < CLC_MAX_PARAMS; i++) {
            clc_type ptype = fn->syms[i].type;
            cl_kernel_arg *arg = (i < kernel->num_args) ? &kernel->args[i] : NULL;
            cl_kernel_arg empty;
            if (!arg) { memset(&empty, 0, sizeof(empty)); arg = &empty; }
            cpu_bind_arg(&params[i], ptype, arg, local_block, static_local, &local_cursor);
        }

        /* run every work-item of the group in order */
        for (lz = 0; lz < lws[2]; lz++) {
        for (ly = 0; ly < lws[1]; ly++) {
        for (lx = 0; lx < lws[0]; lx++) {
            ctx.local_id[0] = lx; ctx.local_id[1] = ly; ctx.local_id[2] = lz;
            ctx.global_id[0] = gwo[0] + gx * lws[0] + lx;
            ctx.global_id[1] = gwo[1] + gy * lws[1] + ly;
            ctx.global_id[2] = gwo[2] + gz * lws[2] + lz;

            /* skip work-items beyond the global size in a partial group */
            if (ctx.global_id[0] >= gwo[0] + gws[0]) continue;
            if (ctx.global_id[1] >= gwo[1] + gws[1]) continue;
            if (ctx.global_id[2] >= gwo[2] + gws[2]) continue;

            if (clc_run_kernel(fn, params, (int)np, &ctx) != 0) {
                rc = CL_INVALID_OPERATION;
                goto done;
            }
        } } }

        if (local_block) { free(local_block); local_block = NULL; }
    } } }

done:
    if (local_block)
        free(local_block);
    if (printf_buf)
        cpu_flush_printf(printf_buf, printf_len);
    free(printf_buf);
    return rc;
}

/* ---- the vtable ---------------------------------------------------------- */
const cl_device_backend cl_backend_cpu = {
    "cpu",
    cpu_build,
    cpu_free_exec,
    cpu_prepare_kernel,
    cpu_ndrange,
    cpu_release_kernel
};
