/* glsl_exec.c - tree-walking evaluator for the CPU GLSL ES 1.00 interpreter.
 *
 * Values are runs of cells (see glsl.h).  Temporaries bump-allocate in the
 * program's scratch arena (saved/restored per statement so peak use is bounded);
 * user-function frames bump-allocate in the program's stack arena.  Neither run
 * path mallocs, because run_fragment executes once per pixel.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "glsl.h"
#include "glsl_util.h"

enum { CTRL_NONE = 0, CTRL_BREAK, CTRL_CONTINUE, CTRL_RETURN, CTRL_DISCARD };

typedef struct {
    glsl_program *prog;
    glsl_stage   *stage;
    int is_vertex;

    glsl_cell *gstore;               /* stage globals home               */
    const float *const *attribs; int nattribs;
    float *varyings_out;             /* VS writes                        */
    const float *varyings_in;        /* FS reads                         */
    const glsl_cell *ublock;         /* uniform block (cells)            */

    /* builtins */
    float gl_position[4], gl_fragcolor[4], gl_fragcoord[4], gl_pointcoord[2];
    int   front_facing, instance_id;
    float gl_fragdepth; int wrote_depth;

    glsl_tex_sample_fn texfn; void *texuser;

    int scratch_top, stack_top;
    const glsl_func *fn; glsl_cell *locals;

    int ctrl;
    glsl_cell ret_buf[256]; int ret_type, ret_stid, ret_n, ret_arr, ret_elem;
    int err;
} env;

/* Register-bytecode VM (defined in glsl_vm.inc, #included at the very end of
 * this file so it can call the static math helpers above and be bit-identical).
 * vm_run returns 1 when `stage` has a compiled VM form and was executed through
 * it, or 0 to fall back to the tree-walker (run_main). */
static int vm_run(glsl_program *p, glsl_stage *stage, int is_vertex, env *E);

/* ------------------------------------------------------------------ */
static glsl_cell *sc(env *E, int n)
{
    glsl_cell *p;
    if (E->scratch_top + n > E->prog->scratch_cap) { E->err = 1; n = 0; }
    p = E->prog->scratch + E->scratch_top;
    E->scratch_top += n;
    return p;
}
static glsl_cell *stk(env *E, int n)
{
    glsl_cell *p;
    if (E->stack_top + n > E->prog->stack_cap) { E->err = 1; n = 0; }
    p = E->prog->stack + E->stack_top;
    E->stack_top += n;
    return p;
}

static inline float cf(const glsl_cell *c, int type, int i)
{ return glsl_is_uint(type) ? (float)c[i].u : glsl_is_int_family(type) ? (float)c[i].i : c[i].f; }
static inline int ci(const glsl_cell *c, int type, int i)
{ return glsl_is_int_family(type) ? c[i].i : (int)c[i].f; }
static glsl_cell convert_cell(int target, const glsl_cell *src, int type, int i)
{
    glsl_cell out;
    if (glsl_is_int_family(target)) {
        if (glsl_is_int_family(type)) out.u = src[i].u;
        else if (glsl_is_uint(target)) out.u = (uint32_t)src[i].f;
        else out.i = (int32_t)src[i].f;
    } else out.f = cf(src, type, i);
    return out;
}

static void setval(glsl_val *v, int type, int stid, glsl_cell *data, int n,
                   int arr, int elem)
{
    v->type = type; v->st_id = stid; v->data = data; v->n = n;
    v->array_len = arr; v->elem_cells = elem;
}

/* copy n cells from src into fresh scratch, returning a value */
static glsl_val copy_val(env *E, int type, int stid, const glsl_cell *src, int n,
                         int arr, int elem)
{
    glsl_val v; glsl_cell *d = sc(E, n > 0 ? n : 1);
    if (n > 0 && src) memcpy(d, src, n * sizeof(glsl_cell));
    setval(&v, type, stid, d, n, arr, elem);
    return v;
}

/* ------------------------------------------------------------------ */
/* lvalues */
typedef struct {
    glsl_cell *mem;
    int type, stid, ncomp, array_len, elem_cells;
    int is_swizzle, swz_n; unsigned char swz[4];
    int valid;
} lv;

static int global_cells_for(const glsl_stage *st, int gi)
{
    return glsl_var_cells(st, st->globals[gi].type, st->globals[gi].st_id,
                          st->globals[gi].array_len);
}

/* find the globals[] entry for a given scope+slot in the current stage */
static int find_global_entry(const glsl_stage *st, int scope, int slot)
{
    int i;
    for (i = 0; i < st->nglobals; ++i)
        if (st->globals[i].scope == scope && st->globals[i].slot == slot)
            return i;
    return -1;
}

static void resolve_lv(env *E, const glsl_expr *e, lv *out);
static glsl_val eval(env *E, const glsl_expr *e);

static void resolve_var_lv(env *E, const glsl_expr *e, lv *L)
{
    L->is_swizzle = 0; L->valid = 1; L->array_len = 0; L->elem_cells = 0;
    switch (e->var_scope) {
    case VARSCOPE_LOCAL: {
        int s = e->var_slot;
        if (s < 0 || s >= E->fn->nlocals) { E->err = 1; L->valid = 0; return; }
        L->mem = E->locals + E->fn->local_off[s];
        L->type = E->fn->local_type[s]; L->stid = E->fn->local_stid[s];
        L->array_len = E->fn->local_array[s];
        L->elem_cells = glsl_type_cells(E->stage, L->type, L->stid);
        L->ncomp = L->array_len > 0 ? L->elem_cells * L->array_len : L->elem_cells;
        break;
    }
    case VARSCOPE_GLOBAL: {
        int gi = find_global_entry(E->stage, VARSCOPE_GLOBAL, e->var_slot);
        if (gi < 0) { E->err = 1; L->valid = 0; return; }
        L->mem = E->gstore + E->stage->globals[gi].goffset;
        L->type = E->stage->globals[gi].type; L->stid = E->stage->globals[gi].st_id;
        L->array_len = E->stage->globals[gi].array_len;
        L->elem_cells = glsl_type_cells(E->stage, L->type, L->stid);
        L->ncomp = global_cells_for(E->stage, gi);
        break;
    }
    case VARSCOPE_UNIFORM: {
        int u = e->var_slot;
        const glsl_uniform *U;
        if (u < 0 || u >= E->prog->nuniforms) { E->err = 1; L->valid = 0; return; }
        U = &E->prog->uniforms[u];
        L->mem = (glsl_cell *)(E->ublock + U->offset);
        L->type = U->type; L->stid = U->st_id;
        L->array_len = U->array_len; L->elem_cells = U->ncomp;
        L->ncomp = U->array_len > 0 ? U->ncomp * U->array_len : U->ncomp;
        break;
    }
    case VARSCOPE_ATTRIB: {
        int loc = e->var_slot;
        if (loc < 0 || loc >= E->prog->nattribs || loc >= E->nattribs ||
            !E->attribs || !E->attribs[loc]) { E->err = 1; L->valid = 0; return; }
        L->mem = (glsl_cell *)E->attribs[loc];
        L->type = T_FLOAT; L->stid = -1;
        L->ncomp = E->prog->attribs[loc].size; L->array_len = 0;
        L->elem_cells = L->ncomp;
        if (L->ncomp == 1) L->type = T_FLOAT;
        else L->type = glsl_float_type_of_len(L->ncomp);
        break;
    }
    case VARSCOPE_VARYING: {
        int s = e->var_slot;
        glsl_cell *base;
        if (s < 0 || s >= E->prog->nvaryings) { E->err = 1; L->valid = 0; return; }
        base = E->is_vertex ? (glsl_cell *)E->varyings_out : (glsl_cell *)E->varyings_in;
        L->mem = base + s * 4;
        L->type = glsl_float_type_of_len(E->prog->varyings[s].ncomp);
        L->stid = -1; L->ncomp = E->prog->varyings[s].ncomp;
        L->array_len = 0; L->elem_cells = L->ncomp;
        break;
    }
    case VARSCOPE_BUILTIN: {
        L->stid = -1; L->array_len = 0;
        switch (e->var_slot) {
        case BI_GL_POSITION:  L->mem=(glsl_cell*)E->gl_position;  L->type=T_VEC4; L->ncomp=4; break;
        case BI_GL_FRAGCOLOR: L->mem=(glsl_cell*)E->gl_fragcolor; L->type=T_VEC4; L->ncomp=4; break;
        case BI_GL_FRAGCOORD: L->mem=(glsl_cell*)E->gl_fragcoord; L->type=T_VEC4; L->ncomp=4; break;
        case BI_GL_POINTCOORD:L->mem=(glsl_cell*)E->gl_pointcoord;L->type=T_VEC2; L->ncomp=2; break;
        case BI_GL_FRONTFACING:L->mem=(glsl_cell*)&E->front_facing;L->type=T_BOOL;L->ncomp=1;break;
        case BI_GL_FRAGDEPTH: L->mem=(glsl_cell*)&E->gl_fragdepth;L->type=T_FLOAT;L->ncomp=1; break;
        case BI_GL_INSTANCEID:L->mem=(glsl_cell*)&E->instance_id;L->type=T_INT;  L->ncomp=1; break;
        default: E->err = 1; L->valid = 0; return;
        }
        L->elem_cells = L->ncomp;
        break;
    }
    default:
        E->err = 1; L->valid = 0; break;
    }
}

static int struct_field(const glsl_stage *st, int stid, const char *name, int *off,
                        int *type, int *fstid, int *farray)
{
    int i;
    if (stid < 0 || stid >= st->nstructs) return -1;
    for (i = 0; i < st->structs[stid].nfields; ++i)
        if (!strcmp(st->structs[stid].fname[i], name)) {
            *off = st->structs[stid].foffset[i];
            *type = st->structs[stid].ftype[i];
            *fstid = st->structs[stid].fst_id[i];
            *farray = st->structs[stid].farray[i];
            return i;
        }
    return -1;
}

/* Resolve the base of a postfix operator (`.swizzle` / `[i]`).  A base that is
 * itself an lvalue resolves normally; any other base (constructor, arithmetic,
 * matrix*vector, function call ...) is an rvalue, so evaluate it and expose its
 * scratch cells as a temporary lvalue so the postfix operator can read (or, for
 * the illegal-as-lvalue case, harmlessly touch) its components.  Without this,
 * `vec4(...).xyz` and `(m * v).xyz` set E->err and abort the shader. */
static int resolve_base_lv(env *E, const glsl_expr *e, lv *out)
{
    if (e->kind == EX_VAR || e->kind == EX_INDEX || e->kind == EX_FIELD) {
        resolve_lv(E, e, out);
        return out->valid;
    }
    {
        glsl_val v = eval(E, e);
        if (E->err || v.n <= 0 || v.data == NULL) return 0;
        memset(out, 0, sizeof(*out));
        out->mem = v.data;
        out->type = v.type; out->stid = v.st_id;
        out->ncomp = v.n; out->array_len = v.array_len; out->elem_cells = v.elem_cells;
        out->valid = 1;
        return 1;
    }
}

static void resolve_lv(env *E, const glsl_expr *e, lv *out)
{
    memset(out, 0, sizeof(*out));
    if (e->kind == EX_VAR) { resolve_var_lv(E, e, out); return; }
    if (e->kind == EX_INDEX) {
        lv base; int idx;
        if (!resolve_base_lv(E, e->a, &base)) { E->err = 1; return; }
        { glsl_val iv = eval(E, e->b); idx = ci(iv.data, iv.type, 0); }
        if (base.array_len > 0) {
            if (idx < 0 || idx >= base.array_len) { E->err = 1; return; }
            out->mem = base.mem + idx * base.elem_cells;
            out->type = base.type; out->stid = base.stid;
            out->array_len = 0; out->ncomp = base.elem_cells; out->elem_cells = base.elem_cells;
            out->valid = 1; return;
        }
        if (glsl_is_matrix(base.type)) {
            int rows = base.type == T_MAT2 ? 2 : base.type == T_MAT3 ? 3 : 4;
            if (idx < 0 || idx >= rows) { E->err = 1; return; }
            out->mem = base.mem + idx * rows;
            out->type = glsl_float_type_of_len(rows); out->stid = -1;
            out->ncomp = rows; out->elem_cells = rows; out->array_len = 0; out->valid = 1; return;
        }
        /* vector / scalar component */
        if (idx < 0 || idx >= base.ncomp) { E->err = 1; return; }
        out->mem = base.mem + idx;
        out->type = glsl_is_uint(base.type) ? T_UINT : glsl_is_int_family(base.type) ? T_INT : T_FLOAT;
        out->stid = -1; out->ncomp = 1; out->elem_cells = 1; out->array_len = 0; out->valid = 1;
        return;
    }
    if (e->kind == EX_FIELD) {
        lv base;
        if (!resolve_base_lv(E, e->a, &base)) { E->err = 1; return; }
        if (e->field_is_swizzle && base.type != T_STRUCT) {
            *out = base; out->is_swizzle = 1; out->swz_n = e->swz_n;
            memcpy(out->swz, e->swz, 4); out->ncomp = e->swz_n;
            out->type = glsl_is_uint(base.type) ? glsl_uint_type_of_len(e->swz_n)
                      : glsl_is_int_family(base.type) ? glsl_int_type_of_len(e->swz_n)
                                                      : glsl_float_type_of_len(e->swz_n);
            out->valid = 1; return;
        }
        /* struct field */
        {
            int off, ty, st2, fa;
            if (struct_field(E->stage, base.stid, e->var_name, &off, &ty, &st2, &fa) < 0) {
                E->err = 1; return;
            }
            out->mem = base.mem + off; out->type = ty; out->stid = st2;
            out->array_len = fa;
            out->elem_cells = glsl_type_cells(E->stage, ty, st2);
            out->ncomp = fa > 0 ? out->elem_cells * fa : out->elem_cells;
            out->valid = 1; return;
        }
    }
    E->err = 1;
}

/* read an lvalue into a scratch value */
static glsl_val read_lv(env *E, const lv *L)
{
    if (L->is_swizzle) {
        int i; glsl_cell *d = sc(E, L->swz_n);
        glsl_val v;
        for (i = 0; i < L->swz_n; ++i) {
            int c = L->swz[i];
            if (glsl_is_int_family(L->type)) d[i].i = L->mem[c].i;
            else d[i].f = L->mem[c].f;
        }
        setval(&v, L->type, L->stid, d, L->swz_n, 0, 0);
        return v;
    }
    return copy_val(E, L->type, L->stid, L->mem, L->ncomp, L->array_len, L->elem_cells);
}

/* write value v into lvalue L */
static void write_lv(env *E, lv *L, const glsl_val *v)
{
    (void)E;
    if (L->is_swizzle) {
        int i;
        for (i = 0; i < L->swz_n; ++i) {
            int c = L->swz[i];
            L->mem[c] = convert_cell(L->type, v->data, v->type, i < v->n ? i : 0);
        }
        return;
    }
    {
        int i, n = L->ncomp < v->n ? L->ncomp : v->n;
        if (L->type == T_STRUCT) {
            memcpy(L->mem, v->data, n * sizeof(glsl_cell));
            return;
        }
        /* scalar RHS broadcasts to a vector/matrix LHS */
        if (v->n == 1 && L->ncomp > 1 && v->array_len == 0) {
            for (i = 0; i < L->ncomp; ++i)
                L->mem[i] = convert_cell(L->type, v->data, v->type, 0);
            return;
        }
        for (i = 0; i < n; ++i)
            L->mem[i] = convert_cell(L->type, v->data, v->type, i);
    }
}

/* ------------------------------------------------------------------ */
/* arithmetic helpers */
static int comp_len(const glsl_val *v)
{
    if (glsl_is_matrix(v->type)) return v->type == T_MAT2 ? 2 : v->type == T_MAT3 ? 3 : 4;
    return glsl_vec_len(v->type);
}

static glsl_val make_scalar_f(env *E, float f)
{ glsl_cell *d = sc(E, 1); d[0].f = f; glsl_val v; setval(&v, T_FLOAT, -1, d, 1, 0, 0); return v; }
static glsl_val make_scalar_i(env *E, int i)
{ glsl_cell *d = sc(E, 1); d[0].i = i; glsl_val v; setval(&v, T_INT, -1, d, 1, 0, 0); return v; }
static glsl_val make_bool(env *E, int b)
{ glsl_cell *d = sc(E, 1); d[0].i = b ? 1 : 0; glsl_val v; setval(&v, T_BOOL, -1, d, 1, 0, 0); return v; }

static void mat_mul(env *E, const glsl_val *a, const glsl_val *b, glsl_val *out);

/* componentwise binary op for vectors/scalars (+ - * / and int %, bitwise) */
static glsl_val arith(env *E, int op, const glsl_val *a, const glsl_val *b)
{
    int af = !glsl_is_int_family(a->type), bf = !glsl_is_int_family(b->type);
    int isfloat = af || bf;
    int isuint = (op == EX_SHL || op == EX_SHR) ? glsl_is_uint(a->type)
                  : glsl_is_uint(a->type) || glsl_is_uint(b->type);
    int al = comp_len(a), bl = comp_len(b);
    int n = al > bl ? al : bl;
    int i, rtype;
    glsl_cell *d;
    glsl_val v;
    if (al != 1 && bl != 1 && al != bl) { E->err = 1; return make_scalar_f(E, 0); }
    rtype = isfloat ? glsl_float_type_of_len(n)
                   : isuint ? glsl_uint_type_of_len(n) : glsl_int_type_of_len(n);
    d = sc(E, n);
    for (i = 0; i < n; ++i) {
        float fa = cf(a->data, a->type, al == 1 ? 0 : i);
        float fb = cf(b->data, b->type, bl == 1 ? 0 : i);
        int ia = isfloat ? 0 : ci(a->data, a->type, al == 1 ? 0 : i);
        int ib = isfloat ? 0 : ci(b->data, b->type, bl == 1 ? 0 : i);
        switch (op) {
        case EX_ADD: if (isfloat) d[i].f = fa + fb; else d[i].u = (uint32_t)ia + (uint32_t)ib; break;
        case EX_SUB: if (isfloat) d[i].f = fa - fb; else d[i].u = (uint32_t)ia - (uint32_t)ib; break;
        case EX_MUL: if (isfloat) d[i].f = fa * fb; else d[i].u = (uint32_t)ia * (uint32_t)ib; break;
        case EX_DIV:
            if (isfloat) d[i].f = fb != 0 ? fa / fb : (fa > 0 ? INFINITY : (fa < 0 ? -INFINITY : 0));
            else if (isuint) d[i].u = ib ? (uint32_t)ia / (uint32_t)ib : 0;
            else d[i].i = ib && !(ia == INT32_MIN && ib == -1) ? ia / ib : 0;
            break;
        case EX_MOD:
            if (isuint) d[i].u = ib ? (uint32_t)ia % (uint32_t)ib : 0;
            else d[i].i = ib && !(ia == INT32_MIN && ib == -1) ? ia % ib : 0;
            break;
        case EX_BAND: d[i].i = ia & ib; break;
        case EX_BOR:  d[i].i = ia | ib; break;
        case EX_BXOR: d[i].i = ia ^ ib; break;
        case EX_SHL:  d[i].u = (uint32_t)ia << ((uint32_t)ib & 31u); break;
        case EX_SHR:
            if (isuint) d[i].u = (uint32_t)ia >> ((uint32_t)ib & 31u);
            else d[i].i = ia >> ((uint32_t)ib & 31u);
            break;
        default: E->err = 1; break;
        }
    }
    if (op == EX_MOD || op == EX_BAND || op == EX_BOR || op == EX_BXOR ||
        op == EX_SHL || op == EX_SHR)
        rtype = isuint ? glsl_uint_type_of_len(n) : glsl_int_type_of_len(n);
    setval(&v, rtype, -1, d, n, 0, 0);
    return v;
}

static glsl_val compare(env *E, int op, const glsl_val *a, const glsl_val *b)
{
    int al = comp_len(a), bl = comp_len(b);
    int n = al > bl ? al : bl, i;
    int isfloat = !glsl_is_int_family(a->type) || !glsl_is_int_family(b->type);
    int isuint = glsl_is_uint(a->type) || glsl_is_uint(b->type);
    glsl_cell *d = sc(E, n);
    glsl_val v;
    for (i = 0; i < n; ++i) {
        float fa = cf(a->data, a->type, al == 1 ? 0 : i);
        float fb = cf(b->data, b->type, bl == 1 ? 0 : i);
        int ia = isfloat ? 0 : ci(a->data, a->type, al == 1 ? 0 : i);
        int ib = isfloat ? 0 : ci(b->data, b->type, bl == 1 ? 0 : i);
        int r = 0;
        switch (op) {
        case EX_LT: r = isfloat ? fa <  fb : isuint ? (uint32_t)ia <  (uint32_t)ib : ia <  ib; break;
        case EX_LE: r = isfloat ? fa <= fb : isuint ? (uint32_t)ia <= (uint32_t)ib : ia <= ib; break;
        case EX_GT: r = isfloat ? fa >  fb : isuint ? (uint32_t)ia >  (uint32_t)ib : ia >  ib; break;
        case EX_GE: r = isfloat ? fa >= fb : isuint ? (uint32_t)ia >= (uint32_t)ib : ia >= ib; break;
        case EX_EQ: r = isfloat ? fa == fb : ia == ib; break;
        case EX_NE: r = isfloat ? fa != fb : ia != ib; break;
        }
        d[i].i = r ? 1 : 0;
    }
    setval(&v, glsl_bool_type_of_len(n), -1, d, n, 0, 0);
    return v;
}

/* matrix column-major helpers */
static int mat_dim(int t) { return t == T_MAT2 ? 2 : t == T_MAT3 ? 3 : 4; }

static glsl_val matvec(env *E, const glsl_val *m, const glsl_val *v, int vec_on_left)
{
    int d = mat_dim(m->type), r, c;
    glsl_cell *o = sc(E, d);
    glsl_val out;
    for (c = 0; c < d; ++c) {
        float s = 0;
        for (r = 0; r < d; ++r) {
            /* column-major: data[col*d + row].  mat*vec wants result row c over
             * columns r (data[r*d + c]); vec*mat wants result column c over
             * rows r (data[c*d + r]).  Mixing these up transposes the matrix
             * and wrecks every projection / normal transform. */
            float mv = vec_on_left ? m->data[c * d + r].f
                                   : m->data[r * d + c].f;
            float vv = cf(v->data, v->type, r);
            s += mv * vv;
        }
        o[c].f = s;
    }
    setval(&out, glsl_float_type_of_len(d), -1, o, d, 0, 0);
    return out;
}

static void mat_mul(env *E, const glsl_val *a, const glsl_val *b, glsl_val *out)
{
    int d = mat_dim(a->type), i, j, k;
    glsl_cell *o = sc(E, d * d);
    /* result[col=d] = sum_k a[col=k] * b[k][d]  (column-major) */
    for (j = 0; j < d; ++j)          /* result column */
        for (i = 0; i < d; ++i) {    /* result row */
            float s = 0;
            for (k = 0; k < d; ++k)
                s += a->data[k * d + i].f * b->data[j * d + k].f;
            o[j * d + i].f = s;
        }
    setval(out, a->type, -1, o, d * d, 0, 0);
}

static glsl_val matscalar(env *E, const glsl_val *m, const glsl_val *s, int div)
{
    int n = m->n, i;
    glsl_cell *o = sc(E, n);
    glsl_val out;
    float f = cf(s->data, s->type, 0);
    for (i = 0; i < n; ++i)
        o[i].f = div ? (f != 0 ? m->data[i].f / f : 0) : m->data[i].f * f;
    setval(&out, m->type, -1, o, n, 0, 0);
    return out;
}

static glsl_val matadd(env *E, const glsl_val *a, const glsl_val *b, int sub)
{
    int n = a->n, i;
    glsl_cell *o = sc(E, n);
    glsl_val out;
    for (i = 0; i < n; ++i) o[i].f = sub ? a->data[i].f - b->data[i].f
                                         : a->data[i].f + b->data[i].f;
    setval(&out, a->type, -1, o, n, 0, 0);
    return out;
}

/* ------------------------------------------------------------------ */
/* constructors */
static glsl_val construct(env *E, int type, glsl_expr **args, int nargs)
{
    int total = glsl_base_comps(type);
    glsl_cell *d;
    glsl_val v;
    glsl_cell flat[64]; int nf = 0, i;
    d = sc(E, total > 0 ? total : 1);

    if (type == T_SAMPLER2D || type == T_SAMPLERCUBE) {
        d[0].i = 0; /* samplers are not runtime-constructed meaningfully */
        setval(&v, type, -1, d, 1, 0, 0);
        return v;
    }
    if (glsl_is_matrix(type)) {
        int dim = mat_dim(type);
        /* zero then fill diagonal / column-major from flattened args */
        for (i = 0; i < dim * dim; ++i) d[i].f = 0;
        if (nargs == 1) {
            glsl_val a = eval(E, args[0]);
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
            int idx = 0, c, r;
            for (i = 0; i < nargs && idx < dim*dim; ++i) {
                glsl_val a = eval(E, args[i]);
                int an = a.n, k;
                for (k = 0; k < an && idx < dim*dim; ++k) d[idx++].f = cf(a.data, a.type, k);
            }
            (void)c; (void)r;
        }
        setval(&v, type, -1, d, dim * dim, 0, 0);
        return v;
    }
    /* scalar / vector: flatten args */
    if (nargs == 1 && total > 1) {
        glsl_val a = eval(E, args[0]);
        if (a.n == 1) {
            for (i = 0; i < total; ++i)
                d[i] = convert_cell(type, a.data, a.type, 0);
        } else {
            for (i = 0; i < total && i < a.n; ++i)
                d[i] = convert_cell(type, a.data, a.type, i);
        }
        setval(&v, type, -1, d, total, 0, 0);
        return v;
    }
    for (i = 0; i < nargs && nf < 64; ++i) {
        glsl_val a = eval(E, args[i]);
        int k;
        for (k = 0; k < a.n && nf < 64; ++k) flat[nf++] = convert_cell(type, a.data, a.type, k);
    }
    for (i = 0; i < total; ++i) {
        if (i < nf) d[i] = flat[i]; else d[i].u = 0;
    }
    setval(&v, type, -1, d, total, 0, 0);
    return v;
}

/* Array constructor (GLSL ES 3.00 array literal): `elem_type[N](args...)`.
 * Unlike construct(), the result is an array value (array_len = N) whose cells
 * are the flattened args, each arg contributing its own components (so a scalar
 * arg fills one cell, a vec2 arg fills two).  Used for the city shader's
 * `const float FONT[64] = float[64](...)` / `const int TXT6[119] = int[119](...)`
 * font and text tables, which are evaluated once at link time by
 * glsl_exec_init_globals and written into the global store via write_lv. */
static glsl_val construct_array(env *E, int elem_type, int arr_len,
                                glsl_expr **args, int nargs)
{
    int elem_cells = glsl_base_comps(elem_type);
    int total, idx = 0, i;
    glsl_cell *d;
    glsl_val v;
    if (elem_cells < 1) elem_cells = 1;
    if (arr_len < 1) arr_len = 1;
    total = elem_cells * arr_len;
    d = sc(E, total);
    memset(d, 0, total * sizeof(glsl_cell));
    for (i = 0; i < nargs && idx < total; ++i) {
        glsl_val a = eval(E, args[i]);
        int k;
        for (k = 0; k < a.n && idx < total; ++k) {
            d[idx] = convert_cell(elem_type, a.data, a.type, k);
            idx++;
        }
    }
    setval(&v, elem_type, -1, d, total, arr_len, elem_cells);
    return v;
}

/* ------------------------------------------------------------------ */
/* builtin functions */
static float bfun1(int id, float x)
{
    switch (id) {
    case BF_RADIANS: return x * 0.017453292519943295f;
    case BF_DEGREES: return x * 57.29577951308232f;
    case BF_SIN: return sinf(x); case BF_COS: return cosf(x); case BF_TAN: return tanf(x);
    case BF_TANH: return tanhf(x);
    case BF_ROUND: return roundf(x);
    case BF_ASIN: return asinf(x); case BF_ACOS: return acosf(x);
    case BF_EXP: return expf(x); case BF_LOG: return logf(x);
    case BF_EXP2: return exp2f(x); case BF_LOG2: return log2f(x);
    case BF_SQRT: return x >= 0 ? sqrtf(x) : 0;
    case BF_INVSQRT: return x > 0 ? 1.0f / sqrtf(x) : 0;
    case BF_ABS: return fabsf(x);
    case BF_SIGN: return x > 0 ? 1 : x < 0 ? -1 : 0;
    case BF_FLOOR: return floorf(x); case BF_CEIL: return ceilf(x);
    case BF_FRACT: return x - floorf(x);
    default: return 0;
    }
}

static glsl_val gen_unary(env *E, int id, const glsl_val *a)
{
    int n = a->n, i; glsl_cell *d = sc(E, n); glsl_val v;
    for (i = 0; i < n; ++i) d[i].f = bfun1(id, cf(a->data, a->type, i));
    setval(&v, n == 1 ? T_FLOAT : glsl_float_type_of_len(n), -1, d, n, 0, 0);
    return v;
}

static glsl_val gen_binary(env *E, int id, const glsl_val *a, const glsl_val *b)
{
    int al = a->n, bl = b->n, n = al > bl ? al : bl, i;
    glsl_cell *d = sc(E, n); glsl_val v;
    for (i = 0; i < n; ++i) {
        float x = cf(a->data, a->type, al == 1 ? 0 : i);
        float y = cf(b->data, b->type, bl == 1 ? 0 : i), r = 0;
        switch (id) {
        case BF_ATAN2: r = atan2f(x, y); break;
        case BF_POW: r = x >= 0 ? powf(x, y) : 0; break;
        case BF_MOD: r = y != 0 ? x - y * floorf(x / y) : 0; break;
        case BF_MIN: r = x < y ? x : y; break;
        case BF_MAX: r = x > y ? x : y; break;
        case BF_STEP: r = y < x ? 0 : 1; break;
        default: break;
        }
        d[i].f = r;
    }
    setval(&v, n == 1 ? T_FLOAT : glsl_float_type_of_len(n), -1, d, n, 0, 0);
    return v;
}

static glsl_val gen_ternary(env *E, int id, const glsl_val *a, const glsl_val *b, const glsl_val *c)
{
    int al=a->n,bl=b->n,cl=c->n,n=al>bl?al:bl,i; glsl_cell*d=sc(E,n); glsl_val v;
    if (cl>n) n=cl;
    for (i=0;i<n;++i){
        float x=cf(a->data,a->type,al==1?0:i),y=cf(b->data,b->type,bl==1?0:i),z=cf(c->data,c->type,cl==1?0:i),r=0;
        if(id==BF_CLAMP) r = x<y?y:(x>z?z:x);
        else if(id==BF_MIX) r = x*(1-z)+y*z;
        else if(id==BF_SMOOTHSTEP){ float e=y==x?0:(z-x)/(y-x); if(e<0)e=0; if(e>1)e=1; r=e*e*(3-2*e); }
        d[i].f=r;
    }
    setval(&v,n==1?T_FLOAT:glsl_float_type_of_len(n),-1,d,n,0,0);
    return v;
}

static glsl_val builtin(env *E, int id, glsl_expr **args, int nargs)
{
    glsl_val a, b, c, v;
    int i;
    glsl_cell *d;
    switch (id) {
    case BF_RADIANS: case BF_DEGREES: case BF_SIN: case BF_COS: case BF_TAN:
    case BF_ASIN: case BF_ACOS: case BF_EXP: case BF_LOG: case BF_EXP2:
    case BF_LOG2: case BF_SQRT: case BF_INVSQRT: case BF_ABS: case BF_SIGN:
    case BF_FLOOR: case BF_CEIL: case BF_FRACT: case BF_TANH: case BF_ROUND:
        a = eval(E, args[0]); return gen_unary(E, id, &a);
    case BF_FLOAT_BITS_TO_UINT:
        a = eval(E, args[0]);
        return copy_val(E, glsl_uint_type_of_len(a.n), -1, a.data, a.n, 0, 0);
    case BF_ATAN:
        a = eval(E, args[0]);
        if (nargs >= 2) { b = eval(E, args[1]); return gen_binary(E, BF_ATAN2, &a, &b); }
        return gen_unary(E, BF_ATAN, &a);
    case BF_ATAN2: case BF_POW: case BF_MOD: case BF_MIN: case BF_MAX: case BF_STEP:
        a = eval(E, args[0]); b = eval(E, args[1]); return gen_binary(E, id, &a, &b);
    case BF_CLAMP: case BF_MIX: case BF_SMOOTHSTEP:
        a = eval(E, args[0]); b = eval(E, args[1]); c = eval(E, args[2]);
        return gen_ternary(E, id, &a, &b, &c);
    case BF_LENGTH: {
        float s = 0; a = eval(E, args[0]);
        for (i = 0; i < a.n; ++i) { float f = cf(a.data,a.type,i); s += f*f; }
        return make_scalar_f(E, sqrtf(s));
    }
    case BF_DISTANCE: {
        float s = 0; a = eval(E,args[0]); b = eval(E,args[1]);
        for (i = 0; i < a.n; ++i) { float f = cf(a.data,a.type,i)-cf(b.data,b.type,i); s+=f*f; }
        return make_scalar_f(E, sqrtf(s));
    }
    case BF_DOT: {
        float s = 0; a = eval(E,args[0]); b = eval(E,args[1]);
        for (i = 0; i < a.n; ++i) s += cf(a.data,a.type,i)*cf(b.data,b.type,i);
        return make_scalar_f(E, s);
    }
    case BF_CROSS: {
        glsl_cell *o = sc(E,3);
        a = eval(E,args[0]); b = eval(E,args[1]);
        o[0].f = cf(a.data,a.type,1)*cf(b.data,b.type,2)-cf(a.data,a.type,2)*cf(b.data,b.type,1);
        o[1].f = cf(a.data,a.type,2)*cf(b.data,b.type,0)-cf(a.data,a.type,0)*cf(b.data,b.type,2);
        o[2].f = cf(a.data,a.type,0)*cf(b.data,b.type,1)-cf(a.data,a.type,1)*cf(b.data,b.type,0);
        setval(&v,T_VEC3,-1,o,3,0,0); return v;
    }
    case BF_NORMALIZE: {
        float s=0,len; a = eval(E,args[0]);
        for(i=0;i<a.n;++i){float f=cf(a.data,a.type,i);s+=f*f;}
        len=sqrtf(s); d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].f = len>0? cf(a.data,a.type,i)/len : 0;
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_FACEFORWARD: {
        a=eval(E,args[0]);b=eval(E,args[1]);c=eval(E,args[2]);
        { float dt=0; for(i=0;i<b.n;++i) dt+=cf(c.data,c.type,i)*cf(b.data,b.type,i);
          d=sc(E,a.n); for(i=0;i<a.n;++i) d[i].f = dt<0? cf(a.data,a.type,i): -cf(a.data,a.type,i);
          setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v; }
    }
    case BF_REFLECT: {
        a=eval(E,args[0]);b=eval(E,args[1]);
        { float dt=0; for(i=0;i<a.n;++i) dt+=cf(a.data,a.type,i)*cf(b.data,b.type,i);
          d=sc(E,a.n); for(i=0;i<a.n;++i) d[i].f = cf(a.data,a.type,i)-2*dt*cf(b.data,b.type,i);
          setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v; }
    }
    case BF_REFRACT: {
        a=eval(E,args[0]);b=eval(E,args[1]);
        { float eta=cf(eval(E,args[2]).data,T_FLOAT,0);
          float ddot=0,k; for(i=0;i<a.n;++i) ddot+=cf(a.data,a.type,i)*cf(b.data,b.type,i);
          k=1-eta*eta*(1-ddot*ddot); d=sc(E,a.n);
          if(k<0){for(i=0;i<a.n;++i)d[i].f=0;}
          else {float f=eta*ddot+sqrtf(k); for(i=0;i<a.n;++i) d[i].f=eta*cf(a.data,a.type,i)-f*cf(b.data,b.type,i);}
          setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v; }
    }
    case BF_MATRIXCOMPMUL: {
        a=eval(E,args[0]);b=eval(E,args[1]); d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].f=cf(a.data,a.type,i)*cf(b.data,b.type,i);
        setval(&v,a.type,-1,d,a.n,0,0); return v;
    }
    case BF_TRANSPOSE: {
        int dim; a=eval(E,args[0]); dim=mat_dim(a.type); d=sc(E,dim*dim);
        { int cc,rr; for(cc=0;cc<dim;++cc)for(rr=0;rr<dim;++rr) d[cc*dim+rr].f=a.data[rr*dim+cc].f; }
        setval(&v,a.type,-1,d,dim*dim,0,0); return v;
    }
    case BF_DETERMINANT: {
        int dim; a=eval(E,args[0]); dim=mat_dim(a.type);
        if(dim==2){float r=a.data[0].f*a.data[3].f-a.data[2].f*a.data[1].f;return make_scalar_f(E,r);}
        if(dim==3){
            float m[9]; for(i=0;i<9;++i)m[i]=a.data[i].f;
            float r=m[0]*(m[4]*m[8]-m[7]*m[5])-m[3]*(m[1]*m[8]-m[7]*m[2])+m[6]*(m[1]*m[5]-m[4]*m[2]);
            return make_scalar_f(E,r);
        }
        if(dim==4){
            float *m=a.data? NULL:NULL; (void)m;
            float mm[16]; for(i=0;i<16;++i)mm[i]=a.data[i].f;
            float r=0; int c;
            for(c=0;c<4;++c){
                float sub[9]; int rr,kk,si=0;
                for(rr=1;rr<4;++rr)for(kk=0;kk<4;++kk){ if(kk==c)continue; sub[si++]=mm[rr*4+kk]; }
                float det3=sub[0]*(sub[4]*sub[8]-sub[7]*sub[5])-sub[3]*(sub[1]*sub[8]-sub[7]*sub[2])+sub[6]*(sub[1]*sub[5]-sub[4]*sub[2]);
                r += ((c&1)?-1:1)*mm[c]*det3;
            }
            return make_scalar_f(E,r);
        }
        return make_scalar_f(E,0);
    }
    case BF_INVERSE: { E->err = 0; return make_scalar_f(E, 0); /* rarely used */ }
    case BF_LESS_THAN: case BF_LESS_THAN_EQUAL: case BF_GREATER_THAN:
    case BF_GREATER_THAN_EQUAL: case BF_EQUAL: case BF_NOT_EQUAL: {
        int op = id==BF_LESS_THAN?EX_LT:id==BF_LESS_THAN_EQUAL?EX_LE:
                 id==BF_GREATER_THAN?EX_GT:id==BF_GREATER_THAN_EQUAL?EX_GE:
                 id==BF_EQUAL?EX_EQ:EX_NE;
        a=eval(E,args[0]); b=eval(E,args[1]); return compare(E,op,&a,&b);
    }
    case BF_ANY: case BF_ALL: {
        a=eval(E,args[0]);
        if(id==BF_ANY){for(i=0;i<a.n;++i) if(ci(a.data,a.type,i)) return make_bool(E,1); return make_bool(E,0);}
        for(i=0;i<a.n;++i) if(!ci(a.data,a.type,i)) return make_bool(E,0); return make_bool(E,1);
    }
    case BF_NOT: {
        a=eval(E,args[0]); d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].i = ci(a.data,a.type,i)?0:1;
        setval(&v,glsl_bool_type_of_len(a.n),-1,d,a.n,0,0); return v;
    }
    case BF_TEXTURE2D: {
        glsl_val uv; int unit; float rgba[4], co[2]; glsl_cell*o=sc(E,4);
        a=eval(E,args[0]); unit=ci(a.data,a.type,0);
        uv=eval(E,args[1]);
        rgba[0]=rgba[1]=rgba[2]=1; rgba[3]=1;
        co[0]=cf(uv.data,uv.type,0); co[1]=cf(uv.data,uv.type,1);
        if(E->texfn) E->texfn(unit, 2, co, rgba, E->texuser);
        for(i=0;i<4;++i) o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURE2DPROJ: {
        glsl_val uv; int unit; float rgba[4],w,co[2]; glsl_cell*o=sc(E,4);
        a=eval(E,args[0]); unit=ci(a.data,a.type,0);
        uv=eval(E,args[1]); w=cf(uv.data,uv.type,uv.n-1);
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=w!=0?cf(uv.data,uv.type,0)/w:0; co[1]=w!=0?cf(uv.data,uv.type,1)/w:0;
        if(E->texfn&&w!=0) E->texfn(unit, 2, co, rgba, E->texuser);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURECUBE: {
        glsl_val dir; int unit; float rgba[4],co[3]; glsl_cell*o=sc(E,4);
        a=eval(E,args[0]); unit=ci(a.data,a.type,0);
        dir=eval(E,args[1]);
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=cf(dir.data,dir.type,0); co[1]=cf(dir.data,dir.type,1); co[2]=cf(dir.data,dir.type,2);
        if(E->texfn) E->texfn(unit, 3, co, rgba, E->texuser);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_TEXTURE2DLOD: {
        glsl_val uv; int unit; float rgba[4],co[2]; glsl_cell*o=sc(E,4);
        a=eval(E,args[0]); unit=ci(a.data,a.type,0);
        uv=eval(E,args[1]);
        rgba[0]=rgba[1]=rgba[2]=1;rgba[3]=1;
        co[0]=cf(uv.data,uv.type,0); co[1]=cf(uv.data,uv.type,1);
        if(E->texfn) E->texfn(unit, 2, co, rgba, E->texuser);
        for(i=0;i<4;++i)o[i].f=rgba[i];
        setval(&v,T_VEC4,-1,o,4,0,0); return v;
    }
    case BF_DFDX: case BF_DFDY: case BF_FWIDTH:
        a=eval(E,args[0]); d=sc(E,a.n);
        for(i=0;i<a.n;++i) d[i].f=0;
        setval(&v,a.n==1?T_FLOAT:glsl_float_type_of_len(a.n),-1,d,a.n,0,0); return v;
    default:
        E->err = 1; return make_scalar_f(E, 0);
    }
}

/* ------------------------------------------------------------------ */
/* user function call */
static glsl_val call_user(env *E, const glsl_expr *e)
{
    const glsl_func *F = NULL;
    int i, save_stack, save_ret_type;
    glsl_cell *frame;
    const glsl_func *save_fn; glsl_cell *save_locals;
    glsl_val argv[GLSL_MAX_PARAMS];
    if (e->op_builtin < 0 || e->op_builtin >= E->stage->nfuncs) { E->err = 1; return make_scalar_f(E,0); }
    F = &E->stage->funcs[e->op_builtin];
    if (!F->body) { E->err = 1; return make_scalar_f(E,0); }
    for (i = 0; i < e->nargs && i < GLSL_MAX_PARAMS; ++i) argv[i] = eval(E, e->args[i]);
    save_stack = E->stack_top;
    frame = stk(E, F->frame_cells > 0 ? F->frame_cells : 1);
    memset(frame, 0, (F->frame_cells>0?F->frame_cells:1) * sizeof(glsl_cell));
    save_fn = E->fn; save_locals = E->locals;
    E->fn = F; E->locals = frame;
    /* bind in/inout params */
    for (i = 0; i < F->nparams && i < e->nargs && i < GLSL_MAX_PARAMS; ++i) {
        if (F->param_inout[i] == 1) continue; /* out: no copy in */
        {
            glsl_cell *dst = frame + F->local_off[i];
            int n = F->local_ncomp[i] * (F->local_array[i] > 0 ? F->local_array[i] : 1);
            int k;
            for (k = 0; k < n; ++k)
                if (glsl_is_int_family(F->local_type[i])) dst[k].i = ci(argv[i].data, argv[i].type, k < argv[i].n ? k : 0);
                else dst[k].f = cf(argv[i].data, argv[i].type, k < argv[i].n ? k : 0);
        }
    }
    save_ret_type = E->ret_type;
    E->ctrl = CTRL_NONE;
    {
        extern int glsl_exec_block(env *E, const glsl_stmt *s);
        glsl_exec_block(E, F->body);
    }
    /* The callee's `return` leaves E->ctrl == CTRL_RETURN.  That token is local
     * to the callee and MUST be consumed here: if it leaks into the caller, the
     * caller's glsl_exec_block / ST_FOR sees CTRL_RETURN and terminates the
     * caller's body (or breaks its loop) right after the call statement, so any
     * user function that calls another user function in a non-final statement
     * silently loses the rest of its body.  Only `discard` propagates out of a
     * call (it must reach the top to drop the fragment); break/continue are
     * always consumed by a loop inside the callee and never escape a function. */
    if (E->ctrl != CTRL_DISCARD) E->ctrl = CTRL_NONE;
    /* Restore the caller's frame BEFORE writing back out/inout params.  The
     * argument expressions e->args[i] belong to the caller's scope, so the
     * resolve_lv below must run against the caller's E->fn/E->locals.  The old
     * order restored them only after this loop, so resolve_lv resolved a caller
     * local (e.g. `fragColor` in `mainImage(fragColor, ...)`) against the
     * callee's frame and wrote the result back into the callee's own locals:
     * out/inout args never reached the caller, and every shadertoy-style
     * `void mainImage(out vec4 fragColor, ...)` returned black on the CPU
     * interpreter (the GPU/mesa path was unaffected).  The callee's param
     * storage stays readable through the saved `frame` pointer because
     * E->stack_top is restored only after the loop. */
    E->fn = save_fn; E->locals = save_locals;
    /* write back out/inout params */
    for (i = 0; i < F->nparams && i < e->nargs && i < GLSL_MAX_PARAMS; ++i) {
        if (F->param_inout[i] == 0) continue;
        {
            lv L; glsl_val src;
            /* target = the argument expression lvalue */
            resolve_lv(E, e->args[i], &L);
            if (L.valid) {
                int n = F->local_ncomp[i] * (F->local_array[i] > 0 ? F->local_array[i] : 1);
                src = copy_val(E, F->local_type[i], F->local_stid[i], frame + F->local_off[i], n, F->local_array[i], F->local_ncomp[i]);
                write_lv(E, &L, &src);
            }
        }
    }
    E->stack_top = save_stack;
    {
        glsl_val v;
        v.type = E->ret_type; v.st_id = E->ret_stid; v.n = E->ret_n;
        v.array_len = E->ret_arr; v.elem_cells = E->ret_elem;
        v.data = sc(E, E->ret_n > 0 ? E->ret_n : 1);
        if (E->ret_n > 0) memcpy(v.data, E->ret_buf, E->ret_n * sizeof(glsl_cell));
        E->ret_type = save_ret_type;
        if (F->ret_type == T_VOID) return make_scalar_f(E, 0);
        return v;
    }
}

/* ------------------------------------------------------------------ */
glsl_val eval(env *E, const glsl_expr *e)
{
    glsl_val v, a, b;
    if (!e) return make_scalar_f(E, 0);
    if (E->err) { setval(&v, T_FLOAT, -1, E->prog->scratch, 0, 0, 0); return v; }
    switch (e->kind) {
    case EX_FLOAT: return make_scalar_f(E, e->lit_f);
    case EX_INT:   return make_scalar_i(E, e->lit_i);
    case EX_UINT:  v = make_scalar_i(E, e->lit_i); v.type = T_UINT; return v;
    case EX_BOOL:  return make_bool(E, e->lit_i);
    case EX_VAR: { lv L; resolve_lv(E, e, &L); if(!L.valid) return make_scalar_f(E,0); return read_lv(E,&L); }
    case EX_INDEX: case EX_FIELD: { lv L; resolve_lv(E,e,&L); if(!L.valid) return make_scalar_f(E,0); return read_lv(E,&L); }
    case EX_CTOR:
        if (e->ctor_array > 0)
            return construct_array(E, e->ctor_type, e->ctor_array, e->args, e->nargs);
        return construct(E, e->ctor_type, e->args, e->nargs);
    case EX_CALL:
        if (e->is_user_fn) return call_user(E, e);
        return builtin(E, e->op_builtin, e->args, e->nargs);
    case EX_NEG: a=eval(E,e->a);
        { int i; glsl_cell*d=sc(E,a.n);
          for(i=0;i<a.n;++i) if(glsl_is_int_family(a.type)) d[i].u=0u-(uint32_t)ci(a.data,a.type,i); else d[i].f=-cf(a.data,a.type,i);
          setval(&v,a.type,-1,d,a.n,0,0); return v; }
    case EX_NOT: a=eval(E,e->a); return make_bool(E, !ci(a.data,a.type,0));
    case EX_BNOT: a=eval(E,e->a);
        { int i; glsl_cell*d=sc(E,a.n); for(i=0;i<a.n;++i)d[i].i=~ci(a.data,a.type,i);
          setval(&v,glsl_is_uint(a.type)?glsl_uint_type_of_len(a.n):glsl_int_type_of_len(a.n),-1,d,a.n,0,0); return v; }
    case EX_PREINC: case EX_PREDEC: case EX_POSTINC: case EX_POSTDEC: {
        lv L; glsl_val cur, one, res; int delta = (e->kind==EX_PREINC||e->kind==EX_POSTINC)?1:-1;
        resolve_lv(E,e->a,&L); if(!L.valid) return make_scalar_f(E,0);
        cur = read_lv(E,&L);
        one = glsl_is_int_family(cur.type)? make_scalar_i(E,delta): make_scalar_f(E,delta);
        res = arith(E, delta>0?EX_ADD:EX_SUB, &cur, &one);
        write_lv(E,&L,&res);
        return (e->kind==EX_PREINC||e->kind==EX_PREDEC)? res : cur;
    }
    case EX_ADD: case EX_SUB: case EX_MUL: case EX_DIV: case EX_MOD:
    case EX_BAND: case EX_BOR: case EX_BXOR: case EX_SHL: case EX_SHR:
        a=eval(E,e->a); b=eval(E,e->b);
        if (glsl_is_matrix(a.type) || glsl_is_matrix(b.type)) {
            if (e->kind==EX_MUL) {
                if (glsl_is_matrix(a.type)&&glsl_is_matrix(b.type)) { mat_mul(E,&a,&b,&v); return v; }
                if (glsl_is_matrix(a.type)) {
                    if (comp_len(&b)==1 && b.n==1) return matscalar(E,&a,&b,0);
                    return matvec(E,&a,&b,0);
                }
                /* b matrix */
                if (a.n==1 && comp_len(&a)==1) return matscalar(E,&b,&a,0);
                return matvec(E,&b,&a,1);
            }
            if (e->kind==EX_ADD) return matadd(E,&a,&b,0);
            if (e->kind==EX_SUB) return matadd(E,&a,&b,1);
            if (e->kind==EX_DIV && glsl_is_matrix(a.type)) return matscalar(E,&a,&b,1);
        }
        return arith(E,e->kind,&a,&b);
    case EX_LT: case EX_LE: case EX_GT: case EX_GE:
        a=eval(E,e->a); b=eval(E,e->b); return compare(E,e->kind,&a,&b);
    case EX_EQ: case EX_NE: {
        a=eval(E,e->a); b=eval(E,e->b);
        { int eq=1,i; int n=a.n>b.n?a.n:b.n;
          for(i=0;i<n;++i) {
              int ai=i<a.n?i:0, bi=i<b.n?i:0;
              int same = glsl_is_int_family(a.type) && glsl_is_int_family(b.type)
                  ? a.data[ai].u == b.data[bi].u
                  : cf(a.data,a.type,ai) == cf(b.data,b.type,bi);
              if(!same){eq=0;break;}
          }
          return make_bool(E, e->kind==EX_EQ? eq : !eq); }
    }
    case EX_LOGAND: a=eval(E,e->a); if(!ci(a.data,a.type,0)) return make_bool(E,0);
                    b=eval(E,e->b); return make_bool(E, ci(b.data,b.type,0)!=0);
    case EX_LOGOR:  a=eval(E,e->a); if(ci(a.data,a.type,0)) return make_bool(E,1);
                    b=eval(E,e->b); return make_bool(E, ci(b.data,b.type,0)!=0);
    case EX_LOGXOR: a=eval(E,e->a); b=eval(E,e->b);
                    return make_bool(E, (ci(a.data,a.type,0)?1:0)!=(ci(b.data,b.type,0)?1:0));
    case EX_TERNARY: a=eval(E,e->a); return ci(a.data,a.type,0)? eval(E,e->b): eval(E,e->cc);
    case EX_COMMA: eval(E,e->a); return eval(E,e->b);
    case EX_ASSIGN: { lv L; resolve_lv(E,e->a,&L); b=eval(E,e->b); if(L.valid) write_lv(E,&L,&b); return b; }
    case EX_ADD_ASSIGN: case EX_SUB_ASSIGN: case EX_MUL_ASSIGN: case EX_DIV_ASSIGN:
    case EX_MOD_ASSIGN: {
        lv L; int baseop; resolve_lv(E,e->a,&L); a=read_lv(E,&L); b=eval(E,e->b);
        baseop = e->kind==EX_ADD_ASSIGN?EX_ADD:e->kind==EX_SUB_ASSIGN?EX_SUB:
                 e->kind==EX_MUL_ASSIGN?EX_MUL:e->kind==EX_DIV_ASSIGN?EX_DIV:EX_MOD;
        if (e->kind==EX_MUL_ASSIGN && (glsl_is_matrix(a.type)||glsl_is_matrix(b.type))) {
            if (glsl_is_matrix(a.type)&&glsl_is_matrix(b.type)) mat_mul(E,&a,&b,&v);
            else if (glsl_is_matrix(a.type)) { if(b.n==1) v=matscalar(E,&a,&b,0); else v=matvec(E,&a,&b,0); }
            else { if(a.n==1) v=matscalar(E,&b,&a,0); else v=matvec(E,&b,&a,1); }
        } else v = arith(E,baseop,&a,&b);
        if(L.valid) write_lv(E,&L,&v); return v;
    }
    case EX_SHL_ASSIGN: case EX_SHR_ASSIGN: case EX_AND_ASSIGN:
    case EX_XOR_ASSIGN: case EX_OR_ASSIGN: {
        lv L; int baseop; resolve_lv(E,e->a,&L); a=read_lv(E,&L); b=eval(E,e->b);
        baseop = e->kind==EX_SHL_ASSIGN?EX_SHL:e->kind==EX_SHR_ASSIGN?EX_SHR:
                 e->kind==EX_AND_ASSIGN?EX_BAND:e->kind==EX_XOR_ASSIGN?EX_BXOR:EX_BOR;
        v = arith(E,baseop,&a,&b); if(L.valid) write_lv(E,&L,&v); return v;
    }
    default:
        E->err = 1; return make_scalar_f(E, 0);
    }
}

/* ------------------------------------------------------------------ */
int glsl_exec_block(env *E, const glsl_stmt *s);

static int exec_stmt(env *E, const glsl_stmt *s)
{
    int save_sc;
    if (!s || E->err) return CTRL_NONE;
    save_sc = E->scratch_top;
    switch (s->kind) {
    case ST_EXPR:
        if (s->expr) eval(E, s->expr);
        break;
    case ST_DECL: {
        const glsl_stmt *d = s;
        for (; d; d = d->next) {
            if (d->kind != ST_DECL) break;
            if (d->decl_init) {
                lv L; glsl_val val;
                /* build a pseudo EX_VAR to target the local */
                glsl_expr tmp;
                memset(&tmp, 0, sizeof tmp);
                tmp.kind = EX_VAR; tmp.var_scope = VARSCOPE_LOCAL; tmp.var_slot = d->decl_slot;
                resolve_lv(E, &tmp, &L);
                val = eval(E, d->decl_init);
                if (L.valid) write_lv(E, &L, &val);
            }
        }
        break;
    }
    case ST_BLOCK:
        glsl_exec_block(E, s);
        break;
    case ST_IF: {
        glsl_val c = eval(E, s->expr);
        if (ci(c.data, c.type, 0)) { if (s->body) exec_stmt(E, s->body); }
        else if (s->els) exec_stmt(E, s->els);
        break;
    }
    case ST_WHILE: {
        int guard = 0;
        for (;;) {
            glsl_val c;
            if (++guard > 10000000) { E->err = 1; break; }
            c = eval(E, s->expr);
            if (!ci(c.data, c.type, 0)) break;
            if (s->body) exec_stmt(E, s->body);
            if (E->ctrl == CTRL_BREAK) { E->ctrl = CTRL_NONE; break; }
            if (E->ctrl == CTRL_CONTINUE) E->ctrl = CTRL_NONE;
            if (E->ctrl == CTRL_RETURN || E->ctrl == CTRL_DISCARD) break;
        }
        break;
    }
    case ST_DOWHILE: {
        int guard = 0;
        for (;;) {
            glsl_val c;
            if (++guard > 10000000) { E->err = 1; break; }
            if (s->body) exec_stmt(E, s->body);
            if (E->ctrl == CTRL_BREAK) { E->ctrl = CTRL_NONE; break; }
            if (E->ctrl == CTRL_CONTINUE) E->ctrl = CTRL_NONE;
            if (E->ctrl == CTRL_RETURN || E->ctrl == CTRL_DISCARD) break;
            c = eval(E, s->expr);
            if (!ci(c.data, c.type, 0)) break;
        }
        break;
    }
    case ST_FOR: {
        int guard = 0;
        if (s->children) exec_stmt(E, s->children); /* init decls */
        if (s->expr) eval(E, s->expr);              /* init expression */
        for (;;) {
            if (++guard > 10000000) { E->err = 1; break; }
            if (s->cond2) { glsl_val c = eval(E, s->cond2); if (!ci(c.data,c.type,0)) break; }
            if (s->body) exec_stmt(E, s->body);
            if (E->ctrl == CTRL_BREAK) { E->ctrl = CTRL_NONE; break; }
            if (E->ctrl == CTRL_CONTINUE) E->ctrl = CTRL_NONE;
            if (E->ctrl == CTRL_RETURN || E->ctrl == CTRL_DISCARD) break;
            if (s->post) eval(E, s->post);
        }
        break;
    }
    case ST_BREAK:    E->ctrl = CTRL_BREAK; break;
    case ST_CONTINUE: E->ctrl = CTRL_CONTINUE; break;
    case ST_RETURN:
        if (s->expr) {
            glsl_val r = eval(E, s->expr);
            E->ret_type = r.type; E->ret_stid = r.st_id; E->ret_n = r.n;
            E->ret_arr = r.array_len; E->ret_elem = r.elem_cells;
            if (r.n > 0 && r.n <= 256) memcpy(E->ret_buf, r.data, r.n * sizeof(glsl_cell));
        } else { E->ret_type = T_VOID; E->ret_n = 0; }
        E->ctrl = CTRL_RETURN;
        break;
    case ST_DISCARD: E->ctrl = CTRL_DISCARD; break;
    default: break;
    }
    E->scratch_top = save_sc;
    return E->ctrl;
}

int glsl_exec_block(env *E, const glsl_stmt *blk)
{
    const glsl_stmt *s;
    if (!blk) return CTRL_NONE;
    for (s = blk->children; s; s = s->next) {
        exec_stmt(E, s);
        if (E->ctrl == CTRL_RETURN || E->ctrl == CTRL_DISCARD || E->err) break;
        /* break/continue propagate to the enclosing loop */
        if (E->ctrl == CTRL_BREAK || E->ctrl == CTRL_CONTINUE) break;
    }
    return E->ctrl;
}

/* expose discard flag through env (defined as macro field) */
#define E_DISCARD(E) ((E)->ctrl == CTRL_DISCARD)

/* ------------------------------------------------------------------ */
/* entry points */
static void run_main(env *E)
{
    const glsl_func *F;
    glsl_cell *frame;
    if (E->stage->main_idx < 0) { E->err = 1; return; }
    F = &E->stage->funcs[E->stage->main_idx];
    if (!F->body) { E->err = 1; return; }
    frame = stk(E, F->frame_cells > 0 ? F->frame_cells : 1);
    memset(frame, 0, (F->frame_cells>0?F->frame_cells:1)*sizeof(glsl_cell));
    E->fn = F; E->locals = frame; E->ctrl = CTRL_NONE;
    glsl_exec_block(E, F->body);
}

void glsl_run_vertex(glsl_program *p, const float *const *attribs, int nattribs,
                     const void *uniform_block, float *varyings_out,
                     float out_gl_position[4], int instance_id)
{
    env E;
    int i;
    /* This runs once per vertex.  Zero only what must start at 0 and SKIP the
     * 1 KiB ret_buf (it is always written by ST_RETURN before call_user reads
     * it), which was ~90ns/vertex of pure memset on the hot path.  ret_type..err
     * sit after ret_buf in the struct, so they are cleared explicitly. */
    memset(&E, 0, offsetof(env, ret_buf));
    E.ret_type = E.ret_stid = E.ret_n = E.ret_arr = E.ret_elem = E.err = 0;
    E.prog = p; E.stage = p->vs; E.is_vertex = 1;
    E.gstore = p->vs_gstore;
    if (p->vs_ginit && p->vs_gcells > 0)
        memcpy(p->vs_gstore, p->vs_ginit, p->vs_gcells * sizeof(glsl_cell));
    E.attribs = attribs; E.nattribs = nattribs;
    E.varyings_out = varyings_out;
    E.ublock = (const glsl_cell *)uniform_block;
    E.instance_id = instance_id;
    E.scratch_top = 0; E.stack_top = 0;
    if (!vm_run(p, p->vs, 1, &E))
        run_main(&E);
    for (i = 0; i < 4; ++i) out_gl_position[i] = E.gl_position[i];
}

void glsl_run_fragment(glsl_program *p, const float *varyings_in,
                       const void *uniform_block, const float gl_fragcoord[4],
                       const float gl_pointcoord[2], int front_facing,
                       glsl_tex_sample_fn texfn, void *texuser,
                       float out_gl_fragcolor[4], int *out_discard,
                       float *out_gl_fragdepth, int *out_wrote_depth)
{
    env E;
    int i;
    /* Once per pixel: skip the 1 KiB ret_buf memset (see glsl_run_vertex).  This
     * was the fixed ~105ns/px floor that capped even an empty shader at ~31 FPS
     * on 640x480.  ret_type..err follow ret_buf, so clear them explicitly. */
    memset(&E, 0, offsetof(env, ret_buf));
    E.ret_type = E.ret_stid = E.ret_n = E.ret_arr = E.ret_elem = E.err = 0;
    E.prog = p; E.stage = p->fs; E.is_vertex = 0;
    E.gstore = p->fs_gstore;
    if (p->fs_ginit && p->fs_gcells > 0)
        memcpy(p->fs_gstore, p->fs_ginit, p->fs_gcells * sizeof(glsl_cell));
    E.varyings_in = varyings_in;
    E.ublock = (const glsl_cell *)uniform_block;
    for (i = 0; i < 4; ++i) E.gl_fragcoord[i] = gl_fragcoord[i];
    for (i = 0; i < 2; ++i) E.gl_pointcoord[i] = gl_pointcoord[i];
    E.front_facing = front_facing;
    E.texfn = texfn; E.texuser = texuser;
    E.gl_fragdepth = gl_fragcoord[2];
    E.scratch_top = 0; E.stack_top = 0;
    E.gl_fragcolor[3] = 1;
    if (!vm_run(p, p->fs, 0, &E))
        run_main(&E);
    for (i = 0; i < 4; ++i) out_gl_fragcolor[i] = E.gl_fragcolor[i];
    if (out_discard) *out_discard = (E.ctrl == CTRL_DISCARD) ? 1 : 0;
    if (out_gl_fragdepth) *out_gl_fragdepth = E.gl_fragdepth;
    if (out_wrote_depth) *out_wrote_depth = E.wrote_depth ? E.wrote_depth
                                       : (p->fs && p->fs->uses_fragdepth ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* link-time global initialisation (reuses the evaluator) */
void glsl_exec_init_globals(glsl_program *p, glsl_stage *st, glsl_cell *gstore)
{
    env E;
    int i;
    if (!st || !gstore) return;
    memset(&E, 0, sizeof E);
    E.prog = p; E.stage = st; E.is_vertex = st->is_vertex;
    E.gstore = gstore;
    E.scratch_top = 0; E.stack_top = 0;
    /* give the evaluator a function frame in case an initialiser calls a
     * user function or references a local-shaped path */
    if (st->main_idx >= 0) {
        const glsl_func *F = &st->funcs[st->main_idx];
        glsl_cell *frame = stk(&E, F->frame_cells > 0 ? F->frame_cells : 1);
        memset(frame, 0, (F->frame_cells>0?F->frame_cells:1)*sizeof(glsl_cell));
        E.fn = F; E.locals = frame;
    }
    for (i = 0; i < st->nglobals; ++i) {
        if (st->globals[i].scope != VARSCOPE_GLOBAL) continue;
        if (!st->globals[i].const_init) continue;
        {
            glsl_expr tmp; lv L; glsl_val val;
            memset(&tmp, 0, sizeof tmp);
            tmp.kind = EX_VAR; tmp.var_scope = VARSCOPE_GLOBAL;
            tmp.var_slot = st->globals[i].slot;
            E.scratch_top = 0;
            resolve_lv(&E, &tmp, &L);
            val = eval(&E, st->globals[i].const_init);
            if (L.valid) write_lv(&E, &L, &val);
            st->globals[i].has_const_val = 1;
        }
    }
}

/* ================================================================== */
/* Register-bytecode VM.  #included here (not a separate TU) so it can */
/* call the static math helpers above verbatim => bit-identical output.*/
/* ================================================================== */
#include "glsl_vm.inc"
