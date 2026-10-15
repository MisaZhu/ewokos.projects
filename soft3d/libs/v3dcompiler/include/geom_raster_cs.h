/* Fixed gouraud geometry rasterizer - V3D 7.1 CSD compute kernel (GLSL ES 3.10).
 *
 * This is a hand-written compute shader compiled verbatim through
 * v3d_compile_compute_raw().  It is the GPU-only replacement for the V3D 7.1
 * 3D binner (which cannot rasterise real primitives on BCM2712): teapot /
 * gears / 3ddemo geometry is transformed, lit and rasterised entirely in
 * compute and written straight into the frame-buffer SSBO.
 *
 * Dispatch model: ONE WORKGROUP PER TRIANGLE, local_size 8x8x1 = 64
 * invocations.  Every invocation redundantly transforms the triangle's three
 * vertices (no shared memory is used - the CSD dispatcher declines
 * cs_shared_size != 0), then the 64 invocations sweep the triangle's screen
 * bounding box as an 8x8 lane tile (gl_LocalInvocationID.xy, stride 8).  Depth is resolved across
 * workgroups with an atomicMin into the depth SSBO; the invocation that wins
 * the atomic writes the packed RGBA8 pixel.
 *
 * SSBO bindings (fixed; expand_uniforms maps binding index -> GPU address):
 *   0  fb      uint[]  - frame buffer, one dword per pixel, row-major (stride
 *                        uFbStride dwords).  Packed like the shadertoy compute
 *                        route: (a<<24)|(r<<16)|(g<<8)|b.
 *   1  vpos    float[] - vertex positions, tightly packed vec3 (stride 3
 *                        floats == 12 bytes, matching pgl_g3d's mesh staging).
 *   2  vnrm    float[] - vertex normals, same packing.
 *   3  depth   uint[]  - depth buffer, one unorm uint per pixel
 *                        (0 = nearest, 0xFFFFFFFF = far/cleared), atomicMin.
 *
 * float[] (not vec3[]) is deliberate: a std430 array of vec3 has a 16-byte
 * element stride, which would NOT match the 12-byte-tight staging the client
 * uploads.  Indexing vpos[v*3+c] reproduces the exact 12-byte stride.
 *
 * Screen mapping matches the CPU rasterizer / shadertoy store convention: the
 * fb row index counts DOWN from the top of the image (memory row 0 = top), so
 * screen_y = (0.5 - ndc.y*0.5) * H.  Vertex index v = tri*3 + {0,1,2}; the
 * client re-bases each drawn sub-range to vertex 0 in the staged buffers, so
 * uTriFirst is normally 0 and uTriCount is the triangle count of the draw.
 */
#ifndef GEOM_RASTER_CS_H
#define GEOM_RASTER_CS_H

static const char geom_raster_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer V3dCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer V3dCsPos   { float vpos[];  };\n"
"layout(std430, binding = 2) buffer V3dCsNrm   { float vnrm[];  };\n"
"layout(std430, binding = 3) buffer V3dCsDepth { uint  depth[]; };\n"
"\n"
"uniform mat4 uMVP;\n"
"uniform mat4 uModelView;\n"
"uniform mat4 uNormalMat;\n"
"uniform vec4 uMaterial;\n"
"uniform vec4 uLightEye;\n"
"uniform vec4 uShade;\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uFbStride;\n"
"uniform int  uTriFirst;\n"
"uniform int  uTriCount;\n"
"\n"
/* Per-vertex gouraud, parameterised to reproduce the two VS lighting models
 * the soft3d apps ship (pgl_g3d_shader_geom_blob selects them by what the
 * app's own VS declares):
 *   uLightEye.w > 0  (teapot): eye-space positional light, two-sided (N is
 *                    flipped away from the viewer), ambient + diffuse on the
 *                    material plus a white Blinn-Phong specular with exponent
 *                    48 evaluated by repeated squaring exactly like the VS;
 *   uLightEye.w == 0 (gears):  directional light along uLightEye.xyz, one
 *                    sided, no eye-space position needed (uModelView unused).
 * uShade = (ambient, diffuse scale, specular weight, -). */
"vec3 gouraud(vec3 p, vec3 n)\n"
"{\n"
"    vec3 ne = normalize((uNormalMat * vec4(n, 0.0)).xyz);\n"
"    vec3 L;\n"
"    float spec = 0.0;\n"
"    if (uLightEye.w > 0.0) {\n"
"        vec3 pe = (uModelView * vec4(p, 1.0)).xyz;\n"
"        vec3 V  = normalize(-pe);\n"
"        if (dot(ne, V) < 0.0) ne = -ne;\n"
"        L = normalize(uLightEye.xyz - pe);\n"
"        vec3 H  = normalize(L + V);\n"
"        float s = max(dot(ne, H), 0.0);\n"
"        float s2 = s * s;\n"
"        float s4 = s2 * s2;\n"
"        float s8 = s4 * s4;\n"
"        float s16 = s8 * s8;\n"
"        float s32 = s16 * s16;\n"
"        spec = s16 * s32;\n"
"    } else {\n"
"        L = normalize(uLightEye.xyz);\n"
"    }\n"
"    float diff = max(dot(ne, L), 0.0);\n"
"    return clamp(uMaterial.rgb * (uShade.x + uShade.y * diff) +\n"
"                 vec3(uShade.z * spec), 0.0, 1.0);\n"
"}\n"
"\n"
"float edgef(vec2 a, vec2 b, vec2 p)\n"
"{\n"
"    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    int tri = int(gl_WorkGroupID.x) + uTriFirst;\n"
"    if (tri >= uTriFirst + uTriCount)\n"
"        return;\n"
"\n"
"    int i0 = tri * 3;\n"
"    int i1 = i0 + 1;\n"
"    int i2 = i0 + 2;\n"
"\n"
"    vec3 p0 = vec3(vpos[i0*3+0], vpos[i0*3+1], vpos[i0*3+2]);\n"
"    vec3 p1 = vec3(vpos[i1*3+0], vpos[i1*3+1], vpos[i1*3+2]);\n"
"    vec3 p2 = vec3(vpos[i2*3+0], vpos[i2*3+1], vpos[i2*3+2]);\n"
"    vec3 n0 = vec3(vnrm[i0*3+0], vnrm[i0*3+1], vnrm[i0*3+2]);\n"
"    vec3 n1 = vec3(vnrm[i1*3+0], vnrm[i1*3+1], vnrm[i1*3+2]);\n"
"    vec3 n2 = vec3(vnrm[i2*3+0], vnrm[i2*3+1], vnrm[i2*3+2]);\n"
"\n"
"    vec4 c0 = uMVP * vec4(p0, 1.0);\n"
"    vec4 c1 = uMVP * vec4(p1, 1.0);\n"
"    vec4 c2 = uMVP * vec4(p2, 1.0);\n"
"    if (c0.w <= 0.0 || c1.w <= 0.0 || c2.w <= 0.0)\n"
"        return;   /* crosses w=0: near-plane clip guard (skip triangle) */\n"
"\n"
"    float iw0 = 1.0 / c0.w, iw1 = 1.0 / c1.w, iw2 = 1.0 / c2.w;\n"
"    vec2 W = uViewportWH;\n"
"    vec2 s0 = vec2((c0.x * iw0 * 0.5 + 0.5) * W.x, (0.5 - c0.y * iw0 * 0.5) * W.y);\n"
"    vec2 s1 = vec2((c1.x * iw1 * 0.5 + 0.5) * W.x, (0.5 - c1.y * iw1 * 0.5) * W.y);\n"
"    vec2 s2 = vec2((c2.x * iw2 * 0.5 + 0.5) * W.x, (0.5 - c2.y * iw2 * 0.5) * W.y);\n"
"    float z0 = c0.z * iw0 * 0.5 + 0.5;\n"
"    float z1 = c1.z * iw1 * 0.5 + 0.5;\n"
"    float z2 = c2.z * iw2 * 0.5 + 0.5;\n"
"\n"
"    vec3 col0 = gouraud(p0, n0);\n"
"    vec3 col1 = gouraud(p1, n1);\n"
"    vec3 col2 = gouraud(p2, n2);\n"
"\n"
"    int minx = int(floor(min(s0.x, min(s1.x, s2.x))));\n"
"    int maxx = int(ceil (max(s0.x, max(s1.x, s2.x))));\n"
"    int miny = int(floor(min(s0.y, min(s1.y, s2.y))));\n"
"    int maxy = int(ceil (max(s0.y, max(s1.y, s2.y))));\n"
"    if (minx < 0) minx = 0;\n"
"    if (miny < 0) miny = 0;\n"
"    if (maxx > int(W.x) - 1) maxx = int(W.x) - 1;\n"
"    if (maxy > int(W.y) - 1) maxy = int(W.y) - 1;\n"
"    if (minx > maxx || miny > maxy)\n"
"        return;\n"
"\n"
"    /* 8x8 lane tile swept over the bbox: every lane owns one pixel per\n"
"       iteration (all 64 lanes issue their atomic together instead of one\n"
"       lane per iteration), per-lane addresses still ascend, and the stride\n"
"       is a constant so no idiv/umul_high is emitted. */\n"
"    int lx = int(gl_LocalInvocationID.x);\n"
"    int ly = int(gl_LocalInvocationID.y);\n"
"    for (int py = miny + ly; py <= maxy; py += 8) {\n"
"    for (int px = minx + lx; px <= maxx; px += 8) {\n"
"        vec2 P = vec2(float(px) + 0.5, float(py) + 0.5);\n"
"\n"
"        float w0 = edgef(s1, s2, P);\n"
"        float w1 = edgef(s2, s0, P);\n"
"        float w2 = edgef(s0, s1, P);\n"
"        float area = w0 + w1 + w2;\n"
"        if (area == 0.0)\n"
"            continue;\n"
"        if (area < 0.0) {\n"
"            if (w0 > 0.0 || w1 > 0.0 || w2 > 0.0) continue;\n"
"        } else {\n"
"            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;\n"
"        }\n"
"        vec3 bary = vec3(w0, w1, w2) / area;\n"
"\n"
"        float iw = bary.x * iw0 + bary.y * iw1 + bary.z * iw2;\n"
"        if (iw <= 0.0) continue;\n"
"        float rw = 1.0 / iw;\n"
"        vec3 col = (bary.x * col0 * iw0 + bary.y * col1 * iw1 +\n"
"                    bary.z * col2 * iw2) * rw;\n"
"        float z  = (bary.x * z0 * iw0 + bary.y * z1 * iw1 +\n"
"                    bary.z * z2 * iw2) * rw;\n"
"\n"
"        uint d = uint(clamp(z, 0.0, 1.0) * 16777215.0);   /* 24-bit unorm depth */\n"
"        int idx = py * uFbStride + px;\n"
"        uint old = atomicMin(depth[idx], d);\n"
"        if (d < old) {\n"
"            vec3 cc = clamp(col, 0.0, 1.0) * 255.0 + 0.5;\n"
"            uvec3 u = uvec3(cc);\n"
"            fb[idx] = (255u << 24) | (u.r << 16) | (u.g << 8) | u.b;\n"
"        }\n"
"    }\n"
"    }\n"
"}\n";

/* TEXTURED variant of the fixed geometry rasterizer (3ddemo).  Same dispatch
 * model and rasterization as geom_raster_cs_glsl above; the differences are:
 *   - V3D 7.1 compute exposes only FOUR SSBO binding points (0-3), so vertex
 *     positions and texture coordinates SHARE binding 1 as one interleaved
 *     float stream with a 6-float stride: [i*6+0..2] = pos.xyz,
 *     [i*6+3..4] = (u, v), [i*6+5] = pad (the client stages it that way);
 *   - SSBO binding 3 is the texture image: a LINEAR (raster-layout) uint32
 *     array whose words already use the fb's 0xAARRGGBB packing, so a fetched
 *     texel is written to the fb verbatim (pgl_g3d_upload_texture byte-copies
 *     the caller's uint32 rows; the app supplies its native 0xAARRGGBB data);
 *   - the interpolated (u,v) is perspective-correct and V-flipped (1.0 - v),
 *     replicating the app VS's flip (the mesh uv convention assumes the GL
 *     bottom-left origin while the texture's row 0 is the top of the image);
 *   - sampling is NEAREST with CLAMP-to-edge, matching the demo's GL state.
 * uTexW/uTexH are patched into the per-draw blob by pgl_g3d from the bound
 * texture slot; uTriFirst/uTriCount/uFbStride/uViewportWH as above. */
static const char geom_raster_tex_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer V3dCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer V3dCsAttr  { float vattr[]; };\n"
"layout(std430, binding = 2) buffer V3dCsDepth { uint  depth[]; };\n"
"layout(std430, binding = 3) buffer V3dCsTex   { uint  tex[];   };\n"
"\n"
"uniform mat4 uMVP;\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uFbStride;\n"
"uniform int  uTriFirst;\n"
"uniform int  uTriCount;\n"
"uniform int  uTexW;\n"
"uniform int  uTexH;\n"
"\n"
"float edgef(vec2 a, vec2 b, vec2 p)\n"
"{\n"
"    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    int tri = int(gl_WorkGroupID.x) + uTriFirst;\n"
"    if (tri >= uTriFirst + uTriCount)\n"
"        return;\n"
"\n"
"    int i0 = tri * 3;\n"
"    int i1 = i0 + 1;\n"
"    int i2 = i0 + 2;\n"
"\n"
"    /* interleaved stride 6: pos.xyz, uv.xy, pad */\n"
"    vec3 p0 = vec3(vattr[i0*6+0], vattr[i0*6+1], vattr[i0*6+2]);\n"
"    vec3 p1 = vec3(vattr[i1*6+0], vattr[i1*6+1], vattr[i1*6+2]);\n"
"    vec3 p2 = vec3(vattr[i2*6+0], vattr[i2*6+1], vattr[i2*6+2]);\n"
"    vec2 t0 = vec2(vattr[i0*6+3], vattr[i0*6+4]);\n"
"    vec2 t1 = vec2(vattr[i1*6+3], vattr[i1*6+4]);\n"
"    vec2 t2 = vec2(vattr[i2*6+3], vattr[i2*6+4]);\n"
"\n"
"    vec4 c0 = uMVP * vec4(p0, 1.0);\n"
"    vec4 c1 = uMVP * vec4(p1, 1.0);\n"
"    vec4 c2 = uMVP * vec4(p2, 1.0);\n"
"    if (c0.w <= 0.0 || c1.w <= 0.0 || c2.w <= 0.0)\n"
"        return;   /* crosses w=0: near-plane clip guard (skip triangle) */\n"
"\n"
"    float iw0 = 1.0 / c0.w, iw1 = 1.0 / c1.w, iw2 = 1.0 / c2.w;\n"
"    vec2 W = uViewportWH;\n"
"    vec2 s0 = vec2((c0.x * iw0 * 0.5 + 0.5) * W.x, (0.5 - c0.y * iw0 * 0.5) * W.y);\n"
"    vec2 s1 = vec2((c1.x * iw1 * 0.5 + 0.5) * W.x, (0.5 - c1.y * iw1 * 0.5) * W.y);\n"
"    vec2 s2 = vec2((c2.x * iw2 * 0.5 + 0.5) * W.x, (0.5 - c2.y * iw2 * 0.5) * W.y);\n"
"    float z0 = c0.z * iw0 * 0.5 + 0.5;\n"
"    float z1 = c1.z * iw1 * 0.5 + 0.5;\n"
"    float z2 = c2.z * iw2 * 0.5 + 0.5;\n"
"\n"
"    int minx = int(floor(min(s0.x, min(s1.x, s2.x))));\n"
"    int maxx = int(ceil (max(s0.x, max(s1.x, s2.x))));\n"
"    int miny = int(floor(min(s0.y, min(s1.y, s2.y))));\n"
"    int maxy = int(ceil (max(s0.y, max(s1.y, s2.y))));\n"
"    if (minx < 0) minx = 0;\n"
"    if (miny < 0) miny = 0;\n"
"    if (maxx > int(W.x) - 1) maxx = int(W.x) - 1;\n"
"    if (maxy > int(W.y) - 1) maxy = int(W.y) - 1;\n"
"    if (minx > maxx || miny > maxy)\n"
"        return;\n"
"\n"
"    /* 8x8 lane tile swept over the bbox (same as the gouraud variant). */\n"
"    int lx = int(gl_LocalInvocationID.x);\n"
"    int ly = int(gl_LocalInvocationID.y);\n"
"    for (int py = miny + ly; py <= maxy; py += 8) {\n"
"    for (int px = minx + lx; px <= maxx; px += 8) {\n"
"        vec2 P = vec2(float(px) + 0.5, float(py) + 0.5);\n"
"\n"
"        float w0 = edgef(s1, s2, P);\n"
"        float w1 = edgef(s2, s0, P);\n"
"        float w2 = edgef(s0, s1, P);\n"
"        float area = w0 + w1 + w2;\n"
"        if (area == 0.0)\n"
"            continue;\n"
"        if (area < 0.0) {\n"
"            if (w0 > 0.0 || w1 > 0.0 || w2 > 0.0) continue;\n"
"        } else {\n"
"            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;\n"
"        }\n"
"        vec3 bary = vec3(w0, w1, w2) / area;\n"
"\n"
"        float iw = bary.x * iw0 + bary.y * iw1 + bary.z * iw2;\n"
"        if (iw <= 0.0) continue;\n"
"        float rw = 1.0 / iw;\n"
"        vec2 uv = (bary.x * t0 * iw0 + bary.y * t1 * iw1 +\n"
"                   bary.z * t2 * iw2) * rw;\n"
"        float z  = (bary.x * z0 * iw0 + bary.y * z1 * iw1 +\n"
"                    bary.z * z2 * iw2) * rw;\n"
"\n"
"        uint d = uint(clamp(z, 0.0, 1.0) * 16777215.0);   /* 24-bit unorm depth */\n"
"        int idx = py * uFbStride + px;\n"
"        uint old = atomicMin(depth[idx], d);\n"
"        if (d < old) {\n"
"            /* NEAREST + CLAMP-to-edge, V-flipped like the app VS.  The texel\n"
"               word is already 0xAARRGGBB (the fb packing), written verbatim. */\n"
"            int tx = clamp(int(uv.x * float(uTexW)), 0, uTexW - 1);\n"
"            int ty = clamp(int((1.0 - uv.y) * float(uTexH)), 0, uTexH - 1);\n"
"            fb[idx] = tex[ty * uTexW + tx];\n"
"        }\n"
"    }\n"
"    }\n"
"}\n";

#endif /* GEOM_RASTER_CS_H */
