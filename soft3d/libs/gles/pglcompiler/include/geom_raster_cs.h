/* Fixed gouraud geometry rasterizer - V3D 7.1 CSD compute kernels (GLSL ES 3.10).
 *
 * These are hand-written compute shaders compiled verbatim through
 * pglc_compile_compute_raw().  They are the GPU-only replacement for the V3D 7.1
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
"layout(std430, binding = 0) buffer PglCsVout { float vout[]; }\n;"
"layout(std430, binding = 1) buffer PglCsPos  { float vpos[]; }\n;"
"layout(std430, binding = 2) buffer PglCsNrm  { float vnrm[]; }\n;"
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
"    vout[v*8+7] = uMaterial.a;\n"
"}\n";

/* ---- Pass 1 (colour variant): precomputed-colour vertex transform ---------
 * Byte-identical screen/z/iw transform to geom_xform_cs_glsl, but attribute 1
 * is a PRECOMPUTED per-vertex colour (rgb) rather than a normal: the app lights
 * the mesh on the CPU (teapot's native gouraud fast-path) and this kernel just
 * copies that colour into the vout record for the SHARED gouraud raster pass.
 * Selected when the app VS carries uMVP but declares no uNormalMat/uLightEye
 * (no GPU-side lighting).  Bindings match the lit kernel (0 vout, 1 vpos,
 * 2 = attr1.xyz) so the daemon's cs_is_geom=1 dispatch, the stride-6 client
 * upload and pgl_g3d_upload_mesh's pos/attr1 de-interleave are all reused
 * verbatim - only the compiled xform QPU differs. */
static const char geom_xform_color_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer PglCsVout { float vout[]; };\n"
"layout(std430, binding = 1) buffer PglCsPos  { float vpos[]; };\n"
"layout(std430, binding = 2) buffer PglCsCol  { float vcol[]; };\n"
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
"    vec3 p = vec3(vpos[v*3+0], vpos[v*3+1], vpos[v*3+2]);\n"
"\n"
"    vec4 c = uMVP * vec4(p, 1.0);\n"
"    vec2 W = uViewportWH;\n"
"    float iw = (c.w > 0.0) ? (1.0 / c.w) : 0.0;   /* clip sentinel */\n"
"    vec2 s = vec2((c.x * iw * 0.5 + 0.5) * W.x, (0.5 - c.y * iw * 0.5) * W.y);\n"
"    float z = c.z * iw * 0.5 + 0.5;\n"
"    vec3 col = (c.w > 0.0) ? vec3(vcol[v*3+0], vcol[v*3+1], vcol[v*3+2])\n"
"                           : vec3(0.0);\n"
"\n"
"    vout[v*8+0] = s.x;\n"
"    vout[v*8+1] = s.y;\n"
"    vout[v*8+2] = z;\n"
"    vout[v*8+3] = iw;\n"
"    vout[v*8+4] = col.r;\n"
"    vout[v*8+5] = col.g;\n"
"    vout[v*8+6] = col.b;\n"
"    vout[v*8+7] = 1.0;\n"
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
 * well under 1ms.  Per-pixel interpolation math is unchanged.
 *
 * Per-draw render state (patched by name from pgl_g3d_draw_mesh_compiled):
 *   uBlendMode  0 = opaque overwrite (alpha forced to 255),
 *               1 = SRC_ALPHA/ONE_MINUS_SRC_ALPHA, 2 = SRC_ALPHA/ONE;
 *               blended modes read-modify-write the fb word with the
 *               per-vertex alpha carried in vout[i*8+7] (the xform kernels
 *               write the material/instance alpha there).  Blended draws must
 *               NOT be coalesced across overlapping geometry: lanes run in
 *               parallel, so painter's order only holds BETWEEN dispatches.
 *   uDepthWrite 1 = atomicMin test-and-write (the historical behaviour),
 *               0 = test-only against the existing depth (glDepthMask(FALSE)).
 *               The depth function is fixed LESS, matching atomicMin.
 *   uCullSign   0 = accept both windings, 1 = keep area < 0, 2 = keep area > 0.
 *               The client maps GL cull mode + front face onto this using
 *               portablegl's is_front_facing convention: its window-space
 *               shoelace `a` equals -area here, so CCW-front BACK-cull (discard
 *               a <= 0) becomes "keep area < 0". */
static const char geom_raster_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer PglCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer PglCsVout  { float vout[];  };\n"
"layout(std430, binding = 2) buffer PglCsDepth { uint  depth[]; };\n"
"\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uFbStride;\n"
"uniform int  uTriFirst;\n"
"uniform int  uTriCount;\n"
"uniform int  uBlendMode;\n"
"uniform int  uDepthWrite;\n"
"uniform int  uCullSign;\n"
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
"    if (uCullSign == 3)\n"
"        return;   /* GL_FRONT_AND_BACK: every triangle is culled */\n"
"\n"
"    vec2 s0 = vec2(vout[i0*8+0], vout[i0*8+1]);\n"
"    vec2 s1 = vec2(vout[i1*8+0], vout[i1*8+1]);\n"
"    vec2 s2 = vec2(vout[i2*8+0], vout[i2*8+1]);\n"
"    float z0 = vout[i0*8+2], z1 = vout[i1*8+2], z2 = vout[i2*8+2];\n"
"    vec3 col0 = vec3(vout[i0*8+4], vout[i0*8+5], vout[i0*8+6]);\n"
"    vec3 col1 = vec3(vout[i1*8+4], vout[i1*8+5], vout[i1*8+6]);\n"
"    vec3 col2 = vec3(vout[i2*8+4], vout[i2*8+5], vout[i2*8+6]);\n"
"    float al0 = vout[i0*8+7], al1 = vout[i1*8+7], al2 = vout[i2*8+7];\n"
"\n"
"    /* area == w0+w1+w2 at EVERY pixel: the three edge functions sum to twice\n"
"     * the signed area, independent of P, so the winding/cull verdict is one\n"
"     * per-TRIANGLE constant.  Resolve it here and return BEFORE the bounding-\n"
"     * box sweep, so a culled or degenerate triangle costs nothing instead of\n"
"     * testing (and rejecting) every pixel it covers.  Pixel-identical: a culled\n"
"     * triangle wrote nothing before either. */\n"
"    float area = edgef(s1, s2, s0);\n"
"    if (area == 0.0)\n"
"        return;\n"
"    if (uCullSign == 1 && area >= 0.0)\n"
"        return;\n"
"    if (uCullSign == 2 && area <= 0.0)\n"
"        return;\n"
"    float inv_area = 1.0 / area;\n"
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
"    /* Incremental edge functions.  edgef(a,b,P) == A*P.x - B*P.y + C with\n"
"     * A=(b.y-a.y), B=(b.x-a.x), C=B*a.y-A*a.x all per-TRIANGLE constants, so\n"
"     * the sweep steps each edge by A across a row (one add) and re-seeds from\n"
"     * -B*fy+C once per row, instead of re-evaluating 2 mul+2 sub per edge per\n"
"     * pixel (~50% of every bounding box is rejected before it reaches the\n"
"     * interpolation, so this is the hot path).  `sgn` folds the winding verdict\n"
"     * into ONE branchless test uniform across lanes: keep the pixel iff every\n"
"     * edge value shares area's sign, i.e. e*sgn >= 0 -- accept/reject identical\n"
"     * to the old if(area<0)/else split, boundary e==0 kept, since sgn is +-1. */\n"
"    float A0 = s2.y - s1.y, B0 = s2.x - s1.x, C0 = B0*s1.y - A0*s1.x;\n"
"    float A1 = s0.y - s2.y, B1 = s0.x - s2.x, C1 = B1*s2.y - A1*s2.x;\n"
"    float A2 = s1.y - s0.y, B2 = s1.x - s0.x, C2 = B2*s0.y - A2*s0.x;\n"
"    float sgn = (area < 0.0) ? -1.0 : 1.0;\n"
"    float cx = float(minx) + 0.5;\n"
"\n"
"    for (int py = miny; py <= maxy; py++) {\n"
"        float fy = float(py) + 0.5;\n"
"        float w0 = A0*cx - B0*fy + C0;\n"
"        float w1 = A1*cx - B1*fy + C1;\n"
"        float w2 = A2*cx - B2*fy + C2;\n"
"    for (int px = minx; px <= maxx; px++) {\n"
"        float e0 = w0, e1 = w1, e2 = w2;\n"
"        w0 += A0; w1 += A1; w2 += A2;   /* step before any continue */\n"
"        if (e0*sgn < 0.0 || e1*sgn < 0.0 || e2*sgn < 0.0) continue;\n"
"        vec3 bary = vec3(e0, e1, e2) * inv_area;\n"
"\n"
"        float iw = bary.x * iw0 + bary.y * iw1 + bary.z * iw2;\n"
"        if (iw <= 0.0) continue;\n"
"        float rw = 1.0 / iw;\n"
"        float z  = (bary.x * z0 * iw0 + bary.y * z1 * iw1 +\n"
"                    bary.z * z2 * iw2) * rw;\n"
"\n"
"        uint d = uint(clamp(z, 0.0, 1.0) * 16777215.0);   /* 24-bit unorm depth */\n"
"        int idx = py * uFbStride + px;\n"
"        if (uDepthWrite != 0) {\n"
"            /* Cheap non-atomic reject first: a fragment already behind the\n"
"             * stored depth can never win the atomicMin, so skip the contended\n"
"             * global read-modify-write for it.  The dense opaque internals\n"
"             * overdraw heavily, so most fragments lose -- this turns their\n"
"             * serialized L2 atomic into a plain (cacheable) load.  Pixel-\n"
"             * identical: the atomic below still resolves the race for the\n"
"             * fragments that pass the pre-read. */\n"
"            if (d >= depth[idx]) continue;\n"
"            uint old = atomicMin(depth[idx], d);\n"
"            if (d >= old) continue;\n"
"        } else {\n"
"            if (d >= depth[idx]) continue;\n"
"        }\n"
"        /* Depth-surviving fragment only: interpolate colour (and, when\n"
"         * blending, alpha) AFTER the depth test.  An occluded fragment used to\n"
"         * pay the full perspective-correct colour+alpha math before being\n"
"         * discarded; hoisting it below the test skips that work for every\n"
"         * rejected pixel.  Pixel-identical: col/al have no side effects and\n"
"         * are unchanged for the fragments that reach the blend/write. */\n"
"        vec3 col = (bary.x * col0 * iw0 + bary.y * col1 * iw1 +\n"
"                    bary.z * col2 * iw2) * rw;\n"
"        if (uBlendMode == 0) {\n"
"            vec3 cc = clamp(col, 0.0, 1.0) * 255.0 + 0.5;\n"
"            uvec3 u = uvec3(cc);\n"
"            fb[idx] = (255u << 24) | (u.r << 16) | (u.g << 8) | u.b;\n"
"        } else {\n"
"            float al = (bary.x * al0 * iw0 + bary.y * al1 * iw1 +\n"
"                        bary.z * al2 * iw2) * rw;\n"
"            uint dw = fb[idx];\n"
"            vec3 dc = vec3(float((dw >> 16) & 255u),\n"
"                           float((dw >>  8) & 255u),\n"
"                           float( dw        & 255u)) / 255.0;\n"
"            vec3 sc = clamp(col, 0.0, 1.0);\n"
"            float sa = clamp(al, 0.0, 1.0);\n"
"            vec3 oc = (uBlendMode == 2) ? (dc + sc * sa)\n"
"                                        : (sc * sa + dc * (1.0 - sa));\n"
"            vec3 cc = clamp(oc, 0.0, 1.0) * 255.0 + 0.5;\n"
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
"layout(std430, binding = 0) buffer PglCsVout { float vout[];  };\n"
"layout(std430, binding = 1) buffer PglCsAttr { float vattr[]; };\n"
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

/* ---- Pass 1: INSTANCED gouraud vertex transform (multi-model single draw) --
 * gears variant: ONE dispatch transforms+lights a whole gear train whose
 * per-vertex model transform differs.  A per-vertex INSTANCE INDEX rides in
 * the position stream's 4th float (binding 1 is a vec4 stream: pos.xyz + idx),
 * and binding 3 is a per-instance matrix SSBO the client refills every frame:
 *   gearMat[g*36 + 0..15]  = model-view-projection (column-major mat4)
 *   gearMat[g*36 + 16..31] = normal matrix        (column-major mat4)
 *   gearMat[g*36 + 32..34] = material rgb,  [35] = pad
 * The index selects the whole per-gear transform at RUNTIME, which a default-
 * block uniform array cannot do here: a dynamically-indexed uniform lowers to
 * QUNIFORM_UBO_ADDR (nir_to_vir.c ntq_emit_load_uniform), which the daemon's
 * expand_uniforms declines.  An SSBO read by a computed offset is the SAME
 * mechanism the plain xform kernel already uses for vpos[v*3+c] every
 * invocation, so it is proven on this silicon.  Bindings: 0 vout(float[],stride8)
 * 1 vpos(float[],stride4: pos.xyz,idx) 2 vnrm(float[],stride3) 3 gearMat(float[],
 * stride36).  Lighting is the gears directional model (uLightEye.w == 0):
 * one-sided pure material*diffuse along normalize(uLightEye.xyz), matching the
 * app VS exactly; uShade = (ambient, diffuse scale, specular weight, -).  The
 * vout record layout is IDENTICAL to the gouraud xform kernel, so pass 2
 * (geom_raster_cs_glsl) is reused unchanged. */
static const char geom_xform_inst_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer PglCsVout { float vout[]; }\n;"
"layout(std430, binding = 1) buffer PglCsPos  { float vpos[]; }\n;"
"layout(std430, binding = 2) buffer PglCsNrm  { float vnrm[]; }\n;"
"layout(std430, binding = 3) buffer PglCsGear { float gear[]; }\n;"
"\n"
"uniform vec4 uLightEye;\n"
"uniform vec4 uShade;\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uVertCount;\n"
"\n"
"mat4 gearMat4(int base)\n"
"{\n"
"    return mat4(vec4(gear[base+0],  gear[base+1],  gear[base+2],  gear[base+3]),\n"
"                vec4(gear[base+4],  gear[base+5],  gear[base+6],  gear[base+7]),\n"
"                vec4(gear[base+8],  gear[base+9],  gear[base+10], gear[base+11]),\n"
"                vec4(gear[base+12], gear[base+13], gear[base+14], gear[base+15]));\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    int v = int(gl_WorkGroupID.x) * 64 +\n"
"            int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x);\n"
"    if (v >= uVertCount)\n"
"        return;\n"
"\n"
"    vec3 p = vec3(vpos[v*4+0], vpos[v*4+1], vpos[v*4+2]);\n"
"    int  g = int(vpos[v*4+3]);\n"
"    vec3 n = vec3(vnrm[v*3+0], vnrm[v*3+1], vnrm[v*3+2]);\n"
"\n"
"    int  gb   = g * 36;\n"
"    mat4 mvp  = gearMat4(gb);\n"
"    mat4 nmat = gearMat4(gb + 16);\n"
"    vec3 mat  = vec3(gear[gb+32], gear[gb+33], gear[gb+34]);\n"
"\n"
"    vec3 ne = normalize((nmat * vec4(n, 0.0)).xyz);\n"
"    vec3 L  = normalize(uLightEye.xyz);\n"
"    float diff = max(dot(ne, L), 0.0);\n"
"    vec3 col = clamp(mat * (uShade.x + uShade.y * diff), 0.0, 1.0);\n"
"\n"
"    vec4 c = mvp * vec4(p, 1.0);\n"
"    vec2 W = uViewportWH;\n"
"    float iw = (c.w > 0.0) ? (1.0 / c.w) : 0.0;   /* clip sentinel */\n"
"    vec2 s = vec2((c.x * iw * 0.5 + 0.5) * W.x, (0.5 - c.y * iw * 0.5) * W.y);\n"
"    float z = c.z * iw * 0.5 + 0.5;\n"
"    if (c.w <= 0.0) col = vec3(0.0);\n"
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

/* ---- Pass 1: INSTANCED-LIT gouraud vertex transform (multi-model batch) ----
 * Generalisation of the gears instanced kernel for apps whose draws differ in
 * the FULL lighting state (engine demo: per-part MVP/normal matrix/material/
 * emissive), selected by the PGL_G3D_INSTANCED_LIT marker in the app VS.  The
 * pgl_engine coalescer stamps every vertex with its draw's instance index and
 * appends one 40-float record per draw, so a whole render pass of many
 * differently-uniformed draws transforms+lights in ONE dispatch.  Record
 * layout (binding 3, stride 40):
 *   [0..15]  model-view-projection (column-major mat4)
 *   [16..31] normal matrix         (column-major mat4)
 *   [32..34] material rgb   [35] emissive   [36] material alpha   [37..39] pad
 * The lighting evaluates the engine VS verbatim: directional eye-space light
 * along normalize(uLightEye.xyz), one-sided diffuse, a reflect-based specular
 * lobe (rd^16 * uShade.z about the fixed eye vector +Z) and a per-instance
 * additive emissive; alpha rides vout[7] so the shared raster kernel can blend
 * per instance.  Bindings and the vout record layout match the plain instanced
 * kernel (0 vout, 1 vpos stride 4 = pos.xyz+idx, 2 vnrm, 3 records), so the
 * daemon's cs_is_geom=3 dispatch and pass 2 are reused unchanged. */
static const char geom_xform_instlit_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer PglCsVout { float vout[]; }\n;"
"layout(std430, binding = 1) buffer PglCsPos  { float vpos[]; }\n;"
"layout(std430, binding = 2) buffer PglCsNrm  { float vnrm[]; }\n;"
"layout(std430, binding = 3) buffer PglCsInst { float inst[]; }\n;"
"\n"
"uniform vec4 uLightEye;\n"
"uniform vec4 uShade;\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uVertCount;\n"
"\n"
"mat4 instMat4(int base)\n"
"{\n"
"    return mat4(vec4(inst[base+0],  inst[base+1],  inst[base+2],  inst[base+3]),\n"
"                vec4(inst[base+4],  inst[base+5],  inst[base+6],  inst[base+7]),\n"
"                vec4(inst[base+8],  inst[base+9],  inst[base+10], inst[base+11]),\n"
"                vec4(inst[base+12], inst[base+13], inst[base+14], inst[base+15]));\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    int v = int(gl_WorkGroupID.x) * 64 +\n"
"            int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x);\n"
"    if (v >= uVertCount)\n"
"        return;\n"
"\n"
"    vec3 p = vec3(vpos[v*4+0], vpos[v*4+1], vpos[v*4+2]);\n"
"    int  g = int(vpos[v*4+3]);\n"
"    vec3 n = vec3(vnrm[v*3+0], vnrm[v*3+1], vnrm[v*3+2]);\n"
"\n"
"    int   gb    = g * 40;\n"
"    mat4  mvp   = instMat4(gb);\n"
"    mat4  nmat  = instMat4(gb + 16);\n"
"    vec3  mat   = vec3(inst[gb+32], inst[gb+33], inst[gb+34]);\n"
"    float emis  = inst[gb+35];\n"
"    float alpha = inst[gb+36];\n"
"\n"
"    vec3  ne   = normalize((nmat * vec4(n, 0.0)).xyz);\n"
"    vec3  L    = normalize(uLightEye.xyz);\n"
"    float diff = max(dot(ne, L), 0.0);\n"
"    vec3  R    = (2.0 * dot(ne, L)) * ne - L;   /* reflect(-L, ne) */\n"
"    float rd   = max(R.z, 0.0);\n"
"    float rd2  = rd * rd;\n"
"    float rd4  = rd2 * rd2;\n"
"    float rd8  = rd4 * rd4;\n"
"    float rd16 = rd8 * rd8;\n"
"    float spec = rd16 * uShade.z;\n"
"    vec3 col = clamp(mat * (uShade.x + uShade.y * diff + emis) + vec3(spec),\n"
"                     0.0, 1.0);\n"
"\n"
"    vec4 c = mvp * vec4(p, 1.0);\n"
"    vec2 W = uViewportWH;\n"
"    float iw = (c.w > 0.0) ? (1.0 / c.w) : 0.0;   /* clip sentinel */\n"
"    vec2 s = vec2((c.x * iw * 0.5 + 0.5) * W.x, (0.5 - c.y * iw * 0.5) * W.y);\n"
"    float z = c.z * iw * 0.5 + 0.5;\n"
"    if (c.w <= 0.0) col = vec3(0.0);\n"
"\n"
"    vout[v*8+0] = s.x;\n"
"    vout[v*8+1] = s.y;\n"
"    vout[v*8+2] = z;\n"
"    vout[v*8+3] = iw;\n"
"    vout[v*8+4] = col.r;\n"
"    vout[v*8+5] = col.g;\n"
"    vout[v*8+6] = col.b;\n"
"    vout[v*8+7] = alpha;\n"
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
"layout(std430, binding = 0) buffer PglCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer PglCsVout  { float vout[];  };\n"
"layout(std430, binding = 2) buffer PglCsDepth { uint  depth[]; };\n"
"layout(std430, binding = 3) buffer PglCsTex   { uint  tex[];   };\n"
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

/* ---- Pass 1: COLOUR-MODULATED TEXTURED vertex transform ------------------
 * imgui variant (cs_is_geom = 4).  pos, uv AND a per-vertex colour SHARE
 * binding 1 as one interleaved float stream with a 10-float stride:
 *   [i*10+0..2] = pos.xyz, [i*10+3..4] = (u, v), [i*10+5..8] = (r,g,b,a),
 *   [i*10+9] = pad   (V3D 7.1 compute exposes only 4 SSBO bindings, so the
 * colour cannot ride a 5th binding - it is interleaved with pos+uv exactly as
 * the plain tex variant interleaves pos+uv).  Applies uMVP and passes uv and
 * colour through to vout unchanged; the raster kernel interpolates both and
 * does the V-flip + bilinear fetch.  vout record grows to a 12-float stride:
 *   [0]=sx [1]=sy [2]=z [3]=iw [4]=u [5]=v [6]=r [7]=g [8]=b [9]=a
 *   [10]=pad [11]=pad. */
static const char geom_xform_texcol_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer PglCsVout { float vout[];  };\n"
"layout(std430, binding = 1) buffer PglCsAttr { float vattr[]; };\n"
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
"    vec3 p = vec3(vattr[v*10+0], vattr[v*10+1], vattr[v*10+2]);\n"
"    vec2 t = vec2(vattr[v*10+3], vattr[v*10+4]);\n"
"    vec4 col = vec4(vattr[v*10+5], vattr[v*10+6],\n"
"                    vattr[v*10+7], vattr[v*10+8]);\n"
"\n"
"    vec4 c = uMVP * vec4(p, 1.0);\n"
"    vec2 W = uViewportWH;\n"
"    float iw = (c.w > 0.0) ? (1.0 / c.w) : 0.0;   /* clip sentinel */\n"
"    vec2 s = vec2((c.x * iw * 0.5 + 0.5) * W.x, (0.5 - c.y * iw * 0.5) * W.y);\n"
"    float z = c.z * iw * 0.5 + 0.5;\n"
"\n"
"    vout[v*12+0] = s.x;\n"
"    vout[v*12+1] = s.y;\n"
"    vout[v*12+2] = z;\n"
"    vout[v*12+3] = iw;\n"
"    vout[v*12+4] = t.x;\n"
"    vout[v*12+5] = t.y;\n"
"    vout[v*12+6] = col.r;\n"
"    vout[v*12+7] = col.g;\n"
"    vout[v*12+8] = col.b;\n"
"    vout[v*12+9] = col.a;\n"
"    vout[v*12+10] = 0.0;\n"
"    vout[v*12+11] = 0.0;\n"
"}\n";

/* ---- Pass 2: COLOUR-MODULATED TEXTURED rasterizer (one workgroup / tri) ---
 * imgui variant (cs_is_geom = 4), same 8x8-workgroup-per-triangle dispatch as
 * geom_raster_tex_cs_glsl (cs_geom_tpw = 0).  Differences from the plain tex
 * raster, all required to reproduce imgui's fixed-function GL state:
 *   - the vout record carries a per-vertex COLOUR (stride 12) as well as (u,v);
 *     both are perspective-correct interpolated, and (u,v) is V-flipped;
 *   - BILINEAR sampling with CLAMP-to-edge (imgui's font atlas is GL_LINEAR):
 *     a 2x2 texel neighbourhood is fetched, unpacked from the fb's 0xAARRGGBB
 *     word to [0,1] and lerped on the fractional texel centre;
 *   - COLOUR MODULATION: src = texel * vColor, matching the imgui FS
 *     gl_FragColor = vColor * texture2D(uTexture, vUV);
 *   - ALPHA BLEND matching imgui's glBlendFuncSeparate(SRC_ALPHA,
 *     ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA): with dst = fb[idx], RGB
 *     is src.rgb*src.a + dst.rgb*(1-src.a) and ALPHA is src.a*1 +
 *     dst.a*(1-src.a) (the alpha source factor is ONE, not SRC_ALPHA), then
 *     repacked and written.  This is a read-modify-write, but imgui's half-open
 *     edge rule gives every covered pixel to exactly one triangle, so a single
 *     draw command's triangles do not race on the same fb word;
 *   - NO DEPTH: imgui disables the depth test, so this variant skips the
 *     atomicMin / depth write entirely (binding 2 stays bound but unused);
 *   - SCISSOR: uScissor = (x, y, w, h) in fb row space (y down); w/h <= 0
 *     disables it.  The bbox is clamped to the window before the sweep, so
 *     per-command clip rects cost nothing outside the covered rows/cols.
 * Bindings: 0 fb, 1 vout, 2 depth(unused), 3 tex.  uTexW/uTexH/uScissor are
 * patched into the per-draw blob. */
static const char geom_raster_texcol_cs_glsl[] =
"#version 310 es\n"
"precision highp float;\n"
"precision highp int;\n"
"\n"
"layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"\n"
"layout(std430, binding = 0) buffer PglCsFb    { uint  fb[];    };\n"
"layout(std430, binding = 1) buffer PglCsVout  { float vout[];  };\n"
"layout(std430, binding = 2) buffer PglCsDepth { uint  depth[]; };\n"
"layout(std430, binding = 3) buffer PglCsTex   { uint  tex[];   };\n"
"\n"
"uniform vec2 uViewportWH;\n"
"uniform int  uFbStride;\n"
"uniform int  uTriFirst;\n"
"uniform int  uTriCount;\n"
"uniform int  uTexW;\n"
"uniform int  uTexH;\n"
"uniform ivec4 uScissor;\n"
"\n"
"float edgef(vec2 a, vec2 b, vec2 p)\n"
"{\n"
"    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);\n"
"}\n"
"\n"
"vec4 unpack(uint w)\n"
"{\n"
"    return vec4(float((w >> 16) & 0xFFu), float((w >> 8) & 0xFFu),\n"
"                float(w & 0xFFu), float((w >> 24) & 0xFFu)) / 255.0;\n"
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
"    float iw0 = vout[i0*12+3], iw1 = vout[i1*12+3], iw2 = vout[i2*12+3];\n"
"    if (iw0 <= 0.0 || iw1 <= 0.0 || iw2 <= 0.0)\n"
"        return;   /* a vertex crossed w=0: near-plane clip guard */\n"
"\n"
"    vec2 s0 = vec2(vout[i0*12+0], vout[i0*12+1]);\n"
"    vec2 s1 = vec2(vout[i1*12+0], vout[i1*12+1]);\n"
"    vec2 s2 = vec2(vout[i2*12+0], vout[i2*12+1]);\n"
"    vec2 t0 = vec2(vout[i0*12+4], vout[i0*12+5]);\n"
"    vec2 t1 = vec2(vout[i1*12+4], vout[i1*12+5]);\n"
"    vec2 t2 = vec2(vout[i2*12+4], vout[i2*12+5]);\n"
"    vec4 c0 = vec4(vout[i0*12+6], vout[i0*12+7], vout[i0*12+8], vout[i0*12+9]);\n"
"    vec4 c1 = vec4(vout[i1*12+6], vout[i1*12+7], vout[i1*12+8], vout[i1*12+9]);\n"
"    vec4 c2 = vec4(vout[i2*12+6], vout[i2*12+7], vout[i2*12+8], vout[i2*12+9]);\n"
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
"    if (uScissor.z > 0 && uScissor.w > 0) {\n"
"        if (minx < uScissor.x) minx = uScissor.x;\n"
"        if (miny < uScissor.y) miny = uScissor.y;\n"
"        if (maxx > uScissor.x + uScissor.z - 1) maxx = uScissor.x + uScissor.z - 1;\n"
"        if (maxy > uScissor.y + uScissor.w - 1) maxy = uScissor.y + uScissor.w - 1;\n"
"    }\n"
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
"        vec4 vc = (bary.x * c0 * iw0 + bary.y * c1 * iw1 +\n"
"                   bary.z * c2 * iw2) * rw;\n"
"\n"
"        /* Bilinear + CLAMP-to-edge.  NO V-flip here: imgui's VS passes the\n"
"           atlas v coordinate through unchanged and the atlas rows are\n"
"           uploaded top-first, matching the fb's y-down row space (the\n"
"           plain tex kernel flips because THAT app's VS pre-flips v). */\n"
"        float fx = uv.x * float(uTexW) - 0.5;\n"
"        float fy = uv.y * float(uTexH) - 0.5;\n"
"        int x0 = int(floor(fx));\n"
"        int y0 = int(floor(fy));\n"
"        float ax = fx - float(x0);\n"
"        float ay = fy - float(y0);\n"
"        int xa = clamp(x0, 0, uTexW - 1);\n"
"        int xb = clamp(x0 + 1, 0, uTexW - 1);\n"
"        int ya = clamp(y0, 0, uTexH - 1);\n"
"        int yb = clamp(y0 + 1, 0, uTexH - 1);\n"
"        vec4 q00 = unpack(tex[ya * uTexW + xa]);\n"
"        vec4 q10 = unpack(tex[ya * uTexW + xb]);\n"
"        vec4 q01 = unpack(tex[yb * uTexW + xa]);\n"
"        vec4 q11 = unpack(tex[yb * uTexW + xb]);\n"
"        vec4 texel = mix(mix(q00, q10, ax), mix(q01, q11, ax), ay);\n"
"\n"
"        vec4 src = texel * vc;   /* colour modulation */\n"
"        int idx = py * uFbStride + px;\n"
"        vec4 dst = unpack(fb[idx]);\n"
"        float sa = clamp(src.a, 0.0, 1.0);\n"
"        /* RGB: SRC_ALPHA / ONE_MINUS_SRC_ALPHA.  A: ONE / ONE_MINUS_SRC_ALPHA\n"
"           (imgui's glBlendFuncSeparate uses GL_ONE for the alpha source). */\n"
"        vec3 outrgb = src.rgb * sa + dst.rgb * (1.0 - sa);\n"
"        float outa = sa + dst.a * (1.0 - sa);\n"
"\n"
"        uvec3 u = uvec3(clamp(outrgb, 0.0, 1.0) * 255.0 + 0.5);\n"
"        uint a8 = uint(clamp(outa, 0.0, 1.0) * 255.0 + 0.5);\n"
"        fb[idx] = (a8 << 24) | (u.r << 16) | (u.g << 8) | u.b;\n"
"    }\n"
"    }\n"
"}\n";

#endif /* GEOM_RASTER_CS_H */
