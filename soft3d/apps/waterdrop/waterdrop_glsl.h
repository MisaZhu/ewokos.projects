/*
 * waterdrop_glsl.h -- GLES2 / GLSL ES 1.00 shaders for the "Waterdrop" demo.
 *
 * A 45-degree PERSPECTIVE water surface fills the LOWER half of the frame; the
 * UPPER half is a BLACK NIGHT SKY carrying a moon.  It is deliberately NOT a
 * top-down view: the water plane is ray-cast through a pinhole camera sitting
 * above and behind it, so ripples foreshorten toward the horizon exactly as
 * they would to a real eye.
 *
 * Water drops fall from the TOP of the frame under gravity.  While airborne a
 * drop is projected back to screen space and drawn as a small falling bead; the
 * instant it reaches the plane (height 0) it splashes and emits an expanding
 * Gaussian wave-ring at that world spot.  Because every source is summed
 * LINEARLY into one height field, overlapping rings interfere physically:
 * crests add (constructive), a crest meets a trough and cancels (destructive).
 *
 * The surface height AND its analytic gradient are accumulated in a SINGLE pass
 * over the sources (each ring contributes both its height and its slope), so the
 * normal costs one traversal instead of three finite-difference taps -- a ~3x
 * saving that matters most on the CPU interpreter.  The normal reflects the
 * night sky, so the moon's image breaks into a shimmering column of moonlight on
 * the rippling water and the interference reads as light/dark bands.
 *
 * GPU route (raspi5 / PGL_G3D): the vertex shader is a passthrough with no
 * uMVP and this fragment shader reads gl_FragCoord, so pgl_engine recognizes
 * the program as a fullscreen effect and dispatches it as a V3D compute kernel
 * (the same transparent path shadertoy/glmatrix use).  CPU route: the engine's
 * interpreter runs the identical GLSL.  Constraints honored on BOTH paths:
 *   - no integer division (the V3D backend aborts on it: imul_high unimpl.)
 *   - no pow() with a possibly-zero/negative base (guarded with epsilons)
 *   - loops with literal bounds; every builtin used is in the engine's table
 */

#ifndef WATERDROP_GLSL_H
#define WATERDROP_GLSL_H

#ifndef __cplusplus
#error "waterdrop_glsl.h requires C++ (raw string literals)"
#endif

/* Passthrough vertex shader: a full-screen clip-space quad.  It intentionally
 * references no uMVP / matrices so the engine flags the program as a fullscreen
 * effect and routes the fragment shader to the GPU compute path. */
static const char* waterdrop_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

#define WATERDROP_FS_PREAMBLE \
	"#version 100\n" \
	"precision highp float;\n"

/* ------------------------------------------------------------------ *
 * Water fragment shader.
 *
 * Uniforms:
 *   iGlobalTime  float  seconds since start (the animation clock)
 *   iResolution  vec3   viewport size (xy used), component 0
 * ------------------------------------------------------------------ */
static const char* waterdrop_fs_glsl = WATERDROP_FS_PREAMBLE R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;

const float PI    = 3.14159265;
const float EPS   = 1e-4;

/* ---- camera / world (world units; camera looks horizontally along +Z) ---- */
const float CAM_H = 30.0;    /* eye height above the water plane            */
const float FOC_K = 0.90;    /* focal length as a fraction of screen height */
const float Z_NEAR= 48.0;    /* nearest water band a procedural drop lands  */
const float Z_FAR = 260.0;   /* farthest water band                         */
const float X_RNG = 170.0;   /* half-width of the drop landing field        */

/* ---- falling drop ---- */
const float DROP_H0 = 95.0;  /* spawn height (above CAM_H so it enters top) */
const float GRAV    = 130.0; /* gravity (world units / s^2)                 */
const float FALL_T  = 1.207; /* sqrt(2*DROP_H0/GRAV): seconds to reach water*/
const float DROP_R  = 1.3;   /* drop world radius                           */

/* ---- ripple / wave ---- */
const float NSLOT   = 6.0;   /* procedural rain slots kept alive            */
const float INTERVAL= 0.62;  /* seconds between procedural drops            */
const float LIFE    = 3.0;   /* ripple lifetime (s)                         */
const float SPEED   = 60.0;  /* ring expansion speed (world units / s)      */
const float WAVELEN = 11.0;  /* ring wavelength (world units)               */
const float WIDTH   = 5.5;   /* Gaussian ring width (world units)           */
const float DAMP    = 1.15;  /* temporal damping                            */
const float SPREAD  = 0.030; /* cylindrical (1/sqrt r) spatial damping      */
const float NSCALE  = 3.0;   /* normal strength                             */
const float SHIN    = 90.0;  /* moon-glint specular power                   */

/* ---- night palette ---- */
const vec3  ZENITH  = vec3(0.008, 0.012, 0.026); /* near-black overhead     */
const vec3  HORIZON = vec3(0.045, 0.062, 0.100); /* faint horizon glow      */
const vec3  WATER_C = vec3(0.024, 0.050, 0.082); /* night water (lifted)    */
const vec3  MOON_C  = vec3(0.88, 0.91, 0.98);    /* cool moonlight          */
const float MOON_R  = 0.045;                     /* disc angular radius     */
const float MOON_GLOW = 0.20;                    /* sky moon halo width     */
const float REFL_GLOW = 0.34;                    /* reflected-moon halo width on water (wider than sky MOON_GLOW) */
const float REFL_GLOW_STR = 0.30;                /* reflected-moon halo peak (replaces, never adds, so no clipping) */
const float WATER_DARK = 0.62;                   /* water reflects the sky noticeably darker than the sky itself */
const float SKY_EPOCH = 6.0;                     /* sky re-randomize period */

/* Cheap 1D -> 2D hash, deterministic per slot index. */
vec2 hash2(float n) {
    return vec2(fract(sin(n * 127.1) * 43758.5453),
                fract(sin(n * 311.7) * 26955.5137));
}

/* Current moon direction, re-randomized every SKY_EPOCH seconds.  Constrained
 * to the +Z hemisphere (over the water) and to an elevation band that keeps the
 * disc on-screen (screen y = horiz + (my/mz)*focal < H), so the moon and its
 * reflected image are always visible.  Both the sky sample and the water
 * reflection call this, so they share one position per epoch. */
vec3 moonDirNow() {
    float epoch = floor(iGlobalTime / SKY_EPOCH);
    vec2 mr = hash2(epoch * 7.77 + 0.5);
    float mx = (mr.x - 0.5) * 1.0;   /* azimuth swing [-0.50, 0.50] */
    float my = 0.15 + mr.y * 0.25;   /* elevation     [ 0.15, 0.40] */
    return normalize(vec3(mx, my, 1.0));
}

/* One expanding ring from world source c, born at time tb, amplitude a.
 * Returns vec3(height, dHeight/dx, dHeight/dz): the ring's slope is accumulated
 * alongside its height so the surface normal needs only ONE pass over sources. */
vec3 waveG(vec2 p, vec2 c, float tb, float a) {
    float age0  = iGlobalTime - tb;
    float alive = smoothstep(0.0, 0.05, age0) * (1.0 - smoothstep(LIFE - 0.6, LIFE, age0));
    float age   = clamp(age0, 0.0, LIFE);
    vec2  d     = p - c;
    float r     = max(length(d), EPS);
    float R     = SPEED * age;
    float k     = 2.0 * PI / WAVELEN;
    float z     = (r - R) / WIDTH;
    float env   = exp(-z * z);
    float decay = exp(-age * DAMP) / sqrt(1.0 + r * SPREAD);
    float ph    = k * r - k * SPEED * age;
    float s     = sin(ph);
    float co    = cos(ph);
    float amp   = a * alive * env * decay;
    /* d(height)/dr = amp * [ k*cos(ph) - (2z/WIDTH)*sin(ph) ]; the slow
     * 1/sqrt(r) taper's own gradient is negligible beside the ring slope and is
     * dropped.  Chain to world x/z with d(r)/d(p) = (p-c)/r. */
    float dhdr = amp * (k * co - (2.0 * z / WIDTH) * s);
    vec2  grad = d * (dhdr / r);
    return vec3(amp * s, grad.x, grad.y);
}

/* Camera-derived constants shared by projection helpers. */
float camCx()     { return iResolution.x * 0.5; }
float camHoriz()  { return iResolution.y * 0.5; }
float camFocal()  { return iResolution.y * FOC_K; }

/* World point (X, height Y, depth Z) -> screen (gl_FragCoord space, y up). */
vec2 worldToScreen(float X, float Y, float Z) {
    float f = camFocal();
    return vec2(camCx() + f * X / Z, camHoriz() - f * (CAM_H - Y) / Z);
}

/* Accumulate height AND gradient at world point wp from the procedural rain.
 * A single traversal feeds both the surface height and its normal. */
vec3 heightGrad(vec2 wp) {
    vec3 acc = vec3(0.0, 0.0, 0.0);

    /* Procedural rain: NSLOT most recent slots, each a drop that already fell
     * from the sky and is now rippling from its impact time. */
    float cur = floor(iGlobalTime / INTERVAL);
    for (int s = 0; s < 6; s++) {
        float slot = cur - float(s);
        if (slot >= 0.0) {
            float spawn = slot * INTERVAL;
            vec2 rnd = hash2(slot * 17.31);
            vec2 c = vec2((rnd.x - 0.5) * 2.0 * X_RNG,
                          Z_NEAR + rnd.y * (Z_FAR - Z_NEAR));
            acc += waveG(wp, c, spawn + FALL_T, 1.0);
        }
    }

    return acc;
}

/* 2D value noise (bilinear-interpolated hash lattice) for the procedural lunar
 * surface: low frequencies give the dark maria, high frequencies the cratered
 * speckle.  Uses only floor/fract/sin/mix, proven on both compilers. */
float vhash2(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = vhash2(i);
    float b = vhash2(i + vec2(1.0, 0.0));
    float c = vhash2(i + vec2(0.0, 1.0));
    float d = vhash2(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/* Night sky radiance along a normalized direction: a near-black gradient, a
 * sparse star field, and the moon -- a textured lunar disc (maria + craters +
 * limb darkening) with a soft halo.  Sampled both for the sky pixels and, via
 * the reflection vector, for the water, so the SAME cratered face shimmers on
 * the ripples as a broken column of moonlight. */
vec3 skyColorDir(vec3 dir) {
    float t = clamp(dir.y, 0.0, 1.0);
    vec3 sky = mix(HORIZON, ZENITH, sqrt(t));
    float epoch = floor(iGlobalTime / SKY_EPOCH);

    /* Stars: round dots with a faint halo, NOT square blocks.  Each star sits at
     * a random point INSIDE its direction cell and fades with radial distance,
     * so it reads as a circle; the old per-cell fill lit the whole cube face and
     * projected as a quad.  The field re-seeds every SKY_EPOCH seconds. */
    vec3  g    = dir * 80.0;
    vec3  cell = floor(g);
    vec3  f    = fract(g);
    float hs   = fract(sin(cell.x * 12.9898 + cell.y * 78.233 + cell.z * 37.719
                           + epoch * 91.7) * 43758.5453);
    /* random centre inside the cell (three decorrelated hashes) */
    vec3 cc = vec3(
        fract(sin(dot(cell, vec3(91.17, 47.53, 13.71)) + epoch * 33.31) * 24634.6345),
        fract(sin(dot(cell, vec3(33.91, 71.13, 97.19)) + epoch * 57.71) * 46311.1234),
        fract(sin(dot(cell, vec3(57.13, 19.91, 63.17)) + epoch * 71.13) * 32113.5123));
    float d    = length(f - cc);
    /* Per-star random diameter 1..4 px.  d is in cell units, so convert the
     * pixel radius to cells via the focal scale (cell = dir*80, 1 px ~ 80/focal
     * in dir) to keep the size resolution-independent.  The halo is a Gaussian
     * roughly one diameter wide -- a glow as wide as the dot itself. */
    float px2cell = 80.0 / (iResolution.y * FOC_K);
    float srnd = fract(sin(dot(cell, vec3(11.71, 91.13, 27.19)) + epoch * 17.17) * 31415.9265);
    float rad  = (0.5 + 1.5 * srnd) * px2cell;   /* core radius 0.5..2 px, in cells */
    float mask = step(0.9955, hs) * step(0.05, dir.y);   /* sparse, above horizon */
    float bri  = 0.55 + 0.45 * fract(hs * 713.17);       /* per-star brightness   */
    float core = smoothstep(rad, 0.0, d);                /* round dot, 1..4 px wide */
    float halo = exp(-d * d / (2.0 * rad * rad)) * 0.40; /* glow ~ one diameter    */
    /* scaled below the moon's peak luminance so the field stays subordinate */
    sky += vec3(0.62, 0.70, 0.84) * (core + halo) * mask * bri * 0.45;

    /* moon halo (broad, cheap) + textured disc (gated to the disc footprint).
     * The direction re-randomizes every SKY_EPOCH seconds. */
    vec3  dv = dir - moonDirNow();
    float md = length(dv);
    float glow = exp(-md * md / (MOON_GLOW * MOON_GLOW));
    sky += MOON_C * glow * 0.28;
    if (md < MOON_R) {
        float disc = smoothstep(MOON_R, MOON_R * 0.82, md);
        /* the moon faces mostly +Z, so the disc-plane coords are just dv.xy */
        vec2 muv = dv.xy / MOON_R;
        float rr = clamp(length(muv), 0.0, 1.0);
        float limb = 0.55 + 0.45 * sqrt(max(1.0 - rr * rr, 0.0));  /* limb darkening */
        float m1 = vnoise(muv * 1.7 + 3.1);
        float m2 = vnoise(muv * 3.4 + 7.7);
        float maria = smoothstep(0.34, 0.62, m1 * 0.66 + m2 * 0.34); /* 0 dark sea,1 highland */
        float crater = smoothstep(0.48, 0.78, vnoise(muv * 9.5 + 1.3));
        vec3 mc = MOON_C * (0.22 + 0.46 * maria) * limb;
        mc += MOON_C * crater * 0.16 * limb;
        mc += MOON_C * smoothstep(0.86, 1.0, rr) * 0.12;   /* bright limb ring */
        sky = mix(sky, mc, disc);
    }
    return sky;
}

/* Composite one airborne drop bead over col if this fragment is inside it.
 * Branchless: dz <= 0 (a sky tap) drives vis to 0 so nothing is drawn. */
vec3 drawDrop(vec3 col, vec2 p, float dx, float hh, float dz, float fade) {
    float dzc = max(dz, 1.0);              /* guard the divides              */
    float vis = step(0.5, dz) * fade;      /* 0 when dz <= 0 (sky tap)        */
    vec2 sc = worldToScreen(dx, hh, dzc);
    float rr = max(camFocal() * DROP_R / dzc, 1.0);
    float d  = length(p - sc);
    float cover = (1.0 - smoothstep(rr * 0.55, rr, d)) * vis;
    float f = clamp(1.0 - d / rr, 0.0, 1.0);
    vec3 bead = mix(vec3(0.05, 0.09, 0.15), vec3(0.55, 0.63, 0.74), f * f);
    bead += MOON_C * 0.55 * f * f * f;
    return mix(col, bead, cover);
}

void main() {
    vec2 p   = gl_FragCoord.xy;
    vec2 res = iResolution.xy;
    float horiz = camHoriz();
    float focal = camFocal();

    vec3 col;

    if (p.y >= horiz) {
        /* ---- NIGHT SKY (upper half): ray direction through the pixel ---- */
        vec3 dir = normalize(vec3((p.x - camCx()) / focal,
                                  (p.y - horiz) / focal, 1.0));
        col = skyColorDir(dir);
    } else {
        /* ---- WATER (lower half): ray-cast the plane ---- */
        float dy = max(horiz - p.y, 0.5);
        float Z  = focal * CAM_H / dy;             /* world depth of this ray */
        float X  = (p.x - camCx()) * Z / focal;    /* world x                 */
        vec2  wp = vec2(X, Z);

        /* Height + analytic gradient in ONE pass; the normal is the slope. */
        vec3  hg = heightGrad(wp);
        float h  = hg.x;
        vec3  n  = normalize(vec3(-hg.y * NSCALE, 1.0, -hg.z * NSCALE));

        vec3 V = normalize(vec3(-X, CAM_H - h, -Z));        /* toward camera */
        vec3 R = reflect(-V, n);

        /* Fresnel-blend the reflected night sky (the moon shimmers on the
         * ripples) over the dark water body, then a sharp glint on the crests. */
        float ndv  = clamp(dot(n, V), 0.0, 1.0);
        float g    = 1.0 - ndv;
        float fres = 0.08 + 0.92 * g * g * g;
        vec3  moonD = moonDirNow();
        vec3  skyR  = skyColorDir(R);             /* reflected sky: moon disc + its own halo */

        /* Bigger moon reflection halo: the sky's moon halo (MOON_GLOW) comes
         * through skyR above, but on the water we widen it to REFL_GLOW so the
         * reflected glow spreads over more ripples.  It is blended in with max()
         * (a replace, not an add) so the halo can never exceed the moon's own
         * brightness -- that keeps the glint core from clipping to flat white
         * while still ringing it with a broader shimmer. */
        float mang  = length(R - moonD);
        float rglow = exp(-mang * mang / (REFL_GLOW * REFL_GLOW)) * REFL_GLOW_STR;
        vec3  refl  = max(skyR, MOON_C * rglow) * WATER_DARK;   /* water darker than sky */

        vec3  base = WATER_C * (0.70 + 0.55 * clamp(h * 3.0, -1.0, 1.0));
        float spec = pow(max(dot(R, moonD), EPS), SHIN);

        col = mix(base, refl, fres) + MOON_C * spec * 0.7;

        /* Fade far water into the horizon glow to hide the plane's hard edge. */
        float haze = smoothstep(0.0, res.y * 0.10, dy);
        col = mix(HORIZON, col, haze);
    }

    /* ---- Falling drops, drawn on top of sky and water alike ---- */
    float cur = floor(iGlobalTime / INTERVAL);
    for (int s = 0; s < 6; s++) {
        float slot = cur - float(s);
        if (slot >= 0.0) {
            float t = iGlobalTime - slot * INTERVAL;
            if (t >= 0.0 && t < FALL_T) {
                vec2 rnd = hash2(slot * 17.31);
                vec2 c = vec2((rnd.x - 0.5) * 2.0 * X_RNG,
                              Z_NEAR + rnd.y * (Z_FAR - Z_NEAR));
                float hh = DROP_H0 - 0.5 * GRAV * t * t;
                col = drawDrop(col, p, c.x, hh, c.y, 1.0);
            }
        }
    }

    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";

#endif /* WATERDROP_GLSL_H */
