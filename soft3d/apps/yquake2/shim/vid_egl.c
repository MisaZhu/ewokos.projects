/*
 * vid_egl.c - EGL-over-xwin video backend for the standalone yquake2 gl3
 * refresher port (projects/soft3d/apps/yquake2).
 *
 * Upstream yquake2 splits the video layer in two:
 *   - the *client* side (vid.c / sdl.c) implements the refimport_t entry
 *     points GLimp_InitGraphics()/GLimp_GetDesktopMode()/Vid_GetModeInfo();
 *     it creates the OS window and then calls back into the refresher via
 *     re.PrepareForWindow()/re.InitContext()/re.GetDrawableSize().
 *   - the *refresher* side (gl3_sdl.c) implements the eight GL3_* context
 *     helpers plus the IsHighDPIaware global.
 *
 * This "refresher only" port merges both sides onto EwokOS: the window is an
 * xwin_t (created by main.c) and the GL context is EGL over that native window,
 * exactly like apps/imgui_demo.  gl3_sdl.c was dropped from the vendored tree;
 * the GL3_* functions below replace it, and the three vid refimports live here
 * too because they need the EGL/window state.
 *
 * The eight GL3_* symbols are consumed by gl3_main.c through re.* (see
 * GetRefAPI); GLimp_InitGraphics() drives them during re.Init().
 */

#include <stdio.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <x/xwin.h>

#include "ref.h"   /* refexport_t, qboolean, EXPORT/IMPORT */

/* Owned by main.c: the application window, its size, and the export table we
 * drive the context helpers through. */
extern xwin_t      *g_xwin;
extern int          g_win_w;
extern int          g_win_h;
extern refexport_t  re;

/* gl3_main.c reads this to decide whether to re-query the drawable size after
 * the context is up.  EwokOS xwin is 1:1 (no hi-dpi scaling), so false. */
qboolean IsHighDPIaware = false;

/* The GLES2-subset front end (libs/gles) declares but does not implement
 * glInvalidateFramebuffer.  gl3_draw.c calls it (under YQ2_GL3_GLES) purely as a
 * bandwidth hint right before GL3_SwapWindow(); discarding the default
 * framebuffer's colour/depth/stencil contents is meaningless for the CPU
 * interpreter and would drop the rendered image, so this is a deliberate
 * no-op: contents are preserved and the swap proceeds normally. */
void GL_APIENTRY
glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments)
{
	(void)target;
	(void)numAttachments;
	(void)attachments;
}

/* glGetStringi() (indexed extension query, GLES3) is likewise declared but not
 * implemented by the front end.  gl3 only reaches it from GL3_Strings(), whose
 * loop is bounded by GL_NUM_EXTENSIONS - which this front end reports as 0 (see
 * the defensive init in gl3_main.c) - so it is never actually called; the stub
 * exists only to satisfy the link. */
const GLubyte *GL_APIENTRY
glGetStringi(GLenum name, GLuint index)
{
	(void)name;
	(void)index;
	return NULL;
}

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static int draw_w = 0;
static int draw_h = 0;
static int vsync_active = 0;

/* ------------------------------------------------------------------ *
 * GL3_* : refresher-side context helpers (replace gl3_sdl.c)
 * ------------------------------------------------------------------ */

int
GL3_InitContext(void *win)
{
	EGLint major = 0, minor = 0, ncfg = 0;
	EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLConfig cfg = NULL;
	xwin_t *xwin = (xwin_t *)win;

	if (xwin == NULL)
	{
		return 0;
	}

	egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (egl_dpy == EGL_NO_DISPLAY)
	{
		return 0;
	}
	if (!eglInitialize(egl_dpy, &major, &minor))
	{
		return 0;
	}
	if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1)
	{
		return 0;
	}

	egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (egl_ctx == EGL_NO_CONTEXT)
	{
		return 0;
	}

	egl_surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)xwin, NULL);
	if (egl_surf == EGL_NO_SURFACE)
	{
		return 0;
	}

	if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx))
	{
		return 0;
	}

	/* Real back-buffer size (may differ from the requested mode on hi-dpi,
	 * which xwin never is, but query it anyway so vid.* is authoritative). */
	{
		EGLint w = 0, h = 0;
		eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
		eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
		draw_w = (w > 0) ? w : g_win_w;
		draw_h = (h > 0) ? h : g_win_h;
	}

	return 1;
}

void
GL3_GetDrawableSize(int *width, int *height)
{
	if (width)
	{
		*width = draw_w;
	}
	if (height)
	{
		*height = draw_h;
	}
}

int
GL3_PrepareForWindow(void)
{
	/* Upstream returns SDL window-creation flags.  xwin needs none. */
	return 0;
}

qboolean
GL3_IsVsyncActive(void)
{
	return vsync_active ? true : false;
}

void
GL3_SwapWindow(void)
{
	if (egl_dpy != EGL_NO_DISPLAY && egl_surf != EGL_NO_SURFACE)
	{
		eglSwapBuffers(egl_dpy, egl_surf);
	}
}

void
GL3_SetVsync(void)
{
	/* The front end is a software/GPU-bridge renderer with no display
	 * refresh to lock to; keep the swap interval at 0 so eglSwapBuffers
	 * never blocks the xwin event loop. */
	if (egl_dpy != EGL_NO_DISPLAY)
	{
		eglSwapInterval(egl_dpy, 0);
	}
	vsync_active = 0;
}

void
GL3_ShutdownContext(void)
{
	if (egl_dpy == EGL_NO_DISPLAY)
	{
		return;
	}

	eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (egl_surf != EGL_NO_SURFACE)
	{
		eglDestroySurface(egl_dpy, egl_surf);
		egl_surf = EGL_NO_SURFACE;
	}
	if (egl_ctx != EGL_NO_CONTEXT)
	{
		eglDestroyContext(egl_dpy, egl_ctx);
		egl_ctx = EGL_NO_CONTEXT;
	}
	eglTerminate(egl_dpy);
	egl_dpy = EGL_NO_DISPLAY;
}

int
GL3_GetSDLVersion(void)
{
	/* re.framework_version is only meaningful when the client mixes SDL
	 * major versions; report a plain 2.0.0 so the value is non-zero. */
	return 2000;
}

/* ------------------------------------------------------------------ *
 * vid refimports : client-side glue that drives the GL3_* helpers
 * ------------------------------------------------------------------ */

qboolean
GLimp_InitGraphics(int fullscreen, int *pwidth, int *pheight)
{
	(void)fullscreen;

	/* Mirrors upstream sdl.c: ask the refresher for window flags (none),
	 * create the GL context on the already-open xwin, then read back the
	 * true drawable size. */
	re.PrepareForWindow();

	if (!re.InitContext((void *)g_xwin))
	{
		return false;
	}

	re.GetDrawableSize(pwidth, pheight);

	return true;
}

qboolean
GLimp_GetDesktopMode(int *pwidth, int *pheight)
{
	if (pwidth)
	{
		*pwidth = g_win_w;
	}
	if (pheight)
	{
		*pheight = g_win_h;
	}
	return true;
}

qboolean
Vid_GetModeInfo(int *width, int *height, int mode)
{
	(void)mode;
	/* The window is created at a fixed size by main.c; every "mode" maps to
	 * it so GL3_SetMode()/SetMode_impl() always succeed. */
	if (width)
	{
		*width = g_win_w;
	}
	if (height)
	{
		*height = g_win_h;
	}
	return true;
}
