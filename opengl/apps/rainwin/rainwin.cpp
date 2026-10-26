/*
 * rainwin.cpp -- GPU-accelerated rain-on-window demo (EGL + GLES2).
 *
 * A faithful port of the reference web effect "Realistic rain shader"
 * (shadertoy.com/view/7XtSD7 by anAccount): a physically-based night-city view
 * through a rain-streaked window -- spherical-cap water drops (static beads plus
 * stick-slip sliders that wipe the condensation, leave a thin film trail and
 * pinch off satellite droplets), each drop a wavelength-dispersive refracting
 * lens with unpolarised Fresnel reflectance, total internal reflection and
 * Beer-Lambert absorption, over a defocused bokeh cityscape of sky glow, lit
 * skylines, blinking beacons, traffic streaks, motion-blurred rain and an
 * occasional lightning flash.  The scene, optics and every tuning constant live
 * in rainwin_glsl.h; the effect is fully procedural (no texture inputs).
 *
 * Structure mirrors apps/city / apps/waterdrop / apps/ocean: a single 6-vertex
 * NDC quad drawn per frame with a procedural fragment shader.  On raspi5 /
 * raspix (PGL_G3D) the engine transparently compiles that shader to a VideoCore
 * VII compute kernel and dispatches it on the GPU through the fullscreen-quad
 * route (a passthrough VS with no uMVP + an FS reading gl_FragCoord is
 * recognised as a fullscreen effect).  glGpuActiveEWOK() reports which path is
 * live for the on-screen HUD.  This app deliberately does NOT call
 * glForceCpuEWOK() -- it wants the GPU target adopted and the scene kept on the
 * GPU.
 *
 * The HUD is a Dear ImGui window (same 16px TTF font, titled auto-resize panel,
 * FPS + colour-coded Renderer as city / waterdrop).  ImGui rasterizes through a
 * GPU-eligible texcol program, which cannot composite with the scene's
 * fullscreen-compute dispatch in one frame, so this app calls the narrower
 * glForceCpuRasterEWOK(): that lever declines ONLY the mesh/texcol raster
 * dispatches to the CPU interpreter -- ImGui then composites into the same
 * NOCACHE fb the GPU scene wrote -- while gpu_try_fullscreen keeps the scene on
 * the V3D GPU and gpu_bringup/g_gpu_ready are untouched.
 *
 * The reference reads iMouse for two interactive controls (X = rain intensity,
 * Y = condensation on the glass).  This app binds iMouse neutral (0,0,0,0) so
 * the shader takes its un-clicked defaults (rain 0.65, fog 0.45) -- exactly the
 * look the web page shows before the mouse is used -- and a click exits
 * (screensaver-style).
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>       /* glGpuActiveEWOK, glForceCpuRasterEWOK */

#include "imgui/imgui.h"                      /* Dear ImGui HUD */
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

#include "rainwin_glsl.h"

#ifdef PGL_G3D
/* pglcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Belongs to the mesa compiler merged into libGLESv2. */
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
/* Engine knob: render the fullscreen faithful compute at 1/N resolution and
 * upscale to the window (same lever city/ocean/waterdrop ship).  The rain optic
 * solve is very heavy per pixel (three bokeh layers, three skylines, traffic,
 * three rain layers and a multi-scale water field, each drop refracted at three
 * wavelengths), so the default denominator is 2; argv "scaleN" (N=1..9)
 * overrides, 1 = native full-res.  The shader math is unchanged (every size it
 * uses is expressed relative to iResolution, and the engine patches both
 * iResolution and pglc_cs_stride to the reduced geometry).  No effect on the CPU
 * interpreter path (virt / GPU-decline), which is not a compute draw. */
extern "C" void pgl_g3d_set_render_scale(int scale);
#endif

/* Window dimensions (can be resized). */
static int win_width  = 800;
static int win_height = 600;
#ifdef PGL_G3D
static int g_scale = 1;   /* faithful-compute render-scale denominator; argv "scaleN"
                             (N=1..9) overrides, 1 = native full-res */
#endif

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint quad_vbo = 0;
static GLuint prog = 0;
static GLint  loc_time = -1;
static GLint  loc_res = -1;
static GLint  loc_mouse = -1;

static uint32_t start_time = 0;
static float    iGlobalTime = 0.0f;

/* FPS counter. */
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;
static int g_fpslog = 0;   /* argv "fpslog": also print the 1s FPS roll to stdout
                              (for headless A/B tuning over ssh; silent by default) */
static int g_dump = 0;     /* argv "dumpN": glReadPixels frame N to /tmp/rainwin.ppm
                              and quit (headless picture check over ssh) */
static float g_freeze = -1.0f; /* argv "freezeN": pin iTime to N seconds */
static int g_frames = 0;

/* Write the just-rendered frame (scene + HUD, before the swap) as a binary
 * PPM so it can be scp'd off the device and inspected. */
static void dump_frame(const char* path)
{
    size_t n = (size_t)win_width * (size_t)win_height;
    unsigned char* rgba = (unsigned char*)malloc(n * 4);
    if (!rgba) return;
    glReadPixels(0, 0, win_width, win_height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", win_width, win_height);
        /* glReadPixels rows are bottom-up; PPM is top-down. */
        for (int y = win_height - 1; y >= 0; y--) {
            const unsigned char* row = rgba + (size_t)y * win_width * 4;
            for (int x = 0; x < win_width; x++)
                fwrite(row + (size_t)x * 4, 1, 3, f);
        }
        fclose(f);
        printf("rainwin: dumped %dx%d to %s\n", win_width, win_height, path);
    }
    free(rgba);
}

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = frame_count;
        frame_count = 0;
        if (g_fpslog && fps > 0)
            printf("rainwin: fps=%d\n", fps);
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
        printf("rainwin: shader compile failed: %s\n", log);
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
        printf("rainwin: program link failed: %s\n", log);
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
    prog = link_prog(rainwin_vs_glsl, rainwin_fs_glsl);
    /* The reference shader reads the shadertoy builtins iTime / iResolution /
     * iMouse by those exact names, so the uniforms keep the reference's
     * spelling.  iMouse is bound neutral (0,0,0,0): the reference treats a zero
     * mouse as "not clicked" and uses its rain/condensation defaults. */
    loc_time  = glGetUniformLocation(prog, "iTime");
    loc_res   = glGetUniformLocation(prog, "iResolution");
    loc_mouse = glGetUniformLocation(prog, "iMouse");
}

/* ------------------------------------------------------------------
 * Dear ImGui HUD: a display-only stats window drawn on top of the rain
 * scene.  ImGui is C++ and renders through the GLES2 backend; the app
 * calls glForceCpuRasterEWOK() once in main so ImGui's texcol raster
 * falls back to the CPU interpreter and composites into the same fb the
 * GPU fullscreen scene wrote.  The HUD is read-only, so no xwin input is
 * forwarded and the window is marked NoInputs.
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
 * route the rain program used this frame, queried while it was bound. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Rainwin", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %d", fps);
#ifdef PGL_G3D
    if (gpu && g_scale > 1)
        ImGui::Text("Scale: 1/%d res", g_scale);
#endif
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
    if (g_freeze >= 0.0f) iGlobalTime = g_freeze;

    glViewport(0, 0, win_width, win_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Re-bind the scene program every frame: the engine's GPU bridge and the
     * HUD overlay each touch GL_CURRENT_PROGRAM, so an explicit bind keeps the
     * quad on the rain shader. */
    glUseProgram(prog);
    if (loc_time >= 0) glUniform1f(loc_time, iGlobalTime);
    if (loc_res >= 0)
        glUniform3f(loc_res, (float)win_width, (float)win_height, 1.0f);
    if (loc_mouse >= 0)
        glUniform4f(loc_mouse, 0.0f, 0.0f, 0.0f, 0.0f);   /* neutral: default rain/fog */

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    /* Overlay the FPS / Renderer readout as a Dear ImGui window.  Query the
     * route while the rain program is still bound (the engine reports the
     * currently-bound program and ImGui binds its own next).
     * glForceCpuRasterEWOK() (called once in main) makes ImGui's texcol raster
     * fall back to the CPU interpreter so it composites into the same fb the
     * GPU fullscreen scene wrote -- correctly overlaying on both virt and
     * raspi5. */
    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    g_frames++;
    if (g_dump > 0 && g_frames == g_dump) {
        dump_frame("/tmp/rainwin.ppm");
        exit(0);
    }

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    /* Screensaver-style exit: any key press or mouse button press quits, so
     * the demo closes on the first sign of user activity (matching waterdrop /
     * city).  The HUD is read-only, so there is no interaction to preserve
     * and no event needs forwarding.  Only the initial MOUSE_STATE_DOWN (not a
     * DRAG continuation) triggers the exit, so a press-and-move does not race
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
    /* argv: optional "WxH" sets the compute workgroup shape (GPU build, A/B
     * tuning), optional "scaleN" sets the faithful-compute render-scale
     * denominator, optional "fpslog" echoes the 1s FPS roll to stdout,
     * optional "freezeN" pins iTime to N seconds and "dumpN" writes frame N to
     * /tmp/rainwin.ppm then quits (headless picture check over ssh).  The
     * demo ships a single effect; there is no tier selection. */
    for (int ai = 1; ai < argc && argv[ai]; ai++) {
        unsigned lx = 0, ly = 0;
        int n = 0;
        if (strcmp(argv[ai], "fpslog") == 0) {
            g_fpslog = 1;
            continue;
        }
        if (sscanf(argv[ai], "dump%d", &n) == 1 && n > 0) {
            g_dump = n;
            continue;
        }
        if (sscanf(argv[ai], "freeze%d", &n) == 1 && n >= 0) {
            g_freeze = (float)n;
            continue;
        }
#ifdef PGL_G3D
        if (strncmp(argv[ai], "scale", 5) == 0 &&
            argv[ai][5] >= '1' && argv[ai][5] <= '9' && argv[ai][6] == '\0') {
            g_scale = argv[ai][5] - '0';
        } else
#endif
        if (sscanf(argv[ai], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0) {
#ifdef PGL_G3D
            pglc_compile_set_cs_local_size(lx, ly);
#endif
        }
    }
#ifdef PGL_G3D
    /* Apply the (possibly argv-overridden) scale unconditionally so the
     * default takes effect on a bare launch, not only when "scaleN" is
     * passed.  Set before the first frame; inert on the CPU interpreter
     * path. */
    pgl_g3d_set_render_scale(g_scale);
#endif

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height,
                             "Rainwin", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* Fullscreen BEFORE the EGL surface is created (same ordering constraint as
     * waterdrop / city): xwin_fullscreen() is synchronous, so on return the
     * window graph already carries the full display size and
     * eglCreateWindowSurface hands gles_engine_init those dimensions, letting
     * pgl_g3d_init allocate the fb/z contig pair once at full size.
     * Fullscreening AFTER EGL would instead route through egl_on_resize ->
     * pglResizeFramebuffer -> pgl_g3d_resize's grow branch, which frees the
     * small runs then asks the contig pool for a large pair in one shot; on
     * fragmentation that fails, the GPU latches off (_pgl_gpu_off=1) and the
     * session falls back to the CPU interpreter with no recovery.
     * win_width/win_height stay as the fallback should a WM reject MAX. */
    xwin_fullscreen(xwin);

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_shaders();

    /* Force ONLY the mesh/texcol raster dispatches (Dear ImGui's HUD) onto the
     * CPU interpreter so they composite into the same fb the GPU fullscreen
     * rain scene wrote; gpu_try_fullscreen keeps the scene on the V3D GPU.
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
