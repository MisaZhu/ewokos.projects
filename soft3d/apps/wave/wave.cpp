/*
 * wave.cpp -- GPU-accelerated procedural water-surface demo (EGL + GLES2).
 *
 * An ORIGINAL animated wave effect: a horizontal water plane is ray-cast
 * through a pinhole camera; surface height comes from a superposition of
 * four directional sine waves plus a two-octave drifting value-noise chop;
 * the sky carries a soft sun disk and a horizon haze band; the water mixes
 * a Schlick-fresnel sky reflection with a depth-tinted body colour and
 * adds a Blinn-Phong glint so the sun sparkles on crests.
 *
 * Structure mirrors apps/waterdrop: a single 6-vertex NDC quad drawn per
 * frame with a procedural fragment shader (wave_glsl.h).  On raspi5
 * (PGL_G3D) the engine transparently compiles that shader to a VideoCore
 * VII compute kernel and dispatches it on the GPU through the
 * fullscreen-quad route (a passthrough VS with no uMVP + an FS reading
 * gl_FragCoord is recognised as a fullscreen effect).  glGpuActiveEWOK()
 * reports which path is live for the on-screen HUD.  This app deliberately
 * does NOT call glForceCpuEWOK() -- it wants the GPU target adopted and
 * the wave scene kept on the GPU.
 *
 * The HUD is a Dear ImGui window (same 16px TTF font, titled auto-resize
 * panel, FPS + colour-coded Renderer as waterdrop / glmatrix).  ImGui
 * rasterizes through a GPU-eligible texcol program, which cannot
 * composite with the wave scene's fullscreen-compute dispatch in one
 * frame, so this app calls the narrower glForceCpuRasterEWOK(): that
 * lever declines ONLY the mesh/texcol raster dispatches to the CPU
 * interpreter -- ImGui then composites into the same NOCACHE fb the GPU
 * scene wrote -- while gpu_try_fullscreen keeps the wave scene on the V3D
 * GPU and gpu_bringup/g_gpu_ready are untouched.
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

#include "wave_glsl.h"

#ifdef PGL_G3D
/* pglcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Belongs to the mesa compiler merged into libGLESv2. */
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
/* Engine knob: render the fullscreen faithful compute at 1/N resolution and
 * upscale to the window.  The N3dSzs port spills hundreds of registers/thread
 * and evaluates the whole wave-stack + caustics per pixel, so at native
 * resolution it runs ~1 FPS on V3D CSD; N=2/3 cuts the pixel count 4x/9x for a
 * matching FPS gain at slightly softer sharpness.  The shader math is unchanged
 * (the engine patches iResolution and pglc_cs_stride to the reduced geometry).
 * No effect on the baked-loop playback path (virt / GPU-decline), which is not
 * a compute draw. */
extern "C" void pgl_g3d_set_render_scale(int scale);
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
static int    g_scale = 3;   /* faithful-compute render-scale denominator; 3 = validated
                                default (~6 FPS on V3D CSD), argv "scaleN" overrides */

/* ---- virt playback of the baked faithful loop (see wave_glsl.h) ---- */
#define WAVE_LOOP_PATH "/apps/wave/res/wave_loop.rgb565"
#define WAVE_LOOP_W    400
#define WAVE_LOOP_H    300
#define WAVE_LOOP_F    48
#define WAVE_LOOP_DUR  8.0f

static int      play_mode = 0;
static GLuint   play_prog = 0;
static uint16_t *loop_buf = NULL;
static uint8_t  *rgba_scratch = NULL;
static GLuint   loop_tex = 0;
static GLuint   texquad_vbo = 0;
static GLint    loc_tex = -1;
static int      cur_frame = -1;

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
        printf("wave: shader compile failed: %s\n", log);
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
        printf("wave: program link failed: %s\n", log);
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

/* Load the baked faithful loop blob and build the texblit program that
 * replays it into play_prog (virt: the only route; GPU targets: the decline
 * fallback -- see wave_glsl.h).  Returns 0 when the resource is absent or
 * truncated, leaving play_prog == 0. */
static int init_playback(void)
{
    size_t need = (size_t)WAVE_LOOP_W * WAVE_LOOP_H * WAVE_LOOP_F * 2;
    FILE *fp = fopen(WAVE_LOOP_PATH, "rb");
    if (!fp) return 0;
    loop_buf = (uint16_t*)malloc(need);
    if (!loop_buf || fread(loop_buf, 1, need, fp) != need) {
        if (fp) fclose(fp);
        free(loop_buf); loop_buf = NULL;
        return 0;
    }
    fclose(fp);
    rgba_scratch = (uint8_t*)malloc((size_t)WAVE_LOOP_W * WAVE_LOOP_H * 4);
    if (!rgba_scratch) return 0;

    glGenTextures(1, &loop_tex);
    glBindTexture(GL_TEXTURE_2D, loop_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WAVE_LOOP_W, WAVE_LOOP_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* NDC quad with texture coords; v flipped because blob row 0 is the
     * image top while GL texture row 0 is the bottom. */
    static const float q[] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f
    };
    glGenBuffers(1, &texquad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, texquad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, wave_vs_tex);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, wave_fs_tex);
    play_prog = glCreateProgram();
    glAttachShader(play_prog, vs);
    glAttachShader(play_prog, fs);
    glBindAttribLocation(play_prog, 0, "aPosition");
    glBindAttribLocation(play_prog, 1, "aTexCoord");
    glLinkProgram(play_prog);
    GLint ok = 0;
    glGetProgramiv(play_prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        printf("wave: playback program link failed\n");
        play_prog = 0;
        return 0;
    }
    loc_tex = glGetUniformLocation(play_prog, "uTex");
    return 1;
}

/* Expand one RGB565 loop frame into the RGBA texture (565 -> 8888 with
 * bit-replication so the dark water gradient does not band). */
static void upload_frame(int idx)
{
    const uint16_t *src = loop_buf + (size_t)idx * WAVE_LOOP_W * WAVE_LOOP_H;
    uint8_t *d = rgba_scratch;
    for (int i = 0; i < WAVE_LOOP_W * WAVE_LOOP_H; ++i) {
        unsigned v = src[i];
        unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        d[0] = (uint8_t)((r << 3) | (r >> 2));
        d[1] = (uint8_t)((g << 2) | (g >> 4));
        d[2] = (uint8_t)((b << 3) | (b >> 2));
        d[3] = 255;
        d += 4;
    }
    glBindTexture(GL_TEXTURE_2D, loop_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WAVE_LOOP_W, WAVE_LOOP_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba_scratch);
    cur_frame = idx;
}

/* Draw the current baked-loop frame as a fullscreen textured quad.  Shared
 * by the CPU-only route (virt) and the GPU-decline fallback (raspi5), so
 * both render exactly the same faithful pixels.  Assumes play_prog is bound
 * and the loop blob is loaded. */
static void draw_playback(void)
{
    int idx = (int)(fmodf(iGlobalTime, WAVE_LOOP_DUR)
                    * (WAVE_LOOP_F / WAVE_LOOP_DUR)) % WAVE_LOOP_F;
    if (idx != cur_frame) upload_frame(idx);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, loop_tex);
    if (loc_tex >= 0) glUniform1i(loc_tex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, texquad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

static void init_shaders(void)
{
#ifndef PGL_G3D
    /* CPU-only target: the interpreter can never run the faithful ocean
     * math interactively (tens of ms per pixel), so play back the baked
     * faithful loop.  There is NO simplified fallback -- a missing blob is
     * a deployment error, because no platform may ever draw a cheap look. */
    if (init_playback()) {
        play_mode = 1;
        prog = play_prog;
        return;
    }
    printf("wave: FATAL baked loop missing (%s); no cheap fallback by design\n",
           WAVE_LOOP_PATH);
#endif
#ifdef PGL_G3D
    /* Hand the faithful single-pass port to the on-device mesa compiler;
     * the interpreter links the minimal stub (wave_fs_glsl) that only owns
     * the iGlobalTime / iResolution uniform metadata.  Same uniform names in
     * both sources keep the engine's by-name uniform staging valid.  The
     * override MUST be set BEFORE glLinkProgram (gles_engine_gpu_link
     * consumes it at link time) and cleared right after so later programs
     * (ImGui) link against their own sources. */
    glSetGpuShaderSourceEWOK(wave_vs_glsl, wave_fs_gpu.c_str());
    prog = link_prog(wave_vs_glsl, wave_fs_glsl.c_str());
    glSetGpuShaderSourceEWOK(NULL, NULL);
    loc_time = glGetUniformLocation(prog, "iGlobalTime");
    loc_res  = glGetUniformLocation(prog, "iResolution");
    /* Prepare the baked faithful playback as the decline fallback: if the
     * GPU ever refuses the faithful program (mesa reject, daemon over-cap or
     * a register-spill uniform the CSD path cannot stage), render the SAME
     * faithful pixels as virt instead of any cheap look -- or a black stub.
     * A missing blob here is a deployment error: without it the GPU-decline
     * fallback has nothing faithful to draw, so say so loudly in the log. */
    if (!init_playback())
        printf("wave: FATAL baked loop missing (%s); GPU-decline fallback "
               "unavailable -- deploy res/wave_loop.rgb565 alongside the "
               "wave binary\n", WAVE_LOOP_PATH);
#endif
}

/* ------------------------------------------------------------------
 * Dear ImGui HUD: a display-only stats window drawn on top of the wave
 * scene.  ImGui is C++ and renders through the GLES2 backend; the app
 * calls glForceCpuRasterEWOK() once in main so ImGui's texcol raster
 * falls back to the CPU interpreter and composites into the same fb the
 * GPU fullscreen scene wrote.  The HUD is read-only, so no xwin input
 * is forwarded and the window is marked NoInputs.
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
 * route the wave program used this frame, queried while it was bound. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Wave", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %d", fps);
    if (gpu && g_scale > 1)
        ImGui::Text("Scale: 1/%d res", g_scale);
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
     * HUD overlay each touch GL_CURRENT_PROGRAM, so an explicit bind keeps
     * the quad on the wave shader. */
    int gpu;
    if (play_mode) {
        /* CPU-only target: always the baked faithful loop. */
        glUseProgram(play_prog);
        draw_playback();
        gpu = 0;
    } else {
        /* GPU target: the faithful scene runs live on the V3D GPU.  Query
         * the route with the scene program bound; if the GPU DECLINED it
         * (mesa reject or daemon over-cap -> cpu_latched), fall back to the
         * SAME baked faithful playback rather than any simplified look. */
        glUseProgram(prog);
        gpu = glGpuActiveEWOK();
        if (!gpu && play_prog) {
            glUseProgram(play_prog);
            draw_playback();
        } else {
            if (loc_time >= 0) glUniform1f(loc_time, iGlobalTime);
            if (loc_res >= 0)
                glUniform3f(loc_res, (float)win_width, (float)win_height, 1.0f);
            glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisableVertexAttribArray(0);
        }
    }

    /* Overlay the FPS / Renderer readout as a Dear ImGui window.
     * glForceCpuRasterEWOK() (called once in main) makes ImGui's texcol
     * raster fall back to the CPU interpreter so it composites into the
     * same fb the GPU fullscreen scene wrote -- correctly overlaying on
     * both virt and raspi5. */
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    /* Screensaver-style exit: any key press or mouse button press quits, so
     * the demo closes on the first sign of user activity (matching
     * waterdrop / glmatrix).  The HUD is read-only, so there is no
     * interaction to preserve and no event needs forwarding.  Only the
     * initial MOUSE_STATE_DOWN (not a DRAG continuation) triggers the exit,
     * so a press-and-move does not race xwin_close against further motion. */
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
    /* argv: "scaleN" (N=1..9) renders the faithful compute at 1/N resolution
     * and upscales (GPU build, the dominant FPS lever for the spill-bound
     * N3dSzs kernel); optional "WxH" sets the compute workgroup shape (GPU
     * build, A/B tuning). */
    for (int ai = 1; ai < argc && argv[ai]; ai++) {
        unsigned lx = 0, ly = 0;
        if (strncmp(argv[ai], "scale", 5) == 0 &&
            argv[ai][5] >= '1' && argv[ai][5] <= '9' && argv[ai][6] == '\0') {
            g_scale = argv[ai][5] - '0';
#ifdef PGL_G3D
            pgl_g3d_set_render_scale(g_scale);
#endif
        } else if (sscanf(argv[ai], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0) {
#ifdef PGL_G3D
            pglc_compile_set_cs_local_size(lx, ly);
#endif
        }
    }
#ifdef PGL_G3D
    /* Apply the (possibly argv-overridden) scale unconditionally so the default
     * takes effect on a bare launch, not only when "scaleN" is passed. */
    pgl_g3d_set_render_scale(g_scale);
#endif

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height,
                             "Wave", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    /* Fullscreen BEFORE the EGL surface is created (same ordering constraint
     * as waterdrop / glmatrix): xwin_fullscreen() is synchronous, so on
     * return the window graph already carries the full display size and
     * eglCreateWindowSurface hands gles_engine_init those dimensions,
     * letting pgl_g3d_init allocate the fb/z contig pair once at full
     * size.  Fullscreening AFTER EGL would instead route through
     * egl_on_resize -> pglResizeFramebuffer -> pgl_g3d_resize's grow
     * branch, which frees the small runs then asks the contig pool for a
     * large pair in one shot; on fragmentation that fails, the GPU latches
     * off (_pgl_gpu_off=1) and the session falls back to the CPU
     * interpreter with no recovery.  win_width/win_height stay as the
     * fallback should a WM reject the MAX state. */
    xwin_fullscreen(xwin);

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_shaders();

    /* Force ONLY the mesh/texcol raster dispatches (Dear ImGui's HUD) onto
     * the CPU interpreter so they composite into the same fb the GPU
     * fullscreen wave scene wrote; gpu_try_fullscreen keeps the scene on
     * the V3D GPU.  Must be set before the first frame is presented. */
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
