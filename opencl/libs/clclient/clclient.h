/*
 * clclient.h - libOpenCL's user-space IPC client to the g2dd daemon's OpenCL
 * compute offload path (G2D_DEV_CNTL_CL_*).  This is a thin wrapper over the
 * same g2dclient dev_cntl transport portablegl uses: it dispatches contig-shm
 * buffers directly, stages other buffers, and marshals the request structs.
 *
 * It is built as a PRIVATE intermediate (libclclient.a) and folded into
 * libOpenCL.a by libs/opencl/Makefile, so an application links only -lOpenCL and
 * never sees this header, g2dclient, or the pglcompiler.  Only the src/gpu
 * sources include it.
 */
#ifndef OPENCL_CLCLIENT_H
#define OPENCL_CLCLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Largest number of __global buffer args one dispatch carries as SSBO bindings
 * (must match G2D_CL_MAX_ARGS in g2dclient.h). */
#define CLCLIENT_MAX_ARGS 16u

/* Result codes returned by the cl_* entry points below.  They mirror the
 * g2dclient G2D_* codes value-for-value so gpu_device.c can classify a result
 * without including g2dclient.h (clclient is the abstraction boundary). */
#define CLCLIENT_OK              0    /* the GPU ran the op                  */
#define CLCLIENT_ERR_FAILED     (-1)  /* transient: a CPU replay is safe     */
#define CLCLIENT_ERR_UNSUPPORTED (-2) /* unsupported launch: use CPU fallback */
#define CLCLIENT_ERR_TIMEOUT    (-3)  /* terminal: quiesce, then CPU replay  */
#define CLCLIENT_ERR_MMU_FAULT  (-4)  /* terminal: quiesce, then CPU replay  */

/* Quiesce the GPU after a terminal result (CLCLIENT_ERR_TIMEOUT / _MMU_FAULT):
 * submitted work may still own its buffers, so this drains it before the caller
 * touches them again (e.g. to replay the launch on the CPU).  A no-op for every
 * other code.  Wraps g2d_quiesce_on_fault. */
void clclient_quiesce(int32_t rc);

/* Probe whether a g2d daemon with a GPU compute path is reachable and latch the
 * on-device compiler's codegen target to the live silicon (pglc_set_version).
 * Returns the backend-neutral GPU version code (e.g. 71 = Pi5 V3D 7.1) when a
 * usable compute GPU is present, or 0 when there is none (no daemon, no GPU, or
 * a generation the compiler backend does not support).  Idempotent. */
uint32_t clclient_gpu_version(void);

/* Upload a serialized compute blob (the g2d_pgl_program_blob_t wire format with
 * only the compute stage populated, as produced by pglc_program_serialize).
 * Returns a non-negative program handle, or a negative G2D_ERR_* code
 * (G2D_ERR_NOT_SUPPORTED == -2 when the platform declines compute, which latches
 * libOpenCL's GPU device off). */
int32_t cl_program_upload(const void *blob, uint32_t size);

/* Run prog_handle over a wg[0] x wg[1] x wg[2] grid of work-groups, each of
 * wg_size work-items (1..256).  arg_host[i]/arg_size[i] are the __global buffer
 * stores in SSBO binding order.  A positive arg_shm[i] identifies a zero-offset
 * contig-shm backing and dispatches it directly without upload/readback; other
 * buffers use persistent staging canvases.  A NULL arg_host[i] (or zero size)
 * marks an unused trailing slot.  unif/num_unif is the packed scalar +
 * __constant uniform blob (num_unif u32 dwords; may be 0/NULL).
 * Returns G2D_OK (0) or a negative G2D_ERR_* code. */
int32_t cl_kernel_dispatch(int32_t prog_handle,
                           const uint32_t wg[3], uint32_t wg_size,
                           void *const *arg_host, const uint32_t *arg_size,
                           const int *arg_shm, uint32_t num_args,
                           const uint32_t *unif, uint32_t num_unif);

/* Release a program handle and the GPU dma it owns.  Returns G2D_OK (0) or a
 * negative G2D_ERR_* code. */
int32_t cl_program_free(int32_t prog_handle);

#ifdef __cplusplus
}
#endif

#endif /* OPENCL_CLCLIENT_H */
