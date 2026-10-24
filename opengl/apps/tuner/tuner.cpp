/* Shadertoy cs2GWD, "20221105_inercia intended one" by 0b5vr.
 * The original Image pass and input provenance are in res/. All scene assets
 * are procedural. The unavailable musicstream uses the approved silent input.
 * Native resolution and original ray budgets, with a read-only ImGui stats HUD.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <string>
#include <unistd.h>
#include <ewoksys/kernel_tic.h>
#include <x/x.h>
#include <x/xwin.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"
#include "gen/tuner_glsl.h"

#ifdef PGL_G3D
/* Engine knob: render the fullscreen faithful compute at 1/N resolution and
 * upscale to the window (the lever grass/wave/ocean ship).  The raymarch is
 * ALU-bound on V3D CSD and its cost scales with the shaded pixel count, so
 * N=2 cuts that ~4x.  The shader needs no change to stay correct: every size
 * it uses is iResolution-relative and the engine patches iResolution and
 * pglc_cs_stride to the reduced geometry together.  Inert on CPU builds. */
extern "C" void pgl_g3d_set_render_scale(int scale);
extern "C" void pgl_g3d_set_render_scale_frac(int num, int den);
extern "C" void pgl_g3d_set_render_taa(int on);
static bool g_taa = false;     /* argv "taa": opt-in temporal-AA accumulate */
/* pglcompiler knob: compute workgroup shape for the GPU path (A/B tuning).
 * Must be set before the first glLinkProgram, which triggers the on-device
 * compile.  Output is bit-identical across shapes; only occupancy/scheduling
 * changes, so this is a quality-neutral throughput lever. */
extern "C" void pglc_compile_set_cs_local_size(unsigned x, unsigned y);
#endif

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;
static bool current = false;
static xwin_t *window;
static GLuint program, quad, audio;
static GLint time_loc, resolution_loc;
static int width = 800, height = 450;
static bool windowed, fpslog, cpu_raster;
#ifdef TUNER_VALIDATE
static bool reference_shader;
static int bench_frames, bench_count;
static uint64_t bench_scene, bench_hud, bench_swap;

static uint32_t usec_now()
{
    uint32_t value = 0;
    kernel_tic32(NULL, NULL, &value);
    return value;
}
#endif
static int dump_at, frames, result;
static float freeze_time = -1.0f;
/* Raymarch step budgets (see apply_march_budget).  argv "stepsN"/"shstepsN"
 * override the primary/shadow march fidelity for throughput on the V3D compute
 * path at native resolution; the default primary budget trades some fidelity
 * for speed while shsteps keeps the reference shadow budget. */
static int g_steps = 32;     /* primary march iterations (domarch(ro,rd,N)) */
static int g_shsteps = 30;   /* per-light shadow march iterations           */
#ifdef PGL_G3D
static int g_scale = 1;      /* faithful-compute render-scale denominator; bare
                             * launch stays native full-res; argv "scaleN"
                             * (N=1..9) overrides, e.g. scale2 = 1/2 res */
static int g_fsnum = 0, g_fsden = 1; /* fractional render-scale num/den via argv
                             * "fscaleNxM" (e.g. fscale3x2 = 2/3 res); 0 = use g_scale */
#endif
static uint32_t start_time, fps_time;
static int fps_frames;
static float fps;
static bool imgui_initialized;
static uint32_t imgui_time;

static bool init_imgui()
{
    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) return false;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)width, (float)height);
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGLES2_Init()) {
        ImGui::DestroyContext();
        return false;
    }
    imgui_initialized = true;
    const char *font = "/usr/system/fonts/system.ttf";
    if (access(font, R_OK) == 0) io.Fonts->AddFontFromFileTTF(font, 16.0f);
    imgui_time = kernel_tic_ms(0);
    return true;
}

static void draw_stats(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    uint32_t now = kernel_tic_ms(0);
    uint32_t elapsed = now - imgui_time;
    io.DeltaTime = elapsed ? elapsed / 1000.0f : 0.001f;
    imgui_time = now;
    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Tuner", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %.2f", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
#ifdef PGL_G3D
    if (gpu && g_fsnum > g_fsden) ImGui::Text("Scale: %d/%d res", g_fsden, g_fsnum);
    else if (gpu && g_scale > 1) ImGui::Text("Scale: 1/%d res", g_scale);
#endif
    ImGui::End();
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());
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
        char log[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "tuner: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* Rewrite the raymarch loop-bound literals in a generated shader source.
 * GLES ES 1.00 requires literal loop bounds and the V3D compute path gains
 * nothing from uniform bounds, so the argv "stepsN"/"shstepsN" knobs patch the
 * literal into the source before it is compiled (the grass/pixar idiom).  The
 * prefixes match every generated variant -- optimized (",true"/",false") and
 * the plain reference form -- because the budget number precedes any extra
 * argument, and the trailing ')' or ',' is left untouched. */
static std::string apply_march_budget(const char *src, int steps, int shsteps)
{
    std::string s(src);
    const char *from[2] = { "domarch(ro,rd,100", "domarch(march.rp,l,30" };
    const char *to[2]   = { "domarch(ro,rd,",    "domarch(march.rp,l,"   };
    int val[2]          = { steps, shsteps };
    for (int k = 0; k < 2; ++k) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%d", to[k], val[k]);
        std::string rep(buf);
        size_t pos = 0;
        while ((pos = s.find(from[k], pos)) != std::string::npos) {
            s.replace(pos, strlen(from[k]), rep);
            pos += rep.size();
        }
    }
    return s;
}

static bool init_scene()
{
    /* Keep the full original interpreter path; the equivalent GPU variant uses
     * the same uniforms and opaque Shadertoy output wrapper.  Both are run
     * through apply_march_budget so the argv step knobs reach the CPU and GPU
     * routes identically. */
    const char *gpu_src = tuner_gpu_fs_glsl;
#ifdef TUNER_VALIDATE
    if (reference_shader) gpu_src = tuner_reference_gpu_fs_glsl;
#endif
    std::string cpu_fs = apply_march_budget(tuner_reference_fs_glsl, g_steps, g_shsteps);
    std::string gpu_fs = apply_march_budget(gpu_src, g_steps, g_shsteps);
    GLuint vs = compile_shader(GL_VERTEX_SHADER, tuner_vs_glsl);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, cpu_fs.c_str());
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    program = glCreateProgram();
    if (program) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "aPosition");
        glSetGpuShaderSourceEWOK(tuner_vs_glsl, gpu_fs.c_str());
        glLinkProgram(program);
        glSetGpuShaderSourceEWOK(NULL, NULL);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!program) return false;
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "tuner: program link failed: %s\n", log);
        return false;
    }
    time_loc = glGetUniformLocation(program, "iTime");
    resolution_loc = glGetUniformLocation(program, "iResolution");
    GLint channel_loc = glGetUniformLocation(program, "iChannel0");
    if (time_loc < 0 || resolution_loc < 0 || channel_loc < 0) {
        fprintf(stderr, "tuner: missing reference uniform\n");
        return false;
    }
    static const float vertices[] = {
        -1,-1,0, 1,-1,0, 1,1,0,
        -1,-1,0, 1,1,0, -1,1,0
    };
    glGenBuffers(1, &quad);
    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    /* Shadertoy's unavailable stream samples red=0. The two rows represent
     * FFT/waveform; vertical flip has no effect on this all-zero texture. */
    static const unsigned char silence[512 * 2 * 4] = {0};
    glGenTextures(1, &audio);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, audio);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* Compiled 2D samplers require REPEAT. For the approved constant-zero
     * input it is exactly equivalent to the reference's CLAMP_TO_EDGE. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, silence);
    glUseProgram(program);
    glUniform1i(channel_loc, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    return quad && audio && glGetError() == GL_NO_ERROR;
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
        if (imgui_initialized) {
            ImGui_ImplGLES2_Shutdown();
            ImGui::DestroyContext();
            imgui_initialized = false;
        }
        if (audio) glDeleteTextures(1, &audio);
        if (quad) glDeleteBuffers(1, &quad);
        glUseProgram(0);
        if (program) glDeleteProgram(program);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        current = false;
    }
    /* The surface detaches callbacks under the window's painting mutex, so
     * destroy it BEFORE xwin_close destroys that mutex and its shared graphs. */
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (display != EGL_NO_DISPLAY) eglTerminate(display);
    surface = EGL_NO_SURFACE;
    context = EGL_NO_CONTEXT;
    display = EGL_NO_DISPLAY;
    audio = quad = program = 0;
}

static bool on_close(xwin_t *)
{
    cleanup();
    return true;
}

static bool dump_frame()
{
    size_t bytes = (size_t)width * height * 4;
    unsigned char *pixels = (unsigned char *)malloc(bytes);
    if (!pixels) return false;
    glFinish();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    bool ok = glGetError() == GL_NO_ERROR;
    FILE *file = ok ? fopen("/tmp/tuner.ppm", "wb") : NULL;
    if (!file) ok = false;
    if (file) {
        ok = fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
        for (int y = height - 1; y >= 0 && ok; --y) {
            unsigned char *row = pixels + (size_t)y * width * 4;
            for (int x = 0; x < width; ++x) {
                row[x * 3] = row[x * 4];
                row[x * 3 + 1] = row[x * 4 + 1];
                row[x * 3 + 2] = row[x * 4 + 2];
            }
            ok = fwrite(row, 3, width, file) == (size_t)width;
        }
        if (fclose(file) != 0) ok = false;
    }
    free(pixels);
    if (ok) printf("tuner: dumped %dx%d to /tmp/tuner.ppm\n", width, height);
    else fprintf(stderr, "tuner: frame dump failed\n");
    return ok;
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
#ifdef TUNER_VALIDATE
    uint32_t phase_start = bench_frames ? usec_now() : 0;
#endif
    uint32_t now = kernel_tic_ms(0);
    float time = freeze_time >= 0 ? freeze_time : (now - start_time) / 1000.0f;
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glUniform1f(time_loc, time);
    glUniform3f(resolution_loc, (float)width, (float)height, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, audio);
    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "tuner: draw failed: 0x%x\n", (unsigned)error);
        result = 1;
        xwin_close(window);
        return;
    }
    /* Query the scene while it is bound, before ImGui switches programs. */
    int gpu = glGpuActiveEWOK();
    if (!frames) printf("tuner: %dx%d native, renderer=%s, audio=silent reference\n",
                       width, height, gpu ? "GPU" : "CPU");
    ++frames;
    if (dump_at && frames == dump_at) {
        /* Keep reference-validation captures free of the stats overlay. */
        result = dump_frame() ? 0 : 1;
        xwin_close(window);
        return;
    }
#ifdef TUNER_VALIDATE
    uint32_t scene_end = bench_frames ? usec_now() : 0;
#endif
    draw_stats(gpu);
#ifdef TUNER_VALIDATE
    if (bench_frames) glFinish();
    uint32_t hud_end = bench_frames ? usec_now() : 0;
#endif
    if (!eglSwapBuffers(display, surface)) {
        fprintf(stderr, "tuner: swap failed: 0x%x\n", (unsigned)eglGetError());
        result = 1;
        xwin_close(window);
        return;
    }
#ifdef TUNER_VALIDATE
    if (bench_frames && frames > 2) {
        bench_scene += (uint32_t)(scene_end - phase_start);
        bench_hud += (uint32_t)(hud_end - scene_end);
        bench_swap += (uint32_t)(usec_now() - hud_end);
        if (++bench_count == bench_frames) {
            FILE *file = fopen("/tmp/tuner-bench.txt", "w");
            const double n = (double)bench_count;
            if (file) {
                fprintf(file, "%s %dx%d frames=%d renderer=%s scene=%.3fms hud=%.3fms swap=%.3fms fps=%.3f\n",
                        reference_shader ? "reference" : "optimized", width, height, bench_count,
                        gpu ? "GPU" : "CPU", bench_scene/n/1000., bench_hud/n/1000.,
                        bench_swap/n/1000., n*1000000./(bench_scene+bench_hud+bench_swap));
                fclose(file);
            }
            xwin_close(window);
            return;
        }
    }
#endif
    ++fps_frames;
    now = kernel_tic_ms(0);
    uint32_t elapsed = now - fps_time;
    if (elapsed >= 1000) {
        fps = fps_frames * 1000.0f / elapsed;
        if (fpslog) printf("tuner: fps=%.2f renderer=%s\n", fps, gpu ? "GPU" : "CPU");
        fps_time = now; fps_frames = 0;
    }
    usleep(1000);
}

static void on_event(xwin_t *win, xevent_t *event)
{
    if ((event->type == XEVT_IM && event->state == XIM_STATE_PRESS) ||
        (event->type == XEVT_MOUSE && event->state == MOUSE_STATE_DOWN))
        xwin_close(win);
}

static bool positive_option(const char *arg, const char *prefix, int *value)
{
    size_t length = strlen(prefix);
    if (strncmp(arg, prefix, length)) return false;
    char *end;
    long n = strtol(arg + length, &end, 10);
    if (end == arg + length || *end || n <= 0 || n > 1000000) return false;
    *value = (int)n;
    return true;
}

int main(int argc, char **argv)
{
#ifdef PGL_G3D
    /* On Pi 5, a tall 4x64 workgroup preserves every output pixel while
     * reducing this scene's frozen-frame CSD time by about 14% versus the
     * generic 32x8 shape. An explicit WxH argv still overrides it. */
    pglc_compile_set_cs_local_size(4, 64);
#endif
    unsigned lx = 0, ly = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "fpslog")) fpslog = true;
#ifdef TUNER_VALIDATE
        else if (!strcmp(argv[i], "reference")) reference_shader = true;
        else if (positive_option(argv[i], "bench", &bench_frames)) {}
#endif
        else if (positive_option(argv[i], "shsteps", &g_shsteps)) {}
        else if (positive_option(argv[i], "steps", &g_steps)) {}
        else if (!strcmp(argv[i], "cpuraster")) cpu_raster = true;
#ifdef PGL_G3D
        else if (!strncmp(argv[i], "fscale", 6)) {
            int n = 0, d = 0;
            if (sscanf(argv[i] + 6, "%dx%d", &n, &d) == 2 && n > 0 && d > 0) {
                g_fsnum = n; g_fsden = d;
            } else return 1;
        }
        else if (!strncmp(argv[i], "scale", 5) && argv[i][5] >= '1' &&
                 argv[i][5] <= '9' && argv[i][6] == '\0')
            g_scale = argv[i][5] - '0';
        else if (!strcmp(argv[i], "taa")) g_taa = true;
        else if (sscanf(argv[i], "%ux%u", &lx, &ly) == 2 && lx > 0 && ly > 0)
            pglc_compile_set_cs_local_size(lx, ly);
#endif
        else if (positive_option(argv[i], "dump", &dump_at)) {}
        else if (!strncmp(argv[i], "window", 6)) {
            char *middle, *end;
            long w = strtol(argv[i] + 6, &middle, 10);
            if (middle == argv[i] + 6 || *middle != 'x') return 1;
            long h = strtol(middle + 1, &end, 10);
            if (end == middle + 1 || *end || w <= 0 || h <= 0 || w > 8192 || h > 8192) return 1;
            windowed = true; width = (int)w; height = (int)h;
        } else if (!strncmp(argv[i], "freeze", 6)) {
            char *end;
            double time = strtod(argv[i] + 6, &end);
            if (end == argv[i] + 6 || *end || !isfinite(time) || time < 0 || time > 1e9) return 1;
            freeze_time = (float)time;
        } else {
            fprintf(stderr, "Usage: tuner [window320x180] [freeze0] [dump1] [fpslog] [steps32] [shsteps30] [cpuraster] [scale1] [WxH]"
#ifdef TUNER_VALIDATE
                    " [reference] [bench30]"
#endif
                    "\n");
            return 1;
        }
    }
    if (g_steps < 8) g_steps = 8; else if (g_steps > 256) g_steps = 256;
    if (g_shsteps < 1) g_shsteps = 1; else if (g_shsteps > 64) g_shsteps = 64;
    /* Optional quality-neutral HUD routing (argv "cpuraster"): decline ONLY the
     * ImGui mesh/texcol raster dispatches to the CPU interpreter so they
     * composite into the same fb the GPU fullscreen scene wrote (the grass
     * idiom), while gpu_try_fullscreen keeps the raymarch on the V3D GPU.  The
     * scene pixels are untouched, so this is a pure overhead lever.  Must be
     * set before the render loop. */
    if (cpu_raster) glForceCpuRasterEWOK();
#ifdef PGL_G3D
    /* Apply the (possibly argv-overridden) scale unconditionally so the default
     * takes effect on a bare launch.  Inert on the CPU interpreter. */
    if (g_fsnum > 0) pgl_g3d_set_render_scale_frac(g_fsnum, g_fsden);
    else pgl_g3d_set_render_scale(g_scale);
    pgl_g3d_set_render_taa(g_taa ? 1 : 0);
#endif
    x_t x;
    x_init(&x, NULL);
    x.on_loop = render;
    window = xwin_open(&x, -1, 32, 32, width, height, "Tuner", XWIN_STYLE_NORMAL);
    if (!window) return 1;
    window->on_close = on_close;
    window->on_event = on_event;
    xwin_set_alpha(window, false);
    if (!windowed) xwin_fullscreen(window);
    if (!init_egl() || !init_scene() || !init_imgui()) {
        fprintf(stderr, "tuner: initialization failed (EGL 0x%x)\n", (unsigned)eglGetError());
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
