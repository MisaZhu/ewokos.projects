/* gles_engine.h - neutral internal API of the libGLESv2/libEGL front end.
 *
 * This header is the ONLY bridge between the standard-facing GLES2/EGL/GLSL
 * translation units and portablegl, and it exists because of two hard,
 * source-verified facts about the vendored portablegl:
 *
 *  1. GLES2/gl2.h and portablegl/portablegl.h CANNOT be included in the same
 *     translation unit.  portablegl declares GL_NO_ERROR as an *enum member*
 *     while gl2.h #defines it to 0; portablegl also reuses GL_STENCIL_BITS /
 *     GL_MAX_VERTEX_ATTRIBS as small integer limits where gl2.h gives them
 *     large query-token values.  Including both yields hard compile errors.
 *
 *  2. portablegl does NOT use the Khronos token values.  Every GL_* token is a
 *     member of one big auto-incrementing anonymous enum (GL_NO_ERROR=0,
 *     GL_ARRAY_BUFFER=6, ... GL_COLOR_BUFFER_BIT=1<<10, GL_DEPTH_BUFFER_BIT=
 *     1<<11).  A standard GL_ARRAY_BUFFER (0x8892) passed straight to
 *     pgl_BindBuffer() would be nonsense.  Every GLenum the front end receives
 *     must therefore be TRANSLATED from its standard value to portablegl's
 *     ordinal before the corresponding pgl_* call.
 *
 * The only TU allowed to include portablegl.h is src/engine/pgl_engine.c, so it
 * is also the only place that knows portablegl's ordinals; consequently ALL
 * translation lives there.  This header declares gles_engine_* wrappers that
 * take STANDARD token values and perform the mapping internally.
 *
 * To let BOTH pgl_engine.c (which sees portablegl.h) and the gles2/egl/glsl TUs
 * (which see gl2.h) include this one header -- so the wrapper definitions are
 * compile-time checked against their declarations -- the prototypes use
 * fixed-width neutral typedefs (GE*) rather than either header's GL types.  The
 * GE* types are ABI-identical to the GL scalar types in BOTH headers
 * (GLenum/GLbitfield=uint32, GLint/GLsizei=int32, GLuint=uint32, GLboolean=uint8,
 * GLfloat=float, GLintptr/GLsizeiptr=intptr), so passing a GLenum where a GEenum
 * is expected is an identity conversion.
 */
#ifndef GLES_ENGINE_H
#define GLES_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t GEenum;   /* GLenum / GLbitfield */
typedef int32_t  GEint;    /* GLint / GLsizei     */
typedef uint32_t GEuint;   /* GLuint              */
typedef uint8_t  GEbool;   /* GLboolean           */
typedef float    GEfloat;  /* GLfloat / GLclampf  */
typedef intptr_t GEiptr;   /* GLintptr / GLsizeiptr */

/* Opaque handle to a linked CPU GLSL ES program (defined in src/glsl/glsl.h).
 * Forward-declared so this header stays portablegl- and glsl-definition-free. */
struct glsl_program;

/* ------------------------------------------------------------------ *
 * Context lifecycle / back buffer (used by the EGL layer)
 * ------------------------------------------------------------------ */

/* Create + initialize the single portablegl glContext owned by the engine,
 * allocating an internal w x h back buffer.  Returns 1 on success. */
int  gles_engine_init(GEint w, GEint h);
/* Release the engine glContext and its back buffer. */
void gles_engine_fini(void);
/* Make the engine glContext the current portablegl context. */
void gles_engine_make_current(void);
/* Resize the engine back buffer.  Returns 1 on success. */
int  gles_engine_resize(GEint w, GEint h);

/* Back-buffer accessors for the EGL swap.  At 32 bpp the pixels are packed
 * u32 0xAARRGGBB, byte-identical to EwokOS graph_t's argb layout, so
 * eglSwapBuffers can blit directly. */
void *gles_engine_backbuffer(void);
void  gles_engine_backbuffer_size(GEint *w, GEint *h);
int   gles_engine_bitdepth(void);
/* Contig shm id of the back buffer when it is the adopted GPU render target
 * (PGL_G3D), else 0.  Lets the present blit go through g2d instead of a CPU
 * read of the NOCACHE target.  Query after gles_engine_backbuffer(). */
int   gles_engine_backbuffer_shm(void);

/* Bridge to the self-contained CPU GLSL ES 1.00 interpreter (src/glsl).
 * `gp` is an already-linked glsl_program (owned by the caller) and `ublock` is
 * its uniform block of `ublock_cells` cells (owned by the caller; the engine
 * only reads it while the program is active).  Creates a portablegl program
 * whose vert_func/frag_func trampolines drive the interpreter and returns the
 * portablegl program id (usable with gles_engine_use_program), or 0 on failure.
 * The engine keeps a registry entry keyed by that id so the shared trampolines
 * can find gp+ublock for the currently-active program. */
GEuint gles_engine_create_interpreted_program(struct glsl_program *gp,
                                              void *ublock, GEint ublock_cells);

/* Drop the registry entry for an interpreted program (called on glDeleteProgram).
 * Does NOT delete the portablegl program object itself. */
void gles_engine_release_interpreted_program(GEuint program);

/* Resolve the portablegl texture name currently bound to a texture unit.
 * Used by the interpreter's texture-sampling callback. */
GEuint gles_engine_texture_for_unit(GEint unit);

/* ------------------------------------------------------------------ *
 * GLES2 state / error / query wrappers (standard tokens in)
 * ------------------------------------------------------------------ */
void    gles_engine_enable(GEenum cap);
void    gles_engine_disable(GEenum cap);
GEbool  gles_engine_is_enabled(GEenum cap);
void    gles_engine_viewport(GEint x, GEint y, GEint w, GEint h);
void    gles_engine_scissor(GEint x, GEint y, GEint w, GEint h);
void    gles_engine_color_mask(GEbool r, GEbool g, GEbool b, GEbool a);
void    gles_engine_clear_color(GEfloat r, GEfloat g, GEfloat b, GEfloat a);
void    gles_engine_clear_depthf(GEfloat d);
void    gles_engine_clear_stencil(GEint s);
void    gles_engine_clear(GEenum mask);
void    gles_engine_depth_func(GEenum func);
void    gles_engine_depth_mask(GEbool flag);
void    gles_engine_blend_func(GEenum sfactor, GEenum dfactor);
void    gles_engine_blend_color(GEfloat r, GEfloat g, GEfloat b, GEfloat a);
void    gles_engine_cull_face(GEenum mode);
void    gles_engine_front_face(GEenum mode);
void    gles_engine_line_width(GEfloat w);
void    gles_engine_polygon_offset(GEfloat factor, GEfloat units);
void    gles_engine_stencil_func(GEenum func, GEint ref, GEuint mask);
void    gles_engine_stencil_op(GEenum sfail, GEenum dpfail, GEenum dppass);
void    gles_engine_stencil_mask(GEuint mask);
void    gles_engine_stencil_func_separate(GEenum face, GEenum func, GEint ref, GEuint mask);
void    gles_engine_stencil_op_separate(GEenum face, GEenum sfail, GEenum dpfail, GEenum dppass);
void    gles_engine_stencil_mask_separate(GEenum face, GEuint mask);
void    gles_engine_blend_func_separate(GEenum srgb, GEenum drgb, GEenum sa, GEenum da);
void    gles_engine_blend_equation(GEenum mode);
void    gles_engine_blend_equation_separate(GEenum rgb, GEenum alpha);
void    gles_engine_depth_rangef(GEfloat n, GEfloat f);
void    gles_engine_sample_coverage(GEfloat value, GEbool invert);
void    gles_engine_read_pixels(GEint x, GEint y, GEint w, GEint h,
                                GEenum format, GEenum type, void *pixels);

/* pgl_* error ordinals are translated back to standard GL error values. */
GEenum       gles_engine_get_error(void);
void         gles_engine_set_error(GEenum error);
const uint8_t *gles_engine_get_string(GEenum name);
void         gles_engine_get_integerv(GEenum pname, GEint *data);
void         gles_engine_get_floatv(GEenum pname, GEfloat *data);
void         gles_engine_get_booleanv(GEenum pname, GEbool *data);

/* ------------------------------------------------------------------ *
 * Buffers
 * ------------------------------------------------------------------ */
void gles_engine_gen_buffers(GEint n, GEuint *buffers);
void gles_engine_delete_buffers(GEint n, const GEuint *buffers);
void gles_engine_bind_buffer(GEenum target, GEuint buffer);
void gles_engine_buffer_data(GEenum target, GEiptr size, const void *data, GEenum usage);
void gles_engine_buffer_sub_data(GEenum target, GEiptr offset, GEiptr size, const void *data);
void gles_engine_get_buffer_parameteriv(GEenum target, GEenum pname, GEint *params);
GEbool gles_engine_is_buffer(GEuint buffer);

/* ------------------------------------------------------------------ *
 * Textures
 * ------------------------------------------------------------------ */
void gles_engine_gen_textures(GEint n, GEuint *textures);
void gles_engine_delete_textures(GEint n, const GEuint *textures);
void gles_engine_bind_texture(GEenum target, GEuint texture);
void gles_engine_active_texture(GEenum texture);
void gles_engine_tex_parameteri(GEenum target, GEenum pname, GEint param);
void gles_engine_tex_parameterf(GEenum target, GEenum pname, GEfloat param);
void gles_engine_pixel_storei(GEenum pname, GEint param);
void gles_engine_tex_image_2d(GEenum target, GEint level, GEint internalformat,
                              GEint w, GEint h, GEint border,
                              GEenum format, GEenum type, const void *data);
void gles_engine_tex_sub_image_2d(GEenum target, GEint level, GEint xoff, GEint yoff,
                                  GEint w, GEint h, GEenum format, GEenum type,
                                  const void *data);
void gles_engine_compressed_tex_image_2d(GEenum target, GEint level, GEenum internalformat,
                                         GEint w, GEint h, GEint border,
                                         GEint image_size, const void *data);
void gles_engine_compressed_tex_sub_image_2d(GEenum target, GEint level,
                                             GEint xoff, GEint yoff, GEint w, GEint h,
                                             GEenum format, GEint image_size,
                                             const void *data);
void gles_engine_copy_tex_image_2d(GEenum target, GEint level, GEenum internalformat,
                                   GEint x, GEint y, GEint w, GEint h, GEint border);
void gles_engine_copy_tex_sub_image_2d(GEenum target, GEint level,
                                       GEint xoff, GEint yoff, GEint x, GEint y,
                                       GEint w, GEint h);
void gles_engine_generate_mipmap(GEenum target);
void gles_engine_get_tex_parameteriv(GEenum target, GEenum pname, GEint *params);
void gles_engine_get_tex_parameterfv(GEenum target, GEenum pname, GEfloat *params);
GEbool gles_engine_is_texture(GEuint texture);
GEbool gles_engine_texture_complete(GEint unit, GEint cube);

/* ------------------------------------------------------------------ *
 * Vertex attributes / draw
 * ------------------------------------------------------------------ */
void gles_engine_vertex_attrib_pointer(GEuint index, GEint size, GEenum type,
                                       GEbool normalized, GEint stride,
                                       const void *pointer);
void gles_engine_enable_vertex_attrib_array(GEuint index);
void gles_engine_disable_vertex_attrib_array(GEuint index);
void gles_engine_vertex_attrib4f(GEuint index, const GEfloat value[4]);
void gles_engine_get_vertex_attribiv(GEuint index, GEenum pname, GEint *params);
void gles_engine_get_vertex_attribfv(GEuint index, GEenum pname, GEfloat *params);
void gles_engine_get_vertex_attrib_pointerv(GEuint index, GEenum pname, void **pointer);
void gles_engine_draw_arrays(GEenum mode, GEint first, GEint count);
void gles_engine_draw_elements(GEenum mode, GEint count, GEenum type, const void *indices);

/* ------------------------------------------------------------------ *
 * Programs
 * ------------------------------------------------------------------ */
void gles_engine_use_program(GEuint program);
void gles_engine_delete_program(GEuint program);
GEbool gles_engine_program_executable(GEuint program);

/* ------------------------------------------------------------------ *
 * Transparent GPU (V3D) offload bridge
 *
 * These are the ONLY GPU-facing entry points the standard-facing GLES2
 * TU calls, and they carry no GPU types - just the engine program id and
 * the two GLSL source strings (still alive at glLinkProgram time).  All
 * GPU logic lives inside pgl_engine.c under PGL_G3D; on a build without
 * PGL_G3D (or when the platform declines) they are inert and every draw
 * runs on the CPU interpreter exactly as before, so an app never has to
 * know whether the GPU was used.
 *
 * gles_engine_gpu_link: compile the linked program's VS/FS to V3D QPU and
 *   attach it to the engine program `pgl_id` if (and only if) it resolves
 *   to one of the fixed geometry kernels.  Returns non-zero when the GPU
 *   route is eligible.  Call right after a successful glLinkProgram.
 * gles_engine_gpu_delete: drop any GPU state attached to `pgl_id`
 *   (compiled program + cached meshes).  Call from glDeleteProgram.
 * gles_engine_gpu_flush: dispatch any draw the engine has coalesced but
 *   not yet submitted (the GPU path merges consecutive same-state draws
 *   into one dispatch).  Inert without PGL_G3D.  Call before presenting
 *   a frame (eglSwapBuffers) or reading pixels so nothing is left pending.
 * gles_engine_gpu_active: non-zero when the currently bound program's draws
 *   are running on the V3D GPU (a compiled geometry kernel is attached and the
 *   program is not latched to the CPU); zero when rendering is on the CPU
 *   interpreter or the build has no PGL_G3D.  A diagnostic for on-screen HUDs;
 *   it never influences routing.
 * gles_engine_force_cpu: pin the whole context to the CPU rasteriser.  A
 *   pure-CPU app (the fullscreen-quad texblit of Fireworks/matrix) must never
 *   adopt the pgl_g3d NOCACHE render target as portablegl's back buffer: that
 *   target is uncached shm, so blitting a full frame into it every refresh is
 *   DRAM-bound and ~an order of magnitude slower than the cached malloc back
 *   buffer.  Linking a GPU-eligible overlay program (e.g. Dear ImGui's uMVP
 *   shader) would otherwise flip g_gpu_has_eligible and trigger that adoption,
 *   mixing a CPU blit with GPU tile dispatches on one surface.  Call this once
 *   before the render loop so gpu_bringup stays inert and the overlay too draws
 *   through the CPU interpreter into the same cached buffer.  Inert without
 *   PGL_G3D.
 * gles_engine_force_cpu_raster: a NARROWER lever for an app whose main scene is
 *   a fullscreen-compute effect (VS has no uMVP + FS reads gl_FragCoord, e.g.
 *   waterdrop/shadertoy) that must stay on the V3D GPU (its CPU path is
 *   ~35s/frame) yet also overlays a GPU-eligible raster HUD (Dear ImGui).  It
 *   declines ONLY the mesh/texcol raster dispatches to the CPU interpreter -- so
 *   the HUD composites into the same NOCACHE fb the GPU scene wrote (the path the
 *   glcommon CPU hud already uses) -- while gpu_try_fullscreen keeps the scene on
 *   the GPU and gpu_bringup/g_gpu_ready are untouched.  Call once before the
 *   render loop.  Inert without PGL_G3D.
 * ------------------------------------------------------------------ */
GEuint gles_engine_gpu_link(GEuint pgl_id, const char *vs_src, const char *fs_src);
void   gles_engine_gpu_delete(GEuint pgl_id);
void   gles_engine_gpu_flush(void);
int    gles_engine_gpu_active(void);
void   gles_engine_force_cpu(void);
void   gles_engine_force_cpu_raster(void);
/* gles_engine_set_gpu_source: compile a DIFFERENT (typically far heavier) pair
 * of shader sources on the on-device mesa compiler than the ones the CPU
 * interpreter linked, for apps whose GPU-only effect exceeds the interpreter's
 * per-function local cap or per-pixel budget.  The interpreted program keeps
 * owning the uniform metadata, so both sources must declare the same uniform
 * names.  NULL fs clears.  Inert without PGL_G3D. */
void   gles_engine_set_gpu_source(const char *vs_src, const char *fs_src);

#ifdef __cplusplus
}
#endif

#endif /* GLES_ENGINE_H */
