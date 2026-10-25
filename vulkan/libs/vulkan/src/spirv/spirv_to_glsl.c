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

/* Kernel-generation modes (see spirv_to_geom_kernels).  GM_NONE is the plain
 * single-entry spirv_to_glsl() path; GM_XFORM/GM_RASTER redirect the interface
 * variables onto generated harness parameters instead of GLSL in/out. */
enum { GM_NONE = 0, GM_XFORM = 1, GM_RASTER = 2 };

/* One recorded graphics-stage interface variable (location in/out). */
typedef struct {
    uint32_t id;
    int      loc;
    uint32_t comps;
    char     tn[12];     /* GLSL value type ("float"/"vec2"/..)              */
    char     pname[24];  /* generated harness parameter / local name          */
} kern_iface;

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

    /* ---- kernel-generation state (GM_XFORM / GM_RASTER only) ---- */
    int        mode;
    kern_iface kin[SPIRV_MAX_LOCATIONS];  int nkin;   /* inputs / varyings    */
    kern_iface kout[SPIRV_MAX_LOCATIONS]; int nkout;  /* VS outputs (varyings)*/
    int        has_position;               /* VS wrote gl_Position            */
    int        has_vertexid;               /* VS reads gl_VertexID            */
    int        has_fragcoord;              /* FS reads gl_FragCoord           */
    int        has_frontfacing;            /* FS reads gl_FrontFacing         */
    int        has_color;                  /* FS wrote a vec4 colour output   */
    int        nsampler;
    uint32_t   sampler_set[SPIRV_GEOM_MAX_SAMPLERS];
    uint32_t   sampler_binding[SPIRV_GEOM_MAX_SAMPLERS];
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
        if (c->mode == GM_RASTER) {
            /* the FS body runs inside vk_fs(); discard becomes "flag + return"
             * so the harness skips this pixel but keeps sweeping the tile. */
            indent(c, ind); gf(c, "vkKilled = true; return;\n");
            break;
        }
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

/* ==========================================================================
 * Geometry-kernel generation mode (see spirv_to_glsl.h / spirv_to_geom_kernels)
 * ========================================================================== */

/* Verify an interface variable is a 32-bit float scalar or 2/3/4-vector and
 * report its component count; anything else declines (the attribute/varying
 * streams are plain float[]). */
static int iface_float_comps(gctx *c, uint32_t tid, uint32_t *comps)
{
    spirv_type *t = spirv_type_find(c->m, tid);
    if (!t) return decl(c, "bad interface type");
    if (t->op == OpTypeFloat && t->width == 32) { *comps = 1; return 0; }
    if (t->op == OpTypeVector) {
        spirv_type *e = spirv_type_find(c->m, t->elem);
        if (e && e->op == OpTypeFloat && e->width == 32 &&
            t->comps >= 1 && t->comps <= 4) { *comps = t->comps; return 0; }
    }
    return decl(c, "kernel interface must be a float scalar/vector");
}

/* The ".xyzw" selector for component k, or "" for a scalar. */
static const char *comp_sel(uint32_t comps, uint32_t k, char *buf, size_t n)
{
    if (comps == 1) { buf[0] = '\0'; return buf; }
    snprintf(buf, n, ".%c", "xyzw"[k]);
    return buf;
}

/* Declare one module-scope OpVariable for a generated kernel.  Interface
 * variables are RECORDED (not emitted as GLSL in/out) and bound to harness
 * parameter names; samplers are emitted as TMU uniforms (raster pass only);
 * everything else on the graphics path declines. */
static int declare_global_var_kernel(gctx *c, const spirv_insn *in)
{
    uint32_t ptrtype = in->w[1], id = in->w[2];
    uint32_t storage = (in->wc >= 4) ? in->w[3] : 0;
    spirv_type *pt = spirv_type_find(c->m, ptrtype);
    uint32_t pointee = pt ? pt->elem : 0;
    spirv_type *bt = spirv_type_find(c->m, pointee);
    int set     = spirv_deco_int(c->m, id, 0xffffffffu, DecoDescriptorSet, 0);
    int binding = spirv_deco_int(c->m, id, 0xffffffffu, DecoBinding, -1);
    int loc     = spirv_deco_int(c->m, id, 0xffffffffu, DecoLocation, -1);
    int builtin = spirv_deco_int(c->m, id, 0xffffffffu, DecoBuiltIn, -1);
    char nm[32], tn[12];
    uint32_t comps;
    int j;

    if (!pt) return decl(c, "bad variable pointer type");

    switch (storage) {
    case SC_Input:
    case SC_Output: {
        int is_out = (storage == SC_Output);
        if (builtin >= 0) {
            if (c->mode == GM_XFORM) {
                if (!is_out && builtin == BiVertexIndex) {
                    c->has_vertexid = 1; set_name(c, id, "vkVertexIndex");
                    c->is_ptr[id] = 1; return 0;
                }
                if (is_out && builtin == BiPosition) {
                    c->has_position = 1; set_name(c, id, "vkPosition");
                    c->is_ptr[id] = 1; return 0;
                }
                return decl(c, "unsupported vertex built-in");
            } else { /* GM_RASTER */
                if (!is_out && builtin == BiFragCoord) {
                    c->has_fragcoord = 1; set_name(c, id, "vkFragCoord");
                    c->is_ptr[id] = 1; return 0;
                }
                if (!is_out && builtin == BiFrontFacing) {
                    c->has_frontfacing = 1; set_name(c, id, "vkFrontFacing");
                    c->is_ptr[id] = 1; return 0;
                }
                return decl(c, "unsupported fragment built-in");
            }
        }
        if (loc < 0) return decl(c, "interface variable without a location");
        if (iface_float_comps(c, pointee, &comps) != 0) return -1;
        if (type_name(c, pointee, tn, sizeof tn) != 0) return -1;
        if (is_out && c->mode == GM_RASTER) {
            /* the single fragment colour attachment */
            if (c->has_color) return decl(c, "multiple fragment outputs unsupported");
            if (comps != 4) return decl(c, "fragment output must be a vec4");
            c->has_color = 1; set_name(c, id, "vkColor"); c->is_ptr[id] = 1;
            return 0;
        }
        snprintf(nm, sizeof nm, is_out ? "ko%d" : "ki%d", loc);
        set_name(c, id, nm);
        c->is_ptr[id] = 1;
        if (is_out) {
            if (c->nkout >= SPIRV_MAX_LOCATIONS) return decl(c, "too many vertex outputs");
            j = c->nkout++;
            c->kout[j].id = id; c->kout[j].loc = loc; c->kout[j].comps = comps;
            snprintf(c->kout[j].tn, sizeof c->kout[j].tn, "%s", tn);
            snprintf(c->kout[j].pname, sizeof c->kout[j].pname, "%s", nm);
        } else {
            if (c->nkin >= SPIRV_MAX_LOCATIONS) return decl(c, "too many inputs");
            j = c->nkin++;
            c->kin[j].id = id; c->kin[j].loc = loc; c->kin[j].comps = comps;
            snprintf(c->kin[j].tn, sizeof c->kin[j].tn, "%s", tn);
            snprintf(c->kin[j].pname, sizeof c->kin[j].pname, "%s", nm);
        }
        return 0;
    }
    case SC_UniformConstant: {
        if (c->mode != GM_RASTER)
            return decl(c, "sampler/image in a vertex kernel unsupported");
        if (set != 0) return decl(c, "descriptor set != 0 unsupported on GPU path");
        if (binding < 0) return decl(c, "sampler without a binding");
        if (!bt || (bt->op != OpTypeSampledImage && bt->op != OpTypeImage))
            return decl(c, "unsupported uniform-constant on GPU geom path");
        if (c->nsampler >= SPIRV_GEOM_MAX_SAMPLERS)
            return decl(c, "more than 4 fragment samplers");
        snprintf(nm, sizeof nm, "v%u", id);
        gf(c, "layout(binding = %d) uniform sampler2D %s;\n", binding, nm);
        set_name(c, id, nm);
        c->is_ptr[id] = 1;
        c->sampler_set[c->nsampler] = (uint32_t)set;
        c->sampler_binding[c->nsampler] = (uint32_t)binding;
        c->nsampler++;
        return 0;
    }
    case SC_Uniform:
    case SC_PushConstant:
        return decl(c, "uniform buffer / push constants unsupported on GPU geom path");
    case SC_StorageBuffer:
        return decl(c, "storage buffer unsupported on GPU geom path");
    default:
        return decl(c, "unsupported storage class on GPU geom path");
    }
}

/* Set up a kernel-mode gctx for one module + entry.  Returns 0 on success. */
static int kern_init(gctx *c, spirv_module *m, const spirv_entry *e, int mode,
                     spirv_glsl_info *scratch)
{
    spirv_func *f;
    memset(c, 0, sizeof(*c));
    c->m = m; c->ep = (const spirv_ep *)e; c->mode = mode; c->info = scratch;
    f = spirv_func_find(m, c->ep->id);
    if (!f || f->first_block == 0xffffffffu) return -1;
    c->f = f;
    c->name = (char **)calloc(m->bound ? m->bound : 1, sizeof(char *));
    c->is_ptr = (uint8_t *)calloc(m->bound ? m->bound : 1, 1);
    if (!c->name || !c->is_ptr) { decl(c, "out of memory"); return -1; }
    return 0;
}

static void kern_free(gctx *c)
{
    if (c->name) {
        uint32_t k;
        for (k = 0; k < c->m->bound; k++) free(c->name[k]);
        free(c->name);
    }
    free(c->is_ptr);
}

/* Scan the module-scope OpVariables before the first function body. */
static int kern_scan_globals(gctx *c)
{
    spirv_module *m = c->m;
    uint32_t i, first_func = m->nfuncs ? m->funcs[0].start : m->ninsns;
    for (i = 0; i < m->ninsns && i < first_func && !c->declined; i++)
        if (m->insns[i].op == OpVariable)
            if (declare_global_var_kernel(c, &m->insns[i]) != 0) return -1;
    return c->declined ? -1 : 0;
}

/* ---- pass 1: per-vertex transform kernel -------------------------------- */
static char *gen_xform_kernel(spirv_module *m, const spirv_entry *e,
                              spirv_geom_layout *L, char *err, size_t errsz)
{
    gctx c;
    spirv_glsl_info scratch;
    uint32_t first_label, aoff = 0, voff = 8, nvar = 0;
    char *out = NULL, sel[8];
    int j, k;
    const char *sep;

    if (err && errsz) err[0] = '\0';
    memset(&scratch, 0, sizeof(scratch));
    if (kern_init(&c, m, e, GM_XFORM, &scratch) != 0) {
        if (err && errsz) snprintf(err, errsz, "vertex entry not found");
        return NULL;
    }

    gf(&c, "#version 310 es\n");
    gf(&c, "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n\n");
    gf(&c, "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n\n");

    if (kern_scan_globals(&c) != 0) goto done;
    if (!c.has_position) { decl(&c, "vertex shader does not write gl_Position"); goto done; }

    /* attribute / vout layout */
    L->num_attrs = c.nkin;
    for (j = 0; j < c.nkin; j++) {
        L->attrs[j].location = (uint32_t)c.kin[j].loc;
        L->attrs[j].offset = aoff;
        L->attrs[j].comps = c.kin[j].comps;
        aoff += c.kin[j].comps;
    }
    L->attr_stride = aoff;
    L->num_varyings = c.nkout;
    for (j = 0; j < c.nkout; j++) {
        L->varyings[j].location = (uint32_t)c.kout[j].loc;
        L->varyings[j].offset = voff;
        L->varyings[j].comps = c.kout[j].comps;
        voff += c.kout[j].comps;
        nvar += c.kout[j].comps;
    }
    L->vout_stride = 8 + 4 * ((nvar + 3) / 4);
    L->uses_vertex_id = c.has_vertexid;

    gf(&c, "layout(std430, binding = 0) buffer PglCsVout { float vout[]; };\n");
    gf(&c, "layout(std430, binding = 1) buffer PglCsAttr { float vattr[]; };\n\n");
    gf(&c, "uniform vec4 uVpOffsetScale;\n");   /* (vp.x, vp.y, vp.w*0.5, vp.h*0.5) */
    gf(&c, "uniform vec2 uVpDepth;\n");         /* (minDepth, maxDepth-minDepth)    */
    gf(&c, "uniform int  uVertCount;\n");
    gf(&c, "uniform int  uVertexBase;\n\n");

    /* the user VS body, interface redirected onto parameters */
    sep = "";
    gf(&c, "void vk_vs(");
    for (j = 0; j < c.nkin; j++) { gf(&c, "%sin %s %s", sep, c.kin[j].tn, c.kin[j].pname); sep = ", "; }
    if (c.has_vertexid) { gf(&c, "%sin int vkVertexIndex", sep); sep = ", "; }
    gf(&c, "%sinout vec4 vkPosition", sep); sep = ", ";
    for (j = 0; j < c.nkout; j++) { gf(&c, "%sinout %s %s", sep, c.kout[j].tn, c.kout[j].pname); sep = ", "; }
    gf(&c, ") {\n");
    declare_phis(&c, 1);
    first_label = m->insns[c.f->first_block].w[1];
    emit_region(&c, first_label, 0, 1);
    if (c.declined) goto done;
    gf(&c, "}\n\n");

    /* main(): one invocation per vertex */
    gf(&c, "void main() {\n");
    gf(&c, "    int vkVid = int(gl_WorkGroupID.x) * 64 + int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x);\n");
    gf(&c, "    if (vkVid >= uVertCount) return;\n");
    if (L->attr_stride) gf(&c, "    int abase = vkVid * %u;\n", L->attr_stride);
    for (j = 0; j < c.nkin; j++) {
        uint32_t off = L->attrs[j].offset, comps = c.kin[j].comps;
        if (comps == 1) {
            gf(&c, "    %s %s = vattr[abase + %u];\n", c.kin[j].tn, c.kin[j].pname, off);
        } else {
            gf(&c, "    %s %s = %s(", c.kin[j].tn, c.kin[j].pname, c.kin[j].tn);
            for (k = 0; k < (int)comps; k++)
                gf(&c, "%svattr[abase + %u]", k ? ", " : "", off + (uint32_t)k);
            gf(&c, ");\n");
        }
    }
    gf(&c, "    vec4 vkPosition = vec4(0.0);\n");
    for (j = 0; j < c.nkout; j++)
        gf(&c, "    %s %s = %s(0.0);\n", c.kout[j].tn, c.kout[j].pname, c.kout[j].tn);
    sep = "";
    gf(&c, "    vk_vs(");
    for (j = 0; j < c.nkin; j++) { gf(&c, "%s%s", sep, c.kin[j].pname); sep = ", "; }
    if (c.has_vertexid) { gf(&c, "%svkVid + uVertexBase", sep); sep = ", "; }
    gf(&c, "%svkPosition", sep); sep = ", ";
    for (j = 0; j < c.nkout; j++) { gf(&c, "%s%s", sep, c.kout[j].pname); sep = ", "; }
    gf(&c, ");\n");
    /* viewport transform + pack (Vulkan conventions, matching transform_vertex) */
    gf(&c, "    float w = vkPosition.w;\n");
    gf(&c, "    float iw = (w > 0.0) ? (1.0 / w) : 0.0;\n");
    gf(&c, "    float ndcx = vkPosition.x * iw;\n");
    gf(&c, "    float ndcy = vkPosition.y * iw;\n");
    gf(&c, "    float ndcz = vkPosition.z * iw;\n");
    gf(&c, "    float sx = uVpOffsetScale.x + uVpOffsetScale.z * (ndcx + 1.0);\n");
    gf(&c, "    float sy = uVpOffsetScale.y + uVpOffsetScale.w * (ndcy + 1.0);\n");
    gf(&c, "    float sz = uVpDepth.x + ndcz * uVpDepth.y;\n");
    gf(&c, "    int vbase = vkVid * %u;\n", L->vout_stride);
    gf(&c, "    vout[vbase + 0] = sx;\n");
    gf(&c, "    vout[vbase + 1] = sy;\n");
    gf(&c, "    vout[vbase + 2] = sz;\n");
    gf(&c, "    vout[vbase + 3] = iw;\n");
    gf(&c, "    vout[vbase + 4] = 0.0;\n");
    gf(&c, "    vout[vbase + 5] = 0.0;\n");
    gf(&c, "    vout[vbase + 6] = 0.0;\n");
    gf(&c, "    vout[vbase + 7] = 0.0;\n");
    for (j = 0; j < c.nkout; j++) {
        uint32_t off = L->varyings[j].offset, comps = c.kout[j].comps;
        for (k = 0; k < (int)comps; k++) {
            comp_sel(comps, (uint32_t)k, sel, sizeof sel);
            gf(&c, "    vout[vbase + %u] = %s%s;\n", off + (uint32_t)k, c.kout[j].pname, sel);
        }
    }
    gf(&c, "}\n");

done:
    if (c.declined) {
        if (err && errsz) snprintf(err, errsz, "%s", c.err[0] ? c.err : "declined");
        free(c.buf);
        out = NULL;
    } else {
        out = c.buf;
    }
    kern_free(&c);
    return out;
}

/* Find the vout float offset of the VS varying at `loc`, or -1. */
static int find_varying_off(const spirv_geom_layout *L, int loc, uint32_t *comps)
{
    int j;
    for (j = 0; j < L->num_varyings; j++)
        if ((int)L->varyings[j].location == loc) {
            *comps = L->varyings[j].comps;
            return (int)L->varyings[j].offset;
        }
    return -1;
}

/* Emit the per-vertex hoisted vout loads for one interpolated varying.  off is
 * the vout float offset, comps the component count, tag a unique local suffix. */
static void emit_vary_loads(gctx *c, uint32_t vstride, int off, uint32_t comps,
                            const char *tag)
{
    uint32_t k;
    for (k = 0; k < comps; k++) {
        gf(c, "    float h%s%u_0 = vout[i0 * %u + %d];\n", tag, k, vstride, off + (int)k);
        gf(c, "    float h%s%u_1 = vout[i1 * %u + %d];\n", tag, k, vstride, off + (int)k);
        gf(c, "    float h%s%u_2 = vout[i2 * %u + %d];\n", tag, k, vstride, off + (int)k);
    }
}

/* Emit the perspective-correct interpolation of one hoisted varying into the
 * local `dst` (type tn, comps components), inside the pixel loop. */
static void emit_vary_interp(gctx *c, const char *tn, uint32_t comps,
                             const char *tag, const char *dst, int ind)
{
    uint32_t k;
    char sel[8];
    indent(c, ind);
    if (comps == 1) {
        gf(c, "%s %s = (b0 * h%s0_0 * iw0 + b1 * h%s0_1 * iw1 + b2 * h%s0_2 * iw2) * rw;\n",
           tn, dst, tag, tag, tag);
        return;
    }
    gf(c, "%s %s = %s(", tn, dst, tn);
    for (k = 0; k < comps; k++) {
        gf(c, "%s(b0 * h%s%u_0 * iw0 + b1 * h%s%u_1 * iw1 + b2 * h%s%u_2 * iw2) * rw",
           k ? ", " : "", tag, k, tag, k, tag, k);
    }
    gf(c, ");\n");
    (void)sel;
}

/* ---- pass 2: per-triangle raster kernel --------------------------------- */
static char *gen_raster_kernel(spirv_module *m, const spirv_entry *e,
                               const spirv_geom_layout *L, spirv_geom_layout *Lout,
                               char *err, size_t errsz)
{
    gctx c;
    spirv_glsl_info scratch;
    uint32_t first_label, vstride = L->vout_stride;
    char *out = NULL, tag[16];
    int j, fs = (e != NULL), off;
    int *voff = NULL;      /* per-FS-input vout offset (-1 = zero-fill) */
    const char *sep;

    if (err && errsz) err[0] = '\0';
    memset(&scratch, 0, sizeof(scratch));

    if (fs) {
        if (kern_init(&c, m, e, GM_RASTER, &scratch) != 0) {
            if (err && errsz) snprintf(err, errsz, "fragment entry not found");
            return NULL;
        }
    } else {
        /* no fragment shader: colour = interpolated varying at location 0.  Use
         * a throwaway gctx with no module walk (nothing to emit for a body). */
        memset(&c, 0, sizeof(c));
        c.mode = GM_RASTER; c.info = &scratch;
        c.m = NULL;   /* nothing dereferences it: no body is emitted */
    }

    gf(&c, "#version 310 es\n");
    gf(&c, "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n\n");
    gf(&c, "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n\n");

    if (fs) {
        if (kern_scan_globals(&c) != 0) goto done;
        if (!c.has_color) { decl(&c, "fragment shader writes no vec4 colour output"); goto done; }
        /* resolve each FS input varying against the VS vout layout */
        voff = (int *)malloc((size_t)(c.nkin ? c.nkin : 1) * sizeof(int));
        if (!voff) { decl(&c, "out of memory"); goto done; }
        for (j = 0; j < c.nkin; j++) {
            uint32_t vc = 0;
            off = find_varying_off(L, c.kin[j].loc, &vc);
            if (off >= 0 && c.kin[j].comps > vc) {
                decl(&c, "fragment input wider than the vertex output");
                goto done;
            }
            voff[j] = off;
        }
        Lout->num_samplers = c.nsampler;
        for (j = 0; j < c.nsampler; j++) {
            Lout->sampler_set[j] = c.sampler_set[j];
            Lout->sampler_binding[j] = c.sampler_binding[j];
        }
        Lout->fs_present = 1;
    } else {
        /* the location-0 varying must exist as a vec4 to serve as the colour */
        uint32_t vc = 0;
        off = find_varying_off(L, 0, &vc);
        if (off < 0 || vc != 4) {
            decl(&c, "no fragment shader and location 0 is not a vec4 varying");
            goto done;
        }
        voff = (int *)malloc(sizeof(int));
        if (!voff) { decl(&c, "out of memory"); goto done; }
        voff[0] = off;
        Lout->fs_present = 0;
    }

    gf(&c, "layout(std430, binding = 0) buffer PglCsFb    { uint  fb[];    };\n");
    gf(&c, "layout(std430, binding = 1) buffer PglCsVout  { float vout[];  };\n");
    gf(&c, "layout(std430, binding = 2) buffer PglCsDepth { uint  depth[]; };\n\n");
    gf(&c, "uniform vec2  uViewportWH;\n");
    gf(&c, "uniform int   uFbStride;\n");
    gf(&c, "uniform int   uTriFirst;\n");
    gf(&c, "uniform int   uTriCount;\n");
    gf(&c, "uniform ivec4 uScissor;\n");
    gf(&c, "uniform int   uDepthFunc;\n");
    gf(&c, "uniform int   uDepthWrite;\n");
    gf(&c, "uniform int   uCullMode;\n");
    gf(&c, "uniform int   uFrontCW;\n");
    gf(&c, "uniform int   uBlendMode;\n");
    gf(&c, "uniform int   uFbSwizzle;\n\n");
    /* edge() in the cpu_raster.c convention: edge(a,b,p) = (b-a)x(p-a). */
    gf(&c, "float edgef(vec2 a, vec2 b, vec2 p)\n{\n");
    gf(&c, "    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);\n}\n\n");

    /* the user FS body as a function (colour + killed via out params) */
    if (fs) {
        sep = "";
        gf(&c, "void vk_fs(");
        for (j = 0; j < c.nkin; j++) { gf(&c, "%sin %s %s", sep, c.kin[j].tn, c.kin[j].pname); sep = ", "; }
        gf(&c, "%sin vec4 vkFragCoord", sep); sep = ", ";
        gf(&c, "%sin bool vkFrontFacing", sep); sep = ", ";
        gf(&c, "%sout vec4 vkColor", sep); sep = ", ";
        gf(&c, "%sout bool vkKilled", sep);
        gf(&c, ") {\n");
        gf(&c, "    vkColor = vec4(0.0);\n");
        gf(&c, "    vkKilled = false;\n");
        declare_phis(&c, 1);
        first_label = m->insns[c.f->first_block].w[1];
        emit_region(&c, first_label, 0, 1);
        if (c.declined) goto done;
        gf(&c, "}\n\n");
    }

    /* main(): one 8x8 workgroup per triangle, sweeping its bbox as a lane tile */
    gf(&c, "void main() {\n");
    gf(&c, "    int tri = int(gl_WorkGroupID.x) + uTriFirst;\n");
    gf(&c, "    if (tri >= uTriFirst + uTriCount) return;\n");
    gf(&c, "    int i0 = tri * 3;\n");
    gf(&c, "    int i1 = i0 + 1;\n");
    gf(&c, "    int i2 = i0 + 2;\n");
    gf(&c, "    float iw0 = vout[i0 * %u + 3], iw1 = vout[i1 * %u + 3], iw2 = vout[i2 * %u + 3];\n",
       vstride, vstride, vstride);
    gf(&c, "    if (iw0 <= 0.0 || iw1 <= 0.0 || iw2 <= 0.0) return;\n");
    gf(&c, "    vec2 s0 = vec2(vout[i0 * %u + 0], vout[i0 * %u + 1]);\n", vstride, vstride);
    gf(&c, "    vec2 s1 = vec2(vout[i1 * %u + 0], vout[i1 * %u + 1]);\n", vstride, vstride);
    gf(&c, "    vec2 s2 = vec2(vout[i2 * %u + 0], vout[i2 * %u + 1]);\n", vstride, vstride);
    gf(&c, "    float z0 = vout[i0 * %u + 2], z1 = vout[i1 * %u + 2], z2 = vout[i2 * %u + 2];\n",
       vstride, vstride, vstride);
    /* hoist every interpolated varying's three per-vertex component loads */
    if (fs) {
        for (j = 0; j < c.nkin; j++) {
            if (voff[j] < 0) continue;
            snprintf(tag, sizeof tag, "%d_", j);
            emit_vary_loads(&c, vstride, voff[j], c.kin[j].comps, tag);
        }
    } else {
        emit_vary_loads(&c, vstride, voff[0], 4, "0_");
    }
    gf(&c, "    float area = edgef(s0, s1, s2);\n");
    gf(&c, "    if (area == 0.0) return;\n");
    gf(&c, "    int front = (area < 0.0) ? 1 : 0;\n");
    gf(&c, "    if (uCullMode == 3) return;\n");
    gf(&c, "    if (uCullMode == 2) { if ((uFrontCW == 1) ? (front == 1) : (front == 0)) return; }\n");
    gf(&c, "    else if (uCullMode == 1) { if ((uFrontCW == 1) ? (front == 0) : (front == 1)) return; }\n");
    gf(&c, "    float inv_area = 1.0 / area;\n");
    gf(&c, "    vec2 W = uViewportWH;\n");
    gf(&c, "    int minx = int(floor(min(s0.x, min(s1.x, s2.x))));\n");
    gf(&c, "    int maxx = int(ceil (max(s0.x, max(s1.x, s2.x))));\n");
    gf(&c, "    int miny = int(floor(min(s0.y, min(s1.y, s2.y))));\n");
    gf(&c, "    int maxy = int(ceil (max(s0.y, max(s1.y, s2.y))));\n");
    gf(&c, "    if (minx < 0) minx = 0;\n");
    gf(&c, "    if (miny < 0) miny = 0;\n");
    gf(&c, "    if (maxx > int(W.x) - 1) maxx = int(W.x) - 1;\n");
    gf(&c, "    if (maxy > int(W.y) - 1) maxy = int(W.y) - 1;\n");
    gf(&c, "    if (uScissor.z > 0 && uScissor.w > 0) {\n");
    gf(&c, "        if (minx < uScissor.x) minx = uScissor.x;\n");
    gf(&c, "        if (miny < uScissor.y) miny = uScissor.y;\n");
    gf(&c, "        if (maxx > uScissor.x + uScissor.z - 1) maxx = uScissor.x + uScissor.z - 1;\n");
    gf(&c, "        if (maxy > uScissor.y + uScissor.w - 1) maxy = uScissor.y + uScissor.w - 1;\n");
    gf(&c, "    }\n");
    gf(&c, "    if (minx > maxx || miny > maxy) return;\n");
    gf(&c, "    int lx = int(gl_LocalInvocationID.x);\n");
    gf(&c, "    int ly = int(gl_LocalInvocationID.y);\n");
    gf(&c, "    for (int py = miny + ly; py <= maxy; py += 8) {\n");
    gf(&c, "    for (int px = minx + lx; px <= maxx; px += 8) {\n");
    gf(&c, "        vec2 P = vec2(float(px) + 0.5, float(py) + 0.5);\n");
    gf(&c, "        float w0 = edgef(s1, s2, P);\n");
    gf(&c, "        float w1 = edgef(s2, s0, P);\n");
    gf(&c, "        float w2 = edgef(s0, s1, P);\n");
    gf(&c, "        if (area > 0.0) { if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue; }\n");
    gf(&c, "        else            { if (w0 > 0.0 || w1 > 0.0 || w2 > 0.0) continue; }\n");
    gf(&c, "        float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;\n");
    gf(&c, "        float iw = b0 * iw0 + b1 * iw1 + b2 * iw2;\n");
    gf(&c, "        if (iw <= 0.0) continue;\n");
    gf(&c, "        float rw = 1.0 / iw;\n");
    gf(&c, "        float z = b0 * z0 + b1 * z1 + b2 * z2;\n");   /* window-linear, matches cpu */
    gf(&c, "        int idx = py * uFbStride + px;\n");
    if (fs) {
        for (j = 0; j < c.nkin; j++) {
            if (voff[j] < 0) {
                gf(&c, "        %s %s = %s(0.0);\n", c.kin[j].tn, c.kin[j].pname, c.kin[j].tn);
            } else {
                snprintf(tag, sizeof tag, "%d_", j);
                emit_vary_interp(&c, c.kin[j].tn, c.kin[j].comps, tag, c.kin[j].pname, 2);
            }
        }
        gf(&c, "        vec4 vkFragCoord = vec4(float(px) + 0.5, float(py) + 0.5, z, iw);\n");
        gf(&c, "        bool vkFrontFacing = (front != 0);\n");
        gf(&c, "        vec4 vkColor;\n");
        gf(&c, "        bool vkKilled;\n");
        sep = "";
        gf(&c, "        vk_fs(");
        for (j = 0; j < c.nkin; j++) { gf(&c, "%s%s", sep, c.kin[j].pname); sep = ", "; }
        gf(&c, "%svkFragCoord, vkFrontFacing, vkColor, vkKilled);\n", sep);
        gf(&c, "        if (vkKilled) continue;\n");
    } else {
        emit_vary_interp(&c, "vec4", 4, "0_", "vkColor", 2);
    }
    /* depth test (24-bit unorm, atomicMin => LESS; matches cpu write_fragment) */
    gf(&c, "        if (uDepthFunc > 0) {\n");
    gf(&c, "            uint d = uint(clamp(z, 0.0, 1.0) * 16777215.0);\n");
    gf(&c, "            if (uDepthWrite != 0) {\n");
    gf(&c, "                if (d >= depth[idx]) continue;\n");
    gf(&c, "                uint old = atomicMin(depth[idx], d);\n");
    gf(&c, "                if (d >= old) continue;\n");
    gf(&c, "            } else {\n");
    gf(&c, "                if (d >= depth[idx]) continue;\n");
    gf(&c, "            }\n");
    gf(&c, "        }\n");
    /* pack the colour into the fb word (RGBA8 or BGRA8 memory order) */
    gf(&c, "        vec4 cc = clamp(vkColor, 0.0, 1.0);\n");
    gf(&c, "        uint r8 = uint(cc.r * 255.0 + 0.5);\n");
    gf(&c, "        uint g8 = uint(cc.g * 255.0 + 0.5);\n");
    gf(&c, "        uint b8 = uint(cc.b * 255.0 + 0.5);\n");
    gf(&c, "        uint a8 = uint(cc.a * 255.0 + 0.5);\n");
    gf(&c, "        if (uFbSwizzle == 0) fb[idx] = (a8 << 24) | (b8 << 16) | (g8 << 8) | r8;\n");
    gf(&c, "        else                 fb[idx] = (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;\n");
    gf(&c, "    }\n");
    gf(&c, "    }\n");
    gf(&c, "}\n");

done:
    free(voff);
    if (c.declined) {
        if (err && errsz) snprintf(err, errsz, "%s", c.err[0] ? c.err : "declined");
        free(c.buf);
        out = NULL;
    } else {
        out = c.buf;
    }
    kern_free(&c);   /* name/is_ptr are NULL on the no-FS path: a safe no-op */
    return out;
}

int spirv_to_geom_kernels(spirv_module *vmod, const spirv_entry *vs,
                          spirv_module *fmod, const spirv_entry *fs,
                          spirv_geom_kernels *out, char *err, size_t errsz)
{
    char ebuf[192];

    if (err && errsz) err[0] = '\0';
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!vmod || !vs) {
        if (err && errsz) snprintf(err, errsz, "missing vertex entry");
        return -1;
    }

    out->xform = gen_xform_kernel(vmod, vs, &out->layout, ebuf, sizeof ebuf);
    if (!out->xform) {
        if (err && errsz) snprintf(err, errsz, "xform: %s", ebuf);
        return -1;
    }
    out->raster = gen_raster_kernel(fmod, fs, &out->layout, &out->layout,
                                    ebuf, sizeof ebuf);
    if (!out->raster) {
        if (err && errsz) snprintf(err, errsz, "raster: %s", ebuf);
        free(out->xform);
        out->xform = NULL;
        return -1;
    }
    return 0;
}
