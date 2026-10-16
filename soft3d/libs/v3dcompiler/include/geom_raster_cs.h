/* Fixed gouraud geometry rasterizer - V3D 7.1 CSD compute kernels (GLSL ES 3.10).
 *
 * These are hand-written compute shaders compiled verbatim through
 * v3d_compile_compute_raw().  They are the GPU-only replacement for the V3D 7.1
 * 3D binner (which cannot rasterise real primitives on BCM2712): teapot /
 * gears / 3ddemo geometry is transformed, lit and rasterised entirely in
 * compute and written straight into the frame-buffer SSBO.
 *
 * TWO-PASS DISPATCH MODEL (vertex shading hoisted out of the rasterizer):
 *
 *   Pass 1 - VERTEX TRANSFORM kernel (geom_xform_cs_glsl / geom_xform_tex_cs_glsl):
 *     local_size 8x8x1 = 64 invocations, grid = ceil(nverts/64) workgroups, so
 *     EVERY vertex is transformed and lit EXACTLY ONCE (one invocation per
 *     vertex).  It reads model-space pos/nrm (or interleaved pos+uv for the tex
 *     variant), applies uMVP (+ uModelView/uNormalMat/gouraud for the gouraud
 *     variant), and writes a tightly packed per-vertex record into the `vout`
 *     SSBO: 8 floats == 32 bytes per vertex.
 *
 *   Pass 2 - RASTER kernel:
 *     gouraud (geom_raster_cs_glsl): ONE LANE PER TRIANGLE.  A 16x16 workgroup
 *       (256 lanes) covers 256 triangles - lane L of workgroup W owns triangle
 *       W*256+L, reads its three pre-transformed `vout` records once and sweeps
 *       that triangle's screen bounding box serially.  The daemon dispatches
 *       ceil(ntris/256) workgroups (program blob cs_geom_tpw = 256).
 *     textured (geom_raster_tex_cs_glsl): the legacy ONE WORKGROUP PER TRIANGLE
 *       shape, local_size 8x8x1 = 64 invocations sweeping the bbox as an 8x8
 *       lane tile (cs_geom_tpw = 0).
 *     Both resolve depth across lanes/workgroups with an atomicMin into the
 *     depth SSBO; the lane that wins the atomic writes the packed RGBA8 pixel.
 *
 *   Splitting the transform out cuts the per-frame vertex ALU from
 *   ntris*64*3 redundant transforms to nverts unique ones.  The one-lane-per-
 *   triangle gouraud grid is the other half of what lets the GPU path beat the
 *   CPU rasterizer: the V3D 7.1 CSD charges a ~3us FIXED overhead PER
 *   WORKGROUP, so the old 6400-workgroup (one per triangle) raster pass cost
 *   ~19ms regardless of kernel contents, while 25 workgroups cost well under
 *   1ms (teapot: ~40 FPS -> ~90 FPS).  The two passes are separate CSD
 *   dispatches; the daemon cleans `vout` after pass 1 and pass 2 reads it back
 *   through the L2 with no invalidate of its own (a textured pass 2 still gets
 *   a ranged invalidate over the ARM-written texture image only - see
 *   draw_compute_geom).
 *
 * `vout` record layout (8 floats / 32 bytes per vertex, float[] not vec4[] so
 * ONE SSBO binding covers it and the tex raster still fits fb/vout/depth/tex in
 * the four V3D 7.1 compute binding points):
 *   gouraud: [0]=screen_x [1]=screen_y [2]=z(0..1) [3]=iw(1/w, 0 if clipped)
 *            [4]=r [5]=g [6]=b [7]=pad
 *   tex:     [0]=screen_x [1]=screen_y [2]=z(0..1) [3]=iw(1/w, 0 if clipped)
 *            [4]=u [5]=v [6]=pad [7]=pad
 *   iw == 0 is the near-plane clip sentinel: a vertex with c.w <= 0 stores
 *   iw = 0, and the raster kernel skips any triangle with a non-positive iw.
 *
 * SSBO bindings (fixed; expand_uniforms maps binding index -> GPU address):
 *   xform gouraud:  0 vout(float[],stride8)  1 vpos(float[],stride3)  2 vnrm(float[],stride3)
 *   xform tex:      0 vout(float[],stride8)  1 vattr(float[],stride6: pos.xyz,uv.xy,pad)
 *   raster gouraud: 0 fb(uint[])  1 vout(float[],stride8)  2 depth(uint[])
 *   raster tex:     0 fb(uint[])  1 vout(float[],stride8)  2 depth(uint[])  3 tex(uint[])
 *
 * float[] (not vec3[]) for the vertex streams is deliberate: a std430 array of
 * vec3/vec4 has a 16-byte element stride, which would NOT match the 12-byte-tight
 * staging the client uploads (nor the 32-byte vout record).  Indexing
 * vpos[v*3+c] / vout[v*8+c] reproduces the exact byte stride.
 *
 * Screen mapping matches the CPU rasterizer / shadertoy store convention: the
 * fb row index counts DOWN from the top of the image (memory row 0 = top), so
 * screen_y = (0.5 - ndc.y*0.5) * H.  Vertex index v = tri*3 + {0,1,2}; the
 * client re-bases each drawn sub-range to vertex 0 in the staged buffers, so
 * uTriFirst is normally 0 and uTriCount is the triangle count of the draw.
 */
#ifndef GEOM_RASTER_CS_H
#define GEOM_RASTER_CS_H

/* ---- Pass 1: gouraud vertex transform (one invocation per vertex) --------
 * Reads model-space pos (binding 1) + nrm (binding 2), writes the packed
 * screen/z/iw/colour record to vout (binding 0).  The gouraud() lighting model
 * is parameterised exactly as the old single-pass kernel did, selected at
 * RUNTIME by uLightEye.w so one compiled kernel serves both shipped models:
 *   uLightEye.w > 0  (teapot): eye-space positional light, two-sided (N is
 *                    flipped away from the viewer), ambient + diffuse on the
 *                    material plus a white Blinn-Phong specular with exponent
 *                    48 evaluated by repeated squaring exactly like the VS;
 *   uLightEye.w == 0 (gears):  directional light along uLightEye.xyz, one
 *                    sided, no eye-space position needed (uModelView unused).
 * uShade = (ambient, diffuse scale, specular weight, -). */
static const char geom_xform_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer V3dCsVout { float vout[]; }\n;"
"layout(std430, binding = 1) buffer V3dCsPos  { float vpos[]; }\n;"
"layout(std430, binding = 2) buffer V3dCsNrm  { float vnrm[]; }\n;"
"\n"
"uniform mat4 uMVP;\n"
"uniform mat4 uModelView;\n"
"uniform mat4 uNormalMat;\n"
"uniform vec4 uMaterial;\n"
"uniform vec4 uLightEye;\n"
"uniform vec4 uShade;\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uVertCount;\n"
"\n"
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
"void main()\n"
"{\n"
"    int v = int(gl_WorkGroupID.x) * 64 +\n"
"            int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x);\n"
"    if (v >= uVertCount)\n"
"        return;\n"
"\n"
"    vec3 p = vec3(vpos[v*3+0], vpos[v*3+1], vpos[v*3+2]);\n"
"    vec3 n = vec3(vnrm[v*3+0], vnrm[v*3+1], vnrm[v*3+2]);\n"
"\n"
"    vec4 c = uMVP * vec4(p, 1.0);\n"
"    vec2 W = uViewportWH;\n"
"    float iw = (c.w > 0.0) ? (1.0 / c.w) : 0.0;   /* clip sentinel */\n"
"    vec2 s = vec2((c.x * iw * 0.5 + 0.5) * W.x, (0.5 - c.y * iw * 0.5) * W.y);\n"
"    float z = c.z * iw * 0.5 + 0.5;\n"
"    vec3 col = (c.w > 0.0) ? gouraud(p, n) : vec3(0.0);\n"
"\n"
"    vout[v*8+0] = s.x;\n"
"    vout[v*8+1] = s.y;\n"
"    vout[v*8+2] = z;\n"
"    vout[v*8+3] = iw;\n"
"    vout[v*8+4] = col.r;\n"
"    vout[v*8+5] = col.g;\n"
"    vout[v*8+6] = col.b;\n"
"    vout[v*8+7] = 0.0;\n"
"}\n";

/* ---- Pass 2: gouraud rasterizer (ONE LANE PER TRIANGLE) ------------------
 * A 16x16 workgroup (256 lanes) covers 256 triangles: lane L of workgroup W
 * owns triangle W*256+L outright, reads its three pre-transformed vout records
 * once, and sweeps that triangle's screen bounding box serially.  Depth is
 * resolved across lanes/workgroups with an atomicMin into the depth SSBO; the
 * lane that wins the atomic writes the packed RGBA8 pixel.  Bindings: 0 fb,
 * 1 vout, 2 depth.  The daemon dispatches ceil(ntris/256) workgroups (program
 * blob cs_geom_tpw = 256); the uTriCount guard exits any over-dispatched lane
 * before it touches an SSBO, so a daemon unaware of cs_geom_tpw still renders
 * correctly (just slowly).
 *
 * One lane per triangle - instead of the earlier one-workgroup-per-triangle
 * shape whose 64 lanes all re-read the same three records - is the whole
 * performance point: device measurement on the raspi5 V3D 7.1 CSD showed the
 * raster pass is dominated by a ~3us FIXED per-workgroup dispatch overhead, so
 * collapsing 6400 workgroups to 25 took the teapot raster pass from ~19ms to
 * well under 1ms.  Per-pixel interpolation math is unchanged. */
static const char geom_raster_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer V3dCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer V3dCsVout  { float vout[];  };\n"
"layout(std430, binding = 2) buffer V3dCsDepth { uint  depth[]; };\n"
"\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uFbStride;\n"
"uniform int  uTriFirst;\n"
"uniform int  uTriCount;\n"
"\n"
"float edgef(vec2 a, vec2 b, vec2 p)\n"
"{\n"
"    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    int lane = int(gl_LocalInvocationID.y) * 16 + int(gl_LocalInvocationID.x);\n"
"    int tri = int(gl_WorkGroupID.x) * 256 + lane + uTriFirst;\n"
"    if (tri >= uTriFirst + uTriCount)\n"
"        return;\n"
"\n"
"    int i0 = tri * 3;\n"
"    int i1 = i0 + 1;\n"
"    int i2 = i0 + 2;\n"
"\n"
"    float iw0 = vout[i0*8+3], iw1 = vout[i1*8+3], iw2 = vout[i2*8+3];\n"
"    if (iw0 <= 0.0 || iw1 <= 0.0 || iw2 <= 0.0)\n"
"        return;\n"
"\n"
"    vec2 s0 = vec2(vout[i0*8+0], vout[i0*8+1]);\n"
"    vec2 s1 = vec2(vout[i1*8+0], vout[i1*8+1]);\n"
"    vec2 s2 = vec2(vout[i2*8+0], vout[i2*8+1]);\n"
"    float z0 = vout[i0*8+2], z1 = vout[i1*8+2], z2 = vout[i2*8+2];\n"
"    vec3 col0 = vec3(vout[i0*8+4], vout[i0*8+5], vout[i0*8+6]);\n"
"    vec3 col1 = vec3(vout[i1*8+4], vout[i1*8+5], vout[i1*8+6]);\n"
"    vec3 col2 = vec3(vout[i2*8+4], vout[i2*8+5], vout[i2*8+6]);\n"
"\n"
"    vec2 W = uViewportWH;\n"
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
"    for (int py = miny; py <= maxy; py++) {\n"
"    for (int px = minx; px <= maxx; px++) {\n"
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
"        float inv_area = 1.0 / area;\n"
"        vec3 bary = vec3(w0, w1, w2) * inv_area;\n"
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

/* ---- Pass 1: TEXTURED vertex transform (one invocation per vertex) -------
 * 3ddemo variant.  pos and uv SHARE binding 1 as one interleaved float stream
 * with a 6-float stride: [i*6+0..2] = pos.xyz, [i*6+3..4] = (u, v), [i*6+5] =
 * pad (the client stages it that way).  Applies uMVP, passes the interpolated
 * (u,v) through to vout unchanged (the raster kernel does the V-flip + fetch).
 * vout record: [0]=sx [1]=sy [2]=z [3]=iw [4]=u [5]=v [6]=pad [7]=pad. */
static const char geom_xform_tex_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer V3dCsVout { float vout[];  };\n"
"layout(std430, binding = 1) buffer V3dCsAttr { float vattr[]; };\n"
"\n"
"uniform mat4 uMVP;\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uVertCount;\n"
"\n"
"void main()\n"
"{\n"
"    int v = int(gl_WorkGroupID.x) * 64 +\n"
"            int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x);\n"
"    if (v >= uVertCount)\n"
"        return;\n"
"\n"
"    vec3 p = vec3(vattr[v*6+0], vattr[v*6+1], vattr[v*6+2]);\n"
"    vec2 t = vec2(vattr[v*6+3], vattr[v*6+4]);\n"
"\n"
"    vec4 c = uMVP * vec4(p, 1.0);\n"
"    vec2 W = uViewportWH;\n"
"    float iw = (c.w > 0.0) ? (1.0 / c.w) : 0.0;   /* clip sentinel */\n"
"    vec2 s = vec2((c.x * iw * 0.5 + 0.5) * W.x, (0.5 - c.y * iw * 0.5) * W.y);\n"
"    float z = c.z * iw * 0.5 + 0.5;\n"
"\n"
"    vout[v*8+0] = s.x;\n"
"    vout[v*8+1] = s.y;\n"
"    vout[v*8+2] = z;\n"
"    vout[v*8+3] = iw;\n"
"    vout[v*8+4] = t.x;\n"
"    vout[v*8+5] = t.y;\n"
"    vout[v*8+6] = 0.0;\n"
"    vout[v*8+7] = 0.0;\n"
"}\n";

/* ---- Pass 2: TEXTURED rasterizer (one workgroup per triangle) ------------
 * Same dispatch model and rasterization as the legacy per-triangle gouraud
 * shape (8x8 workgroup, cs_geom_tpw = 0); the differences are:
 *   - vout carries (u,v) instead of a colour; the interpolated (u,v) is
 *     perspective-correct and V-flipped (1.0 - v), replicating the app VS's
 *     flip (the mesh uv convention assumes the GL bottom-left origin while the
 *     texture's row 0 is the top of the image);
 *   - binding 3 is the texture image: a LINEAR (raster-layout) uint32 array
 *     whose words already use the fb's 0xAARRGGBB packing, so a fetched texel
 *     is written to the fb verbatim (pgl_g3d_upload_texture byte-copies the
 *     caller's uint32 rows; the app supplies its native 0xAARRGGBB data);
 *   - sampling is NEAREST with CLAMP-to-edge, matching the demo's GL state.
 * Bindings: 0 fb, 1 vout, 2 depth, 3 tex.  uTexW/uTexH are patched into the
 * per-draw blob by pgl_g3d from the bound texture slot. */
static const char geom_raster_tex_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer V3dCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer V3dCsVout  { float vout[];  };\n"
"layout(std430, binding = 2) buffer V3dCsDepth { uint  depth[]; };\n"
"layout(std430, binding = 3) buffer V3dCsTex   { uint  tex[];   };\n"
"\n"
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
"    float iw0 = vout[i0*8+3], iw1 = vout[i1*8+3], iw2 = vout[i2*8+3];\n"
"    if (iw0 <= 0.0 || iw1 <= 0.0 || iw2 <= 0.0)\n"
"        return;   /* a vertex crossed w=0: near-plane clip guard */\n"
"\n"
"    vec2 s0 = vec2(vout[i0*8+0], vout[i0*8+1]);\n"
"    vec2 s1 = vec2(vout[i1*8+0], vout[i1*8+1]);\n"
"    vec2 s2 = vec2(vout[i2*8+0], vout[i2*8+1]);\n"
"    float z0 = vout[i0*8+2], z1 = vout[i1*8+2], z2 = vout[i2*8+2];\n"
"    vec2 t0 = vec2(vout[i0*8+4], vout[i0*8+5]);\n"
"    vec2 t1 = vec2(vout[i1*8+4], vout[i1*8+5]);\n"
"    vec2 t2 = vec2(vout[i2*8+4], vout[i2*8+5]);\n"
"\n"
"    vec2 W = uViewportWH;\n"
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
"    /* 8x8 lane tile swept over the bbox. */\n"
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
"        float inv_area = 1.0 / area;\n"
"        vec3 bary = vec3(w0, w1, w2) * inv_area;\n"
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
