/*
 * spirv_to_glsl.h - the SPIR-V -> GLSL-ES-3.10 transpiler used by the GPU device
 * backend (the `clc_to_glsl` analog for Vulkan).  It converts one parsed entry
 * point of a spirv_module into a verbatim shader source string the on-device
 * mesa compiler accepts: a compute entry through pglc_compile_compute_raw(), a
 * vertex+fragment pair through pglc_compile_glsl().
 *
 * The GPU subset is deliberately narrower than the software interpreter's, and
 * declining is always safe (it can only cost speed, never correctness): the
 * transpiler returns NULL with a short reason whenever it meets a construct it
 * cannot faithfully express, and the caller then runs the shader on the software
 * device.  Concretely it covers:
 *   - float/int/uint/bool scalars and 2/3/4-vectors plus square float matrices;
 *   - StorageBuffer (SSBO) descriptor resources, images and combined samplers;
 *   - the Vulkan built-in interface (GlobalInvocationId & friends for compute,
 *     VertexIndex/InstanceIndex for vertex, FragCoord/FrontFacing for fragment)
 *     mapped straight onto the matching GLSL built-ins, and location in/outs;
 *   - scalar/vector/matrix arithmetic, comparison, logic, conversion, select,
 *     composite construct/extract/insert/shuffle, load/store through simple
 *     access chains, structured if/else and while control flow (out-of-SSA phi
 *     lowering), and the common GLSL.std.450 extended instructions.
 * It DECLINES: function calls, switch, atomics, barriers, workgroup shared
 * memory, irregular control flow, and any opcode/type outside the list above.
 *
 * Uniform (UBO) and PushConstant resources are handled per mode.  The
 * single-entry spirv_to_glsl() path (a raw compute / VS / FS shader) still
 * declines them.  The two-pass kernel generator spirv_to_geom_kernels() instead
 * FLATTENS each scalar/vector/contiguous-matrix member of a set-0 UBO or of the
 * push-constant block into a standalone default-block uniform, recording it in
 * spirv_glsl_flat so the GPU backend can fill the reflected blob slot per draw;
 * nested structs, arrays, non-set-0 UBOs and non-contiguous matrices are
 * declined.
 */
#ifndef VULKAN_SPIRV_TO_GLSL_H
#define VULKAN_SPIRV_TO_GLSL_H

#include "spirv.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* V3D 7.1 compute exposes exactly four SSBO binding points (the same limit
 * clc_to_glsl documents).  A compute entry referencing more storage buffers is
 * declined so it stays on the software device. */
#define SPIRV_GLSL_MAX_SSBO 4

/* Maximum number of UBO / push-constant members one entry flattens into
 * default-block uniforms (see spirv_glsl_flat). */
#define SPIRV_MAX_FLAT 32

/* One Uniform-buffer (UBO) or push-constant struct member that the transpiler
 * FLATTENED into a standalone default-block uniform, because the V3D uniform
 * blob reflects loose uniforms by name (a mat4 member is one 16-component slot).
 * The GPU backend fills it per draw: it reads `comps` 32-bit words from the
 * bound buffer (a UBO, at descriptor set/binding) or from the push-constant
 * snapshot at byte `offset`, and writes them into the reflected blob slot named
 * `name`.  The byte offset is taken verbatim from the SPIR-V Offset decoration,
 * so it matches exactly what the software interpreter reads.  `var`/`member`
 * are transient (the transpiler's access-chain resolver keys on them); the
 * backend uses only name/set/binding/offset/comps/is_push. */
typedef struct spirv_glsl_flat {
    char     name[32];   /* emitted default-block uniform name                */
    uint32_t set;        /* UBO descriptor set (ignored when is_push)         */
    uint32_t binding;    /* UBO descriptor binding                            */
    uint32_t offset;     /* member byte offset within the block               */
    uint32_t comps;      /* 32-bit components in the blob (mat4 = 16)         */
    uint8_t  is_push;    /* 1 = push-constant block, 0 = uniform buffer       */
    uint32_t var;        /* OpVariable id of the block (access-chain resolve) */
    uint32_t member;     /* struct member index                               */
} spirv_glsl_flat;

/* One descriptor resource as realised in the emitted GLSL.  The GPU backend
 * uses `binding` (the flattened GLSL binding point) plus `set`/`binding_src`
 * (the original Vulkan descriptor coordinates) to bind the right buffer/image. */
typedef struct spirv_glsl_binding {
    uint32_t       set;          /* Vulkan descriptor set                    */
    uint32_t       binding_src;  /* Vulkan binding within the set            */
    spirv_res_kind kind;
    uint32_t       binding;      /* flattened GLSL binding point             */
    char           name[32];     /* GLSL block/variable name                 */
} spirv_glsl_binding;

/* Everything the GPU backend needs to drive a transpiled entry point. */
typedef struct spirv_glsl_info {
    uint32_t exec_model;         /* SPIRV_EXEC_* of the transpiled entry     */
    uint32_t local_size[3];      /* GLCompute workgroup shape (baked)        */
    int      num_bindings;
    spirv_glsl_binding bindings[SPIRV_MAX_BINDINGS];
    /* location interface the backend must wire up (vertex/fragment). */
    int      num_inputs, num_outputs;
    uint32_t in_locations[SPIRV_MAX_LOCATIONS];
    uint32_t out_locations[SPIRV_MAX_LOCATIONS];
} spirv_glsl_info;

/* Transpile entry point `e` of `m` to GLSL ES 3.10.  On success returns a
 * malloc()ed NUL-terminated source string the caller frees and fills *info; on
 * a declined entry returns NULL and writes a short reason into err (at most
 * errsz-1 chars, NUL-terminated). */
char *spirv_to_glsl(spirv_module *m, const spirv_entry *e,
                    spirv_glsl_info *info, char *err, size_t errsz);

/* ---- geometry-kernel generation mode (raspi5 V3D two-pass rasterizer) -----
 * spirv_to_geom_kernels() turns a Vulkan vertex entry (and an optional
 * fragment entry, possibly from a DIFFERENT spirv_module) into the two GLSL ES
 * 3.10 compute kernels the raspi5 V3D geometry rasterizer dispatches through
 * draw_compute_geom() (the same shape as the hand-written kernels in
 * pglcompiler's geom_raster_cs.h, geom_kind 5):
 *
 *   pass 1 "xform"  - local_size 8x8x1, one invocation per vertex, grid =
 *       ceil(nverts/64).  Reads the per-vertex attribute float stream from the
 *       vattr SSBO (binding 1), runs the user VS body, applies the viewport
 *       transform (uniform uVpOffsetScale/uVpDepth) and writes a packed record
 *       into the vout SSBO (binding 0): [0]=screen_x [1]=screen_y [2]=z(0..1)
 *       [3]=iw(1/w, 0 as the near-plane clip sentinel) [4..7]=reserved, then
 *       every VS output location flattened in the order recorded in
 *       spirv_geom_layout.varyings (offset >= 8).  Record stride (floats) is
 *       vout_stride = 8 + 4*ceil(nvarfloats/4); the per-vertex attribute stride
 *       (floats) is attr_stride.
 *
 *   pass 2 "raster" - local_size 16x16x1, ONE LANE PER TRIANGLE: lane L of
 *       workgroup W owns triangle W*256+L and sweeps its screen bbox serially
 *       (cs_geom_tpw = 256, so the daemon dispatches ceil(ntris/256) workgroups).
 *       Bindings: 0 fb(uint[]) 1 vout(float[]) 2 depth(uint[]); fragment
 *       textures stay TMU sampler2D uniforms (they do NOT take an SSBO slot).
 *       The harness reproduces cpu_raster.c pixel-exactly (same edge/barycentric
 *       sign convention, front = area < 0, Vulkan viewport y-down, perspective-
 *       correct interpolation on vout.iw, 24-bit unorm atomicMin depth), injects
 *       the interpolated varyings + gl_FragCoord/gl_FrontFacing into the user FS
 *       body, and packs the FS colour into the fb word.  discard lowers to a
 *       per-pixel skip.
 *
 * ALL render state is uniform-driven (viewport, depth func/write, cull mode +
 * winding, scissor, blend mode, fb component swizzle), so one compiled kernel
 * pair serves any pipeline state inside the supported subset; the GPU backend
 * patches the uniforms per draw.  Everything outside the subset (non-float
 * interface, gl_InstanceID, multiple colour targets, UBO/push-constant, storage
 * buffers in a graphics stage, >4 samplers, a vec3 colour output) DECLINES, so
 * the draw safely stays on the software rasterizer.
 *
 * Returns 0 on success (out->xform and out->raster are malloc()ed source the
 * caller frees, out->layout describes the wire/stream layout); -1 on decline
 * with a short reason in err and out->xform/out->raster left NULL. */
#define SPIRV_GEOM_MAX_SAMPLERS 4

typedef struct spirv_geom_slot {
    uint32_t location;   /* SPIR-V location                                   */
    uint32_t offset;     /* float offset within the per-vertex record         */
    uint32_t comps;      /* component count (1..4)                            */
} spirv_geom_slot;

typedef struct spirv_geom_layout {
    uint32_t attr_stride;        /* floats per vertex in the vattr stream     */
    uint32_t vout_stride;        /* floats per vertex in the vout record      */
    int      num_attrs;          /* VS inputs, in stream order                */
    spirv_geom_slot attrs[SPIRV_MAX_LOCATIONS];
    int      num_varyings;       /* VS outputs (excl. Position), record order  */
    spirv_geom_slot varyings[SPIRV_MAX_LOCATIONS];
    int      num_samplers;       /* FS combined-image samplers (TMU)          */
    uint32_t sampler_set[SPIRV_GEOM_MAX_SAMPLERS];
    uint32_t sampler_binding[SPIRV_GEOM_MAX_SAMPLERS];
    int      uses_vertex_id;     /* VS reads gl_VertexID (gate indexed draws) */
    int      fs_present;         /* 0 => colour = interpolated varying loc 0   */
    /* UBO / push-constant members flattened into default-block uniforms across
     * BOTH stages, de-duplicated by name (a uniform both stages read appears
     * once; the backend writes it into each stage's reflected blob region). */
    int      num_flat;
    spirv_glsl_flat flat[SPIRV_MAX_FLAT];
} spirv_geom_layout;

typedef struct spirv_geom_kernels {
    char             *xform;     /* pass 1 GLSL source (malloc'd) or NULL      */
    char             *raster;    /* pass 2 GLSL source (malloc'd) or NULL      */
    spirv_geom_layout layout;
} spirv_geom_kernels;

int spirv_to_geom_kernels(spirv_module *vmod, const spirv_entry *vs,
                          spirv_module *fmod, const spirv_entry *fs,
                          spirv_geom_kernels *out, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_SPIRV_TO_GLSL_H */
