/*
 * pgl_g3d.h - portablegl GPU 3D-offload back end (client half).
 *
 * Compiled ONLY when portablegl is built with -DPGL_G3D (a platform opts in by
 * exporting PGL_G3D=yes from its system/make.inc); every other platform builds
 * the pure CPU/NEON rasterizer with none of this code present.  It is the client
 * half of the GPU offload: it owns the shared render target and the
 * vertex/varying staging buffers, batches the CPU-computed post-vertex-shader
 * triangles, and marshals them into the stateless g2dclient /dev/g2d protocol
 * (G2D_DEV_CNTL_PGL_CLEAR / _PGL_DRAW).  The g2dd daemon forwards those ops to
 * the platform 3D driver through the bsp_g3d interface; portablegl itself never
 * maps any GPU MMIO (all of it is root-daemon-only).
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

#ifndef PGL_G3D_H
#define PGL_G3D_H

#ifdef PGL_G3D

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Built-in fixed-function fragment-shader ids, matching the g2dclient
   G2D_PGL_FS_* protocol values exactly.  SOLID and GOURAUD reuse the
   vertex-colour attribute path; TEX_MOD (Phase D) samples a linear RGBA8
   texture image uploaded via pgl_g3d_upload_texture with point filtering and
   CLAMP-to-edge addressing. */
#define PGL_G3D_FS_SOLID   0
#define PGL_G3D_FS_GOURAUD 1
#define PGL_G3D_FS_TEX_MOD 2

/* Phase D texture table: a small fixed number of contig-shm RGBA8 texture
   images addressable by slot index.  Uploading a slot twice replaces its
   image (the previous shm segment is freed).  A TEX_MOD batch references one
   slot; changing the slot mid-frame flushes the batch first (a batch is
   homogeneous in fs_id AND tex_slot). */
#define PGL_G3D_MAX_TEX 4

/* WS4 compiled-shader sampler units: the number of texture units (iChannel0..)
   a compiled fragment shader may sample, value-identical to the g2dclient
   G2D_PGL_MAX_TEX_UNITS protocol constant and the back end's
   V3D_3D_MAX_TEX_UNITS.  A compiled draw binds up to this many uploaded
   texture slots via pgl_g3d_bind_tex_unit. */
#define PGL_G3D_MAX_TEX_UNITS 4

/* Maximum vertices stageable in ONE GPU dispatch (sizing the fixed pos/col/uv/
   normal staging and the per-draw sub-range cap).  Must be >= the g2dclient
   G2D_PGL_MAX_VERTS / back-end VC_PGL3D_MAX_VERTS protocol caps so a draw the
   client stages is never rejected downstream.  Kept large enough (20480) that
   a whole rigid mesh - e.g. the 19200-vertex Utah teapot - fits in a SINGLE
   dispatch: every dispatch pays a large fixed cost (a whole-surface L2
   clean+invalidate of the colour+depth targets, a CSD_DONE IRQ wait, five
   canvas attach/detach and a blocking IPC round-trip) that is independent of
   the triangle count, so splitting one mesh across many small dispatches makes
   the GPU path slower than the CPU rasterizer.  Callers drawing a mesh larger
   than this must split it into contiguous sub-ranges themselves. */
#define PGL_G3D_MAX_VERTS 20480u

/* Depth-function codes, value-identical to the g2dclient G2D_PGL_DEPTH_*
   protocol set.  NONE disables the depth test; all eight GL compare functions
   are covered. */
#define PGL_G3D_DEPTH_NONE     0
#define PGL_G3D_DEPTH_LESS     1
#define PGL_G3D_DEPTH_LEQUAL   2
#define PGL_G3D_DEPTH_GREATER  3
#define PGL_G3D_DEPTH_GEQUAL   4
#define PGL_G3D_DEPTH_ALWAYS   5
#define PGL_G3D_DEPTH_NEVER    6
#define PGL_G3D_DEPTH_EQUAL    7
#define PGL_G3D_DEPTH_NOTEQUAL 8

/* Back-face-cull config codes carried in the cull_en argument (0 = draw both
   facings), value-identical to the g2dclient G2D_PGL_CULL_* protocol set. */
#define PGL_G3D_CULL_NONE       0
#define PGL_G3D_CULL_BACK_CCW   1
#define PGL_G3D_CULL_BACK_CW    2
#define PGL_G3D_CULL_FRONT_CCW  3
#define PGL_G3D_CULL_FRONT_CW   4
#define PGL_G3D_CULL_BOTH       5

/* Fragment blend modes carried to the gouraud geometry raster kernel via
   pgl_g3d_set_blend (patched into the kernel's uBlendMode uniform).  Only the
   INSTANCED-LIT/gouraud raster kernel implements them; the textured/texcol
   kernels keep their own fixed equations.  Value 0 reproduces the historical
   opaque overwrite (alpha forced to 255). */
#define PGL_G3D_BLEND_OPAQUE    0   /* dst = src                            */
#define PGL_G3D_BLEND_ALPHA     1   /* dst = src*a + dst*(1-a)              */
#define PGL_G3D_BLEND_ADD       2   /* dst = dst + src*a                    */

/* Primitive-mode codes carried to the back end, value-identical to the
   g2dclient G2D_PGL_MODE_* protocol set.  A batch is homogeneous in mode too,
   so points/lines/triangles never mix in one dispatch (the GPU primitive-list
   format is per-dispatch render state). */
#define PGL_G3D_MODE_POINTS    0
#define PGL_G3D_MODE_LINES     1
#define PGL_G3D_MODE_TRIANGLES 4

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
} pgl_g3d_ext_t;

/* Bring the GPU path up for a w x h RGBA8 target: allocate the contig-shm
   render target plus the fixed vertex/varying staging, then probe-clear it to
   confirm the daemon actually has a 3D path.  Returns 1 when the GPU is usable
   (pgl_g3d_enabled() then stays true until a later op latches it off), 0 when
   it is not (the caller keeps the CPU rasterizer and must not call again). */
int  pgl_g3d_init(int w, int h);

/* Non-zero while the GPU path is engaged (init succeeded and no op has latched
   it off).  Once it returns 0 it never returns 1 again for this process. */
int  pgl_g3d_enabled(void);

/* The contig-shm render-target pixels: portablegl points its back_buffer here so
   CPU and GPU draws land in the SAME buffer and the demo's present blit goes
   through the existing GPU path for free.  Valid only while enabled. */
void* pgl_g3d_pixels(void);

/* The contig shm id backing pgl_g3d_pixels(), 0 while not enabled.  A present
   blit that tags its source graph with this id (shm_contig=1) is routed by
   graph_blt to the g2d GPU copy instead of a CPU read of the whole NOCACHE
   target.  Re-query per present: a resize may reallocate the segment. */
int   pgl_g3d_pixels_shm(void);

/* Reallocate the render target for a new w x h (viewport resize), preserving
   nothing (the caller re-clears).  Returns 1 on success, 0 (latched off) if the
   allocation fails. */
int  pgl_g3d_resize(int w, int h);

/* Full-frame GPU clear of the target to the packed rgba8 colour (0xAABBGGRR).
   Returns 1 when the GPU ran it, 0 when the caller must clear on the CPU (and
   the GPU has just latched off). */
int  pgl_g3d_clear(uint32_t rgba8);

/* Add one triangle to the current batch.  n0/n1/n2 are the perspective-divided
   clip positions (NDC xyz, 3 floats each); col0/col1/col2 are the per-vertex
   RGBA colours as 4 normalized floats in [0,1] (GOURAUD) or NULL (SOLID, which
   uses solid_rgba for all three).  depth_func (PGL_G3D_DEPTH_*), depth_mask
   (Z-write) and cull_cfg (PGL_G3D_CULL_*) are the per-dispatch render state.
   ext (NULL or all-zero for none) carries the Phase B2c scissor / polygon-
   offset / colour-mask state.  A change of fs_id/solid_rgba/depth/cull/ext
   state, or a full batch, flushes first so a batch is always homogeneous.
   Returns 1 when the triangle was batched, 0 when the caller must rasterize it
   on the CPU (GPU off / latched off during this call). */
int  pgl_g3d_batch_tri(int fs_id, uint32_t solid_rgba,
                        const float* n0, const float* n1, const float* n2,
                        const float* col0, const float* col1, const float* col2,
                        uint32_t depth_func, uint32_t depth_mask,
                        uint32_t cull_cfg, const pgl_g3d_ext_t* ext);

/* Add one point (GL_POINTS) to the current batch.  n0 is the perspective-divided
   clip position (NDC xyz); col0 is the per-vertex RGBA as 4 normalized floats
   (GOURAUD) or NULL (SOLID, which uses solid_rgba).  Same depth/cull state and
   homogeneity/flush rules as batch_tri; the primitive mode joins the batch key
   so a point batch never mixes with lines or triangles.  Returns 1 when the
   point was batched, 0 when the caller must draw it on the CPU. */
int  pgl_g3d_batch_point(int fs_id, uint32_t solid_rgba, const float* n0,
                          const float* col0, uint32_t depth_func,
                          uint32_t depth_mask, uint32_t cull_cfg,
                          const pgl_g3d_ext_t* ext);

/* Add one line segment (GL_LINES/LINE_STRIP/LINE_LOOP) to the current batch:
   two endpoints n0/n1 (NDC xyz) with per-vertex colours col0/col1 (GOURAUD) or
   NULL (SOLID).  Same rules as batch_point; the GPU renders a line batch as a
   list of independent 2-vertex segments.  Returns 1 when the segment was
   batched, 0 when the caller must draw it on the CPU. */
int  pgl_g3d_batch_line(int fs_id, uint32_t solid_rgba,
                         const float* n0, const float* n1,
                         const float* col0, const float* col1,
                         uint32_t depth_func, uint32_t depth_mask,
                         uint32_t cull_cfg, const pgl_g3d_ext_t* ext);

/* Submit the pending batch to the GPU (no-op when empty).  Any non-OK g2d
   result latches the GPU off; a lost batch cannot be replayed, so the caller
   redraws on the CPU from the next primitive onward. */
void pgl_g3d_flush(void);

/* Phase D texture upload: allocate (or replace) a w x h linear RGBA8 texture
   image in slot [0, PGL_G3D_MAX_TEX) and copy the caller's tightly-packed
   rgba8 pixels (w*h*4 bytes) into it.  The image is contig-shm so the GPU can
   address it by physical base with no per-draw copy.  Returns 1 on success, 0
   when the GPU path is off, the slot is out of range, w/h are non-positive,
   or the allocation failed (the GPU is NOT latched off by an allocation
   failure - only a submitted dispatch failure latches). */
int  pgl_g3d_upload_texture(int slot, int w, int h, const void* rgba8);

/* Same as pgl_g3d_upload_texture, but store the image in the V3D texture
   memory layout (LT / UBLINEAR / UIF, chosen from w x h exactly as mesa's
   v3d_setup_slices does) instead of raster order.  The V3D TMU cannot sample
   a raster 2D image, so a slot bound to a compiled-shader sampler unit with
   pgl_g3d_bind_tex_unit MUST be filled by this variant; a tiled slot is in
   turn unusable by the fixed-function TEX_MOD kernel (which reads raster
   rows), so pgl_g3d_batch_tri_tex ignores it.  Same return contract. */
int  pgl_g3d_upload_texture_tiled(int slot, int w, int h, const void* rgba8);

/* Cubemap variant of pgl_g3d_upload_texture_tiled: stage the six square faces
   of a w x w cubemap (faces_rgba8 points at 6*w*w*4 bytes, the +X,-X,+Y,-Y,
   +Z,-Z faces concatenated exactly as portablegl's texture_cubemap stores
   them) into ONE slot as a 6-layer V3D 2D array.  Each face is tiled with the
   same layout pgl_g3d_upload_texture_tiled would use and written at a
   64-byte-aligned per-layer stride, so the daemon can build a texture-state
   record with image_depth = 5 and array_stride_64_byte_aligned set - the
   hardware-native form mesa lowers a samplerCube TMU fetch to.  Bind it to a
   compiled-shader sampler unit with pgl_g3d_bind_tex_unit like a tiled slot.
   Same return contract (0 leaves the unit unbound -> the draw declines). */
int  pgl_g3d_upload_texture_cube_tiled(int slot, int w, int h,
                                       const void* faces_rgba8);

/* Release a texture slot's shm segment (safe on an unused slot).  Any open
   TEX_MOD batch referencing the slot is flushed first so the driver never
   sees a dangling physical base. */
void pgl_g3d_free_texture(int slot);

/* Add one TEX_MOD triangle to the current batch.  n0/n1/n2 are the
   perspective-divided clip positions (NDC xyz, 3 floats each); uv0/uv1/uv2
   are texture coordinates (2 floats each, s/t in [0,1] - values outside are
   CLAMPed by the frag shader).  tex_slot selects a previously-uploaded image
   from the PGL_G3D_MAX_TEX table; depth_func/depth_mask/cull_cfg/ext are the
   per-dispatch render state (identical semantics to batch_tri).  A change of
   tex_slot or any other batch-key field flushes first.  Returns 1 when the
   triangle was batched, 0 when the caller must rasterize it on the CPU (GPU
   off, slot empty, or the batch just latched the GPU off). */
int  pgl_g3d_batch_tri_tex(int tex_slot,
                            const float* n0, const float* n1, const float* n2,
                            const float* uv0, const float* uv1, const float* uv2,
                            uint32_t depth_func, uint32_t depth_mask,
                            uint32_t cull_cfg, const pgl_g3d_ext_t* ext);

/* Mark the start of a new frame: flush any batch pending from the previous
   frame, then record the colour (0xAABBGGRR) and [0,1] depth that the frame's
   FIRST GPU draw dispatch clears the persistent colour+Z surfaces to.  Later
   dispatches of the same frame LOAD them, so a multi-draw frame accumulates
   instead of each dispatch erasing the last.  No dispatch is issued here (the
   clear rides along with the first draw).  No-op when the GPU path is off. */
void pgl_g3d_begin_frame(uint32_t clear_rgba, float clear_z);

/* Consume a pending frame clear CPU-side, BEFORE a CPU-rasterised primitive
   writes the shared back buffer.  pgl_Clear already cleared that buffer (it
   IS the GPU colour surface) and only deferred the GPU-side clear to the
   frame's first draw dispatch; if a CPU draw lands first, that deferred clear
   would erase it afterwards (e.g. a GL_LINES floor grid drawn before the
   frame's first GPU mesh dispatch).  This drops the pending clear - its
   colour half is already in the buffer - and fills the persistent depth
   surface exactly as the geom first-frame fill would (far = 0xFFFFFFFF), so
   later dispatches LOAD instead of clear and the CPU pixels survive.  No-op
   when the GPU path is off or no clear is pending. */
void pgl_g3d_consume_frame_clear(void);

/* Free the shm target and staging and latch the GPU off.  Safe to call when
   never initialised.  Also releases every cached GLSL shader program
   (pgl_g3d_program_free_all). */
void pgl_g3d_shutdown(void);

/* === On-device GLSL shader programs (WS4 arbitrary-shader GPU path) =========
   A "program" is a GLSL vertex+fragment pair compiled IN-PROCESS to the GPU's
   ISA by the machine compiler backend behind <pglcompiler.h> (linked into the
   app via libGLESv2.a) and cached here.  The cached code is later marshalled
   through the g2dclient protocol to the
   g2dd daemon, which uploads them to the GPU and dispatches: a compiled-shader
   draw is real VideoCore hardware execution, never CPU rasterization.  This is
   the app-side half of the GPU wiring (the app compiles; only the root daemon
   may touch the V3D MMIO).

   pgl_g3d_prog is an opaque non-negative handle; PGL_G3D_PROG_INVALID (< 0)
   signals an error.  Handles are process-global and stay valid until freed. */
typedef int pgl_g3d_prog;
#define PGL_G3D_PROG_INVALID (-1)

/* Compile (or return the cached handle for) a GLSL vertex+fragment pair.
   vert_glsl/frag_glsl are NUL-terminated GLSL source strings; both are
   required.  Returns a non-negative handle on success, PGL_G3D_PROG_INVALID on
   failure (and, when errlog != NULL, copies up to errsz bytes of the compiler
   diagnostic).  Compiling the same source pair again returns the same handle
   with no recompilation, so it is cheap to call per frame.  The compile is pure
   CPU work in this process and does NOT require the GPU probe to have run; a
   failure is per-shader and does NOT latch the GPU off (the caller keeps its
   CPU fragment shader for that one effect). */
pgl_g3d_prog pgl_g3d_program_compile(const char* vert_glsl, const char* frag_glsl,
                                     char* errlog, int errsz);

/* Non-zero when p is a valid, successfully-compiled cached handle. */
int pgl_g3d_program_valid(pgl_g3d_prog p);

/* Non-zero when the daemon has (stickily) declined p - its upload or a draw
   referencing it returned NOT_SUPPORTED - so compiled draws of p stay on the
   caller's CPU path while the GPU remains available to other programs. */
int pgl_g3d_program_declined(pgl_g3d_prog p);

/* Release one cached program (safe on an invalid handle) or every cached
   program.  pgl_g3d_shutdown() calls pgl_g3d_program_free_all(). */
void pgl_g3d_program_free(pgl_g3d_prog p);
void pgl_g3d_program_free_all(void);

/* === WS4 compiled-shader draw path (full-screen-quad scoped) ================
   These drive a draw with an on-device-compiled program instead of the
   built-in fixed-function fragment shaders.  The path is scoped to the
   shadertoy full-screen-quad case: a pos-only triangle strip whose fragment
   colour the compiled FS derives procedurally (from uniforms such as
   iGlobalTime / iResolution), so no per-vertex colour or uv is staged.  The
   program's QPU is uploaded to the daemon lazily on the first batched triangle
   (see pgl_g3d_shader_upload); a declined upload keeps THAT effect on the
   caller's CPU rasterizer without latching the whole GPU off. */

/* Maximum per-draw uniform words the client stages for the daemon's QUNIFORM
 * interpreter. This is independent of the daemon's larger per-program static
 * uniform stream; 256 words covers small state arrays such as Bricks' 12x13
 * brick table without requiring float textures. */
#define PGL_G3D_MAX_UNIF 256u

/* Select the active compiled program for subsequent pgl_g3d_batch_tri_compiled
   draws; PGL_G3D_PROG_INVALID clears it back to the fixed-function path.  A
   program change flushes any open batch (a batch is homogeneous in its
   program).  No-op when the GPU path is off. */
void pgl_g3d_set_shader_program(pgl_g3d_prog p);

/* Stage the per-draw GL uniform blob (count u32 values, clamped to
   PGL_G3D_MAX_UNIF) that the daemon's QUNIFORM interpreter indexes for
   QUNIFORM_UNIFORM slots.  Any open batch is flushed FIRST so it keeps the
   uniforms it was batched with; the new blob then applies to subsequent
   batches.  No-op when the GPU path is off or the uniform staging is absent. */
void pgl_g3d_set_uniforms(const uint32_t* data, uint32_t count);

/* Look up the dword offset - within the per-draw uniform blob staged by
   pgl_g3d_set_uniforms - that program p's FRAGMENT shader assigned to a named
   default-block uniform (e.g. "iGlobalTime", "iResolution").  The offset is
   exactly the QUNIFORM_UNIFORM data value the compiled FS reads, so the client
   writes data[offset..offset+num_components-1] with the uniform's components.
   The value MUST be resolved by name through this query, never assumed from a
   fixed [iGlobalTime, iResolution.xyz] layout: mesa's linker dead-strips
   uniforms a given effect never reads, which shifts the surviving uniforms'
   dword offsets per effect (effects that never read iGlobalTime place
   iResolution.x at dword 0, while effects that do place iGlobalTime at 0 and
   iResolution.x at 1).  Returns the non-negative
   dword offset, or -1 when p is invalid, no such default-block uniform exists,
   or name is NULL.  When num_components != NULL it receives the uniform's
   consecutive-dword width (scalar = 1, vec3 = 3).  Opaque uniforms (samplers /
   images) ride the texture path, never the uniform blob, so they are not
   reported here. */
int  pgl_g3d_shader_uniform_dword(pgl_g3d_prog p, const char* name,
                                  uint32_t* num_components);

/* Report whether program p attached the INSTANCED geometry xform kernel (the
   PGL_G3D_INSTANCED variant that reads a per-vertex instance index from pos.w
   and its per-instance matrices from a 4th SSBO).  Returns 1 only when the
   compute stage is present and is the instanced variant, so an app that staged
   an instanced mesh can assert the matching kernel bound before relying on it
   (a plain or textured geom kernel, or the CPU fallback, returns 0). */
int  pgl_g3d_shader_geom_inst(pgl_g3d_prog p);

/* Report whether program p attached the INSTANCED-LIT geometry xform kernel
   (the PGL_G3D_INSTANCED_LIT variant: 40-float per-instance records carrying
   emissive + alpha and the engine demo's reflect-spec lighting model, staged
   and dispatched exactly like the plain instanced variant).  Returns 1 only
   when the compute stage is present and is that variant. */
int  pgl_g3d_shader_geom_inst_lit(pgl_g3d_prog p);

/* Add one triangle of the active compiled program to the current batch:
   n0/n1/n2 are the clip-space positions (NDC xyz, 3 floats each).  Requires an
   active program (pgl_g3d_set_shader_program) whose daemon upload succeeds;
   depth_func/depth_mask/cull_cfg/ext carry the per-dispatch render state
   (identical semantics to pgl_g3d_batch_tri).  Returns 1 when the triangle was
   batched, 0 when the caller must rasterize it on the CPU (no active program,
   upload declined, GPU off, or the batch just latched the GPU off). */
int  pgl_g3d_batch_tri_compiled(const float* n0, const float* n1, const float* n2,
                                uint32_t depth_func, uint32_t depth_mask,
                                uint32_t cull_cfg, const pgl_g3d_ext_t* ext);

/* Bind an uploaded texture slot to a compiled-shader sampler unit
   [0, PGL_G3D_MAX_TEX_UNITS) for subsequent pgl_g3d_batch_tri_compiled draws:
   unit u corresponds to the FS's iChannelU / sampler u whose QUNIFORM_TMU_CONFIG
   pair the back end expands against the bound image.  tex_slot must name a slot
   filled by pgl_g3d_upload_texture_tiled; a tex_slot < 0, an out-of-range slot,
   an empty slot or a raster (pgl_g3d_upload_texture) slot UNBINDS the unit (a
   sample from an unbound unit the FS references
   declines the draw back to the CPU).  The binding persists until changed (like
   GL texture-unit state) and is marshalled into the draw request on flush.  Call
   after pgl_g3d_set_shader_program and before the batch/flush.  No-op when the
   GPU path is off or unit is out of range.  pgl_g3d_bind_tex_unit samples the
   unit NEAREST; pgl_g3d_bind_tex_unit_filter additionally selects the TMU's
   mag/min filter (PGL_G3D_TEX_FILTER_*), carried per unit in the draw request
   so the back end packs a matching sampler record. */
#define PGL_G3D_TEX_FILTER_NEAREST 0
#define PGL_G3D_TEX_FILTER_LINEAR  1
void pgl_g3d_bind_tex_unit(int unit, int tex_slot);
void pgl_g3d_bind_tex_unit_filter(int unit, int tex_slot, int filter);

/* === WS4 generic compiled-mesh draw path (arbitrary 3D geometry) ============
   These drive a static vertex mesh with an on-device-compiled program whose
   VERTEX shader runs the model/view/projection transform AND lighting on the
   QPU (not the CPU): the app uploads its geometry once, packs the per-draw
   uniform blob by name (pgl_g3d_set_uniforms + pgl_g3d_shader_uniform_dword,
   which resolves vertex-stage uniforms such as mvp / modelview / normal matrix
   / material / light), and issues sub-range triangle draws against the compiled
   program.  This is the generic-geometry half of the compiled path (the
   full-screen-quad pgl_g3d_batch_tri_compiled above is the other half); the V3D
   binner runs the program's coordinate-shader variant so a transformed mesh bins
   correctly.  Two vertex attributes are staged per vertex: position (float3,
   attribute location 0) and normal (float3, location 1). */

/* Opaque non-negative handle to an uploaded static mesh;
   PGL_G3D_MESH_INVALID (< 0) signals an error. */
typedef int pgl_g3d_mesh;
#define PGL_G3D_MESH_INVALID (-1)

/* Stage a static pos+normal mesh for pgl_g3d_draw_mesh_compiled.  `interleaved`
   is nverts vertices of stride_floats floats each, laid out position float3 at
   element 0 followed by normal float3 at element 3 (so stride_floats >= 6; any
   extra trailing floats per vertex, e.g. uv, are ignored).  The mesh is
   de-interleaved into separate position/normal float3 arrays held in CPU memory
   and copied once; each draw re-stages its sub-range into the fixed GPU staging
   (a static GPU VBO addressed by per-draw phy offset is not usable here because
   the daemon's attach cache is keyed by segment id and returns the cached base
   physical address, ignoring a per-draw offset).  Returns a non-negative mesh
   handle, or PGL_G3D_MESH_INVALID when the GPU path is off, the arguments are
   bad, the mesh table is full, or allocation failed (none of which latch the GPU
   off - the caller keeps its CPU path for that mesh). */
pgl_g3d_mesh pgl_g3d_upload_mesh(const float* interleaved, uint32_t nverts,
                                 uint32_t stride_floats);

/* Stage a static INSTANCED pos+normal+index mesh for a single-dispatch
   multi-model draw (gears).  Identical to pgl_g3d_upload_mesh except each
   vertex additionally carries a per-vertex INSTANCE INDEX at element 6 (so
   stride_floats >= 7): position float3 at element 0, normal float3 at element
   3, instance index (a small non-negative integer stored as a float) at
   element 6.  The index selects one entry of the per-frame instance-matrix
   buffer staged by pgl_g3d_set_instances, so ONE pgl_g3d_draw_mesh_compiled
   dispatch transforms a whole multi-model mesh whose per-vertex model
   transform differs.  Requires a program compiled from a vertex shader
   carrying the PGL_G3D_INSTANCED marker (which selects the instanced xform
   kernel); drawn with any other program the index is ignored.  Same return
   contract as pgl_g3d_upload_mesh. */
pgl_g3d_mesh pgl_g3d_upload_mesh_instanced(const float* interleaved,
                                           uint32_t nverts,
                                           uint32_t stride_floats);

/* Stage a static COLOUR-MODULATED TEXTURED mesh (the imgui shape) for
   pgl_g3d_draw_mesh_compiled with a program compiled from the colour-tex
   variant (cs_is_geom=4).  `interleaved` is nverts vertices of stride_floats
   floats each (stride_floats >= 10), laid out exactly as the xform kernel
   indexes binding 1:
     [0..2] = position xyz,  [3..4] = (u, v),  [5..8] = colour rgba (floats in
     [0,1]),  [9] = pad.
   The stream is copied verbatim into the mesh slot (no de-interleave) and each
   draw re-stages its sub-range into the fixed GPU pos staging at a 10-float
   stride.  Same return contract as pgl_g3d_upload_mesh. */
pgl_g3d_mesh pgl_g3d_upload_mesh_texcol(const float* interleaved,
                                        uint32_t nverts,
                                        uint32_t stride_floats);

/* Release an uploaded mesh (safe on an invalid handle). */
void pgl_g3d_free_mesh(pgl_g3d_mesh m);

/* Stage the per-frame INSTANCE-MATRIX buffer an instanced mesh draw reads
   (binding 3 of the instanced xform kernel).  `data` is nfloats consecutive
   floats - one 36-float record per instance, laid out exactly as the kernel
   indexes it:
     data[i*36 + 0..15]  = model-view-projection matrix (column-major mat4)
     data[i*36 + 16..31] = normal matrix               (column-major mat4)
     data[i*36 + 32..34] = material rgb,  [35] = pad
   The buffer is copied into a GPU-resident segment (grown on demand, never on
   a same-or-smaller refill) and marshalled as ctex unit 0 by the next instanced
   pgl_g3d_draw_mesh_compiled, so it must be re-staged every frame the matrices
   change.  Returns 1 on success, 0 when the GPU path is off, the arguments are
   bad, or the allocation failed (none of which latch the GPU off). */
int  pgl_g3d_set_instances(const float* data, uint32_t nfloats);

/* Set the per-draw SCISSOR window for the next colour-modulated textured
   pgl_g3d_draw_mesh_compiled (cs_is_geom=4, the only geom variant that clips).
   The rect is in framebuffer row space (y down: x = left column, y = top row,
   w/h = extent); w<=0 or h<=0 disables clipping so the draw covers the whole
   target.  The value is patched into the raster kernel's uScissor uniform;
   other geom variants declare no uScissor and ignore it.  Call before each
   texcol draw (the engine derives it from glScissor). */
void pgl_g3d_set_scissor(int32_t x, int32_t y, int32_t w, int32_t h);

/* Set the fragment blend mode (PGL_G3D_BLEND_*) patched into the gouraud raster
   kernel's uBlendMode uniform by the next pgl_g3d_draw_mesh_compiled.  Blended
   draws read-modify-write the fb word using the per-vertex alpha the xform
   kernel carries in vout[i*8+7]; the depth-write and cull state ride the same
   draw's depth_mask/cull arguments.  Because lanes rasterise in parallel,
   painter's order holds only BETWEEN dispatches, so the caller must not batch
   overlapping blended geometry that needs a strict order.  The value is a
   per-draw render state: call it before each draw (or batch) whose equation
   differs. */
void pgl_g3d_set_blend(int mode);

/* Set the fullscreen-compute internal render-scale denominator.  1 (default) =
   native resolution, the historical full-res path.  N > 1 renders a non-geom
   fullscreen compute effect into a reduced (_w/N x _h/N) scratch target and
   upscales it to the framebuffer with a bilinear g2d_scale_to (the shared
   4-tap scale kernel), trading image sharpness for throughput.  The shader
   math is UNCHANGED: the engine
   patches the kernel's iResolution and pglc_cs_stride uniforms to the reduced
   geometry, so uv/clip mapping stays correct - this is for spill-bound raymarch
   kernels that cannot reach interactive FPS at native resolution on V3D CSD.
   Mesh/geom and fixed-function draws are never affected.  Call once at startup
   (before the first draw); a scratch allocation failure silently reverts to 1. */
void pgl_g3d_set_render_scale(int scale);
/* Fractional render-scale: internal compute target = fb * den / num.  Use when
   an integer divisor aliases too coarsely but the next integer up costs too much
   FPS (e.g. 3/2 sits between native and 1/2).  num/den <= 1 disables. */
void pgl_g3d_set_render_scale_frac(int num, int den);

/* Temporal AA for the render-scale upscale (DEFAULT OFF).  The low-res sampling
   grid is cycled by +-1 px per frame and the upscaled frames are constant-alpha
   accumulated into a full-res history.  Kept as an opt-in experiment: on-device
   A/B showed the small grid nudge adds too little local sub-pixel phase
   diversity to smooth edges, so it is off by default.  Set 1 to enable. */
void pgl_g3d_set_render_taa(int on);

/* Draw vertices [first, first+count) of an uploaded mesh with a compiled program
   as one GPU dispatch: the program's compiled VS/coord QPU runs the transform +
   lighting on the QPU and its FS shades the fragments.  The per-draw uniform
   blob is the one last staged by pgl_g3d_set_uniforms (call it, and
   pgl_g3d_set_shader_program is NOT needed - `prog` is passed explicitly).
   depth_func (PGL_G3D_DEPTH_*), depth_mask (Z-write) and cull (PGL_G3D_CULL_*)
   are the per-dispatch render state.  The frame's first dispatch clears the
   persistent colour+Z surfaces to the values recorded by pgl_g3d_begin_frame;
   later dispatches LOAD them, so a multi-draw frame accumulates.  Any pending
   batch is flushed first to preserve painter's order.  Returns 1 when the GPU
   ran the draw, 0 when the caller must draw it on the CPU (GPU off, bad
   prog/mesh/range, a sub-range larger than the staging, program upload declined,
   or the dispatch just latched the GPU off). */
int  pgl_g3d_draw_mesh_compiled(pgl_g3d_prog prog, pgl_g3d_mesh mesh,
                                uint32_t first, uint32_t count,
                                uint32_t depth_func, uint32_t depth_mask,
                                uint32_t cull);

/* Transient variant for a mesh that already exists as an interleaved staging
   stream.  It performs the same synchronous draw and final GPU-staging pass as
   pgl_g3d_draw_mesh_compiled(prog, mesh, 0, nverts, ...), but borrows the input
   only for the call: no mesh slot, owning copy, allocation or free is needed.
   The stream layout is selected by the compiled program:
     gouraud/textured: pos.xyz at 0, normal.xyz or uv.xy at 3 (stride >= 6;
                       textured element 5 is the zero pad);
     instanced:        the same plus the instance index at 6 (stride >= 7);
     texcol:           pos.xyz, uv.xy, colour rgba and pad at 0..9 (stride >= 10).
   Extra trailing floats are ignored.  Return semantics match the mesh draw. */
int  pgl_g3d_draw_interleaved_compiled(pgl_g3d_prog prog,
                                       const float* interleaved,
                                       uint32_t nverts, uint32_t stride_floats,
                                       uint32_t depth_func, uint32_t depth_mask,
                                       uint32_t cull);

#ifdef __cplusplus
}
#endif

#endif /* PGL_G3D */
#endif /* PGL_G3D_H */
