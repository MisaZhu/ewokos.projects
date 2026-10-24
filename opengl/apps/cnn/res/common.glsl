// Common: Shared utilities for CNN Letter Recognizer
// Original implementation inspired by CNN visualization shaders

// Model architecture constants
#define MODEL_SIDE    32      // Input canvas size (32x32)
#define CLASS_COUNT   26      // A-Z letters
#define FIRST_MAPS    12      // Conv1 output channels
#define SECOND_MAPS   24      // Conv2 output channels
#define POOL_SIZE     2       // Max pooling kernel size

// UI layout constants
#define CANVAS_MARGIN   0.08
#define CANVAS_SIZE     0.35
#define GRID_MARGIN     0.02
#define PRED_HEIGHT     0.12

// ============================================================
// UI Layout Helpers
// ============================================================

// Canvas bounds (left side of screen where user draws)
vec4 canvasBounds(vec2 resolution) {
    float aspect = resolution.x / resolution.y;
    float size = min(CANVAS_SIZE, CANVAS_SIZE * aspect);
    float x0 = CANVAS_MARGIN * resolution.x;
    float y0 = (1.0 - CANVAS_MARGIN - size) * resolution.y;
    return vec4(x0, y0, x0 + size * resolution.y, y0 + size * resolution.y);
}

// Class probability grid bounds (right side)
vec4 classGridBounds(vec2 resolution) {
    vec4 canvas = canvasBounds(resolution);
    float x0 = canvas.z + GRID_MARGIN * resolution.x;
    float y0 = PRED_HEIGHT * resolution.y;
    float x1 = resolution.x - GRID_MARGIN * resolution.x;
    float y1 = resolution.y - GRID_MARGIN * resolution.y;
    return vec4(x0, y0, x1, y1);
}

// ============================================================
// SDF Utilities
// ============================================================

// Signed distance to a rounded box
float roundedBoxDist(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

// Signed distance to a line segment
float segmentDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Signed distance to a circle
float circleDist(vec2 p, vec2 center, float radius) {
    return length(p - center) - radius;
}

// ============================================================
// Color Helpers
// ============================================================

// HSV to RGB conversion
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Class color palette (hue rotation across 26 classes)
vec3 classColor(int classIdx) {
    float hue = float(classIdx) / float(CLASS_COUNT);
    return hsv2rgb(vec3(hue, 0.7, 0.9));
}

// Activation colormap (blue -> cyan -> green -> yellow -> red)
vec3 activationColor(float value) {
    value = clamp(value, 0.0, 1.0);
    if (value < 0.25) {
        return mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.5, 1.0), value * 4.0);
    } else if (value < 0.5) {
        return mix(vec3(0.0, 0.5, 1.0), vec3(0.0, 1.0, 0.0), (value - 0.25) * 4.0);
    } else if (value < 0.75) {
        return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (value - 0.5) * 4.0);
    } else {
        return mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (value - 0.75) * 4.0);
    }
}

// Signed activation colormap (blue for negative, red for positive)
vec3 signedActivationColor(float value) {
    if (value >= 0.0) {
        return mix(vec3(0.1), vec3(1.0, 0.2, 0.2), clamp(value, 0.0, 1.0));
    } else {
        return mix(vec3(0.1), vec3(0.2, 0.4, 1.0), clamp(-value, 0.0, 1.0));
    }
}

// ============================================================
// 5x7 Bitmap Glyph Tables for A-Z (GPU-compatible, no arrays)
// Each glyph encoded as 7 rows of 5 bits
// ============================================================

// Get glyph row bits for a letter (GPU-compatible if-else chain)
int getGlyphRow(int letterIdx, int row) {
    if (row < 0 || row >= 7) return 0;
    
    // Letter A
    if (letterIdx == 0) {
        if (row == 0) return 0x0E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x1F; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x11;
    }
    // Letter B
    if (letterIdx == 1) {
        if (row == 0) return 0x1E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x1E; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x1E;
    }
    // Letter C
    if (letterIdx == 2) {
        if (row == 0) return 0x0E; if (row == 1) return 0x11; if (row == 2) return 0x10;
        if (row == 3) return 0x10; if (row == 4) return 0x10; if (row == 5) return 0x11;
        return 0x0E;
    }
    // Letter D
    if (letterIdx == 3) {
        if (row == 0) return 0x1E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x11; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x1E;
    }
    // Letter E
    if (letterIdx == 4) {
        if (row == 0) return 0x1F; if (row == 1) return 0x10; if (row == 2) return 0x10;
        if (row == 3) return 0x1E; if (row == 4) return 0x10; if (row == 5) return 0x10;
        return 0x1F;
    }
    // Letter F
    if (letterIdx == 5) {
        if (row == 0) return 0x1F; if (row == 1) return 0x10; if (row == 2) return 0x10;
        if (row == 3) return 0x1E; if (row == 4) return 0x10; if (row == 5) return 0x10;
        return 0x10;
    }
    // Letter G
    if (letterIdx == 6) {
        if (row == 0) return 0x0E; if (row == 1) return 0x11; if (row == 2) return 0x10;
        if (row == 3) return 0x17; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x0F;
    }
    // Letter H
    if (letterIdx == 7) {
        if (row == 0) return 0x11; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x1F; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x11;
    }
    // Letter I
    if (letterIdx == 8) {
        if (row == 0) return 0x0E; if (row == 1) return 0x04; if (row == 2) return 0x04;
        if (row == 3) return 0x04; if (row == 4) return 0x04; if (row == 5) return 0x04;
        return 0x0E;
    }
    // Letter J
    if (letterIdx == 9) {
        if (row == 0) return 0x07; if (row == 1) return 0x02; if (row == 2) return 0x02;
        if (row == 3) return 0x02; if (row == 4) return 0x02; if (row == 5) return 0x12;
        return 0x0C;
    }
    // Letter K
    if (letterIdx == 10) {
        if (row == 0) return 0x11; if (row == 1) return 0x12; if (row == 2) return 0x14;
        if (row == 3) return 0x18; if (row == 4) return 0x14; if (row == 5) return 0x12;
        return 0x11;
    }
    // Letter L
    if (letterIdx == 11) {
        if (row == 0) return 0x10; if (row == 1) return 0x10; if (row == 2) return 0x10;
        if (row == 3) return 0x10; if (row == 4) return 0x10; if (row == 5) return 0x10;
        return 0x1F;
    }
    // Letter M
    if (letterIdx == 12) {
        if (row == 0) return 0x11; if (row == 1) return 0x1B; if (row == 2) return 0x15;
        if (row == 3) return 0x15; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x11;
    }
    // Letter N
    if (letterIdx == 13) {
        if (row == 0) return 0x11; if (row == 1) return 0x19; if (row == 2) return 0x15;
        if (row == 3) return 0x13; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x11;
    }
    // Letter O
    if (letterIdx == 14) {
        if (row == 0) return 0x0E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x11; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x0E;
    }
    // Letter P
    if (letterIdx == 15) {
        if (row == 0) return 0x1E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x1E; if (row == 4) return 0x10; if (row == 5) return 0x10;
        return 0x10;
    }
    // Letter Q
    if (letterIdx == 16) {
        if (row == 0) return 0x0E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x11; if (row == 4) return 0x15; if (row == 5) return 0x12;
        return 0x0D;
    }
    // Letter R
    if (letterIdx == 17) {
        if (row == 0) return 0x1E; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x1E; if (row == 4) return 0x14; if (row == 5) return 0x12;
        return 0x11;
    }
    // Letter S
    if (letterIdx == 18) {
        if (row == 0) return 0x0F; if (row == 1) return 0x10; if (row == 2) return 0x10;
        if (row == 3) return 0x0E; if (row == 4) return 0x01; if (row == 5) return 0x01;
        return 0x1E;
    }
    // Letter T
    if (letterIdx == 19) {
        if (row == 0) return 0x1F; if (row == 1) return 0x04; if (row == 2) return 0x04;
        if (row == 3) return 0x04; if (row == 4) return 0x04; if (row == 5) return 0x04;
        return 0x04;
    }
    // Letter U
    if (letterIdx == 20) {
        if (row == 0) return 0x11; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x11; if (row == 4) return 0x11; if (row == 5) return 0x11;
        return 0x0E;
    }
    // Letter V
    if (letterIdx == 21) {
        if (row == 0) return 0x11; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x11; if (row == 4) return 0x11; if (row == 5) return 0x0A;
        return 0x04;
    }
    // Letter W
    if (letterIdx == 22) {
        if (row == 0) return 0x11; if (row == 1) return 0x11; if (row == 2) return 0x11;
        if (row == 3) return 0x15; if (row == 4) return 0x15; if (row == 5) return 0x1B;
        return 0x11;
    }
    // Letter X
    if (letterIdx == 23) {
        if (row == 0) return 0x11; if (row == 1) return 0x11; if (row == 2) return 0x0A;
        if (row == 3) return 0x04; if (row == 4) return 0x0A; if (row == 5) return 0x11;
        return 0x11;
    }
    // Letter Y
    if (letterIdx == 24) {
        if (row == 0) return 0x11; if (row == 1) return 0x11; if (row == 2) return 0x0A;
        if (row == 3) return 0x04; if (row == 4) return 0x04; if (row == 5) return 0x04;
        return 0x04;
    }
    // Letter Z
    if (letterIdx == 25) {
        if (row == 0) return 0x1F; if (row == 1) return 0x01; if (row == 2) return 0x02;
        if (row == 3) return 0x04; if (row == 4) return 0x08; if (row == 5) return 0x10;
        return 0x1F;
    }
    return 0;
}

// Get glyph pixel (returns 1.0 if pixel is set, 0.0 otherwise)
float glyphPixel(int letterIdx, int row, int col) {
    if (letterIdx < 0 || letterIdx >= 26) return 0.0;
    if (row < 0 || row >= 7 || col < 0 || col >= 5) return 0.0;
    
    int bits = getGlyphRow(letterIdx, row);
    return float((bits >> col) & 1);
}

// Render a glyph centered at position with given scale
float renderGlyph(vec2 uv, int letterIdx, vec2 center, float scale) {
    vec2 p = (uv - center) / scale;
    p.y = -p.y; // Flip Y for text rendering
    p += vec2(2.5, 3.5); // Center the 5x7 glyph
    
    if (p.x < 0.0 || p.x >= 5.0 || p.y < 0.0 || p.y >= 7.0) return 0.0;
    
    int col = int(floor(p.x));
    int row = int(floor(p.y));
    return glyphPixel(letterIdx, row, col);
}

// ============================================================
// Hash Functions (for procedural weight generation)
// ============================================================

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// Procedural weight generator (deterministic based on indices)
float proceduralWeight(int layer, int outIdx, int inIdx, int kernelPos) {
    float seed = float(layer) * 10000.0 + float(outIdx) * 100.0 + float(inIdx) * 10.0 + float(kernelPos);
    return (hash11(seed) - 0.5) * 0.5; // Range: [-0.25, 0.25]
}

float proceduralBias(int layer, int idx) {
    float seed = float(layer) * 1000.0 + float(idx);
    return (hash11(seed) - 0.5) * 0.1; // Range: [-0.05, 0.05]
}
