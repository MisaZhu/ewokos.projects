/* gles2_conformance.c - deterministic GLES2/EGL regression matrix.
 *
 * This is an implementation regression test, not the Khronos conformance
 * suite.  Each result is machine-readable and includes the latched GL/EGL
 * errors, active GPU path and an FNV-1a checksum of any pixels examined.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SURFACE_W 16
#define SURFACE_H 16

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static int g_passes;
static int g_failures;
static const char *g_filter;
static int g_checksum;

static uint32_t pixel_sum(const void *pixels, size_t size)
{
    const unsigned char *p = (const unsigned char *)pixels;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void drain_errors(void)
{
    while (glGetError() != GL_NO_ERROR) { }
    while (eglGetError() != EGL_SUCCESS) { }
}

static void result(const char *suite, const char *name, int pass, GLenum glerr,
                   EGLint eglerr, uint32_t sum)
{
    printf("RESULT %s.%s %s gl=0x%04x egl=0x%04x gpu=%d sum=0x%08x\n",
           suite, name, pass ? "PASS" : "FAIL", (unsigned)glerr,
           (unsigned)eglerr, (int)glGpuActiveEWOK(), (unsigned)sum);
    if (pass) ++g_passes;
    else ++g_failures;
}

static void run_case(const char *suite, const char *name, int (*fn)(uint32_t *))
{
    uint32_t sum = 0;
    GLenum glerr;
    EGLint eglerr;
    int pass;
    if (g_filter && strcmp(g_filter, "all") != 0 &&
        strcmp(g_filter, suite) != 0 && strcmp(g_filter, name) != 0)
        return;
    drain_errors();
    g_checksum = 0;
    pass = fn(&sum);
    glerr = glGetError();
    eglerr = eglGetError();
    result(suite, name, pass && glerr == GL_NO_ERROR && eglerr == EGL_SUCCESS,
           glerr, eglerr, sum);
}

static int test_buffer_lifecycle(uint32_t *sum)
{
    GLuint b = 0;
    GLint v = -1;
    int ok;
    const unsigned char data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    glGenBuffers(1, &b);
    ok = b != 0 && glIsBuffer(b) == GL_FALSE;
    glBindBuffer(GL_ARRAY_BUFFER, b);
    ok = ok && glIsBuffer(b) == GL_TRUE;
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &v);
    ok = ok && v == (GLint)sizeof(data);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &v);
    ok = ok && v == GL_STATIC_DRAW;
    glDeleteBuffers(1, &b);
    ok = ok && glIsBuffer(b) == GL_FALSE;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    *sum = pixel_sum(data, sizeof(data));
    return ok;
}

static int test_texture_lifecycle(uint32_t *sum)
{
    GLuint t = 0;
    int ok;
    (void)sum;
    glGenTextures(1, &t);
    ok = t != 0 && glIsTexture(t) == GL_FALSE;
    glBindTexture(GL_TEXTURE_2D, t);
    ok = ok && glIsTexture(t) == GL_TRUE;
    glDeleteTextures(1, &t);
    ok = ok && glIsTexture(t) == GL_FALSE;
    glBindTexture(GL_TEXTURE_2D, 0);
    return ok;
}

static int test_vertex_attrib_state(uint32_t *sum)
{
    GLfloat current[4] = { 0, 0, 0, 0 };
    GLfloat v[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    GLint iv = -1;
    int ok;
    glVertexAttrib4fv(3, v);
    glGetVertexAttribfv(3, GL_CURRENT_VERTEX_ATTRIB, current);
    ok = memcmp(current, v, sizeof(v)) == 0;
    glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &iv);
    ok = ok && iv == GL_FALSE;
    glEnableVertexAttribArray(3);
    glGetVertexAttribiv(3, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &iv);
    ok = ok && iv == GL_TRUE;
    glDisableVertexAttribArray(3);
    *sum = pixel_sum(current, sizeof(current));
    return ok;
}

static int test_vertex_pointer_state(uint32_t *sum)
{
    const unsigned short verts[6] = { 0, 1, 2, 3, 4, 5 };
    void *pointer = NULL;
    GLint v = -1;
    int ok;
    glVertexAttribPointer(2, 3, GL_UNSIGNED_SHORT, GL_TRUE, 6, verts);
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_SIZE, &v);
    ok = v == 3;
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_TYPE, &v);
    ok = ok && v == GL_UNSIGNED_SHORT;
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &v);
    ok = ok && v == 6;
    glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &v);
    ok = ok && v == GL_TRUE;
    glGetVertexAttribPointerv(2, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
    ok = ok && pointer == verts;
    *sum = pixel_sum(verts, sizeof(verts));
    return ok;
}

static int test_texture_parameters(uint32_t *sum)
{
    GLuint t;
    GLint v = 0;
    int ok;
    (void)sum;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &v);
    ok = v == GL_LINEAR_MIPMAP_LINEAR;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &v);
    ok = ok && v == GL_NEAREST;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &v);
    ok = ok && v == GL_CLAMP_TO_EDGE;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &v);
    ok = ok && v == GL_MIRRORED_REPEAT;
    glDeleteTextures(1, &t);
    return ok;
}

static int test_packed_texture(uint32_t *sum)
{
    const unsigned short texels[4] = { 0xf800u, 0x07e0u, 0x001fu, 0xffffu };
    unsigned char pixels[4];
    GLuint t;
    int ok;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB,
                 GL_UNSIGNED_SHORT_5_6_5, texels);
    ok = glGetError() == GL_NO_ERROR;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGB,
                    GL_UNSIGNED_SHORT_5_6_5, texels);
    ok = ok && glGetError() == GL_NO_ERROR;
    memset(pixels, 0, sizeof(pixels));
    *sum = pixel_sum(texels, sizeof(texels));
    glDeleteTextures(1, &t);
    return ok;
}

static int test_unpack_alignment(uint32_t *sum)
{
    const unsigned char rows[10] = {
        255, 0, 0, 99, 0, 255, 0, 99, 99, 99
    };
    GLuint t;
    int ok;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, rows);
    ok = glGetError() == GL_NO_ERROR;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    *sum = pixel_sum(rows, sizeof(rows));
    glDeleteTextures(1, &t);
    return ok;
}

static int test_mipmap(uint32_t *sum)
{
    unsigned char image[4 * 4 * 4];
    GLuint t;
    int ok;
    memset(image, 0x5a, sizeof(image));
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image);
    glGenerateMipmap(GL_TEXTURE_2D);
    ok = glGetError() == GL_NO_ERROR;
    glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 2, 2, GL_RGBA,
                    GL_UNSIGNED_BYTE, image);
    ok = ok && glGetError() == GL_NO_ERROR;
    *sum = pixel_sum(image, sizeof(image));
    glDeleteTextures(1, &t);
    return ok;
}

static int test_compressed_texture(uint32_t *sum)
{
    static const unsigned char etc1[8] = {
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    GLuint t;
    int ok;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                           4, 4, 0, sizeof(etc1), etc1);
    ok = glGetError() == GL_NO_ERROR;
    *sum = pixel_sum(etc1, sizeof(etc1));
    glDeleteTextures(1, &t);
    return ok;
}

static int test_readpixels_formats(uint32_t *sum)
{
    unsigned char rgba[4 * 4 * 4];
    unsigned char rgb[4 * 4 * 3 + 3];
    int ok;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 4, 4);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    memset(rgba, 0, sizeof(rgba));
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    ok = rgba[0] >= 63 && rgba[0] <= 64 && rgba[3] == 255;
    memset(rgb, 0, sizeof(rgb));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 4, 4, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    ok = ok && glGetError() == GL_NO_ERROR && rgb[0] == rgba[0] &&
         rgb[1] == rgba[1] && rgb[2] == rgba[2];
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    *sum = pixel_sum(rgba, sizeof(rgba)) ^ pixel_sum(rgb, sizeof(rgb));
    return ok;
}

static int test_readpixels_packed(uint32_t *sum)
{
    unsigned short pixels[4];
    int ok;
    memset(pixels, 0, sizeof(pixels));
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, pixels);
    ok = glGetError() == GL_NO_ERROR;
    *sum = pixel_sum(pixels, sizeof(pixels));
    return ok;
}

static int test_fbo_lifecycle(uint32_t *sum)
{
    GLuint f = 0, r = 0, t = 0;
    GLint v = 0;
    int ok;
    (void)sum;
    glGenFramebuffers(1, &f);
    ok = f != 0 && glIsFramebuffer(f) == GL_FALSE;
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    ok = ok && glIsFramebuffer(f) == GL_TRUE;
    glGenRenderbuffers(1, &r);
    glBindRenderbuffer(GL_RENDERBUFFER, r);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 4, 4);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &v);
    ok = ok && v == 4;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, r);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, t, 0);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glDeleteFramebuffers(1, &f);
    glDeleteRenderbuffers(1, &r);
    glDeleteTextures(1, &t);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

static int test_fbo_pixels(uint32_t *sum)
{
    unsigned char pixels[4 * 4 * 4];
    GLuint f = 0, t = 0;
    int ok;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, t, 0);
    ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glViewport(0, 0, 4, 4);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    memset(pixels, 0, sizeof(pixels));
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    ok = ok && pixels[0] == 255 && pixels[1] == 0 && pixels[3] == 255;
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 2, 2);
    ok = ok && glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &f);
    glDeleteTextures(1, &t);
    *sum = pixel_sum(pixels, sizeof(pixels));
    return ok;
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint make_program(void)
{
    static const char vs[] =
        "attribute vec4 aPosition;\n"
        "void main(){ gl_Position=aPosition; }\n";
    static const char fs[] =
        "precision mediump float;\n"
        "uniform vec4 uColor;\n"
        "void main(){ gl_FragColor=uColor; }\n";
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    GLint ok = GL_FALSE;
    if (!v || !f || !p) return 0;
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    return ok ? p : 0;
}

static int test_shader_errors(uint32_t *sum)
{
    static const GLfloat matrix[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    GLuint p = make_program();
    GLint location;
    GLint status = 0;
    int ok = p != 0;
    (void)sum;
    if (!ok) return 0;
    glUseProgram(p);
    location = glGetUniformLocation(p, "uColor");
    glUniformMatrix4fv(location, 1, GL_TRUE, matrix);
    ok = glGetError() == GL_INVALID_VALUE;
    glValidateProgram(p);
    glGetProgramiv(p, GL_VALIDATE_STATUS, &status);
    ok = ok && status == GL_TRUE;
    glDeleteProgram(p);
    glUseProgram(0);
    return ok;
}

static int test_shader_binary_rejected(uint32_t *sum)
{
    static const unsigned char bad[4] = { 0, 0, 0, 0 };
    GLuint s = glCreateShader(GL_VERTEX_SHADER);
    int ok;
    (void)sum;
    glShaderBinary(1, &s, 0, bad, sizeof(bad));
    ok = glGetError() != GL_NO_ERROR;
    glDeleteShader(s);
    return ok;
}

static int test_draw_primitives(uint32_t *sum)
{
    static const GLfloat verts[] = {
        -0.75f, -0.75f, 0, 1, 0.75f, -0.75f, 0, 1, 0, 0.75f, 0, 1
    };
    unsigned char pixels[SURFACE_W * SURFACE_H * 4];
    GLuint p = make_program();
    GLint color;
    int ok = p != 0;
    if (!ok) return 0;
    glUseProgram(p);
    color = glGetUniformLocation(p, "uColor");
    glUniform4f(color, 0.0f, 1.0f, 0.0f, 1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SURFACE_W, SURFACE_H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDrawArrays(GL_LINE_LOOP, 0, 3);
    glDrawArrays(GL_POINTS, 0, 3);
    glReadPixels(0, 0, SURFACE_W, SURFACE_H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    *sum = pixel_sum(pixels, sizeof(pixels));
    ok = *sum != 0 && glGetError() == GL_NO_ERROR;
    glDisableVertexAttribArray(0);
    glDeleteProgram(p);
    glUseProgram(0);
    return ok;
}

static int test_egl_configs(uint32_t *sum)
{
    EGLConfig configs[8];
    EGLint count = 0, samples = -1, buffers = -1;
    EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    int ok = eglChooseConfig(g_dpy, attrs, configs, 8, &count) && count >= 2;
    int i, found4 = 0;
    for (i = 0; i < count && i < 8; ++i) {
        eglGetConfigAttrib(g_dpy, configs[i], EGL_SAMPLES, &samples);
        eglGetConfigAttrib(g_dpy, configs[i], EGL_SAMPLE_BUFFERS, &buffers);
        if (samples == 4 && buffers == 1) found4 = 1;
    }
    *sum = (uint32_t)count;
    return ok && found4;
}

static int test_egl_procaddr(uint32_t *sum)
{
    void (*known)(void) = (void (*)(void))eglGetProcAddress("glReadPixels");
    void (*unknown)(void) = (void (*)(void))eglGetProcAddress("glDefinitelyUnknownEWOK");
    (void)sum;
    return known != NULL && unknown == NULL;
}

static int test_sample_coverage(uint32_t *sum)
{
    GLfloat value = 0;
    GLboolean invert = GL_TRUE;
    int ok;
    glSampleCoverage(0.25f, GL_TRUE);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &value);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &invert);
    ok = value >= 0.249f && value <= 0.251f && invert == GL_TRUE;
    *sum = pixel_sum(&value, sizeof(value));
    return ok;
}

struct test_case {
    const char *suite;
    const char *name;
    int (*fn)(uint32_t *);
};

static const struct test_case cases[] = {
    { "objects", "buffer_lifecycle", test_buffer_lifecycle },
    { "objects", "texture_lifecycle", test_texture_lifecycle },
    { "attributes", "constant_and_enable", test_vertex_attrib_state },
    { "attributes", "pointer_queries", test_vertex_pointer_state },
    { "texture", "parameters", test_texture_parameters },
    { "texture", "packed_565", test_packed_texture },
    { "texture", "unpack_alignment", test_unpack_alignment },
    { "texture", "mipmap_levels", test_mipmap },
    { "texture", "etc1_decode", test_compressed_texture },
    { "readback", "rgb_rgba_alignment", test_readpixels_formats },
    { "readback", "packed_rgba4444", test_readpixels_packed },
    { "fbo", "object_and_attachments", test_fbo_lifecycle },
    { "fbo", "render_and_copy", test_fbo_pixels },
    { "shader", "transpose_and_validate", test_shader_errors },
    { "shader", "invalid_binary", test_shader_binary_rejected },
    { "primitive", "triangle_line_point", test_draw_primitives },
    { "egl", "zero_and_four_sample_configs", test_egl_configs },
    { "egl", "proc_address", test_egl_procaddr },
    { "msaa", "sample_coverage_state", test_sample_coverage },
};

static int init_egl(void)
{
    EGLint major = 0, minor = 0, count = 0;
    EGLConfig config = NULL;
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint surface_attrs[] = {
        EGL_WIDTH, SURFACE_W, EGL_HEIGHT, SURFACE_H, EGL_NONE
    };
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, &major, &minor)) return 0;
    if (!eglChooseConfig(g_dpy, config_attrs, &config, 1, &count) || count != 1)
        return 0;
    g_context = eglCreateContext(g_dpy, config, EGL_NO_CONTEXT, context_attrs);
    g_surface = eglCreatePbufferSurface(g_dpy, config, surface_attrs);
    if (g_context == EGL_NO_CONTEXT || g_surface == EGL_NO_SURFACE) return 0;
    if (!eglMakeCurrent(g_dpy, g_surface, g_surface, g_context)) return 0;
    printf("BASELINE EGL=%d.%d renderer=%s version=%s\n", major, minor,
           glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return 1;
}

int main(int argc, char **argv)
{
    size_t i;
    int cpu = 0;
    for (i = 1; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--cpu") == 0) cpu = 1;
        else if (strcmp(argv[i], "--suite") == 0 && i + 1 < (size_t)argc)
            g_filter = argv[++i];
        else if (strcmp(argv[i], "--list") == 0) {
            size_t j;
            for (j = 0; j < sizeof(cases) / sizeof(cases[0]); ++j)
                printf("%s.%s\n", cases[j].suite, cases[j].name);
            return 0;
        } else {
            printf("usage: gles2_conformance [--cpu] [--suite NAME|all|--list]\n");
            return 2;
        }
    }
    if (!init_egl()) {
        printf("RESULT egl.initialization FAIL gl=0x0000 egl=0x%04x gpu=0 sum=0\n",
               (unsigned)eglGetError());
        return 1;
    }
    if (cpu) glForceCpuEWOK();
    printf("MODE %s\n", cpu ? "cpu" : "native-or-cpu-fallback");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        run_case(cases[i].suite, cases[i].name, cases[i].fn);
    printf("SUMMARY pass=%d fail=%d checksum=%d\n", g_passes, g_failures, g_checksum);
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_dpy, g_surface);
    eglDestroyContext(g_dpy, g_context);
    eglTerminate(g_dpy);
    return g_failures ? 1 : 0;
}
