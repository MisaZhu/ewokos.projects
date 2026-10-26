/*
 * gpu_cache.c - the GPU back-end's on-disk compiled-program cache (gpu_cache.h).
 *
 * WHY IT EXISTS
 * gpu_device.c's per-pipeline prepare is: transpile SPIR-V to GLSL, compile it
 * with the on-device mesa back-end, serialize the result into the daemon's wire
 * blob, upload it.  Of those, the compile dominates by orders of magnitude - it
 * is the vendored mesa GLSL->NIR frontend plus the machine's NIR->QPU/ir3
 * back-end, and on V3D the register-allocation ladder retries whole strategies
 * from scratch.  The graphics route pays it TWICE (the xform kernel and the
 * raster kernel).  None of that output depends on anything but the generated
 * GLSL and the live silicon, yet libvulkan recomputed it on EVERY process
 * launch: portablegl has a shader disk cache, but Vulkan had none at all (its
 * VkPipelineCache object is a no-op stub - see api/pipeline.c).  This closes
 * that gap.
 *
 * WHAT A HIT STILL PAYS
 * The SPIR-V transpile and the per-process vk_program_upload.  The transpile
 * cannot be skipped because the generated GLSL *is* the key (see below), but it
 * is string generation over the module's instruction list - milliseconds.  The
 * upload is one IPC round trip.  The compile and the serialize are gone.
 *
 * KEYING
 * The key is FNV-1a over the GENERATED GLSL, not over the app's SPIR-V.  That
 * choice is what makes the cache self-invalidating for free: any change to
 * spirv_to_glsl.c or to spirv_to_geom_kernels changes the emitted text and so
 * changes the key, which means a record can only ever be served for a
 * byte-identical compile input.  Keying on the SPIR-V instead would have been
 * cheaper still (it would skip the transpile too) but would have needed a
 * hand-maintained transpiler version, and a missed bump serves stale QPU -
 * which on V3D does not fail cleanly, it can wedge the CSD dispatcher.
 *
 * Two axes the source text cannot cover are validated in the record header:
 *   - gpu_ver, the live generation from vkclient_gpu_version() (also part of
 *     the filename): code emitted for a Pi5 wedges a Pi4's CSD;
 *   - pglc_blob_format_version(), the backend's wire format: a blob written by
 *     a build with a different section layout is rejected before it is
 *     uploaded.  The client must check this itself rather than let the daemon
 *     do it - Adreno's bsp_cl validates only the blob magic - which is why the
 *     pglcompiler contract exposes the value.
 * A semantic blob change that does NOT bump that version still needs
 * VK_GPU_CACHE_VER raised by hand.  The residual exposure is bounded: such a
 * record fails at upload, the caller discards it and recompiles, and the store
 * at the end of that path overwrites the file - so the worst case is one launch
 * that costs what it cost before this file existed, never a wrong pixel.
 *
 * LIFETIME
 * The directory is on the ramfs mount (/tmp), so records survive an app relaunch
 * but not a reboot - the same lifetime as portablegl's /tmp/pgl_shader_cache.
 *
 * Format (all fields native little-endian, written with fwrite):
 *   [magic 4][ver 4][wire 4 = pglc_blob_format_version()][gpu_ver 4][kind 4]
 *   [key 8]
 *   [blob_size 4][layout_size 4][layout layout_size]
 *   [a_nbindings 4][a_dwords 4][a_bindings a_nbindings * sizeof(pglc_uniform_binding)]
 *   [b_nbindings 4][b_dwords 4][b_bindings ...]
 *   [blob blob_size]
 */
#include "gpu_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define VK_GPU_CACHE_DIR      "/tmp/vk_shader_cache"
#define VK_GPU_CACHE_MAGIC    0x43504B56u  /* 'V','K','P','C' */
#define VK_GPU_CACHE_VER      1u
/* Bounds on the variable sections.  They are never expected to be reached (a
 * blob is tens of KB and a reflection table tens of entries); they exist so a
 * corrupt or hostile file cannot turn a length field into a huge allocation. */
#define VK_GPU_CACHE_MAX_BLOB (32u * 1024u * 1024u)
#define VK_GPU_CACHE_MAX_BIND 4096u

uint64_t vk_gpu_cache_key(const char *a, const char *b)
{
    uint64_t h = 14695981039346656037ULL;
    const unsigned char *p;
    for (p = (const unsigned char *)a; p && *p; p++) { h ^= *p; h *= 1099511628211ULL; }
    h ^= 0x1Fu; h *= 1099511628211ULL;
    for (p = (const unsigned char *)b; p && *p; p++) { h ^= *p; h *= 1099511628211ULL; }
    return h;
}

/* Record path: VK_GPU_CACHE_DIR/<gpu_ver>_<kind>_<key>.bin */
static void cache_path(char *buf, size_t bufsz, uint32_t gpu_ver, int kind,
                       uint64_t key)
{
    snprintf(buf, bufsz, "%s/%u_%d_%016llx.bin",
             VK_GPU_CACHE_DIR, gpu_ver, kind, (unsigned long long)key);
}

static void cache_ensure_dir(void)
{
    static int tried = 0;
    if (tried)
        return;
    tried = 1;
    mkdir(VK_GPU_CACHE_DIR, 0755);
}

void vk_gpu_cache_rec_free(vk_gpu_cache_rec *r)
{
    if (!r)
        return;
    free(r->blob);
    free(r->a_bindings);
    free(r->b_bindings);
    free(r);
}

/* Reject a record whose counts fall outside the arrays a dispatch indexes with.
 * The directory lives on a world-writable ramfs, so a record is untrusted input
 * even after its header and key check out. */
static int layout_valid(int kind, const vk_gpu_cache_rec *r)
{
    if (kind == VK_GPU_CACHE_GFX) {
        const spirv_geom_layout *g = &r->glayout;
        if (g->num_attrs    < 0 || g->num_attrs    > SPIRV_MAX_LOCATIONS)      return 0;
        if (g->num_varyings < 0 || g->num_varyings > SPIRV_MAX_LOCATIONS)      return 0;
        if (g->num_samplers < 0 || g->num_samplers > SPIRV_GEOM_MAX_SAMPLERS)  return 0;
        if (g->num_flat     < 0 || g->num_flat     > SPIRV_MAX_FLAT)           return 0;
        return 1;
    }
    {
        const spirv_glsl_info *i = &r->info;
        if (i->num_bindings < 0 || i->num_bindings > SPIRV_MAX_BINDINGS)       return 0;
        if (i->num_inputs   < 0 || i->num_inputs   > SPIRV_MAX_LOCATIONS)      return 0;
        if (i->num_outputs  < 0 || i->num_outputs  > SPIRV_MAX_LOCATIONS)      return 0;
        return 1;
    }
}

vk_gpu_cache_rec *vk_gpu_cache_load(uint32_t gpu_ver, int kind, uint64_t key)
{
    char path[256];
    FILE *fp;
    vk_gpu_cache_rec *r;
    uint32_t magic, ver, wire, gver, knd, want;
    uint32_t blob_size, layout_size, anb = 0, bnb = 0;
    uint64_t stored;
    int ok = 0;

    if (gpu_ver == 0u ||
        (kind != VK_GPU_CACHE_COMPUTE && kind != VK_GPU_CACHE_GFX))
        return NULL;

    cache_path(path, sizeof(path), gpu_ver, kind, key);
    fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    r = (vk_gpu_cache_rec *)calloc(1, sizeof(*r));
    if (!r) {
        fclose(fp);
        return NULL;
    }

    /* Every read is checked: a record truncated by a killed writer must fail
     * here and be recompiled, never half-populate a dispatch. */
    if (fread(&magic, 4, 1, fp) != 1 || magic != VK_GPU_CACHE_MAGIC)        goto out;
    if (fread(&ver,   4, 1, fp) != 1 || ver   != VK_GPU_CACHE_VER)           goto out;
    if (fread(&wire,  4, 1, fp) != 1 || wire  != pglc_blob_format_version()) goto out;
    if (fread(&gver,  4, 1, fp) != 1 || gver  != gpu_ver)                    goto out;
    if (fread(&knd,   4, 1, fp) != 1 || knd   != (uint32_t)kind)             goto out;
    if (fread(&stored, 8, 1, fp) != 1 || stored != key)                      goto out;

    if (fread(&blob_size, 4, 1, fp) != 1 ||
        blob_size == 0u || blob_size > VK_GPU_CACHE_MAX_BLOB)                goto out;
    if (fread(&layout_size, 4, 1, fp) != 1)                                  goto out;
    want = (kind == VK_GPU_CACHE_GFX) ? (uint32_t)sizeof(spirv_geom_layout)
                                      : (uint32_t)sizeof(spirv_glsl_info);
    if (layout_size != want)                                                 goto out;

    if (kind == VK_GPU_CACHE_GFX) {
        if (fread(&r->glayout, sizeof(r->glayout), 1, fp) != 1)              goto out;
    } else {
        if (fread(&r->info, sizeof(r->info), 1, fp) != 1)                    goto out;
    }
    if (!layout_valid(kind, r))                                              goto out;

    if (fread(&anb, 4, 1, fp) != 1 || anb > VK_GPU_CACHE_MAX_BIND)           goto out;
    if (fread(&r->a_dwords, 4, 1, fp) != 1)                                  goto out;
    if (anb) {
        r->a_bindings = (struct pglc_uniform_binding *)
                        calloc(anb, sizeof(*r->a_bindings));
        if (!r->a_bindings)                                                  goto out;
        if (fread(r->a_bindings, sizeof(*r->a_bindings), anb, fp) != anb)    goto out;
        r->a_nbindings = anb;
    }

    if (fread(&bnb, 4, 1, fp) != 1 || bnb > VK_GPU_CACHE_MAX_BIND)           goto out;
    if (fread(&r->b_dwords, 4, 1, fp) != 1)                                  goto out;
    if (bnb) {
        r->b_bindings = (struct pglc_uniform_binding *)
                        calloc(bnb, sizeof(*r->b_bindings));
        if (!r->b_bindings)                                                  goto out;
        if (fread(r->b_bindings, sizeof(*r->b_bindings), bnb, fp) != bnb)    goto out;
        r->b_nbindings = bnb;
    }
    /* A compute record carries one reflection section only; a non-empty second
     * one means this file was not written by this format.  A gfx record may
     * legitimately reflect nothing in either stage, so no count is required. */
    if (kind == VK_GPU_CACHE_COMPUTE && bnb != 0u)                           goto out;

    r->blob = (uint8_t *)malloc(blob_size);
    if (!r->blob)                                                            goto out;
    if (fread(r->blob, 1, blob_size, fp) != blob_size)                       goto out;
    r->blob_size = blob_size;
    ok = 1;

out:
    fclose(fp);
    if (!ok) {
        vk_gpu_cache_rec_free(r);
        return NULL;
    }
    return r;
}

/* The one writer both store entry points funnel into.  `a`/`b` are the two
 * reflection sections; a compute record passes NULL/0 for `b`. */
static void cache_write(uint32_t gpu_ver, int kind, uint64_t key,
                        const void *layout, uint32_t layout_size,
                        const struct pglc_uniform_binding *ab, uint32_t anb,
                        uint32_t adw,
                        const struct pglc_uniform_binding *bb, uint32_t bnb,
                        uint32_t bdw,
                        const void *blob, uint32_t blob_size)
{
    char path[256];
    FILE *fp;
    uint32_t magic = VK_GPU_CACHE_MAGIC, ver = VK_GPU_CACHE_VER;
    uint32_t wire = pglc_blob_format_version(), knd = (uint32_t)kind;

    if (gpu_ver == 0u || !layout || !blob || blob_size == 0u)
        return;
    /* Never write a section count with no table behind it: the reader treats
     * that as truncation and would reject the record forever. */
    if (anb && !ab) anb = 0;
    if (bnb && !bb) bnb = 0;

    cache_ensure_dir();
    cache_path(path, sizeof(path), gpu_ver, kind, key);
    fp = fopen(path, "wb");
    if (!fp)
        return;

    fwrite(&magic, 4, 1, fp);
    fwrite(&ver, 4, 1, fp);
    fwrite(&wire, 4, 1, fp);
    fwrite(&gpu_ver, 4, 1, fp);
    fwrite(&knd, 4, 1, fp);
    fwrite(&key, 8, 1, fp);
    fwrite(&blob_size, 4, 1, fp);
    fwrite(&layout_size, 4, 1, fp);
    fwrite(layout, layout_size, 1, fp);
    fwrite(&anb, 4, 1, fp);
    fwrite(&adw, 4, 1, fp);
    if (anb)
        fwrite(ab, sizeof(*ab), anb, fp);
    fwrite(&bnb, 4, 1, fp);
    fwrite(&bdw, 4, 1, fp);
    if (bnb)
        fwrite(bb, sizeof(*bb), bnb, fp);
    fwrite(blob, 1, blob_size, fp);
    /* A short write (a full ramfs) leaves a truncated file, which the reader
     * rejects and the next store overwrites; no unlink is needed and none is
     * assumed to exist. */
    fclose(fp);
}

void vk_gpu_cache_store_compute(uint32_t gpu_ver, uint64_t key,
                                const spirv_glsl_info *info,
                                uint32_t unif_dwords,
                                const struct pglc_uniform_binding *bindings,
                                uint32_t nbindings,
                                const void *blob, uint32_t blob_size)
{
    cache_write(gpu_ver, VK_GPU_CACHE_COMPUTE, key,
                info, (uint32_t)sizeof(*info),
                bindings, nbindings, unif_dwords,
                NULL, 0u, 0u,
                blob, blob_size);
}

void vk_gpu_cache_store_gfx(uint32_t gpu_ver, uint64_t key,
                            const spirv_geom_layout *glayout,
                            uint32_t xf_dwords,
                            const struct pglc_uniform_binding *xf_bindings,
                            uint32_t nxf_bindings,
                            uint32_t rs_dwords,
                            const struct pglc_uniform_binding *rs_bindings,
                            uint32_t nrs_bindings,
                            const void *blob, uint32_t blob_size)
{
    cache_write(gpu_ver, VK_GPU_CACHE_GFX, key,
                glayout, (uint32_t)sizeof(*glayout),
                xf_bindings, nxf_bindings, xf_dwords,
                rs_bindings, nrs_bindings, rs_dwords,
                blob, blob_size);
}
