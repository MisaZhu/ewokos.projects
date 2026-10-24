/*
 * clc_interp.h - the CPU interpreter's per-work-item execution context and
 * its entry point.  cpu_device.c fills a clc_exec_ctx for each work-item and
 * calls clc_run_kernel(); device-function calls recurse inside the interpreter
 * using the same context.
 */
#ifndef OPENCL_CLC_INTERP_H
#define OPENCL_CLC_INTERP_H

#include "clc.h"

typedef struct clc_exec_ctx {
    clc_program *prog;

    cl_uint  work_dim;
    size_t   global_offset[3];
    size_t   global_size[3];
    size_t   local_size[3];
    size_t   group_id[3];
    size_t   num_groups[3];
    size_t   global_id[3];
    size_t   local_id[3];

    void    *local_mem;      /* per-work-group __local block base            */

    char    *printf_buf;     /* shared printf sink (may be NULL)             */
    size_t   printf_cap;
    size_t  *printf_len;
} clc_exec_ctx;

/* Execute one work-item of kernel `fn`.  `params` holds nparams already-loaded
 * argument values (pointers as integer lane 0, scalars in their natural lane);
 * they are stored into the frame's typed param slots before the body runs.
 * Returns 0 on success, non-zero on a runtime fault (bad pointer, etc.). */
int clc_run_kernel(clc_function *fn, const clc_value *params, int nparams,
                   clc_exec_ctx *ctx);

#endif /* OPENCL_CLC_INTERP_H */
