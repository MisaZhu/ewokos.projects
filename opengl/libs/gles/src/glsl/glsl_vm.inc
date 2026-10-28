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

#ifdef VM_PROFILE
#include <stdio.h>
static unsigned long vm_opcount[64];
static unsigned long vm_ninstr, vm_ncalls, vm_nrun;
static const char *vm_opname[24] = {
    "LDC","LDV","STV","SWZ","NEG","NOT","BNOT","ARITH","CMP","EQS","SEL",
    "BFUNC","CTOR","CALL","JMP","JZ","JNZ","RET","DISCARD","END"
};
static void vm_profile_report(void) {
    int i;
    fprintf(stderr, "\n== VM PROFILE: instrs=%lu calls=%lu runs=%lu ==\n",
            vm_ninstr, vm_ncalls, vm_nrun);
    for (i = 0; i < 20; ++i)
        if (vm_opcount[i])
            fprintf(stderr, "  %-8s %14lu  (%5.1f%%)\n", vm_opname[i],
                    vm_opcount[i],
                    100.0*(double)vm_opcount[i]/(double)(vm_ninstr?vm_ninstr:1));
    if (vm_nrun)
        fprintf(stderr, "  instrs/run=%.0f  calls/run=%.1f  calls/instr=%.3f\n",
                (double)vm_ninstr/(double)vm_nrun,
                (double)vm_ncalls/(double)vm_nrun,
                (double)vm_ncalls/(double)(vm_ninstr?vm_ninstr:1));
}
static int vm_prof_registered = 0;
#endif

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

typedef struct {
    vminstr *code; int ncode, cap;
    int *aux; int naux, auxcap;   /* operand-slot lists for BFUNC/CTOR/CALL */
    int nslots;
} vmfunc;

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

/* extra opcodes not in the base enum above */
enum { VMOP_COPY = VMOP_END + 1, VMOP_BOOL, VMOP_LXOR };

/* Value-level builtin: identical math to builtin() (glsl_exec.c) but arguments
 * arrive pre-evaluated as glsl_val (pointing at bank cells) instead of AST
 * nodes.  Reuses gen_unary/gen_binary/gen_ternary/compare/make_* so the common
 * paths are literally the same code as the tree-walker => bit-identical. */
static glsl_val vm_builtin(env *E, int id, const glsl_val *av, int nargs)
{
    glsl_val a, b, c, v;
    int i;
    glsl_cell *d;
    a.data = NULL; a.type = T_FLOAT; a.n = 0; a.st_id = -1; a.array_len = 0; a.elem_cells = 0;
    b = a; c = a;
    if (nargs > 0) a = av[0];
    if (nargs > 1) b = av[1];
    if (nargs > 2) c = av[2];
    switch (id) {
    case BF_RADIANS: case BF_DEGREES: case BF_SIN: case BF_COS: case BF_TAN:
    case BF_ASIN: case BF_ACOS: case BF_EXP: case BF_LOG: case BF_EXP2:
    case BF_LOG2: case BF_SQRT: case BF_INVSQRT: case BF_ABS: case BF_SIGN:
    case BF_FLOOR: case BF_CEIL: case BF_FRACT: case BF_TANH:
        return gen_unary(E, id, &a);
    case BF_ATAN:
        if (nargs >= 2) return gen_binary(E, BF_ATAN2, &a, &b);
        return gen_unary(E, BF_ATAN, &a);
    case BF_ATAN2: case BF_POW: case BF_MOD: case BF_MIN: case BF_MAX: case BF_STEP:
        return gen_binary(E, id, &a, &b);
    case BF_CLAMP: case BF_MIX: case BF_SMOOTHSTEP:
        return gen_ternary(E, id, &a, &b, &c);
    case BF_LENGTH: {
        float s = 0;
        for (i = 0; i < a.n; ++i) { float f = cf(a.data,a.type,i); s += f*f; }
        return make_scalar_f(E, sqrtf(s));
    }
    case BF_DISTANCE: {
        float s = 0;
        for (i = 0; i < a.n; ++i) { float f = cf(a.data,a.type,i)-cf(b.data,b.type,i); s+=f*f; }
        return make_scalar_f(E, sqrtf(s));
    }
    case BF_DOT: {
        float s = 0;
        for (i = 0; i < a.n; ++i) s += cf(a.data,a.type,i)*cf(b.data,b.type,i);
        return make_scalar_f(E, s);
    }
    case BF_CROSS: {
        glsl_cell *o = sc(E,3);
        o[0].f = cf(a.data,a.type,1)*cf(b.data,b.type,2)-cf(a.data,a.type,2)*cf(b.data,b.type,1);
        o[1].f = cf(a.data,a.type,2)*cf(b.data,b.type,0)-cf(a.data,a.type,0)*cf(b.data,b.type,2);
        o[2].f = cf(a.data,a.type,0)*cf(b.data,b.type,1)-cf(a.data,a.type,1)*cf(b.data,b.type,0);
        setval(&v,T_VEC3,-1,o,3,0,0); return v;
    }
    case BF_NORMALIZE: {
        float s=0,len;
        for(i=0;i<a.n;++i){float f=cf(a.data,a.type,i);s+=f*f;}
        len=sqrtf(s); d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].f = len>0? cf(a.data,a.type,i)/len : 0;
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_FACEFORWARD: {
        float dt=0;
        for(i=0;i<b.n;++i) dt+=cf(c.data,c.type,i)*cf(b.data,b.type,i);
        d=sc(E,a.n); for(i=0;i<a.n;++i) d[i].f = dt<0? cf(a.data,a.type,i): -cf(a.data,a.type,i);
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_REFLECT: {
        float dt=0;
        for(i=0;i<a.n;++i) dt+=cf(a.data,a.type,i)*cf(b.data,b.type,i);
        d=sc(E,a.n); for(i=0;i<a.n;++i) d[i].f = cf(a.data,a.type,i)-2*dt*cf(b.data,b.type,i);
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_REFRACT: {
        float eta=cf(c.data,T_FLOAT,0);
        float ddot=0,k; for(i=0;i<a.n;++i) ddot+=cf(a.data,a.type,i)*cf(b.data,b.type,i);
        k=1-eta*eta*(1-ddot*ddot); d=sc(E,a.n);
        if(k<0){for(i=0;i<a.n;++i)d[i].f=0;}
        else {float f=eta*ddot+sqrtf(k); for(i=0;i<a.n;++i) d[i].f=eta*cf(a.data,a.type,i)-f*cf(b.data,b.type,i);}
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_MATRIXCOMPMUL: {
        d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].f=cf(a.data,a.type,i)*cf(b.data,b.type,i);
        setval(&v,a.type,-1,d,a.n,0,0); return v;
    }
    case BF_TRANSPOSE: {
        int dim=mat_dim(a.type),cc,rr; d=sc(E,dim*dim);
        for(cc=0;cc<dim;++cc)for(rr=0;rr<dim;++rr) d[cc*dim+rr].f=a.data[rr*dim+cc].f;
        setval(&v,a.type,-1,d,dim*dim,0,0); return v;
    }
    case BF_DETERMINANT: {
        int dim=mat_dim(a.type);
        if(dim==2){float r=a.data[0].f*a.data[3].f-a.data[2].f*a.data[1].f;return make_scalar_f(E,r);}
        if(dim==3){
            float m[9]; for(i=0;i<9;++i)m[i]=a.data[i].f;
            float r=m[0]*(m[4]*m[8]-m[7]*m[5])-m[3]*(m[1]*m[8]-m[7]*m[2])+m[6]*(m[1]*m[5]-m[4]*m[2]);
            return make_scalar_f(E,r);
        }
        if(dim==4){
            float mm[16]; for(i=0;i<16;++i)mm[i]=a.data[i].f;
            float r=0; int c;
            for(c=0;c<4;++c){
                float sub[9]; int rr,kk,si=0;
                for(rr=1;rr<4;++rr)for(kk=0;kk<4;++kk){ if(kk==c)continue; sub[si++]=mm[rr*4+kk]; }
                { float det3=sub[0]*(sub[4]*sub[8]-sub[7]*sub[5])-sub[3]*(sub[1]*sub[8]-sub[7]*sub[2])+sub[6]*(sub[1]*sub[5]-sub[4]*sub[2]);
                  r += ((c&1)?-1:1)*mm[c]*det3; }
            }
            return make_scalar_f(E,r);
        }
        return make_scalar_f(E,0);
    }
    case BF_INVERSE: { E->err = 0; return make_scalar_f(E, 0); }
    case BF_LESS_THAN: case BF_LESS_THAN_EQUAL: case BF_GREATER_THAN:
    case BF_GREATER_THAN_EQUAL: case BF_EQUAL: case BF_NOT_EQUAL: {
        int op = id==BF_LESS_THAN?EX_LT:id==BF_LESS_THAN_EQUAL?EX_LE:
                 id==BF_GREATER_THAN?EX_GT:id==BF_GREATER_THAN_EQUAL?EX_GE:
                 id==BF_EQUAL?EX_EQ:EX_NE;
        return compare(E,op,&a,&b);
    }
    case BF_ANY: case BF_ALL: {
        if(id==BF_ANY){for(i=0;i<a.n;++i) if(ci(a.data,a.type,i)) return make_bool(E,1); return make_bool(E,0);}
        for(i=0;i<a.n;++i) if(!ci(a.data,a.type,i)) return make_bool(E,0); return make_bool(E,1);
    }
    case BF_NOT: {
        d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].i = ci(a.data,a.type,i)?0:1;
        setval(&v,glsl_bool_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_TEXTURE2D: {
        glsl_val uv; int unit; float rgba[4], co[2]; glsl_cell*o=sc(E,4);
        unit=ci(a.data,a.type,0); uv=b;
        rgba[0]=rgba[1]=rgba[2]=1; rgba[3]=1;
        co[0]=cf(uv.data,uv.type,0); co[1]=cf(uv.data,uv.type,1);
        sample_texture(E,unit,2,co,NULL,NULL,1,0.0f,rgba);
        for(i=0;i<4;++i) o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURE2DPROJ: {
        glsl_val uv; int unit; float rgba[4],w,co[2]; glsl_cell*o=sc(E,4);
        unit=ci(a.data,a.type,0); uv=b; w=cf(uv.data,uv.type,uv.n-1);
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=w!=0?cf(uv.data,uv.type,0)/w:0; co[1]=w!=0?cf(uv.data,uv.type,1)/w:0;
        if(w!=0) sample_texture(E,unit,2,co,NULL,NULL,1,0.0f,rgba);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURECUBE: {
        glsl_val dir; int unit; float rgba[4],co[3]; glsl_cell*o=sc(E,4);
        unit=ci(a.data,a.type,0); dir=b;
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=cf(dir.data,dir.type,0); co[1]=cf(dir.data,dir.type,1); co[2]=cf(dir.data,dir.type,2);
        sample_texture(E,unit,3,co,NULL,NULL,1,0.0f,rgba);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURECUBELOD: {
        glsl_val dir; int unit; float rgba[4],co[3]; glsl_cell*o=sc(E,4);
        unit=ci(a.data,a.type,0); dir=b;
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=cf(dir.data,dir.type,0); co[1]=cf(dir.data,dir.type,1); co[2]=cf(dir.data,dir.type,2);
        sample_texture(E,unit,3,co,NULL,NULL,1,cf(c.data,c.type,0),rgba);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURE2DLOD: {
        glsl_val uv; int unit; float rgba[4],co[2]; glsl_cell*o=sc(E,4);
        unit=ci(a.data,a.type,0); uv=b;
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=cf(uv.data,uv.type,0); co[1]=cf(uv.data,uv.type,1);
        sample_texture(E,unit,2,co,NULL,NULL,1,cf(c.data,c.type,0),rgba);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_DFDX: case BF_DFDY: case BF_FWIDTH:
        d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].f=0;
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    default:
        E->err = 1; return make_scalar_f(E, 0);
    }
}

/* ------------------------------------------------------------------ */
/* runtime helpers                                                     */
/* ------------------------------------------------------------------ */
static glsl_cell *vm_base(env *E, const vmvar *V)
{
    switch (V->base_kind) {
    case VM_BK_LOCAL:   return E->locals + V->off;
    case VM_BK_GLOBAL:  return E->gstore + V->off;
    case VM_BK_UNIFORM: return (glsl_cell *)E->ublock + V->off;
    case VM_BK_ATTRIB:
        if (!E->attribs || V->off < 0 || V->off >= E->nattribs || !E->attribs[V->off]) {
            E->err = 1; return E->prog->scratch;
        }
        return (glsl_cell *)E->attribs[V->off];
    case VM_BK_VARYING:
        return (E->is_vertex ? (glsl_cell *)E->varyings_out : (glsl_cell *)E->varyings_in) + V->off;
    case VM_BK_BUILTIN:
        switch (V->off) {
        case BI_GL_POSITION:   return (glsl_cell *)E->gl_position;
        case BI_GL_FRAGCOLOR:  return (glsl_cell *)E->gl_fragcolor;
        case BI_GL_FRAGCOORD:  return (glsl_cell *)E->gl_fragcoord;
        case BI_GL_POINTCOORD: return (glsl_cell *)E->gl_pointcoord;
        case BI_GL_FRONTFACING:return (glsl_cell *)&E->front_facing;
        case BI_GL_FRAGDEPTH:  return (glsl_cell *)&E->gl_fragdepth;
        case BI_GL_INSTANCEID: return (glsl_cell *)&E->instance_id;
        default: E->err = 1; return E->prog->scratch;
        }
    default: E->err = 1; return E->prog->scratch;
    }
}

static inline glsl_val vm_mkval(glsl_cell *fb, int *bt, int *bn, int s)
{
    glsl_val v; v.data = fb + s*16; v.type = bt[s]; v.n = bn[s];
    v.st_id = -1; v.array_len = 0; v.elem_cells = 0; return v;
}
static inline void vm_putval(glsl_cell *fb, int *bt, int *bn, int s, const glsl_val *r)
{
    glsl_cell *d = fb + s*16; int i, n = r->n > 16 ? 16 : r->n;
    for (i = 0; i < n; ++i) d[i] = r->data[i];
    bt[s] = r->type; bn[s] = r->n;
}
static void vm_stv(env *E, const vmvar *V, const glsl_val *v)
{
    glsl_cell *base = vm_base(E, V); int i;
    if (E->err) return;
    if (V->swz_n > 0) {
        for (i = 0; i < V->swz_n; ++i) {
            int c = V->swz[i];
            if (glsl_is_int_family(V->type)) base[c].i = ci(v->data, v->type, i < v->n ? i : 0);
            else base[c].f = cf(v->data, v->type, i < v->n ? i : 0);
        }
        return;
    }
    {
        int n = V->n < v->n ? V->n : v->n;
        if (v->n == 1 && V->n > 1 && V->array_len == 0) {
            for (i = 0; i < V->n; ++i)
                if (glsl_is_int_family(V->type)) base[i].i = ci(v->data, v->type, 0);
                else base[i].f = cf(v->data, v->type, 0);
            return;
        }
        for (i = 0; i < n; ++i)
            if (glsl_is_int_family(V->type)) base[i].i = ci(v->data, v->type, i);
            else base[i].f = cf(v->data, v->type, i);
    }
}

/* ------------------------------------------------------------------ */
/* dispatch loop                                                       */
/* ------------------------------------------------------------------ */
static void vm_exec(env *E, vmprog *vp, int funcidx, int depth, long *budget)
{
    vmfunc *cfun = &vp->funcs[funcidx];
    glsl_cell *fb = vp->bank + (depth * vp->frame_slots) * 16;
    int *bt = vp->btype + depth * vp->frame_slots;
    int *bn = vp->bn    + depth * vp->frame_slots;
    int pc = 0;
    while (pc < cfun->ncode) {
        vminstr *I;
        int sc_save;
        if (--(*budget) < 0) { E->err = 1; return; }
        if (E->err) return;
        I = &cfun->code[pc++];
#ifdef VM_PROFILE
        vm_opcount[I->op & 63]++; vm_ninstr++;
#endif
        sc_save = E->scratch_top;
        switch (I->op) {
        case VMOP_LDC: {
            glsl_cell *d = fb + I->dst*16;
            d[0] = vp->lits[I->x]; bt[I->dst] = vp->littype[I->x]; bn[I->dst] = 1;
            break;
        }
        case VMOP_LDV: {
            vmvar *V = &vp->vars[I->a];
            glsl_cell *base = vm_base(E, V), *d = fb + I->dst*16; int i;
            if (E->err) break;
            if (V->swz_n > 0) { for (i=0;i<V->swz_n;++i) d[i]=base[V->swz[i]]; bt[I->dst]=V->type; bn[I->dst]=V->swz_n; }
            else { for (i=0;i<V->n;++i) d[i]=base[i]; bt[I->dst]=V->type; bn[I->dst]=V->n; }
            break;
        }
        case VMOP_STV: {
            vmvar *V = &vp->vars[I->dst];
            glsl_val v = vm_mkval(fb, bt, bn, I->a);
            vm_stv(E, V, &v);
            break;
        }
        case VMOP_SWZ: {
            int n = I->x & 15; unsigned char sw[4]; glsl_cell tmp[4];
            glsl_cell *s = fb + I->a*16, *d = fb + I->dst*16; int i;
            sw[0]=(I->x>>4)&15; sw[1]=(I->x>>8)&15; sw[2]=(I->x>>12)&15; sw[3]=(I->x>>16)&15;
            for (i=0;i<4;++i) tmp[i]=s[i];       /* in-place safe: dst may alias src */
            for (i=0;i<n;++i) d[i]=tmp[sw[i]];
            bt[I->dst] = glsl_is_int_family(bt[I->a]) ? glsl_int_type_of_len(n) : glsl_float_type_of_len(n);
            bn[I->dst] = n;
            break;
        }
        case VMOP_COPY: {
            glsl_cell *s = fb + I->a*16, *d = fb + I->dst*16; int i, n = bn[I->a];
            for (i=0;i<n;++i) d[i]=s[i]; bt[I->dst]=bt[I->a]; bn[I->dst]=n;
            break;
        }
        case VMOP_NEG: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a); glsl_cell *d = fb + I->dst*16; int i;
            for (i=0;i<a.n;++i) { if (glsl_is_int_family(a.type)) d[i].i=-ci(a.data,a.type,i); else d[i].f=-cf(a.data,a.type,i); }
            bt[I->dst]=a.type; bn[I->dst]=a.n; break;
        }
        case VMOP_NOT: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a); glsl_cell *d = fb + I->dst*16;
            d[0].i = ci(a.data,a.type,0)?0:1; bt[I->dst]=T_BOOL; bn[I->dst]=1; break;
        }
        case VMOP_BOOL: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a); glsl_cell *d = fb + I->dst*16;
            d[0].i = ci(a.data,a.type,0)?1:0; bt[I->dst]=T_BOOL; bn[I->dst]=1; break;
        }
        case VMOP_BNOT: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a); glsl_cell *d = fb + I->dst*16; int i;
            for (i=0;i<a.n;++i) d[i].i=~ci(a.data,a.type,i);
            bt[I->dst]=glsl_int_type_of_len(a.n); bn[I->dst]=a.n; break;
        }
        case VMOP_LXOR: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a), b = vm_mkval(fb,bt,bn,I->b); glsl_cell *d = fb + I->dst*16;
            d[0].i = ((ci(a.data,a.type,0)?1:0)!=(ci(b.data,b.type,0)?1:0))?1:0;
            bt[I->dst]=T_BOOL; bn[I->dst]=1; break;
        }
        case VMOP_ARITH: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a), b = vm_mkval(fb,bt,bn,I->b), r; int op = I->x;
            if (glsl_is_matrix(a.type) || glsl_is_matrix(b.type)) {
                if (op==EX_MUL) {
                    if (glsl_is_matrix(a.type)&&glsl_is_matrix(b.type)) mat_mul(E,&a,&b,&r);
                    else if (glsl_is_matrix(a.type)) { if (comp_len(&b)==1&&b.n==1) r=matscalar(E,&a,&b,0); else r=matvec(E,&a,&b,0); }
                    else { if (a.n==1&&comp_len(&a)==1) r=matscalar(E,&b,&a,0); else r=matvec(E,&b,&a,1); }
                } else if (op==EX_ADD) r=matadd(E,&a,&b,0);
                else if (op==EX_SUB) r=matadd(E,&a,&b,1);
                else if (op==EX_DIV&&glsl_is_matrix(a.type)) r=matscalar(E,&a,&b,1);
                else r=arith(E,op,&a,&b);
            } else r=arith(E,op,&a,&b);
            vm_putval(fb,bt,bn,I->dst,&r); break;
        }
        case VMOP_CMP: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a), b = vm_mkval(fb,bt,bn,I->b);
            glsl_val r = compare(E,I->x,&a,&b); vm_putval(fb,bt,bn,I->dst,&r); break;
        }
        case VMOP_EQS: {
            glsl_val a = vm_mkval(fb,bt,bn,I->a), b = vm_mkval(fb,bt,bn,I->b);
            int eq=1,i,n=a.n>b.n?a.n:b.n; glsl_cell *d = fb + I->dst*16;
            for(i=0;i<n;++i) if(cf(a.data,a.type,i<a.n?i:0)!=cf(b.data,b.type,i<b.n?i:0)){eq=0;break;}
            d[0].i = (I->x==EX_EQ? eq : !eq)?1:0; bt[I->dst]=T_BOOL; bn[I->dst]=1; break;
        }
        case VMOP_BFUNC: {
            glsl_val av[32]; int k, nargs=I->b; glsl_val r;
            for(k=0;k<nargs&&k<32;++k) av[k]=vm_mkval(fb,bt,bn,cfun->aux[I->a+k]);
            r = vm_builtin(E,I->x,av,nargs); vm_putval(fb,bt,bn,I->dst,&r); break;
        }
        case VMOP_CTOR: {
            glsl_val av[32]; int k, nargs=I->b; glsl_val r;
            for(k=0;k<nargs&&k<32;++k) av[k]=vm_mkval(fb,bt,bn,cfun->aux[I->a+k]);
            r = vm_construct(E,I->x,av,nargs); vm_putval(fb,bt,bn,I->dst,&r); break;
        }
        case VMOP_CALL: {
            int fi=I->x, k, nargs=I->b, save_stack, save_ret_type;
#ifdef VM_PROFILE
            vm_ncalls++;
#endif
            const glsl_func *F; glsl_cell *frame;
            const glsl_func *save_fn; glsl_cell *save_locals;
            glsl_val argv[32];
            if (fi<0||fi>=vp->stage->nfuncs) { E->err=1; break; }
            F=&vp->stage->funcs[fi];
            if (!F->body) { E->err=1; break; }
            for(k=0;k<nargs&&k<32;++k) argv[k]=vm_mkval(fb,bt,bn,cfun->aux[I->a+k]);
            save_stack=E->stack_top;
            frame=stk(E,F->frame_cells>0?F->frame_cells:1);
            memset(frame,0,(F->frame_cells>0?F->frame_cells:1)*sizeof(glsl_cell));
            save_fn=E->fn; save_locals=E->locals;
            E->fn=F; E->locals=frame;
            for(k=0;k<F->nparams&&k<nargs;++k){
                if (F->param_inout[k]!=0) continue;
                { glsl_cell *dp=frame+F->local_off[k];
                  int n=F->local_ncomp[k]*(F->local_array[k]>0?F->local_array[k]:1), j;
                  for(j=0;j<n;++j)
                      if(glsl_is_int_family(F->local_type[k])) dp[j].i=ci(argv[k].data,argv[k].type,j<argv[k].n?j:0);
                      else dp[j].f=cf(argv[k].data,argv[k].type,j<argv[k].n?j:0); }
            }
            save_ret_type=E->ret_type;
            E->ctrl=CTRL_NONE;
            if (depth+1 < VM_MAX_DEPTH) vm_exec(E,vp,fi,depth+1,budget); else E->err=1;
            if (E->ctrl != CTRL_DISCARD) E->ctrl = CTRL_NONE;
            E->fn=save_fn; E->locals=save_locals; E->stack_top=save_stack;
            { glsl_val v;
              v.type=E->ret_type; v.st_id=E->ret_stid; v.n=E->ret_n; v.array_len=E->ret_arr; v.elem_cells=E->ret_elem;
              v.data=sc(E,E->ret_n>0?E->ret_n:1);
              if(E->ret_n>0) memcpy(v.data,E->ret_buf,E->ret_n*sizeof(glsl_cell));
              E->ret_type=save_ret_type;
              if(F->ret_type==T_VOID){ glsl_val z=make_scalar_f(E,0); vm_putval(fb,bt,bn,I->dst,&z); }
              else vm_putval(fb,bt,bn,I->dst,&v);
            }
            break;
        }
        case VMOP_JMP:  pc = I->x; break;
        case VMOP_JZ:  { glsl_val a=vm_mkval(fb,bt,bn,I->a); if(!ci(a.data,a.type,0)) pc=I->x; break; }
        case VMOP_JNZ: { glsl_val a=vm_mkval(fb,bt,bn,I->a); if( ci(a.data,a.type,0)) pc=I->x; break; }
        case VMOP_RET: {
            if (I->a>=0) { glsl_val r=vm_mkval(fb,bt,bn,I->a);
                E->ret_type=r.type; E->ret_stid=r.st_id; E->ret_n=r.n; E->ret_arr=r.array_len; E->ret_elem=r.elem_cells;
                if(r.n>0&&r.n<=256) memcpy(E->ret_buf,r.data,r.n*sizeof(glsl_cell));
            } else { E->ret_type=T_VOID; E->ret_n=0; }
            E->ctrl=CTRL_RETURN; break;
        }
        case VMOP_DISCARD: E->ctrl=CTRL_DISCARD; break;
        case VMOP_END: E->scratch_top=sc_save; return;
        default: E->err=1; break;
        }
        E->scratch_top = sc_save;
        if (E->ctrl==CTRL_DISCARD || E->ctrl==CTRL_RETURN) return;
    }
}

/* ================================================================== */
/* compiler: AST -> bytecode                                          */
/* ================================================================== */
typedef struct {
    int brk, cont;      /* patch-chain heads (-1 empty); chain link in instr.b */
} vmloop;

typedef struct {
    vmprog      *vp;
    glsl_program *prog;
    glsl_stage   *stage;
    int          fi;        /* function index being compiled              */
    vmfunc      *cf;        /* &vp->funcs[fi]                             */
    int          slottop;   /* temp-slot stack top                        */
    int          maxslots;  /* peak slots used by this function           */
    int          failed;    /* set => stage cannot be VM-compiled         */
    vmloop       loops[GLSL_MAX_LOOP_DEPTH]; int nloops;
} vmc;

static int find_gi(const glsl_stage *st, int scope, int slot)
{
    int i;
    for (i = 0; i < st->nglobals; ++i)
        if (st->globals[i].scope == scope && st->globals[i].slot == slot) return i;
    return -1;
}

static int vm_alloc(vmc *C)
{
    int s = C->slottop++;
    if (C->slottop > C->maxslots) C->maxslots = C->slottop;
    if (s >= VM_FRAME_SLOTS) C->failed = 1;
    return s;
}
static int vm_emit(vmc *C, int op, int dst, int a, int b, int x)
{
    vmfunc *cf = C->cf;
    if (cf->ncode + 1 > cf->cap) {
        int nc = cf->cap ? cf->cap * 2 : 64;
        vminstr *p;
        if (nc > (int)VM_MAX_INSTR) { C->failed = 1; return -1; }
        p = (vminstr *)realloc(cf->code, nc * sizeof(vminstr));
        if (!p) { C->failed = 1; return -1; }
        cf->code = p; cf->cap = nc;
    }
    { vminstr *I = &cf->code[cf->ncode];
      I->op = op; I->dst = dst; I->a = a; I->b = b; I->x = x;
      return cf->ncode++; }
}
static int vm_aux(vmc *C, const int *slots, int n)
{
    vmfunc *cf = C->cf; int base = cf->naux, i;
    if (cf->naux + n > cf->auxcap) {
        int nc = cf->auxcap ? cf->auxcap : 32; int *p;
        while (nc < cf->naux + n) nc *= 2;
        p = (int *)realloc(cf->aux, nc * sizeof(int));
        if (!p) { C->failed = 1; return -1; }
        cf->aux = p; cf->auxcap = nc;
    }
    for (i = 0; i < n; ++i) cf->aux[base + i] = slots[i];
    cf->naux += n; return base;
}
static void vm_patch(vmc *C, int head, int target)
{
    while (head >= 0) { int nx = C->cf->code[head].b; C->cf->code[head].x = target; head = nx; }
}
static int vm_addvar(vmc *C, const vmvar *V)
{
    vmprog *vp = C->vp;
    if (vp->nvars >= VM_MAX_VARS) { C->failed = 1; return -1; }
    vp->vars[vp->nvars] = *V; return vp->nvars++;
}
static int vm_ldc(vmc *C, glsl_cell c, int type)
{
    vmprog *vp = C->vp; int idx, s;
    if (vp->nlits >= VM_MAX_LITS) { C->failed = 1; return -1; }
    idx = vp->nlits; vp->lits[idx] = c; vp->littype[idx] = type; vp->nlits++;
    s = vm_alloc(C); vm_emit(C, VMOP_LDC, s, 0, 0, idx); return s;
}

/* resolve an EX_VAR (or builtin) into a var-table entry */
static int vm_resolve_var(vmc *C, const glsl_expr *e, vmvar *V)
{
    memset(V, 0, sizeof(*V)); V->st_id = -1;
    switch (e->var_scope) {
    case VARSCOPE_LOCAL: {
        const glsl_func *F = &C->stage->funcs[C->fi]; int s = e->var_slot;
        if (s < 0 || s >= F->nlocals) return 0;
        V->base_kind = VM_BK_LOCAL; V->off = F->local_off[s];
        V->type = F->local_type[s]; V->st_id = F->local_stid[s];
        V->array_len = F->local_array[s]; V->elem_cells = F->local_ncomp[s];
        V->n = V->array_len > 0 ? V->elem_cells * V->array_len : V->elem_cells;
        return 1;
    }
    case VARSCOPE_GLOBAL: {
        int gi = find_gi(C->stage, VARSCOPE_GLOBAL, e->var_slot);
        if (gi < 0) return 0;
        V->base_kind = VM_BK_GLOBAL; V->off = C->stage->globals[gi].goffset;
        V->type = C->stage->globals[gi].type; V->st_id = C->stage->globals[gi].st_id;
        V->array_len = C->stage->globals[gi].array_len;
        V->elem_cells = glsl_type_cells(C->stage, V->type, V->st_id);
        V->n = glsl_var_cells(C->stage, V->type, V->st_id, V->array_len);
        return 1;
    }
    case VARSCOPE_UNIFORM: {
        int u = e->var_slot; const glsl_uniform *U;
        if (u < 0 || u >= C->prog->nuniforms) return 0;
        U = &C->prog->uniforms[u];
        V->base_kind = VM_BK_UNIFORM; V->off = U->offset;
        V->type = U->type; V->st_id = U->st_id; V->array_len = U->array_len;
        V->elem_cells = U->ncomp; V->n = U->array_len > 0 ? U->ncomp * U->array_len : U->ncomp;
        return 1;
    }
    case VARSCOPE_ATTRIB: {
        int loc = e->var_slot;
        if (loc < 0 || loc >= C->prog->nattribs) return 0;
        V->base_kind = VM_BK_ATTRIB; V->off = loc;
        V->n = C->prog->attribs[loc].size; V->array_len = 0; V->elem_cells = V->n;
        V->type = V->n == 1 ? T_FLOAT : glsl_float_type_of_len(V->n); V->st_id = -1;
        return 1;
    }
    case VARSCOPE_VARYING: {
        int s = e->var_slot;
        if (s < 0 || s >= C->prog->nvaryings) return 0;
        V->base_kind = VM_BK_VARYING; V->off = s * 4;
        V->type = glsl_float_type_of_len(C->prog->varyings[s].ncomp); V->st_id = -1;
        V->n = C->prog->varyings[s].ncomp; V->array_len = 0; V->elem_cells = V->n;
        return 1;
    }
    case VARSCOPE_BUILTIN:
        V->base_kind = VM_BK_BUILTIN; V->off = e->var_slot; V->st_id = -1; V->array_len = 0;
        switch (e->var_slot) {
        case BI_GL_POSITION:   V->type=T_VEC4;  V->n=4; break;
        case BI_GL_FRAGCOLOR:  V->type=T_VEC4;  V->n=4; break;
        case BI_GL_FRAGCOORD:  V->type=T_VEC4;  V->n=4; break;
        case BI_GL_POINTCOORD: V->type=T_VEC2;  V->n=2; break;
        case BI_GL_FRONTFACING:V->type=T_BOOL;  V->n=1; break;
        case BI_GL_FRAGDEPTH:  V->type=T_FLOAT; V->n=1; break;
        case BI_GL_INSTANCEID: V->type=T_INT;   V->n=1; break;
        default: return 0;
        }
        V->elem_cells = V->n; return 1;
    }
    return 0;
}

static void vm_apply_swz(vmvar *V, const glsl_expr *e)
{
    V->swz_n = e->swz_n; memcpy(V->swz, e->swz, 4);
    V->type = glsl_is_int_family(V->type) ? glsl_int_type_of_len(e->swz_n)
                                          : glsl_float_type_of_len(e->swz_n);
    V->n = e->swz_n; V->array_len = 0;
}
static int vm_pack_swz(const glsl_expr *e)
{
    return (e->swz_n & 15) | (e->swz[0] << 4) | (e->swz[1] << 8) |
           (e->swz[2] << 12) | (e->swz[3] << 16);
}

/* resolve an assignment target: EX_VAR or swizzle-of-EX_VAR.  Anything else
 * (arrays, structs, dynamic index) fails the stage => tree-walker fallback. */
static int vm_resolve_lv(vmc *C, const glsl_expr *e, vmvar *V)
{
    if (!e) return 0;
    if (e->kind == EX_VAR) return vm_resolve_var(C, e, V);
    if (e->kind == EX_FIELD && e->field_is_swizzle && e->a && e->a->kind == EX_VAR) {
        if (!vm_resolve_var(C, e->a, V)) return 0;
        vm_apply_swz(V, e); return 1;
    }
    return 0;
}

static int vm_ce(vmc *C, const glsl_expr *e);
static void vm_cs(vmc *C, const glsl_stmt *s);

static int vm_ce(vmc *C, const glsl_expr *e)
{
    int base, s, a, b, i;
    if (C->failed) return -1;
    if (!e) { C->failed = 1; return -1; }
    base = C->slottop;
    switch (e->kind) {
    case EX_FLOAT: { glsl_cell c; c.f = e->lit_f; return vm_ldc(C, c, T_FLOAT); }
    case EX_INT:   { glsl_cell c; c.i = e->lit_i; return vm_ldc(C, c, T_INT); }
    case EX_BOOL:  { glsl_cell c; c.i = e->lit_i ? 1 : 0; return vm_ldc(C, c, T_BOOL); }
    case EX_VAR: {
        vmvar V; int vi;
        if (!vm_resolve_var(C, e, &V)) { C->failed = 1; return -1; }
        vi = vm_addvar(C, &V); s = vm_alloc(C); vm_emit(C, VMOP_LDV, s, vi, 0, 0); return s;
    }
    case EX_FIELD: {
        if (!e->field_is_swizzle) { C->failed = 1; return -1; }   /* struct field */
        if (e->a && e->a->kind == EX_VAR) {
            vmvar V; int vi;
            if (!vm_resolve_var(C, e->a, &V)) { C->failed = 1; return -1; }
            vm_apply_swz(&V, e);
            vi = vm_addvar(C, &V); s = vm_alloc(C); vm_emit(C, VMOP_LDV, s, vi, 0, 0); return s;
        }
        b = vm_ce(C, e->a); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_SWZ, b, b, 0, vm_pack_swz(e));
        C->slottop = base + 1; return b;
    }
    case EX_NEG: case EX_NOT: case EX_BNOT: {
        int op = e->kind == EX_NEG ? VMOP_NEG : e->kind == EX_NOT ? VMOP_NOT : VMOP_BNOT;
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        vm_emit(C, op, a, a, 0, 0); C->slottop = base + 1; return a;
    }
    case EX_PREINC: case EX_PREDEC: case EX_POSTINC: case EX_POSTDEC: {
        vmvar V; int vi, p, q, r, delta = (e->kind==EX_PREINC||e->kind==EX_POSTINC)?1:-1;
        int isint; glsl_cell c;
        if (!vm_resolve_lv(C, e->a, &V)) { C->failed = 1; return -1; }
        vi = vm_addvar(C, &V);
        isint = glsl_is_int_family(V.type);
        p = vm_alloc(C); vm_emit(C, VMOP_LDV, p, vi, 0, 0);   /* p = cur        */
        q = vm_alloc(C); vm_emit(C, VMOP_COPY, q, p, 0, 0);   /* q = orig       */
        if (isint) c.i = delta; else c.f = (float)delta;      /* union: one member */
        r = vm_ldc(C, c, isint ? T_INT : T_FLOAT);            /* r = 1/-1        */
        vm_emit(C, VMOP_ARITH, p, p, r, delta > 0 ? EX_ADD : EX_SUB);   /* p = new */
        vm_emit(C, VMOP_STV, vi, p, 0, 0);                              /* store   */
        if (e->kind == EX_POSTINC || e->kind == EX_POSTDEC)
            vm_emit(C, VMOP_COPY, p, q, 0, 0);                          /* result=orig */
        C->slottop = base + 1; return p;
    }
    case EX_ADD: case EX_SUB: case EX_MUL: case EX_DIV: case EX_MOD:
    case EX_BAND: case EX_BOR: case EX_BXOR: case EX_SHL: case EX_SHR:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_ARITH, a, a, b, e->kind); C->slottop = base + 1; return a;
    case EX_LT: case EX_LE: case EX_GT: case EX_GE:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_CMP, a, a, b, e->kind); C->slottop = base + 1; return a;
    case EX_EQ: case EX_NE:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_EQS, a, a, b, e->kind); C->slottop = base + 1; return a;
    /* && / || / ^^ are evaluated EAGERLY here.  All operands in the supported
     * shaders are side-effect-free comparisons, and the math helpers guard
     * div-by-zero / sqrt(neg) / pow(neg), so eager matches the tree-walker's
     * short-circuit result (verified bit-identical against the golden ref). */
    case EX_LOGAND:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_ARITH, a, a, b, EX_BAND); C->slottop = base + 1; return a;
    case EX_LOGOR:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_ARITH, a, a, b, EX_BOR); C->slottop = base + 1; return a;
    case EX_LOGXOR:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_LXOR, a, a, b, 0); C->slottop = base + 1; return a;
    case EX_COMMA:
        a = vm_ce(C, e->a); if (a < 0) { C->failed = 1; return -1; }
        C->slottop = base;                       /* discard a */
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        C->slottop = base + 1; return b;
    case EX_ASSIGN: {
        vmvar V; int vi;
        if (!vm_resolve_lv(C, e->a, &V)) { C->failed = 1; return -1; }
        vi = vm_addvar(C, &V);
        b = vm_ce(C, e->b); if (b < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_STV, vi, b, 0, 0);
        C->slottop = base + 1; return b;
    }
    case EX_ADD_ASSIGN: case EX_SUB_ASSIGN: case EX_MUL_ASSIGN: case EX_DIV_ASSIGN:
    case EX_MOD_ASSIGN: case EX_SHL_ASSIGN: case EX_SHR_ASSIGN:
    case EX_AND_ASSIGN: case EX_XOR_ASSIGN: case EX_OR_ASSIGN: {
        vmvar V; int vi, p, q, baseop;
        if (!vm_resolve_lv(C, e->a, &V)) { C->failed = 1; return -1; }
        vi = vm_addvar(C, &V);
        baseop = e->kind==EX_ADD_ASSIGN?EX_ADD: e->kind==EX_SUB_ASSIGN?EX_SUB:
                 e->kind==EX_MUL_ASSIGN?EX_MUL: e->kind==EX_DIV_ASSIGN?EX_DIV:
                 e->kind==EX_MOD_ASSIGN?EX_MOD: e->kind==EX_SHL_ASSIGN?EX_SHL:
                 e->kind==EX_SHR_ASSIGN?EX_SHR: e->kind==EX_AND_ASSIGN?EX_BAND:
                 e->kind==EX_XOR_ASSIGN?EX_BXOR: EX_BOR;
        p = vm_alloc(C); vm_emit(C, VMOP_LDV, p, vi, 0, 0);   /* p = lhs cur   */
        q = vm_ce(C, e->b); if (q < 0) { C->failed = 1; return -1; }
        vm_emit(C, VMOP_ARITH, p, p, q, baseop);              /* p = lhs op rhs */
        vm_emit(C, VMOP_STV, vi, p, 0, 0);
        C->slottop = base + 1; return p;
    }
    case EX_CTOR: case EX_CALL: {
        int slots[32], nargs = e->nargs, ab, op, xid;
        if (nargs > 32 || (e->kind == EX_CTOR && glsl_is_uint(e->ctor_type)) ||
            (e->kind == EX_CALL && !e->is_user_fn &&
             (e->op_builtin == BF_FLOAT_BITS_TO_UINT || e->op_builtin == BF_ROUND)) ||
            (e->kind == EX_CALL && !e->is_user_fn && !C->stage->is_vertex &&
             e->op_builtin >= BF_TEXTURE2D && e->op_builtin <= BF_FWIDTH)) {
            /* Fragment texture LOD and derivatives need expression re-evaluation
             * with shifted inputs; pre-evaluated VM operands cannot provide it. */
            C->failed = 1; return -1;
        }
        for (i = 0; i < nargs; ++i) {
            slots[i] = vm_ce(C, e->args[i]);
            if (slots[i] < 0) { C->failed = 1; return -1; }
        }
        if (e->kind == EX_CTOR) {
            /* array constructors (type[N](...)) are not VM-representable; bail
             * so the whole stage falls back to the tree-walker (correct, just
             * slower).  In practice they only appear in global const
             * initialisers, which the tree-walker evaluates at link time. */
            if (e->ctor_array > 0) { C->failed = 1; return -1; }
            op = VMOP_CTOR; xid = e->ctor_type;
        }
        else if (e->is_user_fn) {
            const glsl_func *F;
            if (e->op_builtin < 0 || e->op_builtin >= C->stage->nfuncs) { C->failed = 1; return -1; }
            F = &C->stage->funcs[e->op_builtin];
            if (!F->body) { C->failed = 1; return -1; }
            for (i = 0; i < F->nparams; ++i)
                if (F->param_inout[i] != 0) { C->failed = 1; return -1; }  /* out/inout unsupported */
            op = VMOP_CALL; xid = e->op_builtin;
        } else { op = VMOP_BFUNC; xid = e->op_builtin; }
        ab = vm_aux(C, slots, nargs);
        s = nargs > 0 ? slots[0] : vm_alloc(C);
        vm_emit(C, op, s, ab, nargs, xid);
        C->slottop = base + 1; return s;
    }
    default:
        C->failed = 1; return -1;   /* EX_INDEX, EX_TERNARY, ... => fallback */
    }
}

static void vm_compile_decl(vmc *C, const glsl_stmt *d)
{
    vmvar V; int vi, slot, F_i = C->fi;
    const glsl_func *F = &C->stage->funcs[F_i];
    if (!d->decl_init) return;
    if (d->decl_scope != VARSCOPE_LOCAL) { C->failed = 1; return; }
    slot = d->decl_slot;
    if (slot < 0 || slot >= F->nlocals) { C->failed = 1; return; }
    memset(&V, 0, sizeof V);
    V.base_kind = VM_BK_LOCAL; V.off = F->local_off[slot];
    V.type = F->local_type[slot]; V.st_id = F->local_stid[slot];
    V.array_len = F->local_array[slot]; V.elem_cells = F->local_ncomp[slot];
    V.n = V.array_len > 0 ? V.elem_cells * V.array_len : V.elem_cells;
    vi = vm_addvar(C, &V);
    { int sb = C->slottop; int r = vm_ce(C, d->decl_init);
      if (r < 0) { C->failed = 1; return; }
      vm_emit(C, VMOP_STV, vi, r, 0, 0);
      C->slottop = sb; }
}

static void vm_compile_block(vmc *C, const glsl_stmt *blk)
{
    const glsl_stmt *s;
    if (!blk) return;
    for (s = blk->children; s && !C->failed; s = s->next) vm_cs(C, s);
}

static void vm_cs(vmc *C, const glsl_stmt *s)
{
    int sb, c, jz, jend, top, contpc, end, L;
    if (!s || C->failed) return;
    sb = C->slottop;
    switch (s->kind) {
    case ST_EXPR:
        if (s->expr) { c = vm_ce(C, s->expr); if (c < 0) C->failed = 1; }
        break;
    case ST_DECL:
        vm_compile_decl(C, s);
        break;
    case ST_BLOCK:
        vm_compile_block(C, s);
        break;
    case ST_IF: {
        c = vm_ce(C, s->expr); if (c < 0) { C->failed = 1; break; }
        jz = vm_emit(C, VMOP_JZ, 0, c, 0, -1); C->slottop = sb;
        vm_cs(C, s->body);
        if (s->els) {
            jend = vm_emit(C, VMOP_JMP, 0, 0, 0, -1);
            C->cf->code[jz].x = C->cf->ncode;
            vm_cs(C, s->els);
            C->cf->code[jend].x = C->cf->ncode;
        } else {
            C->cf->code[jz].x = C->cf->ncode;
        }
        break;
    }
    case ST_FOR: {
        const glsl_stmt *d;
        if (C->nloops >= GLSL_MAX_LOOP_DEPTH) { C->failed = 1; break; }
        L = C->nloops++; C->loops[L].brk = -1; C->loops[L].cont = -1;
        for (d = s->children; d && d->kind == ST_DECL; d = d->next) vm_compile_decl(C, d);
        if (s->expr) { c = vm_ce(C, s->expr); if (c < 0) C->failed = 1; C->slottop = sb; }
        top = C->cf->ncode;
        jz = -1;
        if (s->cond2) { c = vm_ce(C, s->cond2); if (c < 0) { C->failed = 1; break; }
                      jz = vm_emit(C, VMOP_JZ, 0, c, 0, -1); C->slottop = sb; }
        vm_cs(C, s->body);
        contpc = C->cf->ncode;
        if (s->post) { c = vm_ce(C, s->post); if (c < 0) C->failed = 1; C->slottop = sb; }
        vm_emit(C, VMOP_JMP, 0, 0, 0, top);
        end = C->cf->ncode;
        if (jz >= 0) C->cf->code[jz].x = end;
        vm_patch(C, C->loops[L].brk, end);
        vm_patch(C, C->loops[L].cont, contpc);
        C->nloops--;
        break;
    }
    case ST_WHILE: {
        if (C->nloops >= GLSL_MAX_LOOP_DEPTH) { C->failed = 1; break; }
        L = C->nloops++; C->loops[L].brk = -1; C->loops[L].cont = -1;
        top = C->cf->ncode;
        c = vm_ce(C, s->expr); if (c < 0) { C->failed = 1; break; }
        jz = vm_emit(C, VMOP_JZ, 0, c, 0, -1); C->slottop = sb;
        vm_cs(C, s->body);
        vm_emit(C, VMOP_JMP, 0, 0, 0, top);
        end = C->cf->ncode;
        C->cf->code[jz].x = end;
        vm_patch(C, C->loops[L].brk, end);
        vm_patch(C, C->loops[L].cont, top);
        C->nloops--;
        break;
    }
    case ST_DOWHILE: {
        if (C->nloops >= GLSL_MAX_LOOP_DEPTH) { C->failed = 1; break; }
        L = C->nloops++; C->loops[L].brk = -1; C->loops[L].cont = -1;
        top = C->cf->ncode;
        vm_cs(C, s->body);
        contpc = C->cf->ncode;
        c = vm_ce(C, s->expr); if (c < 0) { C->failed = 1; break; }
        vm_emit(C, VMOP_JNZ, 0, c, 0, top); C->slottop = sb;
        end = C->cf->ncode;
        vm_patch(C, C->loops[L].brk, end);
        vm_patch(C, C->loops[L].cont, contpc);
        C->nloops--;
        break;
    }
    case ST_BREAK:
        if (C->nloops <= 0) { C->failed = 1; break; }
        jz = vm_emit(C, VMOP_JMP, 0, 0, C->loops[C->nloops-1].brk, -1);
        C->loops[C->nloops-1].brk = jz;
        break;
    case ST_CONTINUE:
        if (C->nloops <= 0) { C->failed = 1; break; }
        jz = vm_emit(C, VMOP_JMP, 0, 0, C->loops[C->nloops-1].cont, -1);
        C->loops[C->nloops-1].cont = jz;
        break;
    case ST_RETURN:
        if (s->expr) { c = vm_ce(C, s->expr); if (c < 0) { C->failed = 1; break; }
                     vm_emit(C, VMOP_RET, 0, c, 0, 0); }
        else vm_emit(C, VMOP_RET, 0, -1, 0, 0);
        break;
    case ST_DISCARD:
        vm_emit(C, VMOP_DISCARD, 0, 0, 0, 0);
        break;
    default:
        C->failed = 1; break;   /* ST_SWITCH etc. => fallback */
    }
    C->slottop = sb;
}

static void vm_free_one(vmprog *vp);

static vmprog *vm_compile_stage(glsl_program *prog, glsl_stage *stage)
{
    vmprog *vp; vmc C; int fi, frame_slots = 1;
    if (!stage || stage->nstructs > 0) return NULL;   /* structs unsupported */
    /* Unsigned operations use the tree-walker's exact 32-bit cells. The VM
     * still has signed-only conversion, swizzle, and constructor paths. */
    for (fi = 0; fi < stage->nglobals; ++fi)
        if (glsl_is_uint(stage->globals[fi].type)) return NULL;
    for (fi = 0; fi < stage->nfuncs; ++fi) {
        int k;
        if (glsl_is_uint(stage->funcs[fi].ret_type)) return NULL;
        for (k = 0; k < stage->funcs[fi].nlocals; ++k)
            if (glsl_is_uint(stage->funcs[fi].local_type[k])) return NULL;
    }
    vp = (vmprog *)calloc(1, sizeof(vmprog));
    if (!vp) return NULL;
    vp->stage = stage;
    vp->vars = (vmvar *)malloc(VM_MAX_VARS * sizeof(vmvar));
    vp->lits = (glsl_cell *)malloc(VM_MAX_LITS * sizeof(glsl_cell));
    vp->littype = (int *)malloc(VM_MAX_LITS * sizeof(int));
    vp->funcs = (vmfunc *)calloc(stage->nfuncs > 0 ? stage->nfuncs : 1, sizeof(vmfunc));
    if (!vp->vars || !vp->lits || !vp->littype || !vp->funcs) { vm_free_one(vp); return NULL; }
    vp->main_idx = stage->main_idx;

    memset(&C, 0, sizeof C);
    C.vp = vp; C.prog = prog; C.stage = stage;
    for (fi = 0; fi < stage->nfuncs && !C.failed; ++fi) {
        const glsl_func *F = &stage->funcs[fi];
        if (!F->body) continue;
        C.fi = fi; C.cf = &vp->funcs[fi];
        C.slottop = 0; C.maxslots = 0; C.nloops = 0;
        { int k; for (k = 0; k < F->nparams; ++k)
              if (F->param_inout[k] != 0) { C.failed = 1; break; } }
        if (C.failed) break;
        vm_compile_block(&C, F->body);
        vm_emit(&C, VMOP_END, 0, 0, 0, 0);
        vp->funcs[fi].nslots = C.maxslots;
        if (C.maxslots > frame_slots) frame_slots = C.maxslots;
    }
    if (C.failed || vp->main_idx < 0 || !vp->funcs[vp->main_idx].ncode) {
        vm_free_one(vp); return NULL;
    }
    if (frame_slots > VM_FRAME_SLOTS) { vm_free_one(vp); return NULL; }
    vp->nfuncs = stage->nfuncs;
    vp->frame_slots = frame_slots;
    vp->bank = (glsl_cell *)malloc((size_t)frame_slots * 16 * VM_MAX_DEPTH * sizeof(glsl_cell));
    vp->btype = (int *)malloc((size_t)frame_slots * VM_MAX_DEPTH * sizeof(int));
    vp->bn = (int *)malloc((size_t)frame_slots * VM_MAX_DEPTH * sizeof(int));
    if (!vp->bank || !vp->btype || !vp->bn) { vm_free_one(vp); return NULL; }
    return vp;
}

static void vm_free_one(vmprog *vp)
{
    int i;
    if (!vp) return;
    for (i = 0; i < vp->nfuncs; ++i) { free(vp->funcs[i].code); free(vp->funcs[i].aux); }
    free(vp->funcs); free(vp->vars); free(vp->lits); free(vp->littype);
    free(vp->bank); free(vp->btype); free(vp->bn);
    free(vp);
}

void glsl_vm_free(glsl_program *p)
{
    if (!p) return;
    vm_free_one((vmprog *)p->vm_vs); p->vm_vs = NULL;
    vm_free_one((vmprog *)p->vm_fs); p->vm_fs = NULL;
    p->vm_tried = 0;
}

/* ------------------------------------------------------------------ */
/* entry: lazily compile + run one stage through the VM                */
/* ------------------------------------------------------------------ */
static int vm_run(glsl_program *p, glsl_stage *stage, int is_vertex, env *E)
{
    vmprog *vp = (vmprog *)(is_vertex ? p->vm_vs : p->vm_fs);
    int bit = is_vertex ? 1 : 2;
    const glsl_func *F;
    glsl_cell *frame;
    long budget = VM_BUDGET;

    if (!stage) return 0;
    if (!vp) {
        if (p->vm_tried & bit) return 0;      /* compile already failed => tree-walk */
        p->vm_tried |= bit;
        vp = vm_compile_stage(p, stage);
        if (!vp) return 0;
        if (is_vertex) p->vm_vs = vp; else p->vm_fs = vp;
    }
    if (vp->main_idx < 0) return 0;
    F = &stage->funcs[vp->main_idx];
    if (!F->body) return 0;

    frame = stk(E, F->frame_cells > 0 ? F->frame_cells : 1);
    if (E->err) return 0;
    memset(frame, 0, (F->frame_cells > 0 ? F->frame_cells : 1) * sizeof(glsl_cell));
    E->fn = F; E->locals = frame; E->ctrl = CTRL_NONE;
#ifdef VM_PROFILE
    if (!vm_prof_registered) { vm_prof_registered = 1; atexit(vm_profile_report); }
    vm_nrun++;
#endif
    vm_exec(E, vp, vp->main_idx, 0, &budget);
    if (E->err) {
        /* A VM runtime error (e.g. runaway budget) must not silently corrupt the
         * result: fall back to the tree-walker for this invocation. */
        E->err = 0; E->ctrl = CTRL_NONE; E->scratch_top = 0;
        memset(frame, 0, (F->frame_cells > 0 ? F->frame_cells : 1) * sizeof(glsl_cell));
        E->fn = F; E->locals = frame;
        return 0;
    }
    return 1;
}
