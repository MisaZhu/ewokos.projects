/*
 * clc_builtins.c - the OpenCL-C builtin name -> id table used by the parser.
 * The convert / as / vload / vstore / printf families are recognised structurally
 * by the parser (they return BI_NONE here).  The implementations live in the
 * interpreter (clc_interp.c); this file is purely name resolution.
 */
#include "clc_builtins.h"
#include <string.h>

typedef struct { const char *name; int id; } bi_entry;

static const bi_entry g_builtins[] = {
    /* work-item / sync */
    { "get_global_id", BI_GET_GLOBAL_ID },
    { "get_global_size", BI_GET_GLOBAL_SIZE },
    { "get_global_offset", BI_GET_GLOBAL_OFFSET },
    { "get_local_id", BI_GET_LOCAL_ID },
    { "get_local_size", BI_GET_LOCAL_SIZE },
    { "get_group_id", BI_GET_GROUP_ID },
    { "get_num_groups", BI_GET_NUM_GROUPS },
    { "get_work_dim", BI_GET_WORK_DIM },
    { "barrier", BI_BARRIER },

    /* math */
    { "sqrt", BI_SQRT }, { "rsqrt", BI_RSQRT }, { "fabs", BI_FABS },
    { "floor", BI_FLOOR }, { "ceil", BI_CEIL }, { "round", BI_ROUND },
    { "trunc", BI_TRUNC }, { "rint", BI_RINT },
    { "fmin", BI_FMIN }, { "fmax", BI_FMAX },
    { "pow", BI_POW }, { "pown", BI_POWN }, { "powr", BI_POWR },
    { "exp", BI_EXP }, { "exp2", BI_EXP2 }, { "exp10", BI_EXP10 }, { "expm1", BI_EXPM1 },
    { "log", BI_LOG }, { "log2", BI_LOG2 }, { "log10", BI_LOG10 }, { "log1p", BI_LOG1P },
    { "sin", BI_SIN }, { "cos", BI_COS }, { "tan", BI_TAN },
    { "asin", BI_ASIN }, { "acos", BI_ACOS }, { "atan", BI_ATAN }, { "atan2", BI_ATAN2 },
    { "sinh", BI_SINH }, { "cosh", BI_COSH }, { "tanh", BI_TANH },
    { "asinh", BI_ASINH }, { "acosh", BI_ACOSH }, { "atanh", BI_ATANH },
    { "mad", BI_MAD }, { "fma", BI_FMA }, { "hypot", BI_HYPOT }, { "cbrt", BI_CBRT },
    { "degrees", BI_DEGREES }, { "radians", BI_RADIANS }, { "sign", BI_SIGN },
    { "copysign", BI_COPYSIGN }, { "fdim", BI_FDIM }, { "sincos", BI_SINCOS },

    /* integer / common */
    { "abs", BI_ABS }, { "abs_diff", BI_ABS_DIFF },
    { "min", BI_MIN }, { "max", BI_MAX }, { "clamp", BI_CLAMP }, { "mix", BI_MIX },
    { "step", BI_STEP }, { "smoothstep", BI_SMOOTHSTEP },
    { "mul_hi", BI_MUL_HI }, { "mad_hi", BI_MAD_HI }, { "mad24", BI_MAD24 },
    { "mul24", BI_MUL24 }, { "rotate", BI_ROTATE },
    { "add_sat", BI_ADD_SAT }, { "sub_sat", BI_SUB_SAT }, { "mad_sat", BI_MAD_SAT },
    { "clz", BI_CLZ }, { "popcount", BI_POPCOUNT }, { "upsample", BI_UPSAMPLE },

    /* geometric */
    { "dot", BI_DOT }, { "length", BI_LENGTH }, { "distance", BI_DISTANCE },
    { "normalize", BI_NORMALIZE }, { "cross", BI_CROSS },
    { "fast_length", BI_FAST_LENGTH }, { "fast_distance", BI_FAST_DISTANCE },
    { "fast_normalize", BI_FAST_NORMALIZE },

    /* relational */
    { "isequal", BI_ISEQUAL }, { "isnotequal", BI_ISNOTEQUAL },
    { "isgreater", BI_ISGREATER }, { "isgreaterequal", BI_ISGREATEREQUAL },
    { "isless", BI_ISLESS }, { "islessequal", BI_ISLESSEQUAL },
    { "islessgreater", BI_ISLESSGREATER },
    { "isfinite", BI_ISFINITE }, { "isinf", BI_ISINF }, { "isnan", BI_ISNAN },
    { "isnormal", BI_ISNORMAL }, { "isordered", BI_ISORDERED },
    { "isunordered", BI_ISUNORDERED }, { "signbit", BI_SIGNBIT },
    { "any", BI_ANY }, { "all", BI_ALL },
    { "bitselect", BI_BITSELECT }, { "select", BI_SELECT },

    { NULL, BI_NONE }
};

int clc_builtin_lookup(const char *name)
{
    int i;
    if (!name) return BI_NONE;
    for (i = 0; g_builtins[i].name; i++)
        if (!strcmp(g_builtins[i].name, name))
            return g_builtins[i].id;
    return BI_NONE;
}
