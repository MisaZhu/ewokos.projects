/*
Fork of "96-bit 8x12 Font" by Flyguy. https://shadertoy.com/view/Mt2GWD
Added some textmodes and improvements. 
Also converted the font to integers, to make use of binary operations.

Still WIP

*/

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    fragColor = texelFetch(iChannel0, ivec2(fragCoord), 0);
}