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
#include <math.h>
#include <stdio.h>
#include <time.h>
#endif

#include "portablegl/portablegl.h"
#ifdef PGL_G3D
#include "portablegl/pgl_g3d.h"
#endif

#include "../glsl/glsl.h"
#include "gles_gl_values.h"
#include "gles_engine.h"
#include <ewoksys/klog.h>   /* slog: loud GPU-decline diagnostic (/dev/log) */

/* ------------------------------------------------------------------ *
 * Engine state
 * ------------------------------------------------------------------ */
static glContext g_ctx;
static int       g_inited = 0;
static GLsizei   g_w = 0;
static GLsizei   g_h = 0;

#ifdef PGL_G3D
static int g_profile = -1;
static unsigned g_prof_submits;
static unsigned g_prof_rw_small;
static unsigned g_prof_rw_replay;
static unsigned g_prof_rw_decline;
static unsigned g_prof_native_programs;
static unsigned g_prof_native_vertices;
static unsigned g_prof_interp_vertices;
static unsigned long long g_prof_submit_us;
static unsigned long long g_prof_rw_collect_us, g_prof_rw_route_us;
static unsigned g_prof_rw_tex_uploads;
static unsigned long long g_prof_rw_tex_bytes;
static unsigned g_prof_frames;
static unsigned long long g_prof_epoch;
static void rw_gpu_profile_window(void);
static unsigned long long profile_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ull +
           (unsigned long long)ts.tv_nsec / 1000ull;
}
static int profile_enabled(void)
{
    if (g_profile < 0) {
        const char *v = getenv("PGL_PROFILE");
        g_profile = v && atoi(v) != 0;
    }
    return g_profile;
}
static void profile_inc(unsigned *counter)
{
    if (profile_enabled())
        /* These counters are diagnostics only.  They are touched for every
         * vertex, including worker threads; a non-atomic diagnostic increment
         * keeps the profile path from becoming the new CPU bottleneck. */
        ++*counter;
}
static void profile_frame(void)
{
    unsigned long long now;
    if (!profile_enabled()) return;
    now = profile_us();
    if (!g_prof_epoch) {
        const char *rw = getenv("PGL_RW_GPU");
        const char *batch = getenv("PGL_RW_BATCH");
        const char *native = getenv("PGL_RW_NATIVE");
        const char *native_exp = getenv("PGL_RW_NATIVE_EXPERIMENTAL");
        const char *min_tris = getenv("PGL_RW_MIN_TRIS");
        const char *min_pixels = getenv("PGL_RW_MIN_PIXELS");
        const char *threads = getenv("PGL_VERTEX_THREADS");
        const char *vertex_min = getenv("PGL_VERTEX_MIN");
        const char *cache = getenv("PGL_VERTEX_CACHE");
        printf("PGL_PROFILE config rw_gpu=%s rw_batch=%s rw_native=%s rw_native_experimental=%s rw_min_tris=%s rw_min_pixels=%s vertex_threads=%s vertex_min=%s vertex_cache=%s\n",
               rw ? rw : "(unset)", batch ? batch : "(unset)",
               native ? native : "(unset)", native_exp ? native_exp : "(unset)",
               min_tris ? min_tris : "(unset)",
               min_pixels ? min_pixels : "(unset)",
               threads ? threads : "(unset)", vertex_min ? vertex_min : "(unset)",
               cache ? cache : "(unset)");
        fflush(stdout);
        g_prof_epoch = now;
    }
    ++g_prof_frames;
    if (now - g_prof_epoch < 5000000ull) return;
    printf("PGL_PROFILE submits=%u/%.1fms rw_small=%u rw_replay=%u rw_decline=%u native_prog=%u native_vs=%u interp_vs=%u vertex_workers=%d frames=%u size=%dx%d fps=%.2f rw_route=%.1fms rw_collect=%.1fms window=%.1fms\n",
           g_prof_submits, g_prof_submit_us / 1000.0, g_prof_rw_small,
           g_prof_rw_replay, g_prof_rw_decline, g_prof_native_programs, g_prof_native_vertices,
           g_prof_interp_vertices, pglVertexWorkerCount(), g_prof_frames, g_w, g_h,
           (double)g_prof_frames * 1000000.0 / (double)(now-g_prof_epoch),
           g_prof_rw_route_us/1000.0, g_prof_rw_collect_us/1000.0,
           (now-g_prof_epoch)/1000.0);
    printf("PGL_PROFILE rw_texture uploads=%u bytes=%llu\n",
           g_prof_rw_tex_uploads, g_prof_rw_tex_bytes);
    rw_gpu_profile_window();
    fflush(stdout);
    g_prof_epoch = now;
    g_prof_submits = g_prof_rw_small = g_prof_rw_replay = g_prof_rw_decline = 0;
    g_prof_native_programs = g_prof_native_vertices = g_prof_interp_vertices = 0;
    g_prof_frames = 0;
    g_prof_submit_us = 0;
    g_prof_rw_collect_us = g_prof_rw_route_us = 0;
    g_prof_rw_tex_uploads = 0;
    g_prof_rw_tex_bytes = 0;
}
#endif

/* Texture-unit -> bound texture name tracking.  portablegl's texture2D() takes
 * an explicit texture object name, but a GLES2 sampler2D uniform holds a texture
 * UNIT, so the interpreter's sampling callback resolves unit -> name here. */
static GLuint g_tex_unit[32];
static int    g_active_unit = 0;

/* Direct 1:1 fullscreen textured-blit fast-path (defined below).  Returns 1
 * when it fully handled the draw, 0 to fall through to the interpreter. */
static int cpu_try_direct_blit(GLenum mode, GLint first, GLsizei count);
static void interp_registry_fini(void);

#ifdef PGL_G3D
static void rw_gpu_detach(void);
static void rw_gpu_fini(void);
static void rw_gpu_poll_controls(void);
static int rw_gpu_shm(void);
static void rw_gpu_texture_deleted(GEint n, const GEuint *names);
static int rw_gpu_draw(GLenum mode, GLint first, GLsizei count,
                       GLenum type, const void *indices);
static void rw_gpu_legacy_flush(void);
static int rw_gpu_allowed(void);
#else
static void rw_gpu_detach(void) { }
static void rw_gpu_fini(void) { }
static void rw_gpu_poll_controls(void) { }
static int rw_gpu_shm(void) { return 0; }
static int rw_gpu_flush(void) { return 1; }
static void rw_gpu_texture_deleted(GEint n, const GEuint *names)
{ (void)n; (void)names; }
#endif

#ifdef PGL_G3D
/* Transparent V3D GPU bridge hooks (defined at the bottom of this TU, the only
 * GPU-aware code in the library).  Each is inert unless the active program
 * compiled to one of portablegl's fixed geometry kernels; the wrappers below
 * call them and silently keep the CPU interpreter path otherwise. */
static void gpu_bringup(void);
static int  gpu_try_draw_arrays(GLenum mode, GLint first, GLsizei count);
static int  gpu_try_draw_elements(GLenum mode, GLsizei count, GLenum type,
                                  const void *indices);
static void gpu_on_texture(GLuint name, GEenum format, GEenum type,
                           GEint w, GEint h, const void *data);
#endif

int gles_engine_init(GEint w, GEint h)
{
    pix_t *back = NULL;   /* NULL => portablegl allocates the back buffer */
    if (g_inited)
        return 1;
    pglSetTriangleCallback(NULL, NULL);
    if (!init_glContext(&g_ctx, &back, (GLsizei)w, (GLsizei)h))
        return 0;
    set_glContext(&g_ctx);
    g_w = w;
    g_h = h;
    memset(g_tex_unit, 0, sizeof(g_tex_unit));
    g_active_unit = 0;
    g_inited = 1;
    return 1;
}

void gles_engine_fini(void)
{
    if (!g_inited)
        return;
    gles_engine_gpu_flush();
    pglSetTriangleCallback(NULL, NULL);
    pglStopVertexWorkers();
    interp_registry_fini();
    rw_gpu_fini();
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
    gles_engine_gpu_flush();
    rw_gpu_detach();
    if (!pglResizeFramebuffer((GLsizei)w, (GLsizei)h))
        return 0;
    g_w = w;
    g_h = h;
    return 1;
}

void *gles_engine_backbuffer(void)
{
    gles_engine_gpu_flush();   /* land coalesced GPU draws before a blit/readback */
    rw_gpu_poll_controls();
    profile_frame();
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

int gles_engine_backbuffer_shm(void)
{
    int shm = rw_gpu_shm();
    if (shm) return shm;
#ifdef PGL_G3D
    /* Only while portablegl's back buffer IS the pgl_g3d target (pglGpuBegin
     * adopted it); a CPU-only app keeps its malloc back buffer and must stay
     * on the CPU blit. */
    if (pgl_g3d_enabled() && pglGetBackBuffer() == pgl_g3d_pixels())
        return pgl_g3d_pixels_shm();
#endif
    return 0;
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
    { GLES2_GL_ETC1_RGB8_OES,   GL_ETC1_RGB8_OES   },
    { GLES2_GL_COMPRESSED_RGB8_ETC2, GL_COMPRESSED_RGB8_ETC2 },
    { GLES2_GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
      GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 },
    { GLES2_GL_COMPRESSED_RGBA8_ETC2_EAC, GL_COMPRESSED_RGBA8_ETC2_EAC },
};

/* RenderWare's GLES2 compatibility path still uses desktop sized internal
 * formats (GL_RGB8/GL_RGBA8/GL_RGBA4/GL_RGB5_A1/GL_RGB565).  portablegl
 * stores every 2D level in its canonical RGBA8 backing store and, like GLES2,
 * accepts the unsized format as the upload contract.  Passing the desktop
 * token through makes pgl_TexImage2D reject every upload with
 * GL_INVALID_OPERATION (internalformat != format), turning texture work into
 * repeated failed calls and leaving the GPU sampler with empty images.
 * Normalize only these sized tokens; the external format and type still
 * determine the actual unpack conversion.
 */
static unsigned gles_tex_internalformat(unsigned internalformat, unsigned format)
{
    switch (internalformat) {
    case 0x8051u: /* GL_RGB8 */
    case 0x8058u: /* GL_RGBA8 */
    case 0x8056u: /* GL_RGBA4 */
    case 0x8057u: /* GL_RGB5_A1 */
    case 0x8D62u: /* GL_RGB565 */
        return format;
    default:
        return internalformat;
    }
}

static const epair type_map[] = {
    { GLES2_GL_UNSIGNED_BYTE,  GL_UNSIGNED_BYTE  },
    { GLES2_GL_UNSIGNED_SHORT, GL_UNSIGNED_SHORT },
    { GLES2_GL_UNSIGNED_INT,   GL_UNSIGNED_INT   },
    { GLES2_GL_FLOAT,          GL_FLOAT          },
    { GLES2_GL_UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_SHORT_5_6_5 },
    { GLES2_GL_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_SHORT_4_4_4_4 },
    { GLES2_GL_UNSIGNED_SHORT_5_5_5_1, GL_UNSIGNED_SHORT_5_5_5_1 },
};

static const epair pixel_store_map[] = {
    { GLES2_GL_UNPACK_ALIGNMENT, GL_UNPACK_ALIGNMENT },
    { GLES2_GL_PACK_ALIGNMENT,   GL_PACK_ALIGNMENT   },
};

static const epair get_pname_map[] = {
    { GLES2_GL_VIEWPORT,                     GL_VIEWPORT                     },
    { GLES2_GL_SCISSOR_BOX,                  GL_SCISSOR_BOX                  },
    { GLES2_GL_MAX_TEXTURE_SIZE,             GL_MAX_TEXTURE_SIZE             },
    { GLES2_GL_ARRAY_BUFFER_BINDING,         GL_ARRAY_BUFFER_BINDING         },
    { GLES2_GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_ELEMENT_ARRAY_BUFFER_BINDING },
    { GLES2_GL_TEXTURE_BINDING_2D,           GL_TEXTURE_BINDING_2D           },
    /* GL_CURRENT_PROGRAM is answered in gles2_api.c: the GLES2 program handle
     * differs from the pgl_id portablegl would report. */
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

/* Sample coverage state (MSAA not implemented, but queries must succeed). */
static GEfloat g_sample_coverage_value = 1.0f;
static GEbool  g_sample_coverage_invert = 0;

void gles_engine_sample_coverage(GEfloat value, GEbool invert)
{
    g_sample_coverage_value = value;
    g_sample_coverage_invert = invert;
}

void gles_engine_read_pixels(GEint x, GEint y, GEint w, GEint h,
                             GEenum format, GEenum type, void *pixels)
{
    u32 *fb;
    unsigned char *dst;
    int row, col, bpp;
    size_t stride;
    if (!g_inited) return;
    if (w < 0 || h < 0) {
        gles_engine_set_error(GLES2_GL_INVALID_VALUE);
        return;
    }
    if (!pixels) {
        gles_engine_set_error(GLES2_GL_INVALID_OPERATION);
        return;
    }
    if (format != GLES2_GL_RGBA && format != GLES2_GL_RGB) {
        gles_engine_set_error(GLES2_GL_INVALID_ENUM);
        return;
    }
    if (type == GLES2_GL_UNSIGNED_BYTE) {
        bpp = format == GLES2_GL_RGBA ? 4 : 3;
    } else if (type == GLES2_GL_UNSIGNED_SHORT_5_6_5 && format == GLES2_GL_RGB) {
        bpp = 2;
    } else if ((type == GLES2_GL_UNSIGNED_SHORT_4_4_4_4 ||
                type == GLES2_GL_UNSIGNED_SHORT_5_5_5_1) &&
               format == GLES2_GL_RGBA) {
        bpp = 2;
    } else {
        gles_engine_set_error(GLES2_GL_INVALID_ENUM);
        return;
    }
    gles_engine_gpu_flush();
    fb = (u32 *)pglGetBackBuffer();
    if (!fb) return;
    dst = (unsigned char *)pixels;
    stride = ((size_t)w * (size_t)bpp + (size_t)g_ctx.pack_alignment - 1u) /
             (size_t)g_ctx.pack_alignment * (size_t)g_ctx.pack_alignment;
    for (row = 0; row < h; ++row) {
        int64_t sy = (int64_t)g_h - 1 - ((int64_t)y + row);
        for (col = 0; col < w; ++col) {
            int64_t sx = (int64_t)x + col;
            u32 p = (sx >= 0 && sx < g_w && sy >= 0 && sy < g_h)
                        ? fb[(size_t)sy * (size_t)g_w + (size_t)sx] : 0;
            unsigned r = (p >> 16) & 0xff, g = (p >> 8) & 0xff;
            unsigned b = p & 0xff, a = (p >> 24) & 0xff;
            unsigned char *out = dst + (size_t)row * stride + (size_t)col * (size_t)bpp;
            if (type == GLES2_GL_UNSIGNED_BYTE) {
                out[0] = (unsigned char)r;
                out[1] = (unsigned char)g;
                out[2] = (unsigned char)b;
                if (bpp == 4) out[3] = (unsigned char)a;
            } else {
                unsigned short v;
                if (type == GLES2_GL_UNSIGNED_SHORT_5_6_5)
                    v = (unsigned short)(((r * 31u + 127u) / 255u) << 11 |
                                         ((g * 63u + 127u) / 255u) << 5 |
                                         ((b * 31u + 127u) / 255u));
                else if (type == GLES2_GL_UNSIGNED_SHORT_4_4_4_4)
                    v = (unsigned short)(((r * 15u + 127u) / 255u) << 12 |
                                         ((g * 15u + 127u) / 255u) << 8 |
                                         ((b * 15u + 127u) / 255u) << 4 |
                                         ((a * 15u + 127u) / 255u));
                else
                    v = (unsigned short)(((r * 31u + 127u) / 255u) << 11 |
                                         ((g * 31u + 127u) / 255u) << 6 |
                                         ((b * 31u + 127u) / 255u) << 1 |
                                         (a >= 128u));
                out[0] = (unsigned char)(v & 0xff);
                out[1] = (unsigned char)(v >> 8);
            }
        }
        if (stride > (size_t)w * (size_t)bpp)
            memset(dst + (size_t)row * stride + (size_t)w * (size_t)bpp, 0,
                   stride - (size_t)w * (size_t)bpp);
    }
}

void gles_engine_set_error(GEenum error)
{
    GLenum pgl_error;
    if (!g_inited || g_ctx.error != GL_NO_ERROR) return;
    switch (error) {
    case GLES2_GL_INVALID_ENUM:                  pgl_error = GL_INVALID_ENUM; break;
    case GLES2_GL_INVALID_VALUE:                 pgl_error = GL_INVALID_VALUE; break;
    case GLES2_GL_INVALID_OPERATION:             pgl_error = GL_INVALID_OPERATION; break;
    case GLES2_GL_INVALID_FRAMEBUFFER_OPERATION: pgl_error = GL_INVALID_FRAMEBUFFER_OPERATION; break;
    case GLES2_GL_OUT_OF_MEMORY:                 pgl_error = GL_OUT_OF_MEMORY; break;
    default: return;
    }
    g_ctx.error = pgl_error;
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
    if (!data) return;
    if (pname == GLES2_GL_IMPLEMENTATION_COLOR_READ_FORMAT) {
        *data = GLES2_GL_RGBA;
        return;
    }
    if (pname == GLES2_GL_IMPLEMENTATION_COLOR_READ_TYPE) {
        *data = GLES2_GL_UNSIGNED_BYTE;
        return;
    }
    if (pname == GLES2_GL_PACK_ALIGNMENT || pname == GLES2_GL_UNPACK_ALIGNMENT) {
        *data = pname == GLES2_GL_PACK_ALIGNMENT ? g_ctx.pack_alignment : g_ctx.unpack_alignment;
        return;
    }
    if (pname == GLES2_GL_MAX_VERTEX_ATTRIBS) {
        if (data) *data = GL_MAX_VERTEX_ATTRIBS;
        return;
    }
    /* The active texture unit is engine-tracked (g_active_unit) and is not in
     * get_pname_map; overlays (e.g. the ImGui GLES2 backend) save/restore it
     * around their draws, so answer it here rather than through portablegl. */
    if (pname == GLES2_GL_ACTIVE_TEXTURE) {
        if (data) *data = (GEint)(GLES2_GL_TEXTURE0 + g_active_unit);
        return;
    }
    pgl_GetIntegerv(EMAP(get_pname_map, pname), data);
}

void gles_engine_get_floatv(GEenum pname, GEfloat *data)
{
    if (!data) return;
    if (pname == GLES2_GL_SAMPLE_COVERAGE_VALUE) {
        *data = g_sample_coverage_value;
        return;
    }
    pgl_GetFloatv(EMAP(get_pname_map, pname), data);
}

void gles_engine_get_booleanv(GEenum pname, GEbool *data)
{
    if (!data) return;
    if (pname == GLES2_GL_SAMPLE_COVERAGE_INVERT) {
        *data = g_sample_coverage_invert;
        return;
    }
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

void gles_engine_get_buffer_parameteriv(GEenum target, GEenum pname, GEint *params)
{
    GLenum ptarget = EMAP(buffer_target_map, target);
    GLuint buffer;
    if (!params) return;
    if (ptarget != GL_ARRAY_BUFFER && ptarget != GL_ELEMENT_ARRAY_BUFFER) {
        gles_engine_set_error(GLES2_GL_INVALID_ENUM);
        return;
    }
    if (pname != GLES2_GL_BUFFER_SIZE && pname != GLES2_GL_BUFFER_USAGE) {
        gles_engine_set_error(GLES2_GL_INVALID_ENUM);
        return;
    }
    buffer = g_ctx.bound_buffers[ptarget - GL_ARRAY_BUFFER];
    if (!buffer) {
        gles_engine_set_error(GLES2_GL_INVALID_OPERATION);
        return;
    }
    if (pname == GLES2_GL_BUFFER_SIZE)
        *params = g_ctx.buffers.a[buffer].size;
    else {
        unsigned i;
        *params = 0;
        for (i = 0; i < sizeof(buffer_usage_map) / sizeof(buffer_usage_map[0]); ++i) {
            if (buffer_usage_map[i].p == g_ctx.buffers.a[buffer].usage) {
                *params = (GEint)buffer_usage_map[i].s;
                break;
            }
        }
    }
}

GEbool gles_engine_is_buffer(GEuint buffer)
{
    if (!g_inited || !buffer || buffer >= g_ctx.buffers.size) return GL_FALSE;
    return !g_ctx.buffers.a[buffer].deleted &&
           g_ctx.buffers.a[buffer].type < GL_NUM_BUFFER_TYPES - GL_ARRAY_BUFFER;
}

/* ------------------------------------------------------------------ *
 * Textures
 * ------------------------------------------------------------------ */
void gles_engine_gen_textures(GEint n, GEuint *textures)    { pgl_GenTextures(n, textures); }
void gles_engine_delete_textures(GEint n, const GEuint *t)
{
    GEint i;
    int unit;
    if (n < 0 || !t) { pgl_DeleteTextures(n, t); return; }
    rw_gpu_texture_deleted(n, t);
    for (i = 0; i < n; ++i)
        for (unit = 0; unit < 32; ++unit)
            if (g_tex_unit[unit] == t[i]) g_tex_unit[unit] = 0;
    pgl_DeleteTextures(n, t);
}

void gles_engine_bind_texture(GEenum target, GEuint texture)
{
    pgl_BindTexture(EMAP(tex_target_map, target), texture);
    /* Track the binding per active unit for the interpreter's sampler.  A cube
     * map shares the slot: bridge_tex_sample resolves it by texture name and
     * samples it as a cube when the GLSL builtin is textureCube (dim == 3), so
     * cube binds must be mirrored here too or textureCube reads a stale name. */
    if ((target == GLES2_GL_TEXTURE_2D || target == GLES2_GL_TEXTURE_CUBE_MAP) &&
        g_active_unit >= 0 && g_active_unit < 32)
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
    unsigned mapped_format = EMAP(format_map, format);
    pgl_TexImage2D(EMAP(tex_target_map, target), level,
                   (GLint)gles_tex_internalformat(
                       EMAP(format_map, (unsigned)internalformat), mapped_format),
                   w, h, border,
                   mapped_format, EMAP(type_map, type), data);
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

void gles_engine_compressed_tex_image_2d(GEenum target, GEint level, GEenum internalformat,
                                         GEint w, GEint h, GEint border,
                                         GEint image_size, const void *data)
{
    pgl_CompressedTexImage2D(EMAP(tex_target_map, target), level,
                             EMAP(format_map, internalformat), w, h, border,
                             image_size, data);
}

void gles_engine_compressed_tex_sub_image_2d(GEenum target, GEint level,
                                             GEint xoff, GEint yoff, GEint w, GEint h,
                                             GEenum format, GEint image_size,
                                             const void *data)
{
    pgl_CompressedTexSubImage2D(EMAP(tex_target_map, target), level, xoff, yoff,
                                w, h, EMAP(format_map, format), image_size, data);
}

void gles_engine_copy_tex_image_2d(GEenum target, GEint level, GEenum internalformat,
                                   GEint x, GEint y, GEint w, GEint h, GEint border)
{
    gles_engine_gpu_flush();
    pgl_CopyTexImage2D(EMAP(tex_target_map, target), level,
                       EMAP(format_map, internalformat), x, y, w, h, border);
}

void gles_engine_copy_tex_sub_image_2d(GEenum target, GEint level,
                                       GEint xoff, GEint yoff, GEint x, GEint y,
                                       GEint w, GEint h)
{
    gles_engine_gpu_flush();
    pgl_CopyTexSubImage2D(EMAP(tex_target_map, target), level, xoff, yoff,
                          x, y, w, h);
}

void gles_engine_generate_mipmap(GEenum target)
{
    pgl_GenerateMipmap(EMAP(tex_target_map, target));
}

static glTexture *engine_bound_texture(GEenum target, int *target_index)
{
    GLenum ptarget = EMAP(tex_target_map, target);
    int idx;
    GLuint name;
    if (ptarget != GL_TEXTURE_2D && ptarget != GL_TEXTURE_CUBE_MAP) return NULL;
    idx = ptarget - GL_TEXTURE_UNBOUND - 1;
    if (target_index) *target_index = idx;
    name = g_ctx.bound_textures[idx];
    return name ? &g_ctx.textures.a[name] : &g_ctx.default_textures[idx];
}

void gles_engine_get_tex_parameteriv(GEenum target, GEenum pname, GEint *params)
{
    glTexture *tex = engine_bound_texture(target, NULL);
    const epair *map = NULL;
    unsigned count = 0, i, value = 0;
    if (!params) return;
    if (!tex) { gles_engine_set_error(GLES2_GL_INVALID_ENUM); return; }
    switch (pname) {
    case GLES2_GL_TEXTURE_MIN_FILTER: value = tex->min_filter; map = texfilter_map; count = sizeof(texfilter_map) / sizeof(texfilter_map[0]); break;
    case GLES2_GL_TEXTURE_MAG_FILTER: value = tex->mag_filter; map = texfilter_map; count = sizeof(texfilter_map) / sizeof(texfilter_map[0]); break;
    case GLES2_GL_TEXTURE_WRAP_S: value = tex->wrap_s; map = texwrap_map; count = sizeof(texwrap_map) / sizeof(texwrap_map[0]); break;
    case GLES2_GL_TEXTURE_WRAP_T: value = tex->wrap_t; map = texwrap_map; count = sizeof(texwrap_map) / sizeof(texwrap_map[0]); break;
    default: gles_engine_set_error(GLES2_GL_INVALID_ENUM); return;
    }
    *params = (GEint)value;
    for (i = 0; i < count; ++i) {
        if (map[i].p == value) { *params = (GEint)map[i].s; break; }
    }
}

void gles_engine_get_tex_parameterfv(GEenum target, GEenum pname, GEfloat *params)
{
    GEint value;
    if (!params) return;
    gles_engine_get_tex_parameteriv(target, pname, &value);
    *params = (GEfloat)value;
}

GEbool gles_engine_is_texture(GEuint texture)
{
    if (!g_inited || !texture || texture >= g_ctx.textures.size) return GL_FALSE;
    return !g_ctx.textures.a[texture].deleted &&
           g_ctx.textures.a[texture].type != GL_TEXTURE_UNBOUND;
}

GEbool gles_engine_texture_complete(GEint unit, GEint cube)
{
    GLuint name;
    glTexture *tex;
    if (unit < 0 || unit >= 32) return GL_FALSE;
    name = g_tex_unit[unit];
    if (name) {
        if (name >= g_ctx.textures.size || g_ctx.textures.a[name].deleted) return GL_FALSE;
        tex = &g_ctx.textures.a[name];
    } else {
        tex = &g_ctx.default_textures[cube ? GL_TEXTURE_CUBE_MAP - GL_TEXTURE_UNBOUND - 1
                                          : GL_TEXTURE_2D - GL_TEXTURE_UNBOUND - 1];
    }
    return (GEbool)pgl_texture_complete(tex, cube != 0);
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

void gles_engine_vertex_attrib4f(GEuint index, const GEfloat value[4])
{
    glVertex_Attrib *a;
    if (index >= GL_MAX_VERTEX_ATTRIBS || !value) {
        gles_engine_set_error(GLES2_GL_INVALID_VALUE);
        return;
    }
    a = &g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].vertex_attribs[index];
    a->current_value = make_v4(value[0], value[1], value[2], value[3]);
}

static glVertex_Attrib *engine_vertex_attrib(GEuint index)
{
    if (!g_inited || index >= GL_MAX_VERTEX_ATTRIBS) {
        gles_engine_set_error(GLES2_GL_INVALID_VALUE);
        return NULL;
    }
    return &g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].vertex_attribs[index];
}

void gles_engine_get_vertex_attribiv(GEuint index, GEenum pname, GEint *params)
{
    glVertex_Attrib *a = engine_vertex_attrib(index);
    unsigned i;
    if (!a || !params) return;
    switch (pname) {
    case GLES2_GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *params = (GEint)a->buf; break;
    case GLES2_GL_VERTEX_ATTRIB_ARRAY_ENABLED: *params = a->enabled; break;
    case GLES2_GL_VERTEX_ATTRIB_ARRAY_SIZE: *params = a->size; break;
    case GLES2_GL_VERTEX_ATTRIB_ARRAY_STRIDE: *params = a->specified_stride; break;
    case GLES2_GL_VERTEX_ATTRIB_ARRAY_TYPE:
        *params = (GEint)a->type;
        for (i = 0; i < sizeof(type_map) / sizeof(type_map[0]); ++i)
            if (type_map[i].p == a->type) { *params = (GEint)type_map[i].s; break; }
        break;
    case GLES2_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: *params = a->normalized; break;
    case GLES2_GL_CURRENT_VERTEX_ATTRIB:
        params[0] = (GEint)a->current_value.x;
        params[1] = (GEint)a->current_value.y;
        params[2] = (GEint)a->current_value.z;
        params[3] = (GEint)a->current_value.w;
        break;
    default: gles_engine_set_error(GLES2_GL_INVALID_ENUM); break;
    }
}

void gles_engine_get_vertex_attribfv(GEuint index, GEenum pname, GEfloat *params)
{
    glVertex_Attrib *a = engine_vertex_attrib(index);
    GEint values[4];
    int i;
    if (!a || !params) return;
    if (pname == GLES2_GL_CURRENT_VERTEX_ATTRIB) {
        params[0] = a->current_value.x;
        params[1] = a->current_value.y;
        params[2] = a->current_value.z;
        params[3] = a->current_value.w;
        return;
    }
    gles_engine_get_vertex_attribiv(index, pname, values);
    params[0] = (GEfloat)values[0];
    for (i = 1; i < 4; ++i) params[i] = 0.0f;
}

void gles_engine_get_vertex_attrib_pointerv(GEuint index, GEenum pname, void **pointer)
{
    glVertex_Attrib *a = engine_vertex_attrib(index);
    if (!a || !pointer) return;
    if (pname != GLES2_GL_VERTEX_ATTRIB_ARRAY_POINTER) {
        gles_engine_set_error(GLES2_GL_INVALID_ENUM);
        return;
    }
    *pointer = (void *)(uintptr_t)a->offset;
}

void gles_engine_draw_arrays(GEenum mode, GEint first, GEint count)
{
#ifdef PGL_G3D
    if (rw_gpu_draw(EMAP(prim_map, mode), first, count, 0, NULL))
        return;
    /* Route an eligible draw onto the V3D geometry kernel; a 0 return means the
     * program is CPU-only (never compiled, declined, or a non-triangle mode) and
     * the interpreter draws it unchanged.  Inert off raspi5. */
    if (gpu_try_draw_arrays(EMAP(prim_map, mode), first, count))
        return;
    /* Not GPU-eligible (or declined): drain any pending coalesced geometry batch
     * first so the CPU draw below lands after it (painter's order). */
    gles_engine_gpu_flush();
#endif
    /* Fullscreen 1:1 textured-quad blit (Fireworks/matrix): repack the bound
     * texture straight into the back buffer, bypassing portablegl's rasteriser
     * and per-pixel texture2D (~155x faster, byte-identical).  Declines - and
     * falls through to the interpreter - for anything that is not provably that
     * canonical blit.  Runs on virt (CPU) and raspi5 alike (these apps are never
     * GPU-eligible, so the CPU draws them there too). */
    if (cpu_try_direct_blit(EMAP(prim_map, mode), first, count))
        return;
    pgl_DrawArrays(EMAP(prim_map, mode), first, count);
}

void gles_engine_draw_elements(GEenum mode, GEint count, GEenum type, const void *indices)
{
#ifdef PGL_G3D
    if (rw_gpu_draw(EMAP(prim_map, mode), 0, count, EMAP(type_map, type), indices))
        return;
    /* Colour-modulated textured indexed draw (imgui, cs_is_geom=4): tessellate
     * to a triangle list, stage pos+uv+colour at stride 10 and dispatch on the
     * QPU.  Returns 0 when not eligible or declined, falling through to the CPU
     * interpreter with identical pixels.  Any other indexed draw stays on the
     * CPU: drain a pending coalesced geometry batch first so this draw lands
     * after it (painter's order). */
    if (gpu_try_draw_elements(EMAP(prim_map, mode), count,
                              EMAP(type_map, type), indices))
        return;
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
#ifdef PGL_G3D
    glsl_program  *rw_executor[7]; /* generic VS executor clones */
    int            rw_simple, rw_alpha_test, rw_uv, rw_color, rw_fog;
    int            rw_sampler, rw_alpha, rw_fogcolor;
    int            rw_uv1, rw_envcolor, rw_sampler1, rw_fx;
    /* RenderWare vertex shaders have a small fixed family.  When the linked
     * source hash matches one of those shaders, rw_simple.inc supplies a
     * native C executor for the VS.  The generic GLSL executor remains the
     * fallback for every unrecognised variant. */
    int            rw_native_vs, rw_native_lit, rw_native_full_lights;
    int            rw_runtime_lights;
    int            rw_pos_loc, rw_normal_loc, rw_color_loc;
    int            rw_weights_loc, rw_indices_loc, rw_tex0_loc, rw_tex1_loc;
    int            rw_v_color, rw_v_tex0, rw_v_fog, rw_v_env, rw_v_tex1;
    int            rw_u_xform, rw_u_world, rw_u_view, rw_u_proj;
    int            rw_u_bones, rw_u_amb, rw_u_light_params, rw_u_light_pos;
    int            rw_u_light_dir, rw_u_light_color, rw_u_mat, rw_u_surf;
    int            rw_u_fog_data, rw_u_tex_matrix, rw_u_color_clamp;
    int            rw_u_env_color;
    float          rw_view_proj[16];
    float          rw_cached_view[16];
    float          rw_cached_proj[16];
    int            rw_view_proj_cache_valid;
    int            rw_view_proj_valid;
    /* The unlit IM3D shader only needs P*V*World.  Build this product once
     * per draw so each vertex does one matrix-vector multiply. */
    float          rw_clip_matrix[16];
    int            rw_clip_matrix_valid;
#endif
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
    int            g_color_slot;  /* varying slot modulating the sample, -1 = none */
    /* Native lit-gouraud fast-path (see match_lit_gouraud): when `lit_gouraud`
     * is set the VS runs on ff_lit_gouraud_vert instead of the interpreter,
     * evaluating the diffuse-only ADS formula shared verbatim by sphere.cpp and
     * apps/gears (uNormalMat/uLightEye/uShade/uMaterial) in native C.  The FS is
     * the same `gl_FragColor = vColor` passthrough, so it reuses ff_gouraud_frag.
     * This is the vertex-bound mesh path on virt (no V3D GPU); on raspi5 the
     * same shader is GPU-eligible (uNormalMat) and never reaches here. */
    int            lit_gouraud;
    int            lg_pos_loc;    /* vec3 aPosition attribute location */
    int            lg_nrm_loc;    /* vec3 aNormal attribute location   */
    int            lg_mvp_off;    /* ublock float offset of mat4 uMVP       */
    int            lg_nmat_off;   /* ublock float offset of mat4 uNormalMat */
    int            lg_mat_off;    /* ublock float offset of vec3 uMaterial  */
    int            lg_light_off;  /* ublock float offset of vec4 uLightEye  */
    int            lg_shade_off;  /* ublock float offset of vec4 uShade     */
    /* Native shadertoy fragment fast-path (see match_shadertoy): when
     * st_effect >= 0 the FS runs on ff_shadertoy_frag, a hand-ported native C
     * implementation of one of the 11 shadertoy effects (shadertoy_ff.inc),
     * recognised at link time by an FNV-1a hash of the FS source.  The VS is
     * the trivial passthrough and stays interpreted (a fullscreen quad issues
     * only a handful of vertices).  Uniforms are read straight out of ublock:
     * iGlobalTime (float), iResolution (vec3) and iChannelN sampler cells
     * (whose .i holds the texture unit, exactly as the interpreter reads them).
     * This is the CPU (virt / no-V3D) path; it removes the per-pixel tree-walk
     * that made the heavy raymarchers run at <=1 FPS.  Bit-identical to the
     * interpreter (golden_native harness, all 11 effects). */
    int            st_effect;     /* 0..10 effect index, or -1 if not shadertoy */
    int            st_time_off;   /* ublock offset of iGlobalTime             */
    int            st_res_off;    /* ublock offset of iResolution (vec3)      */
    int            st_ch_off[3];  /* ublock offsets of iChannel0..2, -1 absent */
} interp_entry;

static interp_entry  g_reg[GLES_MAX_INTERP];
static interp_entry *g_active = NULL;
#ifdef PGL_G3D
#include "rw_simple.inc"
#include "rw_gpu.inc"
#endif

static interp_entry *find_entry(GLuint id)
{
    int i;
    for (i = 0; i < GLES_MAX_INTERP; ++i)
        if (g_reg[i].in_use && g_reg[i].pgl_id == id) return &g_reg[i];
    return NULL;
}

/* texture sampling callback: resolve unit -> portablegl texture name, sample. */
static void bridge_tex_sample(int unit, int dim, const float *coord,
                              const glsl_tex_derivatives *deriv,
                              float out_rgba[4], void *user)
{
    GLuint tex = gles_engine_texture_for_unit(unit);
    vec4 c;
    (void)user;
    if (dim == 3) {
        if (deriv && deriv->has_lod)
            c = texture_cubemap_lod(tex, coord[0], coord[1], coord[2], deriv->lod);
        else if (deriv)
            c = texture_cubemap_grad(tex, coord[0], coord[1], coord[2], deriv->ddx, deriv->ddy);
        else
            c = texture_cubemap(tex, coord[0], coord[1], coord[2]);
    } else {
        if (deriv && deriv->has_lod)
            c = texture2D_lod(tex, coord[0], coord[1], deriv->lod);
        else if (deriv)
            c = texture2D_grad(tex, coord[0], coord[1], deriv->ddx, deriv->ddy);
        else
            c = texture2D(tex, coord[0], coord[1]);
    }
    out_rgba[0] = c.x; out_rgba[1] = c.y; out_rgba[2] = c.z; out_rgba[3] = c.w;
}

static void interp_vert(float *vs_output, vec4 *vertex_attribs,
                        Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    const float *attribs[GLSL_MAX_ATTRIBS];
    int i, n;
    (void)uniforms;
    if (!e || !e->gp) return;
#ifdef PGL_G3D
    if (e->rw_native_vs) {
        profile_inc(&g_prof_native_vertices);
        rw_native_vertex(0, vs_output, vertex_attribs, builtins, e);
        return;
    }
#endif
#ifdef PGL_G3D
    profile_inc(&g_prof_interp_vertices);
#endif
    n = e->gp->nattr_components;
    if (n > GLSL_MAX_ATTRIBS) n = GLSL_MAX_ATTRIBS;
    for (i = 0; i < n; ++i) attribs[i] = &vertex_attribs[i].x;
    glsl_run_vertex(e->gp, attribs, n, e->ublock, bridge_tex_sample, e, vs_output,
                    (float *)&builtins->gl_Position, builtins->gl_InstanceID);
}

#ifdef PGL_G3D
static void interp_parallel_vert(int worker, float *out, vec4 *attrs,
                                 Shader_Builtins *b, void *user)
{
    interp_entry *e = (interp_entry *)user;
    glsl_program *gp = e ? e->gp : NULL;
    const float *attribs[GLSL_MAX_ATTRIBS];
    int i, n;
    if (!e || !gp) return;
#ifdef PGL_G3D
    if (e->rw_native_vs) {
        profile_inc(&g_prof_native_vertices);
        rw_native_vertex(worker, out, attrs, b, e);
        return;
    }
#endif
#ifdef PGL_G3D
    profile_inc(&g_prof_interp_vertices);
#endif
    if (worker > 0) gp = e->rw_executor[worker - 1];
    if (!gp) return;
    n = gp->nattr_components;
    if (n > GLSL_MAX_ATTRIBS) n = GLSL_MAX_ATTRIBS;
    for (i = 0; i < n; ++i) attribs[i] = &attrs[i].x;
    glsl_run_vertex(gp, attribs, n, e->ublock, bridge_tex_sample, e, out,
                    (float *)&b->gl_Position, b->gl_InstanceID);
}

static void interp_free_executors(interp_entry *e)
{
    int i;
    if (!e) return;
    for (i = 0; i < 7; ++i) {
        glsl_free_executor(e->rw_executor[i]);
        e->rw_executor[i] = NULL;
    }
}
#else
static void interp_free_executors(interp_entry *e) { (void)e; }
#endif

static void interp_registry_fini(void)
{
    int i;
    for (i = 0; i < GLES_MAX_INTERP; ++i)
        interp_free_executors(&g_reg[i]);
}

static void interp_frag(float *fs_input, Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    int disc = 0, wrote_depth = 0;
    float fdepth = 0;
    (void)uniforms;
    if (!e || !e->gp) return;
    glsl_run_fragment(e->gp, fs_input, builtins->fs_input_dx, builtins->fs_input_dy,
                      e->ublock, (const float *)&builtins->gl_FragCoord,
                      (const float *)&builtins->gl_PointCoord,
                      (const float *)&builtins->gl_PointCoord_dx,
                      (const float *)&builtins->gl_PointCoord_dy,
                      builtins->gl_FrontFacing, bridge_tex_sample, e,
                      (float *)&builtins->gl_FragColor, &disc, &fdepth, &wrote_depth);
    if (disc) builtins->discard = GL_TRUE;
    if (wrote_depth) builtins->gl_FragDepth = fdepth;
}

/* ------------------------------------------------------------------ *
 * Native shadertoy fragment fast-path
 *
 * shadertoy_ff.inc holds hand-ported native C implementations of the 11
 * shadertoy effects, proven bit-identical to the GLSL interpreter by the
 * golden_native harness.  It is #included here (the real render path) and by
 * the host harness, so the exact same code produces the pixels in both.  It
 * needs <math.h>; portablegl.h already pulls it in, but include it explicitly
 * so the .inc never depends on include order.
 * ------------------------------------------------------------------ */
#include <math.h>
#include "shadertoy_ff.inc"

/* st_texfn adapter: resolve unit -> portablegl texture name and sample 2D,
 * identical resolution to bridge_tex_sample (all shadertoy samples are 2D). */
static void st_bridge_tex(int unit, float u, float v, float *rgba, void *user)
{
    vec4 c = texture2D(gles_engine_texture_for_unit(unit), u, v);
    (void)user;
    rgba[0] = c.x; rgba[1] = c.y; rgba[2] = c.z; rgba[3] = c.w;
}

static void ff_shadertoy_frag(float *fs_input, Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    st_ctx c;
    float out[4];
    (void)fs_input; (void)uniforms;
    if (!e || e->st_effect < 0) return;
    c.fcx = builtins->gl_FragCoord.x;
    c.fcy = builtins->gl_FragCoord.y;
    c.gtime = e->ublock[e->st_time_off].f;
    c.resx = e->ublock[e->st_res_off + 0].f;
    c.resy = e->ublock[e->st_res_off + 1].f;
    c.resz = e->ublock[e->st_res_off + 2].f;
    /* sampler cells hold the texture unit in .i (T_SAMPLER2D is int-family),
     * exactly the value the interpreter's ci() would read. */
    c.ch0 = (e->st_ch_off[0] >= 0) ? e->ublock[e->st_ch_off[0]].i : 0;
    c.ch1 = (e->st_ch_off[1] >= 0) ? e->ublock[e->st_ch_off[1]].i : 0;
    c.ch2 = (e->st_ch_off[2] >= 0) ? e->ublock[e->st_ch_off[2]].i : 0;
    c.tex = st_bridge_tex; c.user = NULL;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    shadertoy_ff(e->st_effect, &c, out);
    builtins->gl_FragColor.x = out[0];
    builtins->gl_FragColor.y = out[1];
    builtins->gl_FragColor.z = out[2];
    builtins->gl_FragColor.w = out[3];
}

void gles_engine_use_program(GEuint program)
{
    gles_engine_gpu_flush();   /* drain the outgoing program's coalesced draws */
    pglSetVertexExecutor(NULL, NULL);
    pgl_UseProgram(program);
    g_active = find_entry(program);
#ifdef PGL_G3D
    /* Any program whose VS callback is interp_vert can use the persistent
     * worker pool.  The old rw_simple-only guard left custom RenderWare
     * pipelines on one CPU core even though interp_parallel_vert already
     * handles their generic GLSL executor.  Keep native fixed-function VS/FS
     * callbacks on their original path because replacing those callbacks with
     * the generic interpreter would change pixels. */
    if (g_active && !g_active->gouraud && !g_active->lit_gouraud &&
        !g_active->texblit && g_active->st_effect < 0) {
        int i, complete = 1, requested = 8;
        const char *threads = getenv("PGL_VERTEX_THREADS");
        if (threads) requested = atoi(threads);
        if (requested < 1) requested = 1;
        if (requested > 8) requested = 8;
        /* The caller executes worker 0; only clone the executors that can be
         * reached by the configured worker pool. */
        for (i = 0; i < requested - 1; ++i) {
            if (!g_active->rw_executor[i])
                g_active->rw_executor[i] = glsl_clone_executor(g_active->gp);
            if (!g_active->rw_executor[i]) complete = 0;
        }
        if (complete) pglSetVertexExecutor(interp_parallel_vert, g_active);
    }
#endif
}

void gles_engine_delete_program(GEuint program)
{
    interp_entry *e = find_entry(program);
    gles_engine_gpu_flush();
    if (e) {
        if (g_active == e) pglSetVertexExecutor(NULL, NULL);
        interp_free_executors(e);
        e->in_use = 0;
        if (g_active == e) g_active = NULL;
    }
    pgl_DeleteProgram(program);
}

GEbool gles_engine_program_executable(GEuint program)
{
    if (!g_inited || !program || program >= g_ctx.programs.size ||
        g_ctx.programs.a[program].deleted || !find_entry(program))
        return GL_FALSE;
    return g_ctx.programs.a[program].vertex_shader &&
           g_ctx.programs.a[program].fragment_shader ? GL_TRUE : GL_FALSE;
}

void gles_engine_release_interpreted_program(GEuint program)
{
    interp_entry *e = find_entry(program);
    gles_engine_gpu_flush();
    if (e) {
        if (g_active == e) pglSetVertexExecutor(NULL, NULL);
        interp_free_executors(e);
        e->in_use = 0;
        if (g_active == e) g_active = NULL;
    }
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
    {
        static const float zero[2] = {0.0f, 0.0f};
        const float *dx = builtins->fs_input_dx
                        ? builtins->fs_input_dx + e->g_uv_slot * 4 : zero;
        const float *dy = builtins->fs_input_dy
                        ? builtins->fs_input_dy + e->g_uv_slot * 4 : zero;
        c = texture2D_grad(gles_engine_texture_for_unit(e->ublock[e->g_sampler_off].i),
                           fs_input[e->g_uv_slot * 4 + 0], fs_input[e->g_uv_slot * 4 + 1],
                           dx, dy);
    }
    /* Colour-modulated blit (imgui): gl_FragColor = vColor * texture2D(...).
     * Multiply by the interpolated modulating varying exactly as the
     * interpreter's EX_MUL would, so pixels stay bit-identical. */
    if (e->g_color_slot >= 0) {
        const float *m = &fs_input[e->g_color_slot * 4];
        c.x *= m[0]; c.y *= m[1]; c.z *= m[2]; c.w *= m[3];
    }
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
 * or the colour-modulated form used by Dear ImGui
 *      gl_FragColor = <vec4 varying> * texture2D(<sampler2D uniform>, <vec2+ varying>);
 * (single statement, no discard/fragdepth, no other arithmetic or swizzle on the
 * sample).  This is the fullscreen-blit / UI-atlas shape the CPU-composited
 * demos and imgui draw every frame; sampling it natively instead of through the
 * interpreter removes the per-pixel tree-walk.  The vertex shader is deliberately
 * NOT constrained: ff_texblit_frag reads the same interpolated varyings the
 * interpreter would, so any VS feeding them stays pixel-identical.  Returns 1 and
 * reports the sampler's ublock offset + the texcoord varying slot + (or -1 for)
 * the modulating colour varying slot. */
static int match_texblit(const glsl_program *gp, int *sampler_off, int *uv_slot,
                         int *color_slot)
{
    const glsl_stage *fs;
    const glsl_func *ffn;
    const glsl_stmt *s;
    const glsl_expr *e, *rhs, *call, *mod, *a0, *a1;
    int uslot, vslot, mslot;

    *color_slot = -1;
    if (!gp || !gp->linked || !gp->fs) return 0;
    fs = gp->fs;
    if (fs->uses_discard || fs->uses_fragdepth) return 0;
    if (fs->main_idx < 0) return 0;
    ffn = &fs->funcs[fs->main_idx];
    if (!ffn->body) return 0;

    /* FS: a single `gl_FragColor = [mod *] texture2D(...);` */
    s = ffn->body->children;
    if (!s || s->next || s->kind != ST_EXPR || !s->expr) return 0;
    e = s->expr;
    if (e->kind != EX_ASSIGN || !e->a || !e->b) return 0;
    if (e->a->kind != EX_VAR || e->a->var_scope != VARSCOPE_BUILTIN ||
        e->a->var_slot != BI_GL_FRAGCOLOR) return 0;

    rhs = e->b;
    mod = NULL;
    if (rhs->kind == EX_MUL && rhs->a && rhs->b) {
        /* colour-modulated: one operand the sample, the other a vec4 varying */
        const glsl_expr *cand = NULL;
        if (rhs->a->kind == EX_CALL) { call = rhs->a; cand = rhs->b; }
        else if (rhs->b->kind == EX_CALL) { call = rhs->b; cand = rhs->a; }
        else return 0;
        if (cand->kind != EX_VAR || cand->var_scope != VARSCOPE_VARYING)
            return 0;
        mslot = cand->var_slot;
        if (mslot < 0 || mslot >= gp->nvaryings) return 0;
        if (gp->varyings[mslot].ncomp < 4) return 0;
        mod = cand;
    } else {
        call = rhs;
    }
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
    if (mod) *color_slot = mslot;
    return 1;
}

/* Native lit-gouraud vertex shader (see match_lit_gouraud).  Evaluates the
 * diffuse-only ADS formula shared verbatim by sphere.cpp and apps/gears:
 *      ne   = normalize((uNormalMat * vec4(aNormal, 0)).xyz)
 *      L    = normalize(uLightEye.xyz)
 *      diff = max(dot(ne, L), 0)
 *      col  = clamp(uMaterial * (uShade.x + uShade.y*diff), 0, 1)   // spec==0
 *      vColor      = vec4(col, 1)
 *      gl_Position = uMVP * vec4(aPosition, 1)
 * Every step mirrors the interpreter's builtin math (glsl_exec.c) bit-for-bit:
 * column-major mat*vec accumulated col 0..3 (the w column drops out for the
 * normal since vec4(aNormal,0).w==0, and adds 0.0 exactly for the position),
 * normalize divides by sqrtf of the left-to-right summed squares (0 when the
 * length is 0), and max/clamp use the same comparisons.  The specular term is
 * dropped because both callers hard-code `spec = 0.0`, so vec3(uShade.z*spec)
 * has zero magnitude and can only flip a -0.0 sign that quantises to pixel 0. */
static void ff_lit_gouraud_vert(float *vs_output, vec4 *vertex_attribs,
                                Shader_Builtins *builtins, void *uniforms)
{
    interp_entry *e = g_active;
    const glsl_cell *u, *nm, *mvp, *mat, *le, *sh;
    const float *pos, *nrm;
    vec4 *vo = (vec4 *)vs_output;
    float ex, ey, ez, lx, ly, lz, len, diff, k, cr, cg, cb;
    int cc;
    (void)uniforms;
    if (!e) return;
    u   = e->ublock;
    pos = &vertex_attribs[e->lg_pos_loc].x;
    nrm = &vertex_attribs[e->lg_nrm_loc].x;

    /* ne = normalize((uNormalMat * vec4(aNormal, 0)).xyz); row cc = sum_col
     * nm[col*4 + cc] * nrm[col], the w column (col 3) multiplying 0. */
    nm = u + e->lg_nmat_off;
    ex = nm[0 * 4 + 0].f * nrm[0] + nm[1 * 4 + 0].f * nrm[1] + nm[2 * 4 + 0].f * nrm[2];
    ey = nm[0 * 4 + 1].f * nrm[0] + nm[1 * 4 + 1].f * nrm[1] + nm[2 * 4 + 1].f * nrm[2];
    ez = nm[0 * 4 + 2].f * nrm[0] + nm[1 * 4 + 2].f * nrm[1] + nm[2 * 4 + 2].f * nrm[2];
    len = sqrtf(ex * ex + ey * ey + ez * ez);
    if (len > 0.0f) { ex /= len; ey /= len; ez /= len; }
    else { ex = ey = ez = 0.0f; }

    /* L = normalize(uLightEye.xyz) */
    le = u + e->lg_light_off;
    lx = le[0].f; ly = le[1].f; lz = le[2].f;
    len = sqrtf(lx * lx + ly * ly + lz * lz);
    if (len > 0.0f) { lx /= len; ly /= len; lz /= len; }
    else { lx = ly = lz = 0.0f; }

    /* diff = max(dot(ne, L), 0) -- ternary mirrors the interpreter's max */
    diff = ex * lx + ey * ly + ez * lz;
    diff = diff > 0.0f ? diff : 0.0f;

    /* col = clamp(uMaterial * (uShade.x + uShade.y*diff), 0, 1) */
    mat = u + e->lg_mat_off;
    sh  = u + e->lg_shade_off;
    k = sh[0].f + sh[1].f * diff;
    cr = mat[0].f * k; cg = mat[1].f * k; cb = mat[2].f * k;
    if (cr < 0.0f) cr = 0.0f; else if (cr > 1.0f) cr = 1.0f;
    if (cg < 0.0f) cg = 0.0f; else if (cg > 1.0f) cg = 1.0f;
    if (cb < 0.0f) cb = 0.0f; else if (cb > 1.0f) cb = 1.0f;
    vo[0].x = cr; vo[0].y = cg; vo[0].z = cb; vo[0].w = 1.0f;

    /* gl_Position = uMVP * vec4(aPosition, 1.0); column 3 multiplies w == 1. */
    mvp = u + e->lg_mvp_off;
    for (cc = 0; cc < 4; ++cc) {
        float s = mvp[0 * 4 + cc].f * pos[0] + mvp[1 * 4 + cc].f * pos[1]
                + mvp[2 * 4 + cc].f * pos[2] + mvp[3 * 4 + cc].f;
        if (cc == 0) builtins->gl_Position.x = s;
        else if (cc == 1) builtins->gl_Position.y = s;
        else if (cc == 2) builtins->gl_Position.z = s;
        else builtins->gl_Position.w = s;
    }
}

/* Recognise the diffuse-only lit-gouraud program drawn by sphere.cpp and
 * apps/gears: a single smooth vec4 varying, a `gl_FragColor = vColor`
 * passthrough FS, the vec3 aPosition/aNormal attributes and the five uniforms
 * uMVP/uNormalMat (mat4), uMaterial (vec3), uLightEye/uShade (vec4), whose VS
 * writes both gl_Position and the varying.  The lighting formula itself is NOT
 * re-derived from the AST: this exact set of names is the same name-based
 * contract the raspi5 GPU router already uses to select its lit V3D kernel, and
 * both callers spell the identical spec==0 body, so ff_lit_gouraud_vert can
 * evaluate it natively.  Anything that does not match keeps the interpreter, so
 * this fast-path can only skip the tree-walk - never change pixels.  Attribute
 * locations are read by iterating attribs[] by name (glsl_attrib_index returns
 * the .loc, not the array index, and we also need .size). */
static int match_lit_gouraud(const glsl_program *gp, int *pos_loc, int *nrm_loc,
                             int *mvp_off, int *nmat_off, int *mat_off,
                             int *light_off, int *shade_off)
{
    const glsl_stage *vs, *fs;
    const glsl_func *vfn, *ffn;
    const glsl_stmt *s;
    const glsl_expr *e;
    int iu_mvp, iu_nmat, iu_mat, iu_light, iu_shade;
    int ip = -1, in = -1, j, have_pos = 0, have_col = 0;

    if (!gp || !gp->linked || !gp->vs || !gp->fs) return 0;
    if (gp->nvaryings != 1) return 0;
    if (gp->varyings[0].ncomp != 4 || gp->varyings[0].interp != 0) return 0;
    vs = gp->vs; fs = gp->fs;
    if (fs->uses_discard || fs->uses_fragdepth) return 0;
    if (vs->main_idx < 0 || fs->main_idx < 0) return 0;
    vfn = &vs->funcs[vs->main_idx];
    ffn = &fs->funcs[fs->main_idx];
    if (!vfn->body || !ffn->body) return 0;

    /* FS: a single `gl_FragColor = <varying0>;` (identical to match_gouraud) */
    s = ffn->body->children;
    if (!s || s->next || s->kind != ST_EXPR || !s->expr) return 0;
    e = s->expr;
    if (e->kind != EX_ASSIGN || !e->a || !e->b) return 0;
    if (e->a->kind != EX_VAR || e->a->var_scope != VARSCOPE_BUILTIN ||
        e->a->var_slot != BI_GL_FRAGCOLOR) return 0;
    if (e->b->kind != EX_VAR || e->b->var_scope != VARSCOPE_VARYING ||
        e->b->var_slot != 0) return 0;

    /* the five uniforms, by name, with the exact types the formula needs */
    iu_mvp   = glsl_uniform_index(gp, "uMVP");
    iu_nmat  = glsl_uniform_index(gp, "uNormalMat");
    iu_mat   = glsl_uniform_index(gp, "uMaterial");
    iu_light = glsl_uniform_index(gp, "uLightEye");
    iu_shade = glsl_uniform_index(gp, "uShade");
    if (iu_mvp < 0 || iu_nmat < 0 || iu_mat < 0 || iu_light < 0 || iu_shade < 0)
        return 0;
    if (gp->uniforms[iu_mvp].type   != T_MAT4 || gp->uniforms[iu_mvp].ncomp   != 16) return 0;
    if (gp->uniforms[iu_nmat].type  != T_MAT4 || gp->uniforms[iu_nmat].ncomp  != 16) return 0;
    if (gp->uniforms[iu_mat].type   != T_VEC3 || gp->uniforms[iu_mat].ncomp   != 3)  return 0;
    if (gp->uniforms[iu_light].type != T_VEC4 || gp->uniforms[iu_light].ncomp != 4)  return 0;
    if (gp->uniforms[iu_shade].type != T_VEC4 || gp->uniforms[iu_shade].ncomp != 4)  return 0;

    /* aPosition / aNormal, by name, both vec3 at distinct locations */
    for (j = 0; j < gp->nattribs; ++j) {
        if (!strcmp(gp->attribs[j].name, "aPosition")) ip = j;
        else if (!strcmp(gp->attribs[j].name, "aNormal")) in = j;
    }
    if (ip < 0 || in < 0 || ip == in) return 0;
    if (gp->attribs[ip].size != 3 || gp->attribs[in].size != 3) return 0;
    if (gp->attribs[ip].loc == gp->attribs[in].loc) return 0;

    /* VS main must write both gl_Position and the single varying (slot 0); the
     * local declarations and the lighting arithmetic are intentionally not
     * inspected (see the comment above on the name-based contract). */
    for (s = vfn->body->children; s; s = s->next) {
        if (s->kind != ST_EXPR || !s->expr || s->expr->kind != EX_ASSIGN) continue;
        e = s->expr;
        if (!e->a || e->a->kind != EX_VAR) continue;
        if (e->a->var_scope == VARSCOPE_BUILTIN && e->a->var_slot == BI_GL_POSITION)
            have_pos = 1;
        else if (e->a->var_scope == VARSCOPE_VARYING && e->a->var_slot == 0)
            have_col = 1;
    }
    if (!have_pos || !have_col) return 0;

    *pos_loc   = gp->attribs[ip].loc;
    *nrm_loc   = gp->attribs[in].loc;
    *mvp_off   = gp->uniforms[iu_mvp].offset;
    *nmat_off  = gp->uniforms[iu_nmat].offset;
    *mat_off   = gp->uniforms[iu_mat].offset;
    *light_off = gp->uniforms[iu_light].offset;
    *shade_off = gp->uniforms[iu_shade].offset;
    return 1;
}

/* Recognise one of the 11 shadertoy fragment effects by an FNV-1a hash of its
 * source (computed in glsl_compile_stage into glsl_stage.src_hash, which
 * glsl_link keeps reachable via gp->fs).  A source hash is used rather than an
 * AST shape test because 7 of the effects are a bare main() with no user
 * functions to distinguish them by name; the hash separates all 11 exactly.
 * The constants below MUST stay in sync with shadertoy_glsl.h - regenerate with
 * the st_hash host tool if the GLSL ever changes.  On a match it resolves the
 * standard shadertoy uniforms (iGlobalTime / iResolution / iChannel0..2) to
 * their ublock offsets and returns 1; anything else returns 0 and the program
 * keeps the interpreter, so the fast-path can never change pixels. */
static const unsigned long long st_fs_hash[SHADERTOY_N_EFFECTS] = {
    0x708dd5e1d8b2d0bcULL, /* [ 0] graphing_lines       */
    0xbf43d9ff62f33032ULL, /* [ 1] graphing             */
    0xc87b83c9830f490cULL, /* [ 2] my_tunnel            */
    0xf65baebe8f654f32ULL, /* [ 3] running_in_the_night */
    0x3cb175eaa867f1f2ULL, /* [ 4] square_tunnel        */
    0x8e216dc21e883d99ULL, /* [ 5] deform_tunnel        */
    0x5aedcbd17e393095ULL, /* [ 6] tileable_water_caustic */
    0x03d585ef5f748d51ULL, /* [ 7] iqs_eyeball          */
    0x7e40604832fe88afULL, /* [ 8] voronoise            */
    0xc41d7286f7a3f311ULL, /* [ 9] the_cave             */
    0xcb9d78f726a6d8e9ULL, /* [10] flame                */
};

static int match_shadertoy(const glsl_program *gp, int *effect, int *time_off,
                           int *res_off, int ch_off[3])
{
    unsigned long long h;
    int i, idx = -1, iT, iR, s0, s1, s2;
    if (!gp || !gp->linked || !gp->fs) return 0;
    if (gp->fs->uses_discard || gp->fs->uses_fragdepth) return 0;
    h = gp->fs->src_hash;
    for (i = 0; i < SHADERTOY_N_EFFECTS; ++i)
        if (st_fs_hash[i] == h) { idx = i; break; }
    if (idx < 0) return 0;
    iT = glsl_uniform_index(gp, "iGlobalTime");
    iR = glsl_uniform_index(gp, "iResolution");
    if (iT < 0 || iR < 0) return 0;
    if (gp->uniforms[iR].ncomp < 3) return 0;
    s0 = glsl_uniform_index(gp, "iChannel0");
    s1 = glsl_uniform_index(gp, "iChannel1");
    s2 = glsl_uniform_index(gp, "iChannel2");
    /* The native effect bodies receive only scalar coordinates, not expression
     * gradients.  Keep procedural effects on this fast path and let textured
     * effects use the interpreter's derivative-aware sampling. */
    if (s0 >= 0 || s1 >= 0 || s2 >= 0) return 0;
    ch_off[0] = (s0 >= 0) ? gp->uniforms[s0].offset : -1;
    ch_off[1] = (s1 >= 0) ? gp->uniforms[s1].offset : -1;
    ch_off[2] = (s2 >= 0) ? gp->uniforms[s2].offset : -1;
    *effect = idx;
    *time_off = gp->uniforms[iT].offset;
    *res_off = gp->uniforms[iR].offset;
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
    int texblit, sampler_off = 0, uv_slot = 0, color_slot = -1;
    int litgou, lg_pos = 0, lg_nrm = 0, lg_mvp = 0, lg_nmat = 0;
    int lg_mat = 0, lg_light = 0, lg_shade = 0;
    int shadertoy, st_effect = -1, st_time = 0, st_res = 0, st_ch[3] = {-1,-1,-1};
    GLboolean depth_or_discard;
    (void)ublock_cells;
    if (!gp || !gp->linked) return 0;
    for (i = 0; i < GLES_MAX_INTERP; ++i)
        if (!g_reg[i].in_use) { e = &g_reg[i]; break; }
    if (!e) return 0;
    memset(e, 0, sizeof(*e));
    n = gp->nvaryings * 4;
    if (n > GLSL_MAX_VARYINGS * 4) n = GLSL_MAX_VARYINGS * 4;
    for (i = 0; i < n; ++i) interp[i] = PGL_SMOOTH;
    depth_or_discard = (gp->fs && (gp->fs->uses_discard || gp->fs->uses_fragdepth))
                       ? GL_TRUE : GL_FALSE;
    gouraud = match_gouraud(gp, &pos_loc, &color_loc, &mvp_off);
    litgou = gouraud ? 0 : match_lit_gouraud(gp, &lg_pos, &lg_nrm, &lg_mvp,
                                             &lg_nmat, &lg_mat, &lg_light, &lg_shade);
    texblit = (gouraud || litgou) ? 0
            : match_texblit(gp, &sampler_off, &uv_slot, &color_slot);
    shadertoy = (gouraud || litgou || texblit) ? 0
              : match_shadertoy(gp, &st_effect, &st_time, &st_res, st_ch);
    id = pglCreateProgram(gouraud ? ff_gouraud_vert
                                  : (litgou ? ff_lit_gouraud_vert : interp_vert),
                          (gouraud || litgou) ? ff_gouraud_frag
                                  : (texblit ? ff_texblit_frag
                                  : (shadertoy ? ff_shadertoy_frag : interp_frag)),
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
    e->g_color_slot = texblit ? color_slot : -1;
    e->lit_gouraud  = litgou;
    e->lg_pos_loc   = litgou ? lg_pos   : 0;
    e->lg_nrm_loc   = litgou ? lg_nrm   : 0;
    e->lg_mvp_off   = litgou ? lg_mvp   : 0;
    e->lg_nmat_off  = litgou ? lg_nmat  : 0;
    e->lg_mat_off   = litgou ? lg_mat   : 0;
    e->lg_light_off = litgou ? lg_light : 0;
    e->lg_shade_off = litgou ? lg_shade : 0;
    e->st_effect    = shadertoy ? st_effect : -1;
    e->st_time_off  = shadertoy ? st_time : 0;
    e->st_res_off   = shadertoy ? st_res : 0;
    e->st_ch_off[0] = shadertoy ? st_ch[0] : -1;
    e->st_ch_off[1] = shadertoy ? st_ch[1] : -1;
    e->st_ch_off[2] = shadertoy ? st_ch[2] : -1;
    return id;
}

/* ------------------------------------------------------------------ *
 * Direct 1:1 fullscreen textured-blit fast-path
 *
 * Fireworks and matrix composite the whole frame on the CPU into a
 * 0xAARRGGBB buffer, upload it as a GL_RGBA texture, then draw a canonical
 * fullscreen quad (two triangles tiling NDC [-1,1]^2 with uv (0,0)..(1,1))
 * whose FS is `gl_FragColor = texture2D(uTex0, vUV)`.  Even on the native
 * ff_texblit_frag the rasteriser still walks the quad and runs texture2D's
 * floorf/wrap/Color_to_v4 float round-trip for every covered pixel - measured
 * ~6-15 ms/frame for a full-HD blit on the host vs ~0.04-0.1 ms for a direct
 * repack (~155x).  When the draw is provably that canonical blit we repack the
 * bound texture's packed-RGBA words straight into the back buffer with
 * RGBA_TO_PIXEL - byte-for-byte what the rasteriser writes (texture2D returns
 * Color_to_v4(texel) and the fragment stage stores VEC4_TO_COLOR of that, an
 * identity round-trip), verified on the host against real portablegl.  Anything
 * that does not match falls through to pgl_DrawArrays unchanged, so this is a
 * pure fast-path.
 *
 * The canonical quad's v = (1-y)/2 flip exactly cancels portablegl's top-left
 * back-buffer origin, so texel(x,y) lands on back-buffer pixel (x,y): a plain
 * linear repack, no row flip.  Compiled for both virt (CPU) and raspi5
 * (PGL_G3D), where these never-GPU-eligible apps also render on the CPU. */

/* read ncomp floats of one GL_FLOAT attrib for buffer vertex vi (VBO-backed; a
 * client-array attrib fails the buf check and the blit is declined). */
static int blit_read_attrib(const glVertex_Attrib *v, GLsizei vi, float *out, int ncomp)
{
    const glBuffer *b;
    GLsizeiptr off;
    if (!v->enabled || v->type != GL_FLOAT || v->size < ncomp) return 0;
    if (v->buf >= (GLuint)g_ctx.buffers.size) return 0;
    b = &g_ctx.buffers.a[v->buf];
    if (!b->data) return 0;
    off = v->offset + (GLsizeiptr)v->stride * (GLsizeiptr)vi;
    if (off < 0 || off + (GLsizeiptr)ncomp * 4 > (GLsizeiptr)b->size) return 0;
    memcpy(out, b->data + off, (size_t)ncomp * sizeof(float));
    return 1;
}

static int cpu_try_direct_blit(GLenum mode, GLint first, GLsizei count)
{
    interp_entry *e = g_active;
    const glVertex_Attrib *va;
    const glTexture *t;
    const Color *src;
    pix_t *fb;
    GLuint tex;
    GLsizei bw, bh;
    int unit, tri[2], miss[2], b, k, vi;
    long n, px;

    if (!e || !e->texblit || !e->ublock) return 0;

    /* state: an opaque, unspecialised full-frame overwrite */
    if (g_ctx.blend || g_ctx.depth_test || g_ctx.scissor_test ||
        g_ctx.cull_face || g_ctx.logic_ops || g_ctx.fragdepth_or_discard)
        return 0;
#ifndef PGL_NO_STENCIL
    if (g_ctx.stencil_test) return 0;
#endif
    if (g_ctx.color_mask != (pix_t)~(pix_t)0) return 0;

    /* geometry: exactly the canonical 6-vertex fullscreen quad */
    if (mode != GL_TRIANGLES || count != 6 || first != 0) return 0;
    va = g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].vertex_attribs;

    /* bound texture the FS samples (the sampler cell holds the texture unit) */
    unit = e->ublock[e->g_sampler_off].i;
    tex = gles_engine_texture_for_unit(unit);
    if (tex == 0 || (int)tex >= g_ctx.textures.size) return 0;
    t = &g_ctx.textures.a[tex];
    /* the same validity conditions portablegl's own pgl_gpu_tex_get enforces */
    if (t->deleted || t->type != GL_TEXTURE_2D - (GL_TEXTURE_UNBOUND + 1)) return 0;
    if (!t->data || t->w <= 0 || t->h <= 0) return 0;
    if (t->format != GL_RGBA) return 0;
    if (t->mag_filter != GL_NEAREST || t->min_filter != GL_NEAREST) return 0;

    /* viewport must cover the whole back buffer and the texture map 1:1 onto it */
    fb = (pix_t *)pglGetBackBuffer();
    bw = g_ctx.back_buffer.w; bh = g_ctx.back_buffer.h;
    if (!fb || bw <= 0 || bh <= 0) return 0;
    if (g_ctx.xmin != 0 || g_ctx.ymin != 0 ||
        g_ctx.width != bw || g_ctx.height != bh) return 0;
    if (t->w != bw || t->h != bh) return 0;

    /* every vertex must be an NDC corner with u=(x+1)/2, v=(1-y)/2; collect the
     * corner bits of each triangle to confirm the two together tile the square. */
    tri[0] = tri[1] = 0;
    for (vi = 0; vi < 6; ++vi) {
        float p[2], q[2];
        int cx, cy, bit;
        if (!blit_read_attrib(&va[0], (GLsizei)vi, p, 2)) return 0;
        if (!blit_read_attrib(&va[1], (GLsizei)vi, q, 2)) return 0;
        if ((p[0] != -1.0f && p[0] != 1.0f) || (p[1] != -1.0f && p[1] != 1.0f))
            return 0;
        if (q[0] != (p[0] + 1.0f) * 0.5f || q[1] != (1.0f - p[1]) * 0.5f)
            return 0;
        cx = (p[0] > 0.0f);            /* 0 = left, 1 = right */
        cy = (p[1] > 0.0f);            /* 0 = bottom, 1 = top */
        bit = (cy << 1) | cx;          /* BL=0 BR=1 TL=2 TR=3 */
        tri[vi / 3] |= (1 << bit);
    }
    /* each triangle uses 3 distinct corners and the two missing corners are
     * opposite (shared diagonal) => the pair tiles the full square, no overlap
     * and no gap. */
    for (k = 0; k < 2; ++k) {
        int cnt = 0;
        for (b = 0; b < 4; ++b) if (tri[k] & (1 << b)) ++cnt;
        if (cnt != 3) return 0;
        miss[k] = -1;
        for (b = 0; b < 4; ++b) if (!(tri[k] & (1 << b))) miss[k] = b;
    }
    if ((miss[0] ^ miss[1]) != 3) return 0;

    /* 1:1 repack the texture's packed-RGBA into the back buffer. */
    src = (const Color *)t->data;
    n = (long)bw * bh;
    for (px = 0; px < n; ++px)
        fb[px] = RGBA_TO_PIXEL(src[px].r, src[px].g, src[px].b, src[px].a);
    return 1;
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
 * ran.  Non-triangle modes and indexed draws (no opengl consumer uses
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
/* Colour-modulated textured (imgui, cs_is_geom=4) interleaved stride:
 * pos.xyz + (u,v) + rgba + pad.  A separate constant so the gouraud/tex
 * stride-6 staging and its cross-draw coalescing stay untouched. */
#define GPU_TEXCOL_STRIDE  10
/* INSTANCED-LIT coalescing (engine demo, cs_is_geom=3 + PGL_G3D_INSTANCED_LIT):
 * the vertex staging carries a per-vertex instance index at element 6 (stride
 * 7) and each draw appends one 40-float record (MVP mat4 + normal mat4 +
 * material rgb + emissive + alpha + 3 pad) so a whole render pass of many
 * differently-uniformed draws transforms+lights in ONE dispatch.  See
 * geom_xform_instlit_cs_glsl for the record contract. */
#define GPU_INST_STRIDE    7
#define GPU_INST_REC       40
#define GPU_MAX_INST       1024   /* records per batch (engine uses ~36/pass) */
#define GPU_NUNIF          7   /* uMVP .. uShade, uEmissive */

/* The lighting uniforms the gouraud xform kernel reads, matched by name
 * between the compiled kernel (dword offset via pgl_g3d_shader_uniform_dword)
 * and the app's GLSL program (uniform-block float offset).  The textured xform
 * kernel declares only uMVP, so the other five resolve to -1 and are skipped;
 * pgl_g3d_draw_mesh_compiled auto-patches uViewportWH/uFbStride/uTriFirst/
 * uTriCount/uVertCount (+uTexW/uTexH) into the same blob.  The INSTANCED-LIT
 * kernel declares ONLY uLightEye/uShade (its MVP/normal/material/emissive ride
 * the per-instance record, not the blob), so for it uMVP/uModelView/uNormalMat/
 * uMaterial/uEmissive resolve to kernel offset -1 and are packed per-instance
 * by gpu_try_draw_arrays instead - the shared blob then holds just the two
 * lighting uniforms, which is exactly the coalescing key's invariant part. */
static const char *const g_gpu_uname[GPU_NUNIF] = {
    "uMVP", "uModelView", "uNormalMat", "uMaterial", "uLightEye", "uShade",
    "uEmissive"
};

typedef struct {
    GLuint        pgl_id;
    pgl_g3d_prog  prog;
    int           eligible;      /* compiled to a geometry kernel         */
    int           textured;      /* attr1 is uv; needs a raster texture   */
    int           texcol;        /* colour-modulated textured (imgui): attr2 is a
                                    ubyte-normalised colour; indexed draws stage
                                    pos+uv+colour at stride 10 (cs_is_geom=4) via
                                    gpu_try_draw_elements, never gpu_try_draw_arrays */
    int           fullscreen;    /* passthrough VS + gl_FragCoord FS: routed
                                    by gpu_try_fullscreen, not the mesh path */
    int           inst_lit;      /* PGL_G3D_INSTANCED_LIT kernel attached: draws
                                    coalesce across DIFFERENT uniforms into one
                                    instanced dispatch (per-vertex instance index
                                    at staging element 6 + one 40-float record per
                                    draw), and the raster kernel honours the app's
                                    blend/depth-mask/cull state. */
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

/* Set by gles_engine_force_cpu(): pin the context to the CPU rasteriser so
 * gpu_bringup() never adopts the NOCACHE pgl_g3d target, even after an
 * overlay program (Dear ImGui) links as GPU-eligible.  A pure-CPU blit app
 * keeps its cached malloc back buffer and the overlay draws through the
 * interpreter into the same surface. */
static int       g_force_cpu = 0;

/* Set by gles_engine_force_cpu_raster(): a NARROWER lever than g_force_cpu for
 * an app whose MAIN scene is a fullscreen-compute effect (VS has no uMVP + FS
 * reads gl_FragCoord, e.g. waterdrop/shadertoy) that also overlays a
 * GPU-eligible raster HUD (Dear ImGui).  The fullscreen scene MUST stay on the
 * V3D GPU (its CPU-interpreter path is ~35s/frame), so g_force_cpu (which pins
 * the whole context and would drop the scene to the interpreter) is unusable.
 * This flag instead declines ONLY the mesh/texcol raster dispatches
 * (gpu_try_draw_elements + the non-fullscreen branch of gpu_try_draw_arrays) so
 * an overlay HUD renders through the CPU interpreter into the SAME adopted
 * NOCACHE fb the GPU scene wrote -- the exact path the glcommon CPU hud already
 * composites on -- while gpu_try_fullscreen keeps running the scene on the GPU.
 * It does NOT touch gpu_bringup()/g_gpu_ready, so the scene's GPU adoption is
 * unaffected.  Must be set before the render loop. */
static int       g_force_cpu_raster = 0;

/* The RenderWare Adreno route shares the context-wide CPU override switches
 * with the existing fixed-kernel route. */
static int rw_gpu_allowed(void)
{
    return !g_force_cpu && !g_force_cpu_raster;
}

/* texture-name -> raster-slot map for the textured kernel (PGL_G3D_MAX_TEX) */
static GLuint  g_gpu_texname[PGL_G3D_MAX_TEX];
static int     g_gpu_texused[PGL_G3D_MAX_TEX];
/* texture-name -> tiled-ctex-slot map for compiled samplers (gpu_ctex_slot).
 * The raster and tiled maps share the single pgl_g3d _tex[] pool, so each
 * allocator must skip slots the other already owns: otherwise a raster upload
 * (e.g. an ImGui font atlas staged at glTexImage time, even when that program's
 * draw is CPU-forced) overwrites a tiled slot a cached ctex binding still
 * references, the flush then skips the unit (raster pixels) and the compiled
 * draw declines with "TMU unit unbound". */
static GLuint  g_gpu_ctexname[PGL_G3D_MAX_TEX];
static int     g_gpu_ctexused[PGL_G3D_MAX_TEX];
static int     g_gpu_ctexw[PGL_G3D_MAX_TEX];
static int     g_gpu_ctexh[PGL_G3D_MAX_TEX];
static int     g_gpu_ctexdepth[PGL_G3D_MAX_TEX];
static int     g_gpu_ctexlevels[PGL_G3D_MAX_TEX];
static GLuint  g_gpu_ctexgen[PGL_G3D_MAX_TEX][PGL_TEXTURE_FACES][PGL_MAX_MIP_LEVELS];

/* one reusable interleaved staging buffer (grown on demand) */
static float  *g_gpu_stage = NULL;
static int     g_gpu_stage_cap = 0;   /* in vertices */

/* Separate stride-10 staging for the colour-modulated textured (indexed) route.
 * Indexed draws are never coalesced (one imgui command = one dispatch), so it
 * does not share g_gpu_stage, whose GPU_STAGING_STRIDE is 6. */
static float  *g_gpu_tcol = NULL;
static int     g_gpu_tcol_cap = 0;    /* in vertices */

/* Separate stride-7 staging for the INSTANCED-LIT route (pos.xyz + nrm.xyz +
 * instance index).  It never shares g_gpu_stage: an instanced batch stamps a
 * per-vertex draw index that the stride-6 gouraud/tex staging has no room for,
 * and the two routes never mix in one open batch (different program -> the
 * coalesce key always mismatches -> flush). */
static float  *g_gpu_istage = NULL;
static int     g_gpu_istage_cap = 0;  /* in vertices */

/* Per-batch instance-record staging (one 40-float record per coalesced draw,
 * staged to the GPU by pgl_g3d_set_instances at flush). */
static float  *g_inst_rec = NULL;
static int     g_inst_rec_cap = 0;    /* in records */
static int     g_inst_rec_n = 0;      /* records in the open batch */

/* stride is GPU_STAGING_STRIDE (6) for the gouraud/tex routes and
 * GPU_INST_STRIDE (7) for the instanced-lit route; inst is the instance index
 * stamped into element 6 of every vertex gpu_stage_tri appends (ignored when
 * stride < 7). */
typedef struct { float *v; int n; int cap; int stride; float inst; } gpu_stage;

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
/* Open-batch flavour: 1 when the batch is an INSTANCED-LIT accumulation (staged
 * in g_gpu_istage at stride 7 with g_inst_rec_n per-draw records, flushed via
 * upload_mesh_instanced + set_instances), 0 for the plain stride-6 gouraud/tex
 * batch.  g_coalesce_blend is the PGL_G3D_BLEND_* mode joined into the key so an
 * opaque draw never batches with a translucent one. */
static int          g_coalesce_inst = 0;
static uint32_t     g_coalesce_blend = 0;

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
    /* App pinned to the CPU rasteriser: never adopt the NOCACHE target, even
     * though an overlay program may have set g_gpu_has_eligible (see
     * gles_engine_force_cpu). */
    if (g_force_cpu) return;
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
        if (!g_gpu_texused[i] && !g_gpu_ctexused[i]) { g_gpu_texused[i] = 1; g_gpu_texname[i] = name; return i; }
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

/* Map the context's blend state onto a PGL_G3D_BLEND_* code the instanced-lit
 * raster kernel implements, or -1 when the equation is one the kernel cannot
 * reproduce (so the draw must stay on the CPU interpreter).  Blend disabled is
 * opaque (0); glBlendFunc(ONE,ZERO) is a plain copy, also opaque.  The engine
 * demo's translucent shells use SRC_ALPHA/ONE_MINUS_SRC_ALPHA (1) and its
 * additive flame/glow pass uses SRC_ALPHA/ONE (2).  Only FUNC_ADD is supported. */
static int gpu_blend_code(void)
{
    if (!g_ctx.blend) return PGL_G3D_BLEND_OPAQUE;
    if (g_ctx.blend_eqRGB != GL_FUNC_ADD) return -1;
    if (g_ctx.blend_sRGB == GL_ONE && g_ctx.blend_dRGB == GL_ZERO)
        return PGL_G3D_BLEND_OPAQUE;
    if (g_ctx.blend_sRGB == GL_SRC_ALPHA &&
        g_ctx.blend_dRGB == GL_ONE_MINUS_SRC_ALPHA)
        return PGL_G3D_BLEND_ALPHA;
    if (g_ctx.blend_sRGB == GL_SRC_ALPHA && g_ctx.blend_dRGB == GL_ONE)
        return PGL_G3D_BLEND_ADD;
    return -1;   /* any other factor pair: keep it on the CPU */
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

/* Grow the stride-7 instanced-lit vertex staging to nverts vertices. */
static int gpu_istage_reserve(int nverts)
{
    if (g_gpu_istage && nverts <= g_gpu_istage_cap) return 1;
    free(g_gpu_istage);
    g_gpu_istage = (float *)malloc((size_t)nverts * GPU_INST_STRIDE * sizeof(float));
    if (!g_gpu_istage) { g_gpu_istage_cap = 0; return 0; }
    g_gpu_istage_cap = nverts;
    return 1;
}

/* Grow the per-draw instance-record staging to nrec 40-float records. */
static int gpu_inst_rec_reserve(int nrec)
{
    if (g_inst_rec && nrec <= g_inst_rec_cap) return 1;
    free(g_inst_rec);
    g_inst_rec = (float *)malloc((size_t)nrec * GPU_INST_REC * sizeof(float));
    if (!g_inst_rec) { g_inst_rec_cap = 0; return 0; }
    g_inst_rec_cap = nrec;
    return 1;
}

/* Grow the stride-10 colour-tex staging to nverts vertices (indexed route). */
static int gpu_tcol_reserve(int nverts)
{
    if (g_gpu_tcol && nverts <= g_gpu_tcol_cap) return 1;
    free(g_gpu_tcol);
    g_gpu_tcol = (float *)malloc((size_t)nverts * GPU_TEXCOL_STRIDE * sizeof(float));
    if (!g_gpu_tcol) { g_gpu_tcol_cap = 0; return 0; }
    g_gpu_tcol_cap = nverts;
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

/* Read ncomp components of a COLOUR vertex attribute as normalised floats.
 * imgui supplies aColor as GL_UNSIGNED_BYTE normalised (location 2); GL_FLOAT
 * is also accepted.  Any other type returns 0 so the draw declines to the CPU
 * rather than mis-reading the bytes. */
static int gpu_read_attrib_color(const glVertex_Attrib *v, GLsizei vi,
                                 float *out, int ncomp)
{
    const glBuffer *b;
    GLsizeiptr off;
    int k, esz;
    if (v->size < ncomp) return 0;
    if (v->buf >= (GLuint)g_ctx.buffers.size) return 0;
    b = &g_ctx.buffers.a[v->buf];
    if (!b->data) return 0;
    if (v->type == GL_FLOAT)
        esz = 4;
    else if (v->type == GL_UNSIGNED_BYTE && v->normalized)
        esz = 1;
    else
        return 0;
    off = v->offset + (GLsizeiptr)v->stride * (GLsizeiptr)vi;
    if (off < 0 || off + (GLsizeiptr)ncomp * esz > (GLsizeiptr)b->size) return 0;
    if (esz == 4) {
        memcpy(out, b->data + off, (size_t)ncomp * sizeof(float));
    } else {
        const uint8_t *p = (const uint8_t *)b->data + off;
        for (k = 0; k < ncomp; ++k) out[k] = (float)p[k] / 255.0f;
    }
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
        dst = st->v + (size_t)(st->n + k) * st->stride;
        if (!gpu_read_attrib(a0, idx[k], p, 3)) return 0;
        dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
        if (textured) {
            if (!gpu_read_attrib(a1, idx[k], q, 2)) return 0;
            dst[3] = q[0]; dst[4] = q[1]; dst[5] = 0.0f;
        } else {
            if (!gpu_read_attrib(a1, idx[k], q, 3)) return 0;
            dst[3] = q[0]; dst[4] = q[1]; dst[5] = q[2];
        }
        if (st->stride >= GPU_INST_STRIDE) dst[6] = st->inst;
    }
    st->n += 3;
    return 1;
}

/* Append one INDEXED triangle to the stride-10 colour-tex staging: attr0 =
 * position (vec2 -> xyz with z=0, matching the imgui VS's vec4(aPosition,0,1)),
 * attr1 = uv (vec2), attr2 = colour (rgba, normalised).  Layout matches
 * geom_xform_texcol_cs_glsl binding 1: [0..2]=pos [3..4]=uv [5..8]=rgba [9]=pad. */
static int gpu_stage_tri_tcol(gpu_stage *st, const glVertex_Attrib *a0,
                              const glVertex_Attrib *a1, const glVertex_Attrib *a2,
                              GLsizei i0, GLsizei i1, GLsizei i2)
{
    GLsizei idx[3];
    float p[2], q[2], cc[4], *dst;
    int k;
    if (st->n + 3 > st->cap) return 0;
    idx[0] = i0; idx[1] = i1; idx[2] = i2;
    for (k = 0; k < 3; ++k) {
        dst = st->v + (size_t)(st->n + k) * GPU_TEXCOL_STRIDE;
        if (!gpu_read_attrib(a0, idx[k], p, 2)) return 0;
        dst[0] = p[0]; dst[1] = p[1]; dst[2] = 0.0f;
        if (!gpu_read_attrib(a1, idx[k], q, 2)) return 0;
        dst[3] = q[0]; dst[4] = q[1];
        if (!gpu_read_attrib_color(a2, idx[k], cc, 4)) return 0;
        dst[5] = cc[0]; dst[6] = cc[1]; dst[7] = cc[2]; dst[8] = cc[3];
        dst[9] = 0.0f;
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

/* Resolve the glDrawElements index source exactly as portablegl's vertex_stage
 * does: `indices` is a byte OFFSET into the bound GL_ELEMENT_ARRAY_BUFFER when
 * one is bound, else a direct client pointer.  Bounds-checks the offset+count
 * against the buffer for the bound case; returns NULL on an out-of-range read. */
static const void *gpu_index_base(GLenum type, const void *indices, GLsizei count)
{
    int esz = (type == GL_UNSIGNED_SHORT) ? 2 : (type == GL_UNSIGNED_INT ? 4 : 1);
    GLuint eb;
    const glBuffer *b;
    GLsizeiptr need;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT &&
        type != GL_UNSIGNED_BYTE)
        return NULL;
    if (g_ctx.bound_buffers[GL_ELEMENT_ARRAY_BUFFER - GL_ARRAY_BUFFER]) {
        eb = g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].element_buffer;
        if (eb >= (GLuint)g_ctx.buffers.size) return NULL;
        b = &g_ctx.buffers.a[eb];
        if (!b->data) return NULL;
        need = (GLsizeiptr)(uintptr_t)indices + (GLsizeiptr)count * esz;
        if (need > (GLsizeiptr)b->size) return NULL;
        return (const uint8_t *)b->data + (uintptr_t)indices;
    }
    return indices;   /* client-side pointer: trusted, as portablegl does */
}

static long gpu_read_index(const void *base, GLenum type, GLsizei i)
{
    if (type == GL_UNSIGNED_SHORT) return (long)((const uint16_t *)base)[i];
    if (type == GL_UNSIGNED_INT)   return (long)((const uint32_t *)base)[i];
    return (long)((const uint8_t *)base)[i];
}

/* Tessellate an indexed [0,count) draw of `mode` into the stride-10 colour-tex
 * staging, replicating portablegl's winding (see gpu_tessellate) through the
 * resolved index base.  Appends at st->n; returns 0 on an attrib fault. */
static int gpu_tessellate_elements(gpu_stage *st, GLenum mode, GLsizei count,
                                   const void *base, GLenum type,
                                   const glVertex_Attrib *va)
{
    GLsizei i;
    long c0, c1, c2;
    if (mode == GL_TRIANGLES) {
        for (i = 0; i + 2 < count; i += 3) {
            c0 = gpu_read_index(base, type, i);
            c1 = gpu_read_index(base, type, i + 1);
            c2 = gpu_read_index(base, type, i + 2);
            if (!gpu_stage_tri_tcol(st, &va[0], &va[1], &va[2],
                                    (GLsizei)c0, (GLsizei)c1, (GLsizei)c2)) return 0;
        }
    } else if (mode == GL_TRIANGLE_STRIP) {
        long ia = gpu_read_index(base, type, 0);
        long ib = gpu_read_index(base, type, 1);
        int toggle = 0;
        for (i = 2; i < count; ++i) {
            c2 = gpu_read_index(base, type, i);
            if (!gpu_stage_tri_tcol(st, &va[0], &va[1], &va[2],
                                    (GLsizei)ia, (GLsizei)ib, (GLsizei)c2)) return 0;
            if (!toggle) ia = c2; else ib = c2;
            toggle = !toggle;
        }
    } else { /* GL_TRIANGLE_FAN */
        long i0 = gpu_read_index(base, type, 0);
        long prev = gpu_read_index(base, type, 1);
        for (i = 2; i < count; ++i) {
            c2 = gpu_read_index(base, type, i);
            if (!gpu_stage_tri_tcol(st, &va[0], &va[1], &va[2],
                                    (GLsizei)i0, (GLsizei)prev, (GLsizei)c2)) return 0;
            prev = c2;
        }
    }
    return 1;
}

/* Tessellate an indexed [0,count) draw of `mode` into the stride-6 pos+attr1
 * staging used by the textured/gouraud geometry kernels, replicating
 * portablegl's winding (see gpu_tessellate) through the resolved index base.
 * This is the indexed counterpart of gpu_tessellate, so a standard
 * glDrawElements draw reaches the same kernel gpu_try_draw_arrays drives
 * non-indexed.  Appends at st->n; returns 0 on an attrib fault. */
static int gpu_tessellate_elements_mesh(gpu_stage *st, GLenum mode, GLsizei count,
                                        const void *base, GLenum type,
                                        const glVertex_Attrib *va, int textured)
{
    GLsizei i;
    long c0, c1, c2;
    if (mode == GL_TRIANGLES) {
        for (i = 0; i + 2 < count; i += 3) {
            c0 = gpu_read_index(base, type, i);
            c1 = gpu_read_index(base, type, i + 1);
            c2 = gpu_read_index(base, type, i + 2);
            if (!gpu_stage_tri(st, &va[0], &va[1], textured,
                               (GLsizei)c0, (GLsizei)c1, (GLsizei)c2)) return 0;
        }
    } else if (mode == GL_TRIANGLE_STRIP) {
        long ia = gpu_read_index(base, type, 0);
        long ib = gpu_read_index(base, type, 1);
        int toggle = 0;
        for (i = 2; i < count; ++i) {
            c2 = gpu_read_index(base, type, i);
            if (!gpu_stage_tri(st, &va[0], &va[1], textured,
                               (GLsizei)ia, (GLsizei)ib, (GLsizei)c2)) return 0;
            if (!toggle) ia = c2; else ib = c2;
            toggle = !toggle;
        }
    } else { /* GL_TRIANGLE_FAN */
        long i0 = gpu_read_index(base, type, 0);
        long prev = gpu_read_index(base, type, 1);
        for (i = 2; i < count; ++i) {
            c2 = gpu_read_index(base, type, i);
            if (!gpu_stage_tri(st, &va[0], &va[1], textured,
                               (GLsizei)i0, (GLsizei)prev, (GLsizei)c2)) return 0;
            prev = c2;
        }
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
    int ok;
    if (!g_coalesce_active || g_coalesce_n < 3) {
        g_coalesce_active = 0; g_coalesce_n = 0; g_inst_rec_n = 0;
        return;
    }
    /* Uniform staging patches program-specific compute slots. Select this
     * batch's program so a preceding fullscreen draw cannot corrupt its MVP. */
    pgl_g3d_set_shader_program(g_coalesce_prog);
    pgl_g3d_set_uniforms(g_coalesce_blob, PGL_G3D_MAX_UNIF);
    pgl_g3d_set_blend((int)g_coalesce_blend);
    if (g_coalesce_inst) {
        /* INSTANCED-LIT batch: stage the per-draw 40-float records (binding 3 of
         * the xform kernel) then submit the whole accumulated vertex stream -
         * each vertex already stamped with its draw's instance index at element
         * 6 - as ONE transient interleaved dispatch. */
        if (!pgl_g3d_set_instances(g_inst_rec,
                                   (uint32_t)g_inst_rec_n * GPU_INST_REC)) {
            if (g_coalesce_entry) g_coalesce_entry->cpu_latched = 1;
            g_coalesce_active = 0; g_coalesce_n = 0; g_inst_rec_n = 0;
            return;
        }
        ok = pgl_g3d_draw_interleaved_compiled(
                 g_coalesce_prog, g_gpu_istage, (uint32_t)g_coalesce_n,
                 GPU_INST_STRIDE, g_coalesce_depth, g_coalesce_mask,
                 g_coalesce_cull);
    } else {
        ok = pgl_g3d_draw_interleaved_compiled(
                 g_coalesce_prog, g_gpu_stage, (uint32_t)g_coalesce_n,
                 GPU_STAGING_STRIDE, g_coalesce_depth, g_coalesce_mask,
                 g_coalesce_cull);
    }
    if (!ok && g_coalesce_entry) g_coalesce_entry->cpu_latched = 1;
    g_coalesce_active = 0; g_coalesce_n = 0; g_inst_rec_n = 0;
}

/* ------------------------------------------------------------------ *
 * Fullscreen compiled-shader route (shadertoy).
 *
 * A program whose VS is a passthrough (no uMVP, so not one of the fixed
 * geometry kernels) and whose FS derives colour from gl_FragCoord is a
 * fullscreen effect: the compiled FS computes every pixel procedurally from
 * uniforms (iGlobalTime / iResolution) and optional iChannelN samplers, so no
 * per-vertex attribute is staged.  This mirrors portablegl's pgl_gpu_frame:
 * dispatch a fixed NDC quad as two pos-only triangles through
 * pgl_g3d_batch_tri_compiled, with the per-draw uniform blob packed BY NAME
 * (mesa dead-strips unused uniforms, shifting dword offsets per effect) and
 * each sampler bound to a TILED texture slot (the compiled TMU samples UIF
 * images, never the raster rows the fixed TEX_MOD kernel reads).  Anything that
 * cannot back the draw - no GPU, a sampler whose texture is not NEAREST/REPEAT,
 * a declined upload - latches the program to the CPU interpreter, which renders
 * the identical fullscreen quad, so the app never learns which path ran. */

/* name -> TILED slot for a compiled-shader sampler.  Separate from the raster
 * table above: a back-end slot holds either a raster (TEX_MOD) or a tiled
 * (compiled TMU) image, never both.  Stages the glTexture's internal packed
 * words once and caches by name+size, mirroring portablegl's pgl_gpu_ctex_slot
 * (which gates on GL_REPEAT, the raspi5 compiled-sampler record's wrap).
 * Returns -1 when the texture cannot back a compiled sampler, leaving its unit
 * unbound so the draw declines to the CPU. */
static int gpu_ctex_cache_matches(int slot, const glTexture *t,
                                  int depth, int levels)
{
    int face, level;
    if (g_gpu_ctexw[slot] != (int)t->levels[0][0].w ||
        g_gpu_ctexh[slot] != (int)t->levels[0][0].h ||
        g_gpu_ctexdepth[slot] != depth || g_gpu_ctexlevels[slot] != levels)
        return 0;
    for (face = 0; face < depth; ++face)
        for (level = 0; level < levels; ++level)
            if (g_gpu_ctexgen[slot][face][level] !=
                t->levels[face][level].datagen)
                return 0;
    return 1;
}

static int gpu_ctex_slot(GLuint name)
{
    const glTexture *t;
    const glTextureLevel *base;
    pgl_g3d_tex_level_t images[PGL_TEXTURE_FACES * PGL_MAX_MIP_LEVELS];
    int i, slot = -1, is_cube, depth, levels = 1;
    int face, level;
    if (name == 0 || (int)name >= g_ctx.textures.size) return -1;
    t = &g_ctx.textures.a[name];
    is_cube = (t->type == GL_TEXTURE_CUBE_MAP - (GL_TEXTURE_UNBOUND + 1));
    if (t->deleted ||
        (!is_cube && t->type != GL_TEXTURE_2D - (GL_TEXTURE_UNBOUND + 1)) ||
        !pgl_texture_complete(t, is_cube))
        return -1;
    /* The compiled sampler record currently supports REPEAT for 2D.  Cube
     * coordinates remain within a face, so its CLAMP_TO_EDGE object state is
     * already represented by the lowered array fetch. */
    if (!is_cube && (t->wrap_s != GL_REPEAT || t->wrap_t != GL_REPEAT))
        return -1;
    base = &t->levels[0][0];
    depth = is_cube ? PGL_TEXTURE_FACES : 1;
    if (t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR) {
        int w = base->w, h = base->h;
        while (w > 1 || h > 1) {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
            ++levels;
        }
    }
    if (levels > PGL_MAX_MIP_LEVELS) return -1;
    for (face = 0; face < depth; ++face) {
        for (level = 0; level < levels; ++level) {
            const glTextureLevel *src = &t->levels[face][level];
            pgl_g3d_tex_level_t *dst = &images[face * levels + level];
            dst->rgba8 = src->data;
            dst->w = (uint32_t)src->w;
            dst->h = (uint32_t)src->h;
        }
    }
    for (i = 0; i < PGL_G3D_MAX_TEX; ++i)
        if (g_gpu_ctexused[i] && g_gpu_ctexname[i] == name) {
            if (gpu_ctex_cache_matches(i, t, depth, levels))
                return i;
            slot = i;
            break;
        }
    if (slot < 0) {
        /* A back-end slot holds either a raster (TEX_MOD) or a tiled (compiled
         * TMU) image, never both. */
        for (i = 0; i < PGL_G3D_MAX_TEX; ++i)
            if (g_gpu_texused[i] && g_gpu_texname[i] == name) {
                g_gpu_texused[i] = 0;
                g_gpu_texname[i] = 0;
            }
        for (i = 0; i < PGL_G3D_MAX_TEX; ++i)
            if (!g_gpu_ctexused[i] && !g_gpu_texused[i]) { slot = i; break; }
        if (slot < 0) return -1;
    }
    if (!pgl_g3d_upload_texture_mips_tiled(slot, (uint32_t)depth,
                                           (uint32_t)levels, images))
        return -1;   /* unsupported layout/allocation failure: use CPU sampler */
    g_gpu_ctexused[slot] = 1;
    g_gpu_ctexname[slot] = name;
    g_gpu_ctexw[slot] = (int)base->w;
    g_gpu_ctexh[slot] = (int)base->h;
    g_gpu_ctexdepth[slot] = depth;
    g_gpu_ctexlevels[slot] = levels;
    for (face = 0; face < depth; ++face)
        for (level = 0; level < levels; ++level)
            g_gpu_ctexgen[slot][face][level] = t->levels[face][level].datagen;
    return slot;
}

static void gpu_ctex_bind(int unit, GLuint name, int slot)
{
    const glTexture *t = NULL;
    int mag = PGL_G3D_TEX_FILTER_NEAREST;
    int min = PGL_G3D_TEX_FILTER_NEAREST;
    int mip = PGL_G3D_TEX_FILTER_NEAREST;
    if (name != 0 && (int)name < g_ctx.textures.size)
        t = &g_ctx.textures.a[name];
    if (t) {
        mag = t->mag_filter == GL_LINEAR ? PGL_G3D_TEX_FILTER_LINEAR : mag;
        if (t->min_filter == GL_LINEAR ||
            t->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
            t->min_filter == GL_LINEAR_MIPMAP_LINEAR)
            min = PGL_G3D_TEX_FILTER_LINEAR;
        if (t->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
            t->min_filter == GL_LINEAR_MIPMAP_LINEAR)
            mip = PGL_G3D_TEX_FILTER_LINEAR;
    }
    pgl_g3d_bind_tex_unit_sampler(unit, slot, mag, min, mip);
}

/* Free staged tiled slots holding textures the current program does NOT sample.
 * The slot table is sized PGL_G3D_MAX_TEX (4) but the shadertoy texture-effects
 * reference 5 distinct images, and slots are cached per-texture across effects.
 * Without eviction the_cave (the first effect needing a 5th distinct texture,
 * tex09) finds no free slot for its iChannel2, leaves that unit unbound and the
 * compiled draw declines to the CPU - the whole effect then renders on the
 * interpreter/fast-path instead of the V3D.  Evicting only slots unused by this
 * program keeps every draw's own samplers stageable (<=PGL_G3D_MAX_TEX at once);
 * the cost is a re-upload when switching back to an evicted effect. */
static void gpu_ctex_evict_unused(const glsl_program *gp)
{
    GLuint need[PGL_G3D_MAX_TEX];
    int nneed = 0, i, j;
    for (i = 0; i < gp->nuniforms && nneed < PGL_G3D_MAX_TEX; ++i) {
        const glsl_uniform *u = &gp->uniforms[i];
        if (!u->is_sampler) continue;
        need[nneed++] = gles_engine_texture_for_unit(g_active->ublock[u->offset].i);
    }
    for (i = 0; i < PGL_G3D_MAX_TEX; ++i) {
        int keep = 0;
        if (!g_gpu_ctexused[i]) continue;
        for (j = 0; j < nneed; ++j)
            if (need[j] == g_gpu_ctexname[i]) { keep = 1; break; }
        if (!keep) g_gpu_ctexused[i] = 0;
    }
}

static int gpu_try_fullscreen(gpu_entry *e, GLenum mode, GLint first, GLsizei count)
{
    /* NDC full-screen quad (z=0) as two pos-only triangles, wound so NDC y=+1
     * lands on the target's row 0 to match portablegl's y-down CPU convention
     * (identical corners to pgl_gpu_frame).  The compiled FS derives
     * gl_FragCoord from the rasterised window position, so the app's own vertex
     * data is never read. */
    static const float qa[3] = { -1.0f, -1.0f, 0.0f };
    static const float qb[3] = {  1.0f, -1.0f, 0.0f };
    static const float qc[3] = {  1.0f,  1.0f, 0.0f };
    static const float qd[3] = { -1.0f,  1.0f, 0.0f };
    const glsl_program *gp;
    uint32_t blob[PGL_G3D_MAX_UNIF];
    uint32_t nunif = 0;
    int i;

    (void)first;
    if (mode != GL_TRIANGLES || count < 3) return 0;

    /* A scissored frame stays on the CPU: the compiled quad passes no ext
     * scissor, so declining here keeps pixels identical to the interpreter and
     * gives apps a CPU-pin lever (shadertoy "cpu" check mode uses a full-window
     * scissor to A/B the GPU route against the interpreter on one binary). */
    if (g_ctx.scissor_test) return 0;

    /* drain any open geometry batch first so painter's order holds (a fullscreen
     * app issues none, but a mixed frame must not reorder). */
    gpu_coalesce_flush();
    if (!g_gpu_ready) return 0;
    gp = g_active->gp;

    /* Pack every default-block uniform the compiled FS reads, BY NAME: resolve
     * each app uniform to its GPU dword offset and copy its value as raw float
     * bits.  This generalises the mesh path's fixed six-uniform packing so an
     * arbitrary procedural effect (iGlobalTime/iResolution and any others) is
     * staged without a hardcoded layout. */
    memset(blob, 0, sizeof(blob));
    for (i = 0; i < gp->nuniforms; ++i) {
        const glsl_uniform *u = &gp->uniforms[i];
        uint32_t comps = 0;
        int d, k, n;
        if (u->is_sampler) continue;   /* samplers ride the texture path */
        d = pgl_g3d_shader_uniform_dword(e->prog, u->name, &comps);
        if (d < 0 || comps == 0) continue;
        int app_comps = u->ncomp * (u->array_len > 0 ? u->array_len : 1);
        n = ((int)comps < app_comps) ? (int)comps : app_comps;
        for (k = 0; k < n && d + k < (int)PGL_G3D_MAX_UNIF; ++k) {
            float fv = g_active->ublock[u->offset + k].f;
            memcpy(&blob[d + k], &fv, 4);
        }
        if ((uint32_t)(d + n) > nunif) nunif = (uint32_t)(d + n);
    }

    pgl_g3d_set_shader_program(e->prog);
    pgl_g3d_set_uniforms(nunif ? blob : NULL, nunif);

    /* Bind each sampler to a TILED slot.  The compiled FS's TMU slot is the
     * sampler's DECLARATION-ORDER index (mesa's texture_index, assigned densely
     * over the FS's samplers in v3d_tex.c), NOT the GLES2 texture unit the app
     * bound it to with glUniform1i -- so bind ctex by declaration order (tmu)
     * and use the sampler's unit value only to SELECT which texture image rides
     * that slot.  An app that binds iChannel0..N to units 0..N (shadertoy) makes
     * the two coincide; a sparse binding (grass: iChannel0->unit 0,
     * iChannel2->unit 2) still lands each image on the slot the kernel actually
     * samples, instead of stranding it at ctex[2] while the kernel reads TMU
     * slot 1 (which then declines to the CPU).  An unstageable texture leaves
     * its slot unbound, which declines the draw back to the CPU rather than
     * sampling a wrong image.  Evict slots this program doesn't sample first so
     * an effect with more distinct textures than the free remainder still stages
     * all of its samplers (see gpu_ctex_evict_unused). */
    gpu_ctex_evict_unused(gp);
    {
        int tmu = 0;
        for (i = 0; i < gp->nuniforms; ++i) {
            const glsl_uniform *u = &gp->uniforms[i];
            int unit, slot;
            GLuint tex;
            if (!u->is_sampler) continue;
            unit = g_active->ublock[u->offset].i;
            tex = gles_engine_texture_for_unit(unit);
            slot = gpu_ctex_slot(tex);
            gpu_ctex_bind(tmu, tex, slot);
            ++tmu;
        }
    }

    /* First triangle: a declined upload (or a GPU latched off mid-call) returns
     * 0 with nothing dispatched, so latch this program to the CPU and let the
     * caller redraw the whole frame on the interpreter. */
    if (!pgl_g3d_batch_tri_compiled(qa, qb, qc,
                                    PGL_G3D_DEPTH_NONE, 0u, PGL_G3D_CULL_NONE, NULL)) {
        e->cpu_latched = 1;
        return 0;
    }
    pgl_g3d_batch_tri_compiled(qa, qc, qd,
                               PGL_G3D_DEPTH_NONE, 0u, PGL_G3D_CULL_NONE, NULL);
    pgl_g3d_flush();
    if (pgl_g3d_program_declined(e->prog) || !pgl_g3d_enabled()) {
        e->cpu_latched = 1;
        return 0;
    }
    return 1;
}

/* The fixed gouraud/tex geom kernels write every covered fb word verbatim with
 * a forced-opaque alpha: unlike the texcol kernel (gpu_blend_texcol_ok below,
 * which hardcodes imgui's SRC_ALPHA equation) they implement NO blending at
 * all.  A blended draw dispatched on them would overwrite its destination
 * instead of compositing onto it (a translucent shell renders as a solid box
 * hiding everything inside it), so decline it to the CPU interpreter, which
 * reproduces the blend exactly.  glBlendFunc(ONE,ZERO) is a plain copy and
 * stays eligible. */
static int gpu_blend_geom_ok(void)
{
    if (!g_ctx.blend) return 1;
    return g_ctx.blend_sRGB == GL_ONE && g_ctx.blend_dRGB == GL_ZERO;
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
    int blend;

    gpu_bringup();
    if (!g_gpu_ready) return 0;
    if (!g_active || !g_active->gp || !g_active->ublock) return 0;
    e = gpu_find_entry(g_active->pgl_id);
    if (!e || e->cpu_latched) return 0;
    /* a fullscreen compiled program is routed by its own quad dispatch, never
     * the fixed geometry kernels (it exposes no uMVP, so it is not eligible). */
    if (e->fullscreen) return gpu_try_fullscreen(e, mode, first, count);
    /* Raster/geometry overlay forced to the CPU interpreter (see
     * g_force_cpu_raster).  Placed AFTER the fullscreen routing above so a
     * fullscreen-compute main scene still runs on the GPU. */
    if (g_force_cpu_raster) return 0;
    if (!e->eligible) return 0;
    /* The colour-modulated textured variant is indexed-only: its pos+uv+colour
     * stride-10 staging is built by gpu_try_draw_elements, so it must never be
     * tessellated here at GPU_STAGING_STRIDE. */
    if (e->texcol) return 0;

    /* Blend eligibility.  The INSTANCED-LIT raster kernel implements opaque /
     * SRC_ALPHA / additive blending, so an engine draw whose equation maps to a
     * PGL_G3D_BLEND_* code stays on the GPU.  Any other equation - or ANY blend
     * on the plain gouraud/tex kernels, which still cannot blend - drains the
     * open batch first (painter's order across the GPU->CPU handover), then
     * declines to the interpreter which reproduces the blend exactly. */
    blend = gpu_blend_code();
    if (e->inst_lit) {
        if (blend < 0) { gpu_coalesce_flush(); return 0; }
    } else if (!gpu_blend_geom_ok()) {
        gpu_coalesce_flush(); return 0;
    }

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
        int slot, ok;
        gpu_coalesce_flush();
        if (!gpu_stage_reserve(nout)) return 0;
        st.v = g_gpu_stage; st.n = 0; st.cap = nout;
        st.stride = GPU_STAGING_STRIDE; st.inst = 0.0f;
        if (!gpu_tessellate(&st, mode, first, count, va, e)) return 0;
        if (st.n != nout) return 0;   /* defensive: tessellation/staging mismatch */
        pgl_g3d_set_shader_program(e->prog);
        pgl_g3d_set_uniforms(blob, PGL_G3D_MAX_UNIF);
        slot = gpu_tex_slot(gles_engine_texture_for_unit(0), 0);
        if (slot < 0) return 0;   /* no raster texture staged: CPU this draw */
        pgl_g3d_bind_tex_unit(0, slot);
        ok = pgl_g3d_draw_interleaved_compiled(e->prog, g_gpu_stage,
                                               (uint32_t)nout, GPU_STAGING_STRIDE,
                                               depth, mask, cull);
        if (!ok) { e->cpu_latched = 1; return 0; }   /* declined: latch CPU + fall through */
        return 1;
    }

    /* Untextured geom: coalesce consecutive same-key draws into one dispatch.
     * For the plain gouraud route the key is (program, uniform blob, depth
     * func/mask, cull) and every batched draw shares one blob (gears re-packs an
     * identical blob for all 76 strips of a gear -> 6 dispatches/frame).  For the
     * INSTANCED-LIT route the blob holds ONLY the shared lighting (uLightEye/
     * uShade): the per-draw MVP / normal matrix / material / emissive ride a
     * 40-float instance record and a per-vertex instance index, so draws with
     * DIFFERENT transforms still batch into one dispatch.  The key adds the blend
     * mode and the batch flavour; the blob memcmp now only compares lighting. */
    key_match = g_coalesce_active &&
                g_coalesce_entry == e &&
                g_coalesce_prog == e->prog &&
                g_coalesce_inst == (e->inst_lit ? 1 : 0) &&
                g_coalesce_depth == depth && g_coalesce_mask == mask &&
                g_coalesce_cull == cull &&
                g_coalesce_blend == (uint32_t)blend &&
                memcmp(g_coalesce_blob, blob, sizeof(blob)) == 0 &&
                g_coalesce_n + nout <= (int)PGL_G3D_MAX_VERTS &&
                (!e->inst_lit || g_inst_rec_n + 1 <= GPU_MAX_INST);
    if (!key_match) {
        gpu_coalesce_flush();
        if (!g_gpu_ready) return 0;   /* a flush may have latched the GPU off */
        g_coalesce_entry = e;
        g_coalesce_prog  = e->prog;
        g_coalesce_inst  = e->inst_lit ? 1 : 0;
        g_coalesce_depth = depth; g_coalesce_mask = mask; g_coalesce_cull = cull;
        g_coalesce_blend = (uint32_t)blend;
        memcpy(g_coalesce_blob, blob, sizeof(blob));
        g_coalesce_n = 0;
        g_inst_rec_n = 0;
        g_coalesce_active = 1;
    }

    if (e->inst_lit) {
        /* Append this draw's 40-float instance record (MVP + normal matrix +
         * material rgb + emissive + alpha + pad), then tessellate its triangles
         * into the stride-7 staging with every vertex stamped with the record's
         * instance index.  The record's transforms are copied verbatim from the
         * app's uniform block, so the kernel reconstructs the identical mat4 the
         * interpreter's VS would have used. */
        float *rec;
        int mi = e->aoff[0], ni = e->aoff[2], mtl = e->aoff[3], em = e->aoff[6];
        if (mi < 0 || ni < 0 || mtl < 0) { gpu_coalesce_flush(); return 0; }
        if (!gpu_istage_reserve(PGL_G3D_MAX_VERTS) ||
            !gpu_inst_rec_reserve(g_inst_rec_n + 1)) {
            g_coalesce_active = 0; g_coalesce_n = 0; g_inst_rec_n = 0; return 0;
        }
        rec = g_inst_rec + (size_t)g_inst_rec_n * GPU_INST_REC;
        memset(rec, 0, GPU_INST_REC * sizeof(float));
        memcpy(rec + 0,  &g_active->ublock[mi].f,  16 * sizeof(float));  /* uMVP */
        memcpy(rec + 16, &g_active->ublock[ni].f,  16 * sizeof(float));  /* uNormalMat */
        rec[32] = g_active->ublock[mtl + 0].f;   /* material rgb */
        rec[33] = g_active->ublock[mtl + 1].f;
        rec[34] = g_active->ublock[mtl + 2].f;
        rec[35] = (em >= 0) ? g_active->ublock[em].f : 0.0f;   /* emissive */
        rec[36] = g_active->ublock[mtl + 3].f;   /* alpha = uMaterial.a */
        base = g_coalesce_n;
        st.v = g_gpu_istage; st.n = base; st.cap = PGL_G3D_MAX_VERTS;
        st.stride = GPU_INST_STRIDE; st.inst = (float)g_inst_rec_n;
        if (!gpu_tessellate(&st, mode, first, count, va, e) || st.n != base + nout) {
            g_coalesce_n = base;
            gpu_coalesce_flush();
            return 0;
        }
        g_coalesce_n = st.n;
        g_inst_rec_n++;
        return 1;
    }

    if (!gpu_stage_reserve(PGL_G3D_MAX_VERTS)) {
        g_coalesce_active = 0; g_coalesce_n = 0; return 0;
    }
    base = g_coalesce_n;
    st.v = g_gpu_stage; st.n = base; st.cap = PGL_G3D_MAX_VERTS;
    st.stride = GPU_STAGING_STRIDE; st.inst = 0.0f;
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

/* The colour-tex raster kernel hardcodes imgui's blend state: enabled, FUNC_ADD,
 * glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA)
 * - RGB modulated by the source alpha, alpha by GL_ONE.  Anything else declines
 * so the CPU reproduces the exact blend. */
static int gpu_blend_texcol_ok(void)
{
    if (!g_ctx.blend) return 0;
    if (g_ctx.blend_eqRGB != GL_FUNC_ADD || g_ctx.blend_eqA != GL_FUNC_ADD) return 0;
    if (g_ctx.blend_sRGB != GL_SRC_ALPHA ||
        g_ctx.blend_dRGB != GL_ONE_MINUS_SRC_ALPHA) return 0;
    if (g_ctx.blend_sA != GL_ONE ||
        g_ctx.blend_dA != GL_ONE_MINUS_SRC_ALPHA) return 0;
    return 1;
}

/* Indexed TEXTURED draw (standard glDrawElements alias/geometry): expands the
 * index list into the stride-6 pos+uv staging and dispatches the same compiled
 * textured kernel gpu_try_draw_arrays drives non-indexed, so an app keeps
 * portable indexed GL while the platform still routes it onto the V3D.
 * Mirrors the textured branch of gpu_try_draw_arrays (per-draw dispatch, no
 * coalescing, raster slot on unit 0).  Returns 1 when the GPU ran the draw,
 * 0 to fall through to the CPU interpreter (identical pixels). */
static int gpu_try_draw_elements_textured(gpu_entry *e, GLenum mode, GLsizei count,
                                          GLenum type, const void *indices,
                                          const glVertex_Attrib *va)
{
    gpu_stage st;
    uint32_t blob[PGL_G3D_MAX_UNIF];
    uint32_t depth, mask, cull;
    GLsizei nout, i;
    const void *base;
    int slot, ok;

    nout = (mode == GL_TRIANGLES) ? (count / 3) * 3 : (count - 2) * 3;
    if (nout < 3 || (GLuint)nout > PGL_G3D_MAX_VERTS) return 0;

    /* The textured geom kernel cannot blend either: drain any open coalesce
     * batch first so painter's order holds, then decline to the CPU. */
    if (!gpu_blend_geom_ok()) { gpu_coalesce_flush(); return 0; }

    base = gpu_index_base(type, indices, count);
    if (!base) return 0;

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

    gpu_coalesce_flush();
    if (!g_gpu_ready) return 0;   /* a flush may have latched the GPU off */
    if (!gpu_stage_reserve(nout)) return 0;
    st.v = g_gpu_stage; st.n = 0; st.cap = nout;
    st.stride = GPU_STAGING_STRIDE; st.inst = 0.0f;
    if (!gpu_tessellate_elements_mesh(&st, mode, count, base, type, va, 1)) return 0;
    if (st.n != nout) return 0;   /* defensive: tessellation/staging mismatch */

    pgl_g3d_set_shader_program(e->prog);
    pgl_g3d_set_uniforms(blob, PGL_G3D_MAX_UNIF);
    slot = gpu_tex_slot(gles_engine_texture_for_unit(0), 0);
    if (slot < 0) return 0;   /* no raster texture staged: CPU this draw */
    pgl_g3d_bind_tex_unit(0, slot);
    ok = pgl_g3d_draw_interleaved_compiled(e->prog, g_gpu_stage,
                                           (uint32_t)nout, GPU_STAGING_STRIDE,
                                           depth, mask, cull);
    if (!ok) { e->cpu_latched = 1; return 0; }   /* declined: latch CPU + fall through */
    return 1;
}

/* Colour-modulated textured INDEXED draw (imgui, cs_is_geom=4).  Mirrors the
 * textured branch of gpu_try_draw_arrays but reads a GL_ELEMENT_ARRAY_BUFFER,
 * stages pos(vec2)+uv+ubyte-colour at stride 10, marshals the raster font atlas
 * as ctex unit 0, and passes the per-command scissor window (converted to fb row
 * space) via pgl_g3d_set_scissor.  Returns 1 when the GPU ran the draw, 0 to
 * fall through to the CPU interpreter (identical pixels). */
static int gpu_try_draw_elements(GLenum mode, GLsizei count, GLenum type,
                                 const void *indices)
{
    gpu_entry *e;
    const glVertex_Attrib *va;
    const void *base;
    gpu_stage st;
    uint32_t blob[PGL_G3D_MAX_UNIF];
    uint32_t depth, mask, cull;
    GLsizei nout, i;
    int slot, ok, sx, sy, sw, sh;

    gpu_bringup();
    if (!g_gpu_ready) return 0;
    /* Overlay HUD forced onto the CPU interpreter (Dear ImGui over a fullscreen
     * compute scene): decline so gles_engine_draw_elements falls through to
     * pgl_DrawElements, compositing into the same adopted NOCACHE fb.  See
     * g_force_cpu_raster. */
    if (g_force_cpu_raster) return 0;
    if (!g_active || !g_active->gp || !g_active->ublock) return 0;
    e = gpu_find_entry(g_active->pgl_id);
    if (!e || e->cpu_latched || e->fullscreen) return 0;
    if (!e->eligible || (!e->texcol && !e->textured)) return 0;

    if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN)
        return 0;
    if (count < 3) return 0;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT &&
        type != GL_UNSIGNED_BYTE) return 0;

    /* Standard indexed TEXTURED draw (alias geometry): expand the indices into
     * the stride-6 staging and dispatch the compiled textured kernel, handled
     * before the texcol-only state gates below. */
    if (e->textured && !e->texcol) {
        const glVertex_Attrib *tva =
            g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].vertex_attribs;
        if (!tva[0].enabled || tva[0].type != GL_FLOAT || tva[0].size < 3) return 0;
        if (!tva[1].enabled || tva[1].type != GL_FLOAT || tva[1].size < 2) return 0;
        return gpu_try_draw_elements_textured(e, mode, count, type, indices, tva);
    }

    /* The kernel's fixed state: SRC_ALPHA blend, no depth, no culling.  imgui
     * disables the depth test and face culling, so require that; any other
     * state declines to the CPU. */
    if (!gpu_blend_texcol_ok()) return 0;
    if (g_ctx.depth_test || g_ctx.cull_face) return 0;

    va = g_ctx.vertex_arrays.a[g_ctx.cur_vertex_array].vertex_attribs;
    /* attr0 = pos vec2 (float), attr1 = uv vec2 (float), attr2 = colour (ubyte
     * normalised or float); all three must be enabled for the stride-10 stage. */
    if (!va[0].enabled || va[0].type != GL_FLOAT || va[0].size < 2) return 0;
    if (!va[1].enabled || va[1].type != GL_FLOAT || va[1].size < 2) return 0;
    if (!va[2].enabled) return 0;

    nout = (mode == GL_TRIANGLES) ? (count / 3) * 3 : (count - 2) * 3;
    if (nout < 3 || nout > (GLsizei)PGL_G3D_MAX_VERTS) return 0;

    base = gpu_index_base(type, indices, count);
    if (!base) return 0;

    /* Scissor -> fb row space (y down), matching portablegl's ext conversion
     * (scissor_y = fb_h - uy).  An empty window draws nothing, exactly as the
     * CPU clip would, so claim the draw without dispatching. */
    if (g_ctx.scissor_test) {
        int lx = g_ctx.scissor_lx, ly = g_ctx.scissor_ly;
        int ux = lx + g_ctx.scissor_w, uy = ly + g_ctx.scissor_h;
        if (lx < 0) lx = 0;
        if (ly < 0) ly = 0;
        if (ux > g_w) ux = g_w;
        if (uy > g_h) uy = g_h;
        if (ux <= lx || uy <= ly) return 1;   /* fully clipped: nothing to draw */
        sx = lx; sy = g_h - uy; sw = ux - lx; sh = uy - ly;
    } else {
        sx = sy = sw = sh = 0;   /* disabled */
    }

    /* Pack the per-draw uniform blob (uMVP) exactly as the array route does;
     * draw_mesh_compiled patches the derived uViewportWH/uFbStride/uTriFirst/
     * uTriCount/uVertCount/uTexW/uTexH/uScissor by name into the same blob. */
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

    gpu_coalesce_flush();
    if (!g_gpu_ready) return 0;   /* a flush may have latched the GPU off */
    if (!gpu_tcol_reserve(nout)) return 0;
    st.v = g_gpu_tcol; st.n = 0; st.cap = nout;
    if (!gpu_tessellate_elements(&st, mode, count, base, type, va)) return 0;
    if (st.n != nout) return 0;   /* defensive: tessellation/staging mismatch */

    pgl_g3d_set_shader_program(e->prog);
    pgl_g3d_set_uniforms(blob, PGL_G3D_MAX_UNIF);
    slot = gpu_tex_slot(gles_engine_texture_for_unit(0), 0);
    if (slot < 0) return 0;   /* font atlas not staged as a raster slot: CPU */
    pgl_g3d_bind_tex_unit(0, slot);
    pgl_g3d_set_scissor(sx, sy, sw, sh);
    ok = pgl_g3d_draw_interleaved_compiled(e->prog, g_gpu_tcol,
                                           (uint32_t)nout, GPU_TEXCOL_STRIDE,
                                           depth, mask, cull);
    if (!ok) { e->cpu_latched = 1; return 0; }   /* declined: latch CPU + fall through */
    return 1;
}

/* GPU-only shader-source override.  The GLES2 front-end ALWAYS compiles the
 * app's sources through the CPU interpreter first (it owns the CPU fallback
 * and the uniform metadata), but the interpreter caps locals per function
 * (GLSL_MAX_LOCALS) and runs ~22 ms/px on heavy fullscreen effects, while the
 * on-device mesa compiler has neither limit.  An app whose GPU shader is too
 * big for the interpreter therefore links a CHEAP interpreter-safe source and
 * hands the faithful source here: gles_engine_gpu_link compiles THIS on mesa
 * instead.  Uniform packing is unaffected -- gpu_try_fullscreen stages the
 * interpreted program's uniforms BY NAME against the compiled program, so
 * both sources must declare the same uniform names (they do: iGlobalTime /
 * iResolution).  NULL fs clears the override. */
static char *g_gpu_ovr_vs;
static char *g_gpu_ovr_fs;

static char *gpu_src_dup(const char *s)
{
    char *d;
    size_t n;
    if (!s) return NULL;
    n = strlen(s) + 1;
    d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

void gles_engine_set_gpu_source(const char *vs_src, const char *fs_src)
{
    free(g_gpu_ovr_vs);
    free(g_gpu_ovr_fs);
    g_gpu_ovr_vs = gpu_src_dup(vs_src);
    g_gpu_ovr_fs = gpu_src_dup(fs_src);
}

GEuint gles_engine_gpu_link(GEuint pgl_id, const char *vs_src, const char *fs_src)
{
    gpu_entry *e;
    pgl_g3d_prog prog;
    char err[256];
    uint32_t nc;
    int i, is_fullscreen;
    if (!pgl_id || !vs_src || !fs_src) return 0;
    if (g_gpu_ovr_fs) {
        fs_src = g_gpu_ovr_fs;
        if (g_gpu_ovr_vs) vs_src = g_gpu_ovr_vs;
    }
#ifdef PGL_G3D
    rw_simple_link(pgl_id, fs_src);
    /* RenderWare's native fragment contract is consumed by the ordered
     * Adreno compute raster below.  It has no useful portablegl VS/FS
     * graphics program to compile, and the Adreno backend intentionally has
     * no graphics-pipeline port.  Avoid entering that compiler path for every
     * RW shader: besides wasting startup CPU time, it used to print one
     * "graphics pipeline port" decline per linked shader.  Other programs
     * still take the normal fixed/fullscreen GPU probe. */
    {
        interp_entry *rw_ie = find_entry(pgl_id);
        if (rw_ie && rw_ie->rw_simple)
            return 0;
    }
#endif
    err[0] = '\0';
    prog = pgl_g3d_program_compile(vs_src, fs_src, err, (int)sizeof(err));
    if (prog == PGL_G3D_PROG_INVALID) {
        /* A fullscreen-shaped effect (passthrough VS with no uMVP + FS reading
         * gl_FragCoord, e.g. waterdrop/shadertoy/wave) is EXPECTED to run on the
         * V3D GPU on a GPU-capable platform.  If the on-device mesa compiler
         * rejects it -- a reserved gl_* identifier, an unsupported construct --
         * the app would otherwise fall back to the CPU interpreter SILENTLY and
         * merely read "Renderer: CPU" with no clue why.  Log the compiler's
         * error text once so a GPU platform never quietly drops to software. */
        static int fs_decline_logged = 0;
        static int mesh_decline_logged = 0;
        if (!fs_decline_logged &&
            strstr(vs_src, "uMVP") == NULL &&
            strstr(fs_src, "gl_FragCoord") != NULL) {
            fs_decline_logged = 1;
            slog("PGLG3D: fullscreen FS rejected by GPU compiler -> CPU fallback: %s\n",
                 err[0] ? err : "(no error text)");
        }
        /* Same visibility for the MESH shape (uMVP): its GPU route is the fixed
         * geometry kernel pair, so a decline here means those kernels did not
         * compile (or the backend's VS/FS pair gate fired on a backend with no
         * compute kernels) - not an app-shader problem. */
        if (!mesh_decline_logged && strstr(vs_src, "uMVP") != NULL) {
            mesh_decline_logged = 1;
            slog("PGLG3D: mesh program rejected by GPU compiler -> CPU fallback: %s\n",
                 err[0] ? err : "(no error text)");
        }
        return 0;   /* not GPU-viable: the CPU interpreter keeps this program */
    }
    /* A samplerCube fragment shader is handled per-route below (see the
     * is_fullscreen block): the fullscreen-compute route now stages a cubemap
     * as a 6-layer 2D array for the TMU, while the 2D textured-geometry mesh
     * route still cannot, so it declines there. */
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
        /* A fullscreen effect (shadertoy): a passthrough VS with no uMVP (so it
         * is not one of the fixed geometry kernels) whose FS computes colour
         * from gl_FragCoord.  gpu_try_fullscreen drives it through the compiled
         * full-screen-quad path (pgl_g3d_batch_tri_compiled), so it must survive
         * the fixed-kernel decline below even when its FS samples no texture. */
        is_fullscreen = (strstr(vs_src, "uMVP") == NULL &&
                         strstr(fs_src, "gl_FragCoord") != NULL);
        /* A cubemap cannot be staged yet: gpu_ctex_slot's 6-layer 2D-array
         * upload (pgl_g3d_upload_texture_cube_tiled) still fails on-device, so
         * a samplerCube program that reached the fullscreen-compute route would
         * be marked eligible (flipping the framebuffer to NOCACHE and routing
         * the ImGui overlay through GPU geom dispatches) yet decline every draw
         * at the unbound TMU unit - a half-GPU state that garbles the ImGui HUD.
         * Keep samplerCube on the CPU interpreter (where textureCube is fully
         * supported) until the array staging is proven, then relax this gate
         * ATOMICALLY with the working upload. */
        if (strstr(fs_src, "samplerCube") && !is_fullscreen) {
            pgl_g3d_program_free(prog);
            return 0;
        }
        if (!strstr(fs_src, "sampler") && !strstr(vs_src, "uNormalMat") &&
            !gouraud_color && !is_fullscreen) {
            pgl_g3d_program_free(prog);
            return 0;
        }
    }
    e = gpu_alloc_entry(pgl_id);
    if (!e) { pgl_g3d_program_free(prog); return 0; }
    e->prog = prog;
    e->textured = (strstr(fs_src, "sampler") != NULL);
    /* Colour-modulated textured (imgui): the FS multiplies a vec4 varying by a
     * texture2D sample.  Detected with the IDENTICAL predicate pgl_g3d_shader.c
     * uses to attach the cs_is_geom=4 kernels (tex && "* texture2D"), and the
     * engine passes this same fs_src to pgl_g3d_program_compile, so the stride-10
     * indexed staging can never disagree with the kernel the program got. */
    e->texcol = e->textured && strstr(fs_src, "* texture2D") != NULL;
    e->fullscreen = is_fullscreen;
    /* The INSTANCED-LIT kernel (PGL_G3D_INSTANCED_LIT marker in the app VS) is
     * attached by pgl_g3d_program_compile; it declares only uLightEye/uShade (its
     * MVP/normal/material/emissive ride the per-instance record), so it does NOT
     * expose uMVP and must be recognised here rather than by the koff[0] test. */
    e->inst_lit = pgl_g3d_shader_geom_inst_lit(prog);
    e->eligible = 0;
    for (i = 0; i < GPU_NUNIF; ++i) {
        nc = 0;
        e->koff[i] = pgl_g3d_shader_uniform_dword(prog, g_gpu_uname[i], &nc);
        e->knc[i] = (int)nc;
    }
    /* the fixed geometry xform kernel attached exactly when it exposes uMVP (or,
     * for the instanced-lit variant, uLightEye); a fullscreen program has none,
     * so it stays ineligible for the mesh path and is routed by gpu_try_fullscreen
     * instead. */
    e->eligible = ((e->koff[0] >= 0) || e->inst_lit) && !is_fullscreen;
    e->unif_resolved = 0;
    if (e->eligible || e->fullscreen)
        g_gpu_has_eligible = 1;   /* GPU is now worth bringing up (NOCACHE fb) */
    return (GEuint)((e->eligible || e->fullscreen) ? 1 : 0);
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
    rw_gpu_flush();
    /* Submit any draw the engine coalesced but has not yet dispatched (the GPU
     * path merges consecutive same-state draws into one mesh), then drain the
     * portablegl fixed-function batch, so nothing GPU-side is left pending
     * before a present, readback, clear, program switch or CPU-path draw. */
    if (g_gpu_ready) {
        gpu_coalesce_flush();
        pgl_g3d_flush();
    }
}
#ifdef PGL_G3D
static void rw_gpu_legacy_flush(void)
{
    /* A RenderWare draw must retire an earlier portablegl batch before it
     * writes the shared framebuffer.  Most RE3 draws are already on the RW
     * route, however, so calling pgl_g3d_flush() for every one of them just
     * rechecks an empty staging queue.  Keep the ordering barrier only when
     * there is an actual coalesced legacy batch to submit. */
    if (g_gpu_ready && g_coalesce_active) {
        gpu_coalesce_flush();
        pgl_g3d_flush();
    }
}
#endif

int gles_engine_gpu_active(void)
{
    gpu_entry *e;
    if (!g_gpu_ready) return 0;
    if (!g_active || !g_active->gp) return 0;
    e = gpu_find_entry(g_active->pgl_id);
    if (!e || e->cpu_latched) return 0;
    /* eligible = compiled geometry kernel (mesh route); fullscreen = compiled
     * fullscreen-quad kernel (shadertoy route).  Either means the bound program
     * is drawing on the V3D GPU rather than the CPU interpreter. */
    return (e->eligible || e->fullscreen);
}

void gles_engine_force_cpu(void)
{
    /* Pin the context to the CPU rasteriser before any GPU bringup: gpu_bringup
     * then stays inert, so a pure-CPU blit app keeps its cached back buffer and
     * an overlaid GPU-eligible program (Dear ImGui) also draws through the
     * interpreter into that same surface instead of splitting the frame across
     * a CPU blit and a GPU tile dispatch. */
    g_force_cpu = 1;
}

void gles_engine_force_cpu_raster(void)
{
    /* Decline ONLY the mesh/texcol raster dispatches to the CPU interpreter, so
     * a GPU-eligible overlay HUD (Dear ImGui) composites into the same NOCACHE
     * fb that a fullscreen-compute main scene rendered on the GPU -- without
     * pinning that scene off the GPU the way g_force_cpu would (see
     * g_force_cpu_raster).  Must be called before the render loop. */
    g_force_cpu_raster = 1;
}

#else  /* !PGL_G3D: inert so the GPU-agnostic GLES2/EGL TUs link unchanged */

GEuint gles_engine_gpu_link(GEuint pgl_id, const char *vs_src, const char *fs_src)
{ (void)pgl_id; (void)vs_src; (void)fs_src; return 0; }
void gles_engine_gpu_delete(GEuint pgl_id) { (void)pgl_id; }
void gles_engine_gpu_flush(void) { }
int  gles_engine_gpu_active(void) { return 0; }
void gles_engine_force_cpu(void) { }
void gles_engine_force_cpu_raster(void) { }
void gles_engine_set_gpu_source(const char *vs_src, const char *fs_src)
{ (void)vs_src; (void)fs_src; }

#endif /* PGL_G3D */
