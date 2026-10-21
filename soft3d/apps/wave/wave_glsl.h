/*
 * wave_glsl.h -- GLSL ES 1.00 sources for the soft3d "wave" demo.
 *
 * Three fragment shaders selected at compile time:
 *   WAVE_DIAG=1      flat two-colour split (pipeline sanity check)
 *   PGL_G3D defined  FAITHFUL single-pass port of the reference web effect
 *                    ("Shallow Ocean Waves (Looping)", shadertoy N3dSzs by
 *                    foodi; wave stack forked from "Water Bla", caustics by
 *                    jaybird).  Re-implemented here from the published
 *                    algorithm for this engine's single-pass fullscreen
 *                    route: the reference's separable-Gaussian bloom passes
 *                    have no FBO equivalent here and are approximated
 *                    analytically (wide specular lobe + caustic plateau
 *                    halo); everything else -- 60+20 wave stack with the
 *                    loop crossfade, tetrahedral normal, refracted ground,
 *                    4-warp simplex caustics with per-channel offsets,
 *                    Beer-Lambert absorption, spec^512, 1/2.2 gamma -- is
 *                    ported 1:1.  The wave stacks run as RUNTIME LOOPS whose
 *                    per-wave constants (direction from the original murmur
 *                    hash, quantised looping speed, octave frequency/amplitude)
 *                    are recomputed in-shader from the wave index -- the same
 *                    law the reference uses.  The engine wraps this source as
 *                    a GLSL ES 3.10 COMPUTE kernel for the V3D CSD route, so
 *                    the uint hash ops are available there; the source keeps
 *                    an ES 1.00-parseable branch (selected by __VERSION__) for
 *                    the graphics-stage compile, whose FS stage is never
 *                    dispatched for a fullscreen effect.  A previous C++-baked
 *                    UNROLLED emission of the same math exhausted the process
 *                    heap inside mesa's register allocator on the pi5 (O(n^2)
 *                    interference graph over tens of thousands of virtual
 *                    registers, data-abort at ra_realloc_interference_graph)
 *                    and could never fit V3D_3D_PROG_MAX_QPU anyway.
 *   otherwise        NO live shader: the faithful ocean costs ~700 trig +
 *                    48 simplex evaluations per pixel, which the CPU
 *                    tree-walking interpreter can never run interactively,
 *                    so CPU targets (HW=virt) play back host-baked frames of
 *                    exactly the faithful render (res/wave_loop.rgb565, see
 *                    wave.cpp).  A GPU target that DECLINES the faithful
 *                    program falls back to the SAME baked playback, so the
 *                    cheap analytic variant was removed entirely: no platform
 *                    ever draws a simplified look.
 *
 * The interpreter still needs a linkable FS to own the uniform metadata
 * (iGlobalTime / iResolution) for the GPU override path; wave_fs_stub is a
 * minimal never-rendered carrier for that role -- the faithful pixels always
 * come from the GPU kernel or the baked playback, never from a cheap look.
 */
#ifndef WAVE_GLSL_H
#define WAVE_GLSL_H

#ifndef __cplusplus
#error "wave_glsl.h requires C++ (raw string literals)"
#endif

#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>

/* Passthrough vertex shader: a full-screen clip-space quad.  No uMVP so
 * the engine flags the program as a fullscreen effect and routes the FS
 * to the GPU compute path on PGL_G3D targets. */
static const char* wave_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

/* Playback pair for CPU-only targets (HW=virt): the faithful ocean math
 * costs tens of ms PER PIXEL on the GLSL interpreter (hours per frame at
 * fullscreen), so the virt build instead plays back precomputed frames of
 * exactly the faithful render -- baked host-side from the same formulas
 * (.tmp/pgl_bench/wave_bake.cpp) into res/wave_loop.rgb565 -- through this
 * texblit-shaped program, which the engine's native ff_texblit_frag fast
 * path keeps interactive.  Row 0 of the blob is the image top, hence the
 * flipped v in the quad (wave.cpp). */
static const char* wave_vs_tex =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"attribute vec2 aTexCoord;\n"
	"varying vec2 vUV;\n"
	"void main() {\n"
	"    vUV = aTexCoord;\n"
	"    gl_Position = aPosition;\n"
	"}\n";

static const char* wave_fs_tex =
	"#version 100\n"
	"precision mediump float;\n"
	"varying vec2 vUV;\n"
	"uniform sampler2D uTex;\n"
	"void main() {\n"
	"    gl_FragColor = texture2D(uTex, vUV);\n"
	"}\n";

#define WAVE_FS_PREAMBLE \
	"#version 100\n" \
	"precision highp float;\n"

/* ------------------------------------------------------------------ *
 * Diagnostic toggle.  WAVE_DIAG=1 compiles a minimal two-colour split to
 * confirm the EGL/ImGui/fullscreen pipeline is alive; 0 = real effect.
 * ------------------------------------------------------------------ */
#ifndef WAVE_DIAG
#define WAVE_DIAG 0
#endif

#if WAVE_DIAG
/* ---------------------- DIAGNOSTIC SHADER ------------------------- */
static const std::string wave_fs_glsl = std::string(WAVE_FS_PREAMBLE) + R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;

void main()
{
    vec2 res = iResolution.xy;
    if (res.x < 1.0 || res.y < 1.0) res = vec2(800.0, 600.0);

    float horiz = res.y * 0.60;
    vec3  col;
    if (gl_FragCoord.y >= horiz) {
        col = vec3(0.30, 0.55, 0.90);
    } else {
        col = vec3(0.02, 0.20, 0.35);
    }
    float pulse = 0.5 + 0.5 * sin(iGlobalTime * 2.0);
    col = col * (0.85 + 0.30 * pulse);

    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

#else /* !WAVE_DIAG */

/* ---------------------- INTERPRETER LINK STUB --------------------- *
 * Minimal ES 1.00 fragment shader whose ONLY role is to give the CPU
 * interpreter a linkable program that owns the iGlobalTime / iResolution
 * uniform metadata the GPU override path stages by name.  It is NEVER
 * dispatched: on GPU targets the faithful kernel (wave_fs_gpu) renders, and
 * on any GPU decline -- or on CPU-only targets -- the baked faithful
 * playback (wave_fs_tex) renders instead.  The old cheap analytic variant
 * was deleted outright so no platform ever draws a simplified look.
 *
 * Uniforms:
 *   iGlobalTime  float  seconds since start
 *   iResolution  vec3   viewport size (xy used)
 */
static const std::string wave_fs_stub = std::string(WAVE_FS_PREAMBLE) + R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;

void main()
{
    /* never reaches the screen: a flat placeholder so the program links and
     * the uniform block layout matches the faithful source. */
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ *
 * FAITHFUL PORT, GPU target.  The engine hands this source to
 * pgl_g3d_program_compile, which compiles it TWICE:
 *   1. pglc_compile_glsl -- the graphics VS+FS pair, parsed at the version
 *      the source declares (#version 100 -> GLSL ES 1.00).  For a
 *      fullscreen effect this stage is compiled but NEVER dispatched (the
 *      binner 3D route is disabled on this silicon).
 *   2. pglc_compile_compute -- fragment_to_compute_source() strips the
 *      #version line and re-emits the body inside "#version 310 es" with
 *      gl_FragColor/gl_FragCoord/texture2D #defined to its own globals.
 *      THIS is the kernel the CSD fullscreen dispatch actually runs.
 * The body therefore branches on __VERSION__: the 310 branch carries the
 * faithful ocean with the wave constants recomputed per wave from the
 * reference's murmur-hash law (uint ops are ES 3.10-only), and the 100
 * branch is a minimal valid ES 1.00 stub for the never-executed graphics
 * stage.  Both branches declare identical uniforms so the engine's
 * name-keyed blob staging (iGlobalTime / iResolution) is unaffected.
 * Loop trip counts are made opaque to the unroller via a uniform-dependent
 * guard so nir_opt_loop_unroll can never re-explode the stacks.
 * ------------------------------------------------------------------ */
/* Wave-stack sizes.  The reference uses 60 primary + 20 detail + 8 ground
 * waves. */
#ifndef WAVE_N_PRIMARY
#define WAVE_N_PRIMARY 60
#endif
#ifndef WAVE_N_DETAIL
#define WAVE_N_DETAIL 20
#endif
#ifndef WAVE_N_GROUND
#define WAVE_N_GROUND 8
#endif

namespace wave_gen {

static std::string build_faithful_fs()
{
    std::string g;
    g += WAVE_FS_PREAMBLE;
    g += R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;

#if __VERSION__ >= 300
/* ================= FAITHFUL OCEAN (ES 3.10 compute body) ============ */

/* baked reference constants */
const vec3 SUN_DIR   = vec3(0.7001400, -0.1400280, 0.7001400);
const vec3 GROUND_C  = vec3(0.85, 0.755, 0.65);
const vec3 ABSORB    = vec3(9.1667, 1.8333, 1.2222);

/* ---- the reference's per-wave constant law, recomputed at runtime ----
 *   rand  = hash11(float(i)) * 2 - 1,  hash11 = murmur11 over the float's
 *           bit pattern, result remapped to [0,1)
 *   dir   = (sin, cos)(0.2 + rand * clamp(i/59 + 0.15, 0, 1) * 2pi)
 *   speed = round((1 + s*0.05) * 2pi/8) / (2pi/8)   (loop quantisation)
 *   freq  = 4.6 * 1.028^i,  ampl = 1/1.028^i                        ---- */
uint murmur11u(uint src)
{
    uint M = 0x5bd1e995u;
    uint h = 1190494759u;
    src *= M; src ^= src >> 24u; src *= M;
    h *= M; h ^= src;
    h ^= h >> 13u; h *= M; h ^= h >> 15u;
    return h;
}

float hash11f(float f)
{
    uint r = (murmur11u(floatBitsToUint(f)) & 0x007fffffu) | 0x3f800000u;
    return uintBitsToFloat(r) - 1.0;
}

/* animated WaveHeight(uv, t, num) with the loop crossfade partner phase
 * (sin(ph - 8*speed) via the angle-addition identity) */
float waveAnim(vec2 uv, float t, float al, int num)
{
    float h = 0.0, tw = 0.0;
    float s = 1.0, w = 1.0, f = 4.6;
    int cnt = num;
    if (iGlobalTime < -1.0e30) cnt = 0;   /* opaque trip count: never unroll */
    for (int i = 0; i < cnt; i++) {
        float rnd = hash11f(float(i)) * 2.0 - 1.0;
        float dmd = clamp(float(i) / 59.0 + 0.15, 0.0, 1.0);
        float ph  = 0.2 + rnd * dmd * 6.283185307179586;
        float spd = 1.0 + s * 0.05;
        float sq  = floor(spd * 0.7853981633974483 + 0.5) / 0.7853981633974483;
        float arg = uv.x * (sin(ph) * 10.0 * f)
                  + uv.y * (cos(ph) * 10.0 * f) + t * sq;
        float s1 = sin(arg), c1 = cos(arg);
        float q1 = 0.5 * s1 + 0.5, h1 = q1 * q1 * 2.0 - 1.0;
        float cc = 8.0 * sq;
        float v2 = s1 * cos(cc) - c1 * sin(cc);
        float q2 = 0.5 * v2 + 0.5, h2 = q2 * q2 * 2.0 - 1.0;
        h += mix(h1, h2, al) * w;
        tw += w;
        w /= 1.028; f *= 1.028; s *= 1.028;
    }
    return 0.5 + 0.5 * (h / tw);
}

/* static ground field: time baked to -10 (mod 8 = 6, partner -2, blend 0.75) */
float waveStat(vec2 uv, int num)
{
    float h = 0.0, tw = 0.0;
    float s = 1.0, w = 1.0, f = 4.6;
    int cnt = num;
    if (iGlobalTime < -1.0e30) cnt = 0;   /* opaque trip count: never unroll */
    for (int i = 0; i < cnt; i++) {
        float rnd = hash11f(float(i)) * 2.0 - 1.0;
        float dmd = clamp(float(i) / 59.0 + 0.15, 0.0, 1.0);
        float ph  = 0.2 + rnd * dmd * 6.283185307179586;
        float spd = 1.0 + s * 0.05;
        float sq  = floor(spd * 0.7853981633974483 + 0.5) / 0.7853981633974483;
        float arg = uv.x * (sin(ph) * 10.0 * f)
                  + uv.y * (cos(ph) * 10.0 * f);
        float s1 = sin(arg), c1 = cos(arg);
        float c1a = 6.0 * sq, c2a = -2.0 * sq;
        float a1 = s1 * cos(c1a) + c1 * sin(c1a);
        float q1 = 0.5 * a1 + 0.5, h1 = q1 * q1 * 2.0 - 1.0;
        float a2 = s1 * cos(c2a) + c1 * sin(c2a);
        float q2 = 0.5 * a2 + 0.5, h2 = q2 * q2 * 2.0 - 1.0;
        h += mix(h1, h2, 0.75) * w;
        tw += w;
        w /= 1.028; f *= 1.028; s *= 1.028;
    }
    return 0.5 + 0.5 * (h / tw);
}

/* ---- jaybird simplex noise with analytic gradient; `blur` scales the
 * result (42 sharp .. 0 invisible), replacing the reference's global. ---- */
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }

vec4 snoise(vec3 v, float blur)
{
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 gg = step(x0.yzx, x0.xyz);
    vec3 ll = 1.0 - gg;
    vec3 i1 = min(gg.xyz, ll.zxy);
    vec3 i2 = max(gg.xyz, ll.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - 0.5;
    i = mod(i, 289.0);
    vec4 p = permute(permute(permute(
             i.z + vec4(0.0, i1.z, i2.z, 1.0))
           + i.y + vec4(0.0, i1.y, i2.y, 1.0))
           + i.x + vec4(0.0, i1.x, i2.x, 1.0));
    float n_ = 0.142857142857;
    vec3 ns = n_ * vec3(2.0, 0.5, 1.0).yzx - vec3(0.0, 1.0, 0.0).zxy;
    vec4 j = p - 49.0 * floor(p / 49.0);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = j - 7.0 * x_;
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 g0 = vec3(a0.xy, h.x);
    vec3 g1 = vec3(a0.zw, h.y);
    vec3 g2 = vec3(a1.xy, h.z);
    vec3 g3 = vec3(a1.zw, h.w);
    vec4 nrm = inversesqrt(vec4(dot(g0, g0), dot(g1, g1),
                                dot(g2, g2), dot(g3, g3)));
    g0 *= nrm.x; g1 *= nrm.y; g2 *= nrm.z; g3 *= nrm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1),
                            dot(x2, x2), dot(x3, x3)), 0.0);
    vec4 m2 = m * m;
    vec4 m3 = m2 * m;
    vec4 m4 = m2 * m2;
    vec4 pd = vec4(dot(x0, g0), dot(x1, g1), dot(x2, g2), dot(x3, g3));
    vec3 grad = -6.0 * (m3.x * x0 * pd.x + m3.y * x1 * pd.y +
                        m3.z * x2 * pd.z + m3.w * x3 * pd.w)
                + (m4.x * g0 + m4.y * g1 + m4.z * g2 + m4.w * g3);
    return mix(42.0, 0.0, blur) * vec4(grad, dot(m4, pd));
}

/* ---- 4-stage domain-warped caustic field ---- */
float water_caustics(vec3 pos, float blur)
{
    vec4 n = snoise(pos, blur);
    pos -= 0.07 * n.xyz;
    pos *= 1.62;
    n = snoise(pos, blur);
    pos -= 0.07 * n.xyz;
    n = snoise(pos, blur);
    pos -= 0.07 * n.xyz;
    n = snoise(pos, blur);
    return n.w;
}

/* ---- RGB caustics: per-channel warp offset, shared +1.0 partner ---- */
vec3 caustics_rgb(vec3 pos, float blur)
{
    vec3 o = vec3(0.02, 0.0, 0.02);
    float base = water_caustics(pos + o + 1.0, blur);
    vec3 c;
    c.x = mix(water_caustics(pos + o, blur),       base, 0.5);
    c.y = mix(water_caustics(pos + o * 4.0, blur), base, 0.5);
    c.z = mix(water_caustics(pos + o * 6.0, blur), base, 0.5);
    return exp(c * 4.0 - 1.0);
}

)GLSL";
    /* wave-stack sizes enter the source as literals so the host probe can
     * still recompile with reduced counts (WAVE_N_* knobs). */
    g += "#define PRIMARY_WAVES " + std::to_string(WAVE_N_PRIMARY) + "\n";
    g += "#define DETAIL_WAVES "  + std::to_string(WAVE_N_DETAIL)  + "\n";
    g += "#define GROUND_WAVES "  + std::to_string(WAVE_N_GROUND)  + "\n";
    g += R"GLSL(
float water_h(vec2 xz, float t, float al)
{
    return waveAnim(xz * 0.1, t, al, PRIMARY_WAVES)
         + 0.05 * waveAnim(xz * 0.6, t, al, DETAIL_WAVES);
}

float map_y(vec3 p, float t, float al)
{
    return p.y - water_h(p.xz, t, al);
}

vec3 surf_normal(vec3 p, float t, float al)
{
    const float h = 0.001;
    vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * map_y(p + k.xyy * h, t, al) +
        k.yyx * map_y(p + k.yyx * h, t, al) +
        k.yxy * map_y(p + k.yxy * h, t, al) +
        k.xxx * map_y(p + k.xxx * h, t, al));
}

void main()
{
    vec2 res = iResolution.xy;
    if (res.x < 1.0 || res.y < 1.0) res = vec2(800.0, 600.0);

    float t  = mod(iGlobalTime, 8.0);   /* loop period 8 s */
    float al = t / 8.0;

    /* top-down perspective camera at (0,2,0), focal 1 (cr = -X) */
    vec2 uv = (2.0 * gl_FragCoord.xy - res) / res.y;
    vec3 ro = vec3(0.0, 2.0, 0.0);
    vec3 rd = normalize(vec3(-uv.x, -1.0, uv.y));

    /* ray / mean-water-plane intersection */
    float th = -2.0 / rd.y;
    vec3  p  = ro + rd * th;

    vec3 n  = surf_normal(p, t, al);
    vec3 n2 = normalize(n * vec3(1.0, 0.25, 1.0));
    vec3 refl = rd - 2.0 * dot(n2, rd) * n2;          /* manual reflect */
    float ci = dot(n, rd);
    float kk = 1.0 - (1.0 / 1.44) * (1.0 - ci * ci);  /* eta = 1/1.2 */
    vec3 refr = (kk < 0.0) ? refl
             : (rd / 1.2) - (ci / 1.2 + sqrt(max(kk, 0.0))) * n;

    /* static bumpy ground plane seen through the water */
    float hg = 1.8 * waveStat(p.xz * 0.025 + vec2(0.05, 0.05), GROUND_WAVES)
             + 1.0 * waveStat(p.xz * 0.10 + vec2(1.05, 0.05), GROUND_WAVES);
    float gd = -0.1 + hg;
    float tG = gd / abs(refr.y);
    vec3  pG = p + refr * tG;

    /* caustics on the ground, depth-faded and loop-crossfaded */
    float ca   = 1.0 - clamp(exp(-tG * 2.0), 0.0, 1.0);
    float blur = 1.0 - min(1.0, ca * 2.0);
    vec3  cp   = pG * 1.5 + vec3(0.0, t * 0.25, 0.0);
    vec3  cau  = mix(caustics_rgb(cp, blur),
                     caustics_rgb(cp - vec3(0.0, 2.0, 0.0), blur), al);
    cau *= ca * 1.5;

    float sd   = dot(refl, SUN_DIR);
    float s2   = max(sd, 0.0);
    float spec = s2 * s2; spec *= spec; spec *= spec;   /* ^8   */
    spec *= spec; spec *= spec; spec *= spec;           /* ^64  */
    spec *= spec; spec *= spec; spec *= spec;           /* ^512 */

    vec3 col = (GROUND_C + cau) * exp(-tG * ABSORB) + vec3(spec * 2.0);

    /* analytic stand-in for the reference's separable bloom passes
     * (threshold 1.0): a halo where caustics or the tight spec spike. */
    float bl = smoothstep(0.75, 1.5, (cau.x + cau.y + cau.z) * 0.33333) * 0.30
             + s2 * s2 * s2 * s2 * 0.35;               /* spec ^4 halo */
    col += vec3(bl);

    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    gl_FragColor = vec4(col, 1.0);
}

#else  /* __VERSION__ < 300 */
/* ES 1.00 graphics-stage stub.  pglc_compile_glsl parses the source at the
 * declared #version 100 and MUST succeed for the program to be accepted,
 * but for a fullscreen effect the graphics FS stage is never dispatched on
 * this silicon (the binner 3D route is disabled; the CSD compute kernel
 * from the 310 branch above is what actually runs).  Keep it minimal so
 * the graphics compile is instant.  NOT a rendering simplification: no
 * pixel of this branch ever reaches the screen on any platform. */
void main()
{
    gl_FragColor = vec4(0.5 + 0.5 * sin(gl_FragCoord.y * 0.01 + iGlobalTime),
                        0.40, 0.60, 1.0);
}
#endif /* __VERSION__ */
)GLSL";
    return g;
}

} /* namespace wave_gen */

/* The interpreter front-end links the minimal stub: its per-function
 * local-slot cap (GLSL_MAX_LOCALS) can never hold the faithful wave stack,
 * and ~22 ms/px would freeze any CPU path, so the stub only owns the uniform
 * metadata.  On GPU targets the faithful source reaches mesa separately
 * through glSetGpuShaderSourceEWOK (see wave.cpp); on any decline or on
 * CPU-only targets the baked faithful playback renders instead -- the cheap
 * analytic variant no longer exists. */
static const std::string wave_fs_glsl = wave_fs_stub;
#ifdef PGL_G3D
static const std::string wave_fs_gpu = wave_gen::build_faithful_fs();
#endif

#endif /* WAVE_DIAG */

#endif /* WAVE_GLSL_H */
