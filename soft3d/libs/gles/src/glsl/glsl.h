/* glsl.h - internal interface of the self-contained CPU GLSL ES 1.00
 * interpreter used by libGLESv2 (Phase 5).
 *
 * This mini-library is deliberately portablegl-FREE: it only lexes, parses and
 * tree-walks GLSL ES 1.00 and exposes run_* entry points plus reflection.  The
 * thin bridge that turns a glsl_program_t into portablegl vert_func/frag_func
 * trampolines lives in src/engine/pgl_glsl_bridge.c (the second TU allowed to
 * include portablegl.h) and injects a texture-sampling callback, because the
 * actual sampler is portablegl's texture2D().
 *
 * Design notes:
 *  - Values are stored in a fixed 16-float payload (mat4 = 16, vec4 = 4) plus a
 *    4-int payload for int/bool vectors.  Column-major matrices, matching GLSL.
 *  - A shader stage is compiled to an AST; linking resolves attribute/varying/
 *    uniform names to numeric slots so the hot run path is a tight tree-walk
 *    with no string compares.
 *  - Only the GLSL ES 1.00 subset the soft3d consumers use is supported: the
 *    scalar/vector/matrix types, swizzles, constructors, the standard built-in
 *    function set, if/for/while/do/break/continue/return/discard, user
 *    functions, arrays (1-D, constant index), and struct field select.
 */
#ifndef GLES_GLSL_H
#define GLES_GLSL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Limits (sized for the soft3d shaders; keep modest for embedded RAM)
 * ------------------------------------------------------------------ */
#define GLSL_MAX_VARYINGS      16   /* vec4 varyings shared vs->fs        */
#define GLSL_MAX_ATTRIBS       8    /* vertex attribute locations         */
#define GLSL_MAX_UNIFORMS      64   /* uniform leaves across both stages  */
#define GLSL_MAX_FUNCS         32   /* user-defined functions per stage   */
#define GLSL_MAX_GLOBALS       64   /* file-scope variables/constants      */
#define GLSL_MAX_LOCALS        64   /* locals per function frame          */
#define GLSL_MAX_LOOP_DEPTH    32
#define GLSL_MAX_EXPR_DEPTH    64
#define GLSL_NAME_LEN          48
#define GLSL_ERR_LEN           256
#define GLSL_MAX_ARRAY         64   /* max elements for a declared array  */

/* ------------------------------------------------------------------ *
 * Types
 * ------------------------------------------------------------------ */
typedef enum {
    T_VOID = 0,
    T_BOOL, T_BVEC2, T_BVEC3, T_BVEC4,
    T_INT,  T_IVEC2, T_IVEC3, T_IVEC4,
    T_FLOAT, T_VEC2, T_VEC3, T_VEC4,
    T_MAT2, T_MAT3, T_MAT4,
    T_SAMPLER2D, T_SAMPLERCUBE,
    T_STRUCT           /* user struct: st_id gives the definition */
} glsl_basetype;

/* A value is a run of cells in some storage arena (scratch for temporaries,
 * or a variable's home storage).  Using a pointer + length uniformly represents
 * scalars, vectors, matrices, arrays and structs (which may mix int and float
 * fields).  Float-family components live in .f, int/bool/sampler in .i. */
typedef union glsl_cell { float f; int32_t i; uint32_t u; } glsl_cell;

typedef struct glsl_val {
    int   type;        /* glsl_basetype (element type when array_len > 0) */
    int   st_id;       /* struct definition id when type == T_STRUCT      */
    int   n;           /* total cells (components, or array_len*elem)     */
    int   array_len;   /* 0 = not an array, else element count            */
    int   elem_cells;  /* cells per element (valid when array_len > 0)    */
    glsl_cell *data;   /* points into an arena; never owned by the value   */
} glsl_val;

/* ------------------------------------------------------------------ *
 * AST
 * ------------------------------------------------------------------ */
typedef enum {
    /* leaf */
    EX_FLOAT, EX_INT, EX_BOOL,
    EX_VAR,          /* resolved slot reference (see below)              */
    /* operators */
    EX_NEG, EX_NOT, EX_BNOT, EX_POSTINC, EX_POSTDEC, EX_PREINC, EX_PREDEC,
    EX_ADD, EX_SUB, EX_MUL, EX_DIV, EX_MOD,
    EX_EQ, EX_NE, EX_LT, EX_LE, EX_GT, EX_GE,
    EX_AND, EX_OR, EX_XOR, EX_BAND, EX_BOR, EX_BXOR, EX_SHL, EX_SHR,
    EX_LOGAND, EX_LOGOR, EX_LOGXOR,
    EX_ASSIGN, EX_MUL_ASSIGN, EX_DIV_ASSIGN, EX_MOD_ASSIGN,
    EX_ADD_ASSIGN, EX_SUB_ASSIGN, EX_SHL_ASSIGN, EX_SHR_ASSIGN,
    EX_AND_ASSIGN, EX_XOR_ASSIGN, EX_OR_ASSIGN,
    EX_TERNARY,
    EX_INDEX,        /* a[b]                                             */
    EX_FIELD,        /* a.name  (swizzle or struct field)                */
    EX_CALL,         /* builtin or user function                         */
    EX_CTOR,         /* type constructor                                 */
    EX_COMMA
} glsl_exkind;

typedef struct glsl_expr glsl_expr;
struct glsl_expr {
    int kind;                 /* glsl_exkind                             */
    int op_builtin;           /* for EX_CALL: builtin id or user fn idx  */
    int is_user_fn;           /* EX_CALL: 1 => op_builtin is func index  */
    int ctor_type;            /* EX_CTOR: glsl_basetype being built      */
    int var_scope;            /* EX_VAR: scope class (see VARSCOPE_*)    */
    int var_slot;             /* EX_VAR: index within scope              */
    int var_comp;             /* EX_VAR: component offset (swizzle-less) */
    char var_name[GLSL_NAME_LEN]; /* EX_VAR / user EX_CALL: source name;
                                   * kept so the link pass can re-resolve
                                   * per-stage slots into unified ones.   */
    int field_is_swizzle;     /* EX_FIELD: 1 => swizzle, 0 => struct     */
    unsigned char swz[4];     /* EX_FIELD swizzle component indices      */
    int swz_n;                /* swizzle length 1..4                     */
    int field_idx;            /* EX_FIELD struct member index            */
    int lit_i;                /* EX_INT / EX_BOOL literal                */
    float lit_f;              /* EX_FLOAT literal                        */
    glsl_expr *a, *b, *cc;    /* operands (cc = ternary else / 3rd arg)  */
    glsl_expr **args;         /* EX_CALL / EX_CTOR argument list         */
    int nargs;
};

typedef enum {
    ST_EXPR, ST_DECL, ST_BLOCK, ST_IF, ST_FOR, ST_WHILE, ST_DOWHILE,
    ST_BREAK, ST_CONTINUE, ST_RETURN, ST_DISCARD, ST_CASE, ST_DEFAULT, ST_SWITCH
} glsl_stkind;

typedef struct glsl_stmt glsl_stmt;
struct glsl_stmt {
    int kind;                 /* glsl_stkind                             */
    glsl_expr *expr;          /* ST_EXPR / cond / init / return value    */
    glsl_expr *cond2;         /* ST_FOR condition                        */
    glsl_expr *post;          /* ST_FOR post-expression                  */
    glsl_stmt *body;          /* ST_IF/for/while body (may be block)     */
    glsl_stmt *els;           /* ST_IF else                              */
    glsl_stmt *next;          /* sibling in a block                      */
    glsl_stmt *children;      /* ST_BLOCK child list                     */
    /* ST_DECL */
    int decl_scope;           /* VARSCOPE_*                              */
    int decl_slot;            /* resolved storage slot                   */
    int decl_type;            /* glsl_basetype                           */
    int decl_st_id;           /* struct id if decl_type == T_STRUCT      */
    int decl_array;           /* 0 = scalar, >0 = fixed array length     */
    glsl_expr *decl_init;     /* optional initializer                    */
    int decl_is_const;
};

typedef struct glsl_func {
    char name[GLSL_NAME_LEN];
    int  ret_type;            /* glsl_basetype                           */
    int  ret_st_id;
    /* parameters: each is a local slot with a type */
    int  nparams;
    int  param_type[8];
    int  param_st_id[8];
    int  param_array[8];
    char param_name[8][GLSL_NAME_LEN];
    int  param_inout[8];      /* 0=in 1=out 2=inout                      */
    glsl_stmt *body;          /* compound statement                      */
    int  nlocals;             /* total local slots (params + declared)   */
    /* local frame layout, indexed by local slot (computed at parse) */
    int  frame_cells;
    int  local_off[GLSL_MAX_LOCALS];
    int  local_type[GLSL_MAX_LOCALS];
    int  local_stid[GLSL_MAX_LOCALS];
    int  local_ncomp[GLSL_MAX_LOCALS];   /* cells per element            */
    int  local_array[GLSL_MAX_LOCALS];   /* 0 = scalar, else count       */
} glsl_func;

/* Storage scope classes for resolved EX_VAR / ST_DECL */
enum {
    VARSCOPE_ATTRIB = 0,   /* vertex attribute -> vertex_attribs[slot]  */
    VARSCOPE_VARYING,      /* varying: vs out / fs in -> varyings[slot] */
    VARSCOPE_UNIFORM,      /* uniform leaf -> uniform block offset      */
    VARSCOPE_GLOBAL,       /* file-scope variable/constant              */
    VARSCOPE_LOCAL,        /* function local/param                      */
    VARSCOPE_BUILTIN       /* gl_Position/gl_FragColor/gl_FragCoord/... */
};

/* Built-in variable ids (var_slot when scope == VARSCOPE_BUILTIN) */
enum {
    BI_GL_POSITION = 0, BI_GL_FRAGCOLOR, BI_GL_FRAGCOORD, BI_GL_POINTCOORD,
    BI_GL_FRONTFACING, BI_GL_FRAGDEPTH, BI_GL_POINTSIZE, BI_GL_INSTANCEID,
    BI_DISCARD
};

/* Built-in function ids (EX_CALL, is_user_fn == 0) */
enum {
    BF_RADIANS=0, BF_DEGREES, BF_SIN, BF_COS, BF_TAN, BF_ASIN, BF_ACOS, BF_ATAN,
    BF_ATAN2, BF_POW, BF_EXP, BF_LOG, BF_EXP2, BF_LOG2, BF_SQRT, BF_INVSQRT,
    BF_ABS, BF_SIGN, BF_FLOOR, BF_CEIL, BF_FRACT, BF_MOD, BF_MIN, BF_MAX,
    BF_CLAMP, BF_MIX, BF_STEP, BF_SMOOTHSTEP,
    BF_LENGTH, BF_DISTANCE, BF_DOT, BF_CROSS, BF_NORMALIZE, BF_FACEFORWARD,
    BF_REFLECT, BF_REFRACT,
    BF_MATRIXCOMPMUL, BF_TRANSPOSE, BF_DETERMINANT, BF_INVERSE,
    BF_LESS_THAN, BF_LESS_THAN_EQUAL, BF_GREATER_THAN, BF_GREATER_THAN_EQUAL,
    BF_EQUAL, BF_NOT_EQUAL, BF_ANY, BF_ALL, BF_NOT,
    BF_TEXTURE2D, BF_TEXTURE2DPROJ, BF_TEXTURE2DLOD, BF_TEXTURECUBE,
    BF_DFDX, BF_DFDY, BF_FWIDTH,
    BF_USER_START
};

/* ------------------------------------------------------------------ *
 * Reflection entries produced by link
 * ------------------------------------------------------------------ */
typedef struct {
    char name[GLSL_NAME_LEN];
    int  loc;                 /* attribute location (decl order or bind) */
    int  size;                /* components (2/3/4)                      */
} glsl_attrib;

typedef struct {
    char name[GLSL_NAME_LEN];
    int  slot;                /* vec4 varying slot                       */
    int  ncomp;               /* 2/3/4                                   */
    int  interp;              /* 0 smooth, 1 flat, 2 noperspective       */
} glsl_varying;

typedef struct {
    char name[GLSL_NAME_LEN]; /* full name incl. [i].field for arrays    */
    int  type;                /* glsl_basetype                           */
    int  st_id;
    int  offset;              /* float offset into the uniform block     */
    int  ncomp;               /* component count (1..16)                 */
    int  is_sampler;          /* 1 => value is a texture unit (int)      */
    int  array_len;           /* 0 = scalar, else element count          */
    int  elem_stride;         /* floats per element for arrays           */
} glsl_uniform;

/* ------------------------------------------------------------------ *
 * Compiled stage + linked program
 * ------------------------------------------------------------------ */
typedef struct glsl_stage {
    int is_vertex;            /* 1 vertex, 0 fragment                    */
    glsl_func funcs[GLSL_MAX_FUNCS];
    int nfuncs;
    int main_idx;             /* index of main() in funcs                */
    /* global declarations (uniform/attribute/varying/const/var) */
    struct {
        char name[GLSL_NAME_LEN];
        int  scope;           /* VARSCOPE_*                              */
        int  type, st_id;
        int  array_len;
        int  slot;            /* resolved index within scope             */
        int  is_const;
        glsl_expr *const_init;/* constant expression (evaluated at link) */
        glsl_val  const_val;  /* evaluated constant                      */
        int  has_const_val;
        int  goffset;         /* cell offset into the stage global store */
        int  gcells;          /* cells occupied                          */
    } globals[GLSL_MAX_GLOBALS];
    int nglobals;
    /* structs */
    struct {
        char name[GLSL_NAME_LEN];
        int  nfields;
        char fname[16][GLSL_NAME_LEN];
        int  ftype[16];
        int  fst_id[16];
        int  farray[16];
        int  foffset[16];     /* float offset within the struct          */
        int  total_floats;
    } structs[16];
    int nstructs;
    int uses_fragdepth, uses_discard;
    /* FNV-1a (64-bit) hash of the stage source string, computed at compile
     * time.  Lets the engine recognise a known shader (e.g. one of the 11
     * shadertoy effects) at link time without retaining the source text.
     * glsl_link keeps the stage pointers, so gp->fs->src_hash survives to the
     * matchers.  Inert everywhere else. */
    unsigned long long src_hash;
    char err[GLSL_ERR_LEN];
    int  errored;
} glsl_stage;

typedef struct glsl_program {
    glsl_stage *vs, *fs;
    /* shared reflection (fragment-visible names win for varyings/uniforms) */
    glsl_attrib  attribs[GLSL_MAX_ATTRIBS];  int nattribs;
    glsl_varying varyings[GLSL_MAX_VARYINGS]; int nvaryings;
    glsl_uniform uniforms[GLSL_MAX_UNIFORMS]; int nuniforms;
    int uniform_block_floats;   /* size of the uniform block in floats    */
    int nattr_components;       /* max attribute location+1 used          */
    int linked;
    /* per-stage global variable storage (const/global-var home), allocated
     * and initialised at link. */
    glsl_cell *vs_gstore; int vs_gcells;
    glsl_cell *fs_gstore; int fs_gcells;
    /* post-init snapshots of the global stores; restored at the start of every
     * run so a non-const global mutated during one invocation cannot leak into
     * the next (GLSL global initialisers re-run per invocation). */
    glsl_cell *vs_ginit;
    glsl_cell *fs_ginit;
    /* Persistent execution arenas (allocated at link).  run_vertex/run_fragment
     * are per-vertex/per-pixel hot paths and must not malloc, so expression
     * temporaries bump-allocate in `scratch` (reset each statement) and user
     * function frames bump-allocate in `stack` (saved/restored per call).
     * Rendering is single-threaded per context, so these are program-owned. */
    glsl_cell *scratch; int scratch_cap;
    glsl_cell *stack;   int stack_cap;
    /* Register-bytecode VM compiled form (NULL => run the tree-walker).
     * Opaque to everything outside glsl_exec.c; compiled lazily on the first
     * run of a stage and cached here.  The VM reuses the tree-walker's math
     * helpers so its output is bit-identical; it exists only to make the
     * per-pixel CPU path (virt / no-GPU) fast. */
    void *vm_vs, *vm_fs;
    int   vm_tried;           /* bit0: vs compile attempted, bit1: fs */
    char err[GLSL_ERR_LEN];
} glsl_program;

/* Texture sampling callback injected by the engine bridge.
 * unit  = sampler uniform value (texture unit)
 * dim   = 2 for texture2D/2DProj/2DLod, 3 for textureCube
 * coord = dim floats (s,t) or (x,y,z)
 * out   = rgba result. */
typedef void (*glsl_tex_sample_fn)(int unit, int dim, const float *coord,
                                   float out_rgba[4], void *user);

/* ------------------------------------------------------------------ *
 * Public API
 * ------------------------------------------------------------------ */

/* Compile one stage.  Returns NULL on error (message in *err). */
glsl_stage *glsl_compile_stage(const char *src, int is_vertex,
                               char *err, int errsz);
void glsl_free_stage(glsl_stage *st);

/* Link two compiled stages into a program with resolved reflection.
 * Takes ownership of vs/fs.  Returns NULL on error (message in *err). */
glsl_program *glsl_link(glsl_stage *vs, glsl_stage *fs, char *err, int errsz);
void glsl_free_program(glsl_program *p);
/* Free any VM-compiled form attached to p (defined in glsl_exec.c).  No-op if
 * none was compiled.  Called by glsl_free_program. */
void glsl_vm_free(glsl_program *p);

/* Reflection queries used by the GLES2 object model. */
int  glsl_uniform_index(const glsl_program *p, const char *name); /* -1 none */
int  glsl_attrib_index(const glsl_program *p, const char *name);
int  glsl_prog_varying_count(const glsl_program *p); /* renamed: mesa pglcompiler clash */

/* Bind an attribute location before link (glBindAttribLocation). */
void glsl_bind_attrib_location(glsl_stage *vs, const char *name, int loc);

/* Execution.  uniform_block is a float array of at least
 * p->uniform_block_floats entries.  attribs[loc] points to 4 floats. */
void glsl_run_vertex(glsl_program *p,
                     const float *const *attribs, int nattribs,
                     const void *uniform_block,
                     float *varyings_out,        /* nvaryings*4 floats   */
                     float out_gl_position[4],
                     int instance_id);

void glsl_run_fragment(glsl_program *p,
                       const float *varyings_in, /* nvaryings*4 floats   */
                       const void *uniform_block,
                       const float gl_fragcoord[4],
                       const float gl_pointcoord[2],
                       int front_facing,
                       glsl_tex_sample_fn texfn, void *texuser,
                       float out_gl_fragcolor[4],
                       int *out_discard,
                       float *out_gl_fragdepth, int *out_wrote_depth);

/* Internal: evaluate every file-scope global initialiser of `st` into
 * `gstore` (whose goffset/gcells layout must already be assigned by link).
 * Defined in glsl_exec.c so it can reuse the tree-walking evaluator. */
void glsl_exec_init_globals(glsl_program *p, glsl_stage *st, glsl_cell *gstore);

#ifdef __cplusplus
}
#endif

#endif /* GLES_GLSL_H */
