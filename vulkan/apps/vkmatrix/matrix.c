/*
 * matrix.c - faithful C port of "Inside the Matrix" (Shadertoy 4t3BWl).
 *
 * Original shader by And390 (https://www.shadertoy.com/view/4t3BWl), who
 * grants: "Feel free to do anything you want with this code."  The runes are
 * based on FabriceNeyret2's "runes" (4ltyDM), itself based on otaviogood's
 * "runes" (MsXSRn).  This port keeps the reference algorithm, constants and
 * float precision; the only deviations are performance-motivated and
 * value-preserving:
 *   - pow(x, 2/4/16) on non-negative bases -> repeated squaring,
 *   - the per-pixel camera rotations are hoisted into one per-frame 3x3
 *     matrix (they are linear maps applied to (uv.x, 2, uv.y)),
 *   - sin()/cos() use the float libm variants (GLSL highp float semantics).
 * The verbatim reference pass is kept alongside in res/reference.glsl.
 */
#include "matrix.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265359f

/* reference constants */
#define SPEED_DEFAULT      1.0f
#define STRIP_CHARS_MIN    7.0f
#define STRIP_CHARS_MAX    40.0f
#define STRIP_CHAR_HEIGHT  0.15f
#define STRIP_CHAR_WIDTH   0.10f
#define ZCELL_SIZE         (1.0f * (STRIP_CHAR_HEIGHT * STRIP_CHARS_MAX))
#define XYCELL_SIZE        (12.0f * STRIP_CHAR_WIDTH)
#define BLOCK_SIZE         10
#define BLOCK_GAP          2
#define WALK_SPEED         (1.0f * XYCELL_SIZE)
#define BLOCKS_BEFORE_TURN 3.0f

static float fractf(float x) { return x - floorf(x); }
static float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static float smoothstepf(float e0, float e1, float x)
{
    float d = e1 - e0;
    float t = d == 0.0f ? 0.0f : (x - e0) / d;
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static float sq(float x)   { return x * x; }
static float pow4(float x) { float x2 = x * x; return x2 * x2; }
static float pow16(float x){ float x2 = x * x, x4 = x2 * x2, x8 = x4 * x4; return x8 * x8; }

/* ---- per-frame memo caches -------------------------------------------------
 * The hash inputs repeat heavily across pixels of one frame: a column's
 * (cell, zcell) hashes are shared by every ray crossing it, and a rune's
 * stroke endpoints depend only on its (floored-time) seed.  Direct-mapped
 * caches keyed by the exact inputs memoize them without changing any value.
 * A frame stamp invalidates everything between frames. */
#define XY_CACHE_N   512
#define COL_CACHE_N  1024
#define RUNE_CACHE_N 2048

typedef struct {
    int   cx, cy, stamp;
    float h, z_shift, char_z_shift;
} xy_entry;
typedef struct {
    int   cx, cy, zcell, stamp;
    float ch2[4], chars_count, target_length, target_z, target[2];
} col_entry;
typedef struct {
    float sx, sy;
    int   stamp;
    float pos[4][4];      /* snapped stroke endpoints */
    uint8_t ok[4];        /* stroke not degenerate    */
} rune_entry;

/* One cache set per render worker: row bands run concurrently on separate
 * pthreads, so a shared direct-mapped cache would race on its entries. */
typedef struct {
    xy_entry   xy[XY_CACHE_N];
    col_entry  col[COL_CACHE_N];
    rune_entry rune[RUNE_CACHE_N];
    int        stamp;
} matrix_cache;

static matrix_cache g_cache[MATRIX_MAX_WORKERS];

static uint32_t mix32(uint32_t h)
{
    h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
    return h;
}
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ---- random (reference hash family) -------------------------------------- */

static float hash1(float v)
{
    return fractf(sinf(v) * 43758.5453123f);
}

static float hash2f(float x, float y)   /* hash(vec2) */
{
    return hash1(x * 5.3983f + y * 5.4427f);
}

/* hash2(vec2): v * mat2(127.1, 311.7, 269.5, 183.3) (row-vector convention) */
static void hash2(float x, float y, float out[2])
{
    float px = x * 127.1f + y * 269.5f;
    float py = x * 311.7f + y * 183.3f;
    out[0] = fractf(sinf(px) * 43758.5453123f);
    out[1] = fractf(sinf(py) * 43758.5453123f);
}

/* hash4(vec2): v * mat4x2(...) */
static void hash4_v2(float x, float y, float out[4])
{
    float p[4];
    p[0] = x * 127.1f + y * 311.7f;
    p[1] = x * 269.5f + y * 183.3f;
    p[2] = x * 113.5f + y * 271.9f;
    p[3] = x * 246.1f + y * 124.6f;
    for (int i = 0; i < 4; i++)
        out[i] = fractf(sinf(p[i]) * 43758.5453123f);
}

/* hash4(vec3): v * mat4x3(...) */
static void hash4_v3(float x, float y, float z, float out[4])
{
    float p[4];
    p[0] = x * 127.1f + y * 311.7f + z * 74.7f;
    p[1] = x * 269.5f + y * 183.3f + z * 246.1f;
    p[2] = x * 113.5f + y * 271.9f + z * 124.6f;
    p[3] = x * 271.9f + y * 269.5f + z * 311.7f;
    for (int i = 0; i < 4; i++)
        out[i] = fractf(sinf(p[i]) * 43758.5453123f);
}

/* ---- symbols (runes by FabriceNeyret2 / otaviogood) ----------------------- */

static float rune_line(float px, float py, float ax, float ay, float bx, float by)
{
    float h, dx, dy;
    px -= ax; py -= ay; bx -= ax; by -= ay;
    h = clampf((px * bx + py * by) / (bx * bx + by * by), 0.0f, 1.0f);
    dx = px - bx * h; dy = py - by * h;
    return sqrtf(dx * dx + dy * dy);
}

static float rune(matrix_cache *cc, float ux, float uy, float seedx, float seedy, float highlight)
{
    float d = 1e5f;
    uint32_t h = mix32(fbits(seedx) * 2654435761u ^ fbits(seedy) * 40503u);
    rune_entry *e = &cc->rune[h & (RUNE_CACHE_N - 1)];
    if (e->stamp != cc->stamp || e->sx != seedx || e->sy != seedy) {
        const float snaps[4] = { 2, 3, 2, 3 };
        float sx = seedx, sy = seedy;
        e->stamp = cc->stamp; e->sx = seedx; e->sy = seedy;
        for (int i = 0; i < 4; i++) {
            float pos[4];
            hash4_v2(sx, sy, pos);
            sx += 1.0f; sy += 1.0f;

            /* each rune touches the edge of its box on all 4 sides */
            if (i == 0) pos[1] = 0.0f;
            if (i == 1) pos[0] = 0.999f;
            if (i == 2) pos[0] = 0.0f;
            if (i == 3) pos[1] = 0.999f;
            /* snap the random line endpoints to a grid 2x3 */
            for (int k = 0; k < 4; k++)
                pos[k] = (floorf(pos[k] * snaps[k]) + 0.5f) / snaps[k];
            for (int k = 0; k < 4; k++) e->pos[i][k] = pos[k];
            /* filter out single points (start == end); the reference compares
             * pos.xy != pos.zw as a whole-vector inequality (any component) */
            e->ok[i] = (pos[0] != pos[2] || pos[1] != pos[3]) ? 1 : 0;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (e->ok[i]) {
            const float *p = e->pos[i];
            float l = rune_line(ux, uy, p[0], p[1], p[2] + 0.001f, p[3] + 0.001f);
            if (l < d) d = l;
        }
    }
    return smoothstepf(0.1f, 0.0f, d) + highlight * smoothstepf(0.4f, 0.0f, d);
}

static float random_char(matrix_cache *cc, float outerx, float outery, float ux, float uy, float highlight)
{
    float seedx = outerx * 269.5f + outery * 183.3f;
    float seedy = outerx * 113.5f + outery * 271.9f;
    return rune(cc, ux, uy, seedx, seedy, highlight);
}

/* ---- digital rain --------------------------------------------------------- */

/* xy - horizontal, z - vertical.  Returns premultiplied rgb in out[3]. */
static void rain(matrix_cache *cc, const float ro3[3], const float rd3[3], float time,
                 int iterations, float out[3])
{
    float result[4] = { 0, 0, 0, 0 };
    float ro2[2] = { ro3[0], ro3[1] };
    float rd2[2] = { rd3[0], rd3[1] };
    float rl = sqrtf(rd2[0] * rd2[0] + rd2[1] * rd2[1]);
    float t3_to_t2, t2 = 0.0f;
    int cell_side[3], cell_shift[3], next_cell[2];
    int prefer_dx;

    if (rl == 0.0f) rl = 1.0f;
    rd2[0] /= rl; rd2[1] /= rl;

    prefer_dx = fabsf(rd2[0]) > fabsf(rd2[1]);
    t3_to_t2 = prefer_dx ? rd3[0] / rd2[0] : rd3[1] / rd2[1];

    for (int k = 0; k < 3; k++) {
        cell_side[k]  = rd3[k] >= 0.0f ? 1 : 0;          /* step(0., rd3) */
        cell_shift[k] = rd3[k] > 0.0f ? 1 : (rd3[k] < 0.0f ? -1 : 0);
    }
    next_cell[0] = (int)floorf(ro2[0] / XYCELL_SIZE);
    next_cell[1] = (int)floorf(ro2[1] / XYCELL_SIZE);

    for (int i = 0; i < iterations; i++) {
        int cell[2] = { next_cell[0], next_cell[1] };
        float t2s = t2;
        float side[2], t2_side[2], cib[2], gap = (float)BLOCK_GAP / (float)BLOCK_SIZE;
        float t3s, pos_z, z_shift, char_z_shift;
        int zcell;

        /* nearest side of the current xy-cell along the ray */
        side[0] = (float)(next_cell[0] + cell_side[0]) * XYCELL_SIZE;
        side[1] = (float)(next_cell[1] + cell_side[1]) * XYCELL_SIZE;
        t2_side[0] = (side[0] - ro2[0]) / rd2[0];
        t2_side[1] = (side[1] - ro2[1]) / rd2[1];
        if (t2_side[0] < t2_side[1]) {
            t2 = t2_side[0];
            next_cell[0] += cell_shift[0];
        } else {
            t2 = t2_side[1];
            next_cell[1] += cell_shift[1];
        }

        /* gap cells */
        cib[0] = fractf((float)cell[0] / (float)BLOCK_SIZE);
        cib[1] = fractf((float)cell[1] / (float)BLOCK_SIZE);
        if (cib[0] < gap || cib[1] < gap ||
            (cib[0] < gap + 0.1f && cib[1] < gap + 0.1f))
            continue;

        /* back to 3d: ray segment inside this column */
        t3s = t2s / t3_to_t2;
        pos_z = ro3[2] + rd3[2] * t3s;
        {
            uint32_t hh = mix32((uint32_t)cell[0] * 73856093u ^
                                (uint32_t)cell[1] * 19349663u);
            xy_entry *xe = &cc->xy[hh & (XY_CACHE_N - 1)];
            if (xe->stamp != cc->stamp || xe->cx != cell[0] || xe->cy != cell[1]) {
                float zs;
                xe->stamp = cc->stamp; xe->cx = cell[0]; xe->cy = cell[1];
                xe->h = hash2f((float)cell[0], (float)cell[1]);
                zs = xe->h * 11.0f - time * (0.5f + xe->h +
                     xe->h * xe->h + pow16(xe->h) * 3.0f);
                xe->char_z_shift = floorf(zs / STRIP_CHAR_HEIGHT);
                xe->z_shift = xe->char_z_shift * STRIP_CHAR_HEIGHT;
            }
            char_z_shift = xe->char_z_shift;
            z_shift = xe->z_shift;
        }
        zcell = (int)floorf((pos_z - z_shift) / ZCELL_SIZE);

        for (int j = 0; j < 2; j++) {
            float ch2[4], chars_count, target_length, target_z, target[2];
            float s[2], tmin, u, z, v, c, q, char_hash[2], tf, a, att, col[3], a1;
            const float target_rad = STRIP_CHAR_WIDTH / 2.0f;
            uint32_t hh = mix32((uint32_t)cell[0] * 73856093u ^
                                (uint32_t)cell[1] * 19349663u ^
                                (uint32_t)zcell * 83492791u);
            col_entry *ce = &cc->col[hh & (COL_CACHE_N - 1)];

            if (ce->stamp != cc->stamp || ce->cx != cell[0] || ce->cy != cell[1] ||
                ce->zcell != zcell) {
                float ch[4];
                hash4_v3((float)cell[0], (float)cell[1], (float)zcell, ch);
                ce->stamp = cc->stamp; ce->cx = cell[0]; ce->cy = cell[1]; ce->zcell = zcell;
                ce->ch2[0] = fractf(ch[0] * 127.1f);
                ce->ch2[1] = fractf(ch[1] * 311.7f);
                ce->ch2[2] = fractf(ch[2] * 271.9f);
                ce->ch2[3] = fractf(ch[3] * 124.6f);
                ce->chars_count = ch[3] * (STRIP_CHARS_MAX - STRIP_CHARS_MIN) + STRIP_CHARS_MIN;
                ce->target_length = ce->chars_count * STRIP_CHAR_HEIGHT;
                ce->target_z = ((float)zcell * ZCELL_SIZE + z_shift) +
                               ch[2] * (ZCELL_SIZE - ce->target_length);
                ce->target[0] = (float)cell[0] * XYCELL_SIZE + target_rad +
                                ch[0] * (XYCELL_SIZE - target_rad * 2.0f);
                ce->target[1] = (float)cell[1] * XYCELL_SIZE + target_rad +
                                ch[1] * (XYCELL_SIZE - target_rad * 2.0f);
            }
            memcpy(ch2, ce->ch2, sizeof(ch2));
            chars_count = ce->chars_count;
            target_length = ce->target_length;
            target_z = ce->target_z;
            target[0] = ce->target[0]; target[1] = ce->target[1];

            s[0] = target[0] - ro2[0]; s[1] = target[1] - ro2[1];
            tmin = s[0] * rd2[0] + s[1] * rd2[1];
            if (tmin >= t2s && tmin <= t2) {
                u = s[0] * rd2[1] - s[1] * rd2[0];
                if (fabsf(u) < target_rad) {
                    u = (u / target_rad + 1.0f) / 2.0f;
                    z = ro3[2] + rd3[2] * tmin / t3_to_t2;
                    v = (z - target_z) / target_length;
                    if (v >= 0.0f && v < 1.0f) {
                        c = floorf(v * chars_count);
                        q = fractf(v * chars_count);
                        hash2(c + char_z_shift, ch2[0], char_hash);
                        if (char_hash[0] >= 0.1f || c == 0.0f) {
                            tf = floorf(c == 0.0f ? time * 5.0f :
                                 time * (ch2[2] + ch2[3] * ch2[3] * 4.0f *
                                         pow4(char_hash[1])));
                            a = random_char(cc, char_hash[0], tf, u, q,
                                            fmaxf(1.0f, 3.0f - c / 2.0f) * 0.2f);
                            a *= clampf((chars_count - 0.5f - c) / 2.0f, 0.0f, 1.0f);
                            if (a > 0.0f) {
                                att = 1.0f + sq(0.06f * tmin / t3_to_t2);
                                if (c == 0.0f) {
                                    col[0] = 0.67f / att; col[1] = 1.00f / att; col[2] = 0.82f / att;
                                } else {
                                    col[0] = 0.25f / att; col[1] = 0.80f / att; col[2] = 0.40f / att;
                                }
                                a1 = result[3];
                                result[3] = a1 + (1.0f - a1) * a;
                                for (int k = 0; k < 3; k++)
                                    result[k] = (result[k] * a1 + col[k] * (1.0f - a1) * a) / result[3];
                                if (result[3] > 0.98f) {
                                    out[0] = result[0]; out[1] = result[1]; out[2] = result[2];
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            zcell += cell_shift[2];
        }
    }
    out[0] = result[0] * result[3];
    out[1] = result[1] * result[3];
    out[2] = result[2] * result[3];
}

/* ---- camera --------------------------------------------------------------- */

/* column-major mat3 helpers mirroring the GLSL constructors */
static void rotX(float a, float m[9])
{
    float s = sinf(a), c = cosf(a);
    m[0]=1; m[1]=0; m[2]=0;  m[3]=0; m[4]=c; m[5]=-s;  m[6]=0; m[7]=s; m[8]=c;
}
static void rotY(float a, float m[9])
{
    float s = sinf(a), c = cosf(a);
    m[0]=c; m[1]=0; m[2]=-s;  m[3]=0; m[4]=1; m[5]=0;  m[6]=s; m[7]=0; m[8]=c;
}
static void rotZ(float a, float m[9])
{
    float s = sinf(a), c = cosf(a);
    m[0]=c; m[1]=-s; m[2]=0;  m[3]=s; m[4]=c; m[5]=0;  m[6]=0; m[7]=0; m[8]=1;
}
static void m3mul(const float a[9], const float b[9], float r[9])
{
    for (int c = 0; c < 3; c++)
        for (int rr = 0; rr < 3; rr++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += a[k * 3 + rr] * b[c * 3 + k];
            r[c * 3 + rr] = s;
        }
}
static void m3vec(const float m[9], const float v[3], float r[3])
{
    for (int rr = 0; rr < 3; rr++)
        r[rr] = m[rr] * v[0] + m[3 + rr] * v[1] + m[6 + rr] * v[2];
}
static void rot2(float x, float y, float a, float r[2])
{
    float s = sinf(a), c = cosf(a);
    r[0] = c * x + s * y;
    r[1] = -s * x + c * y;
}
static float smoothstep1(float x) { return smoothstepf(0.0f, 1.0f, x); }

/* Per-frame camera state: ro_base and the combined rotation M so that the
 * unnormalized view ray for a pixel is M * (uv.x, 2, uv.y). */
typedef struct {
    float ro[3];
    float M[9];
} matrix_camera;

static void camera_build(const matrix_frame *f, matrix_camera *cam)
{
    float time = f->time;
    const float turn_rad = 0.25f / BLOCKS_BEFORE_TURN;
    const float turn_abs_time = (PI / 2.0f * turn_rad) * 1.5f;
    const float turn_time = turn_abs_time / (1.0f - 2.0f * turn_rad + turn_abs_time);
    float level1_size = (float)BLOCK_SIZE * BLOCKS_BEFORE_TURN * XYCELL_SIZE;
    float level2_size = 4.0f * level1_size;
    float gap_size = (float)BLOCK_GAP * XYCELL_SIZE;
    float ro[3] = { gap_size / 2.0f, gap_size / 2.0f, 0.0f };
    float tq, t8, t1, angle, prev[2], dir[2];
    float turn[2], turn_sign, dirL[2], dirR[2];
    float up_down = 0.0f, rotate_on_turns = 1.0f, roll_on_turns = 1.0f, add_angel = 0.0f;
    const float first_turn_look_angle = 0.4f;
    const float second_turn_drift_angle = 0.5f;
    const float fifth_turn_drift_angle = 0.25f;
    float rx[9], ry[9], rz[9], tmp[9];
    float roll = 0.0f;

    tq = fractf(time / (level2_size * 4.0f) * WALK_SPEED);
    t8 = fractf(tq * 4.0f);
    t1 = fractf(t8 * 8.0f);

    if (tq < 0.25f)      { prev[0]=0; prev[1]=0; dir[0]=0;  dir[1]=1;  }
    else if (tq < 0.5f)  { prev[0]=0; prev[1]=1; dir[0]=1;  dir[1]=0;  }
    else if (tq < 0.75f) { prev[0]=1; prev[1]=1; dir[0]=0;  dir[1]=-1; }
    else                 { prev[0]=1; prev[1]=0; dir[0]=-1; dir[1]=0;  }
    angle = floorf(tq * 4.0f);
    prev[0] *= 4.0f; prev[1] *= 4.0f;

    rot2(dir[0], dir[1], -PI / 2.0f, dirL);
    dirR[0] = -dirL[0]; dirR[1] = -dirL[1];
    turn_sign = 0.0f;

    if (t8 < 0.125f) {
        turn[0]=dirL[0]; turn[1]=dirL[1];
        turn_sign = -1.0f;
        angle -= first_turn_look_angle *
            (fmaxf(0.0f, t1 - (1.0f - turn_time * 2.0f)) / turn_time -
             fmaxf(0.0f, t1 - (1.0f - turn_time)) / turn_time * 2.5f);
        roll_on_turns = 0.0f;
    } else if (t8 < 0.250f) {
        prev[0]+=dir[0]; prev[1]+=dir[1];
        turn[0]=dir[0]; turn[1]=dir[1];
        dir[0]=dirL[0]; dir[1]=dirL[1];
        angle -= 1.0f;
        turn_sign = 1.0f;
        add_angel += first_turn_look_angle * 0.5f +
            (-first_turn_look_angle * 0.5f + 1.0f + second_turn_drift_angle) * t1;
        rotate_on_turns = 0.0f;
        roll_on_turns = 0.0f;
    } else if (t8 < 0.375f) {
        prev[0]+=dir[0]+dirL[0]; prev[1]+=dir[1]+dirL[1];
        turn[0]=dirR[0]; turn[1]=dirR[1];
        turn_sign = 1.0f;
        add_angel += second_turn_drift_angle * sqrtf(1.0f - t1);
    } else if (t8 < 0.5f) {
        prev[0]+=dir[0]*2+dirL[0]; prev[1]+=dir[1]*2+dirL[1];
        turn[0]=dirR[0]; turn[1]=dirR[1];
        dir[0]=dirR[0]; dir[1]=dirR[1];
        angle += 1.0f;
        turn_sign = 0.0f;
        up_down = sinf(t1 * PI) * 0.37f;
    } else if (t8 < 0.625f) {
        prev[0]+=dir[0]*2; prev[1]+=dir[1]*2;
        turn[0]=dir[0]; turn[1]=dir[1];
        dir[0]=dirR[0]; dir[1]=dirR[1];
        angle += 1.0f;
        turn_sign = -1.0f;
        up_down = sinf(-fminf(1.0f, t1 / (1.0f - turn_time)) * PI) * 0.37f;
    } else if (t8 < 0.750f) {
        prev[0]+=dir[0]*2+dirR[0]; prev[1]+=dir[1]*2+dirR[1];
        turn[0]=dirL[0]; turn[1]=dirL[1];
        turn_sign = -1.0f;
        add_angel -= (fifth_turn_drift_angle + 1.0f) * smoothstep1(t1);
        rotate_on_turns = 0.0f;
        roll_on_turns = 0.0f;
    } else if (t8 < 0.875f) {
        prev[0]+=dir[0]*3+dirR[0]; prev[1]+=dir[1]*3+dirR[1];
        turn[0]=dir[0]; turn[1]=dir[1];
        dir[0]=dirL[0]; dir[1]=dirL[1];
        angle -= 1.0f;
        turn_sign = 1.0f;
        add_angel -= fifth_turn_drift_angle -
            smoothstep1(t1) * (fifth_turn_drift_angle * 2.0f + 1.0f);
        rotate_on_turns = 0.0f;
        roll_on_turns = 0.0f;
    } else {
        prev[0]+=dir[0]*3; prev[1]+=dir[1]*3;
        turn[0]=dirR[0]; turn[1]=dirR[1];
        turn_sign = 1.0f;
        angle += fifth_turn_drift_angle *
            (1.5f * fminf(1.0f, (1.0f - t1) / turn_time) -
             0.5f * smoothstep1(1.0f - fminf(1.0f, t1 / (1.0f - turn_time))));
    }

    if (f->mouse[0] > 10.0f || f->mouse[1] > 10.0f) {
        float mx = f->mouse[0] / (float)f->width * 2.0f - 1.0f;
        float my = f->mouse[1] / (float)f->height * 2.0f - 1.0f;
        up_down = -0.7f * my;
        angle += mx;
        rotate_on_turns = 1.0f;
        roll_on_turns = 0.0f;
    } else {
        angle += add_angel;
    }

    /* rd = rotateX(rd, up_down) */
    rotX(up_down, rx);

    {
        float p[2];
        if (turn_sign == 0.0f) {
            /* move forward */
            p[0] = prev[0] + dir[0] * (turn_rad + 1.0f * t1);
            p[1] = prev[1] + dir[1] * (turn_rad + 1.0f * t1);
        } else if (t1 > (1.0f - turn_time)) {
            /* turn */
            float tr = (t1 - (1.0f - turn_time)) / turn_time;
            float c[2], rr[2];
            c[0] = prev[0] + dir[0] * (1.0f - turn_rad) + turn[0] * turn_rad;
            c[1] = prev[1] + dir[1] * (1.0f - turn_rad) + turn[1] * turn_rad;
            rot2(dir[0], dir[1], (tr - 1.0f) * turn_sign * PI / 2.0f, rr);
            p[0] = c[0] + turn_rad * rr[0];
            p[1] = c[1] + turn_rad * rr[1];
            angle += tr * turn_sign * rotate_on_turns;
            roll = sinf(tr * turn_sign * PI) * 0.2f * roll_on_turns;
        } else {
            /* move forward */
            t1 /= (1.0f - turn_time);
            p[0] = prev[0] + dir[0] * (turn_rad + (1.0f - turn_rad * 2.0f) * t1);
            p[1] = prev[1] + dir[1] * (turn_rad + (1.0f - turn_rad * 2.0f) * t1);
        }
        ro[0] += level1_size * p[0];
        ro[1] += level1_size * p[1];
    }

    /* rd = rotateZ(rotateY(rotateX(rd, up_down), roll), angle * PI/2) */
    rotY(roll, ry);
    rotZ(angle * PI / 2.0f, rz);
    m3mul(ry, rx, tmp);
    m3mul(rz, tmp, cam->M);
    cam->ro[0] = ro[0]; cam->ro[1] = ro[1]; cam->ro[2] = ro[2];
}

/* ---- frame ---------------------------------------------------------------- */

void matrix_gpu_params(const matrix_frame *f, float *params)
{
    matrix_camera cam;
    camera_build(f, &cam);
    params[0] = f->time;
    params[1] = (float)f->width;
    params[2] = (float)f->height;
    params[3] = cam.ro[0]; params[4] = cam.ro[1]; params[5] = cam.ro[2];
    /* M is already column-major (m3vec: r[i] = m[i]*v0 + m[3+i]*v1 + m[6+i]*v2),
     * exactly the layout matrix.comp reads from p[6..14] */
    for (int i = 0; i < 9; i++) params[6 + i] = cam.M[i];
}

/* Render rows [y0, y1) of the frame using worker `worker`'s private memo
 * caches.  Bands are independent: every value derives from (fragCoord, time,
 * mouse, iterations) only, so concurrent bands produce exactly the pixels a
 * single-threaded matrix_render would. */
void matrix_render_band(const matrix_frame *f, uint32_t *rgba,
                        int y0, int y1, int worker)
{
    matrix_camera cam;
    matrix_cache *cc;
    int W = f->width, H = f->height;
    float inv_h = 1.0f / (float)H;

    if (W <= 0 || H <= 0) return;
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (y0 >= y1) return;
    cc = &g_cache[worker % MATRIX_MAX_WORKERS];
    cc->stamp++;   /* invalidate this worker's per-frame memo caches */
    /* reference guard: strip must fit inside its cell */
    if (STRIP_CHAR_WIDTH > XYCELL_SIZE ||
        STRIP_CHAR_HEIGHT * STRIP_CHARS_MAX > ZCELL_SIZE) {
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < W; x++)
                rgba[(size_t)y * W + x] = 0xff0000ffu;
        return;
    }

    camera_build(f, &cam);

    for (int y = y0; y < y1; y++) {
        /* GL fragCoord is bottom-left; output row 0 is the top of the image */
        float fy = (float)H - 0.5f - (float)y;
        float uv_y = (fy * 2.0f - (float)H) * inv_h;
        uint32_t *row = rgba + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            float fx = (float)x + 0.5f;
            float uv_x = (fx * 2.0f - (float)W) * inv_h;
            float v[3] = { uv_x, 2.0f, uv_y };
            float rd_un[3], ro[3], rd[3], col[3], len;
            m3vec(cam.M, v, rd_un);
            ro[0] = cam.ro[0] + rd_un[0] * 0.2f;
            ro[1] = cam.ro[1] + rd_un[1] * 0.2f;
            ro[2] = cam.ro[2] + rd_un[2] * 0.2f;
            len = sqrtf(rd_un[0]*rd_un[0] + rd_un[1]*rd_un[1] + rd_un[2]*rd_un[2]);
            if (len == 0.0f) len = 1.0f;
            rd[0] = rd_un[0]/len; rd[1] = rd_un[1]/len; rd[2] = rd_un[2]/len;
            rain(cc, ro, rd, f->time, f->iterations, col);
            {
                int r = (int)(clampf(col[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                int g = (int)(clampf(col[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                int b = (int)(clampf(col[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                row[x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xff000000u;
            }
        }
    }
}

void matrix_render(const matrix_frame *f, uint32_t *rgba)
{
    matrix_render_band(f, rgba, 0, f->height, 0);
}