/* glsl_vm.inc - register-bytecode VM for the CPU GLSL ES 1.00 interpreter.
 *
 * #included at the END of glsl_exec.c so it shares that translation unit and
 * can call the SAME static math helpers (arith, compare, matvec, mat_mul,
 * matscalar, matadd, gen_unary/binary/ternary, cf/ci/pi, setval, make_*), so
 * the VM's arithmetic is bit-identical to the tree-walker BY CONSTRUCTION.
 *
 * Why it is faster than the tree-walk:
 *   - each variable reference is resolved ONCE at compile time to a var-table
 *     entry (base kind + cell offset + swizzle), so there is no per-access
 *     resolve_lv memset(), no find_global_entry() linear scan, no copy_val().
 *   - expressions become a flat postfix instruction stream executed by a tight
 *     dispatch loop (no recursion); temporaries live in a fixed bank, not the
 *     bump scratch arena.
 *   - only the value-level helpers still touch scratch, and each such op
 *     reclaims its scratch immediately (result copied into the bank).
 *
 * Fallback: a stage that uses an unsupported construct (structs, out/inout
 * params, dynamic array indexing, switch) fails compilation and runs on the
 * tree-walker, so correctness never depends on VM coverage.
 */

/* ------------------------------------------------------------------ */
/* limits                                                             */
/* ------------------------------------------------------------------ */
#define VM_FRAME_SLOTS   64   /* temp slots per function frame            */
#define VM_MAX_DEPTH     16   /* max nested user-function calls           */
#define VM_MAX_INSTR   (1u<<22)
#define VM_MAX_VARS     256
#define VM_MAX_LITS     512
#define VM_BUDGET    400000000L  /* runaway guard (valid shaders << this) */

/* operand / var base kinds */
enum { VM_BK_LOCAL = 0, VM_BK_GLOBAL, VM_BK_UNIFORM, VM_BK_ATTRIB,
       VM_BK_VARYING, VM_BK_BUILTIN };

/* opcodes */
enum {
    VMOP_LDC = 0,   /* dst        = literal[x]                       */
    VMOP_LDV,       /* dst        = var[a] (with swizzle)            */
    VMOP_STV,       /* var[dst]   = slot a (with swizzle write)      */
    VMOP_SWZ,       /* dst        = swizzle(slot a)  packed in x     */
    VMOP_NEG,       /* dst        = -slot a                          */
    VMOP_NOT,       /* dst        = !slot a                          */
    VMOP_BNOT,      /* dst        = ~slot a                          */
    VMOP_ARITH,     /* dst        = arith(x, slot a, slot b)         */
    VMOP_CMP,       /* dst        = compare(x, slot a, slot b) (vec) */
    VMOP_EQS,       /* dst        = scalar ==/!= (x=EX_EQ/EX_NE)     */
    VMOP_SEL,       /* dst        = slot x ? slot a : slot b         */
    VMOP_BFUNC,     /* dst        = builtin(x, slots[a..a+b-1])      */
    VMOP_CTOR,      /* dst        = construct(x, slots[a..a+b-1])    */
    VMOP_CALL,      /* dst        = user fn x (slots[a..a+b-1])      */
    VMOP_JMP,       /* pc = x                                        */
    VMOP_JZ,        /* if !slot a: pc = x                            */
    VMOP_JNZ,       /* if  slot a: pc = x                            */
    VMOP_RET,       /* return slot a (a<0 => void)                   */
    VMOP_DISCARD,   /* discard                                       */
    VMOP_END        /* end of function                               */
};

typedef struct { int op, dst, a, b, x; } vminstr;

/* a resolved variable lvalue/slot reference */
typedef struct {
    int base_kind;      /* VM_BK_*                                  */
    int off;            /* cell offset within base (BI_* for BUILTIN)*/
    int type;           /* result element type                       */
    int st_id;
    int n;              /* cells to load/store (<=16)                */
    int array_len;
    int elem_cells;
    int swz_n;          /* 0 => whole                                */
    unsigned char swz[4];
} vmvar;

typedef struct { vminstr *code; int ncode, cap; int nslots; } vmfunc;

typedef struct vmprog {
    glsl_stage *stage;
    vmvar   *vars;  int nvars;
    glsl_cell *lits; int *littype; int nlits;
    vmfunc  *funcs; int nfuncs; int main_idx;
    /* runtime temp bank (slots*16 per depth * VM_MAX_DEPTH) */
    glsl_cell *bank; int *btype, *bn;
    int frame_slots;
} vmprog;

/* ------------------------------------------------------------------ */
/* value-level cores (math copied verbatim from construct()/builtin()) */
/* ------------------------------------------------------------------ */
static glsl_val vm_construct(env *E, int type, const glsl_val *av, int nargs)
{
    int total = glsl_base_comps(type);
    glsl_cell *d;
    glsl_val v;
    float flat[64]; int nf = 0, i;
    d = sc(E, total > 0 ? total : 1);
    if (type == T_SAMPLER2D || type == T_SAMPLERCUBE) {
        d[0].i = 0;
        setval(&v, type, -1, d, 1, 0, 0);
        return v;
    }
    if (glsl_is_matrix(type)) {
        int dim = mat_dim(type);
        for (i = 0; i < dim * dim; ++i) d[i].f = 0;
        if (nargs == 1) {
            glsl_val a = av[0];
            if (glsl_is_matrix(a.type)) {
                int c, r, m = mat_dim(a.type);
                for (c = 0; c < dim && c < m; ++c)
                    for (r = 0; r < dim && r < m; ++r)
                        d[c * dim + r].f = a.data[c * m + r].f;
                for (c = 0; c < dim; ++c) if (c >= m) d[c*dim+c].f = 1;
            } else {
                float s = cf(a.data, a.type, 0);
                int c;
                for (c = 0; c < dim; ++c) d[c * dim + c].f = s;
            }
        } else {
            int idx = 0;
            for (i = 0; i < nargs && idx < dim*dim; ++i) {
                glsl_val a = av[i];
                int an = a.n, k;
                for (k = 0; k < an && idx < dim*dim; ++k) d[idx++].f = cf(a.data, a.type, k);
            }
        }
        setval(&v, type, -1, d, dim * dim, 0, 0);
        return v;
    }
    if (nargs == 1 && total > 1) {
        glsl_val a = av[0];
        if (a.n == 1) {
            float s = cf(a.data, a.type, 0);
            int isf = !glsl_is_int_family(a.type);
            for (i = 0; i < total; ++i)
                if (glsl_is_int_family(type)) d[i].i = isf ? (int)s : ci(a.data, a.type, 0);
                else d[i].f = isf ? s : (float)ci(a.data, a.type, 0);
        } else {
            for (i = 0; i < total && i < a.n; ++i)
                if (glsl_is_int_family(type)) d[i].i = ci(a.data, a.type, i);
                else d[i].f = cf(a.data, a.type, i);
        }
        setval(&v, type, -1, d, total, 0, 0);
        return v;
    }
    for (i = 0; i < nargs && nf < 64; ++i) {
        glsl_val a = av[i];
        int k;
        for (k = 0; k < a.n && nf < 64; ++k) flat[nf++] = cf(a.data, a.type, k);
    }
    for (i = 0; i < total; ++i) {
        float f = i < nf ? flat[i] : 0;
        if (glsl_is_int_family(type)) d[i].i = (int)f; else d[i].f = f;
    }
    setval(&v, type, -1, d, total, 0, 0);
    return v;
}
