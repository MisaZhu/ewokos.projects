// Image: single GPU fullscreen visualization pass
// Samples the four CPU-staged CNN buffers (A ink, B conv1, C conv2, D classifier)
// Layout bands (uv, y-up): header >=0.90; panels 0.06..0.88; no overlaps.
// Kept lean so the V3D register allocator fits without spills.

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    vec3 col = mix(vec3(0.08, 0.09, 0.12), vec3(0.12, 0.14, 0.18), uv.y);

    if (uv.x >= 0.02 && uv.x < 0.36 && uv.y >= 0.06 && uv.y < 0.88) {
        // Ink canvas
        vec2 c = vec2((uv.x - 0.02) / 0.34, (uv.y - 0.06) / 0.82);
        float ink = texture2D(iChannel0, c).r;
        vec2 g = fract(c * 32.0);
        float gl2 = step(0.95, g.x) + step(0.95, g.y);
        col = mix(vec3(0.15), vec3(0.2), gl2 * 0.3);
        col = mix(col, vec3(1.0), ink);
        float b = min(min(c.x, 1.0 - c.x), min(c.y, 1.0 - c.y));
        if (b < 0.02) col = vec3(0.4, 0.6, 0.9);
    } else if (uv.x >= 0.40 && uv.x < 0.64 && uv.y >= 0.48 && uv.y < 0.88) {
        // Conv1 feature maps (raw region of B)
        vec2 f = vec2((uv.x - 0.40) / 0.24, (uv.y - 0.48) / 0.40);
        vec3 t = texture2D(iChannel1, vec2(f.x, f.y * 0.5)).rgb;
        col = t;
        vec2 g = fract(f * vec2(4.0, 3.0));
        float b = step(g.x, 0.03) + step(1.0 - g.x, 0.03) +
                  step(g.y, 0.03) + step(1.0 - g.y, 0.03);
        col = mix(col, vec3(0.3), b);
    } else if (uv.x >= 0.40 && uv.x < 0.64 && uv.y >= 0.06 && uv.y < 0.46) {
        // Conv2 feature maps (raw region of C)
        vec2 f = vec2((uv.x - 0.40) / 0.24, (uv.y - 0.06) / 0.40);
        vec3 t = texture2D(iChannel2, vec2(f.x, f.y * 0.5)).rgb;
        col = t;
        vec2 g = fract(f * vec2(6.0, 4.0));
        float b = step(g.x, 0.03) + step(1.0 - g.x, 0.03) +
                  step(g.y, 0.03) + step(1.0 - g.y, 0.03);
        col = mix(col, vec3(0.3), b);
    } else if (uv.x >= 0.68 && uv.x < 0.98 && uv.y >= 0.06 && uv.y < 0.88) {
        // Class probability grid
        vec2 gu = vec2((uv.x - 0.68) / 0.30, (uv.y - 0.06) / 0.82);
        int cidx = int(gu.x * 6.0) + int((1.0 - gu.y) * 5.0) * 6;
        if (cidx < 26) {
            vec2 cell = fract(vec2(gu.x * 6.0, (1.0 - gu.y) * 5.0));
            int px = cidx >> 2;
            int ch = cidx & 3;
            vec4 p = texture2D(iChannel3, vec2(float(px) + 0.5, 26.5) / 64.0);
            float prob = ch == 0 ? p.r : ch == 1 ? p.g : ch == 2 ? p.b : p.a;
            vec3 cc = vec3(0.2, 0.5, 0.9) * 0.3;
            if (cell.y < prob) cc = vec3(0.2, 0.5, 0.9);
            float b = step(cell.x, 0.04) + step(1.0 - cell.x, 0.04) +
                      step(cell.y, 0.04) + step(1.0 - cell.y, 0.04);
            col = mix(cc, vec3(0.2), b);
        }
    } else if (uv.y >= 0.90) {
        // Header: clear button, recognized letter glyph, confidence bar
        vec2 bc = vec2(iResolution.x * 0.045, iResolution.y * 0.95);
        if (length(fragCoord - bc) < 12.0) col = vec3(0.8, 0.3, 0.3);
        vec2 bd = abs(fragCoord - bc);
        if (abs(bd.x - bd.y) < 2.0 && length(fragCoord - bc) < 8.0) col = vec3(1.0);
        vec4 pd = texture2D(iChannel3, vec2(0.5, 27.5) / 64.0);
        int topClass = int(pd.r * 255.0);
        float conf = pd.g;
        vec2 lpos = vec2(iResolution.x * 0.13, iResolution.y * 0.95);
        float ls = iResolution.y * 0.011;
        /* glyph font bit 4 is the LEFT column: mirror columns for display */
        vec2 gp = (fragCoord - lpos) / ls;
        gp.y = -gp.y;
        gp += vec2(2.5, 3.5);
        float g = 0.0;
        if (gp.x >= 0.0 && gp.x < 5.0 && gp.y >= 0.0 && gp.y < 7.0)
            g = glyphPixel(topClass, int(floor(gp.y)), 4 - int(floor(gp.x)));
        if (g > 0.5) col = vec3(0.4, 1.0, 0.55);
        if (uv.x > 0.22 && uv.x < 0.55 && uv.y > 0.93 && uv.y < 0.97) {
            float bx = (uv.x - 0.22) / 0.33;
            col = bx < conf ? vec3(0.3, 0.9, 0.4) : vec3(0.2);
        }
    }

    fragColor = vec4(col, 1.0);
}
