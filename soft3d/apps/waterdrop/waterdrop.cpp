/*
 * waterdrop.cpp -- GPU-accelerated water-surface ripple demo (EGL + GLES2).
 *
 * A 45-degree perspective water plane fills the lower half of the frame under a
 * black night sky; the moon's image shimmers on the ripples.  Raindrops fall
 * from the top, land at pseudo-random positions and each launches an expanding
 * circular wave; the surface height is the linear superposition of every live
 * wave, so overlapping rings interfere exactly as real linear water waves do.
 *
 * Structure mirrors libs/demos/shadertoy.cpp: a single 6-vertex NDC quad drawn
 * per frame with a procedural fragment shader (waterdrop_glsl.h).  On raspi5
 * (PGL_G3D) the engine transparently compiles that shader to a VideoCore VII
 * compute kernel and dispatches it on the GPU through the fullscreen-quad route
 * (a passthrough VS with no uMVP + an FS reading gl_FragCoord is recognised as
 * a fullscreen effect).  glGpuActiveEWOK() reports which path is live for the
 * on-screen HUD.  This app deliberately does NOT call glForceCpuEWOK() -- it
 * wants the GPU target adopted and the water scene kept on the GPU.
 *
 * The HUD is a Dear ImGui window (same 16px TTF font, titled auto-resize panel,
 * FPS + colour-coded Renderer as glmatrix).  ImGui rasterizes through a
 * GPU-eligible texcol program, which cannot composite with the water scene's
 * fullscreen-compute dispatch in one frame, so this app calls the narrower
 * glForceCpuRasterEWOK(): that lever declines ONLY the mesh/texcol raster
 * dispatches to the CPU interpreter -- ImGui then composites into the same
 * NOCACHE fb the GPU scene wrote (the path the glcommon CPU hud already used,
 * proven to overlay correctly on virt and raspi5) -- while gpu_try_fullscreen
 * keeps the water scene on the V3D GPU and gpu_bringup/g_gpu_ready are
 * untouched.  Unlike glForceCpuEWOK (which pins the whole context to the CPU and
 * would drop the ~35s/frame water path), this keeps the scene fast.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>       /* glGpuActiveEWOK, glForceCpuRasterEWOK */

#include "imgui/imgui.h"                      /* Dear ImGui HUD (as glmatrix) */
#include "imgui/backends/imgui_impl_gles2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#include "waterdrop_glsl.h"

#ifdef PGL_G3D
/* libv3dcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Belongs to the mesa compiler merged into libGLESv2. */
extern "C" void v3d_compile_set_cs_local_size(unsigned x, unsigned y);
#endif

/* Window dimensions (can be resized). */
static int win_width  = 800;
static int win_height = 600;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint quad_vbo = 0;
static GLuint prog = 0;
static GLint  loc_time = -1;
static GLint  loc_res = -1;

static uint32_t start_time = 0;
static float    iGlobalTime = 0.0f;

/* FPS counter. */
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = frame_count;
        frame_count = 0;
    }
    frame_count++;
}

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("waterdrop: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_prog(const char* vs_src, const char* fs_src)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint p = glCreateProgram();
    GLint ok = 0;
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPosition");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        printf("waterdrop: program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

static void init_quad(void)
{
    /* Fullscreen NDC quad as two triangles (GL_TRIANGLES, 6 verts), matching
     * the fixed corners the engine's fullscreen GPU route dispatches. */
    static const float quad[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };
    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
}

static void init_shaders(void)
{
    prog = link_prog(waterdrop_vs_glsl, waterdrop_fs_glsl);
    loc_time = glGetUniformLocation(prog, "iGlobalTime");
    loc_res  = glGetUniformLocation(prog, "iResolution");
}

/* ------------------------------------------------------------------
 * Dear ImGui HUD (mirrors glmatrix): a display-only stats window drawn on
 * top of the water scene.  ImGui is C++ and renders through the GLES2
 * backend; the app calls glForceCpuRasterEWOK() once in main so ImGui's
 * texcol raster falls back to the CPU interpreter and composites into the
 * same fb the GPU fullscreen scene wrote.  The HUD is read-only, so no
 * xwin input is forwarded and the window is marked NoInputs.
 * ------------------------------------------------------------------ */
static void init_imgui(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                                 /* no imgui.ini    */
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    ImGui::StyleColorsDark();
    ImGui_ImplGLES2_Init();
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

/* Build one ImGui frame: lay out the read-only stats window.  `gpu` is the
 * route the water program used this frame, queried while it was bound. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Waterdrop", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::End();
}

static int init_egl(xwin_t* xwin)
{
    EGLint major, minor, ncfg = 0;
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg = NULL;

    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) return 0;
    if (!eglInitialize(egl_dpy, &major, &minor)) return 0;
    if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) return 0;
    egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) return 0;
    egl_surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)xwin, NULL);
    if (egl_surf == EGL_NO_SURFACE) return 0;
    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) return 0;
    return 1;
}

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height)) {
        win_width = w; win_height = h;
    }
    if (win_width <= 0) win_width = 1;
    if (win_height <= 0) win_height = 1;

    uint32_t now = kernel_tic_ms(0);
    iGlobalTime = (now - start_time) / 1000.0f;

    glViewport(0, 0, win_width, win_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Re-bind the scene program every frame: the engine's GPU bridge and the
     * HUD overlay each touch GL_CURRENT_PROGRAM, so an explicit bind keeps the
     * quad on the water shader. */
    glUseProgram(prog);
    if (loc_time >= 0) glUniform1f(loc_time, iGlobalTime);
    if (loc_res >= 0)
        glUniform3f(loc_res, (float)win_width, (float)win_height, 1.0f);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    /* Overlay the FPS / Renderer readout as a Dear ImGui window, matching
     * glmatrix ("FPS: N", then a colour-coded "Renderer: GPU/CPU").  Query the
     * route while the water program is still bound (the engine reports the
     * currently-bound program and ImGui binds its own next).  glForceCpuRasterEWOK()
     * (called once in main) makes ImGui's texcol raster fall back to the CPU
     * interpreter so it composites into the same fb the GPU fullscreen scene
     * wrote -- correctly overlaying on both virt and raspi5. */
    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    /* Screensaver-style exit: any key press or mouse button press quits, so the
     * demo closes on the first sign of user activity (matching glmatrix).  The
     * HUD is read-only, so there is no interaction to preserve and no event
     * needs forwarding.  Only the initial MOUSE_STATE_DOWN (not a DRAG
     * continuation) triggers the exit, so a press-and-move does not race
     * xwin_close against further motion. */
    if (ev->type == XEVT_IM) {
        if (ev->state == XIM_STATE_PRESS)
            xwin_close(xwin);
    }
    else if (ev->type == XEVT_MOUSE) {
        if (ev->state == MOUSE_STATE_DOWN)
            xwin_close(xwin);
    }
}

static void loop(void* p)
{
    (void)p;
    render_frame();
    usleep(1000);
}

int main(int argc, char** argv)
{
    /* argv: optional "WxH" compute workgroup shape (GPU build, A/B tuning). */
    for (int ai = 1; ai < argc && argv[ai]; ai++) {
        unsigned lx = 0, ly = 0;
        if (sscanf(argv[ai], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0) {
#ifdef PGL_G3D
            v3d_compile_set_cs_local_size(lx, ly);
#endif
        }
    }

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height,
                             "Waterdrop", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* Fullscreen BEFORE the EGL surface is created (same ordering constraint as
     * glmatrix): xwin_fullscreen() is synchronous, so on return the window graph
     * already carries the full display size and eglCreateWindowSurface hands
     * gles_engine_init those dimensions, letting pgl_g3d_init allocate the
     * fb/z contig pair once at full size.  Fullscreening AFTER EGL would instead
     * route through egl_on_resize -> pglResizeFramebuffer -> pgl_g3d_resize's
     * grow branch, which frees the small runs then asks the contig pool for a
     * large pair in one shot; on fragmentation that fails, the GPU latches off
     * (_pgl_gpu_off=1) and the session falls back to the CPU interpreter with no
     * recovery.  win_width/win_height stay as the fallback should a WM reject
     * the MAX state. */
    xwin_fullscreen(xwin);

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_shaders();

    /* Force ONLY the mesh/texcol raster dispatches (Dear ImGui's HUD) onto the
     * CPU interpreter so they composite into the same fb the GPU fullscreen
     * water scene wrote; gpu_try_fullscreen keeps the scene on the V3D GPU.
     * Must be set before the first frame is presented. */
    glForceCpuRasterEWOK();
    init_imgui();

    start_time = kernel_tic_ms(0);

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
    if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);
    return 0;
}
