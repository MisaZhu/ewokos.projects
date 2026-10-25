/*
 * spirv_parse.c - decode a SPIR-V word stream into the spirv_module tables the
 * interpreter walks (see spirv_priv.h).  The parser is intentionally a single
 * forward pass over the instruction stream: it validates the header, records
 * every instruction, and dispatches per-opcode into the type / constant /
 * decoration / variable / entry-point / function tables, plus an id->definition
 * map.  An unsupported-but-harmless opcode is simply recorded (the interpreter
 * decides what it can execute); a malformed stream is rejected here.
 */
#include "spirv_priv.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SPIRV_MAGIC 0x07230203u

#define APPEND(arr, n, cap, T) do { \
        if ((n) == (cap)) { \
            (cap) = (cap) ? (cap) * 2 : 16; \
            (arr) = (T *)realloc((arr), (cap) * sizeof(T)); \
            if (!(arr)) return NULL; \
        } \
    } while (0)

static void set_err(char *err, size_t errsz, const char *msg)
{
    if (err && errsz)
        snprintf(err, errsz, "%s", msg);
}

spirv_type *spirv_type_find(spirv_module *m, uint32_t id)
{
    uint32_t i;
    if (!m || !id) return NULL;
    for (i = 0; i < m->ntypes; i++)
        if (m->types[i].id == id) return &m->types[i];
    return NULL;
}

spirv_const *spirv_const_find(spirv_module *m, uint32_t id)
{
    uint32_t i;
    if (!m || !id) return NULL;
    for (i = 0; i < m->nconsts; i++)
        if (m->consts[i].id == id) return &m->consts[i];
    return NULL;
}

spirv_deco *spirv_deco_find(spirv_module *m, uint32_t target, uint32_t member,
                            uint32_t deco)
{
    uint32_t i;
    if (!m) return NULL;
    for (i = 0; i < m->ndecos; i++)
        if (m->decos[i].target == target && m->decos[i].deco == deco &&
            m->decos[i].member == member)
            return &m->decos[i];
    return NULL;
}

int spirv_deco_int(spirv_module *m, uint32_t target, uint32_t member,
                   uint32_t deco, int dflt)
{
    spirv_deco *d = spirv_deco_find(m, target, member, deco);
    return (d && d->nargs >= 1) ? (int)d->args[0] : dflt;
}

spirv_func *spirv_func_find(spirv_module *m, uint32_t id)
{
    uint32_t i;
    if (!m || !id) return NULL;
    for (i = 0; i < m->nfuncs; i++)
        if (m->funcs[i].id == id) return &m->funcs[i];
    return NULL;
}

/* Byte size of a type; *stride (optional) receives the natural element stride
 * used for array/matrix indexing.  Returns 0 for runtime arrays / images /
 * samplers / void (sizes the interpreter never needs). */
uint32_t spirv_type_size(spirv_module *m, uint32_t type_id, uint32_t *stride)
{
    spirv_type *t = spirv_type_find(m, type_id);
    if (stride) *stride = 0;
    if (!t) return 0;
    switch (t->op) {
    case OpTypeBool:
        return 4;
    case OpTypeInt:
    case OpTypeFloat:
        return t->width / 8u;
    case OpTypeVector: {
        uint32_t es = spirv_type_size(m, t->elem, NULL);
        return es * t->comps;
    }
    case OpTypeMatrix: {
        uint32_t cs = spirv_type_size(m, t->elem, NULL);
        uint32_t ms = spirv_deco_int(m, type_id, 0xffffffff, DecoMatrixStride, 0);
        if (stride) *stride = ms ? ms : cs;
        return (ms ? ms : cs) * t->comps;
    }
    case OpTypeArray: {
        uint32_t es = spirv_type_size(m, t->elem, NULL);
        uint32_t as = spirv_deco_int(m, type_id, 0xffffffff, DecoArrayStride, 0);
        if (!as) as = es;
        if (stride) *stride = as;
        return as * t->count;
    }
    case OpTypeStruct: {
        uint32_t i, end = 0;
        for (i = 0; i < t->nmembers; i++) {
            /* member type ids are not stored in spirv_type; fall back to the
             * Offset decoration of the following member, or accumulate below. */
            (void)i;
            break;
        }
        /* struct size is computed by the interpreter from member offsets when
         * needed; return 0 here (structs are handled by member decorations). */
        (void)end;
        return 0;
    }
    default:
        return 0;
    }
}

void spirv_free(spirv_module *m)
{
    if (!m) return;
    free(m->insns);
    free(m->types);
    free(m->consts);
    free(m->decos);
    free(m->vars);
    free(m->eps);
    free(m->funcs);
    free(m->def_at);
    free(m->label_at);
    free(m->scratch);
    free(m->scratch_valid);
    free(m->arena);
    free(m);
}

/* ---- the parse ------------------------------------------------------------ */
spirv_module *spirv_parse(const uint32_t *words, size_t nwords,
                          char *err, size_t errsz)
{
    spirv_module *m;
    size_t pos;
    uint32_t ninsns, i;
    uint32_t cap_t = 0, cap_c = 0, cap_d = 0, cap_v = 0, cap_e = 0, cap_f = 0;

    if (err && errsz) err[0] = '\0';
    if (!words || nwords < 5) { set_err(err, errsz, "module too small"); return NULL; }
    if (words[0] != SPIRV_MAGIC) { set_err(err, errsz, "bad SPIR-V magic"); return NULL; }

    m = (spirv_module *)calloc(1, sizeof(*m));
    if (!m) { set_err(err, errsz, "oom"); return NULL; }
    m->words = words;
    m->nwords = nwords;
    m->version = words[1];
    m->bound = words[3];
    if (m->bound == 0 || m->bound > (1u << 22)) {
        set_err(err, errsz, "bad id bound"); spirv_free(m); return NULL;
    }

    m->def_at = (int32_t *)malloc(sizeof(int32_t) * m->bound);
    if (!m->def_at) { set_err(err, errsz, "oom def_at"); spirv_free(m); return NULL; }
    for (i = 0; i < m->bound; i++) m->def_at[i] = -1;

    /* count instructions */
    ninsns = 0;
    pos = 5;
    while (pos < nwords) {
        uint32_t w0 = words[pos];
        uint32_t wc = w0 >> 16;
        if (wc == 0 || pos + wc > nwords) {
            set_err(err, errsz, "bad instruction word count");
            spirv_free(m); return NULL;
        }
        ninsns++;
        pos += wc;
    }
    m->insns = (spirv_insn *)calloc(ninsns ? ninsns : 1, sizeof(spirv_insn));
    if (!m->insns) { set_err(err, errsz, "oom insns"); spirv_free(m); return NULL; }

    /* single record pass */
    m->ninsns = 0;
    pos = 5;
    {
        uint32_t idx = 0;
        spirv_func *curfn = NULL;
        while (pos < nwords) {
            const uint32_t *w = words + pos;
            uint32_t w0 = w[0];
            uint32_t op = w0 & 0xffff;
            uint32_t wc = w0 >> 16;
            spirv_insn *in = &m->insns[idx];
            in->op = op; in->wc = wc; in->w = w; in->index = idx;
            idx++;
            pos += wc;

            /* record the definition site for anything with a result id */
            switch (op) {
            case OpLabel:
                if (wc >= 2 && w[1] < m->bound) m->def_at[w[1]] = (int32_t)(idx - 1);
                break;
            case OpTypeVoid: case OpTypeBool: case OpTypeInt: case OpTypeFloat:
            case OpTypeVector: case OpTypeMatrix: case OpTypeImage:
            case OpTypeSampler: case OpTypeSampledImage: case OpTypeArray:
            case OpTypeRuntimeArray: case OpTypeStruct: case OpTypeOpaque:
            case OpTypePointer: case OpTypeFunction:
                if (wc >= 2 && w[1] < m->bound) m->def_at[w[1]] = (int32_t)(idx - 1);
                break;
            case OpExtInstImport:
                if (wc >= 2 && w[1] < m->bound) m->def_at[w[1]] = (int32_t)(idx - 1);
                break;
            /* Ops with NO result id: word2 is an operand (a store's value, a
             * decoration number, a branch target), never a definition.  Recording
             * def_at[w[2]] for these would clobber the real definition of that id
             * -- e.g. OpStore %var %const0 overwrites the constant's def_at with
             * the store, so a later idname(const0) can no longer resolve it to its
             * literal and emits an undeclared "v<id>".  Skip them. */
            case OpStore: case OpCopyMemory: case OpImageWrite:
            case OpAtomicStore: case OpMemoryBarrier: case OpControlBarrier:
            case OpDecorate: case OpMemberDecorate:
            case OpGroupDecorate: case OpGroupMemberDecorate:
            case OpFunctionEnd: case OpLoopMerge: case OpSelectionMerge:
            case OpBranch: case OpBranchConditional: case OpSwitch:
            case OpKill: case OpReturn: case OpReturnValue:
            case OpUnreachable: case OpTerminateInvocation:
                break;
            default:
                /* value/variable/constant/function ops: word2 is the result id */
                if (wc >= 3 && w[2] < m->bound) m->def_at[w[2]] = (int32_t)(idx - 1);
                break;
            }

            switch (op) {
            case OpExtInstImport: {
                const char *nm = (const char *)(w + 2);
                if (!strcmp(nm, "GLSL.std.450")) m->glsl450_set = w[1];
                break;
            }
            case OpEntryPoint: {
                spirv_ep *e;
                const char *s;
                APPEND(m->eps, m->neps, cap_e, spirv_ep);
                e = &m->eps[m->neps++];
                memset(e, 0, sizeof(*e));
                e->id = w[2];
                e->exec_model = w[1];
                /* word3 begins the null-terminated entry name (padded to a word
                 * boundary); the interface id list that follows is not needed
                 * by the interpreter, which reads interface via decorations. */
                s = (const char *)(w + 3);
                strncpy(e->name, s, sizeof(e->name) - 1);
                e->name[sizeof(e->name) - 1] = '\0';
                break;
            }
            case OpExecutionMode:
            case OpExecutionModeId: {
                uint32_t epid = w[1], mode = w[2];
                uint32_t k;
                for (k = 0; k < m->neps; k++) {
                    if (m->eps[k].id != epid) continue;
                    if (mode == ExecLocalSize && wc >= 6) {
                        m->eps[k].local_size[0] = w[3];
                        m->eps[k].local_size[1] = w[4];
                        m->eps[k].local_size[2] = w[5];
                    } else if (mode == ExecLocalSizeId && wc >= 6) {
                        int d;
                        for (d = 0; d < 3; d++) {
                            spirv_const *c = spirv_const_find(m, w[3 + d]);
                            m->eps[k].local_size[d] = c ? c->u[0] : 1;
                        }
                    }
                }
                break;
            }
            case OpName: case OpMemberName: case OpString:
            case OpSource: case OpSourceContinued: case OpSourceExtension:
            case OpLine: case OpCapability: case OpExtension:
            case OpMemoryModel: case OpNop:
            case OpDecorationGroup: case OpGroupDecorate: case OpGroupMemberDecorate:
                break;
            case OpTypeInt: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op; t->width = w[2]; t->is_signed = w[3];
                break;
            }
            case OpTypeFloat: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op; t->width = w[2];
                break;
            }
            case OpTypeVector: case OpTypeMatrix: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op; t->elem = w[2]; t->comps = w[3];
                break;
            }
            case OpTypeArray: {
                spirv_const *len;
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                len = spirv_const_find(m, w[3]);
                t->id = w[1]; t->op = op; t->elem = w[2];
                t->count = len ? len->u[0] : 0;
                break;
            }
            case OpTypeRuntimeArray: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op; t->elem = w[2]; t->count = 0;
                break;
            }
            case OpTypeStruct: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op; t->nmembers = wc - 2;
                break;
            }
            case OpTypePointer: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op; t->storage = w[2]; t->elem = w[3];
                break;
            }
            case OpTypeVoid: case OpTypeBool: case OpTypeSampler:
            case OpTypeImage: case OpTypeSampledImage: case OpTypeFunction:
            case OpTypeOpaque: {
                spirv_type *t;
                APPEND(m->types, m->ntypes, cap_t, spirv_type);
                t = &m->types[m->ntypes++];
                memset(t, 0, sizeof(*t));
                t->id = w[1]; t->op = op;
                break;
            }
            case OpConstant: case OpSpecConstant: {
                spirv_const *c;
                uint32_t k;
                APPEND(m->consts, m->nconsts, cap_c, spirv_const);
                c = &m->consts[m->nconsts++];
                memset(c, 0, sizeof(*c));
                c->id = w[2]; c->type = w[1];
                for (k = 3; k < wc && (k - 3) < 4; k++) c->u[k - 3] = w[k];
                c->n = wc - 3; if (c->n == 0) c->n = 1;
                break;
            }
            case OpConstantTrue: case OpConstantFalse: {
                spirv_const *c;
                APPEND(m->consts, m->nconsts, cap_c, spirv_const);
                c = &m->consts[m->nconsts++];
                memset(c, 0, sizeof(*c));
                c->id = w[2]; c->type = w[1]; c->n = 1;
                c->u[0] = (op == OpConstantTrue) ? 1u : 0u;
                break;
            }
            case OpConstantNull: {
                spirv_const *c;
                APPEND(m->consts, m->nconsts, cap_c, spirv_const);
                c = &m->consts[m->nconsts++];
                memset(c, 0, sizeof(*c));
                c->id = w[2]; c->type = w[1]; c->n = 1; c->is_null = 1;
                break;
            }
            case OpVariable: {
                spirv_var *v;
                APPEND(m->vars, m->nvars, cap_v, spirv_var);
                v = &m->vars[m->nvars++];
                memset(v, 0, sizeof(*v));
                v->id = w[2]; v->type = w[1]; v->storage = (wc >= 4) ? w[3] : 0;
                v->init = (wc >= 5) ? w[4] : 0;
                if (v->storage == SC_Workgroup) m->uses_workgroup = 1;
                break;
            }
            case OpDecorate: {
                spirv_deco *d;
                uint32_t k;
                APPEND(m->decos, m->ndecos, cap_d, spirv_deco);
                d = &m->decos[m->ndecos++];
                memset(d, 0, sizeof(*d));
                d->target = w[1]; d->member = 0xffffffffu; d->deco = w[2];
                for (k = 3; k < wc && (k - 3) < 4; k++) d->args[k - 3] = w[k];
                d->nargs = wc - 3;
                break;
            }
            case OpMemberDecorate: {
                spirv_deco *d;
                uint32_t k;
                APPEND(m->decos, m->ndecos, cap_d, spirv_deco);
                d = &m->decos[m->ndecos++];
                memset(d, 0, sizeof(*d));
                d->target = w[1]; d->member = w[2]; d->deco = w[3];
                for (k = 4; k < wc && (k - 4) < 4; k++) d->args[k - 4] = w[k];
                d->nargs = wc - 4;
                break;
            }
            case OpFunction: {
                spirv_func *f;
                APPEND(m->funcs, m->nfuncs, cap_f, spirv_func);
                f = &m->funcs[m->nfuncs++];
                memset(f, 0, sizeof(*f));
                f->id = w[2]; f->ret_type = w[1];
                f->start = idx - 1; f->first_block = 0xffffffffu;
                curfn = f;
                break;
            }
            case OpLabel:
                if (curfn && curfn->first_block == 0xffffffffu)
                    curfn->first_block = idx - 1;
                break;
            case OpFunctionEnd:
                if (curfn) { curfn->end = idx - 1; curfn = NULL; }
                break;
            case OpControlBarrier: case OpMemoryBarrier:
                m->uses_workgroup = 1;
                break;
            default:
                break;
            }
        }
        m->ninsns = idx;
    }

    if (m->neps == 0) { set_err(err, errsz, "no entry points"); spirv_free(m); return NULL; }

    /* build the label -> OpLabel insn map for the CFG walker */
    m->label_at = (int32_t *)malloc(sizeof(int32_t) * m->bound);
    if (!m->label_at) { set_err(err, errsz, "oom label_at"); spirv_free(m); return NULL; }
    for (i = 0; i < m->bound; i++) m->label_at[i] = -1;
    for (i = 0; i < m->ninsns; i++) {
        if (m->insns[i].op == OpLabel && m->insns[i].wc >= 2) {
            uint32_t lid = m->insns[i].w[1];
            if (lid < m->bound) m->label_at[lid] = (int32_t)i;
        }
    }

    /* resolve entry-point function first-block indices */
    for (i = 0; i < m->neps; i++) {
        spirv_func *f = spirv_func_find(m, m->eps[i].id);
        (void)f;
    }
    return m;
}

/* ---- introspection -------------------------------------------------------- */
int spirv_num_entries(const spirv_module *m)
{
    return m ? (int)m->neps : 0;
}

const spirv_entry *spirv_get_entry(const spirv_module *m, int i)
{
    if (!m || i < 0 || (uint32_t)i >= m->neps) return NULL;
    return (const spirv_entry *)&m->eps[i];
}

const spirv_entry *spirv_find_entry(const spirv_module *m, const char *name,
                                    uint32_t exec_model)
{
    uint32_t i;
    if (!m) return NULL;
    for (i = 0; i < m->neps; i++) {
        if (exec_model != 0xffffffffu && m->eps[i].exec_model != exec_model)
            continue;
        if (name && name[0] && strcmp(m->eps[i].name, name) != 0)
            continue;
        return (const spirv_entry *)&m->eps[i];
    }
    /* fall back to the first entry of the requested model */
    if (name && name[0]) {
        for (i = 0; i < m->neps; i++)
            if (exec_model == 0xffffffffu || m->eps[i].exec_model == exec_model)
                return (const spirv_entry *)&m->eps[i];
    }
    return NULL;
}

/* Gather the descriptor resources referenced by the module.  We scan every
 * OpVariable in a descriptor storage class and read its DescriptorSet/Binding
 * decorations, classifying by the pointee type's Block/BufferBlock decoration. */
int spirv_entry_bindings(const spirv_module *m, const spirv_entry *e,
                         spirv_binding_desc *out, int max)
{
    uint32_t i;
    int n = 0;
    (void)e;
    if (!m || !out) return 0;
    for (i = 0; i < m->nvars && n < max; i++) {
        const spirv_var *v = &m->vars[i];
        spirv_type *pt = spirv_type_find((spirv_module *)m, v->type);
        spirv_binding_desc d;
        int set, binding;
        if (!pt) continue;
        if (v->storage != SC_Uniform && v->storage != SC_StorageBuffer &&
            v->storage != SC_UniformConstant && v->storage != SC_PushConstant)
            continue;
        set = spirv_deco_int((spirv_module *)m, v->id, 0xffffffff, DecoDescriptorSet, 0);
        binding = spirv_deco_int((spirv_module *)m, v->id, 0xffffffff, DecoBinding, -1);
        d.set = (uint32_t)set;
        d.binding = (binding < 0) ? 0 : (uint32_t)binding;
        if (v->storage == SC_PushConstant) {
            d.kind = SPIRV_RES_PUSH_CONSTANT;
        } else if (v->storage == SC_UniformConstant) {
            spirv_type *et = spirv_type_find((spirv_module *)m, pt->elem);
            if (et && et->op == OpTypeSampledImage) d.kind = SPIRV_RES_COMBINED_IMAGE_SAMPLER;
            else if (et && et->op == OpTypeSampler) d.kind = SPIRV_RES_SAMPLER;
            else if (et && et->op == OpTypeImage) d.kind = SPIRV_RES_STORAGE_IMAGE;
            else d.kind = SPIRV_RES_COMBINED_IMAGE_SAMPLER;
        } else if (v->storage == SC_Uniform) {
            /* Uniform can be a UBO (Block on the pointee struct) or a legacy
             * BufferBlock (SSBO); distinguish via the pointee's decoration. */
            int isbuf = spirv_deco_find((spirv_module *)m, pt->elem, 0xffffffff, DecoBufferBlock) != NULL;
            d.kind = isbuf ? SPIRV_RES_STORAGE_BUFFER : SPIRV_RES_UNIFORM_BUFFER;
        } else {
            d.kind = SPIRV_RES_STORAGE_BUFFER;
        }
        out[n++] = d;
    }
    return n;
}
