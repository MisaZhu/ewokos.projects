/*
 * clclient.c - libOpenCL's user-space IPC client to the g2dd daemon's OpenCL
 * compute offload path (see clclient.h).  A thin wrapper over the g2dclient
 * dev_cntl transport portablegl already uses: buffers are staged into
 * physically-contiguous shm canvases (g2d_shm_alloc hands out IPC_CONTIG, so the
 * V3D CSD addresses them by physical base with zero copy), the request structs
 * are marshalled, and results are copied back into the caller's host stores.
 *
 * Every function mirrors the proven portablegl pattern:
 *   clclient_gpu_version  <- pgl_g3d_ensure_version + pgl_g3d_init's gate
 *   cl_program_upload     <- pgl_g3d_shader.c program_upload
 *   cl_kernel_dispatch    <- the pgl pos/col/unif contig-shm staging model
 */
#include "clclient.h"

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

uint32_t clclient_gpu_version(void)
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
     * declines the GPU path entirely, exactly like pgl_g3d_init(). */
    pglc_set_version((int)v);
    if (!pglc_version_supported(v))
        return 0;

    _gpu_ver = v;
    return _gpu_ver;
}

int32_t cl_program_upload(const void *blob, uint32_t size)
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

int32_t cl_kernel_dispatch(int32_t prog_handle,
                           const uint32_t wg[3], uint32_t wg_size,
                           void *const *arg_host, const uint32_t *arg_size,
                           uint32_t num_args,
                           const uint32_t *unif, uint32_t num_unif)
{
    g2d_cl_dispatch_req_t req;
    int      shm[CLCLIENT_MAX_ARGS];
    uint32_t *px[CLCLIENT_MAX_ARGS];
    int      unshm = 0;
    uint32_t *unpx = NULL;
    uint32_t n, i;
    int32_t  rc = G2D_ERR_FAILED;

    if (prog_handle < 0 || wg == NULL || wg_size == 0 || wg_size > 256u)
        return G2D_ERR_FAILED;
    n = num_args > CLCLIENT_MAX_ARGS ? CLCLIENT_MAX_ARGS : num_args;

    memset(shm, 0, sizeof(shm));
    memset(px, 0, sizeof(px));

    g2d_cl_dispatch_req_init(&req, prog_handle, wg[0], wg[1], wg[2], wg_size);
    req.num_args = n;

    /* Stage each __global buffer host store into its own contig-shm canvas and
     * copy the input in.  The canvas is modelled w = ceil(size/4), h = 1 so the
     * driver's size >= w*h*4 attach check holds and the daemon resolves the
     * physical base (contig=1, phy=0) for the CSD's SSBO offset. */
    for (i = 0; i < n; i++) {
        uint32_t sz = arg_size ? arg_size[i] : 0u;
        uint32_t alloc;
        if (sz == 0u || arg_host == NULL || arg_host[i] == NULL) {
            req.args[i] = g2d_canvas(0, 0, 0, 0, 0);   /* unused slot */
            continue;
        }
        alloc = (sz + 3u) & ~3u;
        if (g2d_shm_alloc(alloc, &shm[i], &px[i]) != 0 ||
            shm[i] <= 0 || px[i] == NULL) {
            px[i] = NULL;
            goto done;
        }
        memcpy(px[i], arg_host[i], sz);
        req.args[i] = g2d_canvas(shm[i], alloc, alloc / 4u, 1u, 1);
    }

    /* The scalar + __constant uniform blob: num_unif u32 dwords modelled
     * w = num_unif, h = 1 (matching the daemon's unif.size >= n*4 && unif.w == n
     * check); the daemon reads it by vaddr for the QUNIFORM_UNIFORM slots. */
    if (num_unif > 0u && unif != NULL) {
        uint32_t ub = num_unif * 4u;
        if (g2d_shm_alloc(ub, &unshm, &unpx) != 0 || unshm <= 0 || unpx == NULL) {
            unpx = NULL;
            goto done;
        }
        memcpy(unpx, unif, ub);
        req.num_uniforms = num_unif;
        req.unif = g2d_canvas(unshm, ub, num_unif, 1u, 1);
    } else {
        req.num_uniforms = 0;
        req.unif = g2d_canvas(0, 0, 0, 0, 0);
    }

    rc = g2d_cl_dispatch(&req);
    clclient_quiesce(rc);
    if (rc == G2D_OK) {
        /* the GPU wrote the output buffers by physical base; copy each staged
         * canvas back into the caller's host store so the writes land there. */
        for (i = 0; i < n; i++) {
            uint32_t sz = arg_size ? arg_size[i] : 0u;
            if (sz != 0u && px[i] != NULL && arg_host && arg_host[i])
                memcpy(arg_host[i], px[i], sz);
        }
    }

done:
    /* A terminal job may still own its GPU-visible buffers.  Keep this one
       staging set mapped until process exit; later submissions use new staging
       and the CPU replay uses the caller's host buffers. */
    if (rc != G2D_ERR_TIMEOUT && rc != G2D_ERR_MMU_FAULT) {
        for (i = 0; i < n; i++)
            if (px[i])
                g2d_shm_free(px[i]);
        if (unpx)
            g2d_shm_free(unpx);
    }
    return rc;
}

void clclient_quiesce(int32_t rc)
{
    g2d_quiesce_on_fault(rc);
}

int32_t cl_program_free(int32_t prog_handle)
{
    g2d_cl_program_free_req_t req;

    if (prog_handle < 0)
        return G2D_ERR_FAILED;
    g2d_cl_program_free_req_init(&req, prog_handle);
    return g2d_cl_program_free(&req);
}
