/*
 * colorglass.cpp -- GPU-targeted "Voronoi Stained Glass" demo (EGL + GLES2).
 *
 * A faithful port of the ShaderToy effect "Voronoi Stained Glass" by JackSH3
 * (https://www.shadertoy.com/view/ltSfRW): an animated Voronoi cell pattern forms
 * a pane of coloured stained glass that refracts and reflects the "Uffizi
 * Gallery" environment cubemap through a Fresnel blend, with the "Rock Tiles"
 * texture bump-mapping the pane so the glass ripples.  See colorglass_glsl.h for
 * the shader and the porting notes.
 *
 * Structure mirrors libs/demos/shadertoy.cpp and apps/grass: a single 6-vertex
 * NDC quad drawn per frame with the fragment shader in colorglass_glsl.h.  The
 * two reference inputs are bundled under res/ and uploaded exactly as the
 * original samples them:
 *   iChannel0 = Uffizi Gallery cubemap (6 faces, res/uffizi_0..5.jpg) on unit 0
 *   iChannel1 = Rock Tiles 2D texture  (res/rock_tiles.jpg)          on unit 1
 * On raspi5 (PGL_G3D) the engine would normally compile the shader to a VideoCore
 * VII compute kernel through the fullscreen-quad route, but a samplerCube input
 * is outside what the on-device fullscreen dispatcher accepts, so this effect
 * latches back to the CPU GLSL interpreter (which fully supports textureCube)
 * with identical pixels; glGpuActiveEWOK() reports the live route on the HUD.
 *
 * The HUD is a Dear ImGui window (same 16px TTF font, titled auto-resize panel,
 * FPS + colour-coded Renderer as waterdrop/grass).  ImGui rasterizes through a
 * GPU-eligible texcol program, so this app calls glForceCpuRasterEWOK() to let
 * the HUD composite into the same fb the scene wrote.  The effect has no mouse
 * interaction, so like waterdrop any key or mouse press closes the window.
 */
#include <EGL/egl.h>
#include "glcommon/gltools.h"   /* GLES2/gl2.h + gl2ext.h + load_texture2D/cubemap */

#include "imgui/imgui.h"                      /* Dear ImGui HUD (as waterdrop) */
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

#include "colorglass_glsl.h"

#ifdef PGL_G3D
/* pglcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Belongs to the mesa compiler merged into libGLESv2. */
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
/* mesa V3D_DEBUG bits (V3D_DEBUG_QPU = 1<<4 dumps the QPU assembly of every
 * compiled shader to stderr): argv "qpudump", for tuning only. */
extern "C" uint32_t v3d_mesa_debug;
#endif

/* argv "freezeN": pin iGlobalTime to N seconds so A/B stat windows compare
 * the same frame (-1 = animate normally). */
static float g_freeze = -1.0f;

/* Window dimensions (can be resized). */
static int win_width  = 800;
static int win_height = 600;

/* argv "dumpN": glReadPixels frame N to /tmp/colorglass.ppm and quit
 * (headless picture check over ssh). */
static int g_dump = 0;
static int g_frames = 0;

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
        printf("colorglass: dumped %dx%d to %s\n", win_width, win_height, path);
    }
    free(rgba);
}

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint quad_vbo = 0;
static GLuint prog = 0;
static GLint  loc_time = -1;
static GLint  loc_res = -1;

/* The reference's two inputs: iChannel0 = unit 0 (cubemap), iChannel1 = unit 1. */
static GLuint tex_cube = 0;   /* Uffizi Gallery  6x512x512 */
static GLuint tex_rock = 0;   /* Rock Tiles      512x512   */

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
            printf("colorglass: fps=%d\n", fps);
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
        printf("colorglass: shader compile failed: %s\n", log);
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
        printf("colorglass: program link failed: %s\n", log);
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

/* Load the two bundled reference inputs.  The cubemap's six faces are uploaded
 * in +X,-X,+Y,-Y,+Z,-Z order (the engine loader's fixed mapping) with mipmap
 * filtering to match the reference sampler; vflip=false so no flip.  The Rock
 * Tiles bump map uses GL_REPEAT wrap (the reference's wrap=repeat) and a
 * vertical flip (the reference's vflip=true). */
static void init_textures(void)
{
    static const char* faces[6] = {
        "/apps/colorglass/res/uffizi_0.jpg",
        "/apps/colorglass/res/uffizi_1.jpg",
        "/apps/colorglass/res/uffizi_2.jpg",
        "/apps/colorglass/res/uffizi_3.jpg",
        "/apps/colorglass/res/uffizi_4.jpg",
        "/apps/colorglass/res/uffizi_5.jpg"
    };
    glGenTextures(1, &tex_cube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_cube);
    if (!load_texture_cubemap(faces, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR,
                              GL_FALSE, GL_FALSE))
        printf("colorglass: failed to load Uffizi cubemap\n");

    glGenTextures(1, &tex_rock);
    glBindTexture(GL_TEXTURE_2D, tex_rock);
    if (!load_texture2D("/apps/colorglass/res/rock_tiles.jpg",
                        GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT,
                        GL_TRUE, GL_FALSE, NULL, NULL))
        printf("colorglass: failed to load rock_tiles.jpg\n");
}

static void init_shaders(void)
{
    prog = link_prog(colorglass_vs_glsl, colorglass_fs_glsl);
    loc_time = glGetUniformLocation(prog, "iGlobalTime");
    loc_res  = glGetUniformLocation(prog, "iResolution");

    /* iChannelN samples texture unit N (shadertoy convention). */
    glUseProgram(prog);
    GLint c0 = glGetUniformLocation(prog, "iChannel0");
    GLint c1 = glGetUniformLocation(prog, "iChannel1");
    if (c0 >= 0) glUniform1i(c0, 0);
    if (c1 >= 0) glUniform1i(c1, 1);
}

/* ------------------------------------------------------------------
 * Dear ImGui HUD (mirrors waterdrop/grass): a display-only stats window
 * drawn on top of the scene.  glForceCpuRasterEWOK() (called once in main)
 * makes ImGui's texcol raster fall back to the CPU interpreter so it
 * composites into the same fb the scene wrote.  The HUD is read-only, so
 * it is marked NoInputs and no xwin input is forwarded to it.
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
    ImGui::Begin("Colorglass", NULL,
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
    if (g_freeze >= 0.0f)
        iGlobalTime = g_freeze;

    glViewport(0, 0, win_width, win_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Re-bind the scene program every frame: the engine's GPU bridge and the
     * HUD overlay each touch GL_CURRENT_PROGRAM, so an explicit bind keeps the
     * quad on the glass shader. */
    glUseProgram(prog);
    if (loc_time >= 0) glUniform1f(loc_time, iGlobalTime);
    if (loc_res >= 0)
        glUniform3f(loc_res, (float)win_width, (float)win_height, 1.0f);

    /* Bind the environment cubemap to unit 0 and the bump map to unit 1
     * (iChannel0 / iChannel1). */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_cube);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_rock);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    /* Overlay the FPS / Renderer readout.  Query the route while the scene
     * program is still bound.  glForceCpuRasterEWOK() (called once in main)
     * makes ImGui's texcol raster fall back to the CPU interpreter so it
     * composites into the same fb the scene wrote. */
    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    g_frames++;
    if (g_dump > 0 && g_frames == g_dump) {
        dump_frame("/tmp/colorglass.ppm");
        exit(0);
    }

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    /* Screensaver-style exit: the effect has no mouse interaction, so any key
     * press or mouse button press quits (matching waterdrop).  The HUD is
     * read-only, so no event needs forwarding. */
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
     * tuning), optional "fpslog" echoes the 1s FPS roll to stdout, optional
     * "freezeN" pins the clock, optional "qpudump" dumps the QPU assembly,
     * optional "dumpN" writes frame N to /tmp/colorglass.ppm and quits. */
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
        if (strcmp(argv[ai], "qpudump") == 0) {
            v3d_mesa_debug |= (1u << 4);   /* V3D_DEBUG_QPU */
            continue;
        }
#endif
        if (sscanf(argv[ai], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0) {
#ifdef PGL_G3D
            pglc_compile_set_cs_local_size(lx, ly);
#endif
        }
    }

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height,
                             "Colorglass", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* Fullscreen BEFORE the EGL surface is created (same ordering constraint as
     * waterdrop/grass): xwin_fullscreen() is synchronous, so on return the
     * window graph already carries the full display size and
     * eglCreateWindowSurface hands gles_engine_init those dimensions. */
    xwin_fullscreen(xwin);

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_textures();
    init_shaders();

    /* Force ONLY the mesh/texcol raster dispatches (Dear ImGui's HUD) onto the
     * CPU interpreter so they composite into the same fb the scene wrote.
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
