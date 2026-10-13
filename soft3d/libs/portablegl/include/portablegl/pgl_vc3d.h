/*
 * pgl_vc3d.h - portablegl VideoCore (V3D 7.1) 3D-pipeline GPU back end.
 *
 * Compiled ONLY when portablegl is built with -DPGL_VC3D (the raspi5 Makefile
 * gate); every other HW builds the pure CPU/NEON rasterizer with none of this
 * code present.  It is the client half of the GPU offload: it owns the shared
 * render target and the vertex/varying staging buffers, batches the CPU-computed
 * post-vertex-shader triangles, and marshals them into the stateless
 * g2dclient /dev/g2d protocol (G2D_DEV_CNTL_PGL_CLEAR / _PGL_DRAW).  The raspi5
 * g2dd daemon forwards those ops to the V3D 3D driver in libvideocore; portablegl
 * itself never maps V3D (all MMIO is root-daemon-only).
 *
 * Division of labour (see the plan): the CPU runs portablegl's ARBITRARY vertex
 * shader per vertex and hands this layer the resulting clip-space positions plus
 * a per-vertex colour; the GPU owns rasterization, perspective-correct
 * interpolation, the BUILT-IN fixed-function fragment shader and the store.
 * Arbitrary C fragment shaders are never sent here - they stay on the CPU.
 *
 * Capability latch: the back end engages only after a successful probe clear.
 * The first G2D_ERR_NOT_SUPPORTED (or a transient G2D_ERR_FAILED, whose dispatch
 * can never be replayed) latches the GPU off for good, so the caller keeps its
 * CPU rasterizer for the rest of the process lifetime.
 */

#ifndef PGL_VC3D_H
#define PGL_VC3D_H

#ifdef PGL_VC3D

#include <stdint.h>

/* Built-in fixed-function fragment-shader ids, matching the g2dclient
   G2D_PGL_FS_* / libvideocore VC_PGL3D_FS_* values exactly.  SOLID and
   GOURAUD reuse the vertex-colour attribute path; TEX_MOD (Phase D) samples
   a linear RGBA8 texture image uploaded via pgl_vc3d_upload_texture with
   point filtering and CLAMP-to-edge addressing. */
#define PGL_VC3D_FS_SOLID   0
#define PGL_VC3D_FS_GOURAUD 1
#define PGL_VC3D_FS_TEX_MOD 2

/* Phase D texture table: a small fixed number of contig-shm RGBA8 texture
   images addressable by slot index.  Uploading a slot twice replaces its
   image (the previous shm segment is freed).  A TEX_MOD batch references one
   slot; changing the slot mid-frame flushes the batch first (a batch is
   homogeneous in fs_id AND tex_slot). */
#define PGL_VC3D_MAX_TEX 4

/* Depth-function codes, value-identical to the g2dclient G2D_PGL_DEPTH_* /
   libvideocore VC_PGL3D_DEPTH_* sets.  NONE disables the depth test; all eight
   GL compare functions are covered. */
#define PGL_VC3D_DEPTH_NONE     0
#define PGL_VC3D_DEPTH_LESS     1
#define PGL_VC3D_DEPTH_LEQUAL   2
#define PGL_VC3D_DEPTH_GREATER  3
#define PGL_VC3D_DEPTH_GEQUAL   4
#define PGL_VC3D_DEPTH_ALWAYS   5
#define PGL_VC3D_DEPTH_NEVER    6
#define PGL_VC3D_DEPTH_EQUAL    7
#define PGL_VC3D_DEPTH_NOTEQUAL 8

/* Back-face-cull config codes carried in the cull_en argument (0 = draw both
   facings), value-identical to the G2D_PGL_CULL_* / VC_PGL3D_CULL_* sets. */
#define PGL_VC3D_CULL_NONE       0
#define PGL_VC3D_CULL_BACK_CCW   1
#define PGL_VC3D_CULL_BACK_CW    2
#define PGL_VC3D_CULL_FRONT_CCW  3
#define PGL_VC3D_CULL_FRONT_CW   4
#define PGL_VC3D_CULL_BOTH       5

/* Primitive-mode codes carried to the back end, value-identical to the
   g2dclient G2D_PGL_MODE_* / libvideocore V3D_3D_MODE_* sets.  A batch is
   homogeneous in mode too, so points/lines/triangles never mix in one
   dispatch (the GPU primitive-list format is per-dispatch render state). */
#define PGL_VC3D_MODE_POINTS    0
#define PGL_VC3D_MODE_LINES     1
#define PGL_VC3D_MODE_TRIANGLES 4

/* Per-draw extended render state (Phase B2c): the scissor clip window, polygon
   offset and colour write mask, mirrored field-for-field from the g2dclient
   g2d_pgl_ext_t so portablegl can fill it without pulling in that header.  An
   all-zero ext (or a NULL pointer) is the historical full-frame / no-offset /
   write-all path.  The scissor rect is in the render target's y-down
   orientation (scissor_y = rows from the top edge to the clip window's bottom);
   scissor_w/h == 0 disables it.  poly_enable gates poly_factor/poly_units.
   color_mask is the GL RGBA write-enable nibble (bit0=R..bit3=A); 0xF = all. */
typedef struct {
    int32_t  scissor_x, scissor_y, scissor_w, scissor_h;
    uint8_t  poly_enable;
    float    poly_factor, poly_units;
    uint32_t color_mask;
} pgl_vc3d_ext_t;

/* Bring the GPU path up for a w x h RGBA8 target: allocate the contig-shm
   render target plus the fixed vertex/varying staging, then probe-clear it to
   confirm the daemon actually has a 3D path.  Returns 1 when the GPU is usable
   (pgl_vc3d_enabled() then stays true until a later op latches it off), 0 when
   it is not (the caller keeps the CPU rasterizer and must not call again). */
int  pgl_vc3d_init(int w, int h);

/* Non-zero while the GPU path is engaged (init succeeded and no op has latched
   it off).  Once it returns 0 it never returns 1 again for this process. */
int  pgl_vc3d_enabled(void);

/* The contig-shm render-target pixels: portablegl points its back_buffer here so
   CPU and GPU draws land in the SAME buffer and the demo's present blit goes
   through the existing GPU path for free.  Valid only while enabled. */
void* pgl_vc3d_pixels(void);

/* Reallocate the render target for a new w x h (viewport resize), preserving
   nothing (the caller re-clears).  Returns 1 on success, 0 (latched off) if the
   allocation fails. */
int  pgl_vc3d_resize(int w, int h);

/* Full-frame GPU clear of the target to the packed rgba8 colour (0xAABBGGRR).
   Returns 1 when the GPU ran it, 0 when the caller must clear on the CPU (and
   the GPU has just latched off). */
int  pgl_vc3d_clear(uint32_t rgba8);

/* Add one triangle to the current batch.  n0/n1/n2 are the perspective-divided
   clip positions (NDC xyz, 3 floats each); col0/col1/col2 are the per-vertex
   RGBA colours as 4 normalized floats in [0,1] (GOURAUD) or NULL (SOLID, which
   uses solid_rgba for all three).  depth_func (PGL_VC3D_DEPTH_*), depth_mask
   (Z-write) and cull_cfg (PGL_VC3D_CULL_*) are the per-dispatch render state.
   ext (NULL or all-zero for none) carries the Phase B2c scissor / polygon-
   offset / colour-mask state.  A change of fs_id/solid_rgba/depth/cull/ext
   state, or a full batch, flushes first so a batch is always homogeneous.
   Returns 1 when the triangle was batched, 0 when the caller must rasterize it
   on the CPU (GPU off / latched off during this call). */
int  pgl_vc3d_batch_tri(int fs_id, uint32_t solid_rgba,
                        const float* n0, const float* n1, const float* n2,
                        const float* col0, const float* col1, const float* col2,
                        uint32_t depth_func, uint32_t depth_mask,
                        uint32_t cull_cfg, const pgl_vc3d_ext_t* ext);

/* Add one point (GL_POINTS) to the current batch.  n0 is the perspective-divided
   clip position (NDC xyz); col0 is the per-vertex RGBA as 4 normalized floats
   (GOURAUD) or NULL (SOLID, which uses solid_rgba).  Same depth/cull state and
   homogeneity/flush rules as batch_tri; the primitive mode joins the batch key
   so a point batch never mixes with lines or triangles.  Returns 1 when the
   point was batched, 0 when the caller must draw it on the CPU. */
int  pgl_vc3d_batch_point(int fs_id, uint32_t solid_rgba, const float* n0,
                          const float* col0, uint32_t depth_func,
                          uint32_t depth_mask, uint32_t cull_cfg,
                          const pgl_vc3d_ext_t* ext);

/* Add one line segment (GL_LINES/LINE_STRIP/LINE_LOOP) to the current batch:
   two endpoints n0/n1 (NDC xyz) with per-vertex colours col0/col1 (GOURAUD) or
   NULL (SOLID).  Same rules as batch_point; the GPU renders a line batch as a
   list of independent 2-vertex segments.  Returns 1 when the segment was
   batched, 0 when the caller must draw it on the CPU. */
int  pgl_vc3d_batch_line(int fs_id, uint32_t solid_rgba,
                         const float* n0, const float* n1,
                         const float* col0, const float* col1,
                         uint32_t depth_func, uint32_t depth_mask,
                         uint32_t cull_cfg, const pgl_vc3d_ext_t* ext);

/* Submit the pending batch to the GPU (no-op when empty).  Any non-OK g2d
   result latches the GPU off; a lost batch cannot be replayed, so the caller
   redraws on the CPU from the next primitive onward. */
void pgl_vc3d_flush(void);

/* Phase D texture upload: allocate (or replace) a w x h linear RGBA8 texture
   image in slot [0, PGL_VC3D_MAX_TEX) and copy the caller's tightly-packed
   rgba8 pixels (w*h*4 bytes) into it.  The image is contig-shm so the GPU can
   address it by physical base with no per-draw copy.  Returns 1 on success, 0
   when the GPU path is off, the slot is out of range, w/h are non-positive,
   or the allocation failed (the GPU is NOT latched off by an allocation
   failure - only a submitted dispatch failure latches). */
int  pgl_vc3d_upload_texture(int slot, int w, int h, const void* rgba8);

/* Release a texture slot's shm segment (safe on an unused slot).  Any open
   TEX_MOD batch referencing the slot is flushed first so the driver never
   sees a dangling physical base. */
void pgl_vc3d_free_texture(int slot);

/* Add one TEX_MOD triangle to the current batch.  n0/n1/n2 are the
   perspective-divided clip positions (NDC xyz, 3 floats each); uv0/uv1/uv2
   are texture coordinates (2 floats each, s/t in [0,1] - values outside are
   CLAMPed by the frag shader).  tex_slot selects a previously-uploaded image
   from the PGL_VC3D_MAX_TEX table; depth_func/depth_mask/cull_cfg/ext are the
   per-dispatch render state (identical semantics to batch_tri).  A change of
   tex_slot or any other batch-key field flushes first.  Returns 1 when the
   triangle was batched, 0 when the caller must rasterize it on the CPU (GPU
   off, slot empty, or the batch just latched the GPU off). */
int  pgl_vc3d_batch_tri_tex(int tex_slot,
                            const float* n0, const float* n1, const float* n2,
                            const float* uv0, const float* uv1, const float* uv2,
                            uint32_t depth_func, uint32_t depth_mask,
                            uint32_t cull_cfg, const pgl_vc3d_ext_t* ext);

/* Mark the start of a new frame: flush any batch pending from the previous
   frame, then record the colour (0xAABBGGRR) and [0,1] depth that the frame's
   FIRST GPU draw dispatch clears the persistent colour+Z surfaces to.  Later
   dispatches of the same frame LOAD them, so a multi-draw frame accumulates
   instead of each dispatch erasing the last.  No dispatch is issued here (the
   clear rides along with the first draw).  No-op when the GPU path is off. */
void pgl_vc3d_begin_frame(uint32_t clear_rgba, float clear_z);

/* Free the shm target and staging and latch the GPU off.  Safe to call when
   never initialised. */
void pgl_vc3d_shutdown(void);

#endif /* PGL_VC3D */
#endif /* PGL_VC3D_H */
