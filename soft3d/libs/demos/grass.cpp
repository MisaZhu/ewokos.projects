/*
 * Grass field demo -- EGL + GLES2 (software rasterizer behind libGLESv2).
 *
 * The original used GLES3 instancing (glDrawArraysInstanced + gl_InstanceID)
 * to scatter 65536 blades.  The GLES2 engine has no instanced draw, so the
 * blade field is baked once on the CPU here: every blade is pre-offset by its
 * grid position (+ deterministic jitter) and pre-rotated by its random angle,
 * exactly as the old grass_vs did per instance.  The 6-vertex blade strip is
 * expanded to 4 triangles (12 vertices) and the whole field is uploaded as one
 * static VBO, drawn with a trivial GLSL ES 1.00 program (uMVP * position) and
 * a flat green colour.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Field extent: GRID x GRID blades on unit spacing, centred on the origin.
 * 64x64 = 4096 blades * 12 vertices = 49152 vertices, comfortably inside the
 * rasterizer's per-draw vertex cap and fast enough to re-shade each frame. */
#define GRID 64

static int win_width = 640;
static int win_height = 480;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint program = 0;
static GLuint blade_vbo = 0;
static GLint loc_mvp = -1;
static GLint loc_color = -1;
static int blade_vertex_count = 0;

static float proj_mat[16];

/* Simple camera (yaw/pitch + position). */
struct Camera {
    float pos[3];
    float rot[3];

    Camera() {
        pos[0] = 0; pos[1] = 0.5f; pos[2] = 3.0f;
        rot[0] = 0; rot[1] = 0; rot[2] = 0;
    }

    void get_matrix(float* mat) {
        float eye[3] = {pos[0], pos[1], pos[2]};
        float center[3] = {pos[0], pos[1], pos[2] - 1.0f};
        float up[3] = {0, 1, 0};

        float f[3] = {center[0]-eye[0], center[1]-eye[1], center[2]-eye[2]};
        float len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
        if (len > 0.0001f) { f[0]/=len; f[1]/=len; f[2]/=len; }

        float s[3] = {f[1]*up[2] - f[2]*up[1], f[2]*up[0] - f[0]*up[2], f[0]*up[1] - f[1]*up[0]};
        len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
        if (len > 0.0001f) { s[0]/=len; s[1]/=len; s[2]/=len; }

        float u[3] = {s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0]};

        mat[0] = s[0]; mat[1] = u[0]; mat[2] = -f[0]; mat[3] = 0;
        mat[4] = s[1]; mat[5] = u[1]; mat[6] = -f[1]; mat[7] = 0;
        mat[8] = s[2]; mat[9] = u[2]; mat[10] = -f[2]; mat[11] = 0;
        mat[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
        mat[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
        mat[14] = f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2];
        mat[15] = 1.0f;
    }

    void move_forward(float dist) {
        float rad = rot[1] * (float)M_PI / 180.0f;
        pos[0] -= sinf(rad) * dist;
        pos[2] -= cosf(rad) * dist;
    }

    void rotate_y(float deg) { rot[1] += deg; }
    void rotate_x(float deg) { rot[0] += deg; }
};

static Camera camera;

/* Column-major mat4 helpers. */
static void mat4_multiply(float* result, const float* a, const float* b) {
    float temp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            temp[j*4+i] = 0;
            for (int k = 0; k < 4; k++) {
                temp[j*4+i] += a[k*4+i] * b[j*4+k];
            }
        }
    }
    memcpy(result, temp, sizeof(temp));
}

static void mat4_perspective(float* m, float fovy, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovy / 2.0f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

static int random(int seed, int iterations) {
    unsigned int val = (unsigned int)seed;
    for (int n = 0; n < iterations; ++n) {
        val = ((val >> 7) ^ (val << 9)) * 15485863u;
    }
    return (int)val;
}

static const char* grass_vs_src =
    "attribute vec3 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* grass_fs_src =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() {\n"
    "    gl_FragColor = uColor;\n"
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
        printf("grass: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Build the whole grass field as one interleaved position array. */
static GLfloat* build_blades(int* out_count)
{
    /* One blade = the 6-vertex strip expanded to 4 triangles = 12 vertices. */
    static const float strip[6][2] = {
        {-0.3f, 0.0f}, {0.3f, 0.0f}, {-0.20f, 1.0f},
        {0.1f, 1.3f}, {-0.05f, 2.3f}, {0.0f, 3.3f}
    };
    static const int tri[4][3] = { {0,1,2}, {2,1,3}, {2,3,4}, {4,3,5} };

    int total = GRID * GRID * 12;
    GLfloat* data = (GLfloat*)malloc((size_t)total * 3 * sizeof(GLfloat));
    if (!data) { *out_count = 0; return NULL; }

    int v = 0;
    for (int gi = 0; gi < GRID; gi++) {
        for (int gj = 0; gj < GRID; gj++) {
            int inst = gi * GRID + gj;

            float ox = (float)(gi - GRID / 2);
            float oz = (float)(gj - GRID / 2);

            int num1 = random(inst, 3);
            int num2 = random(num1, 2);

            ox += (float)(num1 & 0xFF) / 128.0f;
            oz += (float)(num2 & 0xFF) / 128.0f;

            float angle1 = (float)num2;
            float st = sinf(angle1);
            float ct = cosf(angle1);

            for (int t = 0; t < 4; t++) {
                for (int c = 0; c < 3; c++) {
                    float bx = strip[tri[t][c]][0];
                    float by = strip[tri[t][c]][1];
                    float rx = ct * bx + st * by;
                    float rz = -st * bx + ct * by;
                    data[v++] = rx + ox;
                    data[v++] = by;
                    data[v++] = rz + oz;
                }
            }
        }
    }
    *out_count = total;
    return data;
}

static void init_gl(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    vs = compile_shader(GL_VERTEX_SHADER, grass_vs_src);
    fs = compile_shader(GL_FRAGMENT_SHADER, grass_fs_src);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        printf("grass: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    loc_mvp = glGetUniformLocation(program, "uMVP");
    loc_color = glGetUniformLocation(program, "uColor");
    glUniform4f(loc_color, 0.0f, 1.0f, 0.0f, 1.0f);

    int count = 0;
    GLfloat* data = build_blades(&count);
    if (!data) return;
    blade_vertex_count = count;

    glGenBuffers(1, &blade_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, blade_vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)count * 3 * sizeof(GLfloat), data, GL_STATIC_DRAW);
    free(data);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

static void reshape(int w, int h)
{
    if (h <= 0) h = 1;
    win_width = w;
    win_height = h;
    glViewport(0, 0, w, h);
    mat4_perspective(proj_mat, 45.0f * (float)M_PI / 180.0f, (float)w / (float)h, 0.01f, 100.0f);
}

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w != win_width || h != win_height)
        reshape(w, h);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float view_mat[16];
    float mvp[16];
    camera.get_matrix(view_mat);
    mat4_multiply(mvp, proj_mat, view_mat);

    glUseProgram(program);
    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    glBindBuffer(GL_ARRAY_BUFFER, blade_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, blade_vertex_count);

    eglSwapBuffers(egl_dpy, egl_surf);
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        int32_t state = ev->state;

        if (state == XIM_STATE_PRESS) {
            switch (key) {
            case KEY_ESC:   xwin_close(xwin); break;
            case KEY_LEFT:  camera.rotate_y(-5.0f); break;
            case KEY_RIGHT: camera.rotate_y(5.0f); break;
            case KEY_UP:    camera.move_forward(0.5f); break;
            case KEY_DOWN:  camera.move_forward(-0.5f); break;
            }
        }
    }
}

static void loop(void* p)
{
    (void)p;
    render_frame();
    usleep(16000);
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

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    x_t x;
    x_init(&x, NULL);

    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Grass", XWIN_STYLE_NORMAL);
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

    camera.pos[0] = 0; camera.pos[1] = 5.0f; camera.pos[2] = 20.0f;
    reshape(win_width, win_height);
    init_gl();

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
    if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    return 0;
}
