/*
 * pgl_g3d.c - portablegl GPU 3D-offload back end, client half.
 * See pgl_g3d.h for the contract.  Compiled only under -DPGL_G3D (a platform
 * opts in by exporting PGL_G3D=yes from its system/make.inc).
 *
 * Everything here is a thin marshaller over the stateless g2dclient protocol:
 * it owns one contig-shm render target (which portablegl adopts as its
 * back_buffer so CPU and GPU draws share one surface) plus fixed vertex/varying
 * staging, batches post-vertex-shader triangles, and turns a full batch into a
 * single G2D_DEV_CNTL_PGL_DRAW.  The g2dd daemon resolves each canvas's
 * physical base and forwards to the platform 3D driver through bsp_g3d; this
 * file never touches any GPU directly.
 *
 * The capability latch is deliberately conservative: a submitted dispatch can
 * never be replayed, so the first non-OK result (G2D_ERR_NOT_SUPPORTED, or a
 * transient G2D_ERR_FAILED) latches the GPU off for the rest of the process and
 * the caller keeps its CPU rasterizer.  init() probes with a clear into the
 * freshly-allocated target (which the caller overwrites) so an unsupported
 * platform is discovered before any triangle is batched.
 */

#ifdef PGL_G3D

#include <portablegl/pgl_g3d.h>
#include <g2dclient/g2dclient.h>
#include <string.h>
#include <stdlib.h>
#include "pgl_g3d_shader.h"   /* private bridge: pgl_g3d_shader_upload/_daemon_handle */
#include <pglcompiler.h>      /* pglc_tex_layout_2d / pglc_tex_store_2d (GPU-layout uploads) */

/* PGL_G3D_MAX_VERTS (the one-dispatch vertex cap and staging size) is defined
   in the public pgl_g3d.h so apps can split an over-cap mesh into sub-ranges. */
/* Fixed-function GPU batching (SOLID/GOURAUD/TEX_MOD built-ins through the
   daemon's hand-written QPU shaders).  0: every such primitive stays on the CPU
   rasterizer and only mesa-compiled programs (teapot, shadertoy) reach the
   binner.  Keep in step with V3D_3D_ENABLE_FIXED_DRAW in v3d_3d.c. */
#define PGL_G3D_FIXED_FUNCTION 0
/* pos is modelled as a w=nverts h=3 canvas (nverts*3*4 bytes); col as a
   w=nverts h=1 canvas (nverts*1*4 bytes) so g2d_attach's size >= w*h*4 holds.
   The TEXTURED geom variant interleaves pos+uv into the pos staging at a
   6-float stride, and the COLOUR-MODULATED textured variant (imgui,
   cs_is_geom=4) interleaves pos+uv+colour at a 10-float stride (V3D 7.1
   compute exposes only 4 SSBO bindings), so the pos buffer is sized for the
   worst case of any layout: nverts*10*4 bytes. */
#define PGL_G3D_POS_BYTES (PGL_G3D_MAX_VERTS * 10u * 4u)
#define PGL_G3D_COL_BYTES (PGL_G3D_MAX_VERTS * 1u * 4u)
/* Phase D: uv float3 per vertex (u, v, pad=0) sharing the pos encoding's
   12-byte stride, so the driver reuses its proven vec_size=3/type=FLOAT
   attribute record instead of an untested float2 combination. */
#define PGL_G3D_UV_BYTES  (PGL_G3D_MAX_VERTS * 3u * 4u)
/* WS4 compiled-mesh normal staging: float3 per vertex (12-byte stride), the
   attribute-1 (normal) array a 2-attribute compiled mesh draw re-stages into.
   Modelled exactly like pos so the driver reuses the proven float3 record. */
#define PGL_G3D_NRM_BYTES (PGL_G3D_MAX_VERTS * 3u * 4u)
/* WS4: per-draw GL uniform blob staged for the daemon's QUNIFORM interpreter
   (u32 words, CPU-read; count matches PGL_G3D_MAX_UNIF in the public header). */
#define PGL_G3D_UNIF_BYTES (PGL_G3D_MAX_UNIF * 4u)

static int      _probed;     /* init() has run (success or not) */
static int      _enabled;    /* GPU path engaged and not latched off */

static int      _w, _h;
/* physical fb/z capacity in pixels (>= _w*_h; alloc_target reserves it with
   grow-with-headroom, see fb_grow_capacity).  Resize reuses the contig-slab
   runs whenever the new viewport fits this capacity, so a shrink - or any
   in-capacity resize - performs zero slab alloc/free operations. */
static uint32_t _fb_cap;
static int      _fb_shm;
static uint32_t* _fb;        /* render-target pixels (RGBA8) */
static ewokos_addr_t _fb_phy;

/* Fullscreen-compute render-scale scratch (see pgl_g3d_set_render_scale): when
   _render_scale > 1 a non-geom fullscreen effect is dispatched into this reduced
   rw x rh target and upscaled to _fb, trading sharpness for throughput on
   spill-heavy raymarch kernels that cannot reach interactive FPS at native res
   on V3D CSD.  Grow-only like _fb; 1 = off (the historical full-res path). */
static int      _rs_num = 1;   /* render-scale as a rational num/den: the internal
                                  compute target is fb * den / num, so num/den > 1
                                  downsamples.  Fractional values (e.g. 3/2) sample
                                  the source aliasing finer than an integer divisor. */
static int      _rs_den = 1;
static int      _rs_shm;
static uint32_t* _rs;        /* reduced-res scratch target pixels (RGBA8) */
static ewokos_addr_t _rs_phy;
static uint32_t _rs_cap;     /* scratch capacity in pixels */
static int      _rs_w, _rs_h; /* active reduced render dims (0 = full-res) */

/* Temporal AA for the render-scale path (see pgl_g3d_set_render_taa).  The
   low-res compute grid is cycled by +-1 px per frame so consecutive frames
   sample the scene at different sub-pixel phases; each upscaled frame is then
   constant-alpha accumulated into a persistent full-res history (_taa_hist)
   via _taa_tmp, and the history is composited to the fb.  Averaging genuine
   multi-phase scene samples is what melts the aliasing a single low-res pass
   bakes in; a plain blur cannot.  Only meaningful while render-scale is on.
   DEFAULT OFF: on-device A/B (freeze, scale2) showed the +-1 px grid nudge
   yields too little LOCAL sub-pixel phase diversity to smooth edges (laplacian
   unchanged vs TAA off), so it costs two full-res blits for no gain.  Kept as
   an opt-in experiment via pgl_g3d_set_render_taa(). */
static int      _taa = 0;
static int      _taa_shm, _taat_shm;
static uint32_t* _taa_hist;     /* accumulated full-res history (RGBA8) */
static uint32_t* _taa_tmp;      /* this frame's upscaled sample (RGBA8) */
static ewokos_addr_t _taa_hist_phy, _taa_tmp_phy;
static uint32_t _taa_cap, _taat_cap;
static int      _taa_valid;     /* history holds a real frame (else copy) */
static int      _taa_phase;     /* grid-variation phase counter */
static int      _taa_prog = -1; /* prog the history was built from */
static uint32_t _taa_alpha = 96;/* new-sample weight (~0.375) */

static int      _pos_shm;
static float*   _pos;        /* float3 NDC per vertex */
static ewokos_addr_t _pos_phy;
static int      _col_shm;
static uint32_t* _col;        /* byte4 RGBA8 per vertex */
static ewokos_addr_t _col_phy;
/* Phase D uv staging (float3 per vertex: u, v, pad); allocated once at init
   alongside _pos / _col so the TEX_MOD batch path never allocates on the hot
   path. */
static int      _uv_shm;
static float*   _uv;
static ewokos_addr_t _uv_phy;

/* WS4 compiled-mesh normal staging (float3 per vertex): the attribute-1 array a
   2-attribute compiled mesh draw re-stages its sub-range into, sent as the col
   canvas (col_iova != 0 is the back end's 2-attribute signal).  Allocated once
   at init alongside _pos; a failure here does NOT latch the GPU off - the
   fixed-function and full-screen-quad compiled paths stay live and only the
   mesh path declines. */
static int      _nrm_shm;
static float*   _nrm;
static ewokos_addr_t _nrm_phy;

/* WS4 compiled-mesh table: a pos+normal mesh de-interleaved by
   pgl_g3d_upload_mesh and drawn in sub-ranges by pgl_g3d_draw_mesh_compiled.
   The source arrays are plain CPU memory (malloc); EVERY draw re-stages its
   range into the fixed base-phy _pos/_nrm GPU staging allocated once at init.
   The slot deliberately owns no GPU segment of its own: the GLES engine
   uploads, draws and frees a mesh per coalesced batch (gpu_coalesce_flush),
   so a per-slot segment would mean two contig shm alloc/free round trips per
   draw plus two guaranteed g2dd attach-cache misses (fresh shm ids), with the
   daemon pinning the dead segments until its TTL sweep - exactly the cost a
   fixed staging exists to avoid.  A static GPU VBO addressed by a per-draw
   phy offset is NOT usable either: g2dd's attach cache is keyed by shm_id and
   returns the cached base phy on a hit, ignoring canvas->phy.  A slot is
   "used" iff .used is set: the gouraud/tex/instanced layouts fill .pos
   (+.nrm/.idx), the colour-modulated textured layout (cs_is_geom=4) fills
   .tcol instead and leaves .pos NULL. */
typedef struct {
	int      used;
	uint32_t nverts;
	float*   pos;   /* float3 per vertex */
	float*   nrm;   /* float3 per vertex */
	float*   idx;   /* float1 per vertex (instance index); NULL unless instanced */
	/* COLOUR-MODULATED textured mesh (imgui, cs_is_geom=4): the raw
	   interleaved pos.xyz+uv.xy+rgba+pad stream at a 10-float stride, kept
	   verbatim (pos/nrm/idx stay NULL) and memcpy'd straight into the pos
	   staging per draw.  tcol_stride is 10. */
	float*   tcol;
	uint32_t tcol_stride;
	int      instanced;
} pgl_g3d_mesh_slot_t;
#define PGL_G3D_MAX_MESH 8
static pgl_g3d_mesh_slot_t _mesh[PGL_G3D_MAX_MESH];

/* WS4 instanced-mesh per-frame matrix buffer (gears): one 36-float record per
   instance (MVP mat4 + normal mat4 + material rgb + pad) staged by
   pgl_g3d_set_instances into a GPU-resident segment and marshalled as ctex
   unit 0 by an instanced pgl_g3d_draw_mesh_compiled (binding 3 of the
   instanced xform kernel).  Grown on demand; _inst_floats is the count last
   staged, _inst_cap the allocated float capacity. */
static int      _inst_shm;
static float*   _inst;
static ewokos_addr_t _inst_phy;
static uint32_t _inst_floats;
static uint32_t _inst_cap;

/* Phase D texture table (see PGL_G3D_MAX_TEX): each slot owns a contig-shm
   RGBA8 image addressable by physical base.  A slot is "loaded" iff
   .pixels != NULL.  tiling is the backend-defined GPU memory layout of the
   pixels (pglc_tex_layout.tiling): PGLC_TEX_TILING_RASTER for the
   fixed-function TEX_MOD kernel (which reads rows at w*4 pitch), a backend
   tiled layout (V3D: LT/UBLINEAR/UIF) for compiled-shader sampler units when
   the GPU's sampler cannot read a raster 2D image; stride is the allocated
   (padded) row pitch and ub_pad the backend's padding (V3D UIF level_0_ub_pad). */
typedef struct {
	int            shm;
	uint32_t*      pixels;
	ewokos_addr_t  phy;
	int            w, h;
	uint32_t       tiling;
	uint32_t       ub_pad;
	uint32_t       stride;
	uint32_t       size;     /* bytes allocated (padded image) */
	/* Cubemap / 2D-array staging: cube != 0 marks a slot holding `depth`
	   tiled layers stored contiguously at a 64-byte-aligned per-layer
	   stride (arrstride64 * 64 bytes); size then covers all depth layers.
	   A plain 2D slot leaves cube/depth/arrstride64 at 0. */
	int            cube;
	uint32_t       depth;
	uint32_t       arrstride64;
} pgl_g3d_tex_slot_t;
static pgl_g3d_tex_slot_t _tex[PGL_G3D_MAX_TEX];

static uint32_t _nverts;     /* vertices currently batched */
static uint32_t _batch_mode; /* primitive mode of the open batch (PGL_G3D_MODE_*) */
static int      _batch_fs;   /* fs_id of the open batch */
static uint32_t _batch_rgba; /* solid colour of the open batch */
/* depth/cull state of the open batch (a batch is homogeneous in these too,
   because they are per-dispatch render state on the GPU) */
static uint32_t _batch_depth_func;
static uint32_t _batch_depth_mask;
static uint32_t _batch_cull;
/* Phase D: texture slot of the open batch (only meaningful when _batch_fs ==
   PGL_G3D_FS_TEX_MOD); part of the homogeneity key so a TEX_MOD batch never
   mixes two texture images in one dispatch.  -1 = "no texture". */
static int      _batch_tex_slot = -1;
/* WS4 compiled-shader sampler-unit bindings: _ctex_bind[u] is the texture slot
   bound to sampler unit u (iChannelU), or -1 when unbound.  Set by
   pgl_g3d_bind_tex_unit and marshalled into req.ctex[] on flush so the daemon
   resolves each bound image's physical base for the back end's TMU_CONFIG
   expansion.  Persistent GL-style unit state, re-established per frame by the
   caller (portablegl's pgl_gpu_frame). */
static int      _ctex_bind[PGL_G3D_MAX_TEX_UNITS] = { -1, -1, -1, -1 };
/* per-unit TMU filter (PGL_G3D_TEX_FILTER_*), set alongside the binding and
   packed into the high nibble of req.ctex_ub_pad[u] on flush. */
static int      _ctex_filter[PGL_G3D_MAX_TEX_UNITS];
/* Phase B2c extended render state of the open batch (scissor / polygon offset /
   colour mask); part of the homogeneity key, copied into req.ext on flush. */
static pgl_g3d_ext_t _batch_ext;

/* Per-draw scissor window for the compiled colour-modulated textured route
   (cs_is_geom=4, imgui): fb row space (y down), w/h<=0 disables.  Set by the
   engine from glScissor before each texcol draw and patched into the raster
   kernel's uScissor uniform by pgl_g3d_draw_mesh_compiled; the other geom
   variants declare no uScissor and ignore it. */
static int32_t _scissor_x, _scissor_y, _scissor_w, _scissor_h;

/* Per-draw fragment blend mode (PGL_G3D_BLEND_*) patched into the gouraud
   raster kernel's uBlendMode uniform.  Set by the engine from its glBlendFunc
   state before each draw/batch; only the INSTANCED-LIT/gouraud raster kernel
   reads it (the textured/texcol kernels keep their fixed equations and ignore
   the patch since they declare no uBlendMode). */
static int _blend_mode = PGL_G3D_BLEND_OPAQUE;

/* WS4 compiled-shader draw-path state: the active on-device-compiled program
   plus the per-draw GL uniform blob the daemon's QUNIFORM interpreter indexes.
   The uniform staging is a CPU-read contig-shm segment (the daemon copies the
   words; the GPU never reads it directly), allocated once at init.  _unif_gen
   bumps on every pgl_g3d_set_uniforms so the blob joins the batch homogeneity
   key - a batch never mixes two uniform blobs.  _batch_prog/_batch_unif_gen
   snapshot the values the open batch was reserved with. */
static pgl_g3d_prog _active_prog = PGL_G3D_PROG_INVALID;
static int      _unif_shm;
static uint32_t* _unif;      /* per-draw GL uniform blob (u32 words) */
static uint32_t _unif_count;
static uint32_t _unif_gen;
static pgl_g3d_prog _batch_prog = PGL_G3D_PROG_INVALID;
static uint32_t _batch_unif_gen;

/* persistent Z32F depth surface (linear, w*4 stride): depth survives across
   the per-draw dispatches of one frame so a multi-draw frame stays coherent
   instead of each dispatch resetting tile-local Z. */
static int      _z_shm;
static uint32_t* _z;
static ewokos_addr_t _z_phy;
/* frame boundary: the next draw dispatch is the frame's first, so it clears
   colour to _clear_rgba and Z to _clear_z; later dispatches LOAD the pair. */
static int      _first_frame;
static uint32_t _clear_rgba;
static float    _clear_z;
/* WS4 geom route: set once the frame's first geom draw has succeeded with the
   daemon clearing fb+depth GPU-side (draw_compute_geom's first_frame fills);
   later frames skip the ~2.5MB NOCACHE CPU clear in pgl_g3d_draw_mesh_compiled. */
static int      _geom_fill_live;

int pgl_g3d_enabled(void)
{
	return _enabled;
}

void* pgl_g3d_pixels(void)
{
	return (void*)_fb;
}

int pgl_g3d_pixels_shm(void)
{
	return (_enabled && _fb != NULL) ? _fb_shm : 0;
}

void pgl_g3d_set_scissor(int32_t x, int32_t y, int32_t w, int32_t h)
{
	_scissor_x = x;
	_scissor_y = y;
	_scissor_w = w;
	_scissor_h = h;
}

void pgl_g3d_set_blend(int mode)
{
	_blend_mode = mode;
}

static uint8_t clamp_u8(float f)
{
	if (f <= 0.0f) return 0;
	if (f >= 1.0f) return 255;
	return (uint8_t)(f * 255.0f + 0.5f);
}

/* pack 4 normalized floats (r,g,b,a) into the col staging at vertex index i as
   bytes R,G,B,A in memory order (the vtx shader reads in[3..6]=rgba) */
static void put_col_f(int i, const float* rgba)
{
	uint8_t* p = (uint8_t*)&_col[i];
	p[0] = clamp_u8(rgba[0]);
	p[1] = clamp_u8(rgba[1]);
	p[2] = clamp_u8(rgba[2]);
	p[3] = clamp_u8(rgba[3]);
}

/* pack a 0xAABBGGRR solid colour into the col staging at vertex index i */
static void put_col_u32(int i, uint32_t rgba8)
{
	uint8_t* p = (uint8_t*)&_col[i];
	p[0] = (uint8_t)(rgba8 & 0xFF);         /* R */
	p[1] = (uint8_t)((rgba8 >> 8) & 0xFF);  /* G */
	p[2] = (uint8_t)((rgba8 >> 16) & 0xFF); /* B */
	p[3] = (uint8_t)((rgba8 >> 24) & 0xFF); /* A */
}

static void put_pos(int i, const float* n)
{
	_pos[i*3 + 0] = n[0];
	_pos[i*3 + 1] = n[1];
	_pos[i*3 + 2] = n[2];
}

/* Phase D: stage uv as float3 (u, v, 0).  The third component is discarded by
   the tex vertex shader (it reads in[3..4]=u,v); reusing the pos encoding's
   12-byte stride avoids an untested float2 vec_size/nvals attribute record. */
static void put_uv(int i, const float* uv)
{
	_uv[i*3 + 0] = uv[0];
	_uv[i*3 + 1] = uv[1];
	_uv[i*3 + 2] = 0.0f;
}

static g2d_canvas_t fb_canvas(void)
{
	/* contig=1 so the daemon addresses the target by physical base; phy is
	   pre-filled from the allocation so g2dd skips its resolve syscall */
	g2d_canvas_t cv = g2d_canvas(_fb_shm, (uint32_t)_w * (uint32_t)_h * 4u,
	                             (uint32_t)_w, (uint32_t)_h, 1);
	cv.phy = _fb_phy;
	return cv;
}

static g2d_canvas_t z_canvas(void)
{
	g2d_canvas_t cv;
	/* no persistent Z surface: send a zero-size canvas so the daemon skips
	   the Z attach and the back end falls back to tile-local Z */
	if (!_z)
		return g2d_canvas(0, 0, 0, 0, 0);
	cv = g2d_canvas(_z_shm, (uint32_t)_w * (uint32_t)_h * 4u,
	                (uint32_t)_w, (uint32_t)_h, 1);
	cv.phy = _z_phy;
	return cv;
}

/* Reduced-resolution scratch target for the fullscreen-compute render-scale path
   (see pgl_g3d_set_render_scale).  Grow-only like alloc_target: an in-capacity
   resize reuses the run, so a window drag does not thrash the contig slab. */
static int rs_ensure(int rw, int rh)
{
	uint32_t need = (uint32_t)rw * (uint32_t)rh;
	uint32_t* px = NULL;
	ewokos_addr_t phy = 0;
	int shm = 0;
	uint32_t cap;

	if (rw <= 0 || rh <= 0)
		return 0;
	if (_rs && need <= _rs_cap) {
		_rs_w = rw;
		_rs_h = rh;
		return 1;
	}
	if (_rs) { g2d_shm_free(_rs); _rs = NULL; _rs_shm = 0; _rs_phy = 0; _rs_cap = 0; }
	cap = need + need / 2u;
	if (g2d_shm_alloc_phy(cap * 4u, &shm, &px, &phy) != 0 || shm <= 0 || px == NULL)
		return 0;
	_rs_shm = shm;
	_rs = px;
	_rs_phy = phy;
	_rs_cap = cap;
	_rs_w = rw;
	_rs_h = rh;
	return 1;
}

static g2d_canvas_t rs_canvas(int rw, int rh)
{
	g2d_canvas_t cv = g2d_canvas(_rs_shm, (uint32_t)rw * (uint32_t)rh * 4u,
	                             (uint32_t)rw, (uint32_t)rh, 1);
	cv.phy = _rs_phy;
	return cv;
}

/* Full-res history / temp canvases for the temporal-AA accumulate.  Both are
   _w x _h; grow-only like rs_ensure so a resize reuses the run. */
static int taa_ensure(int w, int h)
{
	uint32_t need = (uint32_t)w * (uint32_t)h;
	uint32_t* px = NULL;
	ewokos_addr_t phy = 0;
	int shm = 0;

	if (w <= 0 || h <= 0)
		return 0;
	if (_taa_hist && _taa_tmp && need <= _taa_cap && need <= _taat_cap)
		return 1;
	if (_taa_hist) { g2d_shm_free(_taa_hist); _taa_hist = NULL; _taa_shm = 0; _taa_hist_phy = 0; _taa_cap = 0; }
	if (_taa_tmp)  { g2d_shm_free(_taa_tmp);  _taa_tmp  = NULL; _taat_shm = 0; _taa_tmp_phy = 0; _taat_cap = 0; }
	if (g2d_shm_alloc_phy(need * 4u, &shm, &px, &phy) != 0 || shm <= 0 || px == NULL)
		return 0;
	_taa_shm = shm; _taa_hist = px; _taa_hist_phy = phy; _taa_cap = need;
	if (g2d_shm_alloc_phy(need * 4u, &shm, &px, &phy) != 0 || shm <= 0 || px == NULL) {
		g2d_shm_free(_taa_hist); _taa_hist = NULL; _taa_shm = 0; _taa_hist_phy = 0; _taa_cap = 0;
		return 0;
	}
	_taat_shm = shm; _taa_tmp = px; _taa_tmp_phy = phy; _taat_cap = need;
	_taa_valid = 0;
	return 1;
}

static g2d_canvas_t taa_hist_canvas(int w, int h)
{
	g2d_canvas_t cv = g2d_canvas(_taa_shm, (uint32_t)w * (uint32_t)h * 4u,
	                             (uint32_t)w, (uint32_t)h, 1);
	cv.phy = _taa_hist_phy;
	return cv;
}

static g2d_canvas_t taa_tmp_canvas(int w, int h)
{
	g2d_canvas_t cv = g2d_canvas(_taat_shm, (uint32_t)w * (uint32_t)h * 4u,
	                             (uint32_t)w, (uint32_t)h, 1);
	cv.phy = _taa_tmp_phy;
	return cv;
}

void pgl_g3d_set_render_scale(int scale)
{
	_rs_num = (scale > 1) ? scale : 1;
	_rs_den = 1;
}

void pgl_g3d_set_render_scale_frac(int num, int den)
{
	if (num < 1) num = 1;
	if (den < 1) den = 1;
	if (num > den * 8) num = den * 8;   /* cap downscale at 8x */
	_rs_num = num;
	_rs_den = den;
}

void pgl_g3d_set_render_taa(int on)
{
	_taa = on ? 1 : 0;
	_taa_valid = 0;
}

void pgl_g3d_flush(void)
{
	g2d_pgl_req_t req;
	g2d_canvas_t pos, col, z, texcv, uvcv;
	g2d_canvas_t dstcv;
	uint32_t dst_stride;
	int res;
	int tex_path;
	int compiled;
	int rs_active;

	if (!_enabled || _nverts == 0)
		return;

	pos = g2d_canvas(_pos_shm, PGL_G3D_POS_BYTES, _nverts, 3, 1);
	pos.phy = _pos_phy;
	z = z_canvas();

	/* WS4: a compiled-shader batch stages clip-space pos only (the compiled
	   FS derives the colour procedurally), so it never takes the tex path. */
	compiled = (_batch_prog != PGL_G3D_PROG_INVALID);
	tex_path = (!compiled && _batch_fs == PGL_G3D_FS_TEX_MOD &&
	            _batch_tex_slot >= 0 &&
	            _batch_tex_slot < PGL_G3D_MAX_TEX &&
	            _tex[_batch_tex_slot].pixels != NULL &&
	            _tex[_batch_tex_slot].tiling == PGLC_TEX_TILING_RASTER);
	if (tex_path) {
		const pgl_g3d_tex_slot_t* t = &_tex[_batch_tex_slot];
		/* col is unused on the TEX_MOD path: send a zero-size canvas so the
		   g2dd handler skips the attach and the back end keeps col_iova == 0.
		   uv is modelled w=nverts h=3 (nverts*3*4 == nverts*12 bytes). */
		col = g2d_canvas(0, 0, 0, 0, 0);
		uvcv = g2d_canvas(_uv_shm, PGL_G3D_UV_BYTES, _nverts, 3, 1);
		uvcv.phy = _uv_phy;
		texcv = g2d_canvas(t->shm, (uint32_t)t->w * (uint32_t)t->h * 4u,
		                   (uint32_t)t->w, (uint32_t)t->h, 1);
		texcv.phy = t->phy;
	} else if (compiled) {
		/* pos-only: col/uv/tex unused, send zero-size canvases so the daemon
		   keeps their iovas at 0 and the back end takes the compiled branch. */
		col   = g2d_canvas(0, 0, 0, 0, 0);
		texcv = g2d_canvas(0, 0, 0, 0, 0);
		uvcv  = g2d_canvas(0, 0, 0, 0, 0);
	} else {
		col = g2d_canvas(_col_shm, PGL_G3D_COL_BYTES, _nverts, 1, 1);
		col.phy = _col_phy;
		texcv = g2d_canvas(0, 0, 0, 0, 0);
		uvcv  = g2d_canvas(0, 0, 0, 0, 0);
	}

	/* Render-scale: dispatch a non-geom fullscreen compute into the reduced
	   scratch target instead of the fb; the upscale to the real fb runs right
	   after the draw below.  _rs_w/_rs_h were latched by pgl_g3d_set_uniforms
	   this frame so the target and the injected pglc_cs_stride/iResolution agree. */
	rs_active = (compiled && _rs_num > _rs_den && _rs_w > 0 && _rs != NULL &&
	             pgl_g3d_shader_has_compute(_batch_prog) &&
	             !pgl_g3d_shader_is_geom(_batch_prog));
	dstcv = rs_active ? rs_canvas(_rs_w, _rs_h) : fb_canvas();
	dst_stride = rs_active ? (uint32_t)_rs_w * 4u : (uint32_t)_w * 4u;
	g2d_pgl_draw_req_init(&req, dstcv, pos, col,
	                      _batch_mode, _nverts,
	                      G2D_PGL_FMT_RGBA8, dst_stride,
	                      (uint32_t)_batch_fs, _batch_rgba, 0,
	                      _batch_depth_func, (uint8_t)_batch_depth_mask,
	                      (uint8_t)_batch_cull,
	                      z, (uint32_t)_w * 4u, (uint8_t)_first_frame,
	                      _clear_rgba, _clear_z);
	/* Phase B2c: carry the open batch's scissor / polygon-offset / colour-mask
	   state into the request (field names mirror g2d_pgl_ext_t exactly). */
	req.ext.scissor_x   = _batch_ext.scissor_x;
	req.ext.scissor_y   = _batch_ext.scissor_y;
	req.ext.scissor_w   = _batch_ext.scissor_w;
	req.ext.scissor_h   = _batch_ext.scissor_h;
	req.ext.poly_enable = _batch_ext.poly_enable;
	req.ext.poly_factor = _batch_ext.poly_factor;
	req.ext.poly_units  = _batch_ext.poly_units;
	req.ext.color_mask  = _batch_ext.color_mask;
	/* Phase D: attach the texture image + uv canvases (tex_stride == 0 -> the
	   back end derives w * 4).  A no-op on the colour path. */
	if (tex_path) {
		const pgl_g3d_tex_slot_t* t = &_tex[_batch_tex_slot];
		g2d_pgl_draw_req_set_tex(&req, texcv,
		                           (uint32_t)t->w, (uint32_t)t->h, 0u, uvcv);
	}
	/* WS4: bind the compiled program (its cached daemon handle) plus the
	   per-draw uniform blob so the back end builds the shader-state record
	   and expands the QUNIFORM stream.  contig=1 mirrors the CPU-read blob
	   upload in pgl_g3d_shader.c. */
	if (compiled) {
		int u;
		g2d_pgl_draw_req_set_shader(&req,
		                            pgl_g3d_shader_daemon_handle(_batch_prog),
		                            _unif_shm, _unif_count, 1);
		/* WS4 sampler bindings: stage each bound unit's texture image as a
		   ctex canvas (contig-shm, phy pre-filled) so the daemon resolves its
		   physical base and the back end builds its texture-state record for
		   the slot the compiled FS references.  Unbound units keep size 0 (the
		   daemon skips them).  Whether a raster (untiled) slot may be bound is
		   backend-defined: pglc_tex_sampler_reads_raster() is false on V3D (its
		   TMU needs a tiled level 0, so a raster slot is left unbound and the
		   back end declines the draw instead of sampling a scrambled picture)
		   and true on Adreno (its a6xx CS sampler reads linear raster directly).
		   ctex_stride is the padded pitch and the canvas size covers the whole
		   padded (ub_pad included) image. */
		for (u = 0; u < PGL_G3D_MAX_TEX_UNITS; u++) {
			const pgl_g3d_tex_slot_t* t;
			int s = _ctex_bind[u];
			if (s < 0 || s >= PGL_G3D_MAX_TEX || _tex[s].pixels == NULL)
				continue;
			t = &_tex[s];
			if (t->tiling == PGLC_TEX_TILING_RASTER &&
			    !pglc_tex_sampler_reads_raster())
				continue;
			req.ctex[u] = g2d_canvas(t->shm, t->size,
			                         (uint32_t)t->w, (uint32_t)t->h, 1);
			req.ctex[u].phy  = t->phy;
			req.ctex_w[u]      = (uint32_t)t->w;
			req.ctex_h[u]      = (uint32_t)t->h;
			req.ctex_stride[u] = t->stride;
			req.ctex_tiling[u] = (uint8_t)t->tiling;
			req.ctex_ub_pad[u] = G2D_PGL_CTEX_UB_PAD_PACK(t->ub_pad,
			    _ctex_filter[u] == PGL_G3D_TEX_FILTER_LINEAR ?
			        G2D_PGL_CTEX_FILTER_LINEAR : G2D_PGL_CTEX_FILTER_NEAREST);
			/* A cubemap slot carries its 6-layer array geometry so the
			   daemon builds image_depth=5 + array_stride; a plain 2D slot
			   leaves both 0 (single-layer record, unchanged). */
			req.ctex_depth[u]       = (uint8_t)t->depth;
			req.ctex_arrstride64[u] = t->arrstride64;
		}
	}

	_nverts = 0;
	res = g2d_pgl_draw(&req);
	if (res == G2D_OK && rs_active) {
		/* Upscale the reduced-res scratch.  The scale op runs the shared
		   single-pass 4-tap bilinear kernel (g2dd gpu_scale_op).  With temporal
		   AA the upscaled frame lands in _taa_tmp and is accumulated into the
		   persistent history, which is then composited to the fb; without it the
		   scratch goes straight to the fb.  The compute's post-job L2 writeback
		   flushed the scratch to memory; these blits' src invalidate reads it
		   back coherently.  A failure only leaves the fb stale one frame. */
		g2d_scale_to_req_t sreq;
		g2d_blit_req_t breq;
		g2d_rect_t fullr;
		fullr.x = 0; fullr.y = 0; fullr.w = _w; fullr.h = _h;
		if (_taa && taa_ensure(_w, _h)) {
			g2d_scale_to_req_init(&sreq, rs_canvas(_rs_w, _rs_h),
			                    taa_tmp_canvas(_w, _h));
			g2d_scale_to(&sreq);
			if (_batch_prog != _taa_prog || _first_frame) {
				_taa_prog = _batch_prog;
				_taa_valid = 0;
			}
			if (!_taa_valid) {
				g2d_blit_req_init(&breq, taa_hist_canvas(_w, _h),
				                taa_tmp_canvas(_w, _h), fullr, fullr, 255);
				g2d_blit(&breq);
				_taa_valid = 1;
			} else {
				g2d_blit_req_init(&breq, taa_hist_canvas(_w, _h),
				                taa_tmp_canvas(_w, _h), fullr, fullr,
				                (uint8_t)_taa_alpha);
				g2d_blit_alpha(&breq);
			}
			g2d_blit_req_init(&breq, fb_canvas(), taa_hist_canvas(_w, _h),
			                fullr, fullr, 255);
			g2d_blit(&breq);
		} else {
			g2d_scale_to_req_init(&sreq, rs_canvas(_rs_w, _rs_h), fb_canvas());
			g2d_scale_to(&sreq);
		}
	}
	if (res != G2D_OK) {
		/* NOT_SUPPORTED on a compiled batch is a PRE-submission decline by
		   the back end (unbound sampler unit, unsupported uniform kind...)
		   specific to this program: latch only the program so other effects
		   and the fixed-function path keep the GPU.  Anything else (a real
		   dispatch failure/timeout) latches the GPU off: never replay. */
		if (compiled && res == G2D_ERR_NOT_SUPPORTED)
			pgl_g3d_shader_mark_declined(_batch_prog);
		else
			_enabled = 0;
	} else
		_first_frame = 0;   /* this frame's clear has been consumed */
}

/* Mark the start of a new frame.  Flushes any batch still pending from the
   previous frame (so it lands as a normal LOAD dispatch of THAT frame), then
   records the colour/depth the next - the frame's first - GPU draw dispatch
   clears the persistent colour+Z surfaces to.  No dispatch is issued here; the
   clear rides along with the first draw.  No-op when the GPU path is off. */
void pgl_g3d_begin_frame(uint32_t clear_rgba, float clear_z)
{
	if (!_enabled)
		return;
	pgl_g3d_flush();
	_clear_rgba = clear_rgba;
	_clear_z = clear_z;
	_first_frame = 1;
}

/* See pgl_g3d.h.  The colour half of the pending clear needs no work here:
   pgl_Clear CPU-cleared the adopted back buffer (_fb) to exactly the word the
   GPU fill would write (pix_t ARGB == the geom kernel's 0xAARRGGBB).  Only the
   persistent depth surface is filled, mirroring draw_compute_geom's
   first_frame fill, and the pending clear is dropped so the frame's first GPU
   dispatch LOADs instead of erasing the CPU pixels drawn after pgl_Clear. */
void pgl_g3d_consume_frame_clear(void)
{
	uint32_t n, i;

	if (!_enabled || !_first_frame)
		return;
	if (_z != NULL) {
		n = (uint32_t)_w * (uint32_t)_h;
		for (i = 0; i < n; i++)
			_z[i] = 0xFFFFFFFFu;   /* 24-bit unorm far, as the geom fill */
	}
	_first_frame = 0;
}

/* Field-wise equality of two ext states (avoids a padding-sensitive memcmp on
   the struct's float/uint8 alignment gaps). */
static int ext_equal(const pgl_g3d_ext_t* a, const pgl_g3d_ext_t* b)
{
	return a->scissor_x == b->scissor_x && a->scissor_y == b->scissor_y &&
	       a->scissor_w == b->scissor_w && a->scissor_h == b->scissor_h &&
	       a->poly_enable == b->poly_enable &&
	       a->poly_factor == b->poly_factor &&
	       a->poly_units == b->poly_units &&
	       a->color_mask == b->color_mask;
}

/* Open or extend a batch for `nvert` more vertices of primitive `mode`.  A batch
   is homogeneous in mode/fs_id/solid colour/depth/cull/tex_slot/ext (all
   per-dispatch render state on the GPU), so any change - or a batch that would
   overflow the fixed staging - flushes first.  ext may be NULL (no
   scissor/offset/mask); tex_slot is -1 on the colour path.  Returns the vertex
   index to write at, or -1 when the GPU is off or a flush just latched it off
   (caller must draw on the CPU). */
static int batch_reserve(uint32_t mode, int fs_id, uint32_t solid_rgba,
                         uint32_t depth_func, uint32_t depth_mask,
                         uint32_t cull_cfg, const pgl_g3d_ext_t* ext,
                         int tex_slot, int prog, uint32_t unif_gen, uint32_t nvert)
{
	pgl_g3d_ext_t e;

	if (!_enabled)
		return -1;
	/* Fixed-function batches (no compiled program) are declined by the daemon
	   (v3d_3d.c V3D_3D_ENABLE_FIXED_DRAW 0: the hand-written QPU shaders stall
	   the binner and used to wedge the shared V3D).  Decline here, before any
	   vertex is staged, so the caller rasterizes the primitive on the CPU
	   instead of a flush dropping the batch and latching the GPU off. */
	if (prog == PGL_G3D_PROG_INVALID && !PGL_G3D_FIXED_FUNCTION)
		return -1;

	memset(&e, 0, sizeof(e));
	if (ext)
		e = *ext;

	if (_nverts != 0 && (mode != _batch_mode || fs_id != _batch_fs ||
	                     solid_rgba != _batch_rgba ||
	                     depth_func != _batch_depth_func ||
	                     depth_mask != _batch_depth_mask ||
	                     cull_cfg != _batch_cull ||
	                     tex_slot != _batch_tex_slot ||
	                     prog != _batch_prog ||
	                     unif_gen != _batch_unif_gen ||
	                     !ext_equal(&e, &_batch_ext)))
		pgl_g3d_flush();
	/* room for nvert more vertices? */
	if (_nverts + nvert > PGL_G3D_MAX_VERTS)
		pgl_g3d_flush();
	if (!_enabled)
		return -1;   /* a flush may have latched the GPU off */

	if (_nverts == 0) {
		_batch_mode = mode;
		_batch_fs = fs_id;
		_batch_rgba = solid_rgba;
		_batch_depth_func = depth_func;
		_batch_depth_mask = depth_mask;
		_batch_cull = cull_cfg;
		_batch_tex_slot = tex_slot;
		_batch_prog = prog;
		_batch_unif_gen = unif_gen;
		_batch_ext = e;
	}
	return (int)_nverts;
}

int pgl_g3d_batch_tri(int fs_id, uint32_t solid_rgba,
                       const float* n0, const float* n1, const float* n2,
                       const float* col0, const float* col1, const float* col2,
                       uint32_t depth_func, uint32_t depth_mask, uint32_t cull_cfg,
                       const pgl_g3d_ext_t* ext)
{
	int base = batch_reserve(PGL_G3D_MODE_TRIANGLES, fs_id, solid_rgba,
	                         depth_func, depth_mask, cull_cfg, ext, -1,
	                         PGL_G3D_PROG_INVALID, 0u, 3u);
	if (base < 0)
		return 0;

	put_pos(base + 0, n0);
	put_pos(base + 1, n1);
	put_pos(base + 2, n2);

	if (fs_id == PGL_G3D_FS_GOURAUD && col0 && col1 && col2) {
		put_col_f(base + 0, col0);
		put_col_f(base + 1, col1);
		put_col_f(base + 2, col2);
	} else {
		/* SOLID: the GPU gouraud frag shader over constant vertex
		   colours is exactly a solid fill, so no separate QPU shader is
		   needed for the solid built-in */
		put_col_u32(base + 0, solid_rgba);
		put_col_u32(base + 1, solid_rgba);
		put_col_u32(base + 2, solid_rgba);
	}

	_nverts += 3u;
	return 1;
}

int pgl_g3d_batch_point(int fs_id, uint32_t solid_rgba, const float* n0,
                         const float* col0, uint32_t depth_func,
                         uint32_t depth_mask, uint32_t cull_cfg,
                         const pgl_g3d_ext_t* ext)
{
	int base = batch_reserve(PGL_G3D_MODE_POINTS, fs_id, solid_rgba,
	                         depth_func, depth_mask, cull_cfg, ext, -1,
	                         PGL_G3D_PROG_INVALID, 0u, 1u);
	if (base < 0)
		return 0;

	put_pos(base, n0);
	if (fs_id == PGL_G3D_FS_GOURAUD && col0)
		put_col_f(base, col0);
	else
		put_col_u32(base, solid_rgba);

	_nverts += 1u;
	return 1;
}

int pgl_g3d_batch_line(int fs_id, uint32_t solid_rgba,
                        const float* n0, const float* n1,
                        const float* col0, const float* col1,
                        uint32_t depth_func, uint32_t depth_mask,
                        uint32_t cull_cfg, const pgl_g3d_ext_t* ext)
{
	int base = batch_reserve(PGL_G3D_MODE_LINES, fs_id, solid_rgba,
	                         depth_func, depth_mask, cull_cfg, ext, -1,
	                         PGL_G3D_PROG_INVALID, 0u, 2u);
	if (base < 0)
		return 0;

	put_pos(base + 0, n0);
	put_pos(base + 1, n1);

	if (fs_id == PGL_G3D_FS_GOURAUD && col0 && col1) {
		put_col_f(base + 0, col0);
		put_col_f(base + 1, col1);
	} else {
		put_col_u32(base + 0, solid_rgba);
		put_col_u32(base + 1, solid_rgba);
	}

	_nverts += 2u;
	return 1;
}

/* Phase D: TEX_MOD triangle.  Uses the same staging buffers as batch_tri but
   writes uv float3 into _uv instead of RGBA8 into _col, and carries tex_slot
   through the batch key so the flush can attach the right texture canvas. */
int pgl_g3d_batch_tri_tex(int tex_slot,
                           const float* n0, const float* n1, const float* n2,
                           const float* uv0, const float* uv1, const float* uv2,
                           uint32_t depth_func, uint32_t depth_mask,
                           uint32_t cull_cfg, const pgl_g3d_ext_t* ext)
{
	int base;

	if (tex_slot < 0 || tex_slot >= PGL_G3D_MAX_TEX)
		return 0;
	if (_tex[tex_slot].pixels == NULL)
		return 0;   /* slot not uploaded -> caller falls back to CPU */
	if (!_uv)
		return 0;

	base = batch_reserve(PGL_G3D_MODE_TRIANGLES, PGL_G3D_FS_TEX_MOD, 0u,
	                     depth_func, depth_mask, cull_cfg, ext, tex_slot,
	                     PGL_G3D_PROG_INVALID, 0u, 3u);
	if (base < 0)
		return 0;

	put_pos(base + 0, n0);
	put_pos(base + 1, n1);
	put_pos(base + 2, n2);
	put_uv (base + 0, uv0);
	put_uv (base + 1, uv1);
	put_uv (base + 2, uv2);

	_nverts += 3u;
	return 1;
}

/* === WS4 compiled-shader draw path ======================================== */

void pgl_g3d_set_shader_program(pgl_g3d_prog p)
{
	if (p == _active_prog)
		return;
	/* a program change ends the open batch (a batch is homogeneous in its
	   program): flush so pending draws keep the program they were batched
	   with.  No-op flush when the GPU path is off. */
	if (_enabled)
		pgl_g3d_flush();
	_active_prog = p;
}

void pgl_g3d_set_uniforms(const uint32_t* data, uint32_t count)
{
	if (!_enabled || !_unif)
		return;
	if (count > PGL_G3D_MAX_UNIF)
		count = PGL_G3D_MAX_UNIF;
	/* flush FIRST so an already-open batch keeps the uniform blob it was
	   reserved with (the blob joins the homogeneity key via _unif_gen); the
	   new blob then applies to subsequent batches. */
	pgl_g3d_flush();
	if (!_enabled)
		return;
	if (data && count)
		memcpy(_unif, data, (size_t)count * 4u);
	/* Compute route: the compiled compute kernel addresses the frame buffer as
	   dword [y * pglc_cs_stride + x]; the GPU target's row pitch is _w * 4 bytes
	   (fb_canvas / the draw request's stride), so inject _w dwords here where
	   the frame size is known instead of burdening every pack callback. */
	if (_active_prog != PGL_G3D_PROG_INVALID &&
	    pgl_g3d_shader_has_compute(_active_prog)) {
		uint32_t comps = 0;
		int rw = _w, rh = _h;
		int scaled = 0;
		int d, ri = -1;
		uint32_t rc = 0;
		/* Render-scale needs BOTH the scratch AND a by-name iResolution slot:
		   the kernel derives uv/clip from iResolution and its fb row pitch from
		   pglc_cs_stride, so both must move to the reduced geometry together or
		   not at all.  Any miss falls back to the exact full-res path. */
		if (_rs_num > _rs_den && !pgl_g3d_shader_is_geom(_active_prog)) {
			rw = (int)((int64_t)_w * _rs_den / _rs_num); if (rw < 1) rw = 1;
			rh = (int)((int64_t)_h * _rs_den / _rs_num); if (rh < 1) rh = 1;
			/* Temporal AA: nudge the sampling grid by a pixel on a 3-phase
			   cycle so each frame's scene samples fall on a different
			   sub-pixel phase; flush() accumulates them.  uv still spans
			   [0,1) so this changes sample spacing, not framing. */
			if (_taa) {
				switch (_taa_phase++ % 3) {
				case 1:  rw += 1; break;
				case 2:  rh += 1; break;
				default: break;
				}
			}
			ri = pgl_g3d_shader_uniform_dword(_active_prog, "iResolution", &rc);
			if (rs_ensure(rw, rh) && ri >= 0 && rc >= 2u &&
			    (uint32_t)ri + 1u < PGL_G3D_MAX_UNIF)
				scaled = 1;
			else {
				_rs_num = 1; _rs_den = 1;   /* disable for good: never leave partial state */
				rw = _w; rh = _h;
			}
		}
		_rs_w = scaled ? rw : 0;
		_rs_h = scaled ? rh : 0;
		d = pgl_g3d_shader_uniform_dword(_active_prog, "pglc_cs_stride", &comps);
		if (d >= 0 && (uint32_t)d < PGL_G3D_MAX_UNIF) {
			if ((uint32_t)d >= count) {
				memset(&_unif[count], 0, ((uint32_t)d + 1u - count) * 4u);
				count = (uint32_t)d + 1u;
			}
			_unif[d] = (uint32_t)rw;
		}
		if (scaled) {
			float fx = (float)rw, fy = (float)rh;
			if ((uint32_t)ri + 1u >= count) {
				memset(&_unif[count], 0, ((uint32_t)ri + 2u - count) * 4u);
				count = (uint32_t)ri + 2u;
			}
			memcpy(&_unif[ri],     &fx, 4u);
			memcpy(&_unif[ri + 1], &fy, 4u);
		}
	}
	_unif_count = count;
	_unif_gen++;
}

void pgl_g3d_bind_tex_unit(int unit, int tex_slot)
{
	pgl_g3d_bind_tex_unit_filter(unit, tex_slot, PGL_G3D_TEX_FILTER_NEAREST);
}

void pgl_g3d_bind_tex_unit_filter(int unit, int tex_slot, int filter)
{
	if (!_enabled)
		return;
	if (unit < 0 || unit >= PGL_G3D_MAX_TEX_UNITS)
		return;
	/* an out-of-range or empty slot unbinds the unit (so a stale binding from a
	   previous effect never feeds the TMU a wrong image); a sample the FS makes
	   from an unbound unit is declined back to the CPU by the back end. */
	if (tex_slot < 0 || tex_slot >= PGL_G3D_MAX_TEX ||
	    _tex[tex_slot].pixels == NULL)
		_ctex_bind[unit] = -1;
	else
		_ctex_bind[unit] = tex_slot;
	_ctex_filter[unit] = (filter == PGL_G3D_TEX_FILTER_LINEAR) ?
	                     PGL_G3D_TEX_FILTER_LINEAR : PGL_G3D_TEX_FILTER_NEAREST;
}

int pgl_g3d_batch_tri_compiled(const float* n0, const float* n1, const float* n2,
                               uint32_t depth_func, uint32_t depth_mask,
                               uint32_t cull_cfg, const pgl_g3d_ext_t* ext)
{
	int base, dh;

	if (!_enabled)
		return 0;
	if (!pgl_g3d_program_valid(_active_prog))
		return 0;
	/* lazily upload the program's QPU to the daemon (cached after the first;
	   a declined upload keeps THIS effect on the CPU rasterizer without
	   latching the whole GPU off). */
	dh = pgl_g3d_shader_upload(_active_prog);
	if (dh < 0)
		return 0;

	base = batch_reserve(PGL_G3D_MODE_TRIANGLES, PGL_G3D_FS_SOLID, 0u,
	                     depth_func, depth_mask, cull_cfg, ext, -1,
	                     _active_prog, _unif_gen, 3u);
	if (base < 0)
		return 0;

	put_pos(base + 0, n0);
	put_pos(base + 1, n1);
	put_pos(base + 2, n2);

	_nverts += 3u;
	return 1;
}

/* === WS4 generic compiled-mesh draw path ================================== */

pgl_g3d_mesh pgl_g3d_upload_mesh(const float* interleaved, uint32_t nverts,
                                 uint32_t stride_floats)
{
	pgl_g3d_mesh_slot_t* m;
	int slot = -1, i;
	uint32_t v;

	if (!_enabled || interleaved == NULL || nverts == 0)
		return PGL_G3D_MESH_INVALID;
	/* layout contract: position float3 at element 0, normal float3 at element
	   3 (any extra trailing floats per vertex, e.g. uv, are ignored). */
	if (stride_floats < 6u)
		return PGL_G3D_MESH_INVALID;

	for (i = 0; i < PGL_G3D_MAX_MESH; i++)
		if (!_mesh[i].used) { slot = i; break; }
	if (slot < 0)
		return PGL_G3D_MESH_INVALID;   /* table full: caller keeps its CPU path */
	m = &_mesh[slot];

	m->pos = (float*)malloc((size_t)nverts * 3u * sizeof(float));
	m->nrm = (float*)malloc((size_t)nverts * 3u * sizeof(float));
	if (!m->pos || !m->nrm) {
		free(m->pos); free(m->nrm);
		m->pos = NULL; m->nrm = NULL;
		return PGL_G3D_MESH_INVALID;   /* OOM does NOT latch the GPU off */
	}
	/* de-interleave once into separate float3 SoA arrays */
	for (v = 0; v < nverts; v++) {
		const float* src = interleaved + (size_t)v * stride_floats;
		m->pos[v*3u + 0u] = src[0];
		m->pos[v*3u + 1u] = src[1];
		m->pos[v*3u + 2u] = src[2];
		m->nrm[v*3u + 0u] = src[3];
		m->nrm[v*3u + 1u] = src[4];
		m->nrm[v*3u + 2u] = src[5];
	}
	m->nverts = nverts;
	m->idx = NULL;
	m->tcol = NULL;
	m->tcol_stride = 0;
	m->instanced = 0;
	m->used = 1;
	return (pgl_g3d_mesh)slot;
}

/* Instanced variant of pgl_g3d_upload_mesh: additionally reads a per-vertex
   instance index from element 6 (stride_floats >= 7) into a float1 array the
   draw's re-stage interleaves as the position stream's 4th component
   (pos.xyz + idx, vec4 stride 16).  See the public header for the contract. */
pgl_g3d_mesh pgl_g3d_upload_mesh_instanced(const float* interleaved,
                                           uint32_t nverts,
                                           uint32_t stride_floats)
{
	pgl_g3d_mesh_slot_t* m;
	int slot = -1, i;
	uint32_t v;

	if (!_enabled || interleaved == NULL || nverts == 0)
		return PGL_G3D_MESH_INVALID;
	/* layout contract: position float3 at 0, normal float3 at 3, instance
	   index at element 6 (any extra trailing floats per vertex are ignored). */
	if (stride_floats < 7u)
		return PGL_G3D_MESH_INVALID;

	for (i = 0; i < PGL_G3D_MAX_MESH; i++)
		if (!_mesh[i].used) { slot = i; break; }
	if (slot < 0)
		return PGL_G3D_MESH_INVALID;   /* table full: caller keeps its CPU path */
	m = &_mesh[slot];

	m->pos = (float*)malloc((size_t)nverts * 3u * sizeof(float));
	m->nrm = (float*)malloc((size_t)nverts * 3u * sizeof(float));
	m->idx = (float*)malloc((size_t)nverts * sizeof(float));
	if (!m->pos || !m->nrm || !m->idx) {
		free(m->pos); free(m->nrm); free(m->idx);
		m->pos = NULL; m->nrm = NULL; m->idx = NULL;
		return PGL_G3D_MESH_INVALID;   /* OOM does NOT latch the GPU off */
	}
	/* de-interleave once into separate SoA arrays */
	for (v = 0; v < nverts; v++) {
		const float* src = interleaved + (size_t)v * stride_floats;
		m->pos[v*3u + 0u] = src[0];
		m->pos[v*3u + 1u] = src[1];
		m->pos[v*3u + 2u] = src[2];
		m->nrm[v*3u + 0u] = src[3];
		m->nrm[v*3u + 1u] = src[4];
		m->nrm[v*3u + 2u] = src[5];
		m->idx[v]         = src[6];
	}
	m->nverts = nverts;
	m->tcol = NULL;
	m->tcol_stride = 0;
	m->instanced = 1;
	m->used = 1;
	return (pgl_g3d_mesh)slot;
}

/* Colour-modulated textured variant of pgl_g3d_upload_mesh (imgui,
   cs_is_geom=4): each vertex carries pos.xyz at element 0, uv at element 3 and
   an rgba colour at element 5 (stride_floats >= 10; extra trailing floats are
   ignored).  The stream is re-packed to a tight stride-10 and kept verbatim in
   the slot's tcol buffer (pos/nrm/idx stay NULL); a draw memcpy's its sub-range
   straight into the pos staging, which the geom_xform_texcol kernel reads as
   binding 1.  Same return contract as pgl_g3d_upload_mesh. */
pgl_g3d_mesh pgl_g3d_upload_mesh_texcol(const float* interleaved, uint32_t nverts,
                                        uint32_t stride_floats)
{
	pgl_g3d_mesh_slot_t* m;
	int slot = -1, i;
	uint32_t v;

	if (!_enabled || interleaved == NULL || nverts == 0)
		return PGL_G3D_MESH_INVALID;
	if (stride_floats < 10u)
		return PGL_G3D_MESH_INVALID;

	for (i = 0; i < PGL_G3D_MAX_MESH; i++)
		if (!_mesh[i].used) { slot = i; break; }
	if (slot < 0)
		return PGL_G3D_MESH_INVALID;   /* table full: caller keeps its CPU path */
	m = &_mesh[slot];

	m->tcol = (float*)malloc((size_t)nverts * 10u * sizeof(float));
	if (!m->tcol)
		return PGL_G3D_MESH_INVALID;   /* OOM does NOT latch the GPU off */
	for (v = 0; v < nverts; v++)
		memcpy(m->tcol + (size_t)v * 10u,
		       interleaved + (size_t)v * stride_floats, 10u * sizeof(float));
	m->pos = NULL;
	m->nrm = NULL;
	m->idx = NULL;
	m->nverts = nverts;
	m->tcol_stride = 10u;
	m->instanced = 0;
	m->used = 1;
	return (pgl_g3d_mesh)slot;
}

void pgl_g3d_free_mesh(pgl_g3d_mesh m)
{
	pgl_g3d_mesh_slot_t* s;

	if (m < 0 || m >= PGL_G3D_MAX_MESH || !_mesh[m].used)
		return;
	s = &_mesh[m];
	free(s->pos);
	free(s->nrm);
	free(s->idx);
	free(s->tcol);
	s->pos = NULL;
	s->nrm = NULL;
	s->idx = NULL;
	s->tcol = NULL;
	s->tcol_stride = 0;
	s->instanced = 0;
	s->nverts = 0;
	s->used = 0;
}

/* Stage the per-frame instance-matrix buffer an instanced mesh draw reads
   (binding 3 of geom_xform_inst_cs_glsl): nfloats consecutive floats, one
   36-float record per instance (MVP mat4 + normal mat4 + material rgb + pad).
   The buffer lives in a GPU-resident contig-shm segment grown on demand and
   marshalled as ctex unit 0 by the next instanced draw_mesh_compiled. */
int pgl_g3d_set_instances(const float* data, uint32_t nfloats)
{
	uint32_t* px = NULL;
	ewokos_addr_t phy = 0;
	int shm = 0;

	if (!_enabled || data == NULL || nfloats == 0)
		return 0;
	if (nfloats > _inst_cap) {
		/* grow to a 64-float granularity so a per-frame count jitter does not
		   reallocate; a failure frees the old buffer and declines (never latches
		   the GPU off - the caller's CPU path stays live). */
		uint32_t cap = (nfloats + 63u) & ~63u;
		if (_inst != NULL) {
			g2d_shm_free(_inst);
			_inst = NULL;
			_inst_shm = 0;
			_inst_phy = 0;
			_inst_cap = 0;
			_inst_floats = 0;
		}
		if (g2d_shm_alloc_phy(cap * 4u, &shm, &px, &phy) != 0 ||
		    shm <= 0 || px == NULL)
			return 0;
		_inst_shm = shm;
		_inst = (float*)px;
		_inst_phy = phy;
		_inst_cap = cap;
	}
	memcpy(_inst, data, (size_t)nfloats * 4u);
	_inst_floats = nfloats;
	return 1;
}

/* Map a PGL_G3D_CULL_* config onto the gouraud raster kernel's uCullSign
 * (0 = keep both windings, 1 = keep area<0, 2 = keep area>0, 3 = cull all).
 * The kernel's window-space shoelace `area` equals -1 * portablegl's
 * is_front_facing `a` (same y-down screen coords, opposite edge-function sign),
 * and portablegl keeps a triangle when (front_face==CW ? -a : a) > 0 for a BACK
 * cull.  Derivation per code:
 *   BACK_CCW  keep front (CCW, a>0)  -> keep area<0 -> 1
 *   BACK_CW   keep front (CW,  a<0)  -> keep area>0 -> 2
 *   FRONT_CCW keep back  (a<=0)      -> keep area>0 -> 2
 *   FRONT_CW  keep back  (a>0)       -> keep area<0 -> 1
 *   BOTH      cull everything        -> 3 */
static uint32_t pgl_g3d_cull_sign(uint32_t cull)
{
	switch (cull) {
	case PGL_G3D_CULL_BACK_CCW:  return 1u;
	case PGL_G3D_CULL_BACK_CW:   return 2u;
	case PGL_G3D_CULL_FRONT_CCW: return 2u;
	case PGL_G3D_CULL_FRONT_CW:  return 1u;
	case PGL_G3D_CULL_BOTH:      return 3u;
	default:                     return 0u;   /* NONE */
	}
}

int pgl_g3d_draw_mesh_compiled(pgl_g3d_prog prog, pgl_g3d_mesh mesh,
                               uint32_t first, uint32_t count,
                               uint32_t depth_func, uint32_t depth_mask,
                               uint32_t cull)
{
	g2d_pgl_req_t req;
	g2d_canvas_t pos, col, z;
	pgl_g3d_mesh_slot_t* m;
	uint32_t v;
	uint32_t unif_n;
	int dh, res;
	int geom;
	int geom_tex;
	int geom_texcol;
	int geom_inst;

	if (!_enabled)
		return 0;
	if (!pgl_g3d_program_valid(prog))
		return 0;
	if (mesh < 0 || mesh >= PGL_G3D_MAX_MESH || !_mesh[mesh].used)
		return 0;
	m = &_mesh[mesh];
	if (first >= m->nverts || count == 0)
		return 0;
	if (first + count > m->nverts)
		count = m->nverts - first;   /* clamp a trailing partial sub-range */
	if (count > PGL_G3D_MAX_VERTS)
		return 0;   /* a sub-range larger than the staging: caller must split */
	if (!_pos || !_nrm)
		return 0;

	/* A mesh program without a compute stage (its geom kernel failed to
	   compile at program-compile time) has only the daemon's binner route,
	   which rasterises zero fragments on this silicon and reports success -
	   the draw would render nothing and the CPU fallback would never engage.
	   Keep it on the CPU rasterizer; do not even upload a program that can
	   never render. */
	if (!pgl_g3d_shader_has_compute(prog))
		return 0;

	/* lazily upload the program's QPU to the daemon (cached after the first;
	   a declined upload keeps THIS draw on the CPU rasterizer without latching
	   the whole GPU off). */
	dh = pgl_g3d_shader_upload(prog);
	if (dh < 0)
		return 0;

	/* drain any pending batch first so painter's order holds, then re-stage
	   this sub-range into the fixed base-phy pos/normal GPU staging. */
	pgl_g3d_flush();
	if (!_enabled)
		return 0;

	geom = pgl_g3d_shader_is_geom(prog);
	/* texcol takes precedence: pgl_g3d_shader.c still reports geom_tex for a
	   colour-modulated program, so exclude texcol before testing geom_tex to
	   keep it off the stride-6 textured path. */
	geom_texcol = geom && pgl_g3d_shader_geom_texcol(prog);
	geom_tex = geom && !geom_texcol && pgl_g3d_shader_geom_tex(prog);
	geom_inst = geom && pgl_g3d_shader_geom_inst(prog);
	/* Re-stage the sub-range into the fixed _pos/_nrm staging, re-based to
	   vertex 0, in the layout the program's kernel reads.  This is a plain copy
	   into NOCACHE shm every draw, by design: the slot owns no GPU segment (see
	   the mesh table comment), so the daemon always attaches the same two shm
	   ids and its attach cache hits. */
	if (geom_inst) {
		/* Instanced variant: pos is a vec4 stream (pos.xyz + instance index,
		   stride 4) and nrm a float3 stream; the per-instance matrices ride the
		   separate _inst buffer attached as ctex unit 0 below. */
		for (v = 0; v < count; v++) {
			const float* sp = m->pos + (size_t)(first + v) * 3u;
			const float* sn = m->nrm + (size_t)(first + v) * 3u;
			_pos[v*4u + 0u] = sp[0];
			_pos[v*4u + 1u] = sp[1];
			_pos[v*4u + 2u] = sp[2];
			_pos[v*4u + 3u] = m->idx ? m->idx[first + v] : 0.0f;
			_nrm[v*3u + 0u] = sn[0];
			_nrm[v*3u + 1u] = sn[1];
			_nrm[v*3u + 2u] = sn[2];
		}
	} else if (geom_texcol) {
		/* Colour-modulated textured variant (imgui): pos+uv+colour already
		   interleaved at a 10-float stride in the mesh's tcol stream (staged
		   by pgl_g3d_upload_mesh_texcol), so a straight sub-range copy into
		   the pos buffer re-bases it to vertex 0. */
		if (m->tcol == NULL)
			return 0;
		memcpy(_pos, m->tcol + (size_t)first * 10u,
		       (size_t)count * 10u * sizeof(float));
	} else if (geom_tex) {
		/* Textured variant: V3D compute has only 4 SSBO bindings, so pos and
		   uv SHARE the pos staging as one interleaved stream, stride 6 floats
		   (pos.xyz, uv.xy, pad); the normal staging is unused. */
		for (v = 0; v < count; v++) {
			const float* sp = m->pos + (size_t)(first + v) * 3u;
			const float* sn = m->nrm + (size_t)(first + v) * 3u;
			_pos[v*6u + 0u] = sp[0];
			_pos[v*6u + 1u] = sp[1];
			_pos[v*6u + 2u] = sp[2];
			_pos[v*6u + 3u] = sn[0];
			_pos[v*6u + 4u] = sn[1];
			_pos[v*6u + 5u] = 0.0f;
		}
	} else {
		/* gouraud: both streams are already tight float3, so the sub-range is
		   a straight copy (NEON memcpy, not a scalar per-float store loop into
		   NOCACHE memory). */
		memcpy(_pos, m->pos + (size_t)first * 3u, (size_t)count * 12u);
		memcpy(_nrm, m->nrm + (size_t)first * 3u, (size_t)count * 12u);
	}

	/* pos canvas (attribute 0) and col canvas (attribute 1 = normal) are both
	   modelled w=count h=3 float3 (count*3*4 == count*12 bytes), matching the
	   back end's compiled 2-attribute path (col_iova != 0 selects it).  The
	   base phy is always the staging base (never an offset) so g2dd's
	   shm_id-keyed attach cache stays correct.  The geom-tex draw interleaves
	   pos+uv in the pos staging (count*24 bytes, modelled w=count h=6) and
	   sends a zero col canvas (the kernel has no separate uv buffer). */
	if (geom_texcol) {
		/* pos+uv+colour interleaved stride-10: model the canvas w=count h=10
		 * (count*40 bytes) so g2dd's pos.w==nverts check and the size>=w*h*4
		 * attach check both pass; col is unused (zero canvas). */
		pos = g2d_canvas(_pos_shm, PGL_G3D_POS_BYTES, count, 10, 1);
		col = g2d_canvas(0, 0, 0, 0, 0);
		pos.phy = _pos_phy;
	} else if (geom_tex) {
		/* pos+uv interleaved stride-6: the g2dd handler requires
		 * pos.w == nverts, so model the canvas w=count h=6 (the
		 * size>=w*h*4 attach check still encodes count*24 bytes). */
		pos = g2d_canvas(_pos_shm, PGL_G3D_POS_BYTES, count, 6, 1);
		col = g2d_canvas(0, 0, 0, 0, 0);
		pos.phy = _pos_phy;
	} else if (geom_inst) {
		/* pos vec4 stride-4 (count*16 bytes, modelled w=count h=4) + nrm float3. */
		pos = g2d_canvas(_pos_shm, PGL_G3D_POS_BYTES, count, 4, 1);
		col = g2d_canvas(_nrm_shm, PGL_G3D_NRM_BYTES, count, 3, 1);
		col.phy = _nrm_phy;
		pos.phy = _pos_phy;
	} else {
		pos = g2d_canvas(_pos_shm, PGL_G3D_POS_BYTES, count, 3, 1);
		col = g2d_canvas(_nrm_shm, PGL_G3D_NRM_BYTES, count, 3, 1);
		col.phy = _nrm_phy;
		pos.phy = _pos_phy;
	}
	z = z_canvas();

	/* Geometry-rasterizer route: the program's compute stage is the fixed
	   per-triangle kernel (SSBO 0=fb 1=pos 2=nrm 3=depth), which the daemon
	   dispatches instead of the binner (cs_is_geom).  Two client-side duties
	   remain that the kernel cannot do for itself:
	   - the per-draw kernel state that varies with the target or the sub-range
	     (uViewportWH / uFbStride / uTriFirst / uTriCount) is patched into the
	     staged uniform blob here, resolved by name from the compute stage's
	     bindings - the app packs only its lighting uniforms, and the staging
	     re-bases every sub-range to vertex 0 so the draw's first triangle is 0;
	   - the frame's first geom draw clears the fb + depth canvases, because
	     the kernel writes only covered pixels and has no render-pass clear.
	     The daemon clears them GPU-side (draw_compute_geom's first_frame
	     fills); the VERY FIRST geom frame also clears CPU-side (until that
	     first daemon clear has succeeded, _geom_fill_live stays 0 and this
	     loop runs - NOCACHE shm makes the CPU clear visible to the GPU
	     as-is).  _clear_rgba is the back end's 0xAABBGGRR; the kernel's fb
	     layout is the shadertoy-route 0xAARRGGBB, so R and B swap. */
	geom = pgl_g3d_shader_is_geom(prog);
	geom_texcol = geom && pgl_g3d_shader_geom_texcol(prog);
	geom_tex = geom && !geom_texcol && pgl_g3d_shader_geom_tex(prog);
	geom_inst = geom && pgl_g3d_shader_geom_inst(prog);
	unif_n = _unif_count;
	if (geom) {
		uint32_t one[1], wh[2], span;
		float fw = (float)_w, fh = (float)_h;
		if (geom_tex || geom_texcol) {
			/* Textured variant: attr1 carries (u,v,0) per vertex and the
			   texture rides ctex unit 0 as a LINEAR (raster) uint32 image
			   the kernel indexes directly (no TMU).  Require the bound slot
			   to be a raster upload (pgl_g3d_upload_texture); a tiled slot
			   cannot be indexed linearly, so decline to the CPU path. */
			int s = _ctex_bind[0];
			if (s < 0 || s >= PGL_G3D_MAX_TEX || _tex[s].pixels == NULL ||
			    _tex[s].tiling != PGLC_TEX_TILING_RASTER)
				return 0;
			one[0] = (uint32_t)_tex[s].w;
			pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
			                             "uTexW", one, 1u);
			one[0] = (uint32_t)_tex[s].h;
			pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
			                             "uTexH", one, 1u);
		} else if (geom_inst) {
			/* Instanced variant: the per-instance matrix buffer must have been
			   staged this frame (pgl_g3d_set_instances); without it binding 3
			   would be unbound and the daemon declines, so fall back to the CPU
			   path here instead.  The matrices ride ctex unit 0 (marshalled
			   below); the app packs only uLightEye/uShade as named uniforms. */
			if (_inst == NULL || _inst_floats == 0)
				return 0;
		}
		/* uViewportWH is declared by BOTH the xform and the raster kernel, so
		   patch_uniform writes it into each region.  The rest are unique to one
		   region: uVertCount feeds the xform (pass 1) vertex loop bound; the
		   uFbStride/uTriFirst/uTriCount feed the raster (pass 2) kernel.  The
		   staging re-bases every sub-range to vertex 0, so uTriFirst is 0 and
		   uVertCount/uTriCount cover exactly this draw's sub-range. */
		memcpy(&wh[0], &fw, 4);
		memcpy(&wh[1], &fh, 4);
		pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
		                             "uViewportWH", wh, 2u);
		one[0] = (uint32_t)_w;
		pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
		                             "uFbStride", one, 1u);
		one[0] = 0u;
		pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
		                             "uTriFirst", one, 1u);
		one[0] = count / 3u;
		pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
		                             "uTriCount", one, 1u);
		one[0] = count;
		pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
		                             "uVertCount", one, 1u);
		/* Per-draw render state for the gouraud raster kernel (uBlendMode /
		   uDepthWrite / uCullSign).  The INSTANCED-LIT/gouraud route honours
		   the app's blend, depth-mask and cull state so translucent shells and
		   additive glows composite on the GPU.  EVERY OTHER route is pinned to
		   the historical hardcoded behaviour (opaque overwrite, always depth-
		   write, no cull) so teapot/gears/texcol pixels are byte-identical to
		   before these uniforms existed - those kernels either declare none of
		   them (textured/texcol: the patch is a no-op) or, for the plain gouraud
		   kernel, would otherwise start culling/blending they never did. */
		{
			int inst_lit = pgl_g3d_shader_geom_inst_lit(prog);
			one[0] = inst_lit ? (uint32_t)_blend_mode : PGL_G3D_BLEND_OPAQUE;
			pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
			                             "uBlendMode", one, 1u);
			one[0] = inst_lit ? (depth_mask ? 1u : 0u) : 1u;
			pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
			                             "uDepthWrite", one, 1u);
			one[0] = inst_lit ? pgl_g3d_cull_sign(cull) : 0u;
			pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
			                             "uCullSign", one, 1u);
		}
		if (geom_texcol) {
			/* The colour-modulated route is the only geom kernel that clips:
			   pack the per-draw scissor window (fb row space, set by the
			   engine via pgl_g3d_set_scissor) into the raster kernel's
			   uScissor.  w/h<=0 leaves it disabled, so a full-frame draw
			   needs no special case. */
			uint32_t sc[4];
			sc[0] = (uint32_t)_scissor_x;
			sc[1] = (uint32_t)_scissor_y;
			sc[2] = (uint32_t)_scissor_w;
			sc[3] = (uint32_t)_scissor_h;
			pgl_g3d_shader_patch_uniform(prog, _unif, PGL_G3D_MAX_UNIF,
			                             "uScissor", sc, 4u);
		}
		/* The daemon splits the blob at the program's cs2_uniform_blob_dwords,
		   so it must receive the FULL concatenated span [xform | raster] even
		   where the app staged fewer words.  Size it exactly. */
		span = pgl_g3d_shader_geom_blob_dwords(prog);
		if (span > PGL_G3D_MAX_UNIF)
			return 0;   /* cannot happen with the shipped kernels */
		if (span > unif_n)
			unif_n = span;

		if (_first_frame && _fb != NULL && !_geom_fill_live) {
			uint32_t c = _clear_rgba;
			uint32_t px = (c & 0xFF000000u) |
			              ((c & 0x000000FFu) << 16) |
			              (c & 0x0000FF00u) |
			              ((c & 0x00FF0000u) >> 16);
			uint32_t n = (uint32_t)_w * (uint32_t)_h, i;
			for (i = 0; i < n; i++)
				_fb[i] = px;
			if (_z != NULL)
				for (i = 0; i < n; i++)
					_z[i] = 0xFFFFFFFFu;   /* 24-bit unorm far */
		}
	}

	g2d_pgl_draw_req_init(&req, fb_canvas(), pos, col,
	                      PGL_G3D_MODE_TRIANGLES, count,
	                      G2D_PGL_FMT_RGBA8, (uint32_t)_w * 4u,
	                      PGL_G3D_FS_SOLID, 0u, 0,
	                      depth_func, (uint8_t)depth_mask, (uint8_t)cull,
	                      z, (uint32_t)_w * 4u, (uint8_t)_first_frame,
	                      _clear_rgba, _clear_z);
	/* fs_id is ignored on the compiled path (prog_handle >= 0); the per-draw
	   uniform blob staged by pgl_g3d_set_uniforms feeds the VS/coord/FS
	   QUNIFORM expansion.  contig=1 mirrors the CPU-read blob upload.  unif_n
	   covers any per-draw kernel words the geom branch just patched beyond
	   the app-staged count. */
	g2d_pgl_draw_req_set_shader(&req, dh, _unif_shm, unif_n, 1);

	/* WS4 sampler bindings: identical marshalling to flush() above, so a
	   compiled-mesh draw whose FS samples a texture binds its ctex units
	   exactly like the batched full-screen-quad path.  A mesh draw that
	   binds no sampler (teapot / gears, whose _ctex_bind stays all -1)
	   skips every slot, so this is a safe no-op for the untextured compiled
	   meshes.  The GEOM-TEX kernel route needs the opposite selection: its
	   binding-4 texture is indexed as a linear array, so unit 0 carries the
	   RASTER slot (validated in the geom branch above) and no other unit is
	   bound.  The binner route binds only tiled slots (the TMU cannot sample
	   a raster image); an unbound unit the FS references makes the back end
	   decline the draw (CPU fallback). */
	if (geom_tex || geom_texcol) {
		const pgl_g3d_tex_slot_t* t = &_tex[_ctex_bind[0]];
		req.ctex[0] = g2d_canvas(t->shm, t->size,
		                         (uint32_t)t->w, (uint32_t)t->h, 1);
		req.ctex[0].phy  = t->phy;
		req.ctex_w[0]      = (uint32_t)t->w;
		req.ctex_h[0]      = (uint32_t)t->h;
		req.ctex_stride[0] = t->stride;
		req.ctex_tiling[0] = (uint8_t)t->tiling;
		req.ctex_ub_pad[0] = (uint8_t)t->ub_pad;
	} else if (geom_inst) {
		/* INSTANCED route: ctex unit 0 carries the per-frame instance-matrix
		   buffer (binding 3 of the xform kernel), modelled as a 1-row canvas of
		   _inst_floats dwords so the generic ctex plumbing resolves its physical
		   base and the daemon hands it to the kernel as a raw SSBO.  No texture
		   state record is built for it (the kernel indexes it linearly, no TMU). */
		uint32_t bytes = _inst_floats * 4u;
		req.ctex[0] = g2d_canvas(_inst_shm, bytes, _inst_floats, 1u, 1);
		req.ctex[0].phy  = _inst_phy;
		req.ctex_w[0]      = _inst_floats;
		req.ctex_h[0]      = 1u;
		req.ctex_stride[0] = bytes;
		req.ctex_tiling[0] = 0u;
		req.ctex_ub_pad[0] = 0u;
	} else {
		int u;
		for (u = 0; u < PGL_G3D_MAX_TEX_UNITS; u++) {
			const pgl_g3d_tex_slot_t* t;
			int s = _ctex_bind[u];
			if (s < 0 || s >= PGL_G3D_MAX_TEX || _tex[s].pixels == NULL)
				continue;
			t = &_tex[s];
			if (t->tiling == PGLC_TEX_TILING_RASTER &&
			    !pglc_tex_sampler_reads_raster())
				continue;
			req.ctex[u] = g2d_canvas(t->shm, t->size,
			                         (uint32_t)t->w, (uint32_t)t->h, 1);
			req.ctex[u].phy  = t->phy;
			req.ctex_w[u]      = (uint32_t)t->w;
			req.ctex_h[u]      = (uint32_t)t->h;
			req.ctex_stride[u] = t->stride;
			req.ctex_tiling[u] = (uint8_t)t->tiling;
			req.ctex_ub_pad[u] = G2D_PGL_CTEX_UB_PAD_PACK(t->ub_pad,
			    _ctex_filter[u] == PGL_G3D_TEX_FILTER_LINEAR ?
			        G2D_PGL_CTEX_FILTER_LINEAR : G2D_PGL_CTEX_FILTER_NEAREST);
			/* A cubemap slot carries its 6-layer array geometry so the
			   daemon builds image_depth=5 + array_stride; a plain 2D slot
			   leaves both 0 (single-layer record, unchanged). */
			req.ctex_depth[u]       = (uint8_t)t->depth;
			req.ctex_arrstride64[u] = t->arrstride64;
		}
	}

	res = g2d_pgl_draw(&req);
	if (res != G2D_OK) {
		/* NOT_SUPPORTED is a PRE-submission decline by the back end specific
		   to this program (unbound sampler unit, unsupported uniform kind...):
		   latch only the program so other effects and the fixed-function path
		   keep the GPU.  Anything else (a real dispatch failure/timeout) latches
		   the GPU off: never replay.  Mirrors flush()'s handling. */
		if (res == G2D_ERR_NOT_SUPPORTED)
			pgl_g3d_shader_mark_declined(prog);
		else
			_enabled = 0;   /* sticky: never replay, fall back to CPU */
		return 0;
	}
	if (geom && _first_frame)
		_geom_fill_live = 1;   /* the daemon clears fb+depth GPU-side from the
		                          next frame on; the CPU clear is skipped. */
	_first_frame = 0;   /* this frame's clear has been consumed */
	return 1;
}

/* Shared body of the raster / tiled texture uploads: (re)allocate the slot's
   contig-shm image (layout->size bytes), shuffle the caller's raster rgba8
   rows into it per *layout and record the layout on the slot. */
static int tex_slot_fill(int slot, int w, int h, const void* rgba8,
                         const struct pglc_tex_layout* layout)
{
	pgl_g3d_tex_slot_t* t;
	uint32_t* px = NULL;
	ewokos_addr_t phy = 0;
	int shm = 0;

	if (!_enabled)
		return 0;
	if (slot < 0 || slot >= PGL_G3D_MAX_TEX)
		return 0;
	if (w <= 0 || h <= 0 || rgba8 == NULL || layout->size == 0)
		return 0;

	t = &_tex[slot];

	/* Replace-in-place: free the previous image first so a re-upload of the
	   same slot does not leak.  A pending TEX_MOD batch referencing this slot
	   is flushed so the driver never sees the freed physical base. */
	if (t->pixels != NULL) {
		if (_nverts != 0 && _batch_fs == PGL_G3D_FS_TEX_MOD &&
		    _batch_tex_slot == slot)
			pgl_g3d_flush();
		g2d_shm_free(t->pixels);
		memset(t, 0, sizeof(*t));
	}

	if (g2d_shm_alloc_phy(layout->size, &shm, &px, &phy) != 0
	    || shm <= 0 || px == NULL)
		return 0;   /* allocation failure does NOT latch the GPU off */
	pglc_tex_store_2d(px, rgba8, (uint32_t)w * 4u, (uint32_t)w, (uint32_t)h,
	                 4u, layout);

	t->shm = shm;
	t->pixels = px;
	t->phy = phy;
	t->w = w;
	t->h = h;
	t->tiling = layout->tiling;
	t->ub_pad = layout->ub_pad;
	t->stride = layout->stride;
	t->size   = layout->size;
	return 1;
}

int pgl_g3d_upload_texture(int slot, int w, int h, const void* rgba8)
{
	struct pglc_tex_layout layout;

	if (w <= 0 || h <= 0)
		return 0;
	/* raster: tightly packed rows, no padding (what the TEX_MOD kernel reads) */
	memset(&layout, 0, sizeof(layout));
	layout.tiling   = PGLC_TEX_TILING_RASTER;
	layout.padded_w = (uint32_t)w;
	layout.padded_h = (uint32_t)h;
	layout.stride   = (uint32_t)w * 4u;
	layout.size     = layout.stride * (uint32_t)h;
	return tex_slot_fill(slot, w, h, rgba8, &layout);
}

int pgl_g3d_upload_texture_tiled(int slot, int w, int h, const void* rgba8)
{
	struct pglc_tex_layout layout;

	if (w <= 0 || h <= 0)
		return 0;
	if (!pglc_tex_layout_2d((uint32_t)w, (uint32_t)h, 4u, &layout))
		return 0;
	return tex_slot_fill(slot, w, h, rgba8, &layout);
}

int pgl_g3d_upload_texture_cube_tiled(int slot, int w, int h,
                                      const void* faces_rgba8)
{
	struct pglc_tex_layout layout;
	pgl_g3d_tex_slot_t* t;
	const uint8_t* src = (const uint8_t*)faces_rgba8;
	uint32_t* pxw = NULL;
	uint8_t* px;
	ewokos_addr_t phy = 0;
	int shm = 0;
	uint32_t layer_size, total;
	int f;

	/* distinct negative codes so the caller can log exactly which gate
	   failed (1 == success); a failure never latches the GPU off. */
	if (!_enabled)
		return -1;
	if (slot < 0 || slot >= PGL_G3D_MAX_TEX)
		return -2;
	/* a cubemap is six square faces; portablegl stores them concatenated in
	   one buffer (6*w*h*4) with w == h. */
	if (w <= 0 || w != h || src == NULL)
		return -3;
	if (!pglc_tex_layout_2d((uint32_t)w, (uint32_t)h, 4u, &layout))
		return -4;
	if (layout.size == 0)
		return -5;

	/* Round each layer up to 64 bytes so array_stride_64_byte_aligned is an
	   exact integer and every face base stays 64-byte aligned (the texture
	   base pointer's low 6 bits are overlaid by flag fields). */
	layer_size = (layout.size + 63u) & ~63u;
	total = layer_size * 6u;

	t = &_tex[slot];
	/* Replace-in-place: free the previous image first so a re-upload of the
	   same slot does not leak; flush a pending TEX_MOD batch on this slot so
	   the driver never sees the freed physical base (mirrors tex_slot_fill). */
	if (t->pixels != NULL) {
		if (_nverts != 0 && _batch_fs == PGL_G3D_FS_TEX_MOD &&
		    _batch_tex_slot == slot)
			pgl_g3d_flush();
		g2d_shm_free(t->pixels);
		memset(t, 0, sizeof(*t));
	}

	if (g2d_shm_alloc_phy(total, &shm, &pxw, &phy) != 0
	    || shm <= 0 || pxw == NULL)
		return -6;   /* allocation failure does NOT latch the GPU off */
	px = (uint8_t*)pxw;

	memset(px, 0, total);
	for (f = 0; f < 6; f++)
		pglc_tex_store_2d(px + (size_t)f * layer_size,
		                 src + (size_t)f * (size_t)w * (size_t)h * 4u,
		                 (uint32_t)w * 4u, (uint32_t)w, (uint32_t)h,
		                 4u, &layout);

	t->shm = shm;
	t->pixels = pxw;
	t->phy = phy;
	t->w = w;
	t->h = h;
	t->tiling = layout.tiling;
	t->ub_pad = layout.ub_pad;
	t->stride = layout.stride;
	t->size   = total;
	t->cube   = 1;
	t->depth  = 6u;
	t->arrstride64 = layer_size / 64u;
	return 1;
}

void pgl_g3d_free_texture(int slot)
{
	pgl_g3d_tex_slot_t* t;

	if (slot < 0 || slot >= PGL_G3D_MAX_TEX)
		return;
	t = &_tex[slot];
	if (t->pixels == NULL)
		return;
	/* flush any pending batch that references this slot so the driver never
	   sees a dangling physical base after the shm segment is released */
	if (_nverts != 0 && _batch_fs == PGL_G3D_FS_TEX_MOD &&
	    _batch_tex_slot == slot)
		pgl_g3d_flush();
	g2d_shm_free(t->pixels);
	memset(t, 0, sizeof(*t));
}

int pgl_g3d_clear(uint32_t rgba8)
{
	g2d_pgl_req_t req;
	int res;

	if (!_enabled)
		return 0;

	/* a clear must land after any pending draws to keep painter's order */
	pgl_g3d_flush();
	if (!_enabled)
		return 0;

	g2d_pgl_clear_req_init(&req, fb_canvas(), G2D_PGL_FMT_RGBA8,
	                       (uint32_t)_w * 4u, rgba8, 0);
	res = g2d_pgl_clear(&req);
	if (res != G2D_OK) {
		_enabled = 0;
		return 0;
	}
	return 1;
}

static void free_all(void)
{
	int i;

	if (_fb)  { g2d_shm_free(_fb);  _fb = NULL; }
	if (_z)   { g2d_shm_free(_z);   _z = NULL; }
	if (_rs)  { g2d_shm_free(_rs);  _rs = NULL; }
	_rs_shm = 0; _rs_phy = 0; _rs_cap = 0; _rs_w = _rs_h = 0;
	_fb_cap = 0;
	if (_pos) { g2d_shm_free((uint32_t*)_pos); _pos = NULL; }
	if (_col) { g2d_shm_free(_col); _col = NULL; }
	if (_uv)  { g2d_shm_free((uint32_t*)_uv);  _uv = NULL; }
	if (_nrm) { g2d_shm_free((uint32_t*)_nrm); _nrm = NULL; }
	if (_unif) { g2d_shm_free(_unif); _unif = NULL; }
	if (_inst) { g2d_shm_free(_inst); _inst = NULL; }
	_inst_shm = 0;
	_inst_phy = 0;
	_inst_floats = 0;
	_inst_cap = 0;
	for (i = 0; i < PGL_G3D_MAX_TEX; i++) {
		if (_tex[i].pixels) {
			g2d_shm_free(_tex[i].pixels);
			_tex[i].pixels = NULL;
		}
		_tex[i].shm = 0;
		_tex[i].phy = 0;
		_tex[i].w = _tex[i].h = 0;
	}
	for (i = 0; i < PGL_G3D_MAX_MESH; i++) {
		if (_mesh[i].pos) { free(_mesh[i].pos); _mesh[i].pos = NULL; }
		if (_mesh[i].nrm) { free(_mesh[i].nrm); _mesh[i].nrm = NULL; }
		if (_mesh[i].idx) { free(_mesh[i].idx); _mesh[i].idx = NULL; }
		_mesh[i].instanced = 0;
		_mesh[i].nverts = 0;
		_mesh[i].used = 0;
	}
	_fb_shm = _z_shm = _pos_shm = _col_shm = _uv_shm = _nrm_shm = _unif_shm = 0;
	_fb_phy = _z_phy = _pos_phy = _col_phy = _uv_phy = _nrm_phy = 0;
	_nverts = 0;
	_geom_fill_live = 0;
	_batch_tex_slot = -1;
	{
		int u;
		for (u = 0; u < PGL_G3D_MAX_TEX_UNITS; u++)
			_ctex_bind[u] = -1;
	}
	_unif_count = 0;
	_active_prog = PGL_G3D_PROG_INVALID;
	_batch_prog = PGL_G3D_PROG_INVALID;
}

/* Grow-with-headroom capacity for the physical fb/z targets.
 *
   g2d_shm_free() is a client-side shmdt only; the g2dd attach cache holds its
   own shmat reference on every segment it has drawn into, so the kernel does
   not reclaim the contig-slab run until the cache's idle sweep drops the entry
   (G2D_ATTACH_CACHE_TTL_MS == 250ms, system/gui/drivers/g2dd/g2dd.c).  The
   contig sub-allocator (kernel/kernel/src/mm/shm.c) is first-fit with a hard
   SHM_POOL_MAX == 128 entry cap and splits a free run on every alloc, so a
   segment its owner already released still occupies a pool entry while the
   daemon pins it.

   The previous grow path reallocated fb/z to the EXACT new size on every
   incremental resize, so a continuous drag-to-enlarge orphaned a pair of
   segments per event (~60/s) - far faster than the 250ms sweep reclaims them.
   Within a few hundred ms the pinned orphans exhausted the 64-entry pool,
   shm_pool_alloc returned 0, shmget(IPC_CONTIG) failed, alloc_target failed,
   pgl_g3d_resize returned 0 and pglResizeFramebuffer latched the GPU off
   (_pgl_gpu_off = 1) for the rest of the process.  gears and teapot both hit
   this because they share the portablegl resize path.

   Rounding the grown capacity up to 1.5x the requested pixels (on a 16KB
   granularity) collapses a drag's O(events) reallocations into O(log ratio):
   the window must grow 50% in area before the next realloc, which at any
   realistic drag speed leaves far more than one TTL between grows, so the
   sweep reclaims each orphan before the next is created.  Resizes that fit the
   allocated capacity stay on the zero-slab-op reuse path in pgl_g3d_resize. */
static uint32_t fb_grow_capacity(uint32_t need_pixels)
{
	const uint32_t gran = 4096u;   /* 16KB slab granularity */
	uint32_t cap = need_pixels + need_pixels / 2u;   /* 1.5x headroom */
	return (cap + gran - 1u) & ~(gran - 1u);
}

/* Allocate the physical fb/z pair.  w/h are the LOGICAL viewport the daemon
   renders into (fb_canvas marshals them as the canvas dims and _w*4 as the row
   stride); cap_pixels is the physical capacity to reserve, which may exceed
   w*h so a later in-capacity resize needs no slab operation.  The daemon writes
   only w*h tightly-packed pixels, so the spare capacity is never touched. */
static int alloc_target(int w, int h, uint32_t cap_pixels)
{
	uint32_t* px = NULL;
	uint32_t* zpx = NULL;
	ewokos_addr_t phy = 0, zphy = 0;
	int shm = 0, zshm = 0;
	uint32_t need = (uint32_t)w * (uint32_t)h;

	if (w <= 0 || h <= 0)
		return 0;
	if (cap_pixels < need)
		cap_pixels = need;
	if (g2d_shm_alloc_phy(cap_pixels * 4u, &shm, &px, &phy) != 0
	    || shm <= 0 || px == NULL)
		return 0;
	/* persistent Z32F depth surface, same footprint as the colour target */
	if (g2d_shm_alloc_phy(cap_pixels * 4u, &zshm, &zpx, &zphy) != 0
	    || zshm <= 0 || zpx == NULL) {
		g2d_shm_free(px);
		return 0;
	}

	_fb_shm = shm;
	_fb = px;
	_fb_phy = phy;
	_z_shm = zshm;
	_z = zpx;
	_z_phy = zphy;
	_w = w;
	_h = h;
	_fb_cap = cap_pixels;
	return 1;
}

int pgl_g3d_resize(int w, int h)
{
	if (!_enabled)
		return 0;
	if (w <= 0 || h <= 0)
		return 0;
	if (w == _w && h == _h)
		return 1;

	pgl_g3d_flush();

	/* Grow-only physical targets. The kernel contig-slab sub-allocator is
	   first-fit with a hard entry cap (SHM_POOL_MAX) and splits on every
	   alloc, merging only physically-adjacent free runs. The previous
	   alloc-new-before-free-old order forced the slab to hold both the old
	   and new fb/z at once, so the still-held old run could not satisfy the
	   (often smaller) new request; each resize fragmented the slab further
	   and after 2-3 resizes a contig alloc failed and the GPU latched off
	   even though the slab had ample total free memory. Reusing the existing
	   runs whenever the new viewport fits the allocated capacity makes a
	   shrink (or any in-capacity resize) perform ZERO slab operations - it
	   only retargets the viewport. The daemon is handed the logical _w/_h as
	   the viewport and _w*4 as the row stride every draw, and writes only
	   _w*_h pixels tightly packed, so a larger physical buffer is safe. */
	if ((uint32_t)w * (uint32_t)h <= _fb_cap) {
		_w = w;
		_h = h;
		_first_frame = 1;   /* a resized target starts a fresh frame */
		return 1;
	}

	/* Growth beyond capacity: free the old runs FIRST, then allocate the new
	   target with 1.5x headroom (fb_grow_capacity) so a continuous drag does
	   not reallocate - and orphan a daemon-pinned segment pair in the contig
	   pool - on every incremental size increase.  The freed runs stay pinned
	   by the g2dd attach cache until its 250ms sweep, so bounding the NUMBER
	   of grows (not their order) is what keeps the 64-entry pool from
	   exhausting.  On failure the GPU latches off (the caller in
	   pglResizeFramebuffer shuts down and falls back to CPU). */
	if (_fb) { g2d_shm_free(_fb); _fb = NULL; }
	if (_z)  { g2d_shm_free(_z);  _z = NULL; }
	_fb_cap = 0;
	if (!alloc_target(w, h, fb_grow_capacity((uint32_t)w * (uint32_t)h))) {
		_enabled = 0;
		return 0;
	}
	_first_frame = 1;   /* a resized target starts a fresh frame */
	return 1;
}

/* Cached live GPU version code (0 = not queried yet; g2d_get_version()'s
   backend-neutral code, e.g. V3D 42/71, Adreno 690).  soft3d ships ONE binary
   across chip generations on raspix (Pi3 V3D 2.1 / Pi4 4.2 / Pi5 7.1) and the
   compiler runs in the APP process, so the codegen target must be latched to
   the actual silicon BEFORE the first pglc_compile_*().  Shaders are
   compiled at glLinkProgram (pgl_g3d_program_compile), which runs before
   pgl_g3d_init()'s first-draw probe, so the query lives here and is called from
   both sites.  Idempotent: a successful query is cached and never re-issued; a
   failed one (daemon not yet reachable) leaves the cache at 0 so the next caller
   retries rather than silently compiling for the backend's default device. */
static uint32_t _gpu_ver;

uint32_t pgl_g3d_ensure_version(void)
{
	uint32_t v = 0;

	if (_gpu_ver != 0u)
		return _gpu_ver;
	if (g2d_get_version(&v) == G2D_OK && v != 0u) {
		_gpu_ver = v;
		pglc_set_version((int)v);
	}
	return _gpu_ver;   /* 0 when no query has succeeded yet */
}

int pgl_g3d_init(int w, int h)
{
	uint32_t* pos_px = NULL;
	uint32_t* col_px = NULL;
	uint32_t* uv_px = NULL;
	uint32_t* nrm_px = NULL;
	uint32_t* unif_px = NULL;
	ewokos_addr_t pos_phy = 0, col_phy = 0, uv_phy = 0, nrm_phy = 0;
	int pos_shm = 0, col_shm = 0, uv_shm = 0, nrm_shm = 0, unif_shm = 0;

	if (_probed)
		return _enabled;
	_probed = 1;

	/* has_g2d() uses the tree-wide 0=success convention (graph.c, displayd.c,
	   g2dtest.c all test `has_g2d() == 0` for present / `!= 0` for absent):
	   it returns 0 when the g2d daemon is reachable and -1 when it is not.
	   The previous `!has_g2d()` test was inverted, so init bailed out on
	   exactly the devices that DO have g2d. */
	if (has_g2d() != 0)
		return 0;

	/* Latch the codegen target to the live silicon (idempotent; the shaders were
	   already compiled against it at glLinkProgram via the same call in
	   pgl_g3d_program_compile).  A failed query (0), or a GPU code the machine
	   compiler backend does not support (V3D: ver < 41, i.e. VC4 / V3D 2.1 on
	   Pi3, which has no CSD compute for the geom rasterizer), declines the GPU
	   path entirely and portablegl keeps its CPU/NEON rasterizer.  The backend
	   owns that judgement (pglc_version_supported), not this shared client. */
	if (!pglc_version_supported(pgl_g3d_ensure_version()))
		return 0;

	if (!alloc_target(w, h, fb_grow_capacity((uint32_t)w * (uint32_t)h)))
		return 0;

	/* frame-boundary defaults so the very first dispatch clears even if a
	   frame is drawn before the first pgl_Clear/pgl_g3d_begin_frame */
	_first_frame = 1;
	_clear_rgba = 0xFF000000u;
	_clear_z = 1.0f;

	if (g2d_shm_alloc_phy(PGL_G3D_POS_BYTES, &pos_shm, &pos_px, &pos_phy) != 0
	    || pos_shm <= 0 || pos_px == NULL) {
		free_all();
		return 0;
	}
	if (g2d_shm_alloc_phy(PGL_G3D_COL_BYTES, &col_shm, &col_px, &col_phy) != 0
	    || col_shm <= 0 || col_px == NULL) {
		g2d_shm_free(pos_px);
		free_all();
		return 0;
	}
	/* Phase D uv staging: allocated unconditionally so a TEX_MOD batch never
	   allocates on the hot path.  A failure here does NOT latch the GPU off;
	   the colour path stays fully usable and TEX_MOD declines to CPU. */
	if (g2d_shm_alloc_phy(PGL_G3D_UV_BYTES, &uv_shm, &uv_px, &uv_phy) != 0
	    || uv_shm <= 0 || uv_px == NULL) {
		uv_px = NULL;
		uv_shm = 0;
		uv_phy = 0;
	}
	/* WS4 compiled-mesh normal staging: allocated unconditionally so a
	   2-attribute mesh draw never allocates on the hot path.  A failure here
	   does NOT latch the GPU off; the fixed-function and full-screen-quad
	   compiled paths stay usable and only the mesh path declines. */
	if (g2d_shm_alloc_phy(PGL_G3D_NRM_BYTES, &nrm_shm, &nrm_px, &nrm_phy) != 0
	    || nrm_shm <= 0 || nrm_px == NULL) {
		nrm_px = NULL;
		nrm_shm = 0;
		nrm_phy = 0;
	}
	_pos_shm = pos_shm; _pos = (float*)pos_px; _pos_phy = pos_phy;
	_col_shm = col_shm; _col = col_px;         _col_phy = col_phy;
	_uv_shm  = uv_shm;  _uv  = (float*)uv_px;  _uv_phy  = uv_phy;
	_nrm_shm = nrm_shm; _nrm = (float*)nrm_px; _nrm_phy = nrm_phy;
	_batch_tex_slot = -1;

	/* WS4 uniform staging: a CPU-read contig-shm blob the daemon copies to
	   expand the compiled program's QUNIFORM stream.  Allocated non-phy (the
	   GPU never reads it directly); a failure does NOT latch the GPU off - the
	   fixed-function path stays live and the compiled path simply declines. */
	if (g2d_shm_alloc(PGL_G3D_UNIF_BYTES, &unif_shm, &unif_px) != 0
	    || unif_shm <= 0 || unif_px == NULL) {
		unif_shm = 0;
		unif_px = NULL;
	}
	_unif_shm = unif_shm;
	_unif = unif_px;
	_unif_count = 0;

	/* provisional: so pgl_g3d_clear()'s flush path is live for the probe */
	_enabled = 1;
	if (!pgl_g3d_clear(0xFF000000u)) {   /* opaque black probe clear */
		free_all();
		return 0;
	}
	return 1;
}

void pgl_g3d_shutdown(void)
{
	if (_enabled)
		pgl_g3d_flush();
	_enabled = 0;
	free_all();
	pgl_g3d_program_free_all();   /* drop every cached compiled GLSL program */
	_probed = 0;   /* a fresh context may re-probe the GPU path */
}

#endif /* PGL_G3D */
