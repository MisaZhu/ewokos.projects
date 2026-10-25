/*
 * clc_to_glsl.h - the OpenCL-C -> GLSL-ES-3.10 compute transpiler used by the
 * GPU device backend.  It converts one parsed kernel (a clc_function from the
 * shared clc front-end) into a verbatim compute-shader source string that the
 * on-device mesa compiler accepts through pglc_compile_compute_raw().
 *
 * The GPU subset is deliberately narrower than the CPU interpreter's: the V3D
 * CSD bakes the work-group shape at compile time, has only four SSBO binding
 * points, addresses buffers by 32-bit physical base, and offers float/int/uint
 * scalars and 2/3/4-vectors only.  A kernel that uses anything outside that
 * subset (double/long/char/short, 8/16-vectors, structs, __local, barrier,
 * work-group builtins, >4 buffer args, printf, an exotic conversion) is DECLINED
 * here -- clc_to_glsl returns NULL with a reason -- and gpu_device.c then runs
 * it transparently on the CPU device.  Declining is always safe: it can only
 * cost speed, never correctness.
 *
 * Global-id model: the transpiler bakes local_size = (LOCAL_SIZE_X, 1, 1) and
 * gpu_device.c dispatches ceil(gsz0/LOCAL_SIZE_X) x gsz1 x gsz2 work-groups of
 * LOCAL_SIZE_X items.  Under that shape gl_GlobalInvocationID.x sweeps dim0 (a
 * partial tail group is clipped by an emitted guard) and .y/.z map 1:1 onto
 * dim1/dim2, so get_global_id(d) is exact for every d.  The global offset/size
 * the host requested travel as int uniforms u_goff{0,1,2}/u_gsz{0,1,2}, packed
 * by name through the compiler's uniform reflection at dispatch time.
 */
#ifndef OPENCL_CLC_TO_GLSL_H
#define OPENCL_CLC_TO_GLSL_H

#include "clc/clc.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a kernel parameter is realised in the emitted GLSL. */
#define CLC_GLSL_ARG_UNUSED   0   /* not mappable to an SSBO or a uniform   */
#define CLC_GLSL_ARG_SSBO     1   /* __global/__constant T* -> SSBO binding  */
#define CLC_GLSL_ARG_UNIFORM  2   /* scalar by value -> default-block uniform*/

/* V3D 7.1 compute exposes exactly four SSBO binding points (see the
 * geom_raster_cs.h header note: "fb/vout/depth/tex in the four V3D 7.1 compute
 * binding points").  A kernel with more __global buffer args is declined. */
#define CLC_GLSL_MAX_SSBO     4

/* Baked work-group shape.  64 lanes keeps a tail group small while staying well
 * under the CSD's 256-item limit; only get_global_id() is exact under it, so a
 * kernel touching work-group builtins is declined to the CPU. */
#define CLC_GLSL_LOCAL_SIZE_X 64

/* One kernel parameter's GPU realisation, filled by clc_to_glsl(). */
typedef struct clc_glsl_arg {
    int       kind;                 /* CLC_GLSL_ARG_*                        */
    int       binding;              /* SSBO binding index (kind == SSBO)     */
    char      uname[64];            /* uniform name (kind == UNIFORM)        */
    clc_type  type;                 /* the OpenCL-C parameter type           */
} clc_glsl_arg;

/* Everything gpu_device.c needs to pack a dispatch for a transpiled kernel. */
typedef struct clc_glsl_info {
    int            local_size_x;    /* == CLC_GLSL_LOCAL_SIZE_X              */
    int            nargs;           /* kernel parameter count                */
    int            max_binding;     /* highest SSBO binding used (-1 = none) */
    clc_glsl_arg   args[CLC_MAX_PARAMS];
} clc_glsl_info;

/* Transpile `kernel` (a function of `prog`) to a GLSL ES 3.10 compute source.
 * On success returns a malloc()ed NUL-terminated string the caller frees and
 * fills *info; on a declined kernel returns NULL and writes a short reason into
 * err (NUL-terminated, at most errsz-1 chars). */
char *clc_to_glsl(clc_program *prog, clc_function *kernel,
                  clc_glsl_info *info, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif /* OPENCL_CLC_TO_GLSL_H */
