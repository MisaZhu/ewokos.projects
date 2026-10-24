// Buffer D: Simplified FC Classifier (GPU/CPU optimized)
// Reduced complexity: 8 feature samples instead of 96, faster softmax
// Layout: 64x64 buffer
//   - Row 0-25: class logits
//   - Row 26: softmax probabilities (packed 4 per pixel)
//   - Row 27: top prediction

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    ivec2 pixel = ivec2(fragCoord.xy);
    int x = pixel.x;
    int y = pixel.y;
    
    vec4 prev = texture2D(iChannel3, fragCoord.xy / 64.0);
    
    if (y < 26) {
        // Simplified logit: sample only 8 key positions from Buffer C
        float logit = 0.0;
        
        for (int i = 0; i < 8; i++) {
            int sx = (i & 3) * 8;
            int sy = (i >> 2) * 16;
            vec2 sampleUV = vec2(float(sx) + 0.5, float(16 + sy) + 0.5) / 32.0;
            vec4 feat = texture2D(iChannel2, sampleUV);
            
            // Simple procedural weight based on class and position
            float w = proceduralWeight(3, y, i, 0);
            logit += (feat.r + feat.g + feat.b + feat.a) * 0.25 * w;
        }
        
        logit += proceduralBias(3, y);
        fragColor = vec4(logit * 0.5, 0.0, 0.0, 1.0);
        
    } else if (y == 26) {
        // Fast softmax: single pass approximation
        int classStart = x * 4;
        
        // Find max (read 26 logits)
        float maxLogit = -10.0;
        for (int i = 0; i < 26; i++) {
            float l = texture2D(iChannel3, vec2(0.5, float(i) + 0.5) / 64.0).r;
            if (l > maxLogit) maxLogit = l;
        }
        
        // Compute exp sum
        float sumExp = 0.0;
        for (int i = 0; i < 26; i++) {
            float l = texture2D(iChannel3, vec2(0.5, float(i) + 0.5) / 64.0).r;
            sumExp += exp(l - maxLogit);
        }
        
        // Output 4 probabilities
        vec4 probs = vec4(0.0);
        float invSum = 1.0 / max(sumExp, 0.001);
        
        if (classStart < 26) {
            float l = texture2D(iChannel3, vec2(0.5, float(classStart) + 0.5) / 64.0).r;
            probs.r = exp(l - maxLogit) * invSum;
        }
        if (classStart + 1 < 26) {
            float l = texture2D(iChannel3, vec2(0.5, float(classStart + 1) + 0.5) / 64.0).r;
            probs.g = exp(l - maxLogit) * invSum;
        }
        if (classStart + 2 < 26) {
            float l = texture2D(iChannel3, vec2(0.5, float(classStart + 2) + 0.5) / 64.0).r;
            probs.b = exp(l - maxLogit) * invSum;
        }
        if (classStart + 3 < 26) {
            float l = texture2D(iChannel3, vec2(0.5, float(classStart + 3) + 0.5) / 64.0).r;
            probs.a = exp(l - maxLogit) * invSum;
        }
        
        fragColor = mix(prev, probs, 0.5);
        
    } else if (y == 27 && x == 0) {
        // Top prediction
        float maxProb = 0.0;
        int topClass = 0;
        
        for (int i = 0; i < 7; i++) {
            vec4 p = texture2D(iChannel3, vec2(float(i) + 0.5, 26.5) / 64.0);
            if (p.r > maxProb && i * 4 < 26) { maxProb = p.r; topClass = i * 4; }
            if (p.g > maxProb && i * 4 + 1 < 26) { maxProb = p.g; topClass = i * 4 + 1; }
            if (p.b > maxProb && i * 4 + 2 < 26) { maxProb = p.b; topClass = i * 4 + 2; }
            if (p.a > maxProb && i * 4 + 3 < 26) { maxProb = p.a; topClass = i * 4 + 3; }
        }
        
        fragColor = vec4(float(topClass) / 255.0, maxProb, 0.0, 1.0);
        
    } else {
        fragColor = vec4(0.0);
    }
}
