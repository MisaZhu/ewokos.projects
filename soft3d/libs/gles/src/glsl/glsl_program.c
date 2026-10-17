/* glsl_program.c - link + reflection for the CPU GLSL ES 1.00 interpreter.
 *
 * Each stage is parsed independently, so attribute/varying/uniform slots are
 * per-stage.  Linking builds the unified reflection tables on the program
 * (attributes by location, varyings by vec4 slot, uniforms by cell offset into
 * a shared uniform block) and then rewrites every EX_VAR in both stages' ASTs
 * from the per-stage slot to the unified index (matched by var_name).  It also
 * resolves forward-declared user-function calls, lays out and initialises the
 * file-scope global stores, and allocates the scratch/stack execution arenas.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "glsl.h"
#include "glsl_util.h"

/* ------------------------------------------------------------------ */
typedef struct {
    glsl_program *p;
    char err[GLSL_ERR_LEN];
    int  errored;
} linker;

static void lerr(linker *L, const char *fmt, ...)
{
    va_list ap;
    if (L->errored) return;
    L->errored = 1;
    va_start(ap, fmt);
    vsnprintf(L->err, sizeof L->err, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* reflection lookup helpers (linear scan; N is small) */
static int find_attrib(linker *L, const char *name)
{
    int i;
    for (i = 0; i < L->p->nattribs; ++i)
        if (!strcmp(L->p->attribs[i].name, name)) return i;
    return -1;
}
static int find_varying(linker *L, const char *name)
{
    int i;
    for (i = 0; i < L->p->nvaryings; ++i)
        if (!strcmp(L->p->varyings[i].name, name)) return i;
    return -1;
}
static int find_uniform(linker *L, const char *name)
{
    int i;
    for (i = 0; i < L->p->nuniforms; ++i)
        if (!strcmp(L->p->uniforms[i].name, name)) return i;
    return -1;
}
static int find_func(const glsl_stage *st, const char *name)
{
    int i;
    for (i = 0; i < st->nfuncs; ++i)
        if (!strcmp(st->funcs[i].name, name)) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* AST rewrite walk */
static void walk_expr(linker *L, glsl_stage *st, glsl_expr *e);
static void walk_stmt(linker *L, glsl_stage *st, glsl_stmt *s);

static void walk_expr(linker *L, glsl_stage *st, glsl_expr *e)
{
    int i;
    if (!e) return;
    if (e->kind == EX_VAR) {
        switch (e->var_scope) {
        case VARSCOPE_ATTRIB: {
            int a = find_attrib(L, e->var_name);
            if (a < 0) { lerr(L, "unresolved attribute '%s'", e->var_name); return; }
            e->var_slot = L->p->attribs[a].loc;
            break;
        }
        case VARSCOPE_VARYING: {
            int v = find_varying(L, e->var_name);
            if (v < 0) { lerr(L, "unresolved varying '%s'", e->var_name); return; }
            e->var_slot = L->p->varyings[v].slot;
            break;
        }
        case VARSCOPE_UNIFORM: {
            int u = find_uniform(L, e->var_name);
            if (u < 0) { lerr(L, "unresolved uniform '%s'", e->var_name); return; }
            e->var_slot = u;
            break;
        }
        case VARSCOPE_BUILTIN:
            if (e->var_slot == BI_GL_FRAGDEPTH && !st->is_vertex)
                st->uses_fragdepth = 1;
            break;
        default: break; /* GLOBAL/LOCAL keep their per-stage slot */
        }
    } else if (e->kind == EX_CALL && e->is_user_fn && e->op_builtin < 0) {
        int f = find_func(st, e->var_name);
        if (f < 0) { lerr(L, "call to undefined function '%s'", e->var_name); return; }
        e->op_builtin = f;
    }
    walk_expr(L, st, e->a);
    walk_expr(L, st, e->b);
    walk_expr(L, st, e->cc);
    for (i = 0; i < e->nargs; ++i) walk_expr(L, st, e->args[i]);
}

static void walk_stmt(linker *L, glsl_stage *st, glsl_stmt *s)
{
    if (!s) return;
    walk_expr(L, st, s->expr);
    walk_expr(L, st, s->cond2);
    walk_expr(L, st, s->post);
    walk_expr(L, st, s->decl_init);
    walk_stmt(L, st, s->body);
    walk_stmt(L, st, s->els);
    walk_stmt(L, st, s->children);
    walk_stmt(L, st, s->next);
}

static void walk_stage(linker *L, glsl_stage *st)
{
    int i;
    for (i = 0; i < st->nfuncs; ++i) walk_stmt(L, st, st->funcs[i].body);
    for (i = 0; i < st->nglobals; ++i) walk_expr(L, st, st->globals[i].const_init);
}

/* ------------------------------------------------------------------ */
/* table construction */
static void add_attrib(linker *L, const char *name, int type, int loc)
{
    glsl_program *p = L->p;
    int size = glsl_base_comps(type);
    if (p->nattribs >= GLSL_MAX_ATTRIBS) { lerr(L, "too many attributes"); return; }
    if (size < 1) size = 1; if (size > 4) size = 4;
    strncpy(p->attribs[p->nattribs].name, name, GLSL_NAME_LEN-1);
    p->attribs[p->nattribs].loc = loc;
    p->attribs[p->nattribs].size = size;
    p->nattribs++;
    if (loc + 1 > p->nattr_components) p->nattr_components = loc + 1;
}

static void add_varying(linker *L, const char *name, int type)
{
    glsl_program *p = L->p;
    int ncomp;
    if (find_varying(L, name) >= 0) return;
    if (p->nvaryings >= GLSL_MAX_VARYINGS) { lerr(L, "too many varyings"); return; }
    ncomp = glsl_base_comps(type);
    if (ncomp < 1) ncomp = 1; if (ncomp > 4) ncomp = 4;
    strncpy(p->varyings[p->nvaryings].name, name, GLSL_NAME_LEN-1);
    p->varyings[p->nvaryings].slot = p->nvaryings;
    p->varyings[p->nvaryings].ncomp = ncomp;
    p->varyings[p->nvaryings].interp = 0; /* smooth */
    p->nvaryings++;
}

static void add_uniform(linker *L, const glsl_stage *st, const char *name,
                        int type, int st_id, int array_len, int *cursor)
{
    glsl_program *p = L->p;
    glsl_uniform *U;
    int elem, arr, total, sampler;
    if (find_uniform(L, name) >= 0) return;
    if (p->nuniforms >= GLSL_MAX_UNIFORMS) { lerr(L, "too many uniforms"); return; }
    elem = glsl_type_cells(st, type, st_id);
    if (elem < 1) elem = 1;
    arr = array_len > 0 ? array_len : 1;
    sampler = (type == T_SAMPLER2D || type == T_SAMPLERCUBE);
    if (sampler) elem = 1;
    total = elem * arr;
    U = &p->uniforms[p->nuniforms++];
    strncpy(U->name, name, GLSL_NAME_LEN-1);
    U->type = type; U->st_id = st_id;
    U->offset = *cursor; U->ncomp = elem;
    U->is_sampler = sampler; U->array_len = array_len; U->elem_stride = elem;
    *cursor += total;
}

static int layout_globals(glsl_stage *st)
{
    int off = 0, i;
    for (i = 0; i < st->nglobals; ++i) {
        if (st->globals[i].scope != VARSCOPE_GLOBAL) continue;
        st->globals[i].goffset = off;
        st->globals[i].gcells = glsl_var_cells(st, st->globals[i].type,
                                               st->globals[i].st_id,
                                               st->globals[i].array_len);
        if (st->globals[i].gcells < 1) st->globals[i].gcells = 1;
        off += st->globals[i].gcells;
    }
    return off;
}

/* ------------------------------------------------------------------ */
static void fail(glsl_program *p, glsl_stage *vs, glsl_stage *fs,
                 const char *msg, char *err, int errsz)
{
    if (err && errsz > 0) snprintf(err, errsz, "%s", msg ? msg : "link failed");
    if (p) {
        free(p->vs_gstore); free(p->fs_gstore);
        free(p->vs_ginit);  free(p->fs_ginit);
        free(p->scratch);   free(p->stack);
        free(p);
    }
    glsl_free_stage(vs);
    glsl_free_stage(fs);
}

glsl_program *glsl_link(glsl_stage *vs, glsl_stage *fs, char *err, int errsz)
{
    glsl_program *p;
    linker L;
    int i, cursor = 0, vs_cells, fs_cells, vs_frames = 0, fs_frames = 0;

    memset(&L, 0, sizeof L);
    p = (glsl_program *)calloc(1, sizeof(glsl_program));
    if (!p) { if (err) snprintf(err, errsz, "out of memory"); glsl_free_stage(vs); glsl_free_stage(fs); return NULL; }
    L.p = p;
    p->vs = vs; p->fs = fs;

    if (!vs || !fs) { fail(p, vs, fs, "link: missing shader stage", err, errsz); return NULL; }
    if (vs->errored) { fail(p, vs, fs, vs->err, err, errsz); return NULL; }
    if (fs->errored) { fail(p, vs, fs, fs->err, err, errsz); return NULL; }
    if (vs->main_idx < 0 || fs->main_idx < 0) { fail(p, vs, fs, "link: missing main()", err, errsz); return NULL; }

    /* attributes (vertex stage only) */
    for (i = 0; i < vs->nglobals; ++i)
        if (vs->globals[i].scope == VARSCOPE_ATTRIB)
            add_attrib(&L, vs->globals[i].name, vs->globals[i].type, vs->globals[i].slot);

    /* varyings: union of both stages, vs first (stable slot order) */
    for (i = 0; i < vs->nglobals; ++i)
        if (vs->globals[i].scope == VARSCOPE_VARYING)
            add_varying(&L, vs->globals[i].name, vs->globals[i].type);
    for (i = 0; i < fs->nglobals; ++i)
        if (fs->globals[i].scope == VARSCOPE_VARYING)
            add_varying(&L, fs->globals[i].name, fs->globals[i].type);

    /* uniforms: union of both stages into one cell block */
    for (i = 0; i < vs->nglobals; ++i)
        if (vs->globals[i].scope == VARSCOPE_UNIFORM)
            add_uniform(&L, vs, vs->globals[i].name, vs->globals[i].type,
                        vs->globals[i].st_id, vs->globals[i].array_len, &cursor);
    for (i = 0; i < fs->nglobals; ++i)
        if (fs->globals[i].scope == VARSCOPE_UNIFORM)
            add_uniform(&L, fs, fs->globals[i].name, fs->globals[i].type,
                        fs->globals[i].st_id, fs->globals[i].array_len, &cursor);
    p->uniform_block_floats = cursor;

    if (L.errored) { fail(p, vs, fs, L.err, err, errsz); return NULL; }

    /* global variable stores */
    vs_cells = layout_globals(vs);
    fs_cells = layout_globals(fs);
    p->vs_gcells = vs_cells; p->fs_gcells = fs_cells;
    if (vs_cells > 0) p->vs_gstore = (glsl_cell *)calloc(vs_cells, sizeof(glsl_cell));
    if (fs_cells > 0) p->fs_gstore = (glsl_cell *)calloc(fs_cells, sizeof(glsl_cell));
    if ((vs_cells > 0 && !p->vs_gstore) || (fs_cells > 0 && !p->fs_gstore)) {
        fail(p, vs, fs, "link: out of memory (globals)", err, errsz); return NULL;
    }

    /* rewrite AST slots to unified indices + resolve forward calls */
    walk_stage(&L, vs);
    walk_stage(&L, fs);
    if (L.errored) { fail(p, vs, fs, L.err, err, errsz); return NULL; }

    /* execution arenas */
    for (i = 0; i < vs->nfuncs; ++i) vs_frames += vs->funcs[i].frame_cells;
    for (i = 0; i < fs->nfuncs; ++i) fs_frames += fs->funcs[i].frame_cells;
    p->scratch_cap = 8192;
    p->stack_cap = (vs_frames + fs_frames) * 8 + 4096;
    if (p->stack_cap < 8192) p->stack_cap = 8192;
    p->scratch = (glsl_cell *)calloc(p->scratch_cap, sizeof(glsl_cell));
    p->stack   = (glsl_cell *)calloc(p->stack_cap,   sizeof(glsl_cell));
    if (!p->scratch || !p->stack) { fail(p, vs, fs, "link: out of memory (arenas)", err, errsz); return NULL; }

    /* initialise globals, then snapshot for per-invocation restore */
    if (vs_cells > 0) {
        glsl_exec_init_globals(p, vs, p->vs_gstore);
        p->vs_ginit = (glsl_cell *)malloc(vs_cells * sizeof(glsl_cell));
        if (p->vs_ginit) memcpy(p->vs_ginit, p->vs_gstore, vs_cells * sizeof(glsl_cell));
    }
    if (fs_cells > 0) {
        glsl_exec_init_globals(p, fs, p->fs_gstore);
        p->fs_ginit = (glsl_cell *)malloc(fs_cells * sizeof(glsl_cell));
        if (p->fs_ginit) memcpy(p->fs_ginit, p->fs_gstore, fs_cells * sizeof(glsl_cell));
    }

    p->linked = 1;
    if (err && errsz > 0) err[0] = '\0';
    return p;
}

void glsl_free_program(glsl_program *p)
{
    if (!p) return;
    free(p->vs_gstore); free(p->fs_gstore);
    free(p->vs_ginit);  free(p->fs_ginit);
    free(p->scratch);   free(p->stack);
    glsl_free_stage(p->vs);
    glsl_free_stage(p->fs);
    free(p);
}

/* ------------------------------------------------------------------ */
int glsl_uniform_index(const glsl_program *p, const char *name)
{
    int i;
    char base[GLSL_NAME_LEN];
    if (!p || !name) return -1;
    for (i = 0; i < p->nuniforms; ++i)
        if (!strcmp(p->uniforms[i].name, name)) return i;
    /* accept "arr[3]" style lookups by matching the base name */
    {
        const char *br = strchr(name, '[');
        size_t n;
        if (!br) return -1;
        n = (size_t)(br - name);
        if (n >= sizeof base) n = sizeof base - 1;
        memcpy(base, name, n); base[n] = '\0';
        for (i = 0; i < p->nuniforms; ++i)
            if (!strcmp(p->uniforms[i].name, base)) return i;
    }
    return -1;
}

int glsl_attrib_index(const glsl_program *p, const char *name)
{
    int i;
    if (!p || !name) return -1;
    for (i = 0; i < p->nattribs; ++i)
        if (!strcmp(p->attribs[i].name, name)) return p->attribs[i].loc;
    return -1;
}

/* Named glsl_prog_varying_count (not glsl_varying_count) to avoid a strong-symbol
 * clash with mesa's libv3dcompiler, which exports glsl_varying_count too; the two
 * archives are only linked together in a PGL_G3D (raspi5 GPU) build. */
int glsl_prog_varying_count(const glsl_program *p)
{
    return p ? p->nvaryings : 0;
}

void glsl_bind_attrib_location(glsl_stage *vs, const char *name, int loc)
{
    int i;
    if (!vs || !name) return;
    for (i = 0; i < vs->nglobals; ++i)
        if (vs->globals[i].scope == VARSCOPE_ATTRIB &&
            !strcmp(vs->globals[i].name, name)) {
            vs->globals[i].slot = loc;
            return;
        }
}
