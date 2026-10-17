/* libv3dcompiler public API - on-device GLSL -> VideoCore V3D 7.1 QPU compiler.
 *
 * This is the ONLY header portablegl (pgl_g3d) includes from the vendored mesa
 * tree. It exposes a single compilation entry point; every mesa symbol stays
 * hidden inside libv3dcompiler.a. raspi5 (Pi5 VideoCore VII) only.
 *
 * The compiler runs entirely in user space in the calling process: GLSL source
 * goes in, packed QPU instruction words come out. Those words are then marshalled
 * by pgl_g3d -> libg2dclient -> the g2dd root daemon, which is the only entity
 * that may map the V3D MMIO and submit to the GPU (apps cannot touch V3D
 * directly). Nothing here is CPU rasterization - the output is real GPU code.
 *
 * Upstream mesa pin f45825a0c724d06f231a1210484fe9af71a3ffab. See VENDORED.txt.
 */
#ifndef V3DCOMPILER_H
#define V3DCOMPILER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shader stages understood by the on-device compiler. */
enum v3d_shader_stage {
   V3D_STAGE_VERTEX   = 0,
   V3D_STAGE_FRAGMENT = 1,
   /* Coordinate-shader (binning) variant of the vertex stage: the SAME VS NIR
    * compiled with v3d_vs_key.is_coord = true, so it writes only gl_Position
    * (no varyings).  On this stage the QPU stream, the uniform stream
    * (uniform_contents/uniform_data/num_uniforms), the named-uniform
    * reflection and the VPM size fields (vpm_input_size / vpm_output_size /
    * separate_segments / vcm_cache_size) are meaningful; the FS-only fields,
    * the attribute map and num_outputs are zero.  Arbitrary-geometry draws
    * (e.g. the teapot mesh) REQUIRE it: the V3D binner runs the coordinate
    * shader to compute each vertex's clip-space position for tile binning,
    * and a fixed pass-through coord shader applies no model/view/projection
    * transform.  Because it is the same linked VS NIR, the coord variant
    * shares the render VS's uniform driver_locations, so one per-draw uniform
    * blob packed from the VS reflection feeds BOTH the coord and VS uniform
    * expansion. */
   V3D_STAGE_COORD    = 2,
   /* Compute-shader stage (CSD route).  Populated ONLY by v3d_compile_compute()
    * (never by v3d_compile_glsl).  On this stage the QPU stream, the uniform
    * stream, the named-uniform reflection, threads/single_seg and the compute
    * dispatch shape (local_size[]/shared_size) are meaningful; the VS attribute
    * map, the VPM sizes and every FS-only field are zero.  The compiled compute
    * shader reads its per-thread workgroup/local ids from CSD hardware registers
    * (r2/r3 on V3D 7.1) and writes colour to memory via imageStore/SSBO rather
    * than the TLB, so it runs through the proven CSD dispatch, not the binner. */
   V3D_STAGE_COMPUTE  = 3,
   V3D_STAGE_COUNT    = 4,
};

/* Attribute / varying slot limits, mirroring mesa's v3d_limits.h
 * (V3D_MAX_VS_INPUTS / V3D_MAX_FS_INPUTS). They are re-declared here so this
 * header stays free of any mesa include; if the vendored mesa pin ever raises
 * them, these must be raised in lock-step (v3dcompiler_backend.c static-checks
 * the match at build time). */
#define V3DCOMPILER_MAX_VS_INPUTS 64
#define V3DCOMPILER_MAX_FS_INPUTS 64

/* Max length (incl. NUL) of a reflected default-block uniform name. */
#define V3DCOMPILER_MAX_UNIF_NAME 64

/* One named default-block uniform a stage references, with the dword offset its
 * load_uniform was assigned in that stage's per-draw uniform blob.
 *
 * The client MUST pack per-draw uniform values BY NAME through this table,
 * never by a fixed [iGlobalTime, iResolution.xyz] layout: mesa's linker
 * dead-strips uniforms the shader never reads, which shifts the surviving
 * uniforms' dword offsets per effect.  Verified with the WS6 host oracle:
 * effects that never read iGlobalTime (e.g. graphing_lines / graphing /
 * voronoise) place iResolution.x at dword 0, while effects that do read it
 * place iGlobalTime at dword 0 and iResolution.x at dword 1.  dword_offset is
 * exactly the QUNIFORM_UNIFORM data value the backend emits (the load_uniform
 * byte base / 4); num_components is the variable's consecutive-dword width
 * (scalar = 1, vec3 = 3).  Opaque uniforms (samplers/images) are excluded:
 * they ride the texture path, not the uniform blob. */
struct v3d_uniform_binding {
   char     name[V3DCOMPILER_MAX_UNIF_NAME];
   uint32_t dword_offset;
   uint32_t num_components;
};

/* One compiled pipeline stage.
 *
 * A V3D 7.1 QPU instruction is 64 bits. `qpu` stores the packed program as a
 * flat array of little-endian u32 words, two per instruction (lo then hi), so
 * qpu_word_count == 2 * qpu_instr_count. The buffer is owned by the enclosing
 * v3d_compiled_program and released by v3d_compiled_program_free().
 *
 * The uniform/attribute metadata fields are populated by the WS2-entry
 * implementation so pgl_g3d can build the V3D shader-state records without
 * re-parsing the GLSL.
 *
 * PROG_DATA SURFACE (WS4): the fields below the QPU stream carry the slice of
 * mesa's v3d_prog_data the g2dd daemon needs to build the 32-byte GL
 * shader-state record, the two attribute records and the per-draw uniform
 * buffer for compiler-generated QPU. They are a POD, deep-copied snapshot -
 * the ralloc'd prog_data is freed inside libv3dcompiler.a before this returns,
 * so every array here is owned by the v3d_compiled_program and released by
 * v3d_compiled_program_free(). Fields that only apply to one stage are zero on
 * the other. */
struct v3d_compiled_stage {
   uint32_t *qpu;
   uint32_t  qpu_word_count;
   uint32_t  qpu_instr_count;

   uint32_t  num_uniforms;      /* uniform slots referenced by the stage */
   uint32_t  num_outputs;       /* varyings written (VS) / colour outputs (FS) */
   bool      needs_coords;      /* stage reads gl_FragCoord / vertex position */

   /* --- Uniform stream -------------------------------------------------
    * The ordered (contents, data) pairs the driver expands into the stage's
    * uniform buffer at draw time, one entry per uniform slot. contents[i] is
    * an `enum quniform_contents` value (QUNIFORM_CONSTANT, QUNIFORM_UNIFORM,
    * QUNIFORM_VIEWPORT_X_SCALE, QUNIFORM_TMU_CONFIG_P0, ...) stored as u32;
    * data[i] is its 32-bit argument (an immediate value for QUNIFORM_CONSTANT,
    * a texture/UBO index or scale otherwise). Both arrays are num_uniforms
    * long, heap-owned, and NULL when num_uniforms == 0. */
   uint32_t *uniform_contents;
   uint32_t *uniform_data;

   /* --- Named default-block uniform reflection -------------------------
    * The default-block (non-opaque) uniforms this stage references, each with
    * the dword offset its load_uniform was assigned -- see struct
    * v3d_uniform_binding.  Heap-owned by the enclosing v3d_compiled_program
    * (NULL when num_uniform_bindings == 0) and released by
    * v3d_compiled_program_free().  The client packs the per-draw uniform blob
    * by name through this table.
    *
    * uniform_blob_dwords is the total dword span the client's per-draw blob
    * must cover: max(uniform_bindings[i].dword_offset +
    * uniform_bindings[i].num_components) over i, or 0 when the stage
    * references no default-block uniform.  The daemon's expand_uniforms()
    * indexes the blob by the QUNIFORM_UNIFORM data value (== dword_offset),
    * so a blob of exactly this many u32 words (float bits for GL float
    * uniforms) covers every referenced slot with no waste. */
   uint32_t num_uniform_bindings;
   struct v3d_uniform_binding *uniform_bindings;
   uint32_t uniform_blob_dwords;

   /* --- Dispatch shape (base prog_data) -------------------------------- */
   uint8_t   threads;           /* SIMD dispatch width (base.threads) */
   bool      single_seg;        /* dispatch in the after-final-THRSW state */

   /* --- Compute stage only (zero on VS/FS/COORD) -----------------------
    * v3d_compute_prog_data: the declared local workgroup size (x,y,z) and the
    * workgroup shared-memory size in bytes.  The CSD dispatcher derives the
    * workgroup grid from local_size and the frame size, and programs
    * shared_size into the CSD shared-memory config. */
   uint32_t  local_size[3];
   uint32_t  shared_size;

   /* --- Vertex stage only (zero on FS) ---------------------------------
    * VPM segment sizes and the attribute->driver_location map the daemon
    * turns into the shader-record VPM config and the attribute records. */
   uint32_t  vpm_input_size;
   uint32_t  vpm_output_size;
   bool      separate_segments; /* separate VPM input/output segments */
   uint32_t  vcm_cache_size;    /* value programmed into VCM_CACHE_SIZE */
   uint8_t   vattr_sizes[V3DCOMPILER_MAX_VS_INPUTS / 4]; /* components per attr */
   int32_t   driver_location_map[V3DCOMPILER_MAX_VS_INPUTS]; /* loc->drv, -1 unused */

   /* --- Fragment stage only (zero/false on VS) ------------------------- */
   uint32_t  num_inputs;        /* varying inputs read (== VS num_outputs) */
   uint32_t  input_slots[V3DCOMPILER_MAX_FS_INPUTS]; /* packed slot_and_component
                                                        * ((varying_slot << 2) | component)
                                                        * per FS varying input */
   bool      uses_center_w;     /* real_pixel_centre_mode in the record */
   bool      writes_z;          /* FS writes gl_FragDepth */
   bool      disable_ez;        /* early-Z must be disabled for this FS */
   bool      uses_implicit_point_line_varyings;
};

/* Result of compiling a GLSL vertex+fragment pair. */
struct v3d_compiled_program {
   bool success;
   char log[1024];              /* GLSL info-log; compile diagnostics on failure */
   struct v3d_compiled_stage stage[V3D_STAGE_COUNT];
};

/* Compile a GLSL ES vertex+fragment shader pair to VideoCore V3D 7.1 QPU.
 *
 * vert_src / frag_src are NUL-terminated GLSL source strings (either may be
 * NULL to skip that stage). On success returns true and fills res->stage[];
 * the caller must later release them with v3d_compiled_program_free(). On
 * failure returns false and writes human-readable diagnostics to res->log.
 *
 * This is the single entry point of libv3dcompiler.a. */
bool v3d_compile_glsl(const char *vert_src, const char *frag_src,
                      struct v3d_compiled_program *res);

/* Compile a shadertoy-style GLSL ES 1.00 FRAGMENT shader as a V3D 7.1 COMPUTE
 * shader (CSD route).
 *
 * frag_src is the same NUL-terminated fragment source v3d_compile_glsl takes
 * (gl_FragCoord / gl_FragColor / texture2D / iResolution ...).  It is rewritten
 * internally into a GLSL ES 3.10 compute shader (local_size 32x8x1 by default,
 * see v3d_compile_set_cs_local_size) whose
 * main() evaluates one pixel per invocation (gl_FragCoord.y = row from the top,
 * matching the CPU path) and stores the clamped, packed ARGB8888 result into
 * SSBO binding 0 at dword [y * v3d_cs_stride + x].  The dispatcher supplies
 * QUNIFORM_SSBO_OFFSET[0] = frame-buffer address and the named uniform
 * "v3d_cs_stride" (dwords per row) through the normal uniform blob, and runs
 * a CSD workgrid of (ceil(w/local_x), ceil(h/local_y), 1).  On success returns true and fills
 * res->stage[V3D_STAGE_COMPUTE] (qpu, uniform stream + reflection,
 * threads/single_seg, local_size[], shared_size); release with
 * v3d_compiled_program_free().  On failure returns false and writes
 * diagnostics to res->log.  The other stage slots are left zeroed. */
bool v3d_compile_compute(const char *frag_src,
                         struct v3d_compiled_program *res);

/* Compile a hand-written, verbatim GLSL ES 3.10 COMPUTE shader into
 * res->stage[V3D_STAGE_COMPUTE], with NONE of the fragment-shader wrapping that
 * v3d_compile_compute() applies.  This is the entry for kernels like the fixed
 * geometry rasterizer (geom_raster_cs.h) that declare their own SSBO bindings,
 * uniforms and workgroup size.  cs_src is neither modified nor freed.  SSBO
 * QUNIFORM_SSBO_OFFSET slots are remapped from linked block index to declared
 * binding before packing.  On success returns true; on failure false + res->log. */
bool v3d_compile_compute_raw(const char *cs_src,
                             struct v3d_compiled_program *res);

/* Tuning knob for v3d_compile_compute(): workgroup shape used for the
 * generated compute prologue (default 32x8x1 = 256, the V3D maximum and the
 * fastest shape measured on the Pi 5).  x*y must be <= 256; a multiple of 16
 * keeps one workgroup == whole QPU batches.  Affects only compiles issued
 * after the call. */
void v3d_compile_set_cs_local_size(unsigned x, unsigned y);

/* Select the V3D generation the compiler targets, by live version
 * (ver == major*10 + minor: 42 = V3D 4.2 / Pi4, 71 = V3D 7.1 / Pi5).  Any
 * other value leaves the default (71) in place.  Call ONCE at startup, before
 * the first v3d_compile_*(), on a machine that ships one binary across chip
 * generations (raspix: it queries the actual V3D version over the g2d
 * dev-cntl channel and passes it here).  raspi5 targets a single chip (Pi5 /
 * ver 71) and never calls this, so its emitted QPU is byte-for-byte unchanged.
 * Affects only compiles issued after the call. */
void v3d_compiler_set_version(int ver);

/* Release any QPU buffers owned by a successful compile. Safe to call on a
 * failed/zeroed result. */
void v3d_compiled_program_free(struct v3d_compiled_program *res);

/* --- V3D 2D texture memory layout --------------------------------------
 *
 * The V3D TMU cannot sample a raster (row-major) 2D image: level 0 must be in
 * the hardware tiling layout the gallium driver picks from the image size
 * (v3d_resource.c v3d_setup_slices).  Values mirror enum v3d_tiling_mode in
 * broadcom/common/v3d_tiling.h one-for-one; RASTER is never chosen for a 2D
 * texture but is kept so callers can store "linear" explicitly. */
enum v3d_tex_tiling {
   V3D_TEX_TILING_RASTER = 0,
   V3D_TEX_TILING_LINEARTILE,
   V3D_TEX_TILING_UBLINEAR_1_COLUMN,
   V3D_TEX_TILING_UBLINEAR_2_COLUMN,
   V3D_TEX_TILING_UIF_NO_XOR,
   V3D_TEX_TILING_UIF_XOR,
};

struct v3d_tex_layout {
   uint32_t tiling;    /* enum v3d_tex_tiling */
   uint32_t ub_pad;    /* TEXTURE_SHADER_STATE level_0_ub_pad (UIF only) */
   uint32_t padded_w;  /* allocated image width in pixels (>= w) */
   uint32_t padded_h;  /* allocated image height in pixels (>= h), incl. ub_pad */
   uint32_t stride;    /* padded_w * cpp */
   uint32_t size;      /* stride * padded_h: bytes the tiled image occupies */
};

/* Pick the single-level 2D layout for a w x h image of cpp bytes per pixel
 * (cpp in {1,2,4,8,16}).  Returns false on bad arguments. */
bool v3d_tex_layout_2d(uint32_t w, uint32_t h, uint32_t cpp,
                       struct v3d_tex_layout *out);

/* Shuffle the raster image src (row stride src_stride bytes) into dst laid
 * out per *layout (dst must hold layout->size bytes; padding is zeroed). */
void v3d_tex_store_2d(void *dst, const void *src, uint32_t src_stride,
                      uint32_t w, uint32_t h, uint32_t cpp,
                      const struct v3d_tex_layout *layout);

#ifdef __cplusplus
}
#endif

#endif /* V3DCOMPILER_H */
