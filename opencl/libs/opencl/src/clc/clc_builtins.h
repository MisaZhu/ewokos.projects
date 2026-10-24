/*
 * clc_builtins.h - the OpenCL-C builtin library surface shared by the parser
 * (name -> id resolution) and the interpreter (id -> implementation).  Names
 * that begin with "convert_"/"as_" and the vload/vstore families are handled
 * structurally (the target type is carried on the call expression) rather than
 * enumerated per type.
 */
#ifndef OPENCL_CLC_BUILTINS_H
#define OPENCL_CLC_BUILTINS_H

typedef enum {
    BI_NONE = 0,

    /* work-item / synchronization */
    BI_GET_GLOBAL_ID, BI_GET_GLOBAL_SIZE, BI_GET_GLOBAL_OFFSET,
    BI_GET_LOCAL_ID, BI_GET_LOCAL_SIZE, BI_GET_GROUP_ID, BI_GET_NUM_GROUPS,
    BI_GET_WORK_DIM, BI_BARRIER,

    /* math (float) */
    BI_SQRT, BI_RSQRT, BI_FABS, BI_FLOOR, BI_CEIL, BI_ROUND, BI_TRUNC,
    BI_RINT, BI_FMIN, BI_FMAX, BI_POW, BI_POWN, BI_POWR, BI_EXP, BI_EXP2,
    BI_EXP10, BI_EXPM1, BI_LOG, BI_LOG2, BI_LOG10, BI_LOG1P, BI_SIN, BI_COS,
    BI_TAN, BI_ASIN, BI_ACOS, BI_ATAN, BI_ATAN2, BI_SINH, BI_COSH, BI_TANH,
    BI_ASINH, BI_ACOSH, BI_ATANH, BI_MAD, BI_FMA, BI_HYPOT, BI_CBRT,
    BI_DEGREES, BI_RADIANS, BI_SIGN, BI_COPYSIGN, BI_FDIM, BI_SINCOS,

    /* integer / common */
    BI_ABS, BI_ABS_DIFF, BI_MIN, BI_MAX, BI_CLAMP, BI_MIX, BI_STEP,
    BI_SMOOTHSTEP, BI_MUL_HI, BI_MAD_HI, BI_MAD24, BI_MUL24, BI_ROTATE,
    BI_ADD_SAT, BI_SUB_SAT, BI_CLZ, BI_POPCOUNT, BI_UPSAMPLE, BI_MAD_SAT,

    /* geometric */
    BI_DOT, BI_LENGTH, BI_DISTANCE, BI_NORMALIZE, BI_CROSS,
    BI_FAST_LENGTH, BI_FAST_DISTANCE, BI_FAST_NORMALIZE,

    /* relational */
    BI_ISEQUAL, BI_ISNOTEQUAL, BI_ISGREATER, BI_ISGREATEREQUAL, BI_ISLESS,
    BI_ISLESSEQUAL, BI_ISLESSGREATER, BI_ISFINITE, BI_ISINF, BI_ISNAN,
    BI_ISNORMAL, BI_ISORDERED, BI_ISUNORDERED, BI_SIGNBIT, BI_ANY, BI_ALL,
    BI_BITSELECT, BI_SELECT,

    /* structural (target type carried on expr->type) */
    BI_CONVERT, BI_AS, BI_VLOAD, BI_VSTORE,

    /* misc */
    BI_PRINTF
} clc_builtin;

/* Resolve a builtin name to its id, or BI_NONE if the name is not a builtin.
 * For the convert / as / vload / vstore families the base id is returned and the
 * caller decodes the concrete type/width from the name. */
int clc_builtin_lookup(const char *name);

#endif /* OPENCL_CLC_BUILTINS_H */
