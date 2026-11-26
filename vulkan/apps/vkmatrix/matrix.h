/*
 * matrix.h - CPU port of "Inside the Matrix" (Shadertoy 4t3BWl, by And390).
 *
 * The reference is a single-pass procedural fragment shader (no iChannel
 * inputs, only iTime / iResolution / iMouse).  This module reproduces its
 * algorithm in plain C so it can run at interactive rates: the EwokOS Vulkan
 * software device interprets SPIR-V instruction by instruction, which is far
 * too slow for a 40-iteration raymarcher with hundreds of sin() calls per
 * pixel.  The app therefore follows the vkteapot precedent (CPU-side shading
 * uploaded through trivial Vulkan shaders): matrix_render() fills an RGBA8
 * image that the Vulkan pipeline samples onto a fullscreen quad.
 *
 * Pure C, no EwokOS dependencies - also buildable as a host test/benchmark.
 */
#ifndef VKMATRIX_MATRIX_H
#define VKMATRIX_MATRIX_H

#include <stdint.h>

/* row bands may run concurrently on up to this many pthread workers; each
 * worker owns a private memo-cache set inside matrix.c */
#define MATRIX_MAX_WORKERS 8

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   width, height;   /* iResolution.xy (internal render resolution)   */
    float time;            /* iTime * SPEED                                 */
    float mouse[2];        /* iMouse.xy, GL bottom-left origin, in pixels;
                            * both <= 10 selects the auto demo camera       */
    int   iterations;      /* ITERATIONS, reference value 40                */
} matrix_frame;

/* Render one frame.  rgba is width*height uint32_t pixels, row 0 = TOP of the
 * image (the GL bottom-left fragCoord convention is flipped internally),
 * byte order R,G,B,A (alpha always 255). */
void matrix_render(const matrix_frame *f, uint32_t *rgba);

/* rows [y0, y1) only, with worker-local caches; see MATRIX_MAX_WORKERS */
void matrix_render_band(const matrix_frame *f, uint32_t *rgba,
                        int y0, int y1, int worker);

/* GPU path: fill params[15] for shaders/matrix.comp --
 * [0]=time [1]=width [2]=height [3..5]=camera ro [6..14]=rotation M
 * (column-major), built by the SAME camera_build the CPU bands use, so the
 * per-pixel rays are identical on both paths. */
void matrix_gpu_params(const matrix_frame *f, float *params);

#ifdef __cplusplus
}
#endif

#endif