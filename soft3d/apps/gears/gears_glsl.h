/*
 * gears_glsl.h -- GLSL ES 3.10 port of the gears demo's per-vertex diffuse
 * lighting vertex shader paired with a gouraud-passthrough fragment shader.
 *
 * These sources mirror the C vertex_shader() + built-in pgl_ff_gouraud() pair
 * in gears.c EXACTLY, so the gear train looks identical whether it is lit on
 * the CPU rasterizer (the automatic fallback) or with the vertex stage compiled
 * to VideoCore VII QPU (raspi5 / V3D 7.1) by pglc_compile_glsl().
 *
 * Convention (identical to teapot_glsl.h):
 *   - #version 310 es with explicit layout(location=) so aPosition binds vertex
 *     attribute 0 and aNormal binds attribute 1, matching the driver's two
 *     interleaved attribute records (position float3 at base, normal float3 at
 *     base+12).  Declaration order alone is not relied upon.
 *   - mat4 uniforms are uploaded in portablegl's column-major flat order
 *     (typedef float mat4[16]; column c is m[4c..4c+3]), so "uMVP * vec4(p,1)"
 *     here is bit-identical to mult_m4_v4(mvp_mat, p) in the C vertex_shader.
 *   - The normal is transformed with w=0; because uNormalMat is
 *     transpose(inverse(model_view)) its fourth column is (0,0,0,1), so the
 *     transformed normal's xyz is independent of that w (matches the C path,
 *     which only reads v4.xyz before normalizing).
 *   - The light is a fixed world-space direction (5,5,10) normalized, and the
 *     diffuse term is material * max(dot(N,L),0) with no ambient/specular --
 *     exactly the C vertex_shader's lighting model.
 *
 * Plain C string literals so this header is includable from gears.c (C).
 */
#ifndef GEARS_GLSL_H
#define GEARS_GLSL_H

/* Per-vertex diffuse lighting; outputs the lit RGBA colour as a single smooth
 * vec4 varying and the clip-space position.
 *
 * The PGL_G3D_INSTANCED marker below is read by pgl_g3d's compute-attach logic
 * (strstr on the VS source) to select the INSTANCED geometry xform kernel
 * (geom_xform_inst_cs_glsl) instead of the plain per-draw one.  That kernel
 * ignores uMVP/uNormalMat/uMaterial entirely: it reads a per-vertex instance
 * index from pos.w and pulls each gear's MVP / normal matrices and material
 * colour out of a 4th SSBO the app refills every frame, so all six gears are
 * transformed and lit in ONE dispatch.  uMVP is still declared (and the source
 * still computes the identical lighting) because the attach gate keys on
 * "uMVP" and the CPU vertex_shader fallback path in gears.c mirrors this VS
 * exactly; the marker only steers which GPU kernel is bound. */
static const char* gears_vs_glsl =
    "#version 310 es\n"
    "// PGL_G3D_INSTANCED\n"
    "precision highp float;\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uNormalMat;\n"
    "uniform vec3 uMaterial;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vec3 N = normalize((uNormalMat * vec4(aNormal, 0.0)).xyz);\n"
    "    vec3 L = normalize(vec3(5.0, 5.0, 10.0));\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    vColor = vec4(uMaterial * diff, 1.0);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

/* Gouraud passthrough: the interpolated per-vertex colour is the fragment. */
static const char* gears_fs_glsl =
    "#version 310 es\n"
    "precision highp float;\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vColor;\n"
    "}\n";

#endif /* GEARS_GLSL_H */
