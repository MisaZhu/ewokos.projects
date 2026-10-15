/*
 * 3ddemo_glsl.h -- GLSL ES 3.10 port of the 3ddemo textured-mesh shaders:
 * a pass-through-uv vertex shader paired with a texture-sampling fragment
 * shader.
 *
 * These sources mirror the C basic_transform_vp() + built-in
 * pgl_ff_tex_modulate() pair in 3ddemo.cpp EXACTLY, so the model looks identical
 * whether it is textured on the CPU rasterizer (the automatic fallback) or with
 * the shaders compiled to VideoCore VII QPU (raspi5 / V3D 7.1) by
 * v3d_compile_glsl().
 *
 * Convention (mesh path, as in teapot_glsl.h / gears_glsl.h):
 *   - #version 310 es with explicit layout(location=) so aPosition binds vertex
 *     attribute 0 and aTexCoord binds attribute 1, matching the driver's two
 *     interleaved attribute records.  pgl_g3d_upload_mesh stages position float3
 *     at element 0 and the second attribute float3 at element 3, so the (u,v)
 *     pair is uploaded as (u, v, 0) and read back as aTexCoord.xy.
 *   - The mat4 uniform is uploaded in portablegl's column-major flat order
 *     (typedef float mat4[16]), so "uMVP * vec4(p,1)" here is bit-identical to
 *     mult_m4_v4(mvp_mat, p) in the C basic_transform_vp.
 *   - The V coordinate is flipped (1.0 - v) exactly as the C vertex shader does
 *     (vs_output[1] = 1.0f - vertex_attribs[1].y).
 *   - The fragment shader samples the single bound texture unit (iChannel0)
 *     with point filtering / CLAMP-to-edge, matching pgl_ff_tex_modulate and the
 *     GL_NEAREST + GL_CLAMP_TO_EDGE texture params the demo sets.  The sampler
 *     is bound to unit 0 via pgl_g3d_bind_tex_unit(0, slot).  On the GPU the
 *     draw runs on the textured geom rasterizer kernel (geom_raster_tex_cs_glsl):
 *     the texture is a LINEAR image read as a raw uint32 array (no TMU), and the
 *     kernel itself replicates this VS's (u, 1-v) flip while sampling.
 *
 * Plain C string literals so this header is includable from 3ddemo.cpp (C++).
 */
#ifndef THREEDEMO_GLSL_H
#define THREEDEMO_GLSL_H

/* Pass the (u,v) texture coordinate through as a smooth varying (V flipped) and
 * transform the position to clip space. */
static const char* threeddemo_vs_glsl =
    "#version 310 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aTexCoord;\n"
    "uniform mat4 uMVP;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

/* Texture-modulate: the fragment is the texture sampled at the interpolated
 * (u, 1-v) coordinate. */
static const char* threeddemo_fs_glsl =
    "#version 310 es\n"
    "precision highp float;\n"
    "uniform sampler2D iChannel0;\n"
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(iChannel0, vTexCoord);\n"
    "}\n";

#endif /* THREEDEMO_GLSL_H */
