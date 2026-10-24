#ifndef HUD_H
#define HUD_H

/* Tiny self-contained GLES2 heads-up-display text overlay.
 *
 * libEGL owns on_repaint once a window surface exists, so an app can no longer
 * blit a font into the framebuffer after present.  This helper instead renders
 * text INTO the GLES2 back buffer, before eglSwapBuffers, using an embedded 8x8
 * bitmap font atlas and a textured-quad shader of its own.  It is deliberately
 * isolated from the app's scene state:
 *   - the font atlas lives on texture unit 7 (apps use unit 0),
 *   - it owns its VBO and shader program, and queries (rather than assumes)
 *     the vertex-attribute locations the linker assigned; for its two
 *     attributes these are 0 and 1, which is safe because every consumer
 *     re-points its own attribs 0/1 immediately before each scene draw,
 *   - hud_draw saves/restores the viewport, blend/depth/cull enables, active
 *     texture unit and array-buffer binding, and re-binds the caller's program.
 * The HUD program declares no uMVP, so the engine's transparent GPU bridge
 * never claims it (see gles_engine_gpu_link): the overlay always rasterizes on
 * the CPU interpreter, which keeps it correct on both virt and raspi5 builds.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <GLES2/gl2.h>

/* Build the font atlas texture, shader program and VBO.  Call once after the
 * EGL context is current (e.g. at the end of the app's init). */
void hud_init(void);

/* Draw NUL-terminated `text` with the top-left of its first glyph at pixel
 * (x, y) (origin top-left, y down) in a window of size (win_w, win_h).
 * `restore_program` is the app's scene program handle; hud_draw re-binds it
 * before returning so the next frame's uniform writes and draws target the
 * right program.  Call between the scene draw and eglSwapBuffers.  Query
 * glGpuActiveEWOK() BEFORE this call, while the scene program is still bound. */
void hud_draw(GLuint restore_program, int win_w, int win_h, int x, int y,
              const char *text);

/* Override the text colour (default opaque white).  Components in [0,1]. */
void hud_set_color(float r, float g, float b, float a);

#ifdef __cplusplus
}
#endif

#endif /* HUD_H */
