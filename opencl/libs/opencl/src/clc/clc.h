/*
 * clc.h - the OpenCL-C subset front-end shared by the CPU interpreter and the
 * GPU transpiler.  It defines the type system, the AST (expressions,
 * statements, functions, structs) produced by clc_parse(), and the value model
 * the tree-walking interpreter evaluates in.
 *
 * Scope: the documented OpenCL-C subset in projects/opencl/README (scalar and
 * vector arithmetic, the four address spaces, control flow, structs, the
 * work-item builtins, barrier(), and the math/integer/common/geometric/
 * conversion builtin library).  Anything outside the subset is rejected by the
 * parser with a diagnostic in the build log (-> CL_BUILD_PROGRAM_FAILURE).
 *
 * Memory model (CPU device): every variable lives in real, typed host memory
 * (a stack frame byte buffer for locals, the cl_mem host store for __global
 * buffers, a per-work-group block for __local), so pointer arithmetic and
 * element loads/stores use the pointee's true size.  clc_value is used only
 * for expression temporaries and register-like results.
 */
#ifndef OPENCL_CLC_H
#define OPENCL_CLC_H

#include <CL/cl_platform.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLC_MAX_VEC     16
#define CLC_MAX_PARAMS  32
#define CLC_MAX_ARGS    16
#define CLC_MAX_FIELDS  32
#define CLC_MAX_SYMS    256
#define CLC_MAX_GLOBALS 64

/* ---- scalar base types --------------------------------------------------- */
typedef enum {
    CLC_VOID = 0, CLC_BOOL, CLC_CHAR, CLC_UCHAR, CLC_SHORT, CLC_USHORT,
    CLC_INT, CLC_UINT, CLC_LONG, CLC_ULONG, CLC_FLOAT, CLC_DOUBLE, CLC_HALF,
    CLC_SIZE_T, CLC_PTRDIFF_T, CLC_STRUCT,
    CLC_SAMPLER_T, CLC_IMAGE2D_T, CLC_IMAGE3D_T
} clc_base;

/* ---- address spaces ------------------------------------------------------ */
typedef enum {
    CLC_AS_PRIVATE = 0, CLC_AS_GLOBAL, CLC_AS_CONSTANT, CLC_AS_LOCAL
} clc_as;

typedef struct clc_struct clc_struct;

typedef struct clc_type {
    clc_base    base;
    int         vec;        /* 1 (scalar) or 2/3/4/8/16                    */
    int         pointer;    /* 0 = value, 1 = pointer                       */
    clc_as      as;         /* pointer address space (or value qualifier)   */
    clc_struct *str;        /* struct definition when base == CLC_STRUCT    */
} clc_type;

/* ---- struct definition --------------------------------------------------- */
typedef struct clc_field {
    char       *name;
    clc_type    type;
    int         offset;
    int         size;
} clc_field;

struct clc_struct {
    char       *name;               /* may be NULL (anonymous)              */
    clc_field   fields[CLC_MAX_FIELDS];
    int         nfields;
    int         size;
    int         align;
};

/* ---- expression kinds ---------------------------------------------------- */
typedef enum {
    E_CONST_INT, E_CONST_FLOAT, E_VAR, E_ASSIGN, E_BINOP, E_UNOP, E_POSTOP,
    E_CALL, E_INDEX, E_FIELD, E_SWIZZLE, E_CAST, E_TERNARY, E_COMPOUND
} clc_ekind;

/* ---- operators ----------------------------------------------------------- */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_LAND, OP_LOR, OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR,
    OP_ASSIGN, OP_ADD_ASG, OP_SUB_ASG, OP_MUL_ASG, OP_DIV_ASG, OP_MOD_ASG,
    OP_BAND_ASG, OP_BOR_ASG, OP_BXOR_ASG, OP_SHL_ASG, OP_SHR_ASG,
    OP_NEG, OP_LNOT, OP_BNOT, OP_DEREF, OP_ADDR,
    OP_PRE_INC, OP_PRE_DEC, OP_POST_INC, OP_POST_DEC
} clc_op;

typedef struct clc_expr clc_expr;
typedef struct clc_symbol clc_symbol;
typedef struct clc_function clc_function;
typedef struct clc_program clc_program;

struct clc_expr {
    clc_ekind   kind;
    clc_type    type;       /* result type (filled by the parser)           */
    clc_op      op;         /* E_BINOP / E_UNOP / E_POSTOP / E_ASSIGN       */

    clc_expr   *a, *b, *c;  /* operands: a=lhs/target, b=rhs, c=ternary-alt */

    long long   li;         /* E_CONST_INT literal                          */
    double      ld;         /* E_CONST_FLOAT literal                        */

    int         sym;        /* E_VAR: index into function symbol table      */
    int         gindex;     /* E_VAR file-scope global slot                 */
    int         is_global;  /* E_VAR resolves to a program global           */

    char       *name;       /* E_CALL name / E_FIELD field name             */
    clc_expr   *args[CLC_MAX_ARGS];
    int         nargs;
    int         is_builtin; /* E_CALL resolves to a builtin                 */
    int         builtin_id; /* resolved builtin id (see clc_builtins.h)     */
    clc_function *callee;   /* resolved user function (or NULL)             */

    int         field_index;/* E_FIELD struct field slot (-1 for swizzle)   */
    char        swizzle[4]; /* E_SWIZZLE component map                      */
    int         nswiz;

    clc_expr   *init[CLC_MAX_VEC]; /* E_COMPOUND literal components        */
    int         ninit;
};

/* ---- statement kinds ----------------------------------------------------- */
typedef enum {
    S_BLOCK, S_DECL, S_EXPR, S_IF, S_WHILE, S_DOWHILE, S_FOR,
    S_RETURN, S_BREAK, S_CONTINUE, S_SWITCH, S_CASE, S_DEFAULT
} clc_skind;

typedef struct clc_stmt clc_stmt;
struct clc_stmt {
    clc_skind   kind;

    /* S_DECL */
    clc_type    type;
    char       *name;
    clc_expr   *init;
    int         is_array;
    int         array_len;
    int         sym;            /* assigned symbol slot                     */

    /* S_BLOCK */
    clc_stmt  **body;
    int         nbody;

    /* S_EXPR / S_RETURN / S_CASE */
    clc_expr   *expr;

    /* S_IF / S_WHILE / S_DOWHILE / S_SWITCH */
    clc_expr   *cond;
    clc_stmt   *thenb, *elseb;

    /* S_FOR */
    clc_stmt   *sinit;
    clc_expr   *scond, *sinc;
    clc_stmt   *sbody;

    /* S_SWITCH body lives in thenb (a block of S_CASE/S_DEFAULT) */
};

/* ---- symbols (params + locals, resolved at parse time) ------------------- */
struct clc_symbol {
    char       *name;
    clc_type    type;
    int         offset;     /* byte offset in the function frame            */
    int         size;       /* bytes occupied                               */
    int         is_param;
    int         is_array;   /* aggregate stored in frame; value is pointer  */
    int         param_index;
};

/* ---- functions ----------------------------------------------------------- */
struct clc_function {
    char       *name;
    int         is_kernel;
    clc_type    ret;
    clc_symbol  syms[CLC_MAX_SYMS];
    int         nsyms;
    int         nparams;
    int         frame_size;
    int         local_mem_size;  /* statically-declared __local bytes        */
    clc_stmt   *body;            /* S_BLOCK                                  */
    char        attrs[64];       /* reqd_work_group_size etc. (informational)*/
    int         reqd_wg[3];      /* 0 when unspecified                       */
    int         has_reqd_wg;
};

/* ---- file-scope variable (allocated + constant-initialised at parse) ----- */
typedef struct clc_global {
    char       *name;
    clc_type    type;
    void       *data;       /* owned storage                                */
    int         size;
    int         is_array;
} clc_global;

/* ---- program (translation unit) ----------------------------------------- */
struct clc_program {
    clc_function **funcs;
    int            nfuncs;
    clc_struct   **structs;
    int            nstructs;
    clc_global    *globals;
    int            nglobals;
    /* constant expressions folded at parse time are baked into the AST */
};

/* ---- value model (interpreter temporaries) ------------------------------ */
typedef struct clc_value {
    clc_type    type;
    double      f[CLC_MAX_VEC];   /* floating lanes                         */
    long long   i[CLC_MAX_VEC];   /* integer lanes; pointers as uintptr_t   */
} clc_value;

/* ---- front-end API ------------------------------------------------------ */
clc_program *clc_parse(const char *source, char *errbuf, size_t errsz);
void         clc_program_free(clc_program *p);

int          clc_program_num_kernels(clc_program *p);
clc_function *clc_program_kernel(clc_program *p, int i);
clc_function *clc_program_find_kernel(clc_program *p, const char *name);
clc_function *clc_program_find_func(clc_program *p, const char *name);

/* type helpers */
int          clc_type_size(const clc_type *t);
int          clc_type_align(const clc_type *t);
int          clc_type_is_float(const clc_type *t);
int          clc_type_is_signed(const clc_type *t);
int          clc_type_is_pointer(const clc_type *t);
int          clc_type_is_vector(const clc_type *t);
const char  *clc_type_name(const clc_type *t);   /* canonical spelling       */

#ifdef __cplusplus
}
#endif

#endif /* OPENCL_CLC_H */
