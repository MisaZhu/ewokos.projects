/* gles2_api.c - the public libGLESv2 entry points.
 *
 * This TU sees ONLY the standard Khronos headers (<GLES2/gl2.h>) plus the
 * neutral engine API (gles_engine.h).  It must never include portablegl.h.
 * Each gl* function is a thin forwarder: it hands the standard token values to
 * the corresponding gles_engine_* wrapper, which does the portablegl
 * translation and dispatch inside the engine TU.
 *
 * Phase 4 covers the fixed-function state/draw/texture/buffer surface below.
 * The shader/program/uniform object model (glCreateShader, glLinkProgram,
 * glUniform*, ...) is added in Phase 5 once the CPU GLSL ES interpreter lands.
 */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <stddef.h>

#include "../engine/gles_engine.h"

/* --- state --------------------------------------------------------- */
void glEnable (GLenum cap)  { gles_engine_enable(cap); }
void glDisable (GLenum cap) { gles_engine_disable(cap); }
GLboolean glIsEnabled (GLenum cap) { return gles_engine_is_enabled(cap); }

void glViewport (GLint x, GLint y, GLsizei w, GLsizei h) { gles_engine_viewport(x, y, w, h); }
void glScissor (GLint x, GLint y, GLsizei w, GLsizei h)  { gles_engine_scissor(x, y, w, h); }
void glColorMask (GLboolean r, GLboolean g, GLboolean b, GLboolean a) { gles_engine_color_mask(r, g, b, a); }

void glClearColor (GLfloat r, GLfloat g, GLfloat b, GLfloat a) { gles_engine_clear_color(r, g, b, a); }
void glClearDepthf (GLfloat d) { gles_engine_clear_depthf(d); }
void glClearStencil (GLint s)  { gles_engine_clear_stencil(s); }
void glClear (GLbitfield mask) { gles_engine_clear(mask); }

void glDepthFunc (GLenum func) { gles_engine_depth_func(func); }
void glDepthMask (GLboolean flag) { gles_engine_depth_mask(flag); }
void glBlendFunc (GLenum sfactor, GLenum dfactor) { gles_engine_blend_func(sfactor, dfactor); }
void glBlendColor (GLfloat r, GLfloat g, GLfloat b, GLfloat a) { gles_engine_blend_color(r, g, b, a); }
void glCullFace (GLenum mode)  { gles_engine_cull_face(mode); }
void glFrontFace (GLenum mode) { gles_engine_front_face(mode); }
void glLineWidth (GLfloat w)   { gles_engine_line_width(w); }
void glPolygonOffset (GLfloat factor, GLfloat units) { gles_engine_polygon_offset(factor, units); }

/* EWOK_gpu_path: which rasterizer is drawing the bound program right now. */
GLint glGpuActiveEWOK(void) { return (GLint)gles_engine_gpu_active(); }

/* EWOK_gpu_path: pin the whole context to the CPU rasterizer (see gl2ext.h).
 * A pure-CPU blit app calls this before its render loop so an overlaid
 * GPU-eligible program (Dear ImGui) does not trigger adoption of the uncached
 * GPU back buffer and split the frame. */
void glForceCpuEWOK(void) { gles_engine_force_cpu(); }

void glStencilFunc (GLenum func, GLint ref, GLuint mask) { gles_engine_stencil_func(func, ref, mask); }
void glStencilOp (GLenum fail, GLenum zfail, GLenum zpass) { gles_engine_stencil_op(fail, zfail, zpass); }
void glStencilMask (GLuint mask) { gles_engine_stencil_mask(mask); }
void glStencilFuncSeparate (GLenum face, GLenum func, GLint ref, GLuint mask) { gles_engine_stencil_func_separate(face, func, ref, mask); }
void glStencilOpSeparate (GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { gles_engine_stencil_op_separate(face, sfail, dpfail, dppass); }
void glStencilMaskSeparate (GLenum face, GLuint mask) { gles_engine_stencil_mask_separate(face, mask); }

void glBlendFuncSeparate (GLenum srgb, GLenum drgb, GLenum sa, GLenum da) { gles_engine_blend_func_separate(srgb, drgb, sa, da); }
void glBlendEquation (GLenum mode) { gles_engine_blend_equation(mode); }
void glBlendEquationSeparate (GLenum modeRGB, GLenum modeAlpha) { gles_engine_blend_equation_separate(modeRGB, modeAlpha); }
void glDepthRangef (GLfloat n, GLfloat f) { gles_engine_depth_rangef(n, f); }

void glHint (GLenum target, GLenum mode) { (void)target; (void)mode; /* software rasterizer: no hints */ }
void glSampleCoverage (GLfloat value, GLboolean invert) { (void)value; (void)invert; }
void glReadPixels (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)
{ gles_engine_read_pixels(x, y, w, h, format, type, pixels); }

/* --- error / query ------------------------------------------------- */
GLenum glGetError (void) { return gles_engine_get_error(); }
const GLubyte *glGetString (GLenum name) { return gles_engine_get_string(name); }
void glGetIntegerv (GLenum pname, GLint *data)     { gles_engine_get_integerv(pname, data); }
void glGetFloatv (GLenum pname, GLfloat *data)     { gles_engine_get_floatv(pname, data); }
void glGetBooleanv (GLenum pname, GLboolean *data) { gles_engine_get_booleanv(pname, data); }

void glFlush (void)  { /* software rasterizer: drawing is immediate */ }
void glFinish (void) { /* software rasterizer: drawing is immediate */ }

/* --- buffers ------------------------------------------------------- */
void glGenBuffers (GLsizei n, GLuint *buffers)    { gles_engine_gen_buffers(n, buffers); }
void glDeleteBuffers (GLsizei n, const GLuint *b) { gles_engine_delete_buffers(n, b); }
void glBindBuffer (GLenum target, GLuint buffer)  { gles_engine_bind_buffer(target, buffer); }
void glBufferData (GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{ gles_engine_buffer_data(target, size, data, usage); }
void glBufferSubData (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{ gles_engine_buffer_sub_data(target, offset, size, data); }

/* --- textures ------------------------------------------------------ */
void glGenTextures (GLsizei n, GLuint *textures)    { gles_engine_gen_textures(n, textures); }
void glDeleteTextures (GLsizei n, const GLuint *t)  { gles_engine_delete_textures(n, t); }
void glBindTexture (GLenum target, GLuint texture)  { gles_engine_bind_texture(target, texture); }
void glActiveTexture (GLenum texture)               { gles_engine_active_texture(texture); }
void glTexParameteri (GLenum target, GLenum pname, GLint param)   { gles_engine_tex_parameteri(target, pname, param); }
void glTexParameterf (GLenum target, GLenum pname, GLfloat param) { gles_engine_tex_parameterf(target, pname, param); }
void glPixelStorei (GLenum pname, GLint param)      { gles_engine_pixel_storei(pname, param); }
void glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h,
                   GLint border, GLenum format, GLenum type, const void *data)
{ gles_engine_tex_image_2d(target, level, internalformat, w, h, border, format, type, data); }
void glTexSubImage2D (GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h,
                      GLenum format, GLenum type, const void *data)
{ gles_engine_tex_sub_image_2d(target, level, xoff, yoff, w, h, format, type, data); }
void glGenerateMipmap (GLenum target) { gles_engine_generate_mipmap(target); }

/* --- vertex attributes / draw -------------------------------------- */
void glVertexAttribPointer (GLuint index, GLint size, GLenum type, GLboolean normalized,
                            GLsizei stride, const void *pointer)
{ gles_engine_vertex_attrib_pointer(index, size, type, normalized, stride, pointer); }
void glEnableVertexAttribArray (GLuint index)  { gles_engine_enable_vertex_attrib_array(index); }
void glDisableVertexAttribArray (GLuint index) { gles_engine_disable_vertex_attrib_array(index); }
void glDrawArrays (GLenum mode, GLint first, GLsizei count) { gles_engine_draw_arrays(mode, first, count); }
void glDrawElements (GLenum mode, GLsizei count, GLenum type, const void *indices)
{ gles_engine_draw_elements(mode, count, type, indices); }

/* Constant (generic) vertex attributes are not used by the migrated GLES2
 * consumers (they all feed attributes through VBOs), and portablegl exposes no
 * setter for them, so these are accepted and ignored. */
void glVertexAttrib1f (GLuint i, GLfloat x) { (void)i; (void)x; }
void glVertexAttrib2f (GLuint i, GLfloat x, GLfloat y) { (void)i; (void)x; (void)y; }
void glVertexAttrib3f (GLuint i, GLfloat x, GLfloat y, GLfloat z) { (void)i; (void)x; (void)y; (void)z; }
void glVertexAttrib4f (GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { (void)i; (void)x; (void)y; (void)z; (void)w; }
void glVertexAttrib1fv (GLuint i, const GLfloat *v) { (void)i; (void)v; }
void glVertexAttrib2fv (GLuint i, const GLfloat *v) { (void)i; (void)v; }
void glVertexAttrib3fv (GLuint i, const GLfloat *v) { (void)i; (void)v; }
void glVertexAttrib4fv (GLuint i, const GLfloat *v) { (void)i; (void)v; }

/* --- texture parameter arrays / queries ---------------------------- */
void glTexParameterfv (GLenum target, GLenum pname, const GLfloat *params)
{ if (params) glTexParameterf(target, pname, params[0]); }
void glTexParameteriv (GLenum target, GLenum pname, const GLint *params)
{ if (params) glTexParameteri(target, pname, params[0]); }
void glGetTexParameterfv (GLenum target, GLenum pname, GLfloat *params)
{ (void)target; (void)pname; if (params) params[0] = 0; }
void glGetTexParameteriv (GLenum target, GLenum pname, GLint *params)
{ (void)target; (void)pname; if (params) params[0] = 0; }

/* --- compressed / copy texture paths (unused by the consumers) ----- */
void glCompressedTexImage2D (GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei s, const void *d)
{ (void)t;(void)l;(void)f;(void)w;(void)h;(void)b;(void)s;(void)d; }
void glCompressedTexSubImage2D (GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLsizei s, const void *d)
{ (void)t;(void)l;(void)x;(void)y;(void)w;(void)h;(void)f;(void)s;(void)d; }
void glCopyTexImage2D (GLenum t, GLint l, GLenum f, GLint x, GLint y, GLsizei w, GLsizei h, GLint b)
{ (void)t;(void)l;(void)f;(void)x;(void)y;(void)w;(void)h;(void)b; }
void glCopyTexSubImage2D (GLenum t, GLint l, GLint x, GLint y, GLint sx, GLint sy, GLsizei w, GLsizei h)
{ (void)t;(void)l;(void)x;(void)y;(void)sx;(void)sy;(void)w;(void)h; }

/* --- buffer / object queries --------------------------------------- */
void glGetBufferParameteriv (GLenum target, GLenum pname, GLint *params)
{ (void)target; (void)pname; if (params) *params = 0; }
GLboolean glIsBuffer (GLuint b)  { return b ? GL_TRUE : GL_FALSE; }
GLboolean glIsTexture (GLuint t) { return t ? GL_TRUE : GL_FALSE; }
void glGetVertexAttribfv (GLuint i, GLenum p, GLfloat *params) { (void)i;(void)p; if (params) params[0]=0; }
void glGetVertexAttribiv (GLuint i, GLenum p, GLint *params)   { (void)i;(void)p; if (params) params[0]=0; }
void glGetVertexAttribPointerv (GLuint i, GLenum p, void **ptr) { (void)i;(void)p; if (ptr) *ptr=NULL; }

/* --- framebuffers / renderbuffers ---------------------------------- *
 * The engine renders into the single default framebuffer provided by EGL, so
 * FBO objects are name-only and the default framebuffer is always complete. */
static GLuint g_fbo_next = 1, g_rbo_next = 1;
void glGenFramebuffers (GLsizei n, GLuint *f) { GLsizei i; for (i=0;i<n;++i) f[i]=g_fbo_next++; }
void glGenRenderbuffers (GLsizei n, GLuint *r) { GLsizei i; for (i=0;i<n;++i) r[i]=g_rbo_next++; }
void glDeleteFramebuffers (GLsizei n, const GLuint *f) { (void)n; (void)f; }
void glDeleteRenderbuffers (GLsizei n, const GLuint *r) { (void)n; (void)r; }
void glBindFramebuffer (GLenum target, GLuint f) { (void)target; (void)f; }
void glBindRenderbuffer (GLenum target, GLuint r) { (void)target; (void)r; }
GLenum glCheckFramebufferStatus (GLenum target) { (void)target; return GL_FRAMEBUFFER_COMPLETE; }
GLboolean glIsFramebuffer (GLuint f) { (void)f; return GL_FALSE; }
GLboolean glIsRenderbuffer (GLuint r) { (void)r; return GL_FALSE; }
void glRenderbufferStorage (GLenum t, GLenum f, GLsizei w, GLsizei h) { (void)t;(void)f;(void)w;(void)h; }
void glFramebufferRenderbuffer (GLenum t, GLenum a, GLenum rt, GLuint rb) { (void)t;(void)a;(void)rt;(void)rb; }
void glFramebufferTexture2D (GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l) { (void)t;(void)a;(void)tt;(void)tex;(void)l; }
void glGetFramebufferAttachmentParameteriv (GLenum t, GLenum a, GLenum p, GLint *params) { (void)t;(void)a;(void)p; if(params)*params=0; }
void glGetRenderbufferParameteriv (GLenum t, GLenum p, GLint *params) { (void)t;(void)p; if(params)*params=0; }

/* --- shader binary (interpreter only; no binary path) -------------- */
void glShaderBinary (GLsizei c, const GLuint *s, GLenum f, const void *b, GLsizei l)
{ (void)c;(void)s;(void)f;(void)b;(void)l; }

/* glUseProgram and the whole shader/program/uniform object model live in
 * gles2_shader.c. */
