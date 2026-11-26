#version 450
/* Fullscreen triangle: three vec2 corners staged in a vertex buffer (the CPU
 * rasteriser clips the oversized triangle to the viewport, exactly like the
 * GL shadertoy harness' gl_VertexID trick). */
layout(location = 0) in vec2 aPos;

void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
}