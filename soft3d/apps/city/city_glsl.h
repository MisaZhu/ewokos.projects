/*
 * city_glsl.h -- GLES2 / GLSL port of the shadertoy effect "BladeRunnerCity".
 *
 * Faithful port of https://www.shadertoy.com/view/NXtXz2 by Patrice TERRIER
 * (www.objreader.com), the raymarched neon "Pixel City"-style street canyon:
 * Manhattan grid of box / cylinder / setback towers, lit windows, blade and
 * facade neon signs with a 5x7 glyph font, roof relief (parapets, machine
 * rooms, water tanks, spires), parked and flying spinner car lights, sweeping
 * projector beams, wet-asphalt reflections, rain streaks, distant Teotihuacan
 * pyramids, cinematic grade and vignette.  The SDF scene, camera timeline,
 * lighting model and every tuning constant are the reference's, reproduced
 * here under its licence (header comment kept verbatim below).
 *
 * Porting notes (constructs re-expressed so the CPU interpreter (HW=virt) and
 * the on-device mesa compiler (HW=raspi5/raspix) agree, without changing the
 * rendered result):
 *   - every #define constant becomes a file-scope const (the CPU lexer skips
 *     '#' lines, so macros would otherwise be undefined identifiers);
 *   - ZERO becomes a mutable int global assigned at the top of mainImage -- a
 *     runtime 0 that stops mesa unrolling the big raymarch loops, exactly the
 *     intent of the reference's min(int(iTime),0) macro;
 *   - txt()'s i/6 (the shader's only integer division) is done in float: the
 *     V3D backend aborts on a non-power-of-two integer divide, and the result
 *     is bit-identical;
 *   - AUDIO and HOLO are forced off and fftAt()/hologram() are stubbed to 0:
 *     this engine exposes no iChannel0 audio FFT or iChannel1 video, so the
 *     projector beams render at their steady no-music level and the avenue
 *     holograms are simply absent;
 *   - iDate.w (wall-clock seconds, used for the rain fall phase) -> iTime;
 *   - iMouse is declared and bound neutral (0,0,0,0): CAM_MODE=-1 drives an
 *     auto-cycling cinematic camera, and a click exits the app;
 *   - a thin main() forwards gl_FragCoord to mainImage() and writes
 *     gl_FragColor, matching the engine's fullscreen-quad convention.
 *
 * GPU route (raspi5 / raspix, PGL_G3D): the vertex shader is a passthrough with
 * no uMVP and this fragment shader reads gl_FragCoord, so pgl_engine recognises
 * the program as a fullscreen effect and dispatches it as a VideoCore VII
 * compute kernel (#version 310 es) through the same transparent path
 * shadertoy/waterdrop/ocean/robot use -- which also supplies the ES3 array
 * constructors (float[64](...), int[119](...), int[108](...)) used by the font
 * and text tables.  CPU route (virt): the engine's interpreter runs the
 * identical GLSL (array constructors and tanh were added for it); it is a very
 * heavy 160-step raymarch with per-pixel sign glyph lookups, so low FPS there
 * is the documented interpreter cost ceiling, not a defect.
 */

#ifndef CITY_GLSL_H
#define CITY_GLSL_H

#ifndef __cplusplus
#error "city_glsl.h requires C++ (raw string literals)"
#endif

/* Passthrough vertex shader: a full-screen clip-space quad.  It intentionally
 * references no uMVP / matrices so the engine flags the program as a fullscreen
 * effect and routes the fragment shader to the GPU compute path. */
static const char* city_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

static const char* city_fs_glsl =
	"#version 100\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"uniform float iTime;\n"
	"uniform vec3  iResolution;\n"
	"uniform vec4  iMouse;\n"
	"int ZERO = 0;\n"
	R"GLSL(
// Author Patrice TERRIER (www.objreader.com)
// Designed to work with SHCplayer and the ObjReader64 3D engine.
//
// CyberCity - Step 3t : Manhattan grid + fog + lit windows + realistic signs
//                       camera height / zoom / crane, building shapes, beacons,
//                       nightlife signs (BAR, GIRLS, HOTEL...),
//                       pxc windows (drawrect), pxc camera modes,
//                       pxc car lights, varied lettering, cinematic grade,
//                       wet asphalt reflections, flying spinners,
//                       audio-synced projector beams, car brand ads,
//                       street reflection code removed, roof relief,
//                       GPU cost reduced, faster compile (3p, 3q),
//                       car and ovoid spinner bodies, parked cars on cross streets,
//                       full viewport (letterbox optional)
// Shadertoy single pass, fully procedural, no textures.
// Uniforms used: iTime, iResolution, iMouse
// Raymarched box buildings on a city grid, camera in a street canyon.
// iMouse.xy = view orientation (yaw / pitch), slow forward drift with iTime.

const int MAXSTEP = 160;
const float MAXDIST = 80.0;

const float CAM_HEIGHT = 3.0;
const float CAM_ZOOM = 1.3;
const int CAM_MODE = -1;
const int CINEMA = 1;
const int LETTERBOX = 0;
const int AUDIO = 0;
const float CARADS = 0.12;
const float BEAMS = 0.16;
const float CAM_INTERVAL = 15.0;
const float RAIN = 0.6;
const float RAININT = 1.0;
const float REFL = 0.20;
const float CURB = 0.006;
const float ROADW = 0.42;
const int PYRAMID = 1;
const int HOLO = 0;
const float CELL = 1.0;
const float WINDOW = 0.07;
const float SIGNM = 0.012;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// 5x7 font: A-Z, space, '.', '-'; 2 floats per char (rows 0-3, rows 4-6)
const float FONT[64] = float[64](
    1033774.0, 17969.0, 509487.0, 15921.0, 34350.0, 14881.0, 575023.0, 15921.0,
    492607.0, 31777.0, 492607.0, 1057.0, 951854.0, 14897.0, 1033777.0, 17969.0,
    135310.0, 14468.0, 270620.0, 6440.0, 103729.0, 17701.0, 33825.0, 31777.0,
    710513.0, 17969.0, 708145.0, 17977.0, 575022.0, 14897.0, 509487.0, 1057.0,
    575022.0, 22837.0, 509487.0, 17701.0, 459838.0, 15888.0, 135327.0, 4228.0,
    575025.0, 14897.0, 575025.0, 4433.0, 706097.0, 10933.0, 141873.0, 17962.0,
    141873.0, 4228.0, 139807.0, 31778.0, 0.0, 0.0, 0.0, 6336.0,
    458752.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

// Company names, verbatim from pxc Texture.cpp prefix[] / name[] / suffix[],
// then nightlife words (BAR, GIRLS, HOTEL...), short ones first
const int NPRE = 23;
const int NNAME = 33;
const int NSUF = 15;
const int NNITE = 30;
const int NSHORT = 19;
const int NCAR = 3;
// TXT6: 6 characters of 5 bits per int (char i = (TXT6[i/6] >> 5*(i%6)) & 31)
const int TXT6[119] = int[119](
    440550600, 604180890, 497586676, 812786092, 481510543, 291318785, 776383507, 270548819, 147654917, 282737203,
    667118355, 445199377, 825505029, 575132250, 483214792, 255077837, 892932154, 651177568, 579004488, 282749554,
    637987246, 883963336, 76134657, 3610024, 12011539, 619221009, 482478095, 14239187, 280410573, 85198989,
    441011476, 147593764, 652261923, 486248520, 71912049, 629776948, 616713816, 80753925, 114568200, 8965716,
    8491403, 286411788, 484624736, 758265411, 254356014, 215236072, 421902803, 369172580, 612730511, 440612103,
    286770323, 121653394, 369237448, 349177075, 655834504, 570932242, 590495076, 617222510, 618925715, 296207810,
    752970829, 95530542, 19556750, 464637306, 71952996, 605040455, 900928922, 383412564, 105488776, 396506586,
    219153483, 477703720, 682699923, 482398689, 604056723, 545634728, 436323796, 457649282, 481314950, 148114862,
    276386385, 145865165, 27042290, 350241802, 570458180, 622867044, 81858752, 615776874, 20253122, 570485096,
    228742411, 282668584, 372067558, 387192884, 387193682, 387193682, 146258546, 570485330, 631738511, 652466631,
    616079578, 624474610, 577273914, 477957536, 713403539, 287978308, 20517410, 993428020, 152684682, 651188171,
    478723040, 608529325, 430281267, 1026079342, 20349626, 1055510554, 451167706, 585742746, 18);
const int OFF[108] = int[108](
    0, 1, 7, 11, 17, 21, 22, 27, 34, 39, 44, 52, 57, 63, 65, 70,
    79, 88, 97, 102, 111, 117, 123, 132, 138, 142, 146, 151, 160, 166, 170, 176,
    184, 190, 193, 200, 207, 215, 222, 231, 241, 246, 255, 260, 268, 276, 281, 288,
    298, 309, 314, 320, 324, 331, 338, 347, 354, 358, 363, 365, 370, 374, 378, 383,
    386, 391, 396, 401, 404, 414, 418, 422, 425, 430, 435, 439, 442, 447, 453, 459,
    464, 470, 476, 481, 486, 493, 498, 505, 512, 519, 528, 540, 550, 560, 577, 588,
    597, 606, 616, 627, 637, 647, 655, 661, 667, 683, 695, 709);

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

int wlen(int w) {
    return OFF[w + 1] - OFF[w];
}

// Sign text: 60% nightlife (BAR, GIRLS, HOTEL...), otherwise a company
// name as in pxc TEXTURE_LOGOS: prefix+name or name+suffix, blades show
// the name alone. Returns (word a, word b or -1, length).
ivec3 signText(float seed, bool blade) {
    int nm = NPRE + int(hash21(vec2(seed, 1.7)) * float(NNAME));
    int pr = int(hash21(vec2(seed, 3.1)) * float(NPRE));
    int sf = NPRE + NNAME + int(hash21(vec2(seed, 4.3)) * float(NSUF));
    int nt = NPRE + NNAME + NSUF + int(hash21(vec2(seed, 7.3)) * float(blade ? NSHORT : NNITE));
    ivec2 w = ivec2(nm, -1);                            // blade: name alone
    if (!blade) w = (hash21(vec2(seed, 2.9)) < 0.5) ? ivec2(pr, nm) : ivec2(nm, sf);
    if (hash21(vec2(seed, 6.1)) < 0.6) w = ivec2(nt, -1);   // nightlife
    int ca = NPRE + NNAME + NSUF + NNITE + (blade ? 0 : NCAR) + int(hash21(vec2(seed, 5.5)) * float(NCAR));
    if (hash21(vec2(seed, 8.2)) < CARADS) w = ivec2(ca, -1);  // car brand ad
    int len = wlen(w.x);
    if (w.y >= 0) len += wlen(w.y);
    return ivec3(w, len);
}

int txt(int i) {
    int q = int(floor(float(i) / 6.0));          // float-safe: V3D aborts on int division
    int r = i - q * 6;
    return (TXT6[q] >> (5 * r)) & 31;
}

int textChar(ivec3 tx, int j) {
    int la = wlen(tx.x);
    if (j < la) return txt(OFF[tx.x] + j);
    if (tx.y < 0) return -1;
    return txt(OFF[tx.y] + j - la);
}

// Main body height of the building in cell id (realistic proportions:
// mid-rise 2-7 x its width, a few towers up to about 15 x)
float bHeight(vec2 id) {
    float r0 = hash21(id);
    float r1 = hash21(id + 31.7);
    float r2 = hash21(id + 57.3);
    float ht = 1.5 + 3.5 * r0 * r0;                     // dense mid-rise
    if (r1 > 0.85) ht += 3.0 + 6.0 * r2;                // towers
    return ht;
}

// Building type (after Pixel City Building.cpp):
// 0 = box, towers get Manhattan setbacks   1 = cylinder (modern)
// 2 = rounded box                           3 = blocky (intersecting boxes)
float bType(vec2 id) {
    float r4 = hash21(id + 5.5);
    if (bHeight(id) > 4.5) {
        if (r4 < 0.40) return 0.0;
        if (r4 < 0.65) return 1.0;
        if (r4 < 0.85) return 2.0;
        return 3.0;
    }
    return (r4 < 0.2) ? 2.0 : 0.0;
}

// Footprint of the building in cell id: center c, half sizes hs
// (cylinder: hs.x = hs.y = radius). Facade flush on the street side.
void bFoot(vec2 id, out vec2 c, out vec2 hs) {
    float mx   = mod(id.x, 3.0);
    float side = (mx == 1.0) ? -1.0 : 1.0;             // street side (x)
    hs = CELL * vec2(0.34 + 0.10 * hash21(id + 31.7), 0.38 + 0.10 * hash21(id + 57.3));
    if (bType(id) == 1.0) hs = vec2(min(hs.x, hs.y) + 0.02);
    c  = (id + 0.5) * CELL + vec2(side * (0.44 * CELL - hs.x), 0.0);
}

// Top of the building: roof height H, top footprint ths, antenna height
void bTop(vec2 id, out vec2 c, out vec2 ths, out float H, out float ant) {
    vec2 hs;
    bFoot(id, c, hs);
    float ht  = bHeight(id);
    float typ = bType(id);
    float r2  = hash21(id + 57.3);
    float r3  = hash21(id + 91.1);
    H = ht;
    ths = hs;
    ant = 0.0;
    if (typ == 0.0) {
        if (ht > 4.5) ths = hs * 0.6;
        else if (r3 > 0.5) { ths = hs * 0.65; H = ht + ht * 0.25 * r2; }
    }
    if (typ == 1.0 && r3 > 0.4) { ths = hs * 0.7; H = ht * (1.0 + 0.15 * r2); }
    if (typ == 3.0) ths = vec2(hs.x * 0.6, hs.y);
    if (ht > 4.5 && r3 > 0.55) ant = 0.6 + 0.6 * hash21(id);
}

// Roof relief (after pxc ConstructRoof / ConstructSpike): parapet rim,
// machine room, AC unit, Manhattan water tank on stilts, spire on some
// towers, cone cap on cylinders. q = point relative to the building center
// (ground level), ths / H = top footprint and roof height.
float roofDist(vec3 q, vec2 ths, float H, float typ, vec2 id) {
    float r1 = hash21(id + 71.1);
    float r2 = hash21(id + 72.2);
    float r3 = hash21(id + 73.3);
    float r4 = hash21(id + 74.4);
    vec3  qr = q - vec3(0.0, H, 0.0);
    float d;
    if (typ == 1.0) {                                   // cylinder: low cone cap
        float hc = ths.x * (0.25 + 0.3 * r1);
        float k  = clamp(qr.y / hc, 0.0, 1.0);
        d = max(length(qr.xz) - ths.x * (1.0 - k), max(-qr.y, qr.y - hc)) * 0.7;
    } else {
        // machine room and AC unit, inside the parapet
        vec2 o1 = (vec2(r1, r2) - 0.5) * max(ths - 0.2, 0.0);
        vec3 s1 = vec3(0.07 + 0.10 * r3, 0.05 + 0.10 * r4, 0.07 + 0.08 * r2);
        d = sdBox(qr - vec3(o1.x, s1.y, o1.y), s1);
        vec2 o2 = (vec2(r3, r1) - 0.5) * max(ths - 0.12, 0.0);
        d = min(d, sdBox(qr - vec3(o2.x, 0.035, o2.y), vec3(0.04, 0.035, 0.05)));
        if (typ != 2.0) {                               // parapet rim
            float rim = sdBox(qr - vec3(0.0, 0.04, 0.0), vec3(ths.x, 0.04, ths.y));
            rim = max(rim, -sdBox(qr - vec3(0.0, 0.06, 0.0), vec3(ths.x - 0.03, 0.06, ths.y - 0.03)));
            d = min(d, rim);
        }
        if (H < 4.5 && r4 > 0.5) {                      // water tank on stilts
            vec3  qt   = qr - vec3(-o1.x * 0.8, 0.0, -o1.y * 0.8);
            float tank = max(length(qt.xz) - 0.07, abs(qt.y - 0.22) - 0.09);
            float kc   = clamp((qt.y - 0.31) / 0.06, 0.0, 1.0);
            float cap  = max(length(qt.xz) - 0.075 * (1.0 - kc), abs(qt.y - 0.34) - 0.03) * 0.7;
            float legs = sdBox(vec3(abs(qt.x) - 0.045, qt.y - 0.065, abs(qt.z) - 0.045), vec3(0.006, 0.065, 0.006));
            d = min(d, min(min(tank, cap), legs));
        }
        if (H > 6.0 && r2 > 0.6) {                      // pointed spire
            float hs = 0.6 + 0.8 * r1;
            float k  = clamp(qr.y / hs, 0.0, 1.0);
            vec2  sz = ths * 0.7 * (1.0 - k);
            d = min(d, max(max(abs(qr.x) - sz.x, abs(qr.z) - sz.y), max(-qr.y, qr.y - hs)) * 0.6);
        }
    }
    return d;
}

// Building inside one grid cell (id), p in world space
float cellDist(vec3 p, vec2 id) {
    // Manhattan grid: street every 3 cells in x, cross street every 6 in z
    if (mod(id.x, 3.0) == 0.0) return 1.0e3;
    if (mod(id.y, 6.0) == 0.0) return 1.0e3;

    vec2 c, hs, ths;
    float H, ant;
    bTop(id, c, ths, H, ant);
    bFoot(id, c, hs);
    float ht  = bHeight(id);
    float typ = bType(id);
    vec3  q   = p - vec3(c.x, 0.0, c.y);
    float d;

    if (typ == 1.0) {                                   // cylinder + crown
        d = max(length(q.xz) - hs.x, abs(q.y - ht * 0.5) - ht * 0.5);
        if (H > ht)
            d = min(d, max(length(q.xz) - ths.x, abs(q.y - H * 0.5) - H * 0.5));
    } else if (typ == 2.0) {                            // rounded corners
        float rad = 0.35 * min(hs.x, hs.y);
        vec2  e   = abs(q.xz) - (hs - rad);
        float d2  = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0) - rad;
        d = max(d2, abs(q.y - ht * 0.5) - ht * 0.5);
    } else if (typ == 3.0) {                            // blocky
        d = sdBox(q - vec3(0.0, ht * 0.275, 0.0), vec3(hs.x, ht * 0.275, hs.y));
        d = min(d, sdBox(q - vec3(0.0, ht * 0.5, 0.0), vec3(ths.x, ht * 0.5, hs.y)));
        d = min(d, sdBox(q - vec3(0.0, ht * 0.4, 0.0), vec3(hs.x, ht * 0.4, hs.y * 0.55)));
    } else if (ht > 4.5) {                              // Manhattan setbacks
        d = sdBox(q - vec3(0.0, ht * 0.30, 0.0), vec3(hs, ht * 0.30).xzy);
        d = min(d, sdBox(q - vec3(0.0, ht * 0.425, 0.0), vec3(hs * 0.8, ht * 0.425).xzy));
        d = min(d, sdBox(q - vec3(0.0, ht * 0.5, 0.0), vec3(ths, ht * 0.5).xzy));
    } else {                                            // plain box + tier
        d = sdBox(q - vec3(0.0, ht * 0.5, 0.0), vec3(hs.x, ht * 0.5, hs.y));
        if (H > ht)
            d = min(d, sdBox(q - vec3(0.0, (ht + H) * 0.5, 0.0), vec3(ths.x, (H - ht) * 0.5, ths.y)));
    }
    if (q.y > H - 0.2) d = min(d, roofDist(q, ths, H, typ, id));   // roof relief
    if (ant > 0.0)                                      // antenna
        d = min(d, sdBox(q - vec3(0.0, H + ant * 0.5, 0.0), vec3(0.015, ant * 0.5, 0.015)));
    return d;
}

// Ray segment [t0, t1] inside the cell being accumulated (set by march)
vec2 gSeg = vec2(0.0, 1000.0);

// Vehicle bodies: analytic ray / box hits found while the lights of a cell
// are evaluated (no extra march work). Nearest hit on the current segment.
float gBodyT = 1.0e9;                                   // distance of the body hit
vec3  gBodyN = vec3(0.0, 1.0, 0.0);                     // its normal
vec3  gBodyC = vec3(0.0);                               // its paint color

void bodyBox(vec3 ro, vec3 rd, vec3 c, vec3 h, vec3 col) {
    vec3  m  = 1.0 / (rd + vec3(1.0e-7));
    vec3  n  = m * (ro - c);
    vec3  k  = abs(m) * h;
    vec3  t1 = -n - k;
    vec3  t2 = -n + k;
    float tN = max(max(t1.x, t1.y), t1.z);
    float tF = min(min(t2.x, t2.y), t2.z);
    if (tN < tF && tN > gSeg.x && tN < gSeg.y && tN < gBodyT) {
        gBodyT = tN;
        gBodyN = -sign(rd) * step(t1.yzx, t1.xyz) * step(t1.zxy, t1.xyz);
        gBodyC = col;
    }
}

// Ovoid body: analytic ray / ellipsoid hit (center c, radii r)
void bodyEgg(vec3 ro, vec3 rd, vec3 c, vec3 r, vec3 col) {
    vec3  o  = (ro - c) / r;
    vec3  d  = rd / r;
    float a  = dot(d, d);
    float b  = dot(o, d);
    float h  = b * b - a * (dot(o, o) - 1.0);
    if (h > 0.0) {
        float tN = (-b - sqrt(h)) / a;
        if (tN > gSeg.x && tN < gSeg.y && tN < gBodyT) {
            gBodyT = tN;
            gBodyN = normalize((ro + rd * tN - c) / (r * r));
            gBodyC = col;
        }
    }
}

float erfa(float x) {                                   // erf approximation
    return tanh(clamp(1.2 * x, -8.0, 8.0));
}

// Halo of one point light, treated as glow in the air: only the part of the
// halo lying on the ray segment gSeg is returned, so summing the segments of
// all cells crossed gives the full halo, without cuts at cell borders and
// stopped where the ray hits something. tc = nearest light distance.
float ptGlow(vec3 b, vec3 ro, vec3 rd, inout float tc) {
    float tb = dot(b - ro, rd);
    float dd = length(ro + rd * tb - b);
    float r  = clamp(max(tb, 0.0) / (CAM_ZOOM * iResolution.y) * 2.0, 0.006, 0.06);
    float R  = r * 2.5;
    float f1 = 0.5 * (erfa((gSeg.y - tb) / r) - erfa((gSeg.x - tb) / r));
    float f2 = 0.5 * (erfa((gSeg.y - tb) / R) - erfa((gSeg.x - tb) / R));
    if (tb > 0.0) tc = min(tc, tb);
    return (exp(-dd * dd / (r * r)) * 1.6 * f1 + exp(-dd / R) * 0.25 * f2) * exp(-max(tb, 0.0) * 0.03);
}

// Car lights after pxc Car.cpp: lights only, headlights (1, 1, 0.8) toward
// the viewer, red taillights away from it. Two-way lanes on every street
// (cells x%3 == 0 run along z, cells z%6 == 0 run along x), right-hand
// traffic, one speed per lane so cars never overlap.
// pool = false: halo along the ray (tc = nearest light)
// pool = true : headlight pools on the road at ground point p
vec3 carLights(vec2 id, vec3 ro, vec3 rd, vec3 p, bool pool, out float tc) {
    tc = 1.0e3;
    vec3 g = vec3(0.0);
    for (int a = ZERO; a < 2; a++) {                       // 0 = along z, 1 = along x
        bool street = (a == 0) ? (mod(id.x, 3.0) == 0.0) : (mod(id.y, 6.0) == 0.0);
        if (!street) continue;
        float along = (a == 0) ? id.y : id.x;
        float lat   = (a == 0) ? id.x : id.y;
        bool  parked = (a == 1);                        // cross streets: parked cars, no
                                                        // moving traffic, so no crashes
        for (int l = ZERO; l < 2; l++) {                // lane = direction
            float dsg = (l == 0) ? 1.0 : -1.0;
            vec3  dv  = (a == 0) ? vec3(0.0, 0.0, dsg) : vec3(dsg, 0.0, 0.0);
            float off = 0.5 + 0.2 * ((a == 0) ? dsg : -dsg);   // right-hand side
            if (parked) off = 0.5 + 0.36 * dsg;         // along the curbs
            vec2  key = vec2(lat + float(a) * 517.0, float(l));
            float v   = 0.6 + 1.0 * hash21(key + 0.3);   // lane speed
            float sh  = parked ? 0.0 : dsg * v * iTime;
            float lo  = along - 1.0;                    // lights and pools reach
            float hi  = along + 2.0;                    // into the neighbour cells
            float k0  = floor((lo - sh) / 1.7);
            for (int kk = ZERO; kk <= 2; kk++) {
                float k  = k0 + float(kk);
                float hk = hash21(key + vec2(k, 7.7));
                float s  = k * 1.7 + hk * 0.7 + sh;      // car position along the lane
                if (hk > 0.65 || s < lo || s >= hi) continue;
                if (parked && (mod(floor(s), 3.0) == 0.0 || fract(s) < 0.12 || fract(s) > 0.88)) continue;  // not in a crossing
                vec3 cp = (a == 0) ? vec3((lat + off) * CELL, 0.05, s * CELL)
                                   : vec3(s * CELL, 0.05, (lat + off) * CELL);
                if (!pool && length(cross(cp - ro, rd)) > 0.5) continue;  // far from the ray
                vec3 sd = vec3(dv.z, 0.0, -dv.x) * 0.035;   // half spacing of a pair
                if (pool) {
                    if (parked) continue;                   // parked: lights off
                    vec2 c1 = (cp + dv * 0.28).xz;
                    vec2 c2 = (cp - dv * 0.12).xz;
                    g += vec3(1.0, 0.95, 0.75) * 0.35 * exp(-dot(p.xz - c1, p.xz - c1) / 0.02);
                    g += vec3(1.0, 0.10, 0.04) * 0.10 * exp(-dot(p.xz - c2, p.xz - c2) / 0.006);
                } else {
                    // body: flat lower box + cabin, dark random paint
                    vec3  bh  = (a == 0) ? vec3(0.045, 0.022, 0.095) : vec3(0.095, 0.022, 0.045);
                    vec3  ch  = (a == 0) ? vec3(0.036, 0.009, 0.048) : vec3(0.048, 0.009, 0.036);
                    vec3  pc  = 0.03 + 0.09 * vec3(hash21(key + k + 0.1), hash21(key + k + 0.2), hash21(key + k + 0.3));
                    bodyBox(ro, rd, cp - vec3(0.0, 0.012, 0.0), bh, pc);
                    bodyBox(ro, rd, cp + vec3(0.0, 0.019, 0.0) - dv * 0.012, ch, pc * 0.6);   // low cabin
                    if (parked) continue;                   // parked: lights off
                    vec3  f  = cp + dv * 0.097;                 // lights on the body faces
                    vec3  b  = cp - dv * 0.097;
                    float fr = smoothstep(-0.2, 0.5, dot(dv, -rd));  // facing the viewer
                    float bk = smoothstep(-0.2, 0.5, dot(dv, rd));
                    float gf = ptGlow(f + sd, ro, rd, tc) + ptGlow(f - sd, ro, rd, tc);
                    float gb = ptGlow(b + sd, ro, rd, tc) + ptGlow(b - sd, ro, rd, tc);
                    g += gf * fr * vec3(1.0, 1.0, 0.8) * 1.4 + gb * bk * vec3(1.0, 0.12, 0.04) * 0.9;
                }
            }
        }
    }
    return g;
}

// Flying spinners: two air lanes above every street (along z), lights only:
// white front lights, red rear lights, cyan underglow, roof strobe
// (red / blue on police spinners, amber otherwise). tc = nearest light.
vec3 spinnerLights(vec2 id, vec3 ro, vec3 rd, out float tc) {
    tc = 1.0e3;
    vec3 g = vec3(0.0);
    if (mod(id.x, 3.0) == 0.0) {
        for (int l = ZERO; l < 2; l++) {
            float dsg = (l == 0) ? 1.0 : -1.0;
            vec3  dv  = vec3(0.0, 0.0, dsg);
            vec2  key = vec2(id.x + 911.0, float(l));
            float alt = 3.2 + 2.4 * float(l) + 0.8 * hash21(key + 1.9);
            float v   = 2.0 + 1.5 * hash21(key + 0.3);
            float sh  = dsg * v * iTime;
            float k0  = floor((id.y - 1.0 - sh) / 5.0);
            for (int kk = ZERO; kk <= 1; kk++) {
                float k  = k0 + float(kk);
                float hk = hash21(key + vec2(k, 3.3));
                float s  = k * 5.0 + hk * 3.0 + sh;
                if (hk > 0.55 || s < id.y - 1.0 || s >= id.y + 2.0) continue;
                vec3  cp = vec3((id.x + 0.5 + 0.12 * dsg) * CELL, alt + 0.08 * sin(iTime * 1.3 + k * 2.1), s * CELL);
                if (length(cross(cp - ro, rd)) > 0.6) continue;  // far from the ray
                vec3  sd = vec3(0.022, 0.0, 0.0);
                float fr = smoothstep(-0.2, 0.5, dot(dv, -rd));
                float bk = smoothstep(-0.2, 0.5, dot(dv, rd));
                // body: ovoid hull + canopy, dark metal
                bodyEgg(ro, rd, cp, vec3(0.07, 0.035, 0.13), vec3(0.05, 0.055, 0.065));
                bodyEgg(ro, rd, cp + vec3(0.0, 0.026, 0.02 * dsg), vec3(0.035, 0.024, 0.06), vec3(0.03, 0.045, 0.06));
                vec3  f  = cp + dv * 0.124;                     // lights on the nose / tail
                vec3  b  = cp - dv * 0.124;
                g += (ptGlow(f + sd, ro, rd, tc) + ptGlow(f - sd, ro, rd, tc)) * (0.25 + fr) * vec3(1.0, 0.98, 0.9) * 1.5;
                g += (ptGlow(b + sd, ro, rd, tc) + ptGlow(b - sd, ro, rd, tc)) * (0.25 + bk) * vec3(1.0, 0.1, 0.05);
                vec3  u  = cp - vec3(0.0, 0.04, 0.0);           // underglow
                float tu = dot(u - ro, rd);
                float du = length(ro + rd * max(tu, 0.0) - u);
                if (tu > 0.0) g += vec3(0.1, 0.8, 1.0) * 0.12 * exp(-du / 0.08) * exp(-tu * 0.03);
                float pol = step(hash21(key + vec2(k, 5.1)), 0.3);  // roof strobe
                float ph  = fract(iTime * 1.6 + hk * 7.0);
                vec3  stc = (pol > 0.5) ? ((ph < 0.5) ? vec3(1.0, 0.05, 0.05) : vec3(0.1, 0.3, 1.0)) : vec3(1.0, 0.6, 0.1);
                float on  = (pol > 0.5) ? 1.0 : step(0.8, ph);
                g += ptGlow(cp + vec3(0.0, 0.05, 0.0), ro, rd, tc) * stc * on * 1.4;
            }
        }
    }
    return g;
}

// Red aviation lights on the tall towers: roof corners + antenna tip.
// Returns the halo along the ray, tc = distance of the closest beacon.
vec3 beaconGlow(vec2 id, vec3 ro, vec3 rd, out float tc) {
    tc = 1.0e3;
    if (mod(id.x, 3.0) == 0.0 || mod(id.y, 6.0) == 0.0) return vec3(0.0);
    if (bHeight(id) <= 4.5) return vec3(0.0);
    vec2 c, ths;
    float H, ant;
    bTop(id, c, ths, H, ant);
    float ph    = hash21(id + 2.2) * 6.2832;
    float flash = 0.12 + pow(max(sin(iTime * 4.2 + ph), 0.0), 6.0);  // ~40 per minute
    vec3  g = vec3(0.0);
    for (int k = ZERO; k < 5; k++) {
        vec3 b;
        if (k == 4) {
            if (ant <= 0.0) break;
            b = vec3(c.x, H + ant, c.y);
        } else {
            vec2 sg = vec2((k == 1 || k == 2) ? 1.0 : -1.0, (k >= 2) ? 1.0 : -1.0);
            vec2 o  = (bType(id) == 1.0) ? sg * (ths.x - 0.05) * 0.7071 : sg * (ths - 0.05);
            b = vec3(c.x + o.x, H + 0.12, c.y + o.y);  // above the parapet
        }
        g += ptGlow(b, ro, rd, tc);
    }
    return g * flash * vec3(1.0, 0.08, 0.05);
}

// Building tint, from the Pixel City light palette (World.cpp light_colors)
vec3 palette(float r) {
    if (r < 0.30) return vec3(1.00, 0.72, 0.42);        // amber
    if (r < 0.50) return vec3(1.00, 0.90, 0.62);        // pale yellow
    if (r < 0.65) return vec3(0.62, 0.92, 1.00);        // cyan
    if (r < 0.80) return vec3(0.70, 0.80, 1.00);        // pale blue
    return vec3(0.90, 0.92, 0.95);                      // white
}

// Window lights on a building face (after Shamus Young's Pixel City):
// lit windows come in horizontal runs, behaviour changes every 8 floors,
// one window shape per building, blank column groups, slow random flicker.
vec3 windowLight(vec3 p, vec3 n, vec2 id, float t, vec3 rd) {
    if (abs(n.y) > 0.5) return vec3(0.0);               // roofs: no windows
    vec2  bc, bt;
    float bH, ba;
    bTop(id, bc, bt, bH, ba);
    if (p.y > bH + 0.001) return vec3(0.0);             // nor on roof structures

    float r0 = hash21(id + 3.1);
    float r1 = hash21(id + 17.9);
    float r2 = hash21(id + 43.7);

    float u  = (abs(n.x) > 0.5) ? p.z : p.x;
    if (bType(id) == 1.0) {                             // cylinder: arc length
        vec2 c, hs;
        bFoot(id, c, hs);
        u = atan(p.z - c.y, p.x - c.x) * hs.x;
    }
    vec2  w  = vec2(u + r0 * 7.0, p.y) / WINDOW;
    vec2  wi = floor(w);
    vec2  wf = fract(w);

    if (wi.y < 2.0) return vec3(0.0);                   // foundation

    // blank column groups (ConstructWall)
    float grp = 2.0 + floor(r1 * 4.0);                  // 2..5
    if (mod(wi.x, grp) > floor(grp * 0.5)) return vec3(0.0);

    // DrawWindows: behaviour changes every 8 floors, then runs of random
    // length (run = RandomVal(run_length)) are lit 1 in lit_density
    float band    = floor(wi.y / 8.0);
    float runMax  = 2.0 + floor(hash21(id + vec2(band, 5.3)) * 9.0);
    float density = 2.0 + floor(hash21(id + vec2(band, 9.7)) * 2.0) + floor(hash21(id + vec2(band, 2.4)) * 2.0);
    float rl      = 1.0 + floor(hash21(id + vec2(wi.y, 4.1)) * runMax);
    float run     = floor((wi.x + floor(hash21(id + vec2(wi.y, 8.8)) * rl)) / rl);
    float lit     = step(hash21(vec2(run, wi.y) + id * 1.37), 1.0 / density);

    // slow random flicker: a few windows switch now and then
    float wh   = hash21(wi + id * 7.7);
    float slot = floor(iTime / (6.0 + 10.0 * wh) + wh * 13.0);
    if (hash21(vec2(slot, wh * 51.0)) < 0.05) lit = 1.0 - lit;

    // window(): the 9 pxc shapes, one per building (g: 0..1, y down)
    vec2  ws  = wi + id * 11.3;                         // per window seed
    vec2  g   = vec2(wf.x, 1.0 - wf.y);
    float e   = 0.125;
    float mg  = 0.333;
    float sty = floor(r2 * 9.0);
    vec4  A   = vec4(e, e, 1.0 - e, 1.0 - e);           // x0, y0, x1, y1
    vec4  B   = vec4(2.0);                              // second rect: none
    float mult = 1.0;
    if (sty == 1.0) A = vec4(mg, e, 1.0 - mg, 1.0 - e);                     // vertical
    if (sty == 2.0) { A = vec4(e, e, 0.5 - e, 1.0 - mg); B = vec4(0.5 + e, e, 1.0 - e, 1.0 - mg); } // pair
    if (sty == 3.0 && g.y < e + hash21(ws + 0.9) * (1.0 - 2.0 * e)) mult = 0.3;   // blinds
    if (sty == 4.0) {                                                         // stripes
        if (abs(g.x - mg) < e * 0.5) mult = 0.7;
        if (abs(g.x - 1.0 + mg) < e * 0.5) mult = 0.3;
    }
    if (sty == 5.0) A.w = 1.0 - mg;                                           // wide
    if (sty == 6.0) {                                                         // 4-pane
        A.x = 2.0 * e;
        if (abs(g.y - 0.5) < e * 0.5 || abs(g.x - 0.5) < e * 0.5) mult = 0.2;
    }
    if (sty == 7.0) A = vec4(0.5 - e, e, 0.5 + e, 1.0 - mg);                 // narrow
    if (sty == 8.0) A = vec4(e, mg, 1.0 - e, 1.0 - mg - e);                  // horizontal
    float inA   = step(A.x, g.x) * step(g.x, A.z) * step(A.y, g.y) * step(g.y, A.w);
    float inB   = step(B.x, g.x) * step(g.x, B.z) * step(B.y, g.y) * step(g.y, B.w);
    float shape = max(inA, inB) * mult;
    float area  = (A.z - A.x) * (A.w - A.y);
    if (B.x < 1.5) area += (B.z - B.x) * (B.w - B.y);

    // drawrect(): each texel column gets a dark vertical gradient (curtains,
    // furniture), sometimes shorter; lit windows get speckle noise
    float tc    = floor(g.x * 8.0);
    float aT    = hash21(ws + vec2(tc, 0.37));
    float aB    = hash21(ws + vec2(tc, 0.71));
    float hgt   = 1.0;
    float rh    = hash21(ws + vec2(tc, 0.53));
    if (hash21(ws + vec2(tc, 0.13)) < 0.167) hgt = 0.5 + 0.5 * rh * rh * rh;
    float k     = clamp((g.y - (1.0 - hgt)) / hgt, 0.0, 1.0);
    float shade = step(1.0 - hgt, g.y) * mix(aT, aB, k);
    float noise = 0.7 + 0.6 * hash21(ws + floor(g * 8.0) * 1.7);

    // pixel footprint in windows: filter detail, shape and pattern with distance
    float pix = t / (CAM_ZOOM * iResolution.y) / max(abs(dot(rd, n)), 0.15) / WINDOW;
    float df  = 1.0 - smoothstep(0.08, 0.3, pix);
    float inter = mix(0.5, 1.0 - shade, df);
    noise = mix(1.0, noise, df);
    shape = mix(area * 0.85, shape, 1.0 - smoothstep(0.25, 0.8, pix));
    lit   = mix(1.0 / density, lit, 1.0 - smoothstep(0.6, 2.5, pix));

    // colors: lit = 0.5..1.0 + RANDOM_COLOR_SHIFT, unlit = 0..40/256 grey,
    // both tinted by the building light color
    vec3 shift  = 0.18 * vec3(hash21(ws + 1.1), hash21(ws + 2.2), hash21(ws + 3.3));
    vec3 litCol = (0.5 + 0.5 * hash21(ws + 3.3) + shift) * noise;
    vec3 offCol = vec3(0.156 * hash21(ws + 5.1)) * 0.6;
    return shape * mix(offCol, litCol, lit) * palette(r0) * inter * 1.6;
}

// Neon colors: cyan, red, magenta, warm yellow, green
vec3 neonColor(float r) {
    if (r < 0.25) return vec3(0.10, 0.90, 1.00);
    if (r < 0.45) return vec3(1.00, 0.12, 0.18);
    if (r < 0.65) return vec3(1.00, 0.15, 0.75);
    if (r < 0.85) return vec3(1.00, 0.72, 0.20);
    return vec3(0.30, 1.00, 0.45);
}

// Sign k (0..2) on the street facade of building cell bid (side = street
// side). Blades stick out into the street, panels lie flat on the facade.
// Returns false when there is no sign; otherwise box center c, half size b,
// si = (type, half width, half height, seed) with type 0/1 blade neon /
// lightbox, 2/3 panel neon / lightbox, col = color (with slow fade),
// fl = buzz flicker of the face only. Single source for march and shading.
bool signBox(vec2 bid, float side, int k, out vec3 c, out vec3 b, out vec4 si, out vec3 col, out float fl) {
    c   = vec3(1.0e5);
    b   = vec3(0.0);
    si  = vec4(0.0);
    col = vec3(0.0);
    fl  = 1.0;
    bool ok = false;
    if (mod(bid.y, 6.0) != 0.0 && bType(bid) != 1.0) {   // not on cross streets / cylinders
        float lim = bHeight(bid);
        if (lim > 4.5 && bType(bid) != 2.0) lim *= 0.55;  // below the first setback
        float fx = (bid.x + 0.5 + side * 0.44) * CELL;    // facade plane
        float fk = float(k);
        vec2  h  = bid + fk * 17.31;
        float r0 = hash21(h + 71.3);
        float r1 = hash21(h + 12.9);
        float r2 = hash21(h + 33.1);
        if (r1 <= 0.8) {
            float yc   = 0.4 + fk * 0.9 + r2 * 0.5;
            float zc   = (bid.y + 0.2 + 0.6 * r2) * CELL;
            float box  = step(0.55, hash21(h + 5.7));     // 1 = lightbox
            float seed = r0 * 97.0 + r1 * 13.0;
            if (k == 0 || r2 < 0.5) {                     // vertical blade
                // size from hashes only (no text lookup in the march);
                // the text is fitted and centred inside in signFace()
                float ub = 0.012 + 0.010 * hash21(vec2(seed, 9.4));  // max font pixel size
                float hw = 3.0 * ub + SIGNM;
                float hs = 0.22 + 0.30 * hash21(vec2(seed, 3.7)) + SIGNM;
                if (yc + 2.0 * hs <= lim - 0.1) {         // below the roof
                    c  = vec3(fx + side * (hw + 0.015), yc + hs, zc);
                    b  = vec3(hw, hs, 0.012);
                    si = vec4(box, hw, hs, seed);
                    ok = true;
                }
            } else {                                      // flat panel
                float hw = 0.15 + 0.28 * hash21(vec2(seed, 3.7));
                float hh = 4.5 * (0.010 + 0.006 * r2) + SIGNM;
                if (yc + hh <= lim - 0.1) {
                    c  = vec3(fx + side * 0.012, yc, (bid.y + 0.5) * CELL);
                    b  = vec3(0.012, hh, hw);
                    si = vec4(2.0 + box, hw, hh, seed);
                    ok = true;
                }
            }
            if (ok) {
                col = neonColor(r0);
                // some signs buzz (face only), some fade slowly (face and light)
                if (r2 > 0.85) fl = step(0.25, hash21(vec2(floor(iTime * 10.0), r0 * 91.0)));
                else if (r2 < 0.12) col *= 0.3 + 0.7 * smoothstep(-0.3, 0.3, sin(iTime * 0.8 + r1 * 20.0));
            }
        }
    }
    if (!ok) c = vec3(1.0e5);
    return ok;
}

// Nearest sign of building cell bid: distance, col, lq = local position,
// si, fl (see signBox)
float signDist(vec3 p, vec2 bid, float side, out vec3 col, out vec3 lq, out vec4 si, out float fl) {
    float d = 1.0e3;
    col = vec3(0.0);
    fl  = 1.0;
    lq  = vec3(0.0);
    si  = vec4(0.0);
    float fx = (bid.x + 0.5 + side * 0.44) * CELL;      // facade plane
    // early exit: signs stay within 0.22 of the facade and below 4.0
    float far = max(abs(p.x - fx) - 0.22, p.y - 4.0);
    if (far > 0.25) return far;
    for (int k = ZERO; k < 3; k++) {
        vec3  c, b, kc;
        vec4  s4;
        float f4;
        if (signBox(bid, side, k, c, b, s4, kc, f4)) {
            float ds = sdBox(p - c, b);
            if (ds < d) {
                d   = ds;
                lq  = p - c;
                si  = s4;
                col = kc;
                fl  = f4;
            }
        }
    }
    return d;
}

// Signs reachable from grid cell id (street cells see both facades);
// a loop over the facades, so signDist() is compiled once here
float signsInCell(vec3 p, vec2 id, out vec3 col, out vec3 lq, out vec4 si, out float fl) {
    float mx = mod(id.x, 3.0);
    float d  = 1.0e3;
    col = vec3(0.0);
    lq  = vec3(0.0);
    si  = vec4(0.0);
    fl  = 1.0;
    int nb = (mx == 0.0) ? 2 : 1;
    for (int b = ZERO; b < nb; b++) {
        vec2  bid  = id;
        float side = (mx == 1.0) ? -1.0 : 1.0;
        if (mx == 0.0) {                                // street: both facades
            bid  = id + vec2((b == 0) ? -1.0 : 1.0, 0.0);
            side = (b == 0) ? 1.0 : -1.0;
        }
        vec3  c2, l2;
        vec4  s2;
        float f2;
        float d2 = signDist(p, bid, side, c2, l2, s2, f2);
        if (d2 < d) {
            d   = d2;
            col = c2;
            lq  = l2;
            si  = s2;
            fl  = f2;
        }
    }
    return d;
}

float sdBox2(vec2 p, vec2 b) {
    vec2 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float fontBit(int c, ivec2 q) {
    if (c < 0 || q.x < 0 || q.x > 4 || q.y < 0 || q.y > 6) return 0.0;
    float v = (q.y < 4) ? FONT[c * 2] : FONT[c * 2 + 1];
    int   k = (q.y < 4) ? (q.y * 5 + q.x) : ((q.y - 4) * 5 + q.x);
    return mod(floor(v / exp2(float(k))), 2.0);
}

float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}

// Emblems of the fictional car brands (font codes 29..31), drawn in the
// 5x7 character cell (P in font pixels, y down), centre (2.5, 3.5)
float emblemDist(int e, vec2 P) {
    vec2  q = P - vec2(2.5, 3.5);
    float d;
    if (e == 0) {                                       // KESTREL: double chevron
        vec2 a = vec2(abs(q.x), q.y);
        d = min(sdSeg(a, vec2(0.0, -2.4), vec2(2.4, 0.0)),
                sdSeg(a, vec2(0.0, -0.2), vec2(2.4, 2.2)));
    } else if (e == 1) {                                // VANTA: split hexagon
        vec2  h = abs(q);
        float hx = max(h.x * 0.866 + h.y * 0.5, h.y) - 2.6;
        d = abs(hx);
        if (abs(q.x) < 0.45) d = max(d, 0.45 - abs(q.x));  // vertical split
        d = min(d, sdSeg(q, vec2(-1.2, 0.0), vec2(1.2, 0.0)));
    } else {                                            // ORION: three orbits
        float r = length(q);
        float a = atan(q.y, q.x);
        d = min(abs(r - 0.9), min(abs(r - 1.9), abs(r - 2.9)));
        if (abs(a) < 0.6) d = max(d, 0.6 - abs(a));     // open to the right
        d = min(d, length(q - vec2(2.9, 0.0)) - 0.2);   // star on the orbit
    }
    return d;
}

// Distance (in font pixels) to character c drawn as connected tubes
float glyphDist(int c, vec2 P) {
    ivec2 i = ivec2(floor(P));
    float d = 1.0e3;
    if (c >= 29) d = emblemDist(c - 29, P);             // car brand emblem
    for (int dy = -1 + ZERO; dy <= 1; dy++) {
        for (int dx = -1 + ZERO; dx <= 1; dx++) {
            ivec2 q = i + ivec2(dx, dy);
            if (fontBit(c, q) < 0.5) continue;
            vec2 a  = vec2(q) + 0.5;
            bool rt = fontBit(c, q + ivec2(1, 0)) > 0.5;
            bool dn = fontBit(c, q + ivec2(0, 1)) > 0.5;
            d = min(d, length(P - a));
            if (rt) d = min(d, sdSeg(P, a, a + vec2(1.0, 0.0)));
            if (dn) d = min(d, sdSeg(P, a, a + vec2(0.0, 1.0)));
            if (!rt && !dn && fontBit(c, q + ivec2(1, 1)) > 0.5)
                d = min(d, sdSeg(P, a, a + vec2(1.0, 1.0)));
            if (!dn && fontBit(c, q + ivec2(-1, 0)) < 0.5 && fontBit(c, q + ivec2(-1, 1)) > 0.5)
                d = min(d, sdSeg(P, a, a + vec2(-1.0, 1.0)));
        }
    }
    return d;
}

// Lit face of a sign: f = face coords (x right, y up), pix = pixel size
vec3 signFace(vec2 f, vec4 si, vec3 sc, float pix) {
    float m    = SIGNM;
    bool  vert = si.x < 1.5;                            // text runs vertically
    ivec3 tx   = signText(si.w, vert);
    float n    = float(tx.z);
    vec2  a    = vec2(si.y, si.z) - m;                  // inner half size
    float u;                                            // font pixel size: text fitted
    vec2  P;                                            // and centred in the sign
    int   j;
    if (vert) {                                         // stacked letters
        u = min(a.x / 3.0, 2.0 * a.y / (9.0 * n));
        float s = (4.5 * u * n - f.y) / u;
        j = int(floor(s / 9.0));
        P = vec2((f.x + 3.0 * u) / u - 0.5, s - float(j) * 9.0 - 1.0);
    } else {                                            // one text line
        u = min(2.0 * a.y / 9.0, 2.0 * a.x / (6.0 * n));
        float s = (f.x + 3.0 * u * n) / u;
        j = int(floor(s / 6.0));
        P = vec2(s - float(j) * 6.0 - 0.5, (4.5 * u - f.y) / u - 1.0);
    }
    float dg = 1.0e3;
    if (j >= 0 && j < tx.z) dg = glyphDist(textChar(tx, j), P) * u;
    float fade = smoothstep(u * 0.6, u * 1.8, pix);     // too small: average
    float lum  = max(sc.r, max(sc.g, sc.b));            // 0 when flicker is off

    if (mod(si.x, 2.0) > 0.5) {                         // backlit lightbox
        vec3  face = mix(vec3(1.0, 0.97, 0.92), sc, 0.3) * (0.8 + 0.2 * cos(f.y / si.z * 1.4));
        float bw   = mix(0.28, 0.62, hash21(vec2(si.w, 8.8)));     // thin .. bold
        float ink  = 1.0 - smoothstep(u * bw, u * bw + pix, dg);
        ink = mix(ink, 0.3, fade);
        float edge = step(0.0, sdBox2(f, vec2(si.y, si.z) - 0.3 * m));
        return mix(mix(face, sc * 0.3, ink), vec3(0.02), edge) * lum;
    }
    // neon tubes on a dark backing: letters + outline
    float w    = max(u * mix(0.10, 0.38, hash21(vec2(si.w, 8.8))), pix * 0.75);  // thin .. bold tubes
    float d    = min(dg, abs(sdBox2(f, vec2(si.y, si.z) - 0.4 * m)));
    float core = 1.0 - smoothstep(w * 0.4, w, d);
    float halo = exp(-d / (w * 3.0));
    vec3  e    = sc * 0.02 + sc * halo * 0.45 + mix(sc, vec3(1.0), 0.6) * core * 1.3;
    return mix(e, sc * 0.35, fade);
}

// Sidewalks: everything outside the roadways is raised by CURB. Roadways are
// the avenues (x % 3 == 0) and the cross streets (z % 6 == 0), ROADW from
// their centre line, so the sidewalks follow every facade, turn at the
// corners and stop at the crossings.
float curbDist(vec3 p) {
    float cx = (floor((p.x / CELL - 0.5) / 3.0 + 0.5) * 3.0 + 0.5) * CELL;   // nearest avenue centre
    float cz = (floor((p.z / CELL - 0.5) / 6.0 + 0.5) * 6.0 + 0.5) * CELL;   // nearest cross street centre
    return max(ROADW * CELL - min(abs(p.x - cx), abs(p.z - cz)), p.y - CURB);
}

float map(vec3 p) {                                     // buildings and sidewalks (normals);
    return min(cellDist(p, floor(p.xz / CELL)), curbDist(p));   // sign normals come from their box
}

vec3 calcNormal(vec3 p) {                              // 4 taps (tetrahedron), in a loop
    vec3 n = vec3(0.0);                                 // so map() is compiled once
    for (int i = ZERO; i < 4; i++) {
        vec3 e = 0.5773 * (2.0 * vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0);
        n += e * map(p + e * 0.002);
    }
    return (dot(n, n) > 1.0e-12) ? normalize(n) : vec3(0.0, 1.0, 0.0);   // flat field: no NaN
}

// Point lights of one cell: beacons, cars (and their neighbours), spinners
vec3 cellLights(vec2 id, vec3 ro, vec3 rd, vec3 p) {
    float tc;
    vec3 g = beaconGlow(id, ro, rd, tc);
    g += carLights(id, ro, rd, p, false, tc);
    g += spinnerLights(id, ro, rd, tc);
    return g;
}

// March with steps clamped to the current cell boundary,
// accumulating the neon glow (halo) along the ray
float march(vec3 ro, vec3 rd, out vec3 glow) {
    vec2 inv = 1.0 / max(abs(rd.xz), vec2(1.0e-4));
    vec2 sg  = step(0.0, rd.xz);
    float t = 0.0;
    glow = vec3(0.0);
    vec2  lastId = vec2(1.0e5);                         // cell the ray is in
    float t0 = 0.0;                                     // where it entered it
    gBodyT = 1.0e9;
    for (int i = ZERO; i < MAXSTEP; i++) {
        vec3 p  = ro + rd * t;
        vec2 id = floor(p.xz / CELL);
        bool ground = p.y < 0.0;
        vec3 sc = vec3(0.0);
        vec3 lq;
        vec4 si;
        float fl;
        float ds = 1.0e3;
        float d  = 1.0e3;
        if (!ground) {
            ds = signsInCell(p, id, sc, lq, si, fl);
            d  = min(min(cellDist(p, id), ds), curbDist(p));   // buildings, signs, sidewalks
        }
        bool hit = ground || d < 0.001 * t + 0.0005;
        // point lights: add their halo on the segment inside the cell we leave
        if (lastId.x < 1.0e4 && (hit || id != lastId || t > MAXDIST)) {
            gSeg = vec2(t0, t);
            if (t0 < 60.0) glow += cellLights(lastId, ro, rd, p) * smoothstep(60.0, 45.0, t0);  // far lights: lost in fog
            if (gBodyT < t) return gBodyT;              // a car / spinner body is in front
        }
        if (id != lastId) {
            lastId = id;
            t0 = t;
        }
        if (hit) return t;
        if (t > MAXDIST) break;
        vec2 cb = (id + sg) * CELL - p.xz;
        cb *= sign(rd.xz) * inv;
        float tb = min(cb.x, cb.y) + 0.002;
        float st = min(d, tb);
        if (ds < 0.3) st = min(st, 0.04 + 0.5 * ds);   // even sampling near signs only
        if (rd.y < 0.0) st = min(st, p.y / -rd.y + 0.0005);   // land exactly on the road
        glow += sc * exp(-ds * 16.0) * st * exp(-t * 0.04);   // tight halo only: stays in its cell
        t += st;
    }
    return -1.0;
}
// Audio: FFT row of iChannel0 (Shadertoy / SHC layout), 0 when no music
float fftAt(float f) {
    return 0.0;                                  // AUDIO off: no iChannel0 on this engine
}

// Projector beams: some buildings carry a searchlight on their street facade,
// sweeping slowly over the street and up into the spinner lanes. Each beam
// follows its own audio band; without music they stay at a steady level.
// Volumetric look from the closest approach of the view ray to the beam axis,
// hidden behind whatever the ray hits first (tHit).
vec3 beamLights(vec3 ro, vec3 rd, float tHit) {
    vec3  g    = vec3(0.0);
    float live = fftAt(0.03) + fftAt(0.12) + fftAt(0.3);    // music playing?
    vec2  c0   = floor(ro.xz / CELL);
    for (int j = -6 + ZERO; j <= 6; j++) {
        for (int i = -5 + ZERO; i <= 5; i++) {
            vec2  id = c0 + vec2(float(i), float(j));
            float mx = mod(id.x, 3.0);
            if (mx == 0.0 || mod(id.y, 6.0) == 0.0) continue;  // street
            if (hash21(id + 61.7) > BEAMS) continue;           // no projector
            float ht = bHeight(id);
            if (ht < 2.5 || bType(id) == 1.0) continue;
            float side = (mx == 1.0) ? -1.0 : 1.0;             // street side
            float h1 = hash21(id + 12.3);
            float h2 = hash21(id + 45.6);
            float ph = h1 * 6.2832;
            vec3  P  = vec3((id.x + 0.5 + side * 0.46) * CELL, mix(2.0, ht - 0.3, h2), (id.y + 0.5) * CELL);
            vec3  d  = normalize(vec3(side * 0.55, 0.65 + 0.25 * sin(iTime * 0.11 + ph), 0.45 * sin(iTime * 0.15 + ph * 1.7)));
            // where the beam meets the facade across the street (unless it
            // clears that building or crosses a cross street)
            float sW = 1.1 * CELL / max(abs(d.x), 0.05);
            vec3  E  = P + d * sW;
            vec2  cw = vec2(id.x + 2.0 * side, floor(E.z / CELL));
            float L  = 8.0;
            float lvl = (live > 0.001) ? mix(0.2, 1.6, fftAt(0.02 + 0.35 * h1)) : 0.8;
            float fadeC = smoothstep(6.5, 4.0, max(abs(float(i)), abs(float(j)) * 0.85));
            if (mod(cw.y, 6.0) != 0.0 && E.y < bHeight(cw)) {
                L = sW;
                // light spot where the beam lands on the wall
                vec3  dh = ro + rd * tHit - E;
                float wE = 0.02 + L * 0.07;
                g += vec3(0.75, 0.88, 1.0) * exp(-dot(dh, dh) / (wE * wE)) * 0.9 * lvl * fadeC * exp(-tHit * 0.03);
            }
            // closest points of the view ray and the beam axis
            vec3  w  = ro - P;
            float bb = dot(rd, d);
            float den = max(1.0 - bb * bb, 1.0e-4);
            float sb = clamp((dot(d, w) - bb * dot(rd, w)) / den, 0.0, L);
            float tr = dot(P + d * sb - ro, rd);
            if (tr < 0.0 || tr > tHit) continue;               // behind camera / wall
            float dist = length(ro + rd * tr - P - d * sb);
            float wd   = 0.02 + sb * 0.05;                     // cone width
            float q    = dist * dist / (wd * wd);
            float vol  = (exp(-q) + 2.0 * exp(-q * 6.0)) * wd * 1.77 / sqrt(den);   // soft cone + core
            float nearF = smoothstep(0.4, 3.0, tr);         // no wash-out close to the lens
            g += vec3(0.70, 0.85, 1.0) * vol * exp(-sb * 0.15) * exp(-tr * 0.025) * lvl * fadeC * nearF * 1.3;
            // the lamp itself, bright when it faces the viewer
            float dl = length(cross(P - ro, rd));
            g += vec3(0.85, 0.92, 1.0) * exp(-dl * dl / 0.0012) * smoothstep(-0.2, 0.6, dot(d, -rd)) * lvl * fadeC * 2.0;
        }
    }
    return g;
}

const vec3 SKYTOP = vec3(0.008, 0.030, 0.048);
const vec3 SKYHOR = vec3(0.100, 0.360, 0.400);
const vec3 FOGCOL = vec3(0.080, 0.300, 0.340);
const vec3 DARKCOL = vec3(0.006, 0.022, 0.032);

// Sky with glow toward the end of the avenue
vec3 skyCol(vec3 rd) {
    float glow = pow(max(dot(rd, normalize(vec3(0.0, 0.15, 1.0))), 0.0), 6.0);
    return mix(SKYHOR, SKYTOP, smoothstep(0.0, 0.7, rd.y)) + vec3(0.05, 0.20, 0.22) * glow;
}

// Distance + height fog, thinner when the camera is high
vec3 applyFog(vec3 c, float t, float py, vec3 rd, float camH) {
    float fd = 1.0 - exp(-t * 0.05 * mix(1.0, 0.25, smoothstep(5.0, 18.0, camH)));
    float fh = exp(-max(py, 0.0) * 0.08);
    vec3 fogC = mix(mix(FOGCOL, skyCol(rd), 0.3), FOGCOL * 0.35, smoothstep(0.0, -0.6, rd.y)); // darker looking down
    return mix(c, fogC, clamp(fd * (0.6 + 0.4 * fh), 0.0, 1.0));
}

// Wet road: very light reflection, computed analytically (no second march).
// The mirrored ray can only meet the facade plane across the street (x on the
// avenues, z on the cross streets): windows and signs are looked up there,
// the sky above the roofs or through street gaps.
vec3 roadReflect(vec3 p, vec3 rd) {
    vec3  r  = vec3(rd.x, -rd.y, rd.z);
    r.xz += (vec2(hash21(floor(p.xz * 60.0)), hash21(floor(p.xz * 60.0) + 9.1)) - 0.5) * 0.03;  // ripples
    vec2  id = floor(p.xz / CELL);
    bool  av = mod(id.x, 3.0) == 0.0;                   // avenue (along z)
    bool  cr = mod(id.y, 6.0) == 0.0;                   // cross street (along x)
    if (av && cr) av = abs(r.x) > abs(r.z);             // crossing: dominant axis
    float s;
    vec2  bid;
    vec3  n;
    if (av) {
        if (abs(r.x) < 0.02) return skyCol(r);
        float xf = (r.x > 0.0) ? (id.x + 1.0) * CELL + 0.06 : id.x * CELL - 0.06;
        s   = (xf - p.x) / r.x;
        bid = vec2(id.x + sign(r.x), floor((p.z + r.z * s) / CELL));
        n   = vec3(-sign(r.x), 0.0, 0.0);
    } else {
        if (abs(r.z) < 0.02) return skyCol(r);
        float zf = (r.z > 0.0) ? (id.y + 1.0) * CELL + 0.10 : id.y * CELL - 0.10;
        s   = (zf - p.z) / r.z;
        bid = vec2(floor((p.x + r.x * s) / CELL), id.y + sign(r.z));
        n   = vec3(0.0, 0.0, -sign(r.z));
    }
    vec3 q = p + r * s;
    if (s > 30.0 || mod(bid.x, 3.0) == 0.0 || mod(bid.y, 6.0) == 0.0 || q.y > bHeight(bid))
        return skyCol(r);                               // above the roof or a street gap
    float tone = 0.5 + hash21(bid + 7.3);
    float lum  = (0.06 + 0.40 * abs(n.x) * tone) * (0.6 + 0.4 * smoothstep(0.0, 6.0, q.y));
    vec3  c    = DARKCOL + FOGCOL * lum;
    vec2  wv   = vec2((abs(n.x) > 0.5) ? q.z : q.x, q.y) / WINDOW;  // cheap window grid
    vec2  wf   = abs(fract(wv) - 0.5);
    if (q.y > 2.0 * WINDOW && max(wf.x, wf.y) < 0.33 && hash21(floor(wv) + bid * 7.0) > 0.62)
        c += vec3(0.30, 0.24, 0.15);
    if (av) {                                           // signs on that facade
        vec3  c2, l2;
        vec4  s2;
        float f2;
        float d2 = signDist(q, bid, (mod(bid.x, 3.0) == 1.0) ? -1.0 : 1.0, c2, l2, s2, f2);
        c += c2 * 1.2 * exp(-max(d2, 0.0) * 30.0);
    }
    return mix(c, FOGCOL, 1.0 - exp(-s * 0.08));
}

// Shade a hit at distance t along the ray (walls, windows, signs, road)
vec3 shadeHit(vec3 ro, vec3 rd, float t, float camH) {
    vec3 fogCol  = FOGCOL;
    vec3 darkCol = DARKCOL;
    vec3 p = ro + rd * t;

    // sign hit? its normal comes straight from the sign box
    vec3 sc, lq;
    vec4 si;
    float fl;
    float ds = signsInCell(p, floor((p.xz - rd.xz * 0.002) / CELL), sc, lq, si, fl);
    bool onSign = ds < max(0.004, 0.0015 * t + 0.001);  // same tolerance as the march hit
    vec3 n;
    if (p.y < 0.001) {
        n = vec3(0.0, 1.0, 0.0);
    } else if (onSign) {
        vec3 bs = (si.x < 1.5) ? vec3(si.y, si.z, 0.012) : vec3(0.012, si.z, si.y);
        vec3 r  = abs(lq) - bs;                         // face = axis the point is most outside
        n = (r.x > r.y && r.x > r.z) ? vec3(sign(lq.x), 0.0, 0.0)
          : ((r.y > r.z) ? vec3(0.0, sign(lq.y), 0.0) : vec3(0.0, 0.0, sign(lq.z)));
    } else {
        n = calcNormal(p);
    }

    // Night shading: faces lit by the teal haze, front faces stay dark
    float side = abs(n.x);                              // faces along the avenue
    float tone = 0.5 + hash21(floor((p.xz - n.xz * 0.01) / CELL) + 7.3);
    float lum  = 0.06 + 0.40 * side * tone + 0.10 * n.y;
    lum *= 0.6 + 0.4 * smoothstep(0.0, 6.0, p.y);        // darker street level
    vec3 bcol = darkCol + fogCol * lum;
    bcol += windowLight(p, n, floor((p.xz - n.xz * 0.01) / CELL), t, rd);

    // neon: emissive tube pattern on the sign, colored light on walls/street
    if (onSign) {
        bool front = (si.x < 1.5) ? (abs(n.z) > 0.5) : (abs(n.x) > 0.5);
        if (front) {
            vec2  f   = (si.x < 1.5) ? vec2(-lq.x * n.z, lq.y) : vec2(lq.z * n.x, lq.y);
            float pix = t / (CAM_ZOOM * iResolution.y) / max(abs(dot(rd, n)), 0.15);
            bcol = signFace(f, si, sc * fl, pix);
        } else {
            bcol = vec3(0.015, 0.02, 0.025) + sc * 0.04;        // housing
        }
    } else if (p.y > 0.001) {
        bcol += sc * 0.30 * exp(-ds * 8.0);             // sign light on walls
    } else {
        // sign light on the road: one continuous plane, so gather the signs
        // of all buildings around this point, not just of this cell
        vec2 c0 = floor(p.xz / CELL);
        for (int j = -1 + ZERO; j <= 1; j++) {
            for (int i = -1 + ZERO; i <= 1; i++) {
                vec2  bc = c0 + vec2(float(i), float(j));
                float mx = mod(bc.x, 3.0);
                if (mx == 0.0) continue;                // street cell: no building
                vec3  c2, l2;
                vec4  s2;
                float f2;
                float d2 = signDist(p, bc, (mx == 1.0) ? -1.0 : 1.0, c2, l2, s2, f2);
                bcol += c2 * 0.30 * exp(-d2 * 8.0);
            }
        }
    }
    if (p.y < 0.001) {                                  // headlight pools on the road
        float ct;
        bcol += carLights(floor(p.xz / CELL), ro, rd, p, true, ct);
        if (REFL > 0.0) {                               // wet road, fresnel weighted
            float fr = 0.04 + 0.96 * pow(1.0 - max(-rd.y, 0.0), 5.0);
            bcol += roadReflect(p, rd) * fr * REFL;
        }
    }
    bcol += fogCol * 0.15 * exp(-p.y * 0.5);            // haze rising from the street
    return applyFog(bcol, t, p.y, rd, camH);
}

vec3 dirYP(float yaw, float pitch) {
    return vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch));
}

float lookPitch(float h) {                              // look down when high
    return mix(0.18, -0.40, smoothstep(5.0, 18.0, h));
}

// View modes, after pxc Camera.cpp (camera position ro, target ta),
// street level and mid height only, slow cinematic moves:
// 0 street drift (CAM_HEIGHT)   1 flycam, rising and diving along a street
// 2 speed, low and fast         3 fly-by along a cross street
// 4 crane, looking up           5 slow pan from a street corner
void camMode(int mode, float t, out vec3 ro, out vec3 ta) {
    float h;
    if (mode == 1) {
        ro = vec3(0.5 * CELL, 4.0 + 2.8 * sin(t * 0.07), t * 0.45);
        ta = vec3((0.5 + 0.35 * sin(t * 0.11)) * CELL, 3.0 + 2.0 * sin(t * 0.07 + 1.2), ro.z + 10.0);
    } else if (mode == 2) {
        ro = vec3(0.5 * CELL, 0.9, t * 1.6);
        ta = ro + vec3(0.0, 0.45, 8.0);
    } else if (mode == 3) {
        ro = vec3(t * 0.35, 2.2 + 1.2 * sin(t * 0.08), 12.5 * CELL);
        ta = ro + vec3(6.0, 1.6, 0.6 * sin(t * 0.06));
    } else if (mode == 4) {
        h  = mix(0.5, 8.0, 0.5 - 0.5 * cos(t * 0.06));
        ro = vec3(0.5 * CELL, h, t * 0.2);
        ta = ro + dirYP(0.15, 0.32 - 0.02 * h);
    } else if (mode == 5) {
        ro = vec3(0.5 * CELL, 1.6, 12.5 * CELL);
        ta = ro + dirYP(t * 0.06, 0.22 + 0.10 * sin(t * 0.09));
    } else {
        ro = vec3(0.5 * CELL, CAM_HEIGHT, t * 0.25);
        ta = ro + dirYP(0.15, lookPitch(CAM_HEIGHT));
    }
}

// Rain: thin streaks on 4 cylinders around the camera. World aligned, so the
// drops fall straight and keep their place when the view turns. Each streak
// is lit by the scene colour behind it (neon, signs) and fades with distance.
vec3 rain(vec3 rd, float tHit, vec3 sceneCol) {
    float hl = length(rd.xz);
    if (hl < 0.15) return vec3(0.0);
    float a   = atan(rd.x, rd.z);
    float pix = 1.0 / (iResolution.y * CAM_ZOOM);       // pixel size in radians
    vec3  acc = vec3(0.0);
    for (int k = ZERO; k < 4; k++) {
        float R  = 0.6 * exp2(float(k) * 1.1);          // layers 0.6, 1.3, 2.8, 6.0
        float tc = R / hl;                              // ray distance to the layer
        if (tc > tHit) break;                           // hidden behind a wall
        vec2  p  = vec2(a * R, R * rd.y / hl);          // on the cylinder, world units
        p.y += mod(iTime, 600.0) * (5.0 + float(k));  // fall speed, wall clock
        p.x += p.y * 0.12;                              // slight wind slant
        vec2  cs = vec2(0.05, 0.9);                     // cell: width, height
        vec2  id = floor(p / cs);
        vec2  f  = p - id * cs;
        float h  = hash21(id + float(k) * 17.0);
        if (h > RAIN) continue;
        float x  = 0.008 + 0.034 * fract(h * 7.31);     // streak position in its cell
        float yc = 0.15 + 0.6 * fract(h * 3.17);
        float w  = R * pix * 0.9;                       // about one pixel wide, any resolution
        float L  = 0.10 + 0.08 * fract(h * 11.7);       // streak half length
        float st = smoothstep(w, 0.0, abs(f.x - x)) * smoothstep(L, 0.0, abs(f.y - yc));
        acc += st * (sceneCol * 1.6 + vec3(0.05, 0.10, 0.11)) * exp(-tc * 0.12) / (1.0 + float(k) * 0.6);
    }
    return acc;
}

// Ray / truncated pyramid (one tier): base at y0, height h, base half size b,
// walls leaning in by sl per unit of height. Returns entry distance (or -1)
// and the face normal. Analytic: 6 planes, no march.
float tierHit(vec3 o, vec3 rd, float y0, float h, float b, float sl, out vec3 nn) {
    float tn = -1.0e9, tf = 1.0e9;
    nn = vec3(0.0, 1.0, 0.0);
    for (int f = ZERO; f < 6; f++) {
        vec3  n = (f == 0) ? vec3(1.0, sl, 0.0) : (f == 1) ? vec3(-1.0, sl, 0.0)
                : (f == 2) ? vec3(0.0, sl, 1.0) : (f == 3) ? vec3(0.0, sl, -1.0)
                : (f == 4) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, -1.0, 0.0);
        float d = (f < 4) ? b + sl * y0 : (f == 4) ? y0 + h : -y0;
        float len  = length(n);
        float dist = (dot(n, o) - d) / len;             // > 0 outside
        float dn   = dot(n, rd) / len;
        if (abs(dn) < 1.0e-6) { if (dist > 0.0) return -1.0; continue; }
        float tt = -dist / dn;
        if (dn < 0.0) { if (tt > tn) { tn = tt; nn = n / len; } }
        else tf = min(tf, tt);
    }
    return (tn < tf && tn > 0.0) ? tn : -1.0;
}

// Distant pyramids after Teotihuacan (Pyramid of the Sun, and a smaller one
// further back): four stepped tiers with sloped walls, flat summit with a
// small temple, a wide stairway on the face toward the city, lit tier edges
// and steps. Seen only where the city march found sky; they follow the camera
// like a far landmark, ahead along the avenues (+z).
vec3 pyramids(vec3 ro, vec3 rd, vec3 col) {
    for (int k = ZERO; k < 2; k++) {
        float sc = (k == 0) ? 1.0 : 0.62;               // second one: smaller
        vec3  C  = vec3(ro.x, 0.0, ro.z) + ((k == 0) ? vec3(0.0, 0.0, 72.0) : vec3(46.0, 0.0, 100.0));
        vec3  o  = (ro - C) / sc;                       // pyramid space (unit = scale)
        float tb = 1.0e9;
        vec3  nb = vec3(0.0);
        float tierId = 0.0;
        for (int i = ZERO; i < 5; i++) {                // 4 tiers + temple
            float fi = float(i);
            float y0 = (i == 0) ? 0.0 : (i == 1) ? 7.0 : (i == 2) ? 13.0 : (i == 3) ? 18.0 : 22.0;
            float h  = (i == 0) ? 7.0 : (i == 1) ? 6.0 : (i == 2) ? 5.0 : (i == 3) ? 4.0 : 2.2;
            float b  = (i == 0) ? 34.0 : (i == 1) ? 28.0 : (i == 2) ? 23.0 : (i == 3) ? 18.0 : 5.0;
            float sl = (i < 4) ? 0.55 : 0.0;            // talud: walls lean in 0.55 per unit up
            vec3  n;
            float tt = tierHit(o, rd, y0, h, b, sl, n);
            if (tt > 0.0 && tt < tb) { tb = tt; nb = n; tierId = fi; }
        }
        if (tb < 1.0e8) {
            vec3  P   = o + rd * tb;
            float tw  = tb * sc;                        // world distance
            vec3  c   = DARKCOL + FOGCOL * (0.05 + 0.10 * max(nb.y, 0.0) + 0.06 * abs(nb.x));
            // stairway on the face toward the city (-z): lit steps
            bool  stair = (nb.z < -0.3) && (abs(P.x) < 3.2) && (tierId < 3.5);
            if (stair) {
                c  = DARKCOL + FOGCOL * 0.12;
                c += vec3(0.95, 0.70, 0.40) * 0.18 * smoothstep(0.75, 0.95, fract(P.y * 2.0));
            }
            // lights along the top edge of each tier, and warm windows on the walls
            float y0  = (tierId < 0.5) ? 7.0 : (tierId < 1.5) ? 13.0 : (tierId < 2.5) ? 18.0 : (tierId < 3.5) ? 22.0 : 24.2;
            float u   = (abs(nb.x) > abs(nb.z)) ? P.z : P.x;
            c += vec3(1.0, 0.72, 0.42) * 0.45 * exp(-(y0 - P.y) * 3.0) * step(0.5, fract(u * 1.3));
            float wl  = hash21(vec2(floor(u * 1.4), floor(P.y * 1.2) + float(k) * 40.0));
            c += vec3(0.95, 0.70, 0.40) * step(0.82, wl) * step(0.4, fract(P.y * 1.2)) * 0.12;
            if (tierId > 3.5) c += vec3(1.0, 0.70, 0.40) * 0.20;   // temple on the summit
            col = applyFog(c, tw * 0.6, P.y * sc, rd, ro.y);   // same haze as the city
            col = mix(col, FOGCOL * 0.85, 0.535);         // plus aerial haze: far behind the city
        }
        // red beacon above the temple, blinking
        vec3  A  = C + vec3(0.0, 25.0 * sc, 0.0);
        float da = length(cross(A - ro, rd));
        if (dot(A - ro, rd) > 0.0)
            col += vec3(1.0, 0.15, 0.08) * exp(-da * 3.0) * (0.5 + 0.5 * step(0.5, fract(iTime * 0.7 + float(k) * 0.3))) * 0.6;
    }
    return col;
}

// Holograms: Britney (iChannel1 video, close-up) projected in the street
// among the signs, always one in view: mid-block, 4 to 10 units ahead of the
// camera, fading out when too close while the next one fades in further down
// the avenue. A vertical plane that turns to face the camera; additive cyan
// light only, so the city shows through: video luminance in a soft oval
// projection mask, chromatic offset, scanlines, a rolling band, flicker and
// glitch tears, hidden behind whatever the ray hits first (tHit).
vec3 hologram(vec3 ro, vec3 rd, float tHit) {
    return vec3(0.0);                            // HOLO off: no iChannel1 video on this engine
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    ZERO = int(min(iTime, 0.0));                 // runtime 0: blocks compile-time loop unrolling
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // View orientation from iMouse (0,0 = neutral)
    vec2 m = vec2(0.0);
    if (iMouse.x > 0.0 || iMouse.y > 0.0) {
        m = iMouse.xy / iResolution.xy - 0.5;
    }
    // View mode: fixed, or auto cycle with a short fade between views
    int   mode = CAM_MODE;
    float fade = 1.0;
    if (mode < 0) {
        float seg = floor(iTime / CAM_INTERVAL);
        mode = int(mod(seg, 6.0));
        float tl = iTime - seg * CAM_INTERVAL;
        fade = smoothstep(0.0, 0.8, min(tl, CAM_INTERVAL - tl));
    }
    vec3 ro, ta;
    camMode(mode, iTime, ro, ta);
    float camH = ro.y;

    // iMouse turns the view on top of the camera path
    vec3  d0    = normalize(ta - ro);
    float yaw   = atan(d0.x, d0.z) + m.x * 3.14159;
    float pitch = clamp(asin(d0.y) + m.y * 1.2, -1.4, 1.4);
    vec3 fw = dirYP(yaw, pitch);
    vec3 rt = normalize(cross(vec3(0.0, 1.0, 0.0), fw));
    vec3 up = cross(fw, rt);
    vec3 rd = normalize(fw * CAM_ZOOM + uv.x * rt + uv.y * up);

    vec3  neon;
    float t   = march(ro, rd, neon);
    vec3  col;
    if (t > 0.0 && gBodyT < 1.0e8) {                    // car / spinner body
        vec3 n = gBodyN;
        col  = gBodyC * (0.35 + 0.65 * max(n.y, 0.0)) + FOGCOL * 0.08 * (1.0 - abs(n.y));
        col += vec3(0.6, 0.8, 1.0) * 0.12 * pow(1.0 - abs(dot(n, rd)), 3.0);   // cool rim from the haze
        col  = applyFog(col, t, ro.y + rd.y * t, rd, camH);
    } else {
        col = (t > 0.0) ? shadeHit(ro, rd, t, camH) : skyCol(rd);
        if (PYRAMID > 0 && t <= 0.0) col = pyramids(ro, rd, col);
    }
    col += neon * 0.8;                                  // neon halo
    col += beamLights(ro, rd, (t > 0.0) ? t : MAXDIST); // projector beams
    if (float(HOLO) > 0.0) col += hologram(ro, rd, (t > 0.0) ? t : MAXDIST);   // Britney holograms
    if (RAIN > 0.0) col += rain(rd, (t > 0.0) ? t : 1.0e3, col) * RAININT;

    // Cinematic grade: filmic tone curve, teal shadows / warm highlights, grain
    if (CINEMA > 0) {
        col *= 0.95;                                    // exposure
        col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
        float lu = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(col * vec3(0.88, 1.0, 1.06), col * vec3(1.08, 1.0, 0.90), smoothstep(0.15, 0.7, lu));
        col += (hash21(fragCoord + fract(iTime * 7.13) * 371.0) - 0.5) * 0.03;
    }

    // Vignette, fade between views
    vec2 v = fragCoord / iResolution.xy - 0.5;
    col *= (1.0 - 0.5 * dot(v, v)) * fade;

    // 2.39:1 letterbox
    if (LETTERBOX > 0 && abs(uv.y) > 0.5 * iResolution.x / (iResolution.y * 2.39)) col = vec3(0.0);

    fragColor = vec4(col, 1.0);
}
void main() {
	vec4 fragColor;
	mainImage(fragColor, gl_FragCoord.xy);
	gl_FragColor = fragColor;
}
)GLSL";

#endif /* CITY_GLSL_H */
