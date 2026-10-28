/* Host-side exact texture and GLSL derivative tests. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../libs/gles/portablegl/include/portablegl/portablegl.h"
#include "../../../libs/gles/src/glsl/glsl.h"

static glContext g_context;
static pix_t *g_back;
static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

static int nearf(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

static void check_vec4(vec4 v, float r, float g, float b, float a)
{
    CHECK(nearf(v.x, r) && nearf(v.y, g) && nearf(v.z, b) && nearf(v.w, a));
}

static void check_bytes(const u8 *p, int r, int g, int b, int a)
{
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a)
        fprintf(stderr, "pixel got=(%u,%u,%u,%u) expected=(%d,%d,%d,%d)\n",
                p[0], p[1], p[2], p[3], r, g, b, a);
    CHECK(p[0] == r && p[1] == g && p[2] == b && p[3] == a);
}

static GLuint new_texture(GLenum target)
{
    GLuint t = 0;
    pgl_GenTextures(1, &t);
    CHECK(t != 0);
    pgl_BindTexture(target, t);
    pgl_TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pgl_TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    return t;
}

static void upload_solid(GLenum target, int level, int w, int h,
                         int r, int g, int b, int a)
{
    size_t count = (size_t)w * (size_t)h;
    u8 *pixels = (u8 *)malloc(count * 4u);
    size_t i;
    CHECK(pixels != NULL);
    if (!pixels) return;
    for (i = 0; i < count; ++i) {
        pixels[i * 4 + 0] = (u8)r;
        pixels[i * 4 + 1] = (u8)g;
        pixels[i * 4 + 2] = (u8)b;
        pixels[i * 4 + 3] = (u8)a;
    }
    pgl_TexImage2D(target, level, GL_RGBA, w, h, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    CHECK(pgl_GetError() == GL_NO_ERROR);
}

static void test_mipmap_sampling(void)
{
    GLuint t = new_texture(GL_TEXTURE_2D);
    float ddx[2] = { 0.5f, 0.0f }, ddy[2] = { 0.0f, 0.0f };
    upload_solid(GL_TEXTURE_2D, 0, 4, 4, 40, 80, 120, 160);
    upload_solid(GL_TEXTURE_2D, 1, 2, 2, 200, 100, 50, 255);
    upload_solid(GL_TEXTURE_2D, 2, 1, 1, 0, 255, 0, 128);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    check_vec4(texture2D_lod(t, 0.25f, 0.25f, 0.49f), 40/255.0f, 80/255.0f, 120/255.0f, 160/255.0f);
    check_vec4(texture2D_lod(t, 0.25f, 0.25f, 0.50f), 200/255.0f, 100/255.0f, 50/255.0f, 1.0f);
    check_vec4(texture2D_lod(t, 0.25f, 0.25f, 1.50f), 0.0f, 1.0f, 0.0f, 128/255.0f);
    check_vec4(texture2D_grad(t, 0.25f, 0.25f, ddx, ddy), 200/255.0f, 100/255.0f, 50/255.0f, 1.0f);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    check_vec4(texture2D_lod(t, 0.25f, 0.25f, 1.5f),
               100/255.0f, 177.5f/255.0f, 25/255.0f, 191.5f/255.0f);
    pgl_DeleteTextures(1, &t);
}

static void test_generate_mipmap(void)
{
    static const u8 pixels[16] = {
        0, 0, 0, 0,       255, 0, 0, 255,
        0, 255, 0, 255,   0, 0, 255, 255
    };
    GLuint t = new_texture(GL_TEXTURE_2D);
    pgl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, pixels);
    pgl_GenerateMipmap(GL_TEXTURE_2D);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    CHECK(g_context.textures.a[t].levels[0][1].defined);
    CHECK(g_context.textures.a[t].levels[0][1].w == 1);
    CHECK(g_context.textures.a[t].levels[0][1].h == 1);
    check_bytes(g_context.textures.a[t].levels[0][1].data, 64, 64, 64, 191);
    pgl_DeleteTextures(1, &t);
}

static void test_incomplete_and_npot(void)
{
    GLuint t = new_texture(GL_TEXTURE_2D);
    upload_solid(GL_TEXTURE_2D, 0, 3, 3, 10, 20, 30, 40);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    check_vec4(texture2D(t, 0.5f, 0.5f), 10/255.0f, 20/255.0f, 30/255.0f, 40/255.0f);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    check_vec4(texture2D(t, 0.5f, 0.5f), 0, 0, 0, 1);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    check_vec4(texture2D(t, 0.5f, 0.5f), 0, 0, 0, 1);
    pgl_DeleteTextures(1, &t);

    t = new_texture(GL_TEXTURE_2D);
    upload_solid(GL_TEXTURE_2D, 0, 4, 4, 90, 80, 70, 60);
    pgl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    check_vec4(texture2D(t, 0.5f, 0.5f), 0, 0, 0, 1);
    pgl_DeleteTextures(1, &t);
}

static void test_cube_sampling(void)
{
    static const float dirs[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    GLuint t = new_texture(GL_TEXTURE_CUBE_MAP);
    float ddx[3] = { 0, 2, 0 }, ddy[3] = { 0, 0, 0 };
    int face;
    for (face = 0; face < 6; ++face) {
        upload_solid(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 2, 2,
                     10 + face, 20 + face, 30 + face, 255);
        upload_solid(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 1, 1, 1,
                     100 + face, 110 + face, 120 + face, 255);
    }
    pgl_TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    for (face = 0; face < 6; ++face) {
        check_vec4(texture_cubemap_lod(t, dirs[face][0], dirs[face][1], dirs[face][2], 0),
                   (10 + face)/255.0f, (20 + face)/255.0f, (30 + face)/255.0f, 1);
        check_vec4(texture_cubemap_lod(t, dirs[face][0], dirs[face][1], dirs[face][2], 1),
                   (100 + face)/255.0f, (110 + face)/255.0f, (120 + face)/255.0f, 1);
    }
    check_vec4(texture_cubemap_grad(t, 1, 0, 0, ddx, ddy), 100/255.0f, 110/255.0f, 120/255.0f, 1);
    pgl_DeleteTextures(1, &t);
}

static void etc_individual_block(u8 *block, unsigned nibble)
{
    memset(block, 0, 8);
    block[0] = block[1] = block[2] = (u8)((nibble << 4) | nibble);
}

static void test_etc_decode(void)
{
    u8 blocks[32];
    u8 rgba[16];
    u8 punch[8];
    GLuint t = new_texture(GL_TEXTURE_2D);
    int i;
    etc_individual_block(blocks + 0, 1);
    etc_individual_block(blocks + 8, 4);
    etc_individual_block(blocks + 16, 8);
    etc_individual_block(blocks + 24, 15);
    pgl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB8_ETC2,
                             5, 5, 0, sizeof(blocks), blocks);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    for (i = 0; i < 25; ++i) {
        const u8 *p = g_context.textures.a[t].levels[0][0].data + i * 4;
        int expected = (i % 5 >= 4) ? 70 : 19;
        if (i >= 20) expected = (i % 5 >= 4) ? 255 : 138;
        check_bytes(p, expected, expected, expected, 255);
    }
    pgl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                             4, 4, 0, 8, blocks + 16);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    check_bytes(g_context.textures.a[t].levels[0][0].data, 138, 138, 138, 255);

    memset(rgba, 0, sizeof(rgba));
    rgba[0] = 77;
    etc_individual_block(rgba + 8, 8);
    pgl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA8_ETC2_EAC,
                             4, 4, 0, sizeof(rgba), rgba);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    check_bytes(g_context.textures.a[t].levels[0][0].data, 138, 138, 138, 77);

    etc_individual_block(punch, 8);
    punch[5] = 0x01;
    pgl_CompressedTexImage2D(GL_TEXTURE_2D, 0,
                             GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
                             4, 4, 0, sizeof(punch), punch);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    check_bytes(g_context.textures.a[t].levels[0][0].data, 0, 0, 0, 0);
    check_bytes(g_context.textures.a[t].levels[0][0].data + 4, 140, 140, 140, 255);
    pgl_DeleteTextures(1, &t);
}

static void test_etc_subimage(void)
{
    u8 first[16], second[8];
    GLuint t = new_texture(GL_TEXTURE_2D);
    GLuint oldgen;
    etc_individual_block(first, 1);
    etc_individual_block(first + 8, 1);
    etc_individual_block(second, 15);
    pgl_CompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB8_ETC2,
                             8, 4, 0, sizeof(first), first);
    oldgen = g_context.textures.a[t].levels[0][0].datagen;
    pgl_CompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 0, 4, 4,
                                GL_COMPRESSED_RGB8_ETC2, sizeof(second), second);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    CHECK(g_context.textures.a[t].levels[0][0].datagen > oldgen);
    check_bytes(g_context.textures.a[t].levels[0][0].data, 19, 19, 19, 255);
    check_bytes(g_context.textures.a[t].levels[0][0].data + 4 * 4, 255, 255, 255, 255);
    pgl_CompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4,
                                GL_ETC1_RGB8_OES, 8, second);
    CHECK(pgl_GetError() == GL_INVALID_OPERATION);
    pgl_DeleteTextures(1, &t);
}

static void source_rgba(int x, int y, u8 out[4])
{
    int sy = g_context.back_buffer.h - 1 - y;
    if (x >= 0 && x < g_context.back_buffer.w && sy >= 0 && sy < g_context.back_buffer.h) {
        Color color = PIXEL_TO_COLOR(g_back[sy * g_context.back_buffer.w + x]);
        out[0] = color.r; out[1] = color.g; out[2] = color.b; out[3] = color.a;
    } else {
        memset(out, 0, 4);
    }
}

static void test_copy_texture(void)
{
    GLuint t;
    u8 initial[4 * 4 * 4];
    u8 expected[4];
    int x, y;
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            g_back[y * 4 + x] = RGBA_TO_PIXEL(10 + y * 4 + x, 40 + x, 80 + y, 200 + x);
    t = new_texture(GL_TEXTURE_2D);
    pgl_CopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 2, 2, 0);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    for (y = 0; y < 2; ++y)
        for (x = 0; x < 2; ++x) {
            source_rgba(x, y, expected);
            expected[3] = 255;
            check_bytes(g_context.textures.a[t].levels[0][0].data + (y * 2 + x) * 4,
                        expected[0], expected[1], expected[2], expected[3]);
        }
    memset(initial, 9, sizeof(initial));
    pgl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, initial);
    pgl_CopyTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, -1, 3, 2, 1);
    CHECK(pgl_GetError() == GL_NO_ERROR);
    check_bytes(g_context.textures.a[t].levels[0][0].data + (1 * 4 + 0) * 4, 9, 9, 9, 9);
    check_bytes(g_context.textures.a[t].levels[0][0].data + (1 * 4 + 1) * 4, 0, 0, 0, 0);
    source_rgba(0, 3, expected);
    check_bytes(g_context.textures.a[t].levels[0][0].data + (1 * 4 + 2) * 4,
                expected[0], expected[1], expected[2], expected[3]);
    pgl_DeleteTextures(1, &t);
}

typedef struct {
    int calls;
    int dim[4];
    float coord[4][3];
    float ddx[4][3];
    float ddy[4][3];
    float lod[4];
    int has_lod[4];
} sample_log;

static void record_sample(int unit, int dim, const float *coord,
                          const glsl_tex_derivatives *deriv,
                          float out[4], void *user)
{
    sample_log *log = (sample_log *)user;
    int n = log->calls;
    int i;
    CHECK(unit == 0 || unit == 1);
    if (n < 4) {
        log->dim[n] = dim;
        log->lod[n] = deriv ? deriv->lod : 0;
        log->has_lod[n] = deriv ? deriv->has_lod : 0;
        for (i = 0; i < dim; ++i) {
            log->coord[n][i] = coord[i];
            log->ddx[n][i] = deriv && deriv->ddx ? deriv->ddx[i] : 0;
            log->ddy[n][i] = deriv && deriv->ddy ? deriv->ddy[i] : 0;
        }
    }
    ++log->calls;
    out[0] = 0.25f; out[1] = 0.5f; out[2] = 0.75f; out[3] = 1.0f;
}

static glsl_program *link_program(const char *vs_source, const char *fs_source)
{
    char error[512] = { 0 };
    glsl_stage *vs = glsl_compile_stage(vs_source, 1, error, sizeof(error));
    glsl_stage *fs = glsl_compile_stage(fs_source, 0, error, sizeof(error));
    glsl_program *p;
    if (!vs || !fs) {
        fprintf(stderr, "GLSL compile: %s\n", error);
        glsl_free_stage(vs); glsl_free_stage(fs);
        ++failures;
        return NULL;
    }
    p = glsl_link(vs, fs, error, sizeof(error));
    if (!p) {
        fprintf(stderr, "GLSL link: %s\n", error);
        ++failures;
    }
    return p;
}

static void run_fragment_ex(glsl_program *p, const float *varyings,
                            const float *dx, const float *dy,
                            const float point[2], const float point_dx[2],
                            const float point_dy[2], sample_log *log, float out[4])
{
    glsl_cell *uniforms = (glsl_cell *)calloc(p->uniform_block_floats, sizeof(glsl_cell));
    float coord[4] = { 3.5f, 7.5f, 0.5f, 1.0f };
    int sampler = glsl_uniform_index(p, "uTex");
    int discard = 0, wrote = 0;
    float depth;
    CHECK(uniforms != NULL);
    if (!uniforms) return;
    if (sampler >= 0) uniforms[p->uniforms[sampler].offset].i = 0;
    glsl_run_fragment(p, varyings, dx, dy, uniforms, coord, point,
                      point_dx, point_dy, 1, record_sample, log, out,
                      &discard, &depth, &wrote);
    free(uniforms);
}

static void run_fragment(glsl_program *p, const float *varyings,
                         const float *dx, const float *dy,
                         sample_log *log, float out[4])
{
    const float point[2] = { 0, 0 };
    run_fragment_ex(p, varyings, dx, dy, point, NULL, NULL, log, out);
}

static void test_glsl_gradients(void)
{
    static const char vs[] =
        "attribute vec4 aPosition; varying vec2 vUV;"
        "void main(){gl_Position=aPosition;vUV=aPosition.xy;}";
    static const char fs[] =
        "precision mediump float; uniform sampler2D uTex; varying vec2 vUV;"
        "void main(){vec2 q=vUV*vUV;gl_FragColor=texture2D(uTex,q);}";
    glsl_program *p = link_program(vs, fs);
    float varyings[GLSL_MAX_VARYINGS * 4] = { 0 };
    float dx[GLSL_MAX_VARYINGS * 4] = { 0 };
    float dy[GLSL_MAX_VARYINGS * 4] = { 0 };
    float out[4];
    sample_log log;
    if (!p) return;
    memset(&log, 0, sizeof(log));
    varyings[0] = 0.5f; varyings[1] = 0.25f;
    dx[0] = 0.125f; dx[1] = 0.0625f;
    dy[0] = 0.03125f; dy[1] = 0.0625f;
    run_fragment(p, varyings, dx, dy, &log, out);
    CHECK(log.calls == 1 && log.dim[0] == 2 && !log.has_lod[0]);
    CHECK(nearf(log.ddx[0][0], 0.140625f) && nearf(log.ddx[0][1], 0.03515625f));
    CHECK(nearf(log.ddy[0][0], 0.0322265625f) && nearf(log.ddy[0][1], 0.03515625f));
    CHECK(nearf(out[0], 0.25f) && nearf(out[3], 1.0f));
    CHECK(p->vm_fs == NULL);
    glsl_free_program(p);
}

static void test_glsl_derivative_builtins(void)
{
    static const char vs[] =
        "attribute vec4 aPosition; varying vec2 vUV;"
        "void main(){gl_Position=aPosition;vUV=aPosition.xy;}";
    static const char fs[] =
        "precision mediump float; varying vec2 vUV;"
        "void main(){vec2 q=vUV*vUV+gl_FragCoord.xy;"
        "gl_FragColor=vec4(dFdx(q.x),dFdy(q.x),fwidth(q.x),dFdx(gl_FragCoord.y));}";
    glsl_program *p = link_program(vs, fs);
    float varyings[GLSL_MAX_VARYINGS * 4] = { 0 };
    float dx[GLSL_MAX_VARYINGS * 4] = { 0 };
    float dy[GLSL_MAX_VARYINGS * 4] = { 0 };
    float out[4];
    sample_log log;
    if (!p) return;
    memset(&log, 0, sizeof(log));
    varyings[0] = 0.5f; dx[0] = 0.125f; dy[0] = 0.03125f;
    run_fragment(p, varyings, dx, dy, &log, out);
    CHECK(nearf(out[0], 1.140625f));
    CHECK(nearf(out[1], 0.0322265625f));
    CHECK(nearf(out[2], 1.1728515625f));
    CHECK(nearf(out[3], 0.0f));
    glsl_free_program(p);
}

static void test_glsl_point_derivatives(void)
{
    static const char vs[] =
        "attribute vec4 aPosition; void main(){gl_Position=aPosition;}";
    static const char fs[] =
        "precision mediump float; uniform sampler2D uTex;"
        "vec2 coord_for(float bias){vec2 q=gl_PointCoord;"
        "if(q.x+bias>0.5)q.y=q.y*q.x;return q;}"
        "void main(){vec2 q=coord_for(0.25);vec2 r=q;"
        "gl_FragColor=texture2D(uTex,r);}";
    const float varyings[GLSL_MAX_VARYINGS * 4] = { 0 };
    const float point[2] = { 0.5f, 0.25f };
    const float point_dx[2] = { 0.125f, 0.0f };
    const float point_dy[2] = { 0.0f, -0.25f };
    glsl_program *p = link_program(vs, fs);
    float out[4];
    sample_log log;
    if (!p) return;
    memset(&log, 0, sizeof(log));
    run_fragment_ex(p, varyings, NULL, NULL, point, point_dx, point_dy, &log, out);
    CHECK(log.calls == 1 && log.dim[0] == 2 && !log.has_lod[0]);
    CHECK(nearf(log.ddx[0][0], 0.125f) && nearf(log.ddx[0][1], 0.03125f));
    CHECK(nearf(log.ddy[0][0], 0.0f) && nearf(log.ddy[0][1], -0.125f));
    glsl_free_program(p);
}

typedef struct {
    int point_calls;
    int line_calls;
    float point_dx[2], point_dy[2];
    float line_dx[4], line_dy[4];
} raster_derivative_log;

static raster_derivative_log g_raster_log;
static GLenum g_raster_mode;

static void capture_vs(float *vs_output, vec4 *vertex_attribs,
                       Shader_Builtins *builtins, void *uniforms)
{
    int i;
    (void)uniforms;
    builtins->gl_Position = vertex_attribs[0];
    for (i = 0; i < 4; ++i) vs_output[i] = (&vertex_attribs[1].x)[i];
}

static void capture_fs(float *fs_input, Shader_Builtins *builtins, void *uniforms)
{
    int i;
    (void)uniforms;
    if (g_raster_mode == GL_POINTS) {
        if (!g_raster_log.point_calls) {
            g_raster_log.point_dx[0] = builtins->gl_PointCoord_dx.x;
            g_raster_log.point_dx[1] = builtins->gl_PointCoord_dx.y;
            g_raster_log.point_dy[0] = builtins->gl_PointCoord_dy.x;
            g_raster_log.point_dy[1] = builtins->gl_PointCoord_dy.y;
        }
        ++g_raster_log.point_calls;
    } else {
        if (!g_raster_log.line_calls) {
            CHECK(builtins->fs_input_dx != NULL && builtins->fs_input_dy != NULL);
            for (i = 0; i < 4; ++i) {
                g_raster_log.line_dx[i] = builtins->fs_input_dx[i];
                g_raster_log.line_dy[i] = builtins->fs_input_dy[i];
            }
        }
        ++g_raster_log.line_calls;
    }
    SET_V4(builtins->gl_FragColor, 1, 1, 1, 1);
}

static void test_raster_derivatives(void)
{
    GLenum interpolation[4] = { PGL_SMOOTH4 };
    float point_position[4] = { 0, 0, 0, 1 };
    float point_varying[4] = { 0, 2, 4, 6 };
    float line_positions[8] = {
        -0.5f, -0.5f, 0, 1,
         0.5f,  0.5f, 0, 1
    };
    float line_varyings[8] = {
        0, 2, 4, 6,
        1, 4, 7, 10
    };
    GLuint program;
    int i;
    memset(&g_raster_log, 0, sizeof(g_raster_log));
    program = pglCreateProgram(capture_vs, capture_fs, 4, interpolation, GL_FALSE);
    CHECK(program != 0);
    pgl_UseProgram(program);
    pgl_EnableVertexAttribArray(0);
    pgl_EnableVertexAttribArray(1);
    pgl_VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, point_position);
    pgl_VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, point_varying);
    pgl_PointSize(4);
    pgl_PointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT);
    g_raster_mode = GL_POINTS;
    pgl_DrawArrays(GL_POINTS, 0, 1);
    CHECK(g_raster_log.point_calls > 0);
    CHECK(nearf(g_raster_log.point_dx[0], 0.25f) &&
          nearf(g_raster_log.point_dx[1], 0.0f));
    CHECK(nearf(g_raster_log.point_dy[0], 0.0f) &&
          nearf(g_raster_log.point_dy[1], -0.25f));

    pgl_VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, line_positions);
    pgl_VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, line_varyings);
    g_raster_mode = GL_LINES;
    pgl_DrawArrays(GL_LINES, 0, 2);
    CHECK(g_raster_log.line_calls > 0);
    for (i = 0; i < 4; ++i) {
        /* The viewport transform uses width - 0.01, so the screen delta is
         * 1.995 rather than 2 for this 4x4 target. */
        float expected = (i + 1) / 3.99f;
        CHECK(nearf(g_raster_log.line_dx[i], expected));
        CHECK(nearf(g_raster_log.line_dy[i], expected));
    }
    pgl_DisableVertexAttribArray(0);
    pgl_DisableVertexAttribArray(1);
    pgl_PointSize(1);
    pgl_PointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT);
    pgl_UseProgram(0);
    pgl_DeleteProgram(program);
}

static void test_glsl_vertex_lod(void)
{
    static const char vs[] =
        "attribute vec4 aPosition; attribute vec2 aUV;"
        "uniform sampler2D uTex; uniform samplerCube uCube; varying vec4 vColor;"
        "void main(){gl_Position=aPosition;"
        "vColor=texture2DLod(uTex,aUV,2.5)+textureCubeLod(uCube,vec3(1.0,0.0,0.0),1.25);}";
    static const char fs[] =
        "precision mediump float; varying vec4 vColor;"
        "void main(){gl_FragColor=vColor;}";
    glsl_program *p = link_program(vs, fs);
    const float *attribs[GLSL_MAX_ATTRIBS] = { 0 };
    float position[4] = { 0, 0, 0, 1 }, uv[4] = { 0.25f, 0.75f, 0, 0 };
    float varyings[GLSL_MAX_VARYINGS * 4] = { 0 }, out_position[4];
    glsl_cell *uniforms;
    sample_log log;
    int ai, ui, ci;
    if (!p) return;
    memset(&log, 0, sizeof(log));
    uniforms = (glsl_cell *)calloc(p->uniform_block_floats, sizeof(glsl_cell));
    CHECK(uniforms != NULL);
    if (!uniforms) { glsl_free_program(p); return; }
    ai = glsl_attrib_index(p, "aPosition");
    if (ai >= 0) attribs[ai] = position;
    ai = glsl_attrib_index(p, "aUV");
    if (ai >= 0) attribs[ai] = uv;
    ui = glsl_uniform_index(p, "uTex");
    ci = glsl_uniform_index(p, "uCube");
    CHECK(ui >= 0 && ci >= 0);
    if (ui >= 0) uniforms[p->uniforms[ui].offset].i = 0;
    if (ci >= 0) uniforms[p->uniforms[ci].offset].i = 1;
    glsl_run_vertex(p, attribs, GLSL_MAX_ATTRIBS, uniforms, record_sample, &log,
                    varyings, out_position, 0);
    CHECK(log.calls == 2);
    CHECK(log.dim[0] == 2 && log.has_lod[0] && nearf(log.lod[0], 2.5f));
    CHECK(log.dim[1] == 3 && log.has_lod[1] && nearf(log.lod[1], 1.25f));
    CHECK(nearf(varyings[0], 0.5f) && nearf(varyings[1], 1.0f) && nearf(varyings[3], 2.0f));
    free(uniforms);
    glsl_free_program(p);
}

int main(void)
{
    pgl_set_max_vertices(64);
    g_back = NULL;
    if (!init_glContext(&g_context, &g_back, 4, 4)) {
        fprintf(stderr, "init_glContext failed\n");
        return 1;
    }
    test_mipmap_sampling();
    test_generate_mipmap();
    test_incomplete_and_npot();
    test_cube_sampling();
    test_etc_decode();
    test_etc_subimage();
    test_copy_texture();
    test_glsl_gradients();
    test_glsl_derivative_builtins();
    test_glsl_point_derivatives();
    test_raster_derivatives();
    test_glsl_vertex_lod();
    CHECK(pgl_GetError() == GL_NO_ERROR);
    free_glContext(&g_context);
    printf("texture_test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
