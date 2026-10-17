/*
 * Bouncing lit spheres -- EGL + GLES2 (software rasterizer behind libGLESv2).
 *
 * Ported off SDL2 + portablegl.  The per-vertex diffuse+ambient lighting that
 * used to live in a C vertex_shader (paired with pgl_ff_gouraud) is now a GLSL
 * ES 1.00 program: the VS transforms the normal into eye space, applies a
 * moving directional light and writes the lit colour as a varying that the FS
 * interpolates across each triangle.  Sphere geometry, physics and collision
 * response are unchanged.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#ifndef M_PI
#define M_PI 3.14159265
#endif

#define WIDTH 640
#define HEIGHT 480

static int win_width = WIDTH;
static int win_height = HEIGHT;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint program = 0;
static GLint loc_mvp = -1, loc_normal = -1, loc_material = -1, loc_light = -1;

/* Sphere data */
static GLuint sphere_vbo = 0;
static GLuint sphere_ibo = 0;
static int sphere_index_count = 0;

#define NUM_SPHERES 4

typedef struct {
    float x, y;           /* position */
    float vx, vy;         /* velocity */
    float radius;         /* size */
    float color[3];       /* color */
    float rot_x, rot_y, rot_z;                 /* rotation */
    float rot_speed_x, rot_speed_y, rot_speed_z; /* rotation speed */
} Sphere;

static Sphere spheres[NUM_SPHERES];

static float light_rot_x = 0.0f;
static float light_rot_y = 0.0f;

static GLfloat ProjectionMatrix[16];

/* ---- column-major mat4 helpers (from the original demo) ---- */
static void multiply(GLfloat *m, const GLfloat *n)
{
    GLfloat tmp[16];
    const GLfloat *row, *column;
    div_t d;
    int i, j;

    for (i = 0; i < 16; i++) {
        tmp[i] = 0;
        d = div(i, 4);
        row = n + d.quot * 4;
        column = m + d.rem;
        for (j = 0; j < 4; j++)
            tmp[i] += row[j] * column[j * 4];
    }
    memcpy(m, &tmp, sizeof tmp);
}

static void rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float s = sinf(angle);
    float c = cosf(angle);
    GLfloat r[16] = {
        x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
        x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0,
        x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
        0, 0, 0, 1
    };
    multiply(m, r);
}

static void translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  x, y, z, 1 };
    multiply(m, t);
}

static void identity(GLfloat *m)
{
    GLfloat t[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };
    memcpy(m, t, sizeof(t));
}

static void scale(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat s[16] = {
        x,   0.0, 0.0, 0.0,
        0.0, y,   0.0, 0.0,
        0.0, 0.0, z,   0.0,
        0.0, 0.0, 0.0, 1.0,
    };
    multiply(m, s);
}

static void transpose(GLfloat *m)
{
    GLfloat t[16] = {
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]};
    memcpy(m, t, sizeof(t));
}

static void invert(GLfloat *m)
{
    GLfloat t[16];
    identity(t);
    t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];
    m[12] = m[13] = m[14] = 0;
    transpose(m);
    multiply(m, t);
}

static void mat3_from_mat4(GLfloat* m3, const GLfloat* m4)
{
    m3[0] = m4[0]; m3[1] = m4[1]; m3[2] = m4[2];
    m3[3] = m4[4]; m3[4] = m4[5]; m3[5] = m4[6];
    m3[6] = m4[8]; m3[7] = m4[9]; m3[8] = m4[10];
}

static void ortho(GLfloat *m, GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
    GLfloat tmp[16];
    identity(tmp);

    tmp[0] = 2.0f / (right - left);
    tmp[5] = 2.0f / (top - bottom);
    tmp[10] = -2.0f / (zFar - zNear);
    tmp[12] = -(right + left) / (right - left);
    tmp[13] = -(top + bottom) / (top - bottom);
    tmp[14] = -(zFar + zNear) / (zFar - zNear);

    memcpy(m, tmp, sizeof(tmp));
}

/* ---- GLSL ES 1.00 program ---- */
static const char* balls_vs =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat3 uNormalMat;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uMaterial;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(uNormalMat * aNormal);\n"
    "    float diff = max(dot(N, uLightDir), 0.0);\n"
    "    float total = min(diff + 0.3, 1.0);\n"
    "    vColor = uMaterial * total;\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* balls_fs =
    "precision mediump float;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(vColor, 1.0);\n"
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
        printf("balls: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Create sphere vertices and indices (pos3 + normal3 interleaved). */
static void create_sphere(float radius, int stacks, int slices)
{
    int i, j;
    int vertex_count = (stacks + 1) * (slices + 1);
    int index_count = stacks * slices * 6;

    float* vertices = malloc(vertex_count * 6 * sizeof(float));
    GLuint* indices = malloc(index_count * sizeof(GLuint));

    if (!vertices || !indices) {
        if (vertices) free(vertices);
        if (indices) free(indices);
        return;
    }

    int vidx = 0;
    for (i = 0; i <= stacks; i++) {
        float phi = (float)M_PI * i / stacks;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);

        for (j = 0; j <= slices; j++) {
            float theta = 2.0f * (float)M_PI * j / slices;
            float sin_theta = sinf(theta);
            float cos_theta = cosf(theta);

            float nx = sin_phi * cos_theta;
            float ny = cos_phi;
            float nz = sin_phi * sin_theta;

            vertices[vidx++] = nx * radius;
            vertices[vidx++] = ny * radius;
            vertices[vidx++] = nz * radius;
            vertices[vidx++] = nx;
            vertices[vidx++] = ny;
            vertices[vidx++] = nz;
        }
    }

    int iidx = 0;
    for (i = 0; i < stacks; i++) {
        for (j = 0; j < slices; j++) {
            int i0 = i * (slices + 1) + j;
            int i1 = i0 + 1;
            int i2 = (i + 1) * (slices + 1) + j;
            int i3 = i2 + 1;

            indices[iidx++] = i0;
            indices[iidx++] = i2;
            indices[iidx++] = i1;
            indices[iidx++] = i1;
            indices[iidx++] = i2;
            indices[iidx++] = i3;
        }
    }

    glGenBuffers(1, &sphere_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 6 * sizeof(float), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &sphere_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphere_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(GLuint), indices, GL_STATIC_DRAW);

    sphere_index_count = index_count;

    free(vertices);
    free(indices);
}

static void draw_sphere(Sphere* s)
{
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat normal3[9];
    GLfloat model_view_projection[16];
    GLfloat transform[16];

    identity(transform);
    translate(transform, s->x, s->y, 0);
    scale(transform, s->radius, s->radius, s->radius);
    rotate(transform, s->rot_x, 1, 0, 0);
    rotate(transform, s->rot_y, 0, 1, 0);
    rotate(transform, s->rot_z, 0, 0, 1);

    memcpy(model_view, transform, sizeof(model_view));

    memcpy(model_view_projection, ProjectionMatrix, sizeof(model_view_projection));
    multiply(model_view_projection, model_view);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);
    mat3_from_mat4(normal3, normal_matrix);

    /* Moving light on a sphere of radius 10 around the origin. */
    float lr = 10.0f;
    float lx = lr * cosf(light_rot_y) * cosf(light_rot_x);
    float ly = lr * sinf(light_rot_x);
    float lz = lr * sinf(light_rot_y) * cosf(light_rot_x);
    float llen = sqrtf(lx*lx + ly*ly + lz*lz);
    if (llen > 0.0001f) { lx/=llen; ly/=llen; lz/=llen; }

    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, model_view_projection);
    glUniformMatrix3fv(loc_normal, 1, normal3);
    glUniform3f(loc_light, lx, ly, lz);
    glUniform3f(loc_material, s->color[0], s->color[1], s->color[2]);

    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphere_ibo);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glDrawElements(GL_TRIANGLES, sphere_index_count, GL_UNSIGNED_INT, 0);

    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void sphere_idle(double dt, float aspect)
{
    float world_y_max = 1.0f;
    float world_x_max = world_y_max * aspect;

    for (int i = 0; i < NUM_SPHERES; i++) {
        Sphere* s = &spheres[i];

        s->rot_x += s->rot_speed_x * (float)dt;
        s->rot_y += s->rot_speed_y * (float)dt;
        s->rot_z += s->rot_speed_z * (float)dt;

        while (s->rot_x > 2 * M_PI) s->rot_x -= (float)(2 * M_PI);
        while (s->rot_y > 2 * M_PI) s->rot_y -= (float)(2 * M_PI);
        while (s->rot_z > 2 * M_PI) s->rot_z -= (float)(2 * M_PI);

        s->x += s->vx * (float)dt;
        s->y += s->vy * (float)dt;

        int bounced = 0;

        if (s->x - s->radius < -world_x_max) {
            s->x = -world_x_max + s->radius; s->vx = -s->vx; bounced = 1;
        } else if (s->x + s->radius > world_x_max) {
            s->x = world_x_max - s->radius; s->vx = -s->vx; bounced = 1;
        }

        if (s->y - s->radius < -world_y_max) {
            s->y = -world_y_max + s->radius; s->vy = -s->vy; bounced = 1;
        } else if (s->y + s->radius > world_y_max) {
            s->y = world_y_max - s->radius; s->vy = -s->vy; bounced = 1;
        }

        if (bounced) {
            s->color[0] = (rand() % 256) / 255.0f;
            s->color[1] = (rand() % 256) / 255.0f;
            s->color[2] = (rand() % 256) / 255.0f;
        }
    }

    for (int i = 0; i < NUM_SPHERES; i++) {
        for (int j = i + 1; j < NUM_SPHERES; j++) {
            Sphere* s1 = &spheres[i];
            Sphere* s2 = &spheres[j];

            float dx = s2->x - s1->x;
            float dy = s2->y - s1->y;
            float distance = sqrtf(dx * dx + dy * dy);
            float min_distance = s1->radius + s2->radius;

            if (distance < min_distance && distance > 0.001f) {
                float nx = dx / distance;
                float ny = dy / distance;

                float overlap = min_distance - distance;
                float separation_x = nx * overlap * 0.5f;
                float separation_y = ny * overlap * 0.5f;
                s1->x -= separation_x; s1->y -= separation_y;
                s2->x += separation_x; s2->y += separation_y;

                float rvx = s2->vx - s1->vx;
                float rvy = s2->vy - s1->vy;
                float vel_along_normal = rvx * nx + rvy * ny;

                if (vel_along_normal < 0) {
                    float restitution = 1.0f;
                    float impulse = -(1.0f + restitution) * vel_along_normal;
                    impulse /= 2.0f;

                    float impulse_x = impulse * nx;
                    float impulse_y = impulse * ny;

                    s1->vx -= impulse_x; s1->vy -= impulse_y;
                    s2->vx += impulse_x; s2->vy += impulse_y;

                    s1->color[0] = (rand() % 256) / 255.0f;
                    s1->color[1] = (rand() % 256) / 255.0f;
                    s1->color[2] = (rand() % 256) / 255.0f;
                    s2->color[0] = (rand() % 256) / 255.0f;
                    s2->color[1] = (rand() % 256) / 255.0f;
                    s2->color[2] = (rand() % 256) / 255.0f;
                }
            }
        }
    }

    light_rot_x += (float)dt * 0.5f;
    light_rot_y += (float)dt * 0.3f;
}

static void init_gl(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    vs = compile_shader(GL_VERTEX_SHADER, balls_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, balls_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aNormal");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        printf("balls: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    loc_mvp = glGetUniformLocation(program, "uMVP");
    loc_normal = glGetUniformLocation(program, "uNormalMat");
    loc_material = glGetUniformLocation(program, "uMaterial");
    loc_light = glGetUniformLocation(program, "uLightDir");

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    create_sphere(1.0f, 32, 32);

    for (int i = 0; i < NUM_SPHERES; i++) {
        Sphere* s = &spheres[i];
        s->radius = 0.1f + (rand() % 30) / 100.0f;
        s->x = ((rand() % 200) - 100) / 100.0f;
        s->y = ((rand() % 200) - 100) / 100.0f * 0.5f;
        s->vx = ((rand() % 100) - 50) / 50.0f * 1.5f;
        s->vy = ((rand() % 100) - 50) / 50.0f * 1.2f;
        s->color[0] = (rand() % 256) / 255.0f;
        s->color[1] = (rand() % 256) / 255.0f;
        s->color[2] = (rand() % 256) / 255.0f;
        s->rot_x = s->rot_y = s->rot_z = 0.0f;
        s->rot_speed_x = (rand() % 100 - 50) / 20.0f;
        s->rot_speed_y = (rand() % 100 - 50) / 20.0f;
        s->rot_speed_z = (rand() % 100 - 50) / 20.0f;
    }
}

static void reshape(int w, int h)
{
    if (h <= 0) h = 1;
    win_width = w;
    win_height = h;
    float aspect = (float)w / (float)h;
    glViewport(0, 0, w, h);
    ortho(ProjectionMatrix, -aspect, aspect, -1.0f, 1.0f, -10.0f, 10.0f);
}

static uint32_t g_last_ms = 0;

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w != win_width || h != win_height)
        reshape(w, h);

    uint32_t now = kernel_tic_ms(0);
    double dt = (g_last_ms == 0) ? 0.0 : (now - g_last_ms) / 1000.0;
    if (dt > 0.1) dt = 0.1;
    g_last_ms = now;

    sphere_idle(dt, (float)win_width / (float)win_height);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    for (int i = 0; i < NUM_SPHERES; i++)
        draw_sphere(&spheres[i]);

    eglSwapBuffers(egl_dpy, egl_surf);
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS) {
        int key = ev->value.im.value;
        switch (key) {
        case KEY_ESC:
            xwin_close(xwin);
            break;
        case 'r':
            for (int i = 0; i < NUM_SPHERES; i++) {
                Sphere* s = &spheres[i];
                s->radius = 0.1f + (rand() % 30) / 100.0f;
                s->color[0] = (rand() % 256) / 255.0f;
                s->color[1] = (rand() % 256) / 255.0f;
                s->color[2] = (rand() % 256) / 255.0f;
                s->rot_speed_x = (rand() % 100 - 50) / 20.0f;
                s->rot_speed_y = (rand() % 100 - 50) / 20.0f;
                s->rot_speed_z = (rand() % 100 - 50) / 20.0f;
            }
            break;
        }
    }
}

static void loop(void* p)
{
    (void)p;
    render_frame();
    usleep(1000);
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

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    x_t x;
    x_init(&x, NULL);

    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, WIDTH, HEIGHT, "Balls", XWIN_STYLE_NORMAL);
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

    srand((unsigned)kernel_tic_ms(0));
    reshape(WIDTH, HEIGHT);
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
