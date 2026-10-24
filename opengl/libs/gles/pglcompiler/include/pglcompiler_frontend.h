/* pglcompiler_frontend.h - internal API of the shared GLSL -> NIR frontend,
 * consumed ONLY by the machine GPU compiler backends (the libraries under
 * PGL_G3D_COMPILER_DIR that implement pglcompiler.h).
 *
 * The frontend drives mesa's GLSL compiler (preprocess -> AST -> HIR -> NIR
 * -> link) exactly the way mesa's own standalone compiler does, then applies
 * the lowering the gallium state tracker would normally do before handing a
 * shader to a driver (default-block uniform derefs -> load_uniform with dense
 * byte offsets, sampler derefs -> indices, SSBO derefs -> *_ssbo intrinsics).
 * The backend supplies its nir_shader_compiler_options, receives the linked
 * NIR, runs its own NIR->ISA compiler, and packs the result into pglc_stage.
 *
 * Only nir.h crosses this boundary: it has a proper `extern "C"` guard and is
 * routinely included from mesa's own C++ frontend, so the header is safe to
 * include from the C or C++ side of a backend.  The frontend itself is C++
 * (the GLSL entry points have C++ linkage); backends are free to be C.
 *
 * Platform-INDEPENDENT.  Upstream pin f45825a0c724d06f231a1210484fe9af71a3ffab,
 * see VENDORED.txt.
 */
#ifndef PGLCOMPILER_FRONTEND_H
#define PGLCOMPILER_FRONTEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "compiler/nir/nir.h"
#include "pglcompiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A linked shader program: owns the driver-less gl_context and the mesa
 * gl_shader_program the linked NIR is ralloc'd under.  The NIR returned by
 * pglc_fe_nir() stays valid until pglc_fe_shaders_free(); backends that
 * mutate it in place (ir3_finalize_nir etc.) should nir_shader_clone() first
 * if they need the original again (V3D's v3d_compile() clones internally). */
struct pglc_fe_shaders;

/* Compile + link a vertex/fragment pair.  `opts` is the backend's
 * nir_shader_compiler_options (installed for all stages; must outlive the
 * call).  On success *out holds the linked program with:
 *   - gl_FragColor renamed to gl_FragData[0] (FRAG_RESULT_DATA0),
 *   - dense byte driver_locations assigned to default-block uniforms and
 *     nir_lower_io(nir_var_uniform) applied (load_uniform base/offset are
 *     BYTES; dword index = byte / 4),
 *   - dense per-attribute driver_locations (0,1,2,...) on VS inputs in
 *     layout(location) order,
 *   - gl_nir_lower_samplers applied.
 * On failure returns false, *out == NULL and the GLSL/link diagnostics are
 * appended to log (logsz bytes, NUL-terminated). */
bool pglc_fe_compile_glsl(const char *vert_src, const char *frag_src,
                          const struct nir_shader_compiler_options *opts,
                          struct pglc_fe_shaders **out,
                          char *log, size_t logsz);

/* Compile + link a GLSL ES 3.10 compute shader with the same uniform/sampler
 * lowering plus gl_nir_lower_buffers (SSBO derefs -> load/store_ssbo whose
 * block index is the LINKED block index, see pglc_fe_ssbo_binding). */
bool pglc_fe_compile_compute(const char *cs_src,
                             const struct nir_shader_compiler_options *opts,
                             struct pglc_fe_shaders **out,
                             char *log, size_t logsz);

/* Linked NIR of one stage (PGLC_STAGE_VERTEX / FRAGMENT / COMPUTE; COORD
 * aliases VERTEX).  NULL when the program has no such stage. */
nir_shader *pglc_fe_nir(const struct pglc_fe_shaders *sh, enum pglc_stage_id stage);

/* SSBO block-index -> binding remap for the compute stage.  gl_nir_lower_buffers
 * records the linked BLOCK INDEX in each ssbo intrinsic, but the daemon binds
 * buffers BY DECLARED BINDING (layout(binding = N)); with several SSBOs the
 * linker orders blocks by hash iteration, permuting the mapping, so backends
 * must translate every block index through this table before serializing.
 * Returns the number of linked SSBO blocks (0 for non-compute programs) and
 * the binding of block `index` (index itself when out of range). */
uint32_t pglc_fe_num_ssbos(const struct pglc_fe_shaders *sh);
uint32_t pglc_fe_ssbo_binding(const struct pglc_fe_shaders *sh, uint32_t index);

void pglc_fe_shaders_free(struct pglc_fe_shaders *sh);

/* Rewrite a shadertoy-style GLSL ES 1.00 fragment shader into a GLSL ES 3.10
 * compute shader that evaluates one pixel per invocation and stores packed
 * ARGB8888 into `layout(std430, binding = 0) buffer PglCsOut`, indexed
 * row * pglc_cs_stride + x (uniform int pglc_cs_stride), with the workgroup
 * shape last set by pglc_compile_set_cs_local_size() (default 32x8).  Returns
 * a malloc()ed string (caller frees) or NULL on OOM. */
char *pglc_fe_fragment_to_compute_source(const char *frag_src);

/* Current fragment->compute workgroup shape (pglc_compile_set_cs_local_size
 * is implemented by the frontend since it only affects the wrapper text). */
void pglc_fe_get_cs_local_size(unsigned *x, unsigned *y);

/* Snapshot one stage's default-block uniforms (name -> dword offset + width)
 * into st->uniform_bindings / uniform_blob_dwords, reading the byte
 * driver_locations the compile assigned.  Opaque uniforms are skipped.
 * Returns false only on allocation failure. */
bool pglc_fe_build_uniform_bindings(nir_shader *nir, struct pglc_stage *st);

/* Release the neutral parts of a stage (code + uniform_bindings) and zero
 * it; the backend frees st->backend itself before calling this. */
void pglc_fe_stage_free_common(struct pglc_stage *st);

/* Append `src` to a fixed, NUL-terminated log buffer (bounded). */
void pglc_fe_log_append(char *log, size_t logsz, const char *src);

#ifdef __cplusplus
}
#endif

#endif /* PGLCOMPILER_FRONTEND_H */
