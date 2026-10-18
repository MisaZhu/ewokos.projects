/*
 * pgl_g3d_shader.c - portablegl on-device GLSL shader-program cache (WS4
 * client half, t7c).  Compiled only under -DPGL_G3D.
 *
 * This is the APP-SIDE compile step of the GPU-wiring architecture: an effect
 * hands a GLSL vertex+fragment pair to pgl_g3d_program_compile(), which runs
 * libv3dcompiler.a's v3d_compile_glsl() IN THIS PROCESS (the app cannot map the
 * V3D MMIO, but it can compile) and caches the resulting V3D 7.1 QPU words.  A
 * later draw (t7d/t7e) marshals the cached words through the stateless
 * g2dclient protocol to the g2dd daemon, which is the only entity that may
 * upload them to the GPU and dispatch.  Nothing here rasterizes on the CPU: the
 * cache exists purely to avoid recompiling the same shader every frame.
 *
 * The cache is a small fixed table (no dynamic growth, no allocation on a hit).
 * A handle is simply the entry index; identical source pairs return the same
 * handle so a per-frame compile request is free after the first.  Compilation
 * is single-threaded like the rest of pgl_g3d.c (portablegl drives one context
 * per thread and never shares it), so the table needs no locking.
 *
 * A compile failure is per-shader and deliberately does NOT latch the GPU off:
 * the caller keeps its CPU fragment shader for that one effect while every
 * other effect (and the fixed-function solid/gouraud/tex paths) stay on the GPU.
 */

#ifdef PGL_G3D

#include <portablegl/pgl_g3d.h>
#include "pgl_g3d_shader.h"
#include <v3dcompiler.h>
#include <geom_raster_cs.h>
#include <g2dclient/g2dclient.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* The upload blob's fixed-size attribute arrays are dimensioned by the
   G2D_PGL_MAX_* protocol limits; the compiler contract uses V3DCOMPILER_MAX_*.
   They must stay equal or the serializer below would memcpy the wrong span. */
#if V3DCOMPILER_MAX_VS_INPUTS != G2D_PGL_MAX_VS_ATTRS || \
    V3DCOMPILER_MAX_FS_INPUTS != G2D_PGL_MAX_FS_INPUTS
#error "g2d_pgl_program_blob_t attribute limits diverge from v3dcompiler.h"
#endif

/* Fixed compiled-program cache depth.  shadertoy ships 11 effects; 16 leaves
   room for a couple of extra variants before an eviction-free compile fails. */
#define PGL_G3D_MAX_PROGS 16

typedef struct {
	int      used;
	uint32_t hash;   /* FNV-1a pre-filter over both sources */
	char*    vert;   /* strdup'd source: exact-match cache validation */
	char*    frag;
	/* Daemon-side upload state (t7d): the compiled QPU is shipped to g2dd
	   ONCE, lazily on the first draw that references the program, and the
	   returned handle is cached here so later frames never re-send it.
	   daemon_handle is meaningful only when uploaded != 0; upload_declined
	   latches a sticky G2D_ERR_NOT_SUPPORTED so a per-frame draw stops
	   retrying an upload the platform can never honour. */
	int      daemon_handle;
	int      uploaded;
	int      upload_declined;
	/* 1 when stage[COMPUTE] is the fixed geometry rasterizer kernel
	   (geom_raster_cs.h) rather than a fragment-wrapped shadertoy kernel;
	   geom_model selects its lighting model (1 = teapot positional, 0 =
	   gears directional), sniffed from the app VS's uLightEye declaration.
	   geom_tex selects the TEXTURED kernel variant (geom_raster_tex_cs_glsl:
	   attr1 = uv, SSBO 4 = linear texture image), attached when the app FS
	   declares a sampler.  geom_tpw is the raster pass's triangles-per-
	   workgroup geometry (64 = V4 8x8 one-lane-per-tri, 256 = V5 16x16,
	   0 = the legacy one-workgroup-per-triangle kernels V0..V3); the daemon
	   derives its pass-2 grid from it (wire byte cs_geom_tpw). */
	int      is_geom;
	int      geom_model;
	int      geom_tex;
	int      geom_texcol;
	int      geom_inst;
	int      geom_tpw;
	struct v3d_compiled_program prog;
	/* Two-pass geometry rasterizer: the VERTEX-TRANSFORM kernel (pass 1) is
	   stored here as its own compiled program (its COMPUTE stage), separate
	   from prog.stage[COMPUTE] which holds the RASTER kernel (pass 2).  Both
	   are attached together or neither is (is_geom stays 0), so a geom draw
	   always has both stages.  uniform_blob_dwords of this stage's COMPUTE
	   slot is the split point of the concatenated per-draw blob. */
	struct v3d_compiled_program xform_prog;
} prog_entry_t;

static prog_entry_t _progs[PGL_G3D_MAX_PROGS];

/* FNV-1a over the two sources with a separator so ("ab","c") and ("a","bc")
   never collide.  Only a pre-filter: the strcmp in the hit loop is
   authoritative, so a hash collision can never return the wrong program. */
static uint32_t hash_sources(const char* v, const char* f)
{
	uint32_t h = 2166136261u;
	const unsigned char* p;
	for (p = (const unsigned char*)v; *p; p++) { h ^= *p; h *= 16777619u; }
	h ^= 0x1Fu; h *= 16777619u;
	for (p = (const unsigned char*)f; *p; p++) { h ^= *p; h *= 16777619u; }
	return h;
}

static void entry_clear(prog_entry_t* e)
{
	if (e->vert) { free(e->vert); e->vert = NULL; }
	if (e->frag) { free(e->frag); e->frag = NULL; }
	v3d_compiled_program_free(&e->prog);   /* frees each stage[].qpu */
	memset(&e->prog, 0, sizeof(e->prog));
	v3d_compiled_program_free(&e->xform_prog);
	memset(&e->xform_prog, 0, sizeof(e->xform_prog));
	e->hash = 0;
	e->daemon_handle = -1;
	e->uploaded = 0;
	e->upload_declined = 0;
	e->is_geom = 0;
	e->geom_model = 0;
		e->geom_tex = 0;
	e->geom_texcol = 0;
	e->geom_inst = 0;
	e->used = 0;
}

pgl_g3d_prog pgl_g3d_program_compile(const char* vert_glsl, const char* frag_glsl,
                                     char* errlog, int errsz)
{
	uint32_t h;
	int i, slot = -1;

	if (errlog && errsz > 0)
		errlog[0] = '\0';

	if (!vert_glsl || !frag_glsl) {
		if (errlog && errsz > 0)
			snprintf(errlog, (size_t)errsz,
			         "pgl_g3d: both vertex and fragment GLSL are required");
		return PGL_G3D_PROG_INVALID;
	}

	h = hash_sources(vert_glsl, frag_glsl);

	/* cache hit?  compare the hash first, then the exact sources. */
	for (i = 0; i < PGL_G3D_MAX_PROGS; i++) {
		if (_progs[i].used && _progs[i].hash == h &&
		    _progs[i].vert && _progs[i].frag &&
		    strcmp(_progs[i].vert, vert_glsl) == 0 &&
		    strcmp(_progs[i].frag, frag_glsl) == 0)
			return (pgl_g3d_prog)i;
	}

	/* free slot? */
	for (i = 0; i < PGL_G3D_MAX_PROGS; i++) {
		if (!_progs[i].used) { slot = i; break; }
	}
	if (slot < 0) {
		if (errlog && errsz > 0)
			snprintf(errlog, (size_t)errsz,
			         "pgl_g3d: shader program cache full (%d entries)",
			         PGL_G3D_MAX_PROGS);
		return PGL_G3D_PROG_INVALID;
	}

	{
		struct v3d_compiled_program res;
		memset(&res, 0, sizeof(res));
		if (!v3d_compile_glsl(vert_glsl, frag_glsl, &res)) {
			if (errlog && errsz > 0)
				snprintf(errlog, (size_t)errsz, "%s",
				         res.log[0] ? res.log
				                    : "v3d_compile_glsl failed");
			v3d_compiled_program_free(&res);
			return PGL_G3D_PROG_INVALID;   /* per-shader: no GPU latch-off */
		}
		_progs[slot].prog = res;   /* take ownership of res.stage[].qpu */

		/* Compute-route attach.  Two mutually exclusive shapes:
		   - a MESH program (its VS transforms by uMVP) gets the fixed geometry
		     rasterizer kernel (one lane per triangle) - the binner cannot
		     rasterise real primitives on this silicon, so this is the only
		     correct GPU route for mesh draws.  The kernel variant is selected by
		     the app FS: no sampler -> the gouraud kernel (SSBO 0=fb 1=pos 2=nrm
		     3=depth; teapot/gears); a sampler -> the TEXTURED variant
		     (geom_raster_tex_cs_glsl: attr1 = uv, SSBO 4 = linear texture image,
		     NEAREST+CLAMP, no TMU; 3ddemo).
		   - a FULL-SCREEN program (no uMVP; shadertoy) gets its fragment source
		     wrapped as a per-pixel compute kernel as before.
		   A compute compile failure is NOT an error - the program keeps its
		   VS/FS pair and its draws fall back to the CPU rasterizer. */
		if (strstr(vert_glsl, "uMVP") != NULL) {
			struct v3d_compiled_program gres, xres;
			int tex = strstr(frag_glsl, "sampler") != NULL;
			/* COLOUR-MODULATED textured variant (imgui): the FS multiplies a
			   vec4 varying by the texture sample (gl_FragColor = vColor *
			   texture2D(...)), which the plain TEX_MOD kernel cannot express
			   (it writes the fetched texel verbatim, no per-vertex colour).  Sniff
			   that product shape; when present, select the texcol kernels - the
			   per-vertex colour is interleaved into binding 1 at a 10-float
			   stride (V3D 7.1 compute has only 4 SSBO bindings), the vout record
			   grows to 12 floats, and pass 2 does bilinear sampling + colour
			   modulation + SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending + scissor with
			   no depth write.  Serialised as cs_is_geom = 4. */
			int texcol = tex && strstr(frag_glsl, "* texture2D") != NULL;
			/* The INSTANCED variant (gears) is selected by a marker in the
			   app VS: it swaps pass 1 for geom_xform_inst_cs_glsl, which reads
			   a per-vertex instance index from pos.w and the per-gear matrices
			   from a 4th SSBO.  Mutually exclusive with the textured variant
			   (an instanced mesh carries no sampler).  Pass 2 is the plain
			   gouraud raster kernel (V5, cs_geom_tpw = 256) since the vout
			   record layout is unchanged. */
			int inst = !tex && strstr(vert_glsl, "PGL_G3D_INSTANCED") != NULL;
			/* The COLOUR variant (teapot's native gouraud fast-path shape) is a
			   mesh VS that transforms by uMVP but declares NO uNormalMat/uLightEye:
			   attribute 1 is a precomputed per-vertex colour, not a normal, so pass 1
			   (geom_xform_color_cs_glsl) copies it straight through instead of
			   lighting it on the GPU.  Pass 2 (the gouraud raster kernel), the
			   stride-6 upload and the cs_is_geom=1 daemon dispatch are shared with
			   the lit variant unchanged. */
			int color = !tex && !inst &&
			            strstr(vert_glsl, "uNormalMat") == NULL;
			const char* raster_src = texcol ? geom_raster_texcol_cs_glsl
			                             : (tex ? geom_raster_tex_cs_glsl
			                                    : geom_raster_cs_glsl);
			const char* xform_src  = inst ? geom_xform_inst_cs_glsl
			                             : (texcol ? geom_xform_texcol_cs_glsl
			                                    : (tex ? geom_xform_tex_cs_glsl
			                                           : (color ? geom_xform_color_cs_glsl
			                                                  : geom_xform_cs_glsl)));
			memset(&gres, 0, sizeof(gres));
			memset(&xres, 0, sizeof(xres));
			/* BOTH kernels must compile: the raster pass reads the vout
			   records the xform pass writes, so a program with only one of
			   them cannot run.  On any failure neither stage is attached
			   (prog.stage[COMPUTE] stays empty -> has_compute == 0) and the
			   mesh draws on the CPU rasterizer. */
			if (v3d_compile_compute_raw(raster_src, &gres) &&
			    v3d_compile_compute_raw(xform_src, &xres)) {
				/* raster kernel (pass 2) -> prog.stage[COMPUTE] */
				_progs[slot].prog.stage[V3D_STAGE_COMPUTE] =
					gres.stage[V3D_STAGE_COMPUTE];   /* move ownership */
				memset(&gres.stage[V3D_STAGE_COMPUTE], 0,
				       sizeof(gres.stage[V3D_STAGE_COMPUTE]));
				/* xform kernel (pass 1) -> xform_prog.stage[COMPUTE] */
				_progs[slot].xform_prog.stage[V3D_STAGE_COMPUTE] =
					xres.stage[V3D_STAGE_COMPUTE];   /* move ownership */
				memset(&xres.stage[V3D_STAGE_COMPUTE], 0,
				       sizeof(xres.stage[V3D_STAGE_COMPUTE]));
				_progs[slot].is_geom = 1;
				_progs[slot].geom_tex = tex;
				_progs[slot].geom_texcol = texcol;
				_progs[slot].geom_inst = inst;
				/* the tex kernel is the legacy one-workgroup-per-triangle shape
				   (0); the gouraud kernel is one-lane-per-triangle and covers 256
				   triangles per 16x16 workgroup, so the daemon dispatches the
				   matching ceil(ntris/256) grid from cs_geom_tpw. */
				_progs[slot].geom_tpw = tex ? 0 : 256;
				_progs[slot].geom_model =
					strstr(vert_glsl, "uLightEye") != NULL ? 1 : 0;
			}
			v3d_compiled_program_free(&gres);
			v3d_compiled_program_free(&xres);
		} else {
			struct v3d_compiled_program cres;
			memset(&cres, 0, sizeof(cres));
			if (v3d_compile_compute(frag_glsl, &cres)) {
				_progs[slot].prog.stage[V3D_STAGE_COMPUTE] =
					cres.stage[V3D_STAGE_COMPUTE];   /* move ownership */
				memset(&cres.stage[V3D_STAGE_COMPUTE], 0,
				       sizeof(cres.stage[V3D_STAGE_COMPUTE]));
			}
			v3d_compiled_program_free(&cres);
		}
	}

	_progs[slot].vert = strdup(vert_glsl);
	_progs[slot].frag = strdup(frag_glsl);
	if (!_progs[slot].vert || !_progs[slot].frag) {
		/* strdup OOM: roll the half-filled entry back, keep the QPU from
		   leaking, and decline (the caller falls back to its CPU path). */
		if (_progs[slot].vert) { free(_progs[slot].vert); _progs[slot].vert = NULL; }
		if (_progs[slot].frag) { free(_progs[slot].frag); _progs[slot].frag = NULL; }
		v3d_compiled_program_free(&_progs[slot].prog);
		memset(&_progs[slot].prog, 0, sizeof(_progs[slot].prog));
		if (errlog && errsz > 0)
			snprintf(errlog, (size_t)errsz,
			         "pgl_g3d: out of memory caching shader source");
		return PGL_G3D_PROG_INVALID;
	}

	_progs[slot].hash = h;
	_progs[slot].daemon_handle = -1;   /* not uploaded until the first draw */
	_progs[slot].uploaded = 0;
	_progs[slot].upload_declined = 0;
	_progs[slot].used = 1;
	return (pgl_g3d_prog)slot;
}

int pgl_g3d_program_valid(pgl_g3d_prog p)
{
	return (p >= 0 && p < PGL_G3D_MAX_PROGS && _progs[p].used);
}

const struct v3d_compiled_program* pgl_g3d_shader_get(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return NULL;
	return &_progs[p].prog;
}

int pgl_g3d_shader_has_compute(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].prog.stage[V3D_STAGE_COMPUTE].qpu_word_count != 0;
}

int pgl_g3d_shader_is_geom(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].is_geom &&
	       _progs[p].prog.stage[V3D_STAGE_COMPUTE].qpu_word_count != 0;
}

int pgl_g3d_shader_geom_model(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_model;
}

int pgl_g3d_shader_geom_tex(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_tex &&
	       _progs[p].prog.stage[V3D_STAGE_COMPUTE].qpu_word_count != 0;
}

int pgl_g3d_shader_geom_texcol(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_texcol &&
	       _progs[p].prog.stage[V3D_STAGE_COMPUTE].qpu_word_count != 0;
}

int pgl_g3d_shader_geom_inst(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_inst &&
	       _progs[p].prog.stage[V3D_STAGE_COMPUTE].qpu_word_count != 0;
}

int pgl_g3d_shader_uniform_dword(pgl_g3d_prog p, const char* name,
                                 uint32_t* num_components)
{
	const struct v3d_compiled_stage* fs;
	const struct v3d_compiled_stage* vs;
	const struct v3d_compiled_stage* cs;
	uint32_t i;

	if (num_components)
		*num_components = 0;
	if (!name || !pgl_g3d_program_valid(p))
		return -1;

	/* Compute route first: when the program carries a compute stage the daemon
	   dispatches THAT stage, whose linker pass assigned its own dword offsets
	   (it also owns the extra `v3d_cs_stride` uniform), so the per-draw blob
	   must follow the compute layout rather than the FS's. */
	cs = &_progs[p].prog.stage[V3D_STAGE_COMPUTE];
	if (cs->qpu_word_count != 0) {
		/* Two-pass geom: the per-draw blob is the CONCATENATION
		   [xform region | raster region].  Search the xform (pass 1) kernel
		   first (region base 0), then the raster (pass 2) kernel offset by the
		   xform region's span.  A name declared by BOTH (uViewportWH) resolves
		   to the XFORM offset here; pgl_g3d_shader_patch_uniform is what writes
		   every region, so the draw path uses that for derived uniforms. */
		if (_progs[p].is_geom) {
			const struct v3d_compiled_stage* xs =
				&_progs[p].xform_prog.stage[V3D_STAGE_COMPUTE];
			for (i = 0; i < xs->num_uniform_bindings; i++) {
				const struct v3d_uniform_binding* ub = &xs->uniform_bindings[i];
				if (strcmp(ub->name, name) == 0) {
					if (num_components)
						*num_components = ub->num_components;
					return (int)ub->dword_offset;
				}
			}
			for (i = 0; i < cs->num_uniform_bindings; i++) {
				const struct v3d_uniform_binding* ub = &cs->uniform_bindings[i];
				if (strcmp(ub->name, name) == 0) {
					if (num_components)
						*num_components = ub->num_components;
					return (int)(xs->uniform_blob_dwords + ub->dword_offset);
				}
			}
			return -1;   /* a geom program never consults the FS/VS layouts */
		}
		for (i = 0; i < cs->num_uniform_bindings; i++) {
			const struct v3d_uniform_binding* ub = &cs->uniform_bindings[i];
			if (strcmp(ub->name, name) == 0) {
				if (num_components)
					*num_components = ub->num_components;
				return (int)ub->dword_offset;
			}
		}
		return -1;   /* a compute program never consults the FS/VS layouts */
	}

	/* Search the FRAGMENT stage first: it carries the shadertoy default-block
	   uniforms (iGlobalTime / iResolution), whose passthrough vertex shader
	   reads none.  The bindings were exported by build_uniform_bindings() in
	   the compiler, each with the exact dword offset its load_uniform was
	   assigned. */
	fs = &_progs[p].prog.stage[V3D_STAGE_FRAGMENT];
	for (i = 0; i < fs->num_uniform_bindings; i++) {
		const struct v3d_uniform_binding* ub = &fs->uniform_bindings[i];
		if (strcmp(ub->name, name) == 0) {
			if (num_components)
				*num_components = ub->num_components;
			return (int)ub->dword_offset;
		}
	}

	/* WS4: fall back to the VERTEX stage, which carries a mesh draw's
	   uniforms (mvp / modelview / normal matrix / material / light).  A
	   teapot-style VS reflects them here while its passthrough FS reflects
	   none, so the FS loop above already declined.  The COORD (binning)
	   variant is the SAME linked VS NIR, so it shares these VS
	   driver_locations: the returned offset indexes the one per-draw blob
	   that feeds both the coord and render-VS uniform expansion.  mat4
	   uniforms report num_components == 16 (16 consecutive dwords). */
	vs = &_progs[p].prog.stage[V3D_STAGE_VERTEX];
	for (i = 0; i < vs->num_uniform_bindings; i++) {
		const struct v3d_uniform_binding* ub = &vs->uniform_bindings[i];
		if (strcmp(ub->name, name) == 0) {
			if (num_components)
				*num_components = ub->num_components;
			return (int)ub->dword_offset;
		}
	}
	return -1;   /* no such default-block uniform in this program */
}

uint32_t pgl_g3d_shader_geom_blob_dwords(pgl_g3d_prog p)
{
	const struct v3d_compiled_stage* xs;
	const struct v3d_compiled_stage* cs;

	if (!pgl_g3d_program_valid(p) || !_progs[p].is_geom)
		return 0;
	xs = &_progs[p].xform_prog.stage[V3D_STAGE_COMPUTE];
	cs = &_progs[p].prog.stage[V3D_STAGE_COMPUTE];
	return xs->uniform_blob_dwords + cs->uniform_blob_dwords;
}

/* Write nc dwords into one region of the concatenated blob at region_base +
   the binding's dword offset, if `name` is declared there and the write fits.
   Returns one past the highest dword written, or 0 when not written. */
static uint32_t patch_region(const struct v3d_compiled_stage* st, uint32_t region_base,
                             const char* name, uint32_t* blob, uint32_t blobcap,
                             const uint32_t* val, uint32_t nc)
{
	uint32_t i;

	for (i = 0; i < st->num_uniform_bindings; i++) {
		const struct v3d_uniform_binding* ub = &st->uniform_bindings[i];
		uint32_t off, k, span;
		if (strcmp(ub->name, name) != 0)
			continue;
		/* write the smaller of the caller's and the binding's width so a
		   vec4 slot fed 3 components (or vice versa) never overruns */
		span = ub->num_components < nc ? ub->num_components : nc;
		off = region_base + ub->dword_offset;
		if (off + span > blobcap)
			return 0;
		for (k = 0; k < span; k++)
			blob[off + k] = val[k];
		return off + span;
	}
	return 0;
}

uint32_t pgl_g3d_shader_patch_uniform(pgl_g3d_prog p, uint32_t* blob,
                                      uint32_t blobcap, const char* name,
                                      const uint32_t* val, uint32_t nc)
{
	const struct v3d_compiled_stage* xs;
	const struct v3d_compiled_stage* cs;
	uint32_t hi = 0, e;

	if (!name || !val || nc == 0 || !blob ||
	    !pgl_g3d_program_valid(p) || !_progs[p].is_geom)
		return 0;
	xs = &_progs[p].xform_prog.stage[V3D_STAGE_COMPUTE];
	cs = &_progs[p].prog.stage[V3D_STAGE_COMPUTE];

	e = patch_region(xs, 0u, name, blob, blobcap, val, nc);
	if (e > hi) hi = e;
	e = patch_region(cs, xs->uniform_blob_dwords, name, blob, blobcap, val, nc);
	if (e > hi) hi = e;
	return hi;
}

/* Serialize one compiled program into a fresh contig-shm canvas laid out as a
   g2d_pgl_program_blob_t header immediately followed by its variable payload
   (see the struct comment in g2dclient.h for the exact section order), upload
   it with g2d_pgl_program(), then free the canvas (the daemon copies the QPU
   into its own GPU-visible dma).  Returns the daemon's non-negative program
   handle, or a negative G2D_ERR_* code.  The canvas is modelled w=total/4 h=1
   so g2d_attach's size >= w*h*4 check holds with size == the blob length. */
static int program_upload(const prog_entry_t* e)
{
	const struct v3d_compiled_program* cp = &e->prog;
	const struct v3d_compiled_stage* vs = &cp->stage[V3D_STAGE_VERTEX];
	const struct v3d_compiled_stage* fs = &cp->stage[V3D_STAGE_FRAGMENT];
	const struct v3d_compiled_stage* cs = &cp->stage[V3D_STAGE_COORD];
	const struct v3d_compiled_stage* cu = &cp->stage[V3D_STAGE_COMPUTE];
	const struct v3d_compiled_stage* xu = &e->xform_prog.stage[V3D_STAGE_COMPUTE];
	g2d_pgl_program_blob_t hdr;
	g2d_pgl_program_req_t req;
	uint32_t payload_bytes, total, off;
	uint32_t* px = NULL;
	uint8_t* base;
	int shm = 0, handle;

	memset(&hdr, 0, sizeof(hdr));
	hdr.magic   = G2D_PGL_PROGRAM_MAGIC;
	hdr.version = G2D_PGL_PROGRAM_VERSION;
	hdr.vs_qpu_words    = vs->qpu_word_count;
	hdr.fs_qpu_words    = fs->qpu_word_count;
	hdr.vs_num_uniforms = vs->num_uniforms;
	hdr.fs_num_uniforms = fs->num_uniforms;
	hdr.fs_num_inputs    = fs->num_inputs;
	hdr.fs_needs_coords  = fs->needs_coords  ? 1 : 0;
	hdr.fs_uses_center_w = fs->uses_center_w ? 1 : 0;
	hdr.fs_writes_z      = fs->writes_z      ? 1 : 0;
	hdr.fs_disable_ez    = fs->disable_ez    ? 1 : 0;
	hdr.fs_threads       = fs->threads;
	hdr.fs_single_seg    = fs->single_seg ? 1 : 0;
	hdr.vs_threads       = vs->threads;
	hdr.vs_single_seg    = vs->single_seg ? 1 : 0;
	hdr.vs_vpm_input_size    = vs->vpm_input_size;
	hdr.vs_vpm_output_size   = vs->vpm_output_size;
	hdr.vs_vcm_cache_size    = vs->vcm_cache_size;
	hdr.vs_separate_segments = vs->separate_segments ? 1 : 0;
	memcpy(hdr.vs_vattr_sizes, vs->vattr_sizes, sizeof(hdr.vs_vattr_sizes));
	memcpy(hdr.vs_driver_location_map, vs->driver_location_map,
	       sizeof(hdr.vs_driver_location_map));
	memcpy(hdr.fs_input_slots, fs->input_slots, sizeof(hdr.fs_input_slots));
	/* coordinate (binning) stage: the same VS NIR compiled with is_coord=true.
	   It shares the VS's uniform driver_locations, so the client's one per-draw
	   blob feeds both the coord and render-VS expansion; only its QPU, uniform
	   stream and VPM sizes differ (the coord shader writes gl_Position only). */
	hdr.coord_qpu_words         = cs->qpu_word_count;
	hdr.coord_num_uniforms      = cs->num_uniforms;
	hdr.coord_vpm_input_size    = cs->vpm_input_size;
	hdr.coord_vpm_output_size   = cs->vpm_output_size;
	hdr.coord_vcm_cache_size    = cs->vcm_cache_size;
	hdr.coord_separate_segments = cs->separate_segments ? 1 : 0;
	hdr.coord_threads           = cs->threads;
	hdr.coord_single_seg        = cs->single_seg ? 1 : 0;
	/* VERSION 3 optional compute stage (zero when the compute variant did not
	   compile: the daemon then draws the program through the binner). */
	hdr.cs_qpu_words     = cu->qpu_word_count;
	hdr.cs_num_uniforms  = cu->num_uniforms;
	hdr.cs_local_size[0] = cu->local_size[0];
	hdr.cs_local_size[1] = cu->local_size[1];
	hdr.cs_local_size[2] = cu->local_size[2];
	hdr.cs_shared_size   = cu->shared_size;
	hdr.cs_threads       = cu->threads;
	hdr.cs_single_seg    = cu->single_seg ? 1 : 0;
	/* the geometry rasterizer rides the same VERSION-3 wire stage; the
	   daemon switches its dispatch shape (per-triangle grid + SSBO layout)
	   on this byte: 1 = gouraud (fb/pos/nrm/depth), 2 = textured
	   (fb/interleaved pos+uv/depth/tex), 3 = instanced (fb/pos+idx/nrm/depth/
	   matrices), 4 = colour-modulated textured (fb/interleaved pos+uv+colour/
	   depth(unused)/tex, bilinear + blend + scissor). */
	hdr.cs_is_geom       = e->is_geom ? (e->geom_texcol ? 4 : (e->geom_tex ? 2 : (e->geom_inst ? 3 : 1))) : 0;
	/* raster-pass workgroup geometry (rides the VERSION-3 reserved byte):
	   0 = one workgroup per triangle; 64/256 = the V4/V5 one-lane-per-triangle
	   kernels, so the daemon dispatches ceil(ntris/tpw) workgroups instead. */
	hdr.cs_geom_tpw      = (uint8_t)e->geom_tpw;
	/* VERSION 4 second compute stage (cs2): the VERTEX-TRANSFORM pass of the
	   two-pass geometry rasterizer (zero when absent - a single-pass or
	   non-geom program).  cs2_uniform_blob_dwords is the xform region's span,
	   i.e. the split point of the concatenated per-draw uniform blob. */
	hdr.cs2_qpu_words     = xu->qpu_word_count;
	hdr.cs2_num_uniforms  = xu->num_uniforms;
	hdr.cs2_local_size[0] = xu->local_size[0];
	hdr.cs2_local_size[1] = xu->local_size[1];
	hdr.cs2_local_size[2] = xu->local_size[2];
	hdr.cs2_shared_size   = xu->shared_size;
	hdr.cs2_threads       = xu->threads;
	hdr.cs2_single_seg    = xu->single_seg ? 1 : 0;
	hdr.cs2_uniform_blob_dwords = xu->uniform_blob_dwords;

	payload_bytes = (hdr.vs_qpu_words + hdr.fs_qpu_words + hdr.coord_qpu_words +
	                 hdr.vs_num_uniforms * 2u + hdr.fs_num_uniforms * 2u +
	                 hdr.coord_num_uniforms * 2u +
	                 hdr.cs_qpu_words + hdr.cs_num_uniforms * 2u +
	                 hdr.cs2_qpu_words + hdr.cs2_num_uniforms * 2u) * 4u;
	hdr.payload_bytes = payload_bytes;
	total = (uint32_t)sizeof(hdr) + payload_bytes;

	/* A contig segment big enough for header + payload; the daemon reads it
	   as a raw byte buffer, so no physical base is needed client-side. */
	if (g2d_shm_alloc(total, &shm, &px) != 0 || shm <= 0 || px == NULL)
		return G2D_ERR_FAILED;
	base = (uint8_t*)px;

	memcpy(base, &hdr, sizeof(hdr));
	off = (uint32_t)sizeof(hdr);
	if (hdr.vs_qpu_words && vs->qpu) {
		memcpy(base + off, vs->qpu, hdr.vs_qpu_words * 4u);
		off += hdr.vs_qpu_words * 4u;
	}
	if (hdr.fs_qpu_words && fs->qpu) {
		memcpy(base + off, fs->qpu, hdr.fs_qpu_words * 4u);
		off += hdr.fs_qpu_words * 4u;
	}
	if (hdr.coord_qpu_words && cs->qpu) {
		memcpy(base + off, cs->qpu, hdr.coord_qpu_words * 4u);
		off += hdr.coord_qpu_words * 4u;
	}
	if (hdr.vs_num_uniforms && vs->uniform_contents && vs->uniform_data) {
		memcpy(base + off, vs->uniform_contents, hdr.vs_num_uniforms * 4u);
		off += hdr.vs_num_uniforms * 4u;
		memcpy(base + off, vs->uniform_data, hdr.vs_num_uniforms * 4u);
		off += hdr.vs_num_uniforms * 4u;
	}
	if (hdr.fs_num_uniforms && fs->uniform_contents && fs->uniform_data) {
		memcpy(base + off, fs->uniform_contents, hdr.fs_num_uniforms * 4u);
		off += hdr.fs_num_uniforms * 4u;
		memcpy(base + off, fs->uniform_data, hdr.fs_num_uniforms * 4u);
		off += hdr.fs_num_uniforms * 4u;
	}
	if (hdr.coord_num_uniforms && cs->uniform_contents && cs->uniform_data) {
		memcpy(base + off, cs->uniform_contents, hdr.coord_num_uniforms * 4u);
		off += hdr.coord_num_uniforms * 4u;
		memcpy(base + off, cs->uniform_data, hdr.coord_num_uniforms * 4u);
		off += hdr.coord_num_uniforms * 4u;
	}
	if (hdr.cs_qpu_words && cu->qpu) {
		memcpy(base + off, cu->qpu, hdr.cs_qpu_words * 4u);
		off += hdr.cs_qpu_words * 4u;
	}
	if (hdr.cs_num_uniforms && cu->uniform_contents && cu->uniform_data) {
		memcpy(base + off, cu->uniform_contents, hdr.cs_num_uniforms * 4u);
		off += hdr.cs_num_uniforms * 4u;
		memcpy(base + off, cu->uniform_data, hdr.cs_num_uniforms * 4u);
		off += hdr.cs_num_uniforms * 4u;
	}
	/* cs2 (xform) stage payload: QPU then uniform contents+data, appended
	   after the cs sections (the wire order bsp_g3d_program walks). */
	if (hdr.cs2_qpu_words && xu->qpu) {
		memcpy(base + off, xu->qpu, hdr.cs2_qpu_words * 4u);
		off += hdr.cs2_qpu_words * 4u;
	}
	if (hdr.cs2_num_uniforms && xu->uniform_contents && xu->uniform_data) {
		memcpy(base + off, xu->uniform_contents, hdr.cs2_num_uniforms * 4u);
		off += hdr.cs2_num_uniforms * 4u;
		memcpy(base + off, xu->uniform_data, hdr.cs2_num_uniforms * 4u);
		off += hdr.cs2_num_uniforms * 4u;
	}

	g2d_pgl_program_req_init(&req, g2d_canvas(shm, total, total / 4u, 1u, 1));
	handle = g2d_pgl_program(&req);
	g2d_shm_free(px);
	return handle;
}

int pgl_g3d_shader_upload(pgl_g3d_prog p)
{
	prog_entry_t* e;
	int handle;

	if (!pgl_g3d_program_valid(p))
		return PGL_G3D_PROG_INVALID;
	e = &_progs[p];

	/* the sticky decline wins over a cached handle: pgl_g3d_shader_mark_declined
	   latches an already-uploaded program whose DRAW the back end refused */
	if (e->upload_declined)
		return G2D_ERR_NOT_SUPPORTED; /* sticky: stop retrying */
	if (e->uploaded)
		return e->daemon_handle;      /* already resident on the daemon */
	if (!pgl_g3d_enabled())
		return G2D_ERR_NOT_SUPPORTED; /* GPU path off: nothing to upload to */

	handle = program_upload(e);
	if (handle >= 0) {
		e->daemon_handle = handle;
		e->uploaded = 1;
		return handle;
	}
	/* A sticky NOT_SUPPORTED means the platform has no compiled-shader path
	   (the caller keeps its CPU rasterizer); a transient FAILED may succeed
	   on a later frame, so only the sticky case is latched. */
	if (handle == G2D_ERR_NOT_SUPPORTED)
		e->upload_declined = 1;
	return handle;
}

int pgl_g3d_shader_daemon_handle(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p) || !_progs[p].uploaded)
		return PGL_G3D_PROG_INVALID;
	return _progs[p].daemon_handle;
}

void pgl_g3d_shader_mark_declined(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return;
	/* Same sticky latch an upload decline uses: pgl_g3d_shader_upload now
	   answers NOT_SUPPORTED for p, so every compiled draw of THIS program
	   returns 0 to its caller (CPU rasterizer) while other programs and the
	   fixed-function path keep the GPU. */
	_progs[p].upload_declined = 1;
}

int pgl_g3d_program_declined(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].upload_declined;
}

void pgl_g3d_program_free(pgl_g3d_prog p)
{
	if (p < 0 || p >= PGL_G3D_MAX_PROGS)
		return;
	if (_progs[p].used)
		entry_clear(&_progs[p]);
}

void pgl_g3d_program_free_all(void)
{
	int i;
	for (i = 0; i < PGL_G3D_MAX_PROGS; i++)
		if (_progs[i].used)
			entry_clear(&_progs[i]);
}

#endif /* PGL_G3D */
