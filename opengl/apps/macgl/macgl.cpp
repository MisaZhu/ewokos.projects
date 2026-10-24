/* "hello Mac paint" by erpprog, https://www.shadertoy.com/view/fctSzf.
 * Exact Common + Image passes and embedded artwork are preserved in res/.
 * Native-resolution, 57-second animation. GPU lookup data is packed losslessly
 * from the original tables; no external assets, quality presets, camera changes,
 * or default overlays. Optional stats never affect reference captures.
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
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"
#include "gen/macgl_glsl.h"

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;
static bool current, windowed, stats, fpslog, imgui_initialized, reference = false, probe_mode;
static FILE *fps_file;
static xwin_t *window;
static GLuint program, quad, data_texture;
static GLint time_loc, resolution_loc;
static int width = 800, height = 450, frames, result;
static const char *dump_path;
static float freeze_time = -1.0f, fps;
static uint32_t start_time, fps_time, imgui_time;
static int fps_frames;

static bool init_imgui()
{
    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) return false;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
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
    uint32_t now = kernel_tic_ms(0), elapsed = now - imgui_time;
    io.DeltaTime = elapsed ? elapsed / 1000.0f : 0.001f;
    imgui_time = now;
    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Macgl", NULL, ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %.2f", fps);
    ImGui::Text("Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::Text("Shader: %s", reference ? "Reference" : "Optimized");
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
        fprintf(stderr, "macgl: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool init_scene()
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, macgl_vs_glsl);
#ifdef PGL_G3D
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, reference ? macgl_reference_fs_glsl : (probe_mode ? macgl_probe_fs_glsl : macgl_fs_glsl));
#else
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, reference ? macgl_reference_fs_glsl : macgl_cpu_fs_glsl);
#endif
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
#ifdef PGL_G3D
        /* The reflection shader uses the interpreter's analytic inverse;
         * the GPU uses the original inverse and the same lossless lookups. */
        glSetGpuShaderSourceEWOK(macgl_vs_glsl, reference ? macgl_reference_gpu_fs_glsl : (probe_mode ? macgl_probe_fs_glsl : macgl_gpu_fs_glsl));
#endif
        glLinkProgram(program);
#ifdef PGL_G3D
        glSetGpuShaderSourceEWOK(NULL, NULL);
#endif
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!program) return false;
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "macgl: program link failed: %s\n", log);
        return false;
    }
    time_loc = glGetUniformLocation(program, "iTime");
    resolution_loc = glGetUniformLocation(program, "iResolution");
    if (time_loc < 0 || resolution_loc < 0) return false;
#ifdef PGL_G3D
    if (!reference) {
    GLint data_loc = glGetUniformLocation(program, "macglData");
    if (data_loc < 0) return false;
    glGenTextures(1, &data_texture);
    if (!data_texture) return false;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, data_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* The V3D compiled-sampler record is fixed GL_REPEAT, so gpu_ctex_slot only
     * stages a REPEAT-wrapped 2D texture; CLAMP_TO_EDGE leaves the TMU slot
     * unbound and the fullscreen compute draw declines to the CPU. The lookup
     * UVs are texel centers strictly inside [0,1), so REPEAT samples the exact
     * same texel as CLAMP and never wraps: bit-identical, GPU-resident. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 512, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, macgl_data);
    glUseProgram(program);
    glUniform1i(data_loc, 0);
    }
#endif
    static const float vertices[] = {
        -1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0
    };
    glGenBuffers(1, &quad);
    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    return quad && glGetError() == GL_NO_ERROR;
}

static bool init_egl()
{
    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    const EGLint ctx_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) return false;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || !count) return false;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attributes);
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
        if (quad) glDeleteBuffers(1, &quad);
        if (data_texture) glDeleteTextures(1, &data_texture);
        glUseProgram(0);
        if (program) glDeleteProgram(program);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        current = false;
    }
    /* EGL detaches callbacks while the window's painting mutex still exists. */
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    if (display != EGL_NO_DISPLAY) eglTerminate(display);
    surface = EGL_NO_SURFACE;
    context = EGL_NO_CONTEXT;
    display = EGL_NO_DISPLAY;
    quad = program = data_texture = 0;
}

static bool on_close(xwin_t *)
{
    cleanup();
    return true;
}

static bool dump_frame()
{
    unsigned char *pixels = (unsigned char *)malloc((size_t)width * height * 4);
    if (!pixels) return false;
    glFinish();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    bool ok = glGetError() == GL_NO_ERROR;
    FILE *file = ok ? fopen(dump_path, "wb") : NULL;
    if (!file) ok = false;
    if (file) {
        ok = fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
        for (int y = height - 1; y >= 0 && ok; --y)
            for (int x = 0; x < width && ok; ++x)
                ok = fwrite(pixels + ((size_t)y * width + x) * 4, 1, 3, file) == 3;
        if (fclose(file) != 0) ok = false;
    }
    free(pixels);
    if (!ok) fprintf(stderr, "macgl: frame capture failed: %s\n", dump_path);
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
    uint32_t now = kernel_tic_ms(0);
    float time = freeze_time >= 0 ? freeze_time : (now - start_time) / 1000.0f;
    glViewport(0, 0, width, height);
    glUseProgram(program);
    glUniform1f(time_loc, time);
    glUniform3f(resolution_loc, (float)width, (float)height, 1);
    if (data_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, data_texture);
    }
    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    int gpu = glGpuActiveEWOK();
    bool ok = glGetError() == GL_NO_ERROR;
#ifdef PGL_G3D
    if (!gpu) {
        fprintf(stderr, "macgl: exact shader did not enter the GPU path\n");
        ok = false;
    }
#endif
    if (!ok) {
        result = 1;
        xwin_close(window);
        return;
    }
    if (!frames++ && fpslog)
        printf("macgl: %dx%d native, renderer=%s\n", width, height, gpu ? "GPU" : "CPU");
    if (dump_path) {
        result = dump_frame() ? 0 : 1;
        xwin_close(window);
        return;
    }
    if (stats) draw_stats(gpu);
    if (!eglSwapBuffers(display, surface)) {
        fprintf(stderr, "macgl: swap failed: 0x%x\n", (unsigned)eglGetError());
        result = 1;
        xwin_close(window);
        return;
    }
    ++fps_frames;
    now = kernel_tic_ms(0);
    uint32_t elapsed = now - fps_time;
    if (elapsed >= 1000) {
        fps = fps_frames * 1000.0f / elapsed;
        if (fpslog) printf("macgl: fps=%.2f renderer=%s\n", fps, gpu ? "GPU" : "CPU");
        if (fps_file) {
            fprintf(fps_file, "elapsed_ms=%u frames=%d fps=%.3f renderer=%s shader=%s size=%dx%d time=%.3f\n",
                    elapsed, fps_frames, fps, gpu ? "GPU" : "CPU",
                    reference ? "reference" : "optimized", width, height, time);
            fflush(fps_file);
        }
        fps_time = now; fps_frames = 0;
    }
    usleep(1000);
}

static void on_event(xwin_t *win, xevent_t *event)
{
    if ((event->type == XEVT_IM && event->state == XIM_STATE_PRESS) ||
        (event->type == XEVT_MOUSE && event->state == MOUSE_STATE_DOWN)) xwin_close(win);
}

int main(int argc, char **argv)
{
    const char *fps_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "stats")) stats = true;
        else if (!strcmp(argv[i], "fpslog")) fpslog = true;
        else if (!strcmp(argv[i], "reference")) reference = true;
        /* The lossless lookup-texture variant is the DEFAULT: it renders
         * bit-exact and ~60x faster than the reference const-array shader
         * (no V3D scratch/spill). It was previously corrupt because the g2dd
         * TEXTURE_SHADER_STATE packed image_width/height as w-1/h-1 while this
         * V3D silicon uses the field directly as the uv->texel scale; that
         * daemon bug is fixed (true w/h), so texture2D lookups now resolve
         * exactly. `reference` opts back into the slow bit-exact A/B path. */
        else if (!strcmp(argv[i], "fast")) reference = false;
        /* Diagnostic: render the atlas byte-readback grid instead of the scene. */
        else if (!strcmp(argv[i], "probe")) { probe_mode = true; reference = false; }
        else if (!strncmp(argv[i], "fpslog=", 7) && argv[i][7]) fps_path = argv[i] + 7;
        else if (!strncmp(argv[i], "dump=", 5) && argv[i][5]) dump_path = argv[i] + 5;
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
            fprintf(stderr, "Usage: macgl [window320x180] [freeze18] [dump=/path/frame.ppm] [stats] [fpslog[=/path/fps.log]] [fast] [probe]\n");
            return 1;
        }
    }
    if (fps_path) {
        fps_file = fopen(fps_path, "w");
        if (!fps_file) { fprintf(stderr, "macgl: cannot open FPS log: %s\n", fps_path); return 1; }
    }
    x_t x;
    x_init(&x, NULL);
    x.on_loop = render;
    window = xwin_open(&x, -1, 32, 32, width, height, "Macgl", XWIN_STYLE_NORMAL);
    if (!window) return 1;
    window->on_close = on_close;
    window->on_event = on_event;
    xwin_set_alpha(window, false);
    if (!windowed) xwin_fullscreen(window);
    if (!init_egl() || !init_scene() || (stats && !init_imgui())) {
        fprintf(stderr, "macgl: initialization failed (EGL 0x%x)\n", (unsigned)eglGetError());
        cleanup();
        xwin_destroy(window);
        return 1;
    }
    start_time = fps_time = kernel_tic_ms(0);
    xwin_set_visible(window, true);
    x_run(&x, window);
    cleanup();
    xwin_destroy(window);
    if (fps_file) fclose(fps_file);
    return result;
}
