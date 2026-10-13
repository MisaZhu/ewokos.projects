/*
 * pgl_vc3d.c - portablegl VideoCore (V3D 7.1) GPU back end, client half.
 * See pgl_vc3d.h for the contract.  Compiled only under -DPGL_VC3D (raspi5).
 *
 * Everything here is a thin marshaller over the stateless g2dclient protocol:
 * it owns one contig-shm render target (which portablegl adopts as its
 * back_buffer so CPU and GPU draws share one surface) plus fixed vertex/varying
 * staging, batches post-vertex-shader triangles, and turns a full batch into a
 * single G2D_DEV_CNTL_PGL_DRAW.  The raspi5 g2dd daemon resolves each canvas's
 * physical base and forwards to the V3D 3D driver; this file never touches V3D.
 *
 * The capability latch is deliberately conservative: a submitted dispatch can
 * never be replayed, so the first non-OK result (G2D_ERR_NOT_SUPPORTED, or a
 * transient G2D_ERR_FAILED) latches the GPU off for the rest of the process and
 * the caller keeps its CPU rasterizer.  init() probes with a clear into the
 * freshly-allocated target (which the caller overwrites) so an unsupported
 * platform is discovered before any triangle is batched.
 */

#ifdef PGL_VC3D

#include <portablegl/pgl_vc3d.h>
#include <g2dclient/g2dclient.h>
#include <string.h>

/* must match G2D_PGL_MAX_VERTS / VC_PGL3D_MAX_VERTS */
#define PGL_VC3D_MAX_VERTS 4096u
/* pos is modelled as a w=nverts h=3 canvas (nverts*3*4 bytes); col as a
   w=nverts h=1 canvas (nverts*1*4 bytes) so g2d_attach's size >= w*h*4 holds */
#define PGL_VC3D_POS_BYTES (PGL_VC3D_MAX_VERTS * 3u * 4u)
#define PGL_VC3D_COL_BYTES (PGL_VC3D_MAX_VERTS * 1u * 4u)
/* Phase D: uv float3 per vertex (u, v, pad=0) sharing the pos encoding's
   12-byte stride, so the driver reuses its proven vec_size=3/type=FLOAT
   attribute record instead of an untested float2 combination. */
#define PGL_VC3D_UV_BYTES  (PGL_VC3D_MAX_VERTS * 3u * 4u)

static int      _probed;     /* init() has run (success or not) */
static int      _enabled;    /* GPU path engaged and not latched off */

static int      _w, _h;
static int      _fb_shm;
static uint32_t* _fb;        /* render-target pixels (RGBA8) */
static ewokos_addr_t _fb_phy;

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

/* Phase D texture table (see PGL_VC3D_MAX_TEX): each slot owns a contig-shm
   linear RGBA8 image addressable by physical base.  A slot is "loaded" iff
   .pixels != NULL. */
typedef struct {
	int            shm;
	uint32_t*      pixels;
	ewokos_addr_t  phy;
	int            w, h;
} pgl_vc3d_tex_slot_t;
static pgl_vc3d_tex_slot_t _tex[PGL_VC3D_MAX_TEX];

static uint32_t _nverts;     /* vertices currently batched */
static uint32_t _batch_mode; /* primitive mode of the open batch (PGL_VC3D_MODE_*) */
static int      _batch_fs;   /* fs_id of the open batch */
static uint32_t _batch_rgba; /* solid colour of the open batch */
/* depth/cull state of the open batch (a batch is homogeneous in these too,
   because they are per-dispatch render state on the GPU) */
static uint32_t _batch_depth_func;
static uint32_t _batch_depth_mask;
static uint32_t _batch_cull;
/* Phase D: texture slot of the open batch (only meaningful when _batch_fs ==
   PGL_VC3D_FS_TEX_MOD); part of the homogeneity key so a TEX_MOD batch never
   mixes two texture images in one dispatch.  -1 = "no texture". */
static int      _batch_tex_slot = -1;
/* Phase B2c extended render state of the open batch (scissor / polygon offset /
   colour mask); part of the homogeneity key, copied into req.ext on flush. */
static pgl_vc3d_ext_t _batch_ext;

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

int pgl_vc3d_enabled(void)
{
	return _enabled;
}

void* pgl_vc3d_pixels(void)
{
	return (void*)_fb;
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

void pgl_vc3d_flush(void)
{
	g2d_pgl_req_t req;
	g2d_canvas_t pos, col, z, texcv, uvcv;
	int res;
	int tex_path;

	if (!_enabled || _nverts == 0)
		return;

	pos = g2d_canvas(_pos_shm, PGL_VC3D_POS_BYTES, _nverts, 3, 1);
	pos.phy = _pos_phy;
	z = z_canvas();

	tex_path = (_batch_fs == PGL_VC3D_FS_TEX_MOD &&
	            _batch_tex_slot >= 0 &&
	            _batch_tex_slot < PGL_VC3D_MAX_TEX &&
	            _tex[_batch_tex_slot].pixels != NULL);
	if (tex_path) {
		const pgl_vc3d_tex_slot_t* t = &_tex[_batch_tex_slot];
		/* col is unused on the TEX_MOD path: send a zero-size canvas so the
		   g2dd handler skips the attach and the back end keeps col_iova == 0.
		   uv is modelled w=nverts h=3 (nverts*3*4 == nverts*12 bytes). */
		col = g2d_canvas(0, 0, 0, 0, 0);
		uvcv = g2d_canvas(_uv_shm, PGL_VC3D_UV_BYTES, _nverts, 3, 1);
		uvcv.phy = _uv_phy;
		texcv = g2d_canvas(t->shm, (uint32_t)t->w * (uint32_t)t->h * 4u,
		                   (uint32_t)t->w, (uint32_t)t->h, 1);
		texcv.phy = t->phy;
	} else {
		col = g2d_canvas(_col_shm, PGL_VC3D_COL_BYTES, _nverts, 1, 1);
		col.phy = _col_phy;
		texcv = g2d_canvas(0, 0, 0, 0, 0);
		uvcv  = g2d_canvas(0, 0, 0, 0, 0);
	}

	g2d_pgl_draw_req_init(&req, fb_canvas(), pos, col,
	                      _batch_mode, _nverts,
	                      G2D_PGL_FMT_RGBA8, (uint32_t)_w * 4u,
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
		const pgl_vc3d_tex_slot_t* t = &_tex[_batch_tex_slot];
		g2d_pgl_draw_req_set_tex(&req, texcv,
		                           (uint32_t)t->w, (uint32_t)t->h, 0u, uvcv);
	}

	_nverts = 0;
	res = g2d_pgl_draw(&req);
	if (res != G2D_OK)
		_enabled = 0;   /* sticky: never replay, fall back to CPU */
	else
		_first_frame = 0;   /* this frame's clear has been consumed */
}

/* Mark the start of a new frame.  Flushes any batch still pending from the
   previous frame (so it lands as a normal LOAD dispatch of THAT frame), then
   records the colour/depth the next - the frame's first - GPU draw dispatch
   clears the persistent colour+Z surfaces to.  No dispatch is issued here; the
   clear rides along with the first draw.  No-op when the GPU path is off. */
void pgl_vc3d_begin_frame(uint32_t clear_rgba, float clear_z)
{
	if (!_enabled)
		return;
	pgl_vc3d_flush();
	_clear_rgba = clear_rgba;
	_clear_z = clear_z;
	_first_frame = 1;
}

/* Field-wise equality of two ext states (avoids a padding-sensitive memcmp on
   the struct's float/uint8 alignment gaps). */
static int ext_equal(const pgl_vc3d_ext_t* a, const pgl_vc3d_ext_t* b)
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
                         uint32_t cull_cfg, const pgl_vc3d_ext_t* ext,
                         int tex_slot, uint32_t nvert)
{
	pgl_vc3d_ext_t e;

	if (!_enabled)
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
	                     !ext_equal(&e, &_batch_ext)))
		pgl_vc3d_flush();
	/* room for nvert more vertices? */
	if (_nverts + nvert > PGL_VC3D_MAX_VERTS)
		pgl_vc3d_flush();
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
		_batch_ext = e;
	}
	return (int)_nverts;
}

int pgl_vc3d_batch_tri(int fs_id, uint32_t solid_rgba,
                       const float* n0, const float* n1, const float* n2,
                       const float* col0, const float* col1, const float* col2,
                       uint32_t depth_func, uint32_t depth_mask, uint32_t cull_cfg,
                       const pgl_vc3d_ext_t* ext)
{
	int base = batch_reserve(PGL_VC3D_MODE_TRIANGLES, fs_id, solid_rgba,
	                         depth_func, depth_mask, cull_cfg, ext, -1, 3u);
	if (base < 0)
		return 0;

	put_pos(base + 0, n0);
	put_pos(base + 1, n1);
	put_pos(base + 2, n2);

	if (fs_id == PGL_VC3D_FS_GOURAUD && col0 && col1 && col2) {
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

int pgl_vc3d_batch_point(int fs_id, uint32_t solid_rgba, const float* n0,
                         const float* col0, uint32_t depth_func,
                         uint32_t depth_mask, uint32_t cull_cfg,
                         const pgl_vc3d_ext_t* ext)
{
	int base = batch_reserve(PGL_VC3D_MODE_POINTS, fs_id, solid_rgba,
	                         depth_func, depth_mask, cull_cfg, ext, -1, 1u);
	if (base < 0)
		return 0;

	put_pos(base, n0);
	if (fs_id == PGL_VC3D_FS_GOURAUD && col0)
		put_col_f(base, col0);
	else
		put_col_u32(base, solid_rgba);

	_nverts += 1u;
	return 1;
}

int pgl_vc3d_batch_line(int fs_id, uint32_t solid_rgba,
                        const float* n0, const float* n1,
                        const float* col0, const float* col1,
                        uint32_t depth_func, uint32_t depth_mask,
                        uint32_t cull_cfg, const pgl_vc3d_ext_t* ext)
{
	int base = batch_reserve(PGL_VC3D_MODE_LINES, fs_id, solid_rgba,
	                         depth_func, depth_mask, cull_cfg, ext, -1, 2u);
	if (base < 0)
		return 0;

	put_pos(base + 0, n0);
	put_pos(base + 1, n1);

	if (fs_id == PGL_VC3D_FS_GOURAUD && col0 && col1) {
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
int pgl_vc3d_batch_tri_tex(int tex_slot,
                           const float* n0, const float* n1, const float* n2,
                           const float* uv0, const float* uv1, const float* uv2,
                           uint32_t depth_func, uint32_t depth_mask,
                           uint32_t cull_cfg, const pgl_vc3d_ext_t* ext)
{
	int base;

	if (tex_slot < 0 || tex_slot >= PGL_VC3D_MAX_TEX)
		return 0;
	if (_tex[tex_slot].pixels == NULL)
		return 0;   /* slot not uploaded -> caller falls back to CPU */
	if (!_uv)
		return 0;

	base = batch_reserve(PGL_VC3D_MODE_TRIANGLES, PGL_VC3D_FS_TEX_MOD, 0u,
	                     depth_func, depth_mask, cull_cfg, ext, tex_slot, 3u);
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

int pgl_vc3d_upload_texture(int slot, int w, int h, const void* rgba8)
{
	pgl_vc3d_tex_slot_t* t;
	uint32_t* px = NULL;
	ewokos_addr_t phy = 0;
	int shm = 0;
	size_t bytes;

	if (!_enabled)
		return 0;
	if (slot < 0 || slot >= PGL_VC3D_MAX_TEX)
		return 0;
	if (w <= 0 || h <= 0 || rgba8 == NULL)
		return 0;

	t = &_tex[slot];
	bytes = (size_t)w * (size_t)h * 4u;

	/* Replace-in-place: free the previous image first so a re-upload of the
	   same slot does not leak.  A pending TEX_MOD batch referencing this slot
	   is flushed so the driver never sees the freed physical base. */
	if (t->pixels != NULL) {
		if (_nverts != 0 && _batch_fs == PGL_VC3D_FS_TEX_MOD &&
		    _batch_tex_slot == slot)
			pgl_vc3d_flush();
		g2d_shm_free(t->pixels);
		t->pixels = NULL;
		t->shm = 0;
		t->phy = 0;
		t->w = t->h = 0;
	}

	if (g2d_shm_alloc_phy((uint32_t)bytes, &shm, &px, &phy) != 0
	    || shm <= 0 || px == NULL)
		return 0;   /* allocation failure does NOT latch the GPU off */
	memcpy(px, rgba8, bytes);

	t->shm = shm;
	t->pixels = px;
	t->phy = phy;
	t->w = w;
	t->h = h;
	return 1;
}

void pgl_vc3d_free_texture(int slot)
{
	pgl_vc3d_tex_slot_t* t;

	if (slot < 0 || slot >= PGL_VC3D_MAX_TEX)
		return;
	t = &_tex[slot];
	if (t->pixels == NULL)
		return;
	/* flush any pending batch that references this slot so the driver never
	   sees a dangling physical base after the shm segment is released */
	if (_nverts != 0 && _batch_fs == PGL_VC3D_FS_TEX_MOD &&
	    _batch_tex_slot == slot)
		pgl_vc3d_flush();
	g2d_shm_free(t->pixels);
	t->pixels = NULL;
	t->shm = 0;
	t->phy = 0;
	t->w = t->h = 0;
}

int pgl_vc3d_clear(uint32_t rgba8)
{
	g2d_pgl_req_t req;
	int res;

	if (!_enabled)
		return 0;

	/* a clear must land after any pending draws to keep painter's order */
	pgl_vc3d_flush();
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
	if (_pos) { g2d_shm_free((uint32_t*)_pos); _pos = NULL; }
	if (_col) { g2d_shm_free(_col); _col = NULL; }
	if (_uv)  { g2d_shm_free((uint32_t*)_uv);  _uv = NULL; }
	for (i = 0; i < PGL_VC3D_MAX_TEX; i++) {
		if (_tex[i].pixels) {
			g2d_shm_free(_tex[i].pixels);
			_tex[i].pixels = NULL;
		}
		_tex[i].shm = 0;
		_tex[i].phy = 0;
		_tex[i].w = _tex[i].h = 0;
	}
	_fb_shm = _z_shm = _pos_shm = _col_shm = _uv_shm = 0;
	_fb_phy = _z_phy = _pos_phy = _col_phy = _uv_phy = 0;
	_nverts = 0;
	_batch_tex_slot = -1;
}

static int alloc_target(int w, int h)
{
	uint32_t* px = NULL;
	uint32_t* zpx = NULL;
	ewokos_addr_t phy = 0, zphy = 0;
	int shm = 0, zshm = 0;

	if (w <= 0 || h <= 0)
		return 0;
	if (g2d_shm_alloc_phy((uint32_t)w * (uint32_t)h * 4u, &shm, &px, &phy) != 0
	    || shm <= 0 || px == NULL)
		return 0;
	/* persistent Z32F depth surface, same footprint as the colour target */
	if (g2d_shm_alloc_phy((uint32_t)w * (uint32_t)h * 4u, &zshm, &zpx, &zphy) != 0
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
	return 1;
}

int pgl_vc3d_resize(int w, int h)
{
	uint32_t* old = _fb;
	uint32_t* oldz = _z;

	if (!_enabled)
		return 0;
	if (w == _w && h == _h)
		return 1;

	pgl_vc3d_flush();

	_fb = NULL;   /* alloc_target repopulates; keep staging */
	_z = NULL;
	if (!alloc_target(w, h)) {
		_fb = old;
		_z = oldz;
		_enabled = 0;
		return 0;
	}
	if (old)
		g2d_shm_free(old);
	if (oldz)
		g2d_shm_free(oldz);
	_first_frame = 1;   /* a resized target starts a fresh frame */
	return 1;
}

int pgl_vc3d_init(int w, int h)
{
	uint32_t* pos_px = NULL;
	uint32_t* col_px = NULL;
	uint32_t* uv_px = NULL;
	ewokos_addr_t pos_phy = 0, col_phy = 0, uv_phy = 0;
	int pos_shm = 0, col_shm = 0, uv_shm = 0;

	if (_probed)
		return _enabled;
	_probed = 1;

	if (!has_g2d())
		return 0;

	if (!alloc_target(w, h))
		return 0;

	/* frame-boundary defaults so the very first dispatch clears even if a
	   frame is drawn before the first glClear/pgl_vc3d_begin_frame */
	_first_frame = 1;
	_clear_rgba = 0xFF000000u;
	_clear_z = 1.0f;

	if (g2d_shm_alloc_phy(PGL_VC3D_POS_BYTES, &pos_shm, &pos_px, &pos_phy) != 0
	    || pos_shm <= 0 || pos_px == NULL) {
		free_all();
		return 0;
	}
	if (g2d_shm_alloc_phy(PGL_VC3D_COL_BYTES, &col_shm, &col_px, &col_phy) != 0
	    || col_shm <= 0 || col_px == NULL) {
		g2d_shm_free(pos_px);
		free_all();
		return 0;
	}
	/* Phase D uv staging: allocated unconditionally so a TEX_MOD batch never
	   allocates on the hot path.  A failure here does NOT latch the GPU off;
	   the colour path stays fully usable and TEX_MOD declines to CPU. */
	if (g2d_shm_alloc_phy(PGL_VC3D_UV_BYTES, &uv_shm, &uv_px, &uv_phy) != 0
	    || uv_shm <= 0 || uv_px == NULL) {
		uv_px = NULL;
		uv_shm = 0;
		uv_phy = 0;
	}
	_pos_shm = pos_shm; _pos = (float*)pos_px; _pos_phy = pos_phy;
	_col_shm = col_shm; _col = col_px;         _col_phy = col_phy;
	_uv_shm  = uv_shm;  _uv  = (float*)uv_px;  _uv_phy  = uv_phy;
	_batch_tex_slot = -1;

	/* provisional: so pgl_vc3d_clear()'s flush path is live for the probe */
	_enabled = 1;
	if (!pgl_vc3d_clear(0xFF000000u)) {   /* opaque black probe clear */
		free_all();
		return 0;
	}
	return 1;
}

void pgl_vc3d_shutdown(void)
{
	if (_enabled)
		pgl_vc3d_flush();
	_enabled = 0;
	free_all();
	_probed = 0;   /* a fresh context may re-probe the GPU path */
}

#endif /* PGL_VC3D */
