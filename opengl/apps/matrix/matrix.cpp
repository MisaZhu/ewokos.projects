#include "glcommon/gltext.h"

/*
 * Matrix rain demo for EwokOS.
 *
 * The rain physics, glyph bitmaps and the clock overlay are unchanged from
 * the CPU version; what changed is the DRAWING.  Instead of compositing
 * every glyph pixel into an in-memory ARGB buffer and blitting it as a
 * fullscreen textured quad, each glyph is emitted as ONE textured quad and
 * over-blended by the GPU:
 *
 *  - Every character of MATRIX_CHARS is rasterised ONCE into a small RGBA8
 *    atlas (white RGB, one 32x32 slot per char, plus one fully opaque slot
 *    for the no-font fallback block).  The painter's "alpha > 0 -> write
 *    the colour" test is baked in as a hard threshold, so slot alpha is
 *    binary and the texcol kernel's fixed SRC_ALPHA / ONE_MINUS_SRC_ALPHA
 *    blend degrades to the painter's exact overwrite: src = texel * vColor,
 *    so the vertex colour IS the glyph colour.  The quad's uv span lands
 *    each of its pixel centres on the exact texel centre, so the kernel's
 *    fetch is an exact lookup with no bleed between slots.
 *
 *  - Draw order is the painter's: every rain column in order (head white,
 *    the trail dimming toward the tail), then the clock overlay on top.
 *    The kernel's CAS-retried blend RMW keeps overlapping quads from losing
 *    contributions (glyph boxes can touch between adjacent cells); where
 *    two different-colour quads race on one pixel the resolved order is
 *    arbitrary, which only affects single-pixel overlaps at cell edges.
 *
 * On PGL_G3D targets the shader pair has the exact imgui texcol shape
 * (gl_FragColor = vColor * texture2D(uTexture, vUV)), which the engine
 * routes through the one-workgroup-per-triangle texcol raster kernel;
 * without a GPU it runs on the CPU interpreter with identical pixels.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include <vector>

#include <unistd.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

#define WIDTH 640
#define HEIGHT 480
#define FONT_SIZE x_hidpi_size(14)
#define FONT_PATH "/usr/system/fonts/Hack-Regular.ttf"

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

GlyphCacheEntry* get_glyph_cache(char c) {
    unsigned char uc = (unsigned char)c;
    GlyphCacheEntry* entry = &glyph_cache[uc];
    if (entry->valid) {
        return entry;
    }

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

char get_random_matrix_char() {
    size_t char_count = strlen(MATRIX_CHARS);
    if (char_count == 0) return 'A';
    return MATRIX_CHARS[rand() % char_count];
}

/* Logical render size (the engine back buffer): the rain grid and the clock
 * overlay are laid out against it. */
static int width = 0, height = 0;

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
        if (rand() % 3 != 0) {   /* mutate twice as often as before (2/3 of frames) */
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

static GLuint program = 0, vbo = 0, ibo = 0, atlas = 0;
static GLint loc_mvp = -1, loc_tex = -1;

static std::vector<Column> columns;

/* Interleaved quad vertex: position (top-down pixel coords), atlas uv,
 * straight rgb + per-quad alpha.  Layout matches the texcol staging
 * contract: attr0 = pos vec2, attr1 = uv vec2, attr2 = rgba. */
typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} FWVert;

/* Per-frame emission ceiling: one 4-vertex quad per glyph.  65536 quads is
 * far above the worst case (a 4K-wide surface holds ~275 columns x ~36
 * visible glyphs ~= 10K quads plus the clock); at impossible overloads the
 * last quads are simply skipped instead of overflowing the stream. */
#define MAX_QUADS 65536
/* PGL_G3D_MAX_VERTS: the engine's per-glDrawElements ceiling on the texcol
 * route (20480 expanded vertices, one per index).  6 indices per quad ->
 * 3413 quads per draw (20478 indices). */
#define GPU_CHUNK_VERTS 20480
#define QUADS_PER_CHUNK (GPU_CHUNK_VERTS / 6)

/* The atlas grid: one SLOT x SLOT slot per MATRIX_CHARS char (87 chars fit
 * in 128 slots), the LAST slot fully opaque for the fallback block.  The
 * quad's uv span [t/N, (t+w)/N] lands every pixel centre on the exact texel
 * centre, so the kernel's fetch (nearest or bilinear) degrades to an exact
 * lookup with no bleed between slots. */
#define SLOT 32
#define ATLAS_COLS 16
#define ATLAS_ROWS 8
#define ATLAS_W (ATLAS_COLS * SLOT)
#define ATLAS_H (ATLAS_ROWS * SLOT)
#define SLOT_SOLID ((ATLAS_COLS * ATLAS_ROWS) - 1)

/* char -> atlas slot (unlisted chars fall back to slot 0). */
static unsigned char char_slot[128];

/* The imgui texcol pair: on PGL_G3D targets the engine detects
 * gl_FragColor = vColor * texture2D(uTexture, vUV) and routes the draw
 * through the one-workgroup-per-triangle texcol raster kernel, which
 * multiplies the slot texel by the interpolated vertex colour and
 * over-blends with SRC_ALPHA - the painter's exact per-pixel equation.  The
 * CPU interpreter compiles the same sources for the fallback with identical
 * pixels.  The ortho uMVP maps pixel (x, y) - y down, matching the top-down
 * screen coordinates the rain uses - to NDC. */
static const char *part_vs =
    "attribute vec2 aPosition;\n"
    "attribute vec2 aUV;\n"
    "attribute vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    vColor = aColor;\n"
    "    gl_Position = uMVP * vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

static const char *part_fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "varying vec4 vColor;\n"
    "uniform sampler2D uTexture;\n"
    "void main() {\n"
    "    gl_FragColor = vColor * texture2D(uTexture, vUV);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src)
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

/* Build the glyph atlas once: rasterise every MATRIX_CHARS char into its
 * 32x32 slot (white RGB, binary alpha = the painter's "alpha > 0 -> write"
 * test), and fill the solid slot used by the no-font fallback block. */
static void build_atlas(unsigned char out[ATLAS_W * ATLAS_H * 4])
{
    int slot = 0;

    memset(out, 0, (size_t)ATLAS_W * ATLAS_H * 4);
    memset(char_slot, 0, sizeof(char_slot));

    for (const char *p = MATRIX_CHARS; *p; p++) {
        unsigned char uc = (unsigned char)*p;
        GlyphCacheEntry *g = get_glyph_cache(*p);
        if (uc < 128) char_slot[uc] = (unsigned char)slot;
        if (g->w > 0 && g->h > 0 && g->w <= SLOT && g->h <= SLOT) {
            int ox = (slot % ATLAS_COLS) * SLOT;
            int oy = (slot / ATLAS_COLS) * SLOT;
            for (int gy = 0; gy < g->h; gy++) {
                for (int gx = 0; gx < g->w; gx++) {
                    if (g->bitmap[gy * g->w + gx] > 0) {
                        unsigned char *px = out + ((size_t)(oy + gy) * ATLAS_W + (ox + gx)) * 4;
                        px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 255;
                    }
                }
            }
        }
        slot++;
    }

    /* Solid slot: fully opaque white, so a block of any size samples only
     * uniformly opaque texels (the bilinear fetch stays exact). */
    {
        int ox = (SLOT_SOLID % ATLAS_COLS) * SLOT;
        int oy = (SLOT_SOLID / ATLAS_COLS) * SLOT;
        for (int gy = 0; gy < SLOT; gy++) {
            for (int gx = 0; gx < SLOT; gx++) {
                unsigned char *px = out + ((size_t)(oy + gy) * ATLAS_W + (ox + gx)) * 4;
                px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 255;
            }
        }
    }
}

static void unpack_color(uint32_t color, float* r, float* g, float* b)
{
    *r = (float)((color >> 16) & 0xFF) / 255.0f;
    *g = (float)((color >>  8) & 0xFF) / 255.0f;
    *b = (float)( color        & 0xFF) / 255.0f;
}

/* Append one quad (4 verts, 6 indices) to the stream.  Returns 0 when the
 * stream is full. */
static int emit_quad(float x, float y, float x1, float y1,
                     float u0, float v0, float u1, float v1,
                     float r, float g, float b, FWVert* vs, int* nq)
{
    FWVert q[4];

    if (*nq >= MAX_QUADS) return 0;

    q[0].x = x;  q[0].y = y;  q[0].u = u0; q[0].v = v0;
    q[1].x = x1; q[1].y = y;  q[1].u = u1; q[1].v = v0;
    q[2].x = x1; q[2].y = y1; q[2].u = u1; q[2].v = v1;
    q[3].x = x;  q[3].y = y1; q[3].u = u0; q[3].v = v1;
    for (int i = 0; i < 4; i++) {
        q[i].r = r; q[i].g = g; q[i].b = b; q[i].a = 1.0f;
    }
    memcpy(vs + (size_t)*nq * 4, q, sizeof(q));
    (*nq)++;
    return 1;
}

/* Solid-colour block quad over the opaque atlas slot (the no-font /
 * oversized-glyph fallback). */
static int emit_solid_quad(int x, int y, int w, int h,
                           float r, float g, float b, FWVert* vs, int* nq)
{
    if (w <= 0 || h <= 0) return 1;
    {
        float u0 = (float)((SLOT_SOLID % ATLAS_COLS) * SLOT) / (float)ATLAS_W;
        float u1 = (float)((SLOT_SOLID % ATLAS_COLS) * SLOT + SLOT) / (float)ATLAS_W;
        float v0 = (float)((SLOT_SOLID / ATLAS_COLS) * SLOT) / (float)ATLAS_H;
        float v1 = (float)((SLOT_SOLID / ATLAS_COLS) * SLOT + SLOT) / (float)ATLAS_H;
        return emit_quad((float)x, (float)y, (float)(x + w), (float)(y + h),
                         u0, v0, u1, v1, r, g, b, vs, nq);
    }
}

/* One glyph quad: the atlas slot's w x h ink region anchored at the same
 * pixel the painter's x0/y0 offsets chose.  Glyphs larger than a slot
 * (impossible at FONT_SIZE <= 32) fall back to the solid block. */
static int emit_glyph_quad(int x, int y, int w, int h, unsigned char slot,
                           float r, float g, float b, FWVert* vs, int* nq)
{
    if (w <= 0 || h <= 0) return 1;
    if (w > SLOT || h > SLOT)
        return emit_solid_quad(x, y, w, h, r, g, b, vs, nq);
    {
        float u0 = (float)((slot % ATLAS_COLS) * SLOT) / (float)ATLAS_W;
        float u1 = (float)((slot % ATLAS_COLS) * SLOT + w) / (float)ATLAS_W;
        float v0 = (float)((slot / ATLAS_COLS) * SLOT) / (float)ATLAS_H;
        float v1 = (float)((slot / ATLAS_COLS) * SLOT + h) / (float)ATLAS_H;
        return emit_quad((float)x, (float)y, (float)(x + w), (float)(y + h),
                         u0, v0, u1, v1, r, g, b, vs, nq);
    }
}

/* The painter's fallback block (centred 3/5 x 4/5 of the cell), as a solid
 * quad. */
static int emit_fallback_block(int x, int y, int cell_w, int cell_h,
                               float r, float g, float b, FWVert* vs, int* nq)
{
    int block_w = cell_w * 3 / 5;
    int block_h = cell_h * 4 / 5;
    if (block_w < 2) block_w = 2;
    if (block_h < 2) block_h = 2;
    if (block_w > cell_w) block_w = cell_w;
    if (block_h > cell_h) block_h = cell_h;

    int start_x = x + (cell_w - block_w) / 2;
    int start_y = y + (cell_h - block_h) / 2;
    return emit_solid_quad(start_x, start_y, block_w, block_h, r, g, b, vs, nq);
}

/* Emit one clock cell as a random glyph quad (the painter's random char per
 * cell per frame). */
static int emit_matrix_cell(int screen_x, int screen_y, int cell_size,
                            float r, float g, float b, FWVert* vs, int* nq)
{
    if (!is_font_loaded)
        return emit_fallback_block(screen_x, screen_y, cell_size, cell_size,
                                   r, g, b, vs, nq);
    {
        char c = get_random_matrix_char();
        GlyphCacheEntry* glyph = get_glyph_cache(c);
        if (glyph->w <= 0 || glyph->h <= 0)
            return emit_fallback_block(screen_x, screen_y, cell_size, cell_size,
                                       r, g, b, vs, nq);
        {
            int baseline_y = screen_y + (int)(font_ascent * font_scale) + glyph->y0;
            return emit_glyph_quad(screen_x + glyph->x0, baseline_y,
                                   glyph->w, glyph->h,
                                   char_slot[(unsigned char)c], r, g, b, vs, nq);
        }
    }
}

static int render_digit_with_chars(int digit, int x, int y, int char_scale,
                                   uint32_t color, FWVert* vs, int* nq)
{
    const char* pattern = DIGIT_PATTERNS[digit];
    float r, g, b;

    unpack_color(color, &r, &g, &b);
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        for (int col = 0; col < DIGIT_WIDTH; col++) {
            if (pattern[row * DIGIT_WIDTH + col] == '#') {
                int screen_x = x + col * char_scale;
                int screen_y = y + row * char_scale;
                if (!emit_matrix_cell(screen_x, screen_y, char_scale,
                                      r, g, b, vs, nq))
                    return 0;
            }
        }
    }
    return 1;
}

static int render_colon_with_chars(int x, int y, int char_scale,
                                   uint32_t color, FWVert* vs, int* nq)
{
    float r, g, b;

    unpack_color(color, &r, &g, &b);
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        for (int col = 0; col < DIGIT_WIDTH; col++) {
            if (COLON_PATTERN[row * DIGIT_WIDTH + col] == '#') {
                int screen_x = x + col * char_scale;
                int screen_y = y + row * char_scale;
                if (!emit_matrix_cell(screen_x, screen_y, char_scale,
                                      r, g, b, vs, nq))
                    return 0;
            }
        }
    }
    return 1;
}

static int render_matrix_time(const char* time_str, int start_x, int start_y,
                              int char_scale, uint32_t color,
                              FWVert* vs, int* nq)
{
    int x = start_x;
    int digit_w = DIGIT_WIDTH * char_scale;
    int spacing = DIGIT_SPACING * char_scale;

    for (int i = 0; time_str[i] != 0; i++) {
        char c = time_str[i];
        if (c == ':') {
            if (!render_colon_with_chars(x, start_y, char_scale, color, vs, nq))
                return 0;
            x += digit_w + spacing;
        } else if (c >= '0' && c <= '9') {
            if (!render_digit_with_chars(c - '0', x, start_y, char_scale,
                                         color, vs, nq))
                return 0;
            x += digit_w + spacing;
        }
    }
    return 1;
}

/* Emit the central clock overlay (drawn AFTER the rain, the painter's
 * order). */
static void render_time_overlay(FWVert* vs, int* nq)
{
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

    render_matrix_time(time_str, time_x, time_y, char_scale, 0xFF00FF00, vs, nq);
}

/* ------------------------------------------------------------------ *
 * Dear ImGui overlay: the FPS / GPU-vs-CPU readout as a small read-only
 * ImGui window drawn on top of the rain, the same overlay glmatrix uses.
 * ImGui is C++ and renders through the GLES2 backend after the glyph pass.
 * The HUD is display-only, so no xwin input is forwarded to it and the
 * window is marked NoInputs; the central matrix clock overlay is untouched
 * and ESC still quits.
 * ------------------------------------------------------------------ */
static int fps = 0;
static int fps_count = 0;
static uint32_t fps_last_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (fps_last_tic == 0 || (low - fps_last_tic) >= 1000000) {
        fps_last_tic = low;
        fps = fps_count;
        fps_count = 0;
    }
    fps_count++;
}

static void init_imgui(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                                 /* no imgui.ini    */
    io.DisplaySize = ImVec2((float)WIDTH, (float)HEIGHT);
    ImGui::StyleColorsDark();
    ImGui_ImplGLES2_Init();
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

/* Build one ImGui frame: lay out the read-only stats window.  `gpu` is the
 * route the glyph program used this frame, queried while it was bound.  The
 * HUD is display-only (FPS / Renderer), so no input is forwarded to ImGui and
 * the window is marked NoInputs. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Matrix", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::End();
}

static void init_gl()
{
    GLuint vs, fs;
    GLint ok = 0;

    glDisable(GL_DEPTH_TEST);

    vs = compile_shader(GL_VERTEX_SHADER, part_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, part_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    /* Pin the attribute slots: the GPU staging contract reads attr0 =
     * position (vec2), attr1 = uv (vec2), attr2 = colour (vec4). */
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aUV");
    glBindAttribLocation(program, 2, "aColor");
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

    loc_mvp = glGetUniformLocation(program, "uMVP");
    loc_tex = glGetUniformLocation(program, "uTexture");

    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);

    /* Glyph atlas on texture unit 0, uploaded AFTER the link: the RGBA8
     * upload registers the engine's raster texture slot (gpu_on_texture),
     * which needs a GPU-eligible program for gpu_bringup to have run. */
    glGenTextures(1, &atlas);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    {
        unsigned char* atlas_pix =
            (unsigned char*)malloc((size_t)ATLAS_W * ATLAS_H * 4);
        if (atlas_pix) {
            build_atlas(atlas_pix);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_W, ATLAS_H, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, atlas_pix);
            free(atlas_pix);
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Fixed index pattern: quad q -> verts q*4+0..3, two triangles.  Built
     * once for the whole MAX_QUADS stream; each frame draws only the used
     * prefix. */
    {
        GLuint* indices = (GLuint*)malloc((size_t)MAX_QUADS * 6 * sizeof(GLuint));
        if (indices) {
            for (int q = 0; q < MAX_QUADS; q++) {
                GLuint b = (GLuint)q * 4;
                indices[q * 6 + 0] = b + 0;
                indices[q * 6 + 1] = b + 1;
                indices[q * 6 + 2] = b + 2;
                indices[q * 6 + 3] = b + 0;
                indices[q * 6 + 4] = b + 2;
                indices[q * 6 + 5] = b + 3;
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         (GLsizeiptr)MAX_QUADS * 6 * sizeof(GLuint),
                         indices, GL_STATIC_DRAW);
            free(indices);
        }
    }

    init_imgui();
}

static FWVert* vertbuf = NULL;

/* (Re)build the rain grid for a new render size.  The vertex stream is
 * size-independent (MAX_QUADS), so only the column list changes. */
static int ensure_grid(int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    if (vertbuf == NULL) {
        vertbuf = (FWVert*)malloc((size_t)MAX_QUADS * 4 * sizeof(FWVert));
        if (vertbuf == NULL) return 0;
    }
    if (w == width && h == height) return 1;

    width = w; height = h;

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
    free(vertbuf);
    vertbuf = NULL;
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

static uint32_t now_ms(void) {
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    return low / 1000;   /* kernel tic is in microseconds */
}

/* Emit every visible rain glyph as ONE textured quad, in the painter's
 * column-major order (head white, the trail dimming toward the tail). */
static void emit_rain(FWVert* vs, int* nq)
{
    for (size_t ci = 0; ci < columns.size(); ci++) {
        Column& col = columns[ci];
        int col_x = (int)(ci * FONT_SIZE);
        if (col_x >= width) continue;

        for (int j = 0; j < col.length; j++) {
            int char_y = (int)(col.y - j * FONT_SIZE);
            if (char_y < -FONT_SIZE || char_y >= height + FONT_SIZE) continue;

            float brightness = 1.0f - (float)j / col.length;
            float r, g, b;
            if (j == 0) {
                r = 1.0f; g = 1.0f; b = 1.0f;
            } else {
                r = brightness * 50.0f / 255.0f;
                g = brightness;
                b = 0.0f;
            }

            GlyphCacheEntry* glyph = get_glyph_cache(col.chars[j]);
            if (glyph->w <= 0 || glyph->h <= 0) {
                if (!emit_fallback_block(col_x, char_y, FONT_SIZE, FONT_SIZE,
                                         r, g, b, vs, nq))
                    return;
                continue;
            }

            {
                int baseline_y = char_y + (int)(font_ascent * font_scale) + glyph->y0;
                if (!emit_glyph_quad(col_x + glyph->x0, baseline_y,
                                     glyph->w, glyph->h,
                                     char_slot[(unsigned char)col.chars[j]],
                                     r, g, b, vs, nq))
                    return;
            }
        }
    }
}

/* Composite one animation frame: emit every glyph as ONE textured quad,
 * clear the back buffer to black and over-blend the quads in emission
 * order, then draw the ImGui HUD on top and present. */
static void render_frame(void) {
    EGLint w = 0, h = 0;
    int nquad = 0;

    /* Query the render size (stride-aligned on q8b, == window elsewhere) so
     * the ortho uMVP / glViewport / DisplaySize match the engine back
     * buffer. */
    eglQuerySurface(egl_dpy, egl_surf, EGL_RENDER_WIDTH_EWOK, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_RENDER_HEIGHT_EWOK, &h);
    if (w <= 0 || h <= 0) { w = WIDTH; h = HEIGHT; }
    if (!ensure_grid(w, h))
        return;

    unsigned int current_time = now_ms();
    if (last_frame_ms == 0) last_frame_ms = current_time;
    float dt = (current_time - last_frame_ms) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;  // Clamp large frame gaps to avoid visible jumps after stalls
    last_frame_ms = current_time;

    // Update once per rendered frame with the real frame time so the
    // animation stays in step with what is actually displayed.
    for (size_t ci = 0; ci < columns.size(); ci++) {
        columns[ci].update(dt);
    }

    /* Re-assert every state cap per frame: ImGui's GLES2 backend installs its
     * own blend/scissor state while rendering the HUD and does not fully
     * restore it.  Scissor and the colour mask must be default at glClear,
     * or the engine pins the frame's clear (and with it the whole frame) to
     * the CPU path. */
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Over compositing with binary slot alpha reproduces the painter's
     * overwrite exactly (covered pixel -> vertex colour, uncovered -> dst).
     * The texcol kernel's fixed blend IS the imgui BlendFuncSeparate shape
     * the engine's route gate requires: SRC_ALPHA/ONE_MINUS_SRC_ALPHA on RGB
     * and ONE/ONE_MINUS_SRC_ALPHA on alpha; the CPU interpreter uses the
     * same factors for identical pixels. */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program);
    {
        /* Column-major ortho: pixel (x, y), y down -> NDC. */
        float m[16] = {
            2.0f / (float)w, 0.0f,            0.0f, 0.0f,
            0.0f,           -2.0f / (float)h, 0.0f, 0.0f,
            0.0f,           0.0f,            1.0f, 0.0f,
           -1.0f,           1.0f,            0.0f, 1.0f,
        };
        glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, m);
        if (loc_tex >= 0) glUniform1i(loc_tex, 0);
    }
    /* The texcol draw route stages the texture bound to unit 0. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    nquad = 0;
    emit_rain(vertbuf, &nquad);
    render_time_overlay(vertbuf, &nquad);

    if (nquad > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nquad * 4 * sizeof(FWVert),
                     vertbuf, GL_STREAM_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(FWVert),
                              (void*)offsetof(FWVert, x));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FWVert),
                              (void*)offsetof(FWVert, u));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(FWVert),
                              (void*)offsetof(FWVert, r));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);

        /* Submit in <= QUADS_PER_CHUNK chunks (the engine's per-draw cap is
         * 20480 expanded vertices, one per index; 6 indices per quad). */
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        for (int q = 0; q < nquad; q += QUADS_PER_CHUNK) {
            int nq = nquad - q;
            if (nq > QUADS_PER_CHUNK) nq = QUADS_PER_CHUNK;
            glDrawElements(GL_TRIANGLES, nq * 6, GL_UNSIGNED_INT,
                           (void*)((size_t)q * 6 * sizeof(GLuint)));
        }
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(0);
    }

    /* Query the GPU/CPU route while the glyph program is still bound, then
     * draw the ImGui stats window on top (same overlay as glmatrix). */
    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
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

    /* The font must be ready before init_gl builds the glyph atlas. */
    load_font();
    init_glyph_cache();

    /* The glyph program has the engine's texcol shape (gl_FragColor =
     * vColor * texture2D(uTexture, vUV)), so on a PGL_G3D platform the whole
     * show (rain + clock + ImGui overlay) draws on the GPU through the
     * one-workgroup-per-triangle texcol kernel; on the CPU-only platforms
     * the interpreter reproduces it pixel-identically.  No glForceCpuEWOK:
     * that pin was for the old CPU-composited blit. */
    init_gl();

    /* Seed the rain once the real surface size is known. */
    {
        EGLint w = WIDTH, h = HEIGHT;
        eglQuerySurface(egl_dpy, egl_surf, EGL_RENDER_WIDTH_EWOK, &w);
        eglQuerySurface(egl_dpy, egl_surf, EGL_RENDER_HEIGHT_EWOK, &h);
        if (w <= 0 || h <= 0) { w = WIDTH; h = HEIGHT; }
        ensure_grid(w, h);
        for (size_t i = 0; i < columns.size(); i++)
            columns[i].y = (float)(rand() % (height + height)) - height;
    }

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    cleanup();
    return 0;
}
