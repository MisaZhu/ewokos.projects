// Dear ImGui: standalone example application for EwokOS xwin
//
// Rewritten onto the standard EGL + GLES2 + GLSL ES 1.00 front end (libs/gles)
// instead of PortableGL.  The window/context now come from libEGL (native window
// == xwin_t*), present is a plain eglSwapBuffers, and ImGui draws through the
// new GLES2 renderer backend (imgui_impl_gles2).  The editable clear colour and
// the ImGui demo/Hello/Another windows are unchanged, but the solid red
// background triangle is replaced by a meshed, rotating gear cluster ported
// from apps/gears (trimmed to three gears).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

static int win_width = 640;
static int win_height = 480;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

// ImGui state
static bool show_demo_window = true;
static bool show_another_window = false;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

// Background: a meshed, rotating gear cluster ported from apps/gears, trimmed to
// three gears (one red sun driving two planets).  Per-vertex diffuse lighting
// (gouraud) runs as a real GLSL ES 1.00 vertex shader; the cluster spins and the
// camera orbits slowly so it reads as a living background behind the ImGui
// windows.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6

#define MAX_GEARS 8
#define R_PER_TOOTH 0.2f

struct vertex_strip {
    GLint first;
    GLint count;
};

typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

struct gear {
    GearVertex *vertices;
    int nvertices;
    struct vertex_strip *strips;
    int nstrips;
    GLuint vbo;
};

struct gear_instance {
    struct gear *g;
    GLfloat x, y;          // centre position (world units)
    GLfloat color[4];      // material colour
    GLfloat speed;         // rotation = speed * angle + phase (degrees)
    GLfloat phase;
};

static struct gear_instance gears[MAX_GEARS];
static int n_gears = 0;
static GLfloat angle = 0.0f;

// Camera orbit parameters
static float camera_angle = 0.0f;
static GLfloat projection_matrix[16];

static GLuint gear_program = 0;
static GLint loc_mvp = -1, loc_normal = -1, loc_material = -1;
static GLint loc_light = -1, loc_shade = -1;

// Mouse state
static int mouse_x = 0;
static int mouse_y = 0;
static bool mouse_buttons[3] = {false, false, false}; // left, right, middle

// Keyboard state
static bool key_ctrl = false;
static bool key_shift = false;
static bool key_alt = false;
static bool key_super = false;

// FPS counter
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;
static uint32_t last_frame_tic = 0;

// Whether the scene program is currently dispatched on the GPU (raspi5 PGL_G3D)
// or falls back to the CPU GLSL interpreter.  Sampled each frame while the gear
// program is still bound; reported next to the FPS readout.
static int gpu_active = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);

    if (last_tic == 0 || (low - last_tic) >= 3000000) {
        last_tic = low;
        fps = frame_count / 3;
        frame_count = 0;
    }
    frame_count++;
}

// GLSL ES 1.00 shaders: per-vertex diffuse lighting (Gouraud) paired with a
// passthrough fragment shader.  mat4 uniforms are uploaded column-major
// (transpose = GL_FALSE), so "uMVP * vec4(p,1)" reproduces the classic
// mult_m4_v4(mvp, p) exactly.
static const char *gears_vs =
    "attribute vec3 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uNormalMat;\n"
    "uniform vec3 uMaterial;\n"
    "uniform vec4 uLightEye;\n"
    "uniform vec4 uShade;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vec3 ne = normalize((uNormalMat * vec4(aNormal, 0.0)).xyz);\n"
    "    vec3 L = normalize(uLightEye.xyz);\n"
    "    float diff = max(dot(ne, L), 0.0);\n"
    "    float spec = 0.0;\n"
    "    vec3 col = clamp(uMaterial * (uShade.x + uShade.y * diff) +\n"
    "                   vec3(uShade.z * spec), 0.0, 1.0);\n"
    "    vColor = vec4(col, 1.0);\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

static const char *gears_fs =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
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
        klog("imgui_demo: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

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
    memcpy(m, tmp, sizeof(tmp));
}

static void rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat c = cosf(angle * M_PI / 180.0f);
    GLfloat s = sinf(angle * M_PI / 180.0f);
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
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    memcpy(m, t, sizeof(t));
}

static void transpose_mat(GLfloat *m)
{
    GLfloat t[16] = {
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
    memcpy(m, t, sizeof(t));
}

static void invert(GLfloat *m)
{
    GLfloat t[16];
    identity(t);
    t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];
    m[12] = m[13] = m[14] = 0;
    transpose_mat(m);
    multiply(m, t);
}

static void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat z_near, GLfloat z_far)
{
    GLfloat tmp[16];
    identity(tmp);

    GLfloat radians = fovy / 2.0f * M_PI / 180.0f;
    GLfloat sine = sinf(radians);
    GLfloat cosine = cosf(radians);
    GLfloat delta_z = z_far - z_near;

    if (delta_z == 0 || sine == 0 || aspect == 0)
        return;

    GLfloat cotangent = cosine / sine;

    tmp[0] = cotangent / aspect;
    tmp[5] = cotangent;
    tmp[10] = -(z_far + z_near) / delta_z;
    tmp[11] = -1.0f;
    tmp[14] = -2.0f * z_near * z_far / delta_z;
    tmp[15] = 0.0f;

    memcpy(m, tmp, sizeof(tmp));
}

static GearVertex *vert(GearVertex *v, GLfloat x, GLfloat y, GLfloat z, GLfloat n[3])
{
    v[0][0] = x;
    v[0][1] = y;
    v[0][2] = z;
    v[0][3] = n[0];
    v[0][4] = n[1];
    v[0][5] = n[2];
    return v + 1;
}

static struct gear *create_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
                                GLint teeth, GLfloat tooth_depth)
{
    GLfloat r0, r1, r2;
    GLfloat da;
    GearVertex *v;
    struct gear *gear;
    GLfloat normal[3];
    int cur_strip = 0;
    int i;

    gear = (struct gear *)malloc(sizeof(*gear));
    if (gear == NULL)
        return NULL;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;
    da = 2.0f * M_PI / teeth / 4.0f;

    gear->nstrips = STRIPS_PER_TOOTH * teeth;
    gear->strips = (struct vertex_strip *)calloc(gear->nstrips, sizeof(*gear->strips));
    gear->vertices = (GearVertex *)calloc(VERTICES_PER_TOOTH * teeth, sizeof(*gear->vertices));
    if (gear->strips == NULL || gear->vertices == NULL) {
        free(gear->strips);
        free(gear->vertices);
        free(gear);
        return NULL;
    }
    v = gear->vertices;

    for (i = 0; i < teeth; i++) {
        GLfloat s[5], c[5];
        GLfloat sc_val = i * 2.0f * M_PI / teeth;
        int j;
        for (j = 0; j < 5; j++) {
            s[j] = sinf(sc_val + da * j);
            c[j] = cosf(sc_val + da * j);
        }

        struct point { GLfloat x, y; };
        struct point p[7] = {
            { r2 * c[1], r2 * s[1] },
            { r2 * c[2], r2 * s[2] },
            { r1 * c[0], r1 * s[0] },
            { r1 * c[3], r1 * s[3] },
            { r0 * c[0], r0 * s[0] },
            { r1 * c[4], r1 * s[4] },
            { r0 * c[4], r0 * s[4] },
        };

        #define SET_NORMAL(x, y, z) do { normal[0] = (x); normal[1] = (y); normal[2] = (z); } while(0)
        #define START_STRIP do { gear->strips[cur_strip].first = v - gear->vertices; } while(0)
        #define END_STRIP do { \
            int _tmp = (v - gear->vertices); \
            gear->strips[cur_strip].count = _tmp - gear->strips[cur_strip].first; \
            cur_strip++; \
        } while(0)
        #define QUAD_WITH_NORMAL(p1, p2) do { \
            SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
            v = vert(v, p[(p1)].x, p[(p1)].y, -width * 0.5f, normal); \
            v = vert(v, p[(p1)].x, p[(p1)].y,  width * 0.5f, normal); \
            v = vert(v, p[(p2)].x, p[(p2)].y, -width * 0.5f, normal); \
            v = vert(v, p[(p2)].x, p[(p2)].y,  width * 0.5f, normal); \
        } while(0)

        START_STRIP;
        SET_NORMAL(0, 0, 1.0f);
        v = vert(v, p[0].x, p[0].y,  width * 0.5f, normal);
        v = vert(v, p[1].x, p[1].y,  width * 0.5f, normal);
        v = vert(v, p[2].x, p[2].y,  width * 0.5f, normal);
        v = vert(v, p[3].x, p[3].y,  width * 0.5f, normal);
        v = vert(v, p[4].x, p[4].y,  width * 0.5f, normal);
        v = vert(v, p[5].x, p[5].y,  width * 0.5f, normal);
        v = vert(v, p[6].x, p[6].y,  width * 0.5f, normal);
        END_STRIP;

        START_STRIP;
        QUAD_WITH_NORMAL(4, 6);
        END_STRIP;

        START_STRIP;
        SET_NORMAL(0, 0, -1.0f);
        v = vert(v, p[6].x, p[6].y, -width * 0.5f, normal);
        v = vert(v, p[5].x, p[5].y, -width * 0.5f, normal);
        v = vert(v, p[4].x, p[4].y, -width * 0.5f, normal);
        v = vert(v, p[3].x, p[3].y, -width * 0.5f, normal);
        v = vert(v, p[2].x, p[2].y, -width * 0.5f, normal);
        v = vert(v, p[1].x, p[1].y, -width * 0.5f, normal);
        v = vert(v, p[0].x, p[0].y, -width * 0.5f, normal);
        END_STRIP;

        START_STRIP;
        QUAD_WITH_NORMAL(0, 2);
        END_STRIP;

        START_STRIP;
        QUAD_WITH_NORMAL(1, 0);
        END_STRIP;

        START_STRIP;
        QUAD_WITH_NORMAL(3, 1);
        END_STRIP;

        START_STRIP;
        QUAD_WITH_NORMAL(5, 3);
        END_STRIP;
    }

    #undef SET_NORMAL
    #undef START_STRIP
    #undef END_STRIP
    #undef QUAD_WITH_NORMAL

    gear->nvertices = (v - gear->vertices);

    glGenBuffers(1, &gear->vbo);
    if (gear->vbo == 0) {
        free(gear->strips);
        free(gear->vertices);
        free(gear);
        return NULL;
    }
    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
    glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex),
                 gear->vertices, GL_STATIC_DRAW);

    return gear;
}

static void draw_gear(struct gear *gear, GLfloat *transform,
                      GLfloat x, GLfloat y, GLfloat angle_rot, const GLfloat color[4])
{
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat mvp[16];
    int n;

    if (gear == NULL || gear->strips == NULL || gear->vertices == NULL)
        return;
    if (gear->vbo == 0)
        return;

    memcpy(model_view, transform, sizeof(model_view));
    translate(model_view, x, y, 0);
    rotate(model_view, angle_rot, 0, 0, 1);

    memcpy(mvp, projection_matrix, sizeof(mvp));
    multiply(mvp, model_view);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose_mat(normal_matrix);

    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(loc_normal, 1, GL_FALSE, normal_matrix);
    glUniform3f(loc_material, color[0], color[1], color[2]);

    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
                          (void*)(0 + 3*sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    for (n = 0; n < gear->nstrips; n++)
        glDrawArrays(GL_TRIANGLE_STRIP, gear->strips[n].first, gear->strips[n].count);

    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void look_at(GLfloat *m, GLfloat eye_x, GLfloat eye_y, GLfloat eye_z,
                    GLfloat center_x, GLfloat center_y, GLfloat center_z,
                    GLfloat up_x, GLfloat up_y, GLfloat up_z)
{
    GLfloat f[3], s[3], u[3];
    GLfloat f_len, s_len;

    f[0] = center_x - eye_x;
    f[1] = center_y - eye_y;
    f[2] = center_z - eye_z;
    f_len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;

    s[0] = f[1]*up_z - f[2]*up_y;
    s[1] = f[2]*up_x - f[0]*up_z;
    s[2] = f[0]*up_y - f[1]*up_x;
    s_len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= s_len; s[1] /= s_len; s[2] /= s_len;

    u[0] = s[1]*f[2] - s[2]*f[1];
    u[1] = s[2]*f[0] - s[0]*f[2];
    u[2] = s[0]*f[1] - s[1]*f[0];

    GLfloat look_at_mat[16] = {
        s[0], u[0], -f[0], 0,
        s[1], u[1], -f[1], 0,
        s[2], u[2], -f[2], 0,
        -(s[0]*eye_x + s[1]*eye_y + s[2]*eye_z),
        -(u[0]*eye_x + u[1]*eye_y + u[2]*eye_z),
        f[0]*eye_x + f[1]*eye_y + f[2]*eye_z,
        1
    };

    memcpy(m, look_at_mat, sizeof(look_at_mat));
}

/* Add a gear meshing with the driver sun gear (see apps/gears for the phase math). */
static void add_planet_gear(GLint teeth, GLfloat phi_deg, GLfloat width,
                            const GLfloat color[4])
{
    static const GLint sun_teeth = 20;
    GLfloat dist, rad, phi, inner, outer;

    if (n_gears >= MAX_GEARS)
        return;

    dist = R_PER_TOOTH * (sun_teeth + teeth);
    outer = R_PER_TOOTH * teeth;
    inner = outer * 0.25f;

    phi = phi_deg * (1.0f + (GLfloat)sun_teeth / (GLfloat)teeth)
          + 180.0f - 5.0f * 180.0f / (2.0f * teeth);
    while (phi < 0.0f) phi += 360.0f;
    while (phi >= 360.0f) phi -= 360.0f;

    rad = phi_deg * M_PI / 180.0f;
    gears[n_gears].g = create_gear(inner, outer, width, teeth, 0.7f);
    gears[n_gears].x = dist * cosf(rad);
    gears[n_gears].y = dist * sinf(rad);
    memcpy(gears[n_gears].color, color, sizeof(gears[n_gears].color));
    gears[n_gears].speed = -(GLfloat)sun_teeth / (GLfloat)teeth;
    gears[n_gears].phase = phi;
    if (gears[n_gears].g != NULL)
        n_gears++;
}

static void add_sun_gear(GLint teeth, GLfloat width, const GLfloat color[4])
{
    GLfloat outer, inner;
    if (n_gears >= MAX_GEARS)
        return;
    outer = R_PER_TOOTH * teeth;
    inner = outer * 0.25f;
    gears[n_gears].g = create_gear(inner, outer, width, teeth, 0.7f);
    gears[n_gears].x = 0.0f;
    gears[n_gears].y = 0.0f;
    memcpy(gears[n_gears].color, color, sizeof(gears[n_gears].color));
    gears[n_gears].speed = 1.0f;
    gears[n_gears].phase = 0.0f;
    if (gears[n_gears].g != NULL)
        n_gears++;
}

static void init_gears(void)
{
    static const GLfloat red[4]   = { 0.8f, 0.1f, 0.0f, 1.0f };
    static const GLfloat green[4] = { 0.0f, 0.8f, 0.2f, 1.0f };
    static const GLfloat blue[4]  = { 0.2f, 0.3f, 1.0f, 1.0f };

    GLuint vs, fs;
    GLint ok = 0;

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    vs = compile_shader(GL_VERTEX_SHADER, gears_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, gears_fs);
    gear_program = glCreateProgram();
    glAttachShader(gear_program, vs);
    glAttachShader(gear_program, fs);
    glBindAttribLocation(gear_program, 0, "aPosition");
    glBindAttribLocation(gear_program, 1, "aNormal");
    glLinkProgram(gear_program);
    glGetProgramiv(gear_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(gear_program, sizeof(log), NULL, log);
        klog("imgui_demo: gear program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(gear_program);

    loc_mvp      = glGetUniformLocation(gear_program, "uMVP");
    loc_normal   = glGetUniformLocation(gear_program, "uNormalMat");
    loc_material = glGetUniformLocation(gear_program, "uMaterial");
    loc_light    = glGetUniformLocation(gear_program, "uLightEye");
    loc_shade    = glGetUniformLocation(gear_program, "uShade");

    /* Directional light along (5,5,10), pure diffuse shading (ambient 0,
     * diffuse 1, specular 0) — constant for every gear, so set once here. */
    glUniform4f(loc_light, 5.0f, 5.0f, 10.0f, 0.0f);
    glUniform4f(loc_shade, 0.0f, 1.0f, 0.0f, 0.0f);

    perspective(projection_matrix, 60.0f, (GLfloat)win_width / (GLfloat)win_height, 1.0f, 1024.0f);
    glViewport(0, 0, win_width, win_height);

    /* Three gears: a red sun (20 teeth) driving a green and a blue planet. */
    add_sun_gear(20, 1.2f, red);
    add_planet_gear(10,  90.0f, 0.9f, green);
    add_planet_gear(10, 210.0f, 0.9f, blue);

    if (n_gears != 3)
        printf("Failed to create gears (%d/3)\n", n_gears);
}

static void init_imgui(void)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Disable imgui.ini
    io.IniFilename = NULL;

    // Setup display size
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Renderer backend
    ImGui_ImplGLES2_Init();

    // Load Fonts
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

// Map EwokOS key code to ImGuiKey
static ImGuiKey KeyCodeToImGuiKey(int keycode)
{
    switch (keycode)
    {
        case 9: return ImGuiKey_Tab;
        case 27: return ImGuiKey_Escape;
        case 13: return ImGuiKey_Enter;
        case 8: return ImGuiKey_Backspace;
        case 127: return ImGuiKey_Delete;
        case 32: return ImGuiKey_Space;

        // Arrow keys
        case 276: return ImGuiKey_LeftArrow;
        case 275: return ImGuiKey_RightArrow;
        case 273: return ImGuiKey_UpArrow;
        case 274: return ImGuiKey_DownArrow;

        // Page/Home/End
        case 278: return ImGuiKey_Home;
        case 279: return ImGuiKey_End;
        case 280: return ImGuiKey_PageUp;
        case 281: return ImGuiKey_PageDown;
        case 277: return ImGuiKey_Insert;

        // Numbers
        case '0': return ImGuiKey_0;
        case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4;
        case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6;
        case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;

        // Letters
        case 'a': case 'A': return ImGuiKey_A;
        case 'b': case 'B': return ImGuiKey_B;
        case 'c': case 'C': return ImGuiKey_C;
        case 'd': case 'D': return ImGuiKey_D;
        case 'e': case 'E': return ImGuiKey_E;
        case 'f': case 'F': return ImGuiKey_F;
        case 'g': case 'G': return ImGuiKey_G;
        case 'h': case 'H': return ImGuiKey_H;
        case 'i': case 'I': return ImGuiKey_I;
        case 'j': case 'J': return ImGuiKey_J;
        case 'k': case 'K': return ImGuiKey_K;
        case 'l': case 'L': return ImGuiKey_L;
        case 'm': case 'M': return ImGuiKey_M;
        case 'n': case 'N': return ImGuiKey_N;
        case 'o': case 'O': return ImGuiKey_O;
        case 'p': case 'P': return ImGuiKey_P;
        case 'q': case 'Q': return ImGuiKey_Q;
        case 'r': case 'R': return ImGuiKey_R;
        case 's': case 'S': return ImGuiKey_S;
        case 't': case 'T': return ImGuiKey_T;
        case 'u': case 'U': return ImGuiKey_U;
        case 'v': case 'V': return ImGuiKey_V;
        case 'w': case 'W': return ImGuiKey_W;
        case 'x': case 'X': return ImGuiKey_X;
        case 'y': case 'Y': return ImGuiKey_Y;
        case 'z': case 'Z': return ImGuiKey_Z;

        // Function keys
        case 282: return ImGuiKey_F1;
        case 283: return ImGuiKey_F2;
        case 284: return ImGuiKey_F3;
        case 285: return ImGuiKey_F4;
        case 286: return ImGuiKey_F5;
        case 287: return ImGuiKey_F6;
        case 288: return ImGuiKey_F7;
        case 289: return ImGuiKey_F8;
        case 290: return ImGuiKey_F9;
        case 291: return ImGuiKey_F10;
        case 292: return ImGuiKey_F11;
        case 293: return ImGuiKey_F12;

        // Special characters
        case ',': return ImGuiKey_Comma;
        case '.': return ImGuiKey_Period;
        case '/': return ImGuiKey_Slash;
        case ';': return ImGuiKey_Semicolon;
        case '\'': return ImGuiKey_Apostrophe;
        case '[': return ImGuiKey_LeftBracket;
        case ']': return ImGuiKey_RightBracket;
        case '-': return ImGuiKey_Minus;
        case '=': return ImGuiKey_Equal;
        case '`': return ImGuiKey_GraveAccent;

        default: return ImGuiKey_None;
    }
}

static void process_imgui_frame(void)
{
    ImGuiIO& io = ImGui::GetIO();

    // Update display size (in case of resize)
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);

    // Update mouse position
    io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);

    // Update mouse buttons
    io.MouseDown[0] = mouse_buttons[0]; // left
    io.MouseDown[1] = mouse_buttons[1]; // right
    io.MouseDown[2] = mouse_buttons[2]; // middle

    // Update keyboard modifiers
    io.KeyCtrl = key_ctrl;
    io.KeyShift = key_shift;
    io.KeyAlt = key_alt;
    io.KeySuper = key_super;

    // Start the Dear ImGui frame
    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");
        ImGui::Text("This is some useful text.");
        ImGui::Checkbox("Demo Window", &show_demo_window);
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        ImGui::ColorEdit3("clear color", (float*)&clear_color);

        if (ImGui::Button("Button")) {
            counter++;
            printf("%.1f FPS\n", ImGui::GetIO().Framerate);
        }
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS) %s",
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate,
                    gpu_active ? "GPU" : "CPU");
        ImGui::End();
    }

    // 3. Show another simple window
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }
}

static void draw_gears(void)
{
    GLfloat transform[16];
    int i;

    /* ImGui_ImplGLES2_RenderDrawData leaves its own program bound and disables
     * depth/cull every frame, so re-establish the scene state before drawing. */
    glUseProgram(gear_program);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    /* Camera orbits the gear cluster, close enough that it nearly fills the frame. */
    float cam_x = sinf(camera_angle * M_PI / 180.0f) * 15.0f;
    float cam_y = 4.0f;
    float cam_z = cosf(camera_angle * M_PI / 180.0f) * 15.0f;

    look_at(transform, cam_x, cam_y, cam_z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    for (i = 0; i < n_gears; i++)
        draw_gear(gears[i].g, transform, gears[i].x, gears[i].y,
                  gears[i].speed * angle + gears[i].phase, gears[i].color);
}

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    uint32_t current_tic;
    float dt;

    // Pick up a window resize: libEGL already resized the engine back buffer,
    // we only mirror the new size into the viewport/projection and ImGui.
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height)) {
        win_width = w;
        win_height = h;
        perspective(projection_matrix, 60.0f,
                    (GLfloat)win_width / (GLfloat)win_height, 1.0f, 1024.0f);
    }

    kernel_tic32(NULL, NULL, &current_tic);
    if (last_frame_tic == 0)
        last_frame_tic = current_tic;
    dt = (current_tic - last_frame_tic) / 1000000.0f;
    last_frame_tic = current_tic;
    if (dt > 0.1f) dt = 0.1f;

    angle += 240.0f * dt;
    if (angle >= 360.0f) angle -= 360.0f;
    camera_angle += 30.0f * dt;
    if (camera_angle >= 360.0f) camera_angle -= 360.0f;

    glViewport(0, 0, win_width, win_height);
    glScissor(0, 0, win_width, win_height);
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Background gear cluster
    draw_gears();

    // Query GPU status while the scene program is still bound (before ImGui
    // rebinds its own program), so the readout reflects the gear draw path.
    gpu_active = glGpuActiveEWOK();

    // GUI Rendering
    process_imgui_frame();
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    ImGuiIO& io = ImGui::GetIO();

    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        uint32_t shift = ev->value.im.shift;
        uint32_t ctrl = ev->value.im.ctrl;
        int32_t state = ev->state; // XIM_STATE_PRESS or XIM_STATE_RELEASE

        // Update modifier keys
        key_shift = (shift != 0);
        key_ctrl = (ctrl != 0);

        // Handle ESC key to close window (only on press)
        if (key == 27 && state == XIM_STATE_PRESS) {
            xwin_close(xwin);
            return;
        }

        // Map key to ImGuiKey and add event
        ImGuiKey imgui_key = KeyCodeToImGuiKey(key);
        if (imgui_key != ImGuiKey_None) {
            bool pressed = (state == XIM_STATE_PRESS);
            io.AddKeyEvent(imgui_key, pressed);
        }

        // Add character input for printable characters (only on press)
        if (state == XIM_STATE_PRESS && key >= 32 && key < 127) {
            io.AddInputCharacter(key);
        }
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);

        mouse_x = pos.x;
        mouse_y = pos.y;

        // Update mouse buttons based on state (state is at top level of xevent_t)
        int32_t state = ev->state;
        int32_t button = ev->value.mouse.button;

        if (button == MOUSE_BUTTON_LEFT) {
            mouse_buttons[0] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            mouse_buttons[1] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
        else if (button == MOUSE_BUTTON_MID) {
            mouse_buttons[2] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
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

int main(int argc, char* argv[])
{
    x_t x;
    xwin_t* xwin;

    (void)argc;
    (void)argv;

    x_init(&x, NULL);
    x.on_loop = loop;

    xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "ImGui Demo", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin->on_event = on_event;

    // libEGL owns on_repaint/on_resize once the window surface is created.
    if (!init_egl(xwin)) {
        printf("Failed to initialize EGL\n");
        return 1;
    }

    glViewport(0, 0, win_width, win_height);
    glEnable(GL_DEPTH_TEST);

    init_gears();
    init_imgui();

    // Make window visible
    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    // Cleanup
    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);

    return 0;
}
