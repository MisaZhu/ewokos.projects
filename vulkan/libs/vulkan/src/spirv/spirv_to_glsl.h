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
 * It DECLINES: Uniform (UBO) and PushConstant resources, function calls, switch,
 * atomics, barriers, workgroup shared memory, irregular control flow, and any
 * opcode/type outside the list above.
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

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_SPIRV_TO_GLSL_H */
