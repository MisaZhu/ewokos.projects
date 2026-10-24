// Buffer B: Simplified Conv1 (CPU/GPU optimized)
// Reduced from 12 to 4 feature maps packed in RGBA
// Single 3x3 conv sample per pixel

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    ivec2 pixel = ivec2(fragCoord.xy);
    int x = pixel.x;
    int y = pixel.y;
    
    if (y < 32) {
        // Raw conv output: 4 feature maps in RGBA
        vec2 inputUV = (vec2(float(x), float(y)) + 0.5) / 32.0;
        
        // Simple 3x3 conv with 4 kernels
        vec4 sum = vec4(0.0);
        for (int ky = 0; ky < 3; ky++) {
            for (int kx = 0; kx < 3; kx++) {
                vec2 sUV = inputUV + vec2(float(kx - 1), float(ky - 1)) / 32.0;
                float ink = texture2D(iChannel0, sUV).r;
                int kp = ky * 3 + kx;
                sum.r += ink * proceduralWeight(1, 0, 0, kp);
                sum.g += ink * proceduralWeight(1, 1, 0, kp);
                sum.b += ink * proceduralWeight(1, 2, 0, kp);
                sum.a += ink * proceduralWeight(1, 3, 0, kp);
            }
        }
        
        // Bias + ReLU
        sum.r = max(sum.r + proceduralBias(1, 0), 0.0);
        sum.g = max(sum.g + proceduralBias(1, 1), 0.0);
        sum.b = max(sum.b + proceduralBias(1, 2), 0.0);
        sum.a = max(sum.a + proceduralBias(1, 3), 0.0);
        
        fragColor = sum;
        
    } else if (y < 48) {
        // Pooled output (16x16) - 2x2 max pool
        int py = y - 32;
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
