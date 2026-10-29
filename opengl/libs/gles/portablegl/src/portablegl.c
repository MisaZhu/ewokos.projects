#define PGLDEF
#include "../include/portablegl/portablegl.h"
#ifdef PGL_G3D
#include "../include/portablegl/pgl_g3d.h"
#endif

// Fast math function implementations
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif


// Fast sine approximation using lookup table with linear interpolation
#define SIN_TABLE_SIZE 1024
static float sin_table[SIN_TABLE_SIZE];
static int sin_table_initialized = 0;

static void init_sin_table(void) {
    if (sin_table_initialized) return;
    for (int i = 0; i < SIN_TABLE_SIZE; i++) {
        float x = (float)i / SIN_TABLE_SIZE * 2.0f * M_PI;
        sin_table[i] = (float)sin(x);
    }
    sin_table_initialized = 1;
}

inline float pgl_fast_sin(float x) {
    if (!sin_table_initialized) init_sin_table();
    
    // Normalize to [0, 2*PI)
    float x_norm = x / (2.0f * M_PI);
    x_norm = x_norm - (int)x_norm;
    if (x_norm < 0) x_norm += 1.0f;
    
    // Lookup with linear interpolation
    float idx = x_norm * SIN_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = (i0 + 1) & (SIN_TABLE_SIZE - 1);
    float t = idx - i0;
    
    return sin_table[i0] * (1.0f - t) + sin_table[i1] * t;
}

// Fast cosine using lookup table with linear interpolation
#define COS_TABLE_SIZE 1024
static float cos_table[COS_TABLE_SIZE];
static int cos_table_initialized = 0;

static void init_cos_table(void) {
    if (cos_table_initialized) return;
    for (int i = 0; i < COS_TABLE_SIZE; i++) {
        float x = (float)i / COS_TABLE_SIZE * 2.0f * M_PI;
        cos_table[i] = (float)cos(x);
    }
    cos_table_initialized = 1;
}

inline float pgl_fast_cos(float x) {
    if (!cos_table_initialized) init_cos_table();
    
    // Normalize to [0, 2*PI)
    float x_norm = x / (2.0f * M_PI);
    x_norm = x_norm - (int)x_norm;
    if (x_norm < 0) x_norm += 1.0f;
    
    // Lookup with linear interpolation
    float idx = x_norm * COS_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = (i0 + 1) & (COS_TABLE_SIZE - 1);
    float t = idx - i0;
    
    return cos_table[i0] * (1.0f - t) + cos_table[i1] * t;
}

// Fast sqrt using Quake III Arena method with Newton iterations for better precision
inline float pgl_fast_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; int i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    float x2 = x * 0.5f;
    float y = u.f;
    // First Newton iteration
    y = y * (1.5f - (x2 * y * y));
    // Second iteration for better precision
    //y = y * (1.5f - (x2 * y * y));
    // Third iteration for maximum precision
    //y = y * (1.5f - (x2 * y * y));
    return y * x;
}

// Fast tan using sin/cos ratio
inline float pgl_fast_tan(float x) {
    float sx = pgl_fast_sin(x);
    float cx = pgl_fast_cos(x);
    if (cx == 0.0f) return 0.0f;
    return sx / cx;
}

// Fast acos approximation using lookup table with linear interpolation
// Valid for [-1, 1], returns [0, PI]
#define ACOS_TABLE_SIZE 1024
static float acos_table[ACOS_TABLE_SIZE + 1];
static int acos_table_initialized = 0;

static void init_acos_table(void) {
    if (acos_table_initialized) return;
    for (int i = 0; i <= ACOS_TABLE_SIZE; i++) {
        float x = (float)i / ACOS_TABLE_SIZE * 2.0f - 1.0f; // Map [0, 1024] to [-1, 1]
        acos_table[i] = (float)acos(x);
    }
    acos_table_initialized = 1;
}

inline float pgl_fast_acos(float x) {
    if (!acos_table_initialized) init_acos_table();
    
    // Clamp input to [-1, 1]
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f) x = 1.0f;
    
    // Map [-1, 1] to [0, ACOS_TABLE_SIZE]
    float idx = (x + 1.0f) * 0.5f * ACOS_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = i0 + 1;
    if (i1 > ACOS_TABLE_SIZE) i1 = ACOS_TABLE_SIZE;
    float t = idx - i0;
    
    return acos_table[i0] * (1.0f - t) + acos_table[i1] * t;
}

// Fast asin using acos: asin(x) = PI/2 - acos(x)
inline float pgl_fast_asin(float x) {
    return M_PI / 2.0f - pgl_fast_acos(x);
}

// Fast atan approximation using lookup table with linear interpolation
// Valid for all real numbers, returns [-PI/2, PI/2]
#define ATAN_TABLE_SIZE 1024
static float atan_table[ATAN_TABLE_SIZE + 1];
static int atan_table_initialized = 0;

static void init_atan_table(void) {
    if (atan_table_initialized) return;
    for (int i = 0; i <= ATAN_TABLE_SIZE; i++) {
        // Map [0, 1024] to [-8, 8] for good coverage
        float x = ((float)i / ATAN_TABLE_SIZE * 16.0f) - 8.0f;
        atan_table[i] = (float)atan(x);
    }
    atan_table_initialized = 1;
}

inline float pgl_fast_atan(float x) {
    if (!atan_table_initialized) init_atan_table();
    
    // Handle special cases
    if (x < -8.0f) return -M_PI / 2.0f;
    if (x > 8.0f) return M_PI / 2.0f;
    
    // Map [-8, 8] to [0, ATAN_TABLE_SIZE]
    float idx = (x + 8.0f) / 16.0f * ATAN_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = i0 + 1;
    if (i1 > ATAN_TABLE_SIZE) i1 = ATAN_TABLE_SIZE;
    float t = idx - i0;
    
    return atan_table[i0] * (1.0f - t) + atan_table[i1] * t;
}

// Fast atan2 using atan: atan2(y, x) = atan(y/x) with quadrant correction
inline float pgl_fast_atan2(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return M_PI / 2.0f;
        if (y < 0.0f) return -M_PI / 2.0f;
        return 0.0f;
    }
    
    float atan_val = pgl_fast_atan(y / x);
    
    // Correct quadrant
    if (x < 0.0f) {
        if (y >= 0.0f) return atan_val + M_PI;
        else return atan_val - M_PI;
    }
    
    return atan_val;
}

// Use fast math functions
#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atan2f pgl_fast_atan2

// Matrix access macros for extract_rotation_m4
#ifndef ROW_MAJOR
#define M44(m, row, col) m[col*4 + row]
#define M33(m, row, col) m[col*3 + row]
#else
#define M44(m, row, col) m[row*4 + col]
#define M33(m, row, col) m[row*3 + col]
#endif

// Runtime configurable vertex limits
static int _pgl_max_vertices = PGL_DEF_MAX_VERTICES;

// Function to set max vertices before init_glContext()
void pgl_set_max_vertices(int max_vertices)
{
    if (max_vertices > 0) {
        _pgl_max_vertices = max_vertices;
    }
}

// Helper function to get actual max vertices
static int get_max_vertices(void)
{
    if (_pgl_max_vertices > 0) {
        return _pgl_max_vertices;
    }
    return PGL_DEF_MAX_VERTICES;
}

// Define PGL_NEON_ENABLED early so texture conversion functions can use it
#ifdef ARCH_BOOST
#if defined(__aarch64__) || defined(__ARM_NEON) || defined(ARCH_ARM)
#include <arm_neon.h>
#define PGL_NEON_ENABLED 1

// ---------------------------------------------------------------------------
// NEON load/store primitives
//
// Plain typed loads/stores.  The aarch64 make.rule must NOT pass
// -mstrict-align (the kernel boots with SCTLR_EL1.A=0, so unaligned access is
// legal); without it GCC/Clang lower these to real vld1q/vst1q.  An earlier
// hand-written LD1/ST1 inline-asm variant (added to dodge -mstrict-align)
// mis-scheduled under GCC and dropped scattered pixels in filled spans, so it
// is gone: let the compiler emit the vector memory ops itself.
// ---------------------------------------------------------------------------
#define PGL_NEON_LD(name, type, arr) \
static inline type name(const void* p) { return *(const type*)p; }
#define PGL_NEON_ST(name, type, arr) \
static inline void name(void* p, type v) { *(type*)p = v; }

PGL_NEON_LD(pgl_vld1q_f32, float32x4_t, "4s")
PGL_NEON_ST(pgl_vst1q_f32, float32x4_t, "4s")
PGL_NEON_LD(pgl_vld1_f32,  float32x2_t, "2s")
PGL_NEON_ST(pgl_vst1_f32,  float32x2_t, "2s")
PGL_NEON_LD(pgl_vld1q_u32, uint32x4_t,  "4s")
PGL_NEON_ST(pgl_vst1q_u32, uint32x4_t,  "4s")
PGL_NEON_LD(pgl_vld1q_u16, uint16x8_t,  "8h")
PGL_NEON_ST(pgl_vst1q_u16, uint16x8_t,  "8h")
PGL_NEON_LD(pgl_vld1_u16,  uint16x4_t,  "4h")
PGL_NEON_ST(pgl_vst1_u16,  uint16x4_t,  "4h")
PGL_NEON_LD(pgl_vld1q_u8,  uint8x16_t,  "16b")
PGL_NEON_ST(pgl_vst1q_u8,  uint8x16_t,  "16b")
PGL_NEON_LD(pgl_vld1_u8,   uint8x8_t,   "8b")
PGL_NEON_ST(pgl_vst1_u8,   uint8x8_t,   "8b")

#undef PGL_NEON_LD
#undef PGL_NEON_ST

// any lane of a 4x32 mask set?
static inline int pgl_neon_any_u32(uint32x4_t m)
{
#ifdef __aarch64__
    return vmaxvq_u32(m) != 0;
#else
    uint32x2_t t = vorr_u32(vget_low_u32(m), vget_high_u32(m));
    t = vpmax_u32(t, t);
    return vget_lane_u32(t, 0) != 0;
#endif
}

// Multiply-accumulate for the rasterizer hot path. vmlaq_* lowers to a
// separate fmul+fadd pair; AArch64 always has vector FMA so use the fused
// form there (one instruction, and it matches what GCC contracts the scalar
// path to). The arm32 build is -mfpu=neon-vfpv3 which has no vector FMA.
#ifdef __aarch64__
#define pgl_neon_mla(acc, v, w)         vfmaq_f32((acc), (v), (w))
#define pgl_neon_mla_n(acc, v, s)       vfmaq_n_f32((acc), (v), (s))
#define pgl_neon_mul_lane(v, s, l)      vmulq_laneq_f32((v), (s), (l))
#define pgl_neon_mla_lane(acc, v, s, l) vfmaq_laneq_f32((acc), (v), (s), (l))
#else
#define pgl_neon_mla(acc, v, w)         vmlaq_f32((acc), (v), (w))
#define pgl_neon_mla_n(acc, v, s)       vmlaq_n_f32((acc), (v), (s))
#define pgl_neon_mul_lane(v, s, l)      vmulq_lane_f32((v), ((l) < 2 ? vget_low_f32(s) : vget_high_f32(s)), (l) & 1)
#define pgl_neon_mla_lane(acc, v, s, l) vmlaq_lane_f32((acc), (v), ((l) < 2 ? vget_low_f32(s) : vget_high_f32(s)), (l) & 1)
#endif

// ---------------------------------------------------------------------------
// Buffer fills / copies (pgl_Clear, pgl_BufferData, format conversion)
// ---------------------------------------------------------------------------

// dst[i] = v
static inline void pgl_neon_fill_u32(uint32_t* dst, uint32_t v, int n)
{
    uint32x4_t vv = vdupq_n_u32(v);
    int i = 0;
    for (; i + 16 <= n; i += 16) {
        pgl_vst1q_u32(dst + i, vv);
        pgl_vst1q_u32(dst + i + 4, vv);
        pgl_vst1q_u32(dst + i + 8, vv);
        pgl_vst1q_u32(dst + i + 12, vv);
    }
    for (; i + 4 <= n; i += 4) {
        pgl_vst1q_u32(dst + i, vv);
    }
    for (; i < n; i++) {
        dst[i] = v;
    }
}

static inline void pgl_neon_fill_u16(uint16_t* dst, uint16_t v, int n)
{
    uint16x8_t vv = vdupq_n_u16(v);
    int i = 0;
    for (; i + 32 <= n; i += 32) {
        pgl_vst1q_u16(dst + i, vv);
        pgl_vst1q_u16(dst + i + 8, vv);
        pgl_vst1q_u16(dst + i + 16, vv);
        pgl_vst1q_u16(dst + i + 24, vv);
    }
    for (; i + 8 <= n; i += 8) {
        pgl_vst1q_u16(dst + i, vv);
    }
    for (; i < n; i++) {
        dst[i] = v;
    }
}

// dst[i] = (dst[i] & keep) | v   -- pgl_Clear with a color/stencil write mask,
// and the PGL_D24S8 depth clear that must preserve the stencil byte.
static inline void pgl_neon_fill_masked_u32(uint32_t* dst, uint32_t keep, uint32_t v, int n)
{
    uint32x4_t kv = vdupq_n_u32(keep);
    uint32x4_t vv = vdupq_n_u32(v);
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        uint32x4_t d0 = pgl_vld1q_u32(dst + i);
        uint32x4_t d1 = pgl_vld1q_u32(dst + i + 4);
        pgl_vst1q_u32(dst + i, vorrq_u32(vandq_u32(d0, kv), vv));
        pgl_vst1q_u32(dst + i + 4, vorrq_u32(vandq_u32(d1, kv), vv));
    }
    for (; i + 4 <= n; i += 4) {
        uint32x4_t d0 = pgl_vld1q_u32(dst + i);
        pgl_vst1q_u32(dst + i, vorrq_u32(vandq_u32(d0, kv), vv));
    }
    for (; i < n; i++) {
        dst[i] = (dst[i] & keep) | v;
    }
}

// dst[i] = src[i] * s   (perspective divide of a vertex output row)
static inline void pgl_neon_scale_f32(float* dst, const float* src, float s, int n)
{
    float32x4_t sv = vdupq_n_f32(s);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        pgl_vst1q_f32(dst + i, vmulq_f32(pgl_vld1q_f32(src + i), sv));
    }
    for (; i < n; i++) {
        dst[i] = src[i] * s;
    }
}

static inline void pgl_neon_memcpy(void* dst, const void* src, size_t n)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    size_t i = 0;

    for (; i + 64 <= n; i += 64) {
        uint8x16_t v0 = pgl_vld1q_u8(s + i);
        uint8x16_t v1 = pgl_vld1q_u8(s + i + 16);
        uint8x16_t v2 = pgl_vld1q_u8(s + i + 32);
        uint8x16_t v3 = pgl_vld1q_u8(s + i + 48);
        pgl_vst1q_u8(d + i, v0);
        pgl_vst1q_u8(d + i + 16, v1);
        pgl_vst1q_u8(d + i + 32, v2);
        pgl_vst1q_u8(d + i + 48, v3);
    }
    for (; i + 16 <= n; i += 16) {
        pgl_vst1q_u8(d + i, pgl_vld1q_u8(s + i));
    }
    for (; i < n; i++) {
        d[i] = s[i];
    }
}

static inline void pgl_neon_memset(void* dst, int c, size_t n)
{
    u8* d = (u8*)dst;
    uint8x16_t val = vdupq_n_u8((u8)c);
    size_t i = 0;

    for (; i + 64 <= n; i += 64) {
        pgl_vst1q_u8(d + i, val);
        pgl_vst1q_u8(d + i + 16, val);
        pgl_vst1q_u8(d + i + 32, val);
        pgl_vst1q_u8(d + i + 48, val);
    }
    for (; i + 16 <= n; i += 16) {
        pgl_vst1q_u8(d + i, val);
    }
    for (; i < n; i++) {
        d[i] = (u8)c;
    }
}

// ---------------------------------------------------------------------------
// Fragment color conversion
// ---------------------------------------------------------------------------

// vec4 -> float32x4_t. vec4 is only 4-byte aligned so this must go through
// the LD1 wrapper, never a plain vector dereference.
static inline float32x4_t pgl_neon_load_v4(const vec4* v)
{
    return pgl_vld1q_f32((const float*)v);
}

static inline vec4 pgl_neon_store_v4(float32x4_t v)
{
    vec4 r;
    pgl_vst1q_f32((float*)&r, v);
    return r;
}

// clamp_01 + v4_to_Color + RGBA_TO_PIXEL in one go. v4_to_Color truncates
// (plain float->u8 cast) and so does vcvtq_u32_f32.
static inline uint32_t pgl_neon_v4_to_pixel(float32x4_t cf)
{
    cf = vmaxq_f32(cf, vdupq_n_f32(0.0f));
    cf = vminq_f32(cf, vdupq_n_f32(1.0f));
    uint32x4_t u = vcvtq_u32_f32(vmulq_n_f32(cf, 255.0f));
    const int32x4_t shifts = { PGL_RSHIFT, PGL_GSHIFT, PGL_BSHIFT, PGL_ASHIFT };
    u = vshlq_u32(u, shifts);
    uint32x2_t o = vorr_u32(vget_low_u32(u), vget_high_u32(u));
    return vget_lane_u32(o, 0) | vget_lane_u32(o, 1);
}

// PIXEL_TO_COLOR + Color_to_v4. USHL with a negative per-lane count is a
// right shift, which unpacks all four channels in one instruction.
static inline float32x4_t pgl_neon_pixel_to_v4(uint32_t p)
{
    const int32x4_t shifts = { -PGL_RSHIFT, -PGL_GSHIFT, -PGL_BSHIFT, -PGL_ASHIFT };
    uint32x4_t u = vshlq_u32(vdupq_n_u32(p), shifts);
    u = vandq_u32(u, vdupq_n_u32(0xFF));
    return vmulq_n_f32(vcvtq_f32_u32(u), 1.0f / 255.0f);
}

// pgl_BlendFunc factor for all 4 channels. The separate-alpha factors in
// blend_pixel() are exactly lane 3 of the same table, so callers reuse this
// for blend_sA/blend_dA and pick lane 3.
static inline float32x4_t pgl_neon_blend_factor(GLenum f, float32x4_t src, float32x4_t dst,
                                                float32x4_t bc, float sat)
{
    float32x4_t one = vdupq_n_f32(1.0f);
    switch (f) {
    case GL_ZERO:                     return vdupq_n_f32(0.0f);
    case GL_ONE:                      return one;
    case GL_SRC_COLOR:                return src;
    case GL_ONE_MINUS_SRC_COLOR:      return vsubq_f32(one, src);
    case GL_DST_COLOR:                return dst;
    case GL_ONE_MINUS_DST_COLOR:      return vsubq_f32(one, dst);
    case GL_SRC_ALPHA:                return vdupq_lane_f32(vget_high_f32(src), 1);
    case GL_ONE_MINUS_SRC_ALPHA:      return vsubq_f32(one, vdupq_lane_f32(vget_high_f32(src), 1));
    case GL_DST_ALPHA:                return vdupq_lane_f32(vget_high_f32(dst), 1);
    case GL_ONE_MINUS_DST_ALPHA:      return vsubq_f32(one, vdupq_lane_f32(vget_high_f32(dst), 1));
    case GL_CONSTANT_COLOR:           return bc;
    case GL_ONE_MINUS_CONSTANT_COLOR: return vsubq_f32(one, bc);
    case GL_CONSTANT_ALPHA:           return vdupq_lane_f32(vget_high_f32(bc), 1);
    case GL_ONE_MINUS_CONSTANT_ALPHA: return vsubq_f32(one, vdupq_lane_f32(vget_high_f32(bc), 1));
    case GL_SRC_ALPHA_SATURATE:       return vsetq_lane_f32(1.0f, vdupq_n_f32(sat), 3);
    default:
        puts("error unrecognized blend factor!");
        return one;
    }
}

// pgl_BlendEquation combine for all 4 channels
static inline float32x4_t pgl_neon_blend_eq(GLenum eq, float32x4_t Cs, float32x4_t src,
                                            float32x4_t Cd, float32x4_t dst)
{
    switch (eq) {
    case GL_FUNC_ADD:              return vmlaq_f32(vmulq_f32(Cs, src), Cd, dst);
    case GL_FUNC_SUBTRACT:         return vmlsq_f32(vmulq_f32(Cs, src), Cd, dst);
    case GL_FUNC_REVERSE_SUBTRACT: return vmlsq_f32(vmulq_f32(Cd, dst), Cs, src);
    case GL_MIN:                   return vminq_f32(src, dst);
    case GL_MAX:                   return vmaxq_f32(src, dst);
    default:
        puts("error unrecognized blend equation!");
        return src;
    }
}

// ---------------------------------------------------------------------------
// Triangle rasterization
// ---------------------------------------------------------------------------

// Per-triangle interpolation plan: vertex outputs are processed in blocks of
// 4 components. For each block a lane mask selects PGL_SMOOTH (perspective
// correct), PGL_NOPERSPECTIVE or flat (provoking vertex) per component so the
// per-pixel work is branch free. The common all-PGL_SMOOTH case skips the
// nopersp/flat/select work entirely.
typedef struct pgl_neon_interp_plan {
    int nblocks;
    int all_smooth;
    // perspective-divided vertex outputs: v->vs_out[i] * (1/w)
    float persp[3][GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    // raw (non-perspective) vertex outputs, padded copies so block loads
    // never run past the end of the vs_output buffer
    float nopersp[3][GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float flat[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    uint32_t smooth_mask[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    uint32_t nopersp_mask[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
} pgl_neon_interp_plan;

static inline void pgl_neon_interp_plan_init(pgl_neon_interp_plan* p,
                                             const float* vs0, const float* vs1, const float* vs2,
                                             float inv_w0, float inv_w1, float inv_w2,
                                             const float* flat, const GLenum* interpolation, int size)
{
    int padded = (size + 3) & ~3;
    p->nblocks = padded >> 2;
    p->all_smooth = 1;

    pgl_neon_scale_f32(p->persp[0], vs0, inv_w0, size);
    pgl_neon_scale_f32(p->persp[1], vs1, inv_w1, size);
    pgl_neon_scale_f32(p->persp[2], vs2, inv_w2, size);

    for (int i = 0; i < padded; ++i) {
        if (i >= size) {
            // padding lanes: zero everywhere so garbage never reaches fs_input
            p->smooth_mask[i] = 0;
            p->nopersp_mask[i] = 0;
            p->flat[i] = 0.0f;
            p->persp[0][i] = p->persp[1][i] = p->persp[2][i] = 0.0f;
            p->nopersp[0][i] = p->nopersp[1][i] = p->nopersp[2][i] = 0.0f;
        } else if (interpolation[i] == PGL_SMOOTH) {
            p->smooth_mask[i] = 0xFFFFFFFFu;
            p->nopersp_mask[i] = 0;
        } else {
            p->all_smooth = 0;
            p->smooth_mask[i] = 0;
            p->nopersp_mask[i] = (interpolation[i] == PGL_NOPERSPECTIVE) ? 0xFFFFFFFFu : 0;
        }
    }
    if (!p->all_smooth) {
        pgl_neon_memcpy(p->nopersp[0], vs0, size * sizeof(float));
        pgl_neon_memcpy(p->nopersp[1], vs1, size * sizeof(float));
        pgl_neon_memcpy(p->nopersp[2], vs2, size * sizeof(float));
        pgl_neon_memcpy(p->flat, flat, size * sizeof(float));
    }
}

// Barycentric setup for 4 horizontally adjacent pixel centers
// (ix+0.5 .. ix+3.5), one lane per pixel. Everything stays in registers; the
// caller extracts lanes with constant indices.
typedef struct pgl_neon_span4 {
    float32x4_t a, b, g;    // barycentrics
    float32x4_t z;          // window depth
    float32x4_t wsum;       // 1/w interpolated (gl_FragCoord.w)
    uint32x4_t inside;      // 0 / 0xFFFFFFFF per lane
} pgl_neon_span4;

typedef struct pgl_neon_tri_setup {
    // gamma = (A01*x + B01*y + C01) * inv_denom01 ; beta likewise from l20
    float a01, b01, c01, inv_denom01;
    float a20, b20, c20, inv_denom20;
    // tie-break for pixels exactly on an edge: alpha > 0 || edge_test_l12 > 0
    uint32_t et12, et20, et01;
    float inv_w0, inv_w1, inv_w2;
    float z0, z1, z2;
    float poly_offset;
    float depth_scale_half, depth_near;
} pgl_neon_tri_setup;

static inline void pgl_neon_eval_span4(pgl_neon_span4* out, const pgl_neon_tri_setup* t,
                                       int ix, float y, int lanes_valid)
{
    const float32x4_t lane_off = { 0.5f, 1.5f, 2.5f, 3.5f };
    float32x4_t x = vaddq_f32(vdupq_n_f32((float)ix), lane_off);
    float32x4_t zero = vdupq_n_f32(0.0f);

    // line_func(l, x, y) = A*x + B*y + C evaluated at this row
    float32x4_t g = pgl_neon_mla_n(vdupq_n_f32(t->b01 * y + t->c01), x, t->a01);
    float32x4_t b = pgl_neon_mla_n(vdupq_n_f32(t->b20 * y + t->c20), x, t->a20);
    g = vmulq_n_f32(g, t->inv_denom01);
    b = vmulq_n_f32(b, t->inv_denom20);
    float32x4_t a = vsubq_f32(vsubq_f32(vdupq_n_f32(1.0f), b), g);

    uint32x4_t inside = vandq_u32(vcgeq_f32(a, zero), vandq_u32(vcgeq_f32(b, zero), vcgeq_f32(g, zero)));
    uint32x4_t ea = vorrq_u32(vcgtq_f32(a, zero), vdupq_n_u32(t->et12));
    uint32x4_t eb = vorrq_u32(vcgtq_f32(b, zero), vdupq_n_u32(t->et20));
    uint32x4_t eg = vorrq_u32(vcgtq_f32(g, zero), vdupq_n_u32(t->et01));
    inside = vandq_u32(inside, vandq_u32(ea, vandq_u32(eb, eg)));

    // mask off lanes past the right edge of the bounding box
    const uint32x4_t lane_idx = { 0, 1, 2, 3 };
    inside = vandq_u32(inside, vcltq_u32(lane_idx, vdupq_n_u32((uint32_t)lanes_valid)));

    float32x4_t wsum = vmulq_n_f32(a, t->inv_w0);
    wsum = pgl_neon_mla_n(wsum, b, t->inv_w1);
    wsum = pgl_neon_mla_n(wsum, g, t->inv_w2);

    float32x4_t z = pgl_neon_mla_n(vdupq_n_f32(t->poly_offset), a, t->z0);
    z = pgl_neon_mla_n(z, b, t->z1);
    z = pgl_neon_mla_n(z, g, t->z2);
    z = pgl_neon_mla_n(vdupq_n_f32(t->depth_near), vaddq_f32(z, vdupq_n_f32(1.0f)), t->depth_scale_half);

    out->a = a; out->b = b; out->g = g;
    out->z = z; out->wsum = wsum; out->inside = inside;
}

// Conservative per-row pixel range of the triangle. Each barycentric is
// linear in x, so alpha,beta,gamma >= 0 bound x to an interval; the result is
// widened by a pixel on both sides and the exact inside mask of
// pgl_neon_eval_span4 still decides per pixel, this only skips spans that
// cannot contain any fragment. Returns 0 if the row is empty.
static inline int pgl_neon_row_range(const pgl_neon_tri_setup* t, float y,
                                     int ix_min, int ix_max, int* lo_out, int* hi_out)
{
    float cg = t->a01 * t->inv_denom01, kg = (t->b01 * y + t->c01) * t->inv_denom01;
    float cb = t->a20 * t->inv_denom20, kb = (t->b20 * y + t->c20) * t->inv_denom20;
    float ca = -(cg + cb), ka = 1.0f - kg - kb;
    float lo = -1e9f, hi = 1e9f;

#define PGL_ROW_BOUND(cf, k) \
    if ((cf) > 1e-20f) { float r = -(k) / (cf); if (r > lo) lo = r; } \
    else if ((cf) < -1e-20f) { float r = -(k) / (cf); if (r < hi) hi = r; }
    PGL_ROW_BOUND(cg, kg)
    PGL_ROW_BOUND(cb, kb)
    PGL_ROW_BOUND(ca, ka)
#undef PGL_ROW_BOUND

    if (lo > hi) return 0;
    lo = MAX(lo, -1e9f); hi = MIN(hi, 1e9f);
    // pixel centers x = ix + 0.5 in [lo, hi], widened by one pixel each side
    int ilo = (int)floorf(lo - 0.5f) - 1;
    int ihi = (int)ceilf(hi - 0.5f) + 2;
    if (ilo < ix_min) ilo = ix_min;
    if (ihi > ix_max) ihi = ix_max;
    if (ilo >= ihi) return 0;
    *lo_out = ilo;
    *hi_out = ihi;
    return 1;
}

// Interpolate the vertex outputs for the inside lanes of a span into
// fs_in[lane][]. The plan vectors are loaded once per block and reused for
// all 4 lanes; perspective correction is folded into the barycentrics
// (a*inv_wsum etc.) so a smooth attribute is 3 multiply-adds per lane.
static inline void pgl_neon_interp_span(float fs_in[4][GL_MAX_VERTEX_OUTPUT_COMPONENTS],
                                        const pgl_neon_interp_plan* p, const pgl_neon_span4* sp)
{
    uint32x4_t m = sp->inside;
#ifdef __aarch64__
    float32x4_t iw = vdivq_f32(vdupq_n_f32(1.0f), sp->wsum);
#else
    float32x4_t iw = vrecpeq_f32(sp->wsum);
    iw = vmulq_f32(iw, vrecpsq_f32(sp->wsum, iw));
    iw = vmulq_f32(iw, vrecpsq_f32(sp->wsum, iw));
#endif
    float32x4_t pa = vmulq_f32(sp->a, iw);
    float32x4_t pb = vmulq_f32(sp->b, iw);
    float32x4_t pg = vmulq_f32(sp->g, iw);
    int l0 = vgetq_lane_u32(m, 0), l1 = vgetq_lane_u32(m, 1);
    int l2 = vgetq_lane_u32(m, 2), l3 = vgetq_lane_u32(m, 3);

    if (p->all_smooth) {
        for (int b = 0; b < p->nblocks; ++b) {
            int i = b * 4;
            float32x4_t p0 = pgl_vld1q_f32(&p->persp[0][i]);
            float32x4_t p1 = pgl_vld1q_f32(&p->persp[1][i]);
            float32x4_t p2 = pgl_vld1q_f32(&p->persp[2][i]);
#define PGL_SMOOTH_LANE(L) \
            if (l##L) { \
                float32x4_t s = pgl_neon_mul_lane(p0, pa, L); \
                s = pgl_neon_mla_lane(s, p1, pb, L); \
                s = pgl_neon_mla_lane(s, p2, pg, L); \
                pgl_vst1q_f32(&fs_in[L][i], s); \
            }
            PGL_SMOOTH_LANE(0)
            PGL_SMOOTH_LANE(1)
            PGL_SMOOTH_LANE(2)
            PGL_SMOOTH_LANE(3)
#undef PGL_SMOOTH_LANE
        }
        return;
    }

    float32x4_t a = sp->a, bb = sp->b, g = sp->g;
    for (int b = 0; b < p->nblocks; ++b) {
        int i = b * 4;
        float32x4_t p0 = pgl_vld1q_f32(&p->persp[0][i]);
        float32x4_t p1 = pgl_vld1q_f32(&p->persp[1][i]);
        float32x4_t p2 = pgl_vld1q_f32(&p->persp[2][i]);
        float32x4_t n0 = pgl_vld1q_f32(&p->nopersp[0][i]);
        float32x4_t n1 = pgl_vld1q_f32(&p->nopersp[1][i]);
        float32x4_t n2 = pgl_vld1q_f32(&p->nopersp[2][i]);
        float32x4_t f = pgl_vld1q_f32(&p->flat[i]);
        uint32x4_t sm = pgl_vld1q_u32(&p->smooth_mask[i]);
        uint32x4_t nm = pgl_vld1q_u32(&p->nopersp_mask[i]);
#define PGL_MIXED_LANE(L) \
        if (l##L) { \
            float32x4_t s = pgl_neon_mul_lane(p0, pa, L); \
            s = pgl_neon_mla_lane(s, p1, pb, L); \
            s = pgl_neon_mla_lane(s, p2, pg, L); \
            float32x4_t n = pgl_neon_mul_lane(n0, a, L); \
            n = pgl_neon_mla_lane(n, n1, bb, L); \
            n = pgl_neon_mla_lane(n, n2, g, L); \
            float32x4_t r = vbslq_f32(nm, n, f); \
            r = vbslq_f32(sm, s, r); \
            pgl_vst1q_f32(&fs_in[L][i], r); \
        }
        PGL_MIXED_LANE(0)
        PGL_MIXED_LANE(1)
        PGL_MIXED_LANE(2)
        PGL_MIXED_LANE(3)
#undef PGL_MIXED_LANE
    }
}

// ---------------------------------------------------------------------------
// Matrix helpers
//
// mat4 is 16 floats so every column (or row) is a full 128-bit load. mat3 is
// only 9 floats: a 4-wide load/store on m+6 touches m[9], one float past the
// end, so 3x3 code builds its vectors lane by lane and stores scalars.
// ---------------------------------------------------------------------------

// Load the 4 columns of a mat4 as vectors. In ROW_MAJOR storage the memory
// rows are transposed on the way in so callers can always think in columns.
static inline void pgl_neon_load_m4_cols(float32x4_t c[4], const float* m)
{
    float32x4_t v0 = pgl_vld1q_f32(m);
    float32x4_t v1 = pgl_vld1q_f32(m + 4);
    float32x4_t v2 = pgl_vld1q_f32(m + 8);
    float32x4_t v3 = pgl_vld1q_f32(m + 12);
#ifndef ROW_MAJOR
    c[0] = v0; c[1] = v1; c[2] = v2; c[3] = v3;
#else
    float32x4x2_t t01 = vtrnq_f32(v0, v1);
    float32x4x2_t t23 = vtrnq_f32(v2, v3);
    c[0] = vcombine_f32(vget_low_f32(t01.val[0]), vget_low_f32(t23.val[0]));
    c[1] = vcombine_f32(vget_low_f32(t01.val[1]), vget_low_f32(t23.val[1]));
    c[2] = vcombine_f32(vget_high_f32(t01.val[0]), vget_high_f32(t23.val[0]));
    c[3] = vcombine_f32(vget_high_f32(t01.val[1]), vget_high_f32(t23.val[1]));
#endif
}

// out = c0*v.x + c1*v.y + c2*v.z + c3*v.w
static inline float32x4_t pgl_neon_m4_cols_mul_v4(const float32x4_t c[4], float32x4_t v)
{
    float32x4_t r = vmulq_lane_f32(c[0], vget_low_f32(v), 0);
    r = vmlaq_lane_f32(r, c[1], vget_low_f32(v), 1);
    r = vmlaq_lane_f32(r, c[2], vget_high_f32(v), 0);
    r = vmlaq_lane_f32(r, c[3], vget_high_f32(v), 1);
    return r;
}

// C = A * B. Column-major: C_col[j] = sum_k B[k][j] * A_col[k]; row-major:
// C_row[i] = sum_k A[i][k] * B_row[k]. Both are "out[j] = x combined by the
// lanes of y[j]" with (x, y) = (a, b) or (b, a).
static inline void pgl_neon_mult_m4_m4(float* c, const float* a, const float* b)
{
#ifndef ROW_MAJOR
    const float* x = a;
    const float* y = b;
#else
    const float* x = b;
    const float* y = a;
#endif
    float32x4_t x0 = pgl_vld1q_f32(x);
    float32x4_t x1 = pgl_vld1q_f32(x + 4);
    float32x4_t x2 = pgl_vld1q_f32(x + 8);
    float32x4_t x3 = pgl_vld1q_f32(x + 12);

    for (int j = 0; j < 4; ++j) {
        float32x4_t yj = pgl_vld1q_f32(y + j * 4);
        float32x4_t r = vmulq_lane_f32(x0, vget_low_f32(yj), 0);
        r = vmlaq_lane_f32(r, x1, vget_low_f32(yj), 1);
        r = vmlaq_lane_f32(r, x2, vget_high_f32(yj), 0);
        r = vmlaq_lane_f32(r, x3, vget_high_f32(yj), 1);
        pgl_vst1q_f32(c + j * 4, r);
    }
}

static inline void pgl_neon_mult_m4_v4(vec4* r, const float* m, const vec4 v)
{
    float32x4_t c[4];
    pgl_neon_load_m4_cols(c, m);
    *r = pgl_neon_store_v4(pgl_neon_m4_cols_mul_v4(c, pgl_neon_load_v4(&v)));
}

// Batch transform 3 vertices with one matrix load
static inline void pgl_neon_transform_3vertices(vec4* r0, vec4* r1, vec4* r2, const mat4 m,
                                                const vec4 v0, const vec4 v1, const vec4 v2)
{
    float32x4_t c[4];
    pgl_neon_load_m4_cols(c, m);
    *r0 = pgl_neon_store_v4(pgl_neon_m4_cols_mul_v4(c, pgl_neon_load_v4(&v0)));
    *r1 = pgl_neon_store_v4(pgl_neon_m4_cols_mul_v4(c, pgl_neon_load_v4(&v1)));
    *r2 = pgl_neon_store_v4(pgl_neon_m4_cols_mul_v4(c, pgl_neon_load_v4(&v2)));
}

static inline void pgl_neon_mult_m3_v3(vec3* r, const float* m, const vec3 v)
{
#ifndef ROW_MAJOR
    float32x4_t c0 = { m[0], m[1], m[2], 0.0f };
    float32x4_t c1 = { m[3], m[4], m[5], 0.0f };
    float32x4_t c2 = { m[6], m[7], m[8], 0.0f };
#else
    float32x4_t c0 = { m[0], m[3], m[6], 0.0f };
    float32x4_t c1 = { m[1], m[4], m[7], 0.0f };
    float32x4_t c2 = { m[2], m[5], m[8], 0.0f };
#endif
    float32x4_t res = vmulq_n_f32(c0, v.x);
    res = vmlaq_n_f32(res, c1, v.y);
    res = vmlaq_n_f32(res, c2, v.z);
    r->x = vgetq_lane_f32(res, 0);
    r->y = vgetq_lane_f32(res, 1);
    r->z = vgetq_lane_f32(res, 2);
}

static inline void pgl_neon_mult_m2_v2(vec2* r, const float* m, const vec2 v)
{
#ifndef ROW_MAJOR
    float32x2_t c0 = pgl_vld1_f32(m);
    float32x2_t c1 = pgl_vld1_f32(m + 2);
#else
    float32x2_t c0 = { m[0], m[2] };
    float32x2_t c1 = { m[1], m[3] };
#endif
    float32x2_t res = vmul_n_f32(c0, v.x);
    res = vmla_n_f32(res, c1, v.y);
    pgl_vst1_f32((float*)r, res);
}

static inline void pgl_neon_mult_m2_m2(float* c, const float* a, const float* b)
{
    // same (x, y) trick as mult_m4_m4, on 2-lane vectors
#ifndef ROW_MAJOR
    const float* x = a;
    const float* y = b;
#else
    const float* x = b;
    const float* y = a;
#endif
    float32x2_t x0 = pgl_vld1_f32(x);
    float32x2_t x1 = pgl_vld1_f32(x + 2);
    float32x2_t y0 = pgl_vld1_f32(y);
    float32x2_t y1 = pgl_vld1_f32(y + 2);
    pgl_vst1_f32(c,     vmla_lane_f32(vmul_lane_f32(x0, y0, 0), x1, y0, 1));
    pgl_vst1_f32(c + 2, vmla_lane_f32(vmul_lane_f32(x0, y1, 0), x1, y1, 1));
}

static inline void pgl_neon_load_rotation_m2(float* mat, float s, float c)
{
    // column-major [c -s; s c] stores {c, s}, {-s, c}; row-major stores
    // {c, -s}, {s, c}.
#ifndef ROW_MAJOR
    float32x2_t v0 = { c, s };
    float32x2_t v1 = { -s, c };
#else
    float32x2_t v0 = { c, -s };
    float32x2_t v1 = { s, c };
#endif
    pgl_vst1_f32(mat, v0);
    pgl_vst1_f32(mat + 2, v1);
}

// Shared rotation setup. Produces the three 3-component columns (col-major
// sense) of the axis/angle rotation with a zero 4th lane.
static inline void pgl_neon_rotation_cols(float32x4_t out[3], vec3 v, float s, float c)
{
    float one_c = 1.0f - c;
    // vec3 is 12 bytes: never read it with a 16-byte load
    float32x4_t vv = { v.x, v.y, v.z, 0.0f };
    float32x4_t v_yzx = vextq_f32(vv, vv, 1);            // y z x 0
    v_yzx = vsetq_lane_f32(v.x, v_yzx, 2);
    float32x4_t d = vmlaq_n_f32(vdupq_n_f32(c), vmulq_f32(vv, vv), one_c); // xx+c yy+c zz+c
    float32x4_t p = vmulq_n_f32(vmulq_f32(vv, v_yzx), one_c);              // xy yz zx
    float32x4_t sv = vmulq_n_f32(vv, s);                                   // xs ys zs

    float xy = vgetq_lane_f32(p, 0), yz = vgetq_lane_f32(p, 1), zx = vgetq_lane_f32(p, 2);
    float xs = vgetq_lane_f32(sv, 0), ys = vgetq_lane_f32(sv, 1), zs = vgetq_lane_f32(sv, 2);

    float32x4_t c0 = { vgetq_lane_f32(d, 0), xy + zs, zx - ys, 0.0f };
    float32x4_t c1 = { xy - zs, vgetq_lane_f32(d, 1), yz + xs, 0.0f };
    float32x4_t c2 = { zx + ys, yz - xs, vgetq_lane_f32(d, 2), 0.0f };
    out[0] = c0; out[1] = c1; out[2] = c2;
}

static inline void pgl_neon_store_m3_cols(float* mat, const float32x4_t c[3])
{
    // 3 floats per column; the last store cannot be a 4-lane one
#ifndef ROW_MAJOR
    pgl_vst1_f32(mat, vget_low_f32(c[0]));     mat[2] = vgetq_lane_f32(c[0], 2);
    pgl_vst1_f32(mat + 3, vget_low_f32(c[1])); mat[5] = vgetq_lane_f32(c[1], 2);
    pgl_vst1_f32(mat + 6, vget_low_f32(c[2])); mat[8] = vgetq_lane_f32(c[2], 2);
#else
    mat[0] = vgetq_lane_f32(c[0], 0); mat[1] = vgetq_lane_f32(c[1], 0); mat[2] = vgetq_lane_f32(c[2], 0);
    mat[3] = vgetq_lane_f32(c[0], 1); mat[4] = vgetq_lane_f32(c[1], 1); mat[5] = vgetq_lane_f32(c[2], 1);
    mat[6] = vgetq_lane_f32(c[0], 2); mat[7] = vgetq_lane_f32(c[1], 2); mat[8] = vgetq_lane_f32(c[2], 2);
#endif
}

static inline void pgl_neon_store_m4_cols(float* mat, const float32x4_t c[4])
{
#ifndef ROW_MAJOR
    pgl_vst1q_f32(mat, c[0]);
    pgl_vst1q_f32(mat + 4, c[1]);
    pgl_vst1q_f32(mat + 8, c[2]);
    pgl_vst1q_f32(mat + 12, c[3]);
#else
    float32x4x2_t t01 = vtrnq_f32(c[0], c[1]);
    float32x4x2_t t23 = vtrnq_f32(c[2], c[3]);
    pgl_vst1q_f32(mat,      vcombine_f32(vget_low_f32(t01.val[0]), vget_low_f32(t23.val[0])));
    pgl_vst1q_f32(mat + 4,  vcombine_f32(vget_low_f32(t01.val[1]), vget_low_f32(t23.val[1])));
    pgl_vst1q_f32(mat + 8,  vcombine_f32(vget_high_f32(t01.val[0]), vget_high_f32(t23.val[0])));
    pgl_vst1q_f32(mat + 12, vcombine_f32(vget_high_f32(t01.val[1]), vget_high_f32(t23.val[1])));
#endif
}

static inline void pgl_neon_load_rotation_m3(float* mat, vec3 v, float s, float c)
{
    float32x4_t cols[3];
    pgl_neon_rotation_cols(cols, v, s, c);
    pgl_neon_store_m3_cols(mat, cols);
}

static inline void pgl_neon_load_rotation_m4(float* mat, vec3 v, float s, float c)
{
    float32x4_t cols[4];
    pgl_neon_rotation_cols(cols, v, s, c);
    const float32x4_t w = { 0.0f, 0.0f, 0.0f, 1.0f };
    cols[3] = w;
    pgl_neon_store_m4_cols(mat, cols);
}

static inline void pgl_neon_scale_m3(float* m, float x, float y, float z)
{
    // diagonal matrix: identical in both layouts
    const float32x4_t c0 = { x, 0.0f, 0.0f, 0.0f };
    const float32x4_t c1 = { 0.0f, y, 0.0f, 0.0f };
    const float32x4_t c2 = { 0.0f, 0.0f, z, 0.0f };
    pgl_vst1_f32(m, vget_low_f32(c0));     m[2] = 0.0f;
    pgl_vst1_f32(m + 3, vget_low_f32(c1)); m[5] = 0.0f;
    pgl_vst1_f32(m + 6, vget_low_f32(c2)); m[8] = z;
}

static inline void pgl_neon_scale_m4(float* m, float x, float y, float z)
{
    const float32x4_t c0 = { x, 0.0f, 0.0f, 0.0f };
    const float32x4_t c1 = { 0.0f, y, 0.0f, 0.0f };
    const float32x4_t c2 = { 0.0f, 0.0f, z, 0.0f };
    const float32x4_t c3 = { 0.0f, 0.0f, 0.0f, 1.0f };
    pgl_vst1q_f32(m, c0);
    pgl_vst1q_f32(m + 4, c1);
    pgl_vst1q_f32(m + 8, c2);
    pgl_vst1q_f32(m + 12, c3);
}

static inline void pgl_neon_translation_m4(float* m, float x, float y, float z)
{
#ifndef ROW_MAJOR
    const float32x4_t v0 = { 1.0f, 0.0f, 0.0f, 0.0f };
    const float32x4_t v1 = { 0.0f, 1.0f, 0.0f, 0.0f };
    const float32x4_t v2 = { 0.0f, 0.0f, 1.0f, 0.0f };
    const float32x4_t v3 = { x, y, z, 1.0f };
#else
    const float32x4_t v0 = { 1.0f, 0.0f, 0.0f, x };
    const float32x4_t v1 = { 0.0f, 1.0f, 0.0f, y };
    const float32x4_t v2 = { 0.0f, 0.0f, 1.0f, z };
    const float32x4_t v3 = { 0.0f, 0.0f, 0.0f, 1.0f };
#endif
    pgl_vst1q_f32(m, v0);
    pgl_vst1q_f32(m + 4, v1);
    pgl_vst1q_f32(m + 8, v2);
    pgl_vst1q_f32(m + 12, v3);
}

// Upper-left 3x3 of a mat4, optionally with each column normalized
static inline void pgl_neon_extract_rotation_m4(float* dst, const float* src, int normalize)
{
    float32x4_t c[4];
    pgl_neon_load_m4_cols(c, src);
    const uint32x4_t xyz_mask = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0 };
    for (int i = 0; i < 3; ++i) {
        float32x4_t v = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(c[i]), xyz_mask));
        if (normalize) {
            float32x4_t sq = vmulq_f32(v, v);
            float32x2_t sum = vpadd_f32(vget_low_f32(sq), vget_high_f32(sq));
            sum = vpadd_f32(sum, sum);
            // vrsqrte + 2 Newton-Raphson steps: ~full float precision, no libm
            // call (sqrtf is remapped to a 1-step fast approximation here).
            float32x2_t r = vrsqrte_f32(sum);
            r = vmul_f32(r, vrsqrts_f32(vmul_f32(sum, r), r));
            r = vmul_f32(r, vrsqrts_f32(vmul_f32(sum, r), r));
            v = vmulq_lane_f32(v, r, 0);
        }
        c[i] = v;
    }
    pgl_neon_store_m3_cols(dst, c);
}

// ---------------------------------------------------------------------------
// Texture upload format conversion (-> RGBA8). LDn/STn structure loads are
// not subject to the -mstrict-align scalarization, use them directly.
// ---------------------------------------------------------------------------

static inline void pgl_neon_convert_luminance_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            uint8x16_t gray = pgl_vld1q_u8(in_row + j);
            uint8x16x4_t rgba;
            rgba.val[0] = gray;
            rgba.val[1] = gray;
            rgba.val[2] = gray;
            rgba.val[3] = alpha;
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            u8 g = in_row[j];
            out_row[j * 4] = g;
            out_row[j * 4 + 1] = g;
            out_row[j * 4 + 2] = g;
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

static inline void pgl_neon_convert_luminance_alpha_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            uint8x16x2_t la = vld2q_u8(in_row + j * 2);
            uint8x16x4_t rgba;
            rgba.val[0] = la.val[0];
            rgba.val[1] = la.val[0];
            rgba.val[2] = la.val[0];
            rgba.val[3] = la.val[1];
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            u8 l = in_row[j * 2];
            out_row[j * 4] = l;
            out_row[j * 4 + 1] = l;
            out_row[j * 4 + 2] = l;
            out_row[j * 4 + 3] = in_row[j * 2 + 1];
        }
    }
}

static inline void pgl_neon_convert_rg_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    uint8x16_t zero = vdupq_n_u8(0);
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            uint8x16x2_t rg = vld2q_u8(in_row + j * 2);
            uint8x16x4_t rgba;
            rgba.val[0] = rg.val[0];
            rgba.val[1] = rg.val[1];
            rgba.val[2] = zero;
            rgba.val[3] = alpha;
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 2];
            out_row[j * 4 + 1] = in_row[j * 2 + 1];
            out_row[j * 4 + 2] = 0;
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

// 3-byte RGB / BGR -> RGBA. swap selects B,G,R input order.
static inline void pgl_neon_convert_rgb3_to_rgba(u8* out, u8* input, int w, int h, int pitch, int swap)
{
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    int ri = swap ? 2 : 0;
    int bi = swap ? 0 : 2;
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            uint8x16x3_t rgb = vld3q_u8(in_row + j * 3);
            uint8x16x4_t rgba;
            rgba.val[0] = rgb.val[ri];
            rgba.val[1] = rgb.val[1];
            rgba.val[2] = rgb.val[bi];
            rgba.val[3] = alpha;
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 3 + ri];
            out_row[j * 4 + 1] = in_row[j * 3 + 1];
            out_row[j * 4 + 2] = in_row[j * 3 + bi];
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

static inline void pgl_neon_convert_rgb_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    pgl_neon_convert_rgb3_to_rgba(out, input, w, h, pitch, 0);
}

static inline void pgl_neon_convert_bgr_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    pgl_neon_convert_rgb3_to_rgba(out, input, w, h, pitch, 1);
}

static inline void pgl_neon_convert_bgra_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            uint8x16x4_t bgra = vld4q_u8(in_row + j * 4);
            uint8x16x4_t rgba;
            rgba.val[0] = bgra.val[2];
            rgba.val[1] = bgra.val[1];
            rgba.val[2] = bgra.val[0];
            rgba.val[3] = bgra.val[3];
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 4 + 2];
            out_row[j * 4 + 1] = in_row[j * 4 + 1];
            out_row[j * 4 + 2] = in_row[j * 4];
            out_row[j * 4 + 3] = in_row[j * 4 + 3];
        }
    }
}

#endif //endif ARM/AARCH64

#else
#define PGL_NEON_ENABLED 0
#endif  //endif ARCH_BOOST

// bulk copies: NEON when available, libc otherwise
#if PGL_NEON_ENABLED
#define PGL_MEMCPY pgl_neon_memcpy
#define PGL_MEMSET pgl_neon_memset
#else
#define PGL_MEMCPY memcpy
#define PGL_MEMSET memset
#endif

extern inline vec2 make_v2(float x, float y);
extern inline vec2 neg_v2(vec2 v);
extern inline void fprint_v2(FILE* f, vec2 v, const char* append);
extern inline void print_v2(vec2 v, const char* append);
extern inline int fread_v2(FILE* f, vec2* v);
extern inline float len_v2(vec2 a);
extern inline vec2 norm_v2(vec2 a);
extern inline void normalize_v2(vec2* a);
extern inline vec2 add_v2s(vec2 a, vec2 b);
extern inline vec2 sub_v2s(vec2 a, vec2 b);
extern inline vec2 mult_v2s(vec2 a, vec2 b);
extern inline vec2 div_v2s(vec2 a, vec2 b);
extern inline float dot_v2s(vec2 a, vec2 b);
extern inline vec2 scale_v2(vec2 a, float s);
extern inline int equal_v2s(vec2 a, vec2 b);
extern inline int equal_epsilon_v2s(vec2 a, vec2 b, float epsilon);
extern inline float cross_v2s(vec2 a, vec2 b);
extern inline float angle_v2s(vec2 a, vec2 b);


extern inline vec3 make_v3(float x, float y, float z);
extern inline vec3 neg_v3(vec3 v);
extern inline void fprint_v3(FILE* f, vec3 v, const char* append);
extern inline void print_v3(vec3 v, const char* append);
extern inline int fread_v3(FILE* f, vec3* v);
extern inline float len_v3(vec3 a);
extern inline vec3 norm_v3(vec3 a);
extern inline void normalize_v3(vec3* a);
extern inline vec3 add_v3s(vec3 a, vec3 b);
extern inline vec3 sub_v3s(vec3 a, vec3 b);
extern inline vec3 mult_v3s(vec3 a, vec3 b);
extern inline vec3 div_v3s(vec3 a, vec3 b);
extern inline float dot_v3s(vec3 a, vec3 b);
extern inline vec3 scale_v3(vec3 a, float s);
extern inline int equal_v3s(vec3 a, vec3 b);
extern inline int equal_epsilon_v3s(vec3 a, vec3 b, float epsilon);
extern inline vec3 cross_v3s(const vec3 u, const vec3 v);
extern inline float angle_v3s(const vec3 u, const vec3 v);


extern inline vec4 make_v4(float x, float y, float z, float w);
extern inline vec4 neg_v4(vec4 v);
extern inline void fprint_v4(FILE* f, vec4 v, const char* append);
extern inline void print_v4(vec4 v, const char* append);
extern inline int fread_v4(FILE* f, vec4* v);
extern inline float len_v4(vec4 a);
extern inline vec4 norm_v4(vec4 a);
extern inline void normalize_v4(vec4* a);
extern inline vec4 add_v4s(vec4 a, vec4 b);
extern inline vec4 sub_v4s(vec4 a, vec4 b);
extern inline vec4 mult_v4s(vec4 a, vec4 b);
extern inline vec4 div_v4s(vec4 a, vec4 b);
extern inline float dot_v4s(vec4 a, vec4 b);
extern inline vec4 scale_v4(vec4 a, float s);
extern inline int equal_v4s(vec4 a, vec4 b);
extern inline int equal_epsilon_v4s(vec4 a, vec4 b, float epsilon);


extern inline ivec2 make_iv2(int x, int y);
extern inline void fprint_iv2(FILE* f, ivec2 v, const char* append);
extern inline int fread_iv2(FILE* f, ivec2* v);

extern inline ivec3 make_iv3(int x, int y, int z);
extern inline void fprint_iv3(FILE* f, ivec3 v, const char* append);
extern inline int fread_iv3(FILE* f, ivec3* v);

extern inline ivec4 make_iv4(int x, int y, int z, int w);
extern inline void fprint_iv4(FILE* f, ivec4 v, const char* append);
extern inline int fread_iv4(FILE* f, ivec4* v);

extern inline uvec2 make_uv2(unsigned int x, unsigned int y);
extern inline void fprint_uv2(FILE* f, uvec2 v, const char* append);
extern inline int fread_uv2(FILE* f, uvec2* v);

extern inline uvec3 make_uv3(unsigned int x, unsigned int y, unsigned int z);
extern inline void fprint_uv3(FILE* f, uvec3 v, const char* append);
extern inline int fread_uv3(FILE* f, uvec3* v);

extern inline uvec4 make_uv4(unsigned int x, unsigned int y, unsigned int z, unsigned int w);
extern inline void fprint_uv4(FILE* f, uvec4 v, const char* append);
extern inline int fread_uv4(FILE* f, uvec4* v);

extern inline bvec2 make_bv2(int x, int y);
extern inline void fprint_bv2(FILE* f, bvec2 v, const char* append);
extern inline int fread_bv2(FILE* f, bvec2* v);

extern inline bvec3 make_bv3(int x, int y, int z);
extern inline void fprint_bv3(FILE* f, bvec3 v, const char* append);
extern inline int fread_bv3(FILE* f, bvec3* v);

extern inline bvec4 make_bv4(int x, int y, int z, int w);
extern inline void fprint_bv4(FILE* f, bvec4 v, const char* append);
extern inline int fread_bv4(FILE* f, bvec4* v);

extern inline vec2 v4_to_v2(vec4 a);
extern inline vec3 v4_to_v3(vec4 a);
extern inline vec2 v4_to_v2h(vec4 a);
extern inline vec3 v4_to_v3h(vec4 a);

extern inline void fprint_m2(FILE* f, mat2 m, const char* append);
extern inline void fprint_m3(FILE* f, mat3 m, const char* append);
extern inline void fprint_m4(FILE* f, mat4 m, const char* append);
extern inline void print_m2(mat2 m, const char* append);
extern inline void print_m3(mat3 m, const char* append);
extern inline void print_m4(mat4 m, const char* append);
extern inline vec2 mult_m2_v2(mat2 m, vec2 v);
extern inline vec3 mult_m3_v3(mat3 m, vec3 v);
extern inline vec4 mult_m4_v4(mat4 m, vec4 v);
extern inline void scale_m3(mat3 m, float x, float y, float z);
extern inline void scale_m4(mat4 m, float x, float y, float z);
extern inline void translation_m4(mat4 m, float x, float y, float z);
extern inline void extract_rotation_m4(mat3 dst, mat4 src, int normalize);

extern inline vec2 x_m2(mat2 m);
extern inline vec2 y_m2(mat2 m);
extern inline vec2 c1_m2(mat2 m);
extern inline vec2 c2_m2(mat2 m);

extern inline void setc1_m2(mat2 m, vec2 v);
extern inline void setc2_m2(mat2 m, vec2 v);
extern inline void setx_m2(mat2 m, vec2 v);
extern inline void sety_m2(mat2 m, vec2 v);

extern inline vec3 x_m3(mat3 m);
extern inline vec3 y_m3(mat3 m);
extern inline vec3 z_m3(mat3 m);
extern inline vec3 c1_m3(mat3 m);
extern inline vec3 c2_m3(mat3 m);
extern inline vec3 c3_m3(mat3 m);

extern inline void setc1_m3(mat3 m, vec3 v);
extern inline void setc2_m3(mat3 m, vec3 v);
extern inline void setc3_m3(mat3 m, vec3 v);

extern inline void setx_m3(mat3 m, vec3 v);
extern inline void sety_m3(mat3 m, vec3 v);
extern inline void setz_m3(mat3 m, vec3 v);

extern inline vec4 c1_m4(mat4 m);
extern inline vec4 c2_m4(mat4 m);
extern inline vec4 c3_m4(mat4 m);
extern inline vec4 c4_m4(mat4 m);

extern inline vec4 x_m4(mat4 m);
extern inline vec4 y_m4(mat4 m);
extern inline vec4 z_m4(mat4 m);
extern inline vec4 w_m4(mat4 m);

extern inline void setc1_m4v3(mat4 m, vec3 v);
extern inline void setc2_m4v3(mat4 m, vec3 v);
extern inline void setc3_m4v3(mat4 m, vec3 v);
extern inline void setc4_m4v3(mat4 m, vec3 v);

extern inline void setc1_m4v4(mat4 m, vec4 v);
extern inline void setc2_m4v4(mat4 m, vec4 v);
extern inline void setc3_m4v4(mat4 m, vec4 v);
extern inline void setc4_m4v4(mat4 m, vec4 v);

extern inline void setx_m4v3(mat4 m, vec3 v);
extern inline void sety_m4v3(mat4 m, vec3 v);
extern inline void setz_m4v3(mat4 m, vec3 v);
extern inline void setw_m4v3(mat4 m, vec3 v);

extern inline void setx_m4v4(mat4 m, vec4 v);
extern inline void sety_m4v4(mat4 m, vec4 v);
extern inline void setz_m4v4(mat4 m, vec4 v);
extern inline void setw_m4v4(mat4 m, vec4 v);


vec2 mult_m2_v2(mat2 m, vec2 v)
{
    vec2 r;
#if PGL_NEON_ENABLED
    pgl_neon_mult_m2_v2(&r, m, v);
#else
#ifndef ROW_MAJOR
    r.x = m[0]*v.x + m[2]*v.y;
    r.y = m[1]*v.x + m[3]*v.y;
#else
    r.x = m[0]*v.x + m[1]*v.y;
    r.y = m[2]*v.x + m[3]*v.y;
#endif
#endif
    return r;
}

vec3 mult_m3_v3(mat3 m, vec3 v)
{
    vec3 r;
#if PGL_NEON_ENABLED
    pgl_neon_mult_m3_v3(&r, m, v);
#else
#ifndef ROW_MAJOR
    r.x = m[0]*v.x + m[3]*v.y + m[6]*v.z;
    r.y = m[1]*v.x + m[4]*v.y + m[7]*v.z;
    r.z = m[2]*v.x + m[5]*v.y + m[8]*v.z;
#else
    r.x = m[0]*v.x + m[1]*v.y + m[2]*v.z;
    r.y = m[3]*v.x + m[4]*v.y + m[5]*v.z;
    r.z = m[6]*v.x + m[7]*v.y + m[8]*v.z;
#endif
#endif
    return r;
}

vec4 mult_m4_v4(mat4 m, vec4 v)
{
    vec4 r;
#if PGL_NEON_ENABLED
    pgl_neon_mult_m4_v4(&r, m, v);
#else
#ifndef ROW_MAJOR
    r.x = m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12]*v.w;
    r.y = m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13]*v.w;
    r.z = m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w;
    r.w = m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w;
#else
    r.x = m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3]*v.w;
    r.y = m[4]*v.x + m[5]*v.y + m[6]*v.z + m[7]*v.w;
    r.z = m[8]*v.x + m[9]*v.y + m[10]*v.z + m[11]*v.w;
    r.w = m[12]*v.x + m[13]*v.y + m[14]*v.z + m[15]*v.w;
#endif
#endif
    return r;
}

void scale_m3(mat3 m, float x, float y, float z)
{
#if PGL_NEON_ENABLED
    pgl_neon_scale_m3(m, x, y, z);
#else
#ifndef ROW_MAJOR
    m[0] = x; m[3] = 0; m[6] = 0;
    m[1] = 0; m[4] = y; m[7] = 0;
    m[2] = 0; m[5] = 0; m[8] = z;
#else
    m[0] = x; m[1] = 0; m[2] = 0;
    m[3] = 0; m[4] = y; m[5] = 0;
    m[6] = 0; m[7] = 0; m[8] = z;
#endif
#endif
}

void scale_m4(mat4 m, float x, float y, float z)
{
#if PGL_NEON_ENABLED
    pgl_neon_scale_m4(m, x, y, z);
#else
#ifndef ROW_MAJOR
    m[ 0] = x; m[ 4] = 0; m[ 8] = 0; m[12] = 0;
    m[ 1] = 0; m[ 5] = y; m[ 9] = 0; m[13] = 0;
    m[ 2] = 0; m[ 6] = 0; m[10] = z; m[14] = 0;
    m[ 3] = 0; m[ 7] = 0; m[11] = 0; m[15] = 1;
#else
    m[ 0] = x; m[ 1] = 0; m[ 2] = 0; m[ 3] = 0;
    m[ 4] = 0; m[ 5] = y; m[ 6] = 0; m[ 7] = 0;
    m[ 8] = 0; m[ 9] = 0; m[10] = z; m[11] = 0;
    m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
#endif
#endif
}

void translation_m4(mat4 m, float x, float y, float z)
{
#if PGL_NEON_ENABLED
    pgl_neon_translation_m4(m, x, y, z);
#else
#ifndef ROW_MAJOR
    m[ 0] = 1; m[ 4] = 0; m[ 8] = 0; m[12] = x;
    m[ 1] = 0; m[ 5] = 1; m[ 9] = 0; m[13] = y;
    m[ 2] = 0; m[ 6] = 0; m[10] = 1; m[14] = z;
    m[ 3] = 0; m[ 7] = 0; m[11] = 0; m[15] = 1;
#else
    m[ 0] = 1; m[ 1] = 0; m[ 2] = 0; m[ 3] = x;
    m[ 4] = 0; m[ 5] = 1; m[ 6] = 0; m[ 7] = y;
    m[ 8] = 0; m[ 9] = 0; m[10] = 1; m[11] = z;
    m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
#endif
#endif
}

void extract_rotation_m4(mat3 dst, mat4 src, int normalize)
{
#if PGL_NEON_ENABLED
    pgl_neon_extract_rotation_m4(dst, src, normalize);
#else
    vec3 tmp;
    if (normalize) {
        tmp.x = M44(src, 0, 0);
        tmp.y = M44(src, 1, 0);
        tmp.z = M44(src, 2, 0);
        normalize_v3(&tmp);

        M33(dst, 0, 0) = tmp.x;
        M33(dst, 1, 0) = tmp.y;
        M33(dst, 2, 0) = tmp.z;

        tmp.x = M44(src, 0, 1);
        tmp.y = M44(src, 1, 1);
        tmp.z = M44(src, 2, 1);
        normalize_v3(&tmp);

        M33(dst, 0, 1) = tmp.x;
        M33(dst, 1, 1) = tmp.y;
        M33(dst, 2, 1) = tmp.z;

        tmp.x = M44(src, 0, 2);
        tmp.y = M44(src, 1, 2);
        tmp.z = M44(src, 2, 2);
        normalize_v3(&tmp);

        M33(dst, 0, 2) = tmp.x;
        M33(dst, 1, 2) = tmp.y;
        M33(dst, 2, 2) = tmp.z;
    } else {
        M33(dst, 0, 0) = M44(src, 0, 0);
        M33(dst, 1, 0) = M44(src, 1, 0);
        M33(dst, 2, 0) = M44(src, 2, 0);

        M33(dst, 0, 1) = M44(src, 0, 1);
        M33(dst, 1, 1) = M44(src, 1, 1);
        M33(dst, 2, 1) = M44(src, 2, 1);

        M33(dst, 0, 2) = M44(src, 0, 2);
        M33(dst, 1, 2) = M44(src, 1, 2);
        M33(dst, 2, 2) = M44(src, 2, 2);
    }
#endif
}

void load_rotation_m2(mat2 mat, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
#if PGL_NEON_ENABLED
    pgl_neon_load_rotation_m2(mat, s, c);
#else
#ifndef ROW_MAJOR
    mat[0] = c;
    mat[2] = -s;

    mat[1] = s;
    mat[3] = c;
#else
    mat[0] = c;
    mat[1] = -s;

    mat[2] = s;
    mat[3] = c;
#endif
#endif
}

void mult_m2_m2(mat2 c, mat2 a, mat2 b)
{
#if PGL_NEON_ENABLED
    pgl_neon_mult_m2_m2(c, a, b);
#else
#ifndef ROW_MAJOR
    c[0] = a[0]*b[0] + a[2]*b[1];
    c[2] = a[0]*b[2] + a[2]*b[3];

    c[1] = a[1]*b[0] + a[3]*b[1];
    c[3] = a[1]*b[2] + a[3]*b[3];
#else
    c[0] = a[0]*b[0] + a[1]*b[2];
    c[1] = a[0]*b[1] + a[1]*b[3];

    c[2] = a[2]*b[0] + a[3]*b[2];
    c[3] = a[2]*b[1] + a[3]*b[3];
#endif
#endif
}

extern inline void load_rotation_m2(mat2 mat, float angle);

void mult_m3_m3(mat3 c, mat3 a, mat3 b)
{
#ifndef ROW_MAJOR
    c[0] = a[0]*b[0] + a[3]*b[1] + a[6]*b[2];
    c[3] = a[0]*b[3] + a[3]*b[4] + a[6]*b[5];
    c[6] = a[0]*b[6] + a[3]*b[7] + a[6]*b[8];

    c[1] = a[1]*b[0] + a[4]*b[1] + a[7]*b[2];
    c[4] = a[1]*b[3] + a[4]*b[4] + a[7]*b[5];
    c[7] = a[1]*b[6] + a[4]*b[7] + a[7]*b[8];

    c[2] = a[2]*b[0] + a[5]*b[1] + a[8]*b[2];
    c[5] = a[2]*b[3] + a[5]*b[4] + a[8]*b[5];
    c[8] = a[2]*b[6] + a[5]*b[7] + a[8]*b[8];
#else
    c[0] = a[0]*b[0] + a[1]*b[3] + a[2]*b[6];
    c[1] = a[0]*b[1] + a[1]*b[4] + a[2]*b[7];
    c[2] = a[0]*b[2] + a[1]*b[5] + a[2]*b[8];

    c[3] = a[3]*b[0] + a[4]*b[3] + a[5]*b[6];
    c[4] = a[3]*b[1] + a[4]*b[4] + a[5]*b[7];
    c[5] = a[3]*b[2] + a[4]*b[5] + a[5]*b[8];

    c[6] = a[6]*b[0] + a[7]*b[3] + a[8]*b[6];
    c[7] = a[6]*b[1] + a[7]*b[4] + a[8]*b[7];
    c[8] = a[6]*b[2] + a[7]*b[5] + a[8]*b[8];
#endif
}

void load_rotation_m3(mat3 mat, vec3 v, float angle)
{
    float s, c;

    s = sin(angle);
    c = cos(angle);

    // Rotation matrix is normalized
    normalize_v3(&v);

#if PGL_NEON_ENABLED
    pgl_neon_load_rotation_m3(mat, v, s, c);
#else
    float xx, yy, zz, xy, yz, zx, xs, ys, zs, one_c;

    xx = v.x * v.x;
    yy = v.y * v.y;
    zz = v.z * v.z;
    xy = v.x * v.y;
    yz = v.y * v.z;
    zx = v.z * v.x;
    xs = v.x * s;
    ys = v.y * s;
    zs = v.z * s;
    one_c = 1.0f - c;

#ifndef ROW_MAJOR
    mat[0] = (one_c * xx) + c;
    mat[3] = (one_c * xy) - zs;
    mat[6] = (one_c * zx) + ys;

    mat[1] = (one_c * xy) + zs;
    mat[4] = (one_c * yy) + c;
    mat[7] = (one_c * yz) - xs;

    mat[2] = (one_c * zx) - ys;
    mat[5] = (one_c * yz) + xs;
    mat[8] = (one_c * zz) + c;
#else
    mat[0] = (one_c * xx) + c;
    mat[1] = (one_c * xy) - zs;
    mat[2] = (one_c * zx) + ys;

    mat[3] = (one_c * xy) + zs;
    mat[4] = (one_c * yy) + c;
    mat[5] = (one_c * yz) - xs;

    mat[6] = (one_c * zx) - ys;
    mat[7] = (one_c * yz) + xs;
    mat[8] = (one_c * zz) + c;
#endif
#endif
}



/*
 * mat4
 */

//TODO use restrict?
void mult_m4_m4(mat4 c, mat4 a, mat4 b)
{
#if PGL_NEON_ENABLED
    pgl_neon_mult_m4_m4(c, a, b);
#else
#ifndef ROW_MAJOR
    c[ 0] = a[0]*b[ 0] + a[4]*b[ 1] + a[8]*b[ 2] + a[12]*b[ 3];
    c[ 4] = a[0]*b[ 4] + a[4]*b[ 5] + a[8]*b[ 6] + a[12]*b[ 7];
    c[ 8] = a[0]*b[ 8] + a[4]*b[ 9] + a[8]*b[10] + a[12]*b[11];
    c[12] = a[0]*b[12] + a[4]*b[13] + a[8]*b[14] + a[12]*b[15];

    c[ 1] = a[1]*b[ 0] + a[5]*b[ 1] + a[9]*b[ 2] + a[13]*b[ 3];
    c[ 5] = a[1]*b[ 4] + a[5]*b[ 5] + a[9]*b[ 6] + a[13]*b[ 7];
    c[ 9] = a[1]*b[ 8] + a[5]*b[ 9] + a[9]*b[10] + a[13]*b[11];
    c[13] = a[1]*b[12] + a[5]*b[13] + a[9]*b[14] + a[13]*b[15];

    c[ 2] = a[2]*b[ 0] + a[6]*b[ 1] + a[10]*b[ 2] + a[14]*b[ 3];
    c[ 6] = a[2]*b[ 4] + a[6]*b[ 5] + a[10]*b[ 6] + a[14]*b[ 7];
    c[10] = a[2]*b[ 8] + a[6]*b[ 9] + a[10]*b[10] + a[14]*b[11];
    c[14] = a[2]*b[12] + a[6]*b[13] + a[10]*b[14] + a[14]*b[15];

    c[ 3] = a[3]*b[ 0] + a[7]*b[ 1] + a[11]*b[ 2] + a[15]*b[ 3];
    c[ 7] = a[3]*b[ 4] + a[7]*b[ 5] + a[11]*b[ 6] + a[15]*b[ 7];
    c[11] = a[3]*b[ 8] + a[7]*b[ 9] + a[11]*b[10] + a[15]*b[11];
    c[15] = a[3]*b[12] + a[7]*b[13] + a[11]*b[14] + a[15]*b[15];

#else
    c[0] = a[0]*b[0] + a[1]*b[4] + a[2]*b[8] + a[3]*b[12];
    c[1] = a[0]*b[1] + a[1]*b[5] + a[2]*b[9] + a[3]*b[13];
    c[2] = a[0]*b[2] + a[1]*b[6] + a[2]*b[10] + a[3]*b[14];
    c[3] = a[0]*b[3] + a[1]*b[7] + a[2]*b[11] + a[3]*b[15];

    c[4] = a[4]*b[0] + a[5]*b[4] + a[6]*b[8] + a[7]*b[12];
    c[5] = a[4]*b[1] + a[5]*b[5] + a[6]*b[9] + a[7]*b[13];
    c[6] = a[4]*b[2] + a[5]*b[6] + a[6]*b[10] + a[7]*b[14];
    c[7] = a[4]*b[3] + a[5]*b[7] + a[6]*b[11] + a[7]*b[15];

    c[ 8] = a[8]*b[0] + a[9]*b[4] + a[10]*b[8] + a[11]*b[12];
    c[ 9] = a[8]*b[1] + a[9]*b[5] + a[10]*b[9] + a[11]*b[13];
    c[10] = a[8]*b[2] + a[9]*b[6] + a[10]*b[10] + a[11]*b[14];
    c[11] = a[8]*b[3] + a[9]*b[7] + a[10]*b[11] + a[11]*b[15];

    c[12] = a[12]*b[0] + a[13]*b[4] + a[14]*b[8] + a[15]*b[12];
    c[13] = a[12]*b[1] + a[13]*b[5] + a[14]*b[9] + a[15]*b[13];
    c[14] = a[12]*b[2] + a[13]*b[6] + a[14]*b[10] + a[15]*b[14];
    c[15] = a[12]*b[3] + a[13]*b[7] + a[14]*b[11] + a[15]*b[15];
#endif
#endif
}

void load_rotation_m4(mat4 mat, vec3 v, float angle)
{
    float s, c;

    s = sin(angle);
    c = cos(angle);

    // Rotation matrix is normalized
    normalize_v3(&v);

#if PGL_NEON_ENABLED
    pgl_neon_load_rotation_m4(mat, v, s, c);
#else
    float xx, yy, zz, xy, yz, zx, xs, ys, zs, one_c;

    xx = v.x * v.x;
    yy = v.y * v.y;
    zz = v.z * v.z;
    xy = v.x * v.y;
    yz = v.y * v.z;
    zx = v.z * v.x;
    xs = v.x * s;
    ys = v.y * s;
    zs = v.z * s;
    one_c = 1.0f - c;

#ifndef ROW_MAJOR
    mat[ 0] = (one_c * xx) + c;
    mat[ 4] = (one_c * xy) - zs;
    mat[ 8] = (one_c * zx) + ys;
    mat[12] = 0.0f;

    mat[ 1] = (one_c * xy) + zs;
    mat[ 5] = (one_c * yy) + c;
    mat[ 9] = (one_c * yz) - xs;
    mat[13] = 0.0f;

    mat[ 2] = (one_c * zx) - ys;
    mat[ 6] = (one_c * yz) + xs;
    mat[10] = (one_c * zz) + c;
    mat[14] = 0.0f;

    mat[ 3] = 0.0f;
    mat[ 7] = 0.0f;
    mat[11] = 0.0f;
    mat[15] = 1.0f;
#else
    mat[0] = (one_c * xx) + c;
    mat[1] = (one_c * xy) - zs;
    mat[2] = (one_c * zx) + ys;
    mat[3] = 0.0f;

    mat[4] = (one_c * xy) + zs;
    mat[5] = (one_c * yy) + c;
    mat[6] = (one_c * yz) - xs;
    mat[7] = 0.0f;

    mat[8] = (one_c * zx) - ys;
    mat[9] = (one_c * yz) + xs;
    mat[10] = (one_c * zz) + c;
    mat[11] = 0.0f;

    mat[12] = 0.0f;
    mat[13] = 0.0f;
    mat[14] = 0.0f;
    mat[15] = 1.0f;
#endif
#endif
}


/* TODO
static float det_ij(const mat4 m, const int i, const int j)
{
    float ret, mat[3][3];
    int x = 0, y = 0;

    for (int ii=0; ii<4; ++ii) {
        y = 0;
        if (ii == i) continue;
        for (int jj=0; jj<4; ++jj) {
            if (jj == j) continue;
            mat[x][y] = m[ii*4+jj];
            y++;
        }
        x++;
    }



    ret =  mat[0][0]*(mat[1][1]*mat[2][2]-mat[2][1]*mat[1][2]);
    ret -= mat[0][1]*(mat[1][0]*mat[2][2]-mat[2][0]*mat[1][2]);
    ret += mat[0][2]*(mat[1][0]*mat[2][1]-mat[2][0]*mat[1][1]);

    return ret;
}


void invert_m4(mat4 mInverse, const mat4& m)
{
    int i, j;
    float det, detij;
    mat4 inverse_mat;

    // calculate 4x4 determinant
    det = 0.0f;
    for (i = 0; i < 4; i++) {
        det += (i & 0x1) ? (-m.matrix[i] * det_ij(m, 0, i)) : (m.matrix[i] * det_ij(m, 0, i));
    }
    det = 1.0f / det;

    // calculate inverse
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            detij = det_ij(m, j, i);
            inverse_mat[(i*4)+j] = ((i+j) & 0x1) ? (-detij * det) : (detij *det);
        }
    }

}


*/


////////////////////////////////////////////////////////////////////////////////////////////

//assumes converting from canonical view volume [-1,1]^3
//works just like pgl_Viewport, x and y are lower left corner.  opengl should be 1.
void make_viewport_m4(mat4 mat, int x, int y, unsigned int width, unsigned int height, int opengl)
{
    float w, h, l, t, b, r;

    if (opengl) {
        //See glspec page 104, integer grid is lower left pixel corners
        w = width, h = height;
        l = x, b = y;
        //range is [l, l+w) x [b , b+h)
        //TODO pick best epsilon?
        r = l + w - 0.01; //epsilon larger than float precision
        t = b + h - 0.01;

#ifndef ROW_MAJOR
        mat[ 0] = (r - l) / 2;
        mat[ 4] = 0;
        mat[ 8] = 0;
        mat[12] = (l + r) / 2;

        mat[ 1] = 0;
        //see below
        mat[ 5] = (t - b) / 2;
        mat[ 9] = 0;
        mat[13] = (b + t) / 2;

        mat[ 2] = 0;
        mat[ 6] = 0;
        mat[10] = 1;
        mat[14] = 0;

        mat[ 3] = 0;
        mat[ 7] = 0;
        mat[11] = 0;
        mat[15] = 1;
#else
        mat[0] = (r - l) / 2;
        mat[1] = 0;
        mat[2] = 0;
        mat[3] = (l + r) / 2;

        mat[4] = 0;
        //this used to be negative to flip y till I changed glFramebuffer and draw_pixel to accomplish the same thing
        mat[5] = (t - b) / 2;
        mat[6] = 0;
        mat[7] = (b + t) / 2;

        mat[8] = 0;
        mat[9] = 0;
        mat[10] = 1;
        mat[11] = 0;

        mat[12] = 0;
        mat[13] = 0;
        mat[14] = 0;
        mat[15] = 1;
#endif

    } else {
        //old way with pixel centers at integer coordinates
        //see pages 133/4 and 144 of FoCG
        //necessary for fast integer only bresenham line drawing

        w = width, h = height;
        l = x - 0.5f;
        b = y - 0.5f;
        r = l + w;
        t = b + h;

#ifndef ROW_MAJOR
        mat[ 0] = (r - l) / 2;
        mat[ 4] = 0;
        mat[ 8] = 0;
        mat[12] = (l + r) / 2;

        mat[ 1] = 0;
        //see below
        mat[ 5] = (t - b) / 2;
        mat[ 9] = 0;
        mat[13] = (b + t) / 2;

        mat[ 2] = 0;
        mat[ 6] = 0;
        mat[10] = 1;
        mat[14] = 0;

        mat[ 3] = 0;
        mat[ 7] = 0;
        mat[11] = 0;
        mat[15] = 1;
#else
        mat[0] = (r - l) / 2;
        mat[1] = 0;
        mat[2] = 0;
        mat[3] = (l + r) / 2;

        mat[4] = 0;
        //make this negative to reflect y otherwise positive y maps to lower half of the screen
        //this is mapping the unit square [-1,1]^2 to the window size. x is fine because it increases left to right
        //but the screen coordinates (ie framebuffer memory) increase top to bottom opposite of the canonical square
        //negating this is the easiest way to fix it without any side effects.
        mat[5] = (t - b) / 2;
        mat[6] = 0;
        mat[7] = (b + t) / 2;

        mat[8] = 0;
        mat[9] = 0;
        mat[10] = 1;
        mat[11] = 0;

        mat[12] = 0;
        mat[13] = 0;
        mat[14] = 0;
        mat[15] = 1;
#endif
    }
}

//I can't really think of any reason to ever use this matrix alone.
//You'd always do ortho * pers and really if you're doing perspective projection
//just use make_perspective_matrix (or less likely make perspective_proj_matrix)
//
//This function is really just for completeness sake based off of FoCG 3rd edition pg 152
//changed slightly.  z_near and z_far are always positive and z_near < z_far
//
//Inconsistently, to generate an ortho matrix to multiply with that will get the equivalent
//of the other 2 functions you'd use -z_near and -z_far and near > far.
void make_pers_m4(mat4 mat, float z_near, float z_far)
{
#ifndef ROW_MAJOR
    mat[ 0] = z_near;
    mat[ 4] = 0;
    mat[ 8] = 0;
    mat[12] = 0;

    mat[ 1] = 0;
    mat[ 5] = z_near;
    mat[ 9] = 0;
    mat[13] = 0;

    mat[ 2] = 0;
    mat[ 6] = 0;
    mat[10] = z_near + z_far;
    mat[14] = (z_far * z_near);

    mat[ 3] = 0;
    mat[ 7] = 0;
    mat[11] = -1;
    mat[15] = 0;
#else
    mat[0] = z_near;
    mat[1] = 0;
    mat[2] = 0;
    mat[3] = 0;

    mat[4] = 0;
    mat[5] = z_near;
    mat[6] = 0;
    mat[7] = 0;

    mat[ 8] = 0;
    mat[ 9] = 0;
    mat[10] = z_near + z_far;
    mat[11] = (z_far * z_near);

    mat[12] = 0;
    mat[13] = 0;
    mat[14] = -1;
    mat[15] = 0;
#endif
}

// Create a projection matrix
// Similiar to the old gluPerspective... fov is in radians btw...
void make_perspective_m4(mat4 mat, float fov, float aspect, float n, float f)
{
    float t = n * tanf(fov * 0.5f);
    float b = -t;
    float l = b * aspect;
    float r = -l;

    make_perspective_proj_m4(mat, l, r, b, t, n, f);

}

void make_perspective_proj_m4(mat4 mat, float l, float r, float b, float t, float n, float f)
{
#ifndef ROW_MAJOR
    mat[ 0] = (2.0f * n) / (r - l);
    mat[ 4] = 0.0f;
    mat[ 8] = (r + l) / (r - l);
    mat[12] = 0.0f;

    mat[ 1] = 0.0f;
    mat[ 5] = (2.0f * n) / (t - b);
    mat[ 9] = (t + b) / (t - b);
    mat[13] = 0.0f;

    mat[ 2] = 0.0f;
    mat[ 6] = 0.0f;
    mat[10] = -((f + n) / (f - n));
    mat[14] = -((2.0f * (f*n))/(f - n));

    mat[ 3] = 0.0f;
    mat[ 7] = 0.0f;
    mat[11] = -1.0f;
    mat[15] = 0.0f;
#else
    mat[0] = (2.0f * n) / (r - l);
    mat[1] = 0.0f;
    mat[2] = (r + l) / (r - l);
    mat[3] = 0.0f;

    mat[4] = 0.0f;
    mat[5] = (2.0f * n) / (t - b);
    mat[6] = (t + b) / (t - b);
    mat[7] = 0.0f;

    mat[8] = 0.0f;
    mat[9] = 0.0f;
    mat[10] = -((f + n) / (f - n));
    mat[11] = -((2.0f * (f*n))/(f - n));

    mat[12] = 0.0f;
    mat[13] = 0.0f;
    mat[14] = -1.0f;
    mat[15] = 0.0f;
#endif
}

//n and f really are near and far not min and max so if you want the standard looking down the -z axis
// then n > f otherwise n < f
void make_orthographic_m4(mat4 mat, float l, float r, float b, float t, float n, float f)
{
#ifndef ROW_MAJOR
    mat[ 0] = 2.0f / (r - l);
    mat[ 4] = 0;
    mat[ 8] = 0;
    mat[12] = -((r + l)/(r - l));

    mat[ 1] = 0;
    mat[ 5] = 2.0f / (t - b);
    mat[ 9] = 0;
    mat[13] = -((t + b)/(t - b));

    mat[ 2] = 0;
    mat[ 6] = 0;
    mat[10] = 2.0f / (f - n);  //removed - in front of 2 . . . book doesn't have it but superbible did
    mat[14] = -((n + f)/(f - n));

    mat[ 3] = 0;
    mat[ 7] = 0;
    mat[11] = 0;
    mat[15] = 1;
#else
    mat[0] = 2.0f / (r - l);
    mat[1] = 0;
    mat[2] = 0;
    mat[3] = -((r + l)/(r - l));
    mat[4] = 0;
    mat[5] = 2.0f / (t - b);
    mat[6] = 0;
    mat[7] = -((t + b)/(t - b));
    mat[8] = 0;
    mat[9] = 0;
    mat[10] = 2.0f / (f - n);  //removed - in front of 2 . . . book doesn't have it but superbible did
    mat[11] = -((n + f)/(f - n));
    mat[12] = 0;
    mat[13] = 0;
    mat[14] = 0;
    mat[15] = 1;
#endif


    //now I know why the superbible had the -
    //OpenGL uses a left handed canonical view volume [-1,1]^3 when passed the identity matrix
    //ie in Normalized Device Coordinates.  The math/matrix presented in Fundamentals of Computer
    //Graphics assumes a right handed version of the same volume.  The negative isn't necessary
    //if you set n and f correctly as near and far not low and high
}

//per https://www.opengl.org/sdk/docs/man2/xhtml/gluLookAt.xml
//and glm.g-truc.net (glm/gtc/matrix_transform.inl)
void lookAt(mat4 mat, vec3 eye, vec3 center, vec3 up)
{
    SET_IDENTITY_M4(mat);

    vec3 f = norm_v3(sub_v3s(center, eye));
    vec3 s = norm_v3(cross_v3s(f, up));
    vec3 u = cross_v3s(s, f);

    setx_m4v3(mat, s);
    sety_m4v3(mat, u);
    setz_m4v3(mat, neg_v3(f));
    setc4_m4v3(mat, make_v3(-dot_v3s(s, eye), -dot_v3s(u, eye), dot_v3s(f, eye)));
}

extern inline float rsw_randf(void);
extern inline float rsw_randf_range(float min, float max);
extern inline double rsw_map(double x, double a, double b, double c, double d);
extern inline float rsw_mapf(float x, float a, float b, float c, float d);

extern inline Color make_Color(u8 red, u8 green, u8 blue, u8 alpha);
extern inline Color v4_to_Color(vec4 v);
extern inline void print_Color(Color c, const char* append);
extern inline vec4 Color_to_v4(Color c);
extern inline Line make_Line(float x1, float y1, float x2, float y2);
extern inline void normalize_line(Line* line);
extern inline float line_func(Line* line, float x, float y);
extern inline float line_findy(Line* line, float x);
extern inline float line_findx(Line* line, float y);
extern inline float sq_dist_pt_segment2d(vec2 a, vec2 b, vec2 c);
extern inline void closest_pt_pt_segment(vec2 c, vec2 a, vec2 b, float* t, vec2* d);
extern inline float closest_pt_pt_segment_t(vec2 c, vec2 a, vec2 b);


#if defined(CVEC_MALLOC) && defined(CVEC_FREE) && defined(CVEC_REALLOC)
/* ok */
#elif !defined(CVEC_MALLOC) && !defined(CVEC_FREE) && !defined(CVEC_REALLOC)
/* ok */
#else
#error "Must define all or none of CVEC_MALLOC, CVEC_FREE, and CVEC_REALLOC."
#endif

#ifndef CVEC_MALLOC
#include <stdlib.h>
#define CVEC_MALLOC(sz)      malloc(sz)
#define CVEC_REALLOC(p, sz)  realloc(p, sz)
#define CVEC_FREE(p)         free(p)
#endif

#ifndef CVEC_MEMMOVE
#include <string.h>
#define CVEC_MEMMOVE(dst, src, sz)  memmove(dst, src, sz)
#endif

#ifndef CVEC_ASSERT
#include <assert.h>
#define CVEC_ASSERT(x)       assert(x)
#endif

cvec_sz CVEC_glVertex_Array_SZ = 50;

#define CVEC_glVertex_Array_ALLOCATOR(x) ((x+1) * 2)

cvector_glVertex_Array* cvec_glVertex_Array_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glVertex_Array* vec;
    if (!(vec = (cvector_glVertex_Array*)CVEC_MALLOC(sizeof(cvector_glVertex_Array)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_Array_SZ;

    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glVertex_Array* cvec_init_glVertex_Array_heap(glVertex_Array* vals, cvec_sz num)
{
    cvector_glVertex_Array* vec;
    
    if (!(vec = (cvector_glVertex_Array*)CVEC_MALLOC(sizeof(cvector_glVertex_Array)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glVertex_Array_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex_Array)*num);

    return vec;
}

int cvec_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_Array_SZ;

    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glVertex_Array_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex_Array)*num);

    return 1;
}

int cvec_copyc_glVertex_Array(void* dest, void* src)
{
    cvector_glVertex_Array* vec1 = (cvector_glVertex_Array*)dest;
    cvector_glVertex_Array* vec2 = (cvector_glVertex_Array*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glVertex_Array(vec1, vec2);
}

int cvec_copy_glVertex_Array(cvector_glVertex_Array* dest, cvector_glVertex_Array* src)
{
    glVertex_Array* tmp = NULL;
    if (!(tmp = (glVertex_Array*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glVertex_Array));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array a)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glVertex_Array_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glVertex_Array cvec_pop_glVertex_Array(cvector_glVertex_Array* vec)
{
    return vec->a[--vec->size];
}

glVertex_Array* cvec_back_glVertex_Array(cvector_glVertex_Array* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz num)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_Array_SZ;
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array a)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex_Array));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glVertex_Array_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex_Array));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array* a, cvec_sz num)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_Array_SZ;
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glVertex_Array));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glVertex_Array));
    vec->size += num;
    return 1;
}

glVertex_Array cvec_replace_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array a)
{
    glVertex_Array tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glVertex_Array));
    vec->size -= d;
}


int cvec_reserve_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size)
{
    glVertex_Array* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*(size+CVEC_glVertex_Array_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glVertex_Array_SZ;
    }
    return 1;
}

int cvec_set_cap_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size)
{
    glVertex_Array* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glVertex_Array(cvector_glVertex_Array* vec) { vec->size = 0; }

void cvec_free_glVertex_Array_heap(void* vec)
{
    cvector_glVertex_Array* tmp = (cvector_glVertex_Array*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glVertex_Array(void* vec)
{
    cvector_glVertex_Array* tmp = (cvector_glVertex_Array*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glBuffer_SZ = 50;

#define CVEC_glBuffer_ALLOCATOR(x) ((x+1) * 2)

cvector_glBuffer* cvec_glBuffer_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glBuffer* vec;
    if (!(vec = (cvector_glBuffer*)CVEC_MALLOC(sizeof(cvector_glBuffer)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glBuffer_SZ;

    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glBuffer* cvec_init_glBuffer_heap(glBuffer* vals, cvec_sz num)
{
    cvector_glBuffer* vec;
    
    if (!(vec = (cvector_glBuffer*)CVEC_MALLOC(sizeof(cvector_glBuffer)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glBuffer_SZ;
    vec->size = num;
    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glBuffer)*num);

    return vec;
}

int cvec_glBuffer(cvector_glBuffer* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glBuffer_SZ;

    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glBuffer(cvector_glBuffer* vec, glBuffer* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glBuffer_SZ;
    vec->size = num;
    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glBuffer)*num);

    return 1;
}

int cvec_copyc_glBuffer(void* dest, void* src)
{
    cvector_glBuffer* vec1 = (cvector_glBuffer*)dest;
    cvector_glBuffer* vec2 = (cvector_glBuffer*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glBuffer(vec1, vec2);
}

int cvec_copy_glBuffer(cvector_glBuffer* dest, cvector_glBuffer* src)
{
    glBuffer* tmp = NULL;
    if (!(tmp = (glBuffer*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glBuffer));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glBuffer(cvector_glBuffer* vec, glBuffer a)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glBuffer_ALLOCATOR(vec->capacity);
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glBuffer cvec_pop_glBuffer(cvector_glBuffer* vec)
{
    return vec->a[--vec->size];
}

glBuffer* cvec_back_glBuffer(cvector_glBuffer* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glBuffer(cvector_glBuffer* vec, cvec_sz num)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glBuffer_SZ;
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer a)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glBuffer));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glBuffer_ALLOCATOR(vec->capacity);
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glBuffer));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer* a, cvec_sz num)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glBuffer_SZ;
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glBuffer));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glBuffer));
    vec->size += num;
    return 1;
}

glBuffer cvec_replace_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer a)
{
    glBuffer tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glBuffer(cvector_glBuffer* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glBuffer));
    vec->size -= d;
}


int cvec_reserve_glBuffer(cvector_glBuffer* vec, cvec_sz size)
{
    glBuffer* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*(size+CVEC_glBuffer_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glBuffer_SZ;
    }
    return 1;
}

int cvec_set_cap_glBuffer(cvector_glBuffer* vec, cvec_sz size)
{
    glBuffer* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glBuffer(cvector_glBuffer* vec, glBuffer val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glBuffer(cvector_glBuffer* vec, glBuffer val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glBuffer(cvector_glBuffer* vec) { vec->size = 0; }

void cvec_free_glBuffer_heap(void* vec)
{
    cvector_glBuffer* tmp = (cvector_glBuffer*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glBuffer(void* vec)
{
    cvector_glBuffer* tmp = (cvector_glBuffer*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glTexture_SZ = 50;

#define CVEC_glTexture_ALLOCATOR(x) ((x+1) * 2)

cvector_glTexture* cvec_glTexture_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glTexture* vec;
    if (!(vec = (cvector_glTexture*)CVEC_MALLOC(sizeof(cvector_glTexture)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glTexture_SZ;

    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glTexture* cvec_init_glTexture_heap(glTexture* vals, cvec_sz num)
{
    cvector_glTexture* vec;
    
    if (!(vec = (cvector_glTexture*)CVEC_MALLOC(sizeof(cvector_glTexture)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glTexture_SZ;
    vec->size = num;
    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glTexture)*num);

    return vec;
}

int cvec_glTexture(cvector_glTexture* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glTexture_SZ;

    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glTexture(cvector_glTexture* vec, glTexture* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glTexture_SZ;
    vec->size = num;
    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glTexture)*num);

    return 1;
}

int cvec_copyc_glTexture(void* dest, void* src)
{
    cvector_glTexture* vec1 = (cvector_glTexture*)dest;
    cvector_glTexture* vec2 = (cvector_glTexture*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glTexture(vec1, vec2);
}

int cvec_copy_glTexture(cvector_glTexture* dest, cvector_glTexture* src)
{
    glTexture* tmp = NULL;
    if (!(tmp = (glTexture*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glTexture));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glTexture(cvector_glTexture* vec, glTexture a)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glTexture_ALLOCATOR(vec->capacity);
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glTexture cvec_pop_glTexture(cvector_glTexture* vec)
{
    return vec->a[--vec->size];
}

glTexture* cvec_back_glTexture(cvector_glTexture* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glTexture(cvector_glTexture* vec, cvec_sz num)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glTexture_SZ;
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture a)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glTexture));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glTexture_ALLOCATOR(vec->capacity);
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glTexture));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture* a, cvec_sz num)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glTexture_SZ;
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glTexture));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glTexture));
    vec->size += num;
    return 1;
}

glTexture cvec_replace_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture a)
{
    glTexture tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glTexture(cvector_glTexture* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glTexture));
    vec->size -= d;
}


int cvec_reserve_glTexture(cvector_glTexture* vec, cvec_sz size)
{
    glTexture* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*(size+CVEC_glTexture_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glTexture_SZ;
    }
    return 1;
}

int cvec_set_cap_glTexture(cvector_glTexture* vec, cvec_sz size)
{
    glTexture* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glTexture(cvector_glTexture* vec, glTexture val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glTexture(cvector_glTexture* vec, glTexture val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glTexture(cvector_glTexture* vec) { vec->size = 0; }

void cvec_free_glTexture_heap(void* vec)
{
    cvector_glTexture* tmp = (cvector_glTexture*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glTexture(void* vec)
{
    cvector_glTexture* tmp = (cvector_glTexture*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glProgram_SZ = 50;

#define CVEC_glProgram_ALLOCATOR(x) ((x+1) * 2)

cvector_glProgram* cvec_glProgram_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glProgram* vec;
    if (!(vec = (cvector_glProgram*)CVEC_MALLOC(sizeof(cvector_glProgram)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glProgram_SZ;

    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glProgram* cvec_init_glProgram_heap(glProgram* vals, cvec_sz num)
{
    cvector_glProgram* vec;
    
    if (!(vec = (cvector_glProgram*)CVEC_MALLOC(sizeof(cvector_glProgram)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glProgram_SZ;
    vec->size = num;
    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glProgram)*num);

    return vec;
}

int cvec_glProgram(cvector_glProgram* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glProgram_SZ;

    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glProgram(cvector_glProgram* vec, glProgram* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glProgram_SZ;
    vec->size = num;
    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glProgram)*num);

    return 1;
}

int cvec_copyc_glProgram(void* dest, void* src)
{
    cvector_glProgram* vec1 = (cvector_glProgram*)dest;
    cvector_glProgram* vec2 = (cvector_glProgram*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glProgram(vec1, vec2);
}

int cvec_copy_glProgram(cvector_glProgram* dest, cvector_glProgram* src)
{
    glProgram* tmp = NULL;
    if (!(tmp = (glProgram*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glProgram));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glProgram(cvector_glProgram* vec, glProgram a)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glProgram_ALLOCATOR(vec->capacity);
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glProgram cvec_pop_glProgram(cvector_glProgram* vec)
{
    return vec->a[--vec->size];
}

glProgram* cvec_back_glProgram(cvector_glProgram* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glProgram(cvector_glProgram* vec, cvec_sz num)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glProgram_SZ;
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram a)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glProgram));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glProgram_ALLOCATOR(vec->capacity);
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glProgram));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram* a, cvec_sz num)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glProgram_SZ;
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glProgram));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glProgram));
    vec->size += num;
    return 1;
}

glProgram cvec_replace_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram a)
{
    glProgram tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glProgram(cvector_glProgram* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glProgram));
    vec->size -= d;
}


int cvec_reserve_glProgram(cvector_glProgram* vec, cvec_sz size)
{
    glProgram* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*(size+CVEC_glProgram_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glProgram_SZ;
    }
    return 1;
}

int cvec_set_cap_glProgram(cvector_glProgram* vec, cvec_sz size)
{
    glProgram* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glProgram(cvector_glProgram* vec, glProgram val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glProgram(cvector_glProgram* vec, glProgram val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glProgram(cvector_glProgram* vec) { vec->size = 0; }

void cvec_free_glProgram_heap(void* vec)
{
    cvector_glProgram* tmp = (cvector_glProgram*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glProgram(void* vec)
{
    cvector_glProgram* tmp = (cvector_glProgram*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glVertex_SZ = 50;

#define CVEC_glVertex_ALLOCATOR(x) ((x+1) * 2)

cvector_glVertex* cvec_glVertex_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glVertex* vec;
    if (!(vec = (cvector_glVertex*)CVEC_MALLOC(sizeof(cvector_glVertex)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_SZ;

    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glVertex* cvec_init_glVertex_heap(glVertex* vals, cvec_sz num)
{
    cvector_glVertex* vec;
    
    if (!(vec = (cvector_glVertex*)CVEC_MALLOC(sizeof(cvector_glVertex)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glVertex_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex)*num);

    return vec;
}

int cvec_glVertex(cvector_glVertex* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_SZ;

    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glVertex(cvector_glVertex* vec, glVertex* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glVertex_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex)*num);

    return 1;
}

int cvec_copyc_glVertex(void* dest, void* src)
{
    cvector_glVertex* vec1 = (cvector_glVertex*)dest;
    cvector_glVertex* vec2 = (cvector_glVertex*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glVertex(vec1, vec2);
}

int cvec_copy_glVertex(cvector_glVertex* dest, cvector_glVertex* src)
{
    glVertex* tmp = NULL;
    if (!(tmp = (glVertex*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glVertex));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glVertex(cvector_glVertex* vec, glVertex a)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glVertex_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glVertex cvec_pop_glVertex(cvector_glVertex* vec)
{
    return vec->a[--vec->size];
}

glVertex* cvec_back_glVertex(cvector_glVertex* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glVertex(cvector_glVertex* vec, cvec_sz num)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_SZ;
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex a)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glVertex_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex* a, cvec_sz num)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_SZ;
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glVertex));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glVertex));
    vec->size += num;
    return 1;
}

glVertex cvec_replace_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex a)
{
    glVertex tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glVertex(cvector_glVertex* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glVertex));
    vec->size -= d;
}


int cvec_reserve_glVertex(cvector_glVertex* vec, cvec_sz size)
{
    glVertex* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*(size+CVEC_glVertex_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glVertex_SZ;
    }
    return 1;
}

int cvec_set_cap_glVertex(cvector_glVertex* vec, cvec_sz size)
{
    glVertex* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glVertex(cvector_glVertex* vec, glVertex val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glVertex(cvector_glVertex* vec, glVertex val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glVertex(cvector_glVertex* vec) { vec->size = 0; }

void cvec_free_glVertex_heap(void* vec)
{
    cvector_glVertex* tmp = (cvector_glVertex*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glVertex(void* vec)
{
    cvector_glVertex* tmp = (cvector_glVertex*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


static glContext* c;
static pgl_triangle_callback pgl_tri_callback;
static void *pgl_tri_callback_user;
static int pgl_tri_clip_only;

PGLDEF void pglSetTriangleCallback(pgl_triangle_callback fn, void *user)
{
    pgl_tri_callback = fn;
    pgl_tri_callback_user = user;
    pgl_tri_clip_only = 0;
}

PGLDEF void pglSetClipTriangleCallback(pgl_triangle_callback fn, void *user)
{
    pglSetTriangleCallback(fn, user);
    pgl_tri_clip_only = fn != NULL;
}

/* ---- Built-in fixed-function shaders (declared in portablegl.h) -----------
 * Always compiled so a demo using them runs on the CPU rasterizer everywhere;
 * on a PGL_G3D build the matching triangles are offloaded to the GPU (see
 * the pgl_gpu_* helpers below). */
PGLDEF void pgl_ff_solid_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    (void)vs_output;
    builtins->gl_Position = mult_m4_v4(((pgl_ff_solid_uniform*)uniforms)->mvp_matrix, vertex_attribs[0]);
}

PGLDEF void pgl_ff_solid(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    (void)fs_input;
    builtins->gl_FragColor = ((pgl_ff_solid_uniform*)uniforms)->color;
}

PGLDEF void pgl_ff_gouraud_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    ((vec4*)vs_output)[0] = vertex_attribs[1];   /* pass per-vertex colour */
    builtins->gl_Position = mult_m4_v4(((pgl_ff_gouraud_uniform*)uniforms)->mvp_matrix, vertex_attribs[0]);
}

PGLDEF void pgl_ff_gouraud(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    (void)uniforms;
    builtins->gl_FragColor = ((vec4*)fs_input)[0];
}

PGLDEF void pgl_ff_tex_modulate_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    vs_output[0] = vertex_attribs[1].x;
    vs_output[1] = vertex_attribs[1].y;
    builtins->gl_Position = mult_m4_v4(((pgl_ff_tex_modulate_uniform*)uniforms)->mvp_matrix, vertex_attribs[0]);
}

PGLDEF void pgl_ff_tex_modulate(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    builtins->gl_FragColor = texture2D(((pgl_ff_tex_modulate_uniform*)uniforms)->tex0,
                                       fs_input[0], fs_input[1]);
}

#ifdef PGL_G3D
/* ---- GPU 3D-offload fast path (PGL_G3D) ---------------------------------
 * Eligibility is decided here (this TU owns the context `c`); pgl_g3d.c owns
 * the shm target/staging and the g2d marshalling.  A primitive is offloaded only
 * when its program uses a built-in FF fragment shader AND the state is inside
 * the subset the back end renders today (depth test+write, back-face cull,
 * scissor, polygon offset and the colour write mask are all marshalled as of
 * Phase B2c; blend, stencil and fragdepth/discard are not).  Everything else
 * keeps the CPU NEON path, and any CPU primitive flushes the pending GPU batch
 * first so painter's order is preserved across the two engines sharing one back
 * buffer. */
static int _pgl_gpu_active;   /* back buffer adopted the GPU shm target */
static int _pgl_gpu_off;      /* latched off; stop probing */
/* Set for a frame whose pgl_Clear was scissored or colour-masked: the GPU folds
 * the frame clear into its first draw dispatch as a FULL-FRAME, unmasked
 * CLEAR_RENDER_TARGETS that would clobber the precise CPU clear, so the whole
 * frame is pinned to the CPU (re-evaluated at the next pgl_Clear). */
static int _pgl_gpu_frame_cpu;

static int pgl_c255(float f, int max)
{
    int v = (int)(f * (float)max + 0.5f);
    return v < 0 ? 0 : (v > max ? max : v);
}

/* pack a portablegl pixel into the back end's 0xAABBGGRR rgba8 convention */
static uint32_t pgl_rgba8_from_pixel(pix_t p)
{
#if PGL_BITDEPTH == 32
    Color col = PIXEL_TO_COLOR(p);
    return ((uint32_t)col.a << 24) | ((uint32_t)col.b << 16) |
           ((uint32_t)col.g << 8) | (uint32_t)col.r;
#else
    (void)p;
    return 0;   /* GPU target is RGBA8; only 32-bit back buffers are supported */
#endif
}

static uint32_t pgl_rgba8_from_vec4(vec4 v)
{
#if PGL_BITDEPTH == 32
    return pgl_rgba8_from_pixel(RGBA_TO_PIXEL(pgl_c255(v.x, 255), pgl_c255(v.y, 255),
                                              pgl_c255(v.z, 255), pgl_c255(v.w, 255)));
#else
    (void)v;
    return 0;
#endif
}

/* perspective-divide a clip-space position to NDC (the back end's vtx shader
 * applies the viewport scale with a constant w, so the divide is done here) */
static void pgl_gpu_ndc(vec4 clip, float* out)
{
    float iw = (clip.w != 0.0f) ? 1.0f / clip.w : 0.0f;
    out[0] = clip.x * iw;
    out[1] = clip.y * iw;
    out[2] = clip.z * iw;
}

/* Lazily bring up the GPU target and adopt it as the back buffer so CPU and GPU
 * draws land in the same surface (and the demo's present blit stays zero-copy).
 * Returns 1 when the GPU path is live. */
static int pgl_gpu_begin(int w, int h)
{
#if PGL_BITDEPTH != 32
    (void)w; (void)h;
    return 0;
#else
    if (_pgl_gpu_active)
        return pgl_g3d_enabled();
    if (_pgl_gpu_off)
        return 0;
    if (w <= 0 || h <= 0)
        return 0;
    if (!pgl_g3d_init(w, h)) {
        _pgl_gpu_off = 1;
        return 0;
    }
    {
        u8* px = (u8*)pgl_g3d_pixels();
        size_t bytes = (size_t)w * (size_t)h * sizeof(pix_t);
        if (c->back_buffer.buf) {
            if (c->user_alloced_backbuf) {
                /* user filled their own buffer before GPU bring-up
                   (e.g. a pre-rendered CPU frame); carry it over to
                   the GPU target so the first frame is not blank */
                PGL_MEMMOVE(px, c->back_buffer.buf, bytes);
            }
            else {
                /* internally allocated by init_glContext (malloc):
                   holds uninitialised data, so copying it would
                   overwrite the probe-clear's clean black with
                   garbage and cause a startup noise flash on the
                   desktop.  just free it. */
                PGL_FREE(c->back_buffer.buf);
            }
        }
        c->back_buffer.buf = px;
        c->back_buffer.w = w;
        c->back_buffer.h = h;
        c->back_buffer.lastrow = px + (h-1)*w*sizeof(pix_t);
        c->user_alloced_backbuf = GL_TRUE;   /* pgl_g3d owns/frees the shm */
    }
    _pgl_gpu_active = 1;
    return 1;
#endif
}

/* Public bring-up: force the GPU target live and adopt pgl_g3d_pixels() as the
 * back buffer, so an app's compiled-mesh draws (which render into pgl_g3d_pixels())
 * and its CPU pgl_DrawArrays fallback share ONE surface and the present blit stays
 * zero-copy.  Returns 1 when the GPU path is live (then pglGetBackBuffer() is the
 * adopted GPU pixels), 0 when it is off / unavailable (CPU back buffer unchanged).
 * Idempotent: a second call just reports pgl_g3d_enabled(). */
PGLDEF int pglGpuBegin(int w, int h)
{
    return pgl_gpu_begin(w, h);
}

/* flush a pending GPU batch before any CPU-side drawing (painter's order), and
 * consume a pending frame clear: the deferred GPU first-dispatch clear must not
 * run AFTER this CPU primitive wrote the shared back buffer, or it erases it. */
static void pgl_gpu_fence(void)
{
    if (_pgl_gpu_active) {
        pgl_g3d_flush();
        pgl_g3d_consume_frame_clear();
    }
}

/* Phase D texture cache: map a GL texture name to a pgl_g3d slot.  Every
 * active level's datagen participates in the key, so an in-place TexSubImage
 * cannot leave a stale staged image behind. */
typedef struct {
    GLuint      tex_id;   /* 0 = free slot */
    GLsizei     w, h;
    const void* data;     /* level-0 pointer, to catch realloc-on-resize */
    int         slot;
    int         tiled;    /* 1 = backend sampler layout, 0 = raster TEX_MOD */
    int         levels;
    GLuint      datagen[PGL_MAX_MIP_LEVELS];
} pgl_gpu_tex_entry_t;
static pgl_gpu_tex_entry_t _pgl_gpu_tex[PGL_G3D_MAX_TEX];
static int _pgl_gpu_tex_next;

/* Resolve tex_id to a defined canonical 2D texture.  Filter, wrap and mip
 * completeness are path-specific and checked by the two resolvers below. */
static glTexture* pgl_gpu_tex_get(GLuint tex_id)
{
    glTexture* t;

    if (tex_id == 0 || !c)
        return NULL;
    if ((int)tex_id >= c->textures.size)
        return NULL;
    t = &c->textures.a[tex_id];
    /* pgl_BindTexture stores type as target-(GL_TEXTURE_UNBOUND+1) (see the
     * pglDrawFrame tex check), NOT the raw GL_TEXTURE_2D enum. */
    if (t->deleted || t->type != GL_TEXTURE_2D-(GL_TEXTURE_UNBOUND+1) ||
        !t->levels[0][0].defined || t->levels[0][0].data == NULL)
        return NULL;
    if (t->levels[0][0].w <= 0 || t->levels[0][0].h <= 0)
        return NULL;
    return t;
}

/* Shared cache+upload tail for both GPU texture resolvers: return the slot
 * already holding tex_id (same id/size/data/layout), else upload into a free
 * slot (round-robin evict on a full table).  tiled selects the V3D tiled
 * layout a compiled-shader sampler needs (the TMU cannot read raster rows)
 * versus the raster image the fixed-function TEX_MOD kernel reads; the same GL
 * texture used by both paths therefore occupies two slots.  Returns the slot,
 * or -1 on an upload failure (which does NOT latch the GPU off). */
static int pgl_gpu_tex_cache(GLuint tex_id, glTexture* t, int tiled)
{
    const glTextureLevel* base = &t->levels[0][0];
    int i, ok, level, levels = 1;

    if (tiled && t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR) {
        int w = base->w, h = base->h;
        while (w > 1 || h > 1) {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
            ++levels;
        }
    }
    if (levels > PGL_MAX_MIP_LEVELS)
        return -1;
    for (i = 0; i < PGL_G3D_MAX_TEX; i++) {
        if (_pgl_gpu_tex[i].tex_id == tex_id &&
            _pgl_gpu_tex[i].w == base->w && _pgl_gpu_tex[i].h == base->h &&
            _pgl_gpu_tex[i].data == (const void*)base->data &&
            _pgl_gpu_tex[i].tiled == tiled &&
            _pgl_gpu_tex[i].levels == levels) {
            for (level = 0; level < levels; ++level)
                if (_pgl_gpu_tex[i].datagen[level] !=
                    t->levels[0][level].datagen)
                    break;
            if (level == levels)
                return _pgl_gpu_tex[i].slot;
        }
    }

    /* Miss: prefer a free slot, else round-robin evict. */
    for (i = 0; i < PGL_G3D_MAX_TEX; i++) {
        if (_pgl_gpu_tex[i].tex_id == 0) {
            _pgl_gpu_tex_next = i;
            break;
        }
    }
    i = _pgl_gpu_tex_next;
    if (_pgl_gpu_tex[i].tex_id != 0)
        pgl_g3d_free_texture(_pgl_gpu_tex[i].slot);
    if (tiled) {
        pgl_g3d_tex_level_t images[PGL_MAX_MIP_LEVELS];
        for (level = 0; level < levels; ++level) {
            images[level].rgba8 = t->levels[0][level].data;
            images[level].w = (uint32_t)t->levels[0][level].w;
            images[level].h = (uint32_t)t->levels[0][level].h;
        }
        ok = pgl_g3d_upload_texture_mips_tiled(i, 1u, (uint32_t)levels, images);
    } else {
        ok = pgl_g3d_upload_texture(i, base->w, base->h, base->data);
    }
    if (!ok) {
        _pgl_gpu_tex[i].tex_id = 0;
        return -1;
    }
    memset(&_pgl_gpu_tex[i], 0, sizeof(_pgl_gpu_tex[i]));
    _pgl_gpu_tex[i].tex_id = tex_id;
    _pgl_gpu_tex[i].w = base->w;
    _pgl_gpu_tex[i].h = base->h;
    _pgl_gpu_tex[i].data = (const void*)base->data;
    _pgl_gpu_tex[i].slot = i;
    _pgl_gpu_tex[i].tiled = tiled;
    _pgl_gpu_tex[i].levels = levels;
    for (level = 0; level < levels; ++level)
        _pgl_gpu_tex[i].datagen[level] = t->levels[0][level].datagen;
    _pgl_gpu_tex_next = (i + 1) % PGL_G3D_MAX_TEX;
    return i;
}

static int pgl_gpu_tex_slot(GLuint tex_id)
{
    glTexture* t = pgl_gpu_tex_get(tex_id);
    if (!t)
        return -1;
    /* GPU TEX_MOD is point-sampled, CLAMP-to-edge, and reads raster rows. */
    if (t->mag_filter != GL_NEAREST || t->min_filter != GL_NEAREST ||
        t->wrap_s != GL_CLAMP_TO_EDGE || t->wrap_t != GL_CLAMP_TO_EDGE)
        return -1;
    return pgl_gpu_tex_cache(tex_id, t, 0);
}

/* Compiled-shader sampler-unit resolver.  The raspi5 back end's fixed
 * SAMPLER_STATE record is GL_NEAREST + GL_REPEAT (see pack_sampler_state_nearest
 * in v3d_3d.c), so accept exactly that wrap - shadertoy's textures are all
 * NEAREST/REPEAT.  Shares the slot cache with the TEX_MOD path but asks for
 * the V3D tiled layout (the TMU samples LT/UBLINEAR/UIF images, never raster
 * rows; filter/wrap live in the sampler record, not the image).  Returns the
 * slot, or -1 when tex_id cannot back a compiled sampler (leaving its unit
 * unbound -> the draw declines to CPU). */
static int pgl_gpu_ctex_slot(GLuint tex_id)
{
    glTexture* t = pgl_gpu_tex_get(tex_id);
    if (!t)
        return -1;
    if (t->wrap_s != GL_REPEAT || t->wrap_t != GL_REPEAT ||
        !pgl_texture_complete(t, 0))
        return -1;
    return pgl_gpu_tex_cache(tex_id, t, 1);
}

/* Resolve the built-in fixed-function fragment shader plus the eligibility gates
 * and the mapped depth/cull/scissor/polygon-offset/colour-mask render state
 * shared by the triangle/point/line offload paths.  poly_off is the polygon-
 * offset enable relevant to the primitive being drawn (fill/point/line); as of
 * Phase B2c it is marshalled into ext rather than declined, as are an active
 * scissor and a partial colour mask.  blend, stencil and fragdepth/discard still
 * keep the primitive on the CPU.  want_cull is 0 for points/lines (facing is
 * meaningless there) and 1 for triangles.  Returns 1 and fills the fs_id, solid,
 * depth_func, depth_mask, cull_cfg, tex_slot and ext outputs when the GPU may
 * draw the primitive, 0 when the CPU must.  tex_slot is -1 for SOLID/GOURAUD
 * and the pgl_g3d slot index for TEX_MOD (points/lines never take TEX_MOD). */
static int pgl_gpu_state(int poly_off, int want_cull, int* fs_id,
                         uint32_t* solid, uint32_t* depth_func,
                         uint32_t* depth_mask, uint32_t* cull_cfg,
                         int* tex_slot, pgl_g3d_ext_t* ext)
{
    glProgram* p;

    if (!_pgl_gpu_active && _pgl_gpu_off)
        return 0;
    if (!c)
        return 0;
    if (_pgl_gpu_frame_cpu)
        return 0;   /* this frame's pgl_Clear was scissored/colour-masked: the GPU
                     * full-frame first-dispatch clear would clobber it, so the
                     * whole frame stays on the CPU */

    *tex_slot = -1;
    p = &c->programs.a[c->cur_program];
    if (p->fragment_shader == pgl_ff_solid) {
        *fs_id = PGL_G3D_FS_SOLID;
        *solid = pgl_rgba8_from_vec4(((pgl_ff_solid_uniform*)p->uniform)->color);
    } else if (p->fragment_shader == pgl_ff_gouraud) {
        *fs_id = PGL_G3D_FS_GOURAUD;
    } else if (p->fragment_shader == pgl_ff_tex_modulate) {
        int slot = pgl_gpu_tex_slot(((pgl_ff_tex_modulate_uniform*)p->uniform)->tex0);
        if (slot < 0)
            return 0;
        *fs_id = PGL_G3D_FS_TEX_MOD;
        *tex_slot = slot;
    } else {
        return 0;   /* arbitrary C fragment shader stays on the CPU */
    }

    /* blend / stencil / fragdepth-or-discard are outside the fixed-function
     * back end and stay on the CPU.  Scissor (CLIP_WINDOW), polygon offset
     * (CFG_BITS bit3 + Depth Offset) and the colour write mask
     * (COLOR_WRITE_MASKS) ARE rendered on the GPU as of Phase B2c, so they are
     * marshalled into ext below; depth test+write and back-face cull are
     * likewise mapped rather than declined. */
    if (c->blend || c->stencil_test || c->fragdepth_or_discard)
        return 0;

    *depth_func = PGL_G3D_DEPTH_NONE;
    *depth_mask = 0;
    *cull_cfg = PGL_G3D_CULL_NONE;

    if (c->depth_test) {
        switch (c->depth_func) {
        case GL_NEVER:    *depth_func = PGL_G3D_DEPTH_NEVER;    break;
        case GL_LESS:     *depth_func = PGL_G3D_DEPTH_LESS;     break;
        case GL_EQUAL:    *depth_func = PGL_G3D_DEPTH_EQUAL;    break;
        case GL_LEQUAL:   *depth_func = PGL_G3D_DEPTH_LEQUAL;   break;
        case GL_GREATER:  *depth_func = PGL_G3D_DEPTH_GREATER;  break;
        case GL_NOTEQUAL: *depth_func = PGL_G3D_DEPTH_NOTEQUAL; break;
        case GL_GEQUAL:   *depth_func = PGL_G3D_DEPTH_GEQUAL;   break;
        case GL_ALWAYS:   *depth_func = PGL_G3D_DEPTH_ALWAYS;   break;
        default:          return 0;   /* unknown func stays on the CPU */
        }
        *depth_mask = c->depth_mask ? 1u : 0u;
    }

    if (want_cull && c->cull_face) {
        /* encode the full FRONT/BACK x CW/CCW matrix into a PGL_G3D_CULL_*
         * config code; the back end maps it onto the CFG_BITS facing bits. */
        int cw = (c->front_face == GL_CW);
        switch (c->cull_mode) {
        case GL_BACK:  *cull_cfg = cw ? PGL_G3D_CULL_BACK_CW
                                      : PGL_G3D_CULL_BACK_CCW;  break;
        case GL_FRONT: *cull_cfg = cw ? PGL_G3D_CULL_FRONT_CW
                                      : PGL_G3D_CULL_FRONT_CCW; break;
        case GL_FRONT_AND_BACK: *cull_cfg = PGL_G3D_CULL_BOTH;  break;
        default:       return 0;   /* unknown cull mode stays on the CPU */
        }
    }

    /* Phase B2c extended state.  Zeroed first so the struct padding is
     * deterministic (it joins the client batch homogeneity key) and the
     * defaults are scissor-off / no-offset / write-all. */
    memset(ext, 0, sizeof(*ext));

    if (poly_off) {
        ext->poly_enable = 1;
        ext->poly_factor = c->poly_factor;
        ext->poly_units = c->poly_units;
    }

    if (c->scissor_test) {
        /* c->lx/ly/ux/uy are the clamped scissor rect in GL window coords
         * (origin bottom-left, y up); the render target is y-down, so the
         * CLIP_WINDOW Bottom field is fb_h - uy.  An empty rect draws nothing,
         * which a clip window cannot express - keep that frame on the CPU. */
        if (c->ux <= c->lx || c->uy <= c->ly)
            return 0;
        ext->scissor_x = c->lx;
        ext->scissor_y = c->back_buffer.h - c->uy;
        ext->scissor_w = c->ux - c->lx;
        ext->scissor_h = c->uy - c->ly;
    }

#ifndef PGL_DISABLE_COLOR_MASK
    {
        /* c->color_mask is a packed pix_t mask; convert it to the 4-bit GL-order
         * write-enable nibble the back end inverts into COLOR_WRITE_MASKS. */
        uint32_t cm = 0;
        if (c->color_mask & PGL_RMASK) cm |= 0x1u;
        if (c->color_mask & PGL_GMASK) cm |= 0x2u;
        if (c->color_mask & PGL_BMASK) cm |= 0x4u;
        if (c->color_mask & PGL_AMASK) cm |= 0x8u;
        /* all-disabled (write nothing) is degenerate and ext.color_mask == 0
         * means write-all downstream, so keep such a frame on the CPU. */
        if (cm == 0)
            return 0;
        ext->color_mask = cm;
    }
#else
    ext->color_mask = 0xFu;   /* colour masks compiled out: write all four */
#endif

    return 1;
}

/* Try to offload one post-VS triangle to the GPU.  Returns 1 when it was
 * batched (caller must NOT rasterize it), 0 when the CPU must draw it. */
static int pgl_gpu_tri(glVertex* v0, glVertex* v1, glVertex* v2)
{
    int fs_id, tex_slot;
    uint32_t solid = 0, depth_func, depth_mask, cull_cfg;
    const float *c0 = NULL, *c1 = NULL, *c2 = NULL;
    float n0[3], n1[3], n2[3];
    pgl_g3d_ext_t ext;

    if (!pgl_gpu_state(c->poly_offset_fill, 1, &fs_id, &solid,
                       &depth_func, &depth_mask, &cull_cfg, &tex_slot, &ext))
        return 0;
    if (fs_id == PGL_G3D_FS_GOURAUD) {
        c0 = v0->vs_out; c1 = v1->vs_out; c2 = v2->vs_out;
    }

    if (!pgl_gpu_begin(c->back_buffer.w, c->back_buffer.h))
        return 0;

    pgl_gpu_ndc(v0->clip_space, n0);
    pgl_gpu_ndc(v1->clip_space, n1);
    pgl_gpu_ndc(v2->clip_space, n2);

    if (fs_id == PGL_G3D_FS_TEX_MOD) {
        /* pgl_ff_tex_modulate_vs writes u/v into vs_out[0..1]; the GPU TEX_MOD
         * kernel is point-sampled CLAMP-to-edge, matching the CPU path when the
         * texture is NEAREST + CLAMP_TO_EDGE (enforced by pgl_gpu_tex_slot). */
        float uv0[2], uv1[2], uv2[2];
        uv0[0] = v0->vs_out[0]; uv0[1] = v0->vs_out[1];
        uv1[0] = v1->vs_out[0]; uv1[1] = v1->vs_out[1];
        uv2[0] = v2->vs_out[0]; uv2[1] = v2->vs_out[1];
        return pgl_g3d_batch_tri_tex(tex_slot, n0, n1, n2, uv0, uv1, uv2,
                                      depth_func, depth_mask, cull_cfg, &ext);
    }
    return pgl_g3d_batch_tri(fs_id, solid, n0, n1, n2, c0, c1, c2,
                              depth_func, depth_mask, cull_cfg, &ext);
}

/* Try to offload one post-VS point (GL_POINTS) to the GPU.  Only a point_size
 * <= 1 rasterizes a deterministic single pixel on both engines; larger points and
 * point sprites are a multi-pixel quad the fixed-function back end does not model,
 * so they stay on the CPU.  Culling is meaningless for points.  Returns 1 when
 * batched, 0 when the CPU must draw it. */
static int pgl_gpu_point(glVertex* v)
{
    int fs_id, tex_slot;
    uint32_t solid = 0, depth_func, depth_mask, cull_cfg;
    const float* c0 = NULL;
    float n0[3];
    pgl_g3d_ext_t ext;

    if (c->point_size > 1.0f)
        return 0;
    if (!pgl_gpu_state(c->poly_offset_pt, 0, &fs_id, &solid,
                       &depth_func, &depth_mask, &cull_cfg, &tex_slot, &ext))
        return 0;
    if (fs_id == PGL_G3D_FS_TEX_MOD)
        return 0;   /* points carry no uv; keep TEX_MOD points on the CPU */
    if (fs_id == PGL_G3D_FS_GOURAUD)
        c0 = v->vs_out;

    if (!pgl_gpu_begin(c->back_buffer.w, c->back_buffer.h))
        return 0;

    pgl_gpu_ndc(v->clip_space, n0);
    return pgl_g3d_batch_point(fs_id, solid, n0, c0,
                                depth_func, depth_mask, cull_cfg, &ext);
}

/* Try to offload one post-VS line segment to the GPU.  Called only for a fully
 * unclipped segment (both endpoints inside the clip volume), so the raw vertex
 * clip positions ARE the segment.  Only a 1px non-smooth line matches the back
 * end's fixed LINE_WIDTH=1 rasterizer; AA and thick lines stay on the CPU.
 * Culling is meaningless for lines.  Returns 1 when batched, 0 when the CPU must
 * draw it. */
static int pgl_gpu_line(glVertex* v1, glVertex* v2)
{
    int fs_id, tex_slot;
    uint32_t solid = 0, depth_func, depth_mask, cull_cfg;
    const float *c0 = NULL, *c1 = NULL;
    float n0[3], n1[3];
    pgl_g3d_ext_t ext;

    if (c->line_smooth || c->line_width > 1.0f)
        return 0;
    if (!pgl_gpu_state(c->poly_offset_line, 0, &fs_id, &solid,
                       &depth_func, &depth_mask, &cull_cfg, &tex_slot, &ext))
        return 0;
    if (fs_id == PGL_G3D_FS_TEX_MOD)
        return 0;   /* lines carry no uv; keep TEX_MOD lines on the CPU */
    if (fs_id == PGL_G3D_FS_GOURAUD) {
        c0 = v1->vs_out; c1 = v2->vs_out;
    }

    if (!pgl_gpu_begin(c->back_buffer.w, c->back_buffer.h))
        return 0;

    pgl_gpu_ndc(v1->clip_space, n0);
    pgl_gpu_ndc(v2->clip_space, n1);
    return pgl_g3d_batch_line(fs_id, solid, n0, n1, c0, c1,
                               depth_func, depth_mask, cull_cfg, &ext);
}

/* ---- On-device-compiled full-screen-frame GPU offload (WS4/WS5) -----------
 * Registry mapping a CPU full-screen frag_func to its compiled VideoCore
 * program + a per-frame uniform-blob packer.  pglDrawFrame()/pglDrawFrame2()
 * consult it and, when the GPU is live and the program is valid, dispatch the
 * frame as a pos-only full-screen quad (pgl_g3d_batch_tri_compiled) instead of
 * the CPU per-pixel loop.  See portablegl.h for the public registration API. */
#define PGL_FRAME_GPU_MAX 16
typedef struct {
    int               used;
    frag_func         fs;
    int               prog;   /* a pgl_g3d_prog (typedef int) */
    pgl_frame_pack_fn pack;
    /* Sampler-unit -> GL texture-name bindings (iChannelU -> unit U) declared
     * via pglRegisterFrameGPUTextures; 0 = unbound.  Resolved to pgl_g3d slots
     * and bound each frame in pgl_gpu_frame. */
    GLuint            tex_units[PGL_G3D_MAX_TEX_UNITS];
    int               n_tex_units;
} pgl_frame_gpu_entry_t;
static pgl_frame_gpu_entry_t _pgl_frame_gpu[PGL_FRAME_GPU_MAX];
static pgl_frame_gpu_entry_t* pgl_frame_gpu_find(frag_func fs);

PGLDEF void pglRegisterFrameGPU(frag_func fs, int prog, pgl_frame_pack_fn pack)
{
    int i, free_i = -1;
    if (!fs)
        return;
    for (i = 0; i < PGL_FRAME_GPU_MAX; i++) {
        if (_pgl_frame_gpu[i].used && _pgl_frame_gpu[i].fs == fs) {
            _pgl_frame_gpu[i].prog = prog;
            _pgl_frame_gpu[i].pack = pack;
            return;
        }
        if (free_i < 0 && !_pgl_frame_gpu[i].used)
            free_i = i;
    }
    if (free_i < 0)
        return;   /* registry full: the effect simply stays on the CPU */
    _pgl_frame_gpu[free_i].used = 1;
    _pgl_frame_gpu[free_i].fs   = fs;
    _pgl_frame_gpu[free_i].prog = prog;
    _pgl_frame_gpu[free_i].pack = pack;
}

PGLDEF void pglRegisterFrameGPUTextures(frag_func fs, const GLuint* tex_units, int n)
{
    pgl_frame_gpu_entry_t* e = pgl_frame_gpu_find(fs);
    int u;
    if (!e)
        return;
    if (n < 0)
        n = 0;
    if (n > PGL_G3D_MAX_TEX_UNITS)
        n = PGL_G3D_MAX_TEX_UNITS;
    for (u = 0; u < PGL_G3D_MAX_TEX_UNITS; u++)
        e->tex_units[u] = (tex_units && u < n) ? tex_units[u] : 0;
    e->n_tex_units = n;
}

PGLDEF void pglUnregisterFrameGPU(frag_func fs)
{
    int i, u;
    for (i = 0; i < PGL_FRAME_GPU_MAX; i++) {
        if (_pgl_frame_gpu[i].used && _pgl_frame_gpu[i].fs == fs) {
            _pgl_frame_gpu[i].used = 0;
            _pgl_frame_gpu[i].fs   = NULL;
            _pgl_frame_gpu[i].prog = PGL_G3D_PROG_INVALID;
            _pgl_frame_gpu[i].pack = NULL;
            for (u = 0; u < PGL_G3D_MAX_TEX_UNITS; u++)
                _pgl_frame_gpu[i].tex_units[u] = 0;
            _pgl_frame_gpu[i].n_tex_units = 0;
        }
    }
}

static pgl_frame_gpu_entry_t* pgl_frame_gpu_find(frag_func fs)
{
    int i;
    if (!fs)
        return NULL;
    for (i = 0; i < PGL_FRAME_GPU_MAX; i++)
        if (_pgl_frame_gpu[i].used && _pgl_frame_gpu[i].fs == fs)
            return &_pgl_frame_gpu[i];
    return NULL;
}

/* Try to render one full-screen frame on the GPU with the compiled program
 * registered for fs.  Returns 1 when the frame was dispatched on the GPU (the
 * caller must NOT run the CPU loop), 0 when the CPU must draw it (no
 * registration, GPU off/latched, invalid program, or a declined upload). */
static int pgl_gpu_frame(frag_func fs, void* uniforms)
{
    pgl_frame_gpu_entry_t* e = pgl_frame_gpu_find(fs);
    uint32_t blob[PGL_G3D_MAX_UNIF];
    uint32_t count = 0;
    int w, h;
    /* NDC full-screen quad (z=0) as two pos-only triangles; the GPU derives
     * gl_FragCoord from the rasterized window position.
     *
     * ORIENTATION (WS6 hardware check): the CPU path feeds the frag shader
     * gl_FragCoord.y = the row index from the TOP (y-down back buffer).  V3D
     * window coords are y-up, so this quad is wound so NDC y=+1 (top) lands on
     * the target's row 0 to match the CPU convention; if a physical Pi5 shows
     * the effect vertically flipped against the CPU reference, swap the y sign
     * of the four corners here. */
    static const float qa[3] = { -1.0f, -1.0f, 0.0f };
    static const float qb[3] = {  1.0f, -1.0f, 0.0f };
    static const float qc[3] = {  1.0f,  1.0f, 0.0f };
    static const float qd[3] = { -1.0f,  1.0f, 0.0f };

    {
        static int _gf_diag = 0;
        if (!_gf_diag) { _gf_diag = 1;
            printf("PGLGPUFRAME: e=%p prog=%d valid=%d active=%d off=%d c=%p frame_cpu=%d wh=%dx%d\n",
                   (void*)e, e ? e->prog : -99, e ? pgl_g3d_program_valid(e->prog) : -1,
                   _pgl_gpu_active, _pgl_gpu_off, (void*)c, _pgl_gpu_frame_cpu,
                   c ? c->back_buffer.w : -1, c ? c->back_buffer.h : -1); fflush(stdout); }
    }
    if (!e)
        return 0;
    if (e->prog < 0 || !pgl_g3d_program_valid(e->prog))
        return 0;
    if (!_pgl_gpu_active && _pgl_gpu_off)
        return 0;
    if (!c)
        return 0;
    if (_pgl_gpu_frame_cpu)
        return 0;   /* scissored/masked clear pinned this frame to the CPU */

    w = c->back_buffer.w;
    h = c->back_buffer.h;
    if (w <= 0 || h <= 0)
        return 0;
    if (!pgl_gpu_begin(w, h))
        return 0;

    if (e->pack)
        e->pack(blob, &count, uniforms, w, h, e->prog);
    if (count > PGL_G3D_MAX_UNIF)
        count = PGL_G3D_MAX_UNIF;

    pgl_g3d_set_shader_program(e->prog);
    pgl_g3d_set_uniforms(count ? blob : NULL, count);

    /* WS4: bind this effect's sampler-unit textures (iChannelU -> unit U) so the
     * compiled FS's QUNIFORM_TMU_CONFIG slots resolve to the uploaded images.
     * A texture that cannot back a compiled sampler (pgl_gpu_ctex_slot < 0)
     * leaves its unit unbound, which declines the draw back to the CPU rather
     * than sampling a wrong image.  Bound before batching: pgl_g3d_bind_tex_unit
     * only records state (no flush), read at the flush below. */
    {
        int u;
        for (u = 0; u < e->n_tex_units; u++) {
            int slot = pgl_gpu_ctex_slot(e->tex_units[u]);
            glTexture* ft = pgl_gpu_tex_get(e->tex_units[u]);
            int mag = PGL_G3D_TEX_FILTER_NEAREST;
            int min = PGL_G3D_TEX_FILTER_NEAREST;
            int mip = PGL_G3D_TEX_FILTER_NEAREST;
            if (ft) {
                if (ft->mag_filter == GL_LINEAR)
                    mag = PGL_G3D_TEX_FILTER_LINEAR;
                if (ft->min_filter == GL_LINEAR ||
                    ft->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                    ft->min_filter == GL_LINEAR_MIPMAP_LINEAR)
                    min = PGL_G3D_TEX_FILTER_LINEAR;
                if (ft->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
                    ft->min_filter == GL_LINEAR_MIPMAP_LINEAR)
                    mip = PGL_G3D_TEX_FILTER_LINEAR;
            }
            pgl_g3d_bind_tex_unit_sampler(u, slot, mag, min, mip);
            if (slot < 0) {
                static int _gf_unb = 0;
                glTexture* t = (e->tex_units[u] && c && (int)e->tex_units[u] < c->textures.size)
                               ? &c->textures.a[e->tex_units[u]] : NULL;
                if (_gf_unb < 8) { _gf_unb++;
                    printf("PGLGPUFRAME: prog %d unit %d tex %u -> slot -1 (t=%p w=%d h=%d type=0x%x fmt=0x%x filt=0x%x/0x%x wrap=0x%x/0x%x data=%p enabled=%d)\n",
                           e->prog, u, (unsigned)e->tex_units[u], (void*)t,
                           t ? t->w : 0, t ? t->h : 0, t ? (unsigned)t->type : 0u,
                           t ? (unsigned)t->format : 0u,
                           t ? (unsigned)t->mag_filter : 0u, t ? (unsigned)t->min_filter : 0u,
                           t ? (unsigned)t->wrap_s : 0u, t ? (unsigned)t->wrap_t : 0u,
                           t ? (void*)t->data : NULL, pgl_g3d_enabled()); fflush(stdout); }
            }
        }
    }

    /* First triangle: a declined upload (or a GPU latched off mid-call) returns
     * 0 with nothing dispatched, so the caller cleanly falls back to the CPU
     * loop for THIS frame too. */
    if (!pgl_g3d_batch_tri_compiled(qa, qb, qc,
                                    PGL_G3D_DEPTH_NONE, 0u, PGL_G3D_CULL_NONE, NULL)) {
        static int _gf_decl = 0;
        if (_gf_decl < 4) { _gf_decl++;
            printf("PGLGPUFRAME: prog %d batch declined -> CPU frame\n", e->prog); fflush(stdout); }
        return 0;
    }
    pgl_g3d_batch_tri_compiled(qa, qc, qd,
                               PGL_G3D_DEPTH_NONE, 0u, PGL_G3D_CULL_NONE, NULL);
    /* A flush whose dispatch fails latches the GPU off, or - for a back-end
     * pre-submission NOT_SUPPORTED - latches just this program as declined;
     * report either so the caller redraws the frame on the CPU (the full-screen
     * loop overwrites the partial GPU result).  Subsequent frames of a declined
     * program then take the CPU path straight from the batch call above. */
    pgl_g3d_flush();
    if (pgl_g3d_program_declined(e->prog))
        return 0;
    {
        static int _gf_ok = 0;
        if (_gf_ok < 2) { _gf_ok++;
            printf("PGLGPUFRAME: prog %d dispatched enabled=%d\n", e->prog, pgl_g3d_enabled()); fflush(stdout); }
    }
    return pgl_g3d_enabled();
}
#endif /* PGL_G3D */

#if !(PGL_NEON_ENABLED && PGL_BITDEPTH == 32)
static Color blend_pixel(vec4 src, vec4 dst);
#endif
static int fragment_processing(int x, int y, float z);
static void draw_pixel(vec4 cf, int x, int y, float z, int do_frag_processing);
static void run_pipeline(GLenum mode, const GLvoid* indices, GLsizei count, GLsizei instance, GLuint base_instance, GLboolean use_elements);

static float calc_poly_offset(vec3 hp0, vec3 hp1, vec3 hp2);

static void draw_triangle_clip(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke, int clip_bit);
static void draw_triangle_point(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke);
static void draw_triangle_line(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke);
static void draw_triangle_fill(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke);
static void draw_triangle_final(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke);
static void draw_triangle(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke);

static void draw_line_clip(glVertex* v1, glVertex* v2);

// This is the prototype for either implementation; only one is defined based on
// whether PGL_BETTER_THICK_LINES is defined
static void draw_thick_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset);

// Only width 1 supported for now
static void draw_aa_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset);

/* this clip epsilon is needed to avoid some rounding errors after
   several clipping stages */

#define CLIP_EPSILON (1E-5)
#define CLIPZ_MASK 0x3
#define CLIPX_TEST(x) (x >= c->lx && x < c->ux)
#define CLIPY_TEST(y) (y >= c->ly && y < c->uy)
#define CLIPXY_TEST(x, y) (x >= c->lx && x < c->ux && y >= c->ly && y < c->uy)


static inline int gl_clipcode(vec4 pt)
{
    float w;

    w = pt.w * (1.0 + CLIP_EPSILON);
    return
        (((pt.z < -w) |
         ((pt.z >  w) << 1)) &
         ((!c->depth_clamp) |
          (!c->depth_clamp) << 1)) |

        ((pt.x < -w) << 2) |
        ((pt.x >  w) << 3) |
        ((pt.y < -w) << 4) |
        ((pt.y >  w) << 5);

}




static int is_front_facing(glVertex* v0, glVertex* v1, glVertex* v2)
{
    //according to docs culling is done based on window coordinates
    //See page 3.6.1 page 116 of glspec33.core for more on rasterization, culling etc.
    //
    //TODO See if there's a way to determine front facing before
    // clipping the near plane (vertex behind the eye seems to mess
    // up winding).  If yes, can refactor to cull early and handle
    // line and point modes separately
    vec3 p0 = v4_to_v3h(v0->screen_space);
    vec3 p1 = v4_to_v3h(v1->screen_space);
    vec3 p2 = v4_to_v3h(v2->screen_space);

    float a;

    //method from spec
    a = p0.x*p1.y - p1.x*p0.y + p1.x*p2.y - p2.x*p1.y + p2.x*p0.y - p0.x*p2.y;
    //a /= 2;

    if (c->front_face == GL_CW) {
        a = -a;
    }

    if (a <= 0) {
        return 0;
    }

    return 1;
}

// TODO make a config macro that turns this into an inline function/macro that
// only supports float for a small perf boost
static vec4 get_v_attrib(glVertex_Attrib* v, GLsizei i)
{
    // v->buf will be 0 for a client array and buf[0].data
    // is always NULL so this works for both but we have to cast
    // the pointer to GLsizeiptr because adding an offset to a NULL pointer
    // is undefined.  So, do the math as numbers and convert back to a pointer
    GLsizeiptr buf_data = (GLsizeiptr)c->buffers.a[v->buf].data;
    u8* u8p = (u8*)(buf_data + v->offset + v->stride*i);

    i8* i8p = (i8*)u8p;
    u16* u16p = (u16*)u8p;
    i16* i16p = (i16*)u8p;
    u32* u32p = (u32*)u8p;
    i32* i32p = (i32*)u8p;

    vec4 tmpvec4 = { 0.0f, 0.0f, 0.0f, 1.0f };
    float* tv = (float*)&tmpvec4;
    GLenum type = v->type;

    if (type < GL_FLOAT) {
        for (int i=0; i<v->size; i++) {
            if (v->normalized) {
                switch (type) {
                case GL_BYTE:           tv[i] = rsw_mapf(i8p[i], INT8_MIN, INT8_MAX, -1.0f, 1.0f); break;
                case GL_UNSIGNED_BYTE:  tv[i] = rsw_mapf(u8p[i], 0, UINT8_MAX, 0.0f, 1.0f); break;
                case GL_SHORT:          tv[i] = rsw_mapf(i16p[i], INT16_MIN,INT16_MAX, 0.0f, 1.0f); break;
                case GL_UNSIGNED_SHORT: tv[i] = rsw_mapf(u16p[i], 0, UINT16_MAX, 0.0f, 1.0f); break;
                case GL_INT:            tv[i] = rsw_mapf(i32p[i], INT32_MIN, INT32_MAX, 0.0f, 1.0f); break;
                case GL_UNSIGNED_INT:   tv[i] = rsw_mapf(u32p[i], 0, UINT32_MAX, 0.0f, 1.0f); break;
                }
            } else {
                switch (type) {
                case GL_BYTE:           tv[i] = i8p[i]; break;
                case GL_UNSIGNED_BYTE:  tv[i] = u8p[i]; break;
                case GL_SHORT:          tv[i] = i16p[i]; break;
                case GL_UNSIGNED_SHORT: tv[i] = u16p[i]; break;
                case GL_INT:            tv[i] = i32p[i]; break;
                case GL_UNSIGNED_INT:   tv[i] = u32p[i]; break;
                }
            }
        }
    } else {
        // TODO support GL_DOUBLE

        memcpy(tv, u8p, sizeof(float)*v->size);
    }

    //c->cur_vertex_array->vertex_attribs[enabled[j]].buf->data;
    return tmpvec4;
}

#include "pgl_vertex_pool.inc"

// TODO Possibly split for optimization, prep all verts first then do all shader calls at once
// Will need num_verts * vertex_attribs_vs[] space rather than a single attribute staging area...
static void do_vertex(glVertex_Attrib* v, int* enabled, int num_enabled, int i, int vert)
{
    // copy/prep vertex attributes from buffers into appropriate positions for vertex shader to access
    for (int j=0; j<num_enabled; ++j) {
        c->vertex_attribs_vs[enabled[j]] = get_v_attrib(&v[enabled[j]], i);
    }

    float* vs_out = &c->vs_output.output_buf[vert*c->vs_output.size];
    c->programs.a[c->cur_program].vertex_shader(vs_out, c->vertex_attribs_vs, &c->builtins, c->programs.a[c->cur_program].uniform);

    c->glverts.a[vert].vs_out = vs_out;
    c->glverts.a[vert].clip_space = c->builtins.gl_Position;
    c->glverts.a[vert].screen_space_valid = GL_FALSE;
	 c->glverts.a[vert].rw_projected_valid = GL_FALSE;

    // no use setting here because of TRIANGLE_STRIP
    // and TRIANGLE_FAN. While I don't properly
    // generate "primitives", I do expand create unique vertices
    // to process when the user uses an element (index) buffer.
    //
    // so it's done in draw_triangle()
    //c->glverts.a[vert].edge_flag = 1;

    c->glverts.a[vert].clip_code = gl_clipcode(c->builtins.gl_Position);
}

// TODO naming issue/refactor?
// When used with Draw*Arrays* indices is really the index of the first vertex to be used
// When used for Draw*Elements* indices is either a byte offset of the first index or
// an actual pointer to the array of indices depending on whether an ELEMENT_ARRAY_BUFFER is bound
//
// use_elems_type is either 0/false or one of GL_UNSIGNED_BYTE/SHORT/INT
// so used as a boolean and an enum
static void vertex_stage(const GLvoid* indices, GLsizei count, GLsizei instance_id, GLuint base_instance, GLenum use_elems_type)
{
    int i, j, vert, num_enabled;

    glVertex_Attrib* v = c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs;
    GLuint elem_buffer = c->vertex_arrays.a[c->cur_vertex_array].element_buffer;

    //save checking if enabled on every loop if we build this first
    //also initialize the vertex_attrib space
    // TODO does creating enabled array actually help perf?  At what number
    // of GL_MAX_VERTEX_ATTRIBS and vertices does it become a benefit?
    int enabled[GL_MAX_VERTEX_ATTRIBS] = { 0 };
    for (i=0, j=0; i<GL_MAX_VERTEX_ATTRIBS; ++i) {
        c->vertex_attribs_vs[i] = v[i].current_value;
        if (v[i].enabled) {
            if (v[i].divisor == 0) {
                enabled[j++] = i;
            } else if (!(instance_id % v[i].divisor)) {
                //set instanced attributes if necessary
                int n = instance_id/v[i].divisor + base_instance;
                c->vertex_attribs_vs[i] = get_v_attrib(&v[i], n);
            }
        }
    }
    num_enabled = j;

    cvec_reserve_glVertex(&c->glverts, count);

    // gl_InstanceID always starts at 0, base_instance is only added when grabbing attributes
    // https://www.khronos.org/opengl/wiki/Built-in_Variable_(GLSL)#Vertex_shader_inputs
    c->builtins.gl_InstanceID = instance_id;
    c->builtins.gl_BaseInstance = base_instance;
    GLsizeiptr first = (GLsizeiptr)indices;

    if (pgl_parallel_vertices(v, enabled, num_enabled, indices, count,
                              use_elems_type))
        return;

    if (!use_elems_type) {
        for (vert=0, i=first; i<first+count; ++i, ++vert) {
            do_vertex(v, enabled, num_enabled, i, vert);
        }
    } else {
        GLuint* uint_array = (GLuint*)indices;
        GLushort* ushort_array = (GLushort*)indices;
        GLubyte* ubyte_array = (GLubyte*)indices;
        if (c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER]) {
            uint_array = (GLuint*)(c->buffers.a[elem_buffer].data + first);
            ushort_array = (GLushort*)(c->buffers.a[elem_buffer].data + first);
            ubyte_array = (GLubyte*)(c->buffers.a[elem_buffer].data + first);
        }
        if (use_elems_type == GL_UNSIGNED_BYTE) {
            for (i=0; i<count; ++i) {
                do_vertex(v, enabled, num_enabled, ubyte_array[i], i);
            }
        } else if (use_elems_type == GL_UNSIGNED_SHORT) {
            for (i=0; i<count; ++i) {
                do_vertex(v, enabled, num_enabled, ushort_array[i], i);
            }
        } else {
            for (i=0; i<count; ++i) {
                do_vertex(v, enabled, num_enabled, uint_array[i], i);
            }
        }
    }
}


//TODO make fs_input static?  or a member of glContext?
static void draw_point(glVertex* vert, float poly_offset)
{
    float fs_input[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

    vec3 point = v4_to_v3h(vert->screen_space);
    point.z += poly_offset; // couldn't this put it outside of [-1,1]?
    point.z = rsw_mapf(point.z, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

    // TODO necessary for non-perspective?
    //if (c->depth_clamp)
    //	clamp(point.z, c->depth_range_near, c->depth_range_far);

    Shader_Builtins builtins;
    builtins.fs_input_dx = NULL;
    builtins.fs_input_dy = NULL;
    // 3.3 spec pg 110 says r,q are supposed to be replaced with 0 and 1...
    // but PointCoord is a vec2 and that is not in the 4.6 spec so it must be a typo

    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    //TODO why not just pass vs_output directly?  hmmm...
    PGL_MEMCPY(fs_input, vert->vs_out, c->vs_output.size*sizeof(float));

    //accounting for pixel centers at 0.5, using truncation
    float x = point.x + 0.5f;
    float y = point.y + 0.5f;
    float p_size = c->point_size;
    float origin = (c->point_spr_origin == GL_UPPER_LEFT) ? -1.0f : 1.0f;
    SET_V2(builtins.gl_PointCoord_dx, 1.0f/p_size, 0.0f);
    SET_V2(builtins.gl_PointCoord_dy, 0.0f, origin/p_size);
    // NOTE/TODO, According to the spec if the clip coordinate, ie the
    // center of the point is outside the clip volume, you're supposed to
    // clip the whole thing, but some vendors don't do that because it's
    // not what most people want.

    // Can easily clip whole point when point size <= 1
    if (p_size <= 1.0f) {
        if (x < c->lx || y < c->ly || x >= c->ux || y >= c->uy)
            return;
    }

    for (float i = y-p_size/2; i<y+p_size/2; ++i) {
        if (i < c->ly || i >= c->uy)
            continue;

        for (float j = x-p_size/2; j<x+p_size/2; ++j) {

            if (j < c->lx || j >= c->ux)
                continue;

            if (!fragdepth_or_discard && !fragment_processing(j, i, point.z)) {
                continue;
            }

            // per page 110 of 3.3 spec (x,y are s,t)
            builtins.gl_PointCoord.x = 0.5f + ((int)j + 0.5f - point.x)/p_size;
            builtins.gl_PointCoord.y = 0.5f + origin * ((int)i + 0.5f - point.y)/p_size;

            SET_V4(builtins.gl_FragCoord, j, i, point.z, 1/vert->screen_space.w);
            builtins.discard = GL_FALSE;
            builtins.gl_FragDepth = point.z;
            c->programs.a[c->cur_program].fragment_shader(fs_input, &builtins, c->programs.a[c->cur_program].uniform);
            if (!builtins.discard)
                draw_pixel(builtins.gl_FragColor, j, i, builtins.gl_FragDepth, fragdepth_or_discard);
        }
    }
}

static void run_pipeline(GLenum mode, const GLvoid* indices, GLsizei count, GLsizei instance, GLuint base_instance, GLboolean use_elements)
{
    GLsizei i;
    int provoke;

    PGL_ASSERT(count <= get_max_vertices());

    vertex_stage(indices, count, instance, base_instance, use_elements);

    //fragment portion
    if (mode == GL_POINTS) {
        for (i=0; i<count; ++i) {
            // clip only z and let partial points (size > 1)
            // show even if the center would have been clipped
            if (c->glverts.a[i].clip_code & CLIPZ_MASK)
                continue;

            if (!c->glverts.a[i].screen_space_valid) {
                c->glverts.a[i].screen_space =
                    mult_m4_v4(c->vp_mat, c->glverts.a[i].clip_space);
                c->glverts.a[i].screen_space_valid = GL_TRUE;
            }

#ifdef PGL_G3D
            /* GPU fast path: offload the point when eligible, otherwise flush
             * any pending GPU batch so this CPU point lands in painter's order. */
            if (pgl_gpu_point(&c->glverts.a[i]))
                continue;
            pgl_gpu_fence();
#endif
            draw_point(&c->glverts.a[i], 0.0f);
        }
    } else if (mode == GL_LINES) {
        for (i=0; i<count-1; i+=2) {
            draw_line_clip(&c->glverts.a[i], &c->glverts.a[i+1]);
        }
    } else if (mode == GL_LINE_STRIP) {
        for (i=0; i<count-1; i++) {
            draw_line_clip(&c->glverts.a[i], &c->glverts.a[i+1]);
        }
    } else if (mode == GL_LINE_LOOP) {
        for (i=0; i<count-1; i++) {
            draw_line_clip(&c->glverts.a[i], &c->glverts.a[i+1]);
        }
        //draw ending line from last to first point
        draw_line_clip(&c->glverts.a[count-1], &c->glverts.a[0]);

    } else if (mode == GL_TRIANGLES) {
        provoke = (c->provoking_vert == GL_LAST_VERTEX_CONVENTION) ? 2 : 0;

        for (i=0; i<count-2; i+=3) {
            draw_triangle(&c->glverts.a[i], &c->glverts.a[i+1], &c->glverts.a[i+2], i+provoke);
        }

    } else if (mode == GL_TRIANGLE_STRIP) {
        unsigned int a=0, b=1, toggle = 0;
        provoke = (c->provoking_vert == GL_LAST_VERTEX_CONVENTION) ? 0 : -2;

        for (i=2; i<count; ++i) {
            draw_triangle(&c->glverts.a[a], &c->glverts.a[b], &c->glverts.a[i], i+provoke);

            if (!toggle)
                a = i;
            else
                b = i;

            toggle = !toggle;
        }
    } else if (mode == GL_TRIANGLE_FAN) {
        provoke = (c->provoking_vert == GL_LAST_VERTEX_CONVENTION) ? 0 : -1;

        for (i=2; i<count; ++i) {
            draw_triangle(&c->glverts.a[0], &c->glverts.a[i-1], &c->glverts.a[i], i+provoke);
        }
    }
#ifdef PGL_G3D
    /* submit this draw call's batched triangles before returning */
    if (_pgl_gpu_active)
        pgl_g3d_flush();
#endif
}


static int depthtest(u32 zval, u32 zbufval)
{
    switch (c->depth_func) {
    case GL_LESS:
        return zval < zbufval;
    case GL_LEQUAL:
        return zval <= zbufval;
    case GL_GREATER:
        return zval > zbufval;
    case GL_GEQUAL:
        return zval >= zbufval;
    case GL_EQUAL:
        return zval == zbufval;
    case GL_NOTEQUAL:
        return zval != zbufval;
    case GL_ALWAYS:
        return 1;
    case GL_NEVER:
        return 0;
    }
    return 0; //get rid of compile warning
}


static void setup_fs_input(float t, float dt_dx, float dt_dy, float* v1_out, float* v2_out, float wa, float wb, unsigned int provoke)
{
    float* vs_output = &c->vs_output.output_buf[0];

    float inv_wa = 1.0/wa;
    float inv_wb = 1.0/wb;
    float dq = inv_wb - inv_wa;
    float q = inv_wa + t*dq;

    for (int i=0; i<c->vs_output.size; ++i) {
        if (c->vs_output.interpolation[i] == PGL_SMOOTH) {
            float dn = v2_out[i]*inv_wb - v1_out[i]*inv_wa;
            c->fs_input[i] = (v1_out[i]*inv_wa + t*dn) / q;
            c->fs_input_dx[i] = (dn - c->fs_input[i]*dq)*dt_dx/q;
            c->fs_input_dy[i] = (dn - c->fs_input[i]*dq)*dt_dy/q;

        } else if (c->vs_output.interpolation[i] == PGL_NOPERSPECTIVE) {
            float dv = v2_out[i] - v1_out[i];
            c->fs_input[i] = v1_out[i] + t*dv;
            c->fs_input_dx[i] = dv*dt_dx;
            c->fs_input_dy[i] = dv*dt_dy;
        } else {
            c->fs_input[i] = vs_output[provoke*c->vs_output.size + i];
            c->fs_input_dx[i] = 0.0f;
            c->fs_input_dy[i] = 0.0f;
        }
    }

    c->builtins.fs_input_dx = c->fs_input_dx;
    c->builtins.fs_input_dy = c->fs_input_dy;
    c->builtins.discard = GL_FALSE;
}

/* Line Clipping algorithm from 'Computer Graphics', Principles and
   Practice */
static inline int clip_line(float denom, float num, float* tmin, float* tmax)
{
    float t;

    if (denom > 0) {
        t = num / denom;
        if (t > *tmax) return 0;
        if (t > *tmin) {
            *tmin = t;
            //printf("t > *tmin %f\n", t);
        }
    } else if (denom < 0) {
        t = num / denom;
        if (t < *tmin) return 0;
        if (t < *tmax) {
            *tmax = t;
            //printf("t < *tmax %f\n", t);
        }
    } else if (num > 0) return 0;
    return 1;
}


static void interpolate_clipped_line(glVertex* v1, glVertex* v2, float* v1_out, float* v2_out, float tmin, float tmax)
{
    for (int i=0; i<c->vs_output.size; ++i) {
        v1_out[i] = v1->vs_out[i] + (v2->vs_out[i] - v1->vs_out[i])*tmin;
        v2_out[i] = v1->vs_out[i] + (v2->vs_out[i] - v1->vs_out[i])*tmax;

        //v2_out[i] = (1 - tmax)*v1->vs_out[i] + tmax*v2->vs_out[i];
    }
}



static void draw_line_clip(glVertex* v1, glVertex* v2)
{
    int cc1, cc2;
    vec4 d, p1, p2, t1, t2;
    float tmin, tmax;

    cc1 = v1->clip_code;
    cc2 = v2->clip_code;

    p1 = v1->clip_space;
    p2 = v2->clip_space;
    
    float v1_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float v2_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

    vec3 hp1, hp2;

    //TODO ponder this
    unsigned int provoke;
    if (c->provoking_vert == GL_LAST_VERTEX_CONVENTION)
        provoke = (v2 - c->glverts.a)/sizeof(glVertex);
    else
        provoke = (v1 - c->glverts.a)/sizeof(glVertex);

    if (cc1 & cc2) {
        return;
    } else if ((cc1 | cc2) == 0) {
        t1 = v1->screen_space_valid ? v1->screen_space :
             mult_m4_v4(c->vp_mat, p1);
        t2 = v2->screen_space_valid ? v2->screen_space :
             mult_m4_v4(c->vp_mat, p2);

        hp1 = v4_to_v3h(t1);
        hp2 = v4_to_v3h(t2);

#ifdef PGL_G3D
        /* GPU fast path: the segment is fully unclipped, so the raw vertex clip
         * positions ARE the line.  Offload it when eligible, otherwise flush any
         * pending GPU batch so this CPU line lands in painter's order. */
        if (pgl_gpu_line(v1, v2))
            return;
        pgl_gpu_fence();
#endif
        if (c->line_smooth) {
            draw_aa_line(hp1, hp2, t1.w, t2.w, v1->vs_out, v2->vs_out, provoke, 0.0f);
        } else {
            draw_thick_line(hp1, hp2, t1.w, t2.w, v1->vs_out, v2->vs_out, provoke, 0.0f);
        }
    } else {

        d = sub_v4s(p2, p1);

        tmin = 0;
        tmax = 1;
        if (clip_line( d.x+d.w, -p1.x-p1.w, &tmin, &tmax) &&
            clip_line(-d.x+d.w,  p1.x-p1.w, &tmin, &tmax) &&
            clip_line( d.y+d.w, -p1.y-p1.w, &tmin, &tmax) &&
            clip_line(-d.y+d.w,  p1.y-p1.w, &tmin, &tmax) &&
            clip_line( d.z+d.w, -p1.z-p1.w, &tmin, &tmax) &&
            clip_line(-d.z+d.w,  p1.z-p1.w, &tmin, &tmax)) {

            //printf("%f %f\n", tmin, tmax);

            t1 = add_v4s(p1, scale_v4(d, tmin));
            t2 = add_v4s(p1, scale_v4(d, tmax));

            t1 = mult_m4_v4(c->vp_mat, t1);
            t2 = mult_m4_v4(c->vp_mat, t2);
            //print_v4(t1, "\n");
            //print_v4(t2, "\n");

            interpolate_clipped_line(v1, v2, v1_out, v2_out, tmin, tmax);

            hp1 = v4_to_v3h(t1);
            hp2 = v4_to_v3h(t2);

#ifdef PGL_G3D
            /* same contract as the unclipped branch above: a clipped segment
             * can never be offloaded (its endpoints are not the raw clip
             * positions), so flush pending GPU work - and consume a pending
             * frame clear - before this CPU segment lands in the buffer. */
            pgl_gpu_fence();
#endif
            if (c->line_smooth) {
                draw_aa_line(hp1, hp2, t1.w, t2.w, v1_out, v2_out, provoke, 0.0f);
            } else {
                draw_thick_line(hp1, hp2, t1.w, t2.w, v1_out, v2_out, provoke, 0.0f);
            }
        }
    }
}

#ifndef PGL_BETTER_THICK_LINES
static void draw_thick_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset)
{
    float tmp;
    float* tmp_ptr;

    float x1 = hp1.x, x2 = hp2.x, y1 = hp1.y, y2 = hp2.y;
    float z1 = hp1.z, z2 = hp2.z;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;

        tmp = z1;
        z1 = z2;
        z2 = tmp;

        tmp = w1;
        w1 = w2;
        w2 = tmp;

        tmp_ptr = v1_out;
        v1_out = v2_out;
        v2_out = tmp_ptr;
    }

    //calculate slope and implicit line parameters once
    //could just use my Line type/constructor as in draw_triangle
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);

    float t, x, y, z, w, dt_dx, dt_dy;

    vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
    vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
    float line_length_squared = len_v2(sub_p2p1);
    line_length_squared *= line_length_squared;
    dt_dx = line_length_squared > 0.0f ? sub_p2p1.x/line_length_squared : 0.0f;
    dt_dy = line_length_squared > 0.0f ? sub_p2p1.y/line_length_squared : 0.0f;

    frag_func fragment_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;
    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    float i_x1, i_y1, i_x2, i_y2;
    i_x1 = floor(p1.x) + 0.5;
    i_y1 = floor(p1.y) + 0.5;
    i_x2 = floor(p2.x) + 0.5;
    i_y2 = floor(p2.y) + 0.5;

    float x_min, x_max, y_min, y_max;
    x_min = i_x1;
    x_max = i_x2; //always left to right;
    if (m <= 0) {
        y_min = i_y2;
        y_max = i_y1;
    } else {
        y_min = i_y1;
        y_max = i_y2;
    }

    // TODO should be done for each fragment, after poly_offset is added?
    z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
    z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

    float width = roundf(c->line_width);
    if (!width) {
        width = 1.0f;
    }
    //int wi = width;
    float half_w = width * 0.5f;

    // TODO solve off by one issues:
    //   See test outputs where there seems to occasionally be an extra pixel
    //   Also might be drawing lines one pixel lower on the minor axis
    //
    //   Also, I shouldn't have to clamp t, technically if it's outside [0,1]
    //   it's not part of the line so it should be skipped or blended if the
    //   pixel is partially covered and you're doing AA. Or mabye I do have to
    //   clamp but be more particular about starting and ending pixel which..
    //
    // TODO I need to do anyway, since GL specifically says two lines which
    // share an endpoint should *not* evaluate that pixel twice and which
    // gets it should be deterministic
    //
    // TODO maybe try simplifying into only 2 cases steep or not steep like
    // AA algorithm

    //4 cases based on slope
    if (m <= -1) {     //(-infinite, -1]
        //printf("slope <= -1\n");
        for (x = x_min, y = y_max; y>=y_min && x<=x_max; --y) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=x-half_w; j<x+half_w; ++j) {
                if (CLIPXY_TEST(j, y)) {
                    if (fragdepth_or_discard || fragment_processing(j, y, z)) {
                        SET_V4(c->builtins.gl_FragCoord, j, y, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, j, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }

            if (line_func(&line, x+0.5f, y-1) < 0) //A*(x+0.5f) + B*(y-1) + C < 0)
                ++x;
        }
    } else if (m <= 0) {     //(-1, 0]
        //printf("slope = (-1, 0]\n");
        for (x = x_min, y = y_max; x<=x_max && y>=y_min; ++x) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=y-half_w; j<y+half_w; ++j) {
                if (CLIPXY_TEST(x, j)) {
                    if (fragdepth_or_discard || fragment_processing(x, j, z)) {

                        SET_V4(c->builtins.gl_FragCoord, x, j, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, x, j, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (line_func(&line, x+1, y-0.5f) > 0) //A*(x+1) + B*(y-0.5f) + C > 0)
                --y;
        }
    } else if (m <= 1) {     //(0, 1]
        //printf("slope = (0, 1]\n");
        for (x = x_min, y = y_min; x <= x_max && y <= y_max; ++x) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=y-half_w; j<y+half_w; ++j) {
                if (CLIPXY_TEST(x, j)) {
                    if (fragdepth_or_discard || fragment_processing(x, j, z)) {

                        SET_V4(c->builtins.gl_FragCoord, x, j, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, x, j, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (line_func(&line, x+1, y+0.5f) < 0) //A*(x+1) + B*(y+0.5f) + C < 0)
                ++y;
        }

    } else {    //(1, +infinite)
        //printf("slope > 1\n");
        for (x = x_min, y = y_min; y<=y_max && x <= x_max; ++y) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=x-half_w; j<x+half_w; ++j) {
                if (CLIPXY_TEST(j, y)) {
                    if (fragdepth_or_discard || fragment_processing(j, y, z)) {

                        SET_V4(c->builtins.gl_FragCoord, j, y, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, j, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (line_func(&line, x+0.5f, y+1) > 0) //A*(x+0.5f) + B*(y+1) + C > 0)
                ++x;
        }
    }
}
#else
static void draw_thick_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset)
{
    float tmp;
    float* tmp_ptr;

    float x1 = hp1.x, x2 = hp2.x, y1 = hp1.y, y2 = hp2.y;
    float z1 = hp1.z, z2 = hp2.z;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;

        tmp = z1;
        z1 = z2;
        z2 = tmp;

        tmp = w1;
        w1 = w2;
        w2 = tmp;

        tmp_ptr = v1_out;
        v1_out = v2_out;
        v2_out = tmp_ptr;
    }

    // Need half for the rest
    float width = c->line_width * 0.5f;

    //calculate slope and implicit line parameters once
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);
    normalize_line(&line);

    vec2 p1 = { x1, y1 };
    vec2 p2 = { x2, y2 };
    vec2 v12 = sub_v2s(p2, p1);
    vec2 v1r, pr; // v2r

    float dot_1212 = dot_v2s(v12, v12);
    float dt_dx = dot_1212 > 0.0f ? v12.x/dot_1212 : 0.0f;
    float dt_dy = dot_1212 > 0.0f ? v12.y/dot_1212 : 0.0f;

    float x_min, x_max, y_min, y_max;

    x_min = p1.x - width;
    x_max = p2.x + width;
    if (m <= 0) {
        y_min = p2.y - width;
        y_max = p1.y + width;
    } else {
        y_min = p1.y - width;
        y_max = p2.y + width;
    }

    // clipping/scissoring against side planes here
    x_min = MAX(c->lx, x_min);
    x_max = MIN(c->ux, x_max);
    y_min = MAX(c->ly, y_min);
    y_max = MIN(c->uy, y_max);
    // end clipping
    
    y_min = floor(y_min) + 0.5f;
    x_min = floor(x_min) + 0.5f;
    float x_mino = x_min;
    float x_maxo = x_max;


    frag_func fragment_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;
    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    float t, x, y, z, w, e, dist;
    //float width_squared = width*width;

    // calculate x_max or just use last logic?
    //int last = 0;

    //printf("%f %f %f %f   =\n", i_x1, i_y1, i_x2, i_y2);
    //printf("%f %f %f %f   x_min etc\n", x_min, x_max, y_min, y_max);

    // TODO should be done for each fragment, after poly_offset is added?
    z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
    z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

    for (y = y_min; y < y_max; ++y) {
        pr.y = y;
        //last = GL_FALSE;

        // could also check fabsf(line.A) > epsilon
        if (fabsf(m) > 0.0001f) {
            x_min = (-width - line.C - line.B*y)/line.A;
            x_max = (width - line.C - line.B*y)/line.A;
            if (x_min > x_max) {
                tmp = x_min;
                x_min = x_max;
                x_max = tmp;
            }
            x_min = MAX(c->lx, x_min);
            x_min = floorf(x_min) + 0.5f;
            x_max = MIN(c->ux, x_max);
            //printf("%f %f   x_min etc\n", x_min, x_max);
        } else {
            x_min = x_mino;
            x_max = x_maxo;
        }
        for (x = x_min; x < x_max; ++x) {
            pr.x = x;
            v1r = sub_v2s(pr, p1);
            //v2r = sub_v2s(pr, p2);
            e = dot_v2s(v1r, v12);

            // c lies past the ends of the segment v12
            if (e <= 0.0f || e >= dot_1212) {
                continue;
            }

            // can do this because we normalized the line equation
            // TODO square or fabsf?
            dist = line_func(&line, pr.x, pr.y);
            //if (dist*dist < width_squared) {
            if (fabsf(dist) < width) {
                t = e / dot_1212;

                z = (1 - t) * z1 + t * z2;
                z += poly_offset;
                if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                    w = (1 - t) * w1 + t * w2;

                    SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);

                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard)
                        draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            //	last = GL_TRUE;
            //} else if (last) {
            //	break; // we have passed the right edge of the line on this row
            }
        }
    }
}
#endif



// As an adaptation of Xialin Wu's AA line algorithm, unlike all other GL
// rasterization functions, this uses integer pixel centers and passes
// those in glFragCoord.

#define ipart_(X) ((int)(X))
#define round_(X) ((int)(((float)(X))+0.5f))
#define fpart_(X) (((float)(X))-(float)ipart_(X))
#define rfpart_(X) (1.0f-fpart_(X))

#define swap_(a, b) do{ __typeof__(a) tmp;  tmp = a; a = b; b = tmp; } while(0)
static void draw_aa_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset)
{
    float t, z, w, dt_dx, dt_dy;
    int x, y;

    frag_func fragment_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;
    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    float x1 = hp1.x, x2 = hp2.x, y1 = hp1.y, y2 = hp2.y;
    float z1 = hp1.z, z2 = hp2.z;

    float dx = x2 - x1;
    float dy = y2 - y1;

    if (fabsf(dx) > fabsf(dy)) {
        if (x2 < x1) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(z1, z2);
            swap_(w1, w2);
            swap_(v1_out, v2_out);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;
        dt_dx = line_length_squared > 0.0f ? sub_p2p1.x/line_length_squared : 0.0f;
        dt_dy = line_length_squared > 0.0f ? sub_p2p1.y/line_length_squared : 0.0f;

        // TODO should be done for each fragment, after poly_offset is added?
        z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
        z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

        float gradient = dy / dx;
        float xend = round_(x1);
        float yend = y1 + gradient*(xend - x1);
        float xgap = rfpart_(x1 + 0.5);
        int xpxl1 = xend;
        int ypxl1 = ipart_(yend);

        t = 0.0f;
        z = z1 + poly_offset;
        w = w1;

        // TODO This is so ugly and repetitive...Should I bother with end points?
        // Or run the shader only once for each pair?
        x = xpxl1;
        y = ypxl1;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x, y+1)) {
            if (fragdepth_or_discard || fragment_processing(x, y+1, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y+1, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y+1, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        //printf("xgap = %f\n", xgap);
        //printf("%f %f\n", rfpart_(yend), fpart_(yend));
        //printf("%f %f\n", rfpart_(yend)*xgap, fpart_(yend)*xgap);
        float intery = yend + gradient;

        xend = round_(x2);
        yend = y2 + gradient*(xend - x2);
        xgap = fpart_(x2+0.5);
        int xpxl2 = xend;
        int ypxl2 = ipart_(yend);

        t = 1.0f;
        z = z2 + poly_offset;
        w = w2;

        x = xpxl2;
        y = ypxl2;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x, y+1)) {
            if (fragdepth_or_discard || fragment_processing(x, y+1, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y+1, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y+1, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }

        for(x=xpxl1+1; x < xpxl2; x++) {
            pr.x = x;
            pr.y = intery;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            y = ipart_(intery);
            if (CLIPXY_TEST(x, y)) {
                if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= rfpart_(intery);
                        draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (CLIPXY_TEST(x, y+1)) {
                if (fragdepth_or_discard || fragment_processing(x, y+1, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x, y+1, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= fpart_(intery);
                        draw_pixel(c->builtins.gl_FragColor, x, y+1, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }

            intery += gradient;
        }
    } else {
        if (y2 < y1) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(z1, z2);
            swap_(w1, w2);
            swap_(v1_out, v2_out);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;
        dt_dx = line_length_squared > 0.0f ? sub_p2p1.x/line_length_squared : 0.0f;
        dt_dy = line_length_squared > 0.0f ? sub_p2p1.y/line_length_squared : 0.0f;

        // TODO should be done for each fragment, after poly_offset is added?
        z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
        z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

        float gradient = dx / dy;
        float yend = round_(y1);
        float xend = x1 + gradient*(yend - y1);
        float ygap = rfpart_(y1 + 0.5);
        int ypxl1 = yend;
        int xpxl1 = ipart_(xend);

        t = 0.0f;
        z = z1 + poly_offset;
        w = w1;

        x = xpxl1;
        y = ypxl1;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x+1, y)) {
            if (fragdepth_or_discard || fragment_processing(x+1, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x+1, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x+1, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }

        float interx = xend + gradient;

        yend = round_(y2);
        xend = x2 + gradient*(yend - y2);
        ygap = fpart_(y2+0.5);
        int ypxl2 = yend;
        int xpxl2 = ipart_(xend);

        t = 1.0f;
        z = z2 + poly_offset;
        w = w2;

        x = xpxl2;
        y = ypxl2;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x+1, y)) {
            if (fragdepth_or_discard || fragment_processing(x+1, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x+1, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x+1, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }

        for(y=ypxl1+1; y < ypxl2; y++) {
            pr.x = interx;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            x = ipart_(interx);
            if (CLIPXY_TEST(x, y)) {
                if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= rfpart_(interx);
                        draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (CLIPXY_TEST(x+1, y)) {
                if (fragdepth_or_discard || fragment_processing(x+1, y, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x+1, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, dt_dx, dt_dy, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= fpart_(interx);
                        draw_pixel(c->builtins.gl_FragColor, x+1, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }

            interx += gradient;
        }
    }
}

#undef swap_
#undef plot
#undef ipart_
#undef fpart_
#undef round_
#undef rfpart_

static void draw_triangle(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
    int c_or, c_and;
    c_and = v0->clip_code & v1->clip_code & v2->clip_code;
    if (c_and != 0) {
        //printf("triangle outside\n");
        return;
    }

    // have to set here because we can re use vertices
    // for multiple triangles in STRIP and FAN
    v0->edge_flag = v1->edge_flag = v2->edge_flag = 1;

    // TODO figure out how to remove XY clipping while still
    // handling weird edge cases like LearnPortableGL's skybox
    // case
    //v0->clip_code &= CLIPZ_MASK;
    //v1->clip_code &= CLIPZ_MASK;
    //v2->clip_code &= CLIPZ_MASK;
    c_or = v0->clip_code | v1->clip_code | v2->clip_code;
    if (c_or == 0) {
        draw_triangle_final(v0, v1, v2, provoke);
    } else {
        draw_triangle_clip(v0, v1, v2, provoke, 0);
    }
}

static void draw_triangle_final(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
    int front_facing;
    int callback_done = 0;
    /* A clip-space collector projects the final corners itself. Keep culling
     * on the established screen-space path and honor a declined callback. */
    if (pgl_tri_clip_only && !c->cull_face) {
        c->builtins.gl_FrontFacing = GL_TRUE;
        if (pgl_tri_callback(v0, v1, v2, pgl_tri_callback_user))
            return;
        callback_done = 1;
    }
    /* Indexed vertex draws may have already transformed these shared corners
     * while building the vertex cache. Clipped temporary vertices carry a
     * false validity bit and still take the exact transform path here. */
    if (!v0->screen_space_valid || !v1->screen_space_valid ||
        !v2->screen_space_valid) {
#if PGL_NEON_ENABLED
        pgl_neon_transform_3vertices(&v0->screen_space, &v1->screen_space,
                                      &v2->screen_space, c->vp_mat,
                                      v0->clip_space, v1->clip_space,
                                      v2->clip_space);
#else
        v0->screen_space = mult_m4_v4(c->vp_mat, v0->clip_space);
        v1->screen_space = mult_m4_v4(c->vp_mat, v1->clip_space);
        v2->screen_space = mult_m4_v4(c->vp_mat, v2->clip_space);
#endif
        v0->screen_space_valid = v1->screen_space_valid =
            v2->screen_space_valid = GL_TRUE;
    }

    /* The RW GPU collector only needs the post-clip vertices and never reads
     * gl_FrontFacing.  Most RW draws also have face culling disabled, so the
     * winding calculation below would be pure CPU overhead for every
     * collected triangle.  Keep the exact old path when culling is enabled,
     * and retain the callback's normal return semantics so a future callback
     * that declines a triangle still falls through to the CPU rasterizer. */
    if (pgl_tri_callback && !c->cull_face && !callback_done) {
        c->builtins.gl_FrontFacing = GL_TRUE;
        if (pgl_tri_callback(v0, v1, v2, pgl_tri_callback_user))
            return;
        front_facing = is_front_facing(v0, v1, v2);
    } else {
        front_facing = is_front_facing(v0, v1, v2);
    }
    if (c->cull_face) {
        if (c->cull_mode == GL_FRONT_AND_BACK)
            return;
        if (c->cull_mode == GL_BACK && !front_facing) {
            //puts("culling back face");
            return;
        }
        if (c->cull_mode == GL_FRONT && front_facing)
            return;
    }

    c->builtins.gl_FrontFacing = front_facing;

    if (pgl_tri_callback && c->cull_face &&
        pgl_tri_callback(v0, v1, v2, pgl_tri_callback_user))
        return;

    // TODO when/if I get rid of pgl_PolygonMode support for FRONT
    // and BACK, this becomes a single function pointer, no branch
    if (front_facing) {
        c->draw_triangle_front(v0, v1, v2, provoke);
    } else {
        c->draw_triangle_back(v0, v1, v2, provoke);
    }
}


/* We clip the segment [a,b] against the 6 planes of the normal volume.
 * We compute the point 'c' of intersection and the value of the parameter 't'
 * of the intersection if x=a+t(b-a).
 */

#define clip_func(name, sign, dir, dir1, dir2) \
static float name(vec4 *c, vec4 *a, vec4 *b) \
{\
    float t, dx, dy, dz, dw, den;\
    dx = (b->x - a->x);\
    dy = (b->y - a->y);\
    dz = (b->z - a->z);\
    dw = (b->w - a->w);\
    den = -(sign d ## dir) + dw;\
    if (den == 0) t=0;\
    else t = ( sign a->dir - a->w) / den;\
    c->dir1 = a->dir1 + t * d ## dir1;\
    c->dir2 = a->dir2 + t * d ## dir2;\
    c->w = a->w + t * dw;\
    c->dir = sign c->w;\
    return t;\
}


clip_func(clip_xmin, -, x, y, z)

clip_func(clip_xmax, +, x, y, z)

clip_func(clip_ymin, -, y, x, z)

clip_func(clip_ymax, +, y, x, z)

clip_func(clip_zmin, -, z, x, y)

clip_func(clip_zmax, +, z, x, y)


static float (*clip_proc[6])(vec4 *, vec4 *, vec4 *) = {
    clip_zmin, clip_zmax,
    clip_xmin, clip_xmax,
    clip_ymin, clip_ymax
};

static inline void update_clip_pt(glVertex *q, glVertex *v0, glVertex *v1, float t)
{
    for (int i=0; i<c->vs_output.size; ++i) {
        // this is correct for both smooth and noperspective because
        // it's in clip space, pre-perspective divide
        //
        // https://www.khronos.org/opengl/wiki/Vertex_Post-Processing#Clipping
        q->vs_out[i] = v0->vs_out[i] + (v1->vs_out[i] - v0->vs_out[i]) * t;

        //PGL_FLAT should be handled indirectly by the provoke index
        //nothing to do here unless I change that
    }
    
    q->screen_space_valid = GL_FALSE;
    q->rw_projected_valid = GL_FALSE;
    q->clip_code = gl_clipcode(q->clip_space);
    //q->clip_code = gl_clipcode(q->clip_space) & CLIPZ_MASK;
}




static void draw_triangle_clip(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke, int clip_bit)
{
    int c_or, c_and, c_ex_or, cc[3], edge_flag_tmp, clip_mask;
    glVertex tmp1, tmp2, *q[3];
    float tt;

    //quite a bit of stack if there's a lot of clipping ...
    float tmp1_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float tmp2_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

    tmp1.vs_out = tmp1_out;
    tmp2.vs_out = tmp2_out;

    cc[0] = v0->clip_code;
    cc[1] = v1->clip_code;
    cc[2] = v2->clip_code;
    /*
    printf("in draw_triangle_clip\n");
    print_v4(v0->clip_space, "\n");
    print_v4(v1->clip_space, "\n");
    print_v4(v2->clip_space, "\n");
    printf("tmp_out tmp2_out = %p %p\n\n", tmp1_out, tmp2_out);
    */


    c_or = cc[0] | cc[1] | cc[2];
    if (c_or == 0) {
        draw_triangle_final(v0, v1, v2, provoke);
    } else {
        c_and = cc[0] & cc[1] & cc[2];
        /* the triangle is completely outside */
        if (c_and != 0) {
            //printf("triangle outside\n");
            return;
        }

        /* find the next direction to clip */
        // TODO only clip z planes or only near
        while (clip_bit < 6 && (c_or & (1 << clip_bit)) == 0)  {
            ++clip_bit;
        }

        /* this test can be true only in case of rounding errors */
        if (clip_bit == 6) {
#if 1
            printf("Clipping error:\n");
            print_v4(v0->clip_space, "\n");
            print_v4(v1->clip_space, "\n");
            print_v4(v2->clip_space, "\n");
#endif
            return;
        }

        clip_mask = 1 << clip_bit;
        c_ex_or = (cc[0] ^ cc[1] ^ cc[2]) & clip_mask;

        if (c_ex_or)  {
            /* one point outside */

            if (cc[0] & clip_mask) { q[0]=v0; q[1]=v1; q[2]=v2; }
            else if (cc[1] & clip_mask) { q[0]=v1; q[1]=v2; q[2]=v0; }
            else { q[0]=v2; q[1]=v0; q[2]=v1; }

            tt = clip_proc[clip_bit](&tmp1.clip_space, &q[0]->clip_space, &q[1]->clip_space);
            update_clip_pt(&tmp1, q[0], q[1], tt);

            tt = clip_proc[clip_bit](&tmp2.clip_space, &q[0]->clip_space, &q[2]->clip_space);
            update_clip_pt(&tmp2, q[0], q[2], tt);

            tmp1.edge_flag = q[0]->edge_flag;
            edge_flag_tmp = q[2]->edge_flag;
            q[2]->edge_flag = 0;
            draw_triangle_clip(&tmp1, q[1], q[2], provoke, clip_bit+1);

            tmp2.edge_flag = 0;
            tmp1.edge_flag = 0; // fixed from TinyGL, was 1
            q[2]->edge_flag = edge_flag_tmp;
            draw_triangle_clip(&tmp2, &tmp1, q[2], provoke, clip_bit+1);
        } else {
            /* two points outside */

            if ((cc[0] & clip_mask) == 0) { q[0]=v0; q[1]=v1; q[2]=v2; }
            else if ((cc[1] & clip_mask) == 0) { q[0]=v1; q[1]=v2; q[2]=v0; }
            else { q[0]=v2; q[1]=v0; q[2]=v1; }

            tt = clip_proc[clip_bit](&tmp1.clip_space, &q[0]->clip_space, &q[1]->clip_space);
            update_clip_pt(&tmp1, q[0], q[1], tt);

            tt = clip_proc[clip_bit](&tmp2.clip_space, &q[0]->clip_space, &q[2]->clip_space);
            update_clip_pt(&tmp2, q[0], q[2], tt);

            tmp1.edge_flag = 0; // fixed from TinyGL, was 1
            tmp2.edge_flag = q[2]->edge_flag;
            draw_triangle_clip(q[0], &tmp1, &tmp2, provoke, clip_bit+1);
        }
    }
}

static void draw_triangle_point(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke)
{
    //TODO use provoke?
    PGL_UNUSED(provoke);

    glVertex* vert[3] = { v0, v1, v2 };
    vec3 hp[3];
    hp[0] = v4_to_v3h(v0->screen_space);
    hp[1] = v4_to_v3h(v1->screen_space);
    hp[2] = v4_to_v3h(v2->screen_space);

    float poly_offset = 0;
    if (c->poly_offset_pt) {
        poly_offset = calc_poly_offset(hp[0], hp[1], hp[2]);
    }

    // TODO TinyGL uses edge_flags to determine whether to draw
    // a point here...but it doesn't work and there's no way
    // to make it work as far as I can tell.  There are hacks
    // I can do to get proper behavior but for now...meh
    for (int i=0; i<3; ++i) {
        draw_point(vert[i], poly_offset);
    }
}

static void draw_triangle_line(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke)
{
    // TODO early return if no edge_flags
    vec4 s0 = v0->screen_space;
    vec4 s1 = v1->screen_space;
    vec4 s2 = v2->screen_space;

    // TODO remove redundant calc in thick_line_shader
    vec3 hp0 = v4_to_v3h(s0);
    vec3 hp1 = v4_to_v3h(s1);
    vec3 hp2 = v4_to_v3h(s2);
    float w0 = v0->screen_space.w;
    float w1 = v1->screen_space.w;
    float w2 = v2->screen_space.w;

    float poly_offset = 0;
    if (c->poly_offset_line) {
        poly_offset = calc_poly_offset(hp0, hp1, hp2);
    }

    if (c->line_smooth) {
        if (v0->edge_flag) {
            draw_aa_line(hp0, hp1, w0, w1, v0->vs_out, v1->vs_out, provoke, poly_offset);
        }
        if (v1->edge_flag) {
            draw_aa_line(hp1, hp2, w1, w2, v1->vs_out, v2->vs_out, provoke, poly_offset);
        }
        if (v2->edge_flag) {
            draw_aa_line(hp2, hp0, w2, w0, v2->vs_out, v0->vs_out, provoke, poly_offset);
        }
    } else {
        if (v0->edge_flag) {
            draw_thick_line(hp0, hp1, w0, w1, v0->vs_out, v1->vs_out, provoke, poly_offset);
        }
        if (v1->edge_flag) {
            draw_thick_line(hp1, hp2, w1, w2, v1->vs_out, v2->vs_out, provoke, poly_offset);
        }
        if (v2->edge_flag) {
            draw_thick_line(hp2, hp0, w2, w0, v2->vs_out, v0->vs_out, provoke, poly_offset);
        }
    }
}

// TODO make macro or inline?
static float calc_poly_offset(vec3 hp0, vec3 hp1, vec3 hp2)
{
    float max_depth_slope = 0;
    float dzxy[6];
    dzxy[0] = fabsf((hp1.z - hp0.z)/(hp1.x - hp0.x));
    dzxy[1] = fabsf((hp1.z - hp0.z)/(hp1.y - hp0.y));
    dzxy[2] = fabsf((hp2.z - hp1.z)/(hp2.x - hp1.x));
    dzxy[3] = fabsf((hp2.z - hp1.z)/(hp2.y - hp1.y));
    dzxy[4] = fabsf((hp0.z - hp2.z)/(hp0.x - hp2.x));
    dzxy[5] = fabsf((hp0.z - hp2.z)/(hp0.y - hp2.y));

    max_depth_slope = dzxy[0];
    for (int i=1; i<6; ++i) {
        if (dzxy[i] > max_depth_slope)
            max_depth_slope = dzxy[i];
    }

#define SMALLEST_INCR 0.000001;
    return max_depth_slope * c->poly_factor + c->poly_units * SMALLEST_INCR;
#undef SMALLEST_INCR
}

#if PGL_NEON_ENABLED
#ifndef PGL_NO_DEPTH_NO_STENCIL
// 4-wide depth test + conditional depth write for a full span (ix..ix+3 all
// inside the row). Same arithmetic as fragment_processing(): z*PGL_MAX_Z
// truncated to u32, compared with the stored depth. Only valid when stencil
// is off and the shader can neither discard nor write gl_FragDepth, i.e. the
// depth result is final before the shader runs.
static inline uint32x4_t pgl_neon_depth_test4(uint32x4_t inside, float32x4_t z, int ix, int iy)
{
    int i = -iy * c->zbuf.w + ix;
#ifdef PGL_D16
    u16* zp = &((u16*)c->zbuf.lastrow)[i];
    uint16x4_t old16 = pgl_vld1_u16(zp);
    uint32x4_t dst = vmovl_u16(old16);
#else
    u32* zp = &((u32*)c->zbuf.lastrow)[i];
    uint32x4_t old = pgl_vld1q_u32(zp);
    uint32x4_t dst = vshrq_n_u32(old, PGL_ZSHIFT);
#endif
    uint32x4_t src = vcvtq_u32_f32(vmulq_n_f32(z, (float)PGL_MAX_Z));
    uint32x4_t pass;
    switch (c->depth_func) {
    case GL_LESS:     pass = vcltq_u32(src, dst); break;
    case GL_LEQUAL:   pass = vcleq_u32(src, dst); break;
    case GL_GREATER:  pass = vcgtq_u32(src, dst); break;
    case GL_GEQUAL:   pass = vcgeq_u32(src, dst); break;
    case GL_EQUAL:    pass = vceqq_u32(src, dst); break;
    case GL_NOTEQUAL: pass = vmvnq_u32(vceqq_u32(src, dst)); break;
    case GL_ALWAYS:   pass = vdupq_n_u32(0xFFFFFFFFu); break;
    default:          pass = vdupq_n_u32(0); break;
    }
    pass = vandq_u32(pass, inside);
    if (c->depth_mask && pgl_neon_any_u32(pass)) {
#ifdef PGL_D16
        pgl_vst1_u16(zp, vbsl_u16(vmovn_u32(pass), vmovn_u32(src), old16));
#else
        uint32x4_t nw = vorrq_u32(vandq_u32(old, vdupq_n_u32(PGL_STENCIL_MASK)),
                                  vshlq_n_u32(src, PGL_ZSHIFT));
        pgl_vst1q_u32(zp, vbslq_u32(pass, nw, old));
#endif
    }
    return pass;
}
#endif
#endif

static void draw_triangle_fill(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
#ifdef PGL_G3D
    /* GPU fast path: offload the triangle when eligible, otherwise flush any
     * pending GPU batch so this CPU triangle lands in painter's order. */
    if (pgl_gpu_tri(v0, v1, v2))
        return;
    pgl_gpu_fence();
#endif
    vec4 p0 = v0->screen_space;
    vec4 p1 = v1->screen_space;
    vec4 p2 = v2->screen_space;

    vec3 hp0 = v4_to_v3h(p0);
    vec3 hp1 = v4_to_v3h(p1);
    vec3 hp2 = v4_to_v3h(p2);

    // TODO even worth calculating or just some constant?
    float poly_offset = 0;

    if (c->poly_offset_fill) {
        poly_offset = calc_poly_offset(hp0, hp1, hp2);
    }

    /*
    print_v4(hp0, "\n");
    print_v4(hp1, "\n");
    print_v4(hp2, "\n");

    printf("%f %f %f\n", p0.w, p1.w, p2.w);
    print_v3(hp0, "\n");
    print_v3(hp1, "\n");
    print_v3(hp2, "\n\n");
    */

    //can't think of a better/cleaner way to do this than these 8 lines
    float x_min = MIN(hp0.x, hp1.x);
    float x_max = MAX(hp0.x, hp1.x);
    float y_min = MIN(hp0.y, hp1.y);
    float y_max = MAX(hp0.y, hp1.y);

    x_min = MIN(hp2.x, x_min);
    x_max = MAX(hp2.x, x_max);
    y_min = MIN(hp2.y, y_min);
    y_max = MAX(hp2.y, y_max);

    // clipping/scissoring against side planes here
    x_min = MAX(c->lx, x_min);
    x_max = MIN(c->ux, x_max);
    y_min = MAX(c->ly, y_min);
    y_max = MIN(c->uy, y_max);
    // end clipping

    // TODO is there any point to having an int index?
    // I think I did it for OpenMP
    int ix_max = roundf(x_max);
    int iy_max = roundf(y_max);

    //form implicit lines
    Line l01 = make_Line(hp0.x, hp0.y, hp1.x, hp1.y);
    Line l12 = make_Line(hp1.x, hp1.y, hp2.x, hp2.y);
    Line l20 = make_Line(hp2.x, hp2.y, hp0.x, hp0.y);

    // Precompute line function denominators and edge tests
    float denom_l01_hp2 = line_func(&l01, hp2.x, hp2.y);
    float denom_l20_hp1 = line_func(&l20, hp1.x, hp1.y);
    float edge_test_l12 = line_func(&l12, hp0.x, hp0.y) * line_func(&l12, -1, -2.5f);
    float edge_test_l20 = line_func(&l20, hp1.x, hp1.y) * line_func(&l20, -1, -2.5f);
    float edge_test_l01 = line_func(&l01, hp2.x, hp2.y) * line_func(&l01, -1, -2.5f);

    float alpha, beta, gamma, tmp, tmp2, z;
    float fs_input[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float fs_input_dx[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float fs_input_dy[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float* vs_output = &c->vs_output.output_buf[0];

    int vs_output_size = c->vs_output.size;
    float inv_w0 = 1.0f/p0.w;
    float inv_w1 = 1.0f/p1.w;
    float inv_w2 = 1.0f/p2.w;
    float dbeta_dx = l20.A / denom_l20_hp1;
    float dbeta_dy = l20.B / denom_l20_hp1;
    float dgamma_dx = l01.A / denom_l01_hp2;
    float dgamma_dy = l01.B / denom_l01_hp2;
    float dalpha_dx = -dbeta_dx - dgamma_dx;
    float dalpha_dy = -dbeta_dy - dgamma_dy;
    float dq_dx = dalpha_dx*inv_w0 + dbeta_dx*inv_w1 + dgamma_dx*inv_w2;
    float dq_dy = dalpha_dy*inv_w0 + dbeta_dy*inv_w1 + dgamma_dy*inv_w2;

    // Precompute depth mapping
    float depth_near = c->depth_range_near;
    float depth_scale = c->depth_range_far - depth_near;
    float depth_scale_half = depth_scale * 0.5f;

    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;
    Shader_Builtins builtins;
    builtins.gl_InstanceID = c->builtins.gl_InstanceID;
    builtins.fs_input_dx = NULL;
    builtins.fs_input_dy = NULL;

    // The 4-wide NEON span fill below drops scattered single pixels inside
    // filled quads (dotted seams along each quad diagonal, showing the clear
    // colour / whatever was underneath).  It reproduces with both the inline-
    // asm and the intrinsic load/store styles and disappears entirely with the
    // scalar loop, so the defect is in the NEON coverage/stepping path, not in
    // rounding of one expression.  Until that is isolated the triangle fill
    // runs scalar (verified pixel-clean on device); every other NEON kernel
    // (memcpy/fill/blend/matrix) makes no coverage decision and stays on.
#if 0 && PGL_NEON_ENABLED
    (void)alpha; (void)beta; (void)gamma; (void)tmp; (void)fs_input;

    pgl_neon_interp_plan plan;
    pgl_neon_interp_plan_init(&plan, v0->vs_out, v1->vs_out, v2->vs_out,
                              inv_w0, inv_w1, inv_w2,
                              vs_output + provoke*vs_output_size,
                              c->vs_output.interpolation, vs_output_size);

    pgl_neon_tri_setup ts;
    ts.a01 = l01.A; ts.b01 = l01.B; ts.c01 = l01.C; ts.inv_denom01 = 1.0f / denom_l01_hp2;
    ts.a20 = l20.A; ts.b20 = l20.B; ts.c20 = l20.C; ts.inv_denom20 = 1.0f / denom_l20_hp1;
    ts.et12 = (edge_test_l12 > 0.0f) ? 0xFFFFFFFFu : 0;
    ts.et20 = (edge_test_l20 > 0.0f) ? 0xFFFFFFFFu : 0;
    ts.et01 = (edge_test_l01 > 0.0f) ? 0xFFFFFFFFu : 0;
    ts.inv_w0 = inv_w0; ts.inv_w1 = inv_w1; ts.inv_w2 = inv_w2;
    ts.z0 = hp0.z; ts.z1 = hp1.z; ts.z2 = hp2.z;
    ts.poly_offset = poly_offset;
    ts.depth_scale_half = depth_scale_half;
    ts.depth_near = depth_near;

    // Depth/stencil strategy for a span, decided once per triangle:
    //   span_depth : stencil off, shader can't discard/write depth -> test and
    //                write 4 lanes at once before any shader runs
    //   lane_frag  : anything else that needs per-pixel fragment_processing()
    //                before the shader (stencil, or partial spans at the
    //                right edge that must not touch pixels past ix_max)
#ifndef PGL_NO_DEPTH_NO_STENCIL
#ifndef PGL_NO_STENCIL
    int stencil_on = c->stencil_test;
#else
    int stencil_on = 0;
#endif
    int span_depth = !fragdepth_or_discard && c->depth_test && !stencil_on;
    int lane_frag = !fragdepth_or_discard && (c->depth_test || stencil_on);
#else
    int span_depth = 0;
    int lane_frag = 0;
#endif
    void (*fragment_shader)(float*, Shader_Builtins*, void*) = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;

    float fs_in[4][GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    int ix_min = x_min;
    for (int iy = y_min; iy<iy_max; ++iy) {
        float y = iy + 0.5f;

        int row_lo, row_hi;
        if (!pgl_neon_row_range(&ts, y, ix_min, ix_max, &row_lo, &row_hi)) {
            continue;
        }

        // 4 pixel centers per step; the inside mask, barycentrics, depth test
        // and attribute interpolation are all 4-wide, only the shader and the
        // final pixel write run per lane.
        for (int ix = row_lo; ix<row_hi; ix += 4) {
            int lanes = ix_max - ix;
            pgl_neon_span4 sp;
            pgl_neon_eval_span4(&sp, &ts, ix, y, lanes);
            if (!pgl_neon_any_u32(sp.inside)) {
                continue;
            }

#ifndef PGL_NO_DEPTH_NO_STENCIL
            if (span_depth && lanes >= 4) {
                sp.inside = pgl_neon_depth_test4(sp.inside, sp.z, ix, iy);
            } else if (lane_frag) {
                uint32_t m[4];
                pgl_vst1q_u32(m, sp.inside);
                float zl[4];
                pgl_vst1q_f32(zl, sp.z);
                for (int l = 0; l < 4; ++l) {
                    if (m[l] && !fragment_processing(ix + l, iy, zl[l])) {
                        m[l] = 0;
                    }
                }
                sp.inside = pgl_vld1q_u32(m);
            }
            if (!pgl_neon_any_u32(sp.inside)) {
                continue;
            }
#endif

            pgl_neon_interp_span(fs_in, &plan, &sp);

#define PGL_NEON_FRAG_LANE(L) \
            if (vgetq_lane_u32(sp.inside, L)) { \
                z = vgetq_lane_f32(sp.z, L); \
                tmp2 = vgetq_lane_f32(sp.wsum, L); \
                SET_V4(builtins.gl_FragCoord, (ix + L) + 0.5f, y, z, tmp2); \
                builtins.discard = GL_FALSE; \
                builtins.gl_FragDepth = z; \
                fragment_shader(fs_in[L], &builtins, uniform); \
                if (!builtins.discard) { \
                    draw_pixel(builtins.gl_FragColor, ix + L, iy, builtins.gl_FragDepth, fragdepth_or_discard); \
                } \
            }
            PGL_NEON_FRAG_LANE(0)
            PGL_NEON_FRAG_LANE(1)
            PGL_NEON_FRAG_LANE(2)
            PGL_NEON_FRAG_LANE(3)
#undef PGL_NEON_FRAG_LANE
        }
    }
#else
    float perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS*3];
    int ix_min = x_min;
    builtins.fs_input_dx = fs_input_dx;
    builtins.fs_input_dy = fs_input_dy;
    for (int i=0; i<vs_output_size; ++i) {
        perspective[i] = v0->vs_out[i]/p0.w;
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] = v1->vs_out[i]/p1.w;
        perspective[2*GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] = v2->vs_out[i]/p2.w;
    }

    for (int iy = y_min; iy<iy_max; ++iy) {
        float y = iy + 0.5f;
        /* The edge equations are affine in x.  Evaluate their first sample
         * exactly for each row, then advance by the precomputed derivative;
         * this removes three line_func() calls (and their repeated A*x+B*y
         * multiplies) from every candidate pixel while preserving the exact
         * row origin used by the reference rasterizer. */
        float gamma_row = line_func(&l01, ix_min + 0.5f, y) / denom_l01_hp2;
        float beta_row = line_func(&l20, ix_min + 0.5f, y) / denom_l20_hp1;

        for (int ix = x_min; ix<ix_max; ++ix) {
            float x = ix + 0.5f;

            gamma = gamma_row;
            beta = beta_row;
            alpha = 1.0f - beta - gamma;

            if (alpha >= 0.0f && beta >= 0.0f && gamma >= 0.0f) {
                if ((alpha > 0.0f || edge_test_l12 > 0.0f) &&
                    (beta  > 0.0f || edge_test_l20 > 0.0f) &&
                    (gamma > 0.0f || edge_test_l01 > 0.0f)) {
                    tmp2 = alpha*inv_w0 + beta*inv_w1 + gamma*inv_w2;

                    z = alpha * hp0.z + beta * hp1.z + gamma * hp2.z + poly_offset;
                    z = (z + 1.0f) * depth_scale_half + depth_near;

                    if (!fragdepth_or_discard && !fragment_processing(x, y, z)) {
                        if (((ix - ix_min) & 31) == 31) {
                            float next_x = (float)(ix + 1) + 0.5f;
                            gamma_row = line_func(&l01, next_x, y) / denom_l01_hp2;
                            beta_row = line_func(&l20, next_x, y) / denom_l20_hp1;
                        } else {
                            gamma_row += dgamma_dx;
                            beta_row += dbeta_dx;
                        }
                        continue;
                    }

                    for (int i=0; i<vs_output_size; ++i) {
                        int interp = c->vs_output.interpolation[i];
                        if (interp == PGL_SMOOTH) {
                            tmp = alpha*perspective[i] + beta*perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] + gamma*perspective[2*GL_MAX_VERTEX_OUTPUT_COMPONENTS + i];
                            fs_input[i] = tmp/tmp2;
                            tmp = dalpha_dx*perspective[i] + dbeta_dx*perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] + dgamma_dx*perspective[2*GL_MAX_VERTEX_OUTPUT_COMPONENTS + i];
                            fs_input_dx[i] = (tmp - fs_input[i]*dq_dx)/tmp2;
                            tmp = dalpha_dy*perspective[i] + dbeta_dy*perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] + dgamma_dy*perspective[2*GL_MAX_VERTEX_OUTPUT_COMPONENTS + i];
                            fs_input_dy[i] = (tmp - fs_input[i]*dq_dy)/tmp2;
                        } else if (interp == PGL_NOPERSPECTIVE) {
                            fs_input[i] = alpha * v0->vs_out[i] + beta * v1->vs_out[i] + gamma * v2->vs_out[i];
                            fs_input_dx[i] = dalpha_dx*v0->vs_out[i] + dbeta_dx*v1->vs_out[i] + dgamma_dx*v2->vs_out[i];
                            fs_input_dy[i] = dalpha_dy*v0->vs_out[i] + dbeta_dy*v1->vs_out[i] + dgamma_dy*v2->vs_out[i];
                        } else {
                            fs_input[i] = vs_output[provoke*vs_output_size + i];
                            fs_input_dx[i] = 0.0f;
                            fs_input_dy[i] = 0.0f;
                        }
                    }

                    SET_V4(builtins.gl_FragCoord, x, y, z, tmp2);
                    builtins.discard = GL_FALSE;
                    builtins.gl_FragDepth = z;

                    c->programs.a[c->cur_program].fragment_shader(fs_input, &builtins, c->programs.a[c->cur_program].uniform);
                    if (!builtins.discard) {
                        draw_pixel(builtins.gl_FragColor, x, y, builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (((ix - ix_min) & 31) == 31) {
                float next_x = (float)(ix + 1) + 0.5f;
                gamma_row = line_func(&l01, next_x, y) / denom_l01_hp2;
                beta_row = line_func(&l20, next_x, y) / denom_l20_hp1;
            } else {
                gamma_row += dgamma_dx;
                beta_row += dbeta_dx;
            }
        }
    }
#endif
}


// TODO should this be done in colors/integers not vec4/floats?
// and if it's done in Colors/integers what's the performance difference?
#if PGL_NEON_ENABLED && PGL_BITDEPTH == 32
#define PGL_NEON_PIXEL 1
// blend_pixel() + RGBA_TO_PIXEL on 4 lanes at once. RGB and alpha use the
// same factor/equation tables, alpha just takes lane 3 of its own evaluation.
static pix_t blend_pixel_neon(float32x4_t src, float32x4_t dst)
{
    float32x4_t bc = pgl_neon_load_v4(&c->blend_color);
    float sat = MIN(vgetq_lane_f32(src, 3), 1.0f - vgetq_lane_f32(dst, 3));

    float32x4_t Cs = pgl_neon_blend_factor(c->blend_sRGB, src, dst, bc, sat);
    float32x4_t Cd = pgl_neon_blend_factor(c->blend_dRGB, src, dst, bc, sat);
    if (c->blend_sA != c->blend_sRGB) {
        float32x4_t a = pgl_neon_blend_factor(c->blend_sA, src, dst, bc, sat);
        Cs = vsetq_lane_f32(vgetq_lane_f32(a, 3), Cs, 3);
    }
    if (c->blend_dA != c->blend_dRGB) {
        float32x4_t a = pgl_neon_blend_factor(c->blend_dA, src, dst, bc, sat);
        Cd = vsetq_lane_f32(vgetq_lane_f32(a, 3), Cd, 3);
    }

    float32x4_t result = pgl_neon_blend_eq(c->blend_eqRGB, Cs, src, Cd, dst);
    if (c->blend_eqA != c->blend_eqRGB) {
        float32x4_t a = pgl_neon_blend_eq(c->blend_eqA, Cs, src, Cd, dst);
        result = vsetq_lane_f32(vgetq_lane_f32(a, 3), result, 3);
    }
    return pgl_neon_v4_to_pixel(result);
}
#else
#define PGL_NEON_PIXEL 0
static Color blend_pixel(vec4 src, vec4 dst)
{
    vec4 bc = c->blend_color;
    float i = MIN(src.w, 1-dst.w); // in colors this would be min(src.a, 255-dst.a)/255

    // only initializing to get rid of "possibly uninitialized warning"
    vec4 Cs = {0}, Cd = {0};

    switch (c->blend_sRGB) {
    case GL_ZERO:                     SET_V4(Cs, 0,0,0,0);                                 break;
    case GL_ONE:                      SET_V4(Cs, 1,1,1,1);                                 break;
    case GL_SRC_COLOR:                Cs = src;                                              break;
    case GL_ONE_MINUS_SRC_COLOR:      SET_V4(Cs, 1-src.x,1-src.y,1-src.z,1-src.w);         break;
    case GL_DST_COLOR:                Cs = dst;                                              break;
    case GL_ONE_MINUS_DST_COLOR:      SET_V4(Cs, 1-dst.x,1-dst.y,1-dst.z,1-dst.w);         break;
    case GL_SRC_ALPHA:                SET_V4(Cs, src.w, src.w, src.w, src.w);              break;
    case GL_ONE_MINUS_SRC_ALPHA:      SET_V4(Cs, 1-src.w,1-src.w,1-src.w,1-src.w);         break;
    case GL_DST_ALPHA:                SET_V4(Cs, dst.w, dst.w, dst.w, dst.w);              break;
    case GL_ONE_MINUS_DST_ALPHA:      SET_V4(Cs, 1-dst.w,1-dst.w,1-dst.w,1-dst.w);         break;
    case GL_CONSTANT_COLOR:           Cs = bc;                                               break;
    case GL_ONE_MINUS_CONSTANT_COLOR: SET_V4(Cs, 1-bc.x,1-bc.y,1-bc.z,1-bc.w);             break;
    case GL_CONSTANT_ALPHA:           SET_V4(Cs, bc.w, bc.w, bc.w, bc.w);                  break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: SET_V4(Cs, 1-bc.w,1-bc.w,1-bc.w,1-bc.w);             break;

    case GL_SRC_ALPHA_SATURATE:       SET_V4(Cs, i, i, i, 1);                              break;
    /*not implemented yet
     * won't be until I implement dual source blending/dual output from frag shader
     *https://www.opengl.org/wiki/Blending#Dual_Source_Blending
    case GL_SRC1_COLOR:               Cs =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cs =  break;
    case GL_SRC1_ALPHA:               Cs =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cs =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_sRGB!");
        break;
    }

    switch (c->blend_dRGB) {
    case GL_ZERO:                     SET_V4(Cd, 0,0,0,0);                                 break;
    case GL_ONE:                      SET_V4(Cd, 1,1,1,1);                                 break;
    case GL_SRC_COLOR:                Cd = src;                                              break;
    case GL_ONE_MINUS_SRC_COLOR:      SET_V4(Cd, 1-src.x,1-src.y,1-src.z,1-src.w);         break;
    case GL_DST_COLOR:                Cd = dst;                                              break;
    case GL_ONE_MINUS_DST_COLOR:      SET_V4(Cd, 1-dst.x,1-dst.y,1-dst.z,1-dst.w);         break;
    case GL_SRC_ALPHA:                SET_V4(Cd, src.w, src.w, src.w, src.w);              break;
    case GL_ONE_MINUS_SRC_ALPHA:      SET_V4(Cd, 1-src.w,1-src.w,1-src.w,1-src.w);         break;
    case GL_DST_ALPHA:                SET_V4(Cd, dst.w, dst.w, dst.w, dst.w);              break;
    case GL_ONE_MINUS_DST_ALPHA:      SET_V4(Cd, 1-dst.w,1-dst.w,1-dst.w,1-dst.w);         break;
    case GL_CONSTANT_COLOR:           Cd = bc;                                               break;
    case GL_ONE_MINUS_CONSTANT_COLOR: SET_V4(Cd, 1-bc.x,1-bc.y,1-bc.z,1-bc.w);             break;
    case GL_CONSTANT_ALPHA:           SET_V4(Cd, bc.w, bc.w, bc.w, bc.w);                  break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: SET_V4(Cd, 1-bc.w,1-bc.w,1-bc.w,1-bc.w);             break;

    case GL_SRC_ALPHA_SATURATE:       SET_V4(Cd, i, i, i, 1);                              break;
    /*not implemented yet
    case GL_SRC_ALPHA_SATURATE:       Cd =  break;
    case GL_SRC1_COLOR:               Cd =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cd =  break;
    case GL_SRC1_ALPHA:               Cd =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cd =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_dRGB!");
        break;
    }

    // TODO simplify combine redundancies
    switch (c->blend_sA) {
    case GL_ZERO:                     Cs.w = 0;              break;
    case GL_ONE:                      Cs.w = 1;              break;
    case GL_SRC_COLOR:                Cs.w = src.w;          break;
    case GL_ONE_MINUS_SRC_COLOR:      Cs.w = 1-src.w;        break;
    case GL_DST_COLOR:                Cs.w = dst.w;          break;
    case GL_ONE_MINUS_DST_COLOR:      Cs.w = 1-dst.w;        break;
    case GL_SRC_ALPHA:                Cs.w = src.w;          break;
    case GL_ONE_MINUS_SRC_ALPHA:      Cs.w = 1-src.w;        break;
    case GL_DST_ALPHA:                Cs.w = dst.w;          break;
    case GL_ONE_MINUS_DST_ALPHA:      Cs.w = 1-dst.w;        break;
    case GL_CONSTANT_COLOR:           Cs.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_COLOR: Cs.w = 1-bc.w;         break;
    case GL_CONSTANT_ALPHA:           Cs.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: Cs.w = 1-bc.w;         break;

    case GL_SRC_ALPHA_SATURATE:       Cs.w = 1;              break;
    /*not implemented yet
     * won't be until I implement dual source blending/dual output from frag shader
     *https://www.opengl.org/wiki/Blending#Dual_Source_Blending
    case GL_SRC1_COLOR:               Cs =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cs =  break;
    case GL_SRC1_ALPHA:               Cs =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cs =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_sA!");
        break;
    }

    switch (c->blend_dA) {
    case GL_ZERO:                     Cd.w = 0;              break;
    case GL_ONE:                      Cd.w = 1;              break;
    case GL_SRC_COLOR:                Cd.w = src.w;          break;
    case GL_ONE_MINUS_SRC_COLOR:      Cd.w = 1-src.w;        break;
    case GL_DST_COLOR:                Cd.w = dst.w;          break;
    case GL_ONE_MINUS_DST_COLOR:      Cd.w = 1-dst.w;        break;
    case GL_SRC_ALPHA:                Cd.w = src.w;          break;
    case GL_ONE_MINUS_SRC_ALPHA:      Cd.w = 1-src.w;        break;
    case GL_DST_ALPHA:                Cd.w = dst.w;          break;
    case GL_ONE_MINUS_DST_ALPHA:      Cd.w = 1-dst.w;        break;
    case GL_CONSTANT_COLOR:           Cd.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_COLOR: Cd.w = 1-bc.w;         break;
    case GL_CONSTANT_ALPHA:           Cd.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: Cd.w = 1-bc.w;         break;

    case GL_SRC_ALPHA_SATURATE:       Cd.w = 1;              break;
    /*not implemented yet
    case GL_SRC_ALPHA_SATURATE:       Cd =  break;
    case GL_SRC1_COLOR:               Cd =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cd =  break;
    case GL_SRC1_ALPHA:               Cd =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cd =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_dA!");
        break;
    }

    vec4 result;

    // TODO eliminate function calls to avoid alpha component calculations?
    switch (c->blend_eqRGB) {
    case GL_FUNC_ADD:
        result = add_v4s(mult_v4s(Cs, src), mult_v4s(Cd, dst));
        break;
    case GL_FUNC_SUBTRACT:
        result = sub_v4s(mult_v4s(Cs, src), mult_v4s(Cd, dst));
        break;
    case GL_FUNC_REVERSE_SUBTRACT:
        result = sub_v4s(mult_v4s(Cd, dst), mult_v4s(Cs, src));
        break;
    case GL_MIN:
        SET_V4(result, MIN(src.x, dst.x), MIN(src.y, dst.y), MIN(src.z, dst.z), MIN(src.w, dst.w));
        break;
    case GL_MAX:
        SET_V4(result, MAX(src.x, dst.x), MAX(src.y, dst.y), MAX(src.z, dst.z), MAX(src.w, dst.w));
        break;
    default:
        //should never get here
        puts("error unrecognized blend_eqRGB!");
        break;
    }

    switch (c->blend_eqA) {
    case GL_FUNC_ADD:
        result.w = Cs.w*src.w + Cd.w*dst.w;
        break;
    case GL_FUNC_SUBTRACT:
        result.w = Cs.w*src.w - Cd.w*dst.w;
        break;
    case GL_FUNC_REVERSE_SUBTRACT:
        result.w = Cd.w*dst.w - Cs.w*src.w;
        break;
    case GL_MIN:
        result.w = MIN(src.w, dst.w);
        break;
    case GL_MAX:
        result.w = MAX(src.w, dst.w);
        break;
    default:
        //should never get here
        puts("error unrecognized blend_eqRGB!");
        break;
    }

    // TODO should I clamp in v4_to_Color() instead
    result = clamp_01_v4(result);
    return v4_to_Color(result);
}
#endif

// source and destination colors
static pix_t logic_ops_pixel(pix_t s, pix_t d)
{
    switch (c->logic_func) {
    case GL_CLEAR:
        return 0;
    case GL_SET:
        return ~0;
    case GL_COPY:
        return s;
    case GL_COPY_INVERTED:
        return ~s;
    case GL_NOOP:
        return d;
    case GL_INVERT:
        return ~d;
    case GL_AND:
        return s & d;
    case GL_NAND:
        return ~(s & d);
    case GL_OR:
        return s | d;
    case GL_NOR:
        return ~(s | d);
    case GL_XOR:
        return s ^ d;
    case GL_EQUIV:
        return ~(s ^ d);
    case GL_AND_REVERSE:
        return s & ~d;
    case GL_AND_INVERTED:
        return ~s & d;
    case GL_OR_REVERSE:
        return s | ~d;
    case GL_OR_INVERTED:
        return ~s | d;
    default:
        puts("Unrecognized logic op!, defaulting to GL_COPY");
        return s;
    }

}

#ifndef PGL_NO_STENCIL
static int stencil_test(u8 stencil)
{
    int func, ref, mask;
    // TODO what about non-triangles, should use front values, so need to make sure that's set?
    if (c->builtins.gl_FrontFacing) {
        func = c->stencil_func;
        ref = c->stencil_ref;
        mask = c->stencil_valuemask;
    } else {
        func = c->stencil_func_back;
        ref = c->stencil_ref_back;
        mask = c->stencil_valuemask_back;
    }
    switch (func) {
    case GL_NEVER:    return 0;
    case GL_LESS:     return (ref & mask) < (stencil & mask);
    case GL_LEQUAL:   return (ref & mask) <= (stencil & mask);
    case GL_GREATER:  return (ref & mask) > (stencil & mask);
    case GL_GEQUAL:   return (ref & mask) >= (stencil & mask);
    case GL_EQUAL:    return (ref & mask) == (stencil & mask);
    case GL_NOTEQUAL: return (ref & mask) != (stencil & mask);
    case GL_ALWAYS:   return 1;
    default:
        puts("Error: unrecognized stencil function!");
        return 0;
    }

}

// TODO change ints to GLboolean? or just rename to indicate they're booleans?
// stencil_dest is pointer to stencil pixel which may be u32 for PGL_D24S8
static void stencil_op(int stencil, int depth, void* stencil_dest)
{
    GLuint op, ref, mask;
    // TODO make them proper arrays in gl_context?
    // ops is effectively set to an array of sfail, dpfail, dppass for either front
    // or back facing
    GLenum* ops;
    // TODO what about non-triangles, should use front values, so need to make sure that's set?
    if (c->builtins.gl_FrontFacing) {
        ops = &c->stencil_sfail;
        ref = c->stencil_ref;
        mask = c->stencil_writemask;
    } else {
        ops = &c->stencil_sfail_back;
        ref = c->stencil_ref_back;
        mask = c->stencil_writemask_back;
    }
    op = (!stencil) ? ops[0] : ((!depth) ? ops[1] : ops[2]);

    stencil_pix_t orig = *(stencil_pix_t*)stencil_dest;

    // TODO check C conversion guide...is there a point to masking the low byte?
#ifdef PGL_D16
    u8 val = orig;
#else
    u8 val = orig & PGL_STENCIL_MASK;
#endif

    switch (op) {
    case GL_KEEP: return;
    case GL_ZERO: val = 0; break;
    case GL_REPLACE: val = ref; break;
    case GL_INCR: if (val < 255) val++; break;
    case GL_INCR_WRAP: val++; break;
    case GL_DECR: if (val > 0) val--; break;
    case GL_DECR_WRAP: val--; break;
    case GL_INVERT: val = ~val;
    }

    // TODO is this sufficient? It doesn't really write protect
    // the bits not covered by the mask, it will just write 0 there
    //u8 result = val & mask;
    // TODO create a stencil test to verify correct behavior
    u8 result = (orig & ~mask) | (val & mask);

#ifdef PGL_D16
    *(u8*)stencil_dest = result;
#else
    *(u32*)stencil_dest = (orig & ~PGL_STENCIL_MASK) | result;
#endif
}

// end PGL_NO_STENCIL
#endif

/*
 * spec pg 110:
Point rasterization produces a fragment for each framebuffer pixel whose center
lies inside a square centered at the point’s (x w , y w ), with side length equal to the
current point size.

for a 1 pixel size point there are only 3 edge cases where more than 1 pixel center (0.5, 0.5)
would fall on the very edge of a 1 pixel square.  I think just drawing the upper or upper
corner pixel in these cases is fine and makes sense since width and height are actually 0.01 less
than full, see make_viewport_matrix
*/

static int fragment_processing(int x, int y, float z)
{
#ifndef PGL_NO_DEPTH_NO_STENCIL
    // NOTE/TODO assumes all 3 buffers have the same dimensions
    int i = -y*c->zbuf.w + x;

#ifndef PGL_NO_STENCIL
    // Stencil Test
    if (c->stencil_test) {
        stencil_pix_t* stencil_dest = &GET_STENCIL_PIX(i);
        if (!stencil_test(EXTRACT_STENCIL(*stencil_dest))) {
            stencil_op(GL_FALSE, GL_TRUE, stencil_dest);
            return 0;
        }
        // Depth test
        if (c->depth_test) {
            u32 dest_depth = GET_Z(i);
            u32 src_depth = z * PGL_MAX_Z;
            int depth_result = depthtest(src_depth, dest_depth);
            stencil_op(GL_TRUE, depth_result, stencil_dest);
            if (!depth_result) return GL_FALSE;
            SET_Z(i, src_depth & -(c->depth_mask != 0));
        } else {
            stencil_op(GL_TRUE, GL_TRUE, stencil_dest);
        }
    } else
#endif
    {
        // Depth test only (no stencil)
        if (c->depth_test) {
            u32 dest_depth = GET_Z(i);
            u32 src_depth = z * PGL_MAX_Z;
            if (!depthtest(src_depth, dest_depth)) return GL_FALSE;
            if (c->depth_mask) SET_Z(i, src_depth);
        }
    }
    return GL_TRUE;
#else
    return GL_TRUE;
#endif
}


static void draw_pixel(vec4 cf, int x, int y, float z, int do_frag_processing)
{
    if (do_frag_processing && !fragment_processing(x, y, z)) {
        return;
    }

    pix_t* dest_loc = &((pix_t*)c->back_buffer.lastrow)[-y*c->back_buffer.w + x];
    pix_t dst = *dest_loc;
    pix_t src;

#if PGL_NEON_PIXEL
    float32x4_t cfv = pgl_neon_load_v4(&cf);
    if (c->blend) {
        src = blend_pixel_neon(cfv, pgl_neon_pixel_to_v4(dst));
    } else {
        src = pgl_neon_v4_to_pixel(cfv);
    }
#else
    if (c->blend) {
        Color dest_color = PIXEL_TO_COLOR(dst);
        Color src_color = blend_pixel(cf, COLOR_TO_VEC4(dest_color));
        src = RGBA_TO_PIXEL(src_color.r, src_color.g, src_color.b, src_color.a);
    } else {
        cf = clamp_01_v4(cf);
        Color src_color = VEC4_TO_COLOR(cf);
        src = RGBA_TO_PIXEL(src_color.r, src_color.g, src_color.b, src_color.a);
    }
#endif

    if (c->logic_ops) {
        src = logic_ops_pixel(src, dst);
    }

#ifndef PGL_DISABLE_COLOR_MASK
    src = (src & c->color_mask) | (dst & ~c->color_mask);
#endif

    *dest_loc = src;
}



#include <stdarg.h>


/******************************************
 * PORTABLEGL_IMPLEMENTATION
 ******************************************/

#include <stdio.h>
#include <float.h>

// for CHAR_BIT
#include <limits.h>

// TODO different name? NO_ERROR_CHECKING? LOOK_MA_NO_HANDS?
#ifdef PGL_UNSAFE
#define PGL_SET_ERR(err)
#define PGL_ERR(check, err)
#define PGL_ERR_RET_VAL(check, err, ret)
#define PGL_LOG(err)
#else
#define PGL_LOG(err) \
    do { \
        if (c->dbg_output && c->dbg_callback) { \
            int len = snprintf(c->dbg_msg_buf, PGL_MAX_DEBUG_MESSAGE_LENGTH, "%s in %s() at %s:%d", pgl_err_strs[err-GL_NO_ERROR], __func__, __FILE__, __LINE__); \
            c->dbg_callback(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, 0, GL_DEBUG_SEVERITY_HIGH, len, c->dbg_msg_buf, c->dbg_userparam); \
        } \
    } while (0)

#define PGL_SET_ERR(err) \
    do { \
        if (!c->error) c->error = err; \
        PGL_LOG(err); \
    } while (0)

#define PGL_ERR(check, err) \
    do { \
        if (check) {  \
            if (!c->error) c->error = err; \
            PGL_LOG(err); \
            return; \
        } \
    } while (0)

#define PGL_ERR_RET_VAL(check, err, ret) \
    do { \
        if (check) {  \
            if (!c->error) c->error = err; \
            PGL_LOG(err); \
            return ret; \
        } \
    } while (0)
#endif

// I just set everything even if not everything applies to the type
// see section 3.8.15 pg 181 of spec for what it's supposed to be
static void free_texture_levels(glTexture* tex)
{
    int face, level;
    for (face = 0; face < PGL_TEXTURE_FACES; ++face) {
        for (level = 0; level < PGL_MAX_MIP_LEVELS; ++level) {
            glTextureLevel* l = &tex->levels[face][level];
            if (tex->data == l->data)
                tex->data = NULL;
            PGL_FREE(l->data);
            memset(l, 0, sizeof(*l));
        }
    }
}

static void sync_texture_level0(glTexture* tex, int cube)
{
    glTextureLevel* l = &tex->levels[0][0];
    tex->w = l->w;
    tex->h = l->h;
    tex->d = 1;
    tex->format = l->internalformat;
    tex->data = cube ? NULL : l->data;
}

static void INIT_TEX(glTexture* tex, GLenum target)
{
    memset(tex->levels, 0, sizeof(tex->levels));
    tex->type = target;
    tex->mag_filter = GL_LINEAR;
    if (target != GL_TEXTURE_RECTANGLE) {
        //tex->min_filter = GL_NEAREST_MIPMAP_LINEAR;
        tex->min_filter = GL_NEAREST;
        tex->wrap_s = GL_REPEAT;
        tex->wrap_t = GL_REPEAT;
        tex->wrap_r = GL_REPEAT;
    } else {
        tex->min_filter = GL_LINEAR;
        tex->wrap_s = GL_CLAMP_TO_EDGE;
        tex->wrap_t = GL_CLAMP_TO_EDGE;
        tex->wrap_r = GL_CLAMP_TO_EDGE;
    }
    tex->data = NULL;
    tex->datagen = 0;
    tex->deleted = GL_FALSE;
    tex->user_owned = GL_TRUE;
    tex->format = GL_RGBA;
    tex->w = 0;
    tex->h = 0;
    tex->d = 0;

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    tex->border_color = make_v4(0,0,0,0);
#endif
}

// default pass through shaders for index 0
PGLDEF void default_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(vs_output);
    PGL_UNUSED(uniforms);

    builtins->gl_Position = vertex_attribs[PGL_ATTR_VERT];
}

PGLDEF void default_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(fs_input);
    PGL_UNUSED(uniforms);

    vec4* fragcolor = &builtins->gl_FragColor;
    //wish I could use a compound literal, stupid C++ compatibility
    fragcolor->x = 1.0f;
    fragcolor->y = 0.0f;
    fragcolor->z = 0.0f;
    fragcolor->w = 1.0f;
}

// TODO Where to put this and what to name it? move this and all static functions to gl_internal.c?
#ifndef PGL_UNSAFE
static const char* pgl_err_strs[] =
{
    "GL_NO_ERROR",
    "GL_INVALID_ENUM",
    "GL_INVALID_VALUE",
    "GL_INVALID_OPERATION",
    "GL_INVALID_FRAMEBUFFER_OPERATION",
    "GL_OUT_OF_MEMORY"
};

PGLDEF void dflt_dbg_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    PGL_UNUSED(source);
    PGL_UNUSED(type);
    PGL_UNUSED(id);
    PGL_UNUSED(severity);
    PGL_UNUSED(length);
    PGL_UNUSED(userParam);

    fprintf(stderr, "%s\n", message);
}
#endif

static void init_glVertex_Attrib(glVertex_Attrib* v)
{
    /*
    GLint size;      // number of components 1-4
    GLenum type;     // GL_FLOAT, default
    GLsizei stride;  //
    GLsizei offset;  //
    GLboolean normalized;
    unsigned int buf;
    GLboolean enabled;
    GLuint divisor;
*/
    v->size = 4;
    v->type = GL_FLOAT;
    v->stride = 0;
    v->specified_stride = 0;
    v->offset = 0;
    v->normalized = GL_FALSE;
    v->buf = 0;
    v->enabled = 0;
    v->divisor = 0;
    v->current_value = make_v4(0.0f, 0.0f, 0.0f, 1.0f);
}

// TODO these are currently equivalent to memset(0) or = {0}...
static void init_glVertex_Array(glVertex_Array* v)
{
    v->deleted = GL_FALSE;
    for (int i=0; i<GL_MAX_VERTEX_ATTRIBS; ++i)
        init_glVertex_Attrib(&v->vertex_attribs[i]);
}

#define GET_SHIFT(mask, shift) \
    do {\
    shift = 0;\
    while ((mask & 1) == 0) {\
        mask >>= 1;\
        ++shift;\
    }\
    } while (0)


PGLDEF GLboolean init_glContext(glContext* context, pix_t** back, GLsizei w, GLsizei h)
{
    PGL_ERR_RET_VAL(!back, GL_INVALID_VALUE, GL_FALSE);
    PGL_ERR_RET_VAL((w < 0 || h < 0), GL_INVALID_VALUE, GL_FALSE);

    c = context;
    memset(c, 0, sizeof(glContext));

    if (w && h && *back != NULL) {
        c->user_alloced_backbuf = GL_TRUE;
        c->back_buffer.buf = (u8*)*back;
        c->back_buffer.w = w;
        c->back_buffer.h = h;
        c->back_buffer.lastrow = c->back_buffer.buf + (h-1)*w*sizeof(pix_t);
    }

    c->xmin = 0;
    c->ymin = 0;
    c->width = w;
    c->height = h;

    c->color_mask = ~0;

    //initialize all vectors
    cvec_glVertex_Array(&c->vertex_arrays, 0, 3);
    cvec_glBuffer(&c->buffers, 0, 3);
    cvec_glProgram(&c->programs, 0, 3);
    cvec_glTexture(&c->textures, 0, 1);
    cvec_glVertex(&c->glverts, 0, 10);

    // If not pre-allocating max, need to track size and edit pgl_UseProgram and pglSetInterp
    int max_vertices = get_max_vertices();
    c->vs_output.output_buf = (float*)PGL_MALLOC(max_vertices * GL_MAX_VERTEX_OUTPUT_COMPONENTS * sizeof(float));
    PGL_ERR_RET_VAL(!c->vs_output.output_buf, GL_OUT_OF_MEMORY, GL_FALSE);

    c->clear_color = 0;
    SET_V4(c->blend_color, 0, 0, 0, 0);
    c->point_size = 1.0f;
    c->line_width = 1.0f;
    c->clear_depth = 1.0f;
    c->depth_range_near = 0.0f;
    c->depth_range_far = 1.0f;
    make_viewport_m4(c->vp_mat, 0, 0, w, h, 1);

    //set flags
    //TODO match order in structure definition
    c->provoking_vert = GL_LAST_VERTEX_CONVENTION;
    c->cull_mode = GL_BACK;
    c->cull_face = GL_FALSE;
    c->front_face = GL_CCW;
    c->depth_test = GL_FALSE;
    c->fragdepth_or_discard = GL_FALSE;
    c->depth_clamp = GL_FALSE;
    c->depth_mask = GL_TRUE;
    c->blend = GL_FALSE;
    c->logic_ops = GL_FALSE;
    c->poly_offset_pt = GL_FALSE;
    c->poly_offset_line = GL_FALSE;
    c->poly_offset_fill = GL_FALSE;
    c->scissor_test = GL_FALSE;

#ifndef PGL_NO_STENCIL
    c->clear_stencil = 0;

    c->stencil_test = GL_FALSE;
    c->stencil_writemask = -1; // all 1s for the masks
    c->stencil_writemask_back = -1;
    c->stencil_ref = 0;
    c->stencil_ref_back = 0;
    c->stencil_valuemask = -1;
    c->stencil_valuemask_back = -1;
    c->stencil_func = GL_ALWAYS;
    c->stencil_func_back = GL_ALWAYS;
    c->stencil_sfail = GL_KEEP;
    c->stencil_dpfail = GL_KEEP;
    c->stencil_dppass = GL_KEEP;
    c->stencil_sfail_back = GL_KEEP;
    c->stencil_dpfail_back = GL_KEEP;
    c->stencil_dppass_back = GL_KEEP;
#endif

    c->logic_func = GL_COPY;
    c->blend_sRGB = GL_ONE;
    c->blend_sA = GL_ONE;
    c->blend_dRGB = GL_ZERO;
    c->blend_dA = GL_ZERO;
    c->blend_eqRGB = GL_FUNC_ADD;
    c->blend_eqA = GL_FUNC_ADD;
    c->depth_func = GL_LESS;
    c->line_smooth = GL_FALSE;
    c->poly_mode_front = GL_FILL;
    c->poly_mode_back = GL_FILL;
    c->point_spr_origin = GL_UPPER_LEFT;

    c->poly_factor = 0.0f;
    c->poly_units = 0.0f;

    c->scissor_lx = 0;
    c->scissor_ly = 0;
    c->scissor_w = w;
    c->scissor_h = h;

    // According to refpages https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glPixelStore.xhtml
    c->unpack_alignment = 4;
    c->pack_alignment = 4;

    c->draw_triangle_front = draw_triangle_fill;
    c->draw_triangle_back = draw_triangle_fill;

    c->error = GL_NO_ERROR;
#ifndef PGL_UNSAFE
    c->dbg_callback = dflt_dbg_callback;
    c->dbg_output = GL_TRUE;
#else
    c->dbg_callback = NULL;
    c->dbg_output = GL_FALSE;
#endif

    // program 0 is supposed to be undefined but not invalid so I'll
    // just make it default, no transform, just draws things red
    glProgram tmp_prog = { default_vs, default_fs, NULL, 0, {0}, GL_FALSE, GL_FALSE };
    cvec_push_glProgram(&c->programs, tmp_prog);
    pgl_UseProgram(0);

    // setup default vertex_array (vao) at position 0
    // we're like a compatibility profile for this but come on
    // no reason not to have this imo
    // https://www.opengl.org/wiki/Vertex_Specification#Vertex_Array_Object
    glVertex_Array tmp_va;
    init_glVertex_Array(&tmp_va);
    cvec_push_glVertex_Array(&c->vertex_arrays, tmp_va);
    c->cur_vertex_array = 0;

    // buffer 0 is invalid
    glBuffer tmp_buf = {0};
    tmp_buf.user_owned = GL_TRUE;
    tmp_buf.deleted = GL_FALSE;
    cvec_push_glBuffer(&c->buffers, tmp_buf);

    // From pgl_BindTexture():
    // "The value zero is reserved to represent the default texture for each texture target."
    // "In effect, the texture targets become aliases for the textures currently bound to them, and the texture name zero refers to the default textures that were bound to them at initialization."
    //
    // ... which means we can't use the 0 index at all as it can obviously only
    // be one type/target at a time and it would be a pain regardless
    // Still we might as well initialize it since something has to be there
    glTexture tmp_tex;
    INIT_TEX(&tmp_tex, GL_TEXTURE_UNBOUND);
    cvec_push_glTexture(&c->textures, tmp_tex);

    // Initialize the actual default textures..
    // TODO Should I initialize them as their actual types? no
    // Should I do the non-spec white pixel thing?
    for (int i=0; i<GL_NUM_TEXTURE_TYPES-GL_TEXTURE_UNBOUND-1; i++) {
        INIT_TEX(&c->default_textures[i], GL_TEXTURE_UNBOUND);
    }

    // default texture (0) is bound to all targets initially
    memset(c->bound_textures, 0, sizeof(c->bound_textures));

    // invalid buffer (0) bound initially
    memset(c->bound_buffers, 0, sizeof(c->bound_buffers));

    // DRY, do all buffer allocs/init in here
    if (w && h && !pglResizeFramebuffer(w, h)) {
#ifndef PGL_NO_DEPTH_NO_STENCIL
        PGL_FREE(c->zbuf.buf);
#if defined(PGL_D16) && !defined(PGL_NO_STENCIL)
        PGL_FREE(c->stencil_buf.buf);
#endif
#endif
        if (!c->user_alloced_backbuf) {
            PGL_FREE(c->back_buffer.buf);
        }
        return GL_FALSE;
    }

    *back = (pix_t*)c->back_buffer.buf;

    return GL_TRUE;
}

PGLDEF void free_glContext(glContext* ctx)
{
    int i;
#ifdef PGL_G3D
    /* release the shm render target the GPU path adopted as the back buffer
     * (portablegl treats it as user-owned and will not free it) */
    if (ctx == c) {
        pgl_g3d_shutdown();
        _pgl_gpu_active = 0;
        _pgl_gpu_off = 0;
    }
#endif
#ifndef PGL_NO_DEPTH_NO_STENCIL
    PGL_FREE(ctx->zbuf.buf);
#  if defined(PGL_D16) && !defined(PGL_NO_STENCIL)
    PGL_FREE(ctx->stencil_buf.buf);
#  endif
#endif
    if (!ctx->user_alloced_backbuf) {
        PGL_FREE(ctx->back_buffer.buf);
    }

    for (i=0; i<ctx->buffers.size; ++i) {
        if (!ctx->buffers.a[i].user_owned) {
            PGL_FREE(ctx->buffers.a[i].data);
        }
    }

    for (i=0; i<ctx->textures.size; ++i) {
        free_texture_levels(&ctx->textures.a[i]);
        if (!ctx->textures.a[i].user_owned) {
            PGL_FREE(ctx->textures.a[i].data);
        }
    }
    for (i=0; i<GL_NUM_TEXTURE_TYPES-GL_TEXTURE_UNBOUND-1; ++i) {
        free_texture_levels(&ctx->default_textures[i]);
        if (!ctx->default_textures[i].user_owned)
            PGL_FREE(ctx->default_textures[i].data);
    }

    //free vectors
    cvec_free_glVertex_Array(&ctx->vertex_arrays);
    cvec_free_glBuffer(&ctx->buffers);
    cvec_free_glProgram(&ctx->programs);
    cvec_free_glTexture(&ctx->textures);
    cvec_free_glVertex(&ctx->glverts);

    PGL_FREE(ctx->vs_output.output_buf);

    if (c == ctx) {
        c = NULL;
    }
}

PGLDEF void set_glContext(glContext* context)
{
    c = context;
}

PGLDEF GLboolean pglResizeFramebuffer(GLsizei w, GLsizei h)
{
    PGL_ERR_RET_VAL((w < 0 || h < 0), GL_INVALID_VALUE, GL_FALSE);

    // TODO C standard doesn't guarantee that passing the same size to
    // realloc is a no-op and will return the same pointer
    // NOTE checking zbuf because of the separation between pglSetBackBuffer()
    // and pglResizeFramebuffer(). If the former is called before the latter
    // backbuf dimensions would compare the same to the new size even when
    // we still need to update stencil and zbuf
    if (w == c->zbuf.w && h == c->zbuf.h) {
        return GL_TRUE; // no resize necessary = success to me
    }

    u8* tmp;

    if (!c->user_alloced_backbuf) {
        tmp = (u8*)PGL_REALLOC(c->back_buffer.buf, w*h * sizeof(pix_t));
        PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);
        c->back_buffer.buf = tmp;
        c->back_buffer.w = w;
        c->back_buffer.h = h;
        c->back_buffer.lastrow = c->back_buffer.buf + (h-1)*w*sizeof(pix_t);
    }

#ifdef PGL_D24S8
    tmp = (u8*)PGL_REALLOC(c->zbuf.buf, w*h * sizeof(u32));
    PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);

    c->zbuf.buf = tmp;
    c->zbuf.w = w;
    c->zbuf.h = h;
    c->zbuf.lastrow = c->zbuf.buf + (h-1)*w*sizeof(u32);

    // not checking for NO_STENCIL here because it makes no sense not to
    // have it if you're already using the space
    c->stencil_buf.buf = tmp;
    c->stencil_buf.w = w;
    c->stencil_buf.h = h;
    c->stencil_buf.lastrow = c->stencil_buf.buf + (h-1)*w*sizeof(u32);
#elif defined(PGL_D16)
    tmp = (u8*)PGL_REALLOC(c->zbuf.buf, w*h * sizeof(u16));
    PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);

    c->zbuf.buf = tmp;
    c->zbuf.w = w;
    c->zbuf.h = h;
    c->zbuf.lastrow = c->zbuf.buf + (h-1)*w*sizeof(u16);

#ifndef PGL_NO_STENCIL
    tmp = (u8*)PGL_REALLOC(c->stencil_buf.buf, w*h);
    PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);
    c->stencil_buf.buf = tmp;
    c->stencil_buf.w = w;
    c->stencil_buf.h = h;
    c->stencil_buf.lastrow = c->stencil_buf.buf + (h-1)*w;
#endif
#endif

    if (c->scissor_test) {
        int ux = c->scissor_lx+c->scissor_w;
        int uy = c->scissor_ly+c->scissor_h;

        c->lx = MAX(c->scissor_lx, 0);
        c->ly = MAX(c->scissor_ly, 0);
        c->ux = MIN(ux, w);
        c->uy = MIN(uy, h);
    } else {
        c->lx = 0;
        c->ly = 0;
        c->ux = w;
        c->uy = h;
    }

#ifdef PGL_G3D
    /* Keep the GPU shm target in step with the (user-owned) back buffer.  On
     * failure the GPU latches off and the back buffer moves to a fresh CPU
     * allocation of the new size. */
    if (_pgl_gpu_active) {
        if (pgl_g3d_resize(w, h)) {
            u8* px = (u8*)pgl_g3d_pixels();
            c->back_buffer.buf = px;
            c->back_buffer.w = w;
            c->back_buffer.h = h;
            c->back_buffer.lastrow = px + (h-1)*w*sizeof(pix_t);
            c->user_alloced_backbuf = GL_TRUE;
        } else {
            u8* nb = (u8*)PGL_MALLOC((size_t)w * (size_t)h * sizeof(pix_t));
            if (nb) {
                c->back_buffer.buf = nb;
                c->back_buffer.w = w;
                c->back_buffer.h = h;
                c->back_buffer.lastrow = nb + (h-1)*w*sizeof(pix_t);
                c->user_alloced_backbuf = GL_FALSE;
                pgl_g3d_shutdown();
            }
            _pgl_gpu_active = 0;
            _pgl_gpu_off = 1;
        }
    }
#endif

    return GL_TRUE;
}



PGLDEF GLubyte* pgl_GetString(GLenum name)
{
    static GLubyte vendor[] = "Robert Winkler (robertwinkler.com)";
    static GLubyte renderer[] = "PortableGL 0.101.0";
    static GLubyte version[] = "0.101.0";
    static GLubyte shading_language[] = "C/C++";

    switch (name) {
    case GL_VENDOR:                   return vendor;
    case GL_RENDERER:                 return renderer;
    case GL_VERSION:                  return version;
    case GL_SHADING_LANGUAGE_VERSION: return shading_language;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
        return NULL;
    }
}

PGLDEF GLenum pgl_GetError(void)
{
    GLenum err = c->error;
    c->error = GL_NO_ERROR;
    return err;
}

PGLDEF void pgl_GenVertexArrays(GLsizei n, GLuint* arrays)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);

    glVertex_Array tmp = {0};
    //init_glVertex_Array(&tmp);
    tmp.deleted = GL_FALSE;

    //fill up empty slots first
    --n;
    for (int i=1; i<c->vertex_arrays.size && n>=0; ++i) {
        if (c->vertex_arrays.a[i].deleted) {
            c->vertex_arrays.a[i] = tmp;
            arrays[n--] = i;
        }
    }

    for (; n>=0; --n) {
        cvec_push_glVertex_Array(&c->vertex_arrays, tmp);
        arrays[n] = c->vertex_arrays.size-1;
    }
}

PGLDEF void pgl_DeleteVertexArrays(GLsizei n, const GLuint* arrays)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    for (int i=0; i<n; ++i) {
        if (!arrays[i] || arrays[i] >= c->vertex_arrays.size)
            continue;

        // NOTE/TODO: This is non-standard behavior even in a compatibility profile but it
        // is similar to (from the user's perspective) how GL handles DeleteProgram called on
        // the active program.  So instead of getting a blank screen immediately, you just
        // free up the name moving the current vao to the default 0. Of course if you're switching
        // between VAOs and bind to the old name, you will get a GL error even if it still works
        // (because VAOS are POD and I don't overwrite it)... so maybe I should just have an
        // error here
        if (arrays[i] == c->cur_vertex_array) {
            memcpy(&c->vertex_arrays.a[0], &c->vertex_arrays.a[arrays[i]], sizeof(glVertex_Array));
            c->cur_vertex_array = 0;
        }

        c->vertex_arrays.a[arrays[i]].deleted = GL_TRUE;
    }
}

PGLDEF void pgl_GenBuffers(GLsizei n, GLuint* buffers)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    /* Sentinel for .type on a slot that has never been through pgl_BindBuffer().
       .type is stored as (target - GL_ARRAY_BUFFER) and is used as an index into
       c->bound_buffers[] (length GL_NUM_BUFFER_TYPES - GL_ARRAY_BUFFER) by
       pgl_DeleteBuffers(). cvec_extend_glBuffer() only realloc's - it does NOT
       zero the new tail - so an unbound buffer's .type would otherwise hold
       whatever garbage the previous heap occupant left behind, and DeleteBuffers
       would index bound_buffers[] far out of the glContext (observed as a data
       abort at c + garbage*4 + offsetof(bound_buffers) when yquake2's
       GL3_Draw_ShutdownLocal deleted an ebo2D that had never been bound). */
    const GLenum type_unbound = GL_NUM_BUFFER_TYPES - GL_ARRAY_BUFFER;
    //fill up empty slots first
    int j = 0;
    for (int i=1; i<c->buffers.size && j<n; ++i) {
        if (c->buffers.a[i].deleted) {
            c->buffers.a[i].deleted = GL_FALSE;
            c->buffers.a[i].size = 0;
            c->buffers.a[i].type = type_unbound;
            c->buffers.a[i].usage = GL_STATIC_DRAW;
            buffers[j++] = i;
        }
    }

    if (j != n) {
        int s = c->buffers.size;
        cvec_extend_glBuffer(&c->buffers, n-j);
        for (int i=s; j<n; i++) {
            c->buffers.a[i].size = 0;
            c->buffers.a[i].type = type_unbound;
            c->buffers.a[i].usage = GL_STATIC_DRAW;
            c->buffers.a[i].data = NULL;
            c->buffers.a[i].deleted = GL_FALSE;
            c->buffers.a[i].user_owned = GL_FALSE;
            buffers[j++] = i;
        }
    }
}

PGLDEF void pgl_DeleteBuffers(GLsizei n, const GLuint* buffers)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    GLenum type;
    for (int i=0; i<n; ++i) {
        if (!buffers[i] || buffers[i] >= c->buffers.size)
            continue;

        // NOTE(rswinkle): type is stored as correct index not the raw enum value so no need to
        // subtract here see pgl_BindBuffer
        type = c->buffers.a[buffers[i]].type;
        /* A buffer that was generated but never bound (e.g. only ever bound
           through a VAO, or only written via pgl_NamedBufferData) still carries
           the type_unbound sentinel from pgl_GenBuffers. Bound-check before
           indexing bound_buffers[] - the array is only
           GL_NUM_BUFFER_TYPES - GL_ARRAY_BUFFER entries wide. */
        if (type < GL_NUM_BUFFER_TYPES - GL_ARRAY_BUFFER &&
            buffers[i] == c->bound_buffers[type]) {
            c->bound_buffers[type] = 0;
        }

        for (GLuint va = 0; va < c->vertex_arrays.size; ++va) {
            for (GLuint a = 0; a < GL_MAX_VERTEX_ATTRIBS; ++a) {
                if (c->vertex_arrays.a[va].vertex_attribs[a].buf == buffers[i])
                    c->vertex_arrays.a[va].vertex_attribs[a].buf = 0;
            }
        }
        if (!c->buffers.a[buffers[i]].user_owned) {
            PGL_FREE(c->buffers.a[buffers[i]].data);
        }
        c->buffers.a[buffers[i]].data = NULL;
        c->buffers.a[buffers[i]].deleted = GL_TRUE;
        c->buffers.a[buffers[i]].user_owned = GL_FALSE;
    }
}

PGLDEF void pgl_GenTextures(GLsizei n, GLuint* textures)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    int j = 0;
    for (int i=1; i<c->textures.size && j<n; ++i) {
        if (c->textures.a[i].deleted) {
            INIT_TEX(&c->textures.a[i], GL_TEXTURE_UNBOUND);
            textures[j++] = i;
        }
    }
    if (j != n) {
        int s = c->textures.size;
        cvec_extend_glTexture(&c->textures, n-j);
        for (int i=s; j<n; i++) {
            INIT_TEX(&c->textures.a[i], GL_TEXTURE_UNBOUND);
            c->textures.a[i].user_owned = GL_FALSE;
            textures[j++] = i;
        }
    }
}

PGLDEF void pgl_CreateTextures(GLenum target, GLsizei n, GLuint* textures)
{
    PGL_ERR((target < GL_TEXTURE_1D || target >= GL_NUM_TEXTURE_TYPES), GL_INVALID_ENUM);
    PGL_ERR(n < 0, GL_INVALID_VALUE);

    target -= GL_TEXTURE_UNBOUND + 1;
    int j = 0;
    for (int i=1; i<c->textures.size && j<n; ++i) {
        if (c->textures.a[i].deleted) {
            INIT_TEX(&c->textures.a[i], target);
            textures[j++] = i;
        }
    }
    if (j != n) {
        int s = c->textures.size;
        cvec_extend_glTexture(&c->textures, n-j);
        for (int i=s; j<n; i++) {
            INIT_TEX(&c->textures.a[i], target);
            textures[j++] = i;
        }
    }
}

PGLDEF void pgl_DeleteTextures(GLsizei n, const GLuint* textures)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    GLenum type;
    for (int i=0; i<n; ++i) {
        if (!textures[i] || textures[i] >= c->textures.size)
            continue;

        // NOTE(rswinkle): type is stored as correct index not the raw enum value
        // so no need to subtract here see pgl_BindTexture
        type = c->textures.a[textures[i]].type;
        if (type != GL_TEXTURE_UNBOUND &&
            type < GL_NUM_TEXTURE_TYPES - GL_TEXTURE_UNBOUND - 1 &&
            textures[i] == c->bound_textures[type])
            c->bound_textures[type] = 0;

        free_texture_levels(&c->textures.a[textures[i]]);
        if (!c->textures.a[textures[i]].user_owned) {
            PGL_FREE(c->textures.a[textures[i]].data);
        }

        c->textures.a[textures[i]].type = GL_TEXTURE_UNBOUND;
        c->textures.a[textures[i]].data = NULL;
        c->textures.a[textures[i]].deleted = GL_TRUE;
        c->textures.a[textures[i]].user_owned = GL_FALSE;
    }
}

PGLDEF void pgl_BindVertexArray(GLuint array)
{
    PGL_ERR((array >= c->vertex_arrays.size || c->vertex_arrays.a[array].deleted), GL_INVALID_OPERATION);

    c->cur_vertex_array = array;
    c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER] = c->vertex_arrays.a[array].element_buffer;
}

PGLDEF void pgl_BindBuffer(GLenum target, GLuint buffer)
{
    PGL_ERR(target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER, GL_INVALID_ENUM);

    PGL_ERR((buffer >= c->buffers.size || c->buffers.a[buffer].deleted), GL_INVALID_OPERATION);

    target -= GL_ARRAY_BUFFER;
    c->bound_buffers[target] = buffer;

    // Note type isn't set till binding and we're not storing the raw
    // enum but the enum - GL_ARRAY_BUFFER so it's an index into c->bound_buffers
    // TODO need to see what's supposed to happen if you try to bind
    // a buffer to multiple targets
    c->buffers.a[buffer].type = target;

    if (target == GL_ELEMENT_ARRAY_BUFFER - GL_ARRAY_BUFFER) {
        c->vertex_arrays.a[c->cur_vertex_array].element_buffer = buffer;
    }
}

// TODO reuse code, call pgl_NamedBufferData() internally, remove duplicated error checks?
PGLDEF void pgl_BufferData(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage)
{
    PGL_ERR((target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER), GL_INVALID_ENUM);
    PGL_ERR((usage != GL_STREAM_DRAW && usage != GL_STATIC_DRAW &&
             usage != GL_DYNAMIC_DRAW), GL_INVALID_ENUM);
    PGL_ERR(size < 0, GL_INVALID_VALUE);

    target -= GL_ARRAY_BUFFER;
    PGL_ERR(!c->bound_buffers[target], GL_INVALID_OPERATION);

    // the spec says any pre-existing data store is deleted but there's no reason to
    // c->buffers.a[c->bound_buffers[target]].data is always NULL or valid
    u8* tmp = (u8*)PGL_REALLOC(c->buffers.a[c->bound_buffers[target]].data, size);
    PGL_ERR(!tmp, GL_OUT_OF_MEMORY);

    c->buffers.a[c->bound_buffers[target]].data = tmp;

    if (data) {
        PGL_MEMCPY(c->buffers.a[c->bound_buffers[target]].data, data, size);
    }

    c->buffers.a[c->bound_buffers[target]].user_owned = GL_FALSE;
    c->buffers.a[c->bound_buffers[target]].size = size;
    c->buffers.a[c->bound_buffers[target]].usage = usage;
}

PGLDEF void pgl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data)
{
    PGL_ERR(target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER, GL_INVALID_ENUM);
    PGL_ERR((offset < 0 || size < 0), GL_INVALID_VALUE);

    target -= GL_ARRAY_BUFFER;

    PGL_ERR(!c->bound_buffers[target], GL_INVALID_OPERATION);
    PGL_ERR((offset + size > c->buffers.a[c->bound_buffers[target]].size), GL_INVALID_VALUE);

    PGL_MEMCPY(&c->buffers.a[c->bound_buffers[target]].data[offset], data, size);
}

PGLDEF void pgl_NamedBufferData(GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage)
{
    PGL_ERR((!buffer || buffer >= c->buffers.size || c->buffers.a[buffer].deleted), GL_INVALID_OPERATION);
    PGL_ERR((usage != GL_STREAM_DRAW && usage != GL_STATIC_DRAW &&
             usage != GL_DYNAMIC_DRAW), GL_INVALID_ENUM);
    PGL_ERR(size < 0, GL_INVALID_VALUE);

    //always NULL or valid
    PGL_FREE(c->buffers.a[buffer].data);

    c->buffers.a[buffer].data = (u8*)PGL_MALLOC(size);
    PGL_ERR(!c->buffers.a[buffer].data, GL_OUT_OF_MEMORY);

    if (data) {
        PGL_MEMCPY(c->buffers.a[buffer].data, data, size);
    }

    c->buffers.a[buffer].user_owned = GL_FALSE;
    c->buffers.a[buffer].size = size;
    c->buffers.a[buffer].usage = usage;
}

PGLDEF void pgl_NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid* data)
{
    PGL_ERR((!buffer || buffer >= c->buffers.size || c->buffers.a[buffer].deleted), GL_INVALID_OPERATION);
    PGL_ERR((offset < 0 || size < 0), GL_INVALID_VALUE);
    PGL_ERR((offset + size > c->buffers.a[buffer].size), GL_INVALID_VALUE);

    PGL_MEMCPY(&c->buffers.a[buffer].data[offset], data, size);
}

// TODO see page 136-7 of spec
PGLDEF void pgl_BindTexture(GLenum target, GLuint texture)
{
    PGL_ERR((target < GL_TEXTURE_1D || target >= GL_NUM_TEXTURE_TYPES), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    PGL_ERR((texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_VALUE);

    if (texture) {
        GLenum type = c->textures.a[texture].type;
        PGL_ERR((type != GL_TEXTURE_UNBOUND && type != target), GL_INVALID_OPERATION);

        if (type == GL_TEXTURE_UNBOUND) {
            INIT_TEX(&c->textures.a[texture], target);
        }
    }
    c->bound_textures[target] = texture;
}

static void set_texparami(glTexture* tex, GLenum pname, GLint param)
{
    /*
    PGL_ERR((pname != GL_TEXTURE_MIN_FILTER && pname != GL_TEXTURE_MAG_FILTER &&
             pname != GL_TEXTURE_WRAP_S && pname != GL_TEXTURE_WRAP_T &&
             pname != GL_TEXTURE_WRAP_R), GL_INVALID_ENUM);
             */

    // NOTE, currently in the texture access functions
    // if it's not NEAREST, it assumes LINEAR so I could
    // just say that's good rather than these switch statements
    //
    GLint original_param = param;
    // TODO compress this code
    if (pname == GL_TEXTURE_MIN_FILTER) {
        // TODO technically GL_TEXTURE_RECTANGLE can only have NEAREST OR LINEAR, no mipmapping
        // but since we don't actually do mipmaping or use min filter at all...
        switch (param) {
        case GL_NEAREST:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            param = GL_NEAREST;
            break;
        case GL_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            param = GL_LINEAR;
            break;
        default:
            PGL_SET_ERR(GL_INVALID_ENUM);
            return;
        }
        tex->min_filter = original_param;
    } else if (pname == GL_TEXTURE_MAG_FILTER) {
        if (param != GL_NEAREST && param != GL_LINEAR) {
            PGL_SET_ERR(GL_INVALID_ENUM);
            return;
        }
        switch (param) {
        case GL_NEAREST:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            param = GL_NEAREST;
            break;
        case GL_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            param = GL_LINEAR;
            break;
        default:
            PGL_SET_ERR(GL_INVALID_ENUM);
            return;
        }
        tex->mag_filter = param;
    } else if (pname == GL_TEXTURE_WRAP_S) {
        PGL_ERR((param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER && param != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);

        // TODO This is in the standard but I don't really see the point, it costs nothing to support it,
        // maybe I'll make a PGL_WARN() macro or something
        //PGL_ERR((tex->type == GL_TEXTURE_RECTANGLE && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER), GL_INVALID_ENUM);
        tex->wrap_s = param;
    } else if (pname == GL_TEXTURE_WRAP_T) {
        PGL_ERR((param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER && param != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);

        //PGL_ERR((tex->type == GL_TEXTURE_RECTANGLE && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER), GL_INVALID_ENUM);

        tex->wrap_t = param;
    } else if (pname == GL_TEXTURE_WRAP_R) {
        PGL_ERR((param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER && param != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);
        tex->wrap_r = param;
    } else {
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

// TODO handle ParameterI*() functions correctly
static void get_texparami(glTexture* tex, GLenum pname, GLenum type, GLvoid* params)
{
    GLenum val;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: val = tex->min_filter; break;
    case GL_TEXTURE_MAG_FILTER: val = tex->mag_filter; break;
    case GL_TEXTURE_WRAP_S:
        val = tex->wrap_s;
        break;
    case GL_TEXTURE_WRAP_T:
        val = tex->wrap_t;
        break;
    case GL_TEXTURE_WRAP_R:
        val = tex->wrap_r;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
        return;
    }

    if (type == GL_INT) {
        *(GLint*)params = val;
    } else {
        *(GLuint*)params = val;
    }
}

PGLDEF void pgl_TexParameteri(GLenum target, GLenum pname, GLint param)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    //shift to range 0 - NUM_TEXTURES-1 to access bound_textures array
    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    set_texparami(tex, pname, param);
}

PGLDEF void pgl_TexParameterfv(GLenum target, GLenum pname, const GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    PGL_ERR((pname != GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;
    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    memcpy(&tex->border_color, params, sizeof(GLfloat)*4);
#endif
}
PGLDEF void pgl_TexParameteriv(GLenum target, GLenum pname, const GLint* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    PGL_ERR((pname != GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;
    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }

    tex->border_color.x = (2*params[0] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.y = (2*params[1] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.z = (2*params[2] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.w = (2*params[3] + 1)/(UINT32_MAX - 1.0f);
#endif
}

// NOTE: I added the !texture checks to the glTextureParameter*() functions
// even though it's not in the spec because there's no way to know which
// default texture (0) target you're referring to
PGLDEF void pgl_TextureParameteri(GLuint texture, GLenum pname, GLint param)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    set_texparami(&c->textures.a[texture], pname, param);
}

PGLDEF void pgl_TextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    memcpy(&c->textures.a[texture].border_color, params, sizeof(GLfloat)*4);
#endif
}

PGLDEF void pgl_TextureParameteriv(GLuint texture, GLenum pname, const GLint* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    glTexture* tex = &c->textures.a[texture];
    tex->border_color.x = (2*params[0] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.y = (2*params[1] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.z = (2*params[2] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.w = (2*params[3] + 1)/(UINT32_MAX - 1.0f);
#endif
}

PGLDEF void pgl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    PGL_ERR((pname != GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;
    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    memcpy(params, &tex->border_color, sizeof(GLfloat)*4);
#endif
}

PGLDEF void pgl_GetTexParameteriv(GLenum target, GLenum pname, GLint* params)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    get_texparami(tex, pname, GL_INT, (GLvoid*)params);
}

PGLDEF void pgl_GetTexParameterIiv(GLenum target, GLenum pname, GLint* params)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    get_texparami(tex, pname, GL_INT, (GLvoid*)params);
}

PGLDEF void pgl_GetTexParameterIuiv(GLenum target, GLenum pname, GLuint* params)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    get_texparami(tex, pname, GL_UNSIGNED_INT, (GLvoid*)params);
}

PGLDEF void pgl_GetTextureParameterfv(GLuint texture, GLenum pname, GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    memcpy(params, &c->textures.a[texture].border_color, sizeof(GLfloat)*4);
#endif
}

PGLDEF void pgl_GetTextureParameteriv(GLuint texture, GLenum pname, GLint* params)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    get_texparami(&c->textures.a[texture], pname, GL_UNSIGNED_INT, (GLvoid*)params);
}

PGLDEF void pgl_GetTextureParameterIiv(GLuint texture, GLenum pname, GLint* params)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    get_texparami(&c->textures.a[texture], pname, GL_UNSIGNED_INT, (GLvoid*)params);
}

PGLDEF void pgl_GetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint* params)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    get_texparami(&c->textures.a[texture], pname, GL_UNSIGNED_INT, (GLvoid*)params);
}


PGLDEF void pgl_PixelStorei(GLenum pname, GLint param)
{
    PGL_ERR((pname != GL_UNPACK_ALIGNMENT && pname != GL_PACK_ALIGNMENT), GL_INVALID_ENUM);

    PGL_ERR((param != 1 && param != 2 && param != 4 && param != 8), GL_INVALID_VALUE);

    // TODO eliminate branch? or use PGL_SET_ERR in else
    if (pname == GL_UNPACK_ALIGNMENT) {
        c->unpack_alignment = param;
    } else if (pname == GL_PACK_ALIGNMENT) {
        c->pack_alignment = param;
    }

}

// TODO check preprocessor output
#define CHECK_FORMAT_GET_COMP(format, components) \
    do { \
    switch (format) { \
    case GL_RED: \
    case GL_ALPHA: \
    case GL_LUMINANCE: \
    case PGL_ONE_ALPHA: \
        components = 1; \
        break; \
    case GL_RG: \
    case GL_LUMINANCE_ALPHA: \
        components = 2; \
        break; \
    case GL_RGB: \
    case GL_BGR: \
        components = 3; \
        break; \
    case GL_RGBA: \
    case GL_BGRA: \
        components = 4; \
        break; \
    default: \
        PGL_SET_ERR(GL_INVALID_ENUM); \
        return; \
    } \
    } while (0)

PGLDEF void pgl_TexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);
    PGL_UNUSED(border);

    PGL_ERR(target != GL_TEXTURE_1D, GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    tex->w = width;
    tex->h = 1;
    tex->d = 1;

    // TODO NULL or valid ... but what if user_owned?
    PGL_FREE(tex->data);

    //TODO hardcoded 4 till I support more than RGBA/UBYTE internally
    tex->data = (u8*)PGL_MALLOC(width * 4);
    PGL_ERR(!tex->data, GL_OUT_OF_MEMORY);

    u8* texdata = tex->data;

    if (data) {
        convert_format_to_packed_rgba(texdata, (u8*)data, width, 1, width*components, format);
    }

    tex->user_owned = GL_FALSE;
}

static int texture_format_components(GLenum format)
{
    switch (format) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_RED:
        return 1;
    case GL_LUMINANCE_ALPHA:
    case GL_RG:
        return 2;
    case GL_RGB:
    case GL_BGR:
        return 3;
    case GL_RGBA:
    case GL_BGRA:
        return 4;
    default:
        return 0;
    }
}

static int texture_type_valid(GLenum type)
{
    return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_5_6_5 ||
           type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1;
}

static int texture_format_type_valid(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_BYTE)
        return texture_format_components(format) != 0;
    if (type == GL_UNSIGNED_SHORT_5_6_5)
        return format == GL_RGB;
    return format == GL_RGBA;
}

static u8 texture_expand_bits(unsigned value, unsigned max_value)
{
    return (u8)((value * 255u + max_value / 2u) / max_value);
}

static void unpack_texture_rgba(u8* out, int out_pitch, const u8* in, int in_pitch,
                                int w, int h, GLenum format, GLenum type)
{
    int x, y;
    int components = texture_format_components(format);
    for (y = 0; y < h; ++y) {
        const u8* src = in + (size_t)y * (size_t)in_pitch;
        u8* dst = out + (size_t)y * (size_t)out_pitch;
        for (x = 0; x < w; ++x) {
            unsigned r = 0, g = 0, b = 0, a = 255;
            if (type == GL_UNSIGNED_BYTE) {
                const u8* p = src + x * components;
                if (format == GL_RGBA || format == GL_BGRA) {
                    r = p[0]; g = p[1]; b = p[2]; a = p[3];
                } else if (format == GL_RGB || format == GL_BGR) {
                    r = p[0]; g = p[1]; b = p[2];
                } else if (format == GL_LUMINANCE) {
                    r = g = b = p[0];
                } else if (format == GL_LUMINANCE_ALPHA) {
                    r = g = b = p[0]; a = p[1];
                } else if (format == GL_ALPHA) {
                    a = p[0];
                } else if (format == GL_RG) {
                    r = p[0]; g = p[1];
                } else {
                    r = p[0];
                }
                if (format == GL_BGR || format == GL_BGRA) {
                    unsigned tmp = r; r = b; b = tmp;
                }
            } else {
                unsigned v = (unsigned)src[x * 2] | ((unsigned)src[x * 2 + 1] << 8);
                if (type == GL_UNSIGNED_SHORT_5_6_5) {
                    r = texture_expand_bits((v >> 11) & 31u, 31u);
                    g = texture_expand_bits((v >> 5) & 63u, 63u);
                    b = texture_expand_bits(v & 31u, 31u);
                } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
                    r = texture_expand_bits((v >> 12) & 15u, 15u);
                    g = texture_expand_bits((v >> 8) & 15u, 15u);
                    b = texture_expand_bits((v >> 4) & 15u, 15u);
                    a = texture_expand_bits(v & 15u, 15u);
                } else {
                    r = texture_expand_bits((v >> 11) & 31u, 31u);
                    g = texture_expand_bits((v >> 6) & 31u, 31u);
                    b = texture_expand_bits((v >> 1) & 31u, 31u);
                    a = (v & 1u) ? 255u : 0u;
                }
            }
            dst[x * 4] = (u8)r;
            dst[x * 4 + 1] = (u8)g;
            dst[x * 4 + 2] = (u8)b;
            dst[x * 4 + 3] = (u8)a;
        }
    }
}

static glTexture* texture_for_2d_target(GLenum target, int* face, int* cube)
{
    int target_idx;
    int cur_tex_i;
    *face = 0;
    *cube = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
            target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    if (*cube) {
        *face = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        target_idx = GL_TEXTURE_CUBE_MAP - GL_TEXTURE_UNBOUND - 1;
    } else {
        target_idx = target - GL_TEXTURE_UNBOUND - 1;
    }
    cur_tex_i = c->bound_textures[target_idx];
    return cur_tex_i ? &c->textures.a[cur_tex_i] : &c->default_textures[target_idx];
}

static int texture_level_size(GLsizei width, GLsizei height, size_t* size)
{
    if (height && (size_t)width > ((size_t)-1) / ((size_t)height * 4u))
        return 0;
    *size = (size_t)width * (size_t)height * 4u;
    return 1;
}

static void install_texture_level(glTexture* tex, int face, int level,
                                  GLenum internalformat, GLsizei width, GLsizei height,
                                  u8* pixels, int cube)
{
    glTextureLevel* l = &tex->levels[face][level];
    if (tex->data == l->data)
        tex->data = NULL;
    PGL_FREE(l->data);
    l->data = pixels;
    l->w = width;
    l->h = height;
    l->internalformat = internalformat;
    l->defined = GL_TRUE;
    l->datagen = ++tex->datagen;
    tex->user_owned = GL_FALSE;
    sync_texture_level0(tex, cube);
}

PGLDEF void pgl_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    glTexture* tex;
    u8* pixels;
    size_t size, row_bytes, pitch;
    int face, cube, components;
    PGL_ERR((target != GL_TEXTURE_2D && target != GL_TEXTURE_1D_ARRAY &&
             target != GL_TEXTURE_RECTANGLE &&
             (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
              target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)), GL_INVALID_ENUM);
    PGL_ERR((level < 0 || level >= PGL_MAX_MIP_LEVELS || border != 0 ||
             width < 0 || height < 0 || width > PGL_MAX_TEXTURE_SIZE ||
             height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    components = texture_format_components(format);
    PGL_ERR(!components || !texture_type_valid(type), GL_INVALID_ENUM);
    PGL_ERR((internalformat != format || !texture_format_type_valid(format, type)),
            GL_INVALID_OPERATION);
    cube = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    PGL_ERR((cube && width != height), GL_INVALID_VALUE);
    PGL_ERR(!texture_level_size(width, height, &size), GL_OUT_OF_MEMORY);
    tex = texture_for_2d_target(target, &face, &cube);
    pixels = (u8*)PGL_MALLOC(size ? size : 1u);
    PGL_ERR(!pixels, GL_OUT_OF_MEMORY);
    if (data) {
        row_bytes = (size_t)width * (size_t)(type == GL_UNSIGNED_BYTE ? components : 2);
        pitch = (row_bytes + (size_t)c->unpack_alignment - 1u) /
                (size_t)c->unpack_alignment * (size_t)c->unpack_alignment;
        unpack_texture_rgba(pixels, width * 4, (const u8*)data, (int)pitch,
                            width, height, format, type);
    } else {
        memset(pixels, 0, size);
    }
    install_texture_level(tex, face, level, internalformat, width, height, pixels, cube);
}

PGLDEF void pgl_TexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);
    PGL_UNUSED(border);

    PGL_ERR((target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY), GL_INVALID_ENUM);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((height < 0 || height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((depth < 0 || depth > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    tex->w = width;
    tex->h = height;
    tex->d = depth;

    int byte_width = width * components;
    int padding_needed = byte_width % c->unpack_alignment;
    int padded_row_len = (!padding_needed) ? byte_width : byte_width + c->unpack_alignment - padding_needed;

    // NULL or valid
    PGL_FREE(tex->data);

    //TODO hardcoded 4 till I support more than RGBA/UBYTE internally
    tex->data = (u8*)PGL_MALLOC(width*height*depth * 4);
    PGL_ERR(!tex->data, GL_OUT_OF_MEMORY);

    u8* texdata = tex->data;

    if (data) {
        convert_format_to_packed_rgba(texdata, (u8*)data, width, height*depth, padded_row_len, format);
    }

    tex->user_owned = GL_FALSE;
}

PGLDEF void pgl_TexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);

    PGL_ERR(target != GL_TEXTURE_1D, GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    PGL_ERR((xoffset < 0 || xoffset + width > tex->w), GL_INVALID_VALUE);

    u32* texdata = (u32*)tex->data;
    convert_format_to_packed_rgba((u8*)&texdata[xoffset], (u8*)data, width, 1, width*components, format);
}

PGLDEF void pgl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* data)
{
    glTexture* tex;
    glTextureLevel* l;
    size_t row_bytes, pitch;
    int face, cube, components;
    PGL_ERR((target != GL_TEXTURE_2D &&
             (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
              target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)), GL_INVALID_ENUM);
    PGL_ERR((level < 0 || level >= PGL_MAX_MIP_LEVELS || width < 0 || height < 0),
            GL_INVALID_VALUE);
    components = texture_format_components(format);
    PGL_ERR(!components || !texture_type_valid(type), GL_INVALID_ENUM);
    PGL_ERR(!texture_format_type_valid(format, type), GL_INVALID_OPERATION);
    tex = texture_for_2d_target(target, &face, &cube);
    l = &tex->levels[face][level];
    PGL_ERR(!l->defined, GL_INVALID_OPERATION);
    PGL_ERR((format != l->internalformat), GL_INVALID_OPERATION);
    PGL_ERR((xoffset < 0 || yoffset < 0 || xoffset > l->w - width ||
             yoffset > l->h - height), GL_INVALID_VALUE);
    if (!width || !height)
        return;
    PGL_ERR(!data, GL_INVALID_OPERATION);
    row_bytes = (size_t)width * (size_t)(type == GL_UNSIGNED_BYTE ? components : 2);
    pitch = (row_bytes + (size_t)c->unpack_alignment - 1u) /
            (size_t)c->unpack_alignment * (size_t)c->unpack_alignment;
    unpack_texture_rgba(l->data + ((size_t)yoffset * (size_t)l->w + (size_t)xoffset) * 4u,
                        l->w * 4, (const u8*)data, (int)pitch,
                        width, height, format, type);
    l->datagen = ++tex->datagen;
}

PGLDEF void pgl_TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);

    PGL_ERR((target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY), GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((height < 0 || height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((depth < 0 || depth > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    int byte_width = width * components;
    int padding_needed = byte_width % c->unpack_alignment;
    int padded_row_len = (!padding_needed) ? byte_width : byte_width + c->unpack_alignment - padding_needed;

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    PGL_ERR((xoffset < 0 || xoffset + width > tex->w ||
             yoffset < 0 || yoffset + height > tex->h ||
             zoffset < 0 || zoffset + depth > tex->d), GL_INVALID_VALUE);

    int w = tex->w;
    int h = tex->h;
    int p = w*h;
    int pp = h*padded_row_len;
    u8* d = (u8*)data;
    u32* texdata = (u32*)tex->data;
    u8* out;
    u8* in;

    for (int j=0; j<depth; ++j) {
        for (int i=0; i<height; ++i) {
            out = (u8*)&texdata[(zoffset+j)*p + (yoffset+i)*w + xoffset];
            in = &d[j*pp + i*padded_row_len];
            convert_format_to_packed_rgba(out, in, width, 1, padded_row_len, format);
        }
    }
}

PGLDEF void pgl_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer)
{
    // See Section 2.8 pages 37-38 of 3.3 compatiblity spec
    //
    // Compare with Section 2.8 page 29 of 3.3 core spec
    // plus section E.2.2, pg 344 (VAOs required for everything, no default/0 VAO)
    //
    // GLES 2 and 3 match 3.3 compatibility profile
    //
    // Basically, core got rid of client arrays entirely, while compatibility
    // allows them for the default/0 VAO.
    //
    // So for now I've decided to match the compatibility profile
    // but you can easily remove c->cur_vertex_array from the check
    // below to enable client arrays for all VAOs; there's not really
    // any downside in PGL, it's all RAM.
    PGL_ERR((c->cur_vertex_array && !c->bound_buffers[GL_ARRAY_BUFFER-GL_ARRAY_BUFFER] && pointer),
            GL_INVALID_OPERATION);

    PGL_ERR(stride < 0, GL_INVALID_VALUE);
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    PGL_ERR((size < 1 || size > 4), GL_INVALID_VALUE);

    int type_sz = 4;
    switch (type) {
    case GL_BYTE:           type_sz = sizeof(GLbyte); break;
    case GL_UNSIGNED_BYTE:  type_sz = sizeof(GLubyte); break;
    case GL_SHORT:          type_sz = sizeof(GLshort); break;
    case GL_UNSIGNED_SHORT: type_sz = sizeof(GLushort); break;
    case GL_INT:            type_sz = sizeof(GLint); break;
    case GL_UNSIGNED_INT:   type_sz = sizeof(GLuint); break;

    case GL_FLOAT:  type_sz = sizeof(GLfloat); break;
    case GL_DOUBLE: type_sz = sizeof(GLdouble); break;

    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
        return;
    }

    glVertex_Attrib* v = &(c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index]);
    v->size = size;
    v->type = type;
    v->normalized = normalized;
    v->specified_stride = stride;
    v->stride = (stride) ? stride : size*type_sz;

    // offset can still really be a pointer if using the 0 VAO and no bound ARRAY_BUFFER.
    v->offset = (GLsizeiptr)pointer;
    // I put ARRAY_BUFFER-itself instead of 0 to reinforce that bound_buffers is indexed that way, buffer type - GL_ARRAY_BUFFER
    v->buf = c->bound_buffers[GL_ARRAY_BUFFER-GL_ARRAY_BUFFER];
}

PGLDEF void pgl_EnableVertexAttribArray(GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_TRUE;
}

PGLDEF void pgl_DisableVertexAttribArray(GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_FALSE;
}

PGLDEF void pgl_EnableVertexArrayAttrib(GLuint vaobj, GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    PGL_ERR((vaobj >= c->vertex_arrays.size || c->vertex_arrays.a[vaobj].deleted), GL_INVALID_OPERATION);

    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_TRUE;
}

PGLDEF void pgl_DisableVertexArrayAttrib(GLuint vaobj, GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    PGL_ERR((vaobj >= c->vertex_arrays.size || c->vertex_arrays.a[vaobj].deleted), GL_INVALID_OPERATION);
    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_FALSE;
}

PGLDEF void pgl_VertexAttribDivisor(GLuint index, GLuint divisor)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);

    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].divisor = divisor;
}



//TODO(rswinkle): Why is first, an index, a GLint and not GLuint or GLsizei?
PGLDEF void pgl_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(count < 0, GL_INVALID_VALUE);

    if (!count)
        return;

    run_pipeline(mode, (GLvoid*)(GLintptr)first, count, 0, 0, GL_FALSE);
}

PGLDEF void pgl_MultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(drawcount < 0, GL_INVALID_VALUE);

    for (GLsizei i=0; i<drawcount; i++) {
        if (!count[i]) continue;
        run_pipeline(mode, (GLvoid*)(GLintptr)first[i], count[i], 0, 0, GL_FALSE);
    }
}

PGLDEF void pgl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(count < 0, GL_INVALID_VALUE);

    // TODO error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    if (!count)
        return;

    run_pipeline(mode, indices, count, 0, 0, type);
}

// TODO fix
PGLDEF void pgl_MultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const GLvoid* const* indices, GLsizei drawcount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(drawcount < 0, GL_INVALID_VALUE);

    // TODO error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    for (GLsizei i=0; i<drawcount; i++) {
        if (!count[i]) continue;
        run_pipeline(mode, indices[i], count[i], 0, 0, type);
    }
}

PGLDEF void pgl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, (GLvoid*)(GLintptr)first, count, instance, 0, GL_FALSE);
    }
}

PGLDEF void pgl_DrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instancecount, GLuint baseinstance)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, (GLvoid*)(GLintptr)first, count, instance, baseinstance, GL_FALSE);
    }
}


PGLDEF void pgl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLsizei instancecount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    // NOTE: error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, indices, count, instance, 0, type);
    }
}

PGLDEF void pgl_DrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLsizei instancecount, GLuint baseinstance)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    //error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, indices, count, instance, baseinstance, GL_TRUE);
    }
}

PGLDEF void pgl_DebugMessageCallback(GLDEBUGPROC callback, void* userParam)
{
    c->dbg_callback = callback;
    c->dbg_userparam = userParam;
}

PGLDEF void pgl_Viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    PGL_ERR((width < 0 || height < 0), GL_INVALID_VALUE);

    // TODO: Do I need a full matrix? Also I don't actually
    // use these values anywhere else so why save them?  See ref pages or TinyGL for alternative
    make_viewport_m4(c->vp_mat, x, y, width, height, 1);
    c->xmin = x;
    c->ymin = y;
    c->width = width;
    c->height = height;
}

PGLDEF void pgl_ClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    red = clamp_01(red);
    green = clamp_01(green);
    blue = clamp_01(blue);
    alpha = clamp_01(alpha);

    //vec4 tmp = { red, green, blue, alpha };
    //c->clear_color = vec4_to_Color(tmp);
    c->clear_color = RGBA_TO_PIXEL(red*PGL_RMAX, green*PGL_GMAX, blue*PGL_BMAX, alpha*PGL_AMAX);
}

PGLDEF void pgl_ClearDepthf(GLfloat depth)
{
    c->clear_depth = clamp_01(depth);
}

PGLDEF void pgl_ClearDepth(GLdouble depth)
{
    c->clear_depth = clamp_01(depth);
}

PGLDEF void pgl_DepthFunc(GLenum func)
{
    PGL_ERR((func < GL_LESS || func > GL_NEVER), GL_INVALID_ENUM);

    c->depth_func = func;
}

PGLDEF void pgl_DepthRangef(GLfloat nearVal, GLfloat farVal)
{
    c->depth_range_near = clamp_01(nearVal);
    c->depth_range_far = clamp_01(farVal);
}

PGLDEF void pgl_DepthRange(GLdouble nearVal, GLdouble farVal)
{
    c->depth_range_near = clamp_01(nearVal);
    c->depth_range_far = clamp_01(farVal);
}

PGLDEF void pgl_DepthMask(GLboolean flag)
{
    c->depth_mask = flag;
}

PGLDEF void pgl_ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
#ifndef PGL_DISABLE_COLOR_MASK
    // !! ensures 1 or 0
    red = !!red;
    green = !!green;
    blue = !!blue;
    alpha = !!alpha;

    // By multiplying by the pixel format masks there's no need to shift them
    pix_t rmask = red*PGL_RMASK;
    pix_t gmask = green*PGL_GMASK;
    pix_t bmask = blue*PGL_BMASK;
    pix_t amask = alpha*PGL_AMASK;
    c->color_mask = rmask | gmask | bmask | amask;
#endif
}

// pgl_Clear row/span kernels. One contiguous run of n pixels each; the
// scalar bodies are the original per-pixel macros.
#ifndef PGL_DISABLE_COLOR_MASK
static inline void clear_color_span(pix_t* p, pix_t color, pix_t clear_mask, int n)
{
#if PGL_NEON_ENABLED && PGL_BITDEPTH == 32
    if (clear_mask == 0) {
        pgl_neon_fill_u32(p, color, n);
    } else {
        pgl_neon_fill_masked_u32(p, clear_mask, color, n);
    }
#else
    for (int i=0; i<n; ++i) {
        p[i] = (p[i] & clear_mask) | color;
    }
#endif
}
#else
static inline void clear_color_span(pix_t* p, pix_t color, int n)
{
#if PGL_NEON_ENABLED && PGL_BITDEPTH == 32
    pgl_neon_fill_u32(p, color, n);
#elif PGL_NEON_ENABLED && PGL_BITDEPTH == 16
    pgl_neon_fill_u16(p, color, n);
#else
    for (int i=0; i<n; ++i) {
        p[i] = color;
    }
#endif
}
#endif

#ifndef PGL_NO_DEPTH_NO_STENCIL
// p points at the zbuf pixel row, cd is already shifted by PGL_ZSHIFT
#ifdef PGL_D16
static inline void clear_depth_span(u16* p, u32 cd, int n)
{
#if PGL_NEON_ENABLED
    pgl_neon_fill_u16(p, (u16)cd, n);
#else
    for (int i=0; i<n; ++i) {
        p[i] = cd;
    }
#endif
}
#else
static inline void clear_depth_span(u32* p, u32 cd, int n)
{
#if PGL_NEON_ENABLED
    pgl_neon_fill_masked_u32(p, PGL_STENCIL_MASK, cd, n);
#else
    for (int i=0; i<n; ++i) {
        p[i] = (p[i] & PGL_STENCIL_MASK) | cd;
    }
#endif
}
#endif

#ifndef PGL_NO_STENCIL
static inline void clear_stencil_span(stencil_pix_t* p, u8 cs, int n)
{
#ifdef PGL_D16
#if PGL_NEON_ENABLED
    pgl_neon_memset(p, cs, n);
#else
    memset(p, cs, n);
#endif
#else
#if PGL_NEON_ENABLED
    pgl_neon_fill_masked_u32(p, ~PGL_STENCIL_MASK, cs, n);
#else
    for (int i=0; i<n; ++i) {
        p[i] = (p[i] & ~PGL_STENCIL_MASK) | cs;
    }
#endif
#endif
}
#endif
#endif

PGLDEF void pgl_Clear(GLbitfield mask)
{
    // TODO: If a buffer is not present, then a pgl_Clear directed at that buffer has no effect.
    // right now they're all always present

    PGL_ERR((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)), GL_INVALID_VALUE);

#ifdef PGL_G3D
    /* Phase A2 persistent depth.  pgl_Clear marks the frame boundary: it records
     * the clear colour/depth and flags the NEXT GPU draw dispatch as the frame's
     * first, so that dispatch clears the persistent colour+Z surfaces (later
     * dispatches LOAD them, letting a multi-draw frame accumulate instead of
     * each erasing the last).  No GPU dispatch is issued here.  The CPU clear
     * below still runs in full: it keeps c->z_buf correct for any CPU-drawn
     * primitive that may follow GPU draws in the same frame, and guarantees the
     * shared back_buffer is cleared even for a frame that issues no GPU draw. */
    if (_pgl_gpu_active) {
        /* A scissored or colour-masked pgl_Clear is performed precisely by the CPU
         * below, but the GPU folds the frame clear into its first draw dispatch
         * as a FULL-FRAME, unmasked clear that would clobber it.  Pin such a
         * frame to the CPU (pgl_gpu_state declines while _pgl_gpu_frame_cpu is
         * set); begin_frame still runs so any batch pending from the previous
         * frame is flushed in painter's order. */
        _pgl_gpu_frame_cpu = 0;
#ifndef PGL_DISABLE_COLOR_MASK
        if (c->scissor_test || c->color_mask != (GLuint)~(GLuint)0)
            _pgl_gpu_frame_cpu = 1;
#else
        if (c->scissor_test)
            _pgl_gpu_frame_cpu = 1;
#endif
        pgl_g3d_begin_frame(pgl_rgba8_from_pixel(c->clear_color),
                             (float)c->clear_depth);
    }
#endif

    // NOTE: All buffers should have the same dimensions/size
    int sz = c->ux * c->uy;
    int w = c->back_buffer.w;

    pix_t color = c->clear_color;

#ifndef PGL_DISABLE_COLOR_MASK
    // clear out channels not enabled for writing
    // TODO are these casts really necessary?
    color &= (pix_t)c->color_mask;
    // used to erase channels to be written
    pix_t clear_mask = ~((pix_t)c->color_mask);
#define CLEAR_COLOR_SPAN(p, n) clear_color_span((p), color, clear_mask, (n))
#else
#define CLEAR_COLOR_SPAN(p, n) clear_color_span((p), color, (n))
#endif

#ifndef PGL_NO_DEPTH_NO_STENCIL
    u32 cd = (u32)(c->clear_depth * PGL_MAX_Z) << PGL_ZSHIFT;
#  ifndef PGL_NO_STENCIL
    u8 cs = c->clear_stencil;
#  endif
#endif
    if (!c->scissor_test) {
        if (mask & GL_COLOR_BUFFER_BIT) {
            CLEAR_COLOR_SPAN((pix_t*)c->back_buffer.buf, sz);
        }
#ifndef PGL_NO_DEPTH_NO_STENCIL
        if (mask & GL_DEPTH_BUFFER_BIT && c->depth_mask) {
            clear_depth_span(&GET_ZPIX_TOP(0), cd, sz);
        }

#ifndef PGL_NO_STENCIL
        if (mask & GL_STENCIL_BUFFER_BIT) {
            clear_stencil_span(&GET_STENCIL_PIX_TOP(0), cs, sz);
        }
#  endif
#endif
    } else {
        // TODO this code is correct with or without scissor
        // enabled, test performance difference with above before
        // getting rid of above
        int n = c->ux - c->lx;
        if (mask & GL_COLOR_BUFFER_BIT) {
            for (int y=c->ly; y<c->uy; ++y) {
                CLEAR_COLOR_SPAN(&((pix_t*)c->back_buffer.lastrow)[-y*w + c->lx], n);
            }
        }
#ifndef PGL_NO_DEPTH_NO_STENCIL
        if (mask & GL_DEPTH_BUFFER_BIT && c->depth_mask) {
            for (int y=c->ly; y<c->uy; ++y) {
                clear_depth_span(&GET_ZPIX(-y*w + c->lx), cd, n);
            }
        }
#  ifndef PGL_NO_STENCIL
        if (mask & GL_STENCIL_BUFFER_BIT) {
            for (int y=c->ly; y<c->uy; ++y) {
                clear_stencil_span(&GET_STENCIL_PIX(-y*w + c->lx), cs, n);
            }
        }
#  endif
#endif
    }
#undef CLEAR_COLOR_SPAN
}

PGLDEF void pgl_Enable(GLenum cap)
{
    switch (cap) {
    case GL_CULL_FACE:
        c->cull_face = GL_TRUE;
        break;
    case GL_DEPTH_TEST:
        c->depth_test = GL_TRUE;
        break;
    case GL_DEPTH_CLAMP:
        c->depth_clamp = GL_TRUE;
        break;
    case GL_LINE_SMOOTH:
        // TODO implementation needs work/upgrade
        c->line_smooth = GL_TRUE;
        break;
    case GL_BLEND:
        c->blend = GL_TRUE;
        break;
    case GL_COLOR_LOGIC_OP:
        c->logic_ops = GL_TRUE;
        break;
    case GL_POLYGON_OFFSET_POINT:
        c->poly_offset_pt = GL_TRUE;
        break;
    case GL_POLYGON_OFFSET_LINE:
        c->poly_offset_line = GL_TRUE;
        break;
    case GL_POLYGON_OFFSET_FILL:
        c->poly_offset_fill = GL_TRUE;
        break;
    case GL_SCISSOR_TEST: {
        c->scissor_test = GL_TRUE;
        int ux = c->scissor_lx+c->scissor_w;
        int uy = c->scissor_ly+c->scissor_h;
        c->lx = MAX(c->scissor_lx, 0);
        c->ly = MAX(c->scissor_ly, 0);
        c->ux = MIN(ux, c->back_buffer.w);
        c->uy = MIN(uy, c->back_buffer.h);
    } break;
    case GL_STENCIL_TEST:
#ifndef PGL_NO_STENCIL
        c->stencil_test = GL_TRUE;
#endif
        break;
    case GL_DEBUG_OUTPUT:
        c->dbg_output = GL_TRUE;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void pgl_Disable(GLenum cap)
{
    switch (cap) {
    case GL_CULL_FACE:
        c->cull_face = GL_FALSE;
        break;
    case GL_DEPTH_TEST:
        c->depth_test = GL_FALSE;
        break;
    case GL_DEPTH_CLAMP:
        c->depth_clamp = GL_FALSE;
        break;
    case GL_LINE_SMOOTH:
        c->line_smooth = GL_FALSE;
        break;
    case GL_BLEND:
        c->blend = GL_FALSE;
        break;
    case GL_COLOR_LOGIC_OP:
        c->logic_ops = GL_FALSE;
        break;
    case GL_POLYGON_OFFSET_POINT:
        c->poly_offset_pt = GL_FALSE;
        break;
    case GL_POLYGON_OFFSET_LINE:
        c->poly_offset_line = GL_FALSE;
        break;
    case GL_POLYGON_OFFSET_FILL:
        c->poly_offset_fill = GL_FALSE;
        break;
    case GL_SCISSOR_TEST:
        c->scissor_test = GL_FALSE;
        c->lx = 0;
        c->ly = 0;
        c->ux = c->back_buffer.w;
        c->uy = c->back_buffer.h;
        break;
    case GL_STENCIL_TEST:
#ifndef PGL_NO_STENCIL
        c->stencil_test = GL_FALSE;
#endif
        break;
    case GL_DEBUG_OUTPUT:
        c->dbg_output = GL_FALSE;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF GLboolean pgl_IsEnabled(GLenum cap)
{
    // make up my own enum for this?  rename member as no_early_z?
    //GLboolean fragdepth_or_discard;
    switch (cap) {
    case GL_DEPTH_TEST: return c->depth_test;
    case GL_LINE_SMOOTH: return c->line_smooth;
    case GL_CULL_FACE: return c->cull_face;
    case GL_DEPTH_CLAMP: return c->depth_clamp;
    case GL_BLEND: return c->blend;
    case GL_COLOR_LOGIC_OP: return c->logic_ops;
    case GL_POLYGON_OFFSET_POINT: return c->poly_offset_pt;
    case GL_POLYGON_OFFSET_LINE: return c->poly_offset_line;
    case GL_POLYGON_OFFSET_FILL: return c->poly_offset_fill;
    case GL_SCISSOR_TEST: return c->scissor_test;
#ifndef PGL_NO_STENCIL
    case GL_STENCIL_TEST: return c->stencil_test;
#endif
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }

    return GL_FALSE;
}

PGLDEF GLboolean pgl_IsProgram(GLuint program)
{
    if (!program || program >= c->programs.size || c->programs.a[program].deleted) {
        return GL_FALSE;
    }

    return GL_TRUE;
}

PGLDEF void pgl_GetBooleanv(GLenum pname, GLboolean* data)
{
    // not sure it's worth adding every enum, spec says
    // gelGet* will convert/map types if they don't match the function
    switch (pname) {
    case GL_DEPTH_TEST:           *data = c->depth_test;       break;
    case GL_LINE_SMOOTH:          *data = c->line_smooth;      break;
    case GL_CULL_FACE:            *data = c->cull_face;        break;
    case GL_DEPTH_CLAMP:          *data = c->depth_clamp;      break;
    case GL_BLEND:                *data = c->blend;            break;
    case GL_COLOR_LOGIC_OP:       *data = c->logic_ops;        break;
    case GL_POLYGON_OFFSET_POINT: *data = c->poly_offset_pt;  break;
    case GL_POLYGON_OFFSET_LINE:  *data = c->poly_offset_line; break;
    case GL_POLYGON_OFFSET_FILL:  *data = c->poly_offset_fill; break;
    case GL_SCISSOR_TEST:         *data = c->scissor_test;     break;
#ifndef PGL_NO_STENCIL
    case GL_STENCIL_TEST:         *data = c->stencil_test;     break;
#endif
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void pgl_GetFloatv(GLenum pname, GLfloat* data)
{
    switch (pname) {
    case GL_POLYGON_OFFSET_FACTOR:         *data = c->poly_factor;         break;
    case GL_POLYGON_OFFSET_UNITS:          *data = c->poly_units;          break;
    case GL_POINT_SIZE:                    *data = c->point_size;          break;
    case GL_LINE_WIDTH:                    *data = c->line_width;          break;
    case GL_DEPTH_CLEAR_VALUE:             *data = c->clear_depth;         break;
    case GL_SMOOTH_LINE_WIDTH_GRANULARITY: *data = PGL_SMOOTH_GRANULARITY; break;

    case GL_MAX_TEXTURE_SIZE:         *data = PGL_MAX_TEXTURE_SIZE;         break;
    case GL_MAX_3D_TEXTURE_SIZE:      *data = PGL_MAX_3D_TEXTURE_SIZE;      break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: *data = PGL_MAX_ARRAY_TEXTURE_LAYERS; break;

    case GL_ALIASED_LINE_WIDTH_RANGE:
        data[0] = 1.0f;
        data[1] = PGL_MAX_ALIASED_WIDTH;
        break;

    case GL_SMOOTH_LINE_WIDTH_RANGE:
        data[0] = 1.0f;
        data[1] = PGL_MAX_SMOOTH_WIDTH;
        break;

    case GL_DEPTH_RANGE:
        data[0] = c->depth_range_near;
        data[1] = c->depth_range_near;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void pgl_GetIntegerv(GLenum pname, GLint* data)
{
    // TODO maybe make all the enum/int member names match the associated ENUM?
    switch (pname) {
#ifndef PGL_NO_STENCIL
    case GL_STENCIL_WRITE_MASK:       data[0] = c->stencil_writemask; break;
    case GL_STENCIL_REF:              data[0] = c->stencil_ref; break;
    case GL_STENCIL_VALUE_MASK:       data[0] = c->stencil_valuemask; break;
    case GL_STENCIL_FUNC:             data[0] = c->stencil_func; break;
    case GL_STENCIL_FAIL:             data[0] = c->stencil_sfail; break;
    case GL_STENCIL_PASS_DEPTH_FAIL:  data[0] = c->stencil_dpfail; break;
    case GL_STENCIL_PASS_DEPTH_PASS:  data[0] = c->stencil_dppass; break;

    case GL_STENCIL_BACK_WRITE_MASK:      data[0] = c->stencil_writemask_back; break;
    case GL_STENCIL_BACK_REF:             data[0] = c->stencil_ref_back; break;
    case GL_STENCIL_BACK_VALUE_MASK:      data[0] = c->stencil_valuemask_back; break;
    case GL_STENCIL_BACK_FUNC:            data[0] = c->stencil_func_back; break;
    case GL_STENCIL_BACK_FAIL:            data[0] = c->stencil_sfail_back; break;
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL: data[0] = c->stencil_dpfail_back; break;
    case GL_STENCIL_BACK_PASS_DEPTH_PASS: data[0] = c->stencil_dppass_back; break;
#endif

    case GL_LOGIC_OP_MODE:             data[0] = c->logic_func; break;

    //TODO implement pgl_BlendFuncSeparate and pgl_BlendEquationSeparate
    case GL_BLEND_SRC_RGB:             data[0] = c->blend_sRGB; break;
    case GL_BLEND_SRC_ALPHA:           data[0] = c->blend_sA; break;
    case GL_BLEND_DST_RGB:             data[0] = c->blend_dRGB; break;
    case GL_BLEND_DST_ALPHA:           data[0] = c->blend_dA; break;

    case GL_BLEND_EQUATION_RGB:        data[0] = c->blend_eqRGB; break;
    case GL_BLEND_EQUATION_ALPHA:      data[0] = c->blend_eqA; break;

    case GL_CULL_FACE_MODE:            data[0] = c->cull_mode; break;
    case GL_FRONT_FACE:                data[0] = c->front_face; break;
    case GL_DEPTH_FUNC:                data[0] = c->depth_func; break;
    case GL_POINT_SPRITE_COORD_ORIGIN: data[0] = c->point_spr_origin; break;
    case GL_PROVOKING_VERTEX:          data[0] = c->provoking_vert; break;

    case GL_MAX_TEXTURE_SIZE:         data[0] = PGL_MAX_TEXTURE_SIZE;         break;
    case GL_MAX_3D_TEXTURE_SIZE:      data[0] = PGL_MAX_3D_TEXTURE_SIZE;      break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: data[0] = PGL_MAX_ARRAY_TEXTURE_LAYERS; break;

    case GL_MAX_DEBUG_MESSAGE_LENGTH: data[0] = PGL_MAX_DEBUG_MESSAGE_LENGTH; break;

    case GL_POLYGON_MODE:
        data[0] = c->poly_mode_front;
        data[1] = c->poly_mode_back;
        break;

    case GL_VIEWPORT:
        data[0] = c->xmin;
        data[1] = c->ymin;
        data[2] = c->width;
        data[3] = c->height;
        break;

    case GL_SCISSOR_BOX:
        data[0] = c->scissor_lx;
        data[1] = c->scissor_ly;
        data[2] = c->scissor_w;
        data[3] = c->scissor_h;
        break;

    // TODO decide if 3.2 is the best approximation
    case GL_MAJOR_VERSION:             data[0] = 3; break;
    case GL_MINOR_VERSION:             data[0] = 2; break;

    case GL_ARRAY_BUFFER_BINDING:
        data[0] = c->bound_buffers[GL_ARRAY_BUFFER-GL_ARRAY_BUFFER];
        break;

    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
        data[0] = c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER];
        break;

    case GL_VERTEX_ARRAY_BINDING:
        data[0] = c->cur_vertex_array;
        break;

    case GL_CURRENT_PROGRAM:
        data[0] = c->cur_program;
        break;


    case GL_TEXTURE_BINDING_1D:        data[0] = c->bound_textures[GL_TEXTURE_1D-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_2D:        data[0] = c->bound_textures[GL_TEXTURE_2D-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_3D:        data[0] = c->bound_textures[GL_TEXTURE_3D-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_1D_ARRAY:  data[0] = c->bound_textures[GL_TEXTURE_1D_ARRAY-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_2D_ARRAY:  data[0] = c->bound_textures[GL_TEXTURE_2D_ARRAY-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_RECTANGLE: data[0] = c->bound_textures[GL_TEXTURE_RECTANGLE-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_CUBE_MAP:  data[0] = c->bound_textures[GL_TEXTURE_CUBE_MAP-GL_TEXTURE_UNBOUND-1]; break;

    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void pgl_CullFace(GLenum mode)
{
    PGL_ERR((mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK), GL_INVALID_ENUM);
    c->cull_mode = mode;
}

PGLDEF void pgl_FrontFace(GLenum mode)
{
    PGL_ERR((mode != GL_CCW && mode != GL_CW), GL_INVALID_ENUM);
    c->front_face = mode;
}

PGLDEF void pgl_PolygonMode(GLenum face, GLenum mode)
{
    // TODO only support FRONT_AND_BACK like OpenGL 3/4 and OpenGL ES 2/3 ...
    // or keep support for FRONT and BACK like OpenGL 1 and 2?
    // Make final decision before version 1.0.0
    PGL_ERR(((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
             (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL)), GL_INVALID_ENUM);

    if (mode == GL_POINT) {
        if (face == GL_FRONT) {
            c->poly_mode_front = mode;
            c->draw_triangle_front = draw_triangle_point;
        } else if (face == GL_BACK) {
            c->poly_mode_back = mode;
            c->draw_triangle_back = draw_triangle_point;
        } else {
            c->poly_mode_front = mode;
            c->poly_mode_back = mode;
            c->draw_triangle_front = draw_triangle_point;
            c->draw_triangle_back = draw_triangle_point;
        }
    } else if (mode == GL_LINE) {
        if (face == GL_FRONT) {
            c->poly_mode_front = mode;
            c->draw_triangle_front = draw_triangle_line;
        } else if (face == GL_BACK) {
            c->poly_mode_back = mode;
            c->draw_triangle_back = draw_triangle_line;
        } else {
            c->poly_mode_front = mode;
            c->poly_mode_back = mode;
            c->draw_triangle_front = draw_triangle_line;
            c->draw_triangle_back = draw_triangle_line;
        }
    } else  {
        if (face == GL_FRONT) {
            c->poly_mode_front = mode;
            c->draw_triangle_front = draw_triangle_fill;
        } else if (face == GL_BACK) {
            c->poly_mode_back = mode;
            c->draw_triangle_back = draw_triangle_fill;
        } else {
            c->poly_mode_front = mode;
            c->poly_mode_back = mode;
            c->draw_triangle_front = draw_triangle_fill;
            c->draw_triangle_back = draw_triangle_fill;
        }
    }
}

PGLDEF void pgl_LineWidth(GLfloat width)
{
    PGL_ERR(width <= 0.0f, GL_INVALID_VALUE);
    c->line_width = width;
}

PGLDEF void pgl_PointSize(GLfloat size)
{
    PGL_ERR(size <= 0.0f, GL_INVALID_VALUE);
    c->point_size = size;
}

PGLDEF void pgl_PointParameteri(GLenum pname, GLint param)
{
    //also GL_POINT_FADE_THRESHOLD_SIZE
    PGL_ERR((pname != GL_POINT_SPRITE_COORD_ORIGIN ||
            (param != GL_LOWER_LEFT && param != GL_UPPER_LEFT)), GL_INVALID_ENUM);

    c->point_spr_origin = param;
}

PGLDEF void pgl_ProvokingVertex(GLenum provokeMode)
{
    PGL_ERR((provokeMode != GL_FIRST_VERTEX_CONVENTION && provokeMode != GL_LAST_VERTEX_CONVENTION), GL_INVALID_ENUM);

    c->provoking_vert = provokeMode;
}


// Shader functions
PGLDEF GLuint pglCreateProgram(vert_func vertex_shader, frag_func fragment_shader, GLsizei n, GLenum* interpolation, GLboolean fragdepth_or_discard)
{
    // Using pgl_AttachShader error if shader is not a shader object which
    // is the closest analog
    PGL_ERR_RET_VAL((!vertex_shader || !fragment_shader), GL_INVALID_OPERATION, 0);

    PGL_ERR_RET_VAL((n < 0 || n > GL_MAX_VERTEX_OUTPUT_COMPONENTS), GL_INVALID_VALUE, 0);

    glProgram tmp = {vertex_shader, fragment_shader, NULL, n, {0}, fragdepth_or_discard, GL_FALSE };
    for (int i=0; i<n; ++i) {
        tmp.interpolation[i] = interpolation[i];
    }

    for (int i=1; i<c->programs.size; ++i) {
        if (c->programs.a[i].deleted && (GLuint)i != c->cur_program) {
            c->programs.a[i] = tmp;
            return i;
        }
    }

    cvec_push_glProgram(&c->programs, tmp);
    return c->programs.size-1;
}

// Doesn't really do anything except mark for re-use, you
// could still use it even if it wasn't current as long as
// no new program get's assigned to the same spot
PGLDEF void pgl_DeleteProgram(GLuint program)
{
    // This check isn't really necessary since "deleting" only marks it
    // and CreateProgram will never overwrite the 0/default shader
    if (!program)
        return;

    PGL_ERR(program >= c->programs.size, GL_INVALID_VALUE);

    c->programs.a[program].deleted = GL_TRUE;
}

PGLDEF void pgl_UseProgram(GLuint program)
{
    // Not a problem if program is marked "deleted" already
    PGL_ERR(program >= c->programs.size, GL_INVALID_VALUE);

    c->vs_output.size = c->programs.a[program].vs_output_size;
    // c->vs_output.output_buf was pre-allocated to max size needed in init_glContext
    // otherwise would need to assure it's at least
    // c->vs_output_size * PGL_MAX_VERTS * sizeof(float) right here
    c->vs_output.interpolation = c->programs.a[program].interpolation;
    c->fragdepth_or_discard = c->programs.a[program].fragdepth_or_discard;

    c->cur_program = program;
}

PGLDEF void pglSetUniform(void* uniform)
{
    //TODO check for NULL? definitely if I ever switch to storing a local
    //copy in glProgram
    c->programs.a[c->cur_program].uniform = uniform;
}

PGLDEF void pglSetProgramUniform(GLuint program, void* uniform)
{
    // can set uniform for a "deleted" program ... but maybe I should still check and just
    // make an exception if it's the current program?
    PGL_ERR(program >= c->programs.size, GL_INVALID_OPERATION);

    c->programs.a[program].uniform = uniform;
}


PGLDEF void pgl_BlendFunc(GLenum sfactor, GLenum dfactor)
{
    PGL_ERR((sfactor < GL_ZERO || sfactor >= NUM_BLEND_FUNCS || dfactor < GL_ZERO || dfactor >= NUM_BLEND_FUNCS), GL_INVALID_ENUM);

    c->blend_sRGB = sfactor;
    c->blend_sA = sfactor;
    c->blend_dRGB = dfactor;
    c->blend_dA = dfactor;
}

PGLDEF void pgl_BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    PGL_ERR((srcRGB < GL_ZERO || srcRGB >= NUM_BLEND_FUNCS ||
             dstRGB < GL_ZERO || dstRGB >= NUM_BLEND_FUNCS ||
             srcAlpha < GL_ZERO || srcAlpha >= NUM_BLEND_FUNCS ||
             dstAlpha < GL_ZERO || dstAlpha >= NUM_BLEND_FUNCS), GL_INVALID_ENUM);

    c->blend_sRGB = srcRGB;
    c->blend_sA = srcAlpha;
    c->blend_dRGB = dstRGB;
    c->blend_dA = dstAlpha;
}

PGLDEF void pgl_BlendEquation(GLenum mode)
{
    PGL_ERR((mode < GL_FUNC_ADD || mode >= NUM_BLEND_EQUATIONS), GL_INVALID_ENUM);

    c->blend_eqRGB = mode;
    c->blend_eqA = mode;
}

PGLDEF void pgl_BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
    PGL_ERR((modeRGB < GL_FUNC_ADD || modeRGB >= NUM_BLEND_EQUATIONS ||
        modeAlpha < GL_FUNC_ADD || modeAlpha >= NUM_BLEND_EQUATIONS), GL_INVALID_ENUM);

    c->blend_eqRGB = modeRGB;
    c->blend_eqA = modeAlpha;
}

PGLDEF void pgl_BlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    SET_V4(c->blend_color, clamp_01(red), clamp_01(green), clamp_01(blue), clamp_01(alpha));
}

PGLDEF void pgl_LogicOp(GLenum opcode)
{
    PGL_ERR((opcode < GL_CLEAR || opcode > GL_INVERT), GL_INVALID_ENUM);
    c->logic_func = opcode;
}

PGLDEF void pgl_PolygonOffset(GLfloat factor, GLfloat units)
{
    c->poly_factor = factor;
    c->poly_units = units;
}

PGLDEF void pgl_Scissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    PGL_ERR((width < 0 || height < 0), GL_INVALID_VALUE);

    c->scissor_lx = x;
    c->scissor_ly = y;
    c->scissor_w = width;
    c->scissor_h = height;
    int ux = x+width;
    int uy = y+height;

    c->lx = MAX(x, 0);
    c->ly = MAX(y, 0);
    c->ux = MIN(ux, c->back_buffer.w);
    c->uy = MIN(uy, c->back_buffer.h);
}

#ifndef PGL_NO_STENCIL
PGLDEF void pgl_StencilFunc(GLenum func, GLint ref, GLuint mask)
{
    PGL_ERR((func < GL_LESS || func > GL_NEVER), GL_INVALID_ENUM);

    c->stencil_func = func;
    c->stencil_func_back = func;

    // TODO clamp byte function?
    clampi(ref, 0, 255);

    c->stencil_ref = ref;
    c->stencil_ref_back = ref;

    c->stencil_valuemask = mask;
    c->stencil_valuemask_back = mask;
}

PGLDEF void pgl_StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    PGL_ERR((face < GL_FRONT || face > GL_FRONT_AND_BACK), GL_INVALID_ENUM);
    PGL_ERR((func < GL_LESS || func > GL_NEVER), GL_INVALID_ENUM);

    // TODO clamp byte function?
    clampi(ref, 0, 255);

    // Any better way to do this? I don't call pgl_StencilFunc in case
    // I ever want/need debugging/logging info to show the function call
    if (face == GL_FRONT) {
        c->stencil_func = func;
        c->stencil_ref = ref;
        c->stencil_valuemask = mask;
    } else if (face == GL_BACK) {
        c->stencil_func_back = func;
        c->stencil_ref_back = ref;
        c->stencil_valuemask_back = mask;
    } else {
        c->stencil_func = func;
        c->stencil_ref = ref;
        c->stencil_valuemask = mask;

        c->stencil_func_back = func;
        c->stencil_ref_back = ref;
        c->stencil_valuemask_back = mask;
    }
}

PGLDEF void pgl_StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    PGL_ERR((((sfail < GL_INVERT || sfail > GL_DECR_WRAP) && sfail != GL_ZERO) ||
            ((dpfail < GL_INVERT || dpfail > GL_DECR_WRAP) && dpfail != GL_ZERO) ||
            ((dppass < GL_INVERT || dppass > GL_DECR_WRAP) && dppass != GL_ZERO)), GL_INVALID_ENUM);

    c->stencil_sfail = sfail;
    c->stencil_dpfail = dpfail;
    c->stencil_dppass = dppass;

    c->stencil_sfail_back = sfail;
    c->stencil_dpfail_back = dpfail;
    c->stencil_dppass_back = dppass;
}

PGLDEF void pgl_StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)
{
    PGL_ERR((face < GL_FRONT || face > GL_FRONT_AND_BACK), GL_INVALID_ENUM);

    PGL_ERR((((sfail < GL_INVERT || sfail > GL_DECR_WRAP) && sfail != GL_ZERO) ||
            ((dpfail < GL_INVERT || dpfail > GL_DECR_WRAP) && dpfail != GL_ZERO) ||
            ((dppass < GL_INVERT || dppass > GL_DECR_WRAP) && dppass != GL_ZERO)), GL_INVALID_ENUM);


    if (face == GL_FRONT) {
        c->stencil_sfail = sfail;
        c->stencil_dpfail = dpfail;
        c->stencil_dppass = dppass;
    } else if (face == GL_BACK) {
        c->stencil_sfail_back = sfail;
        c->stencil_dpfail_back = dpfail;
        c->stencil_dppass_back = dppass;
    } else {
        c->stencil_sfail = sfail;
        c->stencil_dpfail = dpfail;
        c->stencil_dppass = dppass;

        c->stencil_sfail_back = sfail;
        c->stencil_dpfail_back = dpfail;
        c->stencil_dppass_back = dppass;
    }
}

PGLDEF void pgl_ClearStencil(GLint s)
{
    c->clear_stencil = s & PGL_STENCIL_MASK;
}

PGLDEF void pgl_StencilMask(GLuint mask)
{
    c->stencil_writemask = mask;
    c->stencil_writemask_back = mask;
}

PGLDEF void pgl_StencilMaskSeparate(GLenum face, GLuint mask)
{
    PGL_ERR((face < GL_FRONT || face > GL_FRONT_AND_BACK), GL_INVALID_ENUM);

    if (face == GL_FRONT) {
        c->stencil_writemask = mask;
    } else if (face == GL_BACK) {
        c->stencil_writemask_back = mask;
    } else {
        c->stencil_writemask = mask;
        c->stencil_writemask_back = mask;
    }
}
#endif


// Just wrap my pgl extension getter, unmap does nothing
PGLDEF void* pgl_MapBuffer(GLenum target, GLenum access)
{
    PGL_ERR_RET_VAL((target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER), GL_INVALID_ENUM, NULL);

    PGL_ERR_RET_VAL((access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE), GL_INVALID_ENUM, NULL);

    // adjust to access bound_buffers
    target -= GL_ARRAY_BUFFER;

    void* data = NULL;
    pglGetBufferData(c->bound_buffers[target], &data);
    return data;
}

PGLDEF void* pgl_MapNamedBuffer(GLuint buffer, GLenum access)
{
    // TODO pglGetBufferData will verify buffer is valid, hmm
    PGL_ERR_RET_VAL((access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE), GL_INVALID_ENUM, NULL);

    void* data = NULL;
    pglGetBufferData(buffer, &data);
    return data;
}


#ifndef PGL_EXCLUDE_STUBS

// Stubs to let real OpenGL libs compile with minimal modifications/ifdefs
// add what you need

PGLDEF const GLubyte* pgl_GetStringi(GLenum name, GLuint index) { return NULL; }

PGLDEF void pgl_ColorMaski(GLuint buf, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {}

static int texture_power_of_two(int value)
{
    return value > 0 && !(value & (value - 1));
}

static int texture_max_mip_level(int width, int height)
{
    int level = 0;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++level;
    }
    return level;
}

PGLDEF int pgl_texture_complete(const glTexture* tex, int cube)
{
    int face, level, faces = cube ? PGL_TEXTURE_FACES : 1;
    int max_level, mipmapped;
    const glTextureLevel* base = &tex->levels[0][0];
    if (!base->defined || !base->data || base->w <= 0 || base->h <= 0)
        return 0;
    for (face = 0; face < faces; ++face) {
        const glTextureLevel* l = &tex->levels[face][0];
        if (!l->defined || !l->data || l->w != base->w || l->h != base->h ||
            l->internalformat != base->internalformat)
            return 0;
    }
    mipmapped = tex->min_filter != GL_NEAREST && tex->min_filter != GL_LINEAR;
    if ((!texture_power_of_two(base->w) || !texture_power_of_two(base->h)) &&
        (mipmapped || tex->wrap_s != GL_CLAMP_TO_EDGE || tex->wrap_t != GL_CLAMP_TO_EDGE))
        return 0;
    if (!mipmapped)
        return 1;
    if (!texture_power_of_two(base->w) || !texture_power_of_two(base->h))
        return 0;
    max_level = texture_max_mip_level(base->w, base->h);
    for (level = 1; level <= max_level; ++level) {
        int expected_w = base->w >> level;
        int expected_h = base->h >> level;
        if (!expected_w) expected_w = 1;
        if (!expected_h) expected_h = 1;
        for (face = 0; face < faces; ++face) {
            const glTextureLevel* l = &tex->levels[face][level];
            if (!l->defined || !l->data || l->w != expected_w || l->h != expected_h ||
                l->internalformat != base->internalformat)
                return 0;
        }
    }
    return 1;
}

PGLDEF void pgl_GenerateMipmap(GLenum target)
{
    glTexture* tex;
    glTextureLevel* base;
    int face, level, cube, faces, max_level;
    PGL_ERR(target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP, GL_INVALID_ENUM);
    tex = texture_for_2d_target(target == GL_TEXTURE_CUBE_MAP ?
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X : target, &face, &cube);
    faces = cube ? PGL_TEXTURE_FACES : 1;
    base = &tex->levels[0][0];
    PGL_ERR(!base->defined || !base->data || base->w <= 0 || base->h <= 0,
            GL_INVALID_OPERATION);
    PGL_ERR(!texture_power_of_two(base->w) || !texture_power_of_two(base->h),
            GL_INVALID_OPERATION);
    for (face = 0; face < faces; ++face) {
        const glTextureLevel* l = &tex->levels[face][0];
        PGL_ERR(!l->defined || !l->data || l->w != base->w || l->h != base->h ||
                l->internalformat != base->internalformat, GL_INVALID_OPERATION);
    }
    max_level = texture_max_mip_level(base->w, base->h);
    for (face = 0; face < faces; ++face) {
        for (level = 1; level <= max_level; ++level) {
            glTextureLevel* src = &tex->levels[face][level - 1];
            int dw = src->w > 1 ? src->w / 2 : 1;
            int dh = src->h > 1 ? src->h / 2 : 1;
            size_t size;
            u8* pixels;
            int x, y, channel;
            if (!texture_level_size(dw, dh, &size)) {
                PGL_SET_ERR(GL_OUT_OF_MEMORY);
                return;
            }
            pixels = (u8*)PGL_MALLOC(size ? size : 1u);
            PGL_ERR(!pixels, GL_OUT_OF_MEMORY);
            for (y = 0; y < dh; ++y) {
                int y0 = y * src->h / dh;
                int y1 = (y + 1) * src->h / dh;
                if (y1 <= y0) y1 = y0 + 1;
                for (x = 0; x < dw; ++x) {
                    int x0 = x * src->w / dw;
                    int x1 = (x + 1) * src->w / dw;
                    unsigned sums[4] = { 0, 0, 0, 0 };
                    int sx, sy, count;
                    if (x1 <= x0) x1 = x0 + 1;
                    for (sy = y0; sy < y1; ++sy)
                        for (sx = x0; sx < x1; ++sx)
                            for (channel = 0; channel < 4; ++channel)
                                sums[channel] += src->data[((size_t)sy * src->w + sx) * 4 + channel];
                    count = (x1 - x0) * (y1 - y0);
                    for (channel = 0; channel < 4; ++channel)
                        pixels[((size_t)y * dw + x) * 4 + channel] =
                            (u8)((sums[channel] + (unsigned)count / 2u) / (unsigned)count);
                }
            }
            install_texture_level(tex, face, level, base->internalformat,
                                  dw, dh, pixels, cube);
        }
    }
}

PGLDEF void pgl_GetDoublev(GLenum pname, GLdouble* params) { }
PGLDEF void pgl_GetInteger64v(GLenum pname, GLint64* params) { }

// Drawbuffers
PGLDEF void pgl_DrawBuffers(GLsizei n, const GLenum* bufs) {}
PGLDEF void pgl_NamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum* bufs) {}

// Framebuffers/Renderbuffers
PGLDEF void pgl_GenFramebuffers(GLsizei n, GLuint* ids) {}
PGLDEF void pgl_BindFramebuffer(GLenum target, GLuint framebuffer) {}
PGLDEF void pgl_DeleteFramebuffers(GLsizei n, GLuint* framebuffers) {}
PGLDEF void pgl_FramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {}
PGLDEF void pgl_FramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {}
PGLDEF void pgl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {}
PGLDEF void pgl_FramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint layer) {}
PGLDEF GLboolean pgl_IsFramebuffer(GLuint framebuffer) { return GL_FALSE; }

PGLDEF void pgl_FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {}
PGLDEF void pgl_NamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer) {}

PGLDEF void pgl_ReadBuffer(GLenum mode) {}
PGLDEF void pgl_NamedFramebufferReadBuffer(GLuint framebuffer, GLenum mode) {}

PGLDEF void pgl_BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {}
PGLDEF void pgl_BlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {}

PGLDEF void pgl_GenRenderbuffers(GLsizei n, GLuint* renderbuffers) {}
PGLDEF void pgl_BindRenderbuffer(GLenum target, GLuint renderbuffer) {}
PGLDEF void pgl_DeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {}
PGLDEF void pgl_RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {}
PGLDEF GLboolean pgl_IsRenderbuffer(GLuint renderbuffer) { return GL_FALSE; }

PGLDEF void pgl_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {}
PGLDEF void pgl_NamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {}

PGLDEF void pgl_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {}
// Could also return GL_FRAMEBUFFER_UNDEFINED, but then I'd have to add all
// those enums and really 0 signaling an error makes more sense
PGLDEF GLenum pgl_CheckFramebufferStatus(GLenum target) { return 0; }

PGLDEF void pgl_ClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {}
PGLDEF void pgl_ClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {}
PGLDEF void pgl_ClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {}
PGLDEF void pgl_ClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {}
PGLDEF void pgl_ClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint* value) {}
PGLDEF void pgl_ClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint* value) {}
PGLDEF void pgl_ClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat* value) {}
PGLDEF void pgl_ClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {}

PGLDEF void pgl_GetProgramiv(GLuint program, GLenum pname, GLint* params) { }
PGLDEF void pgl_GetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog) { }
PGLDEF void pgl_AttachShader(GLuint program, GLuint shader) { }
PGLDEF void pgl_CompileShader(GLuint shader) { }
PGLDEF void pgl_GetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog) { }
PGLDEF void pgl_LinkProgram(GLuint program) { }
PGLDEF void pgl_ShaderSource(GLuint shader, GLsizei count, const GLchar** string, const GLint* length) { }
PGLDEF void pgl_GetShaderiv(GLuint shader, GLenum pname, GLint* params) { }
PGLDEF void pgl_DeleteShader(GLuint shader) { }
PGLDEF void pgl_DetachShader(GLuint program, GLuint shader) { }

PGLDEF GLuint pgl_CreateProgram(void) { return 0; }
PGLDEF GLuint pgl_CreateShader(GLenum shaderType) { return 0; }
PGLDEF GLint pgl_GetUniformLocation(GLuint program, const GLchar* name) { return 0; }
PGLDEF GLint pgl_GetAttribLocation(GLuint program, const GLchar* name) { return 0; }

PGLDEF GLboolean pgl_UnmapBuffer(GLenum target) { return GL_TRUE; }
PGLDEF GLboolean pgl_UnmapNamedBuffer(GLuint buffer) { return GL_TRUE; }

// TODO?

PGLDEF void pgl_ActiveTexture(GLenum texture) { }
PGLDEF void pgl_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    pgl_TexParameteri(target, pname, (GLint)param);
}

PGLDEF void pgl_TextureParameterf(GLuint texture, GLenum pname, GLfloat param) {}

// TODO what the heck are these?
PGLDEF void pgl_TexParameterliv(GLenum target, GLenum pname, const GLint* params) {}
PGLDEF void pgl_TexParameterluiv(GLenum target, GLenum pname, const GLuint* params) {}

PGLDEF void pgl_TextureParameterliv(GLuint texture, GLenum pname, const GLint* params) {}
PGLDEF void pgl_TextureParameterluiv(GLuint texture, GLenum pname, const GLuint* params) {}

PGLDEF void pgl_CompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid* data) {}
/* ETC decoding is derived from Mesa's texcompress_etc.c.
 * Copyright (C) 2011 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
typedef struct pglEtcBlock {
    u8 base[3][3];
    u8 paint[4][3];
    const int* modifiers[2];
    u32 indices;
    u64 alpha_indices;
    int distance;
    int flipped;
    int opaque;
    int individual;
    int differential;
    int t_mode;
    int h_mode;
    int planar;
} pglEtcBlock;

static const int pgl_etc1_modifiers[8][4] = {
    { 2, 8, -2, -8 }, { 5, 17, -5, -17 },
    { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
    { 18, 60, -18, -60 }, { 24, 80, -24, -80 },
    { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
};

static const int pgl_etc2_distances[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

static const int pgl_etc2_alpha_modifiers[16][8] = {
    { -3, -6, -9, -15, 2, 5, 8, 14 },
    { -3, -7, -10, -13, 2, 6, 9, 12 },
    { -2, -5, -8, -13, 1, 4, 7, 12 },
    { -2, -4, -6, -13, 1, 3, 5, 12 },
    { -3, -6, -8, -12, 2, 5, 7, 11 },
    { -3, -7, -9, -11, 2, 6, 8, 10 },
    { -4, -7, -8, -11, 3, 6, 7, 10 },
    { -3, -5, -8, -11, 2, 4, 7, 10 },
    { -2, -6, -8, -10, 1, 5, 7, 9 },
    { -2, -5, -8, -10, 1, 4, 7, 9 },
    { -2, -4, -8, -10, 1, 3, 7, 9 },
    { -2, -5, -7, -10, 1, 4, 6, 9 },
    { -3, -4, -7, -10, 2, 3, 6, 9 },
    { -1, -2, -3, -10, 0, 1, 2, 9 },
    { -4, -6, -8, -9, 3, 5, 7, 8 },
    { -3, -5, -7, -9, 2, 4, 6, 8 }
};

static const int pgl_etc2_punch_modifiers[8][4] = {
    { 0, 8, 0, -8 }, { 0, 17, 0, -17 },
    { 0, 29, 0, -29 }, { 0, 42, 0, -42 },
    { 0, 60, 0, -60 }, { 0, 80, 0, -80 },
    { 0, 106, 0, -106 }, { 0, 183, 0, -183 }
};

static u8 pgl_etc_clamp(int value)
{
    return (u8)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static u8 pgl_etc_diff_hi(u8 value)
{
    return (u8)((value & 0xf8) | (value >> 5));
}

static u8 pgl_etc_diff_lo(u8 value)
{
    static const int delta[8] = { 0, 1, 2, 3, -4, -3, -2, -1 };
    int v = (value >> 3) + delta[value & 7];
    return (u8)((v << 3) | (v >> 2));
}

static u8 pgl_etc_ind_hi(u8 value)
{
    return (u8)((value & 0xf0) | ((value & 0xf0) >> 4));
}

static u8 pgl_etc_ind_lo(u8 value)
{
    return (u8)(((value & 0xf) << 4) | (value & 0xf));
}

static u8 pgl_etc_extend4(unsigned value)
{
    return (u8)((value << 4) | value);
}

static void pgl_etc1_decode_block(const u8* src, u8 out[64])
{
    u8 base[2][3];
    u32 indices = ((u32)src[4] << 24) | ((u32)src[5] << 16) |
                  ((u32)src[6] << 8) | src[7];
    int channel, x, y;
    for (channel = 0; channel < 3; ++channel) {
        if (src[3] & 2) {
            base[0][channel] = pgl_etc_diff_hi(src[channel]);
            base[1][channel] = pgl_etc_diff_lo(src[channel]);
        } else {
            base[0][channel] = pgl_etc_ind_hi(src[channel]);
            base[1][channel] = pgl_etc_ind_lo(src[channel]);
        }
    }
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            int bit = y + x * 4;
            int index = (int)(((indices >> (15 + bit)) & 2) |
                              ((indices >> bit) & 1));
            int subblock = (src[3] & 1) ? y >= 2 : x >= 2;
            int table = subblock ? (src[3] >> 2) & 7 : (src[3] >> 5) & 7;
            int modifier = pgl_etc1_modifiers[table][index];
            u8* dst = out + (y * 4 + x) * 4;
            dst[0] = pgl_etc_clamp(base[subblock][0] + modifier);
            dst[1] = pgl_etc_clamp(base[subblock][1] + modifier);
            dst[2] = pgl_etc_clamp(base[subblock][2] + modifier);
            dst[3] = 255;
        }
    }
}

static u8 pgl_etc2_t_base1(const u8* src, int channel)
{
    unsigned value;
    if (channel == 0)
        value = (((src[0] >> 3) & 3) << 2) | (src[0] & 3);
    else if (channel == 1)
        value = (src[1] >> 4) & 15;
    else
        value = src[1] & 15;
    return pgl_etc_extend4(value);
}

static u8 pgl_etc2_t_base2(const u8* src, int channel)
{
    unsigned value = channel == 0 ? (src[2] >> 4) & 15 :
                     channel == 1 ? src[2] & 15 : (src[3] >> 4) & 15;
    return pgl_etc_extend4(value);
}

static u8 pgl_etc2_h_base1(const u8* src, int channel)
{
    unsigned value;
    if (channel == 0)
        value = (src[0] >> 3) & 15;
    else if (channel == 1)
        value = ((src[0] & 7) << 1) | ((src[1] >> 4) & 1);
    else
        value = (src[1] & 8) | ((src[1] & 3) << 1) | ((src[2] >> 7) & 1);
    return pgl_etc_extend4(value);
}

static u8 pgl_etc2_h_base2(const u8* src, int channel)
{
    unsigned value = channel == 0 ? (src[2] >> 3) & 15 :
                     channel == 1 ? ((src[2] & 7) << 1) | ((src[3] >> 7) & 1) :
                                    (src[3] >> 3) & 15;
    return pgl_etc_extend4(value);
}

static u8 pgl_etc2_planar_o(const u8* src, int channel)
{
    unsigned value;
    if (channel == 0) {
        value = (src[0] >> 1) & 0x3f;
        return (u8)((value << 2) | (value >> 4));
    }
    if (channel == 1) {
        value = ((src[0] & 1) << 6) | ((src[1] >> 1) & 0x3f);
        return (u8)((value << 1) | (value >> 6));
    }
    value = ((src[1] & 1) << 5) | (src[2] & 0x18) |
            ((src[2] & 3) << 1) | ((src[3] >> 7) & 1);
    return (u8)((value << 2) | (value >> 4));
}

static u8 pgl_etc2_planar_h(const u8* src, int channel)
{
    unsigned value;
    if (channel == 0) {
        value = ((src[3] & 0x7c) >> 1) | (src[3] & 1);
        return (u8)((value << 2) | (value >> 4));
    }
    if (channel == 1) {
        value = (src[4] >> 1) & 0x7f;
        return (u8)((value << 1) | (value >> 6));
    }
    value = ((src[4] & 1) << 5) | ((src[5] >> 3) & 0x1f);
    return (u8)((value << 2) | (value >> 4));
}

static u8 pgl_etc2_planar_v(const u8* src, int channel)
{
    unsigned value;
    if (channel == 0) {
        value = ((src[5] & 7) << 3) | ((src[6] >> 5) & 7);
        return (u8)((value << 2) | (value >> 4));
    }
    if (channel == 1) {
        value = ((src[6] & 0x1f) << 2) | ((src[7] >> 6) & 3);
        return (u8)((value << 1) | (value >> 6));
    }
    value = src[7] & 0x3f;
    return (u8)((value << 2) | (value >> 4));
}

static void pgl_etc2_parse_block(pglEtcBlock* block, const u8* src, int punchthrough)
{
    static const int delta[8] = { 0, 1, 2, 3, -4, -3, -2, -1 };
    int r = (src[0] >> 3) + delta[src[0] & 7];
    int g = (src[1] >> 3) + delta[src[1] & 7];
    int b = (src[2] >> 3) + delta[src[2] & 7];
    int diffbit = punchthrough ? 0 : src[3] & 2;
    int channel;
    memset(block, 0, sizeof(*block));
    block->opaque = punchthrough ? (src[3] & 2) != 0 : 1;
    if (!diffbit && !punchthrough) {
        block->individual = 1;
        for (channel = 0; channel < 3; ++channel) {
            block->base[0][channel] = pgl_etc_ind_hi(src[channel]);
            block->base[1][channel] = pgl_etc_ind_lo(src[channel]);
        }
    } else if (r < 0 || r > 31) {
        block->t_mode = 1;
        for (channel = 0; channel < 3; ++channel) {
            block->base[0][channel] = pgl_etc2_t_base1(src, channel);
            block->base[1][channel] = pgl_etc2_t_base2(src, channel);
        }
        block->distance = pgl_etc2_distances[(((src[3] >> 2) & 3) << 1) |
                                             (src[3] & 1)];
        for (channel = 0; channel < 3; ++channel) {
            block->paint[0][channel] = block->base[0][channel];
            block->paint[1][channel] = pgl_etc_clamp(block->base[1][channel] + block->distance);
            block->paint[2][channel] = block->base[1][channel];
            block->paint[3][channel] = pgl_etc_clamp(block->base[1][channel] - block->distance);
        }
    } else if (g < 0 || g > 31) {
        unsigned value1, value2;
        block->h_mode = 1;
        for (channel = 0; channel < 3; ++channel) {
            block->base[0][channel] = pgl_etc2_h_base1(src, channel);
            block->base[1][channel] = pgl_etc2_h_base2(src, channel);
        }
        value1 = ((unsigned)block->base[0][0] << 16) |
                 ((unsigned)block->base[0][1] << 8) | block->base[0][2];
        value2 = ((unsigned)block->base[1][0] << 16) |
                 ((unsigned)block->base[1][1] << 8) | block->base[1][2];
        block->distance = pgl_etc2_distances[(src[3] & 4) |
                                             ((src[3] & 1) << 1) |
                                             (value1 >= value2)];
        for (channel = 0; channel < 3; ++channel) {
            block->paint[0][channel] = pgl_etc_clamp(block->base[0][channel] + block->distance);
            block->paint[1][channel] = pgl_etc_clamp(block->base[0][channel] - block->distance);
            block->paint[2][channel] = pgl_etc_clamp(block->base[1][channel] + block->distance);
            block->paint[3][channel] = pgl_etc_clamp(block->base[1][channel] - block->distance);
        }
    } else if (b < 0 || b > 31) {
        block->planar = 1;
        block->opaque = 1;
        for (channel = 0; channel < 3; ++channel) {
            block->base[0][channel] = pgl_etc2_planar_o(src, channel);
            block->base[1][channel] = pgl_etc2_planar_h(src, channel);
            block->base[2][channel] = pgl_etc2_planar_v(src, channel);
        }
    } else {
        block->differential = 1;
        for (channel = 0; channel < 3; ++channel) {
            block->base[0][channel] = pgl_etc_diff_hi(src[channel]);
            block->base[1][channel] = pgl_etc_diff_lo(src[channel]);
        }
    }
    if (block->individual || block->differential) {
        int table0 = (src[3] >> 5) & 7;
        int table1 = (src[3] >> 2) & 7;
        block->modifiers[0] = (!punchthrough || block->opaque) ?
                              pgl_etc1_modifiers[table0] : pgl_etc2_punch_modifiers[table0];
        block->modifiers[1] = (!punchthrough || block->opaque) ?
                              pgl_etc1_modifiers[table1] : pgl_etc2_punch_modifiers[table1];
        block->flipped = src[3] & 1;
    }
    block->indices = ((u32)src[4] << 24) | ((u32)src[5] << 16) |
                     ((u32)src[6] << 8) | src[7];
}

static void pgl_etc2_decode_block(const u8* src, int punchthrough, u8 out[64])
{
    pglEtcBlock block;
    int x, y;
    pgl_etc2_parse_block(&block, src, punchthrough);
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            int bit = y + x * 4;
            int index = (int)(((block.indices >> (15 + bit)) & 2) |
                              ((block.indices >> bit) & 1));
            u8* dst = out + (y * 4 + x) * 4;
            dst[3] = 255;
            if (punchthrough && !block.opaque && index == 2 && !block.planar) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
            } else if (block.individual || block.differential) {
                int subblock = block.flipped ? y >= 2 : x >= 2;
                int modifier = block.modifiers[subblock][index];
                dst[0] = pgl_etc_clamp(block.base[subblock][0] + modifier);
                dst[1] = pgl_etc_clamp(block.base[subblock][1] + modifier);
                dst[2] = pgl_etc_clamp(block.base[subblock][2] + modifier);
            } else if (block.t_mode || block.h_mode) {
                dst[0] = block.paint[index][0];
                dst[1] = block.paint[index][1];
                dst[2] = block.paint[index][2];
            } else {
                int channel;
                for (channel = 0; channel < 3; ++channel) {
                    int value = (x * (block.base[1][channel] - block.base[0][channel]) +
                                 y * (block.base[2][channel] - block.base[0][channel]) +
                                 4 * block.base[0][channel] + 2) >> 2;
                    dst[channel] = pgl_etc_clamp(value);
                }
            }
        }
    }
}

static void pgl_etc2_decode_rgba_block(const u8* src, u8 out[64])
{
    u64 indices = ((u64)src[2] << 40) | ((u64)src[3] << 32) |
                  ((u64)src[4] << 24) | ((u64)src[5] << 16) |
                  ((u64)src[6] << 8) | src[7];
    int base = src[0], multiplier = (src[1] >> 4) & 15, table = src[1] & 15;
    int x, y;
    pgl_etc2_decode_block(src + 8, 0, out);
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            int bit = ((3 - y) + (3 - x) * 4) * 3;
            int index = (int)((indices >> bit) & 7);
            int alpha = base + pgl_etc2_alpha_modifiers[table][index] * multiplier;
            out[(y * 4 + x) * 4 + 3] = pgl_etc_clamp(alpha);
        }
    }
}

static int pgl_etc_format_info(GLenum format, int* block_bytes)
{
    switch (format) {
    case GL_ETC1_RGB8_OES:
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        *block_bytes = 8;
        return 1;
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
        *block_bytes = 16;
        return 1;
    default:
        return 0;
    }
}

static int pgl_etc_image_size(GLsizei width, GLsizei height, int block_bytes,
                              GLsizei* image_size)
{
    size_t blocks_w = ((size_t)width + 3u) / 4u;
    size_t blocks_h = ((size_t)height + 3u) / 4u;
    size_t size = blocks_w * blocks_h * (size_t)block_bytes;
    if (width < 0 || height < 0 || size > 0x7fffffffu)
        return 0;
    *image_size = (GLsizei)size;
    return 1;
}

static void pgl_etc_decode(u8* out, int out_pitch, const u8* src,
                           GLsizei width, GLsizei height, GLenum format)
{
    int block_bytes = 0;
    int punchthrough = format == GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2;
    int bx, by, x, y;
    if (!pgl_etc_format_info(format, &block_bytes))
        return;
    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            u8 decoded[64];
            const u8* block = src + ((size_t)(by / 4) * ((size_t)(width + 3) / 4u) +
                                     (size_t)(bx / 4)) * (size_t)block_bytes;
            if (format == GL_ETC1_RGB8_OES)
                pgl_etc1_decode_block(block, decoded);
            else if (format == GL_COMPRESSED_RGBA8_ETC2_EAC)
                pgl_etc2_decode_rgba_block(block, decoded);
            else
                pgl_etc2_decode_block(block, punchthrough, decoded);
            for (y = 0; y < 4 && by + y < height; ++y)
                for (x = 0; x < 4 && bx + x < width; ++x)
                    memcpy(out + (size_t)(by + y) * (size_t)out_pitch + (bx + x) * 4,
                           decoded + (y * 4 + x) * 4, 4);
        }
    }
}

PGLDEF void pgl_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data)
{
    glTexture* tex;
    u8* pixels;
    size_t size;
    GLsizei expected;
    int face, cube, block_bytes;
    PGL_ERR((target != GL_TEXTURE_2D &&
             (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
              target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)), GL_INVALID_ENUM);
    PGL_ERR(!pgl_etc_format_info(internalformat, &block_bytes), GL_INVALID_ENUM);
    PGL_ERR((level < 0 || level >= PGL_MAX_MIP_LEVELS || border != 0 ||
             width < 0 || height < 0 || width > PGL_MAX_TEXTURE_SIZE ||
             height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    cube = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    PGL_ERR(cube && width != height, GL_INVALID_VALUE);
    PGL_ERR(!pgl_etc_image_size(width, height, block_bytes, &expected) ||
            imageSize != expected, GL_INVALID_VALUE);
    PGL_ERR(imageSize > 0 && !data, GL_INVALID_VALUE);
    PGL_ERR(!texture_level_size(width, height, &size), GL_OUT_OF_MEMORY);
    pixels = (u8*)PGL_MALLOC(size ? size : 1u);
    PGL_ERR(!pixels, GL_OUT_OF_MEMORY);
    if (width && height)
        pgl_etc_decode(pixels, width * 4, (const u8*)data, width, height, internalformat);
    tex = texture_for_2d_target(target, &face, &cube);
    install_texture_level(tex, face, level, internalformat, width, height, pixels, cube);
}

PGLDEF void pgl_CompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid* data)
{
    glTexture* tex;
    glTextureLevel* l;
    u8* pixels;
    size_t size;
    GLsizei expected;
    int face, cube, block_bytes, x, y;
    PGL_ERR((target != GL_TEXTURE_2D &&
             (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
              target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)), GL_INVALID_ENUM);
    PGL_ERR(!pgl_etc_format_info(format, &block_bytes), GL_INVALID_ENUM);
    PGL_ERR(format == GL_ETC1_RGB8_OES, GL_INVALID_OPERATION);
    PGL_ERR((level < 0 || level >= PGL_MAX_MIP_LEVELS || width < 0 || height < 0),
            GL_INVALID_VALUE);
    tex = texture_for_2d_target(target, &face, &cube);
    l = &tex->levels[face][level];
    PGL_ERR(!l->defined || l->internalformat != format, GL_INVALID_OPERATION);
    PGL_ERR((xoffset < 0 || yoffset < 0 || (xoffset & 3) || (yoffset & 3) ||
             xoffset > l->w - width || yoffset > l->h - height), GL_INVALID_VALUE);
    PGL_ERR(((width & 3) && xoffset + width != l->w) ||
            ((height & 3) && yoffset + height != l->h), GL_INVALID_VALUE);
    PGL_ERR(!pgl_etc_image_size(width, height, block_bytes, &expected) ||
            imageSize != expected, GL_INVALID_VALUE);
    PGL_ERR(imageSize > 0 && !data, GL_INVALID_VALUE);
    if (!width || !height)
        return;
    PGL_ERR(!texture_level_size(width, height, &size), GL_OUT_OF_MEMORY);
    pixels = (u8*)PGL_MALLOC(size);
    PGL_ERR(!pixels, GL_OUT_OF_MEMORY);
    pgl_etc_decode(pixels, width * 4, (const u8*)data, width, height, format);
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            memcpy(l->data + ((size_t)(yoffset + y) * (size_t)l->w + xoffset + x) * 4u,
                   pixels + ((size_t)y * (size_t)width + x) * 4u, 4);
    PGL_FREE(pixels);
    l->datagen = ++tex->datagen;
}

static void pgl_copy_texel(u8* dst, GLint x, GLint y, GLenum internalformat)
{
    u32 pixel = 0;
    int64_t sx = x, sy = (int64_t)c->back_buffer.h - 1 - (int64_t)y;
    unsigned r, g, b, a;
    if (sx >= 0 && sx < c->back_buffer.w && sy >= 0 && sy < c->back_buffer.h)
        pixel = ((const u32*)c->back_buffer.buf)[(size_t)sy * (size_t)c->back_buffer.w + (size_t)sx];
    r = (pixel >> 16) & 255u;
    g = (pixel >> 8) & 255u;
    b = pixel & 255u;
    a = (pixel >> 24) & 255u;
    if (internalformat == GL_ALPHA) {
        r = g = b = 0;
    } else if (internalformat == GL_LUMINANCE || internalformat == GL_LUMINANCE_ALPHA) {
        g = b = r;
        if (internalformat == GL_LUMINANCE) a = 255;
    } else if (internalformat == GL_RGB) {
        a = 255;
    }
    dst[0] = (u8)r;
    dst[1] = (u8)g;
    dst[2] = (u8)b;
    dst[3] = (u8)a;
}

PGLDEF void pgl_CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    glTexture* tex;
    u8* pixels;
    size_t size;
    int face, cube, col, row;
    PGL_ERR((target != GL_TEXTURE_2D &&
             (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
              target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)), GL_INVALID_ENUM);
    PGL_ERR((internalformat != GL_ALPHA && internalformat != GL_LUMINANCE &&
             internalformat != GL_LUMINANCE_ALPHA && internalformat != GL_RGB &&
             internalformat != GL_RGBA), GL_INVALID_ENUM);
    PGL_ERR((level < 0 || level >= PGL_MAX_MIP_LEVELS || border != 0 ||
             width < 0 || height < 0 || width > PGL_MAX_TEXTURE_SIZE ||
             height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    cube = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    PGL_ERR(cube && width != height, GL_INVALID_VALUE);
    PGL_ERR(!texture_level_size(width, height, &size), GL_OUT_OF_MEMORY);
    pixels = (u8*)PGL_MALLOC(size ? size : 1u);
    PGL_ERR(!pixels, GL_OUT_OF_MEMORY);
    for (row = 0; row < height; ++row)
        for (col = 0; col < width; ++col)
            pgl_copy_texel(pixels + ((size_t)row * (size_t)width + col) * 4u,
                           x + col, y + row, internalformat);
    tex = texture_for_2d_target(target, &face, &cube);
    install_texture_level(tex, face, level, internalformat, width, height, pixels, cube);
}

PGLDEF void pgl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    glTexture* tex;
    glTextureLevel* l;
    int face, cube, col, row;
    PGL_ERR((target != GL_TEXTURE_2D &&
             (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
              target > GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)), GL_INVALID_ENUM);
    PGL_ERR((level < 0 || level >= PGL_MAX_MIP_LEVELS || width < 0 || height < 0),
            GL_INVALID_VALUE);
    tex = texture_for_2d_target(target, &face, &cube);
    l = &tex->levels[face][level];
    PGL_ERR(!l->defined, GL_INVALID_OPERATION);
    PGL_ERR((xoffset < 0 || yoffset < 0 || xoffset > l->w - width ||
             yoffset > l->h - height), GL_INVALID_VALUE);
    for (row = 0; row < height; ++row)
        for (col = 0; col < width; ++col)
            pgl_copy_texel(l->data + ((size_t)(yoffset + row) * (size_t)l->w + xoffset + col) * 4u,
                           x + col, y + row, l->internalformat);
    l->datagen = ++tex->datagen;
}
PGLDEF void pgl_CompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid* data) {}

PGLDEF void pgl_TexBuffer(GLenum target, GLenum internalformat, GLuint buffer) { }
PGLDEF void pgl_TextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer) { }


PGLDEF void pgl_Uniform1f(GLint location, GLfloat v0) { }
PGLDEF void pgl_Uniform2f(GLint location, GLfloat v0, GLfloat v1) { }
PGLDEF void pgl_Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { }
PGLDEF void pgl_Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { }

PGLDEF void pgl_Uniform1i(GLint location, GLint v0) { }
PGLDEF void pgl_Uniform2i(GLint location, GLint v0, GLint v1) { }
PGLDEF void pgl_Uniform3i(GLint location, GLint v0, GLint v1, GLint v2) { }
PGLDEF void pgl_Uniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { }

PGLDEF void pgl_Uniform1ui(GLint location, GLuint v0) { }
PGLDEF void pgl_Uniform2ui(GLint location, GLuint v0, GLuint v1) { }
PGLDEF void pgl_Uniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) { }
PGLDEF void pgl_Uniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { }

PGLDEF void pgl_Uniform1fv(GLint location, GLsizei count, const GLfloat* value) { }
PGLDEF void pgl_Uniform2fv(GLint location, GLsizei count, const GLfloat* value) { }
PGLDEF void pgl_Uniform3fv(GLint location, GLsizei count, const GLfloat* value) { }
PGLDEF void pgl_Uniform4fv(GLint location, GLsizei count, const GLfloat* value) { }

PGLDEF void pgl_Uniform1iv(GLint location, GLsizei count, const GLint* value) { }
PGLDEF void pgl_Uniform2iv(GLint location, GLsizei count, const GLint* value) { }
PGLDEF void pgl_Uniform3iv(GLint location, GLsizei count, const GLint* value) { }
PGLDEF void pgl_Uniform4iv(GLint location, GLsizei count, const GLint* value) { }

PGLDEF void pgl_Uniform1uiv(GLint location, GLsizei count, const GLuint* value) { }
PGLDEF void pgl_Uniform2uiv(GLint location, GLsizei count, const GLuint* value) { }
PGLDEF void pgl_Uniform3uiv(GLint location, GLsizei count, const GLuint* value) { }
PGLDEF void pgl_Uniform4uiv(GLint location, GLsizei count, const GLuint* value) { }

PGLDEF void pgl_UniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void pgl_UniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }

#endif


/*************************************
 *  GLSL(ish) functions
 *************************************/

/*
float clampf_01(float f)
{
    if (f < 0.0f) return 0.0f;
    if (f > 1.0f) return 1.0f;
    return f;
}

float clampf(float f, float min, float max)
{
    if (f < min) return min;
    if (f > max) return max;
    return f;
}

int clampi(int i, int min, int max)
{
    if (i < min) return min;
    if (i > max) return max;
    return i;
}
*/


// TODO maybe I should put this in crsw_math/rsw_math? static inline?
// guarantees positive mod result
#define positive_mod(a, b) (((a) % (b) + (b)) % (b))

// if I only wanted to support power of 2 textures...
#define positive_mod_pow_of_2(i, n) ((i) & ((n) - 1) + (n)) & ((n) - 1)

// TODO should this be in rsw_math
#define mirror(i) (i) >= 0 ? (i) : -(1 + (i))

// See page 174 of GL 3.3 core spec.
static int wrap(int i, int size, GLenum mode)
{
    switch (mode)
    {
    case GL_REPEAT:
        return positive_mod(i, size);

    // Border is too much of a pain to implement with render to
    // texture.  Trade offs in poor performance or ugly extra code
    // for a feature that almost no one actually uses and even
    // when it is used (barring rare/odd uv coordinates) it's not
    // even noticable.
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    case GL_CLAMP_TO_BORDER:
        if (i >= 0 && i < size) return i;
        return -1;
        // Would use if we went back to literally surrounding textures with a border
        //return clampi(i, -1, size);
#else
    case GL_CLAMP_TO_BORDER:  // just so stuff that uses it compiles
#endif
    case GL_CLAMP_TO_EDGE:
        return clampi(i, 0, size-1);

    case GL_MIRRORED_REPEAT: {
        int sz2 = 2*size;
        i = positive_mod(i, sz2);
        i -= size;
        i = mirror(i);
        i = size - 1 - i;
        return i;
    } break;
    default:
        //should never happen, get rid of compile warning
        assert(0);
        return 0;
    }
}
#undef imod
#undef positive_mod
#undef positive_mod_pow_of_2


// hmm should I have these take a glTexture* somehow?
// It would save the check for 0 for every single access

// used in the following 4 texture access functions
// Not sure if it's actually necessary since wrap() clamps
#define EPSILON 0.000001
PGLDEF vec4 texture1D(GLuint tex, float x)
{
    int i0, i1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_1D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    float w = t->w - EPSILON;

    float xw = x * w;

    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), t->w, t->wrap_s);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if (i0 < 0) return t->border_color;
#endif

        return Color_to_v4(texdata[i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), t->w, t->wrap_s);
        i1 = wrap(floorf(xw + 0.499999f), t->w, t->wrap_s);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        if (alpha < 0) ++alpha;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 ci, ci1;
        if (i0 < 0) ci = t->border_color;
        else ci = Color_to_v4(texdata[i0]);

        if (i1 < 0) ci1 = t->border_color;
        else ci1 = Color_to_v4(texdata[i1]);
#else
        vec4 ci = Color_to_v4(texdata[i0]);
        vec4 ci1 = Color_to_v4(texdata[i1]);
#endif

        ci = scale_v4(ci, (1-alpha));
        ci1 = scale_v4(ci1, alpha);

        ci = add_v4s(ci, ci1);

        return ci;
    }
}

static vec4 texture_sample_level(const glTexture* tex, int face, int level,
                                 float x, float y, GLenum filter)
{
    const glTextureLevel* l = &tex->levels[face][level];
    const Color* pixels = (const Color*)l->data;
    int i0, j0, i1, j1;
    float u = x * l->w;
    float v = y * l->h;
    if (filter == GL_NEAREST) {
        i0 = wrap((int)floorf(u), l->w, tex->wrap_s);
        j0 = wrap((int)floorf(v), l->h, tex->wrap_t);
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0) < 0) return tex->border_color;
#endif
        return Color_to_v4(pixels[j0 * l->w + i0]);
    }

    u -= 0.5f;
    v -= 0.5f;
    i0 = (int)floorf(u);
    j0 = (int)floorf(v);
    i1 = wrap(i0 + 1, l->w, tex->wrap_s);
    j1 = wrap(j0 + 1, l->h, tex->wrap_t);
    {
        float alpha = u - floorf(u);
        float beta = v - floorf(v);
        vec4 c00, c10, c01, c11;
        i0 = wrap(i0, l->w, tex->wrap_s);
        j0 = wrap(j0, l->h, tex->wrap_t);
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        c00 = ((i0 | j0) < 0) ? tex->border_color : Color_to_v4(pixels[j0 * l->w + i0]);
        c10 = ((i1 | j0) < 0) ? tex->border_color : Color_to_v4(pixels[j0 * l->w + i1]);
        c01 = ((i0 | j1) < 0) ? tex->border_color : Color_to_v4(pixels[j1 * l->w + i0]);
        c11 = ((i1 | j1) < 0) ? tex->border_color : Color_to_v4(pixels[j1 * l->w + i1]);
#else
        c00 = Color_to_v4(pixels[j0 * l->w + i0]);
        c10 = Color_to_v4(pixels[j0 * l->w + i1]);
        c01 = Color_to_v4(pixels[j1 * l->w + i0]);
        c11 = Color_to_v4(pixels[j1 * l->w + i1]);
#endif
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha * alpha * (3.0f - 2.0f * alpha);
        beta = beta * beta * (3.0f - 2.0f * beta);
#endif
        c00 = scale_v4(c00, (1.0f - alpha) * (1.0f - beta));
        c10 = scale_v4(c10, alpha * (1.0f - beta));
        c01 = scale_v4(c01, (1.0f - alpha) * beta);
        c11 = scale_v4(c11, alpha * beta);
        return add_v4s(add_v4s(add_v4s(c00, c10), c01), c11);
    }
}

static vec4 texture_sample_with_lod(const glTexture* tex, int face, int cube,
                                    float x, float y, float lod)
{
    int max_level, level0, level1;
    float fraction;
    GLenum filter;
    vec4 a, b;
    if (!pgl_texture_complete(tex, cube))
        return make_v4(0.0f, 0.0f, 0.0f, 1.0f);
    max_level = texture_max_mip_level(tex->levels[face][0].w, tex->levels[face][0].h);
    if (lod < 0.0f) lod = 0.0f;
    if (lod > (float)max_level) lod = (float)max_level;
    if (lod == 0.0f)
        return texture_sample_level(tex, face, 0, x, y, tex->mag_filter);

    if (tex->min_filter == GL_NEAREST || tex->min_filter == GL_LINEAR)
        return texture_sample_level(tex, face, 0, x, y, tex->min_filter);

    filter = (tex->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
              tex->min_filter == GL_NEAREST_MIPMAP_LINEAR) ? GL_NEAREST : GL_LINEAR;
    if (tex->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
        tex->min_filter == GL_LINEAR_MIPMAP_NEAREST) {
        level0 = (int)floorf(lod + 0.5f);
        if (level0 > max_level) level0 = max_level;
        return texture_sample_level(tex, face, level0, x, y, filter);
    }

    level0 = (int)floorf(lod);
    level1 = level0 + 1;
    if (level1 > max_level) level1 = max_level;
    fraction = lod - floorf(lod);
    a = texture_sample_level(tex, face, level0, x, y, filter);
    if (level0 == level1) return a;
    b = texture_sample_level(tex, face, level1, x, y, filter);
    return add_v4s(scale_v4(a, 1.0f - fraction), scale_v4(b, fraction));
}

static glTexture* texture2d_object(GLuint tex)
{
    return tex ? &c->textures.a[tex]
               : &c->default_textures[GL_TEXTURE_2D - GL_TEXTURE_1D];
}

PGLDEF vec4 texture2D_lod(GLuint tex, float x, float y, float lod)
{
    return texture_sample_with_lod(texture2d_object(tex), 0, 0, x, y, lod);
}

PGLDEF vec4 texture2D(GLuint tex, float x, float y)
{
    return texture2D_lod(tex, x, y, 0.0f);
}

PGLDEF vec4 texture2D_grad(GLuint tex, float x, float y,
                           const float* ddx, const float* ddy)
{
    glTexture* object = texture2d_object(tex);
    const glTextureLevel* base = &object->levels[0][0];
    float rx, ry, rho, lod;
    if (!pgl_texture_complete(object, 0))
        return make_v4(0.0f, 0.0f, 0.0f, 1.0f);
    rx = ddx[0] * base->w;
    ry = ddx[1] * base->h;
    rho = sqrtf(rx * rx + ry * ry);
    rx = ddy[0] * base->w;
    ry = ddy[1] * base->h;
    {
        float ry_len = sqrtf(rx * rx + ry * ry);
        if (ry_len > rho) rho = ry_len;
    }
    lod = rho > 1.0f ? log2f(rho) : 0.0f;
    return texture_sample_with_lod(object, 0, 0, x, y, lod);
}

PGLDEF vec4 texture3D(GLuint tex, float x, float y, float z)
{
    int i0, j0, i1, j1, k0, k1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_3D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    float dw = t->w - EPSILON;
    float dh = t->h - EPSILON;
    float dd = t->d - EPSILON;

    int w = t->w;
    int h = t->h;
    int d = t->d;
    int plane = w * t->h;
    float xw = x * dw;
    float yh = y * dh;
    float zd = z * dd;


    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);
        k0 = wrap(floorf(zd), d, t->wrap_r);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0 | k0) < 0) return t->border_color;
#endif

        return Color_to_v4(texdata[k0*plane + j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        k0 = wrap(floorf(zd - 0.5f), d, t->wrap_r);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);
        k1 = wrap(floorf(zd + 0.499999f), d, t->wrap_r);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        float gamma = modff(zd+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;
        if (gamma < 0) ++gamma;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
        gamma = gamma*gamma * (3 - 2*gamma);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cijk, ci1jk, cij1k, ci1j1k, cijk1, ci1jk1, cij1k1, ci1j1k1;
        if ((i0 | j0 | k0) < 0) cijk = t->border_color;
        else cijk = Color_to_v4(texdata[k0*plane + j0*w + i0]);

        if ((i1 | j0 | k0) < 0) ci1jk = t->border_color;
        else ci1jk = Color_to_v4(texdata[k0*plane + j0*w + i1]);

        if ((i0 | j1 | k0) < 0) cij1k = t->border_color;
        else cij1k = Color_to_v4(texdata[k0*plane + j1*w + i0]);

        if ((i1 | j1 | k0) < 0) ci1j1k = t->border_color;
        else ci1j1k = Color_to_v4(texdata[k0*plane + j1*w + i1]);

        if ((i0 | j0 | k1) < 0) cijk1 = t->border_color;
        else cijk1 = Color_to_v4(texdata[k1*plane + j0*w + i0]);

        if ((i1 | j0 | k1) < 0) ci1jk1 = t->border_color;
        else ci1jk1 = Color_to_v4(texdata[k1*plane + j0*w + i1]);

        if ((i0 | j1 | k1) < 0) cij1k1 = t->border_color;
        else cij1k1 = Color_to_v4(texdata[k1*plane + j1*w + i0]);

        if ((i1 | j1 | k1) < 0) ci1j1k1 = t->border_color;
        else ci1j1k1 = Color_to_v4(texdata[k1*plane + j1*w + i1]);
#else
        vec4 cijk = Color_to_v4(texdata[k0*plane + j0*w + i0]);
        vec4 ci1jk = Color_to_v4(texdata[k0*plane + j0*w + i1]);
        vec4 cij1k = Color_to_v4(texdata[k0*plane + j1*w + i0]);
        vec4 ci1j1k = Color_to_v4(texdata[k0*plane + j1*w + i1]);
        vec4 cijk1 = Color_to_v4(texdata[k1*plane + j0*w + i0]);
        vec4 ci1jk1 = Color_to_v4(texdata[k1*plane + j0*w + i1]);
        vec4 cij1k1 = Color_to_v4(texdata[k1*plane + j1*w + i0]);
        vec4 ci1j1k1 = Color_to_v4(texdata[k1*plane + j1*w + i1]);
#endif

        cijk = scale_v4(cijk, (1-alpha)*(1-beta)*(1-gamma));
        ci1jk = scale_v4(ci1jk, alpha*(1-beta)*(1-gamma));
        cij1k = scale_v4(cij1k, (1-alpha)*beta*(1-gamma));
        ci1j1k = scale_v4(ci1j1k, alpha*beta*(1-gamma));
        cijk1 = scale_v4(cijk1, (1-alpha)*(1-beta)*gamma);
        ci1jk1 = scale_v4(ci1jk1, alpha*(1-beta)*gamma);
        cij1k1 = scale_v4(cij1k1, (1-alpha)*beta*gamma);
        ci1j1k1 = scale_v4(ci1j1k1, alpha*beta*gamma);

        cijk = add_v4s(cijk, ci1jk);
        cijk = add_v4s(cijk, cij1k);
        cijk = add_v4s(cijk, ci1j1k);
        cijk = add_v4s(cijk, cijk1);
        cijk = add_v4s(cijk, ci1jk1);
        cijk = add_v4s(cijk, cij1k1);
        cijk = add_v4s(cijk, ci1j1k1);

        return cijk;
    }
}

// for now this should work
PGLDEF vec4 texture2DArray(GLuint tex, float x, float y, int z)
{
    int i0, j0, i1, j1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_2D_ARRAY-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;
    int w = t->w;
    int h = t->h;

    float dw = w - EPSILON;
    float dh = h - EPSILON;

    int plane = w * h;
    float xw = x * dw;
    float yh = y * dh;


    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0) < 0) return t->border_color;
#endif
        return Color_to_v4(texdata[z*plane + j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cij, ci1j, cij1, ci1j1;
        if ((i0 | j0) < 0) cij = t->border_color;
        else cij = Color_to_v4(texdata[z*plane + j0*w + i0]);

        if ((i1 | j0) < 0) ci1j = t->border_color;
        else ci1j = Color_to_v4(texdata[z*plane + j0*w + i1]);

        if ((i0 | j1) < 0) cij1 = t->border_color;
        else cij1 = Color_to_v4(texdata[z*plane + j1*w + i0]);

        if ((i1 | j1) < 0) ci1j1 = t->border_color;
        else ci1j1 = Color_to_v4(texdata[z*plane + j1*w + i1]);
#else
        vec4 cij = Color_to_v4(texdata[z*plane + j0*w + i0]);
        vec4 ci1j = Color_to_v4(texdata[z*plane + j0*w + i1]);
        vec4 cij1 = Color_to_v4(texdata[z*plane + j1*w + i0]);
        vec4 ci1j1 = Color_to_v4(texdata[z*plane + j1*w + i1]);
#endif

        cij = scale_v4(cij, (1-alpha)*(1-beta));
        ci1j = scale_v4(ci1j, alpha*(1-beta));
        cij1 = scale_v4(cij1, (1-alpha)*beta);
        ci1j1 = scale_v4(ci1j1, alpha*beta);

        cij = add_v4s(cij, ci1j);
        cij = add_v4s(cij, cij1);
        cij = add_v4s(cij, ci1j1);

        return cij;
    }
}

PGLDEF vec4 texture_rect(GLuint tex, float x, float y)
{
    int i0, j0, i1, j1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_RECTANGLE-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    int w = t->w;
    int h = t->h;

    float xw = x;
    float yh = y;

    //TODO don't just use mag_filter all the time?
    //is it worth bothering?
    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0) < 0) return t->border_color;
#endif
        return Color_to_v4(texdata[j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cij, ci1j, cij1, ci1j1;
        if ((i0 | j0) < 0) cij = t->border_color;
        else cij = Color_to_v4(texdata[j0*w + i0]);

        if ((i1 | j0) < 0) ci1j = t->border_color;
        else ci1j = Color_to_v4(texdata[j0*w + i1]);

        if ((i0 | j1) < 0) cij1 = t->border_color;
        else cij1 = Color_to_v4(texdata[j1*w + i0]);

        if ((i1 | j1) < 0) ci1j1 = t->border_color;
        else ci1j1 = Color_to_v4(texdata[j1*w + i1]);
#else
        vec4 cij = Color_to_v4(texdata[j0*w + i0]);
        vec4 ci1j = Color_to_v4(texdata[j0*w + i1]);
        vec4 cij1 = Color_to_v4(texdata[j1*w + i0]);
        vec4 ci1j1 = Color_to_v4(texdata[j1*w + i1]);
#endif

        cij = scale_v4(cij, (1-alpha)*(1-beta));
        ci1j = scale_v4(ci1j, alpha*(1-beta));
        cij1 = scale_v4(cij1, (1-alpha)*beta);
        ci1j1 = scale_v4(ci1j1, alpha*beta);

        cij = add_v4s(cij, ci1j);
        cij = add_v4s(cij, cij1);
        cij = add_v4s(cij, ci1j1);

        return cij;
    }
}

static glTexture* texture_cube_object(GLuint texture)
{
    return texture ? &c->textures.a[texture]
                   : &c->default_textures[GL_TEXTURE_CUBE_MAP - GL_TEXTURE_1D];
}

static void texture_cube_coords(float x, float y, float z,
                                int* face, float* out_u, float* out_v)
{
    float ax = fabsf(x), ay = fabsf(y), az = fabsf(z);
    float ma, s, t;
    if (ax >= ay && ax >= az) {
        ma = ax;
        t = -y;
        if (x >= 0.0f) { *face = 0; s = -z; }
        else           { *face = 1; s = z; }
    } else if (ay >= az) {
        ma = ay;
        s = x;
        if (y >= 0.0f) { *face = 2; t = z; }
        else           { *face = 3; t = -z; }
    } else {
        ma = az;
        t = -y;
        if (z >= 0.0f) { *face = 4; s = x; }
        else           { *face = 5; s = -x; }
    }
    if (ma == 0.0f) {
        *face = 0;
        *out_u = *out_v = 0.0f;
        return;
    }
    *out_u = (s / ma + 1.0f) * 0.5f;
    *out_v = (t / ma + 1.0f) * 0.5f;
}

PGLDEF vec4 texture_cubemap_lod(GLuint texture, float x, float y, float z, float lod)
{
    int face;
    float u, v;
    texture_cube_coords(x, y, z, &face, &u, &v);
    return texture_sample_with_lod(texture_cube_object(texture), face, 1, u, v, lod);
}

PGLDEF vec4 texture_cubemap(GLuint texture, float x, float y, float z)
{
    return texture_cubemap_lod(texture, x, y, z, 0.0f);
}

PGLDEF vec4 texture_cubemap_grad(GLuint texture, float x, float y, float z,
                                 const float* ddx, const float* ddy)
{
    glTexture* object = texture_cube_object(texture);
    const glTextureLevel* base = &object->levels[0][0];
    int face, facex, facey;
    float u, v, ux, vx, uy, vy, dux, dvx, duy, dvy, rx, ry, rho, lod;
    if (!pgl_texture_complete(object, 1))
        return make_v4(0.0f, 0.0f, 0.0f, 1.0f);
    texture_cube_coords(x, y, z, &face, &u, &v);
    texture_cube_coords(x + ddx[0], y + ddx[1], z + ddx[2], &facex, &ux, &vx);
    texture_cube_coords(x + ddy[0], y + ddy[1], z + ddy[2], &facey, &uy, &vy);
    dux = facex == face ? ux - u : 1.0f;
    dvx = facex == face ? vx - v : 1.0f;
    duy = facey == face ? uy - u : 1.0f;
    dvy = facey == face ? vy - v : 1.0f;
    rx = dux * base->w;
    ry = dvx * base->h;
    rho = sqrtf(rx * rx + ry * ry);
    rx = duy * base->w;
    ry = dvy * base->h;
    {
        float y_len = sqrtf(rx * rx + ry * ry);
        if (y_len > rho) rho = y_len;
    }
    lod = rho > 1.0f ? log2f(rho) : 0.0f;
    return texture_sample_with_lod(object, face, 1, u, v, lod);
}

PGLDEF vec4 texelFetch1D(GLuint tex, int x, int lod)
{
    PGL_UNUSED(lod);

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_1D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    return Color_to_v4(texdata[x]);
}

PGLDEF vec4 texelFetch2D(GLuint tex, int x, int y, int lod)
{
    PGL_UNUSED(lod);

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_2D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;
    return Color_to_v4(texdata[x*t->w + y]);
}

PGLDEF vec4 texelFetch3D(GLuint tex, int x, int y, int z, int lod)
{
    PGL_UNUSED(lod);

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_3D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;
    int w = t->w;
    int plane = w * t->h;
    return Color_to_v4(texdata[z*plane + y*w + x]);
}

PGLDEF ivec3 textureSize(GLuint tex, GLint lod)
{
    PGL_UNUSED(lod);
    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_1D-GL_TEXTURE_1D];
    }
    return make_iv3(t->w, t->h, t->d);
}

#undef EPSILON


//Raw draw functions that bypass the OpenGL pipeline and draw
//points/lines/triangles directly to the framebuffer, modify as needed.
//
//Example modifications:
//add the blending part of OpenGL to put_pixel
//change them to take vec4's instead of Color's
//change put_triangle to draw all one color or have a separate path/function
//that draws a single color triangle faster (no need to blend)
//
//pass the framebuffer in instead of drawing to c->back_buffer so 
//you can use it elsewhere, independently of a glContext
//etc.
//
PGLDEF void pglClearScreen(void)
{
    PGL_MEMSET(c->back_buffer.buf, 255, c->back_buffer.w * c->back_buffer.h * sizeof(pix_t));
}

PGLDEF void pglSetInterp(GLsizei n, GLenum* interpolation)
{
    c->programs.a[c->cur_program].vs_output_size = n;
    c->vs_output.size = n;

    memcpy(c->programs.a[c->cur_program].interpolation, interpolation, n*sizeof(GLenum));

    // c->vs_output.output_buf was pre-allocated to max size needed in init_glContext
    // otherwise would need to assure it's at least
    // c->vs_output_size * PGL_MAX_VERTS * sizeof(float) right here

    //vs_output.interpolation would be already pointing at current program's array
    //unless the programs array was realloced since the last pgl_UseProgram because
    //they've created a bunch of programs.  Unlikely they'd be changing a shader
    //before creating all their shaders but whatever.
    c->vs_output.interpolation = c->programs.a[c->cur_program].interpolation;
}


// Uses default_vs for vertex shader (passes vertex unchanged, no other attributes or outputs)
// This function is designed to be used with pglDrawFrame(), you don't need it for pglDrawFrame2()
PGLDEF GLuint pglCreateFragProgram(frag_func fragment_shader, GLboolean fragdepth_or_discard)
{
    // Using pgl_AttachShader error if shader is not a shader object which
    // is the closest analog
    PGL_ERR_RET_VAL((!fragment_shader), GL_INVALID_OPERATION, 0);

    glProgram tmp = {default_vs, fragment_shader, NULL, 0, {0}, fragdepth_or_discard, GL_FALSE };

    for (int i=1; i<c->programs.size; ++i) {
        if (c->programs.a[i].deleted && (GLuint)i != c->cur_program) {
            c->programs.a[i] = tmp;
            return i;
        }
    }

    cvec_push_glProgram(&c->programs, tmp);
    return c->programs.size-1;
}

//TODO
//pglDrawRect(x, y, w, h)
//pglDrawPoint(x, y)
//
// TODO worth another draw_pixel() that never does fragment_processing?
// worth duplicating the loops for initial discard check?
PGLDEF void pglDrawFrame(void)
{
    frag_func frag_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniforms = c->programs.a[c->cur_program].uniform;

    Shader_Builtins builtins;
    builtins.fs_input_dx = NULL;
    builtins.fs_input_dy = NULL;
#ifdef PGL_G3D
    /* WS4/WS5: dispatch the frame on the VideoCore GPU when this frag shader has
     * a registered compiled program; fall through to the CPU loop otherwise. */
    if (pgl_gpu_frame(frag_shader, uniforms))
        return;
#endif
    //#pragma omp parallel for private(builtins)
    for (int y=0; y<c->back_buffer.h; ++y) {
        for (int x=0; x<c->back_buffer.w; ++x) {

            //ignore z and w components
            builtins.gl_FragCoord.x = x + 0.5f;
            builtins.gl_FragCoord.y = y + 0.5f;

            builtins.discard = GL_FALSE;
            frag_shader(NULL, &builtins, uniforms);
            if (!builtins.discard)
                draw_pixel(builtins.gl_FragColor, x, y, 0.0f, GL_FALSE);  //scissor/stencil/depth aren't used for pglDrawFrame
        }
    }

}

PGLDEF void pglDrawFrame2(frag_func frag_shader, void* uniforms)
{
    Shader_Builtins builtins;
    builtins.fs_input_dx = NULL;
    builtins.fs_input_dy = NULL;
#ifdef PGL_G3D
    /* WS4/WS5: dispatch the frame on the VideoCore GPU when this frag shader has
     * a registered compiled program; fall through to the CPU loop otherwise. */
    if (pgl_gpu_frame(frag_shader, uniforms))
        return;
#endif
    //#pragma omp parallel for private(builtins)
    for (int y=0; y<c->back_buffer.h; ++y) {
        for (int x=0; x<c->back_buffer.w; ++x) {

            //ignore z and w components
            builtins.gl_FragCoord.x = x + 0.5f;
            builtins.gl_FragCoord.y = y + 0.5f;

            builtins.discard = GL_FALSE;
            frag_shader(NULL, &builtins, uniforms);
            if (!builtins.discard)
                draw_pixel(builtins.gl_FragColor, x, y, 0.0f, GL_FALSE);  //scissor/stencil/depth aren't used for pglDrawFrame
        }
    }
}

PGLDEF void pglBufferData(GLenum target, GLsizei size, const GLvoid* data, GLenum usage)
{
    //TODO check for usage later
    PGL_UNUSED(usage);

    PGL_ERR((target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER), GL_INVALID_ENUM);

    target -= GL_ARRAY_BUFFER;
    PGL_ERR(!c->bound_buffers[target], GL_INVALID_OPERATION);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    // TODO Should I change this in spec functions too?  Or just say don't mix them
    // otherwise bad things/undefined behavior??
    if (!c->buffers.a[c->bound_buffers[target]].user_owned) {
        free(c->buffers.a[c->bound_buffers[target]].data);
    }

    // user_owned buffer, just assign the pointer, will not free
    c->buffers.a[c->bound_buffers[target]].data = (u8*)data;

    c->buffers.a[c->bound_buffers[target]].user_owned = GL_TRUE;
    c->buffers.a[c->bound_buffers[target]].size = size;

    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        c->vertex_arrays.a[c->cur_vertex_array].element_buffer = c->bound_buffers[target];
    }
}

// TODO/NOTE
// All pglTexImage* functions expect the user to pass in packed GL_RGBA
// data. Unlike glTexImage*, no conversion is done, and format != GL_RGBA
// is an INVALID_ENUM error
//
// At least the latter part will change if I ever expand internal format
// support
PGLDEF void pglTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_ERR(target != GL_TEXTURE_1D, GL_INVALID_ENUM);
    GLuint cur_tex = c->bound_textures[target-GL_TEXTURE_UNBOUND-1];

    pglTextureImage1D(cur_tex, level, internalformat, width, border, format, type, data);
}

PGLDEF void pglTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    // NOTE, since this is mapping data, the entire cubemap has to already be arranged in memory in the correct order and we only
    // accept GL_TEXTURE_CUBE_MAP, not any of the individual planes as that wouldn't make sense
    PGL_ERR((target != GL_TEXTURE_2D &&
             target != GL_TEXTURE_RECTANGLE &&
             target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    GLuint cur_tex = c->bound_textures[target-GL_TEXTURE_UNBOUND-1];

    pglTextureImage2D(cur_tex, level, internalformat, width, height, border, format, type, data);
}

PGLDEF void pglTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_ERR((target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY), GL_INVALID_ENUM);

    GLuint cur_tex = c->bound_textures[target-GL_TEXTURE_UNBOUND-1];

    pglTextureImage3D(cur_tex, level, internalformat, width, height, depth, border, format, type, data);
}

PGLDEF void pglTextureImage1D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    // ignore level and internalformat for now
    // (the latter is always converted to RGBA32 anyway)
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);

    PGL_ERR(border, GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    // I do not support DSA mapping of default texture 0, for convenience, no way to know which target it was
    // and I don't want to duplicate code or add extra functions
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    c->textures.a[texture].w = width;
    c->textures.a[texture].h = 1;
    c->textures.a[texture].d = 1;

    // TODO see pglBufferData
    if (!c->textures.a[texture].user_owned)
        free(c->textures.a[texture].data);

    //TODO support other internal formats? components should be of internalformat not format
    c->textures.a[texture].data = (u8*)data;
    c->textures.a[texture].user_owned = GL_TRUE;
}

PGLDEF void pglTextureImage2D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);

    PGL_ERR(border, GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    // have to convert type back from offset to actual enum value
    GLenum target = c->textures.a[texture].type + GL_TEXTURE_UNBOUND + 1;
    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_RECTANGLE) {
        c->textures.a[texture].w = width;
        c->textures.a[texture].h = height;
        c->textures.a[texture].d = 1;

        // TODO see pglBufferData
        if (!c->textures.a[texture].user_owned)
            free(c->textures.a[texture].data);

        // If you're using these pgl mapped functions, it assumes you are respecting
        // your own current unpack alignment settings already
        c->textures.a[texture].data = (u8*)data;
        c->textures.a[texture].user_owned = GL_TRUE;

    } else {  //CUBE_MAP
        // We only accept all the data already arranged, since we're mapping,
        // no individual planes/copying

        // TODO see pglBufferData
        if (!c->textures.a[texture].user_owned)
            free(c->textures.a[texture].data);

        //TODO spec says INVALID_VALUE, man pages say INVALID_ENUM ?
        PGL_ERR(width != height, GL_INVALID_VALUE);

        c->textures.a[texture].w = width;
        c->textures.a[texture].h = height;
        c->textures.a[texture].d = 1;

        c->textures.a[texture].data = (u8*)data;
        c->textures.a[texture].user_owned = GL_TRUE;

    } //end CUBE_MAP

}

PGLDEF void pglTextureImage3D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);

    PGL_ERR(border, GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    c->textures.a[texture].w = width;
    c->textures.a[texture].h = height;
    c->textures.a[texture].d = depth;

    // TODO see pglBufferData
    if (!c->textures.a[texture].user_owned)
        free(c->textures.a[texture].data);

    c->textures.a[texture].data = (u8*)data;
    c->textures.a[texture].user_owned = GL_TRUE;

}

PGLDEF void pglGetBufferData(GLuint buffer, GLvoid** data)
{
    // why'd you even call it?
    PGL_ERR(!data, GL_INVALID_VALUE);

    // matching error code of binding invalid buffecr
    PGL_ERR((!buffer || buffer >= c->buffers.size || c->buffers.a[buffer].deleted),
            GL_INVALID_OPERATION);

    *data = c->buffers.a[buffer].data;
}

PGLDEF void pglGetTextureData(GLuint texture, GLvoid** data)
{
    // why'd you even call it?
    PGL_ERR(!data, GL_INVALID_VALUE);

    // TODO texture 0?
    PGL_ERR((texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    *data = c->textures.a[texture].data;
}

// TODO hmm, void*, or u8*, or GLvoid*?
GLvoid* pglGetBackBuffer(void)
{
    return c->back_buffer.buf;
}

PGLDEF void pglSetBackBuffer(GLvoid* backbuf, GLsizei w, GLsizei h, GLboolean user_owned)
{
    c->back_buffer.w = w;
    c->back_buffer.h = h;
    c->back_buffer.buf = (u8*)backbuf;
    c->back_buffer.lastrow = c->back_buffer.buf + (h-1)*w*sizeof(pix_t);

    c->user_alloced_backbuf = user_owned;
}

PGLDEF void pglSetTexBackBuffer(GLuint texture)
{
    // NOTE, I do not support texture 0
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted ||
             c->textures.a[texture].type+GL_TEXTURE_UNBOUND+1 != GL_TEXTURE_2D), GL_INVALID_OPERATION);
    glTexture* t = &c->textures.a[texture];
    pglSetBackBuffer((GLvoid*)t->data, t->w, t->h, t->user_owned);
}


// Not sure where else to put these two functions, they're helper/stopgap
// measures to deal with PGL only supporting RGBA but they're
// also useful functions on their own and not really "extensions"
// so I don't feel right putting them here or giving them a pgl prefix.
//
// Takes an image with GL_UNSIGNED_BYTE channels in
// a format other than packed GL_RGBA and returns it in (tightly packed) GL_RGBA
// (with the same rules as GLSL texture access for filling the other channels).
// See section 3.6.2 page 65 of the OpenGL ES 2.0.25 spec pdf
//
// IOW this creates an image that will give you the same values in the
// shader that you would have gotten had you used the unsupported
// format.  Passing in a GL_RGBA where pitch == w*4 reduces to a single memcpy
//
// If output is NULL, it will allocate the output image for you
// pitch is the length of a row in bytes.
//
// Returns the resulting packed RGBA image
PGLDEF u8* convert_format_to_packed_rgba(u8* output, u8* input, int w, int h, int pitch, GLenum format)
{
    int i, j, size = w*h;
    int rb = pitch;
    u8* out = output;
    if (!out) {
        out = (u8*)PGL_MALLOC(size*4);
    }
    PGL_MEMSET(out, 0, size*4);

    u8* p = out;

    if (format == PGL_ONE_ALPHA) {
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = UINT8_MAX;
                p[1] = UINT8_MAX;
                p[2] = UINT8_MAX;
                p[3] = input[i*rb+j];
            }
        }
    } else if (format == GL_ALPHA) {
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[3] = input[i*rb+j];
            }
        }
    } else if (format == GL_LUMINANCE) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_luminance_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j];
                p[1] = input[i*rb+j];
                p[2] = input[i*rb+j];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_RED) {
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j];
                p[3] = UINT8_MAX;
            }
        }
    } else if (format == GL_LUMINANCE_ALPHA) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_luminance_alpha_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*2];
                p[1] = input[i*rb+j*2];
                p[2] = input[i*rb+j*2];
                p[3] = input[i*rb+j*2+1];
            }
        }
#endif
    } else if (format == GL_RG) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_rg_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*2];
                p[1] = input[i*rb+j*2+1];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_RGB) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_rgb_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*3];
                p[1] = input[i*rb+j*3+1];
                p[2] = input[i*rb+j*3+2];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_BGR) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_bgr_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*3+2];
                p[1] = input[i*rb+j*3+1];
                p[2] = input[i*rb+j*3];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_BGRA) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_bgra_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*4+2];
                p[1] = input[i*rb+j*4+1];
                p[2] = input[i*rb+j*4];
                p[3] = input[i*rb+j*4+3];
            }
        }
#endif
    } else if (format == GL_RGBA) {
        if (pitch == w*4) {
            // Just a plain copy
            PGL_MEMCPY(out, input, w*h*4);
        } else {
            // get rid of row padding
            int bw = w*4;
            for (i=0; i<h; ++i) {
                PGL_MEMCPY(&out[i*bw], &input[i*rb], bw);
            }
        }
    } else {
        puts("Unrecognized or unsupported input format!");
        free(out);
        out = NULL;
    }
    return out;
}

// pass in packed single channel 8 bit image where background=0, foreground=255
// and get a packed 4-channel rgba image using the colors provided
PGLDEF u8* convert_grayscale_to_rgba(u8* input, int size, u32 bg_rgba, u32 text_rgba)
{
    float rb, gb, bb, ab, rt, gt, bt, at;

    u8* tmp = (u8*)&bg_rgba;
    rb = tmp[0];
    gb = tmp[1];
    bb = tmp[2];
    ab = tmp[3];

    tmp = (u8*)&text_rgba;
    rt = tmp[0];
    gt = tmp[1];
    bt = tmp[2];
    at = tmp[3];

    //printf("background = (%f, %f, %f, %f)\ntext = (%f, %f, %f, %f)\n", rb, gb, bb, ab, rt, gt, bt, at);

    u8* color_image = (u8*)PGL_MALLOC(size * 4);
    float t;
    for (int i=0; i<size; ++i) {
        t = (input[i] - 0) / 255.0;
        color_image[i*4] = rt * t + rb * (1 - t);
        color_image[i*4+1] = gt * t + gb * (1 - t);
        color_image[i*4+2] = bt * t + bb * (1 - t);
        color_image[i*4+3] = at * t + ab * (1 - t);
    }


    return color_image;
}


// Just a convenience to have default textures return a specific color, like white here,
// so a textured shading algorithm will look untextured if you bind texture 0
PGLDEF int setup_default_textures(void)
{
    // just 1 white pixel
    // Could make it static and map it so we don't have a 12 tiny allocations
    GLuint image[1] = {
        0xFFFFFFFF
    };
    int w = 1;
    int h = 1;
    int d = 1;
    int frames = 1;
    // If this was called in init_glContext() or immediately after we would know
    // 0 was already bound
    pgl_BindTexture(GL_TEXTURE_1D, 0);
    pgl_TexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    pgl_BindTexture(GL_TEXTURE_2D, 0);
    pgl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    pgl_BindTexture(GL_TEXTURE_3D, 0);
    pgl_TexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, w, h, d, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    pgl_BindTexture(GL_TEXTURE_1D_ARRAY, 0);
    pgl_TexImage2D(GL_TEXTURE_1D_ARRAY, 0, GL_RGBA, w, frames, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    pgl_BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    pgl_TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, w, h, frames, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    pgl_BindTexture(GL_TEXTURE_RECTANGLE, 0);
    pgl_TexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    GLenum cube[6] =
    {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };
    for (int i=0; i<6; i++) {
        pgl_TexImage2D(cube[i], 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image);
    }

    return GL_TRUE;
}

PGLDEF void put_pixel(Color color, int x, int y)
{
    //u32* dest = &((u32*)c->back_buffer.lastrow)[-y*c->back_buffer.w + x];
    pix_t* dest = &((pix_t*)c->back_buffer.buf)[y*c->back_buffer.w + x];
    //*dest = (u32)color.a << PGL_ASHIFT | (u32)color.r << PGL_RSHIFT | (u32)color.g << PGL_GSHIFT | (u32)color.b << PGL_BSHIFT;
    *dest = RGBA_TO_PIXEL(color.r, color.g, color.b, color.a);
}

PGLDEF void put_pixel_blend(vec4 src, int x, int y)
{
    //u32* dest = &((u32*)c->back_buffer.lastrow)[-y*c->back_buffer.w + x];
    u32* dest = &((u32*)c->back_buffer.buf)[y*c->back_buffer.w + x];

    //Color dest_color = make_Color((*dest & PGL_RMASK) >> PGL_RSHIFT, (*dest & PGL_GMASK) >> PGL_GSHIFT, (*dest & PGL_BMASK) >> PGL_BSHIFT, (*dest & PGL_AMASK) >> PGL_ASHIFT);
    Color dest_color = PIXEL_TO_COLOR(*dest);

    vec4 dst = Color_to_v4(dest_color);

    // standard alpha blending xyzw = rgba
    vec4 final;
    final.x = src.x * src.w + dst.x * (1.0f - src.w);
    final.y = src.y * src.w + dst.y * (1.0f - src.w);
    final.z = src.z * src.w + dst.z * (1.0f - src.w);
    final.w = src.w + dst.w * (1.0f - src.w);

    Color color = v4_to_Color(final);
    //*dest = (u32)color.a << PGL_ASHIFT | (u32)color.r << PGL_RSHIFT | (u32)color.g << PGL_GSHIFT | (u32)color.b << PGL_BSHIFT;
    *dest = RGBA_TO_PIXEL(color.r, color.g, color.b, color.a);
}

PGLDEF void put_wide_line_simple(Color the_color, float width, float x1, float y1, float x2, float y2)
{
    float tmp;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    //calculate slope and implicit line parameters once
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);

    vec2 ab = make_v2(line.A, line.B);
    normalize_v2(&ab);

    int x, y;

    float x_min = MAX(0, MIN(x1, x2));
    float x_max = MIN(c->back_buffer.w-1, MAX(x1, x2));
    float y_min = MAX(0, MIN(y1, y2));
    float y_max = MIN(c->back_buffer.h-1, MAX(y1, y2));

    //4 cases based on slope
    if (m <= -1) {           //(-infinite, -1]
        x = x1;
        for (y=y_max; y>=y_min; --y) {
            for (float j=x-width/2; j<x+width/2; j++) {
                put_pixel(the_color, j, y);
            }
            if (line_func(&line, x+0.5f, y-1) < 0)
                x++;
        }
    } else if (m <= 0) {     //(-1, 0]
        y = y1;
        for (x=x_min; x<=x_max; ++x) {
            for (float j=y-width/2; j<y+width/2; j++) {
                put_pixel(the_color, x, j);
            }
            if (line_func(&line, x+1, y-0.5f) > 0)
                y--;
        }
    } else if (m <= 1) {     //(0, 1]
        y = y1;
        for (x=x_min; x<=x_max; ++x) {
            for (float j=y-width/2; j<y+width/2; j++) {
                put_pixel(the_color, x, j);
            }

            //put_pixel(the_color, x, y);
            if (line_func(&line, x+1, y+0.5f) < 0)
                y++;
        }

    } else {                 //(1, +infinite)
        x = x1;
        for (y=y_min; y<=y_max; ++y) {
            for (float j=x-width/2; j<x+width/2; j++) {
                put_pixel(the_color, j, y);
            }
            if (line_func(&line, x+0.5f, y+1) > 0)
                x++;
        }
    }
}

PGLDEF void put_wide_line(Color color1, Color color2, float width, float x1, float y1, float x2, float y2)
{
    vec2 a = { x1, y1 };
    vec2 b = { x2, y2 };
    vec2 tmp;
    Color tmpc;

    if (x2 < x1) {
        tmp = a;
        a = b;
        b = tmp;
        tmpc = color1;
        color1 = color2;
        color2 = tmpc;
    }

    vec4 c1 = Color_to_v4(color1);
    vec4 c2 = Color_to_v4(color2);

    // need half the width to calculate
    width /= 2.0f;

    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);
    normalize_line(&line);
    vec2 c;

    vec2 ab = sub_v2s(b, a);
    vec2 ac;

    float dot_abab = dot_v2s(ab, ab);

    float x_min = floor(a.x - width) + 0.5f;
    float x_max = floor(b.x + width) + 0.5f;
    float y_min, y_max;
    if (m <= 0) {
        y_min = floor(b.y - width) + 0.5f;
        y_max = floor(a.y + width) + 0.5f;
    } else {
        y_min = floor(a.y - width) + 0.5f;
        y_max = floor(b.y + width) + 0.5f;
    }

    float x, y, e, dist, t;
    float w2 = width*width;
    //int last = 1;
    Color out_c;

    for (y = y_min; y <= y_max; ++y) {
        c.y = y;
        for (x = x_min; x <= x_max; x++) {
            // TODO optimize
            c.x = x;
            ac = sub_v2s(c, a);
            e = dot_v2s(ac, ab);
            
            // c lies past the ends of the segment ab
            if (e <= 0.0f || e >= dot_abab) {
                continue;
            }

            // can do this because we normalized the line equation
            // TODO square or fabsf?
            dist = line_func(&line, c.x, c.y);
            if (dist*dist < w2) {
                t = e / dot_abab;
                out_c = v4_to_Color(mixf_v4(c1, c2, t));
                put_pixel(out_c, x, y);
            }
        }
    }
}

//Should I have it take a glFramebuffer as paramater?
PGLDEF void put_line(Color the_color, float x1, float y1, float x2, float y2)
{
    float tmp;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    //calculate slope and implicit line parameters once
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);

    int x, y;

    float x_min = MAX(0, MIN(x1, x2));
    float x_max = MIN(c->back_buffer.w-1, MAX(x1, x2));
    float y_min = MAX(0, MIN(y1, y2));
    float y_max = MIN(c->back_buffer.h-1, MAX(y1, y2));

    x_min = floorf(x_min) + 0.5f;
    x_max = floorf(x_max) + 0.5f;
    y_min = floorf(y_min) + 0.5f;
    y_max = floorf(y_max) + 0.5f;

    //4 cases based on slope
    if (m <= -1) {           //(-infinite, -1]
        x = x_min;
        for (y=y_max; y>=y_min; --y) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+0.5f, y-1) < 0)
                x++;
        }
    } else if (m <= 0) {     //(-1, 0]
        y = y_max;
        for (x=x_min; x<=x_max; ++x) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+1, y-0.5f) > 0)
                y--;
        }
    } else if (m <= 1) {     //(0, 1]
        y = y_min;
        for (x=x_min; x<=x_max; ++x) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+1, y+0.5f) < 0)
                y++;
        }

    } else {                 //(1, +infinite)
        x = x_min;
        for (y=y_min; y<=y_max; ++y) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+0.5f, y+1) > 0)
                x++;
        }
    }
}


// can't think of a better/cleaner way to do this than these lines
#define CLIP_TRIANGLE() \
    do { \
    x_min = MIN(p1.x, p2.x); \
    x_max = MAX(p1.x, p2.x); \
    y_min = MIN(p1.y, p2.y); \
    y_max = MAX(p1.y, p2.y); \
 \
    x_min = MIN(p3.x, x_min); \
    x_max = MAX(p3.x, x_max); \
    y_min = MIN(p3.y, y_min); \
    y_max = MAX(p3.y, y_max); \
 \
    x_min = MAX(c->lx, x_min); \
    x_max = MIN(c->ux, x_max); \
    y_min = MAX(c->ly, y_min); \
    y_max = MIN(c->uy, y_max); \
    } while (0)

#define MAKE_IMPLICIT_LINES() \
    do { \
    l12 = make_Line(p1.x, p1.y, p2.x, p2.y); \
    l23 = make_Line(p2.x, p2.y, p3.x, p3.y); \
    l31 = make_Line(p3.x, p3.y, p1.x, p1.y); \
    } while (0)

#define ANY_COLORS_NOT_WHITE(c) \
    (c0.r != 255 || c1.r != 255 || c2.r != 255 || \
    c0.g != 255 || c1.g != 255 || c2.g != 255 || \
    c0.b != 255 || c1.b != 255 || c2.b != 255)

PGLDEF void put_triangle_uniform(vec4 color, vec2 p1, vec2 p2, vec2 p3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    // blend
                    put_pixel_blend(color, x, y);
                    //put_pixel(color, x, y);
                }
            }
        }
    }
}

PGLDEF void put_triangle(Color c1, Color c2, Color c3, vec2 p1, vec2 p2, vec2 p3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;
    Color col;
    col.a = 255; // hmm

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    //calculate interoplation here
                    col.r = alpha*c1.r + beta*c2.r + gamma*c3.r;
                    col.g = alpha*c1.g + beta*c2.g + gamma*c3.g;
                    col.b = alpha*c1.b + beta*c2.b + gamma*c3.b;
                    //col.a = alpha*c1.a + beta*c2.a + gamma*c3.a;
                    //put_pixel_blend(c, x, y);
                    put_pixel(col, x, y);
                }
            }
        }
    }
}

PGLDEF void put_triangle_tex(int tex, vec2 uv1, vec2 uv2, vec2 uv3, vec2 p1, vec2 p2, vec2 p3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

#if 0
    print_v2(p1, " p1\n");
    print_v2(p2, " p2\n");
    print_v2(p3, " p3\n");
    print_v2(uv1, " uv1\n");
    print_v2(uv2, " uv2\n");
    print_v2(uv3, " uv3\n");
#endif

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;
    vec2 uv;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    //calculate interoplation here
                    uv = add_v2s(scale_v2(uv1, alpha), scale_v2(uv2, beta));
                    uv = add_v2s(uv, scale_v2(uv3, gamma));
                    put_pixel_blend(texture2D(tex, uv.x, uv.y), x, y);
                }
            }
        }
    }
}

PGLDEF void put_triangle_tex_modulate(int tex, vec2 uv1, vec2 uv2, vec2 uv3, vec2 p1, vec2 p2, vec2 p3, Color c1, Color c2, Color c3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;
    Color col;

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

#if 0
    print_v2(p1, " p1\n");
    print_v2(p2, " p2\n");
    print_v2(p3, " p3\n");
    print_v2(uv1, " uv1\n");
    print_v2(uv2, " uv2\n");
    print_v2(uv3, " uv3\n");
    print_Color(c1, " c1\n");
    print_Color(c2, " c2\n");
    print_Color(c3, " c3\n");
#endif

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;
    vec2 uv;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    //calculate interoplation here
                    uv = add_v2s(scale_v2(uv1, alpha), scale_v2(uv2, beta));
                    uv = add_v2s(uv, scale_v2(uv3, gamma));

                    col.r = alpha*c1.r + beta*c2.r + gamma*c3.r;
                    col.g = alpha*c1.g + beta*c2.g + gamma*c3.g;
                    col.b = alpha*c1.b + beta*c2.b + gamma*c3.b;
                    col.a = alpha*c1.a + beta*c2.a + gamma*c3.a;
                    vec4 cv = Color_to_v4(col);
                    vec4 texcolor = texture2D(tex, uv.x, uv.y);
                    
                    put_pixel_blend(mult_v4s(cv, texcolor), x, y);
                }
            }
        }
    }
}

#define COLOR_EQ(c1, c2) ((c1).r == (c2).r && (c1).g == (c2).g && (c1).b == (c2).b && (c1).a == (c2).a)


// TODO Color* or vec4*? float* for xy/uv or vec2*?
PGLDEF void pgl_draw_geometry_raw(int tex, const float* xy, int xy_stride, const Color* color, int color_stride, const float* uv, int uv_stride, int n_verts, const void* indices, int n_indices, int sz_indices)
{
    int i,j;
    float* x;
    float* u;
    int count = indices ? n_indices : n_verts;

    // TODO make PGL_INVALID_VALUE et all?
    PGL_ERR(!xy, GL_INVALID_VALUE);

    // Matching SDL_RenderGeometryRaw but I feel like they should be able to pass
    // NULL and just use the texture
    PGL_ERR(!color, GL_INVALID_VALUE);


    PGL_ERR(count % 3, GL_INVALID_VALUE);
    PGL_ERR(!(sz_indices==1 || sz_indices==2 || sz_indices==4), GL_INVALID_VALUE);

    if (n_verts < 3) return;

    PGL_ASSERT((get_max_vertices() * GL_MAX_VERTEX_OUTPUT_COMPONENTS * sizeof(float))/sizeof(pgl_copy_data) >= (size_t)count);
    // Allow default texture 0?  many implementations return black (0,0,0,1) when sampling
    // tex 0
    if (tex > 0) {
        PGL_ERR(!uv, GL_INVALID_VALUE);

        PGL_ERR((tex >= c->textures.size || c->textures.a[tex].deleted), GL_INVALID_VALUE);

        PGL_ERR(c->textures.a[tex].type != GL_TEXTURE_2D-(GL_TEXTURE_UNBOUND+1), GL_INVALID_OPERATION);

        pgl_copy_data* verts = (pgl_copy_data*)&c->vs_output.output_buf[0];
        for (i=0; i<count; ++i) {
            if (sz_indices == 1) j = ((GLubyte*)indices)[i];
            else if (sz_indices == 2) j = ((GLushort*)indices)[i];
            else if (sz_indices == 4) j = ((GLuint*)indices)[i];
            else j = i;

            x = (float*)((u8*)xy + j*xy_stride);
            u = (float*)((u8*)uv + j*uv_stride);

            verts[i].c = *(Color*)((u8*)color + j*color_stride);

            // TODO convert to ints here for efficiency or leave floats for
            // flexibility/subpixel accuracy?
            verts[i].src.x = u[0];
            verts[i].src.y = u[1];

            // scale?
            verts[i].dst.x = x[0];
            verts[i].dst.y = x[1];
        }

        vec4 tex_color;
        pgl_copy_data* p = verts;
        int is_uniform = GL_FALSE;
        int has_modulation = GL_FALSE;
        int tex_uniform = GL_FALSE;
        for (i=0; i<count; i+=3, p+=3) {
            is_uniform = (COLOR_EQ(p[0].c, p[1].c) && COLOR_EQ(p[1].c, p[2].c));
            if (is_uniform) {
                has_modulation = (p[0].c.r != 255 || p[0].c.g != 255 || p[0].c.b != 255 || p[0].c.a != 255);
            } else {
                has_modulation = GL_TRUE;
            }
            tex_uniform = (equal_v2s(p[0].src, p[1].src) && equal_v2s(p[1].src, p[2].src));
            if (tex_uniform) tex_color = texture2D(tex, p[0].src.x, p[0].src.y);

            if (has_modulation) {
                if (is_uniform) {
                    if (tex_uniform) {
                        // uniform color triangle, likely uniform color rect
                        vec4 color = mult_v4s(tex_color, Color_to_v4(p[0].c));
                        put_triangle_uniform(color, p[0].dst, p[1].dst, p[2].dst);
                    } else {
                        // need another variant that takes a single color so only
                        // interpolates uv
                        put_triangle_tex_modulate(tex, p[0].src, p[1].src, p[2].src, p[0].dst, p[1].dst, p[2].dst, p[0].c, p[1].c, p[2].c);
                    }
                } else {
                    put_triangle_tex_modulate(tex, p[0].src, p[1].src, p[2].src, p[0].dst, p[1].dst, p[2].dst, p[0].c, p[1].c, p[2].c);
                }
            } else {
                if (tex_uniform) {
                    // uniform color triangle, likely uniform color rect
                    put_triangle_uniform(tex_color, p[0].dst, p[1].dst, p[2].dst);
                } else {
                    put_triangle_tex(tex, p[0].src, p[1].src, p[2].src, p[0].dst, p[1].dst, p[2].dst);
                }
            }
        }
    } else {
        pgl_fill_data* verts = (pgl_fill_data*)&c->vs_output.output_buf[0];
        for (i=0; i<count; ++i) {
            if (sz_indices == 1) j = ((GLubyte*)indices)[i];
            else if (sz_indices == 2) j = ((GLushort*)indices)[i];
            else if (sz_indices == 4) j = ((GLuint*)indices)[i];
            else j = i;

            x = (float*)((u8*)xy + j*xy_stride);
            verts[i].c = *(Color*)((u8*)color + j*color_stride);

            // scale?
            verts[i].dst.x = x[0];
            verts[i].dst.y = x[1];
        }

        pgl_fill_data* p = verts;
        for (i=0; i<count; i+=3, p+=3) {
            put_triangle(p[0].c, p[1].c, p[2].c, p[0].dst, p[1].dst, p[2].dst);
        }
    }
}



#define plot(X,Y,D) do{ c.w = (D); put_pixel_blend(c, X, Y); } while (0)

#define ipart_(X) ((int)(X))
#define round_(X) ((int)(((float)(X))+0.5f))
#define fpart_(X) (((float)(X))-(float)ipart_(X))
#define rfpart_(X) (1.0f-fpart_(X))

#define swap_(a, b) do{ __typeof__(a) tmp;  tmp = a; a = b; b = tmp; } while(0)
PGLDEF void put_aa_line(vec4 c, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    if (fabs(dx) > fabs(dy)) {
        if (x2 < x1) {
            swap_(x1, x2);
            swap_(y1, y2);
        }
        float gradient = dy / dx;
        float xend = round_(x1);
        float yend = y1 + gradient*(xend - x1);
        float xgap = rfpart_(x1 + 0.5);
        int xpxl1 = xend;
        int ypxl1 = ipart_(yend);
        plot(xpxl1, ypxl1, rfpart_(yend)*xgap);
        plot(xpxl1, ypxl1+1, fpart_(yend)*xgap);
        printf("xgap = %f\n", xgap);
        printf("%f %f\n", rfpart_(yend), fpart_(yend));
        printf("%f %f\n", rfpart_(yend)*xgap, fpart_(yend)*xgap);
        float intery = yend + gradient;

        xend = round_(x2);
        yend = y2 + gradient*(xend - x2);
        xgap = fpart_(x2+0.5);
        int xpxl2 = xend;
        int ypxl2 = ipart_(yend);
        plot(xpxl2, ypxl2, rfpart_(yend) * xgap);
        plot(xpxl2, ypxl2 + 1, fpart_(yend) * xgap);

        int x;
        for(x=xpxl1+1; x < xpxl2; x++) {
            plot(x, ipart_(intery), rfpart_(intery));
            plot(x, ipart_(intery) + 1, fpart_(intery));
            intery += gradient;
        }
    } else {
        if ( y2 < y1 ) {
            swap_(x1, x2);
            swap_(y1, y2);
        }
        float gradient = dx / dy;
        float yend = round_(y1);
        float xend = x1 + gradient*(yend - y1);
        float ygap = rfpart_(y1 + 0.5);
        int ypxl1 = yend;
        int xpxl1 = ipart_(xend);
        plot(xpxl1, ypxl1, rfpart_(xend)*ygap);
        plot(xpxl1 + 1, ypxl1, fpart_(xend)*ygap);
        float interx = xend + gradient;

        yend = round_(y2);
        xend = x2 + gradient*(yend - y2);
        ygap = fpart_(y2+0.5);
        int ypxl2 = yend;
        int xpxl2 = ipart_(xend);
        plot(xpxl2, ypxl2, rfpart_(xend) * ygap);
        plot(xpxl2 + 1, ypxl2, fpart_(xend) * ygap);

        int y;
        for(y=ypxl1+1; y < ypxl2; y++) {
            plot(ipart_(interx), y, rfpart_(interx));
            plot(ipart_(interx) + 1, y, fpart_(interx));
            interx += gradient;
        }
    }
}


PGLDEF void put_aa_line_interp(vec4 c1, vec4 c2, float x1, float y1, float x2, float y2)
{
    vec4 c;
    float t;

    float dx = x2 - x1;
    float dy = y2 - y1;

    if (fabs(dx) > fabs(dy)) {
        if (x2 < x1) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(c1, c2);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;

        c = c1;

        float gradient = dy / dx;
        float xend = round_(x1);
        float yend = y1 + gradient*(xend - x1);
        float xgap = rfpart_(x1 + 0.5);
        int xpxl1 = xend;
        int ypxl1 = ipart_(yend);
        plot(xpxl1, ypxl1, rfpart_(yend)*xgap);
        plot(xpxl1, ypxl1+1, fpart_(yend)*xgap);
        printf("xgap = %f\n", xgap);
        printf("%f %f\n", rfpart_(yend), fpart_(yend));
        printf("%f %f\n", rfpart_(yend)*xgap, fpart_(yend)*xgap);
        float intery = yend + gradient;

        c = c2;
        xend = round_(x2);
        yend = y2 + gradient*(xend - x2);
        xgap = fpart_(x2+0.5);
        int xpxl2 = xend;
        int ypxl2 = ipart_(yend);
        plot(xpxl2, ypxl2, rfpart_(yend) * xgap);
        plot(xpxl2, ypxl2 + 1, fpart_(yend) * xgap);

        int x;
        for(x=xpxl1+1; x < xpxl2; x++) {
            pr.x = x;
            pr.y = intery;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            c = mixf_v4(c1, c2, t);

            plot(x, ipart_(intery), rfpart_(intery));
            plot(x, ipart_(intery) + 1, fpart_(intery));
            intery += gradient;
        }
    } else {
        if ( y2 < y1 ) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(c1, c2);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;

        c = c1;

        float gradient = dx / dy;
        float yend = round_(y1);
        float xend = x1 + gradient*(yend - y1);
        float ygap = rfpart_(y1 + 0.5);
        int ypxl1 = yend;
        int xpxl1 = ipart_(xend);
        plot(xpxl1, ypxl1, rfpart_(xend)*ygap);
        plot(xpxl1 + 1, ypxl1, fpart_(xend)*ygap);
        float interx = xend + gradient;


        c = c2;
        yend = round_(y2);
        xend = x2 + gradient*(yend - y2);
        ygap = fpart_(y2+0.5);
        int ypxl2 = yend;
        int xpxl2 = ipart_(xend);
        plot(xpxl2, ypxl2, rfpart_(xend) * ygap);
        plot(xpxl2 + 1, ypxl2, fpart_(xend) * ygap);

        int y;
        for(y=ypxl1+1; y < ypxl2; y++) {
            pr.x = interx;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            c = mixf_v4(c1, c2, t);

            plot(ipart_(interx), y, rfpart_(interx));
            plot(ipart_(interx) + 1, y, fpart_(interx));
            interx += gradient;
        }
    }
}


#undef swap_
#undef plot
#undef ipart_
#undef fpart_
#undef round_
#undef rfpart_



// Collection of standard shaders based on
// https://github.com/rswinkle/oglsuperbible5/blob/master/Src/GLTools/src/GLShaderManager.cpp
//
// Meant to ease the transition from old fixed function a little.  You might be able
// to get away without writing any new shaders, but you'll still need to use uniforms
// and enable attributes etc. things unless you write a full compatibility layer

// Identity Shader, no transformation, uniform color
static void pgl_identity_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(vs_output);
    PGL_UNUSED(uniforms);
    builtins->gl_Position = vertex_attribs[PGL_ATTR_VERT];
}

static void pgl_identity_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(fs_input);
    builtins->gl_FragColor = ((pgl_uniforms*)uniforms)->color;
}

// Flat Shader, Applies the uniform model view matrix transformation, uniform color
static void flat_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(vs_output);
    builtins->gl_Position = mult_m4_v4(*((mat4*)uniforms), vertex_attribs[PGL_ATTR_VERT]);
}

// flat_fs is identical to pgl_identity_fs

// Shaded Shader, interpolates per vertex colors
static void pgl_shaded_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    ((vec4*)vs_output)[0] = vertex_attribs[PGL_ATTR_COLOR]; //color

    builtins->gl_Position = mult_m4_v4(*((mat4*)uniforms), vertex_attribs[PGL_ATTR_VERT]);
}

static void pgl_shaded_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(uniforms);
    builtins->gl_FragColor = ((vec4*)fs_input)[0];
}

// Default Light Shader
// simple diffuse directional light, vertex based shading
// uniforms:
// mat4 mvp_mat
// mat3 normal_mat
// vec4 color
//
// attributes:
// vec4 vertex
// vec3 normal
static void pgl_dflt_light_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec3 norm = norm_v3(mult_m3_v3(u->normal_mat, *(vec3*)&v_attrs[PGL_ATTR_NORMAL]));

    vec3 light_dir = { 0.0f, 0.0f, 1.0f };
    float tmp = dot_v3s(norm, light_dir);
    float fdot = MAX(0.0f, tmp);

    vec4 c = u->color;

    // outgoing fragcolor to be interpolated
    ((vec4*)vs_output)[0] = make_v4(c.x*fdot, c.y*fdot, c.z*fdot, c.w);

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);
}

// default_light_fs is the same as pgl_shaded_fs

// Point Light Diff Shader
// point light, diffuse lighting only
// uniforms:
// mat4 mvp_mat
// mat4 mv_mat
// mat3 normal_mat
// vec4 color
// vec3 light_pos
//
// attributes:
// vec4 vertex
// vec3 normal
static void pgl_pnt_light_diff_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec3 norm = norm_v3(mult_m3_v3(u->normal_mat, *(vec3*)&v_attrs[PGL_ATTR_NORMAL]));

    vec4 ec_pos = mult_m4_v4(u->mv_mat, v_attrs[PGL_ATTR_VERT]);
    vec3 ec_pos3 = v4_to_v3h(ec_pos);

    vec3 light_dir = norm_v3(sub_v3s(u->light_pos, ec_pos3));

    float tmp = dot_v3s(norm, light_dir);
    float fdot = MAX(0.0f, tmp);

    vec4 c = u->color;

    // outgoing fragcolor to be interpolated
    ((vec4*)vs_output)[0] = make_v4(c.x*fdot, c.y*fdot, c.z*fdot, c.w);

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);
}

// point_light_diff_fs is the same as pgl_shaded_fs


// Texture Replace Shader
// Just paste the texture on the triangles
// uniforms:
// mat4 mvp_mat
// GLuint tex0
//
// attributes:
// vec4 vertex
// vec2 texcoord0
static void pgl_tex_rplc_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    ((vec2*)vs_output)[0] = *(vec2*)&v_attrs[PGL_ATTR_TEXCOORD0]; //tex_coords

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);

}

static void pgl_tex_rplc_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    vec2 tex_coords = ((vec2*)fs_input)[0];
    GLuint tex = ((pgl_uniforms*)uniforms)->tex0;

    builtins->gl_FragColor = texture2D(tex, tex_coords.x, tex_coords.y);
}



// Texture Rect Replace Shader
// Just paste the texture on the triangles except using rect textures
// uniforms:
// mat4 mvp_mat
// GLuint tex0
//
// attributes:
// vec4 vertex
// vec2 texcoord0

// texture_rect_rplc_vs is the same as pgl_tex_rplc_vs
static void pgl_tex_rect_rplc_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    vec2 tex_coords = ((vec2*)fs_input)[0];
    GLuint tex = ((pgl_uniforms*)uniforms)->tex0;

    builtins->gl_FragColor = texture_rect(tex, tex_coords.x, tex_coords.y);
}


// Texture Modulate Shader
// Paste texture on triangles but multiplied by a uniform color
// uniforms:
// mat4 mvp_mat
// GLuint tex0
//
// attributes:
// vec4 vertex
// vec2 texcoord0

// texture_modulate_vs is the same as pgl_tex_rplc_vs

static void pgl_tex_modulate_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec2 tex_coords = ((vec2*)fs_input)[0];

    GLuint tex = u->tex0;

    builtins->gl_FragColor = mult_v4s(u->color, texture2D(tex, tex_coords.x, tex_coords.y));
}


// Texture Point Light Diff
// point light, diffuse only with texture
// uniforms:
// mat4 mvp_mat
// mat4 mv_mat
// mat3 normal_mat
// vec4 color
// vec3 light_pos
//
// attributes:
// vec4 vertex
// vec3 normal
static void pgl_tex_pnt_light_diff_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec3 norm = norm_v3(mult_m3_v3(u->normal_mat, *(vec3*)&v_attrs[PGL_ATTR_NORMAL]));

    vec4 ec_pos = mult_m4_v4(u->mv_mat, v_attrs[PGL_ATTR_VERT]);
    vec3 ec_pos3 = v4_to_v3h(ec_pos);

    vec3 light_dir = norm_v3(sub_v3s(u->light_pos, ec_pos3));

    float tmp = dot_v3s(norm, light_dir);
    float fdot = MAX(0.0f, tmp);

    vec4 c = u->color;

    // outgoing fragcolor to be interpolated
    ((vec4*)vs_output)[0] = make_v4(c.x*fdot, c.y*fdot, c.z*fdot, c.w);
    // fragcolor takes up 4 floats, ie 2*sizeof(vec2)
    ((vec2*)vs_output)[2] =  *(vec2*)&v_attrs[PGL_ATTR_TEXCOORD0];

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);
}


static void pgl_tex_pnt_light_diff_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec2 tex_coords = ((vec2*)fs_input)[2];

    GLuint tex = u->tex0;

    builtins->gl_FragColor = mult_v4s(((vec4*)fs_input)[0], texture2D(tex, tex_coords.x, tex_coords.y));
}


PGLDEF void pgl_init_std_shaders(GLuint programs[PGL_NUM_SHADERS])
{
    pgl_prog_info std_shaders[PGL_NUM_SHADERS] =
    {
        { pgl_identity_vs, pgl_identity_fs, 0, {0}, GL_FALSE },
        { flat_vs, pgl_identity_fs, 0, {0}, GL_FALSE },
        { pgl_shaded_vs, pgl_shaded_fs, 4, {PGL_SMOOTH4}, GL_FALSE },
        { pgl_dflt_light_vs, pgl_shaded_fs, 4, {PGL_SMOOTH4}, GL_FALSE },
        { pgl_pnt_light_diff_vs, pgl_shaded_fs, 4, {PGL_SMOOTH4}, GL_FALSE },
        { pgl_tex_rplc_vs, pgl_tex_rplc_fs, 2, {PGL_SMOOTH2}, GL_FALSE },
        { pgl_tex_rplc_vs, pgl_tex_modulate_fs, 2, {PGL_SMOOTH2}, GL_FALSE },
        { pgl_tex_pnt_light_diff_vs, pgl_tex_pnt_light_diff_fs, 6, {PGL_SMOOTH4, PGL_SMOOTH2}, GL_FALSE },


        { pgl_tex_rplc_vs, pgl_tex_rect_rplc_fs, 2, {PGL_SMOOTH2}, GL_FALSE }
    };

    for (int i=0; i<PGL_NUM_SHADERS; i++) {
        pgl_prog_info* p = &std_shaders[i];
        programs[i] = pglCreateProgram(p->vs, p->fs, p->vs_out_sz, p->interp, p->uses_fragdepth_or_discard);
    }
}
