/*
 * clc_interp.c - the tree-walking interpreter that executes the OpenCL-C AST on
 * the CPU device.  Every variable lives in real typed host memory (a per-call
 * frame byte buffer for locals, the cl_mem host store for __global buffers, the
 * per-work-group block for __local), so loads/stores and pointer arithmetic use
 * the pointee's true size.  clc_value is used only for expression temporaries.
 *
 * Execution is serial: cpu_device.c calls clc_run_kernel() once per work-item,
 * in global-id order.  barrier() is a no-op memory fence, which is correct for
 * serial execution (all work-items of a group run to completion in order).
 */
#include "clc.h"
#include "clc_interp.h"
#include "clc_builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* implemented in clc_builtin_impl.c */
int clc_exec_builtin(int id, const clc_value *args, int nargs, clc_value *out,
                     clc_exec_ctx *ctx, clc_type out_type, const char *name);

typedef enum { CTL_NORMAL = 0, CTL_RETURN, CTL_BREAK, CTL_CONTINUE } ctl_t;

typedef struct Frame {
    clc_function   *fn;
    unsigned char  *mem;
    struct Frame   *prev;
} Frame;

typedef struct Interp {
    clc_program  *prog;
    clc_exec_ctx *ctx;
    Frame        *frame;
    clc_value     retval;
    int           fault;
    int           depth;
} Interp;

#define MAX_CALL_DEPTH 128

/* ---- small type helpers -------------------------------------------------- */
static int elem_size(clc_type t)
{
    clc_type s = t;
    if (t.pointer) return (int)sizeof(void *);
    s.vec = 1;
    {
        int n = clc_type_size(&s);
        return n > 0 ? n : 1;
    }
}
static int lane_count(clc_type t) { return t.vec > 1 ? t.vec : 1; }
static clc_type pointee_type(clc_type t) { clc_type p = t; p.pointer = 0; return p; }
static int pointee_size(clc_type t) { return elem_size(pointee_type(t)); }

static long long mask_int(long long v, clc_type t)
{
    int es;
    if (clc_type_is_float(&t)) return v;
    es = elem_size(t);
    switch (es) {
    case 1: return clc_type_is_signed(&t) ? (long long)(signed char)v : (long long)(unsigned char)v;
    case 2: return clc_type_is_signed(&t) ? (long long)(short)v : (long long)(unsigned short)v;
    case 4: return clc_type_is_signed(&t) ? (long long)(int)v : (long long)(unsigned int)v;
    default: return v;
    }
}

static long long read_int(const void *addr, int size, int is_signed)
{
    switch (size) {
    case 1: return is_signed ? (long long)(*(const signed char *)addr) : (long long)(*(const unsigned char *)addr);
    case 2: return is_signed ? (long long)(*(const short *)addr) : (long long)(*(const unsigned short *)addr);
    case 4: return is_signed ? (long long)(*(const int *)addr) : (long long)(*(const unsigned int *)addr);
    case 8: return *(const long long *)addr;
    default: return 0;
    }
}
static void write_int(void *addr, int size, long long v)
{
    switch (size) {
    case 1: *(signed char *)addr = (signed char)v; break;
    case 2: *(short *)addr = (short)v; break;
    case 4: *(int *)addr = (int)v; break;
    case 8: *(long long *)addr = v; break;
    default: break;
    }
}

/* ---- value access -------------------------------------------------------- */
static double lane_d(const clc_value *v, int l)
{
    if (clc_type_is_float(&v->type)) return v->f[l];
    return (double)v->i[l];
}
static long long lane_i(const clc_value *v, int l)
{
    if (clc_type_is_float(&v->type)) return (long long)v->f[l];
    return v->i[l];
}

static clc_value conv(clc_value v, clc_type t)
{
    clc_value o;
    int sv, dv, vf, df, l;
    memset(&o, 0, sizeof(o));
    o.type = t;
    if (t.pointer) { o.i[0] = v.i[0]; return o; }
    if (t.base == CLC_STRUCT) { o.i[0] = v.i[0]; return o; }
    sv = lane_count(v.type);
    dv = lane_count(t);
    vf = clc_type_is_float(&v.type);
    df = clc_type_is_float(&t);
    for (l = 0; l < dv; l++) {
        int sl = (sv == 1) ? 0 : (l < sv ? l : 0);
        if (df) {
            o.f[l] = vf ? v.f[sl] : (double)v.i[sl];
        } else {
            long long iv = vf ? (long long)v.f[sl] : v.i[sl];
            o.i[l] = mask_int(iv, t);
        }
    }
    return o;
}

static clc_value load_value(const void *addr, clc_type t)
{
    clc_value v;
    int lanes, es, l;
    memset(&v, 0, sizeof(v));
    v.type = t;
    if (t.pointer) { v.i[0] = (long long)(uintptr_t)(*(const uintptr_t *)addr); return v; }
    if (t.base == CLC_STRUCT) { v.i[0] = (long long)(uintptr_t)addr; return v; }
    lanes = lane_count(t);
    es = elem_size(t);
    if (clc_type_is_float(&t)) {
        for (l = 0; l < lanes; l++)
            v.f[l] = (t.base == CLC_DOUBLE) ? ((const double *)addr)[l] : ((const float *)addr)[l];
    } else {
        int sg = clc_type_is_signed(&t);
        for (l = 0; l < lanes; l++)
            v.i[l] = read_int((const char *)addr + l * es, es, sg);
    }
    return v;
}

static void store_value(void *addr, clc_type t, clc_value v)
{
    int lanes, es, l;
    v = conv(v, t);
    if (t.pointer) { *(uintptr_t *)addr = (uintptr_t)v.i[0]; return; }
    if (t.base == CLC_STRUCT) { memcpy(addr, (void *)(uintptr_t)v.i[0], clc_type_size(&t)); return; }
    lanes = lane_count(t);
    es = elem_size(t);
    if (clc_type_is_float(&t)) {
        for (l = 0; l < lanes; l++) {
            if (t.base == CLC_DOUBLE) ((double *)addr)[l] = v.f[l];
            else ((float *)addr)[l] = (float)v.f[l];
        }
    } else {
        for (l = 0; l < lanes; l++)
            write_int((char *)addr + l * es, es, v.i[l]);
    }
}

static clc_value val_int(long long x, clc_type t)
{
    clc_value v; memset(&v, 0, sizeof(v)); v.type = t; v.i[0] = mask_int(x, t);
    v.f[0] = (double)v.i[0];
    return v;
}
static clc_value val_float(double x, clc_type t)
{
    clc_value v; memset(&v, 0, sizeof(v)); v.type = t; v.f[0] = x; v.i[0] = (long long)x;
    return v;
}
static clc_value scalar_int(long long x) { return val_int(x, (clc_type){CLC_INT,1,0,CLC_AS_PRIVATE,NULL}); }

static int truthy(const clc_value *v)
{
    if (clc_type_is_float(&v->type)) return v->f[0] != 0.0;
    return v->i[0] != 0;
}

static clc_type rt_bin_type(clc_type a, clc_type b)
{
    clc_type r;
    int vec = a.vec > b.vec ? a.vec : b.vec;
    memset(&r, 0, sizeof(r));
    r.vec = vec;
    if (a.pointer) return a;
    if (b.pointer) return b;
    if (clc_type_is_float(&a) || clc_type_is_float(&b)) {
        r.base = (a.base == CLC_DOUBLE || b.base == CLC_DOUBLE) ? CLC_DOUBLE : CLC_FLOAT;
        return r;
    }
    {
        int wa = elem_size(a), wb = elem_size(b);
        int au = !clc_type_is_signed(&a), bu = !clc_type_is_signed(&b);
        if (wa == wb) r.base = (au || bu) ? (wa >= 8 ? CLC_ULONG : CLC_UINT)
                                          : (wa >= 8 ? CLC_LONG : CLC_INT);
        else if (wa > wb) r.base = a.base;
        else r.base = b.base;
    }
    return r;
}

/* ---- forward decls ------------------------------------------------------- */
static clc_value eval_expr(Interp *in, clc_expr *e);
static int  eval_lvalue(Interp *in, clc_expr *e, void **addr, clc_type *type);
static ctl_t exec_stmt(Interp *in, clc_stmt *s);

/* ---- lvalue -------------------------------------------------------------- */
static int eval_lvalue(Interp *in, clc_expr *e, void **addr, clc_type *type)
{
    switch (e->kind) {
    case E_VAR: {
        if (e->is_global) {
            clc_global *g = &in->prog->globals[e->gindex];
            *addr = g->data;
            *type = g->type;
            if (g->is_array) { type->pointer = 1; }
            return 0;
        } else {
            clc_symbol *s = &in->frame->fn->syms[e->sym];
            if (s->type.as == CLC_AS_LOCAL && !s->type.pointer)
                *addr = (char *)in->ctx->local_mem + s->offset;
            else
                *addr = in->frame->mem + s->offset;
            *type = s->type;
            return 0;
        }
    }
    case E_INDEX: {
        clc_value bv = eval_expr(in, e->a);
        clc_value iv = eval_expr(in, e->b);
        uintptr_t base = (uintptr_t)bv.i[0];
        int esz = pointee_size(bv.type);
        *addr = (void *)(base + (uintptr_t)(lane_i(&iv, 0) * (long long)esz));
        *type = pointee_type(bv.type);
        return 0;
    }
    case E_FIELD: {
        clc_value sv = eval_expr(in, e->a);
        clc_struct *st = e->a->type.str;
        uintptr_t base = (uintptr_t)sv.i[0];
        *addr = (void *)(base + (uintptr_t)st->fields[e->field_index].offset);
        *type = st->fields[e->field_index].type;
        return 0;
    }
    case E_UNOP:
        if (e->op == OP_DEREF) {
            clc_value pv = eval_expr(in, e->a);
            *addr = (void *)(uintptr_t)pv.i[0];
            *type = pointee_type(e->a->type);
            return 0;
        }
        return -1;
    default:
        return -1;
    }
}

/* ---- binary/unary arithmetic -------------------------------------------- */
static clc_value do_binop(Interp *in, clc_op op, clc_value a, clc_value b, clc_type ta, clc_type tb)
{
    clc_value o;
    clc_type rt;
    int lanes, l, af, bf;
    memset(&o, 0, sizeof(o));
    (void)in;

    /* pointer arithmetic */
    if ((ta.pointer || tb.pointer) &&
        (op==OP_ADD||op==OP_SUB||op==OP_LT||op==OP_LE||op==OP_GT||op==OP_GE||op==OP_EQ||op==OP_NE)) {
        if (op == OP_ADD) {
            if (ta.pointer) { uintptr_t p=(uintptr_t)a.i[0]; int es=pointee_size(ta); o.type=ta; o.i[0]=(long long)(p + (uintptr_t)(lane_i(&b,0)*(long long)es)); return o; }
            else { uintptr_t p=(uintptr_t)b.i[0]; int es=pointee_size(tb); o.type=tb; o.i[0]=(long long)(p + (uintptr_t)(lane_i(&a,0)*(long long)es)); return o; }
        } else if (op == OP_SUB) {
            if (ta.pointer && tb.pointer) { int es=pointee_size(ta); o.type=(clc_type){CLC_PTRDIFF_T,1,0,0,NULL}; o.i[0]=(long long)(((uintptr_t)a.i[0]-(uintptr_t)b.i[0])/(unsigned)es); return o; }
            if (ta.pointer) { uintptr_t p=(uintptr_t)a.i[0]; int es=pointee_size(ta); o.type=ta; o.i[0]=(long long)(p - (uintptr_t)(lane_i(&b,0)*(long long)es)); return o; }
        } else {
            /* relational on pointers */
            uintptr_t x=(uintptr_t)a.i[0], y=(uintptr_t)b.i[0];
            int res = op==OP_LT?(x<y):op==OP_LE?(x<=y):op==OP_GT?(x>y):op==OP_GE?(x>=y):op==OP_EQ?(x==y):(x!=y);
            o.type=(clc_type){CLC_INT,1,0,0,NULL}; o.i[0]=res; o.f[0]=res; return o;
        }
    }

    rt = rt_bin_type(ta, tb);
    lanes = lane_count(rt);
    af = clc_type_is_float(&ta);
    bf = clc_type_is_float(&tb);

    /* logical (scalar, short-circuit already handled by caller for && ||) */
    if (op==OP_LAND||op==OP_LOR) {
        int x = truthy(&a), y = truthy(&b);
        int r = (op==OP_LAND) ? (x&&y) : (x||y);
        o.type=(clc_type){CLC_INT,1,0,0,NULL}; o.i[0]=r; o.f[0]=r; return o;
    }

    if (op==OP_EQ||op==OP_NE||op==OP_LT||op==OP_LE||op==OP_GT||op==OP_GE) {
        int isfloat = clc_type_is_float(&rt);
        clc_type it; memset(&it,0,sizeof it); it.base=CLC_INT; it.vec=rt.vec;
        o.type = it;
        for (l = 0; l < lanes; l++) {
            int av = (ta.vec>1)? l : 0, bv=(tb.vec>1)? l:0;
            int res;
            if (isfloat || af || bf) {
                double x = af? a.f[av] : (double)a.i[av];
                double y = bf? b.f[bv] : (double)b.i[bv];
                res = op==OP_EQ?(x==y):op==OP_NE?(x!=y):op==OP_LT?(x<y):op==OP_LE?(x<=y):op==OP_GT?(x>y):(x>=y);
            } else {
                long long x = a.i[av], y = b.i[bv];
                int sgn = clc_type_is_signed(&ta) && clc_type_is_signed(&tb);
                if (!sgn) {
                    unsigned long long ux=(unsigned long long)x, uy=(unsigned long long)y;
                    res = op==OP_EQ?(ux==uy):op==OP_NE?(ux!=uy):op==OP_LT?(ux<uy):op==OP_LE?(ux<=uy):op==OP_GT?(ux>uy):(ux>=uy);
                } else {
                    res = op==OP_EQ?(x==y):op==OP_NE?(x!=y):op==OP_LT?(x<y):op==OP_LE?(x<=y):op==OP_GT?(x>y):(x>=y);
                }
            }
            if (rt.vec>1) o.i[l] = res ? -1 : 0;   /* vector: all-bits true */
            else { o.i[l] = res ? 1 : 0; }
        }
        return o;
    }

    o.type = rt;
    if (clc_type_is_float(&rt)) {
        for (l = 0; l < lanes; l++) {
            int av=(ta.vec>1)?l:0, bv=(tb.vec>1)?l:0;
            double x = af? a.f[av]:(double)a.i[av];
            double y = bf? b.f[bv]:(double)b.i[bv];
            double r = 0;
            switch (op) {
            case OP_ADD: r=x+y; break; case OP_SUB: r=x-y; break;
            case OP_MUL: r=x*y; break; case OP_DIV: r=x/y; break;
            case OP_MOD: r=fmod(x,y); break;
            default: r=0; break;
            }
            o.f[l]=r; o.i[l]=(long long)r;
        }
    } else {
        int sgn = clc_type_is_signed(&rt);
        for (l = 0; l < lanes; l++) {
            int av=(ta.vec>1)?l:0, bv=(tb.vec>1)?l:0;
            long long x=a.i[av], y=b.i[bv], r=0;
            unsigned long long ux=(unsigned long long)x, uy=(unsigned long long)y;
            switch (op) {
            case OP_ADD: r=sgn?(x+y):(long long)(ux+uy); break;
            case OP_SUB: r=sgn?(x-y):(long long)(ux-uy); break;
            case OP_MUL: r=sgn?(x*y):(long long)(ux*uy); break;
            case OP_DIV: r = (y==0)?0 : (sgn?(x/y):(long long)(ux/uy)); break;
            case OP_MOD: r = (y==0)?0 : (sgn?(x%y):(long long)(ux%uy)); break;
            case OP_BAND: r=(long long)(ux&uy); break;
            case OP_BOR:  r=(long long)(ux|uy); break;
            case OP_BXOR: r=(long long)(ux^uy); break;
            case OP_SHL:  r=(long long)(ux<<(uy&63)); break;
            case OP_SHR:  r= sgn? (x>>(y&63)) : (long long)(ux>>(uy&63)); break;
            default: r=0; break;
            }
            o.i[l]=mask_int(r,rt); o.f[l]=(double)o.i[l];
        }
    }
    return o;
}

/* ---- function calls ------------------------------------------------------ */
static int call_function(Interp *in, clc_function *fn, const clc_value *args, int nargs, clc_value *out)
{
    Frame fr;
    int i;
    ctl_t c;
    if (!fn || !fn->body) { in->fault = 1; return -1; }
    if (in->depth >= MAX_CALL_DEPTH) { in->fault = 1; return -1; }
    memset(&fr, 0, sizeof(fr));
    fr.fn = fn;
    fr.mem = (unsigned char *)calloc(1, fn->frame_size > 0 ? fn->frame_size : 1);
    if (!fr.mem) { in->fault = 1; return -1; }
    fr.prev = in->frame;
    for (i = 0; i < fn->nparams && i < nargs; i++) {
        clc_symbol *s = &fn->syms[i];
        store_value(fr.mem + s->offset, s->type, args[i]);
    }
    in->frame = &fr;
    in->depth++;
    memset(&in->retval, 0, sizeof(in->retval));
    in->retval.type = fn->ret;
    c = exec_stmt(in, fn->body);
    (void)c;
    if (out) *out = in->retval;
    in->depth--;
    in->frame = fr.prev;
    free(fr.mem);
    return in->fault;
}

/* ---- expression evaluation ---------------------------------------------- */
static clc_value eval_call(Interp *in, clc_expr *e)
{
    clc_value args[CLC_MAX_ARGS];
    clc_value out;
    int i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < e->nargs; i++) args[i] = eval_expr(in, e->args[i]);
    if (e->is_builtin) {
        clc_exec_builtin(e->builtin_id, args, e->nargs, &out, in->ctx, e->type, e->name);
        return out;
    }
    if (e->callee) {
        call_function(in, e->callee, args, e->nargs, &out);
        return out;
    }
    in->fault = 1;
    out.type = e->type;
    return out;
}

static clc_value eval_expr(Interp *in, clc_expr *e)
{
    clc_value v;
    memset(&v, 0, sizeof(v));
    if (!e) { v.type = (clc_type){CLC_INT,1,0,0,NULL}; return v; }
    switch (e->kind) {
    case E_CONST_INT:
        v.type = e->type; v.i[0] = mask_int(e->li, e->type); v.f[0] = (double)e->li; return v;
    case E_CONST_FLOAT:
        v.type = e->type; v.f[0] = e->ld; v.i[0] = (long long)e->ld; return v;
    case E_VAR: {
        void *addr; clc_type t;
        eval_lvalue(in, e, &addr, &t);
        if (e->is_global ? in->prog->globals[e->gindex].is_array
                         : in->frame->fn->syms[e->sym].is_array) {
            /* array decays to a pointer to its storage */
            clc_type pt = t; pt.pointer = 1;
            if (!(e->is_global) && in->frame->fn->syms[e->sym].type.as == CLC_AS_LOCAL) pt.as = CLC_AS_LOCAL;
            else pt.as = CLC_AS_PRIVATE;
            v.type = pt; v.i[0] = (long long)(uintptr_t)addr; return v;
        }
        if (t.as == CLC_AS_LOCAL && !t.pointer && !e->is_global)
            addr = (char *)in->ctx->local_mem + in->frame->fn->syms[e->sym].offset;
        return load_value(addr, t);
    }
    case E_INDEX: {
        void *addr; clc_type t;
        eval_lvalue(in, e, &addr, &t);
        return load_value(addr, t);
    }
    case E_FIELD: {
        void *addr; clc_type t;
        eval_lvalue(in, e, &addr, &t);
        return load_value(addr, t);
    }
    case E_SWIZZLE: {
        clc_value base = eval_expr(in, e->a);
        int l;
        v.type = e->type;
        for (l = 0; l < e->nswiz; l++) {
            int c = e->swizzle[l];
            if (clc_type_is_float(&base.type)) { v.f[l] = base.f[c]; v.i[l] = (long long)base.f[c]; }
            else { v.i[l] = base.i[c]; v.f[l] = (double)base.i[c]; }
        }
        if (e->nswiz == 1) v.type.vec = 1;
        return v;
    }
    case E_CAST: {
        clc_value a = eval_expr(in, e->a);
        return conv(a, e->type);
    }
    case E_COMPOUND: {
        v.type = e->type;
        if (e->type.base == CLC_STRUCT && e->type.str) {
            /* struct literal: allocate a temporary copy (frame-lived) */
            int sz = clc_type_size(&e->type);
            void *buf = calloc(1, sz > 0 ? sz : 1);
            int i;
            for (i = 0; i < e->ninit && i < e->type.str->nfields; i++) {
                clc_field *fl = &e->type.str->fields[i];
                clc_value fv = eval_expr(in, e->init[i]);
                store_value((char *)buf + fl->offset, fl->type, fv);
            }
            v.i[0] = (long long)(uintptr_t)buf; /* NOTE: leaked until program end (subset limit) */
            return v;
        }
        {
            int lanes = lane_count(e->type);
            int i;
            if (e->ninit == 1 && lanes > 1) {
                clc_value one = eval_expr(in, e->init[0]);
                for (i = 0; i < lanes; i++) {
                    if (clc_type_is_float(&e->type)) { v.f[i] = lane_d(&one, 0); }
                    else { v.i[i] = mask_int(lane_i(&one,0), e->type); }
                }
            } else {
                for (i = 0; i < lanes; i++) {
                    if (i < e->ninit) {
                        clc_value c = eval_expr(in, e->init[i]);
                        if (clc_type_is_float(&e->type)) v.f[i] = lane_d(&c, 0);
                        else v.i[i] = mask_int(lane_i(&c,0), e->type);
                    }
                }
            }
        }
        return v;
    }
    case E_TERNARY: {
        clc_value c = eval_expr(in, e->a);
        return truthy(&c) ? eval_expr(in, e->b) : eval_expr(in, e->c);
    }
    case E_UNOP: {
        clc_value a;
        void *addr; clc_type t;
        switch (e->op) {
        case OP_DEREF:
            eval_lvalue(in, e, &addr, &t);
            return load_value(addr, t);
        case OP_ADDR:
            if (eval_lvalue(in, e->a, &addr, &t) == 0) {
                clc_type pt = t; pt.pointer = 1;
                v.type = pt; v.i[0] = (long long)(uintptr_t)addr; return v;
            }
            in->fault = 1; v.type = e->type; return v;
        case OP_NEG:
            a = eval_expr(in, e->a);
            v.type = a.type;
            if (clc_type_is_float(&a.type)) { int l; for(l=0;l<lane_count(a.type);l++){v.f[l]=-a.f[l];v.i[l]=(long long)v.f[l];} }
            else { int l; for(l=0;l<lane_count(a.type);l++){v.i[l]=mask_int(-a.i[l],a.type);v.f[l]=(double)v.i[l];} }
            return v;
        case OP_BNOT:
            a = eval_expr(in, e->a);
            v.type = a.type;
            { int l; for(l=0;l<lane_count(a.type);l++){v.i[l]=mask_int(~a.i[l],a.type);v.f[l]=(double)v.i[l];} }
            return v;
        case OP_LNOT:
            a = eval_expr(in, e->a);
            return scalar_int(truthy(&a) ? 0 : 1);
        case OP_PRE_INC: case OP_PRE_DEC: {
            clc_value cur, delta, res;
            eval_lvalue(in, e->a, &addr, &t);
            cur = load_value(addr, t);
            if (t.pointer) delta = scalar_int(e->op==OP_PRE_INC?1:-1);
            else if (clc_type_is_float(&t)) delta = val_float(e->op==OP_PRE_INC?1.0:-1.0, t);
            else delta = val_int(e->op==OP_PRE_INC?1:-1, t);
            if (t.pointer) {
                uintptr_t p=(uintptr_t)cur.i[0]; int es=pointee_size(t);
                res=cur; res.i[0]=(long long)(p + (long long)lane_i(&delta,0)*es);
            } else res = do_binop(in, e->op==OP_PRE_INC?OP_ADD:OP_SUB, cur, delta, t, t);
            store_value(addr, t, res);
            return res;
        }
        case OP_POST_INC: case OP_POST_DEC: {
            clc_value cur, delta, res;
            eval_lvalue(in, e->a, &addr, &t);
            cur = load_value(addr, t);
            if (t.pointer) delta = scalar_int(e->op==OP_POST_INC?1:-1);
            else if (clc_type_is_float(&t)) delta = val_float(e->op==OP_POST_INC?1.0:-1.0, t);
            else delta = val_int(e->op==OP_POST_INC?1:-1, t);
            if (t.pointer) {
                uintptr_t p=(uintptr_t)cur.i[0]; int es=pointee_size(t);
                res=cur; res.i[0]=(long long)(p + (long long)lane_i(&delta,0)*es);
            } else res = do_binop(in, e->op==OP_POST_INC?OP_ADD:OP_SUB, cur, delta, t, t);
            store_value(addr, t, res);
            return cur;
        }
        default:
            in->fault = 1; v.type = e->type; return v;
        }
    }
    case E_POSTOP: {
        void *addr; clc_type t;
        clc_value cur, delta, res;
        eval_lvalue(in, e->a, &addr, &t);
        cur = load_value(addr, t);
        if (t.pointer) delta = scalar_int(e->op==OP_POST_INC?1:-1);
        else if (clc_type_is_float(&t)) delta = val_float(e->op==OP_POST_INC?1.0:-1.0, t);
        else delta = val_int(e->op==OP_POST_INC?1:-1, t);
        if (t.pointer) {
            uintptr_t p=(uintptr_t)cur.i[0]; int es=pointee_size(t);
            res=cur; res.i[0]=(long long)(p + (long long)lane_i(&delta,0)*es);
        } else res = do_binop(in, e->op==OP_POST_INC?OP_ADD:OP_SUB, cur, delta, t, t);
        store_value(addr, t, res);
        return cur;
    }
    case E_BINOP: {
        if (e->op == OP_LAND) {
            clc_value a = eval_expr(in, e->a);
            if (!truthy(&a)) return scalar_int(0);
            { clc_value b = eval_expr(in, e->b); return scalar_int(truthy(&b)?1:0); }
        }
        if (e->op == OP_LOR) {
            clc_value a = eval_expr(in, e->a);
            if (truthy(&a)) return scalar_int(1);
            { clc_value b = eval_expr(in, e->b); return scalar_int(truthy(&b)?1:0); }
        }
        {
            clc_value a = eval_expr(in, e->a);
            clc_value b = eval_expr(in, e->b);
            return do_binop(in, e->op, a, b, a.type, b.type);
        }
    }
    case E_ASSIGN: {
        void *addr; clc_type t;
        clc_value rv;
        if (e->a->kind == E_SWIZZLE) {
            /* vector swizzle assignment: v.xy = expr */
            void *vaddr; clc_type vtype;
            clc_type st;
            int k, esz;
            eval_lvalue(in, e->a->a, &vaddr, &vtype);
            esz = elem_size(vtype);
            st = vtype; st.vec = 1;
            rv = eval_expr(in, e->b);
            for (k = 0; k < e->a->nswiz; k++) {
                int comp = e->a->swizzle[k];
                clc_value one;
                memset(&one, 0, sizeof(one)); one.type = st;
                if (clc_type_is_float(&st)) { one.f[0] = lane_d(&rv, (rv.type.vec>1)?k:0); }
                else { one.i[0] = lane_i(&rv, (rv.type.vec>1)?k:0); }
                store_value((char *)vaddr + comp * esz, st, one);
            }
            return rv;
        }
        eval_lvalue(in, e->a, &addr, &t);
        rv = eval_expr(in, e->b);
        if (e->op != OP_ASSIGN) {
            clc_value cur = load_value(addr, t);
            clc_op bo = (e->op==OP_ADD_ASG)?OP_ADD:(e->op==OP_SUB_ASG)?OP_SUB:(e->op==OP_MUL_ASG)?OP_MUL:
                        (e->op==OP_DIV_ASG)?OP_DIV:(e->op==OP_MOD_ASG)?OP_MOD:(e->op==OP_BAND_ASG)?OP_BAND:
                        (e->op==OP_BOR_ASG)?OP_BOR:(e->op==OP_BXOR_ASG)?OP_BXOR:(e->op==OP_SHL_ASG)?OP_SHL:OP_SHR;
            rv = do_binop(in, bo, cur, rv, t, rv.type);
        }
        store_value(addr, t, rv);
        return load_value(addr, t);
    }
    case E_CALL:
        return eval_call(in, e);
    default:
        in->fault = 1;
        v.type = e->type;
        return v;
    }
}

/* ---- statements ---------------------------------------------------------- */
static ctl_t exec_block(Interp *in, clc_stmt *s)
{
    int i;
    for (i = 0; i < s->nbody; i++) {
        ctl_t c = exec_stmt(in, s->body[i]);
        if (c != CTL_NORMAL) return c;
    }
    return CTL_NORMAL;
}

static void init_decl(Interp *in, clc_stmt *s)
{
    clc_symbol *sym = &in->frame->fn->syms[s->sym];
    void *addr;
    if (sym->type.as == CLC_AS_LOCAL && !sym->type.pointer)
        addr = (char *)in->ctx->local_mem + sym->offset;
    else
        addr = in->frame->mem + sym->offset;
    if (!s->init) return;
    if (s->is_array && s->init->kind == E_COMPOUND) {
        int i, es = elem_size(sym->type);
        for (i = 0; i < s->init->ninit && i < s->array_len; i++) {
            clc_value ev = eval_expr(in, s->init->init[i]);
            store_value((char *)addr + i * es, sym->type, ev);
        }
    } else if (s->is_array) {
        /* scalar init of array: fill element 0 only (subset) */
        clc_value ev = eval_expr(in, s->init);
        store_value(addr, sym->type, ev);
    } else {
        clc_value ev = eval_expr(in, s->init);
        store_value(addr, sym->type, ev);
    }
}

static ctl_t exec_stmt(Interp *in, clc_stmt *s)
{
    if (!s || in->fault) return CTL_NORMAL;
    switch (s->kind) {
    case S_BLOCK:
        return exec_block(in, s);
    case S_DECL:
        init_decl(in, s);
        return CTL_NORMAL;
    case S_EXPR:
        if (s->expr) eval_expr(in, s->expr);
        return CTL_NORMAL;
    case S_RETURN:
        if (s->expr) in->retval = eval_expr(in, s->expr);
        return CTL_RETURN;
    case S_BREAK:
        return CTL_BREAK;
    case S_CONTINUE:
        return CTL_CONTINUE;
    case S_IF: {
        clc_value c = eval_expr(in, s->cond);
        if (truthy(&c)) return exec_stmt(in, s->thenb);
        else if (s->elseb) return exec_stmt(in, s->elseb);
        return CTL_NORMAL;
    }
    case S_WHILE: {
        for (;;) {
            clc_value c = eval_expr(in, s->cond);
            ctl_t r;
            if (!truthy(&c)) break;
            r = exec_stmt(in, s->thenb);
            if (r == CTL_BREAK) break;
            if (r == CTL_RETURN) return CTL_RETURN;
            if (in->fault) break;
        }
        return CTL_NORMAL;
    }
    case S_DOWHILE: {
        for (;;) {
            ctl_t r = exec_stmt(in, s->thenb);
            if (r == CTL_BREAK) break;
            if (r == CTL_RETURN) return CTL_RETURN;
            { clc_value c = eval_expr(in, s->cond); if (!truthy(&c)) break; }
            if (in->fault) break;
        }
        return CTL_NORMAL;
    }
    case S_FOR: {
        ctl_t r;
        if (s->sinit) exec_stmt(in, s->sinit);
        for (;;) {
            if (s->scond) { clc_value c = eval_expr(in, s->scond); if (!truthy(&c)) break; }
            r = exec_stmt(in, s->sbody);
            if (r == CTL_BREAK) break;
            if (r == CTL_RETURN) return CTL_RETURN;
            if (s->sinc) eval_expr(in, s->sinc);
            if (in->fault) break;
        }
        return CTL_NORMAL;
    }
    case S_SWITCH: {
        clc_value cv = eval_expr(in, s->cond);
        long long cval = lane_i(&cv, 0);
        clc_stmt *body = s->thenb;
        int i, start = -1, deflt = -1, n;
        ctl_t r = CTL_NORMAL;
        if (!body || body->kind != S_BLOCK) {
            /* single-statement switch body: wrap logically */
            return CTL_NORMAL;
        }
        n = body->nbody;
        for (i = 0; i < n; i++) {
            clc_stmt *cs = body->body[i];
            if (cs->kind == S_CASE && start < 0) {
                clc_value lv = eval_expr(in, cs->expr);
                if (lane_i(&lv, 0) == cval) start = i;
            } else if (cs->kind == S_DEFAULT && deflt < 0) {
                deflt = i;
            }
        }
        if (start < 0) start = deflt;
        if (start < 0) return CTL_NORMAL;
        for (i = start; i < n; i++) {
            clc_stmt *cs = body->body[i];
            if (cs->kind == S_CASE || cs->kind == S_DEFAULT) continue;
            r = exec_stmt(in, cs);
            if (r == CTL_BREAK) { r = CTL_NORMAL; break; }
            if (r == CTL_RETURN) return CTL_RETURN;
            if (in->fault) break;
        }
        return r;
    }
    case S_CASE:
    case S_DEFAULT:
        return CTL_NORMAL;
    default:
        return CTL_NORMAL;
    }
}

/* ---- kernel entry -------------------------------------------------------- */
int clc_run_kernel(clc_function *fn, const clc_value *params, int nparams, clc_exec_ctx *ctx)
{
    Interp in;
    memset(&in, 0, sizeof(in));
    in.prog = ctx->prog;
    in.ctx = ctx;
    in.retval.type = fn->ret;
    call_function(&in, fn, params, nparams, NULL);
    return in.fault;
}
