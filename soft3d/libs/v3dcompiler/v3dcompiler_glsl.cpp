/* v3d_compile_glsl() - the single public entry point of libv3dcompiler.a
 * (WS2-entry).
 *
 * This is the C++ half of the on-device compiler: it drives mesa's GLSL
 * frontend (preprocess -> AST -> HIR -> NIR -> link) exactly the way mesa's own
 * standalone compiler does, then hands the linked vertex+fragment NIR to the C
 * backend bridge (v3dcompiler_backend.c) which runs the broadcom NIR->VIR->QPU
 * backend for V3D 7.1. The resulting 64-bit QPU words are re-packed into the
 * little-endian u32 stream documented by the public v3d_compiled_stage.
 *
 * WHY C++
 * -------
 * The GLSL frontend entry points (initialize_context_to_defaults,
 * standalone_create_shader_program, standalone_add_shader_source,
 * gl_nir_link_glsl, ...) have C++ linkage and are declared in C++ headers with
 * classes; they can only be called from a C++ TU. The v3d backend, by contrast,
 * is C-only and cannot be included from C++ safely (see v3dcompiler_backend.h),
 * so the two halves meet at that C bridge. This file therefore includes ONLY the
 * frontend headers plus the bridge, never v3d_compiler.h.
 *
 * The whole compile runs in user space in the calling app's process. The output
 * is real VideoCore GPU code (not CPU rasterization); the app then marshals it
 * through g2dclient to the g2dd daemon, which is the only entity that may map
 * the V3D MMIO and dispatch it.
 *
 * Platform-INDEPENDENT. Upstream pin f45825a0c724d06f231a1210484fe9af71a3ffab.
 * See VENDORED.txt.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "util/ralloc.h"

#include "glsl_parser_extras.h"
#include "standalone_scaffolding.h"
#include "gl_nir_linker.h"
#include "gl_nir.h"
#include "builtin_functions.h"
#include "linker_util.h"
#include "main/mtypes.h"
#include "program/program.h"
#include "nir_shader_compiler_options.h"
#include "compiler/nir/nir.h"
#include "compiler/glsl_types.h"
#include "pipe/p_screen.h"

#include "v3dcompiler_backend.h"
#include "v3dcompiler.h"   /* public API (include/ is on the -I path) */

/* nir_shader_compiler_options for the V3D 7.1 backend.
 *
 * float_mul_add32 = 0 is load-bearing: it clears the has_ffma bit, so
 * nir_prefers_fmad() returns true and nir_lower_alu_to_scalar() rewrites
 * fdot/fdph into fmul+fadd (which ntq_emit_alu supports) instead of ffma/
 * ffma_weak chains (which it aborts on). V3D has no fused multiply-add ALU.
 *
 * lower_fdiv/fsqrt/fpow/fmod/ffract mirror the intent of the v3d gallium
 * driver; the actual rewriting is done unconditionally by our focused
 * nir_opt_algebraic() (v3dcompiler_algebraic.c), because the generated generic
 * algebraic pass is not vendored. lower_flrp32 is deliberately left UNSET so
 * the backend's own nir_lower_flrp stays a no-op and mix()/flrp is lowered by
 * our algebraic pass instead.
 *
 * Built imperatively (not with designated initializers) because the struct is
 * ~1000 lines and C++ designated-initializer ordering is fragile. */
static struct nir_shader_compiler_options v3d_nir_options;

static void
init_v3d_nir_options(void)
{
   static bool done = false;
   if (done)
      return;
   memset(&v3d_nir_options, 0, sizeof(v3d_nir_options));
   v3d_nir_options.float_mul_add32 = (nir_float_muladd_support)0;
   v3d_nir_options.has_fsub = true;   /* V3D emits fsub/isub natively */
   v3d_nir_options.has_isub = true;
   v3d_nir_options.lower_fdiv = true;
   v3d_nir_options.lower_fsqrt = true;
   v3d_nir_options.lower_fpow = true;
   v3d_nir_options.lower_fmod = true;
   v3d_nir_options.lower_ffract = true;
   /* Compute (CSD route): nir_to_vir.c has no case for
    * load_local_invocation_id / load_global_invocation_id -- it only reads
    * load_workgroup_id (CSD payload r3/r2) and load_local_invocation_index
    * (payload r2 high bits).  nir_lower_compute_system_values honours this
    * option by rewriting local_invocation_id in terms of the index, and
    * nir_lower_system_values rewrites global_invocation_id as
    * workgroup_id * workgroup_size + local_invocation_id.  Same setting as
    * upstream gallium v3d. */
   v3d_nir_options.lower_cs_local_id_to_index = true;
   done = true;
}

/* Uniform type-size callback for nir_lower_io, in BYTES.
 *
 * nir_lower_io computes each load_uniform's byte offset as
 * base (= var->data.driver_location) + the offset get_io_offset() derives with
 * this callback (array/struct strides).  The v3d backend's
 * ntq_emit_load_uniform then does (base + src[0]) / 4 to get the
 * QUNIFORM_UNIFORM dword index, so BOTH driver_location and this callback must
 * be in bytes for the offset to be meaningful.  glsl_get_component_slots()
 * counts 32-bit components, so * 4 is the byte size; this matches the dense
 * packing assign_uniform_driver_locations() uses below. */
static unsigned
type_size_uniform_bytes(const struct glsl_type *type, bool bindless)
{
   (void)bindless;
   return glsl_get_component_slots(type) * 4;
}

/* Assign a dense, 4-byte-aligned driver_location (byte offset in the default
 * uniform block) to every non-opaque default-block uniform of one stage.
 *
 * glsl_to_nir creates nir_var_uniform variables with driver_location left
 * unassigned (~0); neither gl_nir_link_glsl (its nir_lower_io_passes lowers
 * only shader_in/out) nor the vendored broadcom backend assigns it -- upstream
 * does this in the gallium v3d state tracker, which is not part of this port.
 * With driver_location == ~0, nir_lower_io would emit load_uniform with a
 * garbage base, so ntq_emit_load_uniform's (base + src[0]) / 4 QUNIFORM_UNIFORM
 * dword index would be meaningless (and negative offsets fall through to the
 * general-TMU path).
 *
 * Packing is component-dense (glsl_get_component_slots() * 4 bytes), matching
 * gl_nir_link_uniforms' CPU-side remap and keeping every offset 4-byte aligned
 * as try_emit_uniform() requires.  Opaque uniforms (samplers/images) are
 * skipped: they are consumed by texture intrinsics, never by load_deref, so
 * nir_lower_io(nir_var_uniform) leaves them untouched.
 *
 * Iteration is the shader's variable-declaration order (stable), so the
 * resulting QUNIFORM_UNIFORM dword indices are reproducible -- the WS6 host
 * oracle dumps them as the ground truth the client uniform blob is packed
 * against by name. */
static void
assign_uniform_driver_locations(nir_shader *nir)
{
   unsigned byte_offset = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_uniform) {
      const struct glsl_type *base_t = glsl_without_array(var->type);
      if (glsl_type_is_sampler(base_t) || glsl_type_is_image(base_t))
         continue;
      var->data.driver_location = byte_offset;
      byte_offset += glsl_get_component_slots(var->type) * 4;
   }
}

/* Order two vertex-input attributes by their GLSL location for the dense
 * driver_location assignment below.  data.location for a VS input is
 * layout_location + VERT_ATTRIB_GENERIC0 (ast_to_hir.cpp:3519), a constant
 * monotonic offset, so sorting on it preserves the shader's layout(location=)
 * order; location_frac breaks ties within one location. */
static int
cmp_shader_in_by_location(const void *pa, const void *pb)
{
   const nir_variable *a = *(const nir_variable *const *)pa;
   const nir_variable *b = *(const nir_variable *const *)pb;
   if (a->data.location != b->data.location)
      return (int)a->data.location - (int)b->data.location;
   return (int)a->data.location_frac - (int)b->data.location_frac;
}

/* Assign a dense per-attribute driver_location (0,1,2,...) to every vertex
 * shader input, in GLSL location order.
 *
 * glsl_to_nir leaves nir_var_shader_in driver_location unassigned (0), and
 * neither gl_nir_link_glsl (the GLSL link path never calls
 * gl_nir_lower_optimize_varyings, so VS inputs stay as live variables) nor the
 * vendored broadcom backend assigns it -- upstream does this in the gallium v3d
 * state tracker, which is not part of this port.  This is the exact analogue of
 * the uniform gap fixed by assign_uniform_driver_locations() above.
 *
 * With every attribute sharing driver_location 0, nir_lower_io gives every
 * load_input the SAME base, so ntq_emit_load_input() (nir_to_vir.c) computes an
 * identical VPM column for all attributes: attribute 1 (the teapot normal)
 * would read attribute 0's (position) VPM columns.  ntq_setup_vs_inputs() also
 * collapses vattr_sizes onto index 0.  Assigning dense 0,1,2,... makes
 * ntq_emit_load_input() place attribute N at VPM column
 * sum(vattr_sizes[0..N-1]) + component, i.e. attr0 (pos, vec3) at VPM 0-2 and
 * attr1 (normal, vec3) at VPM 3-5 -- exactly what the device's
 * build_attr_records_compiled2() programs into the two attribute records.
 *
 * driver_location is per ATTRIBUTE variable (a vec3 occupies vattr_sizes[dl]
 * consecutive VPM columns), so the dense index counts attributes, not
 * components.  VS input arrays are unsupported by the backend
 * (ntq_setup_vs_inputs asserts glsl_get_length == 1), so one driver_location
 * per variable is correct here. */
static void
assign_attribute_driver_locations(nir_shader *nir)
{
   nir_variable *vars[64];
   unsigned n = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_in) {
      if (n < ARRAY_SIZE(vars))
         vars[n++] = var;
   }
   qsort(vars, n, sizeof(vars[0]), cmp_shader_in_by_location);
   for (unsigned i = 0; i < n; i++)
      vars[i]->data.driver_location = i;
}

/* Snapshot one stage's named default-block uniforms (name -> assigned dword
 * offset + component width) into st->uniform_bindings, so the client can pack
 * the per-draw uniform blob BY NAME.
 *
 * Must run after assign_uniform_driver_locations() (it reads the byte offset
 * that call stored in var->data.driver_location, dividing by 4 to match the
 * QUNIFORM_UNIFORM dword index ntq_emit_load_uniform emits).  It reflects
 * exactly the uniforms that survived linking -- the linker already dead-stripped
 * the ones the shader never reads, which is why the offsets differ per effect
 * and a fixed client-side layout would be wrong.  Opaque uniforms
 * (samplers/images) are skipped to match the assignment.  Returns true on
 * success, including the count == 0 no-op; false only on allocation failure. */
static bool
build_uniform_bindings(nir_shader *nir, struct v3d_compiled_stage *st)
{
   st->num_uniform_bindings = 0;
   st->uniform_bindings = NULL;
   st->uniform_blob_dwords = 0;
   if (!nir)
      return true;

   unsigned count = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_uniform) {
      const struct glsl_type *base_t = glsl_without_array(var->type);
      if (glsl_type_is_sampler(base_t) || glsl_type_is_image(base_t))
         continue;
      count++;
   }
   if (count == 0)
      return true;

   struct v3d_uniform_binding *b =
      (struct v3d_uniform_binding *)calloc(count, sizeof(*b));
   if (!b)
      return false;

   unsigned i = 0;
   uint32_t span = 0;
   nir_foreach_variable_with_modes(var, nir, nir_var_uniform) {
      const struct glsl_type *base_t = glsl_without_array(var->type);
      if (glsl_type_is_sampler(base_t) || glsl_type_is_image(base_t))
         continue;
      uint32_t off   = (uint32_t)var->data.driver_location / 4u;
      uint32_t slots = glsl_get_component_slots(var->type);
      snprintf(b[i].name, sizeof(b[i].name), "%s", var->name ? var->name : "");
      b[i].dword_offset   = off;
      b[i].num_components = slots;
      if (off + slots > span)
         span = off + slots;
      i++;
   }

   st->num_uniform_bindings = count;
   st->uniform_bindings = b;
   st->uniform_blob_dwords = span;
   return true;
}

/* Minimal GLSL version/ES probe: read the first "#version" directive so the
 * context can be configured for whatever pgl_g3d emits (GLES 1.00/3.00 or a
 * desktop profile). Defaults to GLSL ES 1.00 when no directive is present. */
static void
detect_glsl_version(const char *src, unsigned *version, bool *is_es)
{
   *version = 100;
   *is_es = true;
   if (!src)
      return;

   const char *p = strstr(src, "#version");
   if (!p)
      return;
   p += strlen("#version");
   while (*p == ' ' || *p == '\t')
      p++;
   unsigned v = 0;
   while (*p >= '0' && *p <= '9') {
      v = v * 10 + (unsigned)(*p - '0');
      p++;
   }
   if (v == 0)
      return;
   *version = v;
   /* An explicit "es" suffix (GLSL ES 3.00+) marks an ES shader.  GLSL ES 1.00
    * has NO suffix ("es" is only legal from #version 300 es onward), so a bare
    * "#version 100" must stay ES; any other version without the suffix is a
    * desktop profile. */
   const char *q = p;
   while (*q == ' ' || *q == '\t')
      q++;
   if (q[0] == 'e' && q[1] == 's')
      *is_es = true;
   else if (v != 100)
      *is_es = false;
   /* else: v == 100 with no suffix -> keep the default is_es = true */
}

/* Configure a driver-less gl_context, mirroring mesa's standalone compiler
 * initialize_context() (standalone.cpp), which is the proven recipe for running
 * the GLSL frontend with no real GL driver attached. */
static void
init_context(struct gl_context *ctx, unsigned glsl_version, bool is_es)
{
   initialize_context_to_defaults(ctx, is_es ? API_OPENGLES2
                                    : (glsl_version > 130 ? API_OPENGL_CORE
                                                          : API_OPENGL_COMPAT));
   _mesa_glsl_builtin_functions_init_or_ref();

   ctx->Version = 450;

   init_v3d_nir_options();
   ctx->screen->nir_options[MESA_SHADER_VERTEX] = &v3d_nir_options;
   ctx->screen->nir_options[MESA_SHADER_FRAGMENT] = &v3d_nir_options;
   ctx->screen->nir_options[MESA_SHADER_COMPUTE] = &v3d_nir_options;

   ctx->Const.GLSLVersion = glsl_version;
   ctx->Extensions.ARB_ES3_compatibility = true;
   ctx->Extensions.ARB_ES3_1_compatibility = true;
   ctx->Extensions.ARB_ES3_2_compatibility = true;

   ctx->Const.MaxComputeWorkGroupCount[0] = 65535;
   ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
   ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
   ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
   ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
   ctx->Const.MaxComputeWorkGroupSize[2] = 64;
   ctx->Const.MaxComputeWorkGroupInvocations = 1024;
   ctx->Const.MaxComputeSharedMemorySize = 32768;

   /* Compute-stage program limits + SSBO limits (CSD route, GLSL ES 3.10).
    * The linker rejects a compute shader whose resource counts exceed these
    * ("Too many ... " errors), and the per-stage defaults are zero. */
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits = 16;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformComponents = 1024;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxCombinedUniformComponents = 1024;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformBlocks = 12;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxShaderStorageBlocks = 4;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxImageUniforms = 4;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicBuffers = 1;
   ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicCounters = 8;
   ctx->Const.MaxCombinedShaderStorageBlocks = 4;
   ctx->Const.MaxShaderStorageBufferBindings = 4;
   ctx->Const.MaxShaderStorageBlockSize = 1u << 27;   /* 128 MiB: covers any frame */
   ctx->Const.ShaderStorageBufferOffsetAlignment = 4;
   ctx->Const.MaxImageUnits = 4;
   ctx->Const.MaxCombinedImageUniforms = 4;
   ctx->Const.MaxCombinedShaderOutputResources = 8;

   switch (glsl_version) {
   case 100:
      ctx->Const.MaxClipPlanes = 0;
      ctx->Const.MaxCombinedTextureImageUnits = 8;
      ctx->Const.MaxDrawBuffers = 2;
      ctx->Const.MinProgramTexelOffset = 0;
      ctx->Const.MaxProgramTexelOffset = 0;
      ctx->Const.MaxLights = 0;
      ctx->Const.MaxTextureCoordUnits = 0;
      ctx->Const.MaxTextureUnits = 8;

      ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 8;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 0;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = 128 * 4;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = 128 * 4;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 32;

      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits =
         ctx->Const.MaxCombinedTextureImageUnits;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = 16 * 4;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = 16 * 4;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents =
         ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents = 0;

      ctx->Const.MaxVarying =
         ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents / 4;
      break;

   case 110:
   case 120:
      ctx->Const.MaxClipPlanes = 6;
      ctx->Const.MaxCombinedTextureImageUnits = 2;
      ctx->Const.MaxDrawBuffers = 1;
      ctx->Const.MinProgramTexelOffset = 0;
      ctx->Const.MaxProgramTexelOffset = 0;
      ctx->Const.MaxLights = 8;
      ctx->Const.MaxTextureCoordUnits = 2;
      ctx->Const.MaxTextureUnits = 2;

      ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 16;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 0;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = 512;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = 512;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 32;

      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits =
         ctx->Const.MaxCombinedTextureImageUnits;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = 64;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = 64;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents =
         ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents = 0;

      ctx->Const.MaxVarying =
         ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents / 4;
      break;

   case 300:
   case 310:
   case 320:
      ctx->Const.MaxClipPlanes = 8;
      ctx->Const.MaxCombinedTextureImageUnits = 32;
      ctx->Const.MaxDrawBuffers = 4;
      ctx->Const.MinProgramTexelOffset = -8;
      ctx->Const.MaxProgramTexelOffset = 7;
      ctx->Const.MaxLights = 0;
      ctx->Const.MaxTextureCoordUnits = 0;
      ctx->Const.MaxTextureUnits = 0;
      ctx->Const.MaxUniformBufferBindings = 84;
      ctx->Const.MaxVertexStreams = 4;
      ctx->Const.MaxTransformFeedbackBuffers = 4;

      ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 16;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 16;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 16 * 4;

      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits = 16;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = 224;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = 224;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents = 15 * 4;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents = 0;

      ctx->Const.MaxVarying =
         ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents / 4;
      break;

   default: /* desktop 130..460 */
      ctx->Const.MaxClipPlanes = 8;
      ctx->Const.MaxCombinedTextureImageUnits = 48;
      ctx->Const.MaxDrawBuffers = 8;
      ctx->Const.MinProgramTexelOffset = -8;
      ctx->Const.MaxProgramTexelOffset = 7;
      ctx->Const.MaxLights = 8;
      ctx->Const.MaxTextureCoordUnits = 8;
      ctx->Const.MaxTextureUnits = 2;
      ctx->Const.MaxUniformBufferBindings = 84;
      ctx->Const.MaxVertexStreams = 4;
      ctx->Const.MaxTransformFeedbackBuffers = 4;
      ctx->Const.MaxShaderStorageBufferBindings = 4;
      ctx->Const.MaxShaderStorageBlockSize = 4096;
      ctx->Const.MaxAtomicBufferBindings = 4;

      ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 16;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = 16;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0;
      ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 64;

      ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits = 16;
      ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxCombinedUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxInputComponents =
         ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents;
      ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxOutputComponents = 128;

      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits = 16;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = 1024;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents =
         ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxOutputComponents;
      ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents = 0;

      ctx->Const.MaxGeometryOutputVertices = 256;
      ctx->Const.MaxGeometryTotalOutputComponents = 1024;
      ctx->Const.MaxVarying = 60 / 4;
      break;
   }

   ctx->Const.GenerateTemporaryNames = true;
   ctx->Const.MaxPatchVertices = 32;
   ctx->Const.MaxUserAssignableUniformLocations =
      4 * MESA_SHADER_MESH_STAGES * MAX_UNIFORMS;
}

/* Append `src` to the fixed log buffer (bounded, NUL-terminated). */
static void
log_append(char *log, size_t logsz, const char *src)
{
   if (!log || logsz == 0 || !src)
      return;
   size_t used = strlen(log);
   if (used + 1 >= logsz)
      return;
   snprintf(log + used, logsz - used, "%s", src);
}

/* Re-pack the backend's 64-bit QPU words into the public little-endian u32
 * stream (two words per instruction: lo then hi) and deep-copy the prog_data
 * snapshot (uniform stream + VPM/attribute/FS-input fields) into st. Takes
 * ownership of nothing: be stays valid and is freed by v3d_be_result_free().
 * Returns false on any allocation failure (st is left safe to free). */
static bool
pack_stage(const struct v3d_be_stage *be, struct v3d_compiled_stage *st)
{
   st->qpu_instr_count = be->qpu_instr_count;
   st->qpu_word_count  = be->qpu_instr_count * 2u;
   st->num_uniforms    = be->num_uniforms;
   st->num_outputs     = be->num_outputs;
   st->needs_coords    = be->needs_coords;

   /* prog_data snapshot: scalars + fixed arrays copy verbatim. */
   st->threads         = be->threads;
   st->single_seg      = be->single_seg;
   st->vpm_input_size  = be->vpm_input_size;
   st->vpm_output_size = be->vpm_output_size;
   st->separate_segments = be->separate_segments;
   st->vcm_cache_size  = be->vcm_cache_size;
   memcpy(st->vattr_sizes, be->vattr_sizes, sizeof(st->vattr_sizes));
   memcpy(st->driver_location_map, be->driver_location_map,
          sizeof(st->driver_location_map));
   st->num_inputs      = be->num_inputs;
   memcpy(st->input_slots, be->input_slots, sizeof(st->input_slots));
   st->uses_center_w   = be->uses_center_w;
   st->writes_z        = be->writes_z;
   st->disable_ez      = be->disable_ez;
   st->uses_implicit_point_line_varyings = be->uses_implicit_point_line_varyings;
   memcpy(st->local_size, be->local_size, sizeof(st->local_size));
   st->shared_size     = be->shared_size;

   /* Uniform stream: fresh malloc'd copies (be's arrays belong to the backend
    * result and are freed by v3d_be_result_free()). */
   st->uniform_contents = NULL;
   st->uniform_data     = NULL;
   if (be->num_uniforms != 0) {
      size_t bytes = (size_t)be->num_uniforms * sizeof(uint32_t);
      st->uniform_contents = (uint32_t *)malloc(bytes);
      st->uniform_data     = (uint32_t *)malloc(bytes);
      if (!st->uniform_contents || !st->uniform_data) {
         free(st->uniform_contents);
         free(st->uniform_data);
         st->uniform_contents = NULL;
         st->uniform_data     = NULL;
         st->num_uniforms     = 0;
         st->qpu = NULL;
         st->qpu_word_count = 0;
         st->qpu_instr_count = 0;
         return false;
      }
      memcpy(st->uniform_contents, be->uniform_contents, bytes);
      memcpy(st->uniform_data,     be->uniform_data,     bytes);
   }

   if (be->qpu_instr_count == 0) {
      st->qpu = NULL;
      st->qpu_word_count = 0;
      return true;
   }

   st->qpu = (uint32_t *)malloc((size_t)st->qpu_word_count * sizeof(uint32_t));
   if (!st->qpu) {
      st->qpu_word_count = 0;
      st->qpu_instr_count = 0;
      return false;
   }
   for (uint32_t i = 0; i < be->qpu_instr_count; i++) {
      uint64_t w = be->qpu[i];
      st->qpu[2 * i + 0] = (uint32_t)(w & 0xffffffffull);
      st->qpu[2 * i + 1] = (uint32_t)(w >> 32);
   }
   return true;
}

extern "C" bool
v3d_compile_glsl(const char *vert_src, const char *frag_src,
                 struct v3d_compiled_program *res)
{
   if (!res)
      return false;
   memset(res, 0, sizeof(*res));

   if (!vert_src || !frag_src) {
      snprintf(res->log, sizeof(res->log),
               "v3d_compile_glsl: both vertex and fragment source are required "
               "(vert=%p frag=%p)", (const void *)vert_src,
               (const void *)frag_src);
      return false;
   }

   /* Configure the context from whichever stage declares a #version (prefer the
    * vertex shader, fall back to the fragment shader). */
   unsigned version = 100;
   bool is_es = true;
   detect_glsl_version(vert_src, &version, &is_es);
   if (version == 100 && is_es) {
      unsigned fv = 100;
      bool fes = true;
      detect_glsl_version(frag_src, &fv, &fes);
      if (fv != 100 || !fes) {
         version = fv;
         is_es = fes;
      }
   }

   /* gl_context is very large (gl_constants + extensions + driver state), so
    * heap-allocate it rather than put it on the (small) EwokOS app stack.
    * initialize_context_to_defaults() memsets it and calloc()s ctx->screen. */
   struct gl_context *ctx =
      (struct gl_context *)calloc(1, sizeof(struct gl_context));
   if (!ctx) {
      snprintf(res->log, sizeof(res->log),
               "v3d_compile_glsl: out of memory allocating GL context");
      return false;
   }
   init_context(ctx, version, is_es);

   bool ok = false;
   struct gl_shader_program *prog = standalone_create_shader_program();
   if (!prog) {
      snprintf(res->log, sizeof(res->log),
               "v3d_compile_glsl: failed to create shader program");
      goto cleanup_ctx;
   }

   {
      /* Compile both stages. */
      struct gl_shader *vs =
         standalone_add_shader_source(ctx, prog, GL_VERTEX_SHADER, vert_src);
      _mesa_glsl_compile_shader(ctx, vs, NULL, false, false, true);
      if (!vs->CompileStatus) {
         log_append(res->log, sizeof(res->log), "vertex shader:\n");
         log_append(res->log, sizeof(res->log), vs->InfoLog);
         goto cleanup_prog;
      }

      struct gl_shader *fs =
         standalone_add_shader_source(ctx, prog, GL_FRAGMENT_SHADER, frag_src);
      _mesa_glsl_compile_shader(ctx, fs, NULL, false, false, true);
      if (!fs->CompileStatus) {
         log_append(res->log, sizeof(res->log), "fragment shader:\n");
         log_append(res->log, sizeof(res->log), fs->InfoLog);
         goto cleanup_prog;
      }

      /* Lower gl_FragColor (FRAG_RESULT_COLOR) to gl_FragData[0]
       * (FRAG_RESULT_DATA0) so the broadcom backend accepts the FS.
       *
       * nir_lower_fragcolor() cannot be used here: it only rewrites store_deref
       * intrinsics (nir_lower_fragcolor.c:56), but glsl_to_nir emits the
       * whole-vector "gl_FragColor = <vec4>" assignment as a copy_deref (verified
       * by dumping fs->nir), so that pass is a silent no-op -- it returns without
       * touching the output variable.
       *
       * What the backend actually keys on is the output VARIABLE's data.location:
       * the linker's nir_lower_io_passes() (gl_nir_linker.c:1549) bakes it into
       * each store_output's io_semantics, and nir_to_vir.c ntq_setup_outputs()
       * (line 2556) walks the shader-out variables and hits
       * UNREACHABLE("Frag color should be lowered") on FRAG_RESULT_COLOR (line
       * 2568). Renaming the variable to FRAG_RESULT_DATA0 BEFORE the link fixes
       * both the variable scan and the lowered io_semantics, and is form-agnostic
       * (copy_deref / store_deref alike). This is exactly nir_lower_fragcolor()'s
       * effect for a single render target: its extra DATA1..N outputs only appear
       * when max_draw_buffers > 1, which the single-RT shadertoy pipeline
       * (backend fs_key.cbufs = 0x1, num_outputs = 1) must not emit.
       * link_intrastage_shaders() clones fs->nir (gl_nir_linker.c:2880), so the
       * rename propagates through the link. */
      if (fs->nir) {
         nir_foreach_shader_out_variable(var, fs->nir) {
            if (var->data.location == FRAG_RESULT_COLOR) {
               var->data.location = FRAG_RESULT_DATA0;
               nir_variable_set_name(fs->nir, var, "gl_FragData[0]");
               fs->nir->info.outputs_written &= ~BITFIELD64_BIT(FRAG_RESULT_COLOR);
               fs->nir->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DATA0);
            }
         }
      }

      /* Link to a single program, producing the IO-lowered linked NIR. */
      _mesa_clear_shader_program_data(ctx, prog);
      prog->data->LinkStatus = LINKING_SUCCESS;
      link_shaders_init(ctx, prog);
      gl_nir_link_glsl(ctx, prog);

      if (!prog->data->LinkStatus) {
         log_append(res->log, sizeof(res->log), "link:\n");
         log_append(res->log, sizeof(res->log), prog->data->InfoLog);
         goto cleanup_prog;
      }

      nir_shader *vs_nir = prog->_LinkedShaders[MESA_SHADER_VERTEX]
         ? prog->_LinkedShaders[MESA_SHADER_VERTEX]->Program->nir : NULL;
      nir_shader *fs_nir = prog->_LinkedShaders[MESA_SHADER_FRAGMENT]
         ? prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->nir : NULL;
      if (!vs_nir || !fs_nir) {
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_glsl: link produced no VS/FS NIR");
         goto cleanup_prog;
      }

      /* Lower default-block uniform derefs to load_uniform intrinsics.
       *
       * gl_nir_link_glsl -> nir_lower_io_passes only lowers shader_in/out;
       * nir_var_uniform derefs survive as deref_var + load_deref pairs.  The
       * broadcom backend's ntq_emit_instr abort()s on any NIR instr type it
       * does not handle, and nir_instr_type_deref is one of them.  Upstream
       * gallium v3d calls nir_lower_io(nir_var_uniform) before v3d_compile;
       * this port was missing that step, so every shader reading a uniform
       * (iGlobalTime / iResolution / iChannelN) crashed the compiler.
       *
       * nir_lower_io bakes base = var->data.driver_location into each
       * load_uniform, but nothing in this port assigns driver_location for
       * default-block uniforms (glsl_to_nir leaves it ~0; upstream does it in
       * the gallium v3d state tracker, not vendored here).  So assign dense,
       * 4-byte-aligned byte offsets first -- see assign_uniform_driver_locations.
       * ntq_emit_load_uniform then divides (base + src[0]) by 4 to get the
       * QUNIFORM_UNIFORM dword index the client blob must fill. */
      {
         bool _lower_progress;
         assign_uniform_driver_locations(vs_nir);
         assign_uniform_driver_locations(fs_nir);
         /* VS input attributes need a dense driver_location too (see
          * assign_attribute_driver_locations); without it every attribute
          * collapses onto VPM column 0 and the normal reads position data. */
         assign_attribute_driver_locations(vs_nir);
         NIR_PASS(_lower_progress, vs_nir, nir_lower_io, nir_var_uniform,
                  type_size_uniform_bytes, (nir_lower_io_options)0);
         NIR_PASS(_lower_progress, fs_nir, nir_lower_io, nir_var_uniform,
                  type_size_uniform_bytes, (nir_lower_io_options)0);
      }

      /* Lower sampler2D uniform derefs to texture/sampler index constants.
       *
       * nir_lower_io(nir_var_uniform) handles float/vec/mat uniforms but leaves
       * sampler derefs intact (they ride on nir_tex_instr's texture_deref /
       * sampler_deref sources, not load_deref).  gl_nir_lower_samplers does the
       * two-step conversion: gl_nir_lower_samplers_as_deref sets var->data.binding
       * from the linker's UniformStorage opaque index, then nir_lower_samplers
       * rewrites the tex instr sources to plain integer indices.  Without this,
       * deref_var &iChannel0 survives to ntq_emit_instr and abort()s. */
      gl_nir_lower_samplers(vs_nir, prog);
      gl_nir_lower_samplers(fs_nir, prog);

      /* Run the broadcom backend. */
      struct v3d_be_result be;
      memset(&be, 0, sizeof(be));
      if (!v3d_be_compile_program(vs_nir, fs_nir, &be,
                                  res->log, sizeof(res->log))) {
         v3d_be_result_free(&be);
         goto cleanup_prog;
      }

      bool packed = pack_stage(&be.vs, &res->stage[V3D_STAGE_VERTEX]) &&
                    pack_stage(&be.fs, &res->stage[V3D_STAGE_FRAGMENT]) &&
                    pack_stage(&be.coord, &res->stage[V3D_STAGE_COORD]);
      v3d_be_result_free(&be);
      if (!packed) {
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_glsl: out of memory packing QPU");
         v3d_compiled_program_free(res);
         goto cleanup_prog;
      }

      /* Export the name -> dword-offset reflection for the uniforms each stage
       * actually reads.  v3d_compile() clones the NIR (vir.c:608), so vs_nir /
       * fs_nir still carry the driver_location assign_uniform_driver_locations()
       * set; the client packs the per-draw blob by name through this table
       * (offsets shift per effect because the linker dead-strips unused
       * uniforms -- see struct v3d_uniform_binding).  The COORD stage is the
       * same linked VS NIR compiled with is_coord=true, so it shares the VS's
       * driver_locations: reflect vs_nir for it too, giving the client one
       * blob that feeds both the coord and render-VS uniform expansion. */
      if (!build_uniform_bindings(vs_nir, &res->stage[V3D_STAGE_VERTEX]) ||
          !build_uniform_bindings(fs_nir, &res->stage[V3D_STAGE_FRAGMENT]) ||
          !build_uniform_bindings(vs_nir, &res->stage[V3D_STAGE_COORD])) {
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_glsl: out of memory building uniform reflection");
         v3d_compiled_program_free(res);
         goto cleanup_prog;
      }

      res->success = true;
      ok = true;
   }

cleanup_prog:
   standalone_destroy_shader_program(prog);
cleanup_ctx:
   free(ctx->screen);
   free(ctx);
   _mesa_glsl_builtin_functions_decref();
   return ok;
}

/* ---------------------------------------------------------------------------
 * Compute (CSD) route
 * ------------------------------------------------------------------------- */

/* Rewrite a shadertoy-style GLSL ES 1.00 fragment shader into a GLSL ES 3.10
 * compute shader that evaluates one pixel per invocation and stores the
 * packed ARGB8888 result into an SSBO (the frame buffer), instead of going
 * through the TLB.  The effect body is kept verbatim; only the surrounding
 * contract changes:
 *
 *   #version 100            -> #version 310 es + local_size + SSBO declaration
 *   gl_FragCoord            -> v3d_frag_coord (global, set per invocation)
 *   gl_FragColor            -> v3d_frag_color (global, read back after main)
 *   texture2D()             -> texture()     (texture2D was removed in ES 3.00)
 *   void main()             -> void v3d_frag_main()  (via #define main)
 *
 * The epilogue main() derives the pixel from gl_GlobalInvocationID, bounds-
 * checks against iResolution, runs the effect, clamps/packs the colour and
 * writes it at row * v3d_cs_stride + x.  Orientation must match the CPU path:
 * portablegl's draw_pixel stores at lastrow[-y*w + x], i.e. gl_FragCoord.y is
 * the row from the BOTTOM (standard OpenGL y-up), and memory row 0 is the top
 * of the displayed image.  The compute grid counts gid.y DOWN from memory row
 * 0, so the effect sees gl_FragCoord.y = res.y - gid.y - 0.5 while the SSBO
 * store still lands at row gid.y.  The binner path arrives at the same
 * convention through fy = -h/2 (v3d_3d.c viewport).  Colour is clamped and
 * rounded the same way the TLB would for an RGBA8 target.
 *
 * Workgroup shape: 32x8x1 (256 invocations, 16 QPU batches) by default.
 * Measured on the Pi 5 (shadertoy, 640x480): one 16-lane batch per workgroup
 * (16x1x1) left the CSD starved -- the heavy ray-marching effects ran at 2-7
 * FPS and even trivial ones at ~90; 128-lane workgroups gave 2-5x and 256
 * another 10-20% on the light effects (e.g. flame 2 -> 10 FPS, voronoise
 * 7 -> 44, graphing 115 -> 255).  Output is bit-identical across shapes.
 * The daemon reads the compiled local_size and derives the workgrid as
 * (ceil(w/x), ceil(h/y), 1); invocations past the frame edge exit early.
 * 256 is the V3D per-workgroup limit (CFG3 WG_SIZE is 8 bits, 0 == 256, the
 * same encoding mesa uses).  v3d_compile_set_cs_local_size() overrides the
 * shape for A/B measurements (shadertoy argv "WxH").
 *
 * Returns a malloc'd string (caller frees) or NULL on OOM / missing #version. */
static unsigned s_cs_local_x = 32u;
static unsigned s_cs_local_y = 8u;

extern "C" void
v3d_compile_set_cs_local_size(unsigned x, unsigned y)
{
   if (x == 0 || y == 0 || x * y > 256u)
      return;
   s_cs_local_x = x;
   s_cs_local_y = y;
}

static char *
fragment_to_compute_source(const char *frag_src)
{
   /* Skip the original #version line (it must come first in the output). */
   const char *body = frag_src;
   {
      const char *p = frag_src;
      while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
         p++;
      if (strncmp(p, "#version", 8) == 0) {
         const char *nl = strchr(p, '\n');
         body = nl ? nl + 1 : p + strlen(p);
      }
   }

   char layout[96];
   snprintf(layout, sizeof(layout),
            "layout(local_size_x = %u, local_size_y = %u, local_size_z = 1) in;\n",
            s_cs_local_x, s_cs_local_y);

   static const char prologue[] =
      "#version 310 es\n";
   static const char prologue2[] =
      "precision highp float;\n"
      "precision highp int;\n"
      "layout(std430, binding = 0) buffer V3dCsOut { uint v3d_cs_px[]; };\n"
      "uniform int v3d_cs_stride;\n"
      "vec4 v3d_frag_coord;\n"
      "vec4 v3d_frag_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
      "#define gl_FragCoord v3d_frag_coord\n"
      "#define gl_FragColor v3d_frag_color\n"
      "#define texture2D texture\n"
      "#define main v3d_frag_main\n"
      "#line 1\n";

   static const char epilogue[] =
      "\n#undef main\n"
      "void main() {\n"
      "    uvec2 gid = gl_GlobalInvocationID.xy;\n"
      "    uvec2 res = uvec2(iResolution.xy);\n"
      "    if (gid.x >= res.x || gid.y >= res.y) return;\n"
      /* Y-flip: match the CPU rasterizer (gl_FragCoord.y counts UP from the
       * bottom memory row) while the SSBO store still indexes rows top-down. */
      "    v3d_frag_coord = vec4(vec2(gid.x, res.y - 1u - gid.y) + vec2(0.5), 0.0, 1.0);\n"
      "    v3d_frag_main();\n"
      "    vec4 c = clamp(v3d_frag_color, 0.0, 1.0);\n"
      "    uvec4 u = uvec4(c * 255.0 + 0.5);\n"
      "    uint px = (u.a << 24u) | (u.r << 16u) | (u.g << 8u) | u.b;\n"
      "    v3d_cs_px[gid.y * uint(v3d_cs_stride) + gid.x] = px;\n"
      "}\n";

   size_t n = sizeof(prologue) - 1 + strlen(layout) + sizeof(prologue2) - 1 +
              strlen(body) + sizeof(epilogue) - 1 + 1;
   char *out = (char *)malloc(n);
   if (!out)
      return NULL;
   snprintf(out, n, "%s%s%s%s%s", prologue, layout, prologue2, body, epilogue);
   return out;
}

/* Compile a verbatim GLSL ES 3.10 compute source (no fragment wrapping) into
 * res->stage[V3D_STAGE_COMPUTE].  Shared by v3d_compile_compute() (fragment
 * effects, wrapped) and v3d_compile_compute_raw() (hand-written kernels such as
 * the geometry rasterizer).  cs_src is owned by the CALLER and is NOT freed
 * here. */
static bool
compile_compute_source(const char *cs_src,
                       struct v3d_compiled_program *res)
{
   if (!res)
      return false;
   memset(res, 0, sizeof(*res));

   if (!cs_src) {
      snprintf(res->log, sizeof(res->log),
               "compile_compute_source: compute source is required");
      return false;
   }

   struct gl_context *ctx =
      (struct gl_context *)calloc(1, sizeof(struct gl_context));
   if (!ctx) {
      snprintf(res->log, sizeof(res->log),
               "compile_compute_source: out of memory allocating GL context");
      return false;
   }
   /* Compute requires GLSL ES 3.10 (glsl_parser_extras: is_version(430, 310)). */
   init_context(ctx, 310, true);

   bool ok = false;
   struct gl_shader_program *prog = standalone_create_shader_program();
   if (!prog) {
      snprintf(res->log, sizeof(res->log),
               "v3d_compile_compute: failed to create shader program");
      goto cleanup_ctx;
   }

   {
      struct gl_shader *cs =
         standalone_add_shader_source(ctx, prog, GL_COMPUTE_SHADER, cs_src);
      _mesa_glsl_compile_shader(ctx, cs, NULL, false, false, true);
      if (!cs->CompileStatus) {
         log_append(res->log, sizeof(res->log), "compute shader:\n");
         log_append(res->log, sizeof(res->log), cs->InfoLog);
         goto cleanup_prog;
      }

      _mesa_clear_shader_program_data(ctx, prog);
      prog->data->LinkStatus = LINKING_SUCCESS;
      link_shaders_init(ctx, prog);
      gl_nir_link_glsl(ctx, prog);
      if (!prog->data->LinkStatus) {
         log_append(res->log, sizeof(res->log), "link:\n");
         log_append(res->log, sizeof(res->log), prog->data->InfoLog);
         goto cleanup_prog;
      }

      nir_shader *cs_nir = prog->_LinkedShaders[MESA_SHADER_COMPUTE]
         ? prog->_LinkedShaders[MESA_SHADER_COMPUTE]->Program->nir : NULL;
      if (!cs_nir) {
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_compute: link produced no CS NIR");
         goto cleanup_prog;
      }

      /* Same default-block uniform + sampler lowering as the graphics path
       * (see v3d_compile_glsl for why each step is needed). */
      {
         bool _lower_progress;
         assign_uniform_driver_locations(cs_nir);
         NIR_PASS(_lower_progress, cs_nir, nir_lower_io, nir_var_uniform,
                  type_size_uniform_bytes, (nir_lower_io_options)0);
      }
      gl_nir_lower_samplers(cs_nir, prog);

      /* SSBO: the GLSL link path leaves the V3dCsOut block as nir_var_mem_ssbo
       * deref chains (gl_nir_linker never calls this itself -- upstream does it
       * in st_glsl_to_nir).  gl_nir_lower_buffers rewrites them to
       * store_ssbo(block_index=0, byte_offset), which nir_to_vir turns into a
       * TMU general store at QUNIFORM_SSBO_OFFSET[0] + offset.  Without it the
       * deref reaches ntq_emit_instr and aborts. */
      gl_nir_lower_buffers(cs_nir, prog);

      struct v3d_be_result be;
      memset(&be, 0, sizeof(be));
      if (!v3d_be_compile_compute(cs_nir, &be, res->log, sizeof(res->log))) {
         v3d_be_result_free(&be);
         goto cleanup_prog;
      }

      /* SSBO block-index -> binding remap: gl_nir_lower_buffers records the
       * linked BLOCK INDEX in each QUNIFORM_SSBO_OFFSET slot's data, but the
       * daemon's expand_uniforms resolves the slot against its ssbo_iova[] table
       * BY BINDING.  With a single SSBO (shadertoy) index==binding coincides;
       * with several (the geometry rasterizer) the linker orders blocks by hash
       * iteration, permuting the mapping, so rewrite each slot's data to the
       * block's declared Binding before packing. */
      {
         struct gl_program *cs_sh =
            prog->_LinkedShaders[MESA_SHADER_COMPUTE]
               ? prog->_LinkedShaders[MESA_SHADER_COMPUTE]->Program : NULL;
         uint32_t ssbo_off = v3d_be_quniform_ssbo_offset();
         if (cs_sh && cs_sh->sh.ShaderStorageBlocks) {
            uint32_t nblk = (uint32_t)cs_sh->info.num_ssbos;
            for (uint32_t i = 0; i < be.cs.num_uniforms; i++) {
               if (be.cs.uniform_contents[i] == ssbo_off &&
                   be.cs.uniform_data[i] < nblk) {
                  be.cs.uniform_data[i] = (uint32_t)
                     cs_sh->sh.ShaderStorageBlocks[be.cs.uniform_data[i]]->Binding;
               }
            }
         }
      }

      bool packed = pack_stage(&be.cs, &res->stage[V3D_STAGE_COMPUTE]);
      v3d_be_result_free(&be);
      if (!packed) {
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_compute: out of memory packing QPU");
         v3d_compiled_program_free(res);
         goto cleanup_prog;
      }

      if (!build_uniform_bindings(cs_nir, &res->stage[V3D_STAGE_COMPUTE])) {
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_compute: out of memory building uniform reflection");
         v3d_compiled_program_free(res);
         goto cleanup_prog;
      }

      res->success = true;
      ok = true;
   }

cleanup_prog:
   standalone_destroy_shader_program(prog);
cleanup_ctx:
   free(ctx->screen);
   free(ctx);
   _mesa_glsl_builtin_functions_decref();
   return ok;
}

extern "C" bool
v3d_compile_compute(const char *frag_src,
                    struct v3d_compiled_program *res)
{
   if (!frag_src) {
      if (res) {
         memset(res, 0, sizeof(*res));
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_compute: fragment source is required");
      }
      return false;
   }

   char *cs_src = fragment_to_compute_source(frag_src);
   if (!cs_src) {
      if (res) {
         memset(res, 0, sizeof(*res));
         snprintf(res->log, sizeof(res->log),
                  "v3d_compile_compute: out of memory building compute source");
      }
      return false;
   }

   bool ok = compile_compute_source(cs_src, res);
   free(cs_src);
   return ok;
}

/* Verbatim compute entry: compile a hand-written GLSL ES 3.10 compute shader
 * (e.g. the fixed-gouraud geometry rasterizer in geom_raster_cs.h) WITHOUT the
 * fragment-shader wrapping that v3d_compile_compute() applies.  cs_src must be a
 * complete compute shader; it is neither modified nor freed. */
extern "C" bool
v3d_compile_compute_raw(const char *cs_src,
                        struct v3d_compiled_program *res)
{
   return compile_compute_source(cs_src, res);
}

extern "C" void
v3d_compiled_program_free(struct v3d_compiled_program *res)
{
   if (!res)
      return;
   for (int i = 0; i < V3D_STAGE_COUNT; i++) {
      free(res->stage[i].qpu);
      free(res->stage[i].uniform_contents);
      free(res->stage[i].uniform_data);
      free(res->stage[i].uniform_bindings);
      res->stage[i].qpu = NULL;
      res->stage[i].uniform_contents = NULL;
      res->stage[i].uniform_data = NULL;
      res->stage[i].uniform_bindings = NULL;
      res->stage[i].qpu_word_count = 0;
      res->stage[i].qpu_instr_count = 0;
      res->stage[i].num_uniforms = 0;
      res->stage[i].num_uniform_bindings = 0;
      res->stage[i].uniform_blob_dwords = 0;
   }
   res->success = false;
}
