/*
 * vkclient.h - libvulkan's user-space IPC client to the g2dd daemon's compute
 * offload path (G2D_DEV_CNTL_CL_*).  This is the structural twin of OpenCL's
 * clclient: a thin wrapper over the same g2dclient dev_cntl transport
 * portablegl/OpenCL already use.  It stages buffers into physically-contiguous
 * shm canvases (so the V3D CSD addresses them by physical base with zero copy),
 * marshals the request structs, and copies results back.
 *
 * The Vulkan GPU device reuses the proven CL compute verbs rather than adding
 * Vulkan-specific daemon verbs: the serialized program blob is byte-for-byte the
 * g2d_pgl_program_blob_t wire format with only the compute stage populated (the
 * exact payload CL_PROGRAM already carries), and a dispatch references it by the
 * integer handle CL_PROGRAM returns.  No new daemon verbs are required, so the
 * raspi5 compute path (g2dd -> bsp_cl -> vc_cl -> v3d_3d CSD) is shared with
 * OpenCL unchanged.
 *
 * It is built as a PRIVATE intermediate (libvkclient.a) and folded into
 * libvulkan.a by libs/vulkan/Makefile, so an application links only -lvulkan and
 * never sees this header, g2dclient, or the pglcompiler.  Only the src/gpu
 * sources include it.
 */
#ifndef VULKAN_VKCLIENT_H
#define VULKAN_VKCLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Largest number of storage-buffer bindings one dispatch carries as SSBO args
 * (must match G2D_CL_MAX_ARGS in g2dclient.h). */
#define VKCLIENT_MAX_ARGS 16u

/* Result codes returned by the vk_* entry points below.  They mirror the
 * g2dclient G2D_* codes value-for-value so gpu_device.c can classify a result
 * without including g2dclient.h (vkclient is the abstraction boundary). */
#define VKCLIENT_OK              0    /* the GPU ran the op                  */
#define VKCLIENT_ERR_FAILED     (-1)  /* transient: a CPU replay is safe     */
#define VKCLIENT_ERR_UNSUPPORTED (-2) /* unsupported launch: use CPU fallback */
#define VKCLIENT_ERR_TIMEOUT    (-3)  /* terminal: quiesce, then CPU replay  */
#define VKCLIENT_ERR_MMU_FAULT  (-4)  /* terminal: quiesce, then CPU replay  */

/* Quiesce the GPU after a terminal result (VKCLIENT_ERR_TIMEOUT / _MMU_FAULT):
 * submitted work may still own its buffers, so this drains it before the caller
 * touches them again (e.g. to replay the dispatch on the CPU).  A no-op for
 * every other code.  Wraps g2d_quiesce_on_fault. */
void vkclient_quiesce(int32_t rc);

/* Probe whether a g2d daemon with a GPU compute path is reachable and latch the
 * on-device compiler's codegen target to the live silicon (pglc_set_version).
 * Returns the backend-neutral GPU version code (e.g. 71 = Pi5 V3D 7.1) when a
 * usable compute GPU is present, or 0 when there is none (no daemon, no GPU, or
 * a generation the compiler backend does not support).  Idempotent. */
uint32_t vkclient_gpu_version(void);

/* Upload a serialized compute blob (the g2d_pgl_program_blob_t wire format with
 * only the compute stage populated, as produced by pglc_program_serialize).
 * Returns a non-negative program handle, or a negative G2D_ERR_* code
 * (G2D_ERR_NOT_SUPPORTED == -2 when the platform declines compute, which latches
 * libvulkan's GPU device off). */
int32_t vk_program_upload(const void *blob, uint32_t size);

/* Run prog_handle over a wg[0] x wg[1] x wg[2] grid of work-groups, each of
 * wg_size work-items (1..256).  arg_host[i]/arg_size[i] are the storage-buffer
 * host stores in SSBO binding order: each is staged into a contig-shm canvas for
 * the launch and copied back into arg_host[i] afterwards, so the GPU reads the
 * inputs and its writes land in the caller's host buffers.  A NULL arg_host[i]
 * (or arg_size[i] == 0) marks an unused trailing slot.  unif/num_unif is the
 * packed scalar uniform blob (num_unif u32 dwords; may be 0/NULL).  Returns
 * VKCLIENT_OK (0) or a negative code. */
int32_t vk_kernel_dispatch(int32_t prog_handle,
                           const uint32_t wg[3], uint32_t wg_size,
                           void *const *arg_host, const uint32_t *arg_size,
                           uint32_t num_args,
                           const uint32_t *unif, uint32_t num_unif);

/* Release a program handle and the GPU dma it owns.  Returns VKCLIENT_OK (0) or
 * a negative code.  A graphics (geom) handle uploaded via vk_gfx_program_upload
 * shares the daemon program-handle space with a compute handle, so it is freed
 * here too. */
int32_t vk_program_free(int32_t prog_handle);

/* --- Graphics (two-pass geom, geom_kind == 5) offload ----------------------
 * The Vulkan GPU graphics path reuses the proven portablegl PGL verbs rather
 * than adding Vulkan-specific daemon commands: a serialized geom blob (the
 * g2d_pgl_program_blob_t VERSION-6 wire format with BOTH compute stages
 * populated - cs2 = the vertex-transform pass, cs = the raster pass - and
 * cs_is_geom == 5) is uploaded through G2D_DEV_CNTL_PGL_PROGRAM, and each draw
 * is a G2D_DEV_CNTL_PGL_DRAW referencing that handle.  The daemon dispatches the
 * two CSD passes; vkclient stages the framebuffer / attribute / depth / uniform
 * / texture canvases and copies the rendered pixels back into the caller's host
 * colour attachment. */

/* Maximum texture units one graphics draw binds (must match
 * G2D_PGL_MAX_TEX_UNITS in g2dclient.h). */
#define VKCLIENT_MAX_TEX 4u

/* Maximum vertices one graphics draw stages (mirrors G2D_PGL_MAX_VERTS, the
 * daemon's vout capacity; vk_gfx_draw rejects a larger draw).  The GPU backend
 * gates against it before gathering the attribute stream. */
#define VKCLIENT_MAX_VERTS 20480u

/* One combined-image-sampler binding for vk_gfx_draw: `host` points at a tightly
 * packed RGBA8 image (w * h * 4 bytes, row pitch w * 4).  vkclient tiles it into
 * the V3D sampler layout (the TMU cannot read linear raster memory) before the
 * draw.  `linear` selects the TMU filter (0 = NEAREST, non-zero = LINEAR). */
typedef struct vk_gfx_tex {
    const void *host;
    uint32_t    w;
    uint32_t    h;
    int         linear;
} vk_gfx_tex;

/* Upload a serialized two-pass geom blob (pglc_program_serialize output with
 * opts.geom_kind == 5, opts.geom_tpw == 256 (the raster kernel's 16x16 lane
 * count, so the daemon dispatches ceil(ntris/256) workgroups), opts.vout_stride
 * / attr_stride set).
 * Returns a non-negative program handle, or a negative G2D_ERR_* code
 * (G2D_ERR_NOT_SUPPORTED == -2 when the platform declines, which latches
 * libvulkan's GPU graphics path off). */
int32_t vk_gfx_program_upload(const void *blob, uint32_t size);

/* Run one two-pass geom draw of `nverts` vertices (a multiple of 3,
 * <= G2D_PGL_MAX_VERTS) into the caller's colour attachment.
 *
 *   prog_handle  - a handle from vk_gfx_program_upload.
 *   fb_host      - the colour attachment host store, RGBA8/BGRA8 pixels whose
 *                  byte order already matches what the generated raster kernel
 *                  writes (the caller picks the kernel's uFbSwizzle to agree).
 *   fb_w/fb_h    - render-target size in pixels.
 *   fb_stride    - fb_host row pitch in bytes (0 -> derive fb_w * 4).  The GPU
 *                  framebuffer canvas is always tightly packed (fb_w * 4), so a
 *                  pitched host attachment is copied row by row.
 *   attr_host    - the gathered per-vertex attribute float stream, one vertex's
 *                  attributes contiguous, `attr_stride` FLOATS per vertex
 *                  (nverts * attr_stride * 4 bytes total).
 *   unif/num_unif- the packed per-draw uniform blob: the CONCATENATION
 *                  [cs2/xform uniforms | cs/raster uniforms] the daemon splits at
 *                  cs2_uniform_blob_dwords (num_unif u32 dwords; may be 0/NULL).
 *   first_frame  - non-zero: this is the frame's first dispatch, so the daemon
 *                  GPU-clears the framebuffer to `clear_rgba` and the depth
 *                  surface to `clear_z` before rendering (fb_host need not be
 *                  pre-loaded).  Zero: fb_host is copied into the framebuffer
 *                  canvas first and the depth surface keeps its prior contents
 *                  (so it persists across draws within one render pass).
 *   clear_rgba   - the clear colour the daemon fills on first_frame (in the
 *                  g2d_pgl_req_t.clear_rgba encoding the caller computed for the
 *                  kernel's fb byte order).
 *   clear_z      - the depth the daemon fills on first_frame.
 *   tex/ntex     - up to VKCLIENT_MAX_TEX combined-image-sampler bindings in
 *                  binding order (may be NULL/0 when the FS samples nothing).
 *
 * Returns VKCLIENT_OK (0) or a negative code.  On success fb_host holds the
 * rendered pixels; a terminal result (TIMEOUT/MMU_FAULT) abandons the persistent
 * staging (it may still be GPU-owned) exactly like vk_kernel_dispatch. */
int32_t vk_gfx_draw(int32_t prog_handle,
                    void *fb_host, uint32_t fb_w, uint32_t fb_h, uint32_t fb_stride,
                    int fb_shm,
                    const void *attr_host, uint32_t nverts, uint32_t attr_stride,
                    const uint32_t *unif, uint32_t num_unif,
                    int first_frame, uint32_t clear_rgba, float clear_z,
                    const vk_gfx_tex *tex, uint32_t ntex);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_VKCLIENT_H */
