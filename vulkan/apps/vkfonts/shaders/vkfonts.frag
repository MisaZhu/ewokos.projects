#version 450
#extension GL_GOOGLE_include_directive : require
/*
 * vkfonts - Shadertoy "Better Text in Shaders" (kishimisu, view/43t3WX).
 *
 * res/common.glsl and res/image.glsl are the VERBATIM Common / Image tabs of
 * the reference shader.  Only three host-side adaptations are applied here,
 * each forced by a documented contract of the EwokOS Vulkan software device:
 *
 *  1. Sampler emulation.  The reference iChannel0 sampler is
 *     {filter: linear, wrap: repeat, vflip: true} (serialized Shadertoy input
 *     JSON).  The CPU interpreter samples NEAREST + CLAMP only
 *     (spirv_interp.c img_get), so print_char is redefined below to wrap with
 *     fract() and filter with an explicit 4-tap bilinear over texel centres.
 *     The vflip half of the contract is honoured by the APP, which uploads the
 *     atlas rows bottom-up so v=0 addresses the image's bottom row.
 *  2. Fragment origin.  cpu_raster.c feeds gl_FragCoord top-left origin while
 *     Shadertoy's fragCoord is bottom-left, so main() flips Y before calling
 *     the verbatim mainImage().
 *  3. Uniforms.  iResolution/iTime/iFrame/iTimeDelta arrive through one std140
 *     UBO (binding 0) instead of Shadertoy's built-in uniforms.
 *
 * Regenerate vkfonts_spirv.h after editing (see Makefile comment).
 */
layout(set = 0, binding = 0) uniform Params {
    vec2  uResolution;   /* offset 0  */
    float uTime;         /* offset 8  */
    int   uFrame;        /* offset 12 */
    float uTimeDelta;    /* offset 16 */
} P;
#define iResolution P.uResolution
#define iTime       P.uTime
#define iFrame      P.uFrame
#define iTimeDelta  P.uTimeDelta

layout(set = 0, binding = 1) uniform sampler2D iChannel0;

#include "../res/common.glsl"

/* ---- adaptation 1: REPEAT + LINEAR over the NEAREST/CLAMP CPU sampler ---- */
#undef print_char
#define FONT_TEX_SIZE 1024.0   /* res/font.png is a fixed 1024x1024 atlas */

/* One NEAREST tap at integer texel index i, wrapped like GL_REPEAT. */
float font_tap(vec2 i)
{
    return texture(iChannel0, fract((i + 0.5) / FONT_TEX_SIZE)).r;
}

/* GL LINEAR at wrapped coord p: texel-space centre offset -0.5, floor + frac,
 * four wrapped taps mixed exactly as the GL bilinear filter does. */
float font_sample(vec2 p)
{
    vec2 t  = fract(p) * FONT_TEX_SIZE - 0.5;
    vec2 i0 = floor(t);
    vec2 f  = t - i0;
    float a = font_tap(i0);
    float b = font_tap(i0 + vec2(1.0, 0.0));
    float c = font_tap(i0 + vec2(0.0, 1.0));
    float d = font_tap(i0 + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

#define print_char(i) \
    font_sample(u + vec2(float(i)-float(x)/FONT_SPACING + FONT_SPACING/8., 15-(i)/16) / 16.)

#include "../res/image.glsl"

/* ---- adaptation 2: bottom-left fragCoord for the verbatim mainImage ---- */
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 fragCoord = vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y);
    vec4 fragColor;
    mainImage(fragColor, fragCoord);
    outColor = fragColor;
}