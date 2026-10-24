/* hud.c - see hud.h.  Self-contained 8x8 bitmap-font text overlay for the
 * GLES2 back buffer.  Kept independent of the app's scene state so it can be
 * dropped into any EGL/GLES2 app between its draw and eglSwapBuffers. */

#include <glcommon/hud.h>

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * 8x8 font.  Each glyph is 8 row bytes; within a byte bit 7 (0x80) is
 * the LEFT-most column and bit 0 the right-most.  Glyph set covers the
 * HUD vocabulary (digits, upper-case letters, a few separators); any
 * character outside it renders as a blank space.
 * ------------------------------------------------------------------ */
typedef struct { char ch; unsigned char rows[8]; } hud_glyph;

static const hud_glyph g_glyphs[] = {
    { ' ', { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { '0', { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00 } },
    { '1', { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00 } },
    { '2', { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00 } },
    { '3', { 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,0x00 } },
    { '4', { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00 } },
    { '5', { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00 } },
    { '6', { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00 } },
    { '7', { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00 } },
    { '8', { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00 } },
    { '9', { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00 } },
    { ':', { 0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00,0x00 } },
    { '.', { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00 } },
    { '-', { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00 } },
    { '/', { 0x01,0x02,0x02,0x04,0x08,0x08,0x10,0x00 } },
    { '%', { 0x18,0x19,0x02,0x04,0x08,0x13,0x03,0x00 } },
    { 'A', { 0x0E,0x11,0x11,0x11,0x1F,0x11,0x11,0x00 } },
    { 'B', { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00 } },
    { 'C', { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00 } },
    { 'D', { 0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x00 } },
    { 'E', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00 } },
    { 'F', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00 } },
    { 'G', { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00 } },
    { 'H', { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00 } },
    { 'I', { 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00 } },
    { 'J', { 0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00 } },
    { 'K', { 0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00 } },
    { 'L', { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00 } },
    { 'M', { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00 } },
    { 'N', { 0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x00 } },
    { 'O', { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00 } },
    { 'P', { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00 } },
    { 'Q', { 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00 } },
    { 'R', { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00 } },
    { 'S', { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0x00 } },
    { 'T', { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00 } },
    { 'U', { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00 } },
    { 'V', { 0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00 } },
    { 'W', { 0x11,0x11,0x11,0x15,0x15,0x1B,0x11,0x00 } },
    { 'X', { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00 } },
    { 'Y', { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04,0x00 } },
    { 'Z', { 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00 } },
};
#define HUD_NGLYPH ((int)(sizeof(g_glyphs) / sizeof(g_glyphs[0])))
#define HUD_GLYPH_PX 8
#define HUD_ATLAS_COLS 16                       /* glyphs per atlas row */
#define HUD_ATLAS_W (HUD_ATLAS_COLS * HUD_GLYPH_PX)
#define HUD_ATLAS_ROWS ((HUD_NGLYPH + HUD_ATLAS_COLS - 1) / HUD_ATLAS_COLS)
#define HUD_ATLAS_H (HUD_ATLAS_ROWS * HUD_GLYPH_PX)

#define HUD_MAX_CHARS 64
#define HUD_FLOATS_PER_VERT 4                   /* px, py, u, v */
#define HUD_VERTS_PER_QUAD 6

static const char *hud_vs =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "uniform vec2 uWin;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = vec4((aPos.x / uWin.x) * 2.0 - 1.0,\n"
    "                       1.0 - (aPos.y / uWin.y) * 2.0,\n"
    "                       0.0, 1.0);\n"
    "}\n";

/* Sample the atlas into a local vec4 then multiply by uColor: whole-vector
 * math only, no rvalue swizzle, which the CPU interpreter handles reliably. */
static const char *hud_fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uAtlas;\n"
    "uniform vec4 uColor;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uAtlas, vUV);\n"
    "    gl_FragColor = t * uColor;\n"
    "}\n";

static int      g_ready = 0;
static GLuint   g_prog = 0;
static GLuint   g_vbo = 0;
static GLuint   g_atlas = 0;
static GLint    g_attr_pos = -1, g_attr_uv = -1;
static GLint    g_loc_win = -1, g_loc_color = -1, g_loc_atlas = -1;
static float    g_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

/* Index of the glyph for `c` (upper-cased), or 0 (blank) when not covered. */
static int glyph_index(char c)
{
    int i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (i = 0; i < HUD_NGLYPH; ++i)
        if (g_glyphs[i].ch == c) return i;
    return 0;
}

static GLuint hud_compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(sh); return 0; }
    return sh;
}

static void hud_build_atlas(void)
{
    unsigned char *pix = (unsigned char *)malloc(HUD_ATLAS_W * HUD_ATLAS_H * 4);
    int i, r, ccol;
    if (!pix) return;
    memset(pix, 0, HUD_ATLAS_W * HUD_ATLAS_H * 4);
    for (i = 0; i < HUD_NGLYPH; ++i) {
        int gx = (i % HUD_ATLAS_COLS) * HUD_GLYPH_PX;
        int gy = (i / HUD_ATLAS_COLS) * HUD_GLYPH_PX;
        for (r = 0; r < HUD_GLYPH_PX; ++r) {
            unsigned char rowb = g_glyphs[i].rows[r];
            for (ccol = 0; ccol < HUD_GLYPH_PX; ++ccol) {
                int on = (rowb >> (HUD_GLYPH_PX - 1 - ccol)) & 1;  /* bit7 == left-most */
                unsigned char *p = pix + ((gy + r) * HUD_ATLAS_W + (gx + ccol)) * 4;
                p[0] = 255; p[1] = 255; p[2] = 255;
                p[3] = (unsigned char)(on ? 255 : 0);
            }
        }
    }
    /* Upload onto texture unit 7 so the caller's own unit-0 binding (e.g. a
     * scene texture) is never disturbed. */
    glActiveTexture(GL_TEXTURE7);
    glGenTextures(1, &g_atlas);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, HUD_ATLAS_W, HUD_ATLAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pix);
    free(pix);
}

void hud_init(void)
{
    GLuint vs, fs;
    GLint ok = 0;

    if (g_ready) return;

    vs = hud_compile(GL_VERTEX_SHADER, hud_vs);
    fs = hud_compile(GL_FRAGMENT_SHADER, hud_fs);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return; }

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) { g_prog = 0; return; }

    /* Query the locations the linker actually assigned rather than assuming
     * them, so the VBO is always fed to the slots the program reads.  For this
     * two-attribute program they come out as 0 and 1; the apps re-point their
     * own attribs 0/1 every frame, so sharing the slots is safe. */
    g_attr_pos   = glGetAttribLocation(g_prog, "aPos");
    g_attr_uv    = glGetAttribLocation(g_prog, "aUV");
    g_loc_win    = glGetUniformLocation(g_prog, "uWin");
    g_loc_color  = glGetUniformLocation(g_prog, "uColor");
    g_loc_atlas  = glGetUniformLocation(g_prog, "uAtlas");
    if (g_attr_pos < 0 || g_attr_uv < 0) { g_prog = 0; return; }

    glGenBuffers(1, &g_vbo);
    if (g_vbo == 0) { g_prog = 0; return; }

    /* The atlas upload bound it on unit 7; leave the caller parked on unit 0
     * with its own binding intact (do NOT unbind unit 0 here). */
    hud_build_atlas();
    glActiveTexture(GL_TEXTURE0);

    g_ready = 1;
}

void hud_set_color(float r, float g, float b, float a)
{
    g_color[0] = r; g_color[1] = g; g_color[2] = b; g_color[3] = a;
}

void hud_draw(GLuint restore_program, int win_w, int win_h, int x, int y,
              const char *text)
{
    static GLfloat verts[HUD_MAX_CHARS * HUD_VERTS_PER_QUAD * HUD_FLOATS_PER_VERT];
    GLint saved_vp[4] = { 0, 0, 0, 0 };
    GLint saved_abuf = 0;
    GLboolean saved_blend, saved_depth, saved_cull;
    int nvert = 0, i;

    if (!g_ready || !text || win_w <= 0 || win_h <= 0) return;

    /* Save the scene state the HUD disturbs. */
    glGetIntegerv(GL_VIEWPORT, saved_vp);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_abuf);
    saved_blend = glIsEnabled(GL_BLEND);
    saved_depth = glIsEnabled(GL_DEPTH_TEST);
    saved_cull  = glIsEnabled(GL_CULL_FACE);

    for (i = 0; text[i] != '\0' && i < HUD_MAX_CHARS; ++i) {
        int gi = glyph_index(text[i]);
        int gx = (gi % HUD_ATLAS_COLS) * HUD_GLYPH_PX;
        int gy = (gi / HUD_ATLAS_COLS) * HUD_GLYPH_PX;
        float u0 = (float)gx / (float)HUD_ATLAS_W;
        float u1 = (float)(gx + HUD_GLYPH_PX) / (float)HUD_ATLAS_W;
        float v0 = (float)gy / (float)HUD_ATLAS_H;
        float v1 = (float)(gy + HUD_GLYPH_PX) / (float)HUD_ATLAS_H;
        float px = (float)(x + i * HUD_GLYPH_PX);
        float py = (float)y;
        float px1 = px + (float)HUD_GLYPH_PX;
        float py1 = py + (float)HUD_GLYPH_PX;
        GLfloat *o = verts + nvert * HUD_FLOATS_PER_VERT;
        /* two triangles: TL,BL,TR  TR,BL,BR */
        o[0]=px;  o[1]=py;  o[2]=u0; o[3]=v0;
        o[4]=px;  o[5]=py1; o[6]=u0; o[7]=v1;
        o[8]=px1; o[9]=py;  o[10]=u1; o[11]=v0;
        o[12]=px1;o[13]=py; o[14]=u1; o[15]=v0;
        o[16]=px; o[17]=py1;o[18]=u0; o[19]=v1;
        o[20]=px1;o[21]=py1;o[22]=u1; o[23]=v1;
        nvert += HUD_VERTS_PER_QUAD;
    }
    if (nvert == 0) return;

    glUseProgram(g_prog);
    glViewport(0, 0, win_w, win_h);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glUniform1i(g_loc_atlas, 7);
    glUniform2f(g_loc_win, (GLfloat)win_w, (GLfloat)win_h);
    glUniform4f(g_loc_color, g_color[0], g_color[1], g_color[2], g_color[3]);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(nvert * HUD_FLOATS_PER_VERT * sizeof(GLfloat)),
                 verts, GL_STREAM_DRAW);
    glVertexAttribPointer((GLuint)g_attr_pos, 2, GL_FLOAT, GL_FALSE,
                          HUD_FLOATS_PER_VERT * sizeof(GLfloat), 0);
    glVertexAttribPointer((GLuint)g_attr_uv, 2, GL_FLOAT, GL_FALSE,
                          HUD_FLOATS_PER_VERT * sizeof(GLfloat),
                          (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)g_attr_pos);
    glEnableVertexAttribArray((GLuint)g_attr_uv);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, nvert);

    glDisableVertexAttribArray((GLuint)g_attr_uv);
    glDisableVertexAttribArray((GLuint)g_attr_pos);

    /* Restore the scene state exactly as it was. */
    glDisable(GL_BLEND);
    if (saved_blend) glEnable(GL_BLEND);
    if (saved_depth) glEnable(GL_DEPTH_TEST);
    if (saved_cull)  glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved_abuf);
    glViewport(saved_vp[0], saved_vp[1], saved_vp[2], saved_vp[3]);
    glUseProgram(restore_program);
}
