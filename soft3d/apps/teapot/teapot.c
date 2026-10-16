/*
 * Utah Teapot demo for EwokOS (soft3d / PortableGL)
 *
 * The classic Newell Utah teapot, defined as 32 bicubic Bezier patches over
 * 306 control points, tessellated at run time into a smooth triangle mesh with
 * analytic per-vertex normals.  The teapot spins about its own axis while the
 * camera orbits it, so the view angle changes continuously.
 *
 * Lighting is a per-vertex Blinn-Phong model (ambient + diffuse + specular)
 * evaluated in eye space against a world-fixed light.  Because the lit colour
 * is written to vs_output[0] as a vec4 RGBA and the program is paired with the
 * built-in pgl_ff_gouraud fragment shader, this app opts into the raspi5
 * VideoCore (V3D) GPU fast path: on a PGL_G3D build the rasterization +
 * interpolation + depth/cull stage runs on the GPU.  On every other platform
 * (and as the GPU fallback) it runs unchanged on the CPU rasterizer.
 *
 * An on-screen FPS readout, refreshed once per second, is drawn with the font
 * library after the framebuffer is blitted to the window.
 *
 * Teapot control-point / patch data is the canonical public-domain Utah teapot
 * dataset (the same numbers used by glutSolidTeapot).
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

/* ------------------------------------------------------------------ *
 * Utah teapot geometry: 306 control points and 32 bicubic patches.
 * ------------------------------------------------------------------ */
static const GLfloat cpdata[306][3] = {
    {1.4,0,2.4},{1.4,-0.784,2.4},{0.78,-1.4,2.4},{0,-1.4,2.4},
    {1.3375,0,2.53125},{1.3375,-0.749,2.53125},{0.749,-1.3375,2.53125},{0,-1.3375,2.53125},
    {1.4375,0,2.53125},{1.4375,-0.805,2.53125},{0.805,-1.4375,2.53125},{0,-1.4375,2.53125},
    {1.5,0,2.4},{1.5,-0.84,2.4},{0.84,-1.5,2.4},{0,-1.5,2.4},
    {-0.784,-1.4,2.4},{-1.4,-0.784,2.4},{-1.4,0,2.4},{-0.749,-1.3375,2.53125},
    {-1.3375,-0.749,2.53125},{-1.3375,0,2.53125},{-0.805,-1.4375,2.53125},{-1.4375,-0.805,2.53125},
    {-1.4375,0,2.53125},{-0.84,-1.5,2.4},{-1.5,-0.84,2.4},{-1.5,0,2.4},
    {-1.4,0.784,2.4},{-0.784,1.4,2.4},{0,1.4,2.4},{-1.3375,0.749,2.53125},
    {-0.749,1.3375,2.53125},{0,1.3375,2.53125},{-1.4375,0.805,2.53125},{-0.805,1.4375,2.53125},
    {0,1.4375,2.53125},{-1.5,0.84,2.4},{-0.84,1.5,2.4},{0,1.5,2.4},
    {0.784,1.4,2.4},{1.4,0.784,2.4},{0.749,1.3375,2.53125},{1.3375,0.749,2.53125},
    {0.805,1.4375,2.53125},{1.4375,0.805,2.53125},{0.84,1.5,2.4},{1.5,0.84,2.4},
    {1.75,0,1.875},{1.75,-0.98,1.875},{0.98,-1.75,1.875},{0,-1.75,1.875},
    {2,0,1.35},{2,-1.12,1.35},{1.12,-2,1.35},{0,-2,1.35},
    {2,0,0.9},{2,-1.12,0.9},{1.12,-2,0.9},{0,-2,0.9},
    {-0.98,-1.75,1.875},{-1.75,-0.98,1.875},{-1.75,0,1.875},{-1.12,-2,1.35},
    {-2,-1.12,1.35},{-2,0,1.35},{-1.12,-2,0.9},{-2,-1.12,0.9},
    {-2,0,0.9},{-1.75,0.98,1.875},{-0.98,1.75,1.875},{0,1.75,1.875},
    {-2,1.12,1.35},{-1.12,2,1.35},{0,2,1.35},{-2,1.12,0.9},
    {-1.12,2,0.9},{0,2,0.9},{0.98,1.75,1.875},{1.75,0.98,1.875},
    {1.12,2,1.35},{2,1.12,1.35},{1.12,2,0.9},{2,1.12,0.9},
    {2,0,0.45},{2,-1.12,0.45},{1.12,-2,0.45},{0,-2,0.45},
    {1.5,0,0.225},{1.5,-0.84,0.225},{0.84,-1.5,0.225},{0,-1.5,0.225},
    {1.5,0,0.15},{1.5,-0.84,0.15},{0.84,-1.5,0.15},{0,-1.5,0.15},
    {-1.12,-2,0.45},{-2,-1.12,0.45},{-2,0,0.45},{-0.84,-1.5,0.225},
    {-1.5,-0.84,0.225},{-1.5,0,0.225},{-0.84,-1.5,0.15},{-1.5,-0.84,0.15},
    {-1.5,0,0.15},{-2,1.12,0.45},{-1.12,2,0.45},{0,2,0.45},
    {-1.5,0.84,0.225},{-0.84,1.5,0.225},{0,1.5,0.225},{-1.5,0.84,0.15},
    {-0.84,1.5,0.15},{0,1.5,0.15},{1.12,2,0.45},{2,1.12,0.45},
    {0.84,1.5,0.225},{1.5,0.84,0.225},{0.84,1.5,0.15},{1.5,0.84,0.15},
    {-1.6,0,2.025},{-1.6,-0.3,2.025},{-1.5,-0.3,2.25},{-1.5,0,2.25},
    {-2.3,0,2.025},{-2.3,-0.3,2.025},{-2.5,-0.3,2.25},{-2.5,0,2.25},
    {-2.7,0,2.025},{-2.7,-0.3,2.025},{-3,-0.3,2.25},{-3,0,2.25},
    {-2.7,0,1.8},{-2.7,-0.3,1.8},{-3,-0.3,1.8},{-3,0,1.8},
    {-1.5,0.3,2.25},{-1.6,0.3,2.025},{-2.5,0.3,2.25},{-2.3,0.3,2.025},
    {-3,0.3,2.25},{-2.7,0.3,2.025},{-3,0.3,1.8},{-2.7,0.3,1.8},
    {-2.7,0,1.575},{-2.7,-0.3,1.575},{-3,-0.3,1.35},{-3,0,1.35},
    {-2.5,0,1.125},{-2.5,-0.3,1.125},{-2.65,-0.3,0.9375},{-2.65,0,0.9375},
    {-2,-0.3,0.9},{-1.9,-0.3,0.6},{-1.9,0,0.6},{-3,0.3,1.35},
    {-2.7,0.3,1.575},{-2.65,0.3,0.9375},{-2.5,0.3,1.125},{-1.9,0.3,0.6},
    {-2,0.3,0.9},{1.7,0,1.425},{1.7,-0.66,1.425},{1.7,-0.66,0.6},
    {1.7,0,0.6},{2.6,0,1.425},{2.6,-0.66,1.425},{3.1,-0.66,0.825},
    {3.1,0,0.825},{2.3,0,2.1},{2.3,-0.25,2.1},{2.4,-0.25,2.025},
    {2.4,0,2.025},{2.7,0,2.4},{2.7,-0.25,2.4},{3.3,-0.25,2.4},
    {3.3,0,2.4},{1.7,0.66,0.6},{1.7,0.66,1.425},{3.1,0.66,0.825},
    {2.6,0.66,1.425},{2.4,0.25,2.025},{2.3,0.25,2.1},{3.3,0.25,2.4},
    {2.7,0.25,2.4},{2.8,0,2.475},{2.8,-0.25,2.475},{3.525,-0.25,2.49375},
    {3.525,0,2.49375},{2.9,0,2.475},{2.9,-0.15,2.475},{3.45,-0.15,2.5125},
    {3.45,0,2.5125},{2.8,0,2.4},{2.8,-0.15,2.4},{3.2,-0.15,2.4},
    {3.2,0,2.4},{3.525,0.25,2.49375},{2.8,0.25,2.475},{3.45,0.15,2.5125},
    {2.9,0.15,2.475},{3.2,0.15,2.4},{2.8,0.15,2.4},{0,0,3.15},
    {0,-0.002,3.15},{0.002,0,3.15},{0.8,0,3.15},{0.8,-0.45,3.15},
    {0.45,-0.8,3.15},{0,-0.8,3.15},{0,0,2.85},{0.2,0,2.7},
    {0.2,-0.112,2.7},{0.112,-0.2,2.7},{0,-0.2,2.7},{-0.002,0,3.15},
    {-0.45,-0.8,3.15},{-0.8,-0.45,3.15},{-0.8,0,3.15},{-0.112,-0.2,2.7},
    {-0.2,-0.112,2.7},{-0.2,0,2.7},{0,0.002,3.15},{-0.8,0.45,3.15},
    {-0.45,0.8,3.15},{0,0.8,3.15},{-0.2,0.112,2.7},{-0.112,0.2,2.7},
    {0,0.2,2.7},{0.45,0.8,3.15},{0.8,0.45,3.15},{0.112,0.2,2.7},
    {0.2,0.112,2.7},{0.4,0,2.55},{0.4,-0.224,2.55},{0.224,-0.4,2.55},
    {0,-0.4,2.55},{1.3,0,2.55},{1.3,-0.728,2.55},{0.728,-1.3,2.55},
    {0,-1.3,2.55},{1.3,0,2.4},{1.3,-0.728,2.4},{0.728,-1.3,2.4},
    {0,-1.3,2.4},{-0.224,-0.4,2.55},{-0.4,-0.224,2.55},{-0.4,0,2.55},
    {-0.728,-1.3,2.55},{-1.3,-0.728,2.55},{-1.3,0,2.55},{-0.728,-1.3,2.4},
    {-1.3,-0.728,2.4},{-1.3,0,2.4},{-0.4,0.224,2.55},{-0.224,0.4,2.55},
    {0,0.4,2.55},{-1.3,0.728,2.55},{-0.728,1.3,2.55},{0,1.3,2.55},
    {-1.3,0.728,2.4},{-0.728,1.3,2.4},{0,1.3,2.4},{0.224,0.4,2.55},
    {0.4,0.224,2.55},{0.728,1.3,2.55},{1.3,0.728,2.55},{0.728,1.3,2.4},
    {1.3,0.728,2.4},{0,0,0},{1.5,0,0.15},{1.5,0.84,0.15},
    {0.84,1.5,0.15},{0,1.5,0.15},{1.5,0,0.075},{1.5,0.84,0.075},
    {0.84,1.5,0.075},{0,1.5,0.075},{1.425,0,0},{1.425,0.798,0},
    {0.798,1.425,0},{0,1.425,0},{-0.84,1.5,0.15},{-1.5,0.84,0.15},
    {-1.5,0,0.15},{-0.84,1.5,0.075},{-1.5,0.84,0.075},{-1.5,0,0.075},
    {-0.798,1.425,0},{-1.425,0.798,0},{-1.425,0,0},{-1.5,-0.84,0.15},
    {-0.84,-1.5,0.15},{0,-1.5,0.15},{-1.5,-0.84,0.075},{-0.84,-1.5,0.075},
    {0,-1.5,0.075},{-1.425,-0.798,0},{-0.798,-1.425,0},{0,-1.425,0},
    {0.84,-1.5,0.15},{1.5,-0.84,0.15},{0.84,-1.5,0.075},{1.5,-0.84,0.075},
    {0.798,-1.425,0},{1.425,-0.798,0}
};

/* Each patch lists 16 control-point indices (1-based) in row-major 4x4 order. */
static const int patches[32][16] = {
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
    {4,17,18,19,8,20,21,22,12,23,24,25,16,26,27,28},
    {19,29,30,31,22,32,33,34,25,35,36,37,28,38,39,40},
    {31,41,42,1,34,43,44,5,37,45,46,9,40,47,48,13},
    {13,14,15,16,49,50,51,52,53,54,55,56,57,58,59,60},
    {16,26,27,28,52,61,62,63,56,64,65,66,60,67,68,69},
    {28,38,39,40,63,70,71,72,66,73,74,75,69,76,77,78},
    {40,47,48,13,72,79,80,49,75,81,82,53,78,83,84,57},
    {57,58,59,60,85,86,87,88,89,90,91,92,93,94,95,96},
    {60,67,68,69,88,97,98,99,92,100,101,102,96,103,104,105},
    {69,76,77,78,99,106,107,108,102,109,110,111,105,112,113,114},
    {78,83,84,57,108,115,116,85,111,117,118,89,114,119,120,93},
    {121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136},
    {124,137,138,121,128,139,140,125,132,141,142,129,136,143,144,133},
    {133,134,135,136,145,146,147,148,149,150,151,152,69,153,154,155},
    {136,143,144,133,148,156,157,145,152,158,159,149,155,160,161,69},
    {162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177},
    {165,178,179,162,169,180,181,166,173,182,183,170,177,184,185,174},
    {174,175,176,177,186,187,188,189,190,191,192,193,194,195,196,197},
    {177,184,185,174,189,198,199,186,193,200,201,190,197,202,203,194},
    {204,204,204,204,207,208,209,210,211,211,211,211,212,213,214,215},
    {204,204,204,204,210,217,218,219,211,211,211,211,215,220,221,222},
    {204,204,204,204,219,224,225,226,211,211,211,211,222,227,228,229},
    {204,204,204,204,226,230,231,207,211,211,211,211,229,232,233,212},
    {212,213,214,215,234,235,236,237,238,239,240,241,242,243,244,245},
    {215,220,221,222,237,246,247,248,241,249,250,251,245,252,253,254},
    {222,227,228,229,248,255,256,257,251,258,259,260,254,261,262,263},
    {229,232,233,212,257,264,265,234,260,266,267,238,263,268,269,242},
    {270,270,270,270,279,280,281,282,275,276,277,278,93,120,119,114},
    {270,270,270,270,282,289,290,291,278,286,287,288,114,113,112,105},
    {270,270,270,270,291,298,299,300,288,295,296,297,105,104,103,96},
    {270,270,270,270,300,305,306,279,297,303,304,275,96,95,94,93}
};

#define NPATCH      32
#define TEAPOT_GRID 10          /* tessellation per patch; 10x10 quads each */

struct teapot_mesh {
    GLfloat* vertices;          /* 6 floats per vertex: x,y,z, nx,ny,nz */
    int nvertices;
    int patch_first[NPATCH];    /* start vertex of each patch (GL_TRIANGLES) */
    int patch_count[NPATCH];    /* vertex count of each patch */
    GLuint vbo;
};
static struct teapot_mesh teapot;

static GLfloat spin_angle = 0.0f;   /* teapot rotation about its own axis */
static GLfloat camera_angle = 0.0f; /* camera orbit about the teapot */
static GLfloat camera_elev = 15.0f; /* camera elevation (degrees) */
static GLfloat projection_matrix[16];

typedef struct {
    mat4 mvp_mat;
    mat4 modelview_mat;
    mat4 normal_mat;
    vec3 material_color;
    vec3 light_pos_eye;
} My_Uniforms;

static My_Uniforms uniforms;
static GLuint program;

#ifdef PGL_G3D
#include <portablegl/pgl_g3d.h>
#include "teapot_glsl.h"

/* WS5 full-GPU path: the teapot's vertex stage (MVP transform + Blinn-Phong
 * lighting) is compiled to VideoCore VII QPU and runs on the V3D GPU, with the
 * gouraud-passthrough FS shading the fragments - so the VERTEX stage is
 * hardware-accelerated too, not just rasterization.  g3d_active latches on only
 * after the program compiles, every VS uniform resolves by name, and the static
 * mesh uploads; any decline leaves it 0 and the CPU vertex_shader +
 * pgl_ff_gouraud path below renders the identical design. */
static int          g3d_active = 0;
static pgl_g3d_prog g3d_prog   = PGL_G3D_PROG_INVALID;
static pgl_g3d_mesh g3d_mesh   = PGL_G3D_MESH_INVALID;

/* VS uniform dword offsets within the per-draw blob, resolved by name at init
 * (never assumed: mesa dead-strips/renumbers per program).  -1 = unresolved.
 * When the geometry-rasterizer compute stage is attached, these resolve to the
 * KERNEL's bindings (pgl_g3d_shader_uniform_dword is compute-first), where
 * uMaterial/uLightEye are vec4 (the kernel's .w selects its lighting model);
 * the mat/v3 widths actually resolved are kept so the pack step writes the
 * matching number of dwords. */
static int g3d_off_mvp = -1, g3d_off_mv = -1, g3d_off_nm = -1;
static int g3d_off_mat = -1, g3d_off_light = -1, g3d_off_shade = -1;
static uint32_t g3d_comps_mat = 3, g3d_comps_light = 3;

/* The per-draw uniform blob staged for the daemon's QUNIFORM interpreter. */
static uint32_t g3d_unif[PGL_G3D_MAX_UNIF];
static uint32_t g3d_unif_count = 0;
#endif /* PGL_G3D */

/* ------------------------------------------------------------------ *
 * Minimal matrix helpers (row-vector convention, as in gears.c).
 * ------------------------------------------------------------------ */
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
    if (f_len == 0.0f) f_len = 1.0f;
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;

    s[0] = f[1]*up_z - f[2]*up_y;
    s[1] = f[2]*up_x - f[0]*up_z;
    s[2] = f[0]*up_y - f[1]*up_x;
    s_len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    if (s_len == 0.0f) s_len = 1.0f;
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

/* ------------------------------------------------------------------ *
 * Bezier patch tessellation.
 * ------------------------------------------------------------------ */
static void bernstein(float t, float b[4])
{
    float mt = 1.0f - t;
    b[0] = mt * mt * mt;
    b[1] = 3.0f * t * mt * mt;
    b[2] = 3.0f * t * t * mt;
    b[3] = t * t * t;
}

static void bernstein_d(float t, float d[4])
{
    float mt = 1.0f - t;
    d[0] = -3.0f * mt * mt;
    d[1] = 3.0f * mt * mt - 6.0f * t * mt;
    d[2] = 6.0f * t * mt - 3.0f * t * t;
    d[3] = 3.0f * t * t;
}

/* Evaluate one bicubic Bezier patch at (u,v); write position and the unit
 * surface normal (cross product of the u and v tangents). */
static void eval_patch(const int* pidx, float u, float v, float pos[3], float nrm[3])
{
    float bu[4], bv[4], du[4], dv[4];
    bernstein(u, bu);
    bernstein(v, bv);
    bernstein_d(u, du);
    bernstein_d(v, dv);

    float px = 0, py = 0, pz = 0;
    float tu_x = 0, tu_y = 0, tu_z = 0;   /* dS/du */
    float tv_x = 0, tv_y = 0, tv_z = 0;   /* dS/dv */

    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            const GLfloat* cp = cpdata[pidx[i * 4 + j] - 1];
            float w  = bu[i] * bv[j];
            float wu = du[i] * bv[j];
            float wv = bu[i] * dv[j];
            px += w * cp[0];  py += w * cp[1];  pz += w * cp[2];
            tu_x += wu * cp[0]; tu_y += wu * cp[1]; tu_z += wu * cp[2];
            tv_x += wv * cp[0]; tv_y += wv * cp[1]; tv_z += wv * cp[2];
        }
    }

    pos[0] = px; pos[1] = py; pos[2] = pz;

    float nx = tu_y * tv_z - tu_z * tv_y;
    float ny = tu_z * tv_x - tu_x * tv_z;
    float nz = tu_x * tv_y - tu_y * tv_x;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 1e-8f) {
        nx /= len; ny /= len; nz /= len;
    } else {
        nx = 0; ny = 0; nz = 1.0f;
    }
    nrm[0] = nx; nrm[1] = ny; nrm[2] = nz;
}

static GLfloat* emit_vert(GLfloat* v, const float* pos, const float* nrm)
{
    v[0] = pos[0]; v[1] = pos[1]; v[2] = pos[2];
    v[3] = nrm[0]; v[4] = nrm[1]; v[5] = nrm[2];
    return v + 6;
}

static void build_teapot(void)
{
    const int G = TEAPOT_GRID;
    const int per_patch = G * G * 2 * 3;      /* triangles * 3 verts */
    const int total = NPATCH * per_patch;

    teapot.vertices = (GLfloat*)malloc(total * 6 * sizeof(GLfloat));
    if (teapot.vertices == NULL) {
        slog("build_teapot: out of memory\n");
        return;
    }

    /* Per-patch lattice of positions/normals (reused for every patch). */
    static float lat_pos[(TEAPOT_GRID + 1) * (TEAPOT_GRID + 1)][3];
    static float lat_nrm[(TEAPOT_GRID + 1) * (TEAPOT_GRID + 1)][3];

    GLfloat* v = teapot.vertices;
    int p, a, b;
    for (p = 0; p < NPATCH; p++) {
        const int* pidx = patches[p];
        teapot.patch_first[p] = (int)(v - teapot.vertices) / 6;

        for (a = 0; a <= G; a++) {
            float u = (float)a / (float)G;
            for (b = 0; b <= G; b++) {
                float w = (float)b / (float)G;
                int idx = a * (G + 1) + b;
                eval_patch(pidx, u, w, lat_pos[idx], lat_nrm[idx]);
            }
        }

        for (a = 0; a < G; a++) {
            for (b = 0; b < G; b++) {
                int i00 = a * (G + 1) + b;
                int i10 = (a + 1) * (G + 1) + b;
                int i01 = a * (G + 1) + b + 1;
                int i11 = (a + 1) * (G + 1) + b + 1;

                v = emit_vert(v, lat_pos[i00], lat_nrm[i00]);
                v = emit_vert(v, lat_pos[i10], lat_nrm[i10]);
                v = emit_vert(v, lat_pos[i11], lat_nrm[i11]);

                v = emit_vert(v, lat_pos[i00], lat_nrm[i00]);
                v = emit_vert(v, lat_pos[i11], lat_nrm[i11]);
                v = emit_vert(v, lat_pos[i01], lat_nrm[i01]);
            }
        }

        teapot.patch_count[p] = (int)(v - teapot.vertices) / 6 - teapot.patch_first[p];
    }

    teapot.nvertices = (int)(v - teapot.vertices) / 6;

    glGenBuffers(1, &teapot.vbo);
    if (teapot.vbo == 0) {
        slog("build_teapot: glGenBuffers failed\n");
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, teapot.vbo);
    glBufferData(GL_ARRAY_BUFFER, teapot.nvertices * 6 * sizeof(GLfloat),
                 teapot.vertices, GL_STATIC_DRAW);
}

/* ------------------------------------------------------------------ *
 * Vertex shader: per-vertex Blinn-Phong lighting in eye space, written
 * as a vec4 RGBA and paired with the built-in pgl_ff_gouraud fragment
 * shader so the raspi5 V3D back end can offload the per-pixel stage.
 * ------------------------------------------------------------------ */
static float fpow(float b, int e)
{
    float r = 1.0f;
    while (e > 0) {
        if (e & 1) r *= b;
        b *= b;
        e >>= 1;
    }
    return r;
}

static float clampf01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

void vertex_shader(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms_p)
{
    vec4* vs_out = (vec4*)vs_output;
    My_Uniforms* u = (My_Uniforms*)uniforms_p;

    /* Eye-space position and normal. */
    vec4 Pe = mult_m4_v4(u->modelview_mat, v_attrs[0]);
    vec3 P = make_v3(Pe.x, Pe.y, Pe.z);

    vec4 Ne = mult_m4_v4(u->normal_mat, v_attrs[1]);
    vec3 N = norm_v3(make_v3(Ne.x, Ne.y, Ne.z));

    /* View direction (eye at the origin of eye space). */
    vec3 V = norm_v3(scale_v3(P, -1.0f));

    /* Two-sided lighting: flip the normal if it faces away from the viewer so
     * the interior/back faces of the tessellated patches still shade correctly
     * regardless of triangle winding (we render with culling disabled). */
    if (dot_v3s(N, V) < 0.0f)
        N = scale_v3(N, -1.0f);

    /* Light direction from the vertex to the (eye-space) light position. */
    vec3 L = norm_v3(sub_v3s(u->light_pos_eye, P));

    float diff = dot_v3s(N, L);
    if (diff < 0.0f) diff = 0.0f;

    vec3 H = norm_v3(add_v3s(L, V));
    float spec = dot_v3s(N, H);
    if (spec < 0.0f) spec = 0.0f;
    spec = fpow(spec, 48);

    const float ambient = 0.22f;
    float shade = ambient + 0.78f * diff;

    vec3 base = u->material_color;
    float r = clampf01(base.x * shade + spec);
    float g = clampf01(base.y * shade + spec);
    float b = clampf01(base.z * shade + spec);
    vs_out[0] = make_v4(r, g, b, 1.0f);

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[0]);
}

#ifdef PGL_G3D
/* ------------------------------------------------------------------ *
 * WS5 full-GPU path: pack the per-draw uniform blob and issue the 32
 * per-patch compiled-mesh draws.  The matrices are uploaded in
 * portablegl's column-major flat order (teapot_glsl.h documents why
 * this is bit-identical to the C mult_m4_v4 path).
 * ------------------------------------------------------------------ */
static void g3d_set_f(int off, float v)
{
    uint32_t bits;
    if (off < 0 || (uint32_t)off >= PGL_G3D_MAX_UNIF)
        return;
    memcpy(&bits, &v, sizeof(bits));
    g3d_unif[off] = bits;
    if ((uint32_t)off + 1u > g3d_unif_count)
        g3d_unif_count = (uint32_t)off + 1u;
}

static void g3d_set_mat4(int off, const GLfloat* m)
{
    int i;
    for (i = 0; i < 16; i++)
        g3d_set_f(off + i, m[i]);
}

static void g3d_set_v3(int off, vec3 v)
{
    g3d_set_f(off + 0, v.x);
    g3d_set_f(off + 1, v.y);
    g3d_set_f(off + 2, v.z);
}

/* Compile the teapot VS+FS to QPU, resolve the VS uniforms by name, and stage
 * the static pos+normal mesh.  On any decline g3d_active stays 0 and the CPU
 * path renders.  Called once after build_teapot() (and after pglGpuBegin()). */
static void g3d_init(void)
{
    char errlog[512];
    uint32_t comps;

    g3d_active = 0;
    errlog[0] = '\0';

    g3d_prog = pgl_g3d_program_compile(teapot_vs_glsl, teapot_fs_glsl,
                                       errlog, sizeof(errlog));
    if (!pgl_g3d_program_valid(g3d_prog)) {
        slog("teapot: GPU shader compile failed: %s\n", errlog);
        return;
    }

    /* Resolve every VS uniform by name; a mat4 reports 16 components, a vec3
     * reports 3, a vec4 (geom kernel's widened material/light slots) reports 4.
     * A missing/mis-sized uniform means the blob cannot feed the QPU, so
     * decline to the CPU path rather than draw with garbage.  uShade exists
     * only on the geom kernel (ambient/diffuse/specular weights); its absence
     * just means the binner route, so it never declines. */
    g3d_off_mvp = pgl_g3d_shader_uniform_dword(g3d_prog, "uMVP", &comps);
    if (g3d_off_mvp < 0 || comps != 16) { slog("teapot: uMVP unresolved\n"); return; }
    g3d_off_mv = pgl_g3d_shader_uniform_dword(g3d_prog, "uModelView", &comps);
    if (g3d_off_mv < 0 || comps != 16) { slog("teapot: uModelView unresolved\n"); return; }
    g3d_off_nm = pgl_g3d_shader_uniform_dword(g3d_prog, "uNormalMat", &comps);
    if (g3d_off_nm < 0 || comps != 16) { slog("teapot: uNormalMat unresolved\n"); return; }
    g3d_off_mat = pgl_g3d_shader_uniform_dword(g3d_prog, "uMaterial", &g3d_comps_mat);
    if (g3d_off_mat < 0 || (g3d_comps_mat != 3 && g3d_comps_mat != 4)) { slog("teapot: uMaterial unresolved\n"); return; }
    g3d_off_light = pgl_g3d_shader_uniform_dword(g3d_prog, "uLightEye", &g3d_comps_light);
    if (g3d_off_light < 0 || (g3d_comps_light != 3 && g3d_comps_light != 4)) { slog("teapot: uLightEye unresolved\n"); return; }
    g3d_off_shade = pgl_g3d_shader_uniform_dword(g3d_prog, "uShade", &comps);

    if (teapot.vertices == NULL || teapot.nvertices <= 0)
        return;

    /* Stage the static interleaved pos+normal mesh once (stride 6 floats). */
    g3d_mesh = pgl_g3d_upload_mesh(teapot.vertices, (uint32_t)teapot.nvertices, 6u);
    if (g3d_mesh == PGL_G3D_MESH_INVALID) {
        slog("teapot: GPU mesh upload declined\n");
        return;
    }

    g3d_active = 1;
    slog("teapot: GPU compiled-mesh path active (prog=%d mesh=%d)\n",
         g3d_prog, g3d_mesh);
}
#endif /* PGL_G3D */

static void draw_teapot(void)
{
    GLfloat view[16];
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat mvp[16];

    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Camera orbits the teapot at a fixed radius, looking at the origin. */
    float dist = 9.0f;
    float elev = camera_elev * M_PI / 180.0f;
    float azim = camera_angle * M_PI / 180.0f;
    float cam_y = dist * sinf(elev);
    float r = dist * cosf(elev);
    float cam_x = r * sinf(azim);
    float cam_z = r * cosf(azim);
    look_at(view, cam_x, cam_y, cam_z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    /* Model transform: spin the teapot about its own up axis, reorient the
     * data's +Z (lid direction) to world +Y, then drop it to be centred. */
    memcpy(model_view, view, sizeof(model_view));
    translate(model_view, 0.0f, -1.3f, 0.0f);
    rotate(model_view, -90.0f, 1.0f, 0.0f, 0.0f);
    rotate(model_view, spin_angle, 0.0f, 0.0f, 1.0f);

    memcpy(mvp, projection_matrix, sizeof(mvp));
    multiply(mvp, model_view);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);

    memcpy(&uniforms.mvp_mat, mvp, sizeof(mat4));
    memcpy(&uniforms.modelview_mat, model_view, sizeof(mat4));
    memcpy(&uniforms.normal_mat, normal_matrix, sizeof(mat4));
    uniforms.material_color.x = 0.85f;
    uniforms.material_color.y = 0.32f;
    uniforms.material_color.z = 0.20f;

    /* World-fixed light transformed into eye space so highlights sweep across
     * the surface as the teapot spins and the camera orbits. */
    vec4 lw = make_v4(6.0f, 8.0f, 10.0f, 1.0f);
    vec4 le = mult_m4_v4(view, lw);
    uniforms.light_pos_eye = make_v3(le.x, le.y, le.z);

#ifdef PGL_G3D
    /* Full-GPU path: pack the matrices by name and issue the whole mesh as ONE
     * compiled-mesh draw (VS runs the transform + lighting on the QPU).
     * glClear above already drove pgl_g3d_begin_frame, so this frame's first
     * dispatch clears the persistent colour+Z and the rest LOAD/accumulate.
     * GL_LESS depth + Z-write, cull disabled (two-sided lighting). */
    if (g3d_active) {
        int ok = 1;

        g3d_unif_count = 0;
        g3d_set_mat4(g3d_off_mvp, uniforms.mvp_mat);
        g3d_set_mat4(g3d_off_mv,  uniforms.modelview_mat);
        g3d_set_mat4(g3d_off_nm,  uniforms.normal_mat);
        g3d_set_v3(g3d_off_mat,   uniforms.material_color);
        if (g3d_comps_mat == 4)
            g3d_set_f(g3d_off_mat + 3, 0.0f);   /* geom kernel reads .rgb only */
        g3d_set_v3(g3d_off_light, uniforms.light_pos_eye);
        if (g3d_comps_light == 4)
            g3d_set_f(g3d_off_light + 3, 1.0f); /* .w>0: positional-light model */
        if (g3d_off_shade >= 0) {
            /* geom kernel gouraud weights, mirroring the VS exactly:
             * shade = 0.22 + 0.78*diff, specular weight 1. */
            g3d_set_f(g3d_off_shade + 0, 0.22f);
            g3d_set_f(g3d_off_shade + 1, 0.78f);
            g3d_set_f(g3d_off_shade + 2, 1.0f);
            g3d_set_f(g3d_off_shade + 3, 0.0f);
        }
        pgl_g3d_set_uniforms(g3d_unif, g3d_unif_count);

        /* Whole-mesh dispatch: the teapot is ONE rigid mesh under a single
         * transform, so draw all of it in as few GPU dispatches as the staging
         * cap allows (exactly one at the current 19200 vertices, well under
         * PGL_G3D_MAX_VERTS).  The previous per-patch loop issued 32 separate
         * dispatches per frame, and every dispatch pays a large fixed cost that
         * is independent of its ~200 triangles: a whole-surface L2 clean+
         * invalidate of the colour+depth targets (~2.4 MB), a CSD_DONE IRQ wait,
         * five canvas attach/detach and a blocking IPC round-trip.  That 32x
         * fixed overhead is what made the GPU path slower than the CPU
         * rasterizer; batching into one dispatch removes ~97% of it.  Depth
         * stays coherent because the persistent Z surface carries atomicMin
         * across any chunks. */
        {
            uint32_t done = 0;
            while (done < (uint32_t)teapot.nvertices) {
                uint32_t chunk = (uint32_t)teapot.nvertices - done;
                if (chunk > PGL_G3D_MAX_VERTS)
                    chunk = PGL_G3D_MAX_VERTS;
                if (!pgl_g3d_draw_mesh_compiled(g3d_prog, g3d_mesh,
                                                done, chunk,
                                                PGL_G3D_DEPTH_LESS, 1u,
                                                PGL_G3D_CULL_NONE)) {
                    ok = 0;
                    break;
                }
                done += chunk;
            }
        }
        if (ok)
            return;   /* whole teapot rendered on the QPU */

        /* A decline latches the GPU off; re-clear and fall through to the CPU
         * path so this frame is not left half GPU / half CPU.  Every later
         * frame stays on the CPU (g3d_active cleared). */
        g3d_active = 0;
        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
#endif /* PGL_G3D */

    if (teapot.vbo == 0 || teapot.vertices == NULL)
        return;

    glBindBuffer(GL_ARRAY_BUFFER, teapot.vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
                          (void*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    /* One draw call per patch keeps every dispatch small (GPU friendly) while
     * sharing a single VBO. */
    int p;
    for (p = 0; p < NPATCH; p++)
        glDrawArrays(GL_TRIANGLES, teapot.patch_first[p], teapot.patch_count[p]);

    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void init_teapot(void)
{
    glEnable(GL_DEPTH_TEST);
    /* Culling left disabled: the tessellated patches are lit two-sided. */

    GLenum smooth[4] = { PGL_SMOOTH4 };

    /* Custom per-vertex-lighting VS paired with the built-in pgl_ff_gouraud FS
     * (4 smooth RGBA varyings) -> raspi5 V3D GPU fast path; CPU elsewhere. */
    program = CreateProgram(vertex_shader, pgl_ff_gouraud, 4, smooth, GL_FALSE);
    if (program == 0) {
        slog("init_teapot: failed to create program\n");
        return;
    }
    glUseProgram(program);
    SetUniform(&uniforms);

    perspective(projection_matrix, 45.0f, (GLfloat)win_width / (GLfloat)win_height, 0.5f, 100.0f);
    glViewport(0, 0, win_width, win_height);

    build_teapot();

#ifdef PGL_G3D
    /* Compile the VS+FS to QPU and stage the mesh now that the geometry exists
     * and (if pglGpuBegin succeeded) the GPU target is up.  Any decline leaves
     * g3d_active = 0 and the CPU path renders identically. */
    g3d_init();
#endif
}

static void reshape(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    win_width = width;
    win_height = height;

    ResizeFramebuffer(width, height);
    backbuf = (pix_t*)GetBackBuffer();

    glViewport(0, 0, width, height);
    perspective(projection_matrix, 45.0f, (GLfloat)width / (GLfloat)height, 0.5f, 100.0f);
}

static int fps = 0;
static int count = 0;
static uint32_t last_tic = 0;
static uint32_t last_frame_tic = 0;
static int g_last_px = -1;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);

    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = count;
        count = 0;
#ifdef PGL_G3D
        /* headless-measurable FPS + pixel-diff probe: px counts back-buffer
         * pixels that differ from the top-left (clear) pixel, so a blank frame
         * (GPU drew nothing, px~0) is distinguishable from a rendered one
         * (px large), alongside the gpu path flag, all from stdout alone. */
        {
            uint32_t px = 0;
            if (backbuf != NULL && win_width > 0 && win_height > 0) {
                pix_t ref = backbuf[0];
                uint32_t n = (uint32_t)win_width * (uint32_t)win_height;
                uint32_t i;
                for (i = 0; i < n; i++)
                    if (backbuf[i] != ref)
                        px++;
            }
            g_last_px = (int)px;
            printf("TEAPOT FPS: %d (gpu=%d px=%u %dx%d)\n",
                   fps, g3d_active, px, win_width, win_height);
            fflush(stdout);
        }
#endif
    }
    count++;
}

static void on_repaint(xwin_t* xwin, graph_t* g)
{
    (void)xwin;

    uint32_t current_tic;
    kernel_tic32(NULL, NULL, &current_tic);

    if (last_frame_tic == 0)
        last_frame_tic = current_tic;

    float dt = (current_tic - last_frame_tic) / 1000000.0f;
    last_frame_tic = current_tic;
    if (dt > 0.1f) dt = 0.1f;

    /* Continuous angle change: teapot spins, camera orbits. */
    spin_angle += 45.0f * dt;
    if (spin_angle >= 360.0f) spin_angle -= 360.0f;

    camera_angle += 20.0f * dt;
    if (camera_angle >= 360.0f) camera_angle -= 360.0f;

    draw_teapot();

    /* The GPU path adopts pgl_g3d_pixels() as the back buffer on its first
     * draw (pglGpuBegin frees the CPU buffer init_glContext handed us), so
     * re-read the live pointer every frame instead of only in reshape -
     * otherwise the first frames blit a stale, freed buffer and the window
     * stays blank until a resize. */
    backbuf = (pix_t*)GetBackBuffer();

    graph_t bg;
    graph_init(&bg, backbuf, win_width, win_height);
    graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
    update_fps();

    if (font != NULL) {
        char txt[64];
#ifdef PGL_G3D
        /* on-screen readout carries the gpu path flag + live dims so the
         * blank-until-resize state is visible without ssh stdout. */
        snprintf(txt, sizeof(txt), "FPS:%d gpu:%d px:%d %dx%d",
                 fps, g3d_active, g_last_px, win_width, win_height);
#else
        snprintf(txt, sizeof(txt), "Teapot  FPS: %d", fps);
#endif
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
    if (xwin == NULL || xwin->xinfo == NULL)
        return;
    reshape(xwin->xinfo->wsr.w, xwin->xinfo->wsr.h);
}

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
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Teapot", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }

    xwin->on_repaint = on_repaint;
    xwin->on_event = on_event;
    xwin->on_resize = on_resize;

    /* Each per-patch draw stays well under 5000 vertices, so TINY is enough. */
    pgl_set_max_vertices(PGL_TINY_MAX_VERTICES);

    if (!init_glContext(&the_context, &backbuf, win_width, win_height)) {
        printf("Failed to initialize glContext\n");
        return 1;
    }

#ifdef PGL_G3D
    /* Bring up the V3D GPU target and adopt pgl_g3d_pixels() as the back
     * buffer, so the compiled-mesh draws and the CPU fallback share one surface
     * and the present blit stays zero-copy.  On success refresh backbuf; on
     * failure the CPU back buffer from init_glContext stands (g3d_init then
     * declines and the CPU path renders). */
    if (pglGpuBegin(win_width, win_height))
        backbuf = (pix_t*)pglGetBackBuffer();
#endif

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    init_teapot();

    font = font_new(DEFAULT_SYSTEM_FONT, true);

    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    xwin_destroy(xwin);
    if (font != NULL)
        font_free(font);
    if (teapot.vertices != NULL)
        free(teapot.vertices);
    free_glContext(&the_context);

    return 0;
}
