/*
 * Fireworks demo for EwokOS.
 *
 * A GPU particle system: rockets launch, explode, and their glowing debris
 * falls under gravity.  The particle physics and the DOT PROFILE below are
 * unchanged from the original CPU version; what changed is the DRAWING.
 * Instead of compositing every particle per-pixel into an in-memory ARGB
 * framebuffer and blitting it as a fullscreen textured quad, the exact same
 * pixels are emitted as GPU geometry: ONE textured quad per glow dot.
 *
 *  - Each glow dot is the painter's 5x5 profile baked into a tiny RGBA8
 *    texture atlas (white RGB, profile in alpha): the bright 3x3 core plus
 *    the ring pixels at 1.0 / 0.85 / 0.35 / 0.275 / 0.20 and the rocket's
 *    13-pixel head disc in a second tile.  The quad is anchored to the same
 *    integer pixel the painter's (int) casts chose, and the 1:1 ortho uMVP
 *    lands each of its 5x5 pixel centres on the exact texel centre, so the
 *    kernel's bilinear fetch degrades to an exact texel lookup.
 *
 *  - The compositing is the painter's exact SRC_ALPHA / ONE_MINUS_SRC_ALPHA
 *    "over" blend of straight (non-premultiplied) colours with real alpha:
 *    the texcol raster kernel evaluates src = texel * vColor, so a vertex
 *    colour (r, g, b, dot_alpha) over a white profile yields the painter's
 *    per-pixel equation rgb * (profile_a * dot_a) + dst * (1 - ...).  The
 *    kernel's CAS-retried blend RMW resolves overlapping dots (burst cores,
 *    crossing trails) without losing contributions.  The CPU interpreter
 *    reproduces the same blend, so both routes are pixel-identical.
 *
 *  - Particles draw their original dotted velocity trails (a dot every
 *    3.5 px along the half-length of the speed-scaled tail segment, the
 *    head bright and the tail dim), and rockets draw the original
 *    13-pixel head disc plus the 12-dot launch trail.  Rockets emit
 *    first and particles on top, matching the painter's draw order.
 *
 * On PGL_G3D targets the shader pair has the exact imgui texcol shape
 * (gl_FragColor = vColor * texture2D(uTexture, vUV)), which the engine
 * routes through the one-workgroup-per-triangle texcol raster kernel -
 * NO per-tile binning of the whole triangle list, which is what keeps a
 * dense frame's GPU time near zero; without a GPU it runs on the CPU
 * interpreter with identical pixels.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <math.h>

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

#define WIDTH 1024
#define HEIGHT 768
#define MAX_PARTICLES 5000
#define MAX_FIREWORKS 5

/* PGL_G3D_MAX_VERTS: the engine's per-glDrawElements ceiling on the texcol
 * route (20480 expanded vertices, one per index).  One draw with more indices
 * silently falls back to the CPU interpreter, so the per-frame quad list is
 * submitted in chunks no larger than this: 6 indices per quad -> 3413 quads
 * per draw (20478 indices). */
#define GPU_CHUNK_VERTS 20480
#define QUADS_PER_CHUNK (GPU_CHUNK_VERTS / 6)
/* Per-frame emission ceiling: one 4-vertex quad per dot, capped at 200K
 * quads (800K verts, 1.2M indices).  At impossible overloads the last trail
 * dots are simply skipped instead of overflowing the stream. */
#define MAX_QUADS 200000

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Particle structure - dotted trail effect
typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
    float r, g, b;
    float brightness;
    int active;
} Particle;

// Firework rocket
typedef struct {
    float x, y;
    float vx, vy;
    float r, g, b;
    int active;
} Firework;

/* Interleaved quad vertex: position (top-down pixel coords), profile-atlas
 * uv, straight (non-premultiplied) rgb + per-dot alpha.  Layout matches the
 * texcol staging contract: attr0 = pos vec2, attr1 = uv vec2, attr2 = rgba. */
typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} FWVert;

/* Global variables
 */
static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;

static GLuint program = 0, vbo = 0, ibo = 0, atlas = 0;
static GLint loc_mvp = -1, loc_tex = -1;

static int screen_width = WIDTH, screen_height = HEIGHT;

Particle particles[MAX_PARTICLES];
int num_particles = 0;

Firework fireworks[MAX_FIREWORKS];
int next_firework_time = 0;

/* The imgui texcol pair: on PGL_G3D targets the engine detects
 * gl_FragColor = vColor * texture2D(uTexture, vUV) and routes the draw
 * through the one-workgroup-per-triangle texcol raster kernel, which
 * multiplies the profile texel by the interpolated vertex colour and
 * over-blends with SRC_ALPHA - the painter's exact per-pixel equation.  The
 * CPU interpreter compiles the same sources for the fallback with identical
 * pixels.  The ortho uMVP maps pixel (x, y) - y down, matching the top-down
 * screen coordinates the particles use - to NDC. */
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
        klog("Fireworks: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* Per-frame quad scratch: 4 FWVert per dot, glBufferData'd to the particle
 * VBO once per frame. */
static FWVert* vertbuf = NULL;

static void init_imgui(void);

/* ---- The two baked profiles ----------------------------------------------
 * The painter composited each glow dot as its 5x5 pixel profile in a fixed
 * sequence of axis-aligned pixel runs.  Baking that sequence into texture
 * alphas reproduces it pixel-exactly, including two quirks of the painter's
 * run order:
 *   - the four AXIS pixels at distance 2 are redrawn by the d3 runs, so
 *     their final alpha is the sequential over-blend 0.35 over 0.275 =
 *     0.35 + 0.275*(1-0.35) = 0.52875;
 *   - the four DIAGONAL pixels at distance 2 are never painted (holes).
 * The rocket head is the painter's 13-pixel disc (dx^2+dy^2 <= 4, alpha =
 * (255 - 40*dist)/255). */

/* Head disc profile, rows dy = -2..2 (row 0 = top), cols dx = -2..2:
 * 1.0 centre, 215/255 ring 1, 175/255 diagonal ring 2, 95/255 axis ring 2. */
static const float head_prof[25] = {
    0.0f,        95.0f/255.0f,  0.0f,        95.0f/255.0f,  0.0f,
    95.0f/255.0f, 175.0f/255.0f, 215.0f/255.0f, 175.0f/255.0f, 95.0f/255.0f,
    0.0f,        215.0f/255.0f, 1.0f,        215.0f/255.0f, 0.0f,
    95.0f/255.0f, 175.0f/255.0f, 215.0f/255.0f, 175.0f/255.0f, 95.0f/255.0f,
    0.0f,        95.0f/255.0f,  0.0f,        95.0f/255.0f,  0.0f,
};

/* Glow dot profile (same row order): 1.0 centre, 0.85 ring 1, 0.35/0.52875
 * ring 2, 0.275 ring 3, 0.20 ring 4, holes on the diagonal ring-2 pixels. */
static const float dot_prof[25] = {
    0.20f,  0.275f,  0.52875f, 0.275f,  0.20f,
    0.275f, 0.0f,    0.85f,    0.0f,    0.275f,
    0.52875f, 0.85f, 1.0f,     0.85f,   0.52875f,
    0.275f, 0.0f,    0.85f,    0.0f,    0.275f,
    0.20f,  0.275f,  0.52875f, 0.275f,  0.20f,
};

/* Atlas v origin of each 5-row tile (atlas is 5 wide x 11 tall: head rows
 * 0-4, dot rows 5-9, row 10 padding).  The texcol kernel maps a quad's
 * pixel-centre samples to exact texel centres when uv spans [top, top+5/11]
 * over a 5-px quad, so the bilinear fetch degrades to an exact lookup. */
#define ATLAS_W 5
#define ATLAS_H 11
#define HEAD_TOP (0.0f)
#define DOT_TOP  (5.0f / (float)ATLAS_H)

/* Build the 5x11 profile atlas as a tightly packed RGBA8 image: white RGB,
 * profile in alpha (the kernel's src = texel * vColor then puts the profile
 * on the blend's source alpha). */
static void build_atlas(unsigned char out[ATLAS_W * ATLAS_H * 4])
{
    int row, col;
    memset(out, 0, (size_t)ATLAS_W * ATLAS_H * 4);
    for (row = 0; row < 5; row++) {
        for (col = 0; col < 5; col++) {
            int k = row * 5 + col;
            unsigned char ha = (unsigned char)(head_prof[k] * 255.0f + 0.5f);
            unsigned char da = (unsigned char)(dot_prof[k] * 255.0f + 0.5f);
            unsigned char *hp = out + ((size_t)row * ATLAS_W + col) * 4;
            unsigned char *dp = out + ((size_t)(row + 5) * ATLAS_W + col) * 4;
            hp[0] = 255; hp[1] = 255; hp[2] = 255; hp[3] = ha;
            dp[0] = 255; dp[1] = 255; dp[2] = 255; dp[3] = da;
        }
    }
}

static void init_gl(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    unsigned char *atlas_pix;
    GLuint *indices;
    int q;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

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
        klog("Fireworks: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    loc_mvp = glGetUniformLocation(program, "uMVP");
    loc_tex = glGetUniformLocation(program, "uTexture");

    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);

    /* Profile atlas on texture unit 0: RGBA8 upload also registers the
     * engine's raster texture slot (gpu_on_texture), which the texcol draw
     * route stages for the kernel. */
    glGenTextures(1, &atlas);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    atlas_pix = (unsigned char *)malloc((size_t)ATLAS_W * ATLAS_H * 4);
    if (atlas_pix) {
        build_atlas(atlas_pix);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_W, ATLAS_H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, atlas_pix);
        free(atlas_pix);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Fixed index pattern: quad q -> verts q*4+0..3, two triangles.  Built
     * once for the whole MAX_QUADS stream; each frame draws only the used
     * prefix. */
    indices = (GLuint *)malloc((size_t)MAX_QUADS * 6 * sizeof(GLuint));
    if (indices) {
        for (q = 0; q < MAX_QUADS; q++) {
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

    init_imgui();
}

float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

// HSV to RGB conversion
void hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

// Launch firework - random height, supports multiple simultaneous fireworks
void launch_firework() {
    // Find an available firework slot
    int slot = -1;
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return; // No available slots

    Firework* fw = &fireworks[slot];
    fw->x = random_float(screen_width * 0.1f, screen_width * 0.9f);
    fw->y = screen_height;
    fw->vx = random_float(-0.5f, 0.5f);
    // Speed adjusted based on target height, higher targets need faster speed
    fw->vy = random_float(-12.0f, -18.0f);

    float hue = random_float(0.0f, 1.0f);
    hsv_to_rgb(hue, 1.0f, 1.0f, &fw->r, &fw->g, &fw->b);

    fw->active = 1;
}

// Create explosion - smaller and more natural range
void create_explosion(float x, float y, float r, float g, float b) {
    // Check if particle array is full
    if (num_particles >= MAX_PARTICLES - 500) return;

    int streams = 20 + rand() % 12; // 20-32 streams

    for (int s = 0; s < streams; s++) {
        float base_angle = (M_PI * 2 * s) / streams;
        // Add natural random angle variation
        float angle_var = random_float(-0.15f, 0.15f);

        // Reduce particles per stream
        int particles_per_stream = 12 + rand() % 8;

        for (int i = 0; i < particles_per_stream; i++) {
            if (num_particles >= MAX_PARTICLES) break;

            Particle* p = &particles[num_particles++];

            float angle = base_angle + angle_var + random_float(-0.08f, 0.08f);
            // Slower speed, more compact range
            float base_speed = 1.2f + (float)i * 0.25f;
            float speed = base_speed * random_float(0.9f, 1.1f);

            p->x = x + random_float(-2.0f, 2.0f); // Slight randomness in starting position
            p->y = y + random_float(-2.0f, 2.0f);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            p->life = 1.0f;
            p->maxLife = random_float(50.0f, 90.0f);
            p->brightness = 1.0f - (float)i / particles_per_stream * 0.25f;

            // Color gradient
            float fade = 1.0f - (float)i / particles_per_stream * 0.35f;
            p->r = r * fade;
            p->g = g * fade;
            p->b = b * fade;

            p->active = 1;
        }
    }

    // Reduce center particles
    int center_particles = 15;
    for (int i = 0; i < center_particles; i++) {
        if (num_particles >= MAX_PARTICLES) break;

        Particle* p = &particles[num_particles++];

        float angle = random_float(0, M_PI * 2);
        float speed = random_float(0.3f, 1.5f);

        p->x = x;
        p->y = y;
        p->vx = cos(angle) * speed;
        p->vy = sin(angle) * speed;
        p->life = 1.0f;
        p->maxLife = random_float(30.0f, 60.0f);
        p->brightness = 1.0f;
        p->r = r;
        p->g = g;
        p->b = b;
        p->active = 1;
    }
}

// Update fireworks - random explosion height
void update_fireworks() {
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        Firework* fw = &fireworks[i];
        if (!fw->active) continue;

        fw->x += fw->vx;
        fw->y += fw->vy;
        fw->vy += 0.1f;

        // Random explosion height: 10%-50% of screen height (Y axis points down, smaller value means higher)
        float explode_height = screen_height * random_float(0.1f, 0.3f);

        if (fw->vy > -1.0f || fw->y < explode_height) {
            create_explosion(fw->x, fw->y, fw->r, fw->g, fw->b);
            fw->active = 0;
        }
    }
}

// Update particles - enhanced gravity effect
void update_particles() {
    int i = 0;
    while (i < num_particles) {
        Particle* p = &particles[i];
        if (!p->active) {
            i++;
            continue;
        }

        p->x += p->vx;
        p->y += p->vy;
        p->vy += 0.12f; // Enhanced gravity
        p->life -= 1.0f / p->maxLife;

        // Air resistance
        p->vx *= 0.985f;
        p->vy *= 0.985f;

        if (p->life <= 0) {
            p->active = 0;
            particles[i] = particles[--num_particles];
        } else {
            i++;
        }
    }
}

/* ---- GPU geometry emission ----------------------------------------------
 * The original CPU painter composited each glow dot as a 5x5 pixel profile
 * (a bright core plus ring pixels at 0.85 / 0.35 / 0.275 / 0.20 alpha,
 * brightness boosted 1.5x), blended one pixel at a time with an exact
 * 8-bit "over" equation col*a + dst*(1-a).  The GPU reproduces the SAME
 * pixels by emitting ONE textured quad per dot: the painter's per-pixel
 * profile is baked into the atlas alpha channel, and the vertex colour
 * carries (boosted rgb, dot alpha), so the kernel's src = texel * vColor
 * + SRC_ALPHA over blend evaluate the painter's per-pixel equation exactly:
 *
 *   out = rgb * (profile_a * dot_a) + dst * (1 - profile_a * dot_a)
 *
 * The quad corners sit on integer pixel coordinates around the same
 * integer pixel the painter's (int) casts chose, and the 1:1 ortho lands
 * each of the 5x5 pixel centres on the exact texel centre, so the kernel's
 * bilinear fetch degrades to an exact texel lookup. */

/* Append one glow-dot quad (4 verts, 6 indices) to the stream.  `vtop` is
 * the atlas v of the profile tile's top row; the colour is straight
 * (non-premultiplied) with a real per-vertex alpha: the kernel's over blend
 * does the premultiplication.  Returns 0 when the stream is full. */
static int emit_dot_quad(float x, float y, float r, float g, float b,
                         float alpha, float vtop, FWVert* vs, int* nq)
{
    int X = (int)x;
    int Y = (int)y;
    float x0 = (float)(X - 2), x1 = (float)(X + 3);
    float y0 = (float)(Y - 2), y1 = (float)(Y + 3);
    float v0 = vtop, v1 = vtop + 5.0f / (float)ATLAS_H;
    FWVert q[4];

    if (*nq >= MAX_QUADS) return 0;

    q[0].x = x0; q[0].y = y0; q[0].u = 0.0f; q[0].v = v0;
    q[1].x = x1; q[1].y = y0; q[1].u = 1.0f; q[1].v = v0;
    q[2].x = x1; q[2].y = y1; q[2].u = 1.0f; q[2].v = v1;
    q[3].x = x0; q[3].y = y1; q[3].u = 0.0f; q[3].v = v1;
    for (int i = 0; i < 4; i++) {
        q[i].r = r; q[i].g = g; q[i].b = b; q[i].a = alpha;
    }
    memcpy(vs + (size_t)*nq * 4, q, sizeof(q));
    (*nq)++;
    return 1;
}

/* Emit one glow dot at pixel (x, y) - the painter's exact 5x5 profile.
 * Returns 0 when the stream is full.  One quad (4 verts) per dot. */
static int emit_glow_dot(float x, float y, float r, float g, float b,
                         float alpha, FWVert* vs, int* nq)
{
    if (alpha <= 0.0f) return 1;
    /* Increase brightness (the CPU painter's 1.5x boost); the quad anchors
     * on the same integer pixel the painter's (int) casts chose. */
    float br = fminf(1.0f, r * 1.5f);
    float bg = fminf(1.0f, g * 1.5f);
    float bb = fminf(1.0f, b * 1.5f);
    return emit_dot_quad(x, y, br, bg, bb, alpha, DOT_TOP, vs, nq);
}

// Emit all rockets: the painter's 13-pixel head disc plus the 12-dot
// launch trail.  Drawn BEFORE the particles (the painter's order), so
// particles over-blend on top of any overlapping rocket pixels.
static int emit_fireworks(FWVert* vs, int* nq) {
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        Firework* fw = &fireworks[i];
        if (!fw->active) continue;

        /* Rocket head: the baked 13-pixel disc profile (dx^2+dy^2 <= 4,
         * alpha = 255 - 40*dist).  NOT brightness-boosted (the painter
         * only boosted the glow dots). */
        if (!emit_dot_quad(fw->x, fw->y, fw->r, fw->g, fw->b, 1.0f,
                           HEAD_TOP, vs, nq))
            return 0;

        /* Rocket dotted trail: 12 glow dots 0.8 frames apart behind the
         * head, alpha = (200 - 15*j) / 255. */
        for (int j = 1; j <= 12; j++) {
            float t = j * 0.8f;
            float tx = fw->x - fw->vx * t;
            float ty = fw->y - fw->vy * t;
            float alpha = (float)(200 - j * 15) / 255.0f;
            if (!emit_glow_dot(tx, ty, fw->r, fw->g, fw->b, alpha, vs, nq))
                return 0;
        }
    }
    return 1;
}

// Emit all particles - the painter's dotted velocity trails: the trail
// segment reaches half the speed-scaled length (clamped 3..18 px) behind
// the particle, dotted every 3.5 px, the head dot bright and the tail dim.
static int emit_particles(FWVert* vs, int* nq) {
    for (int i = 0; i < num_particles; i++) {
        Particle* p = &particles[i];
        if (!p->active) continue;

        /* Trail length follows speed only (3 px per unit speed, clamped),
         * direction opposite the velocity - the original constants. */
        float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
        float tail_len = speed * 3.0f;
        if (tail_len > 18.0f) tail_len = 18.0f;
        if (tail_len < 3.0f) tail_len = 3.0f;

        float x0 = p->x;
        float y0 = p->y;
        float x1 = p->x - p->vx * tail_len * 0.5f;
        float y1 = p->y - p->vy * tail_len * 0.5f;

        float r = p->r * p->brightness;
        float g = p->g * p->brightness;
        float b = p->b * p->brightness;

        float dx = x1 - x0;
        float dy = y1 - y0;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist < 1.0f) {
            if (!emit_glow_dot(x0, y0, r, g, b, p->life, vs, nq))
                return 0;
            continue;
        }

        /* One dot every 3.5 px along the segment, capped at 30 gaps. */
        int num_dots = (int)(dist / 3.5f);
        if (num_dots < 1) num_dots = 1;
        if (num_dots > 30) num_dots = 30;

        for (int d = 0; d <= num_dots; d++) {
            float t = (float)d / num_dots;
            float x = x0 + dx * t;
            float y = y0 + dy * t;
            /* Head bright, tail dim, but tail stays obvious. */
            float dot_alpha = p->life * (0.6f + 0.4f * (1.0f - t));
            if (dot_alpha > 0.05f) {
                if (!emit_glow_dot(x, y, r, g, b, dot_alpha, vs, nq))
                    return 0;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ *
 * Dear ImGui overlay: the FPS / GPU-vs-CPU readout as a small read-only
 * ImGui window drawn on top of the fireworks, the same overlay glmatrix
 * uses.  ImGui is C++ and renders through the GLES2 backend after the
 * particle pass.  The HUD is display-only, so no xwin input is
 * forwarded to it and the window is marked NoInputs; ESC still quits.
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
 * route the particle program used this frame, queried while it was bound.
 * The HUD is display-only (FPS / Renderer), so no input is forwarded to
 * ImGui and the window is marked NoInputs. */
static void process_imgui_frame(int gpu)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)screen_width, (float)screen_height);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Fireworks", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::End();
}

static int frame_count = 0;

/* Composite one animation frame: emit every glow dot as ONE textured quad,
 * clear the back buffer to black and over-blend the quads in emission
 * order, then draw the ImGui HUD on top and present. */
static void render_frame(void)
{
    EGLint w = 0, h = 0;
    int nquad = 0;

    /* Query the render size (stride-aligned on q8b, == window elsewhere) so
     * the ortho uMVP / glViewport / DisplaySize match the engine back
     * buffer. */
    eglQuerySurface(egl_dpy, egl_surf, EGL_RENDER_WIDTH_EWOK, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_RENDER_HEIGHT_EWOK, &h);
    if (w <= 0 || h <= 0) { w = WIDTH; h = HEIGHT; }
    screen_width = w;
    screen_height = h;

    // Random launch timing, no need to wait for previous to finish
    if (frame_count >= next_firework_time) {
        launch_firework();
        // Next launch time: 10-60 frames later (about 0.16-1 second)
        next_firework_time = frame_count + rand() % 50 + 10;
    }

    update_fireworks();
    update_particles();

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
    /* Over compositing: the painter's exact equation col*a + dst*(1-a) with
     * straight colours and real per-vertex alpha.  The texcol kernel's
     * fixed blend IS the imgui BlendFuncSeparate shape the engine's route
     * gate requires: SRC_ALPHA/ONE_MINUS_SRC_ALPHA on RGB and
     * ONE/ONE_MINUS_SRC_ALPHA on alpha; the CPU interpreter uses the same
     * factors for identical pixels. */
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program);
    {
        /* Column-major ortho: pixel (x, y), y down -> NDC. */
        float m[16] = {
            2.0f / (float)w, 0.0f,           0.0f, 0.0f,
            0.0f,           -2.0f / (float)h, 0.0f, 0.0f,
            0.0f,           0.0f,           1.0f, 0.0f,
           -1.0f,           1.0f,           0.0f, 1.0f,
        };
        glUniformMatrix4fv(loc_mvp, 1, GL_FALSE, m);
        if (loc_tex >= 0) glUniform1i(loc_tex, 0);
    }
    /* The texcol draw route stages the texture bound to unit 0. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    if (!vertbuf)
        vertbuf = (FWVert*)malloc((size_t)MAX_QUADS * 4 * sizeof(FWVert));
    if (vertbuf) {
        nquad = 0;
        emit_fireworks(vertbuf, &nquad);
        emit_particles(vertbuf, &nquad);
    }

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

    /* Query the GPU/CPU route while the particle program is still bound, then
     * draw the ImGui stats window on top (same overlay as glmatrix). */
    int gpu = glGpuActiveEWOK();
    process_imgui_frame(gpu);
    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(egl_dpy, egl_surf);
    frame_count++;
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
        xwin_close(xwin);
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
    x_t x;
    xwin_t* xwin;

    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    // Random seed initialization for launch timing
    next_firework_time = rand() % 30 + 10;

    x_init(&x, NULL);
    x.on_loop = loop;

    xwin = xwin_open(&x, -1, 32, 32, WIDTH, HEIGHT, "Fireworks", XWIN_STYLE_NORMAL);
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

    /* The particle program has the engine's texcol shape (gl_FragColor =
     * vColor * texture2D(uTexture, vUV)), so on a PGL_G3D platform the whole
     * show (particles + ImGui overlay) draws on the GPU through the
     * one-workgroup-per-triangle texcol kernel; on the CPU-only platforms
     * the interpreter reproduces it pixel-identically.  No glForceCpuEWOK:
     * that pin was for the old CPU-composited blit. */
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

    free(vertbuf);
    return 0;
}
