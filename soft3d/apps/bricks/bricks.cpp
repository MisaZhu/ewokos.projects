/* Bricks Game - a faithful C++ port of iq's Shadertoy "Bricks Game"
 * (https://www.shadertoy.com/view/MddGzf, CC BY-NC-SA 3.0).
 *
 * The verbatim reference passes are preserved in res/buffer_a.glsl (the 14x14
 * gameplay simulation) and res/image.glsl (the 2D distance-field renderer);
 * res/source.json records provenance, hashes and the porting rationale.
 *
 * Why a C++ port rather than running the reference GLSL:
 *   - Buffer A reads its OWN previous frame (iChannel0 = Buffer A). The engine
 *     has no render-to-texture (every FBO entry point in libs/gles is a
 *     name-only stub), so a self-referencing multipass chain cannot run as GL.
 *   - The state it stores is float (ball position, brick hit-times = iTime);
 *     engine textures are RGBA8-only, which cannot carry that precision.
 *   - The CPU GLSL interpreter has no texelFetch.
 * So the two passes are translated literally: Buffer A runs per texel over the
 * 14x14 grid with prev/curr float buffers (same read-old/write-new ordering,
 * iFrame reset, 3-substep swept collisions, keyboard/mouse paddle drive), and
 * Image runs per output pixel with a lossless per-frame brick-table precompute
 * (doBrick depends only on the brick id and iTime). Every constant, hash, SDF
 * primitive, glow/shadow term, score glyph and the game-over tint is preserved.
 *
 * The frame is composited on the CPU into 0xAARRGGBB words and presented as a
 * fullscreen textured quad (the matrix/Fireworks idiom), so it renders
 * identically on CPU-only (virt) and GPU (raspi5/raspix) builds.
 *
 * Controls: hold the left mouse button and move to steer the paddle, or use the
 * left/right arrow keys; space restarts; ESC quits.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/keydef.h>
#include <x/x.h>
#include <x/xwin.h>

/* ------------------------------------------------------------------ *
 * Reference constants (identical in both passes).
 * ------------------------------------------------------------------ */
static const float ballRadius  = 0.035f;
static const float paddleSize  = 0.30f;
static const float paddleWidth = 0.06f;
static const float paddlePosY  = -0.90f;
static const float brickW      = 2.0f / 13.0f;
static const float brickH      = 1.0f / 15.0f;

static const float gameSpeed  = 3.0f;
static const float inputSpeed = 2.0f;

static const float shadowOffsetX = -0.03f;
static const float shadowOffsetY =  0.03f;

/* Gameplay state texture is 14x14 texels; brick grid ids are x in [0,13),
 * y in [0,12), stored at texel (id.x, 1 + id.y). */
#define GRID 14
#define BRICK_CX 13
#define BRICK_CY 12

/* ------------------------------------------------------------------ *
 * Small math helpers mirroring the GLSL builtins (highp float == 32-bit).
 * ------------------------------------------------------------------ */
static inline float fclamp(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float ffract(float x) { return x - floorf(x); }
static inline float fsign(float x) { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }
static inline float fmax2(float a, float b) { return a > b ? a : b; }
static inline float fmin2(float a, float b) { return a < b ? a : b; }
static inline float smoothstepf(float e0, float e1, float x)
{
    float t = fclamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline void mixv3(const float a[3], const float b[3], float t, float o[3])
{
    o[0] = a[0] + (b[0] - a[0]) * t;
    o[1] = a[1] + (b[1] - a[1]) * t;
    o[2] = a[2] + (b[2] - a[2]) * t;
}
static inline float hash1f(float n) { return ffract(sinf(n) * 138.5453123f); }

/* ------------------------------------------------------------------ *
 * Game state: two 14x14 RGBA-float buffers, ping-ponged each frame.
 * prev = Buffer A output of the previous frame (what the sim reads);
 * the Image pass reads the freshly written buffer.
 * ------------------------------------------------------------------ */
typedef float StateBuf[GRID][GRID][4];
static StateBuf g_bufA, g_bufB;
static StateBuf *g_prev = &g_bufA;   /* read by the simulation          */
static StateBuf *g_curr = &g_bufB;   /* written by the sim, read by Image */

static int   g_frame = 0;
static float g_time = 0.0f;

/* Input state fed to the simulation. */
static int   g_key_left = 0, g_key_right = 0, g_key_space = 0;
static int   g_mdown = 0;
static float g_mx = 0.0f;

/* ------------------------------------------------------------------ *
 * Buffer A: swept-disk collision primitives (verbatim translations).
 * ------------------------------------------------------------------ */
static float iPlane(float rox, float roy, float rdx, float rdy, float rad,
                    float plax, float play, float plaz)
{
    float a = rdx * plax + rdy * play;
    if (a > 0.0f) return -1.0f;
    float t = (rad - plaz - (rox * plax + roy * play)) / a;
    if (t >= 1.0f) t = -1.0f;
    return t;
}

/* Returns tN in *out_t and the normal in (*out_nx,*out_ny); -1 on miss. */
static float iBox(float rox, float roy, float rdx, float rdy, float rad,
                  float bcex, float bcey, float bwise_x, float bwise_y,
                  float *out_nx, float *out_ny)
{
    float mx = 1.0f / rdx, my = 1.0f / rdy;
    float nx = mx * (rox - bcex), ny = my * (roy - bcey);
    float ax = fabsf(mx), ay = fabsf(my);
    float kx = ax * (bwise_x + rad), ky = ay * (bwise_y + rad);
    float t1x = -nx - kx, t1y = -ny - ky;
    float t2x = -nx + kx, t2y = -ny + ky;
    float tN = fmax2(t1x, t1y);
    float tF = fmin2(t2x, t2y);
    if (tN > tF || tF < 0.0f) return -1.0f;
    if (tN >= 1.0f) return -1.0f;
    /* nor = -sign(rd) * step(t1.yx, t1.xy) */
    float sx = fsign(rdx), sy = fsign(rdy);
    float stepx = (t1x < t1y) ? 0.0f : 1.0f;   /* step(t1.y, t1.x) */
    float stepy = (t1y < t1x) ? 0.0f : 1.0f;   /* step(t1.x, t1.y) */
    *out_nx = -sx * stepx;
    *out_ny = -sy * stepy;
    return tN;
}

/* One Buffer A step: read *prev, write *curr, exactly like the reference pass. */
static void simulate_buffer_a(int iFrame, float iTime, float iTimeDelta, float resX, float resY)
{
    StateBuf &P = *g_prev;
    StateBuf &C = *g_curr;

    for (int py = 0; py < GRID; ++py) {
        for (int px = 0; px < GRID; ++px) {
            float fragCoordX = (float)px + 0.5f;
            float fragCoordY = (float)py + 0.5f;
            if (fragCoordX > 14.0f || fragCoordY > 14.0f) continue; /* discard */

            /* load game state */
            float bpx = P[0][0][0], bpy = P[0][0][1], bvz = P[0][0][2], bvw = P[0][0][3];
            float paddlePos = P[0][1][0];
            float points    = P[0][2][0];
            float state     = P[0][3][0];
            float lastHitX  = P[0][4][0], lastHitY = P[0][4][1], lastHitZ = P[0][4][2];
            float brickX    = P[py][px][0], brickY = P[py][px][1];

            /* reset */
            if (iFrame == 0) state = -1.0f;
            if (state < -0.5f) {
                state = 0.0f;
                bpx = 0.0f;
                bpy = paddlePosY + ballRadius + paddleWidth * 0.5f + 0.001f;
                bvz = 0.6f; bvw = 1.0f;
                paddlePos = 0.0f;
                points = 0.0f;
                state = 0.0f;
                brickX = 1.0f; brickY = -5.0f;
                lastHitX = lastHitY = lastHitZ = -1.0f;
                if (fragCoordX < 1.0f || fragCoordX > 12.0f) { brickX = 0.0f; brickY = -10.0f; }
            }

            if (state > 0.5f) {
                /* game over (or won): wait for space to resume */
                float pressSpace = g_key_space ? 1.0f : 0.0f;
                if (pressSpace > 0.5f) state = -1.0f;
            } else if (state < 0.5f) {
                /* paddle */
                float oldPaddlePos = paddlePos;
                if (g_mdown && g_mx >= 0.0f) {
                    paddlePos = (-1.0f + 2.0f * g_mx / resX) * resX / resY;
                } else {
                    float moveRight = g_key_right ? 1.0f : 0.0f;
                    float moveLeft  = g_key_left  ? 1.0f : 0.0f;
                    paddlePos += 0.02f * inputSpeed * (moveRight - moveLeft);
                }
                paddlePos = fclamp(paddlePos,
                                   -1.0f + 0.5f * paddleSize + paddleWidth * 0.5f,
                                    1.0f - 0.5f * paddleSize - paddleWidth * 0.5f);
                float moveTotal = fsign(paddlePos - oldPaddlePos);

                /* ball */
                float dis = 0.01f * gameSpeed * (iTimeDelta * 60.0f);
                for (int j = 0; j < 3; ++j) {
                    int oidX = -1, oidY = 0, oidZ = 0;
                    float norX = 0.0f, norY = 0.0f;
                    float t = 1000.0f;
                    float rdx = dis * bvz, rdy = dis * bvw;

                    /* walls */
                    float t1 = iPlane(bpx, bpy, rdx, rdy, ballRadius, -1.0f, 0.0f, 1.0f);
                    if (t1 > 0.0f)          { t = t1; norX = -1.0f; norY = 0.0f; oidX = 1; }
                    float t2 = iPlane(bpx, bpy, rdx, rdy, ballRadius, 1.0f, 0.0f, 1.0f);
                    if (t2 > 0.0f && t2 < t){ t = t2; norX = 1.0f;  norY = 0.0f; oidX = 2; }
                    float t3 = iPlane(bpx, bpy, rdx, rdy, ballRadius, 0.0f, -1.0f, 1.0f);
                    if (t3 > 0.0f && t3 < t){ t = t3; norX = 0.0f;  norY = -1.0f; oidX = 3; }

                    /* paddle */
                    float pnx, pny;
                    float t4 = iBox(bpx, bpy, rdx, rdy, ballRadius,
                                    paddlePos, paddlePosY, paddleSize * 0.5f, paddleWidth * 0.5f,
                                    &pnx, &pny);
                    if (t4 > 0.0f && t4 < t) { t = t4; norX = pnx; norY = pny; oidX = 4; }

                    /* bricks */
                    int idrx = (int)floorf((1.0f + bpx) / brickW);
                    int idry = (int)floorf((1.0f - bpy) / brickH);
                    int vsx = (int)fsign(bvz), vsy = (int)fsign(bvw);
                    for (int jj = 0; jj < 3; ++jj)
                    for (int ii = 0; ii < 3; ++ii) {
                        int idx = idrx + vsx * ii;
                        int idy = idry - vsy * jj;
                        if (idx >= 0 && idx < 13 && idy >= 0 && idy < 12) {
                            float brickHere = P[1 + idy][idx][0];
                            if (brickHere > 0.5f) {
                                float cex = -1.0f + (float)idx * brickW + 0.5f * brickW;
                                float cey =  1.0f - (float)idy * brickH - 0.5f * brickH;
                                float bnx, bny;
                                float t5 = iBox(bpx, bpy, rdx, rdy, ballRadius,
                                                cex, cey, 0.5f * brickW, 0.5f * brickH, &bnx, &bny);
                                if (t5 > 0.0f && t5 < t) { oidX = 5; oidY = idx; oidZ = idy; t = t5; norX = bnx; norY = bny; }
                            }
                        }
                    }

                    if (oidX < 0) break;

                    /* bounce */
                    bpx += t * rdx;
                    bpy += t * rdy;
                    dis *= 1.0f - t;
                    rdx = dis * bvz; rdy = dis * bvw;

                    if (oidX < 4) {
                        /* walls: reflect velocity about the normal */
                        float d = 2.0f * (norX * bvz + norY * bvw);
                        bvz -= d * norX; bvw -= d * norY;
                        lastHitZ = iTime;
                    } else if (oidX < 5) {
                        float d = 2.0f * (norX * bvz + norY * bvw);
                        bvz -= d * norX; bvw -= d * norY;
                        if (bpx > (paddlePos + paddleSize * 0.5f))      bvz =  fabsf(bvz);
                        else if (bpx < (paddlePos - paddleSize * 0.5f)) bvz = -fabsf(bvz);
                        bvz += 0.37f * moveTotal;
                        bvz += 0.11f * hash1f((float)iFrame * 7.1f);
                        bvz = fclamp(bvz, -0.9f, 0.9f);
                        float len = sqrtf(bvz * bvz + bvw * bvw);
                        if (len > 0.0f) { bvz /= len; bvw /= len; }
                        lastHitX = iTime; lastHitY = iTime;
                    } else if (oidX < 6) {
                        float d = 2.0f * (norX * bvz + norY * bvw);
                        bvz -= d * norX; bvw -= d * norY;
                        lastHitY = iTime;
                        points += 1.0f;
                        if (points > 131.5f) state = 2.0f;
                        if (px == (0 + oidY) && py == (1 + oidZ)) { brickX = 0.0f; brickY = iTime; }
                    }
                }

                bpx += dis * bvz;
                bpy += dis * bvw;
                if (bpy < -1.0f) state = 1.0f; /* missed: game over */
            }

            /* store game state (sequential, matching the storeValue chain) */
            float o0 = 0.0f, o1 = 0.0f, o2 = 0.0f, o3 = 0.0f;
            if (px == 0 && py == 0) { o0 = bpx; o1 = bpy; o2 = bvz; o3 = bvw; }
            if (px == 1 && py == 0) { o0 = paddlePos; o1 = 0.0f; o2 = 0.0f; o3 = 0.0f; }
            if (px == 2 && py == 0) { o0 = points; o1 = 0.0f; o2 = 0.0f; o3 = 0.0f; }
            if (px == 3 && py == 0) { o0 = state; o1 = 0.0f; o2 = 0.0f; o3 = 0.0f; }
            if (px == 4 && py == 0) { o0 = lastHitX; o1 = lastHitY; o2 = lastHitZ; o3 = 0.0f; }
            if (px >= 0 && py >= 1 && px <= 13 && py <= 12) { o0 = brickX; o1 = brickY; o2 = 0.0f; o3 = 0.0f; }
            C[py][px][0] = o0; C[py][px][1] = o1; C[py][px][2] = o2; C[py][px][3] = o3;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Image pass: 2D distance-field renderer.
 * ------------------------------------------------------------------ */
static inline float udSegment(float px_, float py_, float ax, float ay, float bx, float by)
{
    float pax = px_ - ax, pay = py_ - ay;
    float bax = bx - ax, bay = by - ay;
    float h = fclamp((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0f, 1.0f);
    float dx = pax - bax * h, dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy);
}
static inline float udHorizontalSegment(float px_, float py_, float xa, float xb, float y)
{
    float pax = px_ - xa, pay = py_ - y;
    float ba = xb - xa;
    pax -= ba * fclamp(pax / ba, 0.0f, 1.0f);
    return sqrtf(pax * pax + pay * pay);
}
static inline float udRoundBox(float px_, float py_, float cx, float cy, float bx, float by, float r)
{
    float qx = fmax2(fabsf(px_ - cx) - bx, 0.0f);
    float qy = fmax2(fabsf(py_ - cy) - by, 0.0f);
    return sqrtf(qx * qx + qy * qy) - r;
}

/* Per-frame brick table (doBrick depends only on id and iTime). */
struct BrickCell { float col[3]; float glo; float cenx, ceny; float alp; };
static BrickCell g_bricks[BRICK_CY][BRICK_CX];

static const int g_font[10] = { 0x75557, 0x22222, 0x74717, 0x74747, 0x11574,
                                0x71747, 0x71757, 0x74444, 0x75757, 0x75747 };
static const int g_powers[5] = { 1, 10, 100, 1000, 10000 };

static int PrintInt(float uvx, float uvy, int value)
{
    const int maxDigits = 3;
    if (fabsf(uvy - 0.5f) < 0.5f) {
        int iu = (int)floorf(uvx);
        if (iu >= 0 && iu < maxDigits) {
            int n = (value / g_powers[maxDigits - iu - 1]) % 10;
            uvx = ffract(uvx);
            int qx = (int)floorf(uvx * 4.0f);
            int qy = (int)floorf(uvy * 5.0f);
            return (g_font[n] >> (qx + qy * 4)) & 1;
        }
    }
    return 0;
}

static void build_brick_table(float iTime, StateBuf &S)
{
    for (int idy = 0; idy < BRICK_CY; ++idy) {
        for (int idx = 0; idx < BRICK_CX; ++idx) {
            BrickCell &c = g_bricks[idy][idx];
            c.col[0] = c.col[1] = c.col[2] = 0.0f;
            c.glo = 0.0f; c.cenx = 0.0f; c.ceny = 0.0f; c.alp = 0.0f;
            /* doBrick valid range: id.x>0 && id.x<13 && id.y>=0 && id.y<12 */
            if (idx > 0 && idx < 13 && idy >= 0 && idy < 12) {
                float bx = S[1 + idy][idx][0];
                float by = S[1 + idy][idx][1];
                float alp = 1.0f, glo = 0.0f;
                if (bx < 0.5f) {
                    float t = fmax2(0.0f, iTime - by - 0.1f);
                    alp = expf(-2.0f * t);
                    glo = expf(-4.0f * t);
                }
                float col[3] = { 0.0f, 0.0f, 0.0f };
                if (alp > 0.001f) {
                    float fid = hash1f((float)(idx * 3 + idy * 16));
                    float s0 = sinf(fid * 2.0f + 4.5f);
                    float s1 = sinf(fid * 2.0f + 4.5f + 1.0f);
                    float s2 = sinf(fid * 2.0f + 4.5f + 1.0f);
                    col[0] = 0.5f + 0.4f * s0;
                    col[1] = 0.5f + 0.4f * s1;
                    col[2] = 0.6f + 0.4f * s2;
                    if (hash1f(fid * 13.1f) > 0.85f) {
                        col[0] = 1.0f - 0.9f * col[0];
                        col[1] = 1.0f - 0.9f * col[1];
                        col[2] = 1.0f - 0.9f * col[2];
                        col[0] += 0.2f;
                        col[1] += 0.2f;
                    }
                }
                c.alp = alp; c.glo = glo;
                c.col[0] = col[0]; c.col[1] = col[1]; c.col[2] = col[2];
                c.cenx = -1.0f + (float)idx * brickW + 0.5f * brickW;
                c.ceny =  1.0f - (float)idy * brickH - 0.5f * brickH;
            }
        }
    }
}

static inline const BrickCell &brick_at(int idx, int idy, const BrickCell &zero)
{
    if (idx > 0 && idx < BRICK_CX && idy >= 0 && idy < BRICK_CY) return g_bricks[idy][idx];
    return zero;
}

static void render_image(int W, int H, float iTime, StateBuf &S, uint32_t *out)
{
    build_brick_table(iTime, S);

    float ballPosX = S[0][0][0], ballPosY = S[0][0][1];
    float paddlePos = S[0][1][0];
    float points    = S[0][2][0];
    float gstate    = S[0][3][0];
    float lastHitX  = S[0][4][0], lastHitY = S[0][4][1], lastHitZ = S[0][4][2];

    float resX = (float)W, resY = (float)H;
    float pxf = 2.0f / resY;
    float b = brickW * 0.17f;
    float halfBW = 0.5f * brickW, halfBH = 0.5f * brickH;
    int ipoints = (int)points;
    float borderA = 0.8f + 0.2f * sinf(2.6f * iTime) + 0.1f * sinf(4.0f * iTime);
    float gameOverMix = gstate * (0.5f + 0.5f * sinf(30.0f * iTime));

    static const BrickCell zero = { { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, 0.0f, 0.0f };

    for (int oy = 0; oy < H; ++oy) {
        /* Shadertoy gl_FragCoord is bottom-left origin; bbufpix row 0 is top. */
        float fragCoordY = (float)(H - 1 - oy) + 0.5f;
        for (int ox = 0; ox < W; ++ox) {
            float fragCoordX = (float)ox + 0.5f;
            float uvx = (2.0f * fragCoordX - resX) / resY;
            float uvy = (2.0f * fragCoordY - resY) / resY;

            float col[3], emi[3] = { 0.0f, 0.0f, 0.0f };

            /* board */
            float luv = sqrtf(uvx * uvx + uvy * uvy);
            float k = 0.6f * (1.0f - 0.4f * luv);
            col[0] = 0.4f * k; col[1] = 0.6f * k; col[2] = 0.7f * k;
            float grid = sinf(uvx * 80.0f) * sinf(uvy * 80.0f);
            float gf = 1.0f - 0.1f * smoothstepf(0.0f, 1.0f, grid)
                                  * (1.0f - smoothstepf(1.0f, 1.01f, fabsf(uvx)));
            col[0] *= gf; col[1] *= gf; col[2] *= gf;

            /* bricks: soft shadow */
            {
                float stx = uvx + shadowOffsetX, sty = uvy + shadowOffsetY;
                int idx = (int)floorf((1.0f + stx) / brickW);
                int idy = (int)floorf((1.0f - sty) / brickH);
                float sha = 0.0f;
                for (int jj = -1; jj <= 1; ++jj)
                for (int ii = -1; ii <= 1; ++ii) {
                    const BrickCell &c = brick_at(idx + ii, idy + jj, zero);
                    float f = udRoundBox(stx, sty, c.cenx, c.ceny, halfBW - b, halfBH - b, b);
                    float s = 1.0f - smoothstepf(-brickH * 0.5f, brickH * 1.0f, f);
                    s = mixf(0.0f, s, c.alp);
                    sha = fmax2(sha, s);
                }
                float sc[3] = { col[0] * 0.4f, col[1] * 0.4f, col[2] * 0.4f };
                mixv3(col, sc, sha, col);
            }

            /* bricks: shape + gathered glow */
            {
                int idx = (int)floorf((1.0f + uvx) / brickW);
                int idy = (int)floorf((1.0f - uvy) / brickH);

                const BrickCell &c = brick_at(idx, idy, zero);
                if (c.alp > 0.0001f) {
                    float f = udRoundBox(uvx, uvy, c.cenx, c.ceny, halfBW - b, halfBH - b, b);
                    float glo = c.glo + 0.6f * smoothstepf(-4.0f * pxf, 0.0f, f);
                    float bc[3];
                    float m1 = 0.7f + 0.3f * smoothstepf(-4.0f * pxf, -2.0f * pxf, f);
                    float m2 = 0.5f + 1.7f * glo;
                    bc[0] = c.col[0] * m1 * m2; bc[1] = c.col[1] * m1 * m2; bc[2] = c.col[2] * m1 * m2;
                    float t = c.alp * (1.0f - smoothstepf(-pxf, pxf, f));
                    mixv3(col, bc, t, col);
                }

                for (int jj = -1; jj <= 1; ++jj)
                for (int ii = -1; ii <= 1; ++ii) {
                    const BrickCell &g = brick_at(idx + ii, idy + jj, zero);
                    if (g.glo == 0.0f) continue;   /* bcol*bglo == 0: exact skip */
                    float f = udRoundBox(uvx, uvy, g.cenx, g.ceny, halfBW - b, halfBH - b, b);
                    float e = g.glo * expf(-600.0f * f * f);
                    emi[0] += g.col[0] * e; emi[1] += g.col[1] * e; emi[2] += g.col[2] * e;
                }
            }

            /* ball */
            {
                float hit = expf(-4.0f * (iTime - lastHitY));
                float dxs = uvx - ballPosX + shadowOffsetX, dys = uvy - ballPosY + shadowOffsetY;
                float f = 1.0f - smoothstepf(ballRadius * 0.5f, ballRadius * 2.0f, sqrtf(dxs * dxs + dys * dys));
                float sc[3] = { col[0] * 0.4f, col[1] * 0.4f, col[2] * 0.4f };
                mixv3(col, sc, f, col);

                float dx = uvx - ballPosX, dy = uvy - ballPosY;
                f = sqrtf(dx * dx + dy * dy) - ballRadius;
                float bc[3] = { 1.0f, 0.6f, 0.2f };
                float m1 = 1.0f + 0.7f * smoothstepf(-3.0f * pxf, -1.0f * pxf, f);
                float m2 = 0.7f + 0.3f * hit;
                bc[0] *= m1 * m2; bc[1] *= m1 * m2; bc[2] *= m1 * m2;
                float t = 1.0f - smoothstepf(0.0f, pxf, f);
                mixv3(col, bc, t, col);
                float e = 0.75f * hit * expf(-500.0f * f * f);
                emi[0] += bc[0] * e; emi[1] += bc[1] * e; emi[2] += bc[2] * e;
            }

            /* paddle */
            {
                float dt = iTime - lastHitX;
                float hit = expf(-4.0f * dt) * sinf(20.0f * dt);
                float hit2 = expf(-4.0f * dt);
                float ratio = fabsf(uvx - paddlePos) / (paddleSize * 0.5f);
                float y = uvy + 0.04f * hit * (1.0f - ratio * ratio);

                float f = udHorizontalSegment(uvx + shadowOffsetX, y + shadowOffsetY,
                                              paddlePos - paddleSize * 0.5f, paddlePos + paddleSize * 0.5f, paddlePosY);
                f = 1.0f - smoothstepf(paddleWidth * 0.5f * 0.5f, paddleWidth * 0.5f * 2.0f, f);
                float sc[3] = { col[0] * 0.4f, col[1] * 0.4f, col[2] * 0.4f };
                mixv3(col, sc, f, col);

                f = udHorizontalSegment(uvx, y, paddlePos - paddleSize * 0.5f,
                                        paddlePos + paddleSize * 0.5f, paddlePosY) - paddleWidth * 0.5f;
                float bc[3] = { 1.0f, 0.6f, 0.2f };
                float m1 = 1.0f + 0.7f * smoothstepf(-3.0f * pxf, -1.0f * pxf, f);
                float m2 = 0.7f + 0.3f * hit2;
                bc[0] *= m1 * m2; bc[1] *= m1 * m2; bc[2] *= m1 * m2;
                float t = 1.0f - smoothstepf(-pxf, pxf, f);
                mixv3(col, bc, t, col);
                float e = 0.75f * hit2 * expf(-500.0f * f * f);
                emi[0] += bc[0] * e; emi[1] += bc[1] * e; emi[2] += bc[2] * e;
            }

            /* borders */
            {
                float f = fabsf(fabsf(uvx) - 1.02f);
                f = fmin2(f, udHorizontalSegment(uvx, uvy, -1.0f, 1.0f, 1.0f));
                f *= 2.0f;
                if (f <= 0.6f) {   /* beyond this every term is < 0.5/255: exact in 8-bit */
                    float a = borderA;
                    float hit = expf(-4.0f * (iTime - lastHitZ));
                    a *= 1.0f - 0.3f * hit;
                    float ff = f * f;
                    float e1 = a * 0.5f * expf(-30.0f * ff);
                    float e2 = a * 0.5f * expf(-150.0f * ff);
                    float e3 = a * 1.7f * expf(-900.0f * ff);
                    col[0] += e1 * 0.6f + e2 * 0.6f + e3 * 0.6f;
                    col[1] += e1 * 0.30f + e2 * 0.35f + e3 * 0.50f;
                    col[2] += e1 * 0.1f + e2 * 0.2f + e3 * 0.3f;
                }
            }

            /* score */
            {
                float suvx = (uvx - (-1.5f)) * 10.0f;
                float suvy = (uvy - 0.8f) * 10.0f;
                float f = (float)PrintInt(suvx, suvy, ipoints);
                float white[3] = { 1.0f, 1.0f, 1.0f };
                mixv3(col, white, f, col);
            }

            /* emission + game over tint */
            col[0] += emi[0]; col[1] += emi[1]; col[2] += emi[2];
            float orange[3] = { 1.0f, 0.5f, 0.2f };
            mixv3(col, orange, gameOverMix, col);

            int r = (int)(fclamp(col[0], 0.0f, 1.0f) * 255.0f + 0.5f);
            int g = (int)(fclamp(col[1], 0.0f, 1.0f) * 255.0f + 0.5f);
            int bl = (int)(fclamp(col[2], 0.0f, 1.0f) * 255.0f + 0.5f);
            out[(long)oy * W + ox] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Host: EGL window, CPU-composite fullscreen blit, input.
 * ------------------------------------------------------------------ */
typedef uint32_t pix_t;
static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static GLuint program = 0, quad_vbo = 0, texture = 0;
static GLint loc_tex = -1;
static xwin_t *g_win = NULL;

static pix_t *bbufpix = NULL;
static uint8_t *rgba_buf = NULL;
static int width = 0, height = 0, buf_width = 0, buf_height = 0;
static int g_fpslog = 0, g_result = 0;
static uint32_t g_start_ms = 0, g_last_ms = 0, g_fps_ms = 0;
static int g_fps_frames = 0;

static const char *quad_vs =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";
static const char *quad_fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex0;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTex0, vUV);\n"
    "}\n";

/* Canonical fullscreen quad; texture row 0 maps to the top of the window
 * (top-down bbufpix), matching the matrix/Fireworks direct-blit fast-path. */
static const GLfloat quad_verts[] = {
    -1.0f,  1.0f, 0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
};

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    if (!sh) return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "bricks: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static void init_gl(void)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    GLuint vs = compile_shader(GL_VERTEX_SHADER, quad_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, quad_fs);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aUV");
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "bricks: program link failed: %s\n", log);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);
    loc_tex = glGetUniformLocation(program, "uTex0");

    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(loc_tex, 0);
}

static int ensure_buffer(int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    if (bbufpix && w == buf_width && h == buf_height) return 1;
    free(bbufpix); free(rgba_buf);
    bbufpix = (pix_t *)malloc((size_t)w * h * sizeof(pix_t));
    rgba_buf = (uint8_t *)malloc((size_t)w * h * 4);
    if (!bbufpix || !rgba_buf) {
        free(bbufpix); free(rgba_buf);
        bbufpix = NULL; rgba_buf = NULL;
        return 0;
    }
    width = w; height = h; buf_width = w; buf_height = h;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glViewport(0, 0, w, h);
    return 1;
}

static int init_egl(xwin_t *xwin)
{
    EGLint major, minor, ncfg = 0;
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
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

static void render_frame(void)
{
    EGLint w = 0, h = 0;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    if (!ensure_buffer(w, h)) return;

    uint32_t now = kernel_tic_ms(0);
    if (g_last_ms == 0) g_last_ms = now;
    float dt = (float)(now - g_last_ms) / 1000.0f;
    g_last_ms = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;   /* avoid teleport after a stall */

    /* Advance the simulation and render the just-computed state, matching the
     * Shadertoy multipass order (Buffer A current frame feeds Image). */
    simulate_buffer_a(g_frame, g_time, dt, (float)width, (float)height);
    render_image(width, height, g_time, *g_curr, bbufpix);

    /* ping-pong: next frame reads this frame's output as its previous state */
    StateBuf *tmp = g_prev; g_prev = g_curr; g_curr = tmp;
    g_frame++;
    g_time += dt;

    long total = (long)width * height;
    for (long i = 0; i < total; ++i) {
        uint32_t px = bbufpix[i];
        rgba_buf[i * 4 + 0] = (px >> 16) & 0xFF;
        rgba_buf[i * 4 + 1] = (px >> 8) & 0xFF;
        rgba_buf[i * 4 + 2] = px & 0xFF;
        rgba_buf[i * 4 + 3] = (px >> 24) & 0xFF;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba_buf);

    glUseProgram(program);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);

    if (!eglSwapBuffers(egl_dpy, egl_surf)) {
        fprintf(stderr, "bricks: swap failed: 0x%x\n", (unsigned)eglGetError());
        g_result = 1;
        xwin_close(g_win);
        return;
    }

    if (g_fpslog) {
        g_fps_frames++;
        uint32_t el = now - g_fps_ms;
        if (el >= 1000) {
            printf("bricks: fps=%.2f %dx%d\n", g_fps_frames * 1000.0f / el, width, height);
            g_fps_ms = now; g_fps_frames = 0;
        }
    }
}

static void on_event(xwin_t *xwin, xevent_t *ev)
{
    if (ev->type == XEVT_IM) {
        int k = ev->value.im.value;
        if (ev->state == XIM_STATE_PRESS) {
            if (k == KEY_ESC)   { xwin_close(xwin); return; }
            if (k == KEY_LEFT)  g_key_left = 1;
            if (k == KEY_RIGHT) g_key_right = 1;
            if (k == KEY_SPACE) g_key_space = 1;
        } else { /* release */
            if (k == KEY_LEFT)  g_key_left = 0;
            if (k == KEY_RIGHT) g_key_right = 0;
            if (k == KEY_SPACE) g_key_space = 0;
        }
    } else if (ev->type == XEVT_MOUSE) {
        if (ev->state == MOUSE_STATE_DOWN) {
            g_mdown = 1;
            g_mx = (float)ev->value.mouse.x;
        } else if (ev->state == MOUSE_STATE_UP) {
            g_mdown = 0;
        } else if (ev->state == MOUSE_STATE_DRAG || ev->state == MOUSE_STATE_MOVE) {
            if (g_mdown) g_mx = (float)ev->value.mouse.x;
        }
    }
}

static void loop(void *p)
{
    (void)p;
    render_frame();
    usleep(1000);
}

int main(int argc, char **argv)
{
    int win_w = 600, win_h = 400, fullscreen = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "fpslog")) g_fpslog = 1;
        else if (!strcmp(argv[i], "full")) fullscreen = 1;
        else if (!strncmp(argv[i], "window", 6)) {
            char *mid, *end;
            long w = strtol(argv[i] + 6, &mid, 10);
            if (mid == argv[i] + 6 || *mid != 'x') { fprintf(stderr, "bricks: bad window arg\n"); return 1; }
            long h = strtol(mid + 1, &end, 10);
            if (end == mid + 1 || *end || w <= 0 || h <= 0 || w > 8192 || h > 8192) { fprintf(stderr, "bricks: bad window arg\n"); return 1; }
            win_w = (int)w; win_h = (int)h;
        } else {
            fprintf(stderr, "Usage: bricks [window800x600] [full] [fpslog]\n");
            return 1;
        }
    }

    memset(&g_bufA, 0, sizeof(g_bufA));
    memset(&g_bufB, 0, sizeof(g_bufB));

    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;
    g_win = xwin_open(&x, -1, 32, 32, win_w, win_h, "Bricks Game", XWIN_STYLE_NORMAL);
    if (!g_win) return 1;
    g_win->on_event = on_event;

    if (!init_egl(g_win)) {
        fprintf(stderr, "bricks: failed to initialize EGL\n");
        return 1;
    }
    /* CPU-composited app: pin the context to the CPU rasteriser so the
     * fullscreen blit uses the direct repack fast-path (matrix idiom). */
    glForceCpuEWOK();
    init_gl();

    {
        EGLint w = win_w, h = win_h;
        eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
        eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
        if (w <= 0 || h <= 0) { w = win_w; h = win_h; }
        ensure_buffer(w, h);
    }
    if (fullscreen) xwin_fullscreen(g_win);

    g_start_ms = g_last_ms = g_fps_ms = kernel_tic_ms(0);
    xwin_set_visible(g_win, true);
    x_run(&x, g_win);

    free(bbufpix); free(rgba_buf);
    bbufpix = NULL; rgba_buf = NULL;
    xwin_destroy(g_win);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, egl_surf);
    eglDestroyContext(egl_dpy, egl_ctx);
    eglTerminate(egl_dpy);
    return g_result;
}
