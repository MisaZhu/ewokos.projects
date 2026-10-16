/* WS6 host oracle - uniform-stream dumper.
 *
 * Host-only verification tool (macOS/Linux). Compiles each of the 11 shadertoy
 * GLSL ES effects with the SAME platform-independent v3d_compile_glsl() entry
 * the device uses, then prints the fragment stage's uniform stream so the
 * QUNIFORM_UNIFORM dword offsets (the load_uniform buffer indices the client
 * blob must fill) are known by OBSERVATION, not guesswork.
 *
 * This file is never built for the device; it lives beside the compiler and is
 * compiled by Makefile.host into host/dump_uniforms.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "v3dcompiler.h"
#include "shadertoy_glsl.h"
#include "teapot_glsl.h"
#include "geom_raster_cs.h"

/* enum quniform_contents (mesa v3d_compiler.h) - names for the dump. Index ==
 * enumerator value; entries past the ones the GLSL ES path can emit are left
 * as placeholders. */
static const char *quniform_name(unsigned c)
{
   switch (c) {
   case 0:  return "CONSTANT";
   case 1:  return "UNIFORM";          /* data == dword index into load_uniform */
   case 2:  return "VIEWPORT_X_SCALE";
   case 3:  return "VIEWPORT_Y_SCALE";
   case 4:  return "VIEWPORT_Z_OFFSET";
   case 5:  return "VIEWPORT_Z_SCALE";
   case 6:  return "TMU_CONFIG_P0";
   case 7:  return "TMU_CONFIG_P1";
   case 8:  return "IMAGE_TMU_CONFIG_P0";
   case 9:  return "TEXTURE_FIRST_LEVEL";
   case 10: return "TEXTURE_WIDTH";
   case 11: return "TEXTURE_HEIGHT";
   case 12: return "TEXTURE_DEPTH";
   case 13: return "TEXTURE_ARRAY_SIZE";
   case 14: return "TEXTURE_LEVELS";
   case 15: return "TEXTURE_SAMPLES";
   case 16: return "UBO_ADDR";
   case 17: return "TEXRECT_SCALE_X";
   case 18: return "TEXRECT_SCALE_Y";
   case 19: return "SSBO_OFFSET";
   case 20: return "GET_SSBO_SIZE";
   case 21: return "GET_UBO_SIZE";
   case 28: return "NUM_WORK_GROUPS";
   case 29: return "WORK_GROUP_BASE";
   case 30: return "WORK_GROUP_SIZE";
   case 31: return "SPILL_OFFSET";
   case 32: return "SPILL_SIZE_PER_THR";
   case 33: return "SHARED_OFFSET";
   case 34: return "SHARED_SIZE";
   default: return "?";
   }
}

static const char *effect_names[SHADERTOY_NUM_SHADERS] = {
   "graphing_lines", "graphing", "my_tunnel", "running_in_the_night",
   "square_tunnel", "deform_tunnel", "tileable_water_caustic", "iqs_eyeball",
   "voronoise", "the_cave", "flame",
};

/* WS6 step 1 - teapot VS+FS oracle.
 *
 * Compiles the teapot's GLSL ES 3.10 vertex (per-vertex Blinn-Phong) + fragment
 * (gouraud passthrough) pair with the SAME v3d_compile_glsl() the device uses and
 * dumps exactly what the WS5 client relies on:
 *   - the COORD stage emitted real QPU (arbitrary-geometry binning, not the
 *     fixed pass-through coord shader);
 *   - the VS uniform reflection carries the three mat4 (16 dwords each) and the
 *     two vec3 (3 dwords each) so pgl_g3d_shader_uniform_dword can resolve them
 *     by name and the client blob covers every QUNIFORM_UNIFORM slot;
 *   - the attribute driver_location_map surfaces attr0 (aPosition) and attr1
 *     (aNormal) so the driver's two interleaved attribute records line up;
 *   - the coord and VS QUNIFORM_UNIFORM dword ranges agree (one shared blob).
 * Returns the number of failed checks. */
static int dump_teapot(void)
{
   struct v3d_compiled_program res;
   int fail = 0;
   bool ok = v3d_compile_glsl(teapot_vs_glsl, teapot_fs_glsl, &res);
   printf("\n=== teapot VS+FS (GLSL ES 3.10) : %s ===\n", ok ? "OK" : "FAIL");
   if (!ok) {
      printf("  log: %s\n", res.log);
      return 1;
   }

   const struct v3d_compiled_stage *vs = &res.stage[V3D_STAGE_VERTEX];
   const struct v3d_compiled_stage *fs = &res.stage[V3D_STAGE_FRAGMENT];
   const struct v3d_compiled_stage *cs = &res.stage[V3D_STAGE_COORD];

   printf("  VS qpu_instr=%u needs_coords=%d\n",
          vs->qpu_instr_count, (int)vs->needs_coords);
   printf("  FS qpu_instr=%u num_uniforms=%u num_inputs=%u writes_z=%d\n",
          fs->qpu_instr_count, fs->num_uniforms, fs->num_inputs,
          (int)fs->writes_z);
   printf("  COORD qpu_instr=%u num_uniforms=%u vpm_in=%u vpm_out=%u "
          "sep_seg=%d vcm=%u\n",
          cs->qpu_instr_count, cs->num_uniforms, cs->vpm_input_size,
          cs->vpm_output_size, (int)cs->separate_segments, cs->vcm_cache_size);
   if (cs->qpu_instr_count == 0) {
      printf("  !! COORD stage emitted NO QPU (binning would fall back to the "
             "fixed pass-through coord shader)\n");
      fail++;
   }
   if (fs->num_inputs == 0) {
      printf("  !! FS reads NO varying input (the vec4 vColor did not link)\n");
      fail++;
   }

   /* Attribute driver_locations: aPosition must occupy driver slot 0 (VPM
    * columns 0-2) and aNormal driver slot 1 (VPM columns 3-5) so the two
    * interleaved attribute records (pos float3 @base, normal float3 @base+12)
    * programmed by build_attr_records_compiled2() feed the right shader inputs.
    * The definitive, device-relevant condition is vattr_sizes[0]==3 &&
    * vattr_sizes[1]==3 (ntq_emit_load_input places attr N at VPM column
    * sum(vattr_sizes[0..N-1])+comp).  driver_location_map is indexed by the
    * mesa data.location (layout_location + VERT_ATTRIB_GENERIC0 == 15/16 here,
    * NOT 0/1), so scan the whole map for driver slots 0 and 1 rather than
    * assuming a [0..3] window. */
   int map_slot0 = -1, map_slot1 = -1;
   for (unsigned m = 0; m < sizeof(vs->driver_location_map) /
                            sizeof(vs->driver_location_map[0]); m++) {
      if (vs->driver_location_map[m] == 0 && map_slot0 < 0) map_slot0 = (int)m;
      if (vs->driver_location_map[m] == 1 && map_slot1 < 0) map_slot1 = (int)m;
   }
   printf("  VS attr vattr_sizes[0..1]=%u,%u  driver_location_map: slot0@loc=%d "
          "slot1@loc=%d\n",
          vs->vattr_sizes[0], vs->vattr_sizes[1], map_slot0, map_slot1);
   if (vs->vattr_sizes[0] != 3 || vs->vattr_sizes[1] != 3) {
      printf("  !! expected vattr_sizes[0]==3 (pos) and [1]==3 (normal), got %u,%u "
             "-- attr1 would read attr0's VPM columns\n",
             vs->vattr_sizes[0], vs->vattr_sizes[1]);
      fail++;
   }
   if (map_slot0 < 0 || map_slot1 < 0) {
      printf("  !! attr driver slots 0 and 1 not both present in "
             "driver_location_map\n");
      fail++;
   }

   /* VS uniform reflection: the client resolves uMVP/uModelView/uNormalMat
    * (mat4 -> 16 dwords) and uMaterial/uLightEye (vec3 -> 3 dwords) by name. */
   printf("  VS uniform bindings (num=%u, blob_dwords=%u):\n",
          vs->num_uniform_bindings, vs->uniform_blob_dwords);
   int n_mat4 = 0, n_vec3 = 0;
   for (unsigned b = 0; b < vs->num_uniform_bindings; b++) {
      const struct v3d_uniform_binding *ub = &vs->uniform_bindings[b];
      printf("    %-16s dword=%-3u comps=%u\n",
             ub->name, ub->dword_offset, ub->num_components);
      if (ub->num_components == 16) n_mat4++;
      else if (ub->num_components == 3) n_vec3++;
   }
   if (n_mat4 != 3) {
      printf("  !! expected 3 mat4 (16-dword) VS uniforms, found %d\n", n_mat4);
      fail++;
   }
   if (n_vec3 != 2) {
      printf("  !! expected 2 vec3 (3-dword) VS uniforms, found %d\n", n_vec3);
      fail++;
   }

   /* COORD shares the VS's uniform driver_locations, so its QUNIFORM_UNIFORM
    * dword range must sit inside the VS's (one client blob feeds both). */
   {
      unsigned cmin = 0xffffffffu, cmax = 0u; int cn = 0;
      for (unsigned u = 0; u < cs->num_uniforms; u++)
         if (cs->uniform_contents[u] == 1) {
            unsigned d = cs->uniform_data[u];
            cn++; if (d < cmin) cmin = d; if (d > cmax) cmax = d;
         }
      unsigned vmin = 0xffffffffu, vmax = 0u; int vn = 0;
      for (unsigned u = 0; u < vs->num_uniforms; u++)
         if (vs->uniform_contents[u] == 1) {
            unsigned d = vs->uniform_data[u];
            vn++; if (d < vmin) vmin = d; if (d > vmax) vmax = d;
         }
      printf("  >> VS    QUNIFORM_UNIFORM slots=%d dword range=[%u..%u]\n",
             vn, vn ? vmin : 0, vn ? vmax : 0);
      printf("  >> COORD QUNIFORM_UNIFORM slots=%d dword range=[%u..%u]\n",
             cn, cn ? cmin : 0, cn ? cmax : 0);
      if (cn && vn && (cmin < vmin || cmax > vmax)) {
         printf("  !! COORD dword range escapes the VS range\n");
         fail++;
      }
      if (vn == 0) {
         printf("  !! VS emitted no QUNIFORM_UNIFORM (uniforms not lowered)\n");
         fail++;
      }
   }

   v3d_compiled_program_free(&res);
   printf("=== teapot checks FAILED: %d ===\n", fail);
   return fail;
}

/* CSD compute route: compile every effect's FRAGMENT source through
 * v3d_compile_compute() (fragment -> compute rewrite + MESA_SHADER_COMPUTE
 * backend) and dump what the device CSD dispatcher must satisfy: the QPU
 * size/threads, the declared local_size, and the complete uniform stream
 * (every QUNIFORM_* the dispatcher's expand step has to resolve -- in
 * particular SSBO_OFFSET for the frame buffer, NUM_WORK_GROUPS /
 * WORK_GROUP_* / SHARED_* if the backend asks for them, and the named
 * default-block uniforms incl. v3d_cs_stride). */
static int dump_compute(void)
{
   int fail = 0;
   printf("\n########## COMPUTE (CSD route) ##########\n");
   for (int i = 0; i < SHADERTOY_NUM_SHADERS; i++) {
      struct v3d_compiled_program res;
      bool ok = v3d_compile_compute(shadertoy_fs_glsl[i], &res);
      printf("=== CS [%2d] %-24s : %s ===\n", i, effect_names[i],
             ok ? "OK" : "FAIL");
      if (!ok) {
         printf("  log: %s\n", res.log);
         fail++;
         continue;
      }
      const struct v3d_compiled_stage *cs = &res.stage[V3D_STAGE_COMPUTE];
      printf("  CS qpu_instr=%u num_uniforms=%u threads=%u single_seg=%d "
             "local_size=%ux%ux%u shared=%u\n",
             cs->qpu_instr_count, cs->num_uniforms, cs->threads,
             (int)cs->single_seg, cs->local_size[0], cs->local_size[1],
             cs->local_size[2], cs->shared_size);
      if (cs->qpu_instr_count == 0) {
         printf("  !! CS stage emitted NO QPU\n");
         fail++;
      }
      for (unsigned u = 0; u < cs->num_uniforms; u++) {
         unsigned c = cs->uniform_contents[u];
         unsigned d = cs->uniform_data[u];
         printf("    CS unif[%2u] %-18s data=%-6u (0x%08x)\n",
                u, quniform_name(c), d, d);
      }
      printf("  CS uniform bindings (num=%u, blob_dwords=%u):\n",
             cs->num_uniform_bindings, cs->uniform_blob_dwords);
      for (unsigned b = 0; b < cs->num_uniform_bindings; b++) {
         const struct v3d_uniform_binding *ub = &cs->uniform_bindings[b];
         printf("    %-16s dword=%-3u comps=%u\n",
                ub->name, ub->dword_offset, ub->num_components);
      }
      v3d_compiled_program_free(&res);
   }
   printf("\n=== %d/%d shadertoy effects FAILED to compile as COMPUTE ===\n",
          fail, SHADERTOY_NUM_SHADERS);
   return fail;
}

/* Geometry-rasterizer route: compile the fixed-gouraud pass-2 raster kernel
 * (geom_raster_cs.h) VERBATIM through v3d_compile_compute_raw() and verify the
 * things the device draw_compute_geom() depends on: local_size 16x16x1 (one
 * lane per triangle, 256 triangles per workgroup), no shared memory, and that
 * each SSBO_OFFSET slot's data is the declared BINDING (fb=0, vout=1, depth=2),
 * not the linker's permuted block index.  The two-pass split moved the vertex
 * transform into its own kernel, so pass 2 reads pre-transformed `vout` records
 * and touches exactly three SSBOs. */
static int dump_geom(void)
{
   int fail = 0;
   printf("\n########## GEOM RASTERIZER (compute_raw) ##########\n");
   struct v3d_compiled_program res;
   bool ok = v3d_compile_compute_raw(geom_raster_cs_glsl, &res);
   printf("=== geom_raster_cs : %s ===\n", ok ? "OK" : "FAIL");
   if (!ok) {
      printf("  log: %s\n", res.log);
      return 1;
   }
   const struct v3d_compiled_stage *cs = &res.stage[V3D_STAGE_COMPUTE];
   printf("  CS qpu_instr=%u num_uniforms=%u threads=%u single_seg=%d "
          "local_size=%ux%ux%u shared=%u\n",
          cs->qpu_instr_count, cs->num_uniforms, cs->threads,
          (int)cs->single_seg, cs->local_size[0], cs->local_size[1],
          cs->local_size[2], cs->shared_size);
   if (cs->qpu_instr_count == 0) { printf("  !! no QPU\n"); fail++; }
   if (cs->local_size[0] != 16 || cs->local_size[1] != 16 ||
       cs->local_size[2] != 1) { printf("  !! local_size != 16x16x1\n"); fail++; }
   if (cs->shared_size != 0) { printf("  !! shared_size != 0\n"); fail++; }

   unsigned n_ssbo = 0;
   bool seen[3] = { false, false, false };
   for (unsigned u = 0; u < cs->num_uniforms; u++) {
      unsigned c = cs->uniform_contents[u];
      unsigned d = cs->uniform_data[u];
      printf("    CS unif[%2u] %-18s data=%-6u (0x%08x)\n",
             u, quniform_name(c), d, d);
      if (c == 19 /* SSBO_OFFSET */) {
         n_ssbo++;
         if (d < 3)
            seen[d] = true;
         else { printf("  !! SSBO_OFFSET binding %u out of range\n", d); fail++; }
      }
   }
   /* Each SSBO access (load AND store) emits its own SSBO_OFFSET slot, so there
    * are more than 3 slots; the invariant is that every slot's data is a valid
    * binding in [0,3) and all three bindings are covered (the bijective remap). */
   if (n_ssbo < 3) { printf("  !! expected >=3 SSBO_OFFSET slots, got %u\n", n_ssbo); fail++; }
   for (int b = 0; b < 3; b++)
      if (!seen[b]) { printf("  !! binding %d missing from SSBO_OFFSET remap\n", b); fail++; }

   printf("  CS uniform bindings (num=%u, blob_dwords=%u):\n",
          cs->num_uniform_bindings, cs->uniform_blob_dwords);
   for (unsigned b = 0; b < cs->num_uniform_bindings; b++) {
      const struct v3d_uniform_binding *ub = &cs->uniform_bindings[b];
      printf("    %-16s dword=%-3u comps=%u\n",
             ub->name, ub->dword_offset, ub->num_components);
   }
   v3d_compiled_program_free(&res);
   printf("=== geom checks %s ===\n", fail ? "FAILED" : "PASSED");
   return fail;
}

int main(void)
{
   int fail = 0;
   for (int i = 0; i < SHADERTOY_NUM_SHADERS; i++) {
      struct v3d_compiled_program res;
      bool ok = v3d_compile_glsl(shadertoy_vs_glsl, shadertoy_fs_glsl[i], &res);
      printf("=== [%2d] %-24s : %s ===\n", i, effect_names[i], ok ? "OK" : "FAIL");
      if (!ok) {
         printf("  log: %s\n", res.log);
         fail++;
         continue;
      }

      const struct v3d_compiled_stage *vs = &res.stage[V3D_STAGE_VERTEX];
      const struct v3d_compiled_stage *fs = &res.stage[V3D_STAGE_FRAGMENT];
      const struct v3d_compiled_stage *cs = &res.stage[V3D_STAGE_COORD];

      printf("  VS qpu_instr=%u needs_coords=%d\n",
             vs->qpu_instr_count, (int)vs->needs_coords);
      printf("  FS qpu_instr=%u num_uniforms=%u needs_coords=%d num_inputs=%u "
             "uses_center_w=%d writes_z=%d threads=%u\n",
             fs->qpu_instr_count, fs->num_uniforms, (int)fs->needs_coords,
             fs->num_inputs, (int)fs->uses_center_w, (int)fs->writes_z,
             fs->threads);

      /* COORD stage (WS1): the same VS NIR compiled with is_coord=true. It MUST
       * be present (non-zero QPU) for arbitrary-geometry binning, and its
       * QUNIFORM_UNIFORM dword indices MUST be a subset of the render VS's (one
       * shared driver_location assignment), so one client blob feeds both. */
      printf("  COORD qpu_instr=%u num_uniforms=%u vpm_in=%u vpm_out=%u "
             "sep_seg=%d vcm=%u\n",
             cs->qpu_instr_count, cs->num_uniforms, cs->vpm_input_size,
             cs->vpm_output_size, (int)cs->separate_segments, cs->vcm_cache_size);
      if (cs->qpu_instr_count == 0) {
         printf("  !! COORD stage emitted NO QPU (binning would fall back to the "
                "fixed pass-through coord shader)\n");
         fail++;
      }
      {
         unsigned cmin = 0xffffffffu, cmax = 0u; int cn = 0;
         for (unsigned u = 0; u < cs->num_uniforms; u++) {
            if (cs->uniform_contents[u] == 1) { /* QUNIFORM_UNIFORM */
               unsigned d = cs->uniform_data[u];
               cn++; if (d < cmin) cmin = d; if (d > cmax) cmax = d;
            }
         }
         unsigned vmin = 0xffffffffu, vmax = 0u; int vn = 0;
         for (unsigned u = 0; u < vs->num_uniforms; u++) {
            if (vs->uniform_contents[u] == 1) {
               unsigned d = vs->uniform_data[u];
               vn++; if (d < vmin) vmin = d; if (d > vmax) vmax = d;
            }
         }
         printf("  >> VS    QUNIFORM_UNIFORM slots=%d dword range=[%u..%u]\n",
                vn, vn ? vmin : 0, vn ? vmax : 0);
         printf("  >> COORD QUNIFORM_UNIFORM slots=%d dword range=[%u..%u]\n",
                cn, cn ? cmin : 0, cn ? cmax : 0);
         if (cn && vn && (cmin < vmin || cmax > vmax))
            printf("  !! COORD dword range escapes the VS range - the shared "
                   "blob would not cover the coord stage\n");
      }
      printf("  VS uniform bindings (num=%u, blob_dwords=%u):\n",
             vs->num_uniform_bindings, vs->uniform_blob_dwords);
      for (unsigned b = 0; b < vs->num_uniform_bindings; b++) {
         const struct v3d_uniform_binding *ub = &vs->uniform_bindings[b];
         printf("    %-16s dword=%-3u comps=%u\n",
                ub->name, ub->dword_offset, ub->num_components);
      }
      printf("  VS attr driver_location_map[0..3]=%d,%d,%d,%d vattr_sizes[0]=%u\n",
             vs->driver_location_map[0], vs->driver_location_map[1],
             vs->driver_location_map[2], vs->driver_location_map[3],
             vs->vattr_sizes[0]);

      unsigned min_unif = 0xffffffffu, max_unif = 0u;
      int n_unif_slots = 0;
      for (unsigned u = 0; u < fs->num_uniforms; u++) {
         unsigned c = fs->uniform_contents[u];
         unsigned d = fs->uniform_data[u];
         printf("    FS unif[%2u] %-18s data=%-6u (0x%08x)\n",
                u, quniform_name(c), d, d);
         if (c == 1) { /* QUNIFORM_UNIFORM */
            n_unif_slots++;
            if (d < min_unif) min_unif = d;
            if (d > max_unif) max_unif = d;
         }
      }
      if (n_unif_slots)
         printf("  >> QUNIFORM_UNIFORM slots=%d dword range=[%u..%u]\n",
                n_unif_slots, min_unif, max_unif);
      else
         printf("  >> no QUNIFORM_UNIFORM (effect reads no default-block uniform)\n");

      /* Name -> dword-offset reflection the client packs the blob by. Cross-check:
       * every FS QUNIFORM_UNIFORM data value above must equal some binding's
       * dword_offset (+0..num_components), otherwise the reflected table and the
       * emitted stream disagree. */
      printf("  FS uniform bindings (num=%u, blob_dwords=%u):\n",
             fs->num_uniform_bindings, fs->uniform_blob_dwords);
      for (unsigned b = 0; b < fs->num_uniform_bindings; b++) {
         const struct v3d_uniform_binding *ub = &fs->uniform_bindings[b];
         printf("    %-16s dword=%-3u comps=%u\n",
                ub->name, ub->dword_offset, ub->num_components);
      }

      v3d_compiled_program_free(&res);
   }
   printf("\n=== %d/%d shadertoy effects FAILED to compile ===\n", fail,
          SHADERTOY_NUM_SHADERS);
   int tfail = dump_teapot();
   int cfail = dump_compute();
   int gfail = dump_geom();
   return (fail || tfail || cfail || gfail) ? 1 : 0;
}
