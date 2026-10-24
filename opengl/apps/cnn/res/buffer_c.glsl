// Buffer C: Simplified Conv2 (CPU/GPU optimized)
// Reduced complexity, single pass

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    ivec2 pixel = ivec2(fragCoord.xy);
    int x = pixel.x;
    int y = pixel.y;
    
    if (y < 16) {
        // Raw conv output with skip connection
        vec2 inputUV = (vec2(float(x), float(y)) * 2.0 + 0.5) / 32.0;
        inputUV.y = (inputUV.y + 32.0) / 64.0;  // Offset to Buffer B pooled region
        
        vec4 input = texture2D(iChannel0, inputUV);
        
        // Simple 3x3 conv
        vec4 sum = vec4(0.0);
        for (int ky = 0; ky < 3; ky++) {
            for (int kx = 0; kx < 3; kx++) {
                vec2 sUV = inputUV + vec2(float(kx - 1), float(ky - 1)) / 32.0;
                vec4 s = texture2D(iChannel0, sUV);
                int kp = ky * 3 + kx;
                sum.r += s.r * proceduralWeight(2, 0, 0, kp);
                sum.g += s.g * proceduralWeight(2, 1, 1, kp);
                sum.b += s.b * proceduralWeight(2, 2, 2, kp);
                sum.a += s.a * proceduralWeight(2, 3, 3, kp);
            }
        }
        
        // Skip connection + bias + ReLU
        sum = max(sum + input * 0.5 + vec4(proceduralBias(2, 0), proceduralBias(2, 1), 
                  proceduralBias(2, 2), proceduralBias(2, 3)), vec4(0.0));
        
        fragColor = sum;
        
    } else if (y < 24) {
        // Pooled output (8x8)
        int py = y - 16;
        vec2 baseUV = vec2(float(x * 2), float(py * 2)) / 32.0;
        
        vec4 s0 = texture2D(iChannel0, baseUV + vec2(0.5, 0.5) / 32.0);
        vec4 s1 = texture2D(iChannel0, baseUV + vec2(1.5, 0.5) / 32.0);
        vec4 s2 = texture2D(iChannel0, baseUV + vec2(0.5, 1.5) / 32.0);
        vec4 s3 = texture2D(iChannel0, baseUV + vec2(1.5, 1.5) / 32.0);
        
        fragColor = max(max(s0, s1), max(s2, s3));
        
    } else {
        fragColor = vec4(0.0);
    }
}
