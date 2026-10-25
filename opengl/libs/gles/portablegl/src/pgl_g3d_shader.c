/*
 * pgl_g3d_shader.c - portablegl on-device GLSL shader-program cache (WS4
 * client half, t7c).  Compiled only under -DPGL_G3D.
 *
 * This is the APP-SIDE compile step of the GPU-wiring architecture: an effect
 * hands a GLSL vertex+fragment pair to pgl_g3d_program_compile(), which runs
 * the machine GPU compiler's pglc_compile_glsl() IN THIS PROCESS (the app
 * cannot map the GPU MMIO, but it can compile) and caches the resulting ISA
 * words (struct pglc_stage).  A later draw (t7d/t7e) marshals the cached
 * program through the stateless
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
#include <pglcompiler.h>
#include <geom_raster_cs.h>
#include <g2dclient/g2dclient.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Fixed compiled-program cache depth.  shadertoy ships 11 effects; 16 leaves
   room for a couple of extra variants before an eviction-free compile fails. */
#define PGL_G3D_MAX_PROGS 16

/* ---------------------------------------------------------------------------
 * PERSISTENT DISK CACHE for compiled shader programs.
 *
 * The vendored mesa V3D register-allocation ladder recompiles from scratch on
 * EVERY process launch (costing ~10-18s per strategy attempt, minutes total
 * for a heavy raymarch shader).  This cache stores the serialized daemon blob
 * plus the uniform-binding reflection table so subsequent launches skip the
 * compile entirely and upload the cached blob in <100ms.
 *
 * Cache location: /data/pgl_shader_cache/<gpu_ver>_<hash>.bin
 * Format (all little-endian):
 *   [magic "PGLC" 4B][version 4B][gpu_ver 4B][source_hash 8B]
 *   [blob_size 4B][blob_data blob_size B]
 *   [cs_num_bindings 4B][cs_bindings cs_num_bindings * 72B]
 *   [cs_uniform_blob_dwords 4B][cs_local_size 12B]
 *   [xs_num_bindings 4B][xs_bindings ...]
 *   [xs_uniform_blob_dwords 4B][xs_local_size 12B]
 *   [flags: is_geom geom_model geom_tex geom_texcol geom_inst geom_inst_lit
 *           geom_tpw  -- 7 * 4B]
 * ------------------------------------------------------------------------- */
#define PGL_DISK_CACHE_DIR   "/tmp/pgl_shader_cache"
#define PGL_DISK_CACHE_MAGIC 0x434C4750u  /* 'P','G','L','C' */
#define PGL_DISK_CACHE_VER   1u

typedef struct {
	int      used;
	uint32_t hash;   /* FNV-1a pre-filter over both sources */
	char*    vert;   /* strdup'd source: exact-match cache validation */
	char*    frag;
	/* Daemon-side upload state (t7d): the compiled program is shipped to g2dd
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
	/* INSTANCED-LIT flavour (PGL_G3D_INSTANCED_LIT marker): same bindings
	   and cs_is_geom=3 dispatch as geom_inst, but the xform kernel is
	   geom_xform_instlit_cs_glsl - 40-float per-instance records carrying
	   emissive + alpha and the engine demo's full lighting model. */
	int      geom_inst_lit;
	int      geom_tpw;
	struct pglc_program prog;
	/* Two-pass geometry rasterizer: the VERTEX-TRANSFORM kernel (pass 1) is
	   stored here as its own compiled program (its COMPUTE stage), separate
	   from prog.stage[COMPUTE] which holds the RASTER kernel (pass 2).  Both
	   are attached together or neither is (is_geom stays 0), so a geom draw
	   always has both stages.  uniform_blob_dwords of this stage's COMPUTE
	   slot is the split point of the concatenated per-draw blob. */
	struct pglc_program xform_prog;
	/* Disk-cache: the serialized daemon blob loaded from / saved to disk.
	   When non-NULL, program_upload() uses this directly instead of calling
	   pglc_program_serialize(), and the compile step is skipped entirely on
	   a cache hit. */
	uint8_t* cached_blob;
	uint32_t cached_blob_size;
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

/* 64-bit FNV-1a for the disk-cache key (wider than the in-memory pre-filter
   so cache filenames are effectively collision-free across apps). */
static uint64_t hash_sources_64(const char* v, const char* f)
{
	uint64_t h = 14695981039346656037ULL;
	const unsigned char* p;
	for (p = (const unsigned char*)v; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
	h ^= 0x1Fu; h *= 1099511628211ULL;
	for (p = (const unsigned char*)f; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
	return h;
}

/* Build the cache file path: /data/pgl_shader_cache/<gpu_ver>_<hash64>.bin */
static void cache_path(char* buf, size_t bufsz, uint32_t gpu_ver, uint64_t h64)
{
	snprintf(buf, bufsz, "%s/%u_%016llx.bin",
	         PGL_DISK_CACHE_DIR, gpu_ver, (unsigned long long)h64);
}

static void cache_ensure_dir(void)
{
	static int tried = 0;
	if (tried) return;
	tried = 1;
	mkdir(PGL_DISK_CACHE_DIR, 0755);
}

/* Read a cached program from disk.  Returns 1 on success (entry populated),
   0 on miss/corruption.  On success, e->cached_blob is malloc'd and the
   uniform_bindings for the COMPUTE stage(s) are restored. */
static int cache_load(prog_entry_t* e, uint32_t gpu_ver, uint64_t h64)
{
	char path[256];
	FILE* fp;
	uint32_t magic, ver, gver, blob_size;
	uint64_t stored_hash;
	uint8_t* blob = NULL;
	struct pglc_uniform_binding* cs_b = NULL;
	struct pglc_uniform_binding* xs_b = NULL;
	uint32_t cs_ubd, xs_ubd;
	uint32_t cs_ls[3], xs_ls[3];
	uint32_t cs_nb, xs_nb;
	uint32_t flags[7];

	cache_path(path, sizeof(path), gpu_ver, h64);
	fp = fopen(path, "rb");
	if (!fp) return 0;

	/* header */
	if (fread(&magic, 4, 1, fp) != 1 || magic != PGL_DISK_CACHE_MAGIC) goto fail;
	if (fread(&ver, 4, 1, fp) != 1 || ver != PGL_DISK_CACHE_VER) goto fail;
	if (fread(&gver, 4, 1, fp) != 1 || gver != gpu_ver) goto fail;
	if (fread(&stored_hash, 8, 1, fp) != 1 || stored_hash != h64) goto fail;

	/* blob */
	if (fread(&blob_size, 4, 1, fp) != 1 || blob_size == 0 || blob_size > 32u*1024u*1024u)
		goto fail;
	blob = (uint8_t*)malloc(blob_size);
	if (!blob) goto fail;
	if (fread(blob, 1, blob_size, fp) != blob_size) goto fail;

	/* cs uniform bindings */
	if (fread(&cs_nb, 4, 1, fp) != 1) goto fail;
	if (cs_nb > 4096) goto fail;
	if (cs_nb > 0) {
		cs_b = (struct pglc_uniform_binding*)calloc(cs_nb, sizeof(*cs_b));
		if (!cs_b) goto fail;
		if (fread(cs_b, sizeof(*cs_b), cs_nb, fp) != cs_nb) goto fail;
	}
	if (fread(&cs_ubd, 4, 1, fp) != 1) goto fail;
	if (fread(cs_ls, 4, 3, fp) != 3) goto fail;

	/* xs (xform) uniform bindings */
	if (fread(&xs_nb, 4, 1, fp) != 1) goto fail;
	if (xs_nb > 4096) goto fail;
	if (xs_nb > 0) {
		xs_b = (struct pglc_uniform_binding*)calloc(xs_nb, sizeof(*xs_b));
		if (!xs_b) goto fail;
		if (fread(xs_b, sizeof(*xs_b), xs_nb, fp) != xs_nb) goto fail;
	}
	if (fread(&xs_ubd, 4, 1, fp) != 1) goto fail;
	if (fread(xs_ls, 4, 3, fp) != 3) goto fail;

	/* flags */
	if (fread(flags, 4, 7, fp) != 7) goto fail;
	fclose(fp);

	/* Populate the entry.  The pglc_program stages get their uniform
	   bindings restored but code=NULL (the blob IS the code, already
	   serialized for the daemon).  program_upload() will use cached_blob
	   directly. */
	e->cached_blob = blob;
	e->cached_blob_size = blob_size;

	e->prog.stage[PGLC_STAGE_COMPUTE].uniform_bindings = cs_b;
	e->prog.stage[PGLC_STAGE_COMPUTE].num_uniform_bindings = cs_nb;
	e->prog.stage[PGLC_STAGE_COMPUTE].uniform_blob_dwords = cs_ubd;
	e->prog.stage[PGLC_STAGE_COMPUTE].local_size[0] = cs_ls[0];
	e->prog.stage[PGLC_STAGE_COMPUTE].local_size[1] = cs_ls[1];
	e->prog.stage[PGLC_STAGE_COMPUTE].local_size[2] = cs_ls[2];
	/* Mark code_words non-zero so has_compute() passes; the actual code
	   pointer stays NULL because upload uses cached_blob. */
	e->prog.stage[PGLC_STAGE_COMPUTE].code_words = 1;

	e->xform_prog.stage[PGLC_STAGE_COMPUTE].uniform_bindings = xs_b;
	e->xform_prog.stage[PGLC_STAGE_COMPUTE].num_uniform_bindings = xs_nb;
	e->xform_prog.stage[PGLC_STAGE_COMPUTE].uniform_blob_dwords = xs_ubd;
	e->xform_prog.stage[PGLC_STAGE_COMPUTE].local_size[0] = xs_ls[0];
	e->xform_prog.stage[PGLC_STAGE_COMPUTE].local_size[1] = xs_ls[1];
	e->xform_prog.stage[PGLC_STAGE_COMPUTE].local_size[2] = xs_ls[2];
	if (xs_nb > 0 || xs_ubd > 0)
		e->xform_prog.stage[PGLC_STAGE_COMPUTE].code_words = 1;

	e->is_geom       = flags[0];
	e->geom_model    = flags[1];
	e->geom_tex      = flags[2];
	e->geom_texcol   = flags[3];
	e->geom_inst     = flags[4];
	e->geom_inst_lit = flags[5];
	e->geom_tpw      = flags[6];
	return 1;

fail:
	if (fp) fclose(fp);
	free(blob);
	free(cs_b);
	free(xs_b);
	return 0;
}

/* Write a compiled program to the disk cache.  Best-effort: failures are
   silently ignored (the app still works, it just recompiles next launch). */
static void cache_store(const prog_entry_t* e, uint32_t gpu_ver, uint64_t h64,
                        const uint8_t* blob, uint32_t blob_size)
{
	char path[256];
	FILE* fp;
	uint32_t magic = PGL_DISK_CACHE_MAGIC, ver = PGL_DISK_CACHE_VER;
	uint32_t cs_nb, xs_nb, cs_ubd, xs_ubd;
	uint32_t flags[7];
	const struct pglc_stage* cs = &e->prog.stage[PGLC_STAGE_COMPUTE];
	const struct pglc_stage* xs = &e->xform_prog.stage[PGLC_STAGE_COMPUTE];

	cache_ensure_dir();
	cache_path(path, sizeof(path), gpu_ver, h64);
	fp = fopen(path, "wb");
	if (!fp) return;

	fwrite(&magic, 4, 1, fp);
	fwrite(&ver, 4, 1, fp);
	fwrite(&gpu_ver, 4, 1, fp);
	fwrite(&h64, 8, 1, fp);
	fwrite(&blob_size, 4, 1, fp);
	fwrite(blob, 1, blob_size, fp);

	cs_nb = cs->num_uniform_bindings;
	fwrite(&cs_nb, 4, 1, fp);
	if (cs_nb && cs->uniform_bindings)
		fwrite(cs->uniform_bindings, sizeof(struct pglc_uniform_binding), cs_nb, fp);
	cs_ubd = cs->uniform_blob_dwords;
	fwrite(&cs_ubd, 4, 1, fp);
	fwrite(cs->local_size, 4, 3, fp);

	xs_nb = xs->num_uniform_bindings;
	fwrite(&xs_nb, 4, 1, fp);
	if (xs_nb && xs->uniform_bindings)
		fwrite(xs->uniform_bindings, sizeof(struct pglc_uniform_binding), xs_nb, fp);
	xs_ubd = xs->uniform_blob_dwords;
	fwrite(&xs_ubd, 4, 1, fp);
	fwrite(xs->local_size, 4, 3, fp);

	flags[0] = (uint32_t)e->is_geom;
	flags[1] = (uint32_t)e->geom_model;
	flags[2] = (uint32_t)e->geom_tex;
	flags[3] = (uint32_t)e->geom_texcol;
	flags[4] = (uint32_t)e->geom_inst;
	flags[5] = (uint32_t)e->geom_inst_lit;
	flags[6] = (uint32_t)e->geom_tpw;
	fwrite(flags, 4, 7, fp);
	fclose(fp);
}

static void entry_clear(prog_entry_t* e)
{
	if (e->vert) { free(e->vert); e->vert = NULL; }
	if (e->frag) { free(e->frag); e->frag = NULL; }
	if (e->cached_blob) { free(e->cached_blob); e->cached_blob = NULL; }
	e->cached_blob_size = 0;
	pglc_program_free(&e->prog);   /* frees each stage[].code */
	memset(&e->prog, 0, sizeof(e->prog));
	pglc_program_free(&e->xform_prog);
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
	e->geom_inst_lit = 0;
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

	/* Latch the backend codegen target to the live silicon BEFORE compiling:
	   a backend defaults to its newest device (e.g. the V3D backend to 7.1), so
	   without this a Pi4's geometry kernels would be emitted for 7.1 and wedge
	   the 4.2 CSD on the first dispatch.  Idempotent; see
	   pgl_g3d_ensure_version() in pgl_g3d.c. */
	pgl_g3d_ensure_version();

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

	/* --- DISK CACHE LOOKUP: skip the entire mesa compile when a previous
	   launch already serialized this program.  This is the single biggest
	   startup optimization: a heavy raymarch shader costs 10-18s PER RA
	   strategy attempt (minutes total) on every cold launch; a cache hit
	   loads the pre-compiled blob in <10ms. --- */
	{
		uint32_t gpu_ver = pgl_g3d_ensure_version();
		uint64_t h64 = hash_sources_64(vert_glsl, frag_glsl);
		if (gpu_ver != 0 && cache_load(&_progs[slot], gpu_ver, h64)) {
			_progs[slot].vert = strdup(vert_glsl);
			_progs[slot].frag = strdup(frag_glsl);
			_progs[slot].hash = h;
			_progs[slot].daemon_handle = -1;
			_progs[slot].uploaded = 0;
			_progs[slot].upload_declined = 0;
			_progs[slot].used = 1;
			return (pgl_g3d_prog)slot;
		}
	}

	{
		struct pglc_program res;
		memset(&res, 0, sizeof(res));
		/* A FULL-SCREEN program (no uMVP; shadertoy-style) executes on the GPU
		   ONLY through the ES 3.10 compute wrap, so the ES 1.00 graphics-pair
		   compile is pure waste for that shape: it either rejects the ES3-only
		   syntax the effect uses (explicit-size array constructors like
		   `float[64](...)`, tanh, const-array initialisers, ...) or its VS/FS
		   result is discarded once the compute wrap is attached.  On the heavy
		   raymarch ports that waste is a SECOND full multi-minute on-device
		   compile before the app can show its window, so gate fullscreen
		   programs on the compute wrap ALONE and keep an empty VS/FS pair (the
		   fullscreen route consumes only stage[COMPUTE]; the CPU-interpreter
		   fallback is a separate compiler over the GLSL source and is
		   unaffected).  Non-fullscreen programs keep the ES 1.00 pair compile,
		   but for the MESH shape it is best-effort (see below). */
		int fullscreen = strstr(vert_glsl, "uMVP") == NULL &&
		                 strstr(frag_glsl, "gl_FragCoord") != NULL;
		int mesh = strstr(vert_glsl, "uMVP") != NULL;
		int fs_done = 0;
		int pair_ok = 0;
		if (fullscreen) {
			fs_done = pglc_compile_compute(frag_glsl, &res);
			if (!fs_done) {
				if (errlog && errsz > 0)
					snprintf(errlog, (size_t)errsz,
					         "compute-wrap compile failed: %s",
					         res.log[0] ? res.log : "(no error text)");
				pglc_program_free(&res);
				return PGL_G3D_PROG_INVALID;   /* per-shader: no GPU latch-off */
			}
		} else {
			pair_ok = pglc_compile_glsl(vert_glsl, frag_glsl, &res);
			if (!pair_ok) {
				if (errlog && errsz > 0)
					snprintf(errlog, (size_t)errsz, "%s",
					         res.log[0] ? res.log
					                    : "pglc_compile_glsl failed");
				pglc_program_free(&res);
				/* A MESH program's only GPU route is the fixed two-pass
				   geometry kernel pair attached below, which never consumes
				   the app's VS/FS (the binner cannot rasterise real
				   primitives, and a backend without a graphics-pipeline port
				   - Adreno - declines EVERY pair up front).  Treating the
				   pair's decline as fatal here kept every mesh app
				   (glmatrix/teapot/gears/3ddemo) on the CPU interpreter on
				   q8b, silently and in 0 ms.  So for the mesh shape carry on
				   with an empty VS/FS pair and let the kernel compile decide;
				   errlog already holds the pair's text should that fail too.
				   Any other non-fullscreen shape has no compute route and
				   still needs the pair. */
				if (!mesh)
					return PGL_G3D_PROG_INVALID;   /* per-shader: no GPU latch-off */
				memset(&res, 0, sizeof(res));
			}
		}
		_progs[slot].prog = res;   /* take ownership of res.stage[].code */

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
			struct pglc_program gres, xres;
			int tex = strstr(frag_glsl, "sampler") != NULL;
			/* COLOUR-MODULATED textured variant (imgui): the FS multiplies a
			   vec4 varying by the texture sample (gl_FragColor = vColor *
			   texture2D(...)), which the plain TEX_MOD kernel cannot express
			   (it writes the fetched texel verbatim, no per-vertex colour).  Sniff
			   that product shape; when present, select the texcol kernels - the
			   per-vertex colour is interleaved into binding 1 at a 10-float
			   stride (the compute kernels keep to 4 SSBO bindings), the vout record
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
			   record layout is unchanged.  The INSTANCED-LIT marker is a
			   superset spelling (it CONTAINS the plain marker as a prefix, so
			   sniff it first): 40-float records with per-instance emissive +
			   alpha and the engine demo's reflect-spec lighting model,
			   compiled as geom_xform_instlit_cs_glsl.  Both share the
			   cs_is_geom=3 daemon dispatch and bindings. */
			int inst_lit = !tex &&
			               strstr(vert_glsl, "PGL_G3D_INSTANCED_LIT") != NULL;
			int inst = !tex && !inst_lit &&
			           strstr(vert_glsl, "PGL_G3D_INSTANCED") != NULL;
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
			const char* xform_src  = inst_lit ? geom_xform_instlit_cs_glsl
			                             : (inst ? geom_xform_inst_cs_glsl
			                             : (texcol ? geom_xform_texcol_cs_glsl
			                                    : (tex ? geom_xform_tex_cs_glsl
			                                           : (color ? geom_xform_color_cs_glsl
			                                                  : geom_xform_cs_glsl))));
			memset(&gres, 0, sizeof(gres));
			memset(&xres, 0, sizeof(xres));
			/* BOTH kernels must compile: the raster pass reads the vout
			   records the xform pass writes, so a program with only one of
			   them cannot run.  On any failure neither stage is attached
			   (prog.stage[COMPUTE] stays empty -> has_compute == 0) and the
			   mesh draws on the CPU rasterizer. */
			if (pglc_compile_compute_raw(raster_src, &gres) &&
			    pglc_compile_compute_raw(xform_src, &xres)) {
				/* raster kernel (pass 2) -> prog.stage[COMPUTE] */
				_progs[slot].prog.stage[PGLC_STAGE_COMPUTE] =
					gres.stage[PGLC_STAGE_COMPUTE];   /* move ownership */
				memset(&gres.stage[PGLC_STAGE_COMPUTE], 0,
				       sizeof(gres.stage[PGLC_STAGE_COMPUTE]));
				/* xform kernel (pass 1) -> xform_prog.stage[COMPUTE] */
				_progs[slot].xform_prog.stage[PGLC_STAGE_COMPUTE] =
					xres.stage[PGLC_STAGE_COMPUTE];   /* move ownership */
				memset(&xres.stage[PGLC_STAGE_COMPUTE], 0,
				       sizeof(xres.stage[PGLC_STAGE_COMPUTE]));
				_progs[slot].is_geom = 1;
				_progs[slot].geom_tex = tex;
				_progs[slot].geom_texcol = texcol;
				_progs[slot].geom_inst = inst || inst_lit;
				_progs[slot].geom_inst_lit = inst_lit;
				/* the tex kernel is the legacy one-workgroup-per-triangle shape
				   (0); the gouraud kernel is one-lane-per-triangle and covers 256
				   triangles per 16x16 workgroup, so the daemon dispatches the
				   matching ceil(ntris/256) grid from cs_geom_tpw. */
				_progs[slot].geom_tpw = tex ? 0 : 256;
				_progs[slot].geom_model =
					strstr(vert_glsl, "uLightEye") != NULL ? 1 : 0;
			}
			pglc_program_free(&gres);
			pglc_program_free(&xres);
			/* No VS/FS pair AND no kernels: nothing the GPU could run, so
			   decline instead of caching an empty program (whose draws would
			   only ever bounce off has_compute == 0 on the CPU path). */
			if (!pair_ok && !_progs[slot].is_geom) {
				pglc_program_free(&_progs[slot].prog);
				memset(&_progs[slot].prog, 0, sizeof(_progs[slot].prog));
				return PGL_G3D_PROG_INVALID;   /* errlog: the pair's decline text */
			}
		} else if (!fs_done) {
			struct pglc_program cres;
			memset(&cres, 0, sizeof(cres));
			if (pglc_compile_compute(frag_glsl, &cres)) {
				_progs[slot].prog.stage[PGLC_STAGE_COMPUTE] =
					cres.stage[PGLC_STAGE_COMPUTE];   /* move ownership */
				memset(&cres.stage[PGLC_STAGE_COMPUTE], 0,
				       sizeof(cres.stage[PGLC_STAGE_COMPUTE]));
			}
			pglc_program_free(&cres);
		}
	}

	_progs[slot].vert = strdup(vert_glsl);
	_progs[slot].frag = strdup(frag_glsl);
	if (!_progs[slot].vert || !_progs[slot].frag) {
		/* strdup OOM: roll the half-filled entry back, keep the code from
		   leaking, and decline (the caller falls back to its CPU path). */
		if (_progs[slot].vert) { free(_progs[slot].vert); _progs[slot].vert = NULL; }
		if (_progs[slot].frag) { free(_progs[slot].frag); _progs[slot].frag = NULL; }
		pglc_program_free(&_progs[slot].prog);
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

	/* --- DISK CACHE STORE: serialize the freshly compiled program and persist
	   it so the next launch skips the multi-minute mesa compile entirely. --- */
	{
		uint32_t gpu_ver = pgl_g3d_ensure_version();
		uint64_t h64 = hash_sources_64(vert_glsl, frag_glsl);
		if (gpu_ver != 0) {
			struct pglc_blob_opts copts;
			uint32_t total;
			uint8_t* blob = NULL;
			memset(&copts, 0, sizeof(copts));
			copts.geom_kind = _progs[slot].is_geom
				? (_progs[slot].geom_texcol ? 4
				   : (_progs[slot].geom_tex ? 2
				      : (_progs[slot].geom_inst ? 3 : 1)))
				: 0;
			copts.geom_tpw = (uint32_t)_progs[slot].geom_tpw;
			total = pglc_program_serialize(&_progs[slot].prog,
			                               &_progs[slot].xform_prog,
			                               &copts, NULL, 0);
			if (total > 0 && (total & 3u) == 0) {
				blob = (uint8_t*)malloc(total);
				if (blob) {
					if (pglc_program_serialize(&_progs[slot].prog,
					                           &_progs[slot].xform_prog,
					                           &copts, blob, total) == total) {
						_progs[slot].cached_blob = blob;
						_progs[slot].cached_blob_size = total;
						cache_store(&_progs[slot], gpu_ver, h64, blob, total);
						blob = NULL;  /* ownership moved to entry */
					}
					free(blob);
				}
			}
		}
	}

	return (pgl_g3d_prog)slot;
}

int pgl_g3d_program_valid(pgl_g3d_prog p)
{
	return (p >= 0 && p < PGL_G3D_MAX_PROGS && _progs[p].used);
}

const struct pglc_program* pgl_g3d_shader_get(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return NULL;
	return &_progs[p].prog;
}

int pgl_g3d_shader_has_compute(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].prog.stage[PGLC_STAGE_COMPUTE].code_words != 0;
}

int pgl_g3d_shader_is_geom(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].is_geom &&
	       _progs[p].prog.stage[PGLC_STAGE_COMPUTE].code_words != 0;
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
	       _progs[p].prog.stage[PGLC_STAGE_COMPUTE].code_words != 0;
}

int pgl_g3d_shader_geom_texcol(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_texcol &&
	       _progs[p].prog.stage[PGLC_STAGE_COMPUTE].code_words != 0;
}

int pgl_g3d_shader_geom_inst(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_inst &&
	       _progs[p].prog.stage[PGLC_STAGE_COMPUTE].code_words != 0;
}

int pgl_g3d_shader_geom_inst_lit(pgl_g3d_prog p)
{
	if (!pgl_g3d_program_valid(p))
		return 0;
	return _progs[p].geom_inst_lit &&
	       _progs[p].prog.stage[PGLC_STAGE_COMPUTE].code_words != 0;
}

int pgl_g3d_shader_uniform_dword(pgl_g3d_prog p, const char* name,
                                 uint32_t* num_components)
{
	const struct pglc_stage* fs;
	const struct pglc_stage* vs;
	const struct pglc_stage* cs;
	uint32_t i;

	if (num_components)
		*num_components = 0;
	if (!name || !pgl_g3d_program_valid(p))
		return -1;

	/* Compute route first: when the program carries a compute stage the daemon
	   dispatches THAT stage, whose linker pass assigned its own dword offsets
	   (it also owns the extra `pglc_cs_stride` uniform), so the per-draw blob
	   must follow the compute layout rather than the FS's. */
	cs = &_progs[p].prog.stage[PGLC_STAGE_COMPUTE];
	if (cs->code_words != 0) {
		/* Two-pass geom: the per-draw blob is the CONCATENATION
		   [xform region | raster region].  Search the xform (pass 1) kernel
		   first (region base 0), then the raster (pass 2) kernel offset by the
		   xform region's span.  A name declared by BOTH (uViewportWH) resolves
		   to the XFORM offset here; pgl_g3d_shader_patch_uniform is what writes
		   every region, so the draw path uses that for derived uniforms. */
		if (_progs[p].is_geom) {
			const struct pglc_stage* xs =
				&_progs[p].xform_prog.stage[PGLC_STAGE_COMPUTE];
			for (i = 0; i < xs->num_uniform_bindings; i++) {
				const struct pglc_uniform_binding* ub = &xs->uniform_bindings[i];
				if (strcmp(ub->name, name) == 0) {
					if (num_components)
						*num_components = ub->num_components;
					return (int)ub->dword_offset;
				}
			}
			for (i = 0; i < cs->num_uniform_bindings; i++) {
				const struct pglc_uniform_binding* ub = &cs->uniform_bindings[i];
				if (strcmp(ub->name, name) == 0) {
					if (num_components)
						*num_components = ub->num_components;
					return (int)(xs->uniform_blob_dwords + ub->dword_offset);
				}
			}
			return -1;   /* a geom program never consults the FS/VS layouts */
		}
		for (i = 0; i < cs->num_uniform_bindings; i++) {
			const struct pglc_uniform_binding* ub = &cs->uniform_bindings[i];
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
	fs = &_progs[p].prog.stage[PGLC_STAGE_FRAGMENT];
	for (i = 0; i < fs->num_uniform_bindings; i++) {
		const struct pglc_uniform_binding* ub = &fs->uniform_bindings[i];
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
	vs = &_progs[p].prog.stage[PGLC_STAGE_VERTEX];
	for (i = 0; i < vs->num_uniform_bindings; i++) {
		const struct pglc_uniform_binding* ub = &vs->uniform_bindings[i];
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
	const struct pglc_stage* xs;
	const struct pglc_stage* cs;

	if (!pgl_g3d_program_valid(p) || !_progs[p].is_geom)
		return 0;
	xs = &_progs[p].xform_prog.stage[PGLC_STAGE_COMPUTE];
	cs = &_progs[p].prog.stage[PGLC_STAGE_COMPUTE];
	return xs->uniform_blob_dwords + cs->uniform_blob_dwords;
}

/* Write nc dwords into one region of the concatenated blob at region_base +
   the binding's dword offset, if `name` is declared there and the write fits.
   Returns one past the highest dword written, or 0 when not written. */
static uint32_t patch_region(const struct pglc_stage* st, uint32_t region_base,
                             const char* name, uint32_t* blob, uint32_t blobcap,
                             const uint32_t* val, uint32_t nc)
{
	uint32_t i;

	for (i = 0; i < st->num_uniform_bindings; i++) {
		const struct pglc_uniform_binding* ub = &st->uniform_bindings[i];
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
	const struct pglc_stage* xs;
	const struct pglc_stage* cs;
	uint32_t hi = 0, e;

	if (!name || !val || nc == 0 || !blob ||
	    !pgl_g3d_program_valid(p) || !_progs[p].is_geom)
		return 0;
	xs = &_progs[p].xform_prog.stage[PGLC_STAGE_COMPUTE];
	cs = &_progs[p].prog.stage[PGLC_STAGE_COMPUTE];

	e = patch_region(xs, 0u, name, blob, blobcap, val, nc);
	if (e > hi) hi = e;
	e = patch_region(cs, xs->uniform_blob_dwords, name, blob, blobcap, val, nc);
	if (e > hi) hi = e;
	return hi;
}

/* Serialize one compiled program into a fresh contig-shm canvas holding the
   machine backend's wire blob (the byte layout bsp_g3d_program on the daemon
   side parses: a header followed by its variable payload - V3D's
   g2d_pgl_program_blob_t, Adreno's adreno_prog_blob_t, ...), upload it with
   g2d_pgl_program(), then free the canvas (the daemon copies the code into
   its own GPU-visible dma).  The layout is produced by the backend's
   pglc_program_serialize(): this client never looks inside a stage's
   backend-private state.  Returns the daemon's non-negative program handle,
   or a negative G2D_ERR_* code.  The canvas is modelled w=total/4 h=1 so
   g2d_attach's size >= w*h*4 check holds with size == the blob length.

   FAST PATH: when the entry carries a cached_blob (loaded from the persistent
   disk cache or stored after a fresh compile), the serialization is skipped
   entirely and the pre-built blob is shipped directly. */
static int program_upload(const prog_entry_t* e)
{
	g2d_pgl_program_req_t req;
	uint32_t total;
	uint32_t* px = NULL;
	int shm = 0, handle;

	/* Fast path: use the pre-serialized blob from the disk cache. */
	if (e->cached_blob && e->cached_blob_size > 0) {
		total = e->cached_blob_size;
		if (g2d_shm_alloc(total, &shm, &px) != 0 || shm <= 0 || px == NULL)
			return G2D_ERR_FAILED;
		memcpy(px, e->cached_blob, total);
		g2d_pgl_program_req_init(&req, g2d_canvas(shm, total, total / 4u, 1u, 1));
		handle = g2d_pgl_program(&req);
		g2d_shm_free(px);
		return handle;
	}

	/* Slow path: serialize from the live compiled program. */
	{
		struct pglc_blob_opts opts;
		memset(&opts, 0, sizeof(opts));
		opts.geom_kind = e->is_geom ? (e->geom_texcol ? 4 : (e->geom_tex ? 2 : (e->geom_inst ? 3 : 1))) : 0;
		opts.geom_tpw  = (uint32_t)e->geom_tpw;

		total = pglc_program_serialize(&e->prog, &e->xform_prog, &opts, NULL, 0);
		if (total == 0 || (total & 3u) != 0)
			return G2D_ERR_FAILED;
		if (g2d_shm_alloc(total, &shm, &px) != 0 || shm <= 0 || px == NULL)
			return G2D_ERR_FAILED;
		if (pglc_program_serialize(&e->prog, &e->xform_prog, &opts, px, total) != total) {
			g2d_shm_free(px);
			return G2D_ERR_FAILED;
		}

		g2d_pgl_program_req_init(&req, g2d_canvas(shm, total, total / 4u, 1u, 1));
		handle = g2d_pgl_program(&req);
		g2d_shm_free(px);
		return handle;
	}
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
