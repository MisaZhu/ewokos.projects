/*
 * spirv_to_glsl.c - the SPIR-V -> GLSL-ES-3.10 transpiler (see spirv_to_glsl.h).
 * It walks one entry point of a parsed spirv_module and emits a verbatim shader
 * the on-device mesa compiler accepts, covering the GPU-friendly subset and
 * DECLINING everything else (a declined entry stays on the software device, so
 * declining only ever costs speed, never correctness).
 *
 * Translation model: SPIR-V is SSA, GLSL is not, so the transpiler lowers out of
 * SSA.  Every result id becomes a mutable local named "v<id>" declared at its
 * definition; OpPhi results are declared at the top of main() and each phi
 * operand pair becomes an explicit "v<res> = v<val>;" copy at the end of the
 * matching predecessor block.  Structured control flow (OpSelectionMerge /
 * OpLoopMerge) is reconstructed into if/else and for(;;)/break; anything that
 * needs edge splitting or a break/continue escape is declined.  Access chains
 * are folded into GLSL lvalue expressions (block.data[i], v.x, m.k), so loads
 * and stores become plain reads/writes.
 */
#include "spirv_to_glsl.h"
#include "spirv_priv.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BARRIERS 32
#define MAX_REGION_DEPTH 64

typedef struct {
    char     *buf;
    size_t    len, cap;
    int       declined;
    char      err[192];

    spirv_module      *m;
    const spirv_ep    *ep;
    spirv_func        *f;
    spirv_glsl_info   *info;

    char    **name;      /* id -> malloc'd GLSL read-name / lvalue (NULL lazy) */
    uint8_t  *is_ptr;    /* id -> 1 when name[] is a pointer lvalue            */

    uint32_t  barriers[MAX_BARRIERS];
    int       nbar;
    int       depth;
    int       nssbo;
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

static void gf(gctx *c, const char *fmt, ...)
{
    va_list ap;
    int need;
    if (c->declined) return;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { decl(c, "format error"); return; }
    if (c->len + (size_t)need + 1 > c->cap) {
        size_t nc = c->cap ? c->cap : 1024;
        char *np;
        while (nc < c->len + (size_t)need + 1) nc *= 2;
        np = (char *)realloc(c->buf, nc);
        if (!np) { decl(c, "out of memory"); return; }
        c->buf = np; c->cap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(c->buf + c->len, c->cap - c->len, fmt, ap);
    va_end(ap);
    c->len += (size_t)need;
}

static void indent(gctx *c, int ind)
{
    int i;
    for (i = 0; i < ind; i++) gf(c, "    ");
}

/* ---- name table ---------------------------------------------------------- */
static void set_name(gctx *c, uint32_t id, const char *s)
{
    if (id >= c->m->bound) return;
    free(c->name[id]);
    c->name[id] = (char *)malloc(strlen(s) + 1);
    if (c->name[id]) strcpy(c->name[id], s);
}

static uint32_t id_type(gctx *c, uint32_t id)
{
    int32_t di;
    if (id >= c->m->bound) return 0;
    di = c->m->def_at[id];
    if (di < 0) return 0;
    if (c->m->insns[di].wc >= 3) return c->m->insns[di].w[1];
    return 0;
}

static uint32_t struct_member_type(gctx *c, uint32_t struct_tid, uint32_t member)
{
    int32_t di;
    spirv_insn *in;
    if (struct_tid >= c->m->bound) return 0;
    di = c->m->def_at[struct_tid];
    if (di < 0) return 0;
    in = &c->m->insns[di];
    if (in->op != OpTypeStruct || 2 + member >= in->wc) return 0;
    return in->w[2 + member];
}

/* ---- type names ---------------------------------------------------------- */
/* GLSL type string for a value type.  Declines pointers, structs, arrays and
 * any scalar width other than 32 bits.  Returns 0 on success. */
static int type_name(gctx *c, uint32_t tid, char *out, size_t n)
{
    spirv_type *t = spirv_type_find(c->m, tid);
    if (!t) return decl(c, "unknown type");
    switch (t->op) {
    case OpTypeVoid:  snprintf(out, n, "void"); return 0;
    case OpTypeBool:  snprintf(out, n, "bool"); return 0;
    case OpTypeInt:
        if (t->width != 32) return decl(c, "only 32-bit integers supported");
        snprintf(out, n, t->is_signed ? "int" : "uint");
        return 0;
    case OpTypeFloat:
        if (t->width != 32) return decl(c, "only 32-bit floats supported");
        snprintf(out, n, "float");
        return 0;
    case OpTypeVector: {
        char base[16];
        spirv_type *e = spirv_type_find(c->m, t->elem);
        const char *pfx;
        if (t->comps < 2 || t->comps > 4) return decl(c, "unsupported vector width");
        if (!e) return decl(c, "bad vector element");
        if (e->op == OpTypeFloat && e->width == 32) pfx = "vec";
        else if (e->op == OpTypeInt && e->width == 32) pfx = e->is_signed ? "ivec" : "uvec";
        else if (e->op == OpTypeBool) pfx = "bvec";
        else return decl(c, "unsupported vector element");
        snprintf(out, n, "%s%u", pfx, t->comps);
        (void)base;
        return 0;
    }
    case OpTypeMatrix: {
        spirv_type *col = spirv_type_find(c->m, t->elem);
        if (t->comps < 2 || t->comps > 4) return decl(c, "unsupported matrix size");
        if (!col || col->op != OpTypeVector || col->comps != t->comps)
            return decl(c, "only square float matrices supported");
        {
            spirv_type *ce = spirv_type_find(c->m, col->elem);
            if (!ce || ce->op != OpTypeFloat || ce->width != 32)
                return decl(c, "only float matrices supported");
        }
        snprintf(out, n, "mat%u", t->comps);
        return 0;
    }
    default:
        return decl(c, "type is not a GLSL value");
    }
}

/* ---- constant literals --------------------------------------------------- */
static int fmt_float(gctx *c, uint32_t bits, char *out, size_t n)
{
    float f;
    memcpy(&f, &bits, 4);
    if (!isfinite(f)) return decl(c, "non-finite float constant");
    snprintf(out, n, "%.9g", (double)f);
    if (!strpbrk(out, ".eE")) {
        size_t l = strlen(out);
        if (l + 3 <= n) { out[l] = '.'; out[l + 1] = '0'; out[l + 2] = '\0'; }
    }
    return 0;
}

/* Build (and cache) the GLSL read-name for id: a literal for constants, else
 * "v<id>".  Pointer ids already have their lvalue registered by the time they
 * are referenced. */
static const char *idname(gctx *c, uint32_t id)
{
    int32_t di;
    spirv_insn *in;
    char tmp[128];

    if (id >= c->m->bound) { decl(c, "id out of range"); return "?"; }
    if (c->name[id]) return c->name[id];

    di = c->m->def_at[id];
    if (di < 0) { snprintf(tmp, sizeof tmp, "v%u", id); set_name(c, id, tmp); return c->name[id]; }
    in = &c->m->insns[di];

    switch (in->op) {
    case OpConstantTrue:  set_name(c, id, "true");  return c->name[id];
    case OpConstantFalse: set_name(c, id, "false"); return c->name[id];
    case OpConstantNull: {
        char tn[32];
        if (type_name(c, in->w[1], tn, sizeof tn) != 0) return "?";
        if (strncmp(tn, "vec", 3) == 0 || strncmp(tn, "ivec", 4) == 0 ||
            strncmp(tn, "uvec", 4) == 0 || strncmp(tn, "bvec", 4) == 0 ||
            strncmp(tn, "mat", 3) == 0)
            snprintf(tmp, sizeof tmp, "%s(0.0)", tn);
        else if (strcmp(tn, "uint") == 0) snprintf(tmp, sizeof tmp, "0u");
        else if (strcmp(tn, "bool") == 0) snprintf(tmp, sizeof tmp, "false");
        else if (strcmp(tn, "float") == 0) snprintf(tmp, sizeof tmp, "0.0");
        else snprintf(tmp, sizeof tmp, "0");
        set_name(c, id, tmp);
        return c->name[id];
    }
    case OpConstant: {
        spirv_type *t = spirv_type_find(c->m, in->w[1]);
        if (!t || in->wc < 4) { decl(c, "bad constant"); return "?"; }
        if (t->op == OpTypeFloat) {
            if (fmt_float(c, in->w[3], tmp, sizeof tmp) != 0) return "?";
        } else if (t->op == OpTypeInt) {
            if (t->width != 32) { decl(c, "only 32-bit constants"); return "?"; }
            if (t->is_signed) snprintf(tmp, sizeof tmp, "%d", (int32_t)in->w[3]);
            else snprintf(tmp, sizeof tmp, "%uu", in->w[3]);
        } else { decl(c, "unsupported constant type"); return "?"; }
        set_name(c, id, tmp);
        return c->name[id];
    }
    case OpConstantComposite: {
        char tn[32];
        spirv_type *t = spirv_type_find(c->m, in->w[1]);
        uint32_t k;
        char acc[256];
        size_t len;
        if (!t || (t->op != OpTypeVector && t->op != OpTypeMatrix))
            { decl(c, "unsupported composite constant"); return "?"; }
        if (type_name(c, in->w[1], tn, sizeof tn) != 0) return "?";
        len = (size_t)snprintf(acc, sizeof acc, "%s(", tn);
        if (len >= sizeof acc) { decl(c, "constant literal too long"); return "?"; }
        for (k = 3; k < in->wc; k++) {
            const char *e = idname(c, in->w[k]);
            int w2;
            if (c->declined) return "?";
            w2 = snprintf(acc + len, sizeof acc - len, "%s%s", (k > 3) ? ", " : "", e);
            if (w2 < 0 || (size_t)w2 >= sizeof acc - len) {
                decl(c, "constant literal too long"); return "?";
            }
            len += (size_t)w2;
        }
        if (len + 2 > sizeof acc) { decl(c, "constant literal too long"); return "?"; }
        acc[len++] = ')';
        acc[len] = '\0';
        set_name(c, id, acc);
        return c->name[id];
    }
    default:
        snprintf(tmp, sizeof tmp, "v%u", id);
        set_name(c, id, tmp);
        return c->name[id];
    }
}

/* ---- access chains -> lvalues -------------------------------------------- */
static const char *swz = "xyzw";

/* Append the member selector for struct member `k` of struct type `stid`. */
static void member_sel(gctx *c, uint32_t stid, uint32_t k, char *out, size_t n)
{
    uint32_t mt = struct_member_type(c, stid, k);
    spirv_type *t = spirv_type_find(c->m, mt);
    if (t && t->op == OpTypeRuntimeArray) snprintf(out, n, ".data");
    else snprintf(out, n, ".m%u", k);
}

/* Build the GLSL lvalue for an access chain.  base_name is the lvalue of the
 * pointer base; cur_tid is the type the base points at.  Returns a malloc'd
 * string (caller stores it) or NULL on decline. */
static char *build_access_chain(gctx *c, const char *base_name, uint32_t cur_tid,
                                const uint32_t *idx, uint32_t nidx)
{
    char acc[256];
    uint32_t i;
    size_t len;
    int an = snprintf(acc, sizeof acc, "%s", base_name);
    if (an < 0 || (size_t)an >= sizeof acc) { decl(c, "lvalue too long"); return NULL; }
    len = (size_t)an;
    for (i = 0; i < nidx; i++) {
        spirv_type *t = spirv_type_find(c->m, cur_tid);
        char piece[64];
        int32_t di = (idx[i] < c->m->bound) ? c->m->def_at[idx[i]] : -1;
        if (!t) { decl(c, "access chain: bad type"); return NULL; }
        if (t->op == OpTypeStruct) {
            if (di < 0 || c->m->insns[di].op != OpConstant)
                { decl(c, "non-constant struct index"); return NULL; }
            member_sel(c, cur_tid, c->m->insns[di].w[3], piece, sizeof piece);
            cur_tid = struct_member_type(c, cur_tid, c->m->insns[di].w[3]);
        } else if (t->op == OpTypeRuntimeArray || t->op == OpTypeArray) {
            snprintf(piece, sizeof piece, "[%s]", idname(c, idx[i]));
            cur_tid = t->elem;
        } else if (t->op == OpTypeVector) {
            uint32_t k;
            if (di < 0 || c->m->insns[di].op != OpConstant)
                { decl(c, "non-constant vector index"); return NULL; }
            k = c->m->insns[di].w[3];
            if (k > 3) { decl(c, "vector index out of range"); return NULL; }
            snprintf(piece, sizeof piece, ".%c", swz[k]);
            cur_tid = t->elem;
        } else if (t->op == OpTypeMatrix) {
            snprintf(piece, sizeof piece, "[%s]", idname(c, idx[i]));
            cur_tid = t->elem;
        } else {
            decl(c, "access chain into unsupported type");
            return NULL;
        }
        if (c->declined) return NULL;
        an = snprintf(acc + len, sizeof acc - len, "%s", piece);
        if (an < 0 || (size_t)an >= sizeof acc - len) { decl(c, "lvalue too long"); return NULL; }
        len += (size_t)an;
    }
    return strdup(acc);
}

/* ---- resource / interface declarations ----------------------------------- */
static int add_binding(gctx *c, uint32_t set, uint32_t binding, spirv_res_kind kind,
                       const char *name)
{
    spirv_glsl_binding *b;
    if (c->info->num_bindings >= SPIRV_MAX_BINDINGS) return decl(c, "too many bindings");
    b = &c->info->bindings[c->info->num_bindings++];
    b->set = set; b->binding_src = binding; b->kind = kind; b->binding = binding;
    snprintf(b->name, sizeof b->name, "%s", name);
    return 0;
}

/* Emit the SSBO block body for a StorageBuffer struct type.  Only a trailing
 * runtime array of scalars/vectors, or a flat list of scalar/vector members, is
 * supported. */
static int emit_ssbo_struct(gctx *c, uint32_t struct_tid)
{
    int32_t di = c->m->def_at[struct_tid];
    spirv_insn *in;
    uint32_t k, nm;
    if (struct_tid >= c->m->bound || di < 0) return decl(c, "bad SSBO struct");
    in = &c->m->insns[di];
    if (in->op != OpTypeStruct) return decl(c, "SSBO base is not a struct");
    nm = in->wc - 2;
    gf(c, " {\n");
    for (k = 0; k < nm; k++) {
        uint32_t mt = in->w[2 + k];
        spirv_type *t = spirv_type_find(c->m, mt);
        char tn[32];
        if (t && t->op == OpTypeRuntimeArray) {
            if (type_name(c, t->elem, tn, sizeof tn) != 0) return -1;
            gf(c, "    %s data[];\n", tn);
        } else {
            if (type_name(c, mt, tn, sizeof tn) != 0)
                return decl(c, "unsupported SSBO member type");
            gf(c, "    %s m%u;\n", tn, k);
        }
    }
    gf(c, "}");
    return 0;
}

static int map_builtin_name(gctx *c, uint32_t builtin, int is_output,
                            char *out, size_t n)
{
    switch (builtin) {
    case BiGlobalInvocationId: snprintf(out, n, "gl_GlobalInvocationID"); return 0;
    case BiLocalInvocationId:  snprintf(out, n, "gl_LocalInvocationID"); return 0;
    case BiWorkgroupId:        snprintf(out, n, "gl_WorkGroupID"); return 0;
    case BiNumWorkgroups:      snprintf(out, n, "gl_NumWorkGroups"); return 0;
    case BiLocalInvocationIndex: snprintf(out, n, "gl_LocalInvocationIndex"); return 0;
    case BiWorkgroupSize:      snprintf(out, n, "gl_WorkGroupSize"); return 0;
    case BiVertexIndex:        snprintf(out, n, "gl_VertexID"); return 0;
    case BiInstanceIndex:      snprintf(out, n, "gl_InstanceID"); return 0;
    case BiFragCoord:          snprintf(out, n, "gl_FragCoord"); return 0;
    case BiFrontFacing:        snprintf(out, n, "gl_FrontFacing"); return 0;
    case BiPosition:
        if (!is_output) return decl(c, "Position as an input");
        snprintf(out, n, "gl_Position"); return 0;
    case BiPointSize:
        if (!is_output) return decl(c, "PointSize as an input");
        snprintf(out, n, "gl_PointSize"); return 0;
    default: return decl(c, "unsupported built-in");
    }
}

/* Declare one module-scope OpVariable (resource / interface).  Returns 0 ok. */
static int declare_global_var(gctx *c, const spirv_insn *in)
{
    uint32_t ptrtype = in->w[1], id = in->w[2];
    uint32_t storage = (in->wc >= 4) ? in->w[3] : 0;
    spirv_type *pt = spirv_type_find(c->m, ptrtype);
    uint32_t pointee = pt ? pt->elem : 0;
    spirv_type *bt;
    int set = spirv_deco_int(c->m, id, 0xffffffffu, DecoDescriptorSet, 0);
    int binding = spirv_deco_int(c->m, id, 0xffffffffu, DecoBinding, -1);
    int loc = spirv_deco_int(c->m, id, 0xffffffffu, DecoLocation, -1);
    int builtin = spirv_deco_int(c->m, id, 0xffffffffu, DecoBuiltIn, -1);
    char nm[64], tn[32];
    char tmp[128];

    if (!pt) return decl(c, "bad variable pointer type");
    bt = spirv_type_find(c->m, pointee);

    switch (storage) {
    case SC_Input:
    case SC_Output: {
        int is_out = (storage == SC_Output);
        if (builtin >= 0) {
            if (map_builtin_name(c, (uint32_t)builtin, is_out, nm, sizeof nm) != 0) return -1;
            set_name(c, id, nm);
            c->is_ptr[id] = 1;
            return 0;
        }
        if (loc < 0) return decl(c, "interface variable without a location");
        if (type_name(c, pointee, tn, sizeof tn) != 0) return -1;
        snprintf(nm, sizeof nm, "v%u", id);
        gf(c, "layout(location = %d) %s %s %s;\n", loc, is_out ? "out" : "in", tn, nm);
        set_name(c, id, nm);
        c->is_ptr[id] = 1;
        if (is_out) {
            if (c->info->num_outputs < SPIRV_MAX_LOCATIONS)
                c->info->out_locations[c->info->num_outputs++] = (uint32_t)loc;
        } else {
            if (c->info->num_inputs < SPIRV_MAX_LOCATIONS)
                c->info->in_locations[c->info->num_inputs++] = (uint32_t)loc;
        }
        return 0;
    }
    case SC_StorageBuffer: {
        if (set != 0) return decl(c, "descriptor set != 0 unsupported on GPU path");
        if (binding < 0) return decl(c, "SSBO without a binding");
        if (c->nssbo >= SPIRV_GLSL_MAX_SSBO) return decl(c, "more than 4 storage buffers");
        /* The GLSL binding point is the FLATTENED sequential SSBO slot, not the
         * raw Vulkan descriptor binding: the daemon resolves the dispatch's
         * args[] canvases into the compiled program's QUNIFORM_SSBO_OFFSET slots
         * positionally (args[i] -> the i-th binding), exactly the contract the
         * OpenCL path relies on.  binding_src below keeps the Vulkan descriptor
         * binding so the GPU backend can match it against the bound descriptor
         * set. */
        snprintf(nm, sizeof nm, "ssbo%d", c->nssbo);
        gf(c, "layout(std430, binding = %d) buffer Ssbo%d", c->nssbo, c->nssbo);
        if (emit_ssbo_struct(c, pointee) != 0) return -1;
        gf(c, " %s;\n", nm);
        set_name(c, id, nm);
        c->is_ptr[id] = 1;
        if (add_binding(c, (uint32_t)set, (uint32_t)binding, SPIRV_RES_STORAGE_BUFFER, nm) != 0)
            return -1;
        c->info->bindings[c->info->num_bindings - 1].binding = (uint32_t)c->nssbo;
        c->nssbo++;
        return 0;
    }
    case SC_UniformConstant: {
        if (set != 0) return decl(c, "descriptor set != 0 unsupported on GPU path");
        if (binding < 0) return decl(c, "sampler/image without a binding");
        if (!bt) return decl(c, "bad uniform-constant type");
        snprintf(nm, sizeof nm, "v%u", id);
        if (bt->op == OpTypeSampledImage || bt->op == OpTypeImage) {
            /* only 2D float sampling is expressible on the fixed V3D path */
            gf(c, "layout(binding = %d) uniform sampler2D %s;\n", binding, nm);
            if (add_binding(c, (uint32_t)set, (uint32_t)binding,
                            SPIRV_RES_COMBINED_IMAGE_SAMPLER, nm) != 0) return -1;
        } else if (bt->op == OpTypeSampler) {
            return decl(c, "separate sampler unsupported");
        } else {
            return decl(c, "unsupported uniform-constant type");
        }
        set_name(c, id, nm);
        c->is_ptr[id] = 1;
        return 0;
    }
    case SC_Uniform:      return decl(c, "uniform buffer (UBO) unsupported on GPU path");
    case SC_PushConstant: return decl(c, "push constants unsupported on GPU path");
    case SC_Workgroup:    return decl(c, "workgroup shared memory unsupported");
    case SC_Private:
        /* module-scope private: emit as a main-local below (rare). */
        if (type_name(c, pointee, tn, sizeof tn) != 0) return -1;
        snprintf(tmp, sizeof tmp, "v%u", id);
        gf(c, "/* private */ ");
        set_name(c, id, tmp);
        c->is_ptr[id] = 1;
        return 0;
    default:
        return decl(c, "unsupported storage class");
    }
}

/* ---- statement emission -------------------------------------------------- */
static int assign_start(gctx *c, const spirv_insn *in, int ind)
{
    char tn[32];
    if (type_name(c, in->w[1], tn, sizeof tn) != 0) return -1;
    indent(c, ind);
    gf(c, "%s v%u = ", tn, in->w[2]);
    return 0;
}

static const char *binop_str(uint32_t op)
{
    switch (op) {
    case OpIAdd: case OpFAdd: return "+";
    case OpISub: case OpFSub: return "-";
    case OpIMul: case OpFMul: return "*";
    case OpSDiv: case OpUDiv: case OpFDiv: return "/";
    case OpSRem: case OpUMod: return "%";
    case OpBitwiseAnd: return "&";
    case OpBitwiseOr:  return "|";
    case OpBitwiseXor: return "^";
    case OpShiftLeftLogical: return "<<";
    case OpShiftRightLogical: case OpShiftRightArithmetic: return ">>";
    case OpLogicalAnd: return "&&";
    case OpLogicalOr:  return "||";
    case OpLogicalEqual: case OpIEqual: case OpFOrdEqual: case OpFUnordEqual: return "==";
    case OpLogicalNotEqual: case OpINotEqual: case OpFOrdNotEqual: case OpFUnordNotEqual: return "!=";
    case OpSLessThan: case OpULessThan: case OpFOrdLessThan: case OpFUnordLessThan: return "<";
    case OpSGreaterThan: case OpUGreaterThan: case OpFOrdGreaterThan: case OpFUnordGreaterThan: return ">";
    case OpSLessThanEqual: case OpULessThanEqual: case OpFOrdLessThanEqual: case OpFUnordLessThanEqual: return "<=";
    case OpSGreaterThanEqual: case OpUGreaterThanEqual: case OpFOrdGreaterThanEqual: case OpFUnordGreaterThanEqual: return ">=";
    default: return NULL;
    }
}

static int is_mul_op(uint32_t op)
{
    return op == OpVectorTimesScalar || op == OpMatrixTimesScalar ||
           op == OpVectorTimesMatrix || op == OpMatrixTimesVector ||
           op == OpMatrixTimesMatrix;
}

/* GLSL.std.450 extended instruction -> GLSL call.  Returns 0 on success. */
static int emit_extinst(gctx *c, const spirv_insn *in, int ind)
{
    uint32_t setid = in->w[3], eop = in->w[4];
    const uint32_t *a = in->w + 5;
    uint32_t na = in->wc - 5;
    if (setid != c->m->glsl450_set) return decl(c, "non-GLSL.std.450 extended set");
    switch (eop) {
    case G_FAbs: case G_SAbs: case G_FSign: case G_SSign:
    case G_Floor: case G_Ceil: case G_Fract: case G_Sqrt: case G_InverseSqrt:
    case G_Sin: case G_Cos: case G_Tan: case G_Asin: case G_Acos: case G_Atan:
    case G_Sinh: case G_Cosh: case G_Tanh: case G_Exp: case G_Exp2: case G_Log:
    case G_Log2: case G_Radians: case G_Degrees: case G_Normalize:
        if (na != 1) return decl(c, "bad extinst arity");
        if (assign_start(c, in, ind) != 0) return -1;
        {
            const char *fn;
            switch (eop) {
            case G_FAbs: case G_SAbs: fn = "abs"; break;
            case G_FSign: case G_SSign: fn = "sign"; break;
            case G_Floor: fn = "floor"; break;
            case G_Ceil: fn = "ceil"; break;
            case G_Fract: fn = "fract"; break;
            case G_Sqrt: fn = "sqrt"; break;
            case G_InverseSqrt: fn = "inversesqrt"; break;
            case G_Sin: fn = "sin"; break; case G_Cos: fn = "cos"; break;
            case G_Tan: fn = "tan"; break; case G_Asin: fn = "asin"; break;
            case G_Acos: fn = "acos"; break; case G_Atan: fn = "atan"; break;
            case G_Sinh: fn = "sinh"; break; case G_Cosh: fn = "cosh"; break;
            case G_Tanh: fn = "tanh"; break; case G_Exp: fn = "exp"; break;
            case G_Exp2: fn = "exp2"; break; case G_Log: fn = "log"; break;
            case G_Log2: fn = "log2"; break; case G_Radians: fn = "radians"; break;
            case G_Degrees: fn = "degrees"; break; default: fn = "normalize"; break;
            }
            gf(c, "%s(%s);\n", fn, idname(c, a[0]));
        }
        return 0;
    case G_Trunc:
        if (na != 1) return decl(c, "bad extinst arity");
        /* GLSL ES 3.10 has no trunc(); x - fract(x) is exact for finite x. */
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "(%s - fract(%s));\n", idname(c, a[0]), idname(c, a[0]));
        return 0;
    case G_Pow: case G_Atan2: case G_FMin: case G_NMin: case G_FMax: case G_NMax:
    case G_UMin: case G_SMin: case G_UMax: case G_SMax: case G_Distance:
    case G_Cross: case G_Reflect: case G_FMix:
        if (na != 2 && eop != G_FMix) return decl(c, "bad extinst arity");
        if (assign_start(c, in, ind) != 0) return -1;
        {
            const char *fn;
            switch (eop) {
            case G_Pow: fn = "pow"; break; case G_Atan2: fn = "atan"; break;
            case G_FMin: case G_NMin: case G_UMin: case G_SMin: fn = "min"; break;
            case G_FMax: case G_NMax: case G_UMax: case G_SMax: fn = "max"; break;
            case G_Distance: fn = "distance"; break; case G_Cross: fn = "cross"; break;
            case G_Reflect: fn = "reflect"; break; default: fn = "mix"; break;
            }
            gf(c, "%s(%s, %s);\n", fn, idname(c, a[0]), idname(c, a[1]));
        }
        return 0;
    case G_FClamp: case G_NClamp: case G_UClamp: case G_SClamp: case G_SmoothStep:
    case G_Fma:
        if (na != 3) return decl(c, "bad extinst arity");
        if (assign_start(c, in, ind) != 0) return -1;
        {
            const char *fn = (eop == G_SmoothStep) ? "smoothstep" :
                             (eop == G_Fma) ? "fma" : "clamp";
            gf(c, "%s(%s, %s, %s);\n", fn, idname(c, a[0]), idname(c, a[1]), idname(c, a[2]));
        }
        return 0;
    case G_Step:
        if (na != 2) return decl(c, "bad extinst arity");
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "step(%s, %s);\n", idname(c, a[0]), idname(c, a[1]));
        return 0;
    case G_Length:
        if (na != 1) return decl(c, "bad extinst arity");
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "length(%s);\n", idname(c, a[0]));
        return 0;
    default:
        return decl(c, "unsupported GLSL.std.450 instruction");
    }
}

/* Build the component-path suffix (extract/insert share it).  SPIR-V composite
 * indices are literal words, not ids. */
static int build_member_path(gctx *c, uint32_t cur_tid, const uint32_t *idx,
                             uint32_t nidx, char *out, size_t n)
{
    uint32_t i;
    out[0] = '\0';
    for (i = 0; i < nidx; i++) {
        spirv_type *t = spirv_type_find(c->m, cur_tid);
        char piece[32];
        uint32_t k = idx[i];   /* literal index */
        if (!t) return decl(c, "composite: bad type");
        if (t->op == OpTypeStruct) {
            member_sel(c, cur_tid, k, piece, sizeof piece);
            cur_tid = struct_member_type(c, cur_tid, k);
        } else if (t->op == OpTypeVector) {
            if (k > 3) return decl(c, "vector index out of range");
            snprintf(piece, sizeof piece, ".%c", swz[k]);
            cur_tid = t->elem;
        } else if (t->op == OpTypeMatrix || t->op == OpTypeArray ||
                   t->op == OpTypeRuntimeArray) {
            snprintf(piece, sizeof piece, "[%u]", k);
            cur_tid = t->elem;
        } else {
            return decl(c, "composite into unsupported type");
        }
        if (strlen(out) + strlen(piece) + 1 >= n) return decl(c, "composite path too long");
        strcat(out, piece);
    }
    return 0;
}

static int emit_stmt(gctx *c, const spirv_insn *in, int ind)
{
    const uint32_t *w = in->w;
    char tn[32];

    switch (in->op) {
    case OpNop: case OpLine: case OpLoopMerge: case OpSelectionMerge:
        return 0;

    case OpUndef:
        if (type_name(c, w[1], tn, sizeof tn) != 0) return -1;
        indent(c, ind); gf(c, "%s v%u;\n", tn, w[2]);
        return 0;

    case OpVariable: {
        uint32_t storage = (in->wc >= 4) ? w[3] : 0;
        spirv_type *pt = spirv_type_find(c->m, w[1]);
        uint32_t pointee = pt ? pt->elem : 0;
        char nm[32];
        if (storage != SC_Function)
            return decl(c, "non-function variable inside a body");
        if (type_name(c, pointee, tn, sizeof tn) != 0) return -1;
        snprintf(nm, sizeof nm, "v%u", w[2]);
        indent(c, ind);
        if (in->wc >= 5) gf(c, "%s %s = %s;\n", tn, nm, idname(c, w[4]));
        else gf(c, "%s %s;\n", tn, nm);
        set_name(c, w[2], nm);
        c->is_ptr[w[2]] = 1;
        return 0;
    }

    case OpAccessChain: case OpInBoundsAccessChain: {
        spirv_type *bt = spirv_type_find(c->m, id_type(c, w[3]));
        uint32_t pointee = bt ? bt->elem : 0;
        char *lv = build_access_chain(c, idname(c, w[3]), pointee, w + 4, in->wc - 4);
        if (!lv) return -1;
        set_name(c, w[2], lv);
        free(lv);
        c->is_ptr[w[2]] = 1;
        return 0;
    }

    case OpLoad: {
        spirv_type *rt = spirv_type_find(c->m, w[1]);
        /* loading a sampler/image just aliases the uniform name */
        if (rt && (rt->op == OpTypeSampledImage || rt->op == OpTypeImage ||
                   rt->op == OpTypeSampler)) {
            set_name(c, w[2], idname(c, w[3]));
            return 0;
        }
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "%s;\n", idname(c, w[3]));
        return 0;
    }

    case OpStore:
        indent(c, ind);
        gf(c, "%s = %s;\n", idname(c, w[1]), idname(c, w[2]));
        return 0;

    case OpArrayLength: {
        /* base ptr w[3], member index w[4]; runtime array named "data" */
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "uint(%s.data.length());\n", idname(c, w[3]));
        return 0;
    }

    case OpCopyObject:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "%s;\n", idname(c, w[3]));
        return 0;

    case OpSelect: {
        /* only a scalar-bool condition maps onto the GLSL ternary */
        spirv_type *ct = spirv_type_find(c->m, id_type(c, w[3]));
        if (!ct || ct->op != OpTypeBool) return decl(c, "vector select unsupported");
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "(%s ? %s : %s);\n", idname(c, w[3]), idname(c, w[4]), idname(c, w[5]));
        return 0;
    }

    case OpSNegate: case OpFNegate:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "(-%s);\n", idname(c, w[3]));
        return 0;
    case OpNot:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "(~%s);\n", idname(c, w[3]));
        return 0;
    case OpLogicalNot:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "(!%s);\n", idname(c, w[3]));
        return 0;

    case OpDot:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "dot(%s, %s);\n", idname(c, w[3]), idname(c, w[4]));
        return 0;
    case OpTranspose:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "transpose(%s);\n", idname(c, w[3]));
        return 0;
    case OpOuterProduct:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "outerProduct(%s, %s);\n", idname(c, w[3]), idname(c, w[4]));
        return 0;

    case OpConvertFToS: case OpConvertFToU: case OpConvertSToF: case OpConvertUToF:
    case OpUConvert: case OpSConvert: case OpFConvert:
        if (assign_start(c, in, ind) != 0) return -1;
        {
            char rn[32];
            if (type_name(c, w[1], rn, sizeof rn) != 0) return -1;
            gf(c, "%s(%s);\n", rn, idname(c, w[3]));
        }
        return 0;

    case OpBitcast: {
        spirv_type *st = spirv_type_find(c->m, id_type(c, w[3]));
        spirv_type *dt = spirv_type_find(c->m, w[1]);
        spirv_type *se, *de;
        int sf, df;
        if (!st || !dt) return decl(c, "bitcast: bad type");
        se = (st->op == OpTypeVector) ? spirv_type_find(c->m, st->elem) : st;
        de = (dt->op == OpTypeVector) ? spirv_type_find(c->m, dt->elem) : dt;
        if (!se || !de) return decl(c, "bitcast: bad element");
        sf = (se->op == OpTypeFloat);
        df = (de->op == OpTypeFloat);
        if (sf == df) {
            /* float<->float or int<->uint: same-width reinterpret */
            if (sf) { if (assign_start(c, in, ind) != 0) return -1; gf(c, "%s;\n", idname(c, w[3])); return 0; }
            return decl(c, "int/uint bitcast unsupported");
        }
        if (assign_start(c, in, ind) != 0) return -1;
        if (sf) gf(c, "%s(%s);\n", de->is_signed ? "floatBitsToInt" : "floatBitsToUint", idname(c, w[3]));
        else    gf(c, "%s(%s);\n", se->is_signed ? "intBitsToFloat" : "uintBitsToFloat", idname(c, w[3]));
        return 0;
    }

    case OpCompositeConstruct:
        if (assign_start(c, in, ind) != 0) return -1;
        {
            char rn[32];
            uint32_t k;
            if (type_name(c, w[1], rn, sizeof rn) != 0) return -1;
            gf(c, "%s(", rn);
            for (k = 3; k < in->wc; k++)
                gf(c, "%s%s", (k > 3) ? ", " : "", idname(c, w[k]));
            gf(c, ");\n");
        }
        return 0;

    case OpCompositeExtract: {
        char path[128];
        uint32_t bt = id_type(c, w[3]);
        if (build_member_path(c, bt, w + 4, in->wc - 4, path, sizeof path) != 0) return -1;
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "%s%s;\n", idname(c, w[3]), path);
        return 0;
    }

    case OpCompositeInsert: {
        char path[128];
        uint32_t bt = id_type(c, w[4]);
        if (build_member_path(c, bt, w + 5, in->wc - 5, path, sizeof path) != 0) return -1;
        if (type_name(c, w[1], tn, sizeof tn) != 0) return -1;
        indent(c, ind); gf(c, "%s v%u = %s;\n", tn, w[2], idname(c, w[4]));
        indent(c, ind); gf(c, "v%u%s = %s;\n", w[2], path, idname(c, w[3]));
        return 0;
    }

    case OpVectorShuffle: {
        spirv_type *v1 = spirv_type_find(c->m, id_type(c, w[3]));
        uint32_t n0 = v1 ? v1->comps : 0, k;
        if (!v1 || v1->op != OpTypeVector) return decl(c, "shuffle of a non-vector");
        if (assign_start(c, in, ind) != 0) return -1;
        {
            char rn[32];
            if (type_name(c, w[1], rn, sizeof rn) != 0) return -1;
            gf(c, "%s(", rn);
        }
        for (k = 5; k < in->wc; k++) {
            uint32_t comp = w[k];
            gf(c, "%s%s.%c", (k > 5) ? ", " : "",
               (comp < n0) ? idname(c, w[3]) : idname(c, w[4]),
               swz[(comp < n0) ? comp : comp - n0]);
        }
        gf(c, ");\n");
        return 0;
    }

    case OpImageSampleImplicitLod:
        if (assign_start(c, in, ind) != 0) return -1;
        gf(c, "texture(%s, %s);\n", idname(c, w[3]), idname(c, w[4]));
        return 0;

    case OpExtInst:
        return emit_extinst(c, in, ind);

    default: {
        const char *bo = binop_str(in->op);
        if (bo) {
            if (in->wc < 5) return decl(c, "bad binary op");
            if (assign_start(c, in, ind) != 0) return -1;
            gf(c, "(%s %s %s);\n", idname(c, w[3]), bo, idname(c, w[4]));
            return 0;
        }
        if (is_mul_op(in->op)) {
            if (assign_start(c, in, ind) != 0) return -1;
            gf(c, "(%s * %s);\n", idname(c, w[3]), idname(c, w[4]));
            return 0;
        }
        if (in->op == OpFMod) {
            if (assign_start(c, in, ind) != 0) return -1;
            gf(c, "mod(%s, %s);\n", idname(c, w[3]), idname(c, w[4]));
            return 0;
        }
        {
            char msg[64];
            snprintf(msg, sizeof msg, "unsupported opcode %u", in->op);
            return decl(c, msg);
        }
    }
    }
}

/* ---- structured control flow --------------------------------------------- */
typedef struct {
    uint32_t label;
    int32_t  start;
    int32_t  term;
    uint32_t merge, cont;
    int      is_loop, is_sel;
} blockinfo;

static int get_block(gctx *c, uint32_t label, blockinfo *bi)
{
    int32_t li, pc;
    if (label >= c->m->bound) return decl(c, "bad label");
    li = c->m->label_at[label];
    if (li < 0 || c->m->insns[li].op != OpLabel) return decl(c, "label is not a block");
    memset(bi, 0, sizeof(*bi));
    bi->label = label;
    bi->start = li;
    for (pc = li + 1; pc < (int32_t)c->m->ninsns; pc++) {
        spirv_insn *in = &c->m->insns[pc];
        switch (in->op) {
        case OpLoopMerge:
            bi->is_loop = 1; bi->merge = in->w[1]; bi->cont = in->w[2];
            break;
        case OpSelectionMerge:
            bi->is_sel = 1; bi->merge = in->w[1];
            break;
        case OpBranch: case OpBranchConditional: case OpReturn: case OpReturnValue:
        case OpKill: case OpUnreachable: case OpSwitch: case OpTerminateInvocation:
            bi->term = pc;
            return 0;
        default:
            break;
        }
    }
    return decl(c, "block without a terminator");
}

/* Instructions that may legally precede the OpPhi run at the head of a block
 * (merge/line markers).  The phi scan skips these and stops at anything else. */
static int phi_prelude_op(uint32_t op)
{
    return op == OpLoopMerge || op == OpSelectionMerge ||
           op == OpLine || op == OpNop;
}

/* Emit phi copies flowing along the single-successor edge from -> to. */
static void emit_phi_copies(gctx *c, uint32_t to, uint32_t from, int ind)
{
    int32_t li, pc;
    if (to >= c->m->bound) return;
    li = c->m->label_at[to];
    if (li < 0) return;
    for (pc = li + 1; pc < (int32_t)c->m->ninsns; pc++) {
        spirv_insn *in = &c->m->insns[pc];
        uint32_t k;
        if (in->op != OpPhi) {
            if (phi_prelude_op(in->op)) continue;
            break;
        }
        for (k = 3; k + 1 < in->wc; k += 2) {
            if (in->w[k + 1] == from) {
                indent(c, ind);
                gf(c, "v%u = %s;\n", in->w[2], idname(c, in->w[k]));
                break;
            }
        }
    }
}

static int phi_uses_edge(gctx *c, uint32_t to, uint32_t from)
{
    int32_t li, pc;
    if (to >= c->m->bound) return 0;
    li = c->m->label_at[to];
    if (li < 0) return 0;
    for (pc = li + 1; pc < (int32_t)c->m->ninsns; pc++) {
        spirv_insn *in = &c->m->insns[pc];
        uint32_t k;
        if (in->op != OpPhi) {
            if (phi_prelude_op(in->op)) continue;
            break;
        }
        for (k = 3; k + 1 < in->wc; k += 2)
            if (in->w[k + 1] == from) return 1;
    }
    return 0;
}

static int is_barrier(gctx *c, uint32_t label)
{
    int i;
    for (i = 0; i < c->nbar; i++) if (c->barriers[i] == label) return 1;
    return 0;
}

static void emit_region(gctx *c, uint32_t label, uint32_t follow, int ind);

static void emit_body(gctx *c, const blockinfo *bi, int ind)
{
    int32_t pc;
    for (pc = bi->start + 1; pc < bi->term && !c->declined; pc++) {
        spirv_insn *in = &c->m->insns[pc];
        if (in->op == OpPhi || in->op == OpLabel) continue;
        if (emit_stmt(c, in, ind) != 0) return;
    }
}

static void emit_loop(gctx *c, const blockinfo *bi, uint32_t follow, int ind)
{
    spirv_insn *term = &c->m->insns[bi->term];
    uint32_t body_entry;
    indent(c, ind); gf(c, "for (;;) {\n");
    emit_body(c, bi, ind + 1);
    if (c->declined) return;
    if (term->op == OpBranchConditional) {
        uint32_t t = term->w[2], f = term->w[3];
        if (t == bi->merge) {
            indent(c, ind + 1); gf(c, "if (%s) break;\n", idname(c, term->w[1]));
            body_entry = f;
        } else if (f == bi->merge) {
            indent(c, ind + 1); gf(c, "if (!(%s)) break;\n", idname(c, term->w[1]));
            body_entry = t;
        } else {
            decl(c, "loop exit not through the merge block"); return;
        }
    } else if (term->op == OpBranch) {
        uint32_t t = term->w[1];
        if (t == bi->merge) {
            indent(c, ind + 1); gf(c, "break;\n");
            body_entry = 0;
        } else {
            body_entry = t;
        }
    } else {
        decl(c, "loop header without a branch"); return;
    }
    if (c->nbar + 2 > MAX_BARRIERS) { decl(c, "control flow too deep"); return; }
    c->barriers[c->nbar++] = bi->merge;
    c->barriers[c->nbar++] = bi->cont;
    if (body_entry) emit_region(c, body_entry, bi->cont, ind + 1);
    c->nbar--;   /* pop cont so the continue block itself can be entered */
    emit_region(c, bi->cont, bi->label, ind + 1);
    c->nbar--;   /* pop merge */
    indent(c, ind); gf(c, "}\n");
    emit_region(c, bi->merge, follow, ind);
}

static void emit_region(gctx *c, uint32_t label, uint32_t follow, int ind)
{
    blockinfo bi;
    spirv_insn *term;

    if (c->declined) return;
    if (label == 0 || label == follow) return;
    if (++c->depth > MAX_REGION_DEPTH) { c->depth--; decl(c, "control flow too deep"); return; }
    if (is_barrier(c, label)) { c->depth--; decl(c, "irregular control flow (break/continue escape)"); return; }

    if (get_block(c, label, &bi) != 0) { c->depth--; return; }

    if (bi.is_loop) {
        emit_loop(c, &bi, follow, ind);
        c->depth--;
        return;
    }

    emit_body(c, &bi, ind);
    term = &c->m->insns[bi.term];
    switch (term->op) {
    case OpReturn:
        indent(c, ind); gf(c, "return;\n");
        break;
    case OpUnreachable:
        break;
    case OpKill: case OpTerminateInvocation:
        if (c->ep->exec_model != SPIRV_EXEC_FRAGMENT) { decl(c, "discard outside a fragment shader"); break; }
        indent(c, ind); gf(c, "discard;\n");
        break;
    case OpReturnValue:
        decl(c, "return with a value in an entry point");
        break;
    case OpSwitch:
        decl(c, "switch statements unsupported");
        break;
    case OpBranch: {
        uint32_t t = term->w[1];
        emit_phi_copies(c, t, label, ind);
        if (t != follow) emit_region(c, t, follow, ind);
        break;
    }
    case OpBranchConditional: {
        uint32_t cond = term->w[1], t = term->w[2], f = term->w[3];
        if (!bi.is_sel) { decl(c, "unstructured conditional branch"); break; }
        if (phi_uses_edge(c, t, label) || phi_uses_edge(c, f, label)) {
            decl(c, "phi on a critical edge"); break;
        }
        if (c->nbar + 1 > MAX_BARRIERS) { decl(c, "control flow too deep"); break; }
        c->barriers[c->nbar++] = bi.merge;
        if (t == bi.merge && f == bi.merge) {
            /* both edges go straight to the merge: nothing to emit */
        } else if (t == bi.merge) {
            indent(c, ind); gf(c, "if (!(%s)) {\n", idname(c, cond));
            emit_region(c, f, bi.merge, ind + 1);
            indent(c, ind); gf(c, "}\n");
        } else if (f == bi.merge) {
            indent(c, ind); gf(c, "if (%s) {\n", idname(c, cond));
            emit_region(c, t, bi.merge, ind + 1);
            indent(c, ind); gf(c, "}\n");
        } else {
            indent(c, ind); gf(c, "if (%s) {\n", idname(c, cond));
            emit_region(c, t, bi.merge, ind + 1);
            indent(c, ind); gf(c, "} else {\n");
            emit_region(c, f, bi.merge, ind + 1);
            indent(c, ind); gf(c, "}\n");
        }
        c->nbar--;
        emit_region(c, bi.merge, follow, ind);
        break;
    }
    default:
        decl(c, "unsupported terminator");
        break;
    }
    c->depth--;
}

/* Declare every OpPhi result in the entry as a mutable local at the top. */
static void declare_phis(gctx *c, int ind)
{
    int32_t pc;
    for (pc = c->f->first_block; pc <= (int32_t)c->f->end; pc++) {
        spirv_insn *in = &c->m->insns[pc];
        char tn[32];
        if (in->op != OpPhi) continue;
        if (type_name(c, in->w[1], tn, sizeof tn) != 0) return;
        indent(c, ind); gf(c, "%s v%u;\n", tn, in->w[2]);
    }
}

/* ---- top level ----------------------------------------------------------- */
char *spirv_to_glsl(spirv_module *m, const spirv_entry *e,
                    spirv_glsl_info *info, char *err, size_t errsz)
{
    gctx c;
    spirv_func *f;
    uint32_t i, first_func, first_label;
    char *out;

    if (err && errsz) err[0] = '\0';
    if (!m || !e || !info) { if (err && errsz) snprintf(err, errsz, "bad args"); return NULL; }
    memset(&c, 0, sizeof(c));
    memset(info, 0, sizeof(*info));
    c.m = m;
    c.ep = (const spirv_ep *)e;
    c.info = info;
    info->exec_model = e->exec_model;

    f = spirv_func_find(m, c.ep->id);
    if (!f || f->first_block == 0xffffffffu) {
        if (err && errsz) snprintf(err, errsz, "entry function not found");
        return NULL;
    }
    c.f = f;
    info->local_size[0] = e->local_size[0];
    info->local_size[1] = e->local_size[1];
    info->local_size[2] = e->local_size[2];

    c.name = (char **)calloc(m->bound ? m->bound : 1, sizeof(char *));
    c.is_ptr = (uint8_t *)calloc(m->bound ? m->bound : 1, 1);
    if (!c.name || !c.is_ptr) { decl(&c, "out of memory"); goto done; }

    /* ---- header ---- */
    gf(&c, "#version 310 es\n");
    gf(&c, "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n\n");
    if (e->exec_model == SPIRV_EXEC_GLCOMPUTE) {
        uint32_t lx = e->local_size[0] ? e->local_size[0] : 1;
        uint32_t ly = e->local_size[1] ? e->local_size[1] : 1;
        uint32_t lz = e->local_size[2] ? e->local_size[2] : 1;
        if (lx * ly * lz > 256) { decl(&c, "workgroup too large for V3D CSD"); goto done; }
        gf(&c, "layout(local_size_x = %u, local_size_y = %u, local_size_z = %u) in;\n\n",
           lx, ly, lz);
    }

    /* ---- module-scope resources (everything before the first function) ---- */
    first_func = m->nfuncs ? m->funcs[0].start : m->ninsns;
    for (i = 0; i < m->ninsns && i < first_func && !c.declined; i++) {
        if (m->insns[i].op == OpVariable) {
            if (declare_global_var(&c, &m->insns[i]) != 0) goto done;
        }
    }
    if (c.declined) goto done;
    gf(&c, "\n");

    /* ---- main ---- */
    gf(&c, "void main() {\n");
    declare_phis(&c, 1);
    first_label = m->insns[f->first_block].w[1];
    emit_region(&c, first_label, 0, 1);
    gf(&c, "}\n");

done:
    if (c.declined) {
        if (err && errsz) snprintf(err, errsz, "%s", c.err[0] ? c.err : "declined");
        free(c.buf);
        out = NULL;
    } else {
        out = c.buf;   /* hand ownership to the caller */
    }
    if (c.name) {
        uint32_t k;
        for (k = 0; k < m->bound; k++) free(c.name[k]);
        free(c.name);
    }
    free(c.is_ptr);
    return out;
}
