/*
 * shadertoy_glsl.h -- GLSL ES 1.00 ports of the 11 shadertoy effects.
 *
 * These sources mirror the C frag_func implementations in shadertoy.cpp so the
 * same effects can be compiled by v3d_compile_glsl() and dispatched on the
 * VideoCore VII GPU (raspi5 / V3D 7.1).
 *
 * Convention (see plan todo s2):
 *   - #version 100, precision highp float.
 *   - Uniforms: iGlobalTime (float, seconds), iResolution (vec3, .xy = w/h),
 *     iChannel0..2 (sampler2D) where used.
 *   - gl_FragCoord.xy replaces builtins->gl_FragCoord; gl_FragColor replaces
 *     *(vec4*)&builtins->gl_FragColor.
 *   - texture2D(sampler, vec2) replaces the C texture2D(channel, u, v) helper.
 *   - float loop counters in the_cave are converted to int loops (ES 1.00
 *     requires loop indices to be integer with constant bounds).
 *   - pow(x, even-int) on a possibly-negative base uses abs(x) (GLSL pow
 *     requires a non-negative base; even exponent => identical result).
 *
 * The shared vertex shader is a passthrough of the full-screen quad position
 * (aPosition), matching the points[] buffer in shadertoy.cpp main().
 *
 * This header uses C++ raw string literals and MUST be included from a .cpp.
 */
#ifndef SHADERTOY_GLSL_H
#define SHADERTOY_GLSL_H

#ifndef __cplusplus
#error "shadertoy_glsl.h requires C++ (raw string literals)"
#endif

/* Shared passthrough vertex shader for the full-screen quad. */
static const char* shadertoy_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

/* Common fragment preamble: version, precision, standard uniforms. */
#define SHADERTOY_FS_PREAMBLE \
	"#version 100\n" \
	"precision highp float;\n" \
	"uniform float iGlobalTime;\n" \
	"uniform vec3 iResolution;\n"

#define SHADERTOY_CH0 "uniform sampler2D iChannel0;\n"
#define SHADERTOY_CH1 "uniform sampler2D iChannel1;\n"
#define SHADERTOY_CH2 "uniform sampler2D iChannel2;\n"

/* ------------------------------------------------------------------ */
/* [0] graphing_lines_fs                                               */
/* ------------------------------------------------------------------ */
static const char* shadertoy_graphing_lines_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
void main() {
    vec2 frag = gl_FragCoord.xy;
    frag.x /= iResolution.x;
    frag.y /= iResolution.y;
    frag = frag * 2.0;
    frag = frag - vec2(1.0, 1.0);

    float fragx2 = frag.x * frag.x;
    float x2 = frag.y - fragx2;
    float x3 = frag.y - fragx2 * frag.x + 0.25 * frag.x;
    float x4 = frag.y - fragx2 * fragx2;
    float incr = 2.0 / iResolution.y;

    if (x2 >= -incr && x2 <= incr) {
        gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    } else if (x3 >= -incr && x3 <= incr) {
        gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
    } else if (x4 >= -incr && x4 <= incr) {
        gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [1] graphing_fs                                                     */
/* ------------------------------------------------------------------ */
static const char* shadertoy_graphing_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
void main() {
    vec2 frag = gl_FragCoord.xy;
    frag.x /= iResolution.x;
    frag.y /= iResolution.y;
    frag = frag * 2.0;
    frag = frag - vec2(1.0, 1.0);

    float fragx2 = frag.x * frag.x;
    float x2 = frag.y - fragx2;

    if (x2 > 0.0)
        gl_FragColor = vec4(x2, 0.0, 0.0, 1.0);
    else
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [2] my_tunnel_fs                                                    */
/* ------------------------------------------------------------------ */
static const char* shadertoy_my_tunnel_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
void main() {
    float globaltime = iGlobalTime;
    vec2 frag = gl_FragCoord.xy;
    frag.x /= iResolution.x;
    frag.y /= iResolution.y;
    frag = frag * 2.0;
    frag = frag - vec2(1.0, 1.0);

    float r = pow(pow(abs(frag.x), 16.0) + pow(abs(frag.y), 16.0), 1.0 / 16.0);
    float wave = (0.5 * sin(10.0 * globaltime * (1.0 - r)) + 0.5);

    gl_FragColor = vec4(r * wave, 0.0, r * (1.0 - wave), 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [3] running_in_the_night_fs  (iChannel0 = tex2)                      */
/* ------------------------------------------------------------------ */
static const char* shadertoy_running_in_the_night_fs =
	SHADERTOY_FS_PREAMBLE
	SHADERTOY_CH0
	R"GLSL(
void main() {
    float time  = 45.0 * 3.14159 / 180.0 + cos(iGlobalTime * 12.0) / 40.0;
    float time1 = 45.0 * 3.14159 / 180.0 + cos(iGlobalTime * 22.0) / 30.0;

    vec2 uv = gl_FragCoord.xy * (2.0 / iResolution.x) - vec2(1.0, 1.0);
    uv.y = uv.y * (iResolution.y / iResolution.x);

    if (uv.y < 0.0) {
        vec2 rot = vec2(cos(time), sin(time));
        vec2 mat;
        mat.x = (uv.x * rot.x + (uv.y - 1.0) * rot.y);
        mat.y = ((uv.y - 1.0) * rot.x - uv.x * rot.y);
        vec2 tex;
        tex.x = mat.x * time1 / uv.y + iGlobalTime * 2.0;
        tex.y = mat.y * time1 / uv.y + iGlobalTime * 2.0;

        vec4 tmp = texture2D(iChannel0, tex * 2.0);
        gl_FragColor = vec4(tmp.x * (-uv.y), tmp.y * (-uv.y), tmp.z * (-uv.y), 1.0);
    } else {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [4] square_tunnel_fs  (iChannel0 = tex0)                             */
/* ------------------------------------------------------------------ */
static const char* shadertoy_square_tunnel_fs =
	SHADERTOY_FS_PREAMBLE
	SHADERTOY_CH0
	R"GLSL(
void main() {
    float globaltime = iGlobalTime;
    vec2 resolution = iResolution.xy;
    vec2 frag = gl_FragCoord.xy;

    vec2 p = frag * 2.0 - resolution;
    p = p * (1.0 / resolution.y);

    float a = atan(p.y, p.x);
    float r = pow(pow(p.x * p.x, 16.0) + pow(p.y * p.y, 16.0), 1.0 / 32.0);

    vec2 uv = vec2(0.5 / r + 0.5 * globaltime, a / 3.1416);

    vec4 tmp = texture2D(iChannel0, uv);
    gl_FragColor = vec4(tmp.x * r, tmp.y * r, tmp.z * r, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [5] deform_tunnel_fs  (iChannel0 = tex2, iChannel1 = tex0)           */
/* ------------------------------------------------------------------ */
static const char* shadertoy_deform_tunnel_fs =
	SHADERTOY_FS_PREAMBLE
	SHADERTOY_CH0
	SHADERTOY_CH1
	R"GLSL(
void main() {
    float globaltime = iGlobalTime;
    vec2 resolution = iResolution.xy;
    vec2 frag = gl_FragCoord.xy;

    vec2 p = frag * 2.0 * (1.0 / resolution.x) - vec2(1.0, 1.0);
    p.y = p.y * (resolution.y / resolution.x);

    float r = pow(pow(p.x * p.x, 16.0) + pow(p.y * p.y, 16.0), 1.0 / 32.0);

    vec2 uv;
    uv.x = 0.5 * globaltime + 0.5 / r;
    uv.y = atan(p.y, p.x) / 3.1416;

    float h = sin(32.0 * uv.y);
    uv.x += 0.85 * smoothstep(-0.1, 0.1, h);

    vec4 ch1_ = texture2D(iChannel1, uv * 2.0);
    vec4 ch0_ = texture2D(iChannel0, uv);

    vec3 ch0 = ch0_.xyz;
    vec3 ch1 = ch1_.xyz;
    float aa = smoothstep(0.9, 1.1, abs(p.x / p.y));
    vec3 col = mix(ch1, ch0, aa);

    r *= 1.0 - 0.3 * (smoothstep(0.0, 0.3, h) - smoothstep(0.3, 0.96, h));

    gl_FragColor = vec4(col.x * r * r * 1.2, col.y * r * r * 1.2, col.z * r * r * 1.2, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [6] tileable_water_caustic_fs                                       */
/* ------------------------------------------------------------------ */
static const char* shadertoy_tileable_water_caustic_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
const float TAU = 6.28318530718;
void main() {
    float time = iGlobalTime * 0.5 + 23.0;
    vec2 sp;
    sp.x = gl_FragCoord.x / iResolution.x;
    sp.y = gl_FragCoord.y / iResolution.y;

    vec2 p = sp * TAU - vec2(250.0, 250.0);
    vec2 ic = p;
    float c = 1.0;
    float inten = 0.005;

    for (int n = 0; n < 5; n++) {
        float t = time * (1.0 - (3.5 / float(n + 1)));
        ic.x = p.x + cos(t - ic.x) + sin(t + ic.y);
        ic.y = p.y + sin(t - ic.y) + cos(t + ic.x);
        vec2 len_vec = vec2(
            p.x / (sin(ic.x + t) / inten),
            p.y / (cos(ic.y + t) / inten)
        );
        c += 1.0 / length(len_vec);
    }
    c /= 5.0;
    c = 1.2 - pow(c, 1.2);

    float colour_val = pow(abs(c), 6.0);
    vec3 colour = vec3(colour_val, colour_val, colour_val);

    vec3 final_col = colour + vec3(0.0, 0.35, 0.5);
    final_col = clamp(final_col, 0.0, 1.0);

    gl_FragColor = vec4(final_col, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [7] iqs_eyeball                                                     */
/* ------------------------------------------------------------------ */
static const char* shadertoy_iqs_eyeball_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
float eye_hash(float n) {
    float x = sin(n) * 43758.5453123;
    return x - floor(x);
}

float eye_noise(vec2 x) {
    vec2 p = floor(x);
    vec2 f = x - p;
    f = f * f * (vec2(3.0, 3.0) - 2.0 * f);
    float n = p.x + p.y * 57.0;
    return mix(
        mix(eye_hash(n + 0.0), eye_hash(n + 1.0), f.x),
        mix(eye_hash(n + 57.0), eye_hash(n + 58.0), f.x),
        f.y
    );
}

float fbm(vec2 p) {
    float f = 0.0;
    f += 0.5000 * eye_noise(p);
    f += 0.2500 * eye_noise(p * 2.0);
    f += 0.1250 * eye_noise(p * 4.0);
    f += 0.0625 * eye_noise(p * 8.0);
    return f / 0.9375;
}

void main() {
    float time = iGlobalTime;
    vec2 uv;
    uv.x = gl_FragCoord.x / iResolution.x;
    uv.y = gl_FragCoord.y / iResolution.y;

    vec2 p;
    p.x = -1.0 + 2.0 * uv.x;
    p.y = -1.0 + 2.0 * uv.y;
    p.x *= iResolution.x / iResolution.y;

    float r = sqrt(dot(p, p));
    float a = atan(p.y, p.x);

    vec3 bg_col = vec3(1.0, 1.0, 1.0);
    vec3 col = bg_col;

    float ss = 0.5 + 0.5 * sin(time);
    float anim = 1.0 + 0.1 * ss * clamp(1.0 - r, 0.0, 1.0);
    r *= anim;

    if (r < 0.8) {
        col = vec3(0.0, 0.3, 0.4);

        float f = fbm(p * 5.0);
        col = mix(col, vec3(0.2, 0.5, 0.4), f);

        f = 1.0 - smoothstep(0.2, 0.5, r);
        col = mix(col, vec3(0.9, 0.6, 0.2), f);

        a += 0.05 * fbm(p * 20.0);

        f = smoothstep(0.3, 1.0, fbm(vec2(10.0 * r, 20.0 * a)));
        col = mix(col, vec3(1.0, 1.0, 1.0), f);

        f = smoothstep(0.4, 0.9, fbm(vec2(10.0 * r, 15.0 * a)));
        col = col * (1.0 - 0.5 * f);

        f = smoothstep(0.6, 0.8, r);
        col = col * (1.0 - 0.5 * f);

        f = smoothstep(0.2, 0.25, r);
        col = col * f;

        vec2 pp = p - vec2(0.24, 0.2);
        f = 1.0 - smoothstep(0.0, 0.3, sqrt(dot(pp, pp)));
        col = col + vec3(1.0, 0.9, 0.8) * (f * 0.8);

        f = smoothstep(0.75, 0.8, r);
        col = mix(col, bg_col, f);
    }

    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [8] voronoise_fs                                                    */
/* ------------------------------------------------------------------ */
static const char* shadertoy_voronoise_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
vec3 hash3(vec2 p) {
    vec3 q = vec3(
        dot(p, vec2(127.1, 311.7)),
        dot(p, vec2(269.5, 183.3)),
        dot(p, vec2(419.2, 371.9))
    );
    return fract(vec3(sin(q.x), sin(q.y), sin(q.z)) * 43758.5453);
}

float iqnoise(vec2 x, float u, float v) {
    vec2 p = floor(x);
    vec2 f = fract(x);

    float k = 1.0 + 63.0 * pow(1.0 - v, 4.0);

    float va = 0.0;
    float wt = 0.0;
    for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++) {
        vec2 g = vec2(float(i), float(j));
        vec3 o = hash3(p + g) * vec3(u, u, 1.0);
        vec2 r = g - f + o.xy;
        float d = dot(r, r);
        float ww = pow(1.0 - smoothstep(0.0, 1.414, sqrt(d)), k);
        va += o.z * ww;
        wt += ww;
    }
    return va / wt;
}

void main() {
    vec2 uv;
    uv.x = gl_FragCoord.x / iResolution.x;
    uv.y = gl_FragCoord.y / iResolution.x;

    vec2 p;
    p.x = 0.5 - 0.5 * sin(iGlobalTime * 1.01);
    p.y = 0.5 - 0.5 * sin(iGlobalTime * 1.71);

    float t1 = 3.0 - 2.0 * p.x;
    float t2 = 3.0 - 2.0 * p.y;
    p.x = p.x * p.x * t1;
    p.y = p.y * p.y * t2;
    p.x = p.x * p.x * (3.0 - 2.0 * p.x);
    p.y = p.y * p.y * (3.0 - 2.0 * p.y);
    p.x = p.x * p.x * (3.0 - 2.0 * p.x);
    p.y = p.y * p.y * (3.0 - 2.0 * p.y);

    float f = iqnoise(uv * 24.0, p.x, p.y);

    gl_FragColor = vec4(f, f, f, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [9] the_cave_fs  (iChannel0 = tex6, iChannel1 = tex1, iChannel2 = tex9) */
/* ------------------------------------------------------------------ */
static const char* shadertoy_the_cave_fs =
	SHADERTOY_FS_PREAMBLE
	SHADERTOY_CH0
	SHADERTOY_CH1
	SHADERTOY_CH2
	R"GLSL(
const vec2 cama = vec2(-2.6943, 3.0483);
const vec2 camb = vec2(0.2516, 0.1749);
const vec2 camc = vec2(-3.7902, 2.4478);
const vec2 camd = vec2(0.0865, -0.1664);

const vec2 lighta = vec2(1.4301, 4.0985);
const vec2 lightb = vec2(-0.1276, 0.2347);
const vec2 lightc = vec2(-2.2655, 1.5066);
const vec2 lightd = vec2(-0.1284, 0.0731);

vec2 Position(float z, vec2 a, vec2 b, vec2 c, vec2 d) {
    return vec2(sin(z * a.x), sin(z * a.y)) * b.x + vec2(cos(z * c.x), cos(z * c.y)) * d.x;
}

vec3 Position3D(float time, vec2 a, vec2 b, vec2 c, vec2 d) {
    vec2 pos = Position(time, a, b, c, d);
    return vec3(pos.x, pos.y, time);
}

float Distance_(vec3 p, vec2 a, vec2 b, vec2 c, vec2 d, vec2 e, float r) {
    vec2 pos = Position(p.z, a, b, c, d);
    float radius = max(5.0, r + sin(p.z * e.x) * e.y) / 10000.0;
    vec2 diff = p.xy - pos;
    return radius / dot(diff, diff);
}

float Dist2D(vec3 pos) {
    float d = 0.0;
    d += Distance_(pos, cama, camb, camc, camd, vec2(2.1913, 15.4634), 70.0000);
    d += Distance_(pos, lighta, lightb, lightc, lightd, vec2(0.3814, 12.7206), 17.0590);
    return d;
}

void main() {
    float globaltime = iGlobalTime;

    float time = globaltime / 12.0 + 291.0;

    vec2 p1 = Position(time + 0.05, cama, camb, camc, camd);
    vec3 Pos = Position3D(time, cama, camb, camc, camd);

    vec3 CamDir = normalize(vec3(p1.x - Pos.x, -p1.y + Pos.y, 0.1));
    vec3 CamRight = normalize(cross(CamDir, vec3(0.0, 1.0, 0.0)));
    vec3 CamUp = normalize(cross(CamRight, CamDir));

    vec2 uv = gl_FragCoord.xy * (2.0 / iResolution.x) - vec2(1.0, 1.0);
    uv.y = uv.y * (iResolution.y / iResolution.x);
    float aspect = iResolution.x / iResolution.y;

    vec3 Dir = normalize(vec3(uv.x * aspect, uv.y, 1.0));
    Dir = CamRight * Dir.x + CamUp * Dir.y + CamDir * Dir.z;

    float fade = 0.0;
    const float numit = 75.0;
    const float threshold = 1.20;
    const float scale = 1.5;

    vec3 Posm1 = Pos;

    for (int x = 0; x < 75; x++) {
        if (Dist2D(Pos) < threshold) {
            fade = 1.0 - float(x) / numit;
            break;
        }
        Posm1 = Pos;
        Pos = Pos + Dir * (scale / numit);
    }

    for (int x = 0; x < 6; x++) {
        vec3 p2 = (Posm1 + Pos) * 0.5;
        if (Dist2D(p2) < threshold)
            Pos = p2;
        else
            Posm1 = p2;
    }

    vec3 lp = Position3D(time + 0.5, cama, camb, camc, camd);
    vec3 ld = lp - Pos;
    float lv = 1.0;

    for (int x = 1; x < 15; x++) {
        if (Dist2D(Pos + ld * (float(x) / 15.0)) < threshold) {
            lv = 0.0;
            break;
        }
    }

    vec3 tuv = vec3(Pos.x * 3.0, Pos.y * 3.0, Pos.z * 1.5);

    vec4 tx_ = texture2D(iChannel0, vec2(tuv.y, tuv.z));
    vec4 ty_ = texture2D(iChannel1, vec2(tuv.x, tuv.z));
    vec4 tz_ = texture2D(iChannel2, vec2(tuv.x, tuv.y));

    vec4 col = vec4(
        (tx_.x + ty_.x + tz_.x) * 0.33,
        (tx_.y + ty_.y + tz_.y) * 0.33,
        (tx_.z + ty_.z + tz_.z) * 0.33,
        1.0
    );

    vec4 diff = vec4(lv * 1.2 + 0.2, lv * 1.2 + 0.2, lv * 1.2 + 0.2, 1.0);

    float ff = min(1.0, fade * 10.0);
    gl_FragColor = vec4(col.x * diff.x * ff, col.y * diff.y * ff, col.z * diff.z * ff, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* [10] flame_fs                                                       */
/* ------------------------------------------------------------------ */
static const char* shadertoy_flame_fs =
	SHADERTOY_FS_PREAMBLE
	R"GLSL(
float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 v1 = vec3(1.0, 57.0, 21.0);
    vec3 v2 = vec3(0.0, 57.0, 21.0);
    vec3 v3 = vec3(78.0, 78.0, 78.0);
    vec4 a = vec4(dot(i, v1), 0.0, 0.0, 0.0) + vec4(v2.x, v2.y, v2.z, v3.x);

    vec3 f = (p - i) * acos(-1.0);
    f.x = cos(f.x) * (-0.5) + 0.5;
    f.y = cos(f.y) * (-0.5) + 0.5;
    f.z = cos(f.z) * (-0.5) + 0.5;

    float a1 = sin(cos(a.x) * a.x);
    float a2 = sin(cos(a.y) * a.y);
    float a3 = sin(cos(1.0 + a.z) * (1.0 + a.z));
    float a4 = sin(cos(1.0 + a.w) * (1.0 + a.w));

    float m1 = mix(a1, a3, f.x);
    float m2 = mix(a2, a4, f.x);

    return mix(m1, m2, f.z);
}

float sphere(vec3 p, vec4 spr) {
    return length(spr.xyz - p) - spr.w;
}

float flame_func(vec3 p) {
    vec3 scaled_p = vec3(p.x, p.y * 0.5, p.z);
    vec4 spr = vec4(0.0, -1.0, 0.0, 1.0);
    float d = sphere(scaled_p, spr);
    return d + (noise(p + vec3(0.0, iGlobalTime * 2.0, 0.0)) + noise(p * 3.0) * 0.5) * 0.25 * p.y;
}

float scene(vec3 p) {
    return min(100.0 - length(p), abs(flame_func(p)));
}

vec4 raymarch(vec3 org, vec3 dir) {
    float d = 0.0, glow = 0.0, eps = 0.02;
    vec3 p = org;
    bool glowed = false;

    for (int i = 0; i < 64; i++) {
        d = scene(p) + eps;
        p = p + dir * d;
        if (d > eps) {
            if (flame_func(p) < 0.0)
                glowed = true;
            if (glowed)
                glow = float(i) / 64.0;
        }
    }
    return vec4(p.x, p.y, p.z, glow);
}

void main() {
    vec2 v;
    v.x = -1.0 + 2.0 * gl_FragCoord.x / iResolution.x;
    v.y = -1.0 + 2.0 * gl_FragCoord.y / iResolution.y;
    v.x *= iResolution.x / iResolution.y;

    vec3 org = vec3(0.0, -2.0, 4.0);
    vec3 dir = normalize(vec3(v.x * 1.6, -v.y, -1.5));

    vec4 p = raymarch(org, dir);
    float glow = p.w;

    vec4 col1 = vec4(1.0, 0.5, 0.1, 1.0);
    vec4 col2 = vec4(0.1, 0.5, 1.0, 1.0);
    vec4 col = mix(col1, col2, p.y * 0.02 + 0.4);

    float intensity = pow(glow * 2.0, 4.0);
    if (intensity > 1.0) intensity = 1.0;
    gl_FragColor = vec4(col.x * intensity, col.y * intensity, col.z * intensity, 1.0);
}
)GLSL";

/* ------------------------------------------------------------------ */
/* Ordered tables matching frag_funcs[] display order in shadertoy.cpp. */
/* ------------------------------------------------------------------ */
#define SHADERTOY_NUM_SHADERS 11

static const char* shadertoy_fs_glsl[SHADERTOY_NUM_SHADERS] = {
	shadertoy_graphing_lines_fs,        /* [0] */
	shadertoy_graphing_fs,              /* [1] */
	shadertoy_my_tunnel_fs,             /* [2] */
	shadertoy_running_in_the_night_fs,  /* [3] iChannel0=tex2 */
	shadertoy_square_tunnel_fs,         /* [4] iChannel0=tex0 */
	shadertoy_deform_tunnel_fs,         /* [5] iChannel0=tex2, iChannel1=tex0 */
	shadertoy_tileable_water_caustic_fs,/* [6] */
	shadertoy_iqs_eyeball_fs,           /* [7] */
	shadertoy_voronoise_fs,             /* [8] */
	shadertoy_the_cave_fs,              /* [9] iChannel0=tex6, iChannel1=tex1, iChannel2=tex9 */
	shadertoy_flame_fs                  /* [10] */
};

/* Number of texture channels (iChannelN) each effect consumes. */
static const int shadertoy_fs_channels[SHADERTOY_NUM_SHADERS] = {
	0, 0, 0, 1, 1, 2, 0, 0, 0, 3, 0
};

#endif /* SHADERTOY_GLSL_H */
