/*
 * pixar.cpp -- "Congrats Luxo Jr" soft3d demo (EGL + GLES2).
 *
 *   Reference: "Congrats Luxo Jr" by mplanck (2014-12-18)
 *              https://www.shadertoy.com/view/Xlf3z4
 *              Licensed under CC BY-NC-SA 3.0 (Shadertoy default).
 *
 * A single-pass distance-field raymarcher that renders the Pixar Luxo Jr.
 * desk lamp -- articulated base, lower arm, elbow, upper arm, spring
 * coils, cowl with vent slits and a glowing bulb -- on a wooden floor
 * inside a cylindrical wall.  The pose is driven by a small IK solver and
 * the head bobs on a sine timeline; the camera orbits slowly around the
 * origin and can be steered with the mouse (ShaderToy iMouse semantics).
 *
 * The reference samples two Shadertoy stock textures:
 *   iChannel0 = "Gray Noise Small" (64x64) -- used as a 1D hash for the
 *               per-plank grain rotation of the wooden floor.
 *   iChannel1 = "Wood"            (1024x1024) -- the floor albedo / bump.
 * Both ship under res/ so the port renders with the exact same assets as
 * the original.
 *
 * Host frame is cloned from apps/grass (fullscreen NDC quad + passthrough
 * VS + ImGui HUD).  On raspi5 the engine's gpu_try_fullscreen recognises
 * the passthrough VS / gl_FragCoord FS pair and dispatches the shader as
 * a V3D compute kernel; glGpuActiveEWOK() reports which route is live for
 * the HUD.  glForceCpuRasterEWOK() is called once in main so ImGui's
 * texcol raster composites into the same fb the GPU scene wrote.
 *
 * The reference has 60 march steps and 90 soft-shadow steps; those are
 * hard-coded #defines in the original and exposed here via argv knobs
 * ("stepsN" / "shstepsN") plus the "scaleN" render-scale lever that
 * grass/ocean/wave ship, so slow targets can lower the workload without
 * touching the shader.
 */
#include <EGL/egl.h>
#include "glcommon/gltools.h"   /* GLES2/gl2.h + gl2ext.h + load_texture2D */

#include "imgui/imgui.h"
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

#include "pixar_glsl.h"

#ifdef PGL_G3D
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
extern "C" void pgl_g3d_set_render_scale(int scale);
#endif

static int win_width  = 800;
static int win_height = 600;

/* March / soft-shadow step budgets.  Defaults match the reference exactly
 * (DISTMARCH_STEPS=60, SOFTSHADOW_STEPS=90).  argv "stepsN" / "shstepsN"
 * override so slow targets can trade fidelity for throughput. */
static int g_steps   = 60;
static int g_shsteps = 90;

#ifdef PGL_G3D
static int g_scale = 2;   /* faithful-compute render-scale denominator; argv
                             "scaleN" (N=1..9) overrides, 1 = native */
#endif

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint quad_vbo = 0;
static GLuint prog = 0;
static GLint  loc_time  = -1;
static GLint  loc_res   = -1;
static GLint  loc_mouse = -1;

/* Reference textures (iChannel0 = unit 0, iChannel1 = unit 1). */
static GLuint tex_noise_small = 0;   /* Gray Noise Small 64x64     */
static GLuint tex_wood        = 0;   /* Wood           1024x1024   */

/* Look-around pointer in ShaderToy iMouse semantics: bottom-left origin,
 * updated only while the button is held.  Until the first drag it sits at
 * the window centre so click = (0, 0) and the camera follows the reference's
 * untouched default orbit. */
static float g_mx = -1.0f;
static float g_my = -1.0f;

/* Last known screen rect of the ImGui HUD panel (top-left origin).  The
 * panel is NoInputs, so a click on it is hit-tested here and closes the app. */
static float g_hud_rect[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

static uint32_t start_time = 0;
static float    iGlobalTime = 0.0f;

static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;
static int g_fpslog = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = frame_count;
        frame_count = 0;
        if (g_fpslog && fps > 0)
            printf("pixar: fps=%d\n", fps);
    }
    frame_count++;
}

/* Substitute @@STEPS@@ / @@SHSTEPS@@ tokens with the runtime-chosen literal
 * bounds so the GLSL ES 1.00 loop constraints are met (constant-expression
 * bounds). */
static std::string build_pixar_fs(int steps, int shsteps)
{
    std::string s(pixar_fs_glsl_tmpl);
    struct { const char* tok; int val; } subs[] = {
        { "@@STEPS@@",   steps   },
        { "@@SHSTEPS@@", shsteps },
    };
    for (size_t i = 0; i < sizeof(subs)/sizeof(subs[0]); ++i) {
        char num[16];
        snprintf(num, sizeof(num), "%d", subs[i].val);
        std::string token(subs[i].tok);
        size_t pos = 0;
        while ((pos = s.find(token, pos)) != std::string::npos) {
            s.replace(pos, token.length(), num);
            pos += strlen(num);
        }
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
        printf("pixar: shader compile failed: %s\n", log);
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
        printf("pixar: program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

static void init_quad(void)
{
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

/* Load the two bundled reference textures.
 *
 * iChannel0 (Gray Noise Small, 64x64) is a hash lookup -- the reference sets
 * Shadertoy's filter to NEAREST for this asset (blending neighbouring cells
 * would destroy the hash).  GL_REPEAT is required for the mod-64 coord wrap
 * in noise1f().
 *
 * iChannel1 (Wood, 1024x1024) is a colour image.  Shadertoy's default filter
 * for image channels is LINEAR + REPEAT, and its texture origin is bottom-up
 * (OpenGL convention), so we use GL_LINEAR here and flip=true on upload.
 * Earlier this used NEAREST + flip=false, which produced visibly wrong floor
 * colours on real hardware (blocky grain, upside-down wood pattern relative
 * to the reference).
 *
 * Mip-mapping is not enabled: the engine's GPU tiled-texture path accepts a
 * mip chain but the reference explicitly forced mip 0 via `texture(..., -100.)`
 * on the noise channel, and the wood grain tolerates non-mipmapped LINEAR at
 * the ~200-500 px floor footprint the demo renders. */
static void init_textures(void)
{
    glGenTextures(1, &tex_noise_small);
    glBindTexture(GL_TEXTURE_2D, tex_noise_small);
    if (!load_texture2D("/apps/pixar/res/gray_noise_small.png",
                        GL_NEAREST, GL_NEAREST, GL_REPEAT,
                        GL_FALSE, GL_FALSE, NULL, NULL))
        printf("pixar: failed to load gray_noise_small.png\n");

    glGenTextures(1, &tex_wood);
    glBindTexture(GL_TEXTURE_2D, tex_wood);
    if (!load_texture2D("/apps/pixar/res/wood.jpg",
                        GL_LINEAR, GL_LINEAR, GL_REPEAT,
                        GL_TRUE, GL_FALSE, NULL, NULL))
        printf("pixar: failed to load wood.jpg\n");
}

static void init_shaders(void)
{
    std::string fs = build_pixar_fs(g_steps, g_shsteps);
    prog = link_prog(pixar_vs_glsl, fs.c_str());
    loc_time  = glGetUniformLocation(prog, "iGlobalTime");
    loc_res   = glGetUniformLocation(prog, "iResolution");
    loc_mouse = glGetUniformLocation(prog, "iMouse");

    /* iChannelN samples texture unit N (shadertoy convention); the engine
     * maps unit N to the compiled FS's TMU slot on the GPU route. */
    glUseProgram(prog);
    GLint c0 = glGetUniformLocation(prog, "iChannel0");
    GLint c1 = glGetUniformLocation(prog, "iChannel1");
    if (c0 >= 0) glUniform1i(c0, 0);
    if (c1 >= 0) glUniform1i(c1, 1);
}

static void init_imgui(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
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
    ImGui::Begin("Pixar - Luxo Jr", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    g_hud_rect[0] = wp.x;          g_hud_rect[1] = wp.y;
    g_hud_rect[2] = wp.x + ws.x;   g_hud_rect[3] = wp.y + ws.y;
    ImGui::Text("FPS: %d", fps);
    ImGui::Text("Steps: %d / Shadow: %d", g_steps, g_shsteps);
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

    /* Default look pointer at window centre until the mouse has moved.  In
     * ShaderToy iMouse semantics this makes click = (0.5, 0.5) which
     * animate_globals remaps to (0, 0), giving the reference's untouched
     * camera orbit. */
    float mx = (g_mx < 0.0f) ? (float)win_width  * 0.5f : g_mx;
    float my = (g_my < 0.0f) ? (float)win_height * 0.5f : g_my;

    glViewport(0, 0, win_width, win_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog);
    if (loc_time >= 0)  glUniform1f(loc_time, iGlobalTime);
    if (loc_res >= 0)   glUniform3f(loc_res, (float)win_width, (float)win_height, 1.0f);
    if (loc_mouse >= 0) glUniform4f(loc_mouse, mx, my, 0.0f, 0.0f);

    /* Bind reference textures: iChannel0 (gray noise) -> unit 0, iChannel1
     * (wood) -> unit 1. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_noise_small);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_wood);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    /* ESC quits; a click on the HUD panel closes the app.  The camera
     * look-around follows ShaderToy iMouse: only drags steer, bottom-left
     * origin. */
    if (ev->type == XEVT_IM) {
        if (ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
            xwin_close(xwin);
    }
    else if (ev->type == XEVT_MOUSE) {
        if (ev->state == MOUSE_STATE_DOWN) {
            float px = (float)ev->value.mouse.x;
            float py = (float)ev->value.mouse.y;
            if (px >= g_hud_rect[0] && px <= g_hud_rect[2] &&
                py >= g_hud_rect[1] && py <= g_hud_rect[3])
                xwin_close(xwin);
        }
        else if (ev->state == MOUSE_STATE_DRAG) {
            g_mx = (float)ev->value.mouse.x;
            g_my = (float)(win_height - ev->value.mouse.y);
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
    /* argv: optional "WxH" sets the compute workgroup shape (GPU build),
     * optional "scaleN" sets the render-scale denominator, optional
     * "stepsN" / "shstepsN" override the march / soft-shadow step budgets,
     * optional "fpslog" echoes the 1s FPS roll to stdout. */
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
        if (sscanf(argv[ai], "shsteps%d", &n) == 1 && n >= 4 && n <= 512) {
            g_shsteps = n;
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
    pgl_g3d_set_render_scale(g_scale);
#endif

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height,
                             "Pixar - Luxo Jr", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* Fullscreen BEFORE the EGL surface is created (same ordering constraint
     * as grass/waterdrop). */
    xwin_fullscreen(xwin);

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_textures();
    init_shaders();

    /* Decline ONLY the mesh/texcol raster dispatches (Dear ImGui's HUD) to
     * the CPU interpreter so they composite into the same fb the GPU
     * fullscreen scene wrote. */
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
