/* pglcompiler_frontend.cpp - shared GLSL -> linked NIR frontend of the
 * on-device PGL_G3D compiler (see pglcompiler_frontend.h for the contract).
 *
 * This is the C++ half of the compiler: it drives mesa's GLSL frontend
 * (preprocess -> AST -> HIR -> NIR -> link) exactly the way mesa's own
 * standalone compiler does and applies the state-tracker lowering every
 * gallium driver relies on, then hands the linked NIR to the machine backend
 * (machines/<hw>/system/libs/{v3dcompiler,adrenocompiler}) which runs its own
 * NIR->ISA compiler and fills the neutral pglc_stage.
 *
 * WHY C++
 * -------
 * The GLSL frontend entry points (initialize_context_to_defaults,
 * standalone_create_shader_program, standalone_add_shader_source,
 * gl_nir_link_glsl, ...) have C++ linkage and are declared in C++ headers with
 * classes; they can only be called from a C++ TU.  The GPU backends, by
 * contrast, are C-only and their headers cannot be included from C++ safely
 * (v3d_compiler.h pulls <type_traits> under C linkage), so the two halves meet
 * at the C-linkage API of pglcompiler_frontend.h.  This file therefore knows
 * NOTHING about any GPU: the backend's nir_shader_compiler_options is the only
 * device-specific input.
 *
 * Platform-INDEPENDENT. Upstream pin f45825a0c724d06f231a1210484fe9af71a3ffab.
 * See VENDORED.txt.
 */
#include <stdarg.h>
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

/* TEMPORARY crash forensics (see gl_nir_linker.c NIRDBG): coarse breadcrumbs
 * across the frontend compile pipeline so ONE device run shows whether the
 * data-abort is in the pair or compute compile, and which step.  Remove once
 * the offending NIR pass is fixed. */
#include <ewoksys/klog.h>
#define FEDBG(fmt, ...) slog("fedbg: " fmt "\r\n", ##__VA_ARGS__)

/* TEMPORARY compile-phase wall-clock probe, twin of vir.c/nir_to_vir.c: same
 * /tmp/v3dc.log so the frontend steps (builtin init / parse / link / lower)
 * interleave with the backend strategy attempts in ONE timeline.  The city
 * faithful port spends >75s before the first backend record, so the frontend
 * is where the remaining wall time hides.  Remove once the hotspot is fixed. */
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
static double v3ct_now(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
static void v3ct_log(const char *fmt, ...)
{
   static FILE *f = NULL;
   va_list ap;
   if (!f)
      f = fopen("/tmp/v3dc.log", "a");
   if (!f)
      return;
   fprintf(f, "[%8.1f] ", v3ct_now());
   va_start(ap, fmt);
   vfprintf(f, fmt, ap);
   va_end(ap);
   fputc('\n', f);
   fflush(f);
}

#include "pglcompiler_frontend.h"   /* include/ is on the -I path */

struct pglc_fe_shaders {
   struct gl_context *ctx;
   struct gl_shader_program *prog;
};

/* Uniform type-size callback for nir_lower_io, in BYTES.
 *
 * nir_lower_io computes each load_uniform's byte offset as
 * base (= var->data.driver_location) + the offset get_io_offset() derives with
 * this callback (array/struct strides).  Backends consume (base + src[0]) / 4
 * as the dword index into the per-draw uniform blob, so BOTH driver_location
 * and this callback must be in bytes for the offset to be meaningful.
 * glsl_get_component_slots() counts 32-bit components, so * 4 is the byte
 * size; this matches the dense packing assign_uniform_driver_locations() uses
 * below. */
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
 * only shader_in/out) nor the GPU backends assign it -- upstream does this in
 * the gallium state tracker, which is not part of this port.  With
 * driver_location == ~0, nir_lower_io would emit load_uniform with a garbage
 * base, so the backend's dword index would be meaningless.
 *
 * Packing is component-dense (glsl_get_component_slots() * 4 bytes), matching
 * gl_nir_link_uniforms' CPU-side remap and keeping every offset 4-byte
 * aligned.  Opaque uniforms (samplers/images) are skipped: they are consumed
 * by texture intrinsics, never by load_deref, so nir_lower_io(nir_var_uniform)
 * leaves them untouched.
 *
 * Iteration is the shader's variable-declaration order (stable), so the
 * resulting dword indices are reproducible and the client uniform blob can be
 * packed against them by name. */
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
 * gl_nir_lower_optimize_varyings, so VS inputs stay as live variables) nor
 * the GPU backends assign it -- upstream does this in the gallium state
 * tracker, which is not part of this port.  This is the exact analogue of the
 * uniform gap fixed by assign_uniform_driver_locations() above.
 *
 * With every attribute sharing driver_location 0, nir_lower_io gives every
 * load_input the SAME base, so a backend places all attributes in the same
 * slot (on V3D: attribute 1, the teapot normal, would read attribute 0's
 * position VPM columns).  Assigning dense 0,1,2,... makes attribute N land in
 * its own slot, exactly what the daemon's attribute records program.
 *
 * driver_location is per ATTRIBUTE variable (a vec3 occupies one slot of
 * backend-defined width), so the dense index counts attributes, not
 * components.  VS input arrays are unsupported (one driver_location per
 * variable is correct here). */
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
 * dword index the backend emits).  It reflects exactly the uniforms that
 * survived linking -- the linker already dead-stripped the ones the shader
 * never reads, which is why the offsets differ per effect and a fixed
 * client-side layout would be wrong.  Opaque uniforms (samplers/images) are
 * skipped to match the assignment.  Returns true on success, including the
 * count == 0 no-op; false only on allocation failure. */
extern "C" bool
pglc_fe_build_uniform_bindings(nir_shader *nir, struct pglc_stage *st)
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

   struct pglc_uniform_binding *b =
      (struct pglc_uniform_binding *)calloc(count, sizeof(*b));
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

extern "C" void
pglc_fe_stage_free_common(struct pglc_stage *st)
{
   if (!st)
      return;
   free(st->code);
   free(st->uniform_bindings);
   memset(st, 0, sizeof(*st));
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
 * the GLSL frontend with no real GL driver attached.  `opts` is the backend's
 * nir_shader_compiler_options: it decides which ALU ops glsl_to_nir and the
 * focused nir_opt_algebraic() (pglcompiler_algebraic.c) lower, e.g.
 * lower_fsat for a V3D 4.2 that has no saturating unpack. */
static void
init_context(struct gl_context *ctx, unsigned glsl_version, bool is_es,
             const struct nir_shader_compiler_options *opts)
{
   initialize_context_to_defaults(ctx, is_es ? API_OPENGLES2
                                    : (glsl_version > 130 ? API_OPENGL_CORE
                                                          : API_OPENGL_COMPAT));
   _mesa_glsl_builtin_functions_init_or_ref();

   ctx->Version = 450;

   ctx->screen->nir_options[MESA_SHADER_VERTEX] = opts;
   ctx->screen->nir_options[MESA_SHADER_FRAGMENT] = opts;
   ctx->screen->nir_options[MESA_SHADER_COMPUTE] = opts;

   ctx->Const.GLSLVersion = glsl_version;
   ctx->Extensions.ARB_ES3_compatibility = true;
   ctx->Extensions.ARB_ES3_1_compatibility = true;
   ctx->Extensions.ARB_ES3_2_compatibility = true;
   /* initialize_context_to_defaults() advertises ARB_tessellation_shader, and
    * gl_nir_linker's prelink_lowering() asserts that a backend whose nir
    * options set compact_arrays (ir3 does; V3D does not) also lowers tess
    * levels as inputs. We never link tess stages, but port/assert.h keeps
    * mesa asserts live, so mirror the gallium rule here. */
   ctx->Const.GLSLTessLevelsAsInputs = opts->compact_arrays;

   ctx->Const.MaxComputeWorkGroupCount[0] = 65535;
   ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
   ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
   ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
   ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
   ctx->Const.MaxComputeWorkGroupSize[2] = 64;
   ctx->Const.MaxComputeWorkGroupInvocations = 1024;
   ctx->Const.MaxComputeSharedMemorySize = 32768;

   /* Compute-stage program limits + SSBO limits (compute route, GLSL ES 3.10).
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
extern "C" void
pglc_fe_log_append(char *log, size_t logsz, const char *src)
{
   if (!log || logsz == 0 || !src)
      return;
   size_t used = strlen(log);
   if (used + 1 >= logsz)
      return;
   snprintf(log + used, logsz - used, "%s", src);
}

static void
log_printf(char *log, size_t logsz, const char *fmt, ...)
{
   char tmp[512];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(tmp, sizeof(tmp), fmt, ap);
   va_end(ap);
   pglc_fe_log_append(log, logsz, tmp);
}

/* Allocate the shaders holder with a configured driver-less context.
 * gl_context is very large (gl_constants + extensions + driver state), so
 * heap-allocate it rather than put it on the (small) EwokOS app stack.
 * initialize_context_to_defaults() memsets it and calloc()s ctx->screen. */
static struct pglc_fe_shaders *
shaders_create(unsigned version, bool is_es,
               const struct nir_shader_compiler_options *opts,
               char *log, size_t logsz)
{
   struct pglc_fe_shaders *sh =
      (struct pglc_fe_shaders *)calloc(1, sizeof(*sh));
   if (!sh) {
      pglc_fe_log_append(log, logsz, "pglc_fe: out of memory\n");
      return NULL;
   }
   sh->ctx = (struct gl_context *)calloc(1, sizeof(struct gl_context));
   if (!sh->ctx) {
      pglc_fe_log_append(log, logsz,
                         "pglc_fe: out of memory allocating GL context\n");
      free(sh);
      return NULL;
   }
   init_context(sh->ctx, version, is_es, opts);
   sh->prog = standalone_create_shader_program();
   if (!sh->prog) {
      pglc_fe_log_append(log, logsz,
                         "pglc_fe: failed to create shader program\n");
      pglc_fe_shaders_free(sh);
      return NULL;
   }
   return sh;
}

extern "C" void
pglc_fe_shaders_free(struct pglc_fe_shaders *sh)
{
   if (!sh)
      return;
   if (sh->prog)
      standalone_destroy_shader_program(sh->prog);
   if (sh->ctx) {
      free(sh->ctx->screen);
      free(sh->ctx);
      _mesa_glsl_builtin_functions_decref();
   }
   free(sh);
}

static struct gl_program *
linked_program(const struct pglc_fe_shaders *sh, mesa_shader_stage stage)
{
   if (!sh || !sh->prog || !sh->prog->_LinkedShaders[stage])
      return NULL;
   return sh->prog->_LinkedShaders[stage]->Program;
}

extern "C" nir_shader *
pglc_fe_nir(const struct pglc_fe_shaders *sh, enum pglc_stage_id stage)
{
   mesa_shader_stage ms;
   switch (stage) {
   case PGLC_STAGE_VERTEX:
   case PGLC_STAGE_COORD:    ms = MESA_SHADER_VERTEX;   break;
   case PGLC_STAGE_FRAGMENT: ms = MESA_SHADER_FRAGMENT; break;
   case PGLC_STAGE_COMPUTE:  ms = MESA_SHADER_COMPUTE;  break;
   default: return NULL;
   }
   struct gl_program *p = linked_program(sh, ms);
   return p ? p->nir : NULL;
}

/* prog->_LinkedShaders[stage]->Program is struct gl_program*, whose
 * ->sh.ShaderStorageBlocks is gl_uniform_block** indexed by LINKED BLOCK INDEX
 * and whose ->info.num_ssbos counts them; each block's real binding is
 * ->Binding. */
extern "C" uint32_t
pglc_fe_num_ssbos(const struct pglc_fe_shaders *sh)
{
   struct gl_program *p = linked_program(sh, MESA_SHADER_COMPUTE);
   if (!p || !p->sh.ShaderStorageBlocks)
      return 0;
   return (uint32_t)p->info.num_ssbos;
}

extern "C" uint32_t
pglc_fe_ssbo_binding(const struct pglc_fe_shaders *sh, uint32_t index)
{
   struct gl_program *p = linked_program(sh, MESA_SHADER_COMPUTE);
   if (!p || !p->sh.ShaderStorageBlocks || index >= (uint32_t)p->info.num_ssbos)
      return index;
   return (uint32_t)p->sh.ShaderStorageBlocks[index]->Binding;
}

/* Lower default-block uniform derefs to load_uniform intrinsics.
 *
 * gl_nir_link_glsl -> nir_lower_io_passes only lowers shader_in/out;
 * nir_var_uniform derefs survive as deref_var + load_deref pairs, which no
 * backend accepts (the V3D one abort()s on nir_instr_type_deref).
 * Upstream gallium calls nir_lower_io(nir_var_uniform) in the state tracker
 * before handing the shader to the driver; this port was missing that step,
 * so every shader reading a uniform crashed the compiler.
 *
 * nir_lower_io bakes base = var->data.driver_location into each load_uniform,
 * but nothing else in this port assigns driver_location for default-block
 * uniforms (glsl_to_nir leaves it ~0).  So assign dense, 4-byte-aligned byte
 * offsets first -- see assign_uniform_driver_locations.  Then lower sampler2D
 * uniform derefs to texture/sampler index constants: nir_lower_io leaves
 * sampler derefs intact (they ride on nir_tex_instr's texture_deref /
 * sampler_deref sources, not load_deref).  gl_nir_lower_samplers does the
 * two-step conversion: gl_nir_lower_samplers_as_deref sets var->data.binding
 * from the linker's UniformStorage opaque index, then nir_lower_samplers
 * rewrites the tex instr sources to plain integer indices. */
static void
lower_uniforms_and_samplers(nir_shader *nir, struct gl_shader_program *prog)
{
   bool _lower_progress;
   assign_uniform_driver_locations(nir);
   NIR_PASS(_lower_progress, nir, nir_lower_io, nir_var_uniform,
            type_size_uniform_bytes, (nir_lower_io_options)0);
   gl_nir_lower_samplers(nir, prog);
}

static bool
link_program(struct pglc_fe_shaders *sh, char *log, size_t logsz)
{
   struct gl_context *ctx = sh->ctx;
   struct gl_shader_program *prog = sh->prog;

   _mesa_clear_shader_program_data(ctx, prog);
   prog->data->LinkStatus = LINKING_SUCCESS;
   link_shaders_init(ctx, prog);
   gl_nir_link_glsl(ctx, prog);

   if (!prog->data->LinkStatus) {
      pglc_fe_log_append(log, logsz, "link:\n");
      pglc_fe_log_append(log, logsz, prog->data->InfoLog);
      return false;
   }
   return true;
}

extern "C" bool
pglc_fe_compile_glsl(const char *vert_src, const char *frag_src,
                     const struct nir_shader_compiler_options *opts,
                     struct pglc_fe_shaders **out,
                     char *log, size_t logsz)
{
   if (out)
      *out = NULL;
   if (!out || !opts)
      return false;

   if (!vert_src || !frag_src) {
      log_printf(log, logsz,
                 "pglc_fe_compile_glsl: both vertex and fragment source are "
                 "required (vert=%p frag=%p)", (const void *)vert_src,
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

   struct pglc_fe_shaders *sh = shaders_create(version, is_es, opts, log, logsz);
   if (!sh)
      return false;
   struct gl_context *ctx = sh->ctx;
   struct gl_shader_program *prog = sh->prog;

   /* Compile both stages. */
   struct gl_shader *vs =
      standalone_add_shader_source(ctx, prog, GL_VERTEX_SHADER, vert_src);
   _mesa_glsl_compile_shader(ctx, vs, NULL, false, false, true);
   if (!vs->CompileStatus) {
      pglc_fe_log_append(log, logsz, "vertex shader:\n");
      pglc_fe_log_append(log, logsz, vs->InfoLog);
      goto fail;
   }

   {
      struct gl_shader *fs =
         standalone_add_shader_source(ctx, prog, GL_FRAGMENT_SHADER, frag_src);
      _mesa_glsl_compile_shader(ctx, fs, NULL, false, false, true);
      if (!fs->CompileStatus) {
         pglc_fe_log_append(log, logsz, "fragment shader:\n");
         pglc_fe_log_append(log, logsz, fs->InfoLog);
         goto fail;
      }

      /* Lower gl_FragColor (FRAG_RESULT_COLOR) to gl_FragData[0]
       * (FRAG_RESULT_DATA0) so the backends accept the FS.
       *
       * nir_lower_fragcolor() cannot be used here: it only rewrites store_deref
       * intrinsics (nir_lower_fragcolor.c:56), but glsl_to_nir emits the
       * whole-vector "gl_FragColor = <vec4>" assignment as a copy_deref (verified
       * by dumping fs->nir), so that pass is a silent no-op -- it returns without
       * touching the output variable.
       *
       * What the backend actually keys on is the output VARIABLE's data.location:
       * the linker's nir_lower_io_passes() (gl_nir_linker.c:1549) bakes it into
       * each store_output's io_semantics, and e.g. nir_to_vir.c
       * ntq_setup_outputs() walks the shader-out variables and hits
       * UNREACHABLE("Frag color should be lowered") on FRAG_RESULT_COLOR.
       * Renaming the variable to FRAG_RESULT_DATA0 BEFORE the link fixes both
       * the variable scan and the lowered io_semantics, and is form-agnostic
       * (copy_deref / store_deref alike).  This is exactly nir_lower_fragcolor()'s
       * effect for a single render target: its extra DATA1..N outputs only appear
       * when max_draw_buffers > 1, which the single-RT pipeline must not emit.
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
   }

   /* Link to a single program, producing the IO-lowered linked NIR. */
   FEDBG("pair: link_program enter");
   if (!link_program(sh, log, logsz))
      goto fail;
   FEDBG("pair: link_program done");

   {
      nir_shader *vs_nir = pglc_fe_nir(sh, PGLC_STAGE_VERTEX);
      nir_shader *fs_nir = pglc_fe_nir(sh, PGLC_STAGE_FRAGMENT);
      if (!vs_nir || !fs_nir) {
         pglc_fe_log_append(log, logsz,
                            "pglc_fe_compile_glsl: link produced no VS/FS NIR");
         goto fail;
      }

      /* VS input attributes need a dense driver_location too (see
       * assign_attribute_driver_locations); without it every attribute
       * collapses onto slot 0 and the normal reads position data. */
      assign_attribute_driver_locations(vs_nir);
      lower_uniforms_and_samplers(vs_nir, prog);
      lower_uniforms_and_samplers(fs_nir, prog);
   }

   FEDBG("pair: SUCCESS");
   *out = sh;
   return true;

fail:
   pglc_fe_shaders_free(sh);
   return false;
}

/* ---------------------------------------------------------------------------
 * Compute route
 * ------------------------------------------------------------------------- */

/* Workgroup shape for the fragment->compute wrapper: 32x8x1 (256 invocations)
 * by default.  Measured on the Pi 5 (shadertoy, 640x480): one 16-lane batch
 * per workgroup (16x1x1) left the CSD starved -- the heavy ray-marching
 * effects ran at 2-7 FPS and even trivial ones at ~90; 128-lane workgroups
 * gave 2-5x and 256 another 10-20% on the light effects (e.g. flame 2 -> 10
 * FPS, voronoise 7 -> 44, graphing 115 -> 255).  Output is bit-identical
 * across shapes.  The daemon reads the compiled local_size and derives the
 * workgrid as (ceil(w/x), ceil(h/y), 1); invocations past the frame edge exit
 * early.  256 is the V3D per-workgroup limit (CFG3 WG_SIZE is 8 bits, 0 ==
 * 256) and well within Adreno's 1024.  pglc_compile_set_cs_local_size()
 * overrides the shape for A/B measurements (shadertoy argv "WxH"). */
static unsigned s_cs_local_x = 32u;
static unsigned s_cs_local_y = 8u;

extern "C" void
pglc_compile_set_cs_local_size(unsigned x, unsigned y)
{
   if (x == 0 || y == 0 || x * y > 256u)
      return;
   s_cs_local_x = x;
   s_cs_local_y = y;
}

extern "C" void
pglc_fe_get_cs_local_size(unsigned *x, unsigned *y)
{
   if (x) *x = s_cs_local_x;
   if (y) *y = s_cs_local_y;
}

/* Rewrite a shadertoy-style GLSL ES 1.00 fragment shader into a GLSL ES 3.10
 * compute shader that evaluates one pixel per invocation and stores the
 * packed ARGB8888 result into an SSBO (the frame buffer), instead of going
 * through the render target.  The effect body is kept verbatim; only the
 * surrounding contract changes:
 *
 *   #version 100            -> #version 310 es + local_size + SSBO declaration
 *   gl_FragCoord            -> pglc_frag_coord (global, set per invocation)
 *   gl_FragColor            -> pglc_frag_color (global, read back after main)
 *   texture2D()             -> texture()     (texture2D was removed in ES 3.00)
 *   void main()             -> void pglc_frag_main()  (via #define main)
 *
 * The epilogue main() derives the pixel from gl_GlobalInvocationID, bounds-
 * checks against iResolution, runs the effect, clamps/packs the colour and
 * writes it at row * pglc_cs_stride + x.  Orientation must match the CPU path:
 * portablegl's draw_pixel stores at lastrow[-y*w + x], i.e. gl_FragCoord.y is
 * the row from the BOTTOM (standard OpenGL y-up), and memory row 0 is the top
 * of the displayed image.  The compute grid counts gid.y DOWN from memory row
 * 0, so the effect sees gl_FragCoord.y = res.y - gid.y - 0.5 while the SSBO
 * store still lands at row gid.y.  Colour is clamped and rounded the same way
 * a fixed-function RGBA8 target would.
 *
 * Returns a malloc'd string (caller frees) or NULL on OOM. */
extern "C" char *
pglc_fe_fragment_to_compute_source(const char *frag_src)
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
      "layout(std430, binding = 0) buffer PglCsOut { uint pglc_cs_px[]; };\n"
      "uniform int pglc_cs_stride;\n"
      "vec4 pglc_frag_coord;\n"
      "vec4 pglc_frag_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
      "#define gl_FragCoord pglc_frag_coord\n"
      "#define gl_FragColor pglc_frag_color\n"
      "#define texture2D texture\n"
      "#define textureCube texture\n"
      "#define main pglc_frag_main\n"
      "#line 1\n";

   static const char epilogue[] =
      "\n#undef main\n"
      "void main() {\n"
      "    uvec2 gid = gl_GlobalInvocationID.xy;\n"
      "    uvec2 res = uvec2(iResolution.xy);\n"
      "    if (gid.x >= res.x || gid.y >= res.y) return;\n"
      /* Y-flip: match the CPU rasterizer (gl_FragCoord.y counts UP from the
       * bottom memory row) while the SSBO store still indexes rows top-down. */
      "    pglc_frag_coord = vec4(vec2(gid.x, res.y - 1u - gid.y) + vec2(0.5), 0.0, 1.0);\n"
      "    pglc_frag_main();\n"
      "    vec4 c = clamp(pglc_frag_color, 0.0, 1.0);\n"
      "    uvec4 u = uvec4(c * 255.0 + 0.5);\n"
      "    uint px = (u.a << 24u) | (u.r << 16u) | (u.g << 8u) | u.b;\n"
      "    pglc_cs_px[gid.y * uint(pglc_cs_stride) + gid.x] = px;\n"
      "}\n";

   size_t n = sizeof(prologue) - 1 + strlen(layout) + sizeof(prologue2) - 1 +
              strlen(body) + sizeof(epilogue) - 1 + 1;
   char *out = (char *)malloc(n);
   if (!out)
      return NULL;
   snprintf(out, n, "%s%s%s%s%s", prologue, layout, prologue2, body, epilogue);
   return out;
}

extern "C" bool
pglc_fe_compile_compute(const char *cs_src,
                        const struct nir_shader_compiler_options *opts,
                        struct pglc_fe_shaders **out,
                        char *log, size_t logsz)
{
   if (out)
      *out = NULL;
   if (!out || !opts)
      return false;

   if (!cs_src) {
      pglc_fe_log_append(log, logsz,
                         "pglc_fe_compile_compute: compute source is required");
      return false;
   }

   /* declared up front: the fail goto below must not cross initialisations */
   double _t_sh = 0, _t_cs = 0, _t_lk = 0, _t_u = 0, _t_b = 0;
   double _t_shx = 0, _t_csx = 0, _t_lkx = 0, _t_ux = 0, _t_bx = 0;

   /* Compute requires GLSL ES 3.10 (glsl_parser_extras: is_version(430, 310)). */
   _t_shx = v3ct_now();
   struct pglc_fe_shaders *sh = shaders_create(310, true, opts, log, logsz);
   _t_sh = v3ct_now() - _t_shx;
   if (!sh)
      return false;
   struct gl_context *ctx = sh->ctx;
   struct gl_shader_program *prog = sh->prog;

   struct gl_shader *cs =
      standalone_add_shader_source(ctx, prog, GL_COMPUTE_SHADER, cs_src);
   FEDBG("cs: compile_shader enter");
   _t_csx = v3ct_now();
   _mesa_glsl_compile_shader(ctx, cs, NULL, false, false, true);
   _t_cs = v3ct_now() - _t_csx;
   v3ct_log("fe shaders_create=%.0f compile_shader=%.0f", _t_sh, _t_cs);
   FEDBG("cs: compile_shader done status=%d", (int)cs->CompileStatus);
   if (!cs->CompileStatus) {
      pglc_fe_log_append(log, logsz, "compute shader:\n");
      pglc_fe_log_append(log, logsz, cs->InfoLog);
      goto fail;
   }

   FEDBG("cs: link_program enter");
   _t_lkx = v3ct_now();
   if (!link_program(sh, log, logsz))
      goto fail;
   _t_lk = v3ct_now() - _t_lkx;
   v3ct_log("fe link_program(inline+opts+lower) ms=%.0f", _t_lk);
   FEDBG("cs: link_program done");

   {
      nir_shader *cs_nir = pglc_fe_nir(sh, PGLC_STAGE_COMPUTE);
      if (!cs_nir) {
         pglc_fe_log_append(log, logsz,
                            "pglc_fe_compile_compute: link produced no CS NIR");
         goto fail;
      }

      /* Same default-block uniform + sampler lowering as the graphics path. */
      FEDBG("cs: lower_uniforms enter");
      _t_ux = v3ct_now();
      lower_uniforms_and_samplers(cs_nir, prog);
      _t_u = v3ct_now() - _t_ux;
      v3ct_log("fe lower_uniforms ms=%.0f", _t_u);
      FEDBG("cs: lower_uniforms done");

      /* SSBO: the GLSL link path leaves the SSBO blocks as nir_var_mem_ssbo
       * deref chains (gl_nir_linker never calls this itself -- upstream does it
       * in st_glsl_to_nir).  gl_nir_lower_buffers rewrites them to
       * load/store_ssbo(block_index, byte_offset); block_index is the LINKED
       * index, see pglc_fe_ssbo_binding() for the remap the backend must apply. */
      FEDBG("cs: lower_buffers enter");
      _t_bx = v3ct_now();
      gl_nir_lower_buffers(cs_nir, prog);
      _t_b = v3ct_now() - _t_bx;
      v3ct_log("fe lower_buffers ms=%.0f", _t_b);
      FEDBG("cs: lower_buffers done");
   }

   FEDBG("cs: SUCCESS");
   *out = sh;
   return true;

fail:
   pglc_fe_shaders_free(sh);
   return false;
}
