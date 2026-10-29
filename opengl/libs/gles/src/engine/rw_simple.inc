/* RenderWare's exact simple FS. Token comparison deliberately rejects modified
 * shaders: sharing uniform/varying names alone is not a shader contract. */
#include "../glsl/glsl_lex.h"
#include "rw_shader_contract.h"

#if defined(ARCH_BOOST) && (defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#define RW_NATIVE_NEON 1
#else
#define RW_NATIVE_NEON 0
#endif

/* FNV-1a fingerprints of the RenderWare GLSL ES 3.10 vertex sources shipped
 * by librw.  The engine receives the concatenated shader text, so matching the
 * complete source is stricter than inferring a shader from a few uniform names
 * and keeps the native executor pixel-safe. */
#define RW_VS_IM3D_BASE       0x8b0d389e9c5a4308ULL
#define RW_VS_IM3D_LIGHT      0xcad53374ce0fb1afULL
#define RW_VS_DEFAULT_BASE    0xaf4f6e60c0edaf97ULL
#define RW_VS_DEFAULT_LIGHT   0xf87fd1a3c2296110ULL
#define RW_VS_MATFX_BASE      0x19368af77d83b5a0ULL
#define RW_VS_MATFX_LIGHT     0x579eb41cbe9e3fddULL
#define RW_VS_SKIN_BASE       0xffb2e65737ee6768ULL
#define RW_VS_SKIN_LIGHT      0x4a8fe5cb2a98a797ULL
#define RW_VS_IM2D_BASE       0xc3bfbf4db17aa881ULL
#define RW_VS_IM2D_LIGHT      0x35d2cdce557a76ceULL
/* GLES 2.0 librw builds use the same shaders with the #version 100 preamble. */
#define RW_VS100_IM3D_BASE    0xf449daec10d20979ULL
#define RW_VS100_IM3D_LIGHT   0x03625567605bb17eULL
#define RW_VS100_DEFAULT_BASE 0x45e4921a6ce68d36ULL
#define RW_VS100_DEFAULT_LIGHT 0x5d2b4fecd511b069ULL
#define RW_VS100_MATFX_BASE   0xda0fca4b331a512fULL
#define RW_VS100_MATFX_LIGHT  0xc098c72f8fc736c6ULL
#define RW_VS100_SKIN_BASE    0x693037ee2da6ccf1ULL
#define RW_VS100_SKIN_LIGHT   0xe41775742826f20eULL
#define RW_VS100_IM2D_BASE    0xd1f3e4386edf64e4ULL
#define RW_VS100_IM2D_LIGHT   0x3b5d78c7a1adf88bULL
/* Q8B's GLES2 shader builder emits a stable preamble/body pair for the
 * default RenderWare, dynamic-light, and im2d programs.  These fingerprints
 * were captured from the board image and are kept as exact matches so those
 * high-volume shaders can use the native executor without enabling the broad
 * source-shape matcher for arbitrary effects. */
#define RW_VS_Q8B_DEFAULT_BASE  0x236720f1bed55afaULL
#define RW_VS_Q8B_DEFAULT_LIGHT 0xb9db3a51daf6750dULL
#define RW_VS_Q8B_IM2D_BASE     0xdcfb9dc1dff632b6ULL

static int rw_native_uniform(const glsl_program *gp, const char *name)
{
    int i = glsl_uniform_index(gp, name);
    return i >= 0 ? gp->uniforms[i].offset : -1;
}

static int rw_native_attrib(const glsl_program *gp, const char *name)
{
    return glsl_attrib_index(gp, name);
}

static int rw_native_varying(const glsl_program *gp, const char *name)
{
    int i;
    for (i = 0; i < gp->nvaryings; ++i)
        if (!strcmp(gp->varyings[i].name, name)) return gp->varyings[i].slot;
    return -1;
}

static void rw_native_offsets(interp_entry *e)
{
    const glsl_program *gp = e->gp;
    e->rw_pos_loc = rw_native_attrib(gp, "in_pos");
    e->rw_normal_loc = rw_native_attrib(gp, "in_normal");
    e->rw_color_loc = rw_native_attrib(gp, "in_color");
    e->rw_weights_loc = rw_native_attrib(gp, "in_weights");
    e->rw_indices_loc = rw_native_attrib(gp, "in_indices");
    e->rw_tex0_loc = rw_native_attrib(gp, "in_tex0");
    e->rw_tex1_loc = rw_native_attrib(gp, "in_tex1");
    e->rw_v_color = rw_native_varying(gp, "v_color");
    e->rw_v_tex0 = rw_native_varying(gp, "v_tex0");
    e->rw_v_fog = rw_native_varying(gp, "v_fog");
    e->rw_v_env = rw_native_varying(gp, "v_envColor");
    e->rw_v_tex1 = rw_native_varying(gp, "v_tex1");
    e->rw_u_xform = rw_native_uniform(gp, "u_xform");
    e->rw_u_world = rw_native_uniform(gp, "u_world");
    e->rw_u_view = rw_native_uniform(gp, "u_view");
    e->rw_u_proj = rw_native_uniform(gp, "u_proj");
    e->rw_u_bones = rw_native_uniform(gp, "u_boneMatrices");
    e->rw_u_amb = rw_native_uniform(gp, "u_ambLight");
    e->rw_u_light_params = rw_native_uniform(gp, "u_lightParams");
    e->rw_u_light_pos = rw_native_uniform(gp, "u_lightPosition");
    e->rw_u_light_dir = rw_native_uniform(gp, "u_lightDirection");
    e->rw_u_light_color = rw_native_uniform(gp, "u_lightColor");
    e->rw_u_mat = rw_native_uniform(gp, "u_matColor");
    e->rw_u_surf = rw_native_uniform(gp, "u_surfProps");
    e->rw_u_fog_data = rw_native_uniform(gp, "u_fogData");
    e->rw_u_tex_matrix = rw_native_uniform(gp, "u_texMatrix");
    e->rw_u_color_clamp = rw_native_uniform(gp, "u_colorClamp");
    e->rw_u_env_color = rw_native_uniform(gp, "u_envColor");
}

static int rw_native_has(const interp_entry *e, int off)
{
    return off >= 0;
}

/* The CPU GLSL lexer intentionally skips preprocessor directives.  Keep the
 * original source on the stage so the native executor can still tell whether
 * librw enabled its directional/point/spot light blocks. */
static int rw_source_define(const char *src, const char *name)
{
    const char *p = src;
    size_t n;
    if (!src || !name) return 0;
    n = strlen(name);
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') ++p;
        while (line < p && (*line == ' ' || *line == '\t' || *line == '\r')) ++line;
        if (line < p && line[0] == '#' && line[1] == 'd' &&
            !strncmp(line, "#define", 7) &&
            (line[7] == ' ' || line[7] == '\t')) {
            line += 8;
            while (line < p && (*line == ' ' || *line == '\t')) ++line;
            if ((size_t)(p - line) >= n && !strncmp(line, name, n) &&
                (line[n] == ' ' || line[n] == '\t' || line[n] == '\r' || line[n] == '\n' || line[n] == '\0'))
                return 1;
        }
        if (*p) ++p;
    }
    return 0;
}

static int rw_source_contains(const char *src, const char *needle)
{
    return src && needle && strstr(src, needle) != NULL;
}

static int rw_native_experimental_enabled(void)
{
    const char *v = getenv("PGL_RW_NATIVE_EXPERIMENTAL");
    return v && atoi(v) != 0;
}

static int rw_native_uniform_name_ok(const char *name, int kind)
{
    static const char *const common[] = {
        "u_alphaRef", "u_fogData", "u_fogColor", "u_proj", "u_view",
        "u_world", "u_ambLight", "u_lightParams", "u_lightPosition",
        "u_lightDirection", "u_lightColor", "u_matColor", "u_surfProps",
        NULL
    };
    int i;
    for (i = 0; common[i]; ++i) if (!strcmp(name, common[i])) return 1;
    if (kind == 5) return !strcmp(name, "u_xform");
    if (kind == 3) return !strcmp(name, "u_texMatrix") ||
                            !strcmp(name, "u_colorClamp") ||
                            !strcmp(name, "u_envColor");
    if (kind == 4) return !strcmp(name, "u_boneMatrices");
    return 0;
}

/* Reject a body-only match when a custom RenderWare pipeline adds another
 * uniform.  Several RE3 effects reuse the default lighting lines but add rim,
 * reflection, or eye vectors; running those through the small native VS would
 * silently drop their effect. */
static int rw_native_source_contract(const char *src, int kind)
{
    glsl_token *t;
    char err[64];
    int n, i;
    if (!src) return 0;
    t = glsl_lex(src, &n, err, sizeof err);
    if (!t) return 0;
    for (i = 0; i < n; ++i) {
        if (t[i].kind == TK_UNIFORM) {
            ++i;
            while (i < n && t[i].kind != TK_IDENT) ++i;
            if (i >= n || !rw_native_uniform_name_ok(t[i].text, kind)) {
                free(t);
                return 0;
            }
        }
    }
    free(t);
    /* im2d_UV2 deliberately changes the homogeneous-coordinate setup by
     * forcing w=1 before the xform. The original im2d shader only reads the
     * incoming homogeneous w and is safe for the native executor. */
    if (kind == 5 && rw_source_contains(src, "gl_Position.w =")) return 0;
    return 1;
}

static void rw_native_vs_link(interp_entry *e)
{
    unsigned long long h;
    int kind = 0;
    int light = 0;
    int exact = 1;
    if (!e || !e->gp || !e->gp->vs) return;
    h = e->gp->vs->src_hash;
    if (h == RW_VS_IM3D_BASE || h == RW_VS100_IM3D_BASE) kind = 1;
    else if (h == RW_VS_IM3D_LIGHT || h == RW_VS100_IM3D_LIGHT) kind = 1, light = 1;
    else if (h == RW_VS_DEFAULT_BASE || h == RW_VS100_DEFAULT_BASE) kind = 2;
    else if (h == RW_VS_DEFAULT_LIGHT || h == RW_VS100_DEFAULT_LIGHT) kind = 2, light = 1;
    else if (h == RW_VS_MATFX_BASE || h == RW_VS100_MATFX_BASE) kind = 3;
    else if (h == RW_VS_MATFX_LIGHT || h == RW_VS100_MATFX_LIGHT) kind = 3, light = 1;
    else if (h == RW_VS_SKIN_BASE || h == RW_VS100_SKIN_BASE) kind = 4;
    else if (h == RW_VS_SKIN_LIGHT || h == RW_VS100_SKIN_LIGHT) kind = 4, light = 1;
    else if (h == RW_VS_IM2D_BASE || h == RW_VS100_IM2D_BASE) kind = 5;
    else if (h == RW_VS_IM2D_LIGHT || h == RW_VS100_IM2D_LIGHT) kind = 5, light = 1;
    else if (h == RW_VS_Q8B_DEFAULT_BASE) kind = 2;
    else if (h == RW_VS_Q8B_DEFAULT_LIGHT) kind = 2, light = 1;
    else if (h == RW_VS_Q8B_IM2D_BASE) kind = 5;
    if (!kind) {
        /* Source-shape matching was added for shader variants whose hash is
         * absent from the verified RenderWare family.  It is intentionally
         * opt-in: a false positive here changes vertex positions or varyings
         * while still producing a syntactically valid frame, which shows up
         * as in-game corruption on images that carry a different librw build.
         * Exact family hashes above remain enabled by the normal
         * PGL_RW_NATIVE switch. */
        if (!rw_native_experimental_enabled())
            return;
        /* A different shader preamble/version changes the byte hash.  Match
         * the distinctive RenderWare body before using reflection so a custom
         * vehicle/effect VS with the same uniform names cannot be miscompiled
         * by the native executor. */
        exact = 0;
        rw_native_offsets(e);
        if (rw_source_contains(e->gp->vs->source,
                               "gl_Position.xy = gl_Position.xy * u_xform.xy + u_xform.zw"))
            kind = 5;
        else if (rw_source_contains(e->gp->vs->source, "SkinVertex") &&
                 rw_source_contains(e->gp->vs->source, "SkinNormal"))
            kind = 4;
        else if (rw_source_contains(e->gp->vs->source,
                                    "v_envColor = max(v_color, u_colorClamp) * u_envColor"))
            kind = 3;
        else if (rw_source_contains(e->gp->vs->source,
                                    "v_color.rgb += u_ambLight.rgb*surfAmbient") &&
                 rw_source_contains(e->gp->vs->source,
                                    "DoDynamicLight(Vertex.xyz, Normal)"))
            kind = 2;
        else if (rw_source_contains(e->gp->vs->source,
                                    "v_color.rgb += u_ambLight.rgb*(u_surfProps.x)") &&
                 rw_source_contains(e->gp->vs->source,
                                    "DoDynamicLight(Vertex.xyz, Normal)*(u_surfProps.z)"))
            kind = 2;
        else if (rw_source_contains(e->gp->vs->source,
                                    "vec4 CamVertex = u_view * Vertex"))
            kind = 1;
        if (kind && !rw_native_source_contract(e->gp->vs->source, kind))
            kind = 0;
        if (e->gp->vs->source) {
            light = rw_source_define(e->gp->vs->source, "DIRECTIONALS") ||
                    rw_source_define(e->gp->vs->source, "POINTLIGHTS") ||
                    rw_source_define(e->gp->vs->source, "SPOTLIGHTS");
            /* Some GLES2 shader builders expand the conditional light body
             * before handing the source to the parser, so the #define lines
             * are gone even though the dynamic-light branches remain live. */
            if (!light && rw_source_contains(e->gp->vs->source,
                                              "if(u_lightParams[i].x == 1.0)"))
                light = 1;
        }
    }
    if (!kind) {
        if (profile_enabled()) {
            printf("PGL_PROFILE native_vs_fallback hash=%llx\n", h);
        }
        return;
    }
    if (!exact && profile_enabled())
        printf("PGL_PROFILE native_vs_contract kind=%d light=%d hash=%llx\n", kind, light, h);
    rw_native_offsets(e);
    e->rw_native_lit = kind == 2 || kind == 3 || kind == 4;
    e->rw_native_full_lights = light;
    if (kind == 5) {
        if (!rw_native_has(e, e->rw_u_xform) || e->rw_pos_loc < 0 ||
            e->rw_color_loc < 0 || e->rw_tex0_loc < 0 || e->rw_v_color < 0 ||
            e->rw_v_tex0 < 0 || e->rw_v_fog < 0 || e->rw_u_fog_data < 0)
            return;
    } else {
        if (e->rw_pos_loc < 0 || e->rw_color_loc < 0 || e->rw_tex0_loc < 0 ||
            e->rw_v_color < 0 || e->rw_v_tex0 < 0 || e->rw_v_fog < 0 ||
            e->rw_u_world < 0 || e->rw_u_view < 0 || e->rw_u_proj < 0 ||
            e->rw_u_fog_data < 0)
            return;
        if (e->rw_native_lit && (e->rw_normal_loc < 0 || e->rw_u_amb < 0 ||
            e->rw_u_light_params < 0 || e->rw_u_light_pos < 0 ||
            e->rw_u_light_dir < 0 || e->rw_u_light_color < 0 ||
            e->rw_u_mat < 0 || e->rw_u_surf < 0)) return;
        if (kind == 3 && (e->rw_v_env < 0 || e->rw_v_tex1 < 0 ||
            e->rw_tex1_loc < 0 || e->rw_u_tex_matrix < 0 ||
            e->rw_u_color_clamp < 0 || e->rw_u_env_color < 0)) return;
        if (kind == 4 && (e->rw_weights_loc < 0 || e->rw_indices_loc < 0 ||
            e->rw_u_bones < 0)) return;
    }
    e->rw_native_vs = kind;
    profile_inc(&g_prof_native_programs);
    if (profile_enabled())
        printf("PGL_PROFILE native_vs_ready kind=%d light=%d hash=%llx\n", kind, light, h);
}

static float rw_native_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static vec4 rw_native_m4f(const float *m, vec4 v);

static vec4 rw_native_m4(const glsl_cell *u, int off, vec4 v)
{
    return rw_native_m4f((const float *)(u + off), v);
}

static vec4 rw_native_m4f(const float *m, vec4 v)
{
#if RW_NATIVE_NEON
    float32x4_t c0 = vld1q_f32(m + 0);
    float32x4_t c1 = vld1q_f32(m + 4);
    float32x4_t c2 = vld1q_f32(m + 8);
    float32x4_t c3 = vld1q_f32(m + 12);
    float32x4_t r = vmulq_n_f32(c0, v.x);
    r = vmlaq_n_f32(r, c1, v.y);
    r = vmlaq_n_f32(r, c2, v.z);
    r = vmlaq_n_f32(r, c3, v.w);
    {
        vec4 out;
        vst1q_f32(&out.x, r);
        return out;
    }
#else
    vec4 r;
    r.x = m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12]*v.w;
    r.y = m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13]*v.w;
    r.z = m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w;
    r.w = m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w;
    return r;
#endif
}

/* Prepare the camera product for the draw.  The uniform block is mutable, so
 * callers invoke this immediately before each draw; unchanged camera
 * matrices are retained across the usual sequence of object draws. */
static void rw_native_prepare(interp_entry *e)
{
    const float *p, *v;
    const float *w;
    int row, col, k;
    if (!e || !e->rw_native_vs || e->rw_native_vs == 5 ||
        e->rw_u_view < 0 || e->rw_u_proj < 0) {
        if (e) {
            e->rw_view_proj_valid = e->rw_clip_matrix_valid = 0;
            e->rw_view_proj_cache_valid = 0;
            e->rw_runtime_lights = 0;
        }
        return;
    }
    p = (const float *)(e->ublock + e->rw_u_proj);
    v = (const float *)(e->ublock + e->rw_u_view);
    /* Camera matrices normally stay fixed while RenderWare submits many
     * objects.  Uniforms are mutable in place, so compare the two source
     * matrices before rebuilding P*V; this turns the repeated 4x4 product
     * into a small memcmp on the common multi-draw path. */
    if (!e->rw_view_proj_cache_valid ||
        memcmp(e->rw_cached_proj, p, 16 * sizeof(float)) != 0 ||
        memcmp(e->rw_cached_view, v, 16 * sizeof(float)) != 0) {
        for (col = 0; col < 4; ++col)
            for (row = 0; row < 4; ++row) {
                float x = 0.0f;
                for (k = 0; k < 4; ++k)
                    x += p[k*4 + row] * v[col*4 + k];
                e->rw_view_proj[col*4 + row] = x;
            }
        memcpy(e->rw_cached_proj, p, 16 * sizeof(float));
        memcpy(e->rw_cached_view, v, 16 * sizeof(float));
        e->rw_view_proj_cache_valid = 1;
    }
    e->rw_view_proj_valid = 1;
    e->rw_clip_matrix_valid = 0;
    /* Light arrays are uniform state, so determine once per draw whether the
     * native VS needs the normal transform and dynamic-light loop at all.
     * RenderWare keeps active lights packed from slot zero; an empty first
     * slot means the ambient/material path is the complete shader. */
    e->rw_runtime_lights = e->rw_native_full_lights &&
                           e->rw_u_light_params >= 0 &&
                           e->ublock[e->rw_u_light_params].f != 0.0f;
    /* IM3D's unlit shader does not use the intermediate world-space
     * position, so fold World into the cached P*V matrix. */
    if (e->rw_native_vs == 1 && e->rw_u_world >= 0) {
        w = (const float *)(e->ublock + e->rw_u_world);
        for (col = 0; col < 4; ++col)
            for (row = 0; row < 4; ++row) {
                float x = 0.0f;
                for (k = 0; k < 4; ++k)
                    x += e->rw_view_proj[k*4 + row] * w[col*4 + k];
                e->rw_clip_matrix[col*4 + row] = x;
            }
        e->rw_clip_matrix_valid = 1;
    }
}

static vec3 rw_native_m3(const glsl_cell *u, int off, vec3 v)
{
    const float *m = (const float *)(u + off);
#if RW_NATIVE_NEON
    float32x4_t c0 = {m[0], m[1], m[2], 0.0f};
    float32x4_t c1 = {m[4], m[5], m[6], 0.0f};
    float32x4_t c2 = {m[8], m[9], m[10], 0.0f};
    float32x4_t r = vmulq_n_f32(c0, v.x);
    r = vmlaq_n_f32(r, c1, v.y);
    r = vmlaq_n_f32(r, c2, v.z);
    return (vec3){vget_lane_f32(vget_low_f32(r), 0),
                  vget_lane_f32(vget_low_f32(r), 1),
                  vget_lane_f32(vget_high_f32(r), 0)};
#else
    vec3 r;
    r.x = m[0]*v.x + m[4]*v.y + m[8]*v.z;
    r.y = m[1]*v.x + m[5]*v.y + m[9]*v.z;
    r.z = m[2]*v.x + m[6]*v.y + m[10]*v.z;
    return r;
#endif
}

static float rw_native_comp(vec4 v, int i)
{
    return i == 0 ? v.x : (i == 1 ? v.y : (i == 2 ? v.z : v.w));
}

static vec3 rw_native_light(const interp_entry *e, const glsl_cell *u,
                            vec3 V, vec3 N)
{
    vec3 color = {0, 0, 0};
    int i;
    if (!e->rw_native_full_lights) return color;
    for (i = 0; i < 8; ++i) {
        const float *p = (const float *)(u + e->rw_u_light_params + i*4);
        float type = p[0];
        if (type == 0.0f) break;
        if (type == 1.0f) {
            const float *d = (const float *)(u + e->rw_u_light_dir + i*4);
            const float *c = (const float *)(u + e->rw_u_light_color + i*4);
            float l = fmaxf(0.0f, N.x*(-d[0]) + N.y*(-d[1]) + N.z*(-d[2]));
            color.x += l*c[0]; color.y += l*c[1]; color.z += l*c[2];
        } else if (type == 2.0f || type == 3.0f) {
            const float *lp = (const float *)(u + e->rw_u_light_pos + i*4);
            const float *d = (const float *)(u + e->rw_u_light_dir + i*4);
            const float *c = (const float *)(u + e->rw_u_light_color + i*4);
            vec3 dir = { V.x-lp[0], V.y-lp[1], V.z-lp[2] };
            float dist = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
            float atten = fmaxf(0.0f, 1.0f - dist/p[1]);
            vec3 nd = dir;
            if (dist > 0.0f) { nd.x/=dist; nd.y/=dist; nd.z/=dist; }
            float l = fmaxf(0.0f, N.x*(-nd.x) + N.y*(-nd.y) + N.z*(-nd.z));
            if (type == 3.0f) {
                float pcos = nd.x*d[0] + nd.y*d[1] + nd.z*d[2];
                float ccos = -p[2];
                float falloff = (pcos-ccos)/(1.0f-ccos);
                if (falloff < 0.0f) l = 0.0f;
                l *= fmaxf(falloff, p[3]);
                color.x = l*c[0]*atten; color.y = l*c[1]*atten; color.z = l*c[2]*atten;
                return color;
            }
            color.x += l*c[0]*atten; color.y += l*c[1]*atten; color.z += l*c[2]*atten;
        }
    }
    return color;
}

static void rw_native_vset(float *out, int slot, const float *v, int n)
{
    int i;
    if (slot < 0) return;
    for (i = 0; i < n; ++i) out[slot*4+i] = v[i];
}

static void rw_native_vertex(int worker, float *out, vec4 *attrs,
                             Shader_Builtins *b, void *user)
{
    interp_entry *e = (interp_entry *)user;
    const glsl_cell *u;
    vec4 pos, color, vertex, clip;
    float fog, tmp[4];
    (void)worker;
    if (!e || !e->gp || !e->rw_native_vs) return;
    u = e->ublock;
    /* The native contracts below write every varying consumed by the matching
     * simple/matfx fragment shader.  Do not clear the whole varying array for
     * every vertex; unused slots are never read by those exact contracts and
     * clearing them became a measurable CPU bandwidth cost in re3 scenes. */
    pos = attrs[e->rw_pos_loc];
    color = attrs[e->rw_color_loc];
    if (e->rw_native_vs == 5) {
        clip = pos;
        /* IM2D keeps the vertex colour unchanged.  This branch bypasses the
         * lighting block below, so initialise the fragment colour explicitly
         * before publishing the varying. */
        tmp[0] = color.x; tmp[1] = color.y;
        tmp[2] = color.z; tmp[3] = color.w;
        clip.x = clip.x * u[e->rw_u_xform+0].f + u[e->rw_u_xform+2].f;
        clip.y = clip.y * u[e->rw_u_xform+1].f + u[e->rw_u_xform+3].f;
        fog = rw_native_clamp((clip.w-u[e->rw_u_fog_data+1].f) *
                              u[e->rw_u_fog_data+2].f,
                              u[e->rw_u_fog_data+3].f, 1.0f);
        clip.x *= clip.w; clip.y *= clip.w; clip.z *= clip.w;
    } else {
        if (e->rw_native_vs == 4) {
            vec4 skinv = {0,0,0,0};
            vec3 skinn = {0,0,0};
            int j;
            for (j = 0; j < 4; ++j) {
                int bi = (int)rw_native_comp(attrs[e->rw_indices_loc], j);
                vec4 bp = rw_native_m4(u, e->rw_u_bones + bi*16,
                                        (vec4){pos.x,pos.y,pos.z,1.0f});
                vec3 bn = rw_native_m3(u, e->rw_u_bones + bi*16,
                                        (vec3){attrs[e->rw_normal_loc].x,
                                               attrs[e->rw_normal_loc].y,
                                               attrs[e->rw_normal_loc].z});
                float weight = rw_native_comp(attrs[e->rw_weights_loc], j);
                skinv.x += bp.x*weight;
                skinv.y += bp.y*weight;
                skinv.z += bp.z*weight;
                skinn.x += bn.x*weight;
                skinn.y += bn.y*weight;
                skinn.z += bn.z*weight;
            }
            vertex = rw_native_m4(u, e->rw_u_world, (vec4){skinv.x,skinv.y,skinv.z,1.0f});
            pos = (vec4){skinv.x,skinv.y,skinv.z,1.0f};
            attrs[e->rw_normal_loc] = (vec4){skinn.x,skinn.y,skinn.z,0.0f};
            /* Skinning changes the object-space position before the common
             * view/projection transform.  The old native path left `clip`
             * uninitialised here, allowing stack data to reach clipping and
             * the rasterizer for every skinned draw. */
            if (e->rw_view_proj_valid)
                clip = rw_native_m4f(e->rw_view_proj, vertex);
            else {
                clip = rw_native_m4(u, e->rw_u_view, vertex);
                clip = rw_native_m4(u, e->rw_u_proj, clip);
            }
        } else if (e->rw_native_vs == 1 && e->rw_clip_matrix_valid) {
            /* The unlit path does not consume world-space position. */
            vertex = pos;
            clip = rw_native_m4f(e->rw_clip_matrix, pos);
        } else {
            vertex = rw_native_m4(u, e->rw_u_world,
                                  (vec4){pos.x,pos.y,pos.z,1.0f});
            if (e->rw_view_proj_valid)
                clip = rw_native_m4f(e->rw_view_proj, vertex);
            else {
                clip = rw_native_m4(u, e->rw_u_view, vertex);
                clip = rw_native_m4(u, e->rw_u_proj, clip);
            }
        }
        fog = rw_native_clamp(((e->rw_native_vs == 4 ? clip.z : clip.w) -
                               u[e->rw_u_fog_data+1].f) * u[e->rw_u_fog_data+2].f,
                              u[e->rw_u_fog_data+3].f, 1.0f);
        if (e->rw_native_vs == 1) {
            tmp[0]=color.x;tmp[1]=color.y;tmp[2]=color.z;tmp[3]=color.w;
        } else {
            /* Most RenderWare material variants have no dynamic light blocks.
             * Their native VS still used to build a world-space normal for
             * every vertex before rw_native_light() immediately returned.
             * Skip that matrix multiply when the shader contract has no live
             * directional/point/spot lights; the ambient/material terms below
             * remain unchanged. */
            vec3 normal_world = {0.0f, 0.0f, 0.0f};
            vec3 dyn = {0.0f, 0.0f, 0.0f};
            if (e->rw_runtime_lights) {
                normal_world = rw_native_m3(u, e->rw_u_world,
                                            (vec3){attrs[e->rw_normal_loc].x,
                                                   attrs[e->rw_normal_loc].y,
                                                   attrs[e->rw_normal_loc].z});
                dyn = rw_native_light(e, u,
                                      (vec3){vertex.x,vertex.y,vertex.z}, normal_world);
            }
            float amb = u[e->rw_u_surf].f;
            float diff = u[e->rw_u_surf+2].f;
            const float *a = (const float *)(u + e->rw_u_amb);
            tmp[0]=rw_native_clamp(color.x+a[0]*amb+dyn.x*diff,0,1);
            tmp[1]=rw_native_clamp(color.y+a[1]*amb+dyn.y*diff,0,1);
            tmp[2]=rw_native_clamp(color.z+a[2]*amb+dyn.z*diff,0,1);
            tmp[3]=rw_native_clamp(color.w,0,1);
            if (e->rw_native_vs == 3) {
                /* MatFX needs the same world-space normal for environment
                 * mapping.  Reuse the dynamic-light result when lights are
                 * live; the old code multiplied the normal matrix twice for
                 * every vertex in lit MatFX scenes. */
                vec3 normal3 = e->rw_runtime_lights ? normal_world :
                    rw_native_m3(u, e->rw_u_world,
                                 (vec3){attrs[e->rw_normal_loc].x,
                                        attrs[e->rw_normal_loc].y,
                                        attrs[e->rw_normal_loc].z});
                vec4 ec = rw_native_m4(u, e->rw_u_tex_matrix,
                                       (vec4){normal3.x,normal3.y,normal3.z,1.0f});
                float env[4];
                const float *cc = (const float *)(u + e->rw_u_color_clamp);
                const float *ev = (const float *)(u + e->rw_u_env_color);
                env[0]=fmaxf(tmp[0],cc[0])*ev[0]; env[1]=fmaxf(tmp[1],cc[1])*ev[1];
                env[2]=fmaxf(tmp[2],cc[2])*ev[2]; env[3]=fmaxf(tmp[3],cc[3])*ev[3];
                rw_native_vset(out,e->rw_v_env,env,4);
                rw_native_vset(out,e->rw_v_tex1,(float[]){ec.x,ec.y},2);
            }
            tmp[0]*=u[e->rw_u_mat].f; tmp[1]*=u[e->rw_u_mat+1].f;
            tmp[2]*=u[e->rw_u_mat+2].f; tmp[3]*=u[e->rw_u_mat+3].f;
        }
    }
    b->gl_Position = clip;
    rw_native_vset(out,e->rw_v_color,tmp,4);
    rw_native_vset(out,e->rw_v_tex0,(float[]){attrs[e->rw_tex0_loc].x,
                                               attrs[e->rw_tex0_loc].y},2);
    rw_native_vset(out,e->rw_v_fog,&fog,1);
}

static int rw_source_equal(const char *a, const char *b)
{
    int na, nb, same = 0;
    char err[64];
    glsl_token *ta = glsl_lex(a, &na, err, sizeof err);
    glsl_token *tb = glsl_lex(b, &nb, err, sizeof err);
    if (ta && tb && na == nb) {
        same = 1;
        for (int i = 0; i < na; ++i)
            if (ta[i].kind != tb[i].kind || strcmp(ta[i].text, tb[i].text) ||
                ta[i].fval != tb[i].fval || ta[i].ival != tb[i].ival) {
                same = 0; break;
            }
    }
    free(ta); free(tb);
    return same;
}
static void rw_simple_frag(float *v, Shader_Builtins *b, void *unused)
{
    const interp_entry *e = g_active;
    const glsl_cell *u = e->ublock;
    const float *uv = v + e->rw_uv * 4, *col = v + e->rw_color * 4;
    float fog = v[e->rw_fog * 4];
    vec4 c = texture2D(gles_engine_texture_for_unit(u[e->rw_sampler].i), uv[0], 1.0f-uv[1]);
    (void)unused;
    c.x *= col[0]; c.y *= col[1]; c.z *= col[2]; c.w *= col[3];
    c.x = u[e->rw_fogcolor].f*(1.0f-fog) + c.x*fog;
    c.y = u[e->rw_fogcolor+1].f*(1.0f-fog) + c.y*fog;
    c.z = u[e->rw_fogcolor+2].f*(1.0f-fog) + c.z*fog;
    if(e->rw_simple==2) {
        const float *uv1=v+e->rw_uv1*4,*env=v+e->rw_envcolor*4;
        vec4 t=texture2D(gles_engine_texture_for_unit(u[e->rw_sampler1].i),uv1[0],1.0f-uv1[1]);
        float shine=u[e->rw_fx].f,fba=fmaxf(c.w,u[e->rw_fx+1].f);
        c.x=c.x*c.w+(env[0]*shine*t.x*fog)*fba;
        c.y=c.y*c.w+(env[1]*shine*t.y*fog)*fba;
        c.z=c.z*c.w+(env[2]*shine*t.z*fog)*fba;
    }
    if (e->rw_alpha_test && (c.w < u[e->rw_alpha].f || c.w >= u[e->rw_alpha+1].f)) b->discard = GL_TRUE;
    b->gl_FragColor = c;
}
static int rw_contract_match(const char *fs,const char *contract,int *alpha)
{
    *alpha=rw_source_equal(fs,contract);
    if(*alpha) return 1;
    const char *body="if(a < u_alphaRef.x || a >= u_alphaRef.y) discard;";
    const char *start=strstr(contract,body);
    char *noalpha=(char*)malloc(strlen(contract)+1);
    if(!noalpha) return 0;
    size_t prefix=(size_t)(start-contract);
    memcpy(noalpha,contract,prefix);strcpy(noalpha+prefix,start+strlen(body));
    int match=rw_source_equal(fs,noalpha);free(noalpha);return match;
}
static int rw_simple_body_match(const char *fs, int *alpha, int *kind)
{
    int writes_color;
    if (!fs || !alpha || !kind) return 0;
    /* gl3shader concatenates shaderDecl/header_frag_src with the body.  The
     * macro preamble and whitespace vary between GLES 1.00/3.10, so use the
     * stable RenderWare body expressions as a fallback after the strict token
     * contract misses.  Require all texture/fog terms to keep custom effects
     * out of the ordered RW raster path. */
    writes_color = strstr(fs, "FRAGCOLOR(color)") ||
                   strstr(fs, "gl_FragColor") || strstr(fs, "fragColor");
    if (strstr(fs, "v_color*texture") &&
        strstr(fs, "color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog)") &&
        writes_color) {
        *kind = 1;
        *alpha = !rw_source_define(fs, "NO_ALPHATEST");
        return 1;
    }
    if (strstr(fs, "vec4 pass1 = v_color") &&
        strstr(fs, "pass2 = v_envColor*shininess*texture") &&
        strstr(fs, "pass1.rgb = mix(u_fogColor.rgb, pass1.rgb, v_fog)") &&
        writes_color) {
        *kind = 2;
        *alpha = !rw_source_define(fs, "NO_ALPHATEST");
        return 1;
    }
    return 0;
}
static void rw_simple_link(GLuint id, const char *fs)
{
    interp_entry *e = NULL;
    const char *disable = getenv("PGL_RW_NATIVE");
    for (int i=0; i<GLES_MAX_INTERP; ++i)
        if (g_reg[i].in_use && g_reg[i].pgl_id == id) e = &g_reg[i];
    if (!e || (disable && !atoi(disable))) return;
    int alpha_test,kind=1;
    if(!rw_contract_match(fs,rw_simple_fs,&alpha_test)) {
        if(!rw_contract_match(fs,rw_matfx_fs,&alpha_test)) {
            /* The body matcher is deliberately conservative.  A custom
             * RenderWare effect can retain the standard texture/fog lines
             * while changing the color math; treating it as the ordered RW
             * kernel then produces valid but visibly corrupted pixels. */
            if (!rw_native_experimental_enabled()) return;
            if (!rw_simple_body_match(fs, &alpha_test, &kind)) return;
        } else kind=2;
    }
    e->rw_alpha_test=alpha_test;
    e->rw_uv = e->rw_color = e->rw_fog = -1;
    e->rw_uv1=e->rw_envcolor=-1;
    for (int i=0; i<e->gp->nvaryings; ++i) {
        const glsl_varying *v = &e->gp->varyings[i];
        if (!strcmp(v->name,"v_tex0") && v->ncomp==2) e->rw_uv = i;
        if (!strcmp(v->name,"v_color") && v->ncomp==4) e->rw_color = i;
        if (!strcmp(v->name,"v_fog") && v->ncomp==1) e->rw_fog = i;
        if (!strcmp(v->name,"v_tex1") && v->ncomp==2) e->rw_uv1 = i;
        if (!strcmp(v->name,"v_envColor") && v->ncomp==4) e->rw_envcolor = i;
    }
    int t = glsl_uniform_index(e->gp,"tex0");
    int a = glsl_uniform_index(e->gp,"u_alphaRef");
    int f = glsl_uniform_index(e->gp,"u_fogColor");
    if (t<0 || a<0 || f<0 || e->rw_uv<0 || e->rw_color<0 || e->rw_fog<0) return;
    e->rw_sampler=e->gp->uniforms[t].offset;
    e->rw_alpha=e->gp->uniforms[a].offset;
    e->rw_fogcolor=e->gp->uniforms[f].offset;
    if(kind==2) {
        int t1=glsl_uniform_index(e->gp,"tex1"),fx=glsl_uniform_index(e->gp,"u_fxparams");
        if(t1<0||fx<0||e->rw_uv1<0||e->rw_envcolor<0) return;
        e->rw_sampler1=e->gp->uniforms[t1].offset;e->rw_fx=e->gp->uniforms[fx].offset;
    }
    e->rw_simple=kind;
    g_ctx.programs.a[id].fragment_shader=rw_simple_frag;
    rw_native_vs_link(e);
    if (profile_enabled()) printf("PGL_PROFILE program=%u native RW %s FS\n",id,kind==2?"matfx":"simple");
}
