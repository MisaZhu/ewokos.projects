/*
 * cpu_raster.c - the software graphics pipeline.  For a recorded draw it runs
 * the pipeline's vertex shader per vertex (through the SPIR-V interpreter),
 * assembles primitives per the topology, rasterizes on the CPU (perspective
 * correct barycentric interpolation, viewport/scissor, back-face cull, depth
 * test, blend) and runs the fragment shader per covered fragment, writing into
 * the bound color/depth image memory.  Self-contained: no portablegl / mesa.
 *
 * Anything the interpreter declines (unsupported SPIR-V) or a format we cannot
 * decode/encode leaves the target untouched for that primitive -- the software
 * device never mis-renders, it declines.
 */
#include "cpu_priv.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- render target view -------------------------------------------------- */
typedef struct {
    uint8_t *color[VK_MAX_COLOR_ATTACHMENTS];
    VkFormat cfmt[VK_MAX_COLOR_ATTACHMENTS];
    uint32_t cbpp[VK_MAX_COLOR_ATTACHMENTS];
    uint32_t cpitch[VK_MAX_COLOR_ATTACHMENTS];
    uint32_t  num_color;
    uint8_t  *depth;
    VkFormat  dfmt;
    uint32_t  dbpp, dpitch;
    uint32_t  width, height;
} rt_state;

/* ---- attribute decode ---------------------------------------------------- */
static void decode_attr(VkFormat f, const uint8_t *p, float *out)
{
    out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
    switch (f) {
    case VK_FORMAT_R32_SFLOAT:
        out[0] = *(const float *)p; break;
    case VK_FORMAT_R32G32_SFLOAT:
        out[0] = ((const float *)p)[0]; out[1] = ((const float *)p)[1]; break;
    case VK_FORMAT_R32G32B32_SFLOAT:
        out[0] = ((const float *)p)[0]; out[1] = ((const float *)p)[1];
        out[2] = ((const float *)p)[2]; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        out[0] = ((const float *)p)[0]; out[1] = ((const float *)p)[1];
        out[2] = ((const float *)p)[2]; out[3] = ((const float *)p)[3]; break;
    case VK_FORMAT_R8_UNORM:
        out[0] = p[0] / 255.0f; break;
    case VK_FORMAT_R8G8_UNORM:
        out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f; break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f; out[3] = p[3] / 255.0f; break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f;
        out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f; break;
    case VK_FORMAT_R32_UINT:
        out[0] = (float)*(const uint32_t *)p; break;
    case VK_FORMAT_R32G32_UINT:
        out[0] = (float)((const uint32_t *)p)[0];
        out[1] = (float)((const uint32_t *)p)[1]; break;
    case VK_FORMAT_R32G32B32_UINT:
        out[0] = (float)((const uint32_t *)p)[0];
        out[1] = (float)((const uint32_t *)p)[1];
        out[2] = (float)((const uint32_t *)p)[2]; break;
    case VK_FORMAT_R32G32B32A32_UINT:
        out[0] = (float)((const uint32_t *)p)[0];
        out[1] = (float)((const uint32_t *)p)[1];
        out[2] = (float)((const uint32_t *)p)[2];
        out[3] = (float)((const uint32_t *)p)[3]; break;
    case VK_FORMAT_R16_SFLOAT: {
        /* half -> float */
        uint16_t h = *(const uint16_t *)p;
        uint32_t sign = (uint32_t)(h & 0x8000) << 16;
        uint32_t exp = (h >> 10) & 0x1f, man = h & 0x3ff;
        uint32_t f32;
        if (exp == 0) f32 = sign;                    /* flush denormals to 0 */
        else if (exp == 31) f32 = sign | 0x7f800000 | (man << 13);
        else f32 = sign | ((exp + 112) << 23) | (man << 13);
        memcpy(&out[0], &f32, 4);
        break;
    }
    default:
        break;   /* unsupported attribute format: leaves default (decline) */
    }
}

/* ---- color encode -------------------------------------------------------- */
static uint8_t f2u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static int encode_color(VkFormat f, uint8_t *p, const float *rgba)
{
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
        p[0] = f2u8(rgba[0]); p[1] = f2u8(rgba[1]);
        p[2] = f2u8(rgba[2]); p[3] = f2u8(rgba[3]); return 1;
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
        p[0] = f2u8(rgba[2]); p[1] = f2u8(rgba[1]);
        p[2] = f2u8(rgba[0]); p[3] = f2u8(rgba[3]); return 1;
    case VK_FORMAT_R8G8B8_UNORM:
        p[0] = f2u8(rgba[0]); p[1] = f2u8(rgba[1]); p[2] = f2u8(rgba[2]); return 1;
    case VK_FORMAT_B8G8R8_UNORM:
        p[0] = f2u8(rgba[2]); p[1] = f2u8(rgba[1]); p[2] = f2u8(rgba[0]); return 1;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        ((float *)p)[0] = rgba[0]; ((float *)p)[1] = rgba[1];
        ((float *)p)[2] = rgba[2]; ((float *)p)[3] = rgba[3]; return 1;
    case VK_FORMAT_R32G32B32_SFLOAT:
        ((float *)p)[0] = rgba[0]; ((float *)p)[1] = rgba[1];
        ((float *)p)[2] = rgba[2]; return 1;
    case VK_FORMAT_R8_UNORM:
        p[0] = f2u8(rgba[0]); return 1;
    default:
        return 0;   /* unsupported target format: decline the write */
    }
}

static void decode_color(VkFormat f, const uint8_t *p, float *rgba)
{
    rgba[0] = rgba[1] = rgba[2] = 0.0f; rgba[3] = 1.0f;
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
        rgba[0] = p[0]/255.0f; rgba[1] = p[1]/255.0f;
        rgba[2] = p[2]/255.0f; rgba[3] = p[3]/255.0f; break;
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
        rgba[0] = p[2]/255.0f; rgba[1] = p[1]/255.0f;
        rgba[2] = p[0]/255.0f; rgba[3] = p[3]/255.0f; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        rgba[0] = ((const float*)p)[0]; rgba[1] = ((const float*)p)[1];
        rgba[2] = ((const float*)p)[2]; rgba[3] = ((const float*)p)[3]; break;
    default: break;
    }
}

/* ---- depth access -------------------------------------------------------- */
static int depth_read(const rt_state *rt, uint32_t x, uint32_t y, float *z)
{
    const uint8_t *p;
    if (!rt->depth) return 0;
    p = rt->depth + (size_t)y * rt->dpitch + (size_t)x * rt->dbpp;
    switch (rt->dfmt) {
    case VK_FORMAT_D32_SFLOAT: *z = *(const float *)p; return 1;
    case VK_FORMAT_D16_UNORM: *z = *(const uint16_t *)p / 65535.0f; return 1;
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        *z = (*(const uint32_t *)p & 0x00ffffff) / 16777215.0f; return 1;
    default: return 0;
    }
}

static void depth_write(const rt_state *rt, uint32_t x, uint32_t y, float z)
{
    uint8_t *p;
    if (!rt->depth) return;
    p = rt->depth + (size_t)y * rt->dpitch + (size_t)x * rt->dbpp;
    switch (rt->dfmt) {
    case VK_FORMAT_D32_SFLOAT: *(float *)p = z; break;
    case VK_FORMAT_D16_UNORM: {
        float c = z < 0 ? 0 : (z > 1 ? 1 : z);
        *(uint16_t *)p = (uint16_t)(c * 65535.0f + 0.5f); break;
    }
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32: {
        float c = z < 0 ? 0 : (z > 1 ? 1 : z);
        uint32_t d = (uint32_t)(c * 16777215.0f + 0.5f);
        uint32_t old = *(uint32_t *)p;
        *(uint32_t *)p = (old & 0xff000000u) | (d & 0x00ffffffu);
        break;
    }
    default: break;
    }
}

/* ---- blend --------------------------------------------------------------- */
static float blend_factor(VkBlendFactor f, float src, float dst, float sa, float da)
{
    switch (f) {
    case VK_BLEND_FACTOR_ZERO: return 0.0f;
    case VK_BLEND_FACTOR_ONE: return 1.0f;
    case VK_BLEND_FACTOR_SRC_COLOR: return src;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return 1.0f - src;
    case VK_BLEND_FACTOR_DST_COLOR: return dst;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return 1.0f - dst;
    case VK_BLEND_FACTOR_SRC_ALPHA: return sa;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return 1.0f - sa;
    case VK_BLEND_FACTOR_DST_ALPHA: return da;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return 1.0f - da;
    case VK_BLEND_FACTOR_CONSTANT_COLOR: return 1.0f;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE: {
        float t = 1.0f - da; return sa < t ? sa : t;
    }
    default: return 1.0f;
    }
}

static void blend_pixel(const vk_pipeline *p, float *out, const float *src,
                        const float *dst)
{
    float sa = src[3], da = dst[3];
    int i;
    if (!p->blend_enable) { memcpy(out, src, 4 * sizeof(float)); return; }
    for (i = 0; i < 3; i++) {
        float fs = blend_factor(p->src_color, src[i], dst[i], sa, da);
        float fd = blend_factor(p->dst_color, src[i], dst[i], sa, da);
        float r = (p->color_op == VK_BLEND_OP_ADD) ? src[i]*fs + dst[i]*fd
                : (p->color_op == VK_BLEND_OP_SUBTRACT) ? src[i]*fs - dst[i]*fd
                : (p->color_op == VK_BLEND_OP_REVERSE_SUBTRACT) ? dst[i]*fd - src[i]*fs
                : (p->color_op == VK_BLEND_OP_MIN) ? (src[i] < dst[i] ? src[i] : dst[i])
                : (src[i] > dst[i] ? src[i] : dst[i]);
        out[i] = r;
    }
    {
        float fs = blend_factor(p->src_alpha, sa, da, sa, da);
        float fd = blend_factor(p->dst_alpha, sa, da, sa, da);
        out[3] = (p->alpha_op == VK_BLEND_OP_ADD) ? sa*fs + da*fd
               : (p->alpha_op == VK_BLEND_OP_SUBTRACT) ? sa*fs - da*fd
               : sa + da*(1.0f - sa);
    }
}

/* ---- vertex output ------------------------------------------------------- */
typedef struct {
    float clip[4];                     /* raw clip-space position (VS out)   */
    float sx, sy, sz;                  /* framebuffer-space position         */
    float inv_w;
    float vary[CPU_MAX_VARYING][4];    /* perspective-divided varyings        */
    int   valid;
} vtx_out;

/* ---- per-vertex shading -------------------------------------------------- */
static int shade_vertex(vk_pipeline *p, vk_draw_ctx *dc, const spirv_module *mod,
                        const spirv_entry *e, uint32_t vindex, uint32_t instance,
                        vtx_out *o)
{
    spirv_resources res;
    uint32_t i;
    memset(&res, 0, sizeof(res));
    if (cpu_fill_bindings(dc, &res) != 0)
        return -1;
    res.vertex_index = vindex;
    res.instance_index = instance;

    /* fetch vertex attributes */
    for (i = 0; i < p->num_vtx_attrs; i++) {
        const vk_vertex_attr *a = &p->vtx_attrs[i];
        uint32_t b, stride = 0, found = 0;
        const uint8_t *base;
        if (a->binding >= VK_MAX_VERTEX_BINDINGS || !dc->vb[a->binding])
            continue;
        for (b = 0; b < p->num_vtx_bindings; b++)
            if (p->vtx_bindings[b].binding == a->binding) {
                stride = p->vtx_bindings[b].stride; found = 1; break;
            }
        if (!found) stride = 0;
        base = (const uint8_t *)vk_buffer_ptr(dc->vb[a->binding]);
        if (!base) continue;
        base += dc->vb_off[a->binding] + (size_t)vindex * stride + a->offset;
        if (a->location < SPIRV_MAX_LOCATIONS) {
            float v[4];
            decode_attr(a->format, base, v);
            res.in_loc[a->location][0] = v[0]; res.in_loc[a->location][1] = v[1];
            res.in_loc[a->location][2] = v[2]; res.in_loc[a->location][3] = v[3];
            if (a->location + 1 > res.num_in_loc) res.num_in_loc = a->location + 1;
        }
    }

    if (spirv_run(mod, e, &res) != 0)
        return -1;
    if (!res.wrote_position)
        return -1;

    o->clip[0] = res.position[0]; o->clip[1] = res.position[1];
    o->clip[2] = res.position[2]; o->clip[3] = res.position[3];
    for (i = 0; i < SPIRV_MAX_LOCATIONS; i++) {
        o->vary[i][0] = res.out_loc[i][0]; o->vary[i][1] = res.out_loc[i][1];
        o->vary[i][2] = res.out_loc[i][2]; o->vary[i][3] = res.out_loc[i][3];
    }
    o->valid = 1;
    return 0;
}

/* viewport transform: clip -> framebuffer space (Vulkan conventions). */
static void transform_vertex(const VkViewport *vp, vtx_out *o)
{
    float w = o->clip[3];
    float iw = (w != 0.0f) ? 1.0f / w : 0.0f;
    float ndcx = o->clip[0] * iw, ndcy = o->clip[1] * iw, ndcz = o->clip[2] * iw;
    o->inv_w = iw;
    o->sx = vp->x + (vp->width  * 0.5f) * (ndcx + 1.0f);
    o->sy = vp->y + (vp->height * 0.5f) * (ndcy + 1.0f);
    o->sz = vp->minDepth + ndcz * (vp->maxDepth - vp->minDepth);
}

/* ---- fragment shading ---------------------------------------------------- */
static int shade_fragment(vk_pipeline *p, vk_draw_ctx *dc, const spirv_module *mod,
                          const spirv_entry *e, float fx, float fy, float fz,
                          float inv_w, const float vary[CPU_MAX_VARYING][4],
                          int front_facing, float out_rgba[4])
{
    spirv_resources res;
    uint32_t i;
    (void)p;
    memset(&res, 0, sizeof(res));
    if (cpu_fill_bindings(dc, &res) != 0)
        return -1;
    res.frag_coord[0] = fx; res.frag_coord[1] = fy;
    res.frag_coord[2] = fz; res.frag_coord[3] = inv_w;
    res.front_facing = front_facing;
    for (i = 0; i < SPIRV_MAX_LOCATIONS; i++) {
        res.in_loc[i][0] = vary[i][0]; res.in_loc[i][1] = vary[i][1];
        res.in_loc[i][2] = vary[i][2]; res.in_loc[i][3] = vary[i][3];
    }
    res.num_in_loc = SPIRV_MAX_LOCATIONS;
    if (e) {
        if (spirv_run(mod, e, &res) != 0)
            return -1;
        if (res.killed)
            return 1;   /* discarded */
        out_rgba[0] = res.out_loc[0][0]; out_rgba[1] = res.out_loc[0][1];
        out_rgba[2] = res.out_loc[0][2]; out_rgba[3] = res.out_loc[0][3];
    } else {
        /* no fragment shader: pass through the first varying as color */
        out_rgba[0] = vary[0][0]; out_rgba[1] = vary[0][1];
        out_rgba[2] = vary[0][2]; out_rgba[3] = vary[0][3];
    }
    return 0;
}

static void write_fragment(vk_pipeline *p, const rt_state *rt, uint32_t x,
                           uint32_t y, float z, const float rgba[4])
{
    /* depth test */
    if (p->depth_test_enable && rt->depth) {
        float oldz;
        if (depth_read(rt, x, y, &oldz)) {
            int pass;
            switch (p->depth_compare) {
            case VK_COMPARE_OP_LESS: pass = z < oldz; break;
            case VK_COMPARE_OP_LESS_OR_EQUAL: pass = z <= oldz; break;
            case VK_COMPARE_OP_GREATER: pass = z > oldz; break;
            case VK_COMPARE_OP_GREATER_OR_EQUAL: pass = z >= oldz; break;
            case VK_COMPARE_OP_EQUAL: pass = z == oldz; break;
            case VK_COMPARE_OP_NOT_EQUAL: pass = z != oldz; break;
            case VK_COMPARE_OP_ALWAYS: pass = 1; break;
            default: pass = 1; break;   /* NEVER -> fail */
            }
            if (p->depth_compare == VK_COMPARE_OP_NEVER) pass = 0;
            if (!pass) return;
        }
        if (p->depth_write_enable)
            depth_write(rt, x, y, z);
    }
    if (rt->num_color && rt->color[0]) {
        uint8_t *px = rt->color[0] + (size_t)y * rt->cpitch[0] + (size_t)x * rt->cbpp[0];
        if (p->blend_enable) {
            float dst[4], outc[4];
            decode_color(rt->cfmt[0], px, dst);
            blend_pixel(p, outc, rgba, dst);
            encode_color(rt->cfmt[0], px, outc);
        } else {
            encode_color(rt->cfmt[0], px, rgba);
        }
    }
}

/* ---- triangle rasterization ---------------------------------------------- */
static float edge(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

static void raster_triangle(vk_pipeline *p, vk_draw_ctx *dc, const rt_state *rt,
                            const spirv_module *fmod, const spirv_entry *fe,
                            vtx_out *v0, vtx_out *v1, vtx_out *v2,
                            const VkViewport *vp, const VkRect2D *sc, int has_sc)
{
    float area = edge(v0->sx, v0->sy, v1->sx, v1->sy, v2->sx, v2->sy);
    float minx, miny, maxx, maxy;
    int x0i, y0i, x1i, y1i, px, py, front;
    if (area == 0.0f) return;

    /* back-face culling (area sign in framebuffer space, y-down) */
    front = (area < 0.0f) ? 1 : 0;
    if (p->cull_mode == VK_CULL_MODE_BACK_BIT) {
        int cw = (p->front_face == VK_FRONT_FACE_CLOCKWISE);
        if (cw ? front : !front) return;
    } else if (p->cull_mode == VK_CULL_MODE_FRONT_BIT) {
        int cw = (p->front_face == VK_FRONT_FACE_CLOCKWISE);
        if (cw ? !front : front) return;
    } else if (p->cull_mode == VK_CULL_MODE_FRONT_AND_BACK) {
        return;
    }

    minx = v0->sx; if (v1->sx < minx) minx = v1->sx; if (v2->sx < minx) minx = v2->sx;
    maxx = v0->sx; if (v1->sx > maxx) maxx = v1->sx; if (v2->sx > maxx) maxx = v2->sx;
    miny = v0->sy; if (v1->sy < miny) miny = v1->sy; if (v2->sy < miny) miny = v2->sy;
    maxy = v0->sy; if (v1->sy > maxy) maxy = v1->sy; if (v2->sy > maxy) maxy = v2->sy;

    /* clamp to viewport + scissor + target */
    {
        float lox = vp->x, loy = vp->y;
        float hix = vp->x + vp->width, hiy = vp->y + vp->height;
        if (has_sc) {
            if (sc->offset.x > lox) lox = (float)sc->offset.x;
            if (sc->offset.y > loy) loy = (float)sc->offset.y;
            if (sc->offset.x + sc->extent.width < hix) hix = (float)(sc->offset.x + sc->extent.width);
            if (sc->offset.y + sc->extent.height < hiy) hiy = (float)(sc->offset.y + sc->extent.height);
        }
        if (minx < lox) minx = lox;
        if (maxx > hix) maxx = hix;
        if (miny < loy) miny = loy;
        if (maxy > hiy) maxy = hiy;
        if (rt->width && maxx > (float)rt->width) maxx = (float)rt->width;
        if (rt->height && maxy > (float)rt->height) maxy = (float)rt->height;
    }
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;

    x0i = (int)floorf(minx); y0i = (int)floorf(miny);
    x1i = (int)ceilf(maxx);  y1i = (int)ceilf(maxy);
    if (x1i > (int)rt->width) x1i = (int)rt->width;
    if (y1i > (int)rt->height) y1i = (int)rt->height;

    for (py = y0i; py < y1i; py++) {
        for (px = x0i; px < x1i; px++) {
            float cx = px + 0.5f, cy = py + 0.5f;
            float w0 = edge(v1->sx, v1->sy, v2->sx, v2->sy, cx, cy);
            float w1 = edge(v2->sx, v2->sy, v0->sx, v0->sy, cx, cy);
            float w2 = edge(v0->sx, v0->sy, v1->sx, v1->sy, cx, cy);
            float b0, b1, b2, iw, z, vary[CPU_MAX_VARYING][4], rgba[4];
            uint32_t li;
            int st;
            if (area > 0.0f) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else               { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }
            b0 = w0 / area; b1 = w1 / area; b2 = w2 / area;

            /* perspective-correct interpolation */
            iw = b0*v0->inv_w + b1*v1->inv_w + b2*v2->inv_w;
            z = b0*v0->sz + b1*v1->sz + b2*v2->sz;
            for (li = 0; li < CPU_MAX_VARYING; li++) {
                int k;
                float acc[4] = {0,0,0,0};
                for (k = 0; k < 4; k++)
                    acc[k] = (b0*v0->inv_w*v0->vary[li][k] +
                              b1*v1->inv_w*v1->vary[li][k] +
                              b2*v2->inv_w*v2->vary[li][k]);
                if (iw != 0.0f) {
                    vary[li][0]=acc[0]/iw; vary[li][1]=acc[1]/iw;
                    vary[li][2]=acc[2]/iw; vary[li][3]=acc[3]/iw;
                } else {
                    vary[li][0]=vary[li][1]=vary[li][2]=vary[li][3]=0.0f;
                }
            }
            st = shade_fragment(p, dc, fmod, fe, cx, cy, z, iw, vary, front, rgba);
            if (st != 0) continue;   /* decline or discard */
            write_fragment(p, rt, (uint32_t)px, (uint32_t)py, z, rgba);
        }
    }
}

/* ---- line / point rasterization ------------------------------------------ */
static void raster_point(vk_pipeline *p, vk_draw_ctx *dc, const rt_state *rt,
                         const spirv_module *fmod, const spirv_entry *fe,
                         vtx_out *v, const VkRect2D *sc, int has_sc)
{
    int px = (int)v->sx, py = (int)v->sy;
    float vary[CPU_MAX_VARYING][4], rgba[4];
    uint32_t li;
    if (px < 0 || py < 0 || px >= (int)rt->width || py >= (int)rt->height) return;
    if (has_sc && (px < sc->offset.x || py < sc->offset.y ||
        px >= sc->offset.x + (int)sc->extent.width ||
        py >= sc->offset.y + (int)sc->extent.height)) return;
    for (li = 0; li < CPU_MAX_VARYING; li++)
        memcpy(vary[li], v->vary[li], 4 * sizeof(float));
    if (shade_fragment(p, dc, fmod, fe, v->sx, v->sy, v->sz, v->inv_w, vary, 1, rgba) == 0)
        write_fragment(p, rt, (uint32_t)px, (uint32_t)py, v->sz, rgba);
}

static void raster_line(vk_pipeline *p, vk_draw_ctx *dc, const rt_state *rt,
                        const spirv_module *fmod, const spirv_entry *fe,
                        vtx_out *a, vtx_out *b, const VkRect2D *sc, int has_sc)
{
    float dx = b->sx - a->sx, dy = b->sy - a->sy;
    int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    int i;
    if (steps < 1) steps = 1;
    for (i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        vtx_out v;
        float vary[CPU_MAX_VARYING][4], rgba[4];
        uint32_t li;
        int px, py;
        v.sx = a->sx + dx * t; v.sy = a->sy + dy * t; v.sz = a->sz + (b->sz - a->sz) * t;
        v.inv_w = a->inv_w + (b->inv_w - a->inv_w) * t;
        for (li = 0; li < CPU_MAX_VARYING; li++) {
            int k;
            for (k = 0; k < 4; k++)
                v.vary[li][k] = a->vary[li][k] + (b->vary[li][k] - a->vary[li][k]) * t;
        }
        px = (int)v.sx; py = (int)v.sy;
        if (px < 0 || py < 0 || px >= (int)rt->width || py >= (int)rt->height) continue;
        if (has_sc && (px < sc->offset.x || py < sc->offset.y ||
            px >= sc->offset.x + (int)sc->extent.width ||
            py >= sc->offset.y + (int)sc->extent.height)) continue;
        for (li = 0; li < CPU_MAX_VARYING; li++)
            memcpy(vary[li], v.vary[li], 4 * sizeof(float));
        if (shade_fragment(p, dc, fmod, fe, v.sx, v.sy, v.sz, v.inv_w, vary, 1, rgba) == 0)
            write_fragment(p, rt, (uint32_t)px, (uint32_t)py, v.sz, rgba);
    }
}

/* ---- render target setup ------------------------------------------------- */
static int setup_rt(vk_draw_ctx *dc, rt_state *rt)
{
    uint32_t i;
    memset(rt, 0, sizeof(*rt));
    rt->num_color = dc->num_color;
    for (i = 0; i < dc->num_color && i < VK_MAX_COLOR_ATTACHMENTS; i++) {
        vk_image_view *iv = dc->color[i];
        vk_image *im;
        if (!iv || !iv->image) return -1;
        im = iv->image;
        rt->color[i] = (uint8_t *)vk_image_ptr(im);
        if (!rt->color[i]) return -1;
        rt->cfmt[i] = im->format;
        rt->cbpp[i] = im->bpp ? im->bpp : vk_format_bpp(im->format);
        rt->cpitch[i] = rt->cbpp[i] * im->extent.width;
        if (!rt->cbpp[i]) return -1;
        rt->width = im->extent.width;
        rt->height = im->extent.height;
    }
    if (dc->depth && dc->depth->image) {
        vk_image *im = dc->depth->image;
        rt->depth = (uint8_t *)vk_image_ptr(im);
        if (rt->depth) {
            rt->dfmt = im->format;
            rt->dbpp = im->bpp ? im->bpp : vk_format_bpp(im->format);
            rt->dpitch = rt->dbpp * im->extent.width;
        }
    }
    if (dc->rt_width) rt->width = dc->rt_width;
    if (dc->rt_height) rt->height = dc->rt_height;
    return 0;
}

/* ---- main draw entry ----------------------------------------------------- */
VkResult cpu_draw(vk_command_buffer *cb, vk_draw_ctx *dc,
                  uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount, uint32_t firstInstance,
                  const uint32_t *index_data, VkIndexType index_type)
{
    vk_pipeline *p = dc->pipeline;
    const spirv_module *vmod = NULL, *fmod = NULL;
    const spirv_entry *ve = NULL, *fe = NULL;
    rt_state rt;
    VkViewport vp;
    VkRect2D sc;
    int has_sc = dc->has_sc;
    uint32_t i, inst;
    uint32_t prim_count;

    /* index_data (when non-NULL) is an expanded uint32 index array; otherwise
     * vertices are drawn in order from firstVertex. */
#define VIDX(k) (index_data ? index_data[(k)] : (firstVertex + (k)))

    (void)cb; (void)index_type;
    if (!p) return VK_ERROR_INITIALIZATION_FAILED;

    /* locate shader stages */
    for (i = 0; i < p->num_stages; i++) {
        if (p->stages[i].stage == VK_SHADER_STAGE_VERTEX_BIT) {
            if (p->stages[i].module && p->stages[i].module->module) {
                vmod = p->stages[i].module->module;
                ve = spirv_find_entry(vmod, p->stages[i].entry[0] ? p->stages[i].entry : NULL,
                                      SPIRV_EXEC_VERTEX);
            }
        } else if (p->stages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            if (p->stages[i].module && p->stages[i].module->module) {
                fmod = p->stages[i].module->module;
                fe = spirv_find_entry(fmod, p->stages[i].entry[0] ? p->stages[i].entry : NULL,
                                      SPIRV_EXEC_FRAGMENT);
            }
        }
    }
    if (!vmod || !ve) return VK_SUCCESS;   /* decline: no vertex shader */
    if (p->rasterizer_discard) return VK_SUCCESS;
    if (setup_rt(dc, &rt) != 0) return VK_SUCCESS;
    if (!rt.width || !rt.height) return VK_SUCCESS;

    vp = dc->has_vp ? dc->vp : (VkViewport){0, 0, (float)rt.width, (float)rt.height, 0.0f, 1.0f};
    sc = dc->sc;

    prim_count = vertexCount;

    for (inst = firstInstance; inst < firstInstance + instanceCount; inst++) {
        switch (p->topology) {
        case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: {
            for (i = 0; i < prim_count; i++) {
                vtx_out o;
                uint32_t vi = VIDX(i);
                if (shade_vertex(p, dc, vmod, ve, vi, inst, &o) != 0) continue;
                transform_vertex(&vp, &o);
                raster_point(p, dc, &rt, fmod, fe, &o, &sc, has_sc);
            }
            break;
        }
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: {
            for (i = 0; i + 1 < prim_count; i += 2) {
                vtx_out a, b;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i), inst, &a) != 0) continue;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i + 1), inst, &b) != 0) continue;
                transform_vertex(&vp, &a); transform_vertex(&vp, &b);
                raster_line(p, dc, &rt, fmod, fe, &a, &b, &sc, has_sc);
            }
            break;
        }
        case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: {
            vtx_out prev; int have = 0;
            for (i = 0; i < prim_count; i++) {
                vtx_out cur;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i), inst, &cur) != 0) { have = 0; continue; }
                transform_vertex(&vp, &cur);
                if (have) raster_line(p, dc, &rt, fmod, fe, &prev, &cur, &sc, has_sc);
                prev = cur; have = 1;
            }
            break;
        }
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        default: {
            for (i = 0; i + 2 < prim_count; i += 3) {
                vtx_out a, b, c;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i), inst, &a) != 0) continue;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i + 1), inst, &b) != 0) continue;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i + 2), inst, &c) != 0) continue;
                transform_vertex(&vp, &a); transform_vertex(&vp, &b); transform_vertex(&vp, &c);
                raster_triangle(p, dc, &rt, fmod, fe, &a, &b, &c, &vp, &sc, has_sc);
            }
            break;
        }
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: {
            vtx_out v[3]; int n = 0; uint32_t k = 0;
            for (i = 0; i < prim_count; i++) {
                vtx_out o;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i), inst, &o) != 0) { n = 0; continue; }
                transform_vertex(&vp, &o);
                if (n < 3) { v[n++] = o; }
                else { v[k] = o; k = (k + 1) % 3; }
                if (n == 3) {
                    vtx_out *t0 = &v[(k + 0) % 3], *t1 = &v[(k + 1) % 3], *t2 = &v[(k + 2) % 3];
                    if (i >= 2)
                        raster_triangle(p, dc, &rt, fmod, fe, t0, t1, t2, &vp, &sc, has_sc);
                }
            }
            break;
        }
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: {
            vtx_out center, prev; int have = 0;
            for (i = 0; i < prim_count; i++) {
                vtx_out cur;
                if (shade_vertex(p, dc, vmod, ve, VIDX(i), inst, &cur) != 0) { have = 0; continue; }
                transform_vertex(&vp, &cur);
                if (i == 0) { center = cur; }
                else if (have) raster_triangle(p, dc, &rt, fmod, fe, &center, &prev, &cur, &vp, &sc, has_sc);
                prev = cur; have = 1;
            }
            break;
        }
        }
    }
#undef VIDX
    return VK_SUCCESS;
}
