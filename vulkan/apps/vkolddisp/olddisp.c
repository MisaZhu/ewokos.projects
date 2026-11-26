/*
 * olddisp.c - CPU port of the lc3BRn font engine (Common + Buffer A tabs).
 *
 * Every formula below mirrors res/common.glsl / res/buffera.glsl verbatim
 * (float32 arithmetic, GLSL int semantics); the character sequence in
 * olddisp_text() is the macro-expanded text() of Buffer A.  See olddisp.h
 * for why the per-pixel pass reduces to glyph stamping.
 *
 * Reference: https://www.shadertoy.com/view/lc3BRn (JoostAB), fork of
 * "96-bit 8x12 Font" by Flyguy, https://www.shadertoy.com/view/Mt2GWD.
 * Verbatim sources kept in res/ (CC BY-NC-SA 3.0, attribution preserved).
 */
#include "olddisp.h"
#include "olddisp_font.h"

#include <math.h>
#include <string.h>

/* text modes, res/common.glsl */
#define NORMAL    0x00
#define INVERT    0x01
#define UNDERLINE 0x02
#define BLINVERT  0x04
#define BLINK     0x08
#define GLOW      0x10

#define BLINKRATE 2.0f
#define MAX_INT_DIGITS 4

typedef struct {
    int   mode, indent;
    float px, py;          /* print_pos */
    float resx, resy;      /* res = iResolution.xy / DOWN_SCALE */
    float itime;
    uint8_t *cov;
    int   rw, rh;
} olddisp_ctx;

int olddisp_font_w(int width)  { return (width + 1) / 2; }
int olddisp_font_h(int height) { return (height + 1) / 2; }

/* extract_bit(): shift amount clamped to [-1,24]; the interpreter (and
 * ANGLE) mask shifts with &31, so -1 becomes 31 and yields 0 for the
 * 24-bit glyph words - exactly the reference behaviour. */
static int extract_bit(uint32_t n, int b)
{
    int sb = b < -1 ? -1 : (b > 24 ? 24 : b);
    return (int)((n >> (sb & 31)) & 1u);
}

/* mod(floor(itime*BLINKRATE),2.0)==0.0 */
static int blink_phase0(float itime)
{
    return ((int)floorf(itime * BLINKRATE) & 1) == 0;
}

/* char(): apply the active text modes and stamp the 8x12 sprite at
 * print_pos, then advance one character width. */
static void olddisp_char(olddisp_ctx *c, const uint32_t glyph[4])
{
    uint32_t ch[4];
    int inv = (c->mode & INVERT) != 0;
    int gx, gy, bx, by;

    memcpy(ch, glyph, sizeof(ch));

    if ((c->mode & UNDERLINE) != 0)
        ch[3] = (ch[3] / 256) * 256 + 255;

    if ((c->mode & BLINVERT) != 0 && blink_phase0(c->itime))
        inv = !inv;

    if (inv) {
        for (gx = 0; gx < 4; gx++)
            ch[gx] = 0xFFFFFFu - ch[gx];
    }

    if ((c->mode & BLINK) != 0 && blink_phase0(c->itime)) {
        c->px += 8.0f;                 /* invisible, but still advances */
        return;
    }

    /* sprite() floors (uv - print_pos); print_pos.x is always integral,
     * print_pos.y may carry res.y's fraction, hence ceilf() here. */
    bx = (int)ceilf(c->px);
    by = (int)ceilf(c->py);
    for (gy = 0; gy < 12; gy++) {
        for (gx = 0; gx < 8; gx++) {
            int bit = (7 - gx) + gy * 8;
            int px = extract_bit(ch[0], bit - 72) +
                     extract_bit(ch[1], bit - 48) +
                     extract_bit(ch[2], bit - 24) +
                     extract_bit(ch[3], bit);
            int x, y;
            if (px == 0)
                continue;
            if ((c->mode & GLOW) != 0)
                px *= 2;
            x = bx + gx;
            y = by + gy;
            if (x >= 0 && x < c->rw && y >= 0 && y < c->rh)
                c->cov[y * c->rw + x] = (uint8_t)(c->cov[y * c->rw + x] + px);
        }
    }
    c->px += 8.0f;
}

static void olddisp_str(olddisp_ctx *c, const char *s)
{
    for (; *s; s++) {
        int idx = (unsigned char)*s - 32;
        if (idx >= 0 && idx < 96)
            olddisp_char(c, olddisp_font[idx]);
    }
}

/* charPos() */
static void olddisp_cursor(olddisp_ctx *c, int col, int line)
{
    c->px = (float)(col + c->indent) * 8.0f;
    c->py = c->resy - (float)(line + 1) * 12.0f;
}

/* CRLF */
static void olddisp_crlf(olddisp_ctx *c)
{
    c->px = (float)c->indent * 8.0f;
    c->py -= 12.0f;
}

/* print_integer(): up to MAX_INT_DIGITS digits, 'zeros' forces leading
 * zeros.  Digits index the font table at '0' (ASCII 48 -> slot 16). */
static void olddisp_int(olddisp_ctx *c, float number, int zeros)
{
    int i;
    for (i = MAX_INT_DIGITS; i >= 0; i--) {
        float p = powf(10.0f, (float)i);
        int digit = (int)fmodf(number / p, 10.0f);
        if (fabsf(number) > p || zeros > i || i == 0) {
            if (digit >= 0 && digit <= 9)
                olddisp_char(c, olddisp_font[16 + digit]);
            else
                olddisp_char(c, olddisp_font[0]);  /* get_digit() -> space */
        }
    }
}

/* text() from res/buffera.glsl, character macros expanded. */
static void olddisp_text(olddisp_ctx *c, const float date[4])
{
    int lastcol  = (int)(c->resx / 8.0f) - 1;
    int lastline = (int)(c->resy / 12.0f) - 1;
    float hour, minute, second;

    /* Credits */
    c->mode = NORMAL;
    olddisp_cursor(c, lastcol - 18, lastline - 1);
    olddisp_str(c, "Inspired by ");
    c->mode += GLOW;
    olddisp_str(c, "Flyguy");
    c->mode -= GLOW;

    c->mode = NORMAL;
    olddisp_cursor(c, lastcol - 18, lastline);
    olddisp_str(c, "Extended by ");
    c->mode += GLOW;
    olddisp_str(c, "JoostAB");
    c->mode -= GLOW;

    /* Modes */
    c->indent = 0;
    c->mode = NORMAL;
    olddisp_cursor(c, 0, 0);
    olddisp_str(c, "Modes: ");

    c->indent = 7;
    olddisp_str(c, "Normal");
    olddisp_crlf(c);
    c->mode += UNDERLINE;
    olddisp_str(c, "Underline");
    c->mode -= UNDERLINE;
    olddisp_crlf(c);
    c->mode += BLINK;
    olddisp_str(c, "Blink");
    c->mode -= BLINK;
    olddisp_crlf(c);
    c->mode += INVERT;
    olddisp_str(c, "Invert");
    c->mode -= INVERT;
    olddisp_crlf(c);
    c->mode += BLINVERT;
    olddisp_str(c, "Blinvert");
    c->mode -= BLINVERT;
    olddisp_crlf(c);
    c->mode += GLOW;
    olddisp_str(c, "Glow");
    c->mode -= GLOW;

    /* Title */
    c->indent = 0;
    olddisp_cursor(c, (lastcol / 2) - 8, lastline / 2);
    c->mode += GLOW;
    c->mode += UNDERLINE;
    olddisp_str(c, "96 bit 8x12 Font");
    c->mode -= GLOW;
    c->mode -= UNDERLINE;

    /* Demo text */
    olddisp_cursor(c, (lastcol / 2) - 22, (lastline / 2) + 2);
    c->mode = NORMAL;
    olddisp_str(c, "The quick brown fox jumps over the lazy dog.");

    /* Clock */
    hour   = floorf(date[3] / 60.0f / 60.0f);
    minute = floorf(fmodf(date[3] / 60.0f, 60.0f));
    second = floorf(fmodf(date[3], 60.0f));

    olddisp_cursor(c, lastcol - 18, 0);
    olddisp_int(c, date[2], 2);            /* day   */
    olddisp_str(c, "/");
    olddisp_int(c, date[1] + 1.0f, 2);     /* month */
    olddisp_str(c, "/");
    olddisp_int(c, date[0], 4);            /* year  */
    olddisp_str(c, " ");
    olddisp_int(c, hour, 2);
    olddisp_str(c, ":");
    olddisp_int(c, minute, 2);
    olddisp_str(c, ":");
    olddisp_int(c, second, 2);
}

void olddisp_render(const olddisp_frame *f, uint8_t *cov)
{
    olddisp_ctx c;

    memset(&c, 0, sizeof(c));
    c.rw = olddisp_font_w(f->width);
    c.rh = olddisp_font_h(f->height);
    c.cov = cov;
    c.resx = (float)f->width / 2.0f;
    c.resy = (float)f->height / 2.0f;
    c.itime = f->itime;

    memset(cov, 0, (size_t)c.rw * c.rh);
    olddisp_text(&c, f->date);
}

void olddisp_encode(const olddisp_frame *f, const uint8_t *cov, uint8_t *rgba)
{
    int rw = olddisp_font_w(f->width);
    int x, y;

    /* bottom-up rows: texture row y shows Shadertoy-space screen row y */
    for (y = 0; y < f->height; y++) {
        const uint8_t *row = cov + (size_t)(y / 2) * rw;
        uint8_t *out = rgba + (size_t)y * f->width * 4;
        for (x = 0; x < f->width; x++) {
            uint8_t v = row[x / 2];
            out[x * 4 + 0] = v >= 1 ? 255 : 0;
            out[x * 4 + 1] = v >= 2 ? 255 : 0;
            out[x * 4 + 2] = 0;
            out[x * 4 + 3] = 255;
        }
    }
}