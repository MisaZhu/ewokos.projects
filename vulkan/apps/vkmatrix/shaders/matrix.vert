#version 450
/* vkmatrix vertex stage: fullscreen-quad passthrough.  The digital-rain
 * effect itself is computed on the CPU (matrix.c) into the sampled texture,
 * following the vkteapot CPU-shading precedent. */
layout(location = 0) in vec4 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 0) out vec2 vUV;
void main()
{
    gl_Position = aPos;
    vUV = aUV;
}