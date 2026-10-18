/*
 * GLMatrix - a true-3D "digital rain" screen blanker for EwokOS (soft3d / EGL + GLES2)
 *
 * A GLES2 homage to xscreensaver's "glmatrix" (and phkehl's glmatrix-experiment,
 * https://github.com/phkehl/glmatrix-experiment): the falling code from the title
 * sequence of "The Matrix", rendered as a REAL 3D scene rather than a flat
 * screen-space grid.  A perspective camera looks down through a 3D field of
 * vertical glyph columns spread across X and depth (Z); the glyphs drop in -Y,
 * each column has a bright leading head and a fading trail, distant glyphs are
 * dimmed by fog, and the whole field is slowly tilted and rotated so the depth
 * reads unmistakably.
 *
 * GPU ACCELERATION
 * ----------------
 * The shaders are deliberately the EXACT shape libGLESv2 recognises for its
 * native gouraud fast-path - the same shape teapot uses:
 *      VS:  vColor = aColor;  gl_Position = uMVP * aPosition;
 *      FS:  gl_FragColor = vColor;
 * On Raspberry Pi 5 (PGL_G3D) that shape is claimed by the VideoCore VII V3D 7.1
 * geometry kernel (the colour variant: per-vertex position + colour, opaque,
 * depth-resolved with an atomicMin) and rasterised entirely in compute; on QEMU
 * virt / generic targets the very same source runs on libGLESv2's compiled CPU
 * gouraud fast-path.  No app-side branching and no portablegl types - the Dear
 * ImGui stats window drawn on top reports which path is live.
 *
 * Because that kernel carries per-vertex COLOUR but no texture, glyphs are built
 * as geometry: each 8x8 procedural mark is run-length encoded into horizontal
 * quads.  The billboards are expanded on the CPU in EYE space (so they always
 * face the camera), and uMVP is just the projection matrix - the model-view
 * transform and the brightness/fog ramp are folded into the vertices, exactly as
 * teapot folds its CPU lighting into a vec4 colour attribute.
 *
 * The window surface, present blit and resize are all owned by libEGL; the app
 * only drives frames from its run loop, following the teapot/gears demos.
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

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

static int win_width = 800;
static int win_height = 600;

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

/* ------------------------------------------------------------------ *
 * 3D field layout (world units).  Columns sit on an NCOL x NDEPTH grid
 * in the X/Z plane; glyphs fall along -Y inside each column.
 * ------------------------------------------------------------------ */
#define GLYPH_PX     8
#define NGLYPH_SET   72          /* distinct procedural marks            */

#define NCOL         14          /* columns across X                     */
#define NDEPTH       4           /* layers across Z (depth)              */
#define NCOLS        (NCOL * NDEPTH)
#define MAXTRAIL     16

#define SPACE_X      1.45f
#define SPACE_Z      2.80f
#define GY           0.85f       /* world distance between glyphs in a col */
#define YTOP         9.00f
#define YBOT        (-9.00f)
#define GLYPH_SIZE   0.62f       /* world/eye size of one glyph billboard */

#define CAM_DIST     15.0f
#define CAM_FOV      55.0f
#define CAM_NEAR     0.50f
#define CAM_FAR      80.0f
#define FOG_NEAR     8.0f
#define FOG_FAR      30.0f

#define MAXVERTS     40000       /* scratch capacity (vertices)          */

/* ------------------------------------------------------------------ *
 * Small deterministic PRNG (xorshift32) - used for the glyph set and
 * the rain simulation.
 * ------------------------------------------------------------------ */
static uint32_t g_seed = 0x9E3779B9u;

static uint32_t xrnd(void)
{
    uint32_t x = g_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_seed = x;
    return x;
}

static float xrndf(void)
{
    return (float)(xrnd() & 0xFFFFFFu) / 16777216.0f;   /* [0,1) */
}

static int xrnd_range(int lo, int hi)
{
    if (hi < lo) return lo;
    return lo + (int)(xrnd() % (uint32_t)(hi - lo + 1));
}

/* ------------------------------------------------------------------ *
 * Procedural glyph set: a few connected strokes on an 8x8 grid, giving
 * marks that read as half-width-katakana-style code at rain speed.
 * Bit (7-col) of rows[r] is the pixel at (col, r); bit 7 is left-most.
 * ------------------------------------------------------------------ */
static unsigned char g_glyphset[NGLYPH_SET][GLYPH_PX];

static void glyph_setpx(unsigned char rows[GLYPH_PX], int c, int r)
{
    if (c >= 0 && c < GLYPH_PX && r >= 0 && r < GLYPH_PX)
        rows[r] = (unsigned char)(rows[r] | (0x80 >> c));
}

static void gen_glyph(unsigned char rows[GLYPH_PX])
{
    int strokes = xrnd_range(1, 3);
    int s;
    memset(rows, 0, GLYPH_PX);
    for (s = 0; s < strokes; ++s) {
        int t = xrnd_range(0, 3);
        if (t == 0) {                       /* vertical stroke */
            int c = xrnd_range(1, 6);
            int r0 = xrnd_range(0, 3);
            int r1 = xrnd_range(r0 + 2, GLYPH_PX - 1);
            int r;
            for (r = r0; r <= r1; ++r) glyph_setpx(rows, c, r);
        } else if (t == 1) {                /* horizontal stroke */
            int r = xrnd_range(0, GLYPH_PX - 1);
            int c0 = xrnd_range(0, 3);
            int c1 = xrnd_range(c0 + 2, GLYPH_PX - 1);
            int c;
            for (c = c0; c <= c1; ++c) glyph_setpx(rows, c, r);
        } else if (t == 2) {                /* diagonal stroke */
            int c = xrnd_range(0, 3);
            int r = xrnd_range(0, 3);
            int dir = (xrnd() & 1u) ? 1 : -1;
            int len = xrnd_range(3, 5);
            int k;
            for (k = 0; k < len; ++k) glyph_setpx(rows, c + k * dir, r + k);
        } else {                            /* short tick / dot */
            int c = xrnd_range(1, 6);
            int r = xrnd_range(1, 6);
            glyph_setpx(rows, c, r);
            glyph_setpx(rows, c, r + 1);
        }
    }
}

static void build_glyphset(void)
{
    int i;
    g_seed = 0x9E3779B9u;                   /* deterministic set */
    for (i = 0; i < NGLYPH_SET; ++i)
        gen_glyph(g_glyphset[i]);
}

/* ------------------------------------------------------------------ *
 * Rain simulation state: one entry per column (world x/z, head, speed,
 * trail) and one per cell (glyph index + next flicker time).  The 3D
 * world layout is fixed, so this is built once (not per resize).
 * ------------------------------------------------------------------ */
static float g_colx[NCOLS], g_colz[NCOLS];
static float g_head[NCOLS], g_speed[NCOLS];
static int   g_trail[NCOLS];
static unsigned char g_cellglyph[NCOLS * MAXTRAIL];
static float g_cellflick[NCOLS * MAXTRAIL];
static float g_time = 0.0f;

static void reset_column(int c)
{
    g_trail[c] = xrnd_range(6, 13);
    g_speed[c] = 3.5f + xrndf() * 4.0f;              /* world units / sec */
    g_head[c]  = YTOP + xrndf() * 6.0f;              /* restart above top */
}

static void build_state(void)
{
    int i, j, c, k;
    g_seed = 0x2545F491u;                            /* distinct stream  */
    for (j = 0; j < NDEPTH; ++j) {
        for (i = 0; i < NCOL; ++i) {
            c = j * NCOL + i;
            g_colx[c] = ((float)i - (float)(NCOL - 1) * 0.5f) * SPACE_X;
            g_colz[c] = ((float)(NDEPTH - 1) * 0.5f - (float)j) * SPACE_Z;
            g_trail[c] = xrnd_range(6, 13);
            g_speed[c] = 3.5f + xrndf() * 4.0f;
            /* Spread initial heads through the field so rain is populated
             * on the first frame instead of falling in unison. */
            g_head[c] = YBOT + xrndf() * (YTOP - YBOT + 12.0f);
        }
    }
    for (c = 0; c < NCOLS; ++c) {
        for (k = 0; k < MAXTRAIL; ++k) {
            int cell = c * MAXTRAIL + k;
            g_cellglyph[cell] = (unsigned char)(xrnd() % (uint32_t)NGLYPH_SET);
            g_cellflick[cell] = g_time + xrndf() * 0.6f;
        }
    }
}

static void update_rain(float dt)
{
    int c, k;
    g_time += dt;

    for (c = 0; c < NCOLS; ++c) {
        g_head[c] -= g_speed[c] * dt;                /* fall along -Y */
        if (g_head[c] - (float)g_trail[c] * GY < YBOT - 1.0f)
            reset_column(c);                         /* trail fully below */
    }

    for (c = 0; c < NCOLS; ++c) {
        for (k = 0; k < g_trail[c]; ++k) {
            int cell = c * MAXTRAIL + k;
            if (g_time >= g_cellflick[cell]) {
                g_cellglyph[cell] = (unsigned char)(xrnd() % (uint32_t)NGLYPH_SET);
                g_cellflick[cell] = g_time + 0.08f + xrndf() * 0.7f;
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 * Shaders: the EXACT native-gouraud fast-path / GPU colour-kernel shape
 * (teapot's).  Both attributes are vec4; uMVP is column-major.
 * ------------------------------------------------------------------ */
static const char *rain_vs =
    "attribute vec4 aPosition;\n"
    "attribute vec4 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = uMVP * aPosition;\n"
    "}\n";

static const char *rain_fs =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

static GLuint program = 0;
static GLuint g_posvbo = 0, g_colvbo = 0;
static GLint attr_pos = -1, attr_color = -1;
static GLint loc_mvp = -1;

static GLfloat *g_pos = NULL;      /* MAXVERTS * 4 (eye-space billboard)  */
static GLfloat *g_col = NULL;      /* MAXVERTS * 4 (rgb + a)              */

static GLfloat projection_matrix[16];

/* ------------------------------------------------------------------ *
 * Minimal matrix helpers (row-vector convention, identical to teapot.c
 * so uMVP uploads bit-identically and hits the same GPU kernel).
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

static void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat z_near, GLfloat z_far)
{
    GLfloat tmp[16];
    GLfloat radians, sine, cosine, delta_z, cotangent;
    identity(tmp);

    radians = fovy / 2.0f * M_PI / 180.0f;
    sine = sinf(radians);
    cosine = cosf(radians);
    delta_z = z_far - z_near;
    if (delta_z == 0 || sine == 0 || aspect == 0)
        return;
    cotangent = cosine / sine;

    tmp[0] = cotangent / aspect;
    tmp[5] = cotangent;
    tmp[10] = -(z_far + z_near) / delta_z;
    tmp[11] = -1.0f;
    tmp[14] = -2.0f * z_near * z_far / delta_z;
    tmp[15] = 0.0f;

    memcpy(m, tmp, sizeof(tmp));
}

static void look_at(GLfloat *m, GLfloat ex, GLfloat ey, GLfloat ez,
                    GLfloat cx, GLfloat cy, GLfloat cz,
                    GLfloat ux, GLfloat uy, GLfloat uz)
{
    GLfloat f[3], s[3], u[3];
    GLfloat f_len, s_len;
    GLfloat mat[16];

    f[0] = cx - ex; f[1] = cy - ey; f[2] = cz - ez;
    f_len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (f_len == 0.0f) f_len = 1.0f;
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;

    s[0] = f[1]*uz - f[2]*uy;
    s[1] = f[2]*ux - f[0]*uz;
    s[2] = f[0]*uy - f[1]*ux;
    s_len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    if (s_len == 0.0f) s_len = 1.0f;
    s[0] /= s_len; s[1] /= s_len; s[2] /= s_len;

    u[0] = s[1]*f[2] - s[2]*f[1];
    u[1] = s[2]*f[0] - s[0]*f[2];
    u[2] = s[0]*f[1] - s[1]*f[0];

    mat[0]=s[0]; mat[1]=u[0]; mat[2]=-f[0]; mat[3]=0;
    mat[4]=s[1]; mat[5]=u[1]; mat[6]=-f[1]; mat[7]=0;
    mat[8]=s[2]; mat[9]=u[2]; mat[10]=-f[2]; mat[11]=0;
    mat[12]=-(s[0]*ex + s[1]*ey + s[2]*ez);
    mat[13]=-(u[0]*ex + u[1]*ey + u[2]*ez);
    mat[14]= (f[0]*ex + f[1]*ey + f[2]*ez);
    mat[15]=1;
    memcpy(m, mat, sizeof(mat));
}

/* Column-major M * (x,y,z,1) -> eye space (matches teapot's xform_point). */
static void xform_point(const GLfloat *m, float x, float y, float z, float o[3])
{
    o[0] = m[0]*x + m[4]*y + m[8]*z  + m[12];
    o[1] = m[1]*x + m[5]*y + m[9]*z  + m[13];
    o[2] = m[2]*x + m[6]*y + m[10]*z + m[14];
}

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
        klog("glmatrix: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Emit one eye-space quad (two triangles TL,BL,TR / TR,BL,BR).  x0<x1 are the
 * left/right eye X edges, yb<yt the bottom/top eye Y edges, ez the shared
 * billboard depth.  Colour is constant across the quad (per-glyph). */
static int emit_quad(int nvert, float x0, float x1, float yb, float yt, float ez,
                     float cr, float cg, float cb)
{
    static const float vx[6] = { 0, 0, 1, 1, 0, 1 };   /* x0,x0,x1, x1,x0,x1 */
    static const float vy[6] = { 1, 0, 1, 1, 0, 0 };   /* yt,yb,yt, yt,yb,yb */
    int i;
    if (nvert + 6 > MAXVERTS) return nvert;
    for (i = 0; i < 6; ++i) {
        GLfloat *p = g_pos + (size_t)(nvert + i) * 4;
        GLfloat *q = g_col + (size_t)(nvert + i) * 4;
        p[0] = x0 + vx[i] * (x1 - x0);
        p[1] = yb + vy[i] * (yt - yb);
        p[2] = ez;
        p[3] = 1.0f;
        q[0] = cr; q[1] = cg; q[2] = cb; q[3] = 1.0f;
    }
    return nvert + 6;
}

/* Expand one glyph (8x8 mark) into run-length billboard quads centred on the
 * eye-space point (ex,ey,ez). */
static int emit_glyph(int nvert, float ex, float ey, float ez, int gi,
                      float cr, float cg, float cb)
{
    const unsigned char *rows = g_glyphset[gi];
    float px = GLYPH_SIZE / (float)GLYPH_PX;
    float left = ex - GLYPH_SIZE * 0.5f;
    float top  = ey + GLYPH_SIZE * 0.5f;
    unsigned char done[GLYPH_PX];   /* per-row bitmask of cells already covered */
    int r, c;

    /* Greedy 2D rectangle cover of the 8x8 mark: grow each unemitted lit cell
     * right across its contiguous run, then down while the whole span stays lit
     * and unemitted, and emit ONE quad for the block.  A vertical/diagonal
     * stroke that would otherwise cost a quad per row collapses to a single
     * tall quad, cutting the vertex count several-fold so the denser field
     * still fits the GPU staging budget.  The union of the blocks is exactly
     * the glyph bitmap, so the rendered mark stays pixel-identical. */
    memset(done, 0, sizeof(done));
    for (r = 0; r < GLYPH_PX; ++r) {
        for (c = 0; c < GLYPH_PX; ++c) {
            unsigned int bit = 1u << (GLYPH_PX - 1 - c);
            int c1, h, rr, cc;
            float x0, x1, yt, yb;
            if (!(rows[r] & bit) || ((unsigned int)done[r] & bit)) continue;
            c1 = c;                                    /* grow right */
            while (c1 + 1 < GLYPH_PX) {
                unsigned int nb = 1u << (GLYPH_PX - 2 - c1);
                if ((rows[r] & nb) && !((unsigned int)done[r] & nb)) ++c1;
                else break;
            }
            h = 1;                                     /* grow down */
            while (r + h < GLYPH_PX) {
                int ok = 1;
                for (cc = c; cc <= c1; ++cc) {
                    unsigned int b = 1u << (GLYPH_PX - 1 - cc);
                    if (!(rows[r + h] & b) || ((unsigned int)done[r + h] & b)) { ok = 0; break; }
                }
                if (!ok) break;
                ++h;
            }
            for (rr = r; rr < r + h; ++rr)             /* mark covered */
                for (cc = c; cc <= c1; ++cc)
                    done[rr] |= (unsigned char)(1u << (GLYPH_PX - 1 - cc));
            x0 = left + px * (float)c;
            x1 = left + px * (float)(c1 + 1);
            yt = top  - px * (float)r;
            yb = top  - px * (float)(r + h);
            nvert = emit_quad(nvert, x0, x1, yb, yt, ez, cr, cg, cb);
            if (nvert + 6 > MAXVERTS) return nvert;
        }
    }
    return nvert;
}

static void draw_rain(void)
{
    GLfloat view[16], model_view[16];
    float cam_z, ang_y, ang_x, ex, ey, roll;
    int nvert = 0;
    int c, k;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (g_pos == NULL) return;

    /* Fast, wide roam: the field yaws and pitches through a broad sweep of
     * angles while the camera itself drifts laterally and vertically, dollies
     * in and out and banks, so the viewpoint swims through the rain instead of
     * sitting still.  Every channel is a pure sinf() at its own rate, so the
     * motion stays smooth and never repeats on a short loop. */
    ang_y = 42.0f * sinf(g_time * 0.35f);
    ang_x = 20.0f * sinf(g_time * 0.27f + 0.7f);
    roll  = 12.0f * sinf(g_time * 0.19f + 2.0f);
    ex    = 3.2f  * sinf(g_time * 0.31f + 1.3f);
    ey    = 2.4f  * sinf(g_time * 0.23f + 0.4f);
    cam_z = CAM_DIST + 3.0f * sinf(g_time * 0.17f);

    look_at(view, ex, ey, cam_z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    memcpy(model_view, view, sizeof(model_view));
    rotate(model_view, ang_y, 0.0f, 1.0f, 0.0f);
    rotate(model_view, ang_x, 1.0f, 0.0f, 0.0f);
    rotate(model_view, roll,  0.0f, 0.0f, 1.0f);

    /* uMVP is just the projection: billboards are pre-transformed to eye space
     * on the CPU (and face the camera), so proj * eye == the full MVP. */
    glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, projection_matrix);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    for (c = 0; c < NCOLS; ++c) {
        for (k = 0; k < g_trail[c]; ++k) {
            float wy = g_head[c] - (float)k * GY;
            float ep[3], dist, fog, b, wave, cr, cg, cb;
            if (wy < YBOT - 0.5f || wy > YTOP + 0.5f) continue;

            xform_point(model_view, g_colx[c], wy, g_colz[c], ep);
            dist = -ep[2];
            if (dist < CAM_NEAR + 0.5f) continue;         /* behind / too near */

            fog = (FOG_FAR - dist) / (FOG_FAR - FOG_NEAR);
            if (fog < 0.0f) fog = 0.0f;
            if (fog > 1.0f) fog = 1.0f;
            if (fog <= 0.02f) continue;

            wave = 0.85f + 0.15f * sinf(wy * 0.5f + g_time * 1.5f);
            if (k == 0) {                                  /* bright leader  */
                cr = 0.70f; cg = 1.00f; cb = 0.82f;
            } else {                                        /* fading green  */
                b = 1.0f - (float)k / (float)g_trail[c];
                if (b < 0.05f) b = 0.05f;
                cr = 0.10f * b; cg = 0.95f * b; cb = 0.30f * b;
            }
            cr *= fog; cg *= fog * wave; cb *= fog;

            nvert = emit_glyph(nvert, ep[0], ep[1], ep[2],
                               g_cellglyph[c * MAXTRAIL + k], cr, cg, cb);
            if (nvert + 6 > MAXVERTS) break;
        }
        if (nvert + 6 > MAXVERTS) break;
    }

    if (nvert == 0) { glDisable(GL_DEPTH_TEST); return; }

    glBindBuffer(GL_ARRAY_BUFFER, g_posvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nvert * 4 * sizeof(GLfloat),
                 g_pos, GL_STREAM_DRAW);
    glVertexAttribPointer((GLuint)attr_pos, 4, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray((GLuint)attr_pos);

    glBindBuffer(GL_ARRAY_BUFFER, g_colvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nvert * 4 * sizeof(GLfloat),
                 g_col, GL_STREAM_DRAW);
    glVertexAttribPointer((GLuint)attr_color, 4, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray((GLuint)attr_color);

    /* The V3D geometry staging buffer caps a single draw at PGL_G3D_MAX_VERTS
     * (20480); a larger glDrawArrays is silently declined to the CPU interpreter
     * (see gpu_try_draw_arrays).  A full frame of run-length glyphs can exceed
     * that, so submit it in <=20478-vertex chunks (a multiple of 6, so a chunk
     * border never splits a billboard quad).  Consecutive same-state chunks
     * coalesce on the GPU and flush per staging capacity, keeping the rain
     * hardware-drawn instead of falling back to the software rasteriser. */
    {
        const int CHUNK = 20478;
        int off = 0;
        while (off < nvert) {
            int c = nvert - off;
            if (c > CHUNK) c = CHUNK;
            glDrawArrays(GL_TRIANGLES, off, c);
            off += c;
        }
    }

    glDisableVertexAttribArray((GLuint)attr_pos);
    glDisableVertexAttribArray((GLuint)attr_color);
    glDisable(GL_DEPTH_TEST);
}

static void init_imgui(void);

static void init_gl(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    vs = compile_shader(GL_VERTEX_SHADER, rain_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, rain_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        klog("glmatrix: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    /* libGLESv2 ignores glBindAttribLocation, so query the linked locations. */
    attr_pos   = glGetAttribLocation(program, "aPosition");
    attr_color = glGetAttribLocation(program, "aColor");
    loc_mvp    = glGetUniformLocation(program, "uMVP");
    if (attr_pos < 0 || attr_color < 0) {
        klog("glmatrix: attribute location query failed\n");
        return;
    }

    glGenBuffers(1, &g_posvbo);
    glGenBuffers(1, &g_colvbo);

    g_pos = (GLfloat *)malloc((size_t)MAXVERTS * 4 * sizeof(GLfloat));
    g_col = (GLfloat *)malloc((size_t)MAXVERTS * 4 * sizeof(GLfloat));
    if (!g_pos || !g_col) {
        slog("glmatrix: out of memory allocating vertex scratch\n");
        free(g_pos); g_pos = NULL;
        free(g_col); g_col = NULL;
        return;
    }

    build_glyphset();
    build_state();

    glViewport(0, 0, win_width, win_height);
    perspective(projection_matrix, CAM_FOV,
                (GLfloat)win_width / (GLfloat)win_height, CAM_NEAR, CAM_FAR);

    init_imgui();
}

static void reshape(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    win_width = width;
    win_height = height;
    glViewport(0, 0, width, height);
    perspective(projection_matrix, CAM_FOV,
                (GLfloat)width / (GLfloat)height, CAM_NEAR, CAM_FAR);
}

static int fps = 0;
static int count = 0;
static uint32_t last_tic = 0;
static uint32_t last_frame_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    if (last_tic == 0 || (low - last_tic) >= 1000000) {
        last_tic = low;
        fps = count;
        count = 0;
    }
    count++;
}

/* ------------------------------------------------------------------ *
 * Dear ImGui overlay: the FPS / GPU-vs-CPU readout is presented as a
 * small draggable ImGui window drawn on top of the rain (replacing the
 * old glcommon bitmap HUD).  ImGui is C++ and renders through the GLES2
 * backend; xwin mouse/keyboard events are forwarded to it so the window
 * stays interactive, and ESC still quits like the other soft3d demos.
 * ------------------------------------------------------------------ */
static int  mouse_x = 0, mouse_y = 0;
static bool mouse_buttons[3] = { false, false, false };    /* left, right, mid */
static bool key_ctrl = false, key_shift = false, key_alt = false, key_super = false;

static void init_imgui(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                                 /* no imgui.ini    */
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    ImGui::StyleColorsDark();
    ImGui_ImplGLES2_Init();
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

/* Map an EwokOS key code to the matching ImGuiKey (same table as imgui_demo). */
static ImGuiKey KeyCodeToImGuiKey(int keycode)
{
    switch (keycode) {
        case 9:   return ImGuiKey_Tab;
        case 27:  return ImGuiKey_Escape;
        case 13:  return ImGuiKey_Enter;
        case 8:   return ImGuiKey_Backspace;
        case 127: return ImGuiKey_Delete;
        case 32:  return ImGuiKey_Space;
        case 276: return ImGuiKey_LeftArrow;
        case 275: return ImGuiKey_RightArrow;
        case 273: return ImGuiKey_UpArrow;
        case 274: return ImGuiKey_DownArrow;
        case 278: return ImGuiKey_Home;
        case 279: return ImGuiKey_End;
        case 280: return ImGuiKey_PageUp;
        case 281: return ImGuiKey_PageDown;
        case 277: return ImGuiKey_Insert;
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
        default:  return ImGuiKey_None;
    }
}

/* Build one ImGui frame: forward input, then lay out the stats window.  `gpu`
 * is the route the rain program used this frame, queried while it was bound. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize  = ImVec2((float)win_width, (float)win_height);
    io.MousePos     = ImVec2((float)mouse_x, (float)mouse_y);
    io.MouseDown[0] = mouse_buttons[0];
    io.MouseDown[1] = mouse_buttons[1];
    io.MouseDown[2] = mouse_buttons[2];
    io.KeyCtrl      = key_ctrl;
    io.KeyShift     = key_shift;
    io.KeyAlt       = key_alt;
    io.KeySuper     = key_super;

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("GLMatrix", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::End();
}

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    uint32_t current_tic;
    float dt;
    int gpu;

    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0 && (w != win_width || h != win_height))
        reshape(w, h);

    kernel_tic32(NULL, NULL, &current_tic);
    if (last_frame_tic == 0)
        last_frame_tic = current_tic;
    dt = (current_tic - last_frame_tic) / 1000000.0f;
    last_frame_tic = current_tic;
    if (dt > 0.1f) dt = 0.1f;

    update_rain(dt);

    glUseProgram(program);
    draw_rain();

    /* Query the GPU/CPU route while the rain program is still bound: the engine
     * reports the currently-bound program and ImGui binds its own next.  The
     * readout is then presented as a Dear ImGui window drawn on top. */
    gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
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
        int32_t state = ev->state;                 /* XIM_STATE_PRESS/RELEASE */

        key_shift = (ev->value.im.shift != 0);
        key_ctrl  = (ev->value.im.ctrl  != 0);

        /* ESC (27) quits, matching the other soft3d demos. */
        if (key == 27 && state == XIM_STATE_PRESS) {
            xwin_close(xwin);
            return;
        }

        ImGuiKey ik = KeyCodeToImGuiKey(key);
        if (ik != ImGuiKey_None)
            io.AddKeyEvent(ik, state == XIM_STATE_PRESS);
        if (state == XIM_STATE_PRESS && key >= 32 && key < 127)
            io.AddInputCharacter(key);
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);
        int32_t state  = ev->state;
        int32_t button = ev->value.mouse.button;
        bool down = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);

        mouse_x = pos.x;
        mouse_y = pos.y;
        if (button == MOUSE_BUTTON_LEFT)
            mouse_buttons[0] = down;
        else if (button == MOUSE_BUTTON_RIGHT)
            mouse_buttons[1] = down;
        else if (button == MOUSE_BUTTON_MID)
            mouse_buttons[2] = down;
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

    xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "GLMatrix", XWIN_STYLE_NORMAL);
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

    xwin_set_visible(xwin, true);
    x_run(&x, xwin);

    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();

    xwin_destroy(xwin);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);
    if (g_pos) { free(g_pos); g_pos = NULL; }
    if (g_col) { free(g_col); g_col = NULL; }

    return 0;
}
