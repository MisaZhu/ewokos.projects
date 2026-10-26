/*
 * vkclient.c - libvulkan's user-space IPC client to the g2dd daemon's compute
 * offload path (see vkclient.h).  A thin wrapper over the g2dclient dev_cntl
 * transport OpenCL/portablegl already use: buffers are staged into
 * physically-contiguous shm canvases (g2d_shm_alloc hands out IPC_CONTIG, so the
 * V3D CSD addresses them by physical base with zero copy), the request structs
 * are marshalled, and results are copied back into the caller's host stores.
 *
 * Every function mirrors the proven clclient.c pattern value-for-value:
 *   vkclient_gpu_version  <- has_g2d + g2d_get_version + pglc_set_version gate
 *   vk_program_upload     <- g2d_cl_program (blob in a contig-shm canvas)
 *   vk_kernel_dispatch    <- g2d_cl_dispatch (per-SSBO contig-shm staging)
 *   vk_program_free       <- g2d_cl_program_free
 */
#include "vkclient.h"

#include <stdlib.h>
#include <string.h>

#include <g2dclient/g2dclient.h>
#include <pglcompiler.h>

/* Cached live GPU version code (0 = not queried yet).  The compiler runs in the
 * APP process, so its codegen target must be latched to the actual silicon
 * (pglc_set_version) BEFORE the first pglc_compile_*().  Idempotent: a success
 * is cached and never re-issued; a failure leaves the cache at 0 so the next
 * caller retries rather than silently compiling for the backend's default. */
static uint32_t _gpu_ver;

uint32_t vkclient_gpu_version(void)
{
    uint32_t v = 0;

    if (_gpu_ver != 0u)
        return _gpu_ver;

    /* has_g2d() uses the tree-wide 0=success convention: 0 when the g2d daemon
     * is reachable, -1 when it is not (no daemon -> no GPU compute at all). */
    if (has_g2d() != 0)
        return 0;
    if (g2d_get_version(&v) != G2D_OK || v == 0u)
        return 0;

    /* Latch the codegen target to the live silicon, then let the machine
     * compiler backend decide whether it can target that generation at all
     * (V3D 42/71; Adreno 690).  A generation the backend does not support
     * declines the GPU path entirely, exactly like clclient_gpu_version(). */
    pglc_set_version((int)v);
    if (!pglc_version_supported(v))
        return 0;

    _gpu_ver = v;
    return _gpu_ver;
}

int32_t vk_program_upload(const void *blob, uint32_t size)
{
    g2d_cl_program_req_t req;
    int shm = 0;
    uint32_t *px = NULL;
    int32_t handle;

    /* the blob is the g2d_pgl_program_blob_t wire format (compute stage only);
     * its header size is a multiple of 4, so a 4-aligned total is required for
     * the w = size/4 canvas modelling below to satisfy size >= w*h*4. */
    if (blob == NULL || size == 0 || (size & 3u) != 0)
        return G2D_ERR_FAILED;

    /* the daemon reads the blob as a raw byte buffer (it deep-copies the QPU
     * into GPU dma), so a client-side physical base is not needed - contig=1
     * lets the daemon resolve phy on attach exactly like the pgl upload. */
    if (g2d_shm_alloc(size, &shm, &px) != 0 || shm <= 0 || px == NULL)
        return G2D_ERR_FAILED;
    memcpy(px, blob, size);

    g2d_cl_program_req_init(&req, g2d_canvas(shm, size, size / 4u, 1u, 1));
    handle = g2d_cl_program(&req);   /* >= 0 handle, or a negative G2D_ERR_* */
    g2d_shm_free(px);
    return handle;
}

/* Staged compute canvases persist across submissions.  Their contents are still
 * copied in both directions on every dispatch, so host writes remain visible,
 * but the steady-state path does not repeat allocation/map/free.  Zero-offset
 * buffers already backed by contig shm bypass these canvases entirely. */
typedef struct {
    int       shm;
    uint32_t *px;
    uint32_t  cap;
} vk_stage_t;

static vk_stage_t _compute_args[VKCLIENT_MAX_ARGS], _compute_unif;
static int _compute_direct_blocked;
static int ensure_stage(vk_stage_t *s, uint32_t need);

static void compute_abandon_staging(void)
{
    uint32_t i;

    /* A terminal job may still own its GPU-visible buffers.  Leak this staging
     * set until process exit and force later submissions onto fresh memory. */
    for (i = 0; i < VKCLIENT_MAX_ARGS; i++) {
        _compute_args[i].px = NULL;
        _compute_args[i].shm = 0;
        _compute_args[i].cap = 0;
    }
    _compute_unif.px = NULL;
    _compute_unif.shm = 0;
    _compute_unif.cap = 0;
}

int32_t vk_kernel_dispatch(int32_t prog_handle,
                           const uint32_t wg[3], uint32_t wg_size,
                           void *const *arg_host, const uint32_t *arg_size,
                           const int *arg_shm, uint32_t num_args,
                           const uint32_t *unif, uint32_t num_unif)
{
    g2d_cl_dispatch_req_t req;
    uint32_t n, i;
    int direct_used = 0;
    int32_t  rc = G2D_ERR_FAILED;

    if (prog_handle < 0 || wg == NULL || wg_size == 0 || wg_size > 256u)
        return G2D_ERR_FAILED;
    n = num_args > VKCLIENT_MAX_ARGS ? VKCLIENT_MAX_ARGS : num_args;

    g2d_cl_dispatch_req_init(&req, prog_handle, wg[0], wg[1], wg[2], wg_size);
    req.num_args = n;

    /* Stage each storage-buffer host store into its own contig-shm canvas and
     * copy the input in.  The canvas is modelled w = ceil(size/4), h = 1 so the
     * driver's size >= w*h*4 attach check holds and the daemon resolves the
     * physical base (contig=1, phy=0) for the CSD's SSBO offset. */
    for (i = 0; i < n; i++) {
        uint32_t sz = arg_size ? arg_size[i] : 0u;
        uint32_t alloc;
        vk_stage_t *s = &_compute_args[i];
        if (sz == 0u || arg_host == NULL || arg_host[i] == NULL) {
            req.args[i] = g2d_canvas(0, 0, 0, 0, 0);   /* unused slot */
            continue;
        }
        alloc = (sz + 3u) & ~3u;
        if (arg_shm && arg_shm[i] > 0) {
            if (_compute_direct_blocked)
                return rc;
            req.args[i] = g2d_canvas(arg_shm[i], alloc, alloc / 4u, 1u, 1);
            direct_used = 1;
            continue;
        }
        if (!ensure_stage(s, alloc))
            return rc;
        memcpy(s->px, arg_host[i], sz);
        req.args[i] = g2d_canvas(s->shm, alloc, alloc / 4u, 1u, 1);
    }

    /* The scalar uniform blob: num_unif u32 dwords modelled w = num_unif, h = 1
     * (matching the daemon's unif.size >= n*4 && unif.w == n check); the daemon
     * reads it by vaddr for the QUNIFORM_UNIFORM slots. */
    if (num_unif > 0u && unif != NULL) {
        uint32_t ub = num_unif * 4u;
        if (!ensure_stage(&_compute_unif, ub))
            return rc;
        memcpy(_compute_unif.px, unif, ub);
        req.num_uniforms = num_unif;
        req.unif = g2d_canvas(_compute_unif.shm, ub, num_unif, 1u, 1);
    } else {
        req.num_uniforms = 0;
        req.unif = g2d_canvas(0, 0, 0, 0, 0);
    }

    rc = g2d_cl_dispatch(&req);
    vkclient_quiesce(rc);
    if (rc == G2D_OK) {
        /* the GPU wrote the output buffers by physical base; copy each staged
         * canvas back into the caller's host store so the writes land there. */
        for (i = 0; i < n; i++) {
            uint32_t sz = arg_size ? arg_size[i] : 0u;
            if (sz != 0u && (!arg_shm || arg_shm[i] <= 0) &&
                _compute_args[i].px != NULL && arg_host && arg_host[i])
                memcpy(arg_host[i], _compute_args[i].px, sz);
        }
    } else if (rc == G2D_ERR_TIMEOUT || rc == G2D_ERR_MMU_FAULT) {
        compute_abandon_staging();
        if (direct_used)
            _compute_direct_blocked = 1;
    }
    return rc;
}

void vkclient_quiesce(int32_t rc)
{
    g2d_quiesce_on_fault(rc);
}

int32_t vk_program_free(int32_t prog_handle)
{
    g2d_cl_program_free_req_t req;

    if (prog_handle < 0)
        return G2D_ERR_FAILED;
    g2d_cl_program_free_req_init(&req, prog_handle);
    return g2d_cl_program_free(&req);
}

/* --- Graphics (two-pass geom, geom_kind == 5) offload ----------------------
 * Same g2dclient transport as the CL verbs above, but through the PGL graphics
 * commands (G2D_DEV_CNTL_PGL_PROGRAM / _PGL_DRAW) so the daemon dispatches the
 * two-pass geometry rasterizer instead of a bare compute launch.  A persistent
 * staging cache (fb / z / attr / unif / per-unit tiled texture canvases) is kept
 * between draws and grown on demand, so a steady-state render pass reuses the
 * same physically-contiguous shm segments the CSD addresses by physical base.
 *
 * The daemon resolves each canvas' physical base from its shm id on attach
 * (g2d_attach -> shm_contig_phy_addr), so - exactly like vk_program_upload /
 * vk_kernel_dispatch - these canvases are allocated with g2d_shm_alloc (contig,
 * phy left 0) rather than g2d_shm_alloc_phy. */

static vk_stage_t _fb, _z, _attr, _unif;
static vk_stage_t _tex[VKCLIENT_MAX_TEX];

/* Ensure `s` holds at least `need` bytes, growing with headroom so a render
 * target that resizes by a few pixels does not realloc every frame.  Returns 1
 * on success (s->px valid, s->cap >= need) or 0 on allocation failure (s reset).
 * A segment already large enough is reused untouched. */
static int ensure_stage(vk_stage_t *s, uint32_t need)
{
    uint32_t alloc;

    if (need == 0u)
        return 0;
    if (s->px != NULL && s->cap >= need)
        return 1;
    if (s->px != NULL) {
        g2d_shm_free(s->px);
        s->px = NULL;
        s->shm = 0;
        s->cap = 0;
    }
    alloc = need + need / 2u;
    alloc = (alloc + 63u) & ~63u;   /* 64-byte align: keeps phy GPU-aligned */
    if (g2d_shm_alloc(alloc, &s->shm, &s->px) != 0 ||
        s->shm <= 0 || s->px == NULL) {
        s->px = NULL;
        s->shm = 0;
        s->cap = 0;
        return 0;
    }
    s->cap = alloc;
    return 1;
}

/* Abandon the whole persistent staging cache after a terminal result: a timed-out
 * / faulted job may still own its GPU-visible buffers, so leak them (freed at
 * process exit) and reset the cache so the next draw allocates fresh segments.
 * Mirrors vk_kernel_dispatch's terminal-path policy. */
static void gfx_abandon_staging(void)
{
    uint32_t i;

    _fb.px = NULL;   _fb.shm = 0;   _fb.cap = 0;
    _z.px = NULL;    _z.shm = 0;    _z.cap = 0;
    _attr.px = NULL; _attr.shm = 0; _attr.cap = 0;
    _unif.px = NULL; _unif.shm = 0; _unif.cap = 0;
    for (i = 0; i < VKCLIENT_MAX_TEX; i++) {
        _tex[i].px = NULL;
        _tex[i].shm = 0;
        _tex[i].cap = 0;
    }
}

/* Copy between a tightly packed GPU canvas (pitch = w*4) and a possibly pitched
 * host attachment (pitch = host_stride), row by row. */
static void gfx_copy_rows(void *dst, uint32_t dst_pitch,
                          const void *src, uint32_t src_pitch,
                          uint32_t row_bytes, uint32_t nrows)
{
    uint32_t y;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (dst_pitch == src_pitch && dst_pitch == row_bytes) {
        memcpy(d, s, (size_t)row_bytes * nrows);
        return;
    }
    for (y = 0; y < nrows; y++)
        memcpy(d + (size_t)y * dst_pitch, s + (size_t)y * src_pitch, row_bytes);
}

int32_t vk_gfx_program_upload(const void *blob, uint32_t size)
{
    g2d_pgl_program_req_t req;
    int shm = 0;
    uint32_t *px = NULL;
    int32_t handle;

    /* identical marshalling to vk_program_upload, but the geom blob (both
     * compute stages + cs_is_geom == 5) goes through PGL_PROGRAM so the daemon
     * caches it as a graphics program and dispatches the two-pass rasterizer. */
    if (blob == NULL || size == 0 || (size & 3u) != 0)
        return G2D_ERR_FAILED;
    if (g2d_shm_alloc(size, &shm, &px) != 0 || shm <= 0 || px == NULL)
        return G2D_ERR_FAILED;
    memcpy(px, blob, size);

    g2d_pgl_program_req_init(&req, g2d_canvas(shm, size, size / 4u, 1u, 1));
    handle = g2d_pgl_program(&req);   /* >= 0 handle, or a negative G2D_ERR_* */
    g2d_shm_free(px);
    return handle;
}

int32_t vk_gfx_draw(int32_t prog_handle,
                    void *fb_host, uint32_t fb_w, uint32_t fb_h, uint32_t fb_stride,
                    int fb_shm,
                    const void *attr_host, uint32_t nverts, uint32_t attr_stride,
                    const uint32_t *unif, uint32_t num_unif,
                    int first_frame, uint32_t clear_rgba, float clear_z,
                    const vk_gfx_tex *tex, uint32_t ntex)
{
    g2d_pgl_req_t req;
    uint32_t host_pitch, fb_bytes, attr_bytes, unif_bytes;
    uint32_t i, ntex_bound;
    int32_t  rc;
    int direct_fb;

    if (prog_handle < 0 || fb_host == NULL || fb_w == 0 || fb_h == 0)
        return G2D_ERR_FAILED;
    if (attr_host == NULL || attr_stride == 0)
        return G2D_ERR_FAILED;
    if (nverts == 0 || (nverts % 3u) != 0 || nverts > G2D_PGL_MAX_VERTS)
        return G2D_ERR_FAILED;

    host_pitch = fb_stride ? fb_stride : fb_w * 4u;
    if (host_pitch < fb_w * 4u)
        return G2D_ERR_FAILED;
    fb_bytes   = fb_w * fb_h * 4u;          /* tight GPU framebuffer canvas */
    attr_bytes = nverts * attr_stride * 4u;
    unif_bytes = num_unif * 4u;
    direct_fb = (fb_shm > 0 && host_pitch == fb_w * 4u);

    /* A shared swapchain image is itself the GPU destination.  Other images
     * retain the persistent staging framebuffer and its host readback. */
    if ((!direct_fb && !ensure_stage(&_fb, fb_bytes)) ||
        !ensure_stage(&_z,  fb_bytes) ||
        !ensure_stage(&_attr, attr_bytes))
        return G2D_ERR_FAILED;
    if (num_unif > 0u && unif != NULL) {
        if (!ensure_stage(&_unif, unif_bytes))
            return G2D_ERR_FAILED;
    }

    /* Load the framebuffer canvas: on the frame's first dispatch the daemon
     * GPU-clears fb + depth to clear_rgba/clear_z, so the host pixels are not
     * needed; otherwise stage the current attachment contents (a continuing draw
     * within a render pass) and let the depth surface persist untouched. */
    if (!first_frame && !direct_fb)
        gfx_copy_rows(_fb.px, fb_w * 4u, fb_host, host_pitch,
                      fb_w * 4u, fb_h);
    memcpy(_attr.px, attr_host, attr_bytes);
    if (num_unif > 0u && unif != NULL)
        memcpy(_unif.px, unif, unif_bytes);

    /* Tile each bound combined-image-sampler into its persistent canvas.  V3D's
     * TMU cannot sample linear raster memory (pglc_tex_sampler_reads_raster() ==
     * false), so the RGBA8 host image is converted to the backend's tiled level-0
     * layout every draw (correctness first; per-image change detection is a later
     * optimisation). */
    ntex_bound = 0;
    if (tex != NULL) {
        for (i = 0; i < ntex && i < VKCLIENT_MAX_TEX; i++) {
            struct pglc_tex_layout layout;
            if (tex[i].host == NULL || tex[i].w == 0 || tex[i].h == 0)
                break;   /* an unbound slot ends the contiguous binding run */
            if (!pglc_tex_layout_2d(tex[i].w, tex[i].h, 4u, &layout) ||
                layout.size == 0)
                return G2D_ERR_FAILED;
            if (!ensure_stage(&_tex[i], layout.size))
                return G2D_ERR_FAILED;
            pglc_tex_store_2d(_tex[i].px, tex[i].host, tex[i].w * 4u,
                              tex[i].w, tex[i].h, 4u, &layout);
            ntex_bound = i + 1;
        }
    }

    /* Marshal the draw.  The attribute stream rides the pos canvas slot, modelled
     * w = nverts, h = attr_stride (so w*h*4 == nverts*attr_stride*4 == size and
     * the daemon's pos.w == nverts check holds); col is unused on the compiled
     * geom path (zero canvas); z is the persistent depth surface the raster pass
     * arbitrates with atomicMin, always present for geom.  depth_func / cull /
     * blend in the request are left NONE: for geom_kind == 5 the real state lives
     * in the kernel uniforms (uDepthFunc / uCullSign / uBlendMode), and these
     * zero values keep vc_pgl3d's fixed-function eligibility gates satisfied. */
    {
        g2d_canvas_t dst  = direct_fb
            ? g2d_canvas(fb_shm,  fb_bytes, fb_w, fb_h, 1)
            : g2d_canvas(_fb.shm, fb_bytes, fb_w, fb_h, 1);
        g2d_canvas_t pos  = g2d_canvas(_attr.shm, attr_bytes, nverts, attr_stride, 1);
        g2d_canvas_t col  = g2d_canvas(0, 0, 0, 0, 0);
        g2d_canvas_t zc   = g2d_canvas(_z.shm,    fb_bytes,   fb_w, fb_h, 1);

        g2d_pgl_draw_req_init(&req, dst, pos, col,
                              G2D_PGL_MODE_TRIANGLES, nverts,
                              G2D_PGL_FMT_RGBA8, fb_w * 4u,
                              G2D_PGL_FS_SOLID, 0u, 0u,
                              G2D_PGL_DEPTH_NONE, 0u, G2D_PGL_CULL_NONE,
                              zc, fb_w * 4u, (uint8_t)(first_frame ? 1 : 0),
                              clear_rgba, clear_z);
        g2d_pgl_draw_req_set_shader(&req, prog_handle,
                                    (num_unif > 0u) ? _unif.shm : 0,
                                    num_unif, 1);

        for (i = 0; i < ntex_bound; i++) {
            struct pglc_tex_layout layout;
            /* recompute the layout for the canvas geometry (cheap, no re-alloc);
             * ensure_stage above already sized _tex[i] to layout.size. */
            if (!pglc_tex_layout_2d(tex[i].w, tex[i].h, 4u, &layout))
                continue;
            req.ctex[i] = g2d_canvas(_tex[i].shm, layout.size,
                                     tex[i].w, tex[i].h, 1);
            req.ctex_w[i]      = tex[i].w;
            req.ctex_h[i]      = tex[i].h;
            req.ctex_stride[i] = layout.stride;
            req.ctex_tiling[i] = (uint8_t)layout.tiling;
            req.ctex_ub_pad[i] = G2D_PGL_CTEX_UB_PAD_PACK(layout.ub_pad,
                tex[i].linear ? G2D_PGL_CTEX_FILTER_LINEAR
                              : G2D_PGL_CTEX_FILTER_NEAREST);
        }
    }

    rc = g2d_pgl_draw(&req);
    vkclient_quiesce(rc);
    if (rc == G2D_OK) {
        /* A direct swapchain target already holds the finished pixels. */
        if (!direct_fb)
            gfx_copy_rows(fb_host, host_pitch, _fb.px, fb_w * 4u,
                          fb_w * 4u, fb_h);
    } else if (rc == G2D_ERR_TIMEOUT || rc == G2D_ERR_MMU_FAULT) {
        gfx_abandon_staging();
    }
    return rc;
}
