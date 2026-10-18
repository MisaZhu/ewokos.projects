/*
 * pgl_g3d_shader.h - PRIVATE bridge (lives in src/, is NOT installed) between
 * the public pgl_g3d program API declared in <portablegl/pgl_g3d.h> and the
 * WS4 dispatch marshalling in pgl_g3d.c.
 *
 * The installed public header must stay free of any libv3dcompiler/mesa types
 * (an app that never uses the arbitrary-shader path should not need the
 * compiler header at all).  This private header is the one place that includes
 * <v3dcompiler.h>, exposing the cached compiled program so the flush path can
 * pack its QPU words into a g2d request (t7d) without leaking the struct into
 * the public contract.
 *
 * Compiled only under -DPGL_G3D (see pgl_g3d.h).
 */
#ifndef PGL_G3D_SHADER_H
#define PGL_G3D_SHADER_H

#ifdef PGL_G3D

#include <portablegl/pgl_g3d.h>
#include <v3dcompiler.h>

/* The cached compiled program for a valid handle, or NULL when the handle is
 * invalid / not compiled.  The returned pointer is owned by the program cache
 * and stays valid until pgl_g3d_program_free() or pgl_g3d_program_free_all()
 * releases that entry.  Both stage[] QPU buffers are the little-endian u32
 * streams documented by struct v3d_compiled_stage. */
const struct v3d_compiled_program* pgl_g3d_shader_get(pgl_g3d_prog p);

/* Upload the compiled program for handle p to the g2dd daemon (serializing its
 * QPU + uniform stream + shader-state metadata into the G2D_DEV_CNTL_PGL_PROGRAM
 * blob) and return the daemon's non-negative program handle, uploading at most
 * once - the handle is cached in the entry, so a per-frame draw call is free
 * after the first.  Returns PGL_G3D_PROG_INVALID for a bad handle, or a negative
 * G2D_ERR_* code when the upload was declined: G2D_ERR_NOT_SUPPORTED (sticky, the
 * platform has no compiled-shader path or the GPU is off) or G2D_ERR_FAILED
 * (transient).  The draw path calls this lazily before referencing a program. */
int pgl_g3d_shader_upload(pgl_g3d_prog p);

/* The daemon program handle for an already-uploaded p, or PGL_G3D_PROG_INVALID
 * when p is invalid or has not been uploaded yet.  This is the value a draw
 * request carries so the daemon dispatches the right compiled shader. */
int pgl_g3d_shader_daemon_handle(pgl_g3d_prog p);

/* Latch p as declined (sticky): the daemon answered a DRAW of this program
 * with G2D_ERR_NOT_SUPPORTED before any GPU submission (e.g. a sampler unit
 * it reads is unbound), so pgl_g3d_shader_upload returns NOT_SUPPORTED for p
 * from now on and its compiled draws fall back to the caller's CPU path
 * without latching the whole GPU off. */
void pgl_g3d_shader_mark_declined(pgl_g3d_prog p);

/* Non-zero when p carries a compiled COMPUTE stage (the shadertoy compute
 * route): the daemon dispatches it through the CSD instead of the binner, and
 * its per-draw blob must carry the `v3d_cs_stride` uniform (frame row pitch in
 * dwords) that pgl_g3d_set_uniforms injects. */
int pgl_g3d_shader_has_compute(pgl_g3d_prog p);

/* Non-zero when p's compute stage is the fixed GEOMETRY rasterizer
 * (geom_raster_cs.h): the daemon dispatches one workgroup per triangle with
 * SSBO bindings 0=fb 1=pos 2=nrm 3=depth, and the per-draw blob follows the
 * kernel's uniform layout (uMVP/uModelView/uNormalMat/uMaterial/uLightEye +
 * the derived uShade/uViewportWH/uFbStride/uTriFirst/uTriCount that
 * pgl_g3d_set_uniforms fills).  pgl_g3d_draw_mesh_compiled only accelerates
 * such programs. */
int pgl_g3d_shader_is_geom(pgl_g3d_prog p);

/* Non-zero when p's geom kernel is the TEXTURED variant
 * (geom_raster_tex_cs_glsl, attached when the app FS declares a sampler):
 * attribute 1 carries (u,v,0) per vertex, SSBO binding 4 is the texture
 * image (a LINEAR/raster uint32 array of 0xAARRGGBB words, bound per draw
 * as ctex unit 0), and the per-draw blob additionally carries uTexW/uTexH
 * (patched from the bound slot by pgl_g3d_draw_mesh_compiled). */
int pgl_g3d_shader_geom_tex(pgl_g3d_prog p);

/* Non-zero when p's geom kernel is the COLOUR-MODULATED textured variant
 * (geom_raster_texcol_cs_glsl, attached when the app FS multiplies a vec4
 * varying by a texture2D sample - the imgui shape gl_FragColor = vColor *
 * texture2D(uTexture, vUV)): pass 1 (geom_xform_texcol_cs_glsl) reads pos+uv+
 * colour interleaved at a 10-float stride from binding 1 and writes a 12-float
 * vout record; pass 2 bilinearly samples the ctex-unit-0 raster image, modulates
 * by the interpolated colour, alpha-blends into the fb (SRC_ALPHA /
 * ONE_MINUS_SRC_ALPHA), honouring a uScissor ivec4 (fb row space, w/h<=0 = off)
 * and writing no depth.  Serialised as cs_is_geom = 4. */
int pgl_g3d_shader_geom_texcol(pgl_g3d_prog p);

/* Non-zero when p's geom kernel is the INSTANCED variant
 * (geom_xform_inst_cs_glsl, attached when the app VS carries the
 * PGL_G3D_INSTANCED marker): pass 1 reads a per-vertex instance index from
 * the position stream's 4th float (pos staged as vec4, stride 16 bytes) and
 * selects the whole per-instance transform from a 4th SSBO (binding 3) that
 * pgl_g3d_draw_mesh_compiled marshals as ctex unit 0 from the buffer staged by
 * pgl_g3d_set_instances.  Pass 2 is the plain gouraud raster kernel. */
int pgl_g3d_shader_geom_inst(pgl_g3d_prog p);

/* The geom kernel's lighting model for p: 1 = eye-space positional light,
 * two-sided, Blinn-Phong specular 48 (teapot's VS); 0 = fixed directional
 * (5,5,10) one-sided diffuse (gears' VS).  Selected at compile time by
 * whether the app's VS source declares uLightEye. */
int pgl_g3d_shader_geom_model(pgl_g3d_prog p);

/* Total dword span of the CONCATENATED per-draw uniform blob of a two-pass
 * geom program: [xform region | raster region].  The xform (pass 1) kernel's
 * uniforms occupy dwords [0, N) and the raster (pass 2) kernel's occupy
 * [N, N+M); the returned value is N+M.  0 when p is not a two-pass geom
 * program.  pgl_g3d_draw_mesh_compiled sizes the staged blob to exactly this
 * so the daemon can split it with the program's cs2_uniform_blob_dwords. */
uint32_t pgl_g3d_shader_geom_blob_dwords(pgl_g3d_prog p);

/* Write nc dwords from val into EVERY region of the concatenated per-draw blob
 * that declares `name`.  A derived uniform such as uViewportWH is declared by
 * BOTH the xform and the raster kernel (each at its own region-relative dword
 * offset), so it must be patched twice; a name unique to one region is patched
 * once.  blobcap bounds the write.  Returns one past the highest global dword
 * written (so the caller can grow its count), or 0 when `name` is unknown to
 * both stages. */
uint32_t pgl_g3d_shader_patch_uniform(pgl_g3d_prog p, uint32_t* blob,
                                      uint32_t blobcap, const char* name,
                                      const uint32_t* val, uint32_t nc);

#endif /* PGL_G3D */
#endif /* PGL_G3D_SHADER_H */
