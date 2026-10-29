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
#include <stdio.h>
#include <time.h>

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
    EGLint samples;
    EGLint sample_buffers;
} egl_config_t;

typedef struct egl_context {
    EGLint client_version;
    struct egl_surface *bound_draw;
} egl_context_t;

typedef struct egl_surface {
    EGLint type;              /* EGL_WINDOW_BIT or EGL_PBUFFER_BIT */
    xwin_t *xwin;             /* NULL for pbuffers */
    EGLint w, h;
    EGLint render_w, render_h;
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
static egl_config_t  g_configs[2] = {
    {
        /* id */ 1,
        /* rgba */ 8, 8, 8, 8,
        /* depth/stencil */ 16, 8,
        /* surface_types */ EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        /* renderable */ EGL_OPENGL_ES2_BIT,
        /* samples */ 0, /* sample_buffers */ 0
    },
    {
        /* id */ 2,
        /* rgba */ 8, 8, 8, 8,
        /* depth/stencil */ 16, 8,
        /* surface_types */ EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        /* renderable */ EGL_OPENGL_ES2_BIT,
        /* samples */ 4, /* sample_buffers */ 1
    }
};
#define NUM_CONFIGS 2

static EGLint g_error = EGL_SUCCESS;
static egl_surface_t *g_current_draw = NULL;
static egl_context_t *g_current_ctx  = NULL;

/* Present timing is deliberately diagnostic-only.  Keep the normal EGL path
 * free of clock calls unless PGL_PROFILE is enabled; the profile output lets
 * us separate engine dispatch from the final G2D/window handoff. */
static int g_egl_profile = -1;
static int g_egl_direct_allowed = -1;
static unsigned long long g_egl_epoch;
static unsigned g_egl_frames;
static unsigned long long g_egl_swap_us;
static unsigned long long g_egl_flush_us;
static unsigned long long g_egl_repaint_us;
static unsigned long long g_egl_blit_us;
static unsigned g_egl_direct;
static unsigned g_egl_fallback;
static unsigned long long egl_profile_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ull +
           (unsigned long long)ts.tv_nsec / 1000ull;
}
static int egl_profile_enabled(void)
{
    if (g_egl_profile < 0) {
        const char *v = getenv("PGL_PROFILE");
        g_egl_profile = v && atoi(v) != 0;
    }
    return g_egl_profile;
}
static int egl_direct_allowed(void)
{
    if (g_egl_direct_allowed < 0) {
        const char *v = getenv("PGL_PRESENT_DIRECT");
        /* Directly writing the fps_async handoff buffer is only safe when the
         * matching xserverd/xwin implementation is present in the image.
         * Keep the portable ws_g -> ws_g2 handoff as the default so an SD
         * image with an older compositor cannot turn a valid frame into a
         * black window.  The direct path remains available for an explicit
         * board-side A/B run with PGL_PRESENT_DIRECT=1. */
        g_egl_direct_allowed = v && atoi(v) != 0;
    }
    return g_egl_direct_allowed;
}

static int egl_native_render_size(int *w, int *h)
{
    const char *v = getenv("RE3_NATIVE_RENDER_SIZE");
    int rw, rh;
    if (v == NULL || sscanf(v, "%dx%d", &rw, &rh) != 2 || rw <= 0 || rh <= 0)
        return 0;
    *w = rw;
    *h = rh;
    return 1;
}
static void egl_profile_report(unsigned long long now, int bw, int bh,
                               int dw, int dh, int shm)
{
    if (!egl_profile_enabled())
        return;
    if (!g_egl_epoch) {
        g_egl_epoch = now;
        printf("EGL_PROFILE present backbuffer=%dx%d window=%dx%d shm=%d direct_allowed=%d\n",
               bw, bh, dw, dh, shm, egl_direct_allowed());
        fflush(stdout);
    }
    ++g_egl_frames;
    if (now - g_egl_epoch < 5000000ull)
        return;
    printf("EGL_PROFILE frames=%u swap=%.1fms flush=%.1fms repaint=%.1fms blit=%.1fms direct=%u fallback=%u\n",
           g_egl_frames, g_egl_swap_us / 1000.0, g_egl_flush_us / 1000.0,
           g_egl_repaint_us / 1000.0, g_egl_blit_us / 1000.0,
           g_egl_direct, g_egl_fallback);
    fflush(stdout);
    g_egl_epoch = now;
    g_egl_frames = 0;
    g_egl_swap_us = g_egl_flush_us = g_egl_repaint_us = g_egl_blit_us = 0;
    g_egl_direct = g_egl_fallback = 0;
}

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
    unsigned long long blit_start = 0;
    int direct_present = 0;
    int dx = 0, dy = 0, dw, dh;

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
     * ws_g is done directly, exactly like the SDL backend's fb_on_repaint.
     *
     * When the back buffer is the adopted GPU target it lives in NOCACHE contig
     * shm; tag the source graph with that segment so graph_blt takes the g2d
     * copy (GPU, same daemon that just rendered it) rather than the CPU reading
     * the whole uncached frame.  A CPU back buffer (shm 0) keeps the CPU blit. */
    graph_init(&src, (const uint32_t *)bb, bw, bh);
    src.shm_id = gles_engine_backbuffer_shm();
    src.shm_contig = src.shm_id > 0;

    /* RE3 renders at its logical size while the xwin workspace is fullscreen.
     * Scale into the largest centered rectangle that fits and clear first so
     * the unused letterbox/pillarbox area stays black. */
    dw = g->w;
    dh = g->h;
    if (s->render_w > 0 && s->render_h > 0 &&
            (s->render_w != g->w || s->render_h != g->h)) {
        dh = (int)(((long long)dw * s->render_h) / s->render_w);
        if (dh > g->h) {
            dh = g->h;
            dw = (int)(((long long)dh * s->render_w) / s->render_h);
        }
        dx = (g->w - dw) / 2;
        dy = (g->h - dh) / 2;
    }
    graph_clear(g, 0xFF000000u);
    if (egl_profile_enabled())
        blit_start = egl_profile_us();
    /* fps_async normally copies ws_g to ws_g2 after this callback.  The GPU
       backbuffer is already a complete frame at this point, so publish it
       straight into ws_g2 and avoid that second full-window copy.  If the
       compositor still owns ws_g2, retain the old ws_g path; xwin will mark
       the frame pending and retry it without dropping the picture. */
    if (egl_direct_allowed() && s->render_w == g->w && s->render_h == g->h &&
            xwin->xinfo != NULL &&
            xwin->xinfo->fps_async)
        direct_present = xwin_present_direct(xwin, &src);
    if (direct_present) {
        if (egl_profile_enabled())
            ++g_egl_direct;
    }
    else {
        if (egl_profile_enabled())
            ++g_egl_fallback;
        /* The workspace now contains this frame, so a later async retry may
           safely publish ws_g if the compositor was busy. */
        xwin->direct_frame_valid = false;
        graph_blt(&src, 0, 0, bw, bh, g, dx, dy, dw, dh);
    }
    if (blit_start)
        g_egl_blit_us += egl_profile_us() - blit_start;
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
    s = (egl_surface_t *)xwin->data;
    if (s != NULL) {
        s->w = w;
        s->h = h;
        if (s->render_w == w && s->render_h == h)
            gles_engine_resize(w, h);
    }
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
static int config_is_valid(const egl_config_t *c)
{
    int i;
    for (i = 0; i < NUM_CONFIGS; ++i)
        if (c == &g_configs[i]) return 1;
    return 0;
}

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
    EGLint found = 0;
    int i;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    for (i = 0; i < NUM_CONFIGS; ++i) {
        if (!config_matches(&g_configs[i], attrib_list)) continue;
        if (configs && found < config_size)
            configs[found] = (EGLConfig)&g_configs[i];
        found++;
    }
    if (num_config) *num_config = found;
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
    if (!config_is_valid(c) || value == NULL) return set_err(EGL_BAD_CONFIG);
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
    case EGL_SAMPLES:        *value = c->samples; break;
    case EGL_SAMPLE_BUFFERS: *value = c->sample_buffers; break;
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
    if (!config_is_valid((const egl_config_t *)config)) { set_err(EGL_BAD_CONFIG); return EGL_NO_CONTEXT; }
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
    if (!config_is_valid((const egl_config_t *)config)) { set_err(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
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
    s->render_w = g.w;
    s->render_h = g.h;
    egl_native_render_size(&s->render_w, &s->render_h);
    s->swap_interval = 1;

    if (!gles_engine_init(s->render_w, s->render_h)) {
        free(s);
        set_err(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    gles_engine_resize(s->render_w, s->render_h);

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
    if (!config_is_valid((const egl_config_t *)config)) { set_err(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    parse_wh(attrib_list, 1, 1, &w, &h);
    if (w <= 0 || h <= 0) { set_err(EGL_BAD_ATTRIBUTE); return EGL_NO_SURFACE; }

    s = (egl_surface_t *)calloc(1, sizeof(*s));
    if (s == NULL) { set_err(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
    s->type = EGL_PBUFFER_BIT;
    s->xwin = NULL;
    s->w = w;
    s->h = h;
    s->render_w = w;
    s->render_h = h;
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
    case EGL_CONFIG_ID: *value = g_configs[0].id; break;
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
    gles_engine_viewport(0, 0, s->render_w, s->render_h);
    g_current_draw = s;
    g_current_ctx = (egl_context_t *)ctx;
    s->ctx = g_current_ctx;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    egl_surface_t *s = (egl_surface_t *)surface;
    unsigned long long swap_start = 0, flush_start = 0, repaint_start = 0;
    unsigned long long now;
    int bw = 0, bh = 0;
    if (dpy != (EGLDisplay)&g_display) return set_err(EGL_BAD_DISPLAY);
    if (s == NULL) return set_err(EGL_BAD_SURFACE);
    if (s->type == EGL_PBUFFER_BIT || s->xwin == NULL)
        return EGL_TRUE;   /* nothing to present */
    if (egl_profile_enabled()) {
        swap_start = egl_profile_us();
        gles_engine_backbuffer_size(&bw, &bh);
    }
    /* Drain any pending V3D GPU work into the adopted back buffer before the
     * blit (a no-op on non-PGL_G3D builds where the engine stub returns
     * immediately), so egl_on_repaint presents the fully rendered frame. */
    if (swap_start)
        flush_start = egl_profile_us();
    gles_engine_gpu_flush();
    if (flush_start)
        g_egl_flush_us += egl_profile_us() - flush_start;
    /* Present by requesting a repaint; xwin_repaint invokes egl_on_repaint,
     * which blits the engine back buffer into the window graph under the
     * painting lock.  frame_ready gates that blit so an async event-thread
     * retry never publishes a partial frame. */
    s->frame_ready = 1;
    if (swap_start)
        repaint_start = egl_profile_us();
    xwin_repaint(s->xwin);
    if (repaint_start)
        g_egl_repaint_us += egl_profile_us() - repaint_start;
    s->frame_ready = 0;
    if (swap_start) {
        now = egl_profile_us();
        g_egl_swap_us += now - swap_start;
        egl_profile_report(now, bw, bh, s->w, s->h,
                           gles_engine_backbuffer_shm());
    }
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

/* ------------------------------------------------------------------ *
 * eglGetProcAddress: lookup table for exported GLES2 entry points.
 * The GL functions live in libGLESv2.a (gles2_api.c / gles2_shader.c);
 * we declare them with unspecified-parameter prototypes (valid C) to
 * obtain their addresses without including <GLES2/gl2.h>.
 * ------------------------------------------------------------------ */
extern void glActiveTexture(); extern void glAttachShader();
extern void glBindAttribLocation(); extern void glBindBuffer();
extern void glBindFramebuffer(); extern void glBindRenderbuffer();
extern void glBindTexture(); extern void glBindVertexArray();
extern void glBlendColor(); extern void glBlendEquation();
extern void glBlendEquationSeparate(); extern void glBlendFunc();
extern void glBlendFuncSeparate(); extern void glBufferData();
extern void glBufferSubData(); extern void glCheckFramebufferStatus();
extern void glClear(); extern void glClearColor();
extern void glClearDepthf(); extern void glClearStencil();
extern void glColorMask(); extern void glCompileShader();
extern void glCompressedTexImage2D(); extern void glCompressedTexSubImage2D();
extern void glCopyTexImage2D(); extern void glCopyTexSubImage2D();
extern void glCreateProgram(); extern void glCreateShader();
extern void glCullFace(); extern void glDeleteBuffers();
extern void glDeleteFramebuffers(); extern void glDeleteProgram();
extern void glDeleteRenderbuffers(); extern void glDeleteShader();
extern void glDeleteTextures(); extern void glDeleteVertexArrays();
extern void glDepthFunc(); extern void glDepthMask();
extern void glDepthRangef(); extern void glDetachShader();
extern void glDisable(); extern void glDisableVertexAttribArray();
extern void glDrawArrays(); extern void glDrawElements();
extern void glEnable(); extern void glEnableVertexAttribArray();
extern void glFinish(); extern void glFlush();
extern void glFramebufferRenderbuffer(); extern void glFramebufferTexture2D();
extern void glFrontFace(); extern void glGenBuffers();
extern void glGenFramebuffers(); extern void glGenRenderbuffers();
extern void glGenTextures(); extern void glGenVertexArrays();
extern void glGenerateMipmap(); extern void glGetBooleanv();
extern void glGetBufferParameteriv(); extern void glGetError();
extern void glGetFloatv(); extern void glGetFramebufferAttachmentParameteriv();
extern void glGetIntegerv(); extern void glGetProgramInfoLog();
extern void glGetProgramiv(); extern void glGetRenderbufferParameteriv();
extern void glGetShaderInfoLog(); extern void glGetShaderPrecisionFormat();
extern void glGetShaderSource(); extern void glGetShaderiv();
extern void glGetString(); extern void glGetTexParameterfv();
extern void glGetTexParameteriv(); extern void glGetUniformLocation();
extern void glGetVertexAttribPointerv(); extern void glGetVertexAttribfv();
extern void glGetVertexAttribiv(); extern void glHint();
extern void glIsBuffer(); extern void glIsEnabled();
extern void glIsFramebuffer(); extern void glIsProgram();
extern void glIsRenderbuffer(); extern void glIsShader();
extern void glIsTexture(); extern void glIsVertexArray();
extern void glLineWidth(); extern void glLinkProgram();
extern void glPixelStorei(); extern void glPolygonOffset();
extern void glReadPixels(); extern void glRenderbufferStorage();
extern void glSampleCoverage(); extern void glScissor();
extern void glShaderBinary(); extern void glShaderSource();
extern void glStencilFunc(); extern void glStencilFuncSeparate();
extern void glStencilMask(); extern void glStencilMaskSeparate();
extern void glStencilOp(); extern void glStencilOpSeparate();
extern void glTexImage2D(); extern void glTexParameterf();
extern void glTexParameterfv(); extern void glTexParameteri();
extern void glTexParameteriv(); extern void glTexSubImage2D();
extern void glUniform1f(); extern void glUniform1fv();
extern void glUniform1i(); extern void glUniform1iv();
extern void glUniform2f(); extern void glUniform2fv();
extern void glUniform2i(); extern void glUniform2iv();
extern void glUniform3f(); extern void glUniform3fv();
extern void glUniform3i(); extern void glUniform3iv();
extern void glUniform4f(); extern void glUniform4fv();
extern void glUniform4i(); extern void glUniform4iv();
extern void glUniformMatrix2fv(); extern void glUniformMatrix3fv();
extern void glUniformMatrix4fv(); extern void glUseProgram();
extern void glValidateProgram(); extern void glVertexAttrib1f();
extern void glVertexAttrib1fv(); extern void glVertexAttrib2f();
extern void glVertexAttrib2fv(); extern void glVertexAttrib3f();
extern void glVertexAttrib3fv(); extern void glVertexAttrib4f();
extern void glVertexAttrib4fv(); extern void glVertexAttribPointer();
extern void glViewport();

typedef struct { const char *name; __eglMustCastToProperFunctionPointerType fn; } egl_proc_t;
static const egl_proc_t g_proc_table[] = {
    { "glActiveTexture", (__eglMustCastToProperFunctionPointerType)glActiveTexture },
    { "glAttachShader", (__eglMustCastToProperFunctionPointerType)glAttachShader },
    { "glBindAttribLocation", (__eglMustCastToProperFunctionPointerType)glBindAttribLocation },
    { "glBindBuffer", (__eglMustCastToProperFunctionPointerType)glBindBuffer },
    { "glBindFramebuffer", (__eglMustCastToProperFunctionPointerType)glBindFramebuffer },
    { "glBindRenderbuffer", (__eglMustCastToProperFunctionPointerType)glBindRenderbuffer },
    { "glBindTexture", (__eglMustCastToProperFunctionPointerType)glBindTexture },
    { "glBindVertexArray", (__eglMustCastToProperFunctionPointerType)glBindVertexArray },
    { "glBlendColor", (__eglMustCastToProperFunctionPointerType)glBlendColor },
    { "glBlendEquation", (__eglMustCastToProperFunctionPointerType)glBlendEquation },
    { "glBlendEquationSeparate", (__eglMustCastToProperFunctionPointerType)glBlendEquationSeparate },
    { "glBlendFunc", (__eglMustCastToProperFunctionPointerType)glBlendFunc },
    { "glBlendFuncSeparate", (__eglMustCastToProperFunctionPointerType)glBlendFuncSeparate },
    { "glBufferData", (__eglMustCastToProperFunctionPointerType)glBufferData },
    { "glBufferSubData", (__eglMustCastToProperFunctionPointerType)glBufferSubData },
    { "glCheckFramebufferStatus", (__eglMustCastToProperFunctionPointerType)glCheckFramebufferStatus },
    { "glClear", (__eglMustCastToProperFunctionPointerType)glClear },
    { "glClearColor", (__eglMustCastToProperFunctionPointerType)glClearColor },
    { "glClearDepthf", (__eglMustCastToProperFunctionPointerType)glClearDepthf },
    { "glClearStencil", (__eglMustCastToProperFunctionPointerType)glClearStencil },
    { "glColorMask", (__eglMustCastToProperFunctionPointerType)glColorMask },
    { "glCompileShader", (__eglMustCastToProperFunctionPointerType)glCompileShader },
    { "glCompressedTexImage2D", (__eglMustCastToProperFunctionPointerType)glCompressedTexImage2D },
    { "glCompressedTexSubImage2D", (__eglMustCastToProperFunctionPointerType)glCompressedTexSubImage2D },
    { "glCopyTexImage2D", (__eglMustCastToProperFunctionPointerType)glCopyTexImage2D },
    { "glCopyTexSubImage2D", (__eglMustCastToProperFunctionPointerType)glCopyTexSubImage2D },
    { "glCreateProgram", (__eglMustCastToProperFunctionPointerType)glCreateProgram },
    { "glCreateShader", (__eglMustCastToProperFunctionPointerType)glCreateShader },
    { "glCullFace", (__eglMustCastToProperFunctionPointerType)glCullFace },
    { "glDeleteBuffers", (__eglMustCastToProperFunctionPointerType)glDeleteBuffers },
    { "glDeleteFramebuffers", (__eglMustCastToProperFunctionPointerType)glDeleteFramebuffers },
    { "glDeleteProgram", (__eglMustCastToProperFunctionPointerType)glDeleteProgram },
    { "glDeleteRenderbuffers", (__eglMustCastToProperFunctionPointerType)glDeleteRenderbuffers },
    { "glDeleteShader", (__eglMustCastToProperFunctionPointerType)glDeleteShader },
    { "glDeleteTextures", (__eglMustCastToProperFunctionPointerType)glDeleteTextures },
    { "glDeleteVertexArrays", (__eglMustCastToProperFunctionPointerType)glDeleteVertexArrays },
    { "glDepthFunc", (__eglMustCastToProperFunctionPointerType)glDepthFunc },
    { "glDepthMask", (__eglMustCastToProperFunctionPointerType)glDepthMask },
    { "glDepthRangef", (__eglMustCastToProperFunctionPointerType)glDepthRangef },
    { "glDetachShader", (__eglMustCastToProperFunctionPointerType)glDetachShader },
    { "glDisable", (__eglMustCastToProperFunctionPointerType)glDisable },
    { "glDisableVertexAttribArray", (__eglMustCastToProperFunctionPointerType)glDisableVertexAttribArray },
    { "glDrawArrays", (__eglMustCastToProperFunctionPointerType)glDrawArrays },
    { "glDrawElements", (__eglMustCastToProperFunctionPointerType)glDrawElements },
    { "glEnable", (__eglMustCastToProperFunctionPointerType)glEnable },
    { "glEnableVertexAttribArray", (__eglMustCastToProperFunctionPointerType)glEnableVertexAttribArray },
    { "glFinish", (__eglMustCastToProperFunctionPointerType)glFinish },
    { "glFlush", (__eglMustCastToProperFunctionPointerType)glFlush },
    { "glFramebufferRenderbuffer", (__eglMustCastToProperFunctionPointerType)glFramebufferRenderbuffer },
    { "glFramebufferTexture2D", (__eglMustCastToProperFunctionPointerType)glFramebufferTexture2D },
    { "glFrontFace", (__eglMustCastToProperFunctionPointerType)glFrontFace },
    { "glGenBuffers", (__eglMustCastToProperFunctionPointerType)glGenBuffers },
    { "glGenFramebuffers", (__eglMustCastToProperFunctionPointerType)glGenFramebuffers },
    { "glGenRenderbuffers", (__eglMustCastToProperFunctionPointerType)glGenRenderbuffers },
    { "glGenTextures", (__eglMustCastToProperFunctionPointerType)glGenTextures },
    { "glGenVertexArrays", (__eglMustCastToProperFunctionPointerType)glGenVertexArrays },
    { "glGenerateMipmap", (__eglMustCastToProperFunctionPointerType)glGenerateMipmap },
    { "glGetBooleanv", (__eglMustCastToProperFunctionPointerType)glGetBooleanv },
    { "glGetBufferParameteriv", (__eglMustCastToProperFunctionPointerType)glGetBufferParameteriv },
    { "glGetError", (__eglMustCastToProperFunctionPointerType)glGetError },
    { "glGetFloatv", (__eglMustCastToProperFunctionPointerType)glGetFloatv },
    { "glGetFramebufferAttachmentParameteriv", (__eglMustCastToProperFunctionPointerType)glGetFramebufferAttachmentParameteriv },
    { "glGetIntegerv", (__eglMustCastToProperFunctionPointerType)glGetIntegerv },
    { "glGetProgramInfoLog", (__eglMustCastToProperFunctionPointerType)glGetProgramInfoLog },
    { "glGetProgramiv", (__eglMustCastToProperFunctionPointerType)glGetProgramiv },
    { "glGetRenderbufferParameteriv", (__eglMustCastToProperFunctionPointerType)glGetRenderbufferParameteriv },
    { "glGetShaderInfoLog", (__eglMustCastToProperFunctionPointerType)glGetShaderInfoLog },
    { "glGetShaderPrecisionFormat", (__eglMustCastToProperFunctionPointerType)glGetShaderPrecisionFormat },
    { "glGetShaderSource", (__eglMustCastToProperFunctionPointerType)glGetShaderSource },
    { "glGetShaderiv", (__eglMustCastToProperFunctionPointerType)glGetShaderiv },
    { "glGetString", (__eglMustCastToProperFunctionPointerType)glGetString },
    { "glGetTexParameterfv", (__eglMustCastToProperFunctionPointerType)glGetTexParameterfv },
    { "glGetTexParameteriv", (__eglMustCastToProperFunctionPointerType)glGetTexParameteriv },
    { "glGetUniformLocation", (__eglMustCastToProperFunctionPointerType)glGetUniformLocation },
    { "glGetVertexAttribPointerv", (__eglMustCastToProperFunctionPointerType)glGetVertexAttribPointerv },
    { "glGetVertexAttribfv", (__eglMustCastToProperFunctionPointerType)glGetVertexAttribfv },
    { "glGetVertexAttribiv", (__eglMustCastToProperFunctionPointerType)glGetVertexAttribiv },
    { "glHint", (__eglMustCastToProperFunctionPointerType)glHint },
    { "glIsBuffer", (__eglMustCastToProperFunctionPointerType)glIsBuffer },
    { "glIsEnabled", (__eglMustCastToProperFunctionPointerType)glIsEnabled },
    { "glIsFramebuffer", (__eglMustCastToProperFunctionPointerType)glIsFramebuffer },
    { "glIsProgram", (__eglMustCastToProperFunctionPointerType)glIsProgram },
    { "glIsRenderbuffer", (__eglMustCastToProperFunctionPointerType)glIsRenderbuffer },
    { "glIsShader", (__eglMustCastToProperFunctionPointerType)glIsShader },
    { "glIsTexture", (__eglMustCastToProperFunctionPointerType)glIsTexture },
    { "glIsVertexArray", (__eglMustCastToProperFunctionPointerType)glIsVertexArray },
    { "glLineWidth", (__eglMustCastToProperFunctionPointerType)glLineWidth },
    { "glLinkProgram", (__eglMustCastToProperFunctionPointerType)glLinkProgram },
    { "glPixelStorei", (__eglMustCastToProperFunctionPointerType)glPixelStorei },
    { "glPolygonOffset", (__eglMustCastToProperFunctionPointerType)glPolygonOffset },
    { "glReadPixels", (__eglMustCastToProperFunctionPointerType)glReadPixels },
    { "glRenderbufferStorage", (__eglMustCastToProperFunctionPointerType)glRenderbufferStorage },
    { "glSampleCoverage", (__eglMustCastToProperFunctionPointerType)glSampleCoverage },
    { "glScissor", (__eglMustCastToProperFunctionPointerType)glScissor },
    { "glShaderBinary", (__eglMustCastToProperFunctionPointerType)glShaderBinary },
    { "glShaderSource", (__eglMustCastToProperFunctionPointerType)glShaderSource },
    { "glStencilFunc", (__eglMustCastToProperFunctionPointerType)glStencilFunc },
    { "glStencilFuncSeparate", (__eglMustCastToProperFunctionPointerType)glStencilFuncSeparate },
    { "glStencilMask", (__eglMustCastToProperFunctionPointerType)glStencilMask },
    { "glStencilMaskSeparate", (__eglMustCastToProperFunctionPointerType)glStencilMaskSeparate },
    { "glStencilOp", (__eglMustCastToProperFunctionPointerType)glStencilOp },
    { "glStencilOpSeparate", (__eglMustCastToProperFunctionPointerType)glStencilOpSeparate },
    { "glTexImage2D", (__eglMustCastToProperFunctionPointerType)glTexImage2D },
    { "glTexParameterf", (__eglMustCastToProperFunctionPointerType)glTexParameterf },
    { "glTexParameterfv", (__eglMustCastToProperFunctionPointerType)glTexParameterfv },
    { "glTexParameteri", (__eglMustCastToProperFunctionPointerType)glTexParameteri },
    { "glTexParameteriv", (__eglMustCastToProperFunctionPointerType)glTexParameteriv },
    { "glTexSubImage2D", (__eglMustCastToProperFunctionPointerType)glTexSubImage2D },
    { "glUniform1f", (__eglMustCastToProperFunctionPointerType)glUniform1f },
    { "glUniform1fv", (__eglMustCastToProperFunctionPointerType)glUniform1fv },
    { "glUniform1i", (__eglMustCastToProperFunctionPointerType)glUniform1i },
    { "glUniform1iv", (__eglMustCastToProperFunctionPointerType)glUniform1iv },
    { "glUniform2f", (__eglMustCastToProperFunctionPointerType)glUniform2f },
    { "glUniform2fv", (__eglMustCastToProperFunctionPointerType)glUniform2fv },
    { "glUniform2i", (__eglMustCastToProperFunctionPointerType)glUniform2i },
    { "glUniform2iv", (__eglMustCastToProperFunctionPointerType)glUniform2iv },
    { "glUniform3f", (__eglMustCastToProperFunctionPointerType)glUniform3f },
    { "glUniform3fv", (__eglMustCastToProperFunctionPointerType)glUniform3fv },
    { "glUniform3i", (__eglMustCastToProperFunctionPointerType)glUniform3i },
    { "glUniform3iv", (__eglMustCastToProperFunctionPointerType)glUniform3iv },
    { "glUniform4f", (__eglMustCastToProperFunctionPointerType)glUniform4f },
    { "glUniform4fv", (__eglMustCastToProperFunctionPointerType)glUniform4fv },
    { "glUniform4i", (__eglMustCastToProperFunctionPointerType)glUniform4i },
    { "glUniform4iv", (__eglMustCastToProperFunctionPointerType)glUniform4iv },
    { "glUniformMatrix2fv", (__eglMustCastToProperFunctionPointerType)glUniformMatrix2fv },
    { "glUniformMatrix3fv", (__eglMustCastToProperFunctionPointerType)glUniformMatrix3fv },
    { "glUniformMatrix4fv", (__eglMustCastToProperFunctionPointerType)glUniformMatrix4fv },
    { "glUseProgram", (__eglMustCastToProperFunctionPointerType)glUseProgram },
    { "glValidateProgram", (__eglMustCastToProperFunctionPointerType)glValidateProgram },
    { "glVertexAttrib1f", (__eglMustCastToProperFunctionPointerType)glVertexAttrib1f },
    { "glVertexAttrib1fv", (__eglMustCastToProperFunctionPointerType)glVertexAttrib1fv },
    { "glVertexAttrib2f", (__eglMustCastToProperFunctionPointerType)glVertexAttrib2f },
    { "glVertexAttrib2fv", (__eglMustCastToProperFunctionPointerType)glVertexAttrib2fv },
    { "glVertexAttrib3f", (__eglMustCastToProperFunctionPointerType)glVertexAttrib3f },
    { "glVertexAttrib3fv", (__eglMustCastToProperFunctionPointerType)glVertexAttrib3fv },
    { "glVertexAttrib4f", (__eglMustCastToProperFunctionPointerType)glVertexAttrib4f },
    { "glVertexAttrib4fv", (__eglMustCastToProperFunctionPointerType)glVertexAttrib4fv },
    { "glVertexAttribPointer", (__eglMustCastToProperFunctionPointerType)glVertexAttribPointer },
    { "glViewport", (__eglMustCastToProperFunctionPointerType)glViewport },
    { NULL, NULL }
};

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    const egl_proc_t *p;
    if (procname == NULL) return NULL;
    for (p = g_proc_table; p->name != NULL; ++p) {
        if (strcmp(procname, p->name) == 0)
            return p->fn;
    }
    return NULL;
}
