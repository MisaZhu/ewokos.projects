/*
 * shadertoy.cpp -- EGL + GLES2 (software rasterizer behind libGLESv2).
 *
 * 11 full-screen fragment effects, ported off portablegl's C-callback model
 * (frag_func + pglDrawFrame2 + glContext + graph_blt) to the standard GLES2/EGL
 * front end.  Each effect is now a real GLSL ES 1.00 program (shadertoy_glsl.h):
 * a passthrough vertex shader (gl_Position = aPosition) plus a procedural
 * fragment shader that derives colour from gl_FragCoord / iGlobalTime /
 * iResolution and optional iChannelN samplers.  A single 6-vertex NDC quad
 * (GL_TRIANGLES) is drawn per frame; the window/context come from libEGL and
 * present is a plain eglSwapBuffers.
 *
 * GPU note (raspi5 / PGL_G3D): the engine transparently compiles each effect to
 * VideoCore VII QPU and dispatches it on the GPU through the fullscreen-quad
 * route in pgl_engine.c -- a program whose VS has no uMVP and whose FS reads
 * gl_FragCoord is recognised as a fullscreen effect and drawn with
 * pgl_g3d_batch_tri_compiled, uniforms packed by name, iChannelN bound to tiled
 * TMU slots.  Effects the daemon declines (texture / uniform / size limits)
 * latch back to the CPU GLSL interpreter with identical pixels, so this file is
 * pure GLES2 with no portablegl calls.  glGpuActiveEWOK() reports which path is
 * live for the on-screen HUD.
 *
 * Effect order, channel assignments, textures, timing, auto-advance, check/cpu
 * argv modes and the FPS/GPU overlay are unchanged from the original.
 */
#include "glcommon/gltools.h"   /* GLES2/gl2.h + gl2ext.h (glGpuActiveEWOK) + load_texture2D */
#include "glcommon/hud.h"       /* GLES2 on-screen text overlay (FPS / GPU-CPU) */

#include <EGL/egl.h>

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

#include "shadertoy_glsl.h"

#ifdef PGL_G3D
/* pglcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Not a portablegl symbol -- it belongs to the mesa compiler merged
 * into libGLESv2. */
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
#endif

#define NUM_TEXTURES 5
#define NUM_SHADERS  SHADERTOY_NUM_SHADERS

/* Window dimensions (can be resized). */
static int win_width = 640;
static int win_height = 480;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint quad_vbo = 0;
static GLuint textures[NUM_TEXTURES];
static GLuint shaders[NUM_SHADERS];
static GLint  loc_time[NUM_SHADERS];
static GLint  loc_res[NUM_SHADERS];
static GLint  loc_ch[NUM_SHADERS][3];

static int      cur_shader = 0;
static uint32_t start_time = 0;
static float    iGlobalTime = 0.0f;

/* "check" mode (argv): render every effect at a fixed iGlobalTime, print an
 * FNV-1a checksum + sample pixels once per effect, then exit after the last.
 * Used to compare the GPU route against the CPU interpreter on a headless
 * device.  "cpu" pins frames to the CPU interpreter via a full-window scissor
 * (the engine's fullscreen GPU route declines scissored frames). */
static int g_check_mode = 0;
static int g_check_cpu = 0;
static int g_check_done[32];

/* FPS counter. */
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;

/* textures[] index for each effect's iChannelN (-1 = unused).  Mirrors the
 * original frag_func channel assignments; textures[] file order is
 * [0]=tex00 [1]=tex02 [2]=tex06 [3]=tex01 [4]=tex09, so:
 *   [3] running_in_the_night  iChannel0 = tex2 -> textures[1]
 *   [4] square_tunnel         iChannel0 = tex0 -> textures[0]
 *   [5] deform_tunnel         iChannel0 = tex2, iChannel1 = tex0
 *   [9] the_cave              iChannel0 = tex6, iChannel1 = tex1, iChannel2 = tex9 */
static const int effect_chantex[NUM_SHADERS][3] = {
    {-1,-1,-1}, {-1,-1,-1}, {-1,-1,-1},
    { 1,-1,-1},
    { 0,-1,-1},
    { 1, 0,-1},
    {-1,-1,-1}, {-1,-1,-1}, {-1,-1,-1},
    { 2, 3, 4},
    {-1,-1,-1}
};

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = frame_count;
        printf("FPS: %d (effect %d)\n", fps, cur_shader); fflush(stdout);
        frame_count = 0;
    }
    frame_count++;
}

static void check_frame(int w, int h, int effect)
{
    int n = w * h, i;
    uint32_t hsh = 2166136261u;
    uint32_t* buf = (uint32_t*)malloc((size_t)n * sizeof(uint32_t));
    if (!buf) return;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    for (i = 0; i < n; i++) { hsh ^= buf[i]; hsh *= 16777619u; }
    printf("SHADERTOY CHECK: effect %d %dx%d sum=%08x px0=%08x pxc=%08x pxq=%08x pxl=%08x\n",
           effect, w, h, hsh, buf[0], buf[(h / 2) * w + w / 2],
           buf[(h / 4) * w + w / 4 + 3], buf[n - 1]);
    fflush(stdout);
    free(buf);
}

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("shadertoy: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_prog(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    GLint ok = 0;
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        printf("shadertoy: program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static void init_quad(void) {
    /* Fullscreen NDC quad as two triangles (GL_TRIANGLES, 6 verts).  The engine's
     * fullscreen GPU route requires GL_TRIANGLES with count >= 3, and the fixed
     * corners it dispatches match these exactly, so the CPU interpreter and the
     * GPU cover the same region with the same diagonal. */
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

static void init_textures(void) {
    static const char* files[NUM_TEXTURES] = {
        "/data/media/textures/tex00.jpg",
        "/data/media/textures/tex02.jpg",
        "/data/media/textures/tex06.jpg",
        "/data/media/textures/tex01.jpg",
        "/data/media/textures/tex09.jpg"
    };
    glGenTextures(NUM_TEXTURES, textures);
    for (int i = 0; i < NUM_TEXTURES; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        /* GL_REPEAT wrap is required for the engine's GPU tiled-texture slot. */
        if (!load_texture2D(files[i], GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_FALSE, GL_FALSE, NULL, NULL))
            printf("shadertoy: failed to load texture %s\n", files[i]);
    }
}

static void init_shaders(void) {
    for (int i = 0; i < NUM_SHADERS; i++) {
        shaders[i] = link_prog(shadertoy_vs_glsl, shadertoy_fs_glsl[i]);
        loc_time[i] = glGetUniformLocation(shaders[i], "iGlobalTime");
        loc_res[i]  = glGetUniformLocation(shaders[i], "iResolution");
        glUseProgram(shaders[i]);
        for (int c = 0; c < 3; c++) {
            char nm[16];
            snprintf(nm, sizeof(nm), "iChannel%d", c);
            loc_ch[i][c] = glGetUniformLocation(shaders[i], nm);
            /* iChannelN samples texture unit N; the engine maps unit N to GPU
             * TMU slot N for the fullscreen route. */
            if (c < shadertoy_fs_channels[i] && loc_ch[i][c] >= 0)
                glUniform1i(loc_ch[i][c], c);
        }
    }
}

static int init_egl(xwin_t* xwin) {
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

static void render_frame(void) {
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height)) {
        win_width = w; win_height = h;
    }
    if (win_width <= 0) win_width = 1;
    if (win_height <= 0) win_height = 1;

    uint32_t now = kernel_tic_ms(0);
    const uint32_t switch_ms = g_check_mode ? 1500 : 5000;

    /* Auto-advance so a single headless run exercises all 11 effects. */
    {
        static uint32_t last_switch = 0;
        if (last_switch == 0) last_switch = now;
        if (now - last_switch >= switch_ms) {
            if (g_check_mode && cur_shader == NUM_SHADERS - 1) {
                printf("SHADERTOY CHECK: done\n"); fflush(stdout);
                exit(0);
            }
            last_switch = now;
            cur_shader = (cur_shader + 1) % NUM_SHADERS;
            start_time = now;
        }
    }

    iGlobalTime = (now - start_time) / 1000.0f;
    if (g_check_mode) {
        iGlobalTime = 1.5f;
        if (g_check_cpu) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, win_width, win_height);
        }
    }

    glViewport(0, 0, win_width, win_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaders[cur_shader]);
    if (loc_time[cur_shader] >= 0) glUniform1f(loc_time[cur_shader], iGlobalTime);
    if (loc_res[cur_shader] >= 0)
        glUniform3f(loc_res[cur_shader], (float)win_width, (float)win_height, 1.0f);

    /* Bind this effect's iChannelN textures to units 0..n-1. */
    int nch = shadertoy_fs_channels[cur_shader];
    for (int c = 0; c < nch; c++) {
        int tidx = effect_chantex[cur_shader][c];
        if (tidx < 0) continue;
        glActiveTexture(GL_TEXTURE0 + c);
        glBindTexture(GL_TEXTURE_2D, textures[tidx]);
    }

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    if (g_check_mode && !g_check_done[cur_shader] && now - start_time >= 800) {
        g_check_done[cur_shader] = 1;
        check_frame(win_width, win_height, cur_shader);
    }

    /* On-screen FPS + GPU/CPU readout: query while the scene program is bound,
     * then hud_draw re-binds it before returning. */
    {
        char hud[64];
        int gpu = glGpuActiveEWOK();
        snprintf(hud, sizeof(hud), "FPS:%d %s", fps, gpu ? "GPU" : "CPU");
        hud_draw(shaders[cur_shader], win_width, win_height, 8, 8, hud);
    }

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS) {
        int key = ev->value.im.value;
        switch (key) {
        case KEY_ESC:   xwin_close(xwin); break;
        case KEY_LEFT:  cur_shader = cur_shader ? cur_shader - 1 : NUM_SHADERS - 1; start_time = kernel_tic_ms(0); break;
        case KEY_RIGHT: cur_shader = (cur_shader + 1) % NUM_SHADERS; start_time = kernel_tic_ms(0); break;
        }
    } else if (ev->type == XEVT_MOUSE) {
        /* Left click switches to the next scene. */
        if (ev->state == MOUSE_STATE_DOWN && ev->value.mouse.button == MOUSE_BUTTON_LEFT) {
            cur_shader = (cur_shader + 1) % NUM_SHADERS;
            start_time = kernel_tic_ms(0);
        }
    }
}

static void loop(void* p) {
    (void)p;
    render_frame();
    usleep(1000);
}

int main(int argc, char** argv) {
    /* argv: "WxH" compute workgroup shape (GPU build), "check" checksum mode,
     * "cpu" pin check frames to the CPU interpreter. */
    for (int ai = 1; ai < argc && argv[ai]; ai++) {
        unsigned lx = 0, ly = 0;
        if (strcmp(argv[ai], "check") == 0) {
            g_check_mode = 1;
        } else if (strcmp(argv[ai], "cpu") == 0) {
            g_check_cpu = 1;
        } else if (sscanf(argv[ai], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0) {
#ifdef PGL_G3D
            pglc_compile_set_cs_local_size(lx, ly);
#endif
        }
    }

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Shadertoy", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    xwin->on_event = on_event;

    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_quad();
    init_textures();
    init_shaders();
    hud_init();
    hud_set_color(0.0f, 1.0f, 0.0f, 1.0f);   /* green readout, as the original */

    start_time = kernel_tic_ms(0);

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
    if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);
    return 0;
}
