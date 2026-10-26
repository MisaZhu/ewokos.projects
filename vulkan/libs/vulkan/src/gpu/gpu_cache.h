/*
 * gpu_cache.h - the GPU back-end's on-disk cache of compiled programs.
 *
 * The back-end's whole startup cost is the on-device mesa compile
 * (GLSL -> NIR -> QPU); the graphics route pays it twice, once per pass.  That
 * output depends on nothing but the generated GLSL and the live silicon, so a
 * launch that regenerates the same text can reload the previously serialized
 * daemon blob instead of recompiling.  This module owns that persistence: the
 * record format, its staleness checks, and the file layout.  See gpu_cache.c's
 * header comment for the keying and staleness argument.
 *
 * A record carries the blob PLUS the metadata the back-end needs to drive it,
 * because the blob alone is not sufficient: dispatch resolves uniforms by name
 * through the reflection tables, and the graphics route also needs the
 * transpiler's stream layout.  Both come from the compile, so both are cached.
 *
 * Compiled only under VULKAN_GPU_BACKEND (src/gpu is added to SRCS only then).
 */
#ifndef VULKAN_GPU_CACHE_H
#define VULKAN_GPU_CACHE_H

#include <stdint.h>
#include <stddef.h>

#include "spirv_to_glsl.h"
#include <pglcompiler.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record kinds.  They share one file layout - a header, one kind-specific
 * layout struct, an "a" reflection section and an optional "b" one, then the
 * blob - and differ only in which struct and how many sections they use:
 *   COMPUTE  spirv_glsl_info   + the compute stage's uniform table in "a"
 *   GFX      spirv_geom_layout + the xform table in "a", the raster one in "b"
 */
#define VK_GPU_CACHE_COMPUTE 0
#define VK_GPU_CACHE_GFX     1

/* One record.  The binding arrays are malloc'd and owned by the record; a
 * caller that wants to keep one STEALS it (assigns the pointer, then NULLs the
 * field) so vk_gpu_cache_rec_free() does not release it twice. */
typedef struct vk_gpu_cache_rec {
    uint8_t  *blob;              /* serialized daemon blob                     */
    uint32_t  blob_size;

    spirv_glsl_info   info;      /* VK_GPU_CACHE_COMPUTE only                  */
    spirv_geom_layout glayout;   /* VK_GPU_CACHE_GFX only                      */

    struct pglc_uniform_binding *a_bindings;
    uint32_t                     a_nbindings;
    uint32_t                     a_dwords;   /* compute: the uniform span
                                              * gfx:     the xform region, which
                                              *          is the blob split     */
    struct pglc_uniform_binding *b_bindings;
    uint32_t                     b_nbindings;
    uint32_t                     b_dwords;   /* gfx only: the raster region    */
} vk_gpu_cache_rec;

/* Cache key: FNV-1a over the generated GLSL that was compiled (`b` may be NULL
 * for a single-source record).  A separator between the two texts keeps
 * ("ab", NULL) and ("a", "b") distinct. */
uint64_t vk_gpu_cache_key(const char *a, const char *b);

/* Load the record for (gpu_ver, kind, key), or NULL on a miss.  NULL is also
 * returned for a record that is truncated, whose header disagrees with the
 * request, whose layout struct size differs from this build's, or whose counts
 * fall outside their array bounds - a cache file is untrusted input, so every
 * field is validated before it can reach a dispatch.  gpu_ver == 0 (no live GPU
 * version latched) disables the cache. */
vk_gpu_cache_rec *vk_gpu_cache_load(uint32_t gpu_ver, int kind, uint64_t key);

/* Persist a record, overwriting any existing file for the same key.  Best
 * effort: every failure is silent, because the only consequence is that the
 * next launch recompiles.  A partial write (a full ramfs) leaves a file that
 * vk_gpu_cache_load() rejects and a later store replaces. */
void vk_gpu_cache_store_compute(uint32_t gpu_ver, uint64_t key,
                                const spirv_glsl_info *info,
                                uint32_t unif_dwords,
                                const struct pglc_uniform_binding *bindings,
                                uint32_t nbindings,
                                const void *blob, uint32_t blob_size);

void vk_gpu_cache_store_gfx(uint32_t gpu_ver, uint64_t key,
                            const spirv_geom_layout *glayout,
                            uint32_t xf_dwords,
                            const struct pglc_uniform_binding *xf_bindings,
                            uint32_t nxf_bindings,
                            uint32_t rs_dwords,
                            const struct pglc_uniform_binding *rs_bindings,
                            uint32_t nrs_bindings,
                            const void *blob, uint32_t blob_size);

/* Release a record and everything it owns.  Safe on NULL. */
void vk_gpu_cache_rec_free(vk_gpu_cache_rec *r);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_GPU_CACHE_H */
