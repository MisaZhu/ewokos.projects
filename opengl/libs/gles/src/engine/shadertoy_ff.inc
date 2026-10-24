/* shadertoy_ff.inc -- native C ports of the 11 shadertoy fragment effects.
 *
 * This file is #included by BOTH src/engine/pgl_engine.c (the real CPU render
 * path) and the host golden_native.cpp verification harness, so the exact same
 * code produces the pixels in both - bit-identical by construction, mirroring
 * the glsl_vm.inc sharing pattern.
 *
 * Design contract (why this is pixel-exact, not merely "close"):
 *   - Every arithmetic/builtin op mirrors glsl_exec.c EXACTLY:
 *       DIV   : b != 0 ? a/b : +-inf (0/0=0) (st_div; IEEE, as real GLSL)
 *       POW   : x >= 0 ? powf(x,y) : 0      (st_pow)
 *       SQRT  : x >= 0 ? sqrtf(x) : 0       (st_sqrt)
 *       MIX   : a*(1-t) + b*t               (st_mix, NOT a+(b-a)*t)
 *       SMOOTH: t=(x-e0)/(e1-e0) clamped, t*t*(3-2t)   (st_smoothstep)
 *       CLAMP : x<lo?lo:(x>hi?hi:x)         (st_clamp)
 *       NORMAL: len=sqrtf(s); len>0 ? a/len : 0        (st_normalize3)
 *       atan(y,x) -> atan2f(y,x); all transcendentals single-precision *f.
 *       unary minus is true negation (-0.0 stays -0.0).
 *   - Expression evaluation ORDER matches the GLSL tree the interpreter walks
 *     (left-assoc a*b/c == (a*b)/c, sequential statement side effects, etc.).
 *   - Texture fetch goes through the st_texfn callback so the engine (portablegl
 *     texture2D) and the harness (golden's stub_tex) supply identical samples;
 *     the sampler unit is the raw int the interpreter reads via ci() (.i bits).
 *
 * Requires <math.h> in the including TU.  All symbols are static / st_-prefixed.
 * The 11 effects are recognised at link time by match_shadertoy() in
 * pgl_engine.c via an FNV-1a hash of the FS source (gp->fs->src_hash).
 */

/* Texture fetch callback: sample `unit` at (u,v), write rgba[4]. */
typedef void (*st_texfn)(int unit, float u, float v, float *rgba, void *user);

#define SHADERTOY_N_EFFECTS 11

/* Per-pixel inputs, gathered once by the caller (engine trampoline / harness). */
typedef struct {
    float fcx, fcy;            /* gl_FragCoord.xy                          */
    float gtime;               /* iGlobalTime                              */
    float resx, resy, resz;    /* iResolution.xyz                          */
    int   ch0, ch1, ch2;       /* iChannel0..2 sampler units (.i bits)     */
    st_texfn tex; void *user;  /* texture fetch + opaque user pointer      */
} st_ctx;

/* ------------------------------------------------------------------ *
 * math helpers - byte-for-byte the semantics of glsl_exec.c
 * ------------------------------------------------------------------ */
/* DIV mirrors glsl_exec.c EX_DIV: a float divide-by-zero yields +-inf (IEEE,
 * what a real GPU / the original portablegl C produced), NOT 0.  the_cave
 * depends on this: its Distance_() is radius/dot(diff,diff) and at raymarch
 * step 0 the sample sits exactly on the camera curve (dot==0); returning 0
 * there makes Dist2D<threshold true at once and collapses the whole frame to
 * the constant camera colour.  0/0 stays 0 to avoid NaN. */
static inline float st_div(float a, float b) { return b != 0.0f ? a / b : (a > 0.0f ? INFINITY : (a < 0.0f ? -INFINITY : 0.0f)); }
static inline float st_sqrt(float x) { return x >= 0.0f ? sqrtf(x) : 0.0f; }
/* powf goes through a noinline wrapper: otherwise the compiler expands an
 * integer-constant exponent (e.g. powf(x,6.0f) in tileable_water_caustic) into
 * a multiply chain that rounds differently from the interpreter's runtime powf
 * call, breaking bit-identity.  Keeping the exponent opaque forces a real call. */
#if defined(__GNUC__) || defined(__clang__)
#define ST_NOINLINE __attribute__((noinline))
#else
#define ST_NOINLINE
#endif
static ST_NOINLINE float st_powf_call(float x, float y) { return powf(x, y); }
static inline float st_pow(float x, float y) { return x >= 0.0f ? st_powf_call(x, y) : 0.0f; }
/* sinf/cosf likewise go through noinline wrappers: with builtins enabled the
 * compiler may fuse a same-argument sinf/cosf pair into sincosf (or otherwise
 * transform them), which rounds ~1 ULP differently from the interpreter's
 * separate st_sinf()/st_cosf() libm calls.  The parenthesised (sinf)/(cosf) form
 * below keeps these definitions calling libm directly and hides them from the
 * call-site redirect. */
static ST_NOINLINE float st_sinf(float x) { return (sinf)(x); }
static ST_NOINLINE float st_cosf(float x) { return (cosf)(x); }
static inline float st_abs(float x) { return fabsf(x); }
static inline float st_fract(float x) { return x - floorf(x); }
static inline float st_min(float a, float b) { return a < b ? a : b; }
static inline float st_max(float a, float b) { return a > b ? a : b; }
static inline float st_mix(float a, float b, float t) { return a * (1.0f - t) + b * t; }
static inline float st_clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
/* gen_ternary BF_SMOOTHSTEP: plain '/' (e1==e0 guarded first, so divisor != 0) */
static inline float st_smoothstep(float e0, float e1, float x)
{
    float t = (e1 == e0) ? 0.0f : (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
static inline float st_dot2(float ax, float ay, float bx, float by)
{ return ax * bx + ay * by; }
static inline float st_len2(float x, float y) { return sqrtf(x * x + y * y); }
static inline float st_len3(float x, float y, float z) { return sqrtf(x * x + y * y + z * z); }
static void st_normalize3(float *x, float *y, float *z)
{
    float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (len > 0.0f) { *x = *x / len; *y = *y / len; *z = *z / len; }
    else { *x = 0.0f; *y = 0.0f; *z = 0.0f; }
}
static void st_cross3(float ax, float ay, float az, float bx, float by, float bz,
                      float *ox, float *oy, float *oz)
{
    *ox = ay * bz - az * by;
    *oy = az * bx - ax * bz;
    *oz = ax * by - ay * bx;
}

/* ------------------------------------------------------------------ *
 * [7] iqs_eyeball helpers
 * ------------------------------------------------------------------ */
static float st_eye_hash(float n)
{
    float x = st_sinf(n) * 43758.5453123f;
    return x - floorf(x);
}
static float st_eye_noise(float xx, float xy)
{
    float px = floorf(xx), py = floorf(xy);
    float fx = xx - px, fy = xy - py;
    float n, m1, m2;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    n = px + py * 57.0f;
    m1 = st_mix(st_eye_hash(n + 0.0f), st_eye_hash(n + 1.0f), fx);
    m2 = st_mix(st_eye_hash(n + 57.0f), st_eye_hash(n + 58.0f), fx);
    return st_mix(m1, m2, fy);
}
static float st_fbm(float px, float py)
{
    float f = 0.0f;
    f += 0.5000f * st_eye_noise(px, py);
    f += 0.2500f * st_eye_noise(px * 2.0f, py * 2.0f);
    f += 0.1250f * st_eye_noise(px * 4.0f, py * 4.0f);
    f += 0.0625f * st_eye_noise(px * 8.0f, py * 8.0f);
    return st_div(f, 0.9375f);
}

/* ------------------------------------------------------------------ *
 * [8] voronoise helpers
 * ------------------------------------------------------------------ */
static void st_hash3(float px, float py, float *o)
{
    float qx = st_dot2(px, py, 127.1f, 311.7f);
    float qy = st_dot2(px, py, 269.5f, 183.3f);
    float qz = st_dot2(px, py, 419.2f, 371.9f);
    o[0] = st_fract(st_sinf(qx) * 43758.5453f);
    o[1] = st_fract(st_sinf(qy) * 43758.5453f);
    o[2] = st_fract(st_sinf(qz) * 43758.5453f);
}
static float st_iqnoise(float xx, float xy, float u, float v)
{
    float px = floorf(xx), py = floorf(xy);
    float fx = st_fract(xx), fy = st_fract(xy);
    float k = 1.0f + 63.0f * st_pow(1.0f - v, 4.0f);
    float va = 0.0f, wt = 0.0f;
    int j, i;
    for (j = -2; j <= 2; ++j)
    for (i = -2; i <= 2; ++i) {
        float gx = (float)i, gy = (float)j;
        float h[3], ox, oy, oz, rx, ry, d, ww;
        st_hash3(px + gx, py + gy, h);
        ox = h[0] * u; oy = h[1] * u; oz = h[2] * 1.0f;   /* hash3 * vec3(u,u,1) */
        rx = gx - fx + ox; ry = gy - fy + oy;             /* g - f + o.xy        */
        d = st_dot2(rx, ry, rx, ry);
        ww = st_pow(1.0f - st_smoothstep(0.0f, 1.414f, st_sqrt(d)), k);
        va += oz * ww;
        wt += ww;
    }
    return st_div(va, wt);
}

/* ------------------------------------------------------------------ *
 * [9] the_cave helpers + camera/light path constants
 * ------------------------------------------------------------------ */
static const float st_cama[2]   = { -2.6943f,  3.0483f };
static const float st_camb[2]   = {  0.2516f,  0.1749f };
static const float st_camc[2]   = { -3.7902f,  2.4478f };
static const float st_camd[2]   = {  0.0865f, -0.1664f };
static const float st_lighta[2] = {  1.4301f,  4.0985f };
static const float st_lightb[2] = { -0.1276f,  0.2347f };
static const float st_lightc[2] = { -2.2655f,  1.5066f };
static const float st_lightd[2] = { -0.1284f,  0.0731f };

/* Position(z,a,b,c,d) = vec2(sin(z*a.x),sin(z*a.y))*b.x + vec2(cos(z*c.x),cos(z*c.y))*d.x
 * NOTE: b.x and d.x are SCALARS - both components multiply by the same b.x / d.x. */
static void st_position(float z, const float *a, const float *b, const float *c,
                        const float *d, float *o)
{
    o[0] = st_sinf(z * a[0]) * b[0] + st_cosf(z * c[0]) * d[0];
    o[1] = st_sinf(z * a[1]) * b[0] + st_cosf(z * c[1]) * d[0];
}
static float st_distance_(float px, float py, float pz, const float *a,
                          const float *b, const float *c, const float *d,
                          float ex, float ey, float r)
{
    float pos[2], radius, diffx, diffy;
    st_position(pz, a, b, c, d, pos);
    radius = st_div(st_max(5.0f, r + st_sinf(pz * ex) * ey), 10000.0f);
    diffx = px - pos[0]; diffy = py - pos[1];
    return st_div(radius, st_dot2(diffx, diffy, diffx, diffy));
}
static float st_dist2d(float px, float py, float pz)
{
    float d = 0.0f;
    d += st_distance_(px, py, pz, st_cama, st_camb, st_camc, st_camd,
                      2.1913f, 15.4634f, 70.0000f);
    d += st_distance_(px, py, pz, st_lighta, st_lightb, st_lightc, st_lightd,
                      0.3814f, 12.7206f, 17.0590f);
    return d;
}

/* ------------------------------------------------------------------ *
 * [10] flame helpers
 * ------------------------------------------------------------------ */
static float st_noise10(float px, float py, float pz)
{
    float ix = floorf(px), iy = floorf(py), iz = floorf(pz);
    float pi = acosf(-1.0f);
    float ax = ix * 1.0f + iy * 57.0f + iz * 21.0f;   /* dot(i,vec3(1,57,21)) + 0 */
    float ay = 57.0f, az = 21.0f, aw = 78.0f;         /* + vec4(0,57,21,78)       */
    float fx = (px - ix) * pi, fy = (py - iy) * pi, fz = (pz - iz) * pi;
    float a1, a2, a3, a4, m1, m2;
    fx = st_cosf(fx) * (-0.5f) + 0.5f;
    fy = st_cosf(fy) * (-0.5f) + 0.5f;
    fz = st_cosf(fz) * (-0.5f) + 0.5f;
    a1 = st_sinf(st_cosf(ax) * ax);
    a2 = st_sinf(st_cosf(ay) * ay);
    a3 = st_sinf(st_cosf(1.0f + az) * (1.0f + az));
    a4 = st_sinf(st_cosf(1.0f + aw) * (1.0f + aw));
    m1 = st_mix(a1, a3, fx);
    m2 = st_mix(a2, a4, fx);
    return st_mix(m1, m2, fz);
}
static float st_sphere10(float px, float py, float pz)
{
    float dx = 0.0f - px, dy = -1.0f - py, dz = 0.0f - pz;   /* spr.xyz - p */
    return st_len3(dx, dy, dz) - 1.0f;                        /* - spr.w     */
}
static float st_flame_func(float px, float py, float pz, float gtime)
{
    float d = st_sphere10(px, py * 0.5f, pz);                 /* scaled_p */
    float n1 = st_noise10(px + 0.0f, py + gtime * 2.0f, pz + 0.0f);
    float n2 = st_noise10(px * 3.0f, py * 3.0f, pz * 3.0f);
    return d + (n1 + n2 * 0.5f) * 0.25f * py;
}
static float st_scene10(float px, float py, float pz, float gtime)
{
    return st_min(100.0f - st_len3(px, py, pz),
                  st_abs(st_flame_func(px, py, pz, gtime)));
}
static void st_raymarch10(float ox, float oy, float oz, float dx, float dy,
                          float dz, float gtime, float *out)
{
    float d = 0.0f, glow = 0.0f, eps = 0.02f;
    float px = ox, py = oy, pz = oz;
    int glowed = 0, i;
    for (i = 0; i < 64; ++i) {
        d = st_scene10(px, py, pz, gtime) + eps;
        px = px + dx * d; py = py + dy * d; pz = pz + dz * d;
        if (d > eps) {
            if (st_flame_func(px, py, pz, gtime) < 0.0f) glowed = 1;
            if (glowed) glow = (float)i / 64.0f;
        }
    }
    out[0] = px; out[1] = py; out[2] = pz; out[3] = glow;
}

/* ================================================================== *
 * the 11 effect mains
 * ================================================================== */

/* [0] graphing_lines */
static void st_e0(const st_ctx *c, float *out)
{
    float fx = st_div(c->fcx, c->resx);
    float fy = st_div(c->fcy, c->resy);
    float fragx2, x2, x3, x4, incr;
    fx = fx * 2.0f; fy = fy * 2.0f;
    fx = fx - 1.0f; fy = fy - 1.0f;
    fragx2 = fx * fx;
    x2 = fy - fragx2;
    x3 = fy - fragx2 * fx + 0.25f * fx;
    x4 = fy - fragx2 * fragx2;
    incr = st_div(2.0f, c->resy);
    if (x2 >= -incr && x2 <= incr)      { out[0]=1.0f; out[1]=0.0f; out[2]=0.0f; out[3]=1.0f; }
    else if (x3 >= -incr && x3 <= incr) { out[0]=0.0f; out[1]=1.0f; out[2]=0.0f; out[3]=1.0f; }
    else if (x4 >= -incr && x4 <= incr) { out[0]=0.0f; out[1]=0.0f; out[2]=1.0f; out[3]=1.0f; }
    else                                { out[0]=0.0f; out[1]=0.0f; out[2]=0.0f; out[3]=1.0f; }
}

/* [1] graphing */
static void st_e1(const st_ctx *c, float *out)
{
    float fx = st_div(c->fcx, c->resx);
    float fy = st_div(c->fcy, c->resy);
    float fragx2, x2;
    fx = fx * 2.0f; fy = fy * 2.0f;
    fx = fx - 1.0f; fy = fy - 1.0f;
    fragx2 = fx * fx;
    x2 = fy - fragx2;
    if (x2 > 0.0f) { out[0]=x2; out[1]=0.0f; out[2]=0.0f; out[3]=1.0f; }
    else           { out[0]=0.0f; out[1]=0.0f; out[2]=0.0f; out[3]=1.0f; }
}

/* [2] my_tunnel */
static void st_e2(const st_ctx *c, float *out)
{
    float fx = st_div(c->fcx, c->resx);
    float fy = st_div(c->fcy, c->resy);
    float r, wave;
    fx = fx * 2.0f; fy = fy * 2.0f;
    fx = fx - 1.0f; fy = fy - 1.0f;
    r = st_pow(st_pow(st_abs(fx), 16.0f) + st_pow(st_abs(fy), 16.0f), 1.0f / 16.0f);
    wave = 0.5f * st_sinf(10.0f * c->gtime * (1.0f - r)) + 0.5f;
    out[0] = r * wave; out[1] = 0.0f; out[2] = r * (1.0f - wave); out[3] = 1.0f;
}

/* [3] running_in_the_night (iChannel0) */
static void st_e3(const st_ctx *c, float *out)
{
    float time  = 45.0f * 3.14159f / 180.0f + st_div(st_cosf(c->gtime * 12.0f), 40.0f);
    float time1 = 45.0f * 3.14159f / 180.0f + st_div(st_cosf(c->gtime * 22.0f), 30.0f);
    float k = st_div(2.0f, c->resx);
    float uvx = c->fcx * k - 1.0f;
    float uvy = c->fcy * k - 1.0f;
    uvy = uvy * st_div(c->resy, c->resx);
    if (uvy < 0.0f) {
        float rotx = st_cosf(time), roty = st_sinf(time);
        float matx = uvx * rotx + (uvy - 1.0f) * roty;
        float maty = (uvy - 1.0f) * rotx - uvx * roty;
        float texx = st_div(matx * time1, uvy) + c->gtime * 2.0f;
        float texy = st_div(maty * time1, uvy) + c->gtime * 2.0f;
        float tmp[4], m;
        c->tex(c->ch0, texx * 2.0f, texy * 2.0f, tmp, c->user);
        m = -uvy;
        out[0] = tmp[0] * m; out[1] = tmp[1] * m; out[2] = tmp[2] * m; out[3] = 1.0f;
    } else {
        out[0]=0.0f; out[1]=0.0f; out[2]=0.0f; out[3]=1.0f;
    }
}

/* [4] square_tunnel (iChannel0) */
static void st_e4(const st_ctx *c, float *out)
{
    float px = c->fcx * 2.0f - c->resx;
    float py = c->fcy * 2.0f - c->resy;
    float inv = st_div(1.0f, c->resy);
    float a, r, uvx, uvy, tmp[4];
    px = px * inv; py = py * inv;
    a = atan2f(py, px);
    r = st_pow(st_pow(px * px, 16.0f) + st_pow(py * py, 16.0f), 1.0f / 32.0f);
    uvx = st_div(0.5f, r) + 0.5f * c->gtime;
    uvy = st_div(a, 3.1416f);
    c->tex(c->ch0, uvx, uvy, tmp, c->user);
    out[0] = tmp[0] * r; out[1] = tmp[1] * r; out[2] = tmp[2] * r; out[3] = 1.0f;
}

/* [5] deform_tunnel (iChannel0, iChannel1) */
static void st_e5(const st_ctx *c, float *out)
{
    float kx = st_div(1.0f, c->resx);
    float px = c->fcx * 2.0f * kx - 1.0f;
    float py = c->fcy * 2.0f * kx - 1.0f;
    float r, uvx, uvy, h, aa, c1[4], c0[4], colx, coly, colz;
    py = py * st_div(c->resy, c->resx);
    r = st_pow(st_pow(px * px, 16.0f) + st_pow(py * py, 16.0f), 1.0f / 32.0f);
    uvx = 0.5f * c->gtime + st_div(0.5f, r);
    uvy = st_div(atan2f(py, px), 3.1416f);
    h = st_sinf(32.0f * uvy);
    uvx += 0.85f * st_smoothstep(-0.1f, 0.1f, h);
    c->tex(c->ch1, uvx * 2.0f, uvy * 2.0f, c1, c->user);   /* iChannel1 */
    c->tex(c->ch0, uvx, uvy, c0, c->user);                 /* iChannel0 */
    aa = st_smoothstep(0.9f, 1.1f, st_abs(st_div(px, py)));
    colx = st_mix(c1[0], c0[0], aa);                       /* mix(ch1, ch0, aa) */
    coly = st_mix(c1[1], c0[1], aa);
    colz = st_mix(c1[2], c0[2], aa);
    r *= 1.0f - 0.3f * (st_smoothstep(0.0f, 0.3f, h) - st_smoothstep(0.3f, 0.96f, h));
    out[0] = colx * r * r * 1.2f;
    out[1] = coly * r * r * 1.2f;
    out[2] = colz * r * r * 1.2f;
    out[3] = 1.0f;
}

/* [6] tileable_water_caustic */
static void st_e6(const st_ctx *c, float *out)
{
    float TAU = 6.28318530718f;
    float time = c->gtime * 0.5f + 23.0f;
    float spx = st_div(c->fcx, c->resx);
    float spy = st_div(c->fcy, c->resy);
    float px = spx * TAU - 250.0f;
    float py = spy * TAU - 250.0f;
    float icx = px, icy = py;
    float cc = 1.0f, inten = 0.005f;
    float colour_val, fx, fy, fz;
    int n;
    for (n = 0; n < 5; ++n) {
        float t = time * (1.0f - st_div(3.5f, (float)(n + 1)));
        float nicx = px + st_cosf(t - icx) + st_sinf(t + icy);
        float nicy = py + st_sinf(t - icy) + st_cosf(t + nicx);   /* uses NEW ic.x */
        float lx, ly, len;
        icx = nicx; icy = nicy;
        lx = st_div(px, st_div(st_sinf(icx + t), inten));
        ly = st_div(py, st_div(st_cosf(icy + t), inten));
        len = st_len2(lx, ly);
        cc += st_div(1.0f, len);
    }
    cc = st_div(cc, 5.0f);
    cc = 1.2f - st_pow(cc, 1.2f);
    colour_val = st_pow(st_abs(cc), 6.0f);
    fx = st_clamp(colour_val + 0.0f, 0.0f, 1.0f);
    fy = st_clamp(colour_val + 0.35f, 0.0f, 1.0f);
    fz = st_clamp(colour_val + 0.5f, 0.0f, 1.0f);
    out[0] = fx; out[1] = fy; out[2] = fz; out[3] = 1.0f;
}

/* [7] iqs_eyeball */
static void st_e7(const st_ctx *c, float *out)
{
    float uvx = st_div(c->fcx, c->resx);
    float uvy = st_div(c->fcy, c->resy);
    float px = -1.0f + 2.0f * uvx;
    float py = -1.0f + 2.0f * uvy;
    float r, a, ss, anim, colx, coly, colz, f, mf, ppx, ppy, g;
    px = px * st_div(c->resx, c->resy);
    r = st_sqrt(st_dot2(px, py, px, py));
    a = atan2f(py, px);
    colx = 1.0f; coly = 1.0f; colz = 1.0f;                 /* bg_col */
    ss = 0.5f + 0.5f * st_sinf(c->gtime);
    anim = 1.0f + 0.1f * ss * st_clamp(1.0f - r, 0.0f, 1.0f);
    r *= anim;
    if (r < 0.8f) {
        colx = 0.0f; coly = 0.3f; colz = 0.4f;
        f = st_fbm(px * 5.0f, py * 5.0f);
        colx = st_mix(colx, 0.2f, f); coly = st_mix(coly, 0.5f, f); colz = st_mix(colz, 0.4f, f);
        f = 1.0f - st_smoothstep(0.2f, 0.5f, r);
        colx = st_mix(colx, 0.9f, f); coly = st_mix(coly, 0.6f, f); colz = st_mix(colz, 0.2f, f);
        a += 0.05f * st_fbm(px * 20.0f, py * 20.0f);
        f = st_smoothstep(0.3f, 1.0f, st_fbm(10.0f * r, 20.0f * a));
        colx = st_mix(colx, 1.0f, f); coly = st_mix(coly, 1.0f, f); colz = st_mix(colz, 1.0f, f);
        f = st_smoothstep(0.4f, 0.9f, st_fbm(10.0f * r, 15.0f * a));
        mf = 1.0f - 0.5f * f; colx *= mf; coly *= mf; colz *= mf;
        f = st_smoothstep(0.6f, 0.8f, r);
        mf = 1.0f - 0.5f * f; colx *= mf; coly *= mf; colz *= mf;
        f = st_smoothstep(0.2f, 0.25f, r);
        colx *= f; coly *= f; colz *= f;
        ppx = px - 0.24f; ppy = py - 0.2f;
        f = 1.0f - st_smoothstep(0.0f, 0.3f, st_sqrt(st_dot2(ppx, ppy, ppx, ppy)));
        g = f * 0.8f;
        colx += 1.0f * g; coly += 0.9f * g; colz += 0.8f * g;
        f = st_smoothstep(0.75f, 0.8f, r);
        colx = st_mix(colx, 1.0f, f); coly = st_mix(coly, 1.0f, f); colz = st_mix(colz, 1.0f, f);
    }
    out[0] = colx; out[1] = coly; out[2] = colz; out[3] = 1.0f;
}

/* [8] voronoise */
static void st_e8(const st_ctx *c, float *out)
{
    float uvx = st_div(c->fcx, c->resx);
    float uvy = st_div(c->fcy, c->resx);          /* NOTE: iResolution.x, per GLSL */
    float px = 0.5f - 0.5f * st_sinf(c->gtime * 1.01f);
    float py = 0.5f - 0.5f * st_sinf(c->gtime * 1.71f);
    float t1 = 3.0f - 2.0f * px, t2 = 3.0f - 2.0f * py;
    float f;
    px = px * px * t1; py = py * py * t2;
    px = px * px * (3.0f - 2.0f * px); py = py * py * (3.0f - 2.0f * py);
    px = px * px * (3.0f - 2.0f * px); py = py * py * (3.0f - 2.0f * py);
    f = st_iqnoise(uvx * 24.0f, uvy * 24.0f, px, py);
    out[0] = f; out[1] = f; out[2] = f; out[3] = 1.0f;
}

/* [9] the_cave (iChannel0,1,2) */
static void st_e9(const st_ctx *c, float *out)
{
    float time = st_div(c->gtime, 12.0f) + 291.0f;
    float p1[2], Pos[3], tmp2[2];
    float cdx, cdy, cdz, crx, cry, crz, cux, cuy, cuz;
    float k, uvx, uvy, aspect, dx, dy, dz, odx, ody, odz;
    float fade = 0.0f, numit = 75.0f, threshold = 1.20f, scale = 1.5f, step;
    float Posm1[3], lp[2], lv = 1.0f, fstep;
    float tx[4], ty[4], tz[4], colx, coly, colz, diffv, ff;
    int x;

    st_position(time + 0.05f, st_cama, st_camb, st_camc, st_camd, p1);
    st_position(time, st_cama, st_camb, st_camc, st_camd, tmp2);
    Pos[0] = tmp2[0]; Pos[1] = tmp2[1]; Pos[2] = time;

    cdx = p1[0] - Pos[0]; cdy = -p1[1] + Pos[1]; cdz = 0.1f;
    st_normalize3(&cdx, &cdy, &cdz);
    st_cross3(cdx, cdy, cdz, 0.0f, 1.0f, 0.0f, &crx, &cry, &crz);
    st_normalize3(&crx, &cry, &crz);
    st_cross3(crx, cry, crz, cdx, cdy, cdz, &cux, &cuy, &cuz);
    st_normalize3(&cux, &cuy, &cuz);

    k = st_div(2.0f, c->resx);
    uvx = c->fcx * k - 1.0f; uvy = c->fcy * k - 1.0f;
    uvy = uvy * st_div(c->resy, c->resx);
    aspect = st_div(c->resx, c->resy);
    dx = uvx * aspect; dy = uvy; dz = 1.0f;
    st_normalize3(&dx, &dy, &dz);
    odx = dx; ody = dy; odz = dz;
    dx = crx * odx + cux * ody + cdx * odz;
    dy = cry * odx + cuy * ody + cdy * odz;
    dz = crz * odx + cuz * ody + cdz * odz;

    Posm1[0] = Pos[0]; Posm1[1] = Pos[1]; Posm1[2] = Pos[2];
    step = st_div(scale, numit);
    for (x = 0; x < 75; ++x) {
        if (st_dist2d(Pos[0], Pos[1], Pos[2]) < threshold) {
            fade = 1.0f - st_div((float)x, numit);
            break;
        }
        Posm1[0] = Pos[0]; Posm1[1] = Pos[1]; Posm1[2] = Pos[2];
        Pos[0] = Pos[0] + dx * step;
        Pos[1] = Pos[1] + dy * step;
        Pos[2] = Pos[2] + dz * step;
    }
    for (x = 0; x < 6; ++x) {
        float p2x = (Posm1[0] + Pos[0]) * 0.5f;
        float p2y = (Posm1[1] + Pos[1]) * 0.5f;
        float p2z = (Posm1[2] + Pos[2]) * 0.5f;
        if (st_dist2d(p2x, p2y, p2z) < threshold) { Pos[0]=p2x; Pos[1]=p2y; Pos[2]=p2z; }
        else { Posm1[0]=p2x; Posm1[1]=p2y; Posm1[2]=p2z; }
    }

    st_position(time + 0.5f, st_cama, st_camb, st_camc, st_camd, lp);
    {
        /* lp = Position3D(time+0.5,...) so lp.z = time+0.5; ld = lp - Pos,
         * Pos.z = time  =>  ld.z = 0.5 (NOT time - Pos.z == 0). */
        float ldx = lp[0] - Pos[0], ldy = lp[1] - Pos[1], ldz = (time + 0.5f) - Pos[2];
        for (x = 1; x < 15; ++x) {
            fstep = st_div((float)x, 15.0f);
            if (st_dist2d(Pos[0] + ldx * fstep, Pos[1] + ldy * fstep,
                          Pos[2] + ldz * fstep) < threshold) { lv = 0.0f; break; }
        }
    }

    {
        float tuvy = Pos[1] * 3.0f, tuvz = Pos[2] * 1.5f, tuvx = Pos[0] * 3.0f;
        c->tex(c->ch0, tuvy, tuvz, tx, c->user);
        c->tex(c->ch1, tuvx, tuvz, ty, c->user);
        c->tex(c->ch2, tuvx, tuvy, tz, c->user);
    }
    colx = (tx[0] + ty[0] + tz[0]) * 0.33f;
    coly = (tx[1] + ty[1] + tz[1]) * 0.33f;
    colz = (tx[2] + ty[2] + tz[2]) * 0.33f;
    diffv = lv * 1.2f + 0.2f;
    ff = st_min(1.0f, fade * 10.0f);
    out[0] = colx * diffv * ff;
    out[1] = coly * diffv * ff;
    out[2] = colz * diffv * ff;
    out[3] = 1.0f;
}

/* [10] flame */
static void st_e10(const st_ctx *c, float *out)
{
    float vx = -1.0f + st_div(2.0f * c->fcx, c->resx);
    float vy = -1.0f + st_div(2.0f * c->fcy, c->resy);
    float dx, dy, dz, p[4], glow, t, colx, coly, colz, intensity;
    vx = vx * st_div(c->resx, c->resy);
    dx = vx * 1.6f; dy = -vy; dz = -1.5f;
    st_normalize3(&dx, &dy, &dz);
    st_raymarch10(0.0f, -2.0f, 4.0f, dx, dy, dz, c->gtime, p);
    glow = p[3];
    t = p[1] * 0.02f + 0.4f;
    colx = st_mix(1.0f, 0.1f, t);
    coly = st_mix(0.5f, 0.5f, t);
    colz = st_mix(0.1f, 1.0f, t);
    intensity = st_pow(glow * 2.0f, 4.0f);
    if (intensity > 1.0f) intensity = 1.0f;
    out[0] = colx * intensity; out[1] = coly * intensity;
    out[2] = colz * intensity; out[3] = 1.0f;
}

/* ------------------------------------------------------------------ *
 * dispatcher: run effect [0..10] for one pixel.
 * ------------------------------------------------------------------ */
static void shadertoy_ff(int effect, const st_ctx *c, float *out)
{
    switch (effect) {
    case 0:  st_e0(c, out);  break;
    case 1:  st_e1(c, out);  break;
    case 2:  st_e2(c, out);  break;
    case 3:  st_e3(c, out);  break;
    case 4:  st_e4(c, out);  break;
    case 5:  st_e5(c, out);  break;
    case 6:  st_e6(c, out);  break;
    case 7:  st_e7(c, out);  break;
    case 8:  st_e8(c, out);  break;
    case 9:  st_e9(c, out);  break;
    case 10: st_e10(c, out); break;
    default: out[0]=out[1]=out[2]=0.0f; out[3]=1.0f; break;
    }
}
