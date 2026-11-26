#version 450
/* vkmatrix fragment stage: sample the CPU-rendered rain texture.  The CPU
 * device samples NEAREST with texel-centre truncation, and the quad's UVs map
 * texel centres 1:1 at the internal resolution, so this is an exact copy. */
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main()
{
    outColor = texture(uTex, vUV);
}