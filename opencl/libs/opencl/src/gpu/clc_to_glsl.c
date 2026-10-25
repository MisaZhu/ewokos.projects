/*
 * clc_to_glsl.c - the OpenCL-C -> GLSL-ES-3.10 compute transpiler (see
 * clc_to_glsl.h).  It walks the clc front-end AST for one kernel and emits a
 * verbatim compute shader the on-device mesa compiler accepts through
 * pglc_compile_compute_raw().
 *
 * Strategy: cover exactly the constructs the shipped demos need (float/int/uint
 * scalars and 2/3/4-vectors, __global/__constant buffer args as SSBOs, scalar
 * by-value args as default-block uniforms, get_global_id/size/offset, indexing,
 * arithmetic/comparison/logical operators, if/for/while, vloadN/vstoreN, fma and
 * the common math builtins) and DECLINE everything else.  A declined kernel is
 * run on the CPU device by gpu_device.c, so declining only ever costs speed.
 *
 * Identifier mapping: every symbol (parameter i is syms[i]; locals follow) is
 * renamed "s<sym-index>", which is collision-free and never a GLSL reserved
 * word.  Buffer args become std430 SSBO members, scalar args become uniforms of
 * the same "s<i>" name (gpu_device.c matches them by name through the
 * compiler's uniform reflection), and the host global offset/size ride in the
 * fixed u_goff{0,1,2}/u_gsz{0,1,2} int uniforms.
 */
#include "clc_to_glsl.h"
#include "clc/clc_builtins.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- emit context -------------------------------------------------------- */
typedef struct {
    char         *buf;
    size_t        len, cap;

    int           declined;
    char          err[192];

    clc_program  *prog;
    clc_function *fn;
    clc_glsl_info *info;
} gctx;

static int decl(gctx *c, const char *why)
{
    if (!c->declined) {
        c->declined = 1;
        snprintf(c->err, sizeof(c->err), "%s", why);
    }
    return -1;
}
#define DECLINE(why) return decl(c, (why))

/* growable printf; a no-op once declined so emitters can short-circuit */
static void gf(gctx *c, const char *fmt, ...)
{
    va_list ap;
    int need;

    if (c->declined)
        return;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { decl(c, "format error"); return; }

    if (c->len + (size_t)need + 1 > c->cap) {
        size_t nc = c->cap ? c->cap : 1024;
        char  *np;
        while (nc < c->len + (size_t)need + 1)
            nc *= 2;
        np = (char *)realloc(c->buf, nc);
        if (!np) { decl(c, "out of memory"); return; }
        c->buf = np;
        c->cap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(c->buf + c->len, c->cap - c->len, fmt, ap);
    va_end(ap);
    c->len += (size_t)need;
}

static void indent(gctx *c, int ind)
{
    int i;
    for (i = 0; i < ind; i++)
        gf(c, "    ");
}

/* ---- type helpers -------------------------------------------------------- */
static int is_float_t(clc_type t)
{
    return t.base == CLC_FLOAT || t.base == CLC_DOUBLE || t.base == CLC_HALF;
}
static int supported_scalar(clc_base b)
{
    return b == CLC_FLOAT || b == CLC_INT || b == CLC_UINT;
}

/* GLSL type name for a value type (float/vecN, int/ivecN, uint/uvecN,
 * bool/bvecN).  Declines pointers, structs and every unsupported base. */
static int tname(gctx *c, const clc_type *t, char *out, size_t n)
{
    int v = t->vec;
    const char *s1, *sn;

    if (t->pointer)
        return decl(c, "pointer used as a value");
    if (v < 1 || v > 4)
        return decl(c, "unsupported vector width (only 1/2/3/4)");
    switch (t->base) {
    case CLC_FLOAT: s1 = "float"; sn = "vec";  break;
    case CLC_INT:   s1 = "int";   sn = "ivec"; break;
    case CLC_UINT:  s1 = "uint";  sn = "uvec"; break;
    case CLC_BOOL:  s1 = "bool";  sn = "bvec"; break;
    default:        return decl(c, "unsupported scalar type (only float/int/uint)");
    }
    if (v == 1) snprintf(out, n, "%s", s1);
    else        snprintf(out, n, "%s%d", sn, v);
    return 0;
}

static void fcast_name(int vec, char *out, size_t n)
{
    if (vec <= 1) snprintf(out, n, "float");
    else          snprintf(out, n, "vec%d", vec);
}
static void icast_name(int vec, char *out, size_t n)
{
    if (vec <= 1) snprintf(out, n, "int");
    else          snprintf(out, n, "ivec%d", vec);
}

/* scalar GLSL element name for an SSBO member (float/int/uint). */
static int scalar_name(gctx *c, clc_base b, char *out, size_t n)
{
    switch (b) {
    case CLC_FLOAT: snprintf(out, n, "float"); return 0;
    case CLC_INT:   snprintf(out, n, "int");   return 0;
    case CLC_UINT:  snprintf(out, n, "uint");  return 0;
    default:        return decl(c, "unsupported buffer element type");
    }
}

/* ---- literals ------------------------------------------------------------ */
static void efloatlit(gctx *c, double d)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.10g", d);
    /* GLSL ES float literals need a '.', an exponent, or an inf/nan spelling;
     * there is no 'f' suffix. */
    if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E') &&
        !strchr(tmp, 'n') && !strchr(tmp, 'i'))
        snprintf(tmp + strlen(tmp), sizeof(tmp) - strlen(tmp), ".0");
    gf(c, "%s", tmp);
}

static void eintlit(gctx *c, clc_expr *e)
{
    long long v = e->li;
    if (e->type.base == CLC_UINT && v >= 0)
        gf(c, "%lluu", v);
    else
        gf(c, "%lld", v);
}

/* ---- forward decls ------------------------------------------------------- */
static int eexpr(gctx *c, clc_expr *e);
static int estmt(gctx *c, clc_stmt *s, int ind);
static int econd(gctx *c, clc_expr *e);

/* Emit `e`, wrapped in a float/int constructor when its type differs from the
 * wanted float-ness (GLSL has no implicit int<->float conversion). */
static int emit_h(gctx *c, clc_expr *e, int want_float)
{
    char fn[16];
    if (want_float && !is_float_t(e->type)) {
        fcast_name(e->type.vec, fn, sizeof(fn));
        gf(c, "%s(", fn);
        if (eexpr(c, e) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    return eexpr(c, e);
}

/* Emit `e` cast (only if needed) so it is assignable to `dst`. */
static int emit_cast_to(gctx *c, clc_type dst, clc_expr *e)
{
    int df = (dst.base == CLC_FLOAT), ef = is_float_t(e->type);
    char fn[16];
    if (df && !ef) {
        fcast_name(dst.vec, fn, sizeof(fn));
        gf(c, "%s(", fn);
        if (eexpr(c, e) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    if (!df && ef && (dst.base == CLC_INT || dst.base == CLC_UINT)) {
        icast_name(dst.vec, fn, sizeof(fn));
        gf(c, "%s(", fn);
        if (eexpr(c, e) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    return eexpr(c, e);
}

/* ---- builtin calls ------------------------------------------------------- */
/* get_global_id/size/offset: dim must be a constant 0/1/2. */
static int egid(gctx *c, clc_expr *e, int which)
{
    static const char comp[3] = { 'x', 'y', 'z' };
    int d;

    if (e->nargs != 1 || !e->args[0] || e->args[0]->kind != E_CONST_INT)
        DECLINE("get_global_*(non-constant dimension)");
    d = (int)e->args[0]->li;
    if (d < 0 || d > 2)
        DECLINE("get_global_* dimension out of range");
    if (which == 0)
        gf(c, "(u_goff%d + int(gl_GlobalInvocationID.%c))", d, comp[d]);
    else if (which == 1)
        gf(c, "u_gsz%d", d);
    else
        gf(c, "u_goff%d", d);
    return 0;
}

/* fma/mad(a,b,c) -> ((a)*(b)+(c)): GLSL broadcasts a scalar operand across a
 * vector, so this reproduces OpenCL's fused multiply-add for the subset without
 * relying on an fma() builtin. */
static int efma(gctx *c, clc_expr *e)
{
    int wf = is_float_t(e->type);
    if (e->nargs != 3)
        DECLINE("fma/mad arity");
    gf(c, "((");
    if (emit_h(c, e->args[0], wf) != 0) return -1;
    gf(c, ") * (");
    if (emit_h(c, e->args[1], wf) != 0) return -1;
    gf(c, ") + (");
    if (emit_h(c, e->args[2], wf) != 0) return -1;
    gf(c, "))");
    return 0;
}

/* decode the vector width from a "vloadN"/"vstoreN" name (2/3/4 only). */
static int vwidth(gctx *c, const char *name, const char *pfx)
{
    size_t pl = strlen(pfx);
    int w;
    if (!name || strncmp(name, pfx, pl) != 0)
        return decl(c, "malformed vload/vstore");
    if (name[pl] < '0' || name[pl] > '9')
        return decl(c, "only plain vloadN/vstoreN (no _half) supported");
    w = name[pl] - '0';
    if (name[pl + 1] != '\0' || w < 2 || w > 4)
        return decl(c, "only vloadN/vstoreN for N = 2/3/4");
    return w;
}

/* vloadN(off, ptr) -> vecN(ptr[(off)*N+0], ...).  ptr must be an SSBO array of
 * scalars; the element read stride is exactly N scalars (OpenCL vloadN). */
static int evload(gctx *c, clc_expr *e)
{
    char vt[16];
    int w, k;

    w = vwidth(c, e->name, "vload");
    if (w < 0) return -1;
    if (e->nargs != 2)
        DECLINE("vloadN arity");
    if (tname(c, &e->type, vt, sizeof(vt)) != 0) return -1;
    gf(c, "%s(", vt);
    for (k = 0; k < w; k++) {
        if (k) gf(c, ", ");
        if (eexpr(c, e->args[1]) != 0) return -1;   /* ptr -> "s<i>" */
        gf(c, "[(");
        if (eexpr(c, e->args[0]) != 0) return -1;   /* offset */
        gf(c, ") * %d + %d]", w, k);
    }
    gf(c, ")");
    return 0;
}

/* vstoreN(val, off, ptr) is a statement: N scalar stores into the SSBO. */
static int evstore_stmt(gctx *c, clc_expr *e, int ind)
{
    static const char comps[4] = { 'x', 'y', 'z', 'w' };
    int w, k;

    w = vwidth(c, e->name, "vstore");
    if (w < 0) return -1;
    if (e->nargs != 3)
        DECLINE("vstoreN arity");
    for (k = 0; k < w; k++) {
        indent(c, ind);
        if (eexpr(c, e->args[2]) != 0) return -1;   /* ptr */
        gf(c, "[(");
        if (eexpr(c, e->args[1]) != 0) return -1;   /* offset */
        gf(c, ") * %d + %d] = ", w, k);
        if (eexpr(c, e->args[0]) != 0) return -1;   /* value */
        gf(c, ".%c;\n", comps[k]);
    }
    return 0;
}

/* one/two/three argument math builtin, operands harmonised to the result's
 * float-ness. */
static int efn(gctx *c, clc_expr *e, const char *name, int n)
{
    int wf = is_float_t(e->type), i;
    if (e->nargs < n)
        DECLINE("builtin arity");
    gf(c, "%s(", name);
    for (i = 0; i < n; i++) {
        if (i) gf(c, ", ");
        if (emit_h(c, e->args[i], wf) != 0) return -1;
    }
    gf(c, ")");
    return 0;
}

static int ecall_builtin(gctx *c, clc_expr *e)
{
    switch (e->builtin_id) {
    /* work-item global id family */
    case BI_GET_GLOBAL_ID:     return egid(c, e, 0);
    case BI_GET_GLOBAL_SIZE:   return egid(c, e, 1);
    case BI_GET_GLOBAL_OFFSET: return egid(c, e, 2);

    /* anything that depends on the work-group shape the CSD baked at compile
     * time (which need not match the host's local_work_size) is declined so it
     * runs correctly on the CPU device instead. */
    case BI_GET_LOCAL_ID: case BI_GET_LOCAL_SIZE: case BI_GET_GROUP_ID:
    case BI_GET_NUM_GROUPS: case BI_GET_WORK_DIM: case BI_BARRIER:
        DECLINE("work-group builtin (get_local_id/get_group_id/barrier/...)");

    case BI_FMA: case BI_MAD:  return efma(c, e);
    case BI_VLOAD:             return evload(c, e);
    case BI_VSTORE:            DECLINE("vstore used as a value (must be a statement)");

    /* 1-arg math */
    case BI_SQRT:      return efn(c, e, "sqrt", 1);
    case BI_RSQRT:     return efn(c, e, "inversesqrt", 1);
    case BI_FABS:      return efn(c, e, "abs", 1);
    case BI_FLOOR:     return efn(c, e, "floor", 1);
    case BI_CEIL:      return efn(c, e, "ceil", 1);
    case BI_ROUND:     return efn(c, e, "round", 1);
    case BI_TRUNC:     return efn(c, e, "trunc", 1);
    case BI_RINT:      return efn(c, e, "roundEven", 1);
    case BI_SIN:       return efn(c, e, "sin", 1);
    case BI_COS:       return efn(c, e, "cos", 1);
    case BI_TAN:       return efn(c, e, "tan", 1);
    case BI_ASIN:      return efn(c, e, "asin", 1);
    case BI_ACOS:      return efn(c, e, "acos", 1);
    case BI_SINH:      return efn(c, e, "sinh", 1);
    case BI_COSH:      return efn(c, e, "cosh", 1);
    case BI_TANH:      return efn(c, e, "tanh", 1);
    case BI_EXP:       return efn(c, e, "exp", 1);
    case BI_EXP2:      return efn(c, e, "exp2", 1);
    case BI_LOG:       return efn(c, e, "log", 1);
    case BI_LOG2:      return efn(c, e, "log2", 1);
    case BI_SIGN:      return efn(c, e, "sign", 1);
    case BI_DEGREES:   return efn(c, e, "degrees", 1);
    case BI_RADIANS:   return efn(c, e, "radians", 1);
    case BI_LENGTH:    case BI_FAST_LENGTH:    return efn(c, e, "length", 1);
    case BI_NORMALIZE: case BI_FAST_NORMALIZE: return efn(c, e, "normalize", 1);

    /* 2-arg math */
    case BI_FMIN: case BI_MIN:  return efn(c, e, "min", 2);
    case BI_FMAX: case BI_MAX:  return efn(c, e, "max", 2);
    case BI_POW: case BI_POWR:  return efn(c, e, "pow", 2);
    case BI_ATAN2:              return efn(c, e, "atan", 2);
    case BI_STEP:               return efn(c, e, "step", 2);
    case BI_DOT:                return efn(c, e, "dot", 2);
    case BI_DISTANCE: case BI_FAST_DISTANCE: return efn(c, e, "distance", 2);
    case BI_CROSS:              return efn(c, e, "cross", 2);
    case BI_HYPOT:              return efn(c, e, "length", 2); /* not exact */

    /* 3-arg math */
    case BI_MIX:         return efn(c, e, "mix", 3);
    case BI_CLAMP:       return efn(c, e, "clamp", 3);
    case BI_SMOOTHSTEP:  return efn(c, e, "smoothstep", 3);

    /* pown(x, int n) -> pow(x, float(n)) */
    case BI_POWN:
        if (e->nargs < 2) DECLINE("pown arity");
        gf(c, "pow(");
        if (emit_h(c, e->args[0], 1) != 0) return -1;
        gf(c, ", float(");
        if (eexpr(c, e->args[1]) != 0) return -1;
        gf(c, "))");
        return 0;

    /* everything else (printf, convert_/as_, mul_hi, rotate, clz, popcount,
     * select, the relational is* family, saturating ops, ...) is declined. */
    default:
        DECLINE("unsupported OpenCL-C builtin");
    }
}

/* ---- operators ----------------------------------------------------------- */
static int eunop(gctx *c, clc_expr *e)
{
    switch (e->op) {
    case OP_NEG:  gf(c, "(-"); if (eexpr(c, e->a) != 0) return -1; gf(c, ")"); return 0;
    case OP_LNOT: gf(c, "(!"); if (eexpr(c, e->a) != 0) return -1; gf(c, ")"); return 0;
    case OP_BNOT:
        if (is_float_t(e->a->type)) DECLINE("~ on a float operand");
        gf(c, "(~"); if (eexpr(c, e->a) != 0) return -1; gf(c, ")"); return 0;
    case OP_PRE_INC: gf(c, "(++"); if (eexpr(c, e->a) != 0) return -1; gf(c, ")"); return 0;
    case OP_PRE_DEC: gf(c, "(--"); if (eexpr(c, e->a) != 0) return -1; gf(c, ")"); return 0;
    case OP_DEREF: DECLINE("pointer dereference");
    case OP_ADDR:  DECLINE("address-of");
    default:       DECLINE("unsupported unary operator");
    }
}

static int epostop(gctx *c, clc_expr *e)
{
    switch (e->op) {
    case OP_POST_INC: if (eexpr(c, e->a) != 0) return -1; gf(c, "++"); return 0;
    case OP_POST_DEC: if (eexpr(c, e->a) != 0) return -1; gf(c, "--"); return 0;
    default:          DECLINE("unsupported postfix operator");
    }
}

static const char *assign_op(clc_op op)
{
    switch (op) {
    case OP_ASSIGN:  return "=";
    case OP_ADD_ASG: return "+=";
    case OP_SUB_ASG: return "-=";
    case OP_MUL_ASG: return "*=";
    case OP_DIV_ASG: return "/=";
    case OP_MOD_ASG: return "%=";
    case OP_BAND_ASG:return "&=";
    case OP_BOR_ASG: return "|=";
    case OP_BXOR_ASG:return "^=";
    case OP_SHL_ASG: return "<<=";
    case OP_SHR_ASG: return ">>=";
    default:         return NULL;
    }
}

static int eassign(gctx *c, clc_expr *e)
{
    const char *op = assign_op(e->op);
    if (!op) DECLINE("unsupported assignment operator");
    if (e->a->kind != E_VAR && e->a->kind != E_INDEX && e->a->kind != E_SWIZZLE)
        DECLINE("unsupported assignment target");
    if (eexpr(c, e->a) != 0) return -1;
    gf(c, " %s ", op);
    if (emit_cast_to(c, e->a->type, e->b) != 0) return -1;
    return 0;
}

static int ebinop(gctx *c, clc_expr *e)
{
    int wf;
    switch (e->op) {
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: {
        const char *o = (e->op == OP_ADD) ? "+" : (e->op == OP_SUB) ? "-" :
                        (e->op == OP_MUL) ? "*" : "/";
        wf = is_float_t(e->type);
        gf(c, "(");
        if (emit_h(c, e->a, wf) != 0) return -1;
        gf(c, " %s ", o);
        if (emit_h(c, e->b, wf) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    case OP_MOD:
        if (is_float_t(e->type)) {
            gf(c, "mod(");
            if (emit_h(c, e->a, 1) != 0) return -1;
            gf(c, ", ");
            if (emit_h(c, e->b, 1) != 0) return -1;
            gf(c, ")");
        } else {
            gf(c, "(");
            if (eexpr(c, e->a) != 0) return -1;
            gf(c, " %% ");
            if (eexpr(c, e->b) != 0) return -1;
            gf(c, ")");
        }
        return 0;

    case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        const char *o = (e->op == OP_EQ) ? "==" : (e->op == OP_NE) ? "!=" :
                        (e->op == OP_LT) ? "<"  : (e->op == OP_LE) ? "<=" :
                        (e->op == OP_GT) ? ">"  : ">=";
        int af = is_float_t(e->a->type), bf = is_float_t(e->b->type);
        int vec = af ? e->a->type.vec : (bf ? e->b->type.vec : 1);
        if (af && bf && e->a->type.vec != e->b->type.vec)
            DECLINE("mismatched vector comparison");
        gf(c, "(");
        if (emit_h(c, e->a, af || bf) != 0) return -1;
        gf(c, " %s ", o);
        if (emit_h(c, e->b, af || bf) != 0) return -1;
        gf(c, ")");
        (void)vec;
        return 0;
    }
    case OP_LAND: case OP_LOR: {
        const char *o = (e->op == OP_LAND) ? "&&" : "||";
        if (e->a->type.vec != 1 || e->b->type.vec != 1)
            DECLINE("vector logical operator (use any/all)");
        gf(c, "(");
        if (eexpr(c, e->a) != 0) return -1;
        gf(c, " %s ", o);
        if (eexpr(c, e->b) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
        const char *o = (e->op == OP_BAND) ? "&" : (e->op == OP_BOR) ? "|" :
                        (e->op == OP_BXOR) ? "^" : (e->op == OP_SHL) ? "<<" : ">>";
        if (is_float_t(e->a->type) || is_float_t(e->b->type))
            DECLINE("bitwise operator on a float operand");
        gf(c, "(");
        if (eexpr(c, e->a) != 0) return -1;
        gf(c, " %s ", o);
        if (eexpr(c, e->b) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    default:
        DECLINE("unsupported binary operator");
    }
}

/* ---- expressions --------------------------------------------------------- */
static int eexpr(gctx *c, clc_expr *e)
{
    if (c->declined) return -1;
    if (!e) DECLINE("empty expression");

    switch (e->kind) {
    case E_CONST_INT:   eintlit(c, e); return 0;
    case E_CONST_FLOAT: efloatlit(c, e->ld); return 0;

    case E_VAR:
        if (e->is_global) DECLINE("program-scope global variable");
        gf(c, "s%d", e->sym);
        return 0;

    case E_INDEX:
        if (eexpr(c, e->a) != 0) return -1;
        gf(c, "[");
        if (eexpr(c, e->b) != 0) return -1;
        gf(c, "]");
        return 0;

    case E_SWIZZLE: {
        int i;
        if (eexpr(c, e->a) != 0) return -1;
        gf(c, ".");
        for (i = 0; i < e->nswiz && i < 4; i++)
            gf(c, "%c", e->swizzle[i]);
        return 0;
    }
    case E_FIELD: DECLINE("struct member access");

    case E_COMPOUND: {
        char tn[24];
        int i;
        if (tname(c, &e->type, tn, sizeof(tn)) != 0) return -1;
        gf(c, "%s(", tn);
        for (i = 0; i < e->ninit; i++) {
            if (i) gf(c, ", ");
            if (eexpr(c, e->init[i]) != 0) return -1;
        }
        gf(c, ")");
        return 0;
    }
    case E_CAST: {
        char tn[24];
        if (tname(c, &e->type, tn, sizeof(tn)) != 0) return -1;
        gf(c, "%s(", tn);
        if (eexpr(c, e->a) != 0) return -1;
        gf(c, ")");
        return 0;
    }
    case E_TERNARY:
        gf(c, "(");
        if (econd(c, e->a) != 0) return -1;
        gf(c, " ? ");
        if (emit_h(c, e->b, is_float_t(e->type)) != 0) return -1;
        gf(c, " : ");
        if (emit_h(c, e->c, is_float_t(e->type)) != 0) return -1;
        gf(c, ")");
        return 0;

    case E_UNOP:   return eunop(c, e);
    case E_POSTOP: return epostop(c, e);
    case E_ASSIGN: return eassign(c, e);
    case E_BINOP:  return ebinop(c, e);

    case E_CALL:
        if (e->is_builtin) return ecall_builtin(c, e);
        DECLINE("call to a user-defined function");

    default:
        DECLINE("unsupported expression");
    }
}

/* True when `e` emits as a SCALAR GLSL bool.  GLSL's comparison, logical and
 * `!` operators yield bool even though the clc AST types them as int (C's `<`
 * and `&&` result type is int), so econd must NOT append `!= 0` to them -- that
 * would compare a bool against an int and fail to compile ("operands of `!=`
 * must have the same type"). */
static int glsl_scalar_bool(clc_expr *e)
{
    if (!e || e->type.vec != 1)
        return 0;
    if (e->type.base == CLC_BOOL)
        return 1;
    if (e->kind == E_BINOP) {
        switch (e->op) {
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        case OP_LAND: case OP_LOR:
            return 1;
        default:
            return 0;
        }
    }
    if (e->kind == E_UNOP && e->op == OP_LNOT)
        return 1;
    return 0;
}

/* a scalar bool for if/while/?: conditions; GLSL forbids a non-bool test. */
static int econd(gctx *c, clc_expr *e)
{
    if (!e) DECLINE("empty condition");
    if (glsl_scalar_bool(e))
        return eexpr(c, e);
    if (e->type.vec != 1)
        DECLINE("vector condition (use any/all)");
    gf(c, "(");
    if (eexpr(c, e) != 0) return -1;
    if (is_float_t(e->type)) gf(c, " != 0.0)");
    else                     gf(c, " != 0)");
    return 0;
}

/* ---- statements ---------------------------------------------------------- */
/* a declaration without the trailing ';' (for a for-loop init). */
static int edecl_hdr(gctx *c, clc_stmt *s)
{
    char tn[24];
    if (s->is_array) DECLINE("local array");
    if (tname(c, &s->type, tn, sizeof(tn)) != 0) return -1;
    gf(c, "%s s%d", tn, s->sym);
    if (s->init) {
        gf(c, " = ");
        if (emit_cast_to(c, s->type, s->init) != 0) return -1;
    }
    return 0;
}

/* a braced body for if/for/while (wraps a non-block single statement). */
static int ebody(gctx *c, clc_stmt *b, int ind)
{
    if (!b) { indent(c, ind); gf(c, "{\n"); indent(c, ind); gf(c, "}\n"); return 0; }
    if (b->kind == S_BLOCK) return estmt(c, b, ind);
    indent(c, ind); gf(c, "{\n");
    if (estmt(c, b, ind + 1) != 0) return -1;
    indent(c, ind); gf(c, "}\n");
    return 0;
}

static int estmt(gctx *c, clc_stmt *s, int ind)
{
    if (c->declined) return -1;
    if (!s) return 0;

    switch (s->kind) {
    case S_BLOCK: {
        int i;
        indent(c, ind); gf(c, "{\n");
        for (i = 0; i < s->nbody; i++)
            if (estmt(c, s->body[i], ind + 1) != 0) return -1;
        indent(c, ind); gf(c, "}\n");
        return 0;
    }
    case S_DECL:
        indent(c, ind);
        if (edecl_hdr(c, s) != 0) return -1;
        gf(c, ";\n");
        return 0;

    case S_EXPR:
        if (!s->expr) return 0;   /* empty statement */
        if (s->expr->kind == E_CALL && s->expr->is_builtin &&
            s->expr->builtin_id == BI_VSTORE)
            return evstore_stmt(c, s->expr, ind);
        indent(c, ind);
        if (eexpr(c, s->expr) != 0) return -1;
        gf(c, ";\n");
        return 0;

    case S_IF:
        indent(c, ind); gf(c, "if (");
        if (econd(c, s->cond) != 0) return -1;
        gf(c, ")\n");
        if (ebody(c, s->thenb, ind) != 0) return -1;
        if (s->elseb) {
            indent(c, ind); gf(c, "else\n");
            if (ebody(c, s->elseb, ind) != 0) return -1;
        }
        return 0;

    case S_FOR:
        indent(c, ind); gf(c, "for (");
        if (s->sinit) {
            if (s->sinit->kind == S_DECL) {
                if (edecl_hdr(c, s->sinit) != 0) return -1;
            } else if (s->sinit->kind == S_EXPR && s->sinit->expr) {
                if (eexpr(c, s->sinit->expr) != 0) return -1;
            } else {
                DECLINE("unsupported for-init");
            }
        }
        gf(c, "; ");
        if (s->scond && eexpr(c, s->scond) != 0) return -1;
        gf(c, "; ");
        if (s->sinc && eexpr(c, s->sinc) != 0) return -1;
        gf(c, ")\n");
        if (ebody(c, s->sbody, ind) != 0) return -1;
        return 0;

    case S_WHILE:
        indent(c, ind); gf(c, "while (");
        if (econd(c, s->cond) != 0) return -1;
        gf(c, ")\n");
        if (ebody(c, s->thenb, ind) != 0) return -1;
        return 0;

    case S_DOWHILE:
        indent(c, ind); gf(c, "do\n");
        if (ebody(c, s->thenb, ind) != 0) return -1;
        indent(c, ind); gf(c, "while (");
        if (econd(c, s->cond) != 0) return -1;
        gf(c, ");\n");
        return 0;

    case S_RETURN:
        indent(c, ind);
        if (s->expr) { gf(c, "return "); if (eexpr(c, s->expr) != 0) return -1; gf(c, ";\n"); }
        else         gf(c, "return;\n");
        return 0;

    case S_BREAK:    indent(c, ind); gf(c, "break;\n");    return 0;
    case S_CONTINUE: indent(c, ind); gf(c, "continue;\n"); return 0;

    case S_SWITCH: case S_CASE: case S_DEFAULT:
        DECLINE("switch/case (not in the GPU subset)");

    default:
        DECLINE("unsupported statement");
    }
}

/* ---- parameter classification -------------------------------------------- */
static int classify_params(gctx *c)
{
    clc_function *fn = c->fn;
    int i, binding = 0;

    c->info->nargs       = fn->nparams;
    c->info->max_binding = -1;
    c->info->local_size_x = CLC_GLSL_LOCAL_SIZE_X;

    for (i = 0; i < fn->nparams && i < CLC_MAX_PARAMS; i++) {
        clc_type      t = fn->syms[i].type;
        clc_glsl_arg *a = &c->info->args[i];
        a->type = t;

        if (t.pointer) {
            /* __global/__constant T* -> an SSBO; __local/private -> decline. */
            if (t.as == CLC_AS_LOCAL)
                DECLINE("__local kernel argument");
            if (t.as != CLC_AS_GLOBAL && t.as != CLC_AS_CONSTANT)
                DECLINE("private pointer kernel argument");
            if (t.vec != 1)
                DECLINE("vector-element buffer (only scalar __global T*)");
            if (!supported_scalar(t.base))
                DECLINE("unsupported buffer element type");
            if (binding >= CLC_GLSL_MAX_SSBO)
                DECLINE("more than 4 __global buffer args (V3D 7.1 SSBO limit)");
            a->kind    = CLC_GLSL_ARG_SSBO;
            a->binding = binding++;
            if (a->binding > c->info->max_binding)
                c->info->max_binding = a->binding;
        } else if (t.vec == 1 && supported_scalar(t.base)) {
            /* scalar by value -> a default-block uniform named "s<i>". */
            a->kind = CLC_GLSL_ARG_UNIFORM;
            snprintf(a->uname, sizeof(a->uname), "s%d", i);
        } else {
            DECLINE("unsupported kernel argument type (vector/struct by value)");
        }
    }
    return c->declined ? -1 : 0;
}

/* ---- entry point --------------------------------------------------------- */
char *clc_to_glsl(clc_program *prog, clc_function *kernel,
                  clc_glsl_info *info, char *err, size_t errsz)
{
    gctx c;
    int  i;

    if (err && errsz) err[0] = '\0';
    if (!kernel || !info) return NULL;

    memset(&c, 0, sizeof(c));
    memset(info, 0, sizeof(*info));
    c.prog = prog;
    c.fn   = kernel;
    c.info = info;

    if (!kernel->is_kernel) { decl(&c, "not a kernel"); goto fail; }
    if (classify_params(&c) != 0) goto fail;

    /* --- prologue --- */
    gf(&c, "#version 310 es\n");
    gf(&c, "precision highp float;\n");
    gf(&c, "precision highp int;\n\n");
    gf(&c, "layout(local_size_x = %d, local_size_y = 1, local_size_z = 1) in;\n\n",
       CLC_GLSL_LOCAL_SIZE_X);

    /* SSBO buffer args (binding order == the arg order in cl_kernel_dispatch) */
    for (i = 0; i < info->nargs && i < CLC_MAX_PARAMS; i++) {
        char en[16];
        if (info->args[i].kind != CLC_GLSL_ARG_SSBO) continue;
        if (scalar_name(&c, info->args[i].type.base, en, sizeof(en)) != 0) goto fail;
        gf(&c, "layout(std430, binding = %d) buffer ClBuf%d { %s s%d[]; };\n",
           info->args[i].binding, info->args[i].binding, en, i);
    }
    gf(&c, "\n");

    /* scalar by-value args as uniforms (matched by name via reflection) */
    for (i = 0; i < info->nargs && i < CLC_MAX_PARAMS; i++) {
        char en[16];
        if (info->args[i].kind != CLC_GLSL_ARG_UNIFORM) continue;
        if (scalar_name(&c, info->args[i].type.base, en, sizeof(en)) != 0) goto fail;
        gf(&c, "uniform %s %s;\n", en, info->args[i].uname);
    }

    /* the fixed global offset/size uniforms (dead-stripped when unreferenced;
     * u_gsz0 always survives because the tail-group guard reads it). */
    gf(&c, "uniform int u_goff0;\nuniform int u_goff1;\nuniform int u_goff2;\n");
    gf(&c, "uniform int u_gsz0;\nuniform int u_gsz1;\nuniform int u_gsz2;\n\n");

    /* --- main() --- */
    gf(&c, "void main()\n{\n");
    /* Keep tail-lane control flow inside main.  An early return lets inactive
     * lanes leave before the common TMU-drain tail on V3D 7.1. */
    gf(&c, "    if (gl_GlobalInvocationID.x < uint(u_gsz0)) {\n");
    if (kernel->body && kernel->body->kind == S_BLOCK) {
        int k;
        for (k = 0; k < kernel->body->nbody; k++)
            if (estmt(&c, kernel->body->body[k], 2) != 0) goto fail;
    } else if (kernel->body) {
        if (estmt(&c, kernel->body, 2) != 0) goto fail;
    }
    gf(&c, "    }\n}\n");

    if (c.declined) goto fail;
    return c.buf;   /* caller frees */

fail:
    if (err && errsz)
        snprintf(err, errsz, "%s", c.err[0] ? c.err : "unsupported kernel");
    free(c.buf);
    return NULL;
}
