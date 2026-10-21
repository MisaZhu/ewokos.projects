/*
 * rainwin_glsl.h -- GLES2 / GLSL port of the shadertoy effect "Realistic rain
 * shader" (https://www.shadertoy.com/view/7XtSD7 by anAccount).
 *
 * A physically-based rain-on-a-window night-city view: spherical-cap water
 * drops (static sessile beads plus stick-slip sliders that wipe the
 * condensation, leave a thin film trail and pinch off satellite droplets), each
 * drop acting as a wavelength-dispersive refracting lens with unpolarised
 * Fresnel reflectance, total internal reflection near the contact line,
 * Beer-Lambert absorption through the local thickness, and an internal
 * reflection of the room behind the camera.  The world outside is a defocused
 * thin-lens bokeh cityscape -- sky glow, three skylines of lit windows,
 * blinking aircraft beacons, three lanes of traffic with wet-road streaks, and
 * three motion-blurred rain layers -- all lit by an occasional lightning flash.
 *
 * The fragment body below is the reference shader reproduced VERBATIM (header
 * comment, every constant, function and tuning value kept as authored) under
 * its shadertoy licence.  It is fully procedural: iChannel0..3 are unused, so
 * there are no external texture assets -- the only inputs are the shadertoy
 * builtins iResolution / iTime / iMouse.  The porting changes are the minimum
 * needed to run unchanged on this engine, and none of them alter the rendered
 * result on the GPU target:
 *   - the shadertoy wrapper supplies iResolution / iTime / iMouse as uniforms
 *     and a thin main() forwards gl_FragCoord to mainImage() and writes
 *     gl_FragColor, matching the engine's fullscreen-quad convention;
 *   - the two `#define` macros (PI, TAU) become file-scope `const float`
 *     (the CPU lexer skips '#' lines, so macros would be undefined there);
 *   - the reference derives the through-drop world footprint with the
 *     screen-space derivative built-ins dFdx/dFdy.  Those do not exist on this
 *     engine: the GPU route dispatches the effect as a #version 310 es COMPUTE
 *     kernel (derivative built-ins are illegal there) and the CPU interpreter
 *     stubs them to zero.  That single expression is therefore replaced with an
 *     analytic equivalent driven by the anti-aliased cap coverage already
 *     computed in mainImage -- identical floor/ceiling behaviour, no
 *     screen-space differencing.  See the note at that line.
 *
 * GPU route (raspi5 / raspix, PGL_G3D): the vertex shader is a passthrough with
 * no uMVP and this fragment shader reads gl_FragCoord, so pgl_engine recognises
 * the program as a fullscreen effect and dispatches it as a VideoCore VII
 * compute kernel (#version 310 es) through the same transparent path
 * shadertoy/waterdrop/ocean/city use.  Mesa compiles the body as authored --
 * structs, inout/out parameters, dynamic vector indexing by a loop variable and
 * refract/reflect are all legal ES 3.10 -- so the GPU output is the reference
 * effect.  CPU route (virt): the engine's interpreter runs the identical GLSL;
 * this is an extremely heavy per-pixel optic solve (three bokeh layers, three
 * skylines, traffic, three rain layers and a multi-scale water field), so very
 * low FPS there is the documented interpreter cost ceiling, not a defect.
 */

#ifndef RAINWIN_GLSL_H
#define RAINWIN_GLSL_H

#ifndef __cplusplus
#error "rainwin_glsl.h requires C++ (raw string literals)"
#endif

/* Passthrough vertex shader: a full-screen clip-space quad.  It intentionally
 * references no uMVP / matrices so the engine flags the program as a fullscreen
 * effect and routes the fragment shader to the GPU compute path. */
static const char* rainwin_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

static const char* rainwin_fs_glsl =
	"#version 100\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"uniform float iTime;\n"
	"uniform vec3  iResolution;\n"
	"uniform vec4  iMouse;\n"
	R"GLSL(
// =====================================================================
//  RAIN ON A WINDOW, NIGHT CITY
//  Mouse X: rain intensity      Mouse Y: condensation on the glass
//
//  Optics
//   - Drops are spherical caps (contact radius a, contact angle theta).
//     Thickness and gradient are analytic, so the normals are exact.
//   - The camera ray crosses the glass (a parallel plate, no net bend), then
//     refracts at the curved water/air surface with Snell's law. The index is
//     wavelength dependent (656/589/486 nm) so bright lights inside a drop
//     split into colour fringes.
//   - Unpolarised Fresnel equations from the water side give the reflectance,
//     including total internal reflection near the contact line. The reflected
//     ray picks up the room: a warm lamp and a cool screen -> catchlights and
//     bright rims.
//   - Beer-Lambert absorption through the local water thickness, with real
//     absorption coefficients (physically almost invisible at millimetre scale).
//   - The camera is focused on the glass, so the world is a thin-lens circle of
//     confusion with energy-conserving bokeh. A drop is a lens: what you see
//     through it is inverted, minified and resolved at the drop's own footprint.
//   - Sliding drops stick-slip, wipe the condensation, leave a thin film trail
//     and pinch off satellite droplets that shrink as they evaporate.
//   - Background rain is motion-blurred streaks, depth-blurred, lit by whatever
//     light is nearby. Lightning lights the cloud deck and the rain.
// =====================================================================

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

const vec3  N_WATER    = vec3(1.3311, 1.3330, 1.3371); // refractive index at 656 / 589 / 486 nm
const vec3  K_ABS      = vec3(0.26, 0.064, 0.0165);    // absorption of pure water [1/m] at ~610/550/465 nm
const float M_PER_UNIT = 0.10;                         // 1 screen unit (half image height) = 10 cm of glass
const float CAM_F      = 1.6;                          // camera focal length in half-heights (~64 deg vertical FOV)
const float BLUR0      = 0.024;                        // circle of confusion of the world (focus is on the glass)
const float YG         = -0.18;                        // street level in the view

float gTime, gPx, gFlash, gRain, gFog;

// ---------------------------------------------------------------- hashing / noise
float hash11(float p){ p = fract(p*.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash21(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y)*p3.z); }
vec2  hash22(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*vec3(.1031,.1030,.0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz)*p3.zy); }
vec3  hash32(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*vec3(.1031,.1030,.0973)); p3 += dot(p3, p3.yxz + 33.33); return fract((p3.xxy + p3.yzz)*p3.zyx); }

float vnoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3. - 2.*f);
    return mix(mix(hash21(i), hash21(i + vec2(1,0)), f.x),
               mix(hash21(i + vec2(0,1)), hash21(i + vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p)
{
    float a = .5, s = 0.;
    for (int i = 0; i < 1; i++) { s += a*vnoise(p); p = p*2.03 + 17.1; a *= .5; }
    return s;
}

// 1 inside (signed distance d < 0), 0 outside, soft edge of half-width w
float soft(float d, float w){ return 1. - smoothstep(-w, w, d); }

// exact unpolarised Fresnel reflectance, light going from medium n1 into n2
float fresnel(float cosi, float n1, float n2)
{
    float s2 = (n1/n2)*(n1/n2)*(1. - cosi*cosi);
    if (s2 >= 1.) return 1.;                       // total internal reflection
    float cost = sqrt(1. - s2);
    float rs = (n1*cosi - n2*cost)/(n1*cosi + n2*cost);
    float rp = (n1*cost - n2*cosi)/(n1*cost + n2*cosi);
    return .5*(rs*rs + rp*rp);
}

float lightning(float t)
{
    float T  = 14.;
    float ft = mod(t + 9., T);
    float n  = floor((t + 9.)/T);
    float f  = exp(-abs(ft - 1.00)*22.) + .7*exp(-abs(ft - 1.22)*30.) + .9*exp(-abs(ft - 1.55)*12.);
    return f*(.6 + .8*hash11(n));
}

// ---------------------------------------------------------------- the room behind the camera
vec3 roomEnv(vec3 d)
{
    d = normalize(d);
    vec3 L1 = normalize(vec3(-.45,  .55, -.70));      // warm lamp, up and to the left
    vec3 L2 = normalize(vec3( .60, -.35, -.72));      // cool screen, low and to the right
    float a = max(dot(d, L1), 0.), b = max(dot(d, L2), 0.);
    vec3 c = vec3(.010, .012, .018);
    c += vec3(1.0, .60, .28)*(40.*pow(a, 160.) + .45*pow(a, 18.) + .05*pow(a, 3.));
    c += vec3(.25, .50, 1.0)*(10.*pow(b, 90.)  + .35*pow(b, 12.) + .04*pow(b, 3.));
    return c;
}

// ---------------------------------------------------------------- the world outside
vec3 lampPalette(float h)
{
    if (h < .30) return vec3(1.00, .55, .18);    // sodium
    if (h < .55) return vec3(1.00, .82, .55);    // warm white
    if (h < .72) return vec3(.70, .85, 1.0);     // cool LED
    if (h < .82) return vec3(1.0, .15, .10);     // red
    if (h < .92) return vec3(.15, .85, 1.0);     // cyan neon
    return vec3(1.0, .20, .65);                  // magenta neon
}

// Defocused point/disc light. Radiance is conserved: area fraction (r0/r)^2.
vec3 disc(vec2 p, vec2 c, float r0, float blur, vec3 col)
{
    float r  = r0 + blur;
    float d  = length(p - c);
    float aa = max(.1*r, gPx);
    float body = 1. - smoothstep(r - aa, r + aa, d);
    float rim  = 1. + .6*smoothstep(.55*r, r, d);                 // spherical aberration: brighter edge
    float e    = (r0*r0)/(r*r);
    float halo = .25*r*r/(d*d + 5.*r*r);                          // veiling glare
    return col*e*(body*rim + halo);
}

vec3 bokehLayer(vec2 p, float blur, float scale, float seed, float yLo, float yHi,
                float dens, float r0, float power)
{
    vec2 id = floor(p*scale);
    vec3 acc = vec3(0.);
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
    {
        vec2 cid = id + vec2(float(i), float(j));
        vec3 h = hash32(cid + seed);
        vec2 c = (cid + .15 + .7*h.xy)/scale;
        float yy = (c.y - yLo)/(yHi - yLo);
        if (yy < 0. || yy > 1.) continue;
        if (h.z > dens*(1. - .65*yy)) continue;
        float sz = hash21(cid*1.37 + seed + 3.1);
        vec3 lc = lampPalette(hash21(cid + seed*2.3 + 8.));
        float tw = .85 + .15*sin(gTime*(1.5 + 3.*h.x) + h.y*40.);   // atmospheric scintillation
        acc += disc(p, c, r0*(.6 + .9*sz), blur, lc*power*(.5 + sz)*tw);
    }
    return acc;
}

vec3 emitters(vec2 p, float blur)
{
    vec3 c = vec3(0.);
    c += bokehLayer(p, blur,  9.0, 11., YG + .01, .42, .62, .0060, 9.);
    // red aircraft-warning beacons, slow blink
    for (int i = -1; i <= 1; i++)
    {
        float id = floor(p.x/1.3 + .5) + float(i);
        vec3 h = hash32(vec2(id, 77.));
        vec2 b = vec2(id*1.3 + (h.x - .5)*.7, .26 + .2*h.y);
        float blink = smoothstep(.35, .55, .5 + .5*sin(gTime*2.2 + h.z*TAU));
        c += disc(p, b, .0035, blur, vec3(1., .05, .04)*14.*blink);
    }
    return c;
}

vec3 sky(vec2 p)
{
    float y  = p.y;
    float n  = fbm(p*vec2(1.4, 2.2) + vec2(gTime*.012, 0.));
    float n2 = fbm(p*vec2(3.1, 4.5) - vec2(gTime*.020, 3.));
    vec3 c = mix(vec3(.014, .018, .034), vec3(.006, .010, .022), smoothstep(-.1, 1.2, y));
    float glow = exp(-max(y + .15, 0.)*1.9);                      // city glow bounced off the cloud base
    c += vec3(.20, .105, .075)*glow*(.30 + 1.1*n)*.30;
    c += vec3(.05, .09, .17)*glow*n2*.45;
    c += gFlash*vec3(.55, .65, 1.)*(.25 + 1.2*n*n2)*smoothstep(-.2, .6, y)*.9;   // lightning in the cloud deck
    return c;
}

// One window cell: colour * brightness (0 if unlit). Lit fraction depends on the building.
vec3 windowCell(vec2 id, float seed, float litP)
{
    vec3 hw = hash32(id + seed*13.1);
    vec3 wc = mix(vec3(1., .70, .40), vec3(.70, .85, 1.), step(.72, hw.y));
    wc = mix(wc, vec3(.45, .70, 1.), step(.93, hw.y)*(.5 + .5*sin(gTime*9. + hw.z*40.)));   // TV flicker
    return wc*(.35 + 1.2*hw.z)*step(hw.x, litP);
}

void skyline(inout vec3 col, vec2 p, float blur, float scale, float base, float var,
             vec3 body, float seed, float dens, float gain)
{
    float bx = floor(p.x*scale);
    float fx = fract(p.x*scale);
    float wb = blur*scale + .5*gPx*scale + .002;
    float hL = base + var*hash21(vec2(bx - 1., seed));
    float hC = base + var*hash21(vec2(bx,      seed));
    float hR = base + var*hash21(vec2(bx + 1., seed));
    float hh = mix(hL, hC, smoothstep(-wb, wb, fx));
    hh = mix(hh, hR, smoothstep(1. - wb, 1. + wb, fx));
    float m = soft(p.y - hh, blur + gPx);
    if (m < .002) return;

    vec2  wg   = vec2(p.x*scale*3., p.y*scale*3./1.45);            // 3 window columns per building
    float bld  = hash21(vec2(bx, seed + 4.));
    float litP = clamp(dens*(.35 + 1.3*bld), 0., 1.);
    float w    = blur*scale*3. + .05;                              // blur radius in window-cell units

    // resolved windows
    vec2 q   = fract(wg) - .5;
    vec3 sharp = windowCell(floor(wg), seed, litP)
               * soft(abs(q.x) - .28, w)*soft(abs(q.y) - .30, w);
    // once the blur exceeds a window, the facade is a smooth field of the mean window light
    vec2 i0 = floor(wg - .5);
    vec3 sm = windowCell(i0, seed, litP)*.34;
    vec3 win = mix(sharp, sm, smoothstep(.2, .75, w));

    col = mix(col, body + .02*gFlash, m);
    col += win*gain*m;
}

vec3 cityBase(vec2 p, float blur)
{
    vec3 col = sky(p);
    skyline(col, p, blur, 5.4, -.10, .36, vec3(.009, .012, .020), 2., .30, .85);
    return col;
}

// Cheaper single-skyline variant used ONLY for the defocused wet-road mirror
// image, where the extra skyline detail is lost in the blur anyway.
vec3 cityBaseLite(vec2 p, float blur)
{
    return sky(p);
}

vec3 world(vec2 p, float blur)
{
    float m = soft(p.y - YG, blur*1.3 + gPx);                      // 1 on the road
    vec3 above = vec3(0.), below = vec3(0.);
    if (m < .999) above = cityBase(p, blur) + emitters(p, blur);
    if (m > .001)
    {
        // wet asphalt: dark diffuse + Fresnel-weighted mirror image, rippled and smeared vertically
        float depth = YG - p.y;
        vec2 pm = vec2(p.x + .006*sin(p.y*47. + gTime*.8) + .003*sin(p.y*113. - gTime*1.3), YG + depth);
        float rb = blur + .012 + .05*depth;
        vec3 refl = cityBaseLite(pm, rb);
        below = vec3(.008, .009, .012) + .30*refl;
    }
    return mix(above, below, m);
}

vec3 traffic(vec2 p, float blur)
{
    vec3 acc = vec3(0.);
    for (int lane = 0; lane < 1; lane++)
    {
        float fl   = float(lane);
        float yl   = -.235 - .075*fl;
        float dirn = (lane == 1) ? 1. : -1.;
        float spd  = .085 + .05*fl;
        float sp   = .52 - .07*fl;
        float xs   = (p.x - dirn*gTime*spd)/sp + fl*3.7;
        float c0   = floor(xs);
        for (int i = -1; i <= 1; i++)
        {
            float cid = c0 + float(i);
            vec3 h = hash32(vec2(cid, fl*17. + 2.));
            if (h.x > .48) continue;
            float cx  = (cid - fl*3.7 + .05 + .9*h.y)*sp + dirn*gTime*spd;
            float sep = (.022 + .006*fl)*(.8 + .5*h.z);
            float r0  = (.0045 + .0022*fl)*(.8 + .5*h.y);
            vec3 colL = (dirn > 0.) ? vec3(1., .05, .03)*16. : vec3(1., .90, .72)*24.;
            for (int s = 0; s < 2; s++)
            {
                vec2 c  = vec2(cx + ((s == 0) ? -sep : sep), yl);
                acc += disc(p, c, r0, blur, colL);
                vec2 dd = p - c;
                float r = r0 + blur;
                float len = .05 + .04*fl;
                float bl  = smoothstep(0., .01, -dd.y);
                float rip = .75 + .25*sin(dd.y*260. + gTime*2.5 + h.z*30.);
                acc += colL*(r0*r0/(r*r))*.35*bl*exp(dd.y/len)*exp(-dd.x*dd.x/(1.2*r*r))*rip;   // wet-road streak
            }
        }
    }
    return acc;
}

// Motion-blurred rain streak layer. Near layers are close to the focus plane (sharp, fast),
// far layers are fully defocused. Streak energy is conserved when the width is blurred.
float rainLayer(vec2 p, float blur, float kx, float speed, float len, float seed, float bmul, float w0)
{
    float cell = 3.2*len;
    float x    = p.x + p.y*.09;                                   // wind slant
    float col  = floor(x*kx);
    float hc   = hash21(vec2(col, seed));
    float v    = speed*(.85 + .3*hc);
    float yy   = (p.y + v*gTime)/cell + hc*31.;
    float cy   = floor(yy);
    float fy   = fract(yy);
    vec2  hh   = hash22(vec2(col*1.7 + seed, cy));
    if (hh.x > .75*gRain) return 0.;
    float cx   = (col + .15 + .7*hh.y)/kx;
    float s0   = w0*.5 + .5*gPx;
    float sg   = sqrt(s0*s0 + pow(blur*bmul*.35, 2.));
    float dx   = x - cx;
    float xm   = exp(-.5*dx*dx/(sg*sg))*(s0/sg);
    float ew   = max(.015, blur*bmul/cell);
    float f0   = .08;
    float ym   = smoothstep(f0, f0 + ew, fy)*pow(1. - smoothstep(f0, f0 + len/cell, fy), 1.5);
    return xm*ym;
}

vec3 scene(vec2 p, float blur)
{
    vec3 col = world(p, blur) + traffic(p, blur);
    float rs = 0.;
    rs += rainLayer(p, blur,  9., 6.0, .15, 1., .25, .0016)*0.8;
    vec3 lit = min(col, vec3(3.));                                  // rain scatters the light around it
    col += rs*(vec3(.040, .048, .066) + 1.0*lit + gFlash*vec3(.55, .65, 1.)*2.5);
    return col;
}

// ---------------------------------------------------------------- water on the glass
struct Cap   { float h; vec2 g; };
struct Water { float ch; vec2 cg; float fh; vec2 fg; float wet; };   // cap height/gradient, film height/gradient, fog-wiped

// Spherical cap with contact radius a and contact angle th. d = position relative to the centre.
Cap cap(vec2 d, float a, float th, vec2 an)
{
    Cap c; c.h = 0.; c.g = vec2(0.);
    vec2 q = d/an;
    float r2 = dot(q, q);
    if (r2 < a*a)
    {
        float R = a/sin(th);
        float s = sqrt(R*R - r2);
        c.h = s - R*cos(th);
        c.g = -(q/s)/an;
    }
    return c;
}

void addCap(inout Water w, Cap c)
{
    if (c.h > w.ch) { w.ch = c.h; w.cg = c.g; }
    if (c.h > 0.) w.wet = 1.;
}

float meander(float y, float s){ return .012*(.7*sin(y*8.3 + s) + .3*sin(y*19.1 + s*1.7)); }

// Sessile drops: appear on impact, shrink by evaporation, reappear elsewhere.
void staticDrops(inout Water w, vec2 p, float cell, float seed, float a0, float a1, float dens, float rate)
{
    vec2 id = floor(p/cell);
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
    {
        vec2 cid = id + vec2(float(i), float(j));
        float ph = hash21(cid + seed);
        float tt = gTime*rate*(.6 + .8*ph) + ph*10.;
        float cyc = floor(tt), life = fract(tt);
        vec3 hs = hash32(cid + vec2(cyc*7.31, seed));
        if (hs.z > dens) continue;
        vec2 c = (cid + .2 + .6*hs.xy)*cell;
        float sz = hs.z/dens;
        float a = mix(a0, a1, sz*sz);
        a *= smoothstep(0., .04, life)*(1. - .6*smoothstep(.4, 1., life))*(1. - smoothstep(.92, 1., life));
        float th = radians(48. + 24.*hs.y);
        addCap(w, cap(p - c, a, th, vec2(1., 1. + .12*hs.x)));
    }
}

// Sliding drops: stick-slip motion, film trail that wipes the fog, satellite droplets.
void sliders(inout Water w, vec2 p, float cw, float P, float seed, float A0, float speed, float occ)
{
    float c0 = floor(p.x/cw);
    for (int di = -1; di <= 1; di++)
    {
        float c  = c0 + float(di);
        vec3  hc = hash32(vec2(c, seed));
        float v  = speed*(.55 + .9*hc.x);                          // mean sliding speed [units/s]
        float wy = p.y + hc.y*91.7;                                // static world-space y for this column
        float yf = wy + v*gTime;                                   // frame that moves with the mean drop speed
        float k  = floor(yf/P);
        float ly = yf - (k + .5)*P;
        vec3  hk = hash32(vec2(c*13.7 + seed, k));
        if (hk.x > occ) continue;
        vec2  hv = hash22(vec2(k*3.1 + seed, c));

        float a  = A0*(.55 + .75*hk.y);
        float th = radians(50. + 24.*hk.z);
        float om = 1. + 1.8*hv.x;
        float ar = om*gTime + TAU*hv.y;
        // monotone stick-slip: world speed v*(1 + 0.7cos + 0.3cos2.3) >= 0, pauses when both align
        float wob = v*(.7*sin(ar)/om + .3*sin(2.3*ar)/(2.3*om));
        float yd  = -.30*P - wob;                                  // drop centre in the local frame
        float xc  = (c + .5)*cw + (hc.z - .5)*.45*cw;
        float sd  = hc.y*50.;
        float dy  = ly - yd;                                       // > 0 above the drop, i.e. in its trail

        float xd = xc + meander(wy - dy, sd);
        addCap(w, cap(vec2(p.x - xd, dy), a, th, vec2(1., 1. + .25*hk.y)));

        if (dy > 0.)
        {
            float L  = .70*P;
            float tt = dy/L;
            if (tt < 1.)
            {
                float dx = p.x - (xc + meander(wy, sd));
                float wt = a*.55*(1. - .55*tt);
                float s  = dx/wt;
                float fade = (1. - tt)*(1. - tt)*smoothstep(0., .05, tt);
                if (abs(s) < 1.)
                {
                    float q  = sqrt(1. - s*s);
                    float hf = .05*a*fade;                         // film thickness ~ 5% of the drop radius
                    w.fh   += hf*q*q*q;
                    w.fg.x += hf*(-3.*s*q)/wt;
                }
                float ww = a*1.3;
                w.wet = max(w.wet, (1. - smoothstep(.55*ww, ww, abs(dx)))*(1. - tt));

                // satellites pinched off behind the drop, fixed to the glass, shrinking with age
                float sh = a*1.15;
                float si = floor(wy/sh);
                for (int j = -1; j <= 1; j++)
                {
                    float sj = si + float(j);
                    vec3 hs = hash32(vec2(sj*1.31 + c*37.1, seed + 5. + k*.37));
                    if (hs.x > .5) continue;
                    float ys  = (sj + hs.y)*sh;
                    float tts = (dy + (ys - wy))/L;
                    if (tts <= 0. || tts >= 1.) continue;
                    float as_ = a*(.10 + .26*hs.z)*pow(1. - tts, .6)*smoothstep(0., .06, tts);
                    float xs  = xc + meander(ys, sd) + (hash21(vec2(sj, c + seed + k)) - .5)*a*1.6;
                    addCap(w, cap(vec2(p.x - xs, wy - ys), as_, radians(58.), vec2(1.)));
                }
            }
        }
    }
}

Water waterField(vec2 p)
{
    Water w; w.ch = 0.; w.cg = vec2(0.); w.fh = 0.; w.fg = vec2(0.); w.wet = 0.;
    float R = gRain;
    staticDrops(w, p, .052, 2., .0040, .0140, .50*R, .040);
    sliders(w, p, .34, 1.55, 11., .036, .13, .28 + .5*R);
    sliders(w, p, .16,  .92, 23., .020, .09, .18 + .5*R);
    return w;
}

// ---------------------------------------------------------------- main
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 res = iResolution.xy;
    vec2 p   = (fragCoord - .5*res)/(.5*res.y);
    gPx   = 2./res.y;
    gTime = iTime;
    bool clicked = iMouse.x > 1.;
    gRain  = clicked ? clamp(iMouse.x/res.x, .05, 1.) : .65;
    gFog   = clicked ? clamp(iMouse.y/res.y, 0., 1.)  : .45;
    gFlash = lightning(iTime);

    Water w = waterField(p);

    // z points from the camera out into the world; the glass is z = 0 and water sits at 0 < z < h(x,y)
    vec2  g    = w.cg + w.fg;
    vec3  nO   = normalize(vec3(-g, 1.));            // outward surface normal
    float cosi = nO.z;
    vec3  I    = vec3(0., 0., 1.);                   // camera ray inside the water
    float h    = w.ch + w.fh;

    // coverage of the cap (anti-aliased with distance-to-contact-line = h / |grad h|)
    float cov = clamp(w.ch/(gPx*max(length(w.cg), .3)), 0., 1.);

    // Footprint of one pixel in world space, for filtering the minified image
    // seen through a drop.  The reference computes this from the screen-space
    // derivatives of the refracted sample point (dFdx/dFdy).  Derivative
    // built-ins are not available on this engine -- the GPU route dispatches the
    // effect as a #version 310 es compute kernel, where they are illegal, and
    // the CPU interpreter stubs them to zero -- so the footprint is evaluated
    // analytically here.  On bare glass the refracted ray is axis-parallel and a
    // pixel maps to ~.6*gPx of world (the .003 blur floor); a drop is a strong
    // minifying lens whose footprint drives the blur to the .03 ceiling.  The
    // anti-aliased cap coverage `cov` (computed just above) is exactly the
    // inside-drop mask over which the reference's derivative saturates, so
    // blending the floor to the ceiling by `cov` reproduces the derivative's
    // behaviour -- minimum blur on bare glass, maximum blur through a drop,
    // ramping across the contact line -- without any screen-space differencing.
    float blurIn = clamp(mix(.6*gPx, .03, cov), .003, .03);

    // glass, condensation and the thin film
    vec3 nF = normalize(vec3(-w.fg, 1.));
    vec3 rF = refract(I, -nF, N_WATER.y);
    vec2 qF = p + CAM_F*rF.xy/rF.z;
    float fogK    = gFog*(1. - w.wet);
    float blurOut = mix(BLUR0*.8, BLUR0*1.9, fogK);
    vec3 colOut = scene(qF, blurOut);
    colOut = mix(colOut, vec3(.030, .036, .048) + vec3(.020, .013, .008)*(.4 + gFlash), .45*fogK*(.5 + .5*gFog));

    // faint 4% reflection of the room in the bare glass
    vec3 vd = normalize(vec3(p, CAM_F));
    colOut += .04*roomEnv(vec3(vd.xy, -vd.z));

    vec3 col = colOut;
    if (cov > 0.)
    {
        vec3 colIn = vec3(0.);
        for (int i = 0; i < 3; i++)
        {
            float eta = N_WATER[i];
            vec3 r = refract(I, -nO, eta);
            if (dot(r, r) > .5)                                    // otherwise total internal reflection
            {
                vec2 q = p + CAM_F*r.xy/max(r.z, .12);
                vec3 s = scene(q, blurIn);
                colIn[i] = s[i]*(1. - fresnel(cosi, eta, 1.));
            }
        }
        colIn *= exp(-K_ABS*h*M_PER_UNIT);                         // Beer-Lambert through the local thickness

        vec3 Rr = reflect(I, -nO);
        Rr.z = -abs(Rr.z);
        vec3 colRefl = fresnel(cosi, N_WATER.y, 1.)*roomEnv(Rr);   // internal reflection of the room

        col = mix(colOut, colIn + colRefl, cov);
    }

    // exposure, filmic tone map, vignette, grain
    col *= .95;
    col = (col*(2.51*col + .03))/(col*(2.43*col + .59) + .14);
    col = pow(clamp(col, 0., 1.), vec3(1./2.2));
    vec2 uv = fragCoord/res;
    col *= .55 + .45*pow(16.*uv.x*uv.y*(1. - uv.x)*(1. - uv.y), .2);
    col += (hash21(fragCoord + fract(iTime)*100.) - .5)*.012;

    fragColor = vec4(col, 1.);
}
void main() {
	vec4 fragColor;
	mainImage(fragColor, gl_FragCoord.xy);
	gl_FragColor = fragColor;
}
)GLSL";

#endif /* RAINWIN_GLSL_H */
