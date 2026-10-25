/*
 * gpu_device.c - the raspi5 GPU device back-end (vk_backend_gpu) for machines
 * whose bsp exposes a V3D compute path.  It is the structural twin of OpenCL's
 * src/gpu/gpu_device.c and reuses the exact same on-device route:
 *
 *   1. transpile the pipeline's GLCompute SPIR-V entry to a GLSL ES 3.10 compute
 *      shader (spirv_to_glsl.c) covering the GPU subset;
 *   2. compile + link it with the on-device mesa backend
 *      (pglc_compile_compute_raw) and copy the uniform reflection;
 *   3. serialize the compute stage into the daemon's wire blob
 *      (pglc_program_serialize) and upload it once (vk_program_upload), so the
 *      QPU is never re-sent per dispatch;
 *   4. at vkCmdDispatch, gather the bound storage buffers as SSBO canvases in
 *      the flattened binding order the transpiler emitted and run one grid
 *      (vk_kernel_dispatch over the g2dd CL verbs -> bsp_cl -> vc_cl -> V3D CSD).
 *
 * Compilation is deferred to the first dispatch and cached on the pipeline's
 * backend_data, so a pipeline whose shader is outside the GPU subset still
 * creates successfully and simply runs on the CPU.
 *
 * Everything outside the GPU subset is DECLINED, never mis-compiled: a declined
 * dispatch (an unsupported construct, a compiler rejection, an unresolvable
 * binding, or a negative daemon result) runs transparently on the CPU back-end,
 * so registering the GPU device can only ever add speed, never change results.
 *
 * Graphics draws are ALWAYS declined to the software rasterizer: the V3D
 * compiled geom-raster path executes fixed vendored kernels parameterised by
 * uniforms (see geom_raster_cs.h), not arbitrary user vertex/fragment shaders,
 * so a general Vulkan graphics pipeline cannot be mapped onto it faithfully.
 * Declining is the safe choice the "GPU can only add speed, never change
 * results" rule prescribes; the CPU rasterizer already produces correct pixels.
 *
 * Workgroup model: the transpiler bakes the shader's LocalSize into the GLSL and
 * this file dispatches gx x gy x gz work-groups of local_size[0] items.  Only a
 * 1-D local shape (local_size = (N,1,1), N in 1..256) is offloaded -- that is
 * the shape the V3D CSD 1-D workgroup dispatch models exactly (gl_GlobalInvoca-
 * tionID = wgID*(N,1,1) + (lid,0,0)); any other shape declines to the CPU.
 */
#include "internal.h"
#include "cpu/cpu_priv.h"
#include "spirv.h"
#include "spirv_to_glsl.h"
#include "vkclient.h"

#include <pglcompiler.h>

#include <stdlib.h>
#include <string.h>

/* Per-pipeline GPU state, hung off pipeline->backend_data.  Lazily built on the
 * first dispatch; a declined pipeline keeps cpu_fallback set and the CPU back-end
 * runs every launch. */
typedef struct gpu_pipeline {
    int      prepared;       /* 1: gpu_prepare has run (success or decline)   */
    int      cpu_fallback;   /* 1: run this pipeline on the CPU device        */
    int32_t  handle;         /* uploaded program handle (< 0 = none)          */
    spirv_glsl_info info;    /* transpiled binding map + baked local_size     */
    uint32_t unif_dwords;    /* span of the per-dispatch uniform blob         */
    struct pglc_uniform_binding *bindings;  /* copied uniform reflection      */
    uint32_t nbindings;
} gpu_pipeline;

/* ---- device probe -------------------------------------------------------- */

/* Non-zero when a g2d daemon with a supported compute GPU is reachable.  Also
 * latches the on-device compiler's codegen target to the live silicon (the
 * version is cached, so this is cheap on every call).  runtime.c calls this once
 * at physical-device enumeration to decide whether the GPU back-end is offered. */
int vk_gpu_probe(void)
{
    return vkclient_gpu_version() != 0u;
}

/* ---- SSBO resolution ----------------------------------------------------- */

/* Resolve one storage-buffer descriptor write to its host store.  Mirrors the
 * CPU back-end's resolve_binding for the STORAGE_BUFFER case only.  Returns 0 on
 * success, -1 when the (set,binding) is not written or cannot be resolved to a
 * host pointer (which declines the dispatch to the CPU rather than mis-binding). */
static int gpu_find_ssbo(const vk_draw_ctx *dc, uint32_t set, uint32_t binding,
                         void **host, size_t *size)
{
    uint32_t s, i;
    for (s = 0; s < dc->set_count; s++) {
        const vk_descriptor_set *ds = dc->sets[s];
        uint32_t set_index = dc->first_set + s;
        if (!ds || set_index != set)
            continue;
        for (i = 0; i < ds->num_bindings; i++) {
            const vk_descriptor_binding *b = &ds->bindings[i];
            void *base;
            if (b->binding != binding)
                continue;
            if (b->type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || !b->buffer)
                return -1;
            base = vk_buffer_ptr(b->buffer);
            if (!base)
                return -1;
            *host = (char *)base + b->offset;
            *size = (size_t)(b->range == VK_WHOLE_SIZE
                                 ? (b->buffer->size - b->offset) : b->range);
            return 0;
        }
    }
    return -1;
}

/* ---- lazy per-pipeline prepare ------------------------------------------- */

/* Transpile + compile + serialize + upload the pipeline's compute entry once.
 * Any decline sets cpu_fallback and STILL returns a valid gpu_pipeline, so the
 * dispatch transparently replays on the CPU back-end. */
static gpu_pipeline *gpu_prepare(vk_pipeline *p)
{
    gpu_pipeline *gp;
    const spirv_module *mod = NULL;
    const spirv_entry *e;
    char *glsl, err[256];
    struct pglc_program res;
    struct pglc_blob_opts opts;
    struct pglc_stage *cs;
    uint32_t blobsz;
    void *blob;
    int32_t handle;

    if (p->backend_data)
        return (gpu_pipeline *)p->backend_data;

    gp = (gpu_pipeline *)calloc(1, sizeof(*gp));
    if (!gp)
        return NULL;
    gp->handle = -1;
    p->backend_data = gp;

#define GPU_DECLINE() do { gp->cpu_fallback = 1; gp->prepared = 1; return gp; } while (0)

    e = cpu_find_compute(p, &mod);
    if (!e || !mod)
        GPU_DECLINE();

    /* 1. transpile to GLSL ES 3.10 compute (NULL => outside the GPU subset). */
    glsl = spirv_to_glsl((spirv_module *)mod, e, &gp->info, err, sizeof(err));
    if (!glsl)
        GPU_DECLINE();

    /* only a 1-D local shape maps onto the V3D CSD workgroup dispatch exactly. */
    if (gp->info.local_size[1] != 1u || gp->info.local_size[2] != 1u ||
        gp->info.local_size[0] == 0u || gp->info.local_size[0] > 256u) {
        free(glsl);
        GPU_DECLINE();
    }

    /* 2. compile + link with the on-device mesa backend. */
    memset(&res, 0, sizeof(res));
    if (!pglc_compile_compute_raw(glsl, &res) || !res.success ||
        res.stage[PGLC_STAGE_COMPUTE].code_words == 0) {
        free(glsl);
        pglc_program_free(&res);
        GPU_DECLINE();
    }
    free(glsl);

    /* copy the uniform reflection (name -> blob dword offset) for dispatch. */
    cs = &res.stage[PGLC_STAGE_COMPUTE];
    gp->unif_dwords = cs->uniform_blob_dwords;
    gp->nbindings   = cs->num_uniform_bindings;
    if (gp->nbindings && cs->uniform_bindings) {
        size_t nb = (size_t)gp->nbindings * sizeof(struct pglc_uniform_binding);
        gp->bindings = (struct pglc_uniform_binding *)malloc(nb);
        if (!gp->bindings) {
            pglc_program_free(&res);
            gp->nbindings = 0;
            GPU_DECLINE();
        }
        memcpy(gp->bindings, cs->uniform_bindings, nb);
    }

    /* 3. serialize the compute stage into the daemon's wire blob (size query,
     *    then fill) and upload it once. */
    memset(&opts, 0, sizeof(opts));
    opts.geom_kind = 0;   /* plain compute program (no geometry-raster kind) */
    opts.geom_tpw  = 0;
    blobsz = pglc_program_serialize(&res, NULL, &opts, NULL, 0);
    if (blobsz == 0) {
        pglc_program_free(&res);
        GPU_DECLINE();
    }
    blob = malloc(blobsz);
    if (!blob) {
        pglc_program_free(&res);
        GPU_DECLINE();
    }
    if (pglc_program_serialize(&res, NULL, &opts, blob, blobsz) == 0) {
        free(blob);
        pglc_program_free(&res);
        GPU_DECLINE();
    }

    handle = vk_program_upload(blob, blobsz);
    free(blob);
    pglc_program_free(&res);

    if (handle < 0)
        GPU_DECLINE();

    gp->handle = handle;
    gp->prepared = 1;
    return gp;

#undef GPU_DECLINE
}

/* ---- backend: compute dispatch ------------------------------------------- */

static VkResult gpu_dispatch(vk_command_buffer *cb, vk_draw_ctx *dc,
                             uint32_t gx, uint32_t gy, uint32_t gz)
{
    vk_pipeline *p = dc->pipeline;
    gpu_pipeline *gp;
    void    *arg_host[VKCLIENT_MAX_ARGS];
    uint32_t arg_size[VKCLIENT_MAX_ARGS];
    uint32_t *unif = NULL;
    uint32_t  num_unif = 0;
    uint32_t  nargs = 0;
    uint32_t  wg[3];
    int32_t   rc;
    int       i;

    if (!p)
        return VK_ERROR_INITIALIZATION_FAILED;

    gp = gpu_prepare(p);
    if (!gp || gp->cpu_fallback || gp->handle < 0)
        return cpu_dispatch(cb, dc, gx, gy, gz);

    /* gather the bound storage buffers as SSBO host stores, indexed by the
     * flattened GLSL binding slot the transpiler emitted (args[] is positional). */
    memset(arg_host, 0, sizeof(arg_host));
    memset(arg_size, 0, sizeof(arg_size));
    for (i = 0; i < gp->info.num_bindings; i++) {
        const spirv_glsl_binding *b = &gp->info.bindings[i];
        void *host = NULL;
        size_t sz = 0;
        if (b->kind != SPIRV_RES_STORAGE_BUFFER)
            continue;
        if (b->binding >= VKCLIENT_MAX_ARGS)
            return cpu_dispatch(cb, dc, gx, gy, gz);
        if (gpu_find_ssbo(dc, b->set, b->binding_src, &host, &sz) != 0)
            return cpu_dispatch(cb, dc, gx, gy, gz);   /* unresolvable: CPU replay */
        arg_host[b->binding] = host;
        arg_size[b->binding] = (uint32_t)sz;
        if (b->binding + 1u > nargs)
            nargs = b->binding + 1u;
    }

    /* the transpiled subset carries no scalar uniforms (UBO/push are declined),
     * so the reflected blob is normally empty; if the backend still reserved a
     * span, send it zero-filled to satisfy the daemon's size check. */
    if (gp->unif_dwords > 0u) {
        unif = (uint32_t *)calloc(gp->unif_dwords, sizeof(uint32_t));
        if (!unif)
            return cpu_dispatch(cb, dc, gx, gy, gz);
        num_unif = gp->unif_dwords;
    }

    wg[0] = gx; wg[1] = gy; wg[2] = gz;
    rc = vk_kernel_dispatch(gp->handle, wg, gp->info.local_size[0],
                            arg_host, arg_size, nargs, unif, num_unif);
    free(unif);

    if (rc == VKCLIENT_OK)
        return VK_SUCCESS;
    /* No device-wide failure latch: a later dispatch must be able to retry the
     * GPU.  Terminal staging is retained by vkclient until process exit. */
    return cpu_dispatch(cb, dc, gx, gy, gz);
}

/* ---- backend: graphics draw (always declined to the software rasterizer) -- */

static VkResult gpu_draw(vk_command_buffer *cb, vk_draw_ctx *dc,
                         uint32_t vertexCount, uint32_t firstVertex,
                         uint32_t instanceCount, uint32_t firstInstance,
                         const uint32_t *index_data, VkIndexType index_type)
{
    /* See the file header: the V3D compiled geom-raster path runs fixed vendored
     * kernels, not arbitrary user VS/FS, so a general Vulkan graphics pipeline
     * cannot be offloaded faithfully.  Decline to the (correct) CPU rasterizer. */
    return cpu_draw(cb, dc, vertexCount, firstVertex,
                    instanceCount, firstInstance, index_data, index_type);
}

/* ---- backend: pipeline release ------------------------------------------- */

static void gpu_pipeline_free(vk_pipeline *p)
{
    gpu_pipeline *gp;

    if (!p)
        return;
    gp = (gpu_pipeline *)p->backend_data;
    if (gp) {
        if (gp->handle >= 0)
            vk_program_free(gp->handle);
        free(gp->bindings);
        free(gp);
    }
    p->backend_data = NULL;
}

/* ---- the vtable ---------------------------------------------------------- */
const vk_device_backend vk_backend_gpu = {
    "gpu",
    gpu_dispatch,
    gpu_draw,
    gpu_pipeline_free
};
