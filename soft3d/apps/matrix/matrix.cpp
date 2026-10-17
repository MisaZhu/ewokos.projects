#include "glcommon/gltext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <vector>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define WIDTH 640
#define HEIGHT 480
#define FONT_SIZE 14
#define FONT_PATH "/usr/system/fonts/Hack-Regular.ttf"
#define MAX_WIDTH 3840
#define MAX_HEIGHT 2160

/* The composited framebuffer is plain 0xAARRGGBB words; pix_t keeps the rest of
 * the (unchanged) CPU drawing code readable. */
typedef uint32_t pix_t;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char* MATRIX_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@#$%^&*()_+-=[]{}|;:,.<>?";

unsigned char* font_buffer = NULL;
stbtt_fontinfo font;
int font_ascent, font_descent, font_linegap;
float font_scale = 0;
#define MAX_CHAR_BITMAP_SIZE 64

int is_font_loaded = 0;

#define MAX_COLUMN_LENGTH 100

struct GlyphCacheEntry {
    unsigned char bitmap[MAX_CHAR_BITMAP_SIZE * MAX_CHAR_BITMAP_SIZE];
    int w;
    int h;
    int x0, y0;
    int valid;
};

#define MAX_GLYPH_CACHE 256
static GlyphCacheEntry glyph_cache[MAX_GLYPH_CACHE];
static int glyph_cache_initialized = 0;

void init_glyph_cache() {
    if (glyph_cache_initialized) return;
    glyph_cache_initialized = 1;
    memset(glyph_cache, 0, sizeof(glyph_cache));
}

extern int width;
extern int height;
extern pix_t* bbufpix;

GlyphCacheEntry* get_glyph_cache(char c) {
    unsigned char uc = (unsigned char)c;
    GlyphCacheEntry* entry = &glyph_cache[uc];
    if (entry->valid) {
        return entry;
    }

    int glyph = stbtt_FindGlyphIndex(&font, uc);
    int empty = stbtt_IsGlyphEmpty(&font, glyph);
    int gx0 = 0, gy0 = 0, gx1 = 0, gy1 = 0;
    int has_box = stbtt_GetGlyphBox(&font, glyph, &gx0, &gy0, &gx1, &gy1);

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&font, uc, font_scale, font_scale, &x0, &y0, &x1, &y1);

    int w = x1 - x0;
    int h = y1 - y0;

    entry->w = w;
    entry->h = h;
    entry->x0 = x0;
    entry->y0 = y0;

    if (w > 0 && h > 0 && w <= MAX_CHAR_BITMAP_SIZE && h <= MAX_CHAR_BITMAP_SIZE) {
        memset(entry->bitmap, 0, sizeof(entry->bitmap));
        stbtt_MakeCodepointBitmap(&font, entry->bitmap, w, h, w, font_scale, font_scale, uc);
    }
    entry->valid = 1;
    return entry;
}

static void draw_fallback_block(int x, int y, int cell_w, int cell_h, uint32_t color) {
    if (!bbufpix || cell_w <= 0 || cell_h <= 0) return;

    int block_w = cell_w * 3 / 5;
    int block_h = cell_h * 4 / 5;
    if (block_w < 2) block_w = 2;
    if (block_h < 2) block_h = 2;
    if (block_w > cell_w) block_w = cell_w;
    if (block_h > cell_h) block_h = cell_h;

    int start_x = x + (cell_w - block_w) / 2;
    int start_y = y + (cell_h - block_h) / 2;

    for (int py = start_y; py < start_y + block_h; ++py) {
        if (py < 0 || py >= height) continue;
        for (int px = start_x; px < start_x + block_w; ++px) {
            if (px < 0 || px >= width) continue;
            bbufpix[(long)py * width + px] = color;
        }
    }
}

#define DIGIT_HEIGHT 14
#define DIGIT_WIDTH 9
#define DIGIT_SPACING 1

static const char* DIGIT_PATTERNS[10] = {
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########",
    "   ##    "
    "  ###    "
    " ####    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    " ########"
    " ########",
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########"
    "###      "
    "###      "
    "###      "
    "###      "
    "#########"
    "#########",
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########",
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "      ###",
    "#########"
    "#########"
    "###      "
    "###      "
    "###      "
    "###      "
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########",
    "#########"
    "#########"
    "###      "
    "###      "
    "###      "
    "###      "
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########",
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "    ###  "
    "    ###  "
    "  ###    "
    "  ###    "
    "###      "
    "###      "
    "###      "
    "###      ",
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########",
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########"
};

static const char* COLON_PATTERN =
    "         "
    "         "
    "   ###   "
    "   ###   "
    "   ###   "
    "   ###   "
    "         "
    "         "
    "   ###   "
    "   ###   "
    "   ###   "
    "   ###   "
    "         "
    "         ";

static bool get_time_string(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    if(now < 100000)
        return false;
    struct tm time_info;
    localtime_r(&now, &time_info);
    snprintf(buf, buf_size, "%02d:%02d:%02d", time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
    return true;
}

int g_char_scale = 6;

char get_random_matrix_char() {
    size_t char_count = strlen(MATRIX_CHARS);
    if (char_count == 0) return 'A';
    return MATRIX_CHARS[rand() % char_count];
}

void render_digit_with_chars(int digit, int x, int y, int char_scale, uint32_t color) {
    const char* pattern = DIGIT_PATTERNS[digit];
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        for (int col = 0; col < DIGIT_WIDTH; col++) {
            if (pattern[row * DIGIT_WIDTH + col] == '#') {
                int screen_x = x + col * char_scale;
                int screen_y = y + row * char_scale;
                if (!is_font_loaded) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                char c = get_random_matrix_char();
                GlyphCacheEntry* glyph = get_glyph_cache(c);
                if (glyph->w <= 0 || glyph->h <= 0) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                int baseline_y = screen_y + (int)(font_ascent * font_scale) + glyph->y0;
                for (int gy = 0; gy < glyph->h; gy++) {
                    int py = baseline_y + gy;
                    if (py < 0 || py >= height) continue;
                    for (int gx = 0; gx < glyph->w; gx++) {
                        int px = screen_x + glyph->x0 + gx;
                        if (px < 0 || px >= width) continue;
                        unsigned char alpha = glyph->bitmap[gy * glyph->w + gx];
                        if (alpha > 0) {
                            long idx = ((long)py * width) + px;
                            bbufpix[idx] = color;
                        }
                    }
                }
            }
        }
    }
}

void render_colon_with_chars(int x, int y, int char_scale, uint32_t color) {
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        for (int col = 0; col < DIGIT_WIDTH; col++) {
            if (COLON_PATTERN[row * DIGIT_WIDTH + col] == '#') {
                int screen_x = x + col * char_scale;
                int screen_y = y + row * char_scale;
                if (!is_font_loaded) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                char c = get_random_matrix_char();
                GlyphCacheEntry* glyph = get_glyph_cache(c);
                if (glyph->w <= 0 || glyph->h <= 0) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                int baseline_y = screen_y + (int)(font_ascent * font_scale) + glyph->y0;
                for (int gy = 0; gy < glyph->h; gy++) {
                    int py = baseline_y + gy;
                    if (py < 0 || py >= height) continue;
                    for (int gx = 0; gx < glyph->w; gx++) {
                        int px = screen_x + glyph->x0 + gx;
                        if (px < 0 || px >= width) continue;
                        unsigned char alpha = glyph->bitmap[gy * glyph->w + gx];
                        if (alpha > 0) {
                            long idx = ((long)py * width) + px;
                            bbufpix[idx] = color;
                        }
                    }
                }
            }
        }
    }
}

void render_matrix_time(const char* time_str, int start_x, int start_y, int char_scale, uint32_t color) {
    int x = start_x;
    int digit_w = DIGIT_WIDTH * char_scale;
    int spacing = DIGIT_SPACING * char_scale;

    for (int i = 0; time_str[i] != 0; i++) {
        char c = time_str[i];
        if (c == ':') {
            render_colon_with_chars(x, start_y, char_scale, color);
            x += digit_w + spacing;
        } else if (c >= '0' && c <= '9') {
            render_digit_with_chars(c - '0', x, start_y, char_scale, color);
            x += digit_w + spacing;
        }
    }
}

void render_time_overlay() {
    char time_str[16];
    if(!get_time_string(time_str, sizeof(time_str)))
        return;

    int num_digits = 6;
    int num_colons = 2;
    int num_gaps = num_digits + num_colons - 1;
    int time_chars_w = num_digits * DIGIT_WIDTH + num_colons * DIGIT_WIDTH + num_gaps * DIGIT_SPACING;
    int time_chars_h = DIGIT_HEIGHT;

    int target_time_width = width * 2 / 3;
    int target_time_height = height / 2;

    int char_scale_x = target_time_width / time_chars_w;
    int char_scale_y = target_time_height / time_chars_h;
    int char_scale = char_scale_x < char_scale_y ? char_scale_x : char_scale_y;
    if (char_scale < 4) char_scale = 4;
    if (char_scale > 30) char_scale = 30;

    int digit_w = DIGIT_WIDTH * char_scale;
    int spacing = DIGIT_SPACING * char_scale;
    int actual_time_width = num_digits * digit_w + num_colons * digit_w + num_gaps * spacing;
    int actual_time_height = time_chars_h * char_scale;
    int time_x = (width - actual_time_width) / 2;
    int time_y = (height - actual_time_height) / 2;

    render_matrix_time(time_str, time_x, time_y, char_scale, 0xFF00FF00);
}

struct Column {
    float y;
    float speed;
    int length;
    char chars[MAX_COLUMN_LENGTH];

    Column() {
        reset();
    }

    void reset() {
        y = (float)(rand() % height) - height;
        speed = 60.0f + (float)(rand() % 241);  // random fall speed, 60~300 pixels per second
        length = 10 + rand() % 40;
        if (length > MAX_COLUMN_LENGTH) length = MAX_COLUMN_LENGTH;
        size_t char_count = strlen(MATRIX_CHARS);
        if (char_count == 0) char_count = 1;
        for (int i = 0; i < length; i++) {
            chars[i] = MATRIX_CHARS[rand() % char_count];
        }
    }

    void update(float dt) {
        y += speed * dt;
        if (y - length * FONT_SIZE > height) {
            reset();
        }
        if (rand() % 3 == 0) {
            size_t char_count = strlen(MATRIX_CHARS);
            if (char_count == 0) char_count = 1;

            int idx = rand() % length;
            chars[idx] = MATRIX_CHARS[rand() % char_count];
        }
    }
};

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint program = 0, quad_vbo = 0, texture = 0;
static GLint loc_tex = -1;

pix_t* bbufpix = nullptr;
static uint8_t* rgba_buf = nullptr;

int width = 0, height = 0;
int buf_width = 0, buf_height = 0;

std::vector<Column> columns;

static const char* quad_vs =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char* quad_fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex0, vUV);\n"
    "}\n";

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
        klog("matrix: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Fullscreen quad in clip space; texture row 0 maps to the top of the window,
 * matching the top-down screen coordinates the rain and glyphs are drawn in. */
static const GLfloat quad_verts[] = {
    -1.0f,  1.0f, 0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
};

static void init_gl()
{
    GLuint vs, fs;
    GLint ok = 0;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    vs = compile_shader(GL_VERTEX_SHADER, quad_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, quad_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aUV");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        klog("matrix: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    loc_tex = glGetUniformLocation(program, "uTex0");

    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(loc_tex, 0);
}

/* (Re)allocate the CPU compositing buffers + texture storage for a new size and
 * rebuild the rain columns to match. */
static int ensure_buffer(int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    if (bbufpix != NULL && w == buf_width && h == buf_height) return 1;

    free(bbufpix);
    free(rgba_buf);
    bbufpix = (pix_t*)malloc((size_t)w * h * sizeof(pix_t));
    rgba_buf = (uint8_t*)malloc((size_t)w * h * 4);
    if (bbufpix == NULL || rgba_buf == NULL) {
        free(bbufpix); free(rgba_buf);
        bbufpix = NULL; rgba_buf = NULL;
        return 0;
    }
    width = w; height = h;
    buf_width = w; buf_height = h;

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glViewport(0, 0, w, h);

    int num_cols = w / FONT_SIZE;
    if (num_cols <= 0) num_cols = 1;
    if (num_cols > 10000) num_cols = 10000;
    columns.resize(num_cols);
    for (size_t k = 0; k < columns.size(); k++)
        columns[k].reset();
    return 1;
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

void cleanup() {
    if (font_buffer) free(font_buffer);
    free(bbufpix);
    free(rgba_buf);
    bbufpix = NULL;
    rgba_buf = NULL;
}

static int load_font_from_path(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    font_buffer = (unsigned char*)malloc(fsize);
    if (!font_buffer) {
        fclose(f);
        return 0;
    }

    size_t total_read = 0;
    while (total_read < (size_t)fsize) {
        size_t nread = fread(font_buffer + total_read, 1, (size_t)fsize - total_read, f);
        if (nread == 0) {
            break;
        }
        total_read += nread;
    }
    fclose(f);

    if (total_read != (size_t)fsize) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }

    int font_offset = stbtt_GetFontOffsetForIndex(font_buffer, 0);
    if (font_offset < 0 || !stbtt_InitFont(&font, font_buffer, font_offset)) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }

    stbtt_GetFontVMetrics(&font, &font_ascent, &font_descent, &font_linegap);
    font_scale = stbtt_ScaleForPixelHeight(&font, FONT_SIZE);
    if (font_ascent <= font_descent || font_scale <= 0.0f || font_scale > 1000.0f) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }
    is_font_loaded = 1;
    return 1;
}

int load_font() {
    static const char* font_paths[] = {
        FONT_PATH,
        "/usr/system/fonts/system.ttf",
        "/usr/system/fonts/DejaVuSansMono.ttf",
        "/usr/system/fonts/UbuntuMono-Regular.ttf",
    };

    for (size_t i = 0; i < sizeof(font_paths) / sizeof(font_paths[0]); ++i) {
        if (load_font_from_path(font_paths[i])) {
            return 1;
        }
    }
    klog("matrix: failed to load any font, rendering will stay blank\n");
    return 0;
}

static uint32_t last_frame_ms = 0;
static int frame_count = 0;
static uint32_t fps_time = 0;

static uint32_t now_ms(void) {
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    return low / 1000;   /* kernel tic is in microseconds */
}

/* Composite one animation frame into bbufpix, upload it and present it as a
 * fullscreen textured quad. */
static void render_frame(void) {
    EGLint w = 0, h = 0;

    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) { w = WIDTH; h = HEIGHT; }
    if (!ensure_buffer(w, h))
        return;

    unsigned int current_time = now_ms();
    if (last_frame_ms == 0) last_frame_ms = current_time;
    float dt = (current_time - last_frame_ms) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;  // Clamp large frame gaps to avoid visible jumps after stalls
    last_frame_ms = current_time;

    frame_count++;
    if (fps_time == 0) fps_time = current_time;
    if (current_time - fps_time > 3000) {
        klog("%d FPS (%dx%d)\n", frame_count * 1000 / (current_time - fps_time), width, height);
        frame_count = 0;
        fps_time = current_time;
    }

    long total_pixels = (long)width * height;
    for (long i = 0; i < total_pixels; ++i) {
        bbufpix[i] = 0xFF000000;
    }

    // Update once per rendered frame with the real frame time so the
    // animation stays in step with what is actually displayed.
    for (size_t ci = 0; ci < columns.size(); ci++) {
        columns[ci].update(dt);
    }

    if (is_font_loaded) {
        for (size_t ci = 0; ci < columns.size(); ci++) {

            Column& col = columns[ci];
            int col_x = (int)(ci * FONT_SIZE);
            if (col_x >= width) continue;

            for (int j = 0; j < col.length; j++) {
                int char_y = (int)(col.y - j * FONT_SIZE);
                if (char_y < -FONT_SIZE || char_y >= height + FONT_SIZE) continue;

                char c = col.chars[j];

                float brightness = 1.0f - (float)j / col.length;
                uint32_t color;
                if (j == 0) {
                    color = 0xFFFFFFFF;
                } else {
                    unsigned char g_val = (unsigned char)(brightness * 255);
                    unsigned char r_val = (unsigned char)(brightness * 50);
                    color = 0xFF000000 | ((uint32_t)r_val << 16) | ((uint32_t)g_val << 8);
                }

                GlyphCacheEntry* glyph = get_glyph_cache(c);
                if (glyph->w <= 0 || glyph->h <= 0) {
                    draw_fallback_block(col_x, char_y, FONT_SIZE, FONT_SIZE, color);
                    continue;
                }

                int baseline_y = char_y + (int)(font_ascent * font_scale) + glyph->y0;

                for (int row = 0; row < glyph->h; row++) {
                    int py = baseline_y + row;
                    if (py < 0 || py >= height) continue;

                    for (int col_idx = 0; col_idx < glyph->w; col_idx++) {
                        int px = col_x + glyph->x0 + col_idx;
                        if (px < 0 || px >= width) continue;

                        unsigned char alpha = glyph->bitmap[row * glyph->w + col_idx];
                        if (alpha > 0) {
                            long idx = ((long)py * width) + px;
                            bbufpix[idx] = color;
                        }
                    }
                }
            }
        }
    }

    render_time_overlay();

    // Convert 0xAARRGGBB words to tightly-packed RGBA8 for the texture upload.
    for (long i = 0; i < total_pixels; ++i) {
        uint32_t px = bbufpix[i];
        rgba_buf[i * 4 + 0] = (px >> 16) & 0xFF;   /* r */
        rgba_buf[i * 4 + 1] = (px >> 8)  & 0xFF;   /* g */
        rgba_buf[i * 4 + 2] = px & 0xFF;           /* b */
        rgba_buf[i * 4 + 3] = (px >> 24) & 0xFF;   /* a */
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba_buf);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (void*)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(egl_dpy, egl_surf);
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
        xwin_close(xwin);
}

static void loop(void* p) {
    (void)p;
    render_frame();
    usleep(3000);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;

    xwin_t* xwin = xwin_open(&x, -1, 32, 32, WIDTH, HEIGHT, "Matrix Rain", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin->on_event = on_event;

    /* libEGL owns on_repaint/on_resize once the window surface is created. */
    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    init_gl();
    load_font();
    init_glyph_cache();

    /* Seed the rain once the real surface size is known. */
    {
        EGLint w = WIDTH, h = HEIGHT;
        eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
        eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
        if (w <= 0 || h <= 0) { w = WIDTH; h = HEIGHT; }
        ensure_buffer(w, h);
        for (size_t i = 0; i < columns.size(); i++)
            columns[i].y = (float)(rand() % (height + height)) - height;
    }

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    cleanup();
    return 0;
}
