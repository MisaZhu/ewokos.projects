/* Portable vendoring stubs for the mesa GLSL->QPU compiler (libv3dcompiler.a).
 *
 * These definitions are platform-INDEPENDENT: they stand in for mesa code that
 * this compiler-only vendored subset deliberately omits, and they are identical
 * on every host (the EwokOS device build and the macOS/Linux oracle build alike).
 * The EwokOS-specific OS/libc glue (process name, qsort_r fallback, and the SDK's
 * missing libc entries) does NOT live here - it lives in the owning platform's
 * bsp g3d layer (exported to the build as V3D_PORT_DIR), so this file and the
 * rest of the compiler stay free of any board/OS coupling.
 *
 * Upstream pin f45825a0c724d06f231a1210484fe9af71a3ffab. See VENDORED.txt.
 */
#include <stdbool.h>
#include <stddef.h>

#include "compiler/nir/nir.h"

/* The generic NIR algebraic optimisation pass is generated from
 * nir_opt_algebraic.py into a ~49 MB / 3.76 M-line nir_opt_algebraic.c that is
 * impractical to commit or to compile on the device.
 *
 * nir_opt_algebraic() itself is NOT stubbed here: the broadcom backend cannot
 * emit fdiv/fsqrt/fpow/fmod/frem/ffract/flrp/ffma and would abort() on them, so
 * a focused, correctness-preserving replacement lives in v3dcompiler_algebraic.c
 * and rewrites exactly those opcodes into backend-supported primitives.
 *
 * The five sibling entry points below ARE pure optimisations (late algebraic,
 * pre-ffma/pre-int64 splitting, source-modifier distribution, integer
 * promotion); no-oping them leaves the shader semantically unchanged, so they
 * simply report "no progress" and the pass manager moves on. broadcom's own much
 * smaller v3d_nir_lower_algebraic.c still runs in the v3d pipeline. Regenerate
 * the full generic pass with regen_codegen.sh on a larger host if wanted.
 */
bool
nir_opt_algebraic_before_ffma(nir_shader *shader)
{
   (void)shader;
   return false;
}

bool
nir_opt_algebraic_before_lower_int64(nir_shader *shader)
{
   (void)shader;
   return false;
}

bool
nir_opt_algebraic_late(nir_shader *shader)
{
   (void)shader;
   return false;
}

bool
nir_opt_algebraic_distribute_src_mods(nir_shader *shader)
{
   (void)shader;
   return false;
}

bool
nir_opt_algebraic_integer_promotion(nir_shader *shader)
{
   (void)shader;
   return false;
}

/* mesa/program/symbol_table.c calls _mesa_error_no_memory() when a scope/symbol
 * allocation fails. The real implementation lives in main/errors.c, which we do
 * not vendor (it drags in the whole mesa/main GL error machinery). This minimal
 * definition keeps libv3dcompiler.a self-contained: the caller treats it as a
 * failure for that symbol and unwinds, so the GLSL compile simply fails and the
 * integrator (pgl_g3d) falls back to the CPU rasterizer. */
void _mesa_error_no_memory(const char *caller);
void
_mesa_error_no_memory(const char *caller)
{
   (void)caller;
}

/* GLSL #include support (glcpp-parse.y -> _mesa_{get,set}_shader_include_cursor
 * and _mesa_lookup_shader_include) lives in main/shaderobj.c, which manages a
 * per-context include list on gl_shared_state. We vendor main/ headers only, so
 * those three are stubbed here. The shadertoy effects this compiler targets use
 * no #include, so lookup reports "not found" (NULL) and the recursion-depth
 * cursor accessors are no-ops; a shader that does use #include simply fails to
 * preprocess, and the integrator (pgl_g3d) falls back to the CPU rasterizer.
 * Signatures match main/shaderapi.h exactly (C linkage - only the name binds). */
struct gl_context;
struct gl_shared_state;

size_t
_mesa_get_shader_include_cursor(struct gl_shared_state *shared)
{
   (void)shared;
   return 0;
}

void
_mesa_set_shader_include_cursor(struct gl_shared_state *shared, size_t cursor)
{
   (void)shared;
   (void)cursor;
}

const char *
_mesa_lookup_shader_include(struct gl_context *ctx, char *path,
                            bool error_check)
{
   (void)ctx;
   (void)path;
   (void)error_check;
   return NULL;
}
