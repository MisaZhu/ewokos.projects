/*
Some effects to make life more beautiful!
*/
//#define EFF_GRID
#define EFF_SCAN


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec3 col = texelFetch(iChannel0, ivec2(fragCoord), 0).rgb;
    
    #ifdef EFF_GRID
        // Add a small grid
        vec2 uv = fragCoord.xy / DOWN_SCALE;
        col *= (1.-distance(mod(uv,vec2(1.0)),vec2(0.65)))*1.2;
    #endif
    
    #ifdef EFF_SCAN
        // Add scanlines, but only if DOWN_SCALE is a whole number and > 1.0
        if (DOWN_SCALE > 1.0 && (floor(DOWN_SCALE) == DOWN_SCALE)) {
            col -= mod(fragCoord.y, 2.0) < 1.0 ? 0.5 : 0.0;
        }
    #endif
    
    fragColor = vec4(col,1.0);
}