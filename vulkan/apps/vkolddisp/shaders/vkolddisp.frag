#version 450
#extension GL_GOOGLE_include_directive : require

/*
 * vkolddisp.frag - Shadertoy lc3BRn "Improved 96-bit 8x12 Font" (JoostAB,
 * fork of Flyguy's Mt2GWD) adapted for the EwokOS Vulkan software device.
 *
 * Architecture (vkmatrix precedent): the reference's Common + Buffer A
 * font engine is ported natively (olddisp.c) because the SPIR-V interpreter
 * is too slow for its per-pixel character-macro chain.  The native pass
 * stamps the exact same glyphs into a coverage grid and uploads it as a
 * window-sized RGBA8 texture (iChannel0 below), encoded losslessly:
 *   R = (coverage >= 1), G = (coverage >= 2)   (2 = GLOW)
 * olddisp_fetchA() reconstructs Buffer A's exact float output from that,
 * which a plain RGBA8 color could not carry (GLOW green = 1.8 would clamp).
 *
 * Buffer B (scanlines) and Image run VERBATIM from res/: the A->B->Image
 * chain is same-pixel texelFetch with no feedback, so each pass keeps its
 * own mainImage (renamed) and reads the previous pass directly.  The
 * interpreter has no OpImageFetch, so texelFetch(s,c,l) is emulated with
 * texture() at texel centers - exact under the NEAREST+CLAMP sampler.
 *
 * The coverage texture is uploaded bottom-up (row 0 = screen bottom) and
 * main() flips gl_FragCoord.y, restoring Shadertoy's bottom-left origin.
 *
 * std140 Params UBO (binding 0): iResolution/iTime/iFrame/iTimeDelta/iDate.
 */

layout(set = 0, binding = 0) uniform Params {
    vec2  uResolution;
    float uTime;
    int   uFrame;
    float uTimeDelta;
    vec4  uDate;
} P;

layout(set = 0, binding = 1) uniform sampler2D iChannel0;

#define iResolution vec3(P.uResolution, 1.0)
#define iTime       P.uTime
#define iFrame      P.uFrame
#define iTimeDelta  P.uTimeDelta
#define iDate       P.uDate

/* The only Common tab value Buffer B needs is DOWN_SCALE (verbatim from
 * res/common.glsl).  Including the whole Common tab would drag its Private
 * font-array globals into the module, which the GPU transpiler declines. */
#define DOWN_SCALE 2.0

/* Buffer A output, reconstructed from the native coverage texture. */
vec4 olddisp_fetchA(ivec2 c) {
    vec2 t = texture(iChannel0, (vec2(c) + 0.5) / P.uResolution).rg;
    float pixel = t.r + t.g;                    /* coverage in {0,1,2} */
    vec3 col = vec3(1.0) * mix(vec3(0.2), vec3(0, 1, 0), pixel);
    return vec4(vec3(col), 1.0);
}

#define mainImage bufferB_mainImage
#define texelFetch(s, c, l) olddisp_fetchA(c)
#include "../res/bufferb.glsl"
#undef texelFetch
#undef mainImage

vec4 olddisp_fetchB(ivec2 c) {
    vec4 fc;
    bufferB_mainImage(fc, vec2(c) + 0.5);       /* pixel-center fragCoord */
    return fc;
}

#define mainImage image_mainImage
#define texelFetch(s, c, l) olddisp_fetchB(c)
#include "../res/image.glsl"
#undef texelFetch
#undef mainImage

layout(location = 0) out vec4 outColor;

void main() {
    vec2 fragCoord = vec2(gl_FragCoord.x, P.uResolution.y - gl_FragCoord.y);
    vec4 fragColor;
    image_mainImage(fragColor, fragCoord);
    outColor = fragColor;
}