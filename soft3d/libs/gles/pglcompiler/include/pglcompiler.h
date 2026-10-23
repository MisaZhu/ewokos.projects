/* pglcompiler.h - backend-neutral on-device GLSL compiler contract for
 * portablegl's PGL_G3D GPU path.
 *
 * LAYERING
 * --------
 * The shared FRONTEND (projects/soft3d/libs/gles/pglcompiler, vendored mesa
 * GLSL -> NIR) knows nothing about any GPU.  Every function declared here is
 * implemented by the MACHINE BACKEND library selected at build time through
 * PGL_G3D_COMPILER_DIR (exported from machines/<hw>/system/make.inc):
 *
 *   machines/raspi5|raspix/system/libs/v3dcompiler    VideoCore V3D (NIR->QPU)
 *   machines/q8b/system/libs/adrenocompiler           Adreno (NIR->ir3)
 *
 * The backend drives the frontend through pglcompiler_frontend.h, compiles
 * the resulting NIR with its own mesa backend, and packs the result into the
 * neutral pglc_stage below.  Everything the daemon needs that is NOT neutral
 * (V3D uniform streams and VPM sizes, ir3 const/regid state, ...) lives
 * behind the opaque `backend` pointer and only ever crosses the client/daemon
 * boundary inside the wire blob produced by pglc_program_serialize(), whose
 * layout is likewise owned by the backend (g2d_pgl_program_blob_t on V3D,
 * adreno_prog_blob_t on Adreno).  portablegl therefore never interprets GPU
 * code: it compiles, reflects uniforms by name, serializes and uploads.
 *
 * The whole compile runs in user space in the calling app's process; the
 * blob is then marshalled through g2dclient to the g2dd daemon, the only
 * entity that may touch the GPU.
 */
#ifndef PGLCOMPILER_H
#define PGLCOMPILER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stage slots of a pglc_program.  COORD is V3D's binning variant of the
 * vertex stage (the same VS NIR compiled to output gl_Position only); other
 * backends leave it empty (code_words == 0). */
enum pglc_stage_id {
   PGLC_STAGE_VERTEX = 0,
   PGLC_STAGE_FRAGMENT,
   PGLC_STAGE_COORD,
   PGLC_STAGE_COMPUTE,
   PGLC_STAGE_COUNT
};

#define PGLC_MAX_UNIF_NAME 64

/* Name -> dword-offset reflection of one default-block uniform that
 * survived linking.  The client packs the per-draw uniform blob BY NAME
 * through this table: offsets differ per program because the linker
 * dead-strips uniforms the shader never reads, so a fixed client-side layout
 * would be wrong.  dword_offset is the uniform's position in the blob,
 * num_components its 32-bit component count (a mat4 is 16). */
struct pglc_uniform_binding {
   char     name[PGLC_MAX_UNIF_NAME];
   uint32_t dword_offset;
   uint32_t num_components;
};

/* One compiled stage.  code is a malloc()ed little-endian u32 stream of
 * backend ISA words (V3D: QPU instructions as lo/hi pairs; Adreno: ir3
 * instruction dwords); code_words == 0 means the stage does not exist.  The
 * uniform reflection is backend-neutral; `backend` is the backend's private
 * prog_data snapshot (V3D uniform stream + VPM/FS fields, ir3 const state,
 * ...), owned and released by the backend in pglc_program_free(). */
struct pglc_stage {
   uint32_t *code;
   uint32_t  code_words;

   uint32_t  num_uniform_bindings;
   struct pglc_uniform_binding *uniform_bindings;
   uint32_t  uniform_blob_dwords;      /* span of the per-draw uniform blob */

   uint32_t  local_size[3];            /* compute stage workgroup shape */

   void     *backend;
};

struct pglc_program {
   bool success;
   char log[1024];
   struct pglc_stage stage[PGLC_STAGE_COUNT];
};

/* Compile a vertex+fragment GLSL pair (the binner/draw route).  A backend
 * without a graphics pipeline port returns false and says so in res->log;
 * portablegl then keeps the program on its CPU rasterizer.  res is fully
 * initialised either way; release with pglc_program_free(). */
bool pglc_compile_glsl(const char *vert_src, const char *frag_src,
                       struct pglc_program *res);

/* Compile a shadertoy-style fragment shader as a one-pixel-per-invocation
 * compute kernel writing packed ARGB8888 into SSBO binding 0 (see
 * pglc_fe_fragment_to_compute_source).  Fills res->stage[PGLC_STAGE_COMPUTE]. */
bool pglc_compile_compute(const char *frag_src, struct pglc_program *res);

/* Compile a verbatim GLSL ES 3.10 compute shader (e.g. the geom_raster_cs.h
 * kernels) without any wrapping.  Fills res->stage[PGLC_STAGE_COMPUTE]. */
bool pglc_compile_compute_raw(const char *cs_src, struct pglc_program *res);

/* Override the workgroup shape pglc_compile_compute() wraps fragment effects
 * in (default 32x8; x*y must be <= 256).  For A/B measurements. */
void pglc_compile_set_cs_local_size(unsigned x, unsigned y);

/* Release everything a pglc_program owns.  Safe on a zeroed/failed result. */
void pglc_program_free(struct pglc_program *res);

/* GPU generation selector.  `ver` is the value g2d_get_version() reports: a
 * backend-neutral GPU code interpreted by the machine backend (V3D 42 = Pi4,
 * 71 = Pi5; Adreno 690).  pglc_set_version() must be called before the
 * first compile; pglc_version_supported() is portablegl's gate for enabling
 * the GPU path at all (replaces any numeric comparison in the client). */
void pglc_set_version(int ver);
bool pglc_version_supported(uint32_t ver);

/* Options for serializing a program into the daemon's wire blob.  geom_kind
 * is the geometry-rasterizer dispatch kind the daemon switches on (0 = plain
 * fullscreen/binner program, 1 = gouraud, 2 = textured, 3 = instanced,
 * 4 = colour-modulated textured); geom_tpw is the raster pass's
 * triangles-per-workgroup (0 = one workgroup per triangle). */
struct pglc_blob_opts {
   uint8_t  geom_kind;
   uint32_t geom_tpw;
};

/* Serialize `main` (VS/FS/COORD/CS stages) plus the optional `xform`
 * program's COMPUTE stage (the two-pass geometry rasterizer's vertex
 * transform; may be NULL or have code_words == 0) into the byte buffer
 * bsp_g3d_program() expects on this machine.  With out == NULL returns the
 * required size; otherwise writes up to cap bytes and returns the number
 * written (0 on error / cap too small). */
uint32_t pglc_program_serialize(const struct pglc_program *main_prog,
                                const struct pglc_program *xform_prog,
                                const struct pglc_blob_opts *opts,
                                void *out, uint32_t cap);

/* 2D texture memory layout for the GPU's sampler.  V3D tiles level 0
 * (LT/UBLINEAR/UIF, chosen from the image size); Adreno's compute texture
 * kernels read linear raster memory (tiling == 0, stride == w*cpp).  The
 * tiling value is backend-defined and only meaningful to that machine's
 * daemon, except that PGLC_TEX_TILING_RASTER (0) always means plain linear
 * rows on every backend (the client uses it to pick the fixed-function
 * linear-texture kernels). */
#define PGLC_TEX_TILING_RASTER 0u

struct pglc_tex_layout {
   uint32_t tiling;
   uint32_t ub_pad;
   uint32_t padded_w;
   uint32_t padded_h;
   uint32_t stride;
   uint32_t size;
};

bool pglc_tex_layout_2d(uint32_t w, uint32_t h, uint32_t cpp,
                        struct pglc_tex_layout *out);

/* Copy a raster image (src, src_stride bytes/row) into `dst` laid out per
 * `l` (dst must hold l->size bytes). */
void pglc_tex_store_2d(void *dst, const void *src, uint32_t src_stride,
                       uint32_t w, uint32_t h, uint32_t cpp,
                       const struct pglc_tex_layout *l);

/* Whether this backend's GPU texture sampler can read a PGLC_TEX_TILING_RASTER
 * (plain linear row-major) image directly.  V3D's TMU requires a tiled level 0
 * (LT / UBLINEAR / UIF), so it returns false and the client leaves a raster
 * slot unbound (the draw then declines rather than samples a scrambled image);
 * Adreno's a6xx compute sampler builds TILE6_LINEAR texture descriptors, so it
 * returns true and the client binds raster slots for the daemon to sample.
 * Implemented by every backend selected through PGL_G3D_COMPILER_DIR. */
bool pglc_tex_sampler_reads_raster(void);

#ifdef __cplusplus
}
#endif

#endif /* PGLCOMPILER_H */
