/*
 * olddisp.h - native port of the Shadertoy lc3BRn font engine
 * ("Improved 96-bit 8x12 Font", JoostAB, fork of Flyguy's Mt2GWD).
 *
 * Buffer A of the reference evaluates text() per pixel, but every glyph
 * stamps non-overlapping 8x12 sprites into a DOWN_SCALE=2 font grid, so
 * the whole pass reduces to stamping the same character sequence into a
 * coverage grid of (w+1)/2 x (h+1)/2 cells with values in {0,1,2}
 * (2 = GLOW).  The verbatim Buffer B + Image passes then run on the GPU /
 * SPIR-V interpreter reading this coverage as iChannel0 (see
 * shaders/vkolddisp.frag).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef VKOLDDISP_H
#define VKOLDDISP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   width, height;   /* screen size in pixels                    */
    float itime;           /* iTime: only drives the blink/blinvert phase */
    float date[4];         /* iDate: year, month-1, day, secs since midnight */
} olddisp_frame;

/* font-grid dimensions for a given screen size */
int olddisp_font_w(int width);
int olddisp_font_h(int height);

/*
 * Render the coverage grid: olddisp_font_w(w) * olddisp_font_h(h) bytes,
 * cell values 0..2, Shadertoy orientation (row 0 = bottom of the screen).
 */
void olddisp_render(const olddisp_frame *f, uint8_t *cov);

/*
 * Expand the coverage grid into a window-sized bottom-up RGBA8 texture
 * (row 0 = bottom, matching the Vulkan upload convention used by the app).
 * Encoding is lossless: R = (col >= 1), G = (col >= 2), so the shader can
 * reconstruct the exact float coverage including GLOW's 2.0.
 */
void olddisp_encode(const olddisp_frame *f, const uint8_t *cov, uint8_t *rgba);

#ifdef __cplusplus
}
#endif

#endif