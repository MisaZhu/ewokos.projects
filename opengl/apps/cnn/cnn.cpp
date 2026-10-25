/* Shadertoy Nfy3DW "Handwritten Letter Recognizer" by rickyyy
 * https://www.shadertoy.com/view/Nfy3DW
 *
 * GPU-routed CNN visualizer. The raspi5 V3D fullscreen-compute route can only
 * sample textures that were uploaded from CPU pixels with GL_REPEAT wrap
 * (pgl_engine.c gpu_ctex_slot); FBO colour attachments (glTexImage2D NULL +
 * CLAMP_TO_EDGE) never stage a ctex slot and silently decline to the CPU
 * interpreter. So the tiny CNN (32x32 / 32x64 / 32x32 / 64x64) runs on the CPU
 * into plain pixel arrays that are re-uploaded each frame with glTexImage2D,
 * and only the screen-resolution visualization runs as a single GPU fullscreen
 * pass sampling those four staged textures.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <ewoksys/kernel_tic.h>
#include <x/x.h>
#include <x/xwin.h>
#include "gen/cnn_glsl.h"

#ifdef PGL_G3D
extern "C" void pgl_g3d_set_render_scale(int scale);
#endif

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;
static bool current = false;
static xwin_t *window;

static GLuint texid[4];     /* A, B, C, D staged as CPU-uploaded ctex slots */
static GLuint progImage;
static GLuint quad;
static int width = 800, height = 450;
static bool fpslog;
static uint32_t start_time, fps_time;
static int frames, fps_frames, result;
static float fps;
static bool clear_req;

/* Mouse state for drawing (canvas coords 0..32) */
static float mouse_cx = 0, mouse_cy = 0;
static float last_cx = 0, last_cy = 0;
static bool have_last = false;
static bool mouse_down = false;

/* ---- CPU-side CNN state ---- */
static float ink[32 * 32];
static float convB_raw[32 * 32 * 4];
static float convB_pool[16 * 16 * 4];
static float convC_raw[16 * 16 * 4];
static float convC_pool[8 * 8 * 4];
static float probs[26];
static int   topClass = 0;
static float topConf = 0;

static uint8_t texA[32 * 32 * 4];
static uint8_t texB[32 * 64 * 4];
static uint8_t texC[32 * 32 * 4];
static uint8_t texD[64 * 64 * 4];

static float fractf(float x) { return x - floorf(x); }
static float hash11(float p)
{
    p = fractf(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return fractf(p);
}
static float procWeight(int layer, int outIdx, int inIdx, int kp)
{
    float seed = (float)layer * 10000.f + (float)outIdx * 100.f +
                 (float)inIdx * 10.f + (float)kp;
    return (hash11(seed) - 0.5f) * 0.5f;
}
static float procBias(int layer, int idx)
{
    float seed = (float)layer * 1000.f + (float)idx;
    return (hash11(seed) - 0.5f) * 0.1f;
}
/* 5x7 glyph bitmaps (bit c of row r = column c), same font the GPU header uses */
static const uint8_t glyphBits[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static uint8_t f2b(float v) { return (uint8_t)(clamp01(v) * 255.f); }

static float ink_at(int x, int y)
{
    if (x < 0) x = 0; if (x > 31) x = 31;
    if (y < 0) y = 0; if (y > 31) y = 31;
    return ink[y * 32 + x];
}
static float poolB_at(int x, int y, int m)
{
    if (x < 0) x = 0; if (x > 15) x = 15;
    if (y < 0) y = 0; if (y > 15) y = 15;
    return convB_pool[(y * 16 + x) * 4 + m];
}
static float poolC_at(int x, int y, int m)
{
    if (x < 0) x = 0; if (x > 7) x = 7;
    if (y < 0) y = 0; if (y > 7) y = 7;
    return convC_pool[(y * 8 + x) * 4 + m];
}

static void cpu_step(void)
{
    int x, y, m, ky, kx, c, i;

    if (clear_req) {
        memset(ink, 0, sizeof(ink));
        clear_req = false;
    }

    /* Stamp ink where the pointer is held, interpolating between events so
       fast swipes leave a continuous stroke */
    if (mouse_down) {
        float tx = mouse_cx, ty = mouse_cy;
        if (!have_last) { last_cx = tx; last_cy = ty; have_last = true; }
        float dx = tx - last_cx, dy = ty - last_cy;
        float span = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
        int n = (int)span + 1;
        if (n > 64) n = 64;
        for (i = 0; i <= n; ++i) {
            int cx = (int)(last_cx + dx * (float)i / (float)n);
            int cy = (int)(last_cy + dy * (float)i / (float)n);
            for (ky = -1; ky <= 1; ++ky)
                for (kx = -1; kx <= 1; ++kx) {
                    int px = cx + kx, py = cy + ky;
                    if (px >= 0 && px < 32 && py >= 0 && py < 32)
                        ink[py * 32 + px] = 1.f;
                }
        }
        last_cx = tx; last_cy = ty;
    }

    /* conv1: 32x32, 4 maps */
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            for (m = 0; m < 4; ++m) {
                float s = 0;
                for (ky = 0; ky < 3; ++ky)
                    for (kx = 0; kx < 3; ++kx)
                        s += ink_at(x + kx - 1, y + ky - 1) *
                             procWeight(1, m, 0, ky * 3 + kx);
                s += procBias(1, m);
                convB_raw[(y * 32 + x) * 4 + m] = s > 0 ? s : 0;
            }
    /* pool B 16x16 */
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
            for (m = 0; m < 4; ++m) {
                float a = convB_raw[((y * 2) * 32 + x * 2) * 4 + m];
                float b = convB_raw[((y * 2) * 32 + x * 2 + 1) * 4 + m];
                float cc = convB_raw[((y * 2 + 1) * 32 + x * 2) * 4 + m];
                float d = convB_raw[((y * 2 + 1) * 32 + x * 2 + 1) * 4 + m];
                convB_pool[(y * 16 + x) * 4 + m] = fmaxf(fmaxf(a, b), fmaxf(cc, d));
            }
    /* conv2: 16x16, 4 maps with skip */
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
            for (m = 0; m < 4; ++m) {
                float s = 0;
                for (ky = 0; ky < 3; ++ky)
                    for (kx = 0; kx < 3; ++kx)
                        s += poolB_at(x + kx - 1, y + ky - 1, m) *
                             procWeight(2, m, m, ky * 3 + kx);
                s += procBias(2, m) + poolB_at(x, y, m) * 0.5f;
                convC_raw[(y * 16 + x) * 4 + m] = s > 0 ? s : 0;
            }
    /* pool C 8x8 */
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x)
            for (m = 0; m < 4; ++m) {
                float a = convC_raw[((y * 2) * 16 + x * 2) * 4 + m];
                float b = convC_raw[((y * 2) * 16 + x * 2 + 1) * 4 + m];
                float cc = convC_raw[((y * 2 + 1) * 16 + x * 2) * 4 + m];
                float d = convC_raw[((y * 2 + 1) * 16 + x * 2 + 1) * 4 + m];
                convC_pool[(y * 8 + x) * 4 + m] = fmaxf(fmaxf(a, b), fmaxf(cc, d));
            }
    /* Classifier: template-match the ink against the 26 glyph bitmaps.
       Normalize the ink to its bounding box, average into a 5x7 cell grid and
       take the cosine similarity to each letter mask, so a drawn letter
       actually recognises as that letter. */
    {
        float logits[26], maxL = -1e9f, sumExp = 0;
        int x0 = 99, x1 = -1, y0 = 99, y1 = -1, r, cc;
        float total = 0;
        for (y = 0; y < 32; ++y)
            for (x = 0; x < 32; ++x) {
                float v = ink[y * 32 + x];
                total += v;
                if (v > 0.3f) {
                    if (x < x0) x0 = x; if (x > x1) x1 = x;
                    if (y < y0) y0 = y; if (y > y1) y1 = y;
                }
            }
        if (total < 4.0f || x1 < 0) {
            for (c = 0; c < 26; ++c) probs[c] = 0;
            topClass = 255; topConf = 0;
        } else {
            float cell[7][5], normI = 0;
            int hgt = y1 - y0 + 1;
            for (r = 0; r < 7; ++r)
                for (cc = 0; cc < 5; ++cc) {
                    float s = 0; int n = 0;
                    /* ink rows are y-up (row 0 = bottom) but glyph rows are
                       top-down, so walk the bounding box from the top */
                    int ya = y1 - hgt * (r + 1) / 7, yb = y1 - hgt * r / 7 + 1;
                    int xa = x0 + (x1 - x0 + 1) * cc / 5, xb = x0 + (x1 - x0 + 1) * (cc + 1) / 5;
                    if (yb <= ya) yb = ya + 1;
                    if (xb <= xa) xb = xa + 1;
                    for (y = ya; y < yb && y < 32; ++y)
                        for (x = xa; x < xb && x < 32; ++x) { s += ink[y * 32 + x]; n++; }
                    cell[r][cc] = n ? s / n : 0;
                }
            for (r = 0; r < 7; ++r)
                for (cc = 0; cc < 5; ++cc) normI += cell[r][cc] * cell[r][cc];
            normI = sqrtf(normI);
            for (c = 0; c < 26; ++c) {
                float dot = 0, normT = 0;
                for (r = 0; r < 7; ++r)
                    for (cc = 0; cc < 5; ++cc) {
                        /* font bit 4 is the LEFT column, cc runs left->right */
                        float t = (glyphBits[c][r] >> (4 - cc)) & 1;
                        dot += cell[r][cc] * t;
                        normT += t * t;
                    }
                logits[c] = (normI > 1e-4f ? dot / (normI * sqrtf(normT)) : 0) * 12.0f;
                if (logits[c] > maxL) maxL = logits[c];
            }
            for (c = 0; c < 26; ++c) sumExp += expf(logits[c] - maxL);
            if (sumExp < 0.001f) sumExp = 0.001f;
            topConf = 0; topClass = 255;
            for (c = 0; c < 26; ++c) {
                probs[c] = expf(logits[c] - maxL) / sumExp;
                if (probs[c] > topConf) { topConf = probs[c]; topClass = c; }
            }
        }
    }

    /* Pack pixel arrays for GPU upload */
    for (i = 0; i < 32 * 32; ++i) {
        texA[i * 4 + 0] = f2b(ink[i]);
        texA[i * 4 + 1] = 0; texA[i * 4 + 2] = 0; texA[i * 4 + 3] = 255;
    }
    memset(texB, 0, sizeof(texB));
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            for (m = 0; m < 4; ++m)
                texB[(y * 32 + x) * 4 + m] = f2b(convB_raw[(y * 32 + x) * 4 + m]);
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
            for (m = 0; m < 4; ++m)
                texB[((32 + y) * 32 + x) * 4 + m] = f2b(convB_pool[(y * 16 + x) * 4 + m]);
    memset(texC, 0, sizeof(texC));
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; ++x)
            for (m = 0; m < 4; ++m)
                texC[(y * 32 + x) * 4 + m] = f2b(convC_raw[(y * 16 + x) * 4 + m]);
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x)
            for (m = 0; m < 4; ++m)
                texC[((16 + y) * 32 + x) * 4 + m] = f2b(convC_pool[(y * 8 + x) * 4 + m]);
    memset(texD, 0, sizeof(texD));
    for (x = 0; x < 7; ++x)
        for (m = 0; m < 4; ++m) {
            int ci = x * 4 + m;
            texD[(26 * 64 + x) * 4 + m] = (ci < 26) ? f2b(probs[ci]) : 0;
        }
    texD[(27 * 64 + 0) * 4 + 0] = (uint8_t)topClass;
    texD[(27 * 64 + 0) * 4 + 1] = f2b(topConf);
    texD[(27 * 64 + 0) * 4 + 3] = 255;
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "cnn: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_program(const char *fs_source)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, cnn_vs_glsl);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    if (prog) {
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glBindAttribLocation(prog, 0, "aPosition");
        glLinkProgram(prog);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) return 0;
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "cnn: program link failed: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static bool init_scene()
{
    static const int dims[4][2] = { {32,32}, {32,64}, {32,32}, {64,64} };
    glGenTextures(4, texid);
    for (int i = 0; i < 4; ++i) {
        glBindTexture(GL_TEXTURE_2D, texid[i]);
        /* REPEAT wrap + CPU pixels are mandatory for the V3D ctex staging gate */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dims[i][0], dims[i][1], 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    progImage = create_program(cnn_image_fs_glsl);
    if (!progImage) {
        fprintf(stderr, "cnn: failed to create image program\n");
        return false;
    }

    static const float vertices[] = {
        -1,-1,0, 1,-1,0, 1,1,0,
        -1,-1,0, 1,1,0, -1,1,0
    };
    glGenBuffers(1, &quad);
    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    return glGetError() == GL_NO_ERROR;
}

static bool init_egl()
{
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) return false;
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || !count) return false;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT) return false;
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)window, NULL);
    if (surface == EGL_NO_SURFACE) return false;
    current = eglMakeCurrent(display, surface, surface, context) == EGL_TRUE;
    return current;
}

static void cleanup()
{
    if (current) {
        glFinish();
        if (progImage) glDeleteProgram(progImage);
        glDeleteBuffers(1, &quad);
        glDeleteTextures(4, texid);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        current = false;
    }
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (display != EGL_NO_DISPLAY) eglTerminate(display);
    surface = EGL_NO_SURFACE;
    context = EGL_NO_CONTEXT;
    display = EGL_NO_DISPLAY;
}

static bool on_close(xwin_t *)
{
    cleanup();
    return true;
}

static void upload_and_bind(int unit, int ch, GLuint tex, int w, int h, const uint8_t *px)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLint loc;
    char name[16];
    snprintf(name, sizeof(name), "iChannel%d", ch);
    loc = glGetUniformLocation(progImage, name);
    if (loc >= 0) glUniform1i(loc, unit);
}

static void render(void *)
{
    if (!current) return;

    EGLint w = 0, h = 0;
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &w) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &h) || w <= 0 || h <= 0) {
        result = 1;
        xwin_close(window);
        return;
    }
    width = w; height = h;

    uint32_t now = kernel_tic_ms(0);
    float time = (now - start_time) / 1000.0f;

    cpu_step();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(progImage);
    glUniform1f(glGetUniformLocation(progImage, "iTime"), time);
    glUniform3f(glGetUniformLocation(progImage, "iResolution"), (float)width, (float)height, 1.0f);
    glUniform4f(glGetUniformLocation(progImage, "iMouse"), mouse_cx, mouse_cy, 0, 0);
    glUniform1i(glGetUniformLocation(progImage, "iFrame"), frames);

    upload_and_bind(0, 0, texid[0], 32, 32, texA);
    upload_and_bind(1, 1, texid[1], 32, 64, texB);
    upload_and_bind(2, 2, texid[2], 32, 32, texC);
    upload_and_bind(3, 3, texid[3], 64, 64, texD);

    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "cnn: draw failed: 0x%x\n", (unsigned)error);
        result = 1;
        xwin_close(window);
        return;
    }

    int gpu = glGpuActiveEWOK();
    if (!frames) printf("cnn: %dx%d, renderer=%s, cpu-net + gpu-viz\n", width, height, gpu ? "GPU" : "CPU");
    ++frames;

    if (!eglSwapBuffers(display, surface)) {
        fprintf(stderr, "cnn: swap failed: 0x%x\n", (unsigned)eglGetError());
        result = 1;
        xwin_close(window);
        return;
    }

    ++fps_frames;
    now = kernel_tic_ms(0);
    uint32_t elapsed = now - fps_time;
    if (elapsed >= 1000) {
        fps = fps_frames * 1000.0f / elapsed;
        if (fpslog) printf("cnn: fps=%.2f renderer=%s\n", fps, gpu ? "GPU" : "CPU");
        fps_time = now; fps_frames = 0;
    }
    usleep(1000);
}

static void on_event(xwin_t *win, xevent_t *event)
{
    if (event->type == XEVT_MOUSE) {
        /* Raw event coords are screen-space; convert to client-area (y-down) */
        gpos_t pos = xwin_get_inside_pos(win, event->value.mouse.x, event->value.mouse.y);
        float px = (float)pos.x, py = (float)pos.y;
        /* Canvas rect in uv (y-up): x 0.02..0.36, y 0.06..0.88 */
        mouse_cx = (px / (float)width - 0.02f) / 0.34f * 32.0f;
        mouse_cy = ((float)height - py) / (float)height;
        mouse_cy = (mouse_cy - 0.06f) / 0.82f * 32.0f;
        if (event->state == MOUSE_STATE_DOWN) {
            /* Clear button: header left at uv (0.045, 0.95) y-up = 0.05 y-down */
            float bcx = width * 0.045f, bcy = height * 0.05f;
            float dx = px - bcx, dy = py - bcy;
            if (dx * dx + dy * dy < 14.0f * 14.0f) {
                clear_req = true; have_last = false; return;
            }
            mouse_down = true; have_last = false;
        } else if (event->state == MOUSE_STATE_UP) {
            mouse_down = false; have_last = false;
        }
    }
    if (event->type == XEVT_IM && event->state == XIM_STATE_PRESS) {
        xwin_close(win);
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "fpslog")) fpslog = true;
#ifdef PGL_G3D
        else if (!strncmp(argv[i], "scale", 5) && argv[i][5] >= '1' && argv[i][5] <= '9')
            pgl_g3d_set_render_scale(argv[i][5] - '0');
#endif
        else {
            fprintf(stderr, "Usage: cnn [window320x180] [fpslog] [scale1]\n");
            return 1;
        }
    }

    x_t x;
    x_init(&x, NULL);
    x.on_loop = render;
    window = xwin_open(&x, -1, 32, 32, width, height, "CNN Letter Recognizer", XWIN_STYLE_NORMAL);
    if (!window) return 1;
    window->on_close = on_close;
    window->on_event = on_event;
    xwin_set_alpha(window, false);
    xwin_max(window);

    if (!init_egl() || !init_scene()) {
        fprintf(stderr, "cnn: initialization failed (EGL 0x%x)\n", (unsigned)eglGetError());
        cleanup();
        xwin_destroy(window);
        return 1;
    }

    start_time = fps_time = kernel_tic_ms(0);
    xwin_set_visible(window, true);
    x_run(&x, window);
    cleanup();
    xwin_destroy(window);
    return result;
}
