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
 * creates successfully and simply runs on the CPU.  The compiled result is ALSO
 * persisted across launches (gpu_cache.c), keyed on the generated GLSL, so a
 * relaunch re-uploads the previous blob instead of re-running the mesa compile.
 *
 * Everything outside the GPU subset is DECLINED, never mis-compiled: a declined
 * dispatch (an unsupported construct, a compiler rejection, an unresolvable
 * binding, or a negative daemon result) runs transparently on the CPU back-end,
 * so registering the GPU device can only ever add speed, never change results.
 *
 * Graphics draws take the same route through a generated two-pass geometry
 * rasterizer: on the first draw gpu_prepare_gfx() feeds the pipeline's VS (+FS)
 * to spirv_to_geom_kernels(), which emits TWO GLSL ES 3.10 compute kernels (pass
 * 1 vertex-transform -> vout, pass 2 raster -> fb) that reproduce cpu_raster.c
 * pixel-exactly, compiles both on-device, serializes them into one geom blob
 * (geom_kind 5) and uploads it.  gpu_draw() then gates the draw against the
 * supported subset, gathers the vertex-attribute float stream + FS samplers,
 * packs the per-draw uniform blob and dispatches through vk_gfx_draw.
 *
 * Everything outside that subset is DECLINED, never mis-rendered: a construct
 * the transpiler cannot express, an unsupported pipeline/draw state (non
 * TRIANGLE_LIST, blend, a depth func other than LESS+write, a non-float vertex
 * format, an indexed draw whose VS reads gl_VertexID, ...), an unresolvable
 * binding, or a negative daemon result all fall back to cpu_draw.  Registering
 * the GPU device therefore can only ever add speed, never change pixels.
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
#include "gpu_cache.h"

#include <pglcompiler.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

    /* ---- graphics (two-pass geom rasterizer, geom_kind 5) ----
     * A graphics pipeline compiles TWO compute kernels (pass 1 xform -> vout,
     * pass 2 raster -> fb) generated from its VS/FS by spirv_to_geom_kernels(),
     * serialized into ONE blob whose per-draw uniform region is the concatenation
     * [xform | raster] split at xf_dwords.  glayout carries the attribute/varying
     * stream layout and the FS sampler (set,binding) list the draw resolves. */
    int                 gfx;          /* 1: prepared as a graphics pipeline    */
    spirv_geom_layout   glayout;
    uint32_t            xf_dwords;    /* xform (cs2) uniform region = split     */
    uint32_t            rs_dwords;    /* raster (cs) uniform region             */
    struct pglc_uniform_binding *xf_bindings;   /* xform uniform reflection     */
    uint32_t            nxf_bindings;
    struct pglc_uniform_binding *rs_bindings;   /* raster uniform reflection    */
    uint32_t            nrs_bindings;
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

/* Physical-device identification for vkinfo.  The transport is shared, but the
 * compiler/backend and silicon are machine-specific; do not label Adreno as V3D. */
const char *vk_gpu_name(void)
{
    return vkclient_gpu_version() == 690u ?
           "EwokOS GPU (Adreno 690)" : "EwokOS GPU (V3D)";
}

/* ---- SSBO resolution ----------------------------------------------------- */

/* Resolve one storage-buffer descriptor write to its host store.  Mirrors the
 * CPU back-end's resolve_binding for the STORAGE_BUFFER case only.  Returns 0 on
 * success, -1 when the (set,binding) is not written or cannot be resolved to a
 * host pointer (which declines the dispatch to the CPU rather than mis-binding). */
static int gpu_find_ssbo(const vk_draw_ctx *dc, uint32_t set, uint32_t binding,
                         void **host, size_t *size, int *shm)
{
    uint32_t s, i;
    *shm = 0;
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
            /* The daemon's SSBO address is the segment base.  A zero-offset
             * binding can use that segment directly; suballocations retain the
             * staging path because g2d_canvas_t has no byte-offset field. */
            if (b->buffer->mem->gfx_shm > 0 &&
                b->buffer->offset == 0 && b->offset == 0)
                *shm = b->buffer->mem->gfx_shm;
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
    vk_gpu_cache_rec *rec;
    uint32_t blobsz, gpu_ver;
    uint64_t key;
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
    if (!e || !mod) {
        fprintf(stderr, "vulkan-gpu: prepare decline: no GLCompute entry/module\n");
        GPU_DECLINE();
    }

    /* 1. transpile to GLSL ES 3.10 compute (NULL => outside the GPU subset). */
    glsl = spirv_to_glsl((spirv_module *)mod, e, &gp->info, err, sizeof(err));
    if (!glsl) {
        fprintf(stderr, "vulkan-gpu: '%s' transpile DECLINED: %.200s\n",
                e->name, err[0] ? err : "(no reason)");
        GPU_DECLINE();
    }

    /* only a 1-D local shape maps onto the V3D CSD workgroup dispatch exactly. */
    if (gp->info.local_size[1] != 1u || gp->info.local_size[2] != 1u ||
        gp->info.local_size[0] == 0u || gp->info.local_size[0] > 256u) {
        fprintf(stderr, "vulkan-gpu: '%s' local shape (%u,%u,%u) not 1-D offloadable\n",
                e->name, gp->info.local_size[0], gp->info.local_size[1],
                gp->info.local_size[2]);
        free(glsl);
        GPU_DECLINE();
    }

    /* 1b. disk cache.  A previous launch already compiled this exact generated
     *     GLSL for this silicon, so its serialized blob can go straight to the
     *     daemon.  The transpile above still ran - the generated text IS the
     *     key, which is what makes the record self-invalidating when the
     *     transpiler changes - but the mesa compile and the serialize, which
     *     dominate the prepare, are skipped.  A record whose blob the daemon
     *     rejects (a wire-format change that did not bump the backend's
     *     pglc_blob_format_version()) is discarded rather than fatal: control
     *     falls through to the real compile below, whose store overwrites it. */
    gpu_ver = vkclient_gpu_version();
    key = vk_gpu_cache_key(glsl, NULL);
    rec = vk_gpu_cache_load(gpu_ver, VK_GPU_CACHE_COMPUTE, key);
    if (rec) {
        handle = vk_program_upload(rec->blob, rec->blob_size);
        if (handle >= 0) {
            gp->info        = rec->info;
            gp->unif_dwords = rec->a_dwords;
            gp->bindings    = rec->a_bindings;   /* stolen from the record */
            rec->a_bindings = NULL;
            gp->nbindings   = rec->a_nbindings;
            vk_gpu_cache_rec_free(rec);
            free(glsl);
            gp->handle   = handle;
            gp->prepared = 1;
            return gp;
        }
        fprintf(stderr, "vulkan-gpu: '%s' cached blob UPLOAD failed (handle=%d): recompiling\n",
                e->name, (int)handle);
        vk_gpu_cache_rec_free(rec);
    }

    /* 2. compile + link with the on-device mesa backend. */
    memset(&res, 0, sizeof(res));
    {
        bool ok = pglc_compile_compute_raw(glsl, &res);
        if (!ok || !res.success ||
            res.stage[PGLC_STAGE_COMPUTE].code_words == 0) {
            const char *gs = glsl;
            int gln = 1;
            fprintf(stderr, "vulkan-gpu: '%s' pglc compile FAILED (ok=%d success=%d words=%u): %.400s\n",
                    e->name, (int)ok, (int)res.success,
                    res.stage[PGLC_STAGE_COMPUTE].code_words,
                    res.log[0] ? res.log : "(no log)");
            fprintf(stderr, "---- generated GLSL for '%s' ----\n", e->name);
            while (gs && *gs) {
                const char *nl = strchr(gs, '\n');
                fprintf(stderr, "%3d: %.*s\n", gln++,
                        nl ? (int)(nl - gs) : (int)strlen(gs), gs);
                if (!nl) break;
                gs = nl + 1;
            }
            fprintf(stderr, "---- end GLSL ----\n");
            free(glsl);
            pglc_program_free(&res);
            GPU_DECLINE();
        }
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
        fprintf(stderr, "vulkan-gpu: '%s' serialize size query returned 0\n", e->name);
        pglc_program_free(&res);
        GPU_DECLINE();
    }
    blob = malloc(blobsz);
    if (!blob) {
        fprintf(stderr, "vulkan-gpu: '%s' oom allocating %u byte blob\n", e->name, blobsz);
        pglc_program_free(&res);
        GPU_DECLINE();
    }
    if (pglc_program_serialize(&res, NULL, &opts, blob, blobsz) == 0) {
        fprintf(stderr, "vulkan-gpu: '%s' serialize fill failed\n", e->name);
        free(blob);
        pglc_program_free(&res);
        GPU_DECLINE();
    }

    handle = vk_program_upload(blob, blobsz);
    /* Persist only a blob the daemon actually accepted: caching one it rejected
     * would make every later launch replay the rejection. */
    if (handle >= 0)
        vk_gpu_cache_store_compute(gpu_ver, key, &gp->info, gp->unif_dwords,
                                   gp->bindings, gp->nbindings, blob, blobsz);
    free(blob);
    pglc_program_free(&res);

    if (handle < 0) {
        fprintf(stderr, "vulkan-gpu: '%s' UPLOAD failed (handle=%d blobsz=%u)\n",
                e->name, (int)handle, blobsz);
        GPU_DECLINE();
    }

    gp->handle = handle;
    gp->prepared = 1;
    return gp;

#undef GPU_DECLINE
}

/* ---- graphics helpers ---------------------------------------------------- */

/* Bit-pattern of a float, for packing into the u32 uniform blob (a float
 * uniform is stored as its IEEE bits, an int as its two's-complement value). */
static uint32_t gfx_f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* Clamp a [0,1] float to a UNORM byte, matching cpu_raster.c's f2u8 / cmdexec's
 * cc_u8 so the GPU-side clear colour equals the host clear the executor wrote. */
static uint8_t gfx_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

/* Write nvals dwords into the packed per-draw uniform blob at the reflected
 * offset of `name` within one stage's region (base = that region's start in the
 * concatenated [xform | raster] blob).  A uniform the linker dead-stripped (the
 * kernel never reads it) is simply absent from the reflection table, which is
 * not an error: the value is unused, so skipping it is correct. */
static void gfx_pack(const struct pglc_uniform_binding *bs, uint32_t n,
                     uint32_t base, uint32_t *blob, const char *name,
                     const uint32_t *vals, uint32_t nvals)
{
    uint32_t i, k, span;
    for (i = 0; i < n; i++) {
        if (strcmp(bs[i].name, name) != 0)
            continue;
        span = bs[i].num_components < nvals ? bs[i].num_components : nvals;
        for (k = 0; k < span; k++)
            blob[base + bs[i].dword_offset + k] = vals[k];
        return;
    }
}

/* Deep-copy one compiled stage's uniform reflection table (name -> blob offset). */
static int gfx_copy_bindings(struct pglc_uniform_binding **dst, uint32_t *n,
                             const struct pglc_stage *st)
{
    *dst = NULL;
    *n = 0;
    if (st->num_uniform_bindings && st->uniform_bindings) {
        size_t nb = (size_t)st->num_uniform_bindings * sizeof(struct pglc_uniform_binding);
        *dst = (struct pglc_uniform_binding *)malloc(nb);
        if (!*dst)
            return -1;
        memcpy(*dst, st->uniform_bindings, nb);
        *n = st->num_uniform_bindings;
    }
    return 0;
}

/* Resolve one combined-image-sampler descriptor write to its image view + sampler
 * (the graphics twin of gpu_find_ssbo).  Returns 0 on success, -1 when the
 * (set,binding) is not written as a sampled image (which declines the draw rather
 * than sampling an unbound unit). */
static int gpu_find_sampler(const vk_draw_ctx *dc, uint32_t set, uint32_t binding,
                            vk_image_view **view, vk_sampler **smp)
{
    uint32_t s, i;
    for (s = 0; s < dc->set_count; s++) {
        const vk_descriptor_set *ds = dc->sets[s];
        uint32_t set_index = dc->first_set + s;
        if (!ds || set_index != set)
            continue;
        for (i = 0; i < ds->num_bindings; i++) {
            const vk_descriptor_binding *b = &ds->bindings[i];
            if (b->binding != binding)
                continue;
            if (b->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || !b->view)
                return -1;
            *view = b->view;
            *smp = b->sampler;
            return 0;
        }
    }
    return -1;
}

/* Resolve one uniform-buffer descriptor write to its host store (the UBO twin of
 * gpu_find_ssbo, used to source a flattened UBO member).  Returns 0 on success,
 * -1 when the (set,binding) is not written as a UNIFORM_BUFFER. */
static int gpu_find_ubo(const vk_draw_ctx *dc, uint32_t set, uint32_t binding,
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
            if (b->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || !b->buffer)
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

/* Non-zero when a stage's reflected uniform table contains `name` (the linker
 * kept it).  A flattened UBO/push member that was dead-stripped from BOTH stages
 * needs no host value, so the draw does not decline when its buffer is unbound. */
static int gfx_reflected(const struct pglc_uniform_binding *bs, uint32_t n,
                         const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (strcmp(bs[i].name, name) == 0)
            return 1;
    return 0;
}

/* Decode one vertex attribute component set to floats.  The GPU attribute stream
 * is plain float[], so every format must be widened here.  This MUST stay in
 * lock-step with cpu_raster.c decode_attr: an attribute format the GPU path
 * decoded differently (or not at all) would silently mis-render on the GPU while
 * the CPU fallback looked correct -- e.g. ImGui's packed R8G8B8A8_UNORM vertex
 * colour fell through to the {0,0,0,1} default and drew a solid black box.  An
 * unsupported format leaves the {0,0,0,1} default exactly like the CPU. */
static void gfx_decode_attr(VkFormat f, const uint8_t *p, float *out)
{
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (f) {
    case VK_FORMAT_R32_SFLOAT:
        out[0] = *(const float *)p; break;
    case VK_FORMAT_R32G32_SFLOAT:
        out[0] = ((const float *)p)[0]; out[1] = ((const float *)p)[1]; break;
    case VK_FORMAT_R32G32B32_SFLOAT:
        out[0] = ((const float *)p)[0]; out[1] = ((const float *)p)[1];
        out[2] = ((const float *)p)[2]; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        out[0] = ((const float *)p)[0]; out[1] = ((const float *)p)[1];
        out[2] = ((const float *)p)[2]; out[3] = ((const float *)p)[3]; break;
    case VK_FORMAT_R8_UNORM:
        out[0] = p[0] / 255.0f; break;
    case VK_FORMAT_R8G8_UNORM:
        out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f; break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f; out[3] = p[3] / 255.0f; break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f;
        out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f; break;
    case VK_FORMAT_R32_UINT:
        out[0] = (float)*(const uint32_t *)p; break;
    case VK_FORMAT_R32G32_UINT:
        out[0] = (float)((const uint32_t *)p)[0];
        out[1] = (float)((const uint32_t *)p)[1]; break;
    case VK_FORMAT_R32G32B32_UINT:
        out[0] = (float)((const uint32_t *)p)[0];
        out[1] = (float)((const uint32_t *)p)[1];
        out[2] = (float)((const uint32_t *)p)[2]; break;
    case VK_FORMAT_R32G32B32A32_UINT:
        out[0] = (float)((const uint32_t *)p)[0];
        out[1] = (float)((const uint32_t *)p)[1];
        out[2] = (float)((const uint32_t *)p)[2];
        out[3] = (float)((const uint32_t *)p)[3]; break;
    case VK_FORMAT_R16_SFLOAT: {
        /* half -> float */
        uint16_t h = *(const uint16_t *)p;
        uint32_t sign = (uint32_t)(h & 0x8000) << 16;
        uint32_t exp = (h >> 10) & 0x1f, man = h & 0x3ff;
        uint32_t f32;
        if (exp == 0) f32 = sign;                    /* flush denormals to 0 */
        else if (exp == 31) f32 = sign | 0x7f800000 | (man << 13);
        else f32 = sign | ((exp + 112) << 23) | (man << 13);
        memcpy(&out[0], &f32, 4);
        break;
    }
    default:
        break;   /* unsupported: leave the {0,0,0,1} default (matches the CPU) */
    }
}

/* ---- lazy per-pipeline graphics prepare ---------------------------------- */

/* Generate + compile + serialize + upload the pipeline's two-pass geom kernels
 * once (the graphics twin of gpu_prepare).  Any decline sets cpu_fallback and
 * STILL returns a valid gpu_pipeline, so gpu_draw transparently replays on the
 * CPU rasterizer. */
static gpu_pipeline *gpu_prepare_gfx(vk_pipeline *p)
{
    gpu_pipeline *gp;
    const spirv_module *vmod = NULL, *fmod = NULL;
    const spirv_entry *ve = NULL, *fe = NULL;
    spirv_geom_kernels kern;
    struct pglc_program res_x, res_r;
    struct pglc_blob_opts opts;
    struct pglc_stage *sx, *sr;
    vk_gpu_cache_rec *rec;
    char err[256];
    uint32_t i, blobsz, gpu_ver;
    uint64_t key;
    void *blob = NULL;
    int32_t handle;

    if (p->backend_data)
        return (gpu_pipeline *)p->backend_data;

    gp = (gpu_pipeline *)calloc(1, sizeof(*gp));
    if (!gp)
        return NULL;
    gp->handle = -1;
    p->backend_data = gp;

#define GFX_DECLINE() do { gp->cpu_fallback = 1; gp->prepared = 1; return gp; } while (0)
#define GFX_CLEANUP() do { \
        free(kern.xform); free(kern.raster); \
        pglc_program_free(&res_x); pglc_program_free(&res_r); \
        free(blob); \
    } while (0)

    memset(&kern, 0, sizeof(kern));
    memset(&res_x, 0, sizeof(res_x));
    memset(&res_r, 0, sizeof(res_r));

    /* locate the VS/FS entries exactly like cpu_draw does */
    for (i = 0; i < p->num_stages; i++) {
        if (p->stages[i].stage == VK_SHADER_STAGE_VERTEX_BIT) {
            if (p->stages[i].module && p->stages[i].module->module) {
                vmod = p->stages[i].module->module;
                ve = spirv_find_entry(vmod,
                        p->stages[i].entry[0] ? p->stages[i].entry : NULL,
                        SPIRV_EXEC_VERTEX);
            }
        } else if (p->stages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            if (p->stages[i].module && p->stages[i].module->module) {
                fmod = p->stages[i].module->module;
                fe = spirv_find_entry(fmod,
                        p->stages[i].entry[0] ? p->stages[i].entry : NULL,
                        SPIRV_EXEC_FRAGMENT);
            }
        }
    }
    if (!vmod || !ve) {
        fprintf(stderr, "vulkan-gpu: gfx prepare decline: no usable vertex stage\n");
        GFX_DECLINE();   /* no vertex shader: nothing to offload */
    }

    /* 1. generate the two GLSL ES 3.10 compute kernels from VS (+ optional FS). */
    if (spirv_to_geom_kernels((spirv_module *)vmod, ve, (spirv_module *)fmod, fe,
                              &kern, err, sizeof(err)) != 0) {
        fprintf(stderr, "vulkan-gpu: geom kernel generation DECLINED: %.200s\n",
                err[0] ? err : "(no reason)");
        GFX_DECLINE();
    }

    /* 1b. disk cache (see gpu_prepare for the staleness argument).  The key
     *     covers BOTH generated kernels, so a record is served only when the
     *     xform and the raster source are each byte-identical to what was
     *     compiled before.  This is the most expensive prepare in the whole
     *     back-end - it pays the mesa compile twice, and the raster kernel is a
     *     large generated source (the full rasterizer harness plus the user's
     *     fragment body inlined into it) - so it is also where the cache saves
     *     the most. */
    gpu_ver = vkclient_gpu_version();
    key = vk_gpu_cache_key(kern.xform, kern.raster);
    rec = vk_gpu_cache_load(gpu_ver, VK_GPU_CACHE_GFX, key);
    if (rec) {
        handle = vk_gfx_program_upload(rec->blob, rec->blob_size);
        if (handle >= 0) {
            gp->glayout      = rec->glayout;
            gp->xf_dwords    = rec->a_dwords;
            gp->rs_dwords    = rec->b_dwords;
            gp->xf_bindings  = rec->a_bindings;   /* stolen from the record */
            rec->a_bindings  = NULL;
            gp->nxf_bindings = rec->a_nbindings;
            gp->rs_bindings  = rec->b_bindings;   /* stolen from the record */
            rec->b_bindings  = NULL;
            gp->nrs_bindings = rec->b_nbindings;
            vk_gpu_cache_rec_free(rec);
            GFX_CLEANUP();   /* res_x/res_r are still zeroed, blob still NULL */
            gp->gfx      = 1;
            gp->handle   = handle;
            gp->prepared = 1;
            return gp;
        }
        fprintf(stderr, "vulkan-gpu: cached gfx blob UPLOAD failed (handle=%d): recompiling\n",
                (int)handle);
        vk_gpu_cache_rec_free(rec);
    }

    /* 2. compile both with the on-device mesa backend (pass 1 = xform, pass 2 =
     *    raster).  Each lands in its own pglc_program's COMPUTE stage. */
    if (!pglc_compile_compute_raw(kern.xform, &res_x) || !res_x.success ||
        res_x.stage[PGLC_STAGE_COMPUTE].code_words == 0) {
        fprintf(stderr, "vulkan-gpu: XFORM kernel pglc compile FAILED: %.400s\n",
                res_x.log[0] ? res_x.log : "(no log)");
        GFX_CLEANUP();
        GFX_DECLINE();
    }
    if (!pglc_compile_compute_raw(kern.raster, &res_r) || !res_r.success ||
        res_r.stage[PGLC_STAGE_COMPUTE].code_words == 0) {
        fprintf(stderr, "vulkan-gpu: RASTER kernel pglc compile FAILED: %.400s\n",
                res_r.log[0] ? res_r.log : "(no log)");
        GFX_CLEANUP();
        GFX_DECLINE();
    }

    sx = &res_x.stage[PGLC_STAGE_COMPUTE];
    sr = &res_r.stage[PGLC_STAGE_COMPUTE];
    gp->glayout   = kern.layout;
    gp->xf_dwords = sx->uniform_blob_dwords;   /* the daemon's blob split point */
    gp->rs_dwords = sr->uniform_blob_dwords;
    if (gfx_copy_bindings(&gp->xf_bindings, &gp->nxf_bindings, sx) != 0 ||
        gfx_copy_bindings(&gp->rs_bindings, &gp->nrs_bindings, sr) != 0) {
        GFX_CLEANUP();
        GFX_DECLINE();
    }

    /* 3. serialize (main = raster/cs, xform = cs2) with geom_kind 5, the raster
     *    grid shape the transpiler chose, and the VERSION-6 stream strides.  The
     *    raster kernel is emitted in one of two shapes (see spirv_to_glsl.h):
     *    a NON-TEXTURED (gouraud) FS is ONE-LANE-PER-TRIANGLE, so geom_tpw = 256
     *    (the 16x16 lane count) and the daemon dispatches ceil(ntris/256) wide
     *    workgroups; a TEXTURED FS (imgui) is COOPERATIVE one-workgroup-per-
     *    triangle, so geom_tpw = 0 and the daemon dispatches ntris 8x8 workgroups
     *    whose 64 lanes tile each triangle's bbox.  The wide grid is the whole
     *    perf point for a mesh (the CSD charges ~3us of fixed overhead PER
     *    workgroup); the cooperative shape is the perf point for the few huge
     *    triangles of a GUI overlay.  A nonzero geom_tpw MUST equal the raster
     *    local_size product or the daemon declines the program -- the transpiler
     *    guarantees both.  A VS with no inputs reports attr_stride 0; the blob
     *    field must be >= 1 (the daemon validates [1,64]), and the generated
     *    kernel reads no vattr in that case, so a 1-float dummy stride is staged. */
    memset(&opts, 0, sizeof(opts));
    opts.geom_kind   = 5;
    opts.geom_tpw    = kern.layout.geom_tpw;
    opts.vout_stride = kern.layout.vout_stride;
    opts.attr_stride = kern.layout.attr_stride ? kern.layout.attr_stride : 1u;
    blobsz = pglc_program_serialize(&res_r, &res_x, &opts, NULL, 0);
    if (blobsz == 0) {
        GFX_CLEANUP();
        GFX_DECLINE();
    }
    blob = malloc(blobsz);
    if (!blob) {
        GFX_CLEANUP();
        GFX_DECLINE();
    }
    if (pglc_program_serialize(&res_r, &res_x, &opts, blob, blobsz) == 0) {
        GFX_CLEANUP();
        GFX_DECLINE();
    }

    handle = vk_gfx_program_upload(blob, blobsz);
    /* Persist only a blob the daemon accepted (see gpu_prepare). */
    if (handle >= 0)
        vk_gpu_cache_store_gfx(gpu_ver, key, &gp->glayout,
                               gp->xf_dwords, gp->xf_bindings, gp->nxf_bindings,
                               gp->rs_dwords, gp->rs_bindings, gp->nrs_bindings,
                               blob, blobsz);
    GFX_CLEANUP();

    if (handle < 0)
        GFX_DECLINE();

    gp->gfx      = 1;
    gp->handle   = handle;
    gp->prepared = 1;
    return gp;

#undef GFX_CLEANUP
#undef GFX_DECLINE
}

/* ---- backend: compute dispatch ------------------------------------------- */

static VkResult gpu_dispatch(vk_command_buffer *cb, vk_draw_ctx *dc,
                             uint32_t gx, uint32_t gy, uint32_t gz)
{
    vk_pipeline *p = dc->pipeline;
    gpu_pipeline *gp;
    void    *arg_host[VKCLIENT_MAX_ARGS];
    uint32_t arg_size[VKCLIENT_MAX_ARGS];
    int      arg_shm[VKCLIENT_MAX_ARGS];
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
    memset(arg_shm, 0, sizeof(arg_shm));
    for (i = 0; i < gp->info.num_bindings; i++) {
        const spirv_glsl_binding *b = &gp->info.bindings[i];
        void *host = NULL;
        size_t sz = 0;
        if (b->kind != SPIRV_RES_STORAGE_BUFFER)
            continue;
        if (b->binding >= VKCLIENT_MAX_ARGS) {
            fprintf(stderr, "vulkan-gpu: dispatch fallback: binding %u >= VKCLIENT_MAX_ARGS\n", b->binding);
            return cpu_dispatch(cb, dc, gx, gy, gz);
        }
        if (gpu_find_ssbo(dc, b->set, b->binding_src, &host, &sz,
                          &arg_shm[b->binding]) != 0) {
            fprintf(stderr, "vulkan-gpu: dispatch fallback: SSBO (set=%u binding=%u) unresolvable\n", b->set, b->binding_src);
            return cpu_dispatch(cb, dc, gx, gy, gz);   /* unresolvable: CPU replay */
        }
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
                            arg_host, arg_size, arg_shm, nargs, unif, num_unif);
    free(unif);

    if (rc == VKCLIENT_OK)
        return VK_SUCCESS;
    /* No device-wide failure latch: a later dispatch must be able to retry the
     * GPU.  Terminal staging is retained by vkclient until process exit. */
    fprintf(stderr, "vulkan-gpu: dispatch fallback: vk_kernel_dispatch rc=%d (handle=%d nargs=%u wg=%u,%u,%u ls=%u)\n",
            (int)rc, (int)gp->handle, nargs, wg[0], wg[1], wg[2], gp->info.local_size[0]);
    return cpu_dispatch(cb, dc, gx, gy, gz);
}

/* ---- backend: graphics draw (two-pass geom rasterizer, geom_kind 5) ------- */

/* Offload a graphics draw to the V3D two-pass geometry rasterizer when the
 * pipeline + draw state fall inside the supported subset; otherwise decline to
 * the (always-correct) CPU rasterizer.  The subset mirrors the feature face the
 * generated kernels reproduce pixel-exactly (see spirv_to_glsl.h): TRIANGLE_LIST,
 * a single RGBA8/BGRA8 colour attachment, <= 1 instance, no blend, depth off or
 * LESS+write, any viewport/scissor, <= 4 set-0 combined-image samplers.
 *
 * Everything the kernels cannot reproduce faithfully is DECLINED, never
 * mis-rendered: an unsupported construct (the transpiler declines it), an
 * unresolvable binding, an indexed draw whose VS reads gl_VertexID, a non-float
 * vertex format the stream cannot carry, or a negative daemon result all fall
 * back to cpu_draw, so enabling the GPU can only add speed, never change pixels.
 *
 * Depth model (v1): the raster kernel arbitrates depth on vkclient's PRIVATE
 * z canvas (24-bit unorm atomicMin, nearest-wins), NOT the host depth
 * attachment, and that canvas is neither seeded from nor written back to the
 * attachment.  A depth-enabled draw therefore requires the render pass to have
 * cleared colour and depth-to-1.0 (so the frame's first GPU draw's first_frame
 * fill reproduces both), and assumes the whole pass runs on the GPU; reading the
 * depth attachment after the pass, or interleaving a CPU draw in a depth pass,
 * are documented v1 boundaries that decline or stay consistent per-pass. */
static VkResult gpu_draw(vk_command_buffer *cb, vk_draw_ctx *dc,
                         uint32_t vertexCount, uint32_t firstVertex,
                         uint32_t instanceCount, uint32_t firstInstance,
                         const uint32_t *index_data, VkIndexType index_type)
{
    vk_pipeline *p = dc->pipeline;
    gpu_pipeline *gp;
    vk_image *cim;
    void *fb_host;
    float *attr = NULL;
    uint32_t *unif = NULL;
    vk_gfx_tex tex[VKCLIENT_MAX_TEX];
    VkViewport vp;
    uint32_t cbpp, rt_w, rt_h, fb_stride, ntris, nverts, astride;
    uint32_t num_unif, ntex = 0, k, j, cull, frontcw, fbswz, blend_mode = 0, clear_rgba = 0;
    int bgra, depth_on, have_depth, first_frame = 0;
    float lox, loy, hix, hiy;
    int cx0, cy0, cx1, cy1, scx, scy, scw, sch;
    float clear_z = 1.0f;
    int32_t rc;

#define GFX_CPU() do { free(unif); \
        return cpu_draw(cb, dc, vertexCount, firstVertex, instanceCount, \
                        firstInstance, index_data, index_type); } while (0)

    if (!p)
        return VK_ERROR_INITIALIZATION_FAILED;

    /* ---- static eligibility gate (pipeline + draw state) ---- */
    if (p->bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS)  GFX_CPU();
    if (p->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) GFX_CPU();
    if (instanceCount != 1u)                               GFX_CPU();
    if (p->rasterizer_discard)                             GFX_CPU();
    if (p->blend_enable) {
        /* Only ImGui's canonical SRC_ALPHA/ONE_MINUS_SRC_ALPHA (colour) +
         * ONE/ONE_MINUS_SRC_ALPHA (alpha), both ADD, maps to raster kernel
         * blend mode 1; any other factor/op combination still declines to the
         * (correct) CPU replay. */
        if (p->color_op  == VK_BLEND_OP_ADD &&
            p->src_color == VK_BLEND_FACTOR_SRC_ALPHA &&
            p->dst_color == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
            p->alpha_op  == VK_BLEND_OP_ADD &&
            p->src_alpha == VK_BLEND_FACTOR_ONE &&
            p->dst_alpha == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            blend_mode = 1u;
        else
            GFX_CPU();
    }

    /* single RGBA8/BGRA8 colour attachment (the only formats the kernel packs) */
    if (dc->num_color != 1u || !dc->color[0] || !dc->color[0]->image)
        GFX_CPU();
    cim = dc->color[0]->image;
    if (cim->format != VK_FORMAT_R8G8B8A8_UNORM && cim->format != VK_FORMAT_R8G8B8A8_SRGB &&
        cim->format != VK_FORMAT_B8G8R8A8_UNORM && cim->format != VK_FORMAT_B8G8R8A8_SRGB)
        GFX_CPU();
    bgra = (cim->format == VK_FORMAT_B8G8R8A8_UNORM ||
            cim->format == VK_FORMAT_B8G8R8A8_SRGB) ? 1 : 0;
    fbswz = bgra ? 1u : 0u;   /* kernel fb byte order selector */

    cbpp = cim->bpp ? cim->bpp : vk_format_bpp(cim->format);
    if (cbpp != 4u)            GFX_CPU();
    fb_host = vk_image_ptr(cim);
    if (!fb_host)              GFX_CPU();

    /* render-target size (setup_rt semantics: image extent, overridden by the
     * recorded rt size); host row pitch is cbpp * image width, matching the CPU
     * rasterizer's cpitch so both address the attachment identically. */
    rt_w = cim->extent.width;
    rt_h = cim->extent.height;
    if (dc->rt_width)  rt_w = dc->rt_width;
    if (dc->rt_height) rt_h = dc->rt_height;
    if (!rt_w || !rt_h)        GFX_CPU();
    fb_stride = cbpp * cim->extent.width;
    if (fb_stride < rt_w * 4u) GFX_CPU();

    /* depth: the kernel arbitrates strict LESS + write, or is off.  A depth
     * attachment the CPU would actually test against (depth_test_enable && a
     * resident depth image) must be LESS + write to offload; anything else
     * declines.  depth_test_enable with NO attachment is a no-op on the CPU too,
     * so it maps to the kernel's depth-off path. */
    have_depth = (dc->depth && dc->depth->image &&
                  vk_image_ptr(dc->depth->image)) ? 1 : 0;
    depth_on = (p->depth_test_enable && have_depth) ? 1 : 0;
    if (depth_on && (p->depth_compare != VK_COMPARE_OP_LESS || !p->depth_write_enable))
        GFX_CPU();

    /* triangle count: the CPU draws floor(vertexCount/3) triangles, ignoring a
     * trailing partial, so stage exactly ntris*3 vertices. */
    ntris = vertexCount / 3u;
    if (ntris == 0u)           GFX_CPU();   /* nothing to draw (CPU is a no-op) */
    nverts = ntris * 3u;
    if (nverts > VKCLIENT_MAX_VERTS) GFX_CPU();

    /* ---- lazily generate + compile + upload the two kernels ---- */
    gp = gpu_prepare_gfx(p);
    if (!gp || !gp->gfx || gp->cpu_fallback || gp->handle < 0)
        GFX_CPU();

    /* an indexed draw re-bases vertices to 0..n-1 in the staged stream, so a VS
     * that reads gl_VertexID would see the wrong index; decline it (the CPU path
     * resolves index_data faithfully). */
    if (index_data && gp->glayout.uses_vertex_id)
        GFX_CPU();

    /* ---- viewport + scissor -> the kernel's integer clip rect ----
     * The kernel clamps its bbox to the render target and (when enabled) to
     * uScissor, but NOT to a viewport sub-rect, so uScissor carries the
     * intersection of the viewport and the user scissor to reproduce the CPU's
     * clamp.  A full-target rect disables the kernel scissor (fast path). */
    vp = dc->has_vp ? dc->vp
                    : (VkViewport){ 0.0f, 0.0f, (float)rt_w, (float)rt_h, 0.0f, 1.0f };
    lox = vp.x;               loy = vp.y;
    hix = vp.x + vp.width;    hiy = vp.y + vp.height;
    if (dc->has_sc) {
        float sx0 = (float)dc->sc.offset.x, sy0 = (float)dc->sc.offset.y;
        float sx1 = sx0 + (float)dc->sc.extent.width;
        float sy1 = sy0 + (float)dc->sc.extent.height;
        if (sx0 > lox) lox = sx0;
        if (sy0 > loy) loy = sy0;
        if (sx1 < hix) hix = sx1;
        if (sy1 < hiy) hiy = sy1;
    }
    cx0 = (int)floorf(lox); if (cx0 < 0) cx0 = 0;
    cy0 = (int)floorf(loy); if (cy0 < 0) cy0 = 0;
    cx1 = (int)ceilf(hix);  if (cx1 > (int)rt_w) cx1 = (int)rt_w;
    cy1 = (int)ceilf(hiy);  if (cy1 > (int)rt_h) cy1 = (int)rt_h;
    if (cx1 <= cx0 || cy1 <= cy0)
        return VK_SUCCESS;   /* empty clip rect: the CPU also draws nothing */
    if (cx0 <= 0 && cy0 <= 0 && cx1 >= (int)rt_w && cy1 >= (int)rt_h) {
        scx = scy = scw = sch = 0;   /* covers the target: disable kernel scissor */
    } else {
        scx = cx0; scy = cy0; scw = cx1 - cx0; sch = cy1 - cy0;
    }

    /* ---- clear / first_frame decision ---- */
    if (depth_on) {
        uint8_t r8, g8, b8, a8;
        /* the private z canvas is only cleared by the frame's first GPU draw
         * (first_frame fill -> depth max), so the pass must have cleared colour
         * (reproduced by the same fill) and depth to 1.0; otherwise decline. */
        if (!dc->gfx_color_clear || !dc->gfx_depth_clear ||
            !(dc->gfx_clear_depth >= 1.0f))
            GFX_CPU();
        first_frame = dc->gfx_first_draw ? 1 : 0;
        /* Encode the clear colour for the daemon's fb fill: it writes
         * swap_RB(clear_rgba), which must equal the kernel's fb word for the
         * clear colour, i.e. clear_rgba is the OPPOSITE swizzle of the fb. */
        r8 = gfx_u8(dc->gfx_clear_color[0]);
        g8 = gfx_u8(dc->gfx_clear_color[1]);
        b8 = gfx_u8(dc->gfx_clear_color[2]);
        a8 = gfx_u8(dc->gfx_clear_color[3]);
        clear_rgba = bgra ? (((uint32_t)a8 << 24) | ((uint32_t)b8 << 16) |
                             ((uint32_t)g8 << 8)  |  (uint32_t)r8)
                          : (((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) |
                             ((uint32_t)g8 << 8)  |  (uint32_t)b8);
    }

    /* ---- gather directly into the persistent GPU attribute canvas ----
     * Resolve each VS input's pipeline binding once, not once per vertex.  The
     * gathered stream is already the vkclient draw canvas, so there is no
     * temporary heap allocation or second nverts*astride copy before dispatch. */
    astride = gp->glayout.attr_stride ? gp->glayout.attr_stride : 1u;
    attr = (float *)vk_gfx_attr_stage(nverts, astride);
    if (!attr)
        GFX_CPU();
    {
        typedef struct {
            const uint8_t *base;
            uint32_t stride, offset, comps;
            VkFormat format;
        } gfx_attr_plan;
        uint32_t nattrs = (uint32_t)gp->glayout.num_attrs;
        gfx_attr_plan plans[nattrs ? nattrs : 1u];

        for (j = 0; j < nattrs; j++) {
            const spirv_geom_slot *sl = &gp->glayout.attrs[j];
            const vk_vertex_attr *va = NULL;
            uint32_t a, bb;
            memset(&plans[j], 0, sizeof(plans[j]));
            plans[j].offset = sl->offset;
            plans[j].comps = sl->comps > 4u ? 4u : sl->comps;
            for (a = 0; a < p->num_vtx_attrs; a++)
                if (p->vtx_attrs[a].location == sl->location) {
                    va = &p->vtx_attrs[a];
                    break;
                }
            if (!va || va->binding >= VK_MAX_VERTEX_BINDINGS ||
                !dc->vb[va->binding])
                continue;   /* unwritten/unbound input stays zero */
            for (bb = 0; bb < p->num_vtx_bindings; bb++) {
                if (p->vtx_bindings[bb].binding != va->binding)
                    continue;
                if (p->vtx_bindings[bb].rate != VK_VERTEX_INPUT_RATE_VERTEX)
                    GFX_CPU();   /* per-instance needs gl_InstanceID */
                plans[j].stride = p->vtx_bindings[bb].stride;
                break;
            }
            plans[j].base = (const uint8_t *)vk_buffer_ptr(dc->vb[va->binding]);
            if (!plans[j].base)
                continue;
            plans[j].base += dc->vb_off[va->binding] + va->offset;
            plans[j].format = va->format;
        }

        /* Clear only genuinely unwritten/gapped locations; a fully covered
         * draw (including vkteapot) skips the nverts*astride memset entirely. */
        {
            uint32_t off;
            int need_zero = 0;
            for (off = 0; off < astride && !need_zero; off++) {
                uint32_t covered = 0;
                for (j = 0; j < nattrs; j++) {
                    if (plans[j].base && off >= plans[j].offset &&
                        off < plans[j].offset + plans[j].comps) {
                        covered = 1;
                        break;
                    }
                }
                if (!covered)
                    need_zero = 1;
            }
            if (need_zero)
                memset(attr, 0, (size_t)nverts * astride * sizeof(float));
        }

        for (j = 0; j < nattrs; j++) {
            const gfx_attr_plan *pl = &plans[j];
            float *dst = attr + pl->offset;
            const uint8_t *src;
            if (!pl->base)
                continue;
            if (!index_data) {
                src = pl->base + (size_t)firstVertex * pl->stride;
                if (pl->format == VK_FORMAT_R32G32B32A32_SFLOAT &&
                    pl->comps == 4u) {
                    for (k = 0; k < nverts; k++) {
                        const float *f = (const float *)src;
                        dst[0] = f[0]; dst[1] = f[1];
                        dst[2] = f[2]; dst[3] = f[3];
                        src += pl->stride;
                        dst += astride;
                    }
                } else {
                    for (k = 0; k < nverts; k++) {
                        float v[4];
                        uint32_t a;
                        gfx_decode_attr(pl->format, src, v);
                        for (a = 0; a < pl->comps; a++)
                            dst[a] = v[a];
                        src += pl->stride;
                        dst += astride;
                    }
                }
            } else if (pl->format == VK_FORMAT_R32G32B32A32_SFLOAT &&
                       pl->comps == 4u) {
                for (k = 0; k < nverts; k++) {
                    const float *f = (const float *)(pl->base +
                        (size_t)index_data[k] * pl->stride);
                    dst[0] = f[0]; dst[1] = f[1];
                    dst[2] = f[2]; dst[3] = f[3];
                    dst += astride;
                }
            } else {
                for (k = 0; k < nverts; k++) {
                    float v[4];
                    uint32_t a;
                    src = pl->base + (size_t)index_data[k] * pl->stride;
                    gfx_decode_attr(pl->format, src, v);
                    for (a = 0; a < pl->comps; a++)
                        dst[a] = v[a];
                    dst += astride;
                }
            }
        }
    }
    /* ---- resolve the FS combined-image samplers into TMU textures ---- */
    memset(tex, 0, sizeof(tex));
    for (j = 0; j < (uint32_t)gp->glayout.num_samplers; j++) {
        uint32_t bset = gp->glayout.sampler_set[j];
        uint32_t bbind = gp->glayout.sampler_binding[j];
        vk_image_view *iv = NULL;
        vk_sampler *smp = NULL;
        vk_image *tim;
        void *th;
        uint32_t tbpp, tpitch;
        if (bbind >= VKCLIENT_MAX_TEX)
            GFX_CPU();
        /* the FS samples this unit, so it MUST be bound: decline rather than let
         * the kernel read an unbound TMU (the daemon declines that too). */
        if (gpu_find_sampler(dc, bset, bbind, &iv, &smp) != 0 || !iv || !iv->image)
            GFX_CPU();
        tim = iv->image;
        if (tim->format != VK_FORMAT_R8G8B8A8_UNORM &&
            tim->format != VK_FORMAT_R8G8B8A8_SRGB)
            GFX_CPU();   /* only tightly packed RGBA8 tiles into the TMU layout */
        th = vk_image_ptr(tim);
        tbpp = tim->bpp ? tim->bpp : vk_format_bpp(tim->format);
        tpitch = tim->row_pitch ? tim->row_pitch : tbpp * tim->extent.width;
        if (!th || tbpp != 4u || tpitch != tim->extent.width * 4u ||
            tim->extent.width == 0 || tim->extent.height == 0)
            GFX_CPU();
        tex[bbind].host   = th;
        tex[bbind].w      = tim->extent.width;
        tex[bbind].h      = tim->extent.height;
        tex[bbind].linear = (smp && (smp->mag == VK_FILTER_LINEAR ||
                                     smp->min_f == VK_FILTER_LINEAR)) ? 1 : 0;
        /* Content generation of the image backing: vkclient keys its tiled-texture
         * cache on it, so an unchanged sampled image is not reconverted per draw.
         * th is non-NULL here, which implies tim->mem is non-NULL. */
        tex[bbind].version = tim->mem ? tim->mem->content_version : 0u;
        if (bbind + 1u > ntex)
            ntex = bbind + 1u;
    }

    /* ---- pack the per-draw uniform blob [xform | raster] ---- */
    num_unif = gp->xf_dwords + gp->rs_dwords;
    if (num_unif > 0u) {
        uint32_t v4[4], rb = gp->xf_dwords;
        unif = (uint32_t *)calloc(num_unif, sizeof(uint32_t));
        if (!unif)
            GFX_CPU();
        /* pass 1 xform uniforms */
        v4[0] = gfx_f2u(vp.x); v4[1] = gfx_f2u(vp.y);
        v4[2] = gfx_f2u(vp.width * 0.5f); v4[3] = gfx_f2u(vp.height * 0.5f);
        gfx_pack(gp->xf_bindings, gp->nxf_bindings, 0, unif, "uVpOffsetScale", v4, 4);
        v4[0] = gfx_f2u(vp.minDepth); v4[1] = gfx_f2u(vp.maxDepth - vp.minDepth);
        gfx_pack(gp->xf_bindings, gp->nxf_bindings, 0, unif, "uVpDepth", v4, 2);
        v4[0] = nverts;
        gfx_pack(gp->xf_bindings, gp->nxf_bindings, 0, unif, "uVertCount", v4, 1);
        v4[0] = index_data ? 0u : firstVertex;   /* gl_VertexID base */
        gfx_pack(gp->xf_bindings, gp->nxf_bindings, 0, unif, "uVertexBase", v4, 1);
        /* pass 2 raster uniforms (offset by the xform region) */
        v4[0] = gfx_f2u((float)rt_w); v4[1] = gfx_f2u((float)rt_h);
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uViewportWH", v4, 2);
        v4[0] = rt_w;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uFbStride", v4, 1);
        v4[0] = 0u;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uTriFirst", v4, 1);
        v4[0] = ntris;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uTriCount", v4, 1);
        v4[0] = (uint32_t)scx; v4[1] = (uint32_t)scy;
        v4[2] = (uint32_t)scw; v4[3] = (uint32_t)sch;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uScissor", v4, 4);
        v4[0] = depth_on ? 1u : 0u;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uDepthFunc", v4, 1);
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uDepthWrite", v4, 1);
        cull = (uint32_t)p->cull_mode & 3u;   /* NONE/FRONT/BACK/BOTH == 0/1/2/3 */
        v4[0] = cull;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uCullMode", v4, 1);
        frontcw = (p->front_face == VK_FRONT_FACE_CLOCKWISE) ? 1u : 0u;
        v4[0] = frontcw;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uFrontCW", v4, 1);
        v4[0] = blend_mode;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uBlendMode", v4, 1);
        v4[0] = fbswz;
        gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, "uFbSwizzle", v4, 1);
        /* user UBO / push-constant members (flattened by the transpiler): read
         * each from its bound host buffer / the push snapshot and write it into
         * BOTH stage regions by name (a name only one stage reflects lands there;
         * the other region's gfx_pack is a no-op).  A member dead-stripped from
         * both stages needs no value; a reflected one whose source is missing
         * declines to the CPU rather than feeding the kernel garbage. */
        for (j = 0; j < (uint32_t)gp->glayout.num_flat; j++) {
            const spirv_glsl_flat *fl = &gp->glayout.flat[j];
            const uint8_t *src = NULL;
            uint32_t vals[16];
            if (fl->comps > 16u)
                GFX_CPU();
            if (!gfx_reflected(gp->xf_bindings, gp->nxf_bindings, fl->name) &&
                !gfx_reflected(gp->rs_bindings, gp->nrs_bindings, fl->name))
                continue;   /* dead-stripped everywhere: no value required */
            if (fl->is_push) {
                if (dc->push && fl->offset + fl->comps * 4u <= dc->push_size)
                    src = dc->push + fl->offset;
            } else {
                void *uh = NULL;
                size_t usz = 0;
                if (gpu_find_ubo(dc, fl->set, fl->binding, &uh, &usz) == 0 &&
                    fl->offset + fl->comps * 4u <= usz)
                    src = (const uint8_t *)uh + fl->offset;
            }
            if (!src)
                GFX_CPU();   /* reflected but unresolvable: the CPU replay is correct */
            memcpy(vals, src, (size_t)fl->comps * 4u);
            gfx_pack(gp->xf_bindings, gp->nxf_bindings, 0, unif, fl->name, vals, fl->comps);
            gfx_pack(gp->rs_bindings, gp->nrs_bindings, rb, unif, fl->name, vals, fl->comps);
        }
    }

    /* ---- dispatch the two passes over the g2dd PGL verbs ---- */
    rc = vk_gfx_draw(gp->handle, fb_host, rt_w, rt_h, fb_stride,
                     (cim->mem && cim->mem->gfx_shm > 0) ? cim->mem->gfx_shm : 0,
                     attr, nverts, astride, unif, num_unif,
                     first_frame, clear_rgba, clear_z,
                     ntex ? tex : NULL, ntex);
    free(unif);
#undef GFX_CPU

    if (rc == VKCLIENT_OK)
        return VK_SUCCESS;
    /* No device-wide failure latch: a declined/failed GPU draw replays on the
     * CPU rasterizer (correct pixels); a later draw may retry the GPU.  Terminal
     * staging was abandoned inside vkclient exactly like vk_kernel_dispatch. */
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
        free(gp->xf_bindings);
        free(gp->rs_bindings);
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
