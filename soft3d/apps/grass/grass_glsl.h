/*
 * grass_glsl.h -- GLES2 / GLSL ES 1.00 shaders for the "Grass" demo.
 *
 * A faithful re-implementation of the ShaderToy effect "Windyplains" by kuvkar
 * (https://www.shadertoy.com/view/ltXXRM): a first-person walk along a worn
 * dirt path through a wind-bent grass field rendered from a height map, under
 * scrolling fbm clouds that cast moving shadows, with distance fog, a bright
 * horizon and a sun halo.  The reference is a SINGLE-pass effect (only the
 * Image pass exists -- there are no Buffer/Cubemap/Sound passes), so it maps
 * directly onto this engine's fullscreen fragment-shader route with no feedback
 * buffers.
 *
 * Assets are the reference's own two noise images, bundled under res/ and
 * sampled exactly as the original does:
 *   iChannel0 = "Gray Noise Small"  (64x64, 1ch)  -- grass height + fbm clouds
 *   iChannel2 = "RGBA Noise Medium" (256x256,4ch) -- straw tint + colour patches
 * Both are uploaded with GL_REPEAT wrap (required by the engine's GPU tiled
 * texture slot) and NEAREST filtering; the on-device V3D compiled sampler is
 * fixed at NEAREST/REPEAT, so the CPU path uses the same to keep the two
 * routes pixel-consistent.  The reference samples its channels with a mipmap
 * filter, so the shader emulates bilinear + mip-fade by hand (texB) on top
 * of the NEAREST hardware taps.
 *
 * GPU route (raspi5 / PGL_G3D): the vertex shader is a passthrough with no
 * uMVP and this fragment shader reads the window position, so the engine
 * recognises the program as a fullscreen effect and dispatches it as a VideoCore
 * VII compute kernel (the same transparent path waterdrop/ocean/shadertoy use).
 * Rays that look above the horizon skip the march entirely (rd.y < 0 gate), so
 * the sky half of the frame is cheap.  CPU route: the engine's interpreter runs
 * the identical GLSL.
 *
 * Engine-subset constraints honoured on BOTH paths:
 *   - only the reserved names gl_FragCoord / gl_FragColor / gl_Position appear
 *   - no mat2/mat3 (rotations expanded by hand), no out-parameters, no structs
 *   - no integer division; pow()/exp() bases guarded with max()/epsilons
 *   - reversed-edge smoothstep expanded manually (undefined on mesa)
 *   - loops with literal bounds (the march bound is injected as a literal by
 *     grass.cpp replacing the @@STEPS@@ token before compile)
 */

#ifndef GRASS_GLSL_H
#define GRASS_GLSL_H

#ifndef __cplusplus
#error "grass_glsl.h requires C++ (raw string literals)"
#endif

/* Passthrough vertex shader: a full-screen clip-space quad.  It intentionally
 * references no uMVP / matrices so the engine flags the program as a fullscreen
 * effect and routes the fragment shader to the GPU compute path. */
static const char* grass_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

#define GRASS_FS_PREAMBLE \
	"#version 100\n" \
	"precision highp float;\n"

/* ------------------------------------------------------------------ *
 * Windyplains fragment shader (single pass).
 *
 * Uniforms:
 *   iGlobalTime  float      seconds since start (animation clock)
 *   iResolution  vec3       viewport size (xy used)
 *   iMouse       vec4       xy = look pointer in pixels (y down), zw unused
 *   iChannel0    sampler2D  Gray Noise Small  (grass height + clouds)
 *   iChannel2    sampler2D  RGBA Noise Medium (straw tint + colour patches)
 *
 * The march step bound is the literal that replaces @@STEPS@@ at build time.
 * ------------------------------------------------------------------ */
static const char* grass_fs_glsl_tmpl = GRASS_FS_PREAMBLE R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;
uniform vec4  iMouse;
uniform sampler2D iChannel0;
uniform sampler2D iChannel2;

/* Grass blade height (world units). */
const float H = 0.2;

/* ---- hand-expanded rotations (the engine subset has no mat2/mat3) ----
 * Semantics match the reference's rd *= rotx(a) / rd *= roty(a): the author's
 * row matrices act as written, so a positive angle pitches DOWN onto the
 * grass -- exactly what the reference's untouched-mouse default view shows
 * (verified against zero-interaction canvas captures), and dragging the
 * mouse steers the same way as on shadertoy. */
vec3 rotX(vec3 v, float a) {
    float s = sin(a);
    float c = cos(a);
    return vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}
vec3 rotY(vec3 v, float a) {
    float s = sin(a);
    float c = cos(a);
    return vec3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
}
vec2 rot2d(vec2 p, float a) {
    float s = sin(a);
    float c = cos(a);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

/* smoothstep with VARIABLE / REVERSED edges, expanded by hand: the reference
 * calls smoothstep(xc, -0.4, 0.0) and smoothstep(0.5, -0.4, rd.y) with the
 * spatial value in edge0, which is undefined on mesa, so reproduce GLSL's
 * clamp((x-e0)/(e1-e0)) + hermite formula exactly with a zero-divisor guard. */
float sstep(float e0, float e1, float x) {
    float d = e1 - e0;
    float t = (abs(d) < 1e-6) ? 0.0 : clamp((x - e0) / d, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/* 8-tap height ring averaging the march (the reference's AA struct, flattened
 * to globals because the engine subset has no structs), plus the warped hit
 * point and the traced colour the reference returns through inout args. */
float g_aa_a, g_aa_b, g_aa_c, g_aa_d, g_aa_e, g_aa_f, g_aa_g, g_aa_h;
vec3  g_warpedRp;
vec3  g_col;
vec3  g_rp;
/* cached low-freq height modulation (bilinear, refreshed every 0.25 world
 * units along the ray instead of every step: the field is smooth at the
 * 0.025 scale, so holding it costs nothing visible and saves 4 taps/step) */
float g_mod, g_modl;
/* gated per-step transcendentals: the path banks, wind phase, blade-bend
 * rotation and step-growth exp all advance far slower than the march in its
 * early (dense) phase, so each is recomputed only once its own argument
 * moves past a sub-texel tolerance instead of every iteration. */
float g_s1, g_s2, g_t, g_sn, g_cs, g_expf;
float g_px, g_pz, g_ang, g_lx;
float g_lod, g_rdy;

void aa_rotate(float v) {
    g_aa_h = g_aa_g; g_aa_g = g_aa_f; g_aa_f = g_aa_e; g_aa_e = g_aa_d;
    g_aa_d = g_aa_c; g_aa_c = g_aa_b; g_aa_b = g_aa_a; g_aa_a = v;
}
float aa_avg() {
    float f1 = 0.7; float f2 = 0.5; float f3 = 0.8;
    float a = g_aa_a + g_aa_b;
    float b = g_aa_c + g_aa_d;
    float c = g_aa_e + g_aa_f;
    float d = g_aa_g + g_aa_h;
    float a1 = mix(a, b, 1.0 - f1);
    float a2 = mix(c, d, 1.0 - f2);
    float a3 = mix(a1, a2, 1.0 - f3);
    return a3 * 0.5;
}

float flterF(float f, float a) {
    f = clamp(f - a, 0.0, 1.0);
    return f / (1.0 - a);
}

/* Mip-emulating bilinear tap.  The reference samples both channels with
 * ShaderToy's mipmap filter; the on-device compiled sampler is fixed at
 * NEAREST, so filter by hand: four NEAREST taps at texel centres (the
 * hardware REPEAT wrap covers the edges) give the within-mip bilinear, and
 * once the pixel footprint exceeds a texel the result fades to the noise
 * mean exactly like a deep mip level would.  lod <= 0 is plain bilinear --
 * what the reference's bias(-100) height tap gets. */
vec4 texB(sampler2D s, vec2 uv, vec2 sz, float lod) {
    vec2 t = uv * sz;
    vec2 i = floor(t);
    vec2 f = t - i;
    vec2 c = (i + 0.5) / sz;
    vec2 ox = vec2(1.0 / sz.x, 0.0);
    vec2 oy = vec2(0.0, 1.0 / sz.y);
    vec4 a = texture2D(s, c);
    vec4 b = texture2D(s, c + ox);
    vec4 d = texture2D(s, c + oy);
    vec4 e = texture2D(s, c + ox + oy);
    vec4 v = mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
    return mix(v, vec4(0.5), clamp((lod - 1.0) / 3.0, 0.0, 1.0));
}

/* 5-octave value fbm from the gray-noise red channel (x1/2/4/8/32);
 * lb is the log2 pixel footprint (in texels) of the base octave. */
float fbm(vec2 uv, float lb) {
    float f = 0.0;
    f += 0.50000 * (texB(iChannel0, uv, vec2(64.0), lb).r - 0.5);
    f += 0.25000 * (texB(iChannel0, uv * 2.0, vec2(64.0), lb + 1.0).r - 0.5);
    f += 0.12500 * (texB(iChannel0, uv * 4.0, vec2(64.0), lb + 2.0).r - 0.5);
    /* octaves whose mip-fade weight is already 1 contribute exactly zero */
    if (lb < 1.0)
        f += 0.06250 * (texB(iChannel0, uv * 8.0, vec2(64.0), lb + 3.0).r - 0.5);
    if (lb < -1.0)
        f += 0.03125 * (texB(iChannel0, uv * 32.0, vec2(64.0), lb + 5.0).r - 0.5);
    return f + 0.5;
}

/* Terrain / grass height field: wind domain-warp, the two raised banks that
 * groove the worn path (reversed-edge smoothstep as the reference writes it),
 * the noise height optionally averaged through the AA ring, and a low-freq
 * modulation.  Records the warped point for the hit-shading lookups.  All
 * smooth per-step quantities arrive through the gated march_cache(). */
void march_cache(vec3 rp, float l, float tol) {
    if (abs(rp.x - g_px) > tol || abs(rp.z - g_pz) > tol) {
        g_px = rp.x; g_pz = rp.z;
        float gt = iGlobalTime * 0.9;
        g_t = (sin(gt + rp.x * 1.2) + sin(gt + rp.z * 1.4)) * 0.25;
        g_s1 = 1.0 - sstep(rp.x + sin(rp.z * 3.0) * 0.1 + sin(rp.z * 5.0) * 0.12,
                           -0.4, 0.0);
        g_s2 = 1.0 - sstep(rp.x - sin(rp.z * 4.4) * 0.1 + sin(rp.z * 14.0) * 0.04,
                            0.4, 0.0);
    }
    float ang = rp.y * 20.0 * g_t;
    if (abs(ang - g_ang) > 0.15) { g_ang = ang; g_sn = sin(ang); g_cs = cos(ang); }
    if (l - g_lx > 0.25) {
        g_lx = l;
        g_expf = exp(max(0.0, (l - 0.5) * 0.2));
        /* Height-tap mip level: the reference's channel0 is mipmap-filtered,
         * so a pixel whose footprint spans N texels reads a blurred level.
         * Footprint = 416 texels/world * world-per-pixel (arc length l times
         * the pixel angle, stretched by the ray's ground incidence). */
        g_lod = log2(max(416.0 * l * (2.0 / iResolution.x) /
                         max(abs(g_rdy), 0.05), 1.0));
    }
}

float map(vec3 rp, int useAA) {
    /* rot2d(vec2(0,5),ang) = (-5*sin, 5*cos), folded into the warp scales */
    rp.x += 0.2 * g_sn;
    rp.y -= 0.025 * g_cs;
    vec2 uv = rp.xz * 6.5;
    rp.y += (g_s1 + g_s2) * 0.03;

    /* The reference samples iChannel0 with linear filtering: the height must
     * vary continuously per pixel or the depth fade turns every texel jump
     * into a black speckle.  Four NEAREST taps at texel centres are exactly
     * the hardware bilinear (the compiled sampler stays NEAREST/REPEAT). */
    float h = texB(iChannel0, uv, vec2(64.0), g_lod).r;
    if (useAA == 1) {
        aa_rotate(h);
        h = aa_avg();
    }
    h *= mix(g_mod, 1.0, 0.7);
    h *= H;
    g_warpedRp = rp;
    return rp.y - h;
}

/* Single-tap self-shadow along +z. */
float shadow(vec3 rp) {
    float d = 0.05;
    float h = map(rp + vec3(0.0, 0.0, d), 0);
    return clamp(h / d, 0.0, 1.0);
}

/* Two scrolling fbm cloud layers projected onto the y==1 plane. */
float clouds(vec3 ro, vec3 rd) {
    float gt = iGlobalTime * 0.5;
    float ary = max(abs(rd.y), 1e-4);
    vec3 p = ro + rd * (1.0 / ary);
    /* log2 texel footprint of one pixel on the y=const cloud plane for the
       0.009-scaled base octave: drives the mip fade of the fbm taps. */
    float lb = log2(max(0.009 * 64.0 * (1.0 + length(rd.xz) / ary) / ary
                        * (2.0 / iResolution.y), 1e-4));
    vec2 uv = p.xz;
    uv.y += gt;
    float f1 = flterF(fbm(uv * 0.009, lb), 0.3);
    uv = p.xz;
    uv.y += gt * 1.5;
    uv.x += gt * 0.5;
    float f2 = flterF(fbm(uv * 0.015, lb + 0.737), 0.5);
    float f = mix(f1, f2, 0.2);
    return clamp(f * 1.2, 0.0, 1.0);
}

/* Shade a confirmed hit into g_col (straw/ground mix, depth fade, cloud
 * shadow, self shadow).  g_warpedRp must already correspond to rp. */
void shade_hit(vec3 ro, vec3 rd, vec3 rp) {
    float dl = length(rp - ro);
    vec3 tx = texB(iChannel2, g_warpedRp.xz * 0.5, vec2(256.0),
                   log2(max(512.0 * dl * dl / iResolution.y, 1e-4))).rgb;
    float strw = clamp(tx.g - 0.5, 0.0, 1.0);
    strw /= (1.0 - 0.5);
    vec3 strawcol = mix(vec3(1.0, 1.0, 0.0), vec3(0.8, 0.6, 0.3), strw) * 1.5;
    vec3 c2 = texB(iChannel2, g_warpedRp.xz * 0.008, vec2(256.0), 0.0).rgb;
    g_col = mix(c2, strawcol, 0.8);
    g_col = mix(vec3(0.0), g_col, smoothstep(H * 0.2, H * 1.4, rp.y));
    g_col *= 1.0 - clouds(ro, rd);
    g_col *= mix(shadow(rp), 1.0, 0.7);
}

/* March the grass field (downward rays only); on a hit shade into g_col and
 * record the hit point in g_rp.  The reference computes grad() here but never
 * uses it, so no diffuse term is applied. */
int trace(vec3 ro, vec3 rd) {
    vec3 rp = ro;
    if (rd.y > 0.0) { g_rp = rp; return 0; }
    float ydiff = rp.y - H;
    rp += rd * (ydiff / max(abs(rd.y), 1e-4));
    g_aa_a = 0.0; g_aa_b = 0.0; g_aa_c = 0.0; g_aa_d = 0.0;
    g_aa_e = 0.0; g_aa_f = 0.0; g_aa_g = 0.0; g_aa_h = 0.0;
    float l = 0.0;
    g_modl = -1.0;
    g_px = 1e9; g_pz = 1e9; g_ang = 1e9; g_lx = -1.0;
    g_rdy = rd.y; g_lod = 0.0;
    vec3 prp = rp;
    for (int i = 0; i < @@STEPS@@; i++) {
        march_cache(rp, l, 0.02);
        if (l - g_modl > 0.25) {
            g_mod = texB(iChannel0, rp.xz * 0.1625, vec2(64.0),
                         log2(max(6.4 * l * l / iResolution.y, 1e-4))).r;
            g_modl = l;
        }
        float h = map(rp, 1);
        if (h <= 0.0 || rp.y < 0.0) {
            /* Bisect back onto the surface: a raw crossing can land deep
             * inside the blade layer, where smoothstep(H*0.2,H*1.4,rp.y)
             * collapses and speckles the field with black dots. */
            vec3 lo = prp;
            for (int k = 0; k < 4; k++) {
                vec3 mid = (lo + rp) * 0.5;
                march_cache(mid, l, 0.02);
                if (map(mid, 0) > 0.0) lo = mid; else rp = mid;
            }
            march_cache(rp, l, 0.02);
            map(rp, 0);
            shade_hit(ro, rd, rp);
            g_rp = rp;
            return 1;
        }
        vec3 diff = rp - ro;
        if (dot(diff, diff) > 30.0) {
            /* Cutoff miss: a grazing ray can ride the blade tops past the
             * distance cap; shading it as a surface hit keeps the horizon
             * silhouette free of black speckles (a raw miss here leaves col
             * at zero inside the fog's blank window). */
            march_cache(rp, l, 0.02);
            if (map(rp, 0) < 0.15) {
                shade_hit(ro, rd, rp);
                g_rp = rp;
                return 1;
            }
            g_rp = rp;
            return 0;
        }
        float dist = max(0.00001, h * 0.04 * g_expf);
        prp = rp;
        rp += rd * dist;
        l += dist;
    }
    /* Zeno exhaustion: the ray rides the surface asymptotically.  Shade it as
     * a shallow hit; a miss here would leave col at zero with dist inside
     * the fog's blank window, which is the black wedge at the horizon. */
    march_cache(rp, l, 0.02);
    if (map(rp, 0) < 0.15) {
        shade_hit(ro, rd, rp);
        g_rp = rp;
        return 1;
    }
    g_rp = rp;
    return 0;
}

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy - vec2(0.5, 0.5);
    uv.y /= iResolution.x / iResolution.y;

    /* Camera walks forward along +z; the mouse looks around, defaulting to a
     * slight downward pitch when untouched (reference: m = (0, 0.2*res.y)). */
    vec3 rp = vec3(0.0, 0.5, -1.0);
    rp.z += iGlobalTime * 0.15;
    vec3 ro = rp;
    vec3 rd = normalize(vec3(uv, 0.5));
    vec2 m = iMouse.xy - 0.5 * iResolution.xy;
    if (iMouse.xy == vec2(0.0)) m = vec2(0.0, 0.2 * iResolution.y);
    rd = rotX(rd, (m.y / iResolution.y) * 2.0);
    rd = rotY(rd, (m.x / iResolution.y) * 3.0);
    rd = normalize(rd);

    vec3 col = vec3(0.0);
    int hit = trace(ro, rd);
    if (hit == 1) col = g_col;
    rp = g_rp;
    if (hit == 0) {
        if (rd.y > -0.08) rp = rp + rd * 10.0;
    }
    float dist = length(ro - rp);

    /* Distance fog toward a white horizon (reversed-edge smoothstep). */
    vec3 fog = vec3(0.1, 0.25, 0.4) * 0.7;
    fog = mix(fog, vec3(1.0), sstep(0.5, -0.4, rd.y));
    col = mix(col, fog, smoothstep(3.0, 10.0, dist));

    /* Sky clouds above the horizon. */
    if (rd.y > 0.0) {
        float cl = clouds(ro, rd) * smoothstep(0.0, 0.2, rd.y);
        col = mix(col, vec3(0.95), cl);
    }

    /* Sun halo around the view axis, suppressed when looking backward. */
    float hl = length(rd.xy);
    if (rd.z < 0.0) hl = 11.0;
    col += clamp(1.0 - pow(max(hl, 0.0), 0.3), 0.0, 1.0);

    /* Per-pixel max-channel normalisation, then the contrast curve. */
    float mx = max(col.r, max(col.g, col.b));
    col /= max(1.0, mx);
    col = col * 0.2 + 0.8 * col * col * (3.0 - 2.0 * col);
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";

#endif /* GRASS_GLSL_H */
