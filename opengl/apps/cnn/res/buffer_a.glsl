// Buffer A: 32x32 ink canvas with mouse drawing
// Stores pen strokes and canvas metadata
// Self-feedback: reads previous frame to accumulate strokes

// Canvas pixel layout:
// - Pixels (0,0) to (31,31): ink intensity [0,1] in R channel
// - Pixel (32,0) if exists: metadata (not used in 32x32)
// Output: R = ink, G = stroke age, B = pressure, A = 1.0

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    ivec2 pixel = ivec2(fragCoord.xy);
    
    // Get canvas bounds in screen space
    vec4 bounds = canvasBounds(iResolution.xy);
    
    // Check if this pixel is within the canvas region
    bool inCanvas = fragCoord.x >= bounds.x && fragCoord.x < bounds.z &&
                    fragCoord.y >= bounds.y && fragCoord.y < bounds.w;
    
    // Read previous frame (self-feedback)
    vec4 prev = texture2D(iChannel0, fragCoord.xy / vec2(float(MODEL_SIDE), float(MODEL_SIDE)));
    
    // Default: keep previous ink
    float ink = prev.r;
    float age = prev.g;
    float pressure = prev.b;
    
    // Mouse drawing
    vec4 mouse = iMouse;
    bool mouseDown = mouse.z > 0.0;
    vec2 mousePos = mouse.xy;
    vec2 mouseClick = mouse.zw;
    
    if (mouseDown && inCanvas) {
        // Convert mouse position to canvas coordinates (0-31)
        vec2 canvasPos;
        canvasPos.x = (mousePos.x - bounds.x) / (bounds.z - bounds.x) * float(MODEL_SIDE);
        canvasPos.y = (mousePos.y - bounds.y) / (bounds.w - bounds.y) * float(MODEL_SIDE);
        
        // Check if mouse is over this pixel
        vec2 pixelCenter = vec2(float(pixel.x) + 0.5, float(pixel.y) + 0.5);
        float dist = length(canvasPos - pixelCenter);
        
        // Brush radius (1.5 pixels for smooth strokes)
        float brushRadius = 1.5;
        
        if (dist < brushRadius) {
            // Interpolate stroke from click position to current position
            vec2 clickCanvas;
            clickCanvas.x = (mouseClick.x - bounds.x) / (bounds.z - bounds.x) * float(MODEL_SIDE);
            clickCanvas.y = (mouseClick.y - bounds.y) / (bounds.w - bounds.y) * float(MODEL_SIDE);
            
            // Distance to stroke segment
            float segDist = segmentDist(pixelCenter, clickCanvas, canvasPos);
            
            if (segDist < brushRadius) {
                // Apply ink with falloff
                float falloff = 1.0 - smoothstep(0.0, brushRadius, segDist);
                ink = max(ink, falloff);
                age = 0.0; // Fresh stroke
                pressure = falloff;
            }
        }
    }
    
    // Age the ink slightly (fade over time if not redrawn)
    if (!mouseDown || !inCanvas) {
        age = min(age + iTimeDelta * 0.1, 1.0);
        // Very slow fade (optional, can be disabled)
        // ink *= 0.999;
    }
    
    // Clear button region (top-right corner of canvas)
    vec2 clearBtnCenter = vec2(float(MODEL_SIDE) - 3.0, float(MODEL_SIDE) - 3.0);
    float clearDist = length(vec2(float(pixel.x), float(pixel.y)) - clearBtnCenter);
    
    if (clearDist < 2.0 && mouseDown) {
        vec2 canvasPos;
        canvasPos.x = (mousePos.x - bounds.x) / (bounds.z - bounds.x) * float(MODEL_SIDE);
        canvasPos.y = (mousePos.y - bounds.y) / (bounds.w - bounds.y) * float(MODEL_SIDE);
        
        if (length(canvasPos - clearBtnCenter) < 2.5) {
            // Clear entire canvas
            ink = 0.0;
            age = 0.0;
            pressure = 0.0;
        }
    }
    
    // Output: R=ink, G=age, B=pressure, A=1
    fragColor = vec4(ink, age, pressure, 1.0);
}
