/*
 * colorglass_glsl.h -- GLES2 / GLSL ES 1.00 shaders for the "Colorglass" demo.
 *
 * A faithful port of the ShaderToy effect "Voronoi Stained Glass" by JackSH3
 * (https://www.shadertoy.com/view/ltSfRW): an animated Voronoi cell pattern is
 * turned into a pane of coloured stained glass -- each cell gets its own hue and
 * its own surface normal -- and the pane refracts and reflects a real
 * environment cubemap through a Fresnel (Schlick) blend, with a procedural bump
 * map perturbing the normals so the glass ripples.  A dark lead "frame" is drawn
 * along the Voronoi cell borders.
 *
 * Assets are the reference's own two stock inputs, bundled under res/ and sampled
 * exactly as the original does:
 *   iChannel0 = "Uffizi Gallery"  cubemap (6x512x512) -- reflection/refraction env
 *   iChannel1 = "Rock Tiles"      2D texture          -- bump map for the normals
 * iChannel0 is uploaded with the engine's 6-face cubemap loader, iChannel1 with
 * GL_REPEAT wrap + vertical flip (the reference's sampler vflip=true).
 *
 * The reference is a SINGLE-pass effect (only the Image pass exists), so it maps
 * directly onto this engine's fullscreen fragment-shader route: a passthrough
 * vertex shader plus this fragment shader over a 6-vertex NDC quad.
 *
 * Porting notes vs the GLSL 3.00 original (the engine is GLSL ES 1.00):
 *   - texture(samplerCube,vec3) -> textureCube(...); texture(sampler2D,vec2)
 *     -> texture2D(...).
 *   - iTime -> iGlobalTime (the engine's animation-clock uniform).
 *   - mat2 rotation and the mat3 tangent-space basis are expanded by hand (the
 *     engine subset avoids mat2/mat3, same as the grass port).
 *   - the saturate() float/vec3 overloads are inlined as clamp(...,0,1).
 *   - the unused hash()/rand()/sampleHemisphere() helpers and the #if 0 debug
 *     block are dropped; ANIMATE is always on and the SHOW_* debug defines are
 *     all off, so only the live code path remains.
 *   - loops keep literal bounds (voronoi's 3x3 and 5x5 neighbourhoods).
 */

#ifndef COLORGLASS_GLSL_H
#define COLORGLASS_GLSL_H

#ifndef __cplusplus
#error "colorglass_glsl.h requires C++ (raw string literals)"
#endif

/* Passthrough vertex shader: a full-screen clip-space quad.  It intentionally
 * references no uMVP / matrices so the engine flags the program as a fullscreen
 * effect and routes the fragment shader to the GPU compute path. */
static const char* colorglass_vs_glsl =
	"#version 100\n"
	"attribute vec4 aPosition;\n"
	"void main() {\n"
	"    gl_Position = aPosition;\n"
	"}\n";

#define COLORGLASS_FS_PREAMBLE \
	"#version 100\n" \
	"precision highp float;\n"

/* ------------------------------------------------------------------ *
 * Voronoi Stained Glass fragment shader (single pass).
 *
 * Uniforms:
 *   iGlobalTime  float       seconds since start (the animation clock)
 *   iResolution  vec3        viewport size (xy used)
 *   iChannel0    samplerCube Uffizi Gallery environment (reflection/refraction)
 *   iChannel1    sampler2D   Rock Tiles bump map
 * ------------------------------------------------------------------ */
static const char* colorglass_fs_glsl = COLORGLASS_FS_PREAMBLE R"GLSL(
uniform float iGlobalTime;
uniform vec3  iResolution;
uniform samplerCube iChannel0;
uniform sampler2D   iChannel1;

/* ANIMATE is always on in this port (the reference ships it enabled). */
const float ANIMATION_RADIUS = 0.4;   /* up to 0.5 */
const float NUM_CELLS = 10.0;
const float IOR = 1.5;
const float F0 = 0.04;                /* ((1-IOR)/(1+IOR))^2 */
const float F90 = 1.0;
const float BASE_SHARPNESS = 1.0;
const float cubemapPower = 15.0;
const float chromaFactor = 6.0;
const float toneFactor = 0.55;
const float voronoiColorFactor = 0.4; /* (1/cubemapPower)*chromaFactor */

const float PI = 3.1415926;

vec2 hash2(vec2 p)
{
    /* procedural white noise (the reference's active branch) */
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

/* from https://www.shadertoy.com/view/ldl3W8 (bit modified to return desired values)
 * returns,
 *   x: distance to nearest neighbor
 *   y: cell id
 *   z: distance to border
 */
vec3 voronoi(in vec2 x)
{
    vec2 n = floor(x);
    vec2 f = fract(x);

    //----------------------------------
    // first pass: regular voronoi
    //----------------------------------
    vec2 mg, mr;
    float id = 0.0;

    float md = 8.0;
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
    {
        vec2 g = vec2(float(i), float(j));
        vec2 o = hash2(n + g);
        vec2 ao = (1.0 - ANIMATION_RADIUS) + ANIMATION_RADIUS * sin(iGlobalTime + 6.2831 * o);
        vec2 r = g + ao - f;
        float d = dot(r, r);

        if (d < md)
        {
            md = d;
            mr = r;
            mg = g;
            id = o.x + o.y;
        }
    }

    //----------------------------------
    // second pass: distance to borders
    //----------------------------------
    md = 8.0;
    for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++)
    {
        vec2 g = mg + vec2(float(i), float(j));
        vec2 o = hash2(n + g);
        o = (1.0 - ANIMATION_RADIUS) + ANIMATION_RADIUS * sin(iGlobalTime + 6.2831 * o);
        vec2 r = g + o - f;

        if (dot(mr - r, mr - r) > 0.00001)
            md = min(md, dot(0.5 * (mr + r), normalize(r - mr)));
    }

    return vec3(length(mr), id, md);
}

/* based on https://www.shadertoy.com/view/ldj3zz
 * The mat3 tangent-space basis (uaxis, vaxis, surfaceNormal as columns) is
 * expanded by hand: basis * norm == uaxis*norm.x + vaxis*norm.y + n*norm.z. */
vec3 bump(vec3 surfaceNormal, vec2 uv)
{
    vec3 uaxis = normalize(cross(vec3(0.0, 1.0, 0.0), surfaceNormal));
    vec3 vaxis = normalize(cross(uaxis, surfaceNormal));

    float delta = -1.0 / 512.0;
    float A = texture2D(iChannel1, uv + vec2(0.0, 0.0)).x;
    float B = texture2D(iChannel1, uv + vec2(delta, 0.0)).x;
    float C = texture2D(iChannel1, uv + vec2(0.0, delta)).x;

    /* animate sharpness */
    float sharpness = BASE_SHARPNESS;
    float tm = mod(iGlobalTime / 5.0, 3.5);
    if (tm < 1.0) {
        sharpness += 32.0 * ((cos(fract(tm) * 6.2831)) * -0.5 + 0.5);
    }
    vec3 norm = normalize(vec3(B - A, C - A, sharpness));

    return normalize(uaxis * norm.x + vaxis * norm.y + surfaceNormal * norm.z);
}

float F_Schlick(in float f0, in float f90, in float u)
{
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

void main()
{
    vec2 p = gl_FragCoord.xy / max(iResolution.x, iResolution.y);

    /* compute voronoi pattern */
    vec3 c = voronoi(p * NUM_CELLS);

    /* colorize */
    vec3 col = 0.5 + 0.5 * cos(c.y * 6.2831 + vec3(0.0, 1.0, 2.0));
    col *= voronoiColorFactor;

    /* calculate ray direction */
    vec2 pos = (gl_FragCoord.xy * 2.0 - iResolution.xy) / iResolution.xy;
    vec3 camAt = vec3(0.0, 0.0, -4.0);
    vec3 camTarget = vec3(0.0, 0.5, 0.0);
    vec3 camDir = normalize(camTarget - camAt);
    vec3 camRight = cross(camDir, vec3(0.0, 1.0, 0.0));
    vec3 camUp = normalize(cross(camRight, camDir));
    float focus = 1.0;
    float aspect = iResolution.y / iResolution.x;
    vec3 rayDir = normalize(camRight * pos.x + camUp * pos.y * aspect + camDir * focus);

    /* generate normal */
    vec3 n = cos(c.y * 6.2831 + vec3(1.0, 2.0, 3.0));
    n = normalize(normalize(n) + -20.0 * camDir);   /* get toward to camera */
    n = bump(n, p);

    /* rotate camera and normal (mat2 expanded by hand, column-major) */
    float rot = iGlobalTime * 6.2831 * -0.01;
    float cr = cos(rot);
    float sr = sin(rot);
    vec2 rd_xz = vec2(cr * rayDir.x + sr * rayDir.z, -sr * rayDir.x + cr * rayDir.z);
    rayDir.x = rd_xz.x;
    rayDir.z = rd_xz.y;
    vec2 n_xz = vec2(cr * n.x + sr * n.z, -sr * n.x + cr * n.z);
    n.x = n_xz.x;
    n.z = n_xz.y;

    /* transmitted color */
    vec3 tr = refract(rayDir, n, 1.0 / IOR);
    vec3 tColor = textureCube(iChannel0, tr).rgb;
    float thickness = smoothstep(0.0, 0.4, c.z);
    thickness = pow(thickness, 0.25);
    tColor = (col * thickness) * (tColor * cubemapPower);
    /* add frame */
    float frame = smoothstep(0.015, 0.06, c.z);
    tColor.rgb *= frame;

    /* reflected color */
    vec3 r = reflect(rayDir, n);
    vec3 rColor = textureCube(iChannel0, r).rgb;
    rColor *= cubemapPower;
    rColor.rgb *= (0.3 + 0.7 * frame);

    /* combine refracted and reflected colors */
    float NdotV = dot(n, -rayDir);
    float f = F_Schlick(F0, F90, NdotV);
    vec3 outc = (f * rColor) + (1.0 - (f * rColor)) * tColor;
    outc *= toneFactor;

    gl_FragColor = vec4(clamp(outc, 0.0, 1.0), 1.0);
}
)GLSL";

#endif /* COLORGLASS_GLSL_H */
