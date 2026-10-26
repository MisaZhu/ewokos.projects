/*
 * clclient.c - libOpenCL's user-space IPC client to the g2dd daemon's OpenCL
 * compute offload path (see clclient.h).  A thin wrapper over the g2dclient
 * dev_cntl transport portablegl already uses: contig-shm buffers dispatch
 * directly, other buffers use persistent staging, and the request structs are
 * marshalled for the daemon.
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

/* Staging canvases persist across launches.  Buffers already backed by contig
 * shm bypass them entirely; other buffers keep host visibility through copies. */
typedef struct {
    int       shm;
    uint32_t *px;
    uint32_t  cap;
} cl_stage_t;

static cl_stage_t _compute_args[CLCLIENT_MAX_ARGS], _compute_unif;
static int _compute_direct_blocked;

static int ensure_stage(cl_stage_t *s, uint32_t need)
{
    if (s->px && s->cap >= need)
        return 1;
    if (s->px) {
        g2d_shm_free(s->px);
        s->px = NULL;
        s->cap = 0;
    }
    if (g2d_shm_alloc(need, &s->shm, &s->px) != 0 ||
        s->shm <= 0 || s->px == NULL) {
        s->px = NULL;
        s->cap = 0;
        return 0;
    }
    s->cap = need;
    return 1;
}

static void compute_abandon_staging(void)
{
    uint32_t i;

    /* A terminal job may still own these mappings.  Leak this set deliberately
     * and allocate fresh storage for any later launch. */
    for (i = 0; i < CLCLIENT_MAX_ARGS; i++) {
        _compute_args[i].shm = 0;
        _compute_args[i].px = NULL;
        _compute_args[i].cap = 0;
    }
    _compute_unif.shm = 0;
    _compute_unif.px = NULL;
    _compute_unif.cap = 0;
}

int32_t cl_kernel_dispatch(int32_t prog_handle,
                           const uint32_t wg[3], uint32_t wg_size,
                           void *const *arg_host, const uint32_t *arg_size,
                           const int *arg_shm, uint32_t num_args,
                           const uint32_t *unif, uint32_t num_unif)
{
    g2d_cl_dispatch_req_t req;
    uint32_t n, i;
    int direct_used = 0;
    int32_t rc = G2D_ERR_FAILED;

    if (prog_handle < 0 || wg == NULL || wg_size == 0 || wg_size > 256u)
        return G2D_ERR_FAILED;
    n = num_args > CLCLIENT_MAX_ARGS ? CLCLIENT_MAX_ARGS : num_args;

    g2d_cl_dispatch_req_init(&req, prog_handle, wg[0], wg[1], wg[2], wg_size);
    req.num_args = n;

    for (i = 0; i < n; i++) {
        uint32_t sz = arg_size ? arg_size[i] : 0u;
        uint32_t alloc;
        cl_stage_t *s = &_compute_args[i];

        if (sz == 0u || arg_host == NULL || arg_host[i] == NULL) {
            req.args[i] = g2d_canvas(0, 0, 0, 0, 0);
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
    clclient_quiesce(rc);
    if (rc == G2D_OK) {
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
