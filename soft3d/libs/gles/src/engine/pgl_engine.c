/* pgl_engine.c - the portablegl engine adapter.
 *
 * This is the ONLY translation unit in libGLESv2/libEGL that includes
 * portablegl.h.  It owns the single glContext the front end renders into and
 * implements the gles_engine_* wrappers declared in gles_engine.h.  Because the
 * standard GLES2 token values (what the gles2/egl/glsl TUs hand down) differ
 * from portablegl's internal auto-incrementing enum ordinals, every GLenum is
 * translated here through small lookup tables before the pgl_* call.  The
 * standard values are spelled GLES2_GL_* (from gles_gl_values.h) since this TU
 * cannot include <GLES2/gl2.h> (it clashes with portablegl.h).
 *
 * Note: the vendored portablegl is a split .h/.c library -- portablegl.h holds
 * declarations + inline math, and the bodies live in portablegl.c, compiled
 * into libportablegl.a.  This TU therefore only *declares* (extern) and calls
 * portablegl; it never re-instantiates the implementation.  The final app links
 * -lGLESv2 -lEGL -lportablegl.
 */
#include <stddef.h>
#include <string.h>
#ifdef PGL_G3D
#include <stdlib.h>
#endif

#include "portablegl/portablegl.h"
#ifdef PGL_G3D
#include "portablegl/pgl_g3d.h"
#endif

#include "../glsl/glsl.h"
#include "gles_gl_values.h"
#include "gles_engine.h"

/* ------------------------------------------------------------------ *
 * Engine state
 * ------------------------------------------------------------------ */
static glContext g_ctx;
static int       g_inited = 0;
static GLsizei   g_w = 0;
static GLsizei   g_h = 0;

/* Texture-unit -> bound texture name tracking.  portablegl's texture2D() takes
 * an explicit texture object name, but a GLES2 sampler2D uniform holds a texture
 * UNIT, so the interpreter's sampling callback resolves unit -> name here. */
static GLuint g_tex_unit[32];
static int    g_active_unit = 0;

#ifdef PGL_G3D
/* Transparent V3D GPU bridge hooks (defined at the bottom of this TU, the only
 * GPU-aware code in the library).  Each is inert unless the active program
 * compiled to one of portablegl's fixed geometry kernels; the wrappers below
 * call them and silently keep the CPU interpreter path otherwise. */
static void gpu_bringup(void);
static int  gpu_try_draw_arrays(GLenum mode, GLint first, GLsizei count);
static void gpu_on_texture(GLuint name, GEenum format, GEenum type,
                           GEint w, GEint h, const void *data);
#endif

int gles_engine_init(GEint w, GEint h)
{
    pix_t *back = NULL;   /* NULL => portablegl allocates the back buffer */
    if (g_inited)
        return 1;
    if (!init_glContext(&g_ctx, &back, (GLsizei)w, (GLsizei)h))
        return 0;
    set_glContext(&g_ctx);
    g_w = w;
    g_h = h;
    g_inited = 1;
    return 1;
}

void gles_engine_fini(void)
{
    if (!g_inited)
        return;
    free_glContext(&g_ctx);
    g_inited = 0;
    g_w = g_h = 0;
}

void gles_engine_make_current(void)
{
    if (g_inited) {
        set_glContext(&g_ctx);
#ifdef PGL_G3D
        /* Adopt the GPU render target as portablegl's back buffer (once) so the
         * compiled-mesh draws and the CPU interpreter fallback share one
         * surface and eglSwapBuffers stays zero-copy.  Inert off raspi5. */
        gpu_bringup();
#endif
    }
}

int gles_engine_resize(GEint w, GEint h)
{
    if (!g_inited)
        return 0;
    if (!pglResizeFramebuffer((GLsizei)w, (GLsizei)h))
        return 0;
    g_w = w;
    g_h = h;
    return 1;
}

void *gles_engine_backbuffer(void)
{
    gles_engine_gpu_flush();   /* land coalesced GPU draws before a blit/readback */
    return pglGetBackBuffer();
}

void gles_engine_backbuffer_size(GEint *w, GEint *h)
{
    if (w) *w = g_w;
    if (h) *h = g_h;
}

int gles_engine_bitdepth(void)
{
    return PGL_BITDEPTH;
}

/* ------------------------------------------------------------------ *
 * Token translation tables (standard GLES2_GL_* -> portablegl GL_*)
 * ------------------------------------------------------------------ */
typedef struct { unsigned int s; unsigned int p; } epair;

#define EMAP(tbl, v) emap((tbl), (unsigned)(sizeof(tbl) / sizeof((tbl)[0])), (v))

static unsigned int emap(const epair *t, unsigned n, unsigned s)
{
    unsigned i;
    for (i = 0; i < n; ++i)
        if (t[i].s == s)
            return t[i].p;
    /* unmapped: pass the standard value through; portablegl will raise
     * GL_INVALID_ENUM rather than silently misinterpret it. */
    return s;
}

static const epair cap_map[] = {
    { GLES2_GL_DEPTH_TEST,    GL_DEPTH_TEST    },
    { GLES2_GL_CULL_FACE,     GL_CULL_FACE     },
    { GLES2_GL_BLEND,         GL_BLEND         },
    { GLES2_GL_SCISSOR_TEST,  GL_SCISSOR_TEST  },
#ifndef PGL_NO_STENCIL
    { GLES2_GL_STENCIL_TEST,  GL_STENCIL_TEST  },
#endif
    { GLES2_GL_POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_FILL },
};

static const epair prim_map[] = {
    { GLES2_GL_POINTS,         GL_POINTS         },
    { GLES2_GL_LINES,          GL_LINES          },
    { GLES2_GL_LINE_STRIP,     GL_LINE_STRIP     },
    { GLES2_GL_LINE_LOOP,      GL_LINE_LOOP      },
    { GLES2_GL_TRIANGLES,      GL_TRIANGLES      },
    { GLES2_GL_TRIANGLE_STRIP, GL_TRIANGLE_STRIP },
    { GLES2_GL_TRIANGLE_FAN,   GL_TRIANGLE_FAN   },
};

static const epair buffer_target_map[] = {
    { GLES2_GL_ARRAY_BUFFER,         GL_ARRAY_BUFFER         },
    { GLES2_GL_ELEMENT_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER },
};

static const epair buffer_usage_map[] = {
    { GLES2_GL_STREAM_DRAW,  GL_STREAM_DRAW  },
    { GLES2_GL_STATIC_DRAW,  GL_STATIC_DRAW  },
    { GLES2_GL_DYNAMIC_DRAW, GL_DYNAMIC_DRAW },
};

static const epair tex_target_map[] = {
    { GLES2_GL_TEXTURE_2D,             GL_TEXTURE_2D             },
    { GLES2_GL_TEXTURE_CUBE_MAP,       GL_TEXTURE_CUBE_MAP       },
    { GLES2_GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_POSITIVE_X },
    { GLES2_GL_TEXTURE_CUBE_MAP_NEGATIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X },
    { GLES2_GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Y },
    { GLES2_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y },
    { GLES2_GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_POSITIVE_Z },
    { GLES2_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z },
};

static const epair cmp_func_map[] = {
    { GLES2_GL_NEVER,    GL_NEVER    },
    { GLES2_GL_LESS,     GL_LESS     },
    { GLES2_GL_EQUAL,    GL_EQUAL    },
    { GLES2_GL_LEQUAL,   GL_LEQUAL   },
    { GLES2_GL_GREATER,  GL_GREATER  },
    { GLES2_GL_NOTEQUAL, GL_NOTEQUAL },
    { GLES2_GL_GEQUAL,   GL_GEQUAL   },
    { GLES2_GL_ALWAYS,   GL_ALWAYS   },
};

static const epair blend_factor_map[] = {
    { GLES2_GL_ZERO,                     GL_ZERO                     },
    { GLES2_GL_ONE,                      GL_ONE                      },
    { GLES2_GL_SRC_COLOR,                GL_SRC_COLOR                },
    { GLES2_GL_ONE_MINUS_SRC_COLOR,      GL_ONE_MINUS_SRC_COLOR      },
    { GLES2_GL_DST_COLOR,                GL_DST_COLOR                },
    { GLES2_GL_ONE_MINUS_DST_COLOR,      GL_ONE_MINUS_DST_COLOR      },
    { GLES2_GL_SRC_ALPHA,                GL_SRC_ALPHA                },
    { GLES2_GL_ONE_MINUS_SRC_ALPHA,      GL_ONE_MINUS_SRC_ALPHA      },
    { GLES2_GL_DST_ALPHA,                GL_DST_ALPHA                },
    { GLES2_GL_ONE_MINUS_DST_ALPHA,      GL_ONE_MINUS_DST_ALPHA      },
    { GLES2_GL_CONSTANT_COLOR,           GL_CONSTANT_COLOR           },
    { GLES2_GL_ONE_MINUS_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR },
    { GLES2_GL_CONSTANT_ALPHA,           GL_CONSTANT_ALPHA           },
    { GLES2_GL_ONE_MINUS_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA },
    { GLES2_GL_SRC_ALPHA_SATURATE,       GL_SRC_ALPHA_SATURATE       },
};

static const epair face_map[] = {
    { GLES2_GL_FRONT,           GL_FRONT           },
    { GLES2_GL_BACK,            GL_BACK            },
    { GLES2_GL_FRONT_AND_BACK,  GL_FRONT_AND_BACK  },
};

static const epair blend_eq_map[] = {
    { GLES2_GL_FUNC_ADD,              GL_FUNC_ADD              },
    { GLES2_GL_FUNC_SUBTRACT,         GL_FUNC_SUBTRACT         },
    { GLES2_GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT },
};

static const epair stencil_op_map[] = {
    { GLES2_GL_KEEP,     GL_KEEP     },
    { GLES2_GL_ZERO,     GL_ZERO     },
    { GLES2_GL_REPLACE,  GL_REPLACE  },
    { GLES2_GL_INCR,     GL_INCR     },
    { GLES2_GL_INCR_WRAP,GL_INCR_WRAP},
    { GLES2_GL_DECR,     GL_DECR     },
    { GLES2_GL_DECR_WRAP,GL_DECR_WRAP},
    { GLES2_GL_INVERT,   GL_INVERT   },
};

static const epair front_face_map[] = {
    { GLES2_GL_CW,  GL_CW  },
    { GLES2_GL_CCW, GL_CCW },
};

static const epair texparam_map[] = {
    { GLES2_GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MIN_FILTER },
    { GLES2_GL_TEXTURE_MAG_FILTER, GL_TEXTURE_MAG_FILTER },
    { GLES2_GL_TEXTURE_WRAP_S,     GL_TEXTURE_WRAP_S     },
    { GLES2_GL_TEXTURE_WRAP_T,     GL_TEXTURE_WRAP_T     },
};

static const epair texfilter_map[] = {
    { GLES2_GL_NEAREST,                GL_NEAREST                },
    { GLES2_GL_LINEAR,                 GL_LINEAR                 },
    { GLES2_GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_NEAREST },
    { GLES2_GL_LINEAR_MIPMAP_NEAREST,  GL_LINEAR_MIPMAP_NEAREST  },
    { GLES2_GL_NEAREST_MIPMAP_LINEAR,  GL_NEAREST_MIPMAP_LINEAR  },
    { GLES2_GL_LINEAR_MIPMAP_LINEAR,   GL_LINEAR_MIPMAP_LINEAR   },
};

static const epair texwrap_map[] = {
    { GLES2_GL_REPEAT,          GL_REPEAT          },
    { GLES2_GL_CLAMP_TO_EDGE,   GL_CLAMP_TO_EDGE   },
    { GLES2_GL_MIRRORED_REPEAT, GL_MIRRORED_REPEAT },
};

static const epair format_map[] = {
    { GLES2_GL_ALPHA,           GL_ALPHA           },
    { GLES2_GL_RGB,             GL_RGB             },
    { GLES2_GL_RGBA,            GL_RGBA            },
    { GLES2_GL_LUMINANCE,       GL_LUMINANCE       },
    { GLES2_GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA },
};

static const epair type_map[] = {
    { GLES2_GL_UNSIGNED_BYTE,  GL_UNSIGNED_BYTE  },
    { GLES2_GL_UNSIGNED_SHORT, GL_UNSIGNED_SHORT },
    { GLES2_GL_FLOAT,          GL_FLOAT          },
};

static const epair pixel_store_map[] = {
    { GLES2_GL_UNPACK_ALIGNMENT, GL_UNPACK_ALIGNMENT },
    { GLES2_GL_PACK_ALIGNMENT,   GL_PACK_ALIGNMENT   },
};

static const epair get_pname_map[] = {
    { GLES2_GL_VIEWPORT,             GL_VIEWPORT             },
    { GLES2_GL_SCISSOR_BOX,          GL_SCISSOR_BOX          },
    { GLES2_GL_MAX_TEXTURE_SIZE,     GL_MAX_TEXTURE_SIZE     },
    { GLES2_GL_ARRAY_BUFFER_BINDING, GL_ARRAY_BUFFER_BINDING },
};

/* ------------------------------------------------------------------ *
 * State / error / query
 * ------------------------------------------------------------------ */
void gles_engine_enable(GEenum cap)  { pgl_Enable(EMAP(cap_map, cap)); }
void gles_engine_disable(GEenum cap) { pgl_Disable(EMAP(cap_map, cap)); }

GEbool gles_engine_is_enabled(GEenum cap)
{
    return (GEbool)pgl_IsEnabled(EMAP(cap_map, cap));
}

void gles_engine_viewport(GEint x, GEint y, GEint w, GEint h)
{
    pgl_Viewport(x, y, w, h);
}

void gles_engine_scissor(GEint x, GEint y, GEint w, GEint h)
{
    pgl_Scissor(x, y, w, h);
}

void gles_engine_color_mask(GEbool r, GEbool g, GEbool b, GEbool a)
{
    pgl_ColorMask(r, g, b, a);
}

void gles_engine_clear_color(GEfloat r, GEfloat g, GEfloat b, GEfloat a)
{
    pgl_ClearColor(r, g, b, a);
}

void gles_engine_clear_depthf(GEfloat d) { pgl_ClearDepthf(d); }
void gles_engine_clear_stencil(GEint s)
{
#ifndef PGL_NO_STENCIL
    pgl_ClearStencil(s);
#else
    (void)s;
#endif
}

void gles_engine_clear(GEenum mask)
{
    GLbitfield m = 0;
    /* Land any coalesced draws from the previous frame before pgl_Clear resets
     * the geom frame boundary (pgl_g3d_begin_frame). */
    gles_engine_gpu_flush();
    if (mask & GLES2_GL_COLOR_BUFFER_BIT)   m |= GL_COLOR_BUFFER_BIT;
    if (mask & GLES2_GL_DEPTH_BUFFER_BIT)   m |= GL_DEPTH_BUFFER_BIT;
#ifndef PGL_NO_STENCIL
    if (mask & GLES2_GL_STENCIL_BUFFER_BIT) m |= GL_STENCIL_BUFFER_BIT;
#endif
    pgl_Clear(m);
}

void gles_engine_depth_func(GEenum func) { pgl_DepthFunc(EMAP(cmp_func_map, func)); }
void gles_engine_depth_mask(GEbool flag) { pgl_DepthMask(flag); }

void gles_engine_blend_func(GEenum sfactor, GEenum dfactor)
{
    pgl_BlendFunc(EMAP(blend_factor_map, sfactor), EMAP(blend_factor_map, dfactor));
}

void gles_engine_blend_color(GEfloat r, GEfloat g, GEfloat b, GEfloat a)
{
    pgl_BlendColor(r, g, b, a);
}

void gles_engine_cull_face(GEenum mode)   { pgl_CullFace(EMAP(face_map, mode)); }
void gles_engine_front_face(GEenum mode)  { pgl_FrontFace(EMAP(front_face_map, mode)); }
void gles_engine_line_width(GEfloat w)    { pgl_LineWidth(w); }

void gles_engine_polygon_offset(GEfloat factor, GEfloat units)
{
    pgl_PolygonOffset(factor, units);
}

void gles_engine_stencil_func(GEenum func, GEint ref, GEuint mask)
{
#ifndef PGL_NO_STENCIL
    pgl_StencilFunc(EMAP(cmp_func_map, func), ref, mask);
#else
    (void)func; (void)ref; (void)mask;
#endif
}

void gles_engine_stencil_op(GEenum sfail, GEenum dpfail, GEenum dppass)
{
#ifndef PGL_NO_STENCIL
    /* stencil ops reuse the same symbolic names as the compare funcs plus
     * KEEP/REPLACE/INCR/DECR/INVERT; map the common ones inline. */
    GLenum s = EMAP(stencil_op_map, sfail);
    GLenum d = EMAP(stencil_op_map, dpfail);
    GLenum p = EMAP(stencil_op_map, dppass);
    pgl_StencilOp(s, d, p);
#else
    (void)sfail; (void)dpfail; (void)dppass;
#endif
}

void gles_engine_stencil_mask(GEuint mask)
{
#ifndef PGL_NO_STENCIL
    pgl_StencilMask(mask);
#else
    (void)mask;
#endif
}

void gles_engine_stencil_func_separate(GEenum face, GEenum func, GEint ref, GEuint mask)
{
#ifndef PGL_NO_STENCIL
    pgl_StencilFuncSeparate(EMAP(face_map, face), EMAP(cmp_func_map, func), ref, mask);
#else
    (void)face; (void)func; (void)ref; (void)mask;
#endif
}

void gles_engine_stencil_op_separate(GEenum face, GEenum sfail, GEenum dpfail, GEenum dppass)
{
#ifndef PGL_NO_STENCIL
    pgl_StencilOpSeparate(EMAP(face_map, face), EMAP(stencil_op_map, sfail),
                          EMAP(stencil_op_map, dpfail), EMAP(stencil_op_map, dppass));
#else
    (void)face; (void)sfail; (void)dpfail; (void)dppass;
#endif
}

void gles_engine_stencil_mask_separate(GEenum face, GEuint mask)
{
#ifndef PGL_NO_STENCIL
    pgl_StencilMaskSeparate(EMAP(face_map, face), mask);
#else
    (void)face; (void)mask;
#endif
}

void gles_engine_blend_func_separate(GEenum srgb, GEenum drgb, GEenum sa, GEenum da)
{
    pgl_BlendFuncSeparate(EMAP(blend_factor_map, srgb), EMAP(blend_factor_map, drgb),
                          EMAP(blend_factor_map, sa),  EMAP(blend_factor_map, da));
}

void gles_engine_blend_equation(GEenum mode)
{
    pgl_BlendEquation(EMAP(blend_eq_map, mode));
}

void gles_engine_blend_equation_separate(GEenum rgb, GEenum alpha)
{
    pgl_BlendEquationSeparate(EMAP(blend_eq_map, rgb), EMAP(blend_eq_map, alpha));
}

void gles_engine_depth_rangef(GEfloat n, GEfloat f)
{
    pgl_DepthRangef(n, f);
}

void gles_engine_read_pixels(GEint x, GEint y, GEint w, GEint h,
                             GEenum format, GEenum type, void *pixels)
{
    u32 *fb;
    unsigned char *dst;
    int row, col;
    (void)format;
    if (!g_inited || !pixels || type != GLES2_GL_UNSIGNED_BYTE) return;
    gles_engine_gpu_flush();   /* land coalesced GPU draws before reading pixels */
    fb = (u32 *)pglGetBackBuffer();
    if (!fb) return;
    dst = (unsigned char *)pixels;
    /* GL framebuffer origin is bottom-left; the back buffer is top-left. */
    for (row = 0; row < h; ++row) {
        int sy = g_h - 1 - (y + row);
        if (sy < 0 || sy >= g_h) { memset(dst + (size_t)row * w * 4, 0, (size_t)w * 4); continue; }
        for (col = 0; col < w; ++col) {
            int sx = x + col;
            u32 p = (sx >= 0 && sx < g_w) ? fb[sy * g_w + sx] : 0;
            dst[((size_t)row * w + col) * 4 + 0] = (unsigned char)((p >> 16) & 0xff); /* R */
            dst[((size_t)row * w + col) * 4 + 1] = (unsigned char)((p >>  8) & 0xff); /* G */
            dst[((size_t)row * w + col) * 4 + 2] = (unsigned char)( p        & 0xff); /* B */
            dst[((size_t)row * w + col) * 4 + 3] = (unsigned char)((p >> 24) & 0xff); /* A */
        }
    }
}

GEenum gles_engine_get_error(void)
{
    switch (pgl_GetError()) {
    case GL_NO_ERROR:                     return GLES2_GL_NO_ERROR;
    case GL_INVALID_ENUM:                 return GLES2_GL_INVALID_ENUM;
    case GL_INVALID_VALUE:                return GLES2_GL_INVALID_VALUE;
    case GL_INVALID_OPERATION:            return GLES2_GL_INVALID_OPERATION;
    case GL_INVALID_FRAMEBUFFER_OPERATION:return GLES2_GL_INVALID_FRAMEBUFFER_OPERATION;
    case GL_OUT_OF_MEMORY:                return GLES2_GL_OUT_OF_MEMORY;
    default:                              return GLES2_GL_NO_ERROR;
    }
}

static const GLubyte g_ext_string[] = "";

const uint8_t *gles_engine_get_string(GEenum name)
{
    switch (name) {
    case GLES2_GL_VENDOR:                   return pgl_GetString(GL_VENDOR);
    case GLES2_GL_RENDERER:                 return pgl_GetString(GL_RENDERER);
    case GLES2_GL_VERSION:                  return pgl_GetString(GL_VERSION);
    case GLES2_GL_SHADING_LANGUAGE_VERSION: return pgl_GetString(GL_SHADING_LANGUAGE_VERSION);
    case GLES2_GL_EXTENSIONS:               return g_ext_string;
    default:                                return pgl_GetString(GL_VERSION);
    }
}

void gles_engine_get_integerv(GEenum pname, GEint *data)
{
    if (pname == GLES2_GL_MAX_VERTEX_ATTRIBS) {
        if (data) *data = GL_MAX_VERTEX_ATTRIBS;
        return;
    }
    pgl_GetIntegerv(EMAP(get_pname_map, pname), data);
}

void gles_engine_get_floatv(GEenum pname, GEfloat *data)
{
    pgl_GetFloatv(EMAP(get_pname_map, pname), data);
}

void gles_engine_get_booleanv(GEenum pname, GEbool *data)
{
    pgl_GetBooleanv(EMAP(get_pname_map, pname), data);
}

/* ------------------------------------------------------------------ *
 * Buffers
 * ------------------------------------------------------------------ */
void gles_engine_gen_buffers(GEint n, GEuint *buffers)    { pgl_GenBuffers(n, buffers); }
void gles_engine_delete_buffers(GEint n, const GEuint *b) { pgl_DeleteBuffers(n, b); }

void gles_engine_bind_buffer(GEenum target, GEuint buffer)
{
    pgl_BindBuffer(EMAP(buffer_target_map, target), buffer);
}

void gles_engine_buffer_data(GEenum target, GEiptr size, const void *data, GEenum usage)
{
    pgl_BufferData(EMAP(buffer_target_map, target), (GLsizeiptr)size, data,
                   EMAP(buffer_usage_map, usage));
}

void gles_engine_buffer_sub_data(GEenum target, GEiptr offset, GEiptr size, const void *data)
{
    pgl_BufferSubData(EMAP(buffer_target_map, target), (GLintptr)offset,
                      (GLsizeiptr)size, data);
}

/* ------------------------------------------------------------------ *
 * Textures
 * ------------------------------------------------------------------ */
void gles_engine_gen_textures(GEint n, GEuint *textures)    { pgl_GenTextures(n, textures); }
void gles_engine_delete_textures(GEint n, const GEuint *t)  { pgl_DeleteTextures(n, t); }

void gles_engine_bind_texture(GEenum target, GEuint texture)
{
    pgl_BindTexture(EMAP(tex_target_map, target), texture);
    /* track the 2D binding per active unit for the interpreter's sampler */
    if (target == GLES2_GL_TEXTURE_2D && g_active_unit >= 0 && g_active_unit < 32)
        g_tex_unit[g_active_unit] = texture;
}

void gles_engine_active_texture(GEenum texture)
{
    /* GL_TEXTURE0..GL_TEXTURE31 are consecutive in both enums. */
    if (texture >= GLES2_GL_TEXTURE0 && texture < GLES2_GL_TEXTURE0 + 32) {
        g_active_unit = (int)(texture - GLES2_GL_TEXTURE0);
        pgl_ActiveTexture(GL_TEXTURE0 + (texture - GLES2_GL_TEXTURE0));
    } else {
        g_active_unit = 0;
        pgl_ActiveTexture(GL_TEXTURE0);
    }
}

GEuint gles_engine_texture_for_unit(GEint unit)
{
    if (unit < 0 || unit >= 32) return 0;
    return g_tex_unit[unit];
}

void gles_engine_tex_parameteri(GEenum target, GEenum pname, GEint param)
{
    GLint v;
    switch (pname) {
    case GLES2_GL_TEXTURE_MIN_FILTER:
    case GLES2_GL_TEXTURE_MAG_FILTER:
        v = (GLint)EMAP(texfilter_map, (unsigned)param);
        break;
    case GLES2_GL_TEXTURE_WRAP_S:
    case GLES2_GL_TEXTURE_WRAP_T:
        v = (GLint)EMAP(texwrap_map, (unsigned)param);
        break;
    default:
        v = param;
        break;
    }
    pgl_TexParameteri(EMAP(tex_target_map, target), EMAP(texparam_map, pname), v);
}

void gles_engine_tex_parameterf(GEenum target, GEenum pname, GEfloat param)
{
    pgl_TexParameterf(EMAP(tex_target_map, target), EMAP(texparam_map, pname), param);
}

void gles_engine_pixel_storei(GEenum pname, GEint param)
{
    pgl_PixelStorei(EMAP(pixel_store_map, pname), param);
}

void gles_engine_tex_image_2d(GEenum target, GEint level, GEint internalformat,
                              GEint w, GEint h, GEint border,
                              GEenum format, GEenum type, const void *data)
{
    pgl_TexImage2D(EMAP(tex_target_map, target), level,
                   (GLint)EMAP(format_map, (unsigned)internalformat),
                   w, h, border,
                   EMAP(format_map, format), EMAP(type_map, type), data);
#ifdef PGL_G3D
    /* Mirror a 2D RGBA8 image into a GPU raster-texture slot so the textured
     * geometry kernel can sample it; the CPU interpreter keeps using the
     * portablegl texture uploaded above.  Inert off raspi5. */
    if (target == GLES2_GL_TEXTURE_2D && g_active_unit >= 0 && g_active_unit < 32)
        gpu_on_texture(g_tex_unit[g_active_unit], format, type, w, h, data);
#endif
}

void gles_engine_tex_sub_image_2d(GEenum target, GEint level, GEint xoff, GEint yoff,
                                  GEint w, GEint h, GEenum format, GEenum type,
                                  const void *data)
{
    pgl_TexSubImage2D(EMAP(tex_target_map, target), level, xoff, yoff, w, h,
                      EMAP(format_map, format), EMAP(type_map, type), data);
}

void gles_engine_generate_mipmap(GEenum target)
{
    pgl_GenerateMipmap(EMAP(tex_target_map, target));
}

/* ------------------------------------------------------------------ *
 * Vertex attributes / draw
 * ------------------------------------------------------------------ */
void gles_engine_vertex_attrib_pointer(GEuint index, GEint size, GEenum type,
                                       GEbool normalized, GEint stride,
                                       const void *pointer)
{
    pgl_VertexAttribPointer(index, size, EMAP(type_map, type), normalized, stride, pointer);
}

void gles_engine_enable_vertex_attrib_array(GEuint index)  { pgl_EnableVertexAttribArray(index); }
void gles_engine_disable_vertex_attrib_array(GEuint index) { pgl_DisableVertexAttribArray(index); }

void gles_engine_draw_arrays(GEenum mode, GEint first, GEint count)
{
#ifdef PGL_G3D
    /* Route an eligible draw onto the V3D geometry kernel; a 0 return means the
     * program is CPU-only (never compiled, declined, or a non-triangle mode) and
     * the interpreter draws it unchanged.  Inert off raspi5. */
    if (gpu_try_draw_arrays(EMAP(prim_map, mode), first, count))
        return;
    /* Not GPU-eligible (or declined): drain any pending coalesced geometry batch
     * first so the CPU draw below lands after it (painter's order). */
    gles_engine_gpu_flush();
#endif
    pgl_DrawArrays(EMAP(prim_map, mode), first, count);
}

void gles_engine_draw_elements(GEenum mode, GEint count, GEenum type, const void *indices)
{
#ifdef PGL_G3D
    /* Indexed draws stay on the CPU interpreter: drain any pending coalesced
     * geometry batch first so this draw lands after it (painter's order). */
    gles_engine_gpu_flush();
#endif
    pgl_DrawElements(EMAP(prim_map, mode), count, EMAP(type_map, type), indices);
}

/* ------------------------------------------------------------------ *
 * Programs + CPU GLSL ES interpreter bridge
 *
 * portablegl shaders are C function pointers, so a single pair of trampolines
 * is shared by every interpreted program; the currently-active program's
 * glsl_program + uniform block are looked up from a small registry (set by
 * gles_engine_use_program).  Rendering is single-threaded per context, so one
 * `g_active` pointer is sufficient and keeps the per-pixel path O(1).
 * ------------------------------------------------------------------ */
#define GLES_MAX_INTERP 32
typedef struct {
    GLuint         pgl_id;
    glsl_program  *gp;
    glsl_cell     *ublock;
    int            in_use;
    /* Native gouraud fast-path (see match_gouraud): when `gouraud` is set the
     * program runs on ff_gouraud_vert/ff_gouraud_frag instead of the tree-
     * walking interpreter, reading the MVP mat4 straight out of the uniform
     * block and passing attribute g_color_loc through the single vec4 varying. */
    int            gouraud;
    int            g_pos_loc;    /* vec4 position attribute location */
    int            g_color_loc;  /* vec4 colour attribute location   */
    int            g_mvp_off;    /* ublock float offset of the mat4 MVP */
    /* Native textured-blit fast-path (see match_texblit): when `texblit` is set
     * the FS runs on ff_texblit_frag instead of the interpreter, sampling the
     * g_sampler_off sampler at the g_uv_slot varying's (s,t).  The VS stays
     * interpreted - a fullscreen blit issues only a handful of vertices. */
    int            texblit;
    int            g_sampler_off; /* ublock cell offset of the sampler2D uniform */
    int            g_uv_slot;     /* varying slot supplying the vec2 texcoord    */
} interp_entry;

static interp_entry  g_reg[GLES_MAX_INTERP];
static interp_entry *g_active = NULL;

static interp_entry *find_entry(GLuint id)
{
    int i;
    for (i = 0; i < GLES_MAX_INTERP; ++i)
        if (g_reg[i].in_use && g_reg[i].pgl_id == id) return &g_reg[i];
    return NULL;
}

/* texture sampling callback: resolve unit -> portablegl texture name, sample. */
static void bridge_tex_sample(int unit, int dim, const float *coord,
                              float out_rgba[4], void *user)
{
    GLuint tex = gles_engine_texture_for_unit(unit);
    (void)user;
    if (dim == 3) {
        vec4 c = texture_cubemap(tex, coord[0], coord[1], coord[2]);
        out_rgba[0] = c.x; out_rgba[1] = c.y; out_rgba[2] = c.z; out_rgba[3] = c.w;
    } else {
        vec4 c = texture2D(tex, coord[0], coord[1]);
        out_rgba[0] = c.x; out_rgba[1] = c.y; out_rgba[2] = c.z; out_rgba[3] = c.w;
    }
}

static void interp_vert(float *vs_output, vec4 *vertex_attribs,
                        Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    const float *attribs[GLSL_MAX_ATTRIBS];
    int i, n;
    (void)uniforms;
    if (!e || !e->gp) return;
    n = e->gp->nattr_components;
    if (n > GLSL_MAX_ATTRIBS) n = GLSL_MAX_ATTRIBS;
    for (i = 0; i < n; ++i) attribs[i] = &vertex_attribs[i].x;
    glsl_run_vertex(e->gp, attribs, n, e->ublock, vs_output,
                    (float *)&builtins->gl_Position, builtins->gl_InstanceID);
}

static void interp_frag(float *fs_input, Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    int disc = 0, wrote_depth = 0;
    float fdepth = 0;
    (void)uniforms;
    if (!e || !e->gp) return;
    glsl_run_fragment(e->gp, fs_input, e->ublock,
                      (const float *)&builtins->gl_FragCoord,
                      (const float *)&builtins->gl_PointCoord,
                      builtins->gl_FrontFacing, bridge_tex_sample, e,
                      (float *)&builtins->gl_FragColor, &disc, &fdepth, &wrote_depth);
    if (disc) builtins->discard = GL_TRUE;
    if (wrote_depth) builtins->gl_FragDepth = fdepth;
}

void gles_engine_use_program(GEuint program)
{
    gles_engine_gpu_flush();   /* drain the outgoing program's coalesced draws */
    pgl_UseProgram(program);
    g_active = find_entry(program);
}

void gles_engine_delete_program(GEuint program)
{
    interp_entry *e = find_entry(program);
    if (e) { e->in_use = 0; if (g_active == e) g_active = NULL; }
    pgl_DeleteProgram(program);
}

void gles_engine_release_interpreted_program(GEuint program)
{
    interp_entry *e = find_entry(program);
    if (e) { e->in_use = 0; if (g_active == e) g_active = NULL; }
}

/* ------------------------------------------------------------------ *
 * Native gouraud fast-path
 *
 * The CPU GLSL interpreter costs ~5 us per vertex on a lighting shader, which
 * makes vertex-heavy meshes (e.g. the 19200-vertex Utah teapot) an order of
 * magnitude slower than the same scene on portablegl's compiled C shaders.  A
 * program whose VS is exactly
 *      vColor = aColor;  gl_Position = uMVP * aPosition;
 * and whose FS is exactly
 *      gl_FragColor = vColor;
 * (one smooth vec4 varying, no discard/fragdepth) is mathematically identical
 * to portablegl's built-in gouraud pair, so it can run on the two compiled C
 * trampolines below instead of the interpreter.  match_gouraud() recognises
 * that exact shape at link time; anything else keeps the interpreter, so the
 * fast-path can never change pixels - only skip the tree-walk.  The native VS
 * reproduces the interpreter's column-major mat*vec bit-for-bit:
 *      gl_Position[c] = sum_r M[r*4 + c] * pos[r].
 * ------------------------------------------------------------------ */
static void ff_gouraud_vert(float *vs_output, vec4 *vertex_attribs,
                            Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    const glsl_cell *u;
    const float *p;
    int r, cc;
    (void)uniforms;
    if (!e) return;
    ((vec4 *)vs_output)[0] = vertex_attribs[e->g_color_loc];
    u = e->ublock;
    p = &vertex_attribs[e->g_pos_loc].x;
    for (cc = 0; cc < 4; ++cc) {
        float s = 0.0f;
        for (r = 0; r < 4; ++r) s += u[e->g_mvp_off + r * 4 + cc].f * p[r];
        if (cc == 0) builtins->gl_Position.x = s;
        else if (cc == 1) builtins->gl_Position.y = s;
        else if (cc == 2) builtins->gl_Position.z = s;
        else builtins->gl_Position.w = s;
    }
}

static void ff_gouraud_frag(float *fs_input, Shader_Builtins *builtins, void *uniforms)
{
    (void)uniforms;
    builtins->gl_FragColor = ((vec4 *)fs_input)[0];
}

/* Native textured-blit fragment (see match_texblit).  Reproduces interp_frag's
 * `gl_FragColor = texture2D(<sampler>, <varying>)` exactly - the same unit->name
 * resolution as bridge_tex_sample (the sampler uniform cell holds the texture
 * unit as an int, since T_SAMPLER2D is int-family) and the same portablegl
 * texture2D fetch - but without the per-pixel tree-walk.  A fullscreen blit
 * issues one of these per covered pixel, so skipping the interpreter is what
 * keeps the CPU-composited demos (Fireworks, matrix) at speed after they moved
 * off SDL_RenderCopy onto a GLES2 fullscreen textured quad. */
static void ff_texblit_frag(float *fs_input, Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    vec4 c;
    (void)uniforms;
    if (!e) return;
    c = texture2D(gles_engine_texture_for_unit(e->ublock[e->g_sampler_off].i),
                  fs_input[e->g_uv_slot * 4 + 0], fs_input[e->g_uv_slot * 4 + 1]);
    builtins->gl_FragColor = c;
}

static int match_gouraud(const glsl_program *gp, int *pos_loc, int *color_loc,
                         int *mvp_off)
{
    const glsl_stage *vs, *fs;
    const glsl_func *vfn, *ffn;
    const glsl_stmt *s;
    const glsl_expr *e;
    int nstmt, have_pos = 0, have_col = 0;
    int pl = -1, cl = -1, mo = -1;

    if (!gp || !gp->linked || !gp->vs || !gp->fs) return 0;
    if (gp->nvaryings != 1) return 0;
    if (gp->varyings[0].ncomp != 4 || gp->varyings[0].interp != 0) return 0;
    vs = gp->vs; fs = gp->fs;
    if (fs->uses_discard || fs->uses_fragdepth) return 0;
    if (vs->main_idx < 0 || fs->main_idx < 0) return 0;
    vfn = &vs->funcs[vs->main_idx];
    ffn = &fs->funcs[fs->main_idx];
    if (!vfn->body || !ffn->body) return 0;

    /* FS: a single `gl_FragColor = <varying0>;` */
    s = ffn->body->children;
    if (!s || s->next || s->kind != ST_EXPR || !s->expr) return 0;
    e = s->expr;
    if (e->kind != EX_ASSIGN || !e->a || !e->b) return 0;
    if (e->a->kind != EX_VAR || e->a->var_scope != VARSCOPE_BUILTIN ||
        e->a->var_slot != BI_GL_FRAGCOLOR) return 0;
    if (e->b->kind != EX_VAR || e->b->var_scope != VARSCOPE_VARYING ||
        e->b->var_slot != 0) return 0;

    /* VS: exactly two plain assignments, in either order */
    for (s = vfn->body->children, nstmt = 0; s; s = s->next, ++nstmt) {
        if (s->kind != ST_EXPR || !s->expr || s->expr->kind != EX_ASSIGN) return 0;
        e = s->expr;
        if (!e->a || e->a->kind != EX_VAR || !e->b) return 0;
        if (e->a->var_scope == VARSCOPE_VARYING && e->a->var_slot == 0 &&
            e->b->kind == EX_VAR && e->b->var_scope == VARSCOPE_ATTRIB) {
            cl = e->b->var_slot; have_col = 1; continue;   /* vColor = aColor */
        }
        if (e->a->var_scope == VARSCOPE_BUILTIN && e->a->var_slot == BI_GL_POSITION &&
            e->b->kind == EX_MUL && e->b->a && e->b->b &&
            e->b->a->kind == EX_VAR && e->b->a->var_scope == VARSCOPE_UNIFORM &&
            e->b->b->kind == EX_VAR && e->b->b->var_scope == VARSCOPE_ATTRIB) {
            int uslot = e->b->a->var_slot;
            if (uslot < 0 || uslot >= gp->nuniforms) return 0;
            if (gp->uniforms[uslot].type != T_MAT4 || gp->uniforms[uslot].ncomp != 16) return 0;
            mo = gp->uniforms[uslot].offset; pl = e->b->b->var_slot; have_pos = 1;
            continue;                                        /* gl_Position = uMVP * aPos */
        }
        return 0;
    }
    if (nstmt != 2 || !have_pos || !have_col || pl == cl) return 0;
    if (pl < 0 || pl >= gp->nattribs || gp->attribs[pl].size != 4) return 0;
    if (cl < 0 || cl >= gp->nattribs || gp->attribs[cl].size != 4) return 0;

    *pos_loc = pl; *color_loc = cl; *mvp_off = mo;
    return 1;
}

/* Recognise a fragment shader that is exactly
 *      gl_FragColor = texture2D(<sampler2D uniform>, <vec2+ varying>);
 * (single statement, no discard/fragdepth, no arithmetic or swizzle on the
 * sample).  This is the fullscreen-blit shape the CPU-composited demos draw
 * every frame; sampling it natively instead of through the interpreter removes
 * the per-pixel tree-walk.  The vertex shader is deliberately NOT constrained:
 * ff_texblit_frag reads the same interpolated varying the interpreter would, so
 * any VS feeding varying g_uv_slot stays pixel-identical.  Returns 1 and reports
 * the sampler's ublock offset + the texcoord varying slot. */
static int match_texblit(const glsl_program *gp, int *sampler_off, int *uv_slot)
{
    const glsl_stage *fs;
    const glsl_func *ffn;
    const glsl_stmt *s;
    const glsl_expr *e, *call, *a0, *a1;
    int uslot, vslot;

    if (!gp || !gp->linked || !gp->fs) return 0;
    fs = gp->fs;
    if (fs->uses_discard || fs->uses_fragdepth) return 0;
    if (fs->main_idx < 0) return 0;
    ffn = &fs->funcs[fs->main_idx];
    if (!ffn->body) return 0;

    /* FS: a single `gl_FragColor = texture2D(...);` */
    s = ffn->body->children;
    if (!s || s->next || s->kind != ST_EXPR || !s->expr) return 0;
    e = s->expr;
    if (e->kind != EX_ASSIGN || !e->a || !e->b) return 0;
    if (e->a->kind != EX_VAR || e->a->var_scope != VARSCOPE_BUILTIN ||
        e->a->var_slot != BI_GL_FRAGCOLOR) return 0;

    call = e->b;
    if (call->kind != EX_CALL || call->is_user_fn || call->op_builtin != BF_TEXTURE2D)
        return 0;
    if (call->nargs != 2 || !call->args) return 0;
    a0 = call->args[0]; a1 = call->args[1];
    if (!a0 || !a1) return 0;

    /* arg0: the sampler2D uniform (its cell holds the texture unit as an int) */
    if (a0->kind != EX_VAR || a0->var_scope != VARSCOPE_UNIFORM) return 0;
    uslot = a0->var_slot;
    if (uslot < 0 || uslot >= gp->nuniforms) return 0;
    if (!gp->uniforms[uslot].is_sampler || gp->uniforms[uslot].type != T_SAMPLER2D)
        return 0;

    /* arg1: a vec2 (or wider) varying supplying (s,t) */
    if (a1->kind != EX_VAR || a1->var_scope != VARSCOPE_VARYING) return 0;
    vslot = a1->var_slot;
    if (vslot < 0 || vslot >= gp->nvaryings) return 0;
    if (gp->varyings[vslot].ncomp < 2) return 0;

    *sampler_off = gp->uniforms[uslot].offset;
    *uv_slot = vslot;
    return 1;
}

GEuint gles_engine_create_interpreted_program(struct glsl_program *gp,
                                              void *ublock, GEint ublock_cells)
{
    GLenum interp[GLSL_MAX_VARYINGS * 4];
    interp_entry *e = NULL;
    GLuint id;
    int i, n;
    int gouraud, pos_loc = 0, color_loc = 0, mvp_off = 0;
    int texblit, sampler_off = 0, uv_slot = 0;
    GLboolean depth_or_discard;
    (void)ublock_cells;
    if (!gp || !gp->linked) return 0;
    for (i = 0; i < GLES_MAX_INTERP; ++i)
        if (!g_reg[i].in_use) { e = &g_reg[i]; break; }
    if (!e) return 0;
    n = gp->nvaryings * 4;
    if (n > GLSL_MAX_VARYINGS * 4) n = GLSL_MAX_VARYINGS * 4;
    for (i = 0; i < n; ++i) interp[i] = PGL_SMOOTH;
    depth_or_discard = (gp->fs && (gp->fs->uses_discard || gp->fs->uses_fragdepth))
                       ? GL_TRUE : GL_FALSE;
    gouraud = match_gouraud(gp, &pos_loc, &color_loc, &mvp_off);
    texblit = gouraud ? 0 : match_texblit(gp, &sampler_off, &uv_slot);
    id = pglCreateProgram(gouraud ? ff_gouraud_vert : interp_vert,
                          gouraud ? ff_gouraud_frag
                                  : (texblit ? ff_texblit_frag : interp_frag),
                          (GLsizei)n, interp, depth_or_discard);
    if (!id) return 0;
    e->pgl_id = id; e->gp = gp; e->ublock = (glsl_cell *)ublock; e->in_use = 1;
    e->gouraud = gouraud;
    e->g_pos_loc = gouraud ? pos_loc : 0;
    e->g_color_loc = gouraud ? color_loc : 1;
    e->g_mvp_off = gouraud ? mvp_off : 0;
    e->texblit = texblit;
    e->g_sampler_off = texblit ? sampler_off : 0;
    e->g_uv_slot = texblit ? uv_slot : 0;
    return id;
}

/* ================================================================== *
 * Transparent V3D GPU (PGL_G3D) bridge
 *
 * Compiled ONLY under -DPGL_G3D (raspi5); this is the sole GPU-aware code
 * in libGLESv2 and the only place that includes pgl_g3d.h.  At
 * glLinkProgram the program's GLSL is handed to pgl_g3d_program_compile;
 * when it resolves to one of portablegl's fixed two-pass geometry kernels
 * (signalled by the xform kernel exposing uMVP) the program is eligible,
 * and each eligible glDrawArrays is tessellated to a triangle list,
 * staged as pos+normal (gouraud) or pos+uv (textured), and dispatched on
 * the QPU with its own uMVP/uNormalMat/uMaterial/uLightEye/uShade.  A
 * program that does not compile to a kernel, or whose first dispatch is
 * declined, is latched CPU-only and falls through to pgl_DrawArrays (the
 * interpreter) - identical pixels, so an app never learns whether the GPU
 * ran.  Non-triangle modes and indexed draws (no soft3d consumer uses
 * glDrawElements; the fixed kernels are vertex-range based) stay on the
 * CPU interpreter.
 *
 * Static constructors are NOT run here: libewoksys _start (cmain.c) runs
 * the image's .init_array via run_init_array() immediately before main(),
 * so the mesa compiler's C++ globals are already constructed by the time
 * any app reaches glLinkProgram.  Running them again would double-invoke
 * C++ global constructors.
 * ================================================================== */
#ifdef PGL_G3D

#define GPU_STAGING_STRIDE 6   /* pos.xyz + (normal.xyz | u,v,pad) */
#define GPU_NUNIF          6   /* uMVP .. uShade */

/* The lighting uniforms the gouraud xform kernel reads, matched by name
 * between the compiled kernel (dword offset via pgl_g3d_shader_uniform_dword)
 * and the app's GLSL program (uniform-block float offset).  The textured xform
 * kernel declares only uMVP, so the other five resolve to -1 and are skipped;
 * pgl_g3d_draw_mesh_compiled auto-patches uViewportWH/uFbStride/uTriFirst/
 * uTriCount/uVertCount (+uTexW/uTexH) into the same blob. */
static const char *const g_gpu_uname[GPU_NUNIF] = {
    "uMVP", "uModelView", "uNormalMat", "uMaterial", "uLightEye", "uShade"
};

typedef struct {
    GLuint        pgl_id;
    pgl_g3d_prog  prog;
    int           eligible;      /* compiled to a geometry kernel         */
    int           textured;      /* attr1 is uv; needs a raster texture   */
    int           cpu_latched;   /* a dispatch declined: stay on the CPU  */
    int           unif_resolved;
    int           in_use;
    int           koff[GPU_NUNIF], knc[GPU_NUNIF];  /* kernel dword off / ncomp */
    int           aoff[GPU_NUNIF], anc[GPU_NUNIF];  /* app ublock float off / ncomp */
} gpu_entry;

static gpu_entry g_gpu[GLES_MAX_INTERP];
static int       g_gpu_ready = 0;
/* Set once a program links to a GPU-eligible geometry kernel.  gpu_bringup()
 * only adopts the pgl_g3d render target as portablegl's back buffer after this
 * is set: that target is contig NOCACHE (uncached) shm so the GPU can DMA it,
 * and a pure-CPU app (no uMVP -> never eligible, e.g. the fullscreen-quad
 * texblit of Fireworks/matrix) would otherwise rasterise every pixel into
 * uncached memory - DRAM-bound with no write-combining, ~an order of magnitude
 * slower than the cached malloc back buffer it uses off raspi5.  Deferring the
 * adoption keeps GPU apps byte-identical (they set this before their first
 * draw) while CPU apps never touch NOCACHE. */
static int       g_gpu_has_eligible = 0;

/* texture-name -> raster-slot map for the textured kernel (PGL_G3D_MAX_TEX) */
static GLuint  g_gpu_texname[PGL_G3D_MAX_TEX];
static int     g_gpu_texused[PGL_G3D_MAX_TEX];

/* one reusable interleaved staging buffer (grown on demand) */
static float  *g_gpu_stage = NULL;
static int     g_gpu_stage_cap = 0;   /* in vertices */

typedef struct { float *v; int n; int cap; } gpu_stage;

/* Cross-draw coalescing state (the gles_engine.h contract: "the GPU path merges
 * consecutive same-state draws into one dispatch").  Eligible untextured geom
 * draws accumulate their tessellated triangles into g_gpu_stage and are
 * submitted as ONE mesh when the render key (program / uniform blob / depth /
 * cull) changes, when a non-coalescable draw or readback occurs, or at the frame
 * boundary (gles_engine_gpu_flush).  Every V3D geom dispatch pays a large fixed
 * cost (~2.4MB colour+depth L2 clean/invalidate + blocking IPC + CSD_DONE IRQ)
 * independent of triangle count, so merging e.g. gears' 532 per-strip draws into
 * 6 per-gear dispatches (uniforms change once per gear) is the entire win. */
static int          g_coalesce_active = 0;
static int          g_coalesce_n = 0;                    /* accumulated vertices */
static uint32_t     g_coalesce_blob[PGL_G3D_MAX_UNIF];   /* open batch's uniforms */
static pgl_g3d_prog g_coalesce_prog = PGL_G3D_PROG_INVALID;
static gpu_entry   *g_coalesce_entry = NULL;
static uint32_t     g_coalesce_depth, g_coalesce_mask, g_coalesce_cull;

static gpu_entry *gpu_find_entry(GLuint id)
{
    int i;
    for (i = 0; i < GLES_MAX_INTERP; ++i)
        if (g_gpu[i].in_use && g_gpu[i].pgl_id == id) return &g_gpu[i];
    return NULL;
}

static gpu_entry *gpu_alloc_entry(GLuint id)
{
    gpu_entry *e = gpu_find_entry(id);
    int i, k;
    if (e) return e;
    for (i = 0; i < GLES_MAX_INTERP; ++i) {
        if (!g_gpu[i].in_use) {
            e = &g_gpu[i];
            memset(e, 0, sizeof(*e));
            e->pgl_id = id;
            e->prog = PGL_G3D_PROG_INVALID;
            for (k = 0; k < GPU_NUNIF; ++k) { e->koff[k] = -1; e->aoff[k] = -1; }
            e->in_use = 1;
            return e;
        }
    }
    return NULL;
}

static void gpu_bringup(void)
{
    if (g_gpu_ready || !g_inited) return;
    /* No GPU-eligible program yet: keep portablegl's cached malloc back buffer.
     * Adopting the NOCACHE pgl_g3d target now would make a pure-CPU app render
     * every pixel uncached (see g_gpu_has_eligible). */
    if (!g_gpu_has_eligible) return;
    if (pglGpuBegin(g_w, g_h))
        g_gpu_ready = 1;
}

static int gpu_tex_slot(GLuint name, int alloc)
{
    int i;
    if (name == 0) return -1;
    for (i = 0; i < PGL_G3D_MAX_TEX; ++i)
        if (g_gpu_texused[i] && g_gpu_texname[i] == name) return i;
    if (!alloc) return -1;
    for (i = 0; i < PGL_G3D_MAX_TEX; ++i)
        if (!g_gpu_texused[i]) { g_gpu_texused[i] = 1; g_gpu_texname[i] = name; return i; }
    return -1;   /* slot table full: this texture stays CPU-only */
}

static void gpu_on_texture(GLuint name, GEenum format, GEenum type,
                           GEint w, GEint h, const void *data)
{
    const unsigned char *s;
    uint32_t *rgba;
    size_t n, i;
    int slot;
    gpu_bringup();
    if (!g_gpu_ready || !data || w <= 0 || h <= 0) return;
    /* only a tightly-packed RGBA8 image maps to the kernel's raster uint32 texel */
    if (format != GLES2_GL_RGBA || type != GLES2_GL_UNSIGNED_BYTE) return;
    slot = gpu_tex_slot(name, 1);
    if (slot < 0) return;
    n = (size_t)w * (size_t)h;
    rgba = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!rgba) { g_gpu_texused[slot] = 0; return; }
    /* The geom-tex kernel writes the fetched texel word to the fb verbatim, so
     * it must already be in the fb's 0xAARRGGBB packing.  A GL_RGBA/UBYTE image
     * is [R,G,B,A] bytes; rebuild each word as (A<<24)|(R<<16)|(G<<8)|B so the
     * GPU texel equals the CPU-sampled colour. */
    s = (const unsigned char *)data;
    for (i = 0; i < n; ++i)
        rgba[i] = ((uint32_t)s[i*4+3] << 24) | ((uint32_t)s[i*4+0] << 16) |
                  ((uint32_t)s[i*4+1] << 8)  |  (uint32_t)s[i*4+2];
    if (!pgl_g3d_upload_texture(slot, w, h, rgba))
        g_gpu_texused[slot] = 0;   /* not fatal: the textured draw declines to CPU */
    free(rgba);
}

static uint32_t gpu_depth_code(void)
{
    if (!g_ctx.depth_test) return PGL_G3D_DEPTH_NONE;
    switch (g_ctx.depth_func) {
    case GL_NEVER:    return PGL_G3D_DEPTH_NEVER;
    case GL_LESS:     return PGL_G3D_DEPTH_LESS;
    case GL_EQUAL:    return PGL_G3D_DEPTH_EQUAL;
    case GL_LEQUAL:   return PGL_G3D_DEPTH_LEQUAL;
    case GL_GREATER:  return PGL_G3D_DEPTH_GREATER;
    case GL_NOTEQUAL: return PGL_G3D_DEPTH_NOTEQUAL;
    case GL_GEQUAL:   return PGL_G3D_DEPTH_GEQUAL;
    case GL_ALWAYS:   return PGL_G3D_DEPTH_ALWAYS;
    default:          return PGL_G3D_DEPTH_LESS;
    }
}

static uint32_t gpu_cull_code(void)
{
    int ccw;
    if (!g_ctx.cull_face) return PGL_G3D_CULL_NONE;
    ccw = (g_ctx.front_face == GL_CCW);
    switch (g_ctx.cull_mode) {
    case GL_BACK:           return ccw ? PGL_G3D_CULL_BACK_CCW  : PGL_G3D_CULL_BACK_CW;
    case GL_FRONT:          return ccw ? PGL_G3D_CULL_FRONT_CCW : PGL_G3D_CULL_FRONT_CW;
    case GL_FRONT_AND_BACK: return PGL_G3D_CULL_BOTH;
    default:                return PGL_G3D_CULL_NONE;
    }
}

static int gpu_stage_reserve(int nverts)
{
    if (g_gpu_stage && nverts <= g_gpu_stage_cap) return 1;
    free(g_gpu_stage);
    g_gpu_stage = (float *)malloc((size_t)nverts * GPU_STAGING_STRIDE * sizeof(float));
    if (!g_gpu_stage) { g_gpu_stage_cap = 0; return 0; }
    g_gpu_stage_cap = nverts;
    return 1;
}

/* read ncomp floats of one GL_FLOAT vertex attribute for buffer vertex vi */
static int gpu_read_attrib(const glVertex_Attrib *v, GLsizei vi, float *out, int ncomp)
{
    const glBuffer *b;
    GLsizeiptr off;
    if (v->type != GL_FLOAT || v->size < ncomp) return 0;
    if (v->buf >= (GLuint)g_ctx.buffers.size) return 0;
    b = &g_ctx.buffers.a[v->buf];
    if (!b->data) return 0;
    off = v->offset + (GLsizeiptr)v->stride * (GLsizeiptr)vi;
    if (off < 0 || off + (GLsizeiptr)ncomp * 4 > (GLsizeiptr)b->size) return 0;
    memcpy(out, b->data + off, (size_t)ncomp * sizeof(float));
    return 1;
}

/* append one triangle (three buffer vertex indices) to the interleaved staging */
static int gpu_stage_tri(gpu_stage *st, const glVertex_Attrib *a0,
                         const glVertex_Attrib *a1, int textured,
                         GLsizei i0, GLsizei i1, GLsizei i2)
{
    GLsizei idx[3];
    float p[3], q[3], *dst;
    int k;
    if (st->n + 3 > st->cap) return 0;
    idx[0] = i0; idx[1] = i1; idx[2] = i2;
    for (k = 0; k < 3; ++k) {
        dst = st->v + (size_t)(st->n + k) * GPU_STAGING_STRIDE;
        if (!gpu_read_attrib(a0, idx[k], p, 3)) return 0;
        dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
        if (textured) {
            if (!gpu_read_attrib(a1, idx[k], q, 2)) return 0;
            dst[3] = q[0]; dst[4] = q[1]; dst[5] = 0.0f;
        } else {
            if (!gpu_read_attrib(a1, idx[k], q, 3)) return 0;
            dst[3] = q[0]; dst[4] = q[1]; dst[5] = q[2];
        }
    }
    st->n += 3;
    return 1;
}

/* cache the app program's uniform-block offsets for the six lighting uniforms */
static void gpu_resolve_app_uniforms(gpu_entry *e, const glsl_program *gp)
{
    int i;
    if (e->unif_resolved) return;
    for (i = 0; i < GPU_NUNIF; ++i) {
        int idx = glsl_uniform_index(gp, g_gpu_uname[i]);
        if (idx >= 0) { e->aoff[i] = gp->uniforms[idx].offset; e->anc[i] = gp->uniforms[idx].ncomp; }
        else          { e->aoff[i] = -1; e->anc[i] = 0; }
    }
    e->unif_resolved = 1;
}

/* Tessellate [first, first+count) of `mode` into the interleaved staging,
 * replicating portablegl's winding exactly (portablegl.c draw loop) and re-based
 * into buffer space as first + local index.  Appends at st->n, so it extends an
 * already-open coalesced batch.  Returns 0 on an attrib-read / overflow fault. */
static int gpu_tessellate(gpu_stage *st, GLenum mode, GLint first, GLsizei count,
                          const glVertex_Attrib *va, const gpu_entry *e)
{
    GLsizei i;
    if (mode == GL_TRIANGLES) {
        for (i = 0; i < count - 2; i += 3)
            if (!gpu_stage_tri(st, &va[0], &va[1], e->textured,
                               (GLsizei)(first+i), (GLsizei)(first+i+1),
                               (GLsizei)(first+i+2))) return 0;
    } else if (mode == GL_TRIANGLE_STRIP) {
        GLsizei a = 0, b = 1; int toggle = 0;
        for (i = 2; i < count; ++i) {
            if (!gpu_stage_tri(st, &va[0], &va[1], e->textured,
                               (GLsizei)(first+a), (GLsizei)(first+b),
                               (GLsizei)(first+i))) return 0;
            if (!toggle) a = i; else b = i;
            toggle = !toggle;
        }
    } else { /* GL_TRIANGLE_FAN */
        for (i = 2; i < count; ++i)
            if (!gpu_stage_tri(st, &va[0], &va[1], e->textured,
                               (GLsizei)first, (GLsizei)(first+i-1),
                               (GLsizei)(first+i))) return 0;
    }
    return 1;
}

/* Submit the open coalesced batch as ONE mesh through the existing, hardware-
 * validated upload_mesh + draw_mesh_compiled path, then reset the accumulator.
 * No-op when nothing is pending.  A declined dispatch latches only that program
 * to the CPU (mirroring the per-draw path); a staging OOM does the same so a
 * batch is never silently retried-and-dropped every frame. */
static void gpu_coalesce_flush(void)
{
    pgl_g3d_mesh mesh;
    int ok;
    if (!g_coalesce_active || g_coalesce_n < 3) {
        g_coalesce_active = 0; g_coalesce_n = 0;
        return;
    }
    pgl_g3d_set_uniforms(g_coalesce_blob, PGL_G3D_MAX_UNIF);
    mesh = pgl_g3d_upload_mesh(g_gpu_stage, (uint32_t)g_coalesce_n, GPU_STAGING_STRIDE);
    if (mesh == PGL_G3D_MESH_INVALID) {
        if (g_coalesce_entry) g_coalesce_entry->cpu_latched = 1;
    } else {
        ok = pgl_g3d_draw_mesh_compiled(g_coalesce_prog, mesh, 0,
                                        (uint32_t)g_coalesce_n, g_coalesce_depth,
                                        g_coalesce_mask, g_coalesce_cull);
        pgl_g3d_free_mesh(mesh);
        if (!ok && g_coalesce_entry) g_coalesce_entry->cpu_latched = 1;
    }
    g_coalesce_active = 0; g_coalesce_n = 0;
}

static int gpu_try_draw_arrays(GLenum mode, GLint first, GLsizei count)
{
    gpu_entry *e;
    const glVertex_Attrib *va;
    gpu_stage st;
    uint32_t blob[PGL_G3D_MAX_UNIF];
    uint32_t depth, mask, cull;
    GLsizei nout, i;
    int key_match, base;

    gpu_bringup();
    if (!g_gpu_ready) return 0;
    if (!g_active || !g_active->gp || !g_active->ublock) return 0;
    e = gpu_find_entry(g_active->pgl_id);
    if (!e || !e->eligible || e->cpu_latched) return 0;

    /* the fixed kernels rasterise triangles only */
    if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN)
        return 0;
    if (count < 3 || first < 0) return 0;

    va = g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].vertex_attribs;
    /* attrib 0 = position (float3); attrib 1 = normal (gouraud) or uv (textured) */
    if (!va[0].enabled || va[0].type != GL_FLOAT || va[0].size < 3) return 0;
    if (!va[1].enabled || va[1].type != GL_FLOAT) return 0;
    if (va[1].size < (e->textured ? 2 : 3)) return 0;

    nout = (mode == GL_TRIANGLES) ? (count / 3) * 3 : (count - 2) * 3;
    if (nout < 3 || (GLuint)nout > PGL_G3D_MAX_VERTS) return 0;

    /* Pack the per-draw uniform blob FIRST: it doubles as the coalescing key, so
     * it must be known before deciding whether this draw extends the open batch
     * or forces a flush.  Match each kernel lighting uniform to the app program's
     * uniform by name and copy its value as raw float bits (wider of the two
     * widths zero-padded; blob was memset to 0). */
    gpu_resolve_app_uniforms(e, g_active->gp);
    memset(blob, 0, sizeof(blob));
    for (i = 0; i < GPU_NUNIF; ++i) {
        int k, n;
        if (e->koff[i] < 0 || e->aoff[i] < 0) continue;
        n = e->knc[i] < e->anc[i] ? e->knc[i] : e->anc[i];
        for (k = 0; k < n && e->koff[i] + k < (int)PGL_G3D_MAX_UNIF; ++k) {
            float fv = g_active->ublock[e->aoff[i] + k].f;
            memcpy(&blob[e->koff[i] + k], &fv, 4);
        }
    }
    depth = gpu_depth_code();
    mask  = g_ctx.depth_mask ? 1u : 0u;
    cull  = gpu_cull_code();

    /* Textured geom keeps the per-draw dispatch (its interleaved pos+uv staging
     * and raster-slot binding are not coalesced), but drains any pending
     * untextured batch first so painter's order holds. */
    if (e->textured) {
        pgl_g3d_mesh mesh;
        int slot, ok;
        gpu_coalesce_flush();
        if (!gpu_stage_reserve(nout)) return 0;
        st.v = g_gpu_stage; st.n = 0; st.cap = nout;
        if (!gpu_tessellate(&st, mode, first, count, va, e)) return 0;
        if (st.n != nout) return 0;   /* defensive: tessellation/staging mismatch */
        pgl_g3d_set_uniforms(blob, PGL_G3D_MAX_UNIF);
        slot = gpu_tex_slot(gles_engine_texture_for_unit(0), 0);
        if (slot < 0) return 0;   /* no raster texture staged: CPU this draw */
        pgl_g3d_bind_tex_unit(0, slot);
        mesh = pgl_g3d_upload_mesh(g_gpu_stage, (uint32_t)nout, GPU_STAGING_STRIDE);
        if (mesh == PGL_G3D_MESH_INVALID) return 0;   /* transient (OOM): CPU this draw */
        ok = pgl_g3d_draw_mesh_compiled(e->prog, mesh, 0, (uint32_t)nout,
                                        depth, mask, cull);
        pgl_g3d_free_mesh(mesh);
        if (!ok) { e->cpu_latched = 1; return 0; }   /* declined: latch CPU + fall through */
        return 1;
    }

    /* Untextured geom: coalesce consecutive same-key draws into one mesh.  The
     * key is (program, uniform blob, depth func/mask, cull); a change - or a
     * batch that would overflow the staging cap - flushes the open batch first.
     * gears re-packs an identical blob for all 76 strips of a gear and changes
     * it only per gear, so this yields 6 dispatches/frame instead of 532. */
    key_match = g_coalesce_active &&
                g_coalesce_entry == e &&
                g_coalesce_prog == e->prog &&
                g_coalesce_depth == depth && g_coalesce_mask == mask &&
                g_coalesce_cull == cull &&
                memcmp(g_coalesce_blob, blob, sizeof(blob)) == 0 &&
                g_coalesce_n + nout <= (int)PGL_G3D_MAX_VERTS;
    if (!key_match) {
        gpu_coalesce_flush();
        if (!g_gpu_ready) return 0;   /* a flush may have latched the GPU off */
        g_coalesce_entry = e;
        g_coalesce_prog  = e->prog;
        g_coalesce_depth = depth; g_coalesce_mask = mask; g_coalesce_cull = cull;
        memcpy(g_coalesce_blob, blob, sizeof(blob));
        g_coalesce_n = 0;
        g_coalesce_active = 1;
    }
    if (!gpu_stage_reserve(PGL_G3D_MAX_VERTS)) {
        g_coalesce_active = 0; g_coalesce_n = 0; return 0;
    }
    base = g_coalesce_n;
    st.v = g_gpu_stage; st.n = base; st.cap = PGL_G3D_MAX_VERTS;
    if (!gpu_tessellate(&st, mode, first, count, va, e) || st.n != base + nout) {
        /* attrib-read fault mid-primitive: keep the valid prefix, dispatch it,
         * and let the caller draw THIS primitive on the CPU (no geometry lost,
         * no partial triangle submitted). */
        g_coalesce_n = base;
        gpu_coalesce_flush();
        return 0;
    }
    g_coalesce_n = st.n;
    return 1;
}

GEuint gles_engine_gpu_link(GEuint pgl_id, const char *vs_src, const char *fs_src)
{
    gpu_entry *e;
    pgl_g3d_prog prog;
    char err[256];
    uint32_t nc;
    int i;
    if (!pgl_id || !vs_src || !fs_src) return 0;
    err[0] = '\0';
    prog = pgl_g3d_program_compile(vs_src, fs_src, err, (int)sizeof(err));
    if (prog == PGL_G3D_PROG_INVALID)
        return 0;   /* not GPU-viable: the CPU interpreter keeps this program */
    /* Three GPU mesh shapes are staged, all sharing the stride-6 pos+attr1
     * upload and the gouraud/tex raster pass:
     *   - FS declares a sampler   -> textured kernel (attr1 = uv);
     *   - VS declares uNormalMat  -> lit gouraud kernel (attr1 = normal, lit on
     *                                the GPU from uMaterial/uLightEye/uShade);
     *   - the native gouraud shape-> colour kernel (attr1 = a precomputed colour,
     *     (match_gouraud: uMVP only,  transformed but not lit on the GPU).
     *     vColor = aColor)          Recognised via the interp entry's `gouraud`
     *                               flag set by gles_engine_create_interpreted_
     *                               program (which runs before this), so teapot's
     *                               CPU-lit mesh still rasterises on the GPU.
     * Anything else keeps the CPU interpreter. */
    {
        interp_entry *ie = find_entry(pgl_id);
        /* The GPU staging contract (gpu_try_draw_arrays) reads attr0 = position
         * and attr1 = colour/normal/uv.  Only take the colour path when the
         * gouraud program's attributes actually land in those two slots; else
         * keep the location-agnostic CPU fast-path so a draw can never be
         * mis-staged. */
        int gouraud_color = (ie != NULL && ie->gouraud &&
                             ie->g_pos_loc == 0 && ie->g_color_loc == 1);
        if (!strstr(fs_src, "sampler") && !strstr(vs_src, "uNormalMat") &&
            !gouraud_color) {
            pgl_g3d_program_free(prog);
            return 0;
        }
    }
    e = gpu_alloc_entry(pgl_id);
    if (!e) { pgl_g3d_program_free(prog); return 0; }
    e->prog = prog;
    e->textured = (strstr(fs_src, "sampler") != NULL);
    e->eligible = 0;
    for (i = 0; i < GPU_NUNIF; ++i) {
        nc = 0;
        e->koff[i] = pgl_g3d_shader_uniform_dword(prog, g_gpu_uname[i], &nc);
        e->knc[i] = (int)nc;
    }
    /* the fixed geometry xform kernel attached exactly when it exposes uMVP */
    e->eligible = (e->koff[0] >= 0);
    e->unif_resolved = 0;
    if (e->eligible)
        g_gpu_has_eligible = 1;   /* GPU is now worth bringing up (NOCACHE fb) */
    return (GEuint)(e->eligible ? 1 : 0);
}

void gles_engine_gpu_delete(GEuint pgl_id)
{
    gpu_entry *e = gpu_find_entry(pgl_id);
    if (!e) return;
    if (e->prog != PGL_G3D_PROG_INVALID)
        pgl_g3d_program_free(e->prog);
    memset(e, 0, sizeof(*e));   /* in_use = 0 */
}

void gles_engine_gpu_flush(void)
{
    /* Submit any draw the engine coalesced but has not yet dispatched (the GPU
     * path merges consecutive same-state draws into one mesh), then drain the
     * portablegl fixed-function batch, so nothing GPU-side is left pending
     * before a present, readback, clear, program switch or CPU-path draw. */
    if (g_gpu_ready) {
        gpu_coalesce_flush();
        pgl_g3d_flush();
    }
}

int gles_engine_gpu_active(void)
{
    gpu_entry *e;
    if (!g_gpu_ready) return 0;
    if (!g_active || !g_active->gp) return 0;
    e = gpu_find_entry(g_active->pgl_id);
    if (!e || !e->eligible || e->cpu_latched) return 0;
    return 1;
}

#else  /* !PGL_G3D: inert so the GPU-agnostic GLES2/EGL TUs link unchanged */

GEuint gles_engine_gpu_link(GEuint pgl_id, const char *vs_src, const char *fs_src)
{ (void)pgl_id; (void)vs_src; (void)fs_src; return 0; }
void gles_engine_gpu_delete(GEuint pgl_id) { (void)pgl_id; }
void gles_engine_gpu_flush(void) { }
int  gles_engine_gpu_active(void) { return 0; }

#endif /* PGL_G3D */
