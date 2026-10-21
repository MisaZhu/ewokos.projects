/*
 * grass.cpp -- GPU-accelerated wind-blown grass field demo (EGL + GLES2).
 *
 * A faithful re-implementation of the ShaderToy effect "Windyplains" by kuvkar
 * (https://www.shadertoy.com/view/ltXXRM): a first-person walk along a worn
 * dirt path through a swaying grass field rendered from a height map, under
 * scrolling fbm clouds that cast moving shadows, with distance fog, a bright
 * horizon and a sun halo.  Mouse movement looks around; ESC quits.
 *
 * The reference is a single-pass effect, so it maps directly onto the engine's
 * fullscreen fragment-shader route: a single 6-vertex NDC quad drawn per frame
 * with the procedural fragment shader in grass_glsl.h.  It samples the
 * reference's own two noise images (bundled under res/): "Gray Noise Small"
 * (64x64) drives the grass height field and the fbm clouds, "RGBA Noise Medium"
 * (256x256) drives the straw tint and large colour patches.  iChannelN follows
 * the shadertoy convention -- iChannel0 samples texture unit 0, iChannel2
 * samples texture unit 2 -- which is also the unit the engine maps to the V3D
 * TMU slot on the GPU route.
 *
 * On raspi5 (PGL_G3D) the engine transparently compiles the shader to a
 * VideoCore VII compute kernel and dispatches it through the fullscreen-quad
 * route (a passthrough VS with no uMVP + an FS reading gl_FragCoord is
 * recognised as a fullscreen effect).  glGpuActiveEWOK() reports which path is
 * live for the on-screen HUD.  This app deliberately does NOT call
 * glForceCpuEWOK() -- it wants the scene kept on the GPU.
 *
 * The HUD is a Dear ImGui window (same 16px TTF font, titled auto-resize panel,
 * FPS + colour-coded Renderer as waterdrop).  ImGui rasterizes through a
 * GPU-eligible texcol program, which cannot composite with the scene's
 * fullscreen-compute dispatch in one frame, so this app calls the narrower
 * glForceCpuRasterEWOK(): that lever declines ONLY the mesh/texcol raster
 * dispatches to the CPU interpreter -- ImGui then composites into the same
 * NOCACHE fb the GPU scene wrote -- while gpu_try_fullscreen keeps the grass on
 * the V3D GPU and gpu_bringup/g_gpu_ready are untouched.
 */
#include <EGL/egl.h>
#include "glcommon/gltools.h"   /* GLES2/gl2.h + gl2ext.h + load_texture2D */

#include "imgui/imgui.h"                      /* Dear ImGui HUD (as waterdrop) */
#include "imgui/backends/imgui_impl_gles2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <string>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#include "grass_glsl.h"

#ifdef PGL_G3D
/* pglcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Belongs to the mesa compiler merged into libGLESv2. */
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
/* Engine knob: render the fullscreen faithful compute at 1/N resolution and
 * upscale to the window (the lever wave/ocean/waterdrop ship).  The grass
 * raymarcher is ALU-bound on V3D CSD and its cost scales with the shaded pixel
 * count, so N=2 cuts that 4x.  The shader needs no change to stay correct:
 * every size it uses is expressed relative to iResolution, and the engine
 * patches both iResolution and pglc_cs_stride to the reduced geometry.  No
 * effect on the CPU interpreter path (virt / GPU-decline). */
extern "C" void pgl_g3d_set_render_scale(int scale);
#endif

/* Window dimensions (can be resized). */
static int win_width  = 800;
static int win_height = 600;

/* Raymarch step budget, identical to the reference (450).  Lower budgets
 * starve the march: the exponential step growth then overshoots the smooth
 * bilinear height field and hits land too deep in the blade layer, which
 * darkens the field and leaves a black jagged band at the horizon (128 was
 * visibly wrong; 256 already converges).  argv "stepsN" overrides. */
static int g_steps = 450;

#ifdef PGL_G3D
static int g_scale = 2;   /* faithful-compute render-scale denominator; argv "scaleN"
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

/* The reference's two noise images (iChannel0 = unit 0, iChannel2 = unit 2). */
static GLuint tex_noise_small = 0;   /* Gray Noise Small  64x64  */
static GLuint tex_noise_med   = 0;   /* RGBA Noise Medium 256x256 */

/* Look-around pointer (window pixels, y down).  Until the mouse moves it sits
 * slightly below centre so the camera pitches down onto the grass, matching the
 * reference's default of m = (0, 0.2*res.y). */
static float g_mx = -1.0f;
static float g_my = -1.0f;

static uint32_t start_time = 0;
static float    iGlobalTime = 0.0f;

/* FPS counter. */
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;
static int g_fpslog = 0;   /* argv "fpslog": also print the 1s FPS roll to stdout */

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = frame_count;
        frame_count = 0;
        if (g_fpslog && fps > 0)
            printf("grass: fps=%d\n", fps);
    }
    frame_count++;
}

/* Replace every occurrence of the @@STEPS@@ token in the shader template with
 * the literal march-step budget, so the loop bound is a compile-time constant
 * in the GLSL (ES 1.00 requires literal loop bounds). */
static std::string build_grass_fs(int steps)
{
    std::string s(grass_fs_glsl_tmpl);
    const std::string token = "@@STEPS@@";
    char num[16];
    snprintf(num, sizeof(num), "%d", steps);
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::string::npos) {
        s.replace(pos, token.length(), num);
        pos += strlen(num);
    }
    return s;
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
        printf("grass: shader compile failed: %s\n", log);
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
        printf("grass: program link failed: %s\n", log);
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

/* Load the two bundled noise images.  GL_REPEAT wrap is required for the
 * engine's GPU tiled-texture slot; NEAREST filtering keeps the CPU path
 * consistent with the on-device compiled sampler (fixed NEAREST/REPEAT). */
static void init_textures(void)
{
    glGenTextures(1, &tex_noise_small);
    glBindTexture(GL_TEXTURE_2D, tex_noise_small);
    if (!load_texture2D("/apps/grass/res/gray_noise_small.png",
                        GL_NEAREST, GL_NEAREST, GL_REPEAT,
                        GL_FALSE, GL_FALSE, NULL, NULL))
        printf("grass: failed to load gray_noise_small.png\n");

    glGenTextures(1, &tex_noise_med);
    glBindTexture(GL_TEXTURE_2D, tex_noise_med);
    if (!load_texture2D("/apps/grass/res/rgba_noise_medium.png",
                        GL_NEAREST, GL_NEAREST, GL_REPEAT,
                        GL_FALSE, GL_FALSE, NULL, NULL))
        printf("grass: failed to load rgba_noise_medium.png\n");
}

static void init_shaders(void)
{
    std::string fs = build_grass_fs(g_steps);
    prog = link_prog(grass_vs_glsl, fs.c_str());
    loc_time  = glGetUniformLocation(prog, "iGlobalTime");
    loc_res   = glGetUniformLocation(prog, "iResolution");
    loc_mouse = glGetUniformLocation(prog, "iMouse");

    /* iChannelN samples texture unit N (shadertoy convention); the engine maps
     * unit N to the compiled FS's TMU slot on the GPU route. */
    glUseProgram(prog);
    GLint c0 = glGetUniformLocation(prog, "iChannel0");
    GLint c2 = glGetUniformLocation(prog, "iChannel2");
    if (c0 >= 0) glUniform1i(c0, 0);
    if (c2 >= 0) glUniform1i(c2, 2);
}

/* ------------------------------------------------------------------
 * Dear ImGui HUD (mirrors waterdrop): a display-only stats window drawn on
 * top of the grass scene.  glForceCpuRasterEWOK() (called once in main) makes
 * ImGui's texcol raster fall back to the CPU interpreter so it composites into
 * the same fb the GPU fullscreen scene wrote.  The HUD is read-only, so it is
 * marked NoInputs and no xwin input is forwarded to it.
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

static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Grass", NULL,
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

    /* Default look pointer (slightly below centre -> pitch down onto grass)
     * until the mouse has moved at least once. */
    float mx = (g_mx < 0.0f) ? (float)win_width  * 0.5f : g_mx;
    float my = (g_my < 0.0f) ? (float)win_height * 0.7f : g_my;

    glViewport(0, 0, win_width, win_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Re-bind the scene program every frame: the engine's GPU bridge and the
     * HUD overlay each touch the current program, so an explicit bind keeps the
     * quad on the grass shader. */
    glUseProgram(prog);
    if (loc_time >= 0)  glUniform1f(loc_time, iGlobalTime);
    if (loc_res >= 0)   glUniform3f(loc_res, (float)win_width, (float)win_height, 1.0f);
    if (loc_mouse >= 0) glUniform4f(loc_mouse, mx, my, 0.0f, 0.0f);

    /* Bind the noise images to units 0 and 2 (iChannel0 / iChannel2). */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_noise_small);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex_noise_med);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    /* Overlay the FPS / Renderer readout.  Query the route while the scene
     * program is still bound.  glForceCpuRasterEWOK() (called once in main)
     * makes ImGui's texcol raster fall back to the CPU interpreter so it
     * composites into the same fb the GPU fullscreen scene wrote. */
    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    /* ESC quits.  Mouse motion drives the look-around (the reference's "Mouse to
     * look around"), so unlike the screensaver-style demos a click does NOT
     * close the window. */
    if (ev->type == XEVT_IM) {
        if (ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
            xwin_close(xwin);
    }
    else if (ev->type == XEVT_MOUSE) {
        if (ev->state == MOUSE_STATE_MOVE || ev->state == MOUSE_STATE_DRAG) {
            g_mx = (float)ev->value.mouse.x;
            g_my = (float)ev->value.mouse.y;
        }
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
     * denominator, optional "stepsN" sets the raymarch step budget, optional
     * "fpslog" echoes the 1s FPS roll to stdout. */
    for (int ai = 1; ai < argc && argv[ai]; ai++) {
        unsigned lx = 0, ly = 0;
        int n = 0;
        if (strcmp(argv[ai], "fpslog") == 0) {
            g_fpslog = 1;
            continue;
        }
        if (sscanf(argv[ai], "steps%d", &n) == 1 && n >= 8 && n <= 1024) {
            g_steps = n;
            continue;
        }
#ifdef PGL_G3D
        if (strncmp(argv[ai], "scale", 5) == 0 &&
            argv[ai][5] >= '1' && argv[ai][5] <= '9' && argv[ai][6] == '\0') {
            g_scale = argv[ai][5] - '0';
            continue;
        }
#endif
        if (sscanf(argv[ai], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0) {
#ifdef PGL_G3D
            pglc_compile_set_cs_local_size(lx, ly);
#endif
        }
    }
#ifdef PGL_G3D
    /* Apply the (possibly argv-overridden) scale unconditionally so the
     * default takes effect on a bare launch.  Inert on the CPU interpreter. */
    pgl_g3d_set_render_scale(g_scale);
#endif

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height,
                             "Grass", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* Fullscreen BEFORE the EGL surface is created (same ordering constraint as
     * waterdrop): xwin_fullscreen() is synchronous, so on return the window
     * graph already carries the full display size and eglCreateWindowSurface
     * hands gles_engine_init those dimensions, letting pgl_g3d_init allocate the
     * fb/z contig pair once at full size.  Fullscreening AFTER EGL would route
     * through the grow branch, which on fragmentation can fail and latch the GPU
     * off with no recovery. */
    xwin_fullscreen(xwin);

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_textures();
    init_shaders();

    /* Force ONLY the mesh/texcol raster dispatches (Dear ImGui's HUD) onto the
     * CPU interpreter so they composite into the same fb the GPU fullscreen
     * scene wrote; gpu_try_fullscreen keeps the grass on the V3D GPU.  Must be
     * set before the first frame is presented. */
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
