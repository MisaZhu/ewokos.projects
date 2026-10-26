/*
 * spirv_interp.c - a self-contained tree-walking / CFG interpreter for the
 * SPIR-V subset real compute, vertex and fragment kernels emit.  It executes one
 * shader invocation against a spirv_resources bundle: descriptor-backed buffers
 * and images, per-location interface, push constants and the Vulkan built-ins.
 *
 * Design notes
 * ------------
 * Values are cached in a per-module scratch table indexed by SPIR-V id, so an
 * operand fetch is O(1) once its defining block has run.  Control flow is walked
 * as a CFG (block by block) so OpPhi can select by predecessor label; straight
 * line data flow within a block is evaluated in program order.  Variable storage
 * (Function / Private / Input / Output) lives in a bump arena that is reset --
 * not freed -- between invocations so a compute dispatch does not malloc per
 * work-item.
 *
 * The interpreter never mis-executes: any opcode, storage class, type or image
 * form outside the supported subset sets st->error and spirv_run returns
 * non-zero, letting the caller fall back to another path.
 */
#include "spirv_priv.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SV_MAX_COMP   16
#define MAX_CALL_DEPTH 32
#define INSN_BUDGET   50000000L
#define MAX_OUT_VARS  64

/* GLSL.std.450 extended-instruction numbers live in spirv_priv.h (shared with
 * the spirv_to_glsl transpiler). */

typedef struct interp {
    spirv_module    *m;
    spirv_resources *res;

    sval    *vals;      /* == m->scratch      */
    uint8_t *valid;     /* == m->scratch_valid */
    uint32_t n;         /* == m->scratch_n (>= bound) */

   uint32_t prev_label;
    long     budget;
    int      error;
    int      kill;
    int      depth;

    /* registered Output variables, copied back to res at the end of the run */
    uint32_t out_id[MAX_OUT_VARS];
    uint32_t out_type[MAX_OUT_VARS];
    uint8_t *out_base[MAX_OUT_VARS];
    int      nout;
} interp;

/* ---- little helpers ------------------------------------------------------- */

static void *arena_alloc(interp *st, size_t bytes)
{
    spirv_module *m = st->m;
    uint8_t *p;
    bytes = (bytes + 15u) & ~(size_t)15u;
    if (m->arena_used + bytes > m->arena_cap) {
        size_t need = m->arena_cap ? m->arena_cap : 4096;
        uint8_t *na;
        while (need < m->arena_used + bytes) need *= 2;
        na = (uint8_t *)realloc(m->arena, need);
        if (!na) { st->error = 1; return NULL; }
        m->arena = na; m->arena_cap = need;
    }
    p = m->arena + m->arena_used;
    m->arena_used += bytes;
    return p;
}

/* Fill an sval's shape fields from a type id. */
static void describe(spirv_module *m, uint32_t tid, sval *v)
{
    spirv_type *t;
    memset(v, 0, sizeof(*v));
    v->type = tid; v->klass = SV_UNDEF; v->n = 1; v->skind = SV_FLOAT;
    t = spirv_type_find(m, tid);
    if (!t) return;
    switch (t->op) {
    case OpTypeBool:
        v->klass = SV_BOOL; v->skind = SV_BOOL; v->n = 1; break;
    case OpTypeInt:
        v->klass = t->is_signed ? SV_INT : SV_UINT;
        v->skind = v->klass; v->sign = (uint8_t)t->is_signed; v->n = 1; break;
    case OpTypeFloat:
        v->klass = SV_FLOAT; v->skind = SV_FLOAT; v->n = 1; break;
    case OpTypeVector: {
        sval c; describe(m, t->elem, &c);
        v->klass = SV_VEC; v->n = (uint8_t)t->comps;
        v->skind = c.skind; v->sign = c.sign; break;
    }
    case OpTypeMatrix: {
        sval c; describe(m, t->elem, &c);   /* column vector */
        v->klass = SV_VEC; v->skind = SV_FLOAT;
        v->cols = (uint8_t)t->comps; v->rows = c.n;
        v->n = (uint8_t)(t->comps * c.n);
        break;
    }
    case OpTypeStruct:
        v->klass = SV_STRUCT; v->n = 0; break;
    case OpTypeImage: case OpTypeSampler: case OpTypeSampledImage:
        v->klass = SV_IMAGE; v->n = 1; break;
    case OpTypePointer:
        v->klass = SV_PTR; v->n = 1; break;
    default:
        break;
    }
}

static double comp_f(const sval *v, int i)
{
    uint32_t b;
    if (i < 0) i = 0;
    if (i >= v->n) i = v->n - 1;
    if (i < 0) return 0.0;
    b = v->u[i];
    switch (v->skind) {
    case SV_FLOAT: { float f; memcpy(&f, &b, 4); return (double)f; }
    case SV_INT:   return (double)(int32_t)b;
    case SV_UINT:  return (double)b;
    case SV_BOOL:  return b ? 1.0 : 0.0;
    default:       return 0.0;
    }
}

static int64_t comp_i(const sval *v, int i)
{
    uint32_t b;
    if (i < 0) i = 0;
    if (i >= v->n) i = v->n - 1;
    if (i < 0) return 0;
    b = v->u[i];
    return (v->skind == SV_INT || v->skind == SV_BOOL) ? (int64_t)(int32_t)b : (int64_t)b;
}

static uint32_t comp_u(const sval *v, int i)
{
    if (i < 0) i = 0;
    if (i >= v->n) i = v->n - 1;
    if (i < 0) return 0;
    return v->u[i];
}

static void set_f(sval *v, int i, double d)
{
    float f = (float)d;
    if (i >= 0 && i < SV_MAX_COMP) { memcpy(&v->u[i], &f, 4); }
}
static void set_i(sval *v, int i, int64_t x)
{
    if (i >= 0 && i < SV_MAX_COMP) v->u[i] = (uint32_t)(int32_t)x;
}
static void set_u(sval *v, int i, uint32_t x)
{
    if (i >= 0 && i < SV_MAX_COMP) v->u[i] = x;
}

/* ---- type geometry -------------------------------------------------------- */

static uint32_t struct_member_type(spirv_module *m, uint32_t struct_tid, uint32_t member)
{
    int32_t di;
    spirv_insn *in;
    if (struct_tid >= m->bound) return 0;
    di = m->def_at[struct_tid];
    if (di < 0) return 0;
    in = &m->insns[di];
    if (in->op != OpTypeStruct) return 0;
    if (2 + member >= in->wc) return 0;
    return in->w[2 + member];
}

static uint32_t member_offset(spirv_module *m, uint32_t struct_tid, uint32_t member)
{
    int off = spirv_deco_int(m, struct_tid, member, DecoOffset, -1);
    if (off >= 0)
        return (uint32_t)off;
    /* No Offset decoration: an interface block (an Input/Output struct such as
     * gl_PerVertex or a {vec4 Color; vec2 UV;} varying block) has no std140
     * member offsets, so lay its members out sequentially by natural size.  A
     * valid UBO/SSBO struct always carries Offset decorations and is unaffected.
     * fill_input / copyback_outputs use the same layout to map members to
     * consecutive locations, keeping access-chain offsets consistent. */
    {
        spirv_type *t = spirv_type_find(m, struct_tid);
        uint32_t i, acc = 0;
        if (!t)
            return 0;
        for (i = 0; i < member && i < t->nmembers; i++) {
            uint32_t mt = struct_member_type(m, struct_tid, i);
            uint32_t ms = spirv_type_size(m, mt, NULL);
            acc += ms ? ms : 16u;
        }
        return acc;
    }
}

static uint32_t elem_stride(spirv_module *m, uint32_t tid)
{
    spirv_type *t = spirv_type_find(m, tid);
    uint32_t s;
    if (!t) return 0;
    switch (t->op) {
    case OpTypeArray:
    case OpTypeRuntimeArray:
        s = (uint32_t)spirv_deco_int(m, tid, 0xffffffff, DecoArrayStride, 0);
        if (!s) s = spirv_type_size(m, t->elem, NULL);
        return s;
    case OpTypeMatrix:
        s = (uint32_t)spirv_deco_int(m, tid, 0xffffffff, DecoMatrixStride, 0);
        if (!s) { sval c; describe(m, t->elem, &c); s = (uint32_t)c.n * 4u; }
        return s;
    default:
        return spirv_type_size(m, tid, NULL);
    }
}

/* ---- descriptor binding lookup ------------------------------------------- */

static spirv_binding *find_binding(interp *st, uint32_t set, uint32_t binding)
{
    uint32_t i;
    spirv_resources *r = st->res;
    for (i = 0; i < r->num_bindings; i++)
        if (r->bindings[i].set == set && r->bindings[i].binding == binding)
            return &r->bindings[i];
    return NULL;
}

/* ---- memory read / write -------------------------------------------------- */

static int mem_load(interp *st, sval *out, const void *base, int64_t off, uint32_t tid)
{
    spirv_module *m = st->m;
    spirv_type *t = spirv_type_find(m, tid);
    const uint8_t *p;
    if (!base) { st->error = 1; return -1; }
    p = (const uint8_t *)base + off;
    describe(m, tid, out);
    if (!t) { st->error = 1; return -1; }
    switch (t->op) {
    case OpTypeBool:
        out->klass = SV_BOOL; out->skind = SV_BOOL; out->n = 1;
        out->u[0] = (*(const uint32_t *)p) ? 1u : 0u;
        return 0;
    case OpTypeInt:
    case OpTypeFloat: {
        uint32_t sz = t->width / 8u;
        out->n = 1;
        if (sz == 4) out->u[0] = *(const uint32_t *)p;
        else if (sz == 8) memcpy(out->u, p, 8);
        else if (sz == 2) {
            uint16_t v; memcpy(&v, p, 2);
            out->u[0] = (t->op == OpTypeInt && t->is_signed)
                        ? (uint32_t)(int32_t)(int16_t)v : (uint32_t)v;
        } else if (sz == 1) {
            uint8_t v = *p;
            out->u[0] = (t->op == OpTypeInt && t->is_signed)
                        ? (uint32_t)(int32_t)(int8_t)v : (uint32_t)v;
        } else { st->error = 1; return -1; }
        return 0;
    }
    case OpTypeVector: {
        uint32_t csize = spirv_type_size(m, t->elem, NULL);
        uint32_t i;
        if (csize == 0) csize = 4;
        out->n = (uint8_t)t->comps;
        for (i = 0; i < t->comps && i < SV_MAX_COMP; i++) {
            const uint8_t *q = p + (int64_t)i * csize;
            if (csize == 4) out->u[i] = *(const uint32_t *)q;
            else if (csize == 2) { uint16_t v; memcpy(&v, q, 2); out->u[i] = v; }
            else if (csize == 1) out->u[i] = *q;
            else if (csize == 8) { memcpy(&out->u[0], p, 8); break; }
            else { st->error = 1; return -1; }
        }
        return 0;
    }
    case OpTypeMatrix: {
        uint32_t ms = elem_stride(m, tid);
        uint32_t colv, rows, c, r;
        sval cv;
        if (!ms) ms = 16;
        colv = t->elem;
        describe(m, colv, &cv);
        rows = cv.n;
        out->n = (uint8_t)(t->comps * rows);
        out->cols = (uint8_t)t->comps; out->rows = (uint8_t)rows;
        out->skind = SV_FLOAT;
        for (c = 0; c < t->comps; c++)
            for (r = 0; r < rows; r++) {
                uint32_t bits;
                memcpy(&bits, p + (int64_t)c * ms + (int64_t)r * 4, 4);
                out->u[c * rows + r] = bits;
            }
        return 0;
    }
    case OpTypeStruct:
        out->klass = SV_STRUCT; out->agg = (void *)p; out->n = 0;
        return 0;
    default:
        st->error = 1; return -1;
    }
}

static int mem_store(interp *st, void *base, int64_t off, uint32_t tid, const sval *v)
{
    spirv_module *m = st->m;
    spirv_type *t = spirv_type_find(m, tid);
    uint8_t *p;
    uint32_t i;
    if (!base) { st->error = 1; return -1; }
    p = (uint8_t *)base + off;
    if (!t) { st->error = 1; return -1; }
    switch (t->op) {
    case OpTypeBool: {
        uint32_t b = v->u[0] ? 1u : 0u;
        *(uint32_t *)p = b; return 0;
    }
    case OpTypeInt:
    case OpTypeFloat: {
        uint32_t sz = t->width / 8u;
        if (sz == 4) *(uint32_t *)p = v->u[0];
        else if (sz == 8) memcpy(p, v->u, 8);
        else if (sz == 2) { uint16_t x = (uint16_t)v->u[0]; memcpy(p, &x, 2); }
        else if (sz == 1) *p = (uint8_t)v->u[0];
        else { st->error = 1; return -1; }
        return 0;
    }
    case OpTypeVector: {
        uint32_t csize = spirv_type_size(m, t->elem, NULL);
        if (csize == 0) csize = 4;
        for (i = 0; i < t->comps && i < SV_MAX_COMP; i++) {
            uint8_t *q = p + (int64_t)i * csize;
            if (csize == 4) *(uint32_t *)q = v->u[i];
            else if (csize == 2) { uint16_t x = (uint16_t)v->u[i]; memcpy(q, &x, 2); }
            else if (csize == 1) *q = (uint8_t)v->u[i];
            else if (csize == 8) { memcpy(p, v->u, 8); break; }
            else { st->error = 1; return -1; }
        }
        return 0;
    }
    case OpTypeMatrix: {
        uint32_t ms = elem_stride(m, tid);
        sval cv; uint32_t rows, c, r;
        if (!ms) ms = 16;
        describe(m, t->elem, &cv);
        rows = cv.n;
        for (c = 0; c < t->comps; c++)
            for (r = 0; r < rows; r++) {
                uint32_t b = v->u[c * rows + r];
                memcpy(p + (int64_t)c * ms + (int64_t)r * 4, &b, 4);
            }
        return 0;
    }
    case OpTypeStruct:
        /* whole-struct store: copy the aggregate bytes if sizes agree */
        if (v->klass == SV_STRUCT && v->agg) {
            uint32_t sz = spirv_type_size(m, tid, NULL);
            if (sz) memcpy(p, v->agg, sz);
        } else { st->error = 1; return -1; }
        return 0;
    default:
        st->error = 1; return -1;
    }
}

/* ---- constant materialisation -------------------------------------------- */

static int materialize_const(interp *st, uint32_t id);

static int fetch_const_composite(interp *st, uint32_t id, sval *out)
{
    spirv_module *m = st->m;
    int32_t di = (id < m->bound) ? m->def_at[id] : -1;
    spirv_insn *in;
    uint32_t k;
    if (di < 0) return -1;
    in = &m->insns[di];
    if (in->op != OpConstantComposite && in->op != OpSpecConstantComposite)
        return -1;
    describe(m, in->w[1], out);
    out->n = 0;
    for (k = 3; k < in->wc; k++) {
        sval c;
        uint32_t j;
        if (materialize_const(st, in->w[k]) != 0) return -1;
        c = st->vals[in->w[k]];
        if (c.klass == SV_STRUCT) { out->klass = SV_STRUCT; out->agg = c.agg; return 0; }
        for (j = 0; j < c.n && out->n < SV_MAX_COMP; j++)
            out->u[out->n++] = c.u[j];
    }
    return 0;
}

static int materialize_const(interp *st, uint32_t id)
{
    spirv_module *m = st->m;
    spirv_const *c;
    sval *out;
    if (id >= m->bound) return -1;
    if (st->valid[id]) return 0;
    if (fetch_const_composite(st, id, &st->vals[id]) == 0) {
        st->valid[id] = 1;
        return 0;
    }
    c = spirv_const_find(m, id);
    if (!c) return -1;
    out = &st->vals[id];
    describe(m, c->type, out);
    if (c->is_null) {
        memset(out->u, 0, sizeof(out->u));
        out->n = (out->klass == SV_VEC || out->klass == SV_STRUCT) ? out->n : 1;
        if (out->klass == SV_STRUCT) { out->agg = NULL; }
    } else {
        uint32_t i;
        out->n = (uint8_t)(c->n ? c->n : 1);
        if (out->klass == SV_BOOL) out->u[0] = c->u[0] ? 1u : 0u;
        else for (i = 0; i < c->n && i < SV_MAX_COMP; i++) out->u[i] = c->u[i];
        if (out->klass == SV_VEC && out->cols == 0) { /* vector of scalar */ }
    }
    st->valid[id] = 1;
    return 0;
}


/* ---- forward decls -------------------------------------------------------- */
static int exec_insn(interp *st, const spirv_insn *in);
static int exec_func(interp *st, spirv_func *f, const sval *args, uint32_t nargs,
                     sval *ret);

/* ---- operand fetch -------------------------------------------------------- */
static sval *fetch(interp *st, uint32_t id)
{
    if (id >= st->m->bound) { st->error = 1; return NULL; }
    if (st->valid[id]) return &st->vals[id];
    if (materialize_const(st, id) == 0) return &st->vals[id];
    st->error = 1;
    return NULL;
}

/* ---- interface / descriptor variable setup -------------------------------- */

static uint32_t var_bytes(spirv_module *m, uint32_t pointee)
{
    uint32_t sz = spirv_type_size(m, pointee, NULL);
    if (sz == 0) {
        spirv_type *t = spirv_type_find(m, pointee);
        if (t && t->op == OpTypeStruct) {
            /* sum member extents from Offset decorations */
            uint32_t i, end = 0;
            for (i = 0; i < t->nmembers; i++) {
                uint32_t mo = member_offset(m, pointee, i);
                uint32_t mt = struct_member_type(m, pointee, i);
                uint32_t ms = spirv_type_size(m, mt, NULL);
                if (mo + ms > end) end = mo + ms;
            }
            sz = end;
        }
    }
    return sz;
}

static void fill_input(interp *st, uint32_t var_id, uint32_t pointee, uint8_t *b)
{
    spirv_module *m = st->m;
    spirv_resources *r = st->res;
    int builtin = spirv_deco_int(m, var_id, 0xffffffff, DecoBuiltIn, -1);
    int loc, i;
    if (builtin >= 0) {
        switch (builtin) {
        case BiVertexIndex:  *(uint32_t *)b = r->vertex_index; return;
        case BiInstanceIndex:*(uint32_t *)b = r->instance_index; return;
        case BiLocalInvocationIndex: *(uint32_t *)b = r->local_index; return;
        case BiGlobalInvocationId:
            for (i = 0; i < 3; i++) ((uint32_t *)b)[i] = r->global_id[i];
            return;
        case BiLocalInvocationId:
            for (i = 0; i < 3; i++) ((uint32_t *)b)[i] = r->local_id[i];
            return;
        case BiWorkgroupId:
            for (i = 0; i < 3; i++) ((uint32_t *)b)[i] = r->workgroup_id[i];
            return;
        case BiNumWorkgroups:
            for (i = 0; i < 3; i++) ((uint32_t *)b)[i] = r->num_wg[i];
            return;
        case BiWorkgroupSize:
            for (i = 0; i < 3; i++) ((uint32_t *)b)[i] = r->wg_size[i];
            return;
        case BiFragCoord:
            for (i = 0; i < 4; i++) ((float *)b)[i] = r->frag_coord[i];
            return;
        case BiFrontFacing:
            *(uint32_t *)b = r->front_facing ? 1u : 0u; return;
        default: return;   /* unknown input builtin: leave zeroed */
        }
    }
    loc = spirv_deco_int(m, var_id, 0xffffffff, DecoLocation, -1);
    if (loc >= 0) {
        spirv_type *t = spirv_type_find(m, pointee);
        if (t && t->op == OpTypeStruct) {
            /* Interface block: each member occupies consecutive locations and
             * lives at its sequential byte offset within the variable buffer
             * (matching member_offset).  ImGui's fragment input
             * {vec4 Color; vec2 UV;} at location 0 maps Color->0, UV->1. */
            uint32_t mj, l = (uint32_t)loc;
            for (mj = 0; mj < t->nmembers; mj++) {
                uint32_t mt = struct_member_type(m, pointee, mj);
                uint32_t mo = member_offset(m, pointee, mj);
                spirv_type *mtt = spirv_type_find(m, mt);
                uint32_t nc = (mtt && mtt->op == OpTypeVector) ? mtt->comps : 1;
                if (nc > 4) nc = 4;
                if (l < SPIRV_MAX_LOCATIONS)
                    for (i = 0; (uint32_t)i < nc; i++)
                        ((float *)(b + mo))[i] = r->in_loc[l][i];
                l += (mtt && mtt->op == OpTypeMatrix) ? mtt->comps : 1u;
            }
        } else if (loc < SPIRV_MAX_LOCATIONS) {
            uint32_t nc = (t && t->op == OpTypeVector) ? t->comps : 1;
            if (nc > 4) nc = 4;
            for (i = 0; (uint32_t)i < nc; i++) ((float *)b)[i] = r->in_loc[loc][i];
        }
    }
}

/* output copyback registry lives in the interp */

static void register_output(interp *st, uint32_t var_id, uint32_t pointee, uint8_t *base);

static int setup_global_var(interp *st, uint32_t var_id, uint32_t ptr_type_id,
                            uint32_t storage, uint32_t init)
{
    spirv_module *m = st->m;
    spirv_type *pt = spirv_type_find(m, ptr_type_id);
    uint32_t pointee = pt ? pt->elem : 0;
    sval v;
    int set, binding;
    memset(&v, 0, sizeof(v));
    v.type = ptr_type_id; v.klass = SV_PTR; v.n = 1;
    v.ptr.elem = pointee; v.ptr.storage = storage; v.ptr.off = 0;
    v.ptr.base = NULL; v.ptr.binding = 0xffffffffu;

    switch (storage) {
    case SC_Input: {
        uint32_t sz = var_bytes(m, pointee); if (!sz) sz = 64;
        uint8_t *b = (uint8_t *)arena_alloc(st, sz);
        if (!b) return -1;
        memset(b, 0, sz);
        fill_input(st, var_id, pointee, b);
        v.ptr.base = b;
        break;
    }
    case SC_Output: {
        uint32_t sz = var_bytes(m, pointee); if (!sz) sz = 64;
        uint8_t *b = (uint8_t *)arena_alloc(st, sz);
        if (!b) return -1;
        memset(b, 0, sz);
        v.ptr.base = b;
        register_output(st, var_id, pointee, b);
        break;
    }
    case SC_Private: {
        uint32_t sz = var_bytes(m, pointee); if (!sz) sz = 64;
        uint8_t *b = (uint8_t *)arena_alloc(st, sz);
        if (!b) return -1;
        memset(b, 0, sz);
        if (init && materialize_const(st, init) == 0)
            mem_store(st, b, 0, pointee, &st->vals[init]);
        v.ptr.base = b;
        break;
    }
    case SC_PushConstant:
        v.ptr.base = (void *)st->res->push;
        break;
    case SC_Uniform:
    case SC_StorageBuffer:
    case SC_UniformConstant:
        set = spirv_deco_int(m, var_id, 0xffffffff, DecoDescriptorSet, 0);
        binding = spirv_deco_int(m, var_id, 0xffffffff, DecoBinding, 0);
        {
            spirv_binding *bb = find_binding(st, (uint32_t)set, (uint32_t)binding);
            if (bb) {
                v.ptr.base = bb->data;
                v.ptr.binding = (uint32_t)(bb - st->res->bindings);
            }
        }
        break;
    default:
        st->error = 1;
        return -1;
    }
    st->vals[var_id] = v;
    st->valid[var_id] = 1;
    return 0;
}

/* ---- access chain --------------------------------------------------------- */
static int do_access_chain(interp *st, const sval *base, const uint32_t *idxids,
                           uint32_t nidx, sval *out)
{
    spirv_module *m = st->m;
    int64_t off = base->ptr.off;
    uint32_t elem = base->ptr.elem;
    uint32_t i;
    *out = *base;                     /* inherit storage + binding + base */
    for (i = 0; i < nidx; i++) {
        sval *iv = fetch(st, idxids[i]);
        int64_t idx = iv ? comp_i(iv, 0) : 0;
        spirv_type *t = spirv_type_find(m, elem);
        if (!t) { st->error = 1; return -1; }
        switch (t->op) {
        case OpTypeStruct: {
            uint32_t mo = member_offset(m, elem, (uint32_t)idx);
            uint32_t mt = struct_member_type(m, elem, (uint32_t)idx);
            off += (int64_t)mo; elem = mt;
            break;
        }
        case OpTypeArray:
        case OpTypeRuntimeArray:
        case OpTypeMatrix: {
            uint32_t s = elem_stride(m, elem);
            off += idx * (int64_t)s; elem = t->elem;
            break;
        }
        case OpTypeVector: {
            uint32_t cs = spirv_type_size(m, t->elem, NULL); if (!cs) cs = 4;
            off += idx * (int64_t)cs; elem = t->elem;
            break;
        }
        default:
            st->error = 1; return -1;
        }
    }
    out->ptr.off = off;
    out->ptr.elem = elem;
    return 0;
}

/* ---- arithmetic / logic helpers ------------------------------------------ */

static void bin_index(const sval *v, int i, int n_out, int *oi)
{
    if (v->n == 1) *oi = 0;
    else { *oi = i; if (*oi >= v->n) *oi = v->n - 1; }
    (void)n_out;
}

static void arith_binary(sval *r, const sval *a, const sval *b, uint32_t op)
{
    int i;
    for (i = 0; i < r->n; i++) {
        int ai, bi;
        bin_index(a, i, r->n, &ai);
        bin_index(b, i, r->n, &bi);
        switch (op) {
        case OpIAdd: set_i(r, i, comp_i(a, ai) + comp_i(b, bi)); break;
        case OpISub: set_i(r, i, comp_i(a, ai) - comp_i(b, bi)); break;
        case OpIMul: set_i(r, i, comp_i(a, ai) * comp_i(b, bi)); break;
        case OpFAdd: set_f(r, i, comp_f(a, ai) + comp_f(b, bi)); break;
        case OpFSub: set_f(r, i, comp_f(a, ai) - comp_f(b, bi)); break;
        case OpFMul: set_f(r, i, comp_f(a, ai) * comp_f(b, bi)); break;
        case OpSDiv: { int64_t d = comp_i(b, bi); set_i(r, i, d ? comp_i(a, ai) / d : 0); break; }
        case OpUDiv: { uint32_t d = comp_u(b, bi); set_u(r, i, d ? comp_u(a, ai) / d : 0u); break; }
        case OpFDiv: { double d = comp_f(b, bi); set_f(r, i, d != 0.0 ? comp_f(a, ai) / d : 0.0); break; }
        case OpSRem: case OpSMod: { int64_t d = comp_i(b, bi); set_i(r, i, d ? comp_i(a, ai) % d : 0); break; }
        case OpUMod: { uint32_t d = comp_u(b, bi); set_u(r, i, d ? comp_u(a, ai) % d : 0u); break; }
        case OpFRem: case OpFMod: { double d = comp_f(b, bi); set_f(r, i, d != 0.0 ? fmod(comp_f(a, ai), d) : 0.0); break; }
        case OpBitwiseOr:  set_u(r, i, comp_u(a, ai) | comp_u(b, bi)); break;
        case OpBitwiseXor: set_u(r, i, comp_u(a, ai) ^ comp_u(b, bi)); break;
        case OpBitwiseAnd: set_u(r, i, comp_u(a, ai) & comp_u(b, bi)); break;
        case OpShiftLeftLogical:   set_u(r, i, comp_u(a, ai) << (comp_u(b, bi) & 31u)); break;
        case OpShiftRightLogical:  set_u(r, i, comp_u(a, ai) >> (comp_u(b, bi) & 31u)); break;
        case OpShiftRightArithmetic: set_i(r, i, comp_i(a, ai) >> (comp_u(b, bi) & 31u)); break;
        default: break;
        }
    }
}

static void cmp_binary(sval *r, const sval *a, const sval *b, uint32_t op)
{
    int i;
    for (i = 0; i < r->n; i++) {
        int ai, bi, res = 0;
        double fa, fb;
        bin_index(a, i, r->n, &ai);
        bin_index(b, i, r->n, &bi);
        fa = comp_f(a, ai); fb = comp_f(b, bi);
        switch (op) {
        case OpIEqual:            res = comp_i(a, ai) == comp_i(b, bi); break;
        case OpINotEqual:         res = comp_i(a, ai) != comp_i(b, bi); break;
        case OpUGreaterThan:      res = comp_u(a, ai) >  comp_u(b, bi); break;
        case OpSGreaterThan:      res = comp_i(a, ai) >  comp_i(b, bi); break;
        case OpUGreaterThanEqual: res = comp_u(a, ai) >= comp_u(b, bi); break;
        case OpSGreaterThanEqual: res = comp_i(a, ai) >= comp_i(b, bi); break;
        case OpULessThan:         res = comp_u(a, ai) <  comp_u(b, bi); break;
        case OpSLessThan:         res = comp_i(a, ai) <  comp_i(b, bi); break;
        case OpULessThanEqual:    res = comp_u(a, ai) <= comp_u(b, bi); break;
        case OpSLessThanEqual:    res = comp_i(a, ai) <= comp_i(b, bi); break;
        case OpFOrdEqual: case OpFUnordEqual:                 res = fa == fb; break;
        case OpFOrdNotEqual: case OpFUnordNotEqual:           res = fa != fb; break;
        case OpFOrdLessThan: case OpFUnordLessThan:           res = fa <  fb; break;
        case OpFOrdGreaterThan: case OpFUnordGreaterThan:     res = fa >  fb; break;
        case OpFOrdLessThanEqual: case OpFUnordLessThanEqual: res = fa <= fb; break;
        case OpFOrdGreaterThanEqual: case OpFUnordGreaterThanEqual: res = fa >= fb; break;
        case OpLogicalEqual:      res = (comp_u(a, ai) != 0) == (comp_u(b, bi) != 0); break;
        case OpLogicalNotEqual:   res = (comp_u(a, ai) != 0) != (comp_u(b, bi) != 0); break;
        case OpLogicalAnd:        res = (comp_u(a, ai) != 0) && (comp_u(b, bi) != 0); break;
        case OpLogicalOr:         res = (comp_u(a, ai) != 0) || (comp_u(b, bi) != 0); break;
        default: break;
        }
        r->u[i] = res ? 1u : 0u;
    }
    r->skind = SV_BOOL;
}

static void unary_op(sval *r, const sval *a, uint32_t op)
{
    int i;
    for (i = 0; i < r->n; i++) {
        int ai; bin_index(a, i, r->n, &ai);
        switch (op) {
        case OpSNegate: set_i(r, i, -comp_i(a, ai)); break;
        case OpFNegate: set_f(r, i, -comp_f(a, ai)); break;
        case OpNot:     set_u(r, i, ~comp_u(a, ai)); break;
        case OpLogicalNot: r->u[i] = (comp_u(a, ai) == 0) ? 1u : 0u; break;
        case OpConvertFToU: set_u(r, i, (uint32_t)comp_f(a, ai)); break;
        case OpConvertFToS: set_i(r, i, (int32_t)comp_f(a, ai)); break;
        case OpConvertSToF: set_f(r, i, (double)comp_i(a, ai)); break;
        case OpConvertUToF: set_f(r, i, (double)comp_u(a, ai)); break;
        case OpUConvert: set_u(r, i, comp_u(a, ai)); break;
        case OpSConvert: set_i(r, i, comp_i(a, ai)); break;
        case OpFConvert: set_f(r, i, comp_f(a, ai)); break;
        case OpBitcast:  r->u[i] = a->u[ai]; break;
        default: break;
        }
    }
}

static void vec_math(sval *r, const sval *a, const sval *b, uint32_t op, int *err)
{
    int i;
    switch (op) {
    case OpDot: {
        double s = 0.0;
        for (i = 0; i < a->n; i++) s += comp_f(a, i) * comp_f(b, i);
        r->n = 1; set_f(r, 0, s);
        break;
    }
    case OpVectorTimesScalar: {
        double s = comp_f(b, 0);
        for (i = 0; i < r->n; i++) set_f(r, i, comp_f(a, i) * s);
        break;
    }
    case OpMatrixTimesScalar: {
        double s = comp_f(b, 0);
        for (i = 0; i < r->n; i++) set_f(r, i, comp_f(a, i) * s);
        break;
    }
    case OpMatrixTimesVector: {   /* M(cols x rows) * v(cols) -> rows */
        int cols = a->cols, rows = a->rows, c, rr;
        for (rr = 0; rr < rows; rr++) {
            double s = 0.0;
            for (c = 0; c < cols; c++) s += comp_f(a, c * rows + rr) * comp_f(b, c);
            set_f(r, rr, s);
        }
        r->n = (uint8_t)rows;
        break;
    }
    case OpVectorTimesMatrix: {   /* v(rows) * M(cols x rows) -> cols */
        int cols = b->cols, rows = b->rows, c, rr;
        for (c = 0; c < cols; c++) {
            double s = 0.0;
            for (rr = 0; rr < rows; rr++) s += comp_f(a, rr) * comp_f(b, c * rows + rr);
            set_f(r, c, s);
        }
        r->n = (uint8_t)cols;
        break;
    }
    case OpMatrixTimesMatrix: {   /* A(ca x ra) * B(cb x rb), ca==rb -> (cb x ra) */
        int ca = a->cols, ra = a->rows, cb = b->cols, c, rr, k;
        r->cols = (uint8_t)cb; r->rows = (uint8_t)ra; r->n = (uint8_t)(cb * ra);
        for (c = 0; c < cb; c++)
            for (rr = 0; rr < ra; rr++) {
                double s = 0.0;
                for (k = 0; k < ca; k++)
                    s += comp_f(a, k * ra + rr) * comp_f(b, c * ca + k);
                set_f(r, c * ra + rr, s);
            }
        break;
    }
    case OpOuterProduct: {   /* a(len m) outer b(len n) -> matrix cols=n rows=m */
        int m = a->n, n = b->n, c, rr;
        r->cols = (uint8_t)n; r->rows = (uint8_t)m; r->n = (uint8_t)(m * n);
        for (c = 0; c < n; c++)
            for (rr = 0; rr < m; rr++)
                set_f(r, c * m + rr, comp_f(a, rr) * comp_f(b, c));
        break;
    }
    default: *err = 1; break;
    }
}

static void do_select(sval *r, const sval *cond, const sval *o1, const sval *o2)
{
    int i;
    for (i = 0; i < r->n; i++) {
        int ci = (cond->n == 1) ? 0 : i;
        int ai, bi;
        bin_index(o1, i, r->n, &ai);
        bin_index(o2, i, r->n, &bi);
        r->u[i] = (comp_u(cond, ci) != 0) ? o1->u[ai] : o2->u[bi];
    }
}

/* ---- composite ops -------------------------------------------------------- */

static void composite_extract(interp *st, sval *r, const sval *base,
                              const uint32_t *idx, uint32_t nidx)
{
    spirv_module *m = st->m;
    if (base->klass == SV_STRUCT && base->agg) {
        /* walk struct members by offset, then load the final type */
        const uint8_t *p = (const uint8_t *)base->agg;
        uint32_t tid = base->type, i;
        for (i = 0; i < nidx; i++) {
            spirv_type *t = spirv_type_find(m, tid);
            if (!t) { st->error = 1; return; }
            if (t->op == OpTypeStruct) {
                p += member_offset(m, tid, idx[i]);
                tid = struct_member_type(m, tid, idx[i]);
            } else if (t->op == OpTypeArray || t->op == OpTypeRuntimeArray) {
                p += (int64_t)idx[i] * elem_stride(m, tid);
                tid = t->elem;
            } else if (t->op == OpTypeMatrix) {
                sval cv; describe(m, t->elem, &cv);
                p += (int64_t)idx[i] * elem_stride(m, tid);
                tid = t->elem;
                (void)cv;
            } else if (t->op == OpTypeVector) {
                uint32_t cs = spirv_type_size(m, t->elem, NULL); if (!cs) cs = 4;
                p += (int64_t)idx[i] * cs;
                tid = t->elem;
            } else { st->error = 1; return; }
        }
        mem_load(st, r, p, 0, r->type);
        return;
    }
    /* vector / matrix / scalar held in u[] */
    if (nidx == 0) { *r = *base; r->type = r->type; return; }
    if (base->cols && base->rows) {
        /* matrix */
        int col = (int)idx[0];
        if (nidx == 1) {
            int rows = base->rows, rr;
            describe(m, r->type, r);
            for (rr = 0; rr < rows; rr++) r->u[rr] = base->u[col * rows + rr];
        } else {
            int rows = base->rows, row = (int)idx[1];
            describe(m, r->type, r);
            r->u[0] = base->u[col * rows + row];
        }
        return;
    }
    describe(m, r->type, r);
    {
        int i, src = (int)idx[0];
        if (r->n == 1) {
            r->u[0] = base->u[src];
        } else {
            /* extracting a sub-vector is uncommon; copy contiguous */
            for (i = 0; i < r->n; i++) r->u[i] = base->u[src + i];
        }
    }
}


/* ---- output copyback ------------------------------------------------------ */

static void register_output(interp *st, uint32_t var_id, uint32_t pointee, uint8_t *base)
{
    if (st->nout >= MAX_OUT_VARS) return;
    st->out_id[st->nout] = var_id;
    st->out_type[st->nout] = pointee;
    st->out_base[st->nout] = base;
    st->nout++;
}

static void copyback_outputs(interp *st)
{
    spirv_module *m = st->m;
    spirv_resources *r = st->res;
    int k, i;
    for (k = 0; k < st->nout; k++) {
        uint32_t var = st->out_id[k], tid = st->out_type[k];
        uint8_t *b = st->out_base[k];
        int builtin = spirv_deco_int(m, var, 0xffffffff, DecoBuiltIn, -1);
        spirv_type *t = spirv_type_find(m, tid);
        sval v;

        /* Interface blocks (gl_PerVertex, or a struct of user varyings such as
         * ImGui's {vec4 Color; vec2 UV;}) carry their BuiltIn / Location on each
         * MEMBER, not on the variable.  Expand every member to its own slot so
         * gl_Position (a member builtin) and multi-member varying structs are
         * captured; each member consumes consecutive locations. */
        if (t && t->op == OpTypeStruct) {
            int var_loc = spirv_deco_int(m, var, 0xffffffff, DecoLocation, -1);
            uint32_t mj, l = (uint32_t)(var_loc >= 0 ? var_loc : 0);
            for (mj = 0; mj < t->nmembers; mj++) {
                uint32_t mt = struct_member_type(m, tid, mj);
                uint32_t mo = member_offset(m, tid, mj);
                spirv_type *mtt = spirv_type_find(m, mt);
                int mb = spirv_deco_int(m, tid, mj, DecoBuiltIn, -1);
                sval mv;
                if (mem_load(st, &mv, b, mo, mt) != 0) {
                    st->error = 0;
                } else if (mb == BiPosition) {
                    for (i = 0; i < 4; i++) r->position[i] = (float)comp_f(&mv, i);
                    r->wrote_position = 1;
                } else if (mb == BiPointSize) {
                    r->point_size = (float)comp_f(&mv, 0);
                } else if (var_loc >= 0 && l < SPIRV_MAX_LOCATIONS) {
                    int nc = mv.n ? mv.n : 1, li;
                    if (nc > 4) nc = 4;
                    for (li = 0; li < nc; li++) r->out_loc[l][li] = (float)comp_f(&mv, li);
                    if (l >= r->num_out_loc) r->num_out_loc = l + 1;
                }
                l += (mtt && mtt->op == OpTypeMatrix) ? mtt->comps : 1u;
            }
            continue;
        }

        if (mem_load(st, &v, b, 0, tid) != 0) { st->error = 0; continue; }
        if (builtin == BiPosition) {
            for (i = 0; i < 4; i++) r->position[i] = (float)comp_f(&v, i);
            r->wrote_position = 1;
        } else if (builtin == BiPointSize) {
            r->point_size = (float)comp_f(&v, 0);
        } else {
            int loc = spirv_deco_int(m, var, 0xffffffff, DecoLocation, -1);
            if (loc >= 0 && loc < SPIRV_MAX_LOCATIONS) {
                int nc = v.n ? v.n : 1;
                if (nc > 4) nc = 4;
                for (i = 0; i < nc; i++) r->out_loc[loc][i] = (float)comp_f(&v, i);
                if ((uint32_t)loc >= r->num_out_loc) r->num_out_loc = (uint32_t)loc + 1;
            }
        }
    }
}

/* ---- image helpers -------------------------------------------------------- */

static spirv_binding *img_binding(interp *st, const sval *img)
{
    uint32_t idx = img->u[0];
    if (idx == 0xffffffffu || idx >= st->res->num_bindings) return NULL;
    return &st->res->bindings[idx];
}

static void img_get(spirv_binding *b, int x, int y, float out[4])
{
    uint8_t *p; size_t off;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (!b || !b->data || !b->width || !b->height) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x >= b->width) x = (int)b->width - 1;
    if ((uint32_t)y >= b->height) y = (int)b->height - 1;
    off = ((size_t)y * b->width + (size_t)x) * (b->bpp ? b->bpp : 4);
    p = (uint8_t *)b->data + off;
    if (b->bpp == 16) memcpy(out, p, 16);
    else { int i; for (i = 0; i < 4; i++) out[i] = p[i] / 255.0f; }
}

static void img_put(spirv_binding *b, int x, int y, const float in[4])
{
    uint8_t *p; size_t off;
    if (!b || !b->data || !b->width || !b->height) return;
    if (x < 0 || y < 0 || (uint32_t)x >= b->width || (uint32_t)y >= b->height) return;
    off = ((size_t)y * b->width + (size_t)x) * (b->bpp ? b->bpp : 4);
    p = (uint8_t *)b->data + off;
    if (b->bpp == 16) memcpy(p, in, 16);
    else { int i; for (i = 0; i < 4; i++) {
        float v = in[i] * 255.0f; if (v < 0) v = 0; if (v > 255) v = 255;
        p[i] = (uint8_t)(v + 0.5f);
    } }
}

/* ---- GLSL.std.450 -------------------------------------------------------- */

static int do_extinst(interp *st, sval *r, uint32_t setid, uint32_t op,
                      const uint32_t *args, uint32_t nargs)
{
    spirv_module *m = st->m;
    sval *a0, *a1 = NULL, *a2 = NULL;
    int i;
    (void)m;
    if (setid != st->m->glsl450_set) { st->error = 1; return -1; }
    if (nargs < 1) { st->error = 1; return -1; }
    a0 = fetch(st, args[0]);
    if (nargs > 1) a1 = fetch(st, args[1]);
    if (nargs > 2) a2 = fetch(st, args[2]);
    if (!a0 || (nargs > 1 && !a1) || (nargs > 2 && !a2)) { st->error = 1; return -1; }

    switch (op) {
    case G_SAbs:
        for (i = 0; i < r->n; i++) { int ai; bin_index(a0, i, r->n, &ai);
            set_i(r, i, (comp_i(a0, ai) < 0) ? -comp_i(a0, ai) : comp_i(a0, ai)); }
        r->skind = a0->skind; return 0;
    case G_FSign:
        for (i = 0; i < r->n; i++) { int ai; bin_index(a0, i, r->n, &ai);
            { double x = comp_f(a0, ai); set_f(r, i, (x > 0) ? 1 : ((x < 0) ? -1 : 0)); } }
        r->skind = SV_FLOAT; return 0;
    case G_SSign:
        for (i = 0; i < r->n; i++) { int ai; bin_index(a0, i, r->n, &ai);
            { int64_t x = comp_i(a0, ai); set_i(r, i, (x > 0) ? 1 : ((x < 0) ? -1 : 0)); } }
        r->skind = a0->skind; return 0;
    case G_Length: {
        double s = 0; for (i = 0; i < a0->n; i++) { double v = comp_f(a0, i); s += v * v; }
        r->n = 1; set_f(r, 0, sqrt(s)); return 0;
    }
    case G_Distance: {
        double s = 0; for (i = 0; i < a0->n; i++) { double d = comp_f(a0, i) - comp_f(a1, i); s += d * d; }
        r->n = 1; set_f(r, 0, sqrt(s)); return 0;
    }
    case G_Normalize: {
        double s = 0; for (i = 0; i < a0->n; i++) { double v = comp_f(a0, i); s += v * v; }
        s = sqrt(s); if (s == 0) s = 1;
        for (i = 0; i < r->n; i++) set_f(r, i, comp_f(a0, i) / s);
        return 0;
    }
    case G_Cross: {
        double x = comp_f(a0, 1) * comp_f(a1, 2) - comp_f(a0, 2) * comp_f(a1, 1);
        double y = comp_f(a0, 2) * comp_f(a1, 0) - comp_f(a0, 0) * comp_f(a1, 2);
        double z = comp_f(a0, 0) * comp_f(a1, 1) - comp_f(a0, 1) * comp_f(a1, 0);
        r->n = 3; set_f(r, 0, x); set_f(r, 1, y); set_f(r, 2, z); return 0;
    }
    case G_Reflect: {
        double d = 0; for (i = 0; i < a0->n; i++) d += comp_f(a1, i) * comp_f(a0, i);
        for (i = 0; i < r->n; i++) set_f(r, i, comp_f(a0, i) - 2.0 * d * comp_f(a1, i));
        return 0;
    }
    default: break;
    }

    for (i = 0; i < r->n; i++) {
        int ai; double v0, v1 = 0, v2 = 0, t = 0;
        bin_index(a0, i, r->n, &ai);
        v0 = comp_f(a0, ai);
        if (a1) v1 = comp_f(a1, (a1->n == 1) ? 0 : i);
        if (a2) v2 = comp_f(a2, (a2->n == 1) ? 0 : i);
        switch (op) {
        case G_FAbs: t = fabs(v0); break;
        case G_Floor: t = floor(v0); break;
        case G_Ceil: t = ceil(v0); break;
        case G_Trunc: t = (v0 < 0) ? ceil(v0) : floor(v0); break;
        case G_Round: t = floor(v0 + 0.5); break;
        case G_RoundEven: { double f = floor(v0); double d = v0 - f;
            t = (d > 0.5) ? f + 1 : (d < 0.5) ? f : (((int)f & 1) ? f + 1 : f); break; }
        case G_Fract: t = v0 - floor(v0); break;
        case G_Sin: t = sin(v0); break;
        case G_Cos: t = cos(v0); break;
        case G_Tan: t = tan(v0); break;
        case G_Asin: t = asin(v0); break;
        case G_Acos: t = acos(v0); break;
        case G_Atan: t = atan(v0); break;
        case G_Sinh: t = sinh(v0); break;
        case G_Cosh: t = cosh(v0); break;
        case G_Tanh: t = tanh(v0); break;
        case G_Exp: t = exp(v0); break;
        case G_Exp2: t = pow(2.0, v0); break;
        case G_Log: t = log(v0); break;
        case G_Log2: t = log(v0) / log(2.0); break;
        case G_Sqrt: t = sqrt(v0); break;
        case G_InverseSqrt: t = 1.0 / sqrt(v0); break;
        case G_Radians: t = v0 * (3.14159265358979323846 / 180.0); break;
        case G_Degrees: t = v0 * (180.0 / 3.14159265358979323846); break;
        case G_Atan2: t = atan2(v0, v1); break;
        case G_Pow: t = pow(v0, v1); break;
        case G_FMin: case G_NMin: t = (v0 < v1) ? v0 : v1; break;
        case G_FMax: case G_NMax: t = (v0 > v1) ? v0 : v1; break;
        case G_FClamp: case G_NClamp: t = (v0 < v1) ? v1 : ((v0 > v2) ? v2 : v0); break;
        case G_FMix: t = v0 + (v1 - v0) * v2; break;
        case G_Fma: t = v0 * v1 + v2; break;
        case G_Step: t = (v1 < v0) ? 0.0 : 1.0; break;
        case G_SmoothStep: {
            double d = v1 - v0, tt = (d == 0) ? 0.0 : (v2 - v0) / d;
            if (tt < 0) tt = 0;
            if (tt > 1) tt = 1;
            t = tt * tt * (3.0 - 2.0 * tt); break;
        }
        default: st->error = 1; return -1;
        }
        set_f(r, i, t);
    }
    r->skind = SV_FLOAT;
    return 0;
}

/* ---- the per-instruction executor ---------------------------------------- */

static int exec_insn(interp *st, const spirv_insn *in)
{
    spirv_module *m = st->m;
    const uint32_t *w = in->w;
    uint32_t op = in->op;
    sval r, *a, *b;

    switch (op) {
    case OpNop: case OpLine: case OpLoopMerge: case OpSelectionMerge:
    case OpMemoryBarrier: case OpControlBarrier:
        return 0;

    case OpVariable: {
        uint32_t ptrtype = w[1], res_id = w[2];
        uint32_t storage = (in->wc >= 4) ? w[3] : 0;
        uint32_t init = (in->wc >= 5) ? w[4] : 0;
        if (storage == SC_Function) {
            spirv_type *pt = spirv_type_find(m, ptrtype);
            uint32_t pointee = pt ? pt->elem : 0;
            uint32_t sz = var_bytes(m, pointee); if (!sz) sz = 64;
            uint8_t *bb = (uint8_t *)arena_alloc(st, sz);
            sval v;
            if (!bb) return -1;
            memset(bb, 0, sz);
            memset(&v, 0, sizeof(v));
            v.type = ptrtype; v.klass = SV_PTR; v.n = 1;
            v.ptr.base = bb; v.ptr.elem = pointee; v.ptr.storage = SC_Function;
            v.ptr.off = 0; v.ptr.binding = 0xffffffffu;
            if (init && materialize_const(st, init) == 0)
                mem_store(st, bb, 0, pointee, &st->vals[init]);
            st->vals[res_id] = v; st->valid[res_id] = 1;
            return 0;
        }
        return setup_global_var(st, res_id, ptrtype, storage, init);
    }

    case OpAccessChain: case OpInBoundsAccessChain: {
        sval *base = fetch(st, w[3]);
        if (!base) return -1;
        memset(&r, 0, sizeof(r));
        if (do_access_chain(st, base, w + 4, in->wc - 4, &r) != 0) return -1;
        r.type = w[1]; r.klass = SV_PTR; r.n = 1;
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpLoad: {
        sval *p = fetch(st, w[3]);
        spirv_type *et;
        if (!p) return -1;
        describe(m, w[1], &r);
        et = spirv_type_find(m, p->ptr.elem);
        if (et && (et->op == OpTypeImage || et->op == OpTypeSampler ||
                   et->op == OpTypeSampledImage)) {
            r.klass = SV_IMAGE; r.n = 1; r.u[0] = p->ptr.binding;
        } else if (mem_load(st, &r, p->ptr.base, p->ptr.off, p->ptr.elem) != 0) {
            return -1;
        }
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpStore: {
        sval *p = fetch(st, w[1]);
        sval *obj = fetch(st, w[2]);
        if (!p || !obj) return -1;
        return mem_store(st, p->ptr.base, p->ptr.off, p->ptr.elem, obj);
    }

    case OpCopyMemory: {
        sval *dst = fetch(st, w[1]);
        sval *src = fetch(st, w[2]);
        uint32_t sz;
        if (!dst || !src) return -1;
        sz = var_bytes(m, dst->ptr.elem);
        if (sz && dst->ptr.base && src->ptr.base)
            memcpy((uint8_t *)dst->ptr.base + dst->ptr.off,
                   (uint8_t *)src->ptr.base + src->ptr.off, sz);
        return 0;
    }

    case OpArrayLength: {
        sval *sp = fetch(st, w[3]);
        uint32_t memidx, rt_tid, off, es, len;
        spirv_type *rt;
        size_t total = 0;
        if (!sp) return -1;
        memidx = w[4];
        rt_tid = struct_member_type(m, sp->ptr.elem, memidx);
        off = member_offset(m, sp->ptr.elem, memidx);
        rt = spirv_type_find(m, rt_tid);
        es = rt ? elem_stride(m, rt->elem) : 0;
        if (sp->ptr.binding != 0xffffffffu && sp->ptr.binding < st->res->num_bindings)
            total = st->res->bindings[sp->ptr.binding].size;
        describe(m, w[1], &r); r.n = 1;
        len = (es && total > (size_t)off) ? (uint32_t)((total - off) / es) : 0;
        set_u(&r, 0, len); r.skind = SV_UINT; r.klass = SV_UINT; r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    /* ---- binary arithmetic / bitwise ---- */
    case OpIAdd: case OpISub: case OpIMul: case OpFAdd: case OpFSub: case OpFMul:
    case OpSDiv: case OpUDiv: case OpFDiv: case OpSRem: case OpSMod: case OpUMod:
    case OpFRem: case OpFMod: case OpBitwiseOr: case OpBitwiseXor: case OpBitwiseAnd:
    case OpShiftLeftLogical: case OpShiftRightLogical: case OpShiftRightArithmetic:
        a = fetch(st, w[3]); b = fetch(st, w[4]);
        if (!a || !b) return -1;
        describe(m, w[1], &r);
        arith_binary(&r, a, b, op);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    /* ---- comparison / logical ---- */
    case OpIEqual: case OpINotEqual: case OpUGreaterThan: case OpSGreaterThan:
    case OpUGreaterThanEqual: case OpSGreaterThanEqual: case OpULessThan:
    case OpSLessThan: case OpULessThanEqual: case OpSLessThanEqual:
    case OpFOrdEqual: case OpFUnordEqual: case OpFOrdNotEqual: case OpFUnordNotEqual:
    case OpFOrdLessThan: case OpFUnordLessThan: case OpFOrdGreaterThan:
    case OpFUnordGreaterThan: case OpFOrdLessThanEqual: case OpFUnordLessThanEqual:
    case OpFOrdGreaterThanEqual: case OpFUnordGreaterThanEqual:
    case OpLogicalEqual: case OpLogicalNotEqual: case OpLogicalOr: case OpLogicalAnd:
        a = fetch(st, w[3]); b = fetch(st, w[4]);
        if (!a || !b) return -1;
        describe(m, w[1], &r);
        cmp_binary(&r, a, b, op);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    /* ---- unary ---- */
    case OpSNegate: case OpFNegate: case OpNot: case OpLogicalNot:
    case OpConvertFToU: case OpConvertFToS: case OpConvertSToF: case OpConvertUToF:
    case OpUConvert: case OpSConvert: case OpFConvert: case OpBitcast:
        a = fetch(st, w[3]);
        if (!a) return -1;
        describe(m, w[1], &r);
        unary_op(&r, a, op);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    case OpSelect: {
        sval *c = fetch(st, w[3]);
        sval *o1 = fetch(st, w[4]);
        sval *o2 = fetch(st, w[5]);
        if (!c || !o1 || !o2) return -1;
        describe(m, w[1], &r);
        do_select(&r, c, o1, o2);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    /* ---- vector / matrix math ---- */
    case OpDot: case OpVectorTimesScalar: case OpMatrixTimesScalar:
    case OpMatrixTimesVector: case OpVectorTimesMatrix: case OpMatrixTimesMatrix:
    case OpOuterProduct: {
        int err = 0;
        a = fetch(st, w[3]); b = fetch(st, w[4]);
        if (!a || !b) return -1;
        describe(m, w[1], &r);
        vec_math(&r, a, b, op, &err);
        if (err) { st->error = 1; return -1; }
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    /* ---- composites ---- */
    case OpCompositeConstruct: {
        spirv_type *t = spirv_type_find(m, w[1]);
        describe(m, w[1], &r);
        if (t && t->op == OpTypeStruct) {
            uint32_t sz = var_bytes(m, w[1]); if (!sz) sz = 16;
            uint8_t *bb = (uint8_t *)arena_alloc(st, sz);
            uint32_t k;
            if (!bb) return -1;
            memset(bb, 0, sz);
            for (k = 3; k < in->wc; k++) {
                sval *mv = fetch(st, w[k]);
                uint32_t mt;
                if (!mv) return -1;
                mt = struct_member_type(m, w[1], k - 3);
                mem_store(st, bb, member_offset(m, w[1], k - 3), mt, mv);
            }
            r.klass = SV_STRUCT; r.agg = bb; r.n = 0;
        } else {
            int pos = 0;
            uint32_t k;
            for (k = 3; k < in->wc; k++) {
                sval *cv = fetch(st, w[k]);
                int j;
                if (!cv) return -1;
                for (j = 0; j < cv->n && pos < SV_MAX_COMP; j++) r.u[pos++] = cv->u[j];
            }
        }
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpCompositeExtract: {
        sval *base = fetch(st, w[3]);
        if (!base) return -1;
        describe(m, w[1], &r);
        composite_extract(st, &r, base, w + 4, in->wc - 4);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpCompositeInsert: {
        sval *obj = fetch(st, w[3]);
        sval *base = fetch(st, w[4]);
        if (!obj || !base) return -1;
        r = *base;
        if (in->wc >= 6) {
            uint32_t idx = w[5];
            int j;
            if (base->cols && base->rows) {
                int rows = base->rows;
                for (j = 0; j < rows && j < obj->n; j++) r.u[idx * rows + j] = obj->u[j];
            } else {
                for (j = 0; j < obj->n && (int)idx + j < SV_MAX_COMP; j++) r.u[idx + j] = obj->u[j];
            }
        } else {
            r = *obj;
        }
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpVectorShuffle: {
        a = fetch(st, w[3]); b = fetch(st, w[4]);
        if (!a || !b) return -1;
        describe(m, w[1], &r);
        {
            uint32_t k; int pos = 0;
            for (k = 5; k < in->wc && pos < SV_MAX_COMP; k++) {
                uint32_t c = w[k];
                r.u[pos++] = (c < a->n) ? a->u[c] : b->u[c - a->n];
            }
        }
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpVectorExtractDynamic: {
        a = fetch(st, w[3]); b = fetch(st, w[4]);
        if (!a || !b) return -1;
        describe(m, w[1], &r);
        r.u[0] = a->u[comp_u(b, 0) % (a->n ? a->n : 1)];
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpVectorInsertDynamic: {
        sval *vec = fetch(st, w[3]);
        sval *cv = fetch(st, w[4]);
        sval *idx = fetch(st, w[5]);
        if (!vec || !cv || !idx) return -1;
        r = *vec;
        r.u[comp_u(idx, 0)] = cv->u[0];
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpCopyObject:
        a = fetch(st, w[3]); if (!a) return -1;
        r = *a; r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    case OpTranspose: {
        int c, rr, cols, rows;
        a = fetch(st, w[3]); if (!a) return -1;
        describe(m, w[1], &r);
        cols = a->cols; rows = a->rows;
        for (c = 0; c < rows; c++)
            for (rr = 0; rr < cols; rr++)
                r.u[c * cols + rr] = a->u[rr * rows + c];
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpUndef:
        describe(m, w[1], &r);
        memset(r.u, 0, sizeof(r.u));
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    case OpFunctionCall: {
        spirv_func *cf = spirv_func_find(m, w[3]);
        uint32_t nargs = in->wc - 4, k;
        sval argbuf[16], ret;
        if (!cf || nargs > 16) { st->error = 1; return -1; }
        for (k = 0; k < nargs; k++) {
            sval *av = fetch(st, w[4 + k]);
            if (!av) return -1;
            argbuf[k] = *av;
        }
        memset(&ret, 0, sizeof(ret));
        if (exec_func(st, cf, argbuf, nargs, &ret) != 0) return -1;
        ret.type = w[1];
        st->vals[w[2]] = ret; st->valid[w[2]] = 1;
        return 0;
    }

    case OpExtInst:
        describe(m, w[1], &r);
        if (do_extinst(st, &r, w[3], w[4], w + 5, in->wc - 5) != 0) return -1;
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    /* ---- images ---- */
    case OpImage: case OpSampledImage:
        a = fetch(st, w[3]); if (!a) return -1;
        r = *a; r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;

    case OpImageRead: {
        sval *img = fetch(st, w[3]);
        sval *coord = fetch(st, w[4]);
        spirv_binding *bb;
        float px[4];
        int i;
        if (!img || !coord) return -1;
        bb = img_binding(st, img);
        describe(m, w[1], &r);
        img_get(bb, (int)comp_i(coord, 0), (int)comp_i(coord, 1), px);
        for (i = 0; i < r.n && i < 4; i++) set_f(&r, i, px[i]);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpImageWrite: {
        sval *img = fetch(st, w[1]);
        sval *coord = fetch(st, w[2]);
        sval *tex = fetch(st, w[3]);
        spirv_binding *bb;
        float px[4] = { 0, 0, 0, 0 };
        int i;
        if (!img || !coord || !tex) return -1;
        bb = img_binding(st, img);
        for (i = 0; i < tex->n && i < 4; i++) px[i] = (float)comp_f(tex, i);
        img_put(bb, (int)comp_i(coord, 0), (int)comp_i(coord, 1), px);
        return 0;
    }

    case OpImageQuerySize: {
        sval *img = fetch(st, w[3]);
        spirv_binding *bb = img ? img_binding(st, img) : NULL;
        describe(m, w[1], &r);
        set_u(&r, 0, bb ? bb->width : 0);
        if (r.n > 1) set_u(&r, 1, bb ? bb->height : 0);
        if (r.n > 2) set_u(&r, 2, bb ? bb->depth : 0);
        r.skind = SV_UINT; r.klass = (r.n > 1) ? SV_VEC : SV_UINT; r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    case OpImageSampleImplicitLod: case OpImageSampleExplicitLod: {
        sval *img = fetch(st, w[3]);
        sval *coord = fetch(st, w[4]);
        spirv_binding *bb;
        float px[4];
        int i, x = 0, y = 0;
        if (!img || !coord) return -1;
        bb = img_binding(st, img);
        describe(m, w[1], &r);
        if (bb) {
            x = (int)(comp_f(coord, 0) * (double)bb->width);
            y = (int)(comp_f(coord, 1) * (double)bb->height);
        }
        img_get(bb, x, y, px);
        for (i = 0; i < r.n && i < 4; i++) set_f(&r, i, px[i]);
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    /* ---- atomics (single-threaded equivalents) ---- */
    case OpAtomicLoad: {
        sval *p = fetch(st, w[3]);
        if (!p) return -1;
        if (mem_load(st, &r, p->ptr.base, p->ptr.off, p->ptr.elem) != 0) return -1;
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }
    case OpAtomicStore: {
        sval *p = fetch(st, w[1]);
        sval *obj = fetch(st, w[4]);
        if (!p || !obj) return -1;
        return mem_store(st, p->ptr.base, p->ptr.off, p->ptr.elem, obj);
    }
    case OpAtomicIIncrement: case OpAtomicIDecrement: {
        sval *p = fetch(st, w[3]);
        uint32_t delta = (op == OpAtomicIIncrement) ? 1u : 0xffffffffu;
        if (!p) return -1;
        if (mem_load(st, &r, p->ptr.base, p->ptr.off, p->ptr.elem) != 0) return -1;
        {
            sval nv = r;
            nv.u[0] = r.u[0] + delta;
            mem_store(st, p->ptr.base, p->ptr.off, p->ptr.elem, &nv);
        }
        r.type = w[1];
        st->vals[w[2]] = r; st->valid[w[2]] = 1;
        return 0;
    }

    default:
        st->error = 1;
        return -1;
    }
}

/* ---- CFG function walker -------------------------------------------------- */

static int exec_func(interp *st, spirv_func *f, const sval *args, uint32_t nargs,
                     sval *ret)
{
    spirv_module *m = st->m;
    uint32_t save_prev = st->prev_label;
    uint32_t cur_label = 0;
    int32_t pc, q;
    uint32_t ai = 0;

    if (!f || f->first_block == 0xffffffffu) { st->error = 1; return -1; }
    if (st->depth >= MAX_CALL_DEPTH) { st->error = 1; return -1; }
    st->depth++;

    /* bind OpFunctionParameter ids */
    for (q = (int32_t)f->start + 1; q < (int32_t)m->ninsns &&
              m->insns[q].op == OpFunctionParameter; q++) {
        if (args && ai < nargs) {
            st->vals[m->insns[q].w[2]] = args[ai];
            st->valid[m->insns[q].w[2]] = 1;
            ai++;
        }
    }

    st->prev_label = 0;
    pc = (int32_t)f->first_block;
    while (pc >= 0 && !st->error && !st->kill) {
        spirv_insn *lin = &m->insns[pc];
        int done = 0;
        if (lin->op != OpLabel) { st->error = 1; break; }
        cur_label = lin->w[1];
        pc++;
        while (!done) {
            spirv_insn *in;
            if (pc < 0 || (uint32_t)pc >= m->ninsns) { st->error = 1; break; }
            in = &m->insns[pc];
            if (--st->budget < 0) { st->error = 1; break; }
            switch (in->op) {
            case OpLabel:
                done = 1;   /* pc already at the next block's label */
                break;
            case OpBranch: {
                uint32_t tgt = in->w[1];
                st->prev_label = cur_label;
                pc = (tgt < m->bound) ? m->label_at[tgt] : -1;
                done = 1;
                break;
            }
            case OpBranchConditional: {
                sval *c = fetch(st, in->w[1]);
                int t = c ? (c->u[0] != 0) : 0;
                uint32_t tgt = t ? in->w[2] : in->w[3];
                st->prev_label = cur_label;
                pc = (tgt < m->bound) ? m->label_at[tgt] : -1;
                done = 1;
                break;
            }
            case OpSwitch: {
                sval *s = fetch(st, in->w[1]);
                uint32_t sel = s ? comp_u(s, 0) : 0;
                uint32_t tgt = in->w[2], j;
                for (j = 3; j + 1 < in->wc; j += 2)
                    if (in->w[j] == sel) { tgt = in->w[j + 1]; break; }
                st->prev_label = cur_label;
                pc = (tgt < m->bound) ? m->label_at[tgt] : -1;
                done = 1;
                break;
            }
            case OpReturn:
                pc = -1; done = 1; break;
            case OpReturnValue: {
                sval *rv = fetch(st, in->w[1]);
                if (rv && ret) *ret = *rv;
                pc = -1; done = 1; break;
            }
            case OpKill: case OpTerminateInvocation:
                st->kill = 1; pc = -1; done = 1; break;
            case OpUnreachable:
                pc = -1; done = 1; break;
            case OpPhi: {
                sval chosen;
                uint32_t k;
                memset(&chosen, 0, sizeof(chosen));
                describe(m, in->w[1], &chosen);
                for (k = 3; k + 1 < in->wc; k += 2) {
                    if (in->w[k + 1] == st->prev_label) {
                        sval *vv = fetch(st, in->w[k]);
                        if (vv) chosen = *vv;
                        break;
                    }
                }
                chosen.type = in->w[1];
                st->vals[in->w[2]] = chosen; st->valid[in->w[2]] = 1;
                pc++;
                break;
            }
            default:
                exec_insn(st, in);
                pc++;
                break;
            }
            if (st->error) break;
        }
        if (pc < 0) break;
    }

    st->depth--;
    st->prev_label = save_prev;
    return st->error ? -1 : 0;
}

/* ---- entry point ---------------------------------------------------------- */

int spirv_run(const spirv_module *cm, const spirv_entry *ce, spirv_resources *res)
{
    spirv_module *m = (spirv_module *)cm;
    const spirv_ep *ep = (const spirv_ep *)ce;
    interp st;
    spirv_func *f;
    uint32_t i, first_func;

    if (!m || !ep || !res) return -1;
    f = spirv_func_find(m, ep->id);
    if (!f) return -1;

    /* size the value cache to the id bound (cached across invocations) */
    if (m->scratch_n < m->bound) {
        free(m->scratch); free(m->scratch_valid);
        m->scratch = (sval *)calloc(m->bound ? m->bound : 1, sizeof(sval));
        m->scratch_valid = (uint8_t *)calloc(m->bound ? m->bound : 1, 1);
        if (!m->scratch || !m->scratch_valid) {
            free(m->scratch); free(m->scratch_valid);
            m->scratch = NULL; m->scratch_valid = NULL; m->scratch_n = 0;
            return -1;
        }
        m->scratch_n = m->bound;
    }

    memset(&st, 0, sizeof(st));
    st.m = m; st.res = res;
    st.vals = m->scratch; st.valid = m->scratch_valid; st.n = m->scratch_n;
    st.budget = INSN_BUDGET;
    m->arena_used = 0;                 /* reset arena, keep capacity */
    memset(st.valid, 0, m->scratch_n);

    /* set up module-scope variables (descriptors, interface, private) */
    first_func = m->nfuncs ? m->funcs[0].start : m->ninsns;
    for (i = 0; i < m->ninsns && i < first_func; i++) {
        if (m->insns[i].op == OpVariable) {
            const uint32_t *w = m->insns[i].w;
            uint32_t storage = (m->insns[i].wc >= 4) ? w[3] : 0;
            uint32_t init = (m->insns[i].wc >= 5) ? w[4] : 0;
            if (storage == SC_Workgroup) return -1;   /* decline shared memory */
            if (setup_global_var(&st, w[2], w[1], storage, init) != 0) return -1;
        }
    }

    res->wrote_position = 0;
    res->killed = 0;

    if (exec_func(&st, f, NULL, 0, NULL) != 0) return -1;
    copyback_outputs(&st);
    res->killed = st.kill;
    return st.error ? -1 : 0;
}
