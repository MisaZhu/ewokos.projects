/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Ported to GLES2.
 * Kristian Høgsberg <krh@bitplanet.net>
 * May 3, 2010
 * 
 * Improve GLES2 port:
 *   * Refactor gear drawing.
 *   * Use correct normals for surfaces.
 *   * Improve shader.
 *   * Use perspective projection transformation.
 *   * Add FPS count.
 *   * Add comments.
 * Alexandros Frantzis <alexandros.frantzis@linaro.org>
 * Jul 13, 2010
 *
 * Ported to SDL2 and OpenGL 3.3 core
 *   * Fix up shaders
 *   * Add a VAO
 *   * Remove all glut/egl code replace with SDL2
 *   * Add polygon_mode toggle for fun
 *   * TODO: refactor/clean up more
 *   * repo at:         https://github.com/rswinkle/opengl_reference
 *   * original source: https://cgit.freedesktop.org/mesa/demos/tree/src/egl/opengles2/es2gears.c
 * Robert Winkler
 * April 9, 2016
 *
 * Ported to PortableGL
 * Robert Winkler
 * September 5, 2016
 *
 * Ported to EwokOS xwin/graph_t
 * Removed SDL dependency
 */

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
#include <graph/graph.h>
#include <font/font.h>

#include "glcommon/gltools.h"

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int win_width = 640;
static int win_height = 480;

static glContext the_context;
static pix_t* backbuf = NULL;
static font_t* font = NULL;

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6

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

static GLfloat view_rot[3] = { 20.0f, 30.0f, 0.0f };

/* All gears share one module so their teeth interleave: pitch radius R = 0.2 * teeth
 * (pitch circle sits at create_gear()'s outer_radius).  A sun gear at the origin is
 * the driver; every planet meshes with it, its centre placed at R_sun + R_planet. */
#define MAX_GEARS 8
#define R_PER_TOOTH 0.2f

struct gear_instance {
    struct gear *g;
    GLfloat x, y;          /* centre position (world units) */
    GLfloat color[4];      /* material colour */
    GLfloat speed;         /* rotation = speed * angle + phase (degrees) */
    GLfloat phase;
};
static struct gear_instance gears[MAX_GEARS];
static int n_gears = 0;
static GLfloat angle = 0.0f;

// Camera orbit parameters
static float camera_angle = 0.0f;
static GLfloat projection_matrix[16];

typedef struct {
    mat4 mvp_mat;
    mat4 normal_mat;
    vec3 material_color;
} My_Uniforms;

static My_Uniforms uniforms;
static GLuint program;

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

static void transpose(GLfloat *m)
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
    transpose(m);
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

    gear = malloc(sizeof(*gear));
    if (gear == NULL)
        return NULL;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;
    da = 2.0f * M_PI / teeth / 4.0f;

    gear->nstrips = STRIPS_PER_TOOTH * teeth;
    gear->strips = calloc(gear->nstrips, sizeof(*gear->strips));
    gear->vertices = calloc(VERTICES_PER_TOOTH * teeth, sizeof(*gear->vertices));
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

/* PortableGL vertex shader.
 *
 * gears lights per vertex (Gouraud shading), so the lit colour is written to
 * vs_output[0..3] as a vec4 RGBA and the program pairs this VS with the
 * built-in pgl_ff_gouraud fragment shader.  That makes the fragment stage
 * semantically identical to the previous hand-written fragment_shader (which
 * just emitted the interpolated colour) while letting the raspi5 V3D back end
 * offload rasterization + interpolation + depth/cull to the GPU; on every other
 * platform it runs unchanged on the CPU rasterizer. */
void vertex_shader(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    vec4* vs_out = (vec4*)vs_output;
    My_Uniforms* u = uniforms;

    vec4 v4 = mult_m4_v4(u->normal_mat, v_attrs[1]);
    vec3 v3 = { v4.x, v4.y, v4.z };
    vec3 N = norm_v3(v3);

    const vec3 light_pos = { 5.0f, 5.0f, 10.0f };
    vec3 L = norm_v3(light_pos);

    float tmp = dot_v3s(N, L);
    float diff_intensity = MAX(tmp, 0.0f);

    vec3 c = scale_v3(u->material_color, diff_intensity);
    vs_out[0] = make_v4(c.x, c.y, c.z, 1.0f);

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[0]);
}

static void draw_gear(struct gear *gear, GLfloat *transform,
                      GLfloat x, GLfloat y, GLfloat angle_rot, const GLfloat color[4])
{
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat mvp[16];

    if (gear == NULL || gear->strips == NULL || gear->vertices == NULL) {
        klog("draw_gear: NULL check failed: gear=%p, strips=%p, vertices=%p\n", gear, gear ? gear->strips : NULL, gear ? gear->vertices : NULL);
        return;
    }

    if (gear->vbo == 0) {
        klog("draw_gear: invalid vbo\n");
        return;
    }

    memcpy(model_view, transform, sizeof(model_view));
    translate(model_view, x, y, 0);
    rotate(model_view, angle_rot, 0, 0, 1);

    memcpy(mvp, projection_matrix, sizeof(mvp));
    multiply(mvp, model_view);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);

    memcpy(&uniforms.mvp_mat, mvp, sizeof(mat4));
    memcpy(&uniforms.normal_mat, normal_matrix, sizeof(mat4));
    uniforms.material_color.x = color[0];
    uniforms.material_color.y = color[1];
    uniforms.material_color.z = color[2];

    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
                          (void*)(0 + 3*sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    int n;
    int total_verts = 0;
    for (n = 0; n < gear->nstrips; n++) {
        glDrawArrays(GL_TRIANGLE_STRIP, gear->strips[n].first, gear->strips[n].count);
        total_verts += gear->strips[n].count;
    }

    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void look_at(GLfloat *m, GLfloat eye_x, GLfloat eye_y, GLfloat eye_z,
                    GLfloat center_x, GLfloat center_y, GLfloat center_z,
                    GLfloat up_x, GLfloat up_y, GLfloat up_z)
{
    GLfloat f[3], s[3], u[3];
    GLfloat f_len, s_len, u_len;
    
    // Forward vector (from eye to center)
    f[0] = center_x - eye_x;
    f[1] = center_y - eye_y;
    f[2] = center_z - eye_z;
    f_len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;
    
    // Side vector (cross product of forward and up)
    s[0] = f[1]*up_z - f[2]*up_y;
    s[1] = f[2]*up_x - f[0]*up_z;
    s[2] = f[0]*up_y - f[1]*up_x;
    s_len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= s_len; s[1] /= s_len; s[2] /= s_len;
    
    // Recompute up vector (cross product of side and forward)
    u[0] = s[1]*f[2] - s[2]*f[1];
    u[1] = s[2]*f[0] - s[0]*f[2];
    u[2] = s[0]*f[1] - s[1]*f[0];
    
    // Build look-at matrix
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

static void draw_gears(void)
{
    GLfloat transform[16];
    int i;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Camera orbits the gear cluster, close enough that it nearly fills the frame.
    // The cluster's outermost tooth reaches ~9.6 units from the origin; with a 60 deg
    // vertical FOV a distance of 16.5 gives a half-height of ~9.5, so the gears span
    // almost the whole window.  cam_y is kept low for a near face-on view (a higher
    // camera would tilt the circles into ellipses and waste vertical space).
    float cam_x = sinf(camera_angle * M_PI / 180.0f) * 16.5f;
    float cam_y = 4.0f;  // Slightly above the gears
    float cam_z = cosf(camera_angle * M_PI / 180.0f) * 16.5f;

    // Build look-at matrix to always face the center (0, 0, 0)
    look_at(transform, cam_x, cam_y, cam_z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    for (i = 0; i < n_gears; i++)
        draw_gear(gears[i].g, transform, gears[i].x, gears[i].y,
                  gears[i].speed * angle + gears[i].phase, gears[i].color);
}

/* Add a gear meshing with the driver sun gear.  The planet is placed with its
 * centre at exactly R_sun + R_planet along phi_deg, spun at -N_sun/N_planet
 * times the sun's angle (external gears counter-rotate), and given the phase
 *
 *   phi = phi_deg*(1 + N_sun/N_planet) + 180 - 5*180/(2*N_planet)
 *
 * that drops each tooth into the mating gap.  This reproduces the original
 * hand-tuned offsets exactly (gear2: phi=0 -> 135 deg == -9 deg mod one tooth
 * pitch of 36 deg; gear3 -> 407.7 deg == -25 deg), so the whole train meshes. */
static void add_planet_gear(GLint teeth, GLfloat phi_deg, GLfloat width,
                            const GLfloat color[4])
{
    static const GLint sun_teeth = 20;   // must match the sun created in init_gears()
    GLfloat dist, rad, phi, inner, outer;

    if (n_gears >= MAX_GEARS)
        return;

    dist = R_PER_TOOTH * (sun_teeth + teeth);   // centre distance = R_sun + R_planet
    outer = R_PER_TOOTH * teeth;                // pitch radius = outer_radius
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
    gears[n_gears].speed = 1.0f;   // driver: rotation = angle
    gears[n_gears].phase = 0.0f;
    if (gears[n_gears].g != NULL)
        n_gears++;
}

static void init_gears(void)
{
    static const GLfloat red[4]     = { 0.8f, 0.1f, 0.0f, 1.0f };
    static const GLfloat green[4]   = { 0.0f, 0.8f, 0.2f, 1.0f };
    static const GLfloat blue[4]    = { 0.2f, 0.3f, 1.0f, 1.0f };
    static const GLfloat yellow[4]  = { 1.0f, 0.8f, 0.0f, 1.0f };
    static const GLfloat magenta[4] = { 0.9f, 0.2f, 0.8f, 1.0f };
    static const GLfloat cyan[4]    = { 0.1f, 0.8f, 0.9f, 1.0f };

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    GLenum smooth[4] = { PGL_SMOOTH4 };

    /* Create the shader program: gears' own per-vertex-lighting VS paired with
     * the built-in pgl_ff_gouraud FS (4 smooth RGBA varyings). */
    program = CreateProgram(vertex_shader, pgl_ff_gouraud, 4, smooth, GL_FALSE);
    if (program == 0) {
        klog("init_gears: failed to create program\n");
        return;
    }
    glUseProgram(program);

    /* Set uniform pointer */
    SetUniform(&uniforms);

    perspective(projection_matrix, 60.0f, (GLfloat)win_width / (GLfloat)win_height, 1.0f, 1024.0f);
    glViewport(0, 0, win_width, win_height);

    /* One red sun gear at the origin drives five differently-coloured planets
     * spread around it; each meshes with the sun and none overlap its neighbour. */
    add_sun_gear(20, 1.2f, red);
    add_planet_gear(10,   0.0f, 0.9f, green);
    add_planet_gear(14,  72.0f, 1.1f, blue);
    add_planet_gear(10, 144.0f, 0.8f, yellow);
    add_planet_gear(12, 216.0f, 1.0f, magenta);
    add_planet_gear(10, 288.0f, 0.9f, cyan);

    if (n_gears != 6) {
        printf("Failed to create gears (%d/6)\n", n_gears);
    }
}

static void reshape(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
        
    win_width = width;
    win_height = height;
    
    /* Resize the PortableGL framebuffer first */
    ResizeFramebuffer(width, height);
    backbuf = (pix_t*)GetBackBuffer();
    
    /* Update viewport */
    glViewport(0, 0, width, height);
    
    /* Update projection matrix with new aspect ratio */
    perspective(projection_matrix, 60.0f, (GLfloat)width / (GLfloat)height, 1.0f, 1024.0f);
}

static int fps = 0;
static int count = 0;
static uint32_t last_tic = 0;
static uint32_t last_frame_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    
    /* Update FPS statistics once per second */
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = count;
        count = 0;
    }
    count++;
}

static void on_repaint(xwin_t* xwin, graph_t* g)
{
    (void)xwin;

    // Calculate time-based animation for smooth rotation regardless of frame rate
    uint32_t current_tic;
    kernel_tic32(NULL, NULL, &current_tic);
    
    if (last_frame_tic == 0) {
        last_frame_tic = current_tic;
    }
    
    // Calculate delta time in seconds (kernel_tic is in microseconds)
    float dt = (current_tic - last_frame_tic) / 1000000.0f;
    last_frame_tic = current_tic;
    
    // Limit dt to avoid large jumps
    if (dt > 0.1f) dt = 0.1f;
    
    // Rotate at 240 degrees per second for smooth animation (4x speed)
    angle += 240.0f * dt;
    if (angle >= 360.0f)
        angle -= 360.0f;
    
    // Update camera orbit angle (30 degrees per second, 2x speed)
    camera_angle += 30.0f * dt;
    if (camera_angle >= 360.0f)
        camera_angle -= 360.0f;

    draw_gears();

    graph_t bg;
    graph_init(&bg, backbuf, win_width, win_height);
    graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
    update_fps();

    if (font != NULL) {
        char txt[32];
        snprintf(txt, sizeof(txt), "FPS: %d", fps);
        graph_draw_text_font(g, 8, 8, txt, font, 16, 0xff00ff00);
    }
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    int key = 0;
    if (ev->type == XEVT_IM) {
        key = ev->value.im.value;
        if (key == 27)
            xwin_close(xwin);
    }
}

static void on_resize(xwin_t* xwin)
{
    if(xwin == NULL || xwin->xinfo == NULL)
        return;
    reshape(xwin->xinfo->wsr.w, xwin->xinfo->wsr.h);
}

static bool _repaint = false;

static void loop(void* p)
{
    xwin_t* xwin = (xwin_t*)p;
    xwin_repaint(xwin);
    usleep(1000);
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    x_t x;
    x_init(&x, NULL);
    
    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Gears", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    
    xwin->on_repaint = on_repaint;
    xwin->on_event = on_event;
    xwin->on_resize = on_resize;

    pgl_set_max_vertices(PGL_TINY_MAX_VERTICES);

    /* Initialize PortableGL context */
    if (!init_glContext(&the_context, &backbuf, win_width, win_height)) {
        printf("Failed to initialize glContext\n");
        return 1;
    }

    /* Create and bind VAO (required in core profile) */
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    /* Initialize gears */
    init_gears();

    /* Load system font for the on-screen FPS readout */
    font = font_new(DEFAULT_SYSTEM_FONT, true);

    /* Make window visible after all initialization is done */
    xwin_set_visible(xwin, true);
    
    x_run(&x, xwin);
    
    /* Cleanup */
    xwin_destroy(xwin);
    if (font != NULL)
        font_free(font);
    free_glContext(&the_context);
    
    return 0;
}
