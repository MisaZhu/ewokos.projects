/*
 * teapot_glsl.h -- GLSL ES 3.10 port of the Utah teapot's per-vertex
 * Blinn-Phong vertex shader paired with a gouraud-passthrough fragment shader.
 *
 * These sources mirror the C vertex_shader() + built-in pgl_ff_gouraud() pair in
 * teapot.c EXACTLY, so the teapot looks identical whether it is lit on the CPU
 * rasterizer (the automatic fallback) or with the vertex stage compiled to
 * VideoCore VII QPU (raspi5 / V3D 7.1) by v3d_compile_glsl().
 *
 * Convention:
 *   - #version 310 es with explicit layout(location=) so aPosition binds vertex
 *     attribute 0 and aNormal binds attribute 1, matching the driver's two
 *     interleaved attribute records (position float3 at base, normal float3 at
 *     base+12).  Declaration order alone is not relied upon.
 *   - mat4 uniforms are uploaded in portablegl's column-major flat order
 *     (typedef float mat4[16]; column c is m[4c..4c+3]), so "uMVP * vec4(p,1)"
 *     here is bit-identical to mult_m4_v4(mvp_mat, p) in the C vertex_shader.
 *   - The normal is transformed with w=0; because normal_mat is
 *     transpose(inverse(model_view)) its fourth column is (0,0,0,1), so the
 *     transformed normal's xyz is independent of that w (matches the C path,
 *     which only reads Ne.xyz before normalizing).
 *   - The specular exponent (48) is evaluated by repeated squaring to match the
 *     C fpow() exactly (GLSL pow() may differ in the last ulp).
 *   - Two-sided lighting: flip N when it faces away from the viewer (the teapot
 *     renders with culling disabled).
 *
 * Plain C string literals so this header is includable from teapot.c (C).
 */
#ifndef TEAPOT_GLSL_H
#define TEAPOT_GLSL_H

/* Per-vertex Blinn-Phong in eye space; outputs the lit RGBA colour as a single
 * smooth vec4 varying and the clip-space position. */
static const char* teapot_vs_glsl =
    "#version 310 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModelView;\n"
    "uniform mat4 uNormalMat;\n"
    "uniform vec3 uMaterial;\n"
    "uniform vec3 uLightEye;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vec3 P = (uModelView * vec4(aPosition, 1.0)).xyz;\n"
    "    vec3 N = normalize((uNormalMat * vec4(aNormal, 0.0)).xyz);\n"
    "    vec3 V = normalize(-P);\n"
    "    if (dot(N, V) < 0.0) N = -N;\n"
    "    vec3 L = normalize(uLightEye - P);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float spec = max(dot(N, H), 0.0);\n"
    "    float s2 = spec * spec;\n"
    "    float s4 = s2 * s2;\n"
    "    float s8 = s4 * s4;\n"
    "    float s16 = s8 * s8;\n"
    "    float s32 = s16 * s16;\n"
    "    spec = s16 * s32;\n"
    "    float shade = 0.22 + 0.78 * diff;\n"
    "    vec3 col = clamp(uMaterial * shade + vec3(spec), 0.0, 1.0);\n"
    "    vColor = vec4(col, 1.0);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

/* Gouraud passthrough: the interpolated per-vertex colour is the fragment. */
static const char* teapot_fs_glsl =
    "#version 310 es\n"
    "precision highp float;\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vColor;\n"
    "}\n";

#endif /* TEAPOT_GLSL_H */
