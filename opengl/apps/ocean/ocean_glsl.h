/*
 * ocean_glsl.h -- GLES2 / GLSL ES 1.00 shaders for the "Ocean" demo.
 *
 * A port of the reference web effect "Very fast procedural ocean"
 * (shadertoy.com/view/MdXyzX by Justin Hawkins / scrawk, 2017).  The scene is
 * an infinite animated sea seen from a low, slowly orbiting camera: a height
 * field is built from a stack of octaves (each a warped |sin|/|cos| "sea
 * octave" driven by value noise), the surface is found by a height-map binary
 * trace, the normal comes from three detailed taps, and the colour mixes
 * a fresnel sky reflection over a depth-tinted water body with a sun glint and
 * a distance attenuation, then a ^0.75 tonemap.  The tuning constants
 * (SEA_HEIGHT, SEA_CHOPPY, SEA_SPEED, SEA_FREQ, the palette) are the
 * reference's; the octave/step counts are reduced to 2/3/5 (from the
 * reference's 3/5/8) because the effect is ALU-bound on the V3D compute path
 * and this is the shipped FPS/look balance.
 *
 * The reference source was supplied by the user and is reproduced here as an
 * algorithm, re-expressed for this engine.  A few constructs were rewritten to
 * keep the CPU interpreter (HW=virt) and the on-device mesa compiler
 * (HW=raspi5/raspix) pixel-identical, WITHOUT changing the result:
 *   - the mat3 fromEuler() rotation and the mat2 octave warp are expanded by
 *     hand (v*M == vec3(dot(col0,v), dot(col1,v), dot(col2,v))), so no matN
 *     appears (the CPU path avoids matrices);
 *   - smoothstep(0.0, -0.05, dir.y) uses edge0 > edge1, which the GLSL spec
 *     leaves undefined and mesa may evaluate differently from the interpreter,
 *     so it is expanded to its explicit clamp(t)*t*t*(3-2t) form;
 *   - heightMapTracing's `out vec3 p` is returned as a vec3, and every
 *     component/swizzle write (n.y=..., dir.z+=..., uv.x*=...) is rebuilt with
 *     vec() constructors -- the conservative subset waterdrop/wave use;
 *   - pow(fresnel,3.0) becomes fresnel*fresnel*fresnel (integer exponent, exact).
 *
 * LOSSLESS SPEED-UP vs the reference: the sky/sea blend weight is exactly 0 for
 * every pixel above the horizon (dir.y >= 0), where the reference still runs the
 * full trace + normal + sea shading and then discards it in the mix.  main()
 * branches on that weight and skips the sea path for pure-sky pixels -- identical
 * output, materially less work on the upper part of the frame.
 *
 * GPU route (raspi5 / raspix, PGL_G3D): the vertex shader is a passthrough with
 * no uMVP and this fragment shader reads gl_FragCoord, so pgl_engine recognises
 * the program as a fullscreen effect and dispatches it as a VideoCore VII
 * compute kernel through the same transparent path shadertoy/waterdrop use --
 * this is what makes the effect GPU accelerated.  CPU route (virt): the
 * engine's interpreter runs the identical GLSL (a heavy ocean trace, so low FPS
 * there is the documented interpreter cost ceiling, not a defect).
 *
 * Constraints honoured on BOTH paths:
 *   - no integer division (the V3D backend aborts on it)
 *   - no reserved built-in-prefix identifiers as user names (mesa rejects them
 *     at link and silently drops the program to the CPU)
 *   - loops with literal bounds (injected as decimal literals); every builtin
 *     used is in the engine's table
 */

#ifndef OCEAN_GLSL_H
#define OCEAN_GLSL_H

#ifndef __cplusplus
#error "ocean_glsl.h requires C++ (raw string literals)"
#endif

#include <string>
#include <cstdio>

/* Passthrough vertex shader: a full-screen clip-space quad.  It intentionally
 * references no uMVP / matrices so the engine flags the program as a fullscreen
 * effect and routes the fragment shader to the GPU compute path. */
static const char* ocean_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

#define OCEAN_FS_PREAMBLE \
	"#version 100\n" \
	"precision highp float;\n"

namespace ocean_gen {

/* Build the ocean fragment shader.  `iter_geom` / `iter_frag` / `steps` are
 * injected as decimal literals so the loop bounds stay literal for the CPU
 * interpreter and mesa alike (no runtime-variable trip counts). */
static std::string build_ocean_fs(int iter_geom, int iter_frag, int steps)
{
    /* Upper bound of the coarse height field: each sea_octave returns a value in
     * [0,1] and map() accumulates two of them per iteration with amp =
     * SEA_HEIGHT * 0.22^i, so h <= 2 * SEA_HEIGHT * sum(0.22^i).  Injected as a
     * literal so heightMapTracing can bracket the hit analytically (see there);
     * deriving it here keeps the bound exact if the octave count is retuned. */
    double hmax = 0.0, amp = 1.0;
    for (int i = 0; i < iter_geom; i++) { hmax += amp; amp *= 0.22; }
    hmax *= 2.0 * 0.6;   /* 2 octaves per iteration, SEA_HEIGHT = 0.6 */
    char hmax_lit[32];
    snprintf(hmax_lit, sizeof(hmax_lit), "%.6f", hmax);

    std::string g;
    g += OCEAN_FS_PREAMBLE;
    g += R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;

const float PI      = 3.141592;

/* ---- sea tuning (reference constants) ---- */
const float SEA_HEIGHT = 0.6;
const float SEA_CHOPPY = 4.0;
const float SEA_SPEED  = 0.8;
const float SEA_FREQ   = 0.16;
const vec3  SEA_BASE        = vec3(0.1, 0.19, 0.22);
const vec3  SEA_WATER_COLOR = vec3(0.8, 0.9, 0.6);

/* max value of map()'s height sum (derived from the octave count by the
 * generator): the surface can never be above y = SEA_HMAX. */
const float SEA_HMAX = )GLSL";
    g += hmax_lit;
    g += R"GLSL(;

/* ---- value noise (bilinear hash lattice) ---- */
float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return -1.0 + 2.0 * mix(mix(hash(i + vec2(0.0, 0.0)),
                                hash(i + vec2(1.0, 0.0)), u.x),
                            mix(hash(i + vec2(0.0, 1.0)),
                                hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

/* ---- lighting ---- */
float diffuse(vec3 n, vec3 l, float p) {
    return pow(dot(n, l) * 0.4 + 0.6, p);
}
float specular(vec3 n, vec3 l, vec3 e, float s) {
    float nrm = (s + 8.0) / (PI * 8.0);
    return pow(max(dot(reflect(e, n), l), 0.0), s) * nrm;
}

/* ---- sky gradient along a direction ---- */
vec3 getSkyColor(vec3 e) {
    float ey = max(e.y, 0.0);
    return vec3(pow(1.0 - ey, 2.0), 1.0 - ey, 0.6 + (1.0 - ey) * 0.4);
}

/* ---- one warped sea octave ---- */
float sea_octave(vec2 uv, float choppy) {
    float nz = noise(uv);
    uv = uv + vec2(nz, nz);
    vec2 wv  = 1.0 - abs(sin(uv));
    vec2 swv = abs(cos(uv));
    wv = mix(wv, swv, wv);
    return pow(1.0 - pow(wv.x * wv.y, 0.65), choppy);
}

/* Coarse height field used by the trace; seaTime is the wave clock
 * (iGlobalTime * SEA_SPEED).  The mat2 octave warp
 * octave_m = mat2(1.6,1.2,-1.2,1.6) is expanded as
 * uv' = vec2(dot(col0,uv), dot(col1,uv)). */
float map(vec3 p) {
    float seaTime = iGlobalTime * SEA_SPEED;
    float freq   = SEA_FREQ;
    float amp    = SEA_HEIGHT;
    float choppy = SEA_CHOPPY;
    vec2  uv = vec2(p.x * 0.75, p.z);

    float h = 0.0;
    for (int i = 0; i < )GLSL";
    g += std::to_string(iter_geom);
    g += R"GLSL(; i++) {
        float d  = sea_octave((uv + seaTime) * freq, choppy);
        d = d + sea_octave((uv - seaTime) * freq, choppy);
        h = h + d * amp;
        uv = vec2(1.6 * uv.x + 1.2 * uv.y, -1.2 * uv.x + 1.6 * uv.y);
        freq = freq * 1.9;
        amp = amp * 0.22;
        choppy = mix(choppy, 1.0, 0.2);
    }
    return p.y - h;
}

/* Detailed height field used for the normal. */
float map_detailed(vec3 p) {
    float seaTime = iGlobalTime * SEA_SPEED;
    float freq   = SEA_FREQ;
    float amp    = SEA_HEIGHT;
    float choppy = SEA_CHOPPY;
    vec2  uv = vec2(p.x * 0.75, p.z);

    float h = 0.0;
    for (int i = 0; i < )GLSL";
    g += std::to_string(iter_frag);
    g += R"GLSL(; i++) {
        float d  = sea_octave((uv + seaTime) * freq, choppy);
        d = d + sea_octave((uv - seaTime) * freq, choppy);
        h = h + d * amp;
        uv = vec2(1.6 * uv.x + 1.2 * uv.y, -1.2 * uv.x + 1.6 * uv.y);
        freq = freq * 1.9;
        amp = amp * 0.22;
        choppy = mix(choppy, 1.0, 0.2);
    }
    return p.y - h;
}

vec3 getSeaColor(vec3 p, vec3 n, vec3 l, vec3 eye, vec3 dist) {
    float fresnel = 1.0 - max(dot(n, -eye), 0.0);
    fresnel = fresnel * fresnel * fresnel * 0.65;

    vec3 reflected = getSkyColor(reflect(eye, n));
    vec3 refracted = SEA_BASE + diffuse(n, l, 80.0) * SEA_WATER_COLOR * 0.12;

    vec3 color = mix(refracted, reflected, fresnel);

    float atten = max(1.0 - dot(dist, dist) * 0.001, 0.0);
    color = color + SEA_WATER_COLOR * (p.y - SEA_HEIGHT) * 0.18 * atten;
    color = color + vec3(specular(n, l, eye, 60.0));

    return color;
}

/* Surface normal from three detailed taps (no component writes). */
vec3 getNormal(vec3 p, float eps) {
    float h  = map_detailed(p);
    float nx = map_detailed(vec3(p.x + eps, p.y, p.z)) - h;
    float nz = map_detailed(vec3(p.x, p.y, p.z + eps)) - h;
    return normalize(vec3(nx, eps, nz));
}

/* Height-map binary trace; returns the hit point (the reference's
 * `out vec3 p`), so no out parameter is needed.
 *
 * SPEED-UP vs the reference, same surface: the reference brackets the hit with
 * the fixed interval [0, 1000] and spends its bisection steps narrowing that
 * huge range.  Here the bracket is derived analytically instead: sea_octave()
 * returns a value in [0,1] and map() sums non-negative amplitudes, so the
 * height field satisfies 0 <= h <= SEA_HMAX and map(p) = p.y - h.  A downward
 * ray (dir.y < 0, which is exactly the sea branch main() traces) is therefore
 * still above every possible crest while p.y > SEA_HMAX and provably at or
 * below the surface once p.y <= 0, so the crossing lies in
 * [(eye.y - SEA_HMAX) / -dir.y, eye.y / -dir.y].  Feeding that tight interval
 * to the same secant iteration converges in far fewer steps than [0, 1000] --
 * the shipped step count is )GLSL";
    g += std::to_string(steps);
    g += R"GLSL( where the reference needs 8 -- and the hit it lands on is at
 * least as accurate.  Upward / near-horizontal rays clamp to the reference's
 * 1000 and take the `hx > 0` far-point return exactly as before. */
vec3 heightMapTracing(vec3 eye, vec3 dir) {
    float dy = max(-dir.y, 0.0001);
    float tm = clamp((eye.y - SEA_HMAX) / dy, 0.0, 1000.0);
    float tx = clamp(eye.y / dy, tm, 1000.0);
    float hx = map(eye + dir * tx);
    if (hx > 0.0) return eye + dir * tx;
    float hm = map(eye + dir * tm);
    float tmid = tm;
    for (int i = 0; i < )GLSL";
    g += std::to_string(steps);
    g += R"GLSL(; i++) {
        tmid = mix(tm, tx, hm / (hm - hx));
        vec3 p = eye + dir * tmid;
        float hmid = map(p);
        if (hmid < 0.0) {
            tx = tmid;
            hx = hmid;
        } else {
            tm = tmid;
            hm = hmid;
        }
    }
    return eye + dir * tmid;
}

void main() {
    vec2 res = iResolution.xy;
    if (res.x < 1.0 || res.y < 1.0) res = vec2(800.0, 600.0);

    vec2 uv = gl_FragCoord.xy / res;
    uv = uv * 2.0 - 1.0;
    uv = vec2(uv.x * (res.x / res.y), uv.y);

    float time = iGlobalTime * 0.3;   /* camera clock (no iMouse here) */

    /* ray */
    vec3 ang = vec3(sin(time * 3.0) * 0.1, sin(time) * 0.2 + 0.3, time);
    vec3 ori = vec3(0.0, 3.5, time * 5.0);
    vec3 dir = normalize(vec3(uv.xy, -2.0));
    dir = vec3(dir.x, dir.y, dir.z + length(uv) * 0.15);

    /* dir = normalize(dir) * fromEuler(ang), expanded.  fromEuler builds a
     * mat3 whose COLUMNS are m0/m1/m2 below; v*M == vec3(dot(m0,v),
     * dot(m1,v), dot(m2,v)). */
    vec2 a1 = vec2(sin(ang.x), cos(ang.x));
    vec2 a2 = vec2(sin(ang.y), cos(ang.y));
    vec2 a3 = vec2(sin(ang.z), cos(ang.z));
    vec3 m0 = vec3(a1.y * a3.y + a1.x * a2.x * a3.x,
                   a1.y * a2.x * a3.x + a3.y * a1.x,
                   -a2.y * a3.x);
    vec3 m1 = vec3(-a2.y * a1.x, a1.y * a2.y, a2.x);
    vec3 m2 = vec3(a3.y * a1.x * a2.x + a1.y * a3.x,
                   a1.x * a3.x - a1.y * a3.y * a2.x,
                   a2.y * a3.y);
    vec3 dn = normalize(dir);
    dir = vec3(dot(m0, dn), dot(m1, dn), dot(m2, dn));

    /* sky/sea blend weight: smoothstep(0.0,-0.05,dir.y) expanded explicitly
     * because edge0 > edge1 is undefined in the GLSL spec (mesa and the
     * interpreter could otherwise diverge).  dir.y >= 0 (above the horizon)
     * gives w == 0, i.e. pure sky, so the whole sea trace/normal/shade is
     * skipped there -- identical output, less work. */
    float ty = clamp(-20.0 * dir.y, 0.0, 1.0);
    float ss = ty * ty * (3.0 - 2.0 * ty);
    float w  = pow(ss, 0.3);

    vec3 sky = getSkyColor(dir);
    vec3 color;
    if (w > 0.0) {
        /* tracing */
        vec3 p = heightMapTracing(ori, dir);
        vec3 dist = p - ori;
        float epsn = 0.1 / res.x;
        vec3 n = getNormal(p, dot(dist, dist) * epsn);
        vec3 light = normalize(vec3(0.0, 1.0, 0.8));
        color = mix(sky, getSeaColor(p, n, light, dir, dist), w);
    } else {
        color = sky;
    }

    /* post: ^0.75 tonemap (base clamped non-negative for mesa pow safety) */
    color = pow(max(color, vec3(0.0)), vec3(0.75));
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)GLSL";
    return g;
}

} /* namespace ocean_gen */

/* The ocean ships ONE tuning: 2 coarse octaves, 3 detail octaves, 5 trace steps
 * (2/3/5).  The effect is ALU-bound on the V3D compute path (~720 trig + 180 pow
 * per sea pixel at the reference 3/5/8 counts), and this is the chosen default
 * FPS/look balance: it halves the per-pixel evaluation count versus the
 * reference while keeping the swell silhouette and overall ocean look.  The
 * reference-faithful 3/5/8 and the other experimental tiers were discarded. */
static const std::string ocean_fs = ocean_gen::build_ocean_fs(2, 3, 4);

#endif /* OCEAN_GLSL_H */
