/* egl_api.c - the public libEGL entry points for EwokOS.
 *
 * Native window = xwin_t*.  An EGLSurface wraps the xwin_t plus the portablegl
 * engine back buffer; eglSwapBuffers presents by blitting the engine back
 * buffer into the window graph_t under the xwin painting lock, using the same
 * on_repaint + xwin_repaint mechanism the SDL2 and gears paths use.
 *
 * This TU includes <x/xwin.h> + <graph/graph.h> + <EGL/egl.h> + gles_engine.h.
 * It must NOT include portablegl.h or GLES2/gl2.h (the engine header is the
 * neutral bridge; see gles_engine.h for why those two clash).
 *
 * Phase 2 scope: single-display, single-config, single-context object model with
 * a working window/pbuffer surface and swap.  Phase 3 hardens resize, config
 * attribute matching and swap-interval throttling.
 */
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <x/xwin.h>
#include <graph/graph.h>
#include <EGL/egl.h>

#include "../engine/gles_engine.h"

/* ------------------------------------------------------------------ *
 * Internal object model
 * ------------------------------------------------------------------ */
typedef struct egl_display {
    EGLint  initialized;
    EGLint  major;
    EGLint  minor;
} egl_display_t;

typedef struct egl_config {
    EGLint id;
    EGLint red, green, blue, alpha;
    EGLint depth, stencil;
    EGLint surface_types;
    EGLint renderable;
} egl_config_t;

typedef struct egl_context {
    EGLint client_version;
    struct egl_surface *bound_draw;
} egl_context_t;

typedef struct egl_surface {
    EGLint type;              /* EGL_WINDOW_BIT or EGL_PBUFFER_BIT */
    xwin_t *xwin;             /* NULL for pbuffers */
    EGLint w, h;
    EGLint swap_interval;
    /* Set only for the duration of an eglSwapBuffers->xwin_repaint call.
     * libx's event thread runs xwin_retry_pending_presents() (which invokes
     * on_repaint) on every input event; without this gate such an async repaint
     * could blit the engine back buffer while the app is mid-frame, publishing
     * a half-drawn picture (the same race the SDL2 EwokOS backend hit).  When
     * frame_ready is 0 the callback leaves ws_g untouched, so the last complete
     * frame stays on screen. */
    volatile int frame_ready;
    egl_context_t *ctx;
} egl_surface_t;

static egl_display_t g_display = { 0, 1, 4 };
static egl_config_t  g_config  = {
    /* id */ 1,
    /* rgba */ 8, 8, 8, 8,
    /* depth/stencil */ 16, 8,
    /* surface_types */ EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
    /* renderable */ EGL_OPENGL_ES2_BIT
};

static EGLint g_error = EGL_SUCCESS;
static egl_surface_t *g_current_draw = NULL;
static egl_context_t *g_current_ctx  = NULL;

static EGLBoolean set_err(EGLint e) { g_error = e; return EGL_FALSE; }

/* ------------------------------------------------------------------ *
 * Present callback: blit the engine back buffer into the window graph
 * ------------------------------------------------------------------ */
static void egl_on_repaint(xwin_t *xwin, graph_t *g)
{
    egl_surface_t *s;
    void  *bb;
    GEint  bw = 0, bh = 0;
    graph_t src;

    if (xwin == NULL || g == NULL || g->buffer == NULL)
        return;
    s = (egl_surface_t *)xwin->data;
    /* Only publish on an explicit eglSwapBuffers; an async event-thread retry
     * (frame_ready == 0) must not copy a possibly-incomplete back buffer. */
    if (s == NULL || !s->frame_ready)
        return;
    bb = gles_engine_backbuffer();
    if (bb == NULL)
        return;
    gles_engine_backbuffer_size(&bw, &bh);
    if (bw <= 0 || bh <= 0 || gles_engine_bitdepth() != 32)
        return;

    /* engine back buffer is packed u32 0xAARRGGBB, identical to graph_t.
     * No lock here: this callback runs inside xwin_repaint, which already holds
     * xwin->painting_lock across the on_repaint call.  Re-locking it would
     * self-deadlock (EwokOS pthread_mutex_t is non-recursive), so the blit into
     * ws_g is done directly, exactly like the SDL backend's fb_on_repaint. */
    graph_init(&src, (const uint32_t *)bb, bw, bh);
    graph_blt(&src, 0, 0, bw, bh, g, 0, 0, g->w, g->h);
}

static void egl_on_resize(xwin_t *xwin)
{
    egl_surface_t *s;
    int w, h;
    if (xwin == NULL || xwin->xinfo == NULL)
        return;
    w = xwin->xinfo->wsr.w;
    h = xwin->xinfo->wsr.h;
    if (w <= 0 || h <= 0)
        return;
    gles_engine_resize(w, h);
    s = (egl_surface_t *)xwin->data;
    if (s != NULL) { s->w = w; s->h = h; }
}

/* ------------------------------------------------------------------ *
 * Display
 * ------------------------------------------------------------------ */
EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    (void)display_id;
    return (EGLDisplay)&g_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    if (dpy != (EGLDisplay)&g_display)
        return set_err(EGL_BAD_DISPLAY);
    g_display.initialized = 1;
    g_display.major = 1;
    g_display.minor = 4;
    if (major) *major = g_display.major;
    if (minor) *minor = g_display.minor;
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    if (dpy != (EGLDisplay)&g_display)
        return set_err(EGL_BAD_DISPLAY);
    g_display.initialized = 0;
    g_current_draw = NULL;
    g_current_ctx = NULL;
    gles_engine_fini();
    return EGL_TRUE;
}

EGLint eglGetError(void)
{
    EGLint e = g_error;
    g_error = EGL_SUCCESS;
    return e;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    if (dpy != (EGLDisplay)&g_display) { g_error = EGL_BAD_DISPLAY; return NULL; }
    switch (name) {
    case EGL_VENDOR:      return "EwokOS";
    case EGL_VERSION:     return "1.4 EwokOS libEGL (portablegl backend)";
    case EGL_CLIENT_APIS: return "OpenGL_ES2";
    case EGL_EXTENSIONS:  return "";
    default:              g_error = EGL_BAD_PARAMETER; return NULL;
    }
}

/* ------------------------------------------------------------------ *
 * Configs
 * ------------------------------------------------------------------ */
static EGLBoolean config_matches(const egl_config_t *c, const EGLint *attrib)
{
    if (attrib == NULL)
        return EGL_TRUE;
    for (; *attrib != EGL_NONE; attrib += 2) {
        EGLint v = attrib[1];
        switch (attrib[0]) {
        case EGL_RENDERABLE_TYPE: if ((c->renderable & v) == 0) return EGL_FALSE; break;
        case EGL_SURFACE_TYPE:    if ((c->surface_types & v) != v) return EGL_FALSE; break;
        case EGL_RED_SIZE:        if (c->red   < v) return EGL_FALSE; break;
        case EGL_GREEN_SIZE:      if (c->green < v) return EGL_FALSE; break;
        case EGL_BLUE_SIZE:       if (c->blue  < v) return EGL_FALSE; break;
        case EGL_ALPHA_SIZE:      if (c->alpha < v) return EGL_FALSE; break;
        case EGL_DEPTH_SIZE:      if (c->depth < v) return EGL_FALSE; break;
        case EGL_STENCIL_SIZE:    if (c->stencil < v) return EGL_FALSE; break;
        default: break;
        }
    }
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (!config_matches(&g_config, attrib_list)) {
        if (num_config) *num_config = 0;
        return EGL_TRUE;
    }
    if (configs && config_size > 0)
        configs[0] = (EGLConfig)&g_config;
    if (num_config) *num_config = 1;
    return EGL_TRUE;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    return eglChooseConfig(dpy, NULL, configs, config_size, num_config);
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
    const egl_config_t *c = (const egl_config_t *)config;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (c != &g_config || value == NULL) return set_err(EGL_BAD_CONFIG);
    switch (attribute) {
    case EGL_CONFIG_ID:      *value = c->id; break;
    case EGL_RED_SIZE:       *value = c->red; break;
    case EGL_GREEN_SIZE:     *value = c->green; break;
    case EGL_BLUE_SIZE:      *value = c->blue; break;
    case EGL_ALPHA_SIZE:     *value = c->alpha; break;
    case EGL_DEPTH_SIZE:     *value = c->depth; break;
    case EGL_STENCIL_SIZE:   *value = c->stencil; break;
    case EGL_SURFACE_TYPE:   *value = c->surface_types; break;
    case EGL_RENDERABLE_TYPE:*value = c->renderable; break;
    case EGL_COLOR_BUFFER_TYPE: *value = EGL_RGB_BUFFER; break;
    case EGL_CONFIG_CAVEAT:  *value = EGL_NONE; break;
    case EGL_MAX_PBUFFER_WIDTH:  *value = 4096; break;
    case EGL_MAX_PBUFFER_HEIGHT: *value = 4096; break;
    case EGL_NATIVE_RENDERABLE:  *value = EGL_TRUE; break;
    case EGL_SAMPLES:        *value = 0; break;
    case EGL_SAMPLE_BUFFERS: *value = 0; break;
    default:                 *value = 0; break;
    }
    return EGL_TRUE;
}

/* ------------------------------------------------------------------ *
 * Contexts
 * ------------------------------------------------------------------ */
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint *attrib_list)
{
    egl_context_t *ctx;
    (void)share_context;
    if (dpy != (EGLDisplay)&g_display) { set_err(EGL_BAD_DISPLAY); return EGL_NO_CONTEXT; }
    if (config != (EGLConfig)&g_config) { set_err(EGL_BAD_CONFIG); return EGL_NO_CONTEXT; }
    ctx = (egl_context_t *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) { set_err(EGL_BAD_ALLOC); return EGL_NO_CONTEXT; }
    ctx->client_version = 2;
    if (attrib_list) {
        for (; *attrib_list != EGL_NONE; attrib_list += 2) {
            if (attrib_list[0] == EGL_CONTEXT_CLIENT_VERSION)
                ctx->client_version = attrib_list[1];
        }
    }
    return (EGLContext)ctx;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (ctx == EGL_NO_CONTEXT) return set_err(EGL_BAD_CONTEXT);
    if ((egl_context_t *)ctx == g_current_ctx)
        g_current_ctx = NULL;
    free(ctx);
    return EGL_TRUE;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (ctx == EGL_NO_CONTEXT || value == NULL) return set_err(EGL_BAD_CONTEXT);
    if (attribute == EGL_CONTEXT_CLIENT_VERSION)
        *value = ((egl_context_t *)ctx)->client_version;
    else
        *value = 0;
    return EGL_TRUE;
}

/* ------------------------------------------------------------------ *
 * Surfaces
 * ------------------------------------------------------------------ */
static int parse_wh(const EGLint *attrib, int dw, int dh, int *w, int *h)
{
    *w = dw; *h = dh;
    if (attrib) {
        for (; *attrib != EGL_NONE; attrib += 2) {
            if (attrib[0] == EGL_WIDTH)  *w = attrib[1];
            if (attrib[0] == EGL_HEIGHT) *h = attrib[1];
        }
    }
    return 1;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win, const EGLint *attrib_list)
{
    egl_surface_t *s;
    xwin_t *xwin = (xwin_t *)win;
    graph_t g;
    (void)attrib_list;

    if (dpy != (EGLDisplay)&g_display) { set_err(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    if (config != (EGLConfig)&g_config) { set_err(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    if (xwin == NULL) { set_err(EGL_BAD_NATIVE_WINDOW); return EGL_NO_SURFACE; }

    if (xwin_fetch_graph(xwin, &g) == NULL || g.buffer == NULL || g.w <= 0 || g.h <= 0) {
        set_err(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }

    s = (egl_surface_t *)calloc(1, sizeof(*s));
    if (s == NULL) { set_err(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
    s->type = EGL_WINDOW_BIT;
    s->xwin = xwin;
    s->w = g.w;
    s->h = g.h;
    s->swap_interval = 1;

    if (!gles_engine_init(g.w, g.h)) {
        free(s);
        set_err(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    gles_engine_resize(g.w, g.h);

    pthread_mutex_lock(&xwin->painting_lock);
    xwin->data = s;
    xwin->on_repaint = egl_on_repaint;
    xwin->on_resize = egl_on_resize;
    pthread_mutex_unlock(&xwin->painting_lock);

    return (EGLSurface)s;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list)
{
    egl_surface_t *s;
    int w = 0, h = 0;
    if (dpy != (EGLDisplay)&g_display) { set_err(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    if (config != (EGLConfig)&g_config) { set_err(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    parse_wh(attrib_list, 1, 1, &w, &h);
    if (w <= 0 || h <= 0) { set_err(EGL_BAD_ATTRIBUTE); return EGL_NO_SURFACE; }

    s = (egl_surface_t *)calloc(1, sizeof(*s));
    if (s == NULL) { set_err(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
    s->type = EGL_PBUFFER_BIT;
    s->xwin = NULL;
    s->w = w;
    s->h = h;
    if (!gles_engine_init(w, h)) { free(s); set_err(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
    gles_engine_resize(w, h);
    return (EGLSurface)s;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    egl_surface_t *s = (egl_surface_t *)surface;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (s == NULL) return set_err(EGL_BAD_SURFACE);
    if (s->xwin != NULL) {
        pthread_mutex_lock(&s->xwin->painting_lock);
        if (s->xwin->data == s) {
            s->xwin->data = NULL;
            s->xwin->on_repaint = NULL;
            s->xwin->on_resize = NULL;
        }
        pthread_mutex_unlock(&s->xwin->painting_lock);
    }
    if (g_current_draw == s) g_current_draw = NULL;
    free(s);
    return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
    egl_surface_t *s = (egl_surface_t *)surface;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (s == NULL || value == NULL) return set_err(EGL_BAD_SURFACE);
    switch (attribute) {
    case EGL_WIDTH:  *value = s->w; break;
    case EGL_HEIGHT: *value = s->h; break;
    case EGL_CONFIG_ID: *value = g_config.id; break;
    default: *value = 0; break;
    }
    return EGL_TRUE;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
    (void)dpy; (void)surface; (void)attribute; (void)value;
    return EGL_TRUE;
}

/* ------------------------------------------------------------------ *
 * Current binding / swap
 * ------------------------------------------------------------------ */
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
    egl_surface_t *s = (egl_surface_t *)draw;
    (void)read;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);

    if (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE) {
        g_current_ctx = NULL;
        g_current_draw = NULL;
        return EGL_TRUE;
    }
    if (s == NULL) return set_err(EGL_BAD_SURFACE);
    if (ctx == EGL_NO_CONTEXT) return set_err(EGL_BAD_CONTEXT);

    gles_engine_make_current();
    gles_engine_viewport(0, 0, s->w, s->h);
    g_current_draw = s;
    g_current_ctx = (egl_context_t *)ctx;
    s->ctx = g_current_ctx;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    egl_surface_t *s = (egl_surface_t *)surface;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (s == NULL) return set_err(EGL_BAD_SURFACE);
    if (s->type == EGL_PBUFFER_BIT || s->xwin == NULL)
        return EGL_TRUE;   /* nothing to present */
    /* Drain any pending V3D GPU work into the adopted back buffer before the
     * blit (a no-op on non-PGL_G3D builds where the engine stub returns
     * immediately), so egl_on_repaint presents the fully rendered frame. */
    gles_engine_gpu_flush();
    /* Present by requesting a repaint; xwin_repaint invokes egl_on_repaint,
     * which blits the engine back buffer into the window graph under the
     * painting lock.  frame_ready gates that blit so an async event-thread
     * retry never publishes a partial frame. */
    s->frame_ready = 1;
    xwin_repaint(s->xwin);
    s->frame_ready = 0;
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (g_current_draw) g_current_draw->swap_interval = interval;
    return EGL_TRUE;
}

EGLDisplay eglGetCurrentDisplay(void)
{
    return g_current_ctx ? (EGLDisplay)&g_display : EGL_NO_DISPLAY;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
    (void)readdraw;
    return g_current_draw ? (EGLSurface)g_current_draw : EGL_NO_SURFACE;
}

/* ------------------------------------------------------------------ *
 * Entry points that this backend does not support; return clean errors.
 * ------------------------------------------------------------------ */
EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{ (void)dpy; (void)surface; (void)target; return set_err(EGL_BAD_SURFACE); }

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativePixmapType pixmap, const EGLint *attrib_list)
{ (void)dpy; (void)config; (void)pixmap; (void)attrib_list; set_err(EGL_BAD_NATIVE_PIXMAP); return EGL_NO_SURFACE; }

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer,
                                            EGLConfig config, const EGLint *attrib_list)
{ (void)dpy; (void)buftype; (void)buffer; (void)config; (void)attrib_list; set_err(EGL_BAD_PARAMETER); return EGL_NO_SURFACE; }

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{ (void)dpy; (void)surface; (void)buffer; return set_err(EGL_BAD_SURFACE); }

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{ (void)dpy; (void)surface; (void)buffer; return set_err(EGL_BAD_SURFACE); }

EGLBoolean eglBindAPI(EGLenum api)
{
    if (api == EGL_OPENGL_ES_API) return EGL_TRUE;
    return set_err(EGL_BAD_PARAMETER);
}

EGLenum eglQueryAPI(void) { return EGL_OPENGL_ES_API; }

EGLBoolean eglWaitGL(void)     { return EGL_TRUE; }
EGLBoolean eglWaitClient(void) { return EGL_TRUE; }
EGLBoolean eglWaitNative(EGLint engine) { (void)engine; return EGL_TRUE; }
EGLBoolean eglReleaseThread(void)
{
    g_current_draw = NULL;
    g_current_ctx = NULL;
    return EGL_TRUE;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    (void)procname;
    return NULL;   /* all supported entry points are exported directly */
}
