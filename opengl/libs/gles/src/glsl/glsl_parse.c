/* glsl_parse.c - recursive-descent parser for GLSL ES 1.00.
 *
 * Produces a glsl_stage: user functions (AST bodies), file-scope globals
 * (attribute/varying/uniform/const/var) and struct definitions.  Identifiers
 * are resolved to a scope+slot as they are parsed; per-stage varying/uniform/
 * attribute slots are re-numbered into unified ones by the link pass (which
 * matches by the var_name kept on every EX_VAR node).
 *
 * The interpreter is dynamically typed at run time, so EX_FIELD/EX_INDEX do not
 * need static type inference: swizzles are recognised lexically (xyzw/rgba/
 * stpq) and struct members are looked up by name against the runtime value's
 * struct id.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>

#include "glsl.h"
#include "glsl_lex.h"
#include "glsl_util.h"

typedef struct {
    char name[GLSL_NAME_LEN];
    int  scope, slot, type, st_id, array_len, depth;
} symbol;

typedef struct {
    const glsl_token *tk;
    int ntok, pos;
    int is_vertex;
    glsl_stage *st;
    /* per-stage slot counters */
    int next_attrib, next_varying, next_uniform, next_global;
    /* current function local frame */
    symbol locals[GLSL_MAX_LOCALS];
    int nlocals, cur_depth, local_high;
    int in_func;
    int cur_func;            /* index of the function being parsed        */
    int local_cell_high;     /* running cell offset for the local frame   */
    jmp_buf err_jmp;
    char err[GLSL_ERR_LEN];
    int errored;
} parser;

/* ------------------------------------------------------------------ */
static void perr(parser *P, const char *fmt, ...)
{
    va_list ap;
    if (P->errored) return;
    P->errored = 1;
    {
        char msg[192];
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        snprintf(P->err, sizeof P->err, "line %d: %s",
                 P->pos < P->ntok ? P->tk[P->pos].line : 0, msg);
    }
    longjmp(P->err_jmp, 1);
}

static const glsl_token *cur(parser *P)  { return &P->tk[P->pos]; }
static int tkkind(parser *P)             { return P->tk[P->pos].kind; }
static int at(parser *P, int k)          { return P->tk[P->pos].kind == k; }
static int adv(parser *P)                { int k = P->tk[P->pos].kind; if (P->pos < P->ntok-1) P->pos++; return k; }
static int accept(parser *P, int k)      { if (at(P, k)) { adv(P); return 1; } return 0; }
static void expect(parser *P, int k, const char *what)
{ if (!accept(P, k)) perr(P, "expected %s", what); }

static void *xalloc(size_t z) { void *p = calloc(1, z); return p; }

static glsl_expr *new_expr(int kind)
{
    glsl_expr *e = (glsl_expr *)xalloc(sizeof(glsl_expr));
    e->kind = kind;
    return e;
}

/* ------------------------------------------------------------------ */
/* forward decls */
static glsl_expr *parse_assign(parser *P);
static glsl_stmt *parse_statement(parser *P);
static glsl_stmt *parse_compound(parser *P);
static void parse_type(parser *P, int *type, int *st_id, int *array_len);
static int  is_type_start(parser *P);

/* builtin function name -> id */
typedef struct { const char *n; int id; int nargs; } bif;
static const bif k_bifs[] = {
    {"radians",BF_RADIANS,1},{"degrees",BF_DEGREES,1},{"sin",BF_SIN,1},{"cos",BF_COS,1},
    {"tan",BF_TAN,1},{"asin",BF_ASIN,1},{"acos",BF_ACOS,1},{"atan",BF_ATAN,-1},
    {"tanh",BF_TANH,1}, {"round",BF_ROUND,1},
    {"floatBitsToUint",BF_FLOAT_BITS_TO_UINT,1},
    {"pow",BF_POW,2},{"exp",BF_EXP,1},{"log",BF_LOG,1},{"exp2",BF_EXP2,1},{"log2",BF_LOG2,1},
    {"sqrt",BF_SQRT,1},{"inversesqrt",BF_INVSQRT,1},{"abs",BF_ABS,1},{"sign",BF_SIGN,1},
    {"floor",BF_FLOOR,1},{"ceil",BF_CEIL,1},{"fract",BF_FRACT,1},{"mod",BF_MOD,2},
    {"min",BF_MIN,2},{"max",BF_MAX,2},{"clamp",BF_CLAMP,3},{"mix",BF_MIX,3},{"step",BF_STEP,2},
    {"smoothstep",BF_SMOOTHSTEP,3},{"length",BF_LENGTH,1},{"distance",BF_DISTANCE,2},
    {"dot",BF_DOT,2},{"cross",BF_CROSS,2},{"normalize",BF_NORMALIZE,1},{"faceforward",BF_FACEFORWARD,3},
    {"reflect",BF_REFLECT,2},{"refract",BF_REFRACT,3},{"matrixCompMult",BF_MATRIXCOMPMUL,2},
    {"transpose",BF_TRANSPOSE,1},{"determinant",BF_DETERMINANT,1},{"inverse",BF_INVERSE,1},
    {"lessThan",BF_LESS_THAN,2},{"lessThanEqual",BF_LESS_THAN_EQUAL,2},
    {"greaterThan",BF_GREATER_THAN,2},{"greaterThanEqual",BF_GREATER_THAN_EQUAL,2},
    {"equal",BF_EQUAL,2},{"notEqual",BF_NOT_EQUAL,2},{"any",BF_ANY,1},{"all",BF_ALL,1},
    {"not",BF_NOT,1},
    {"texture2D",BF_TEXTURE2D,-1},{"texture2DProj",BF_TEXTURE2DPROJ,-1},
    {"texture2DLod",BF_TEXTURE2DLOD,3},{"textureCube",BF_TEXTURECUBE,-1},
    {"textureCubeLod",BF_TEXTURECUBELOD,3},
    {"dFdx",BF_DFDX,1},{"dFdy",BF_DFDY,1},{"fwidth",BF_FWIDTH,1},
    {NULL,0,0}
};

/* builtin variable name -> BI_ id */
static int builtin_var(const char *n)
{
    if (!strcmp(n,"gl_Position"))   return BI_GL_POSITION;
    if (!strcmp(n,"gl_FragColor"))  return BI_GL_FRAGCOLOR;
    if (!strcmp(n,"gl_FragCoord"))  return BI_GL_FRAGCOORD;
    if (!strcmp(n,"gl_PointCoord")) return BI_GL_POINTCOORD;
    if (!strcmp(n,"gl_FrontFacing"))return BI_GL_FRONTFACING;
    if (!strcmp(n,"gl_FragDepth"))  return BI_GL_FRAGDEPTH;
    if (!strcmp(n,"gl_PointSize"))  return BI_GL_POINTSIZE;
    if (!strcmp(n,"gl_InstanceID")) return BI_GL_INSTANCEID;
    return -1;
}

static int type_from_token(int k)
{
    switch (k) {
    case TK_BOOL: return T_BOOL; case TK_INT_T: return T_INT; case TK_FLOAT_T: return T_FLOAT;
    case TK_VEC2: return T_VEC2; case TK_VEC3: return T_VEC3; case TK_VEC4: return T_VEC4;
    case TK_IVEC2: return T_IVEC2; case TK_IVEC3: return T_IVEC3; case TK_IVEC4: return T_IVEC4;
    case TK_UINT_T: return T_UINT;
    case TK_UVEC2: return T_UVEC2; case TK_UVEC3: return T_UVEC3; case TK_UVEC4: return T_UVEC4;
    case TK_BVEC2: return T_BVEC2; case TK_BVEC3: return T_BVEC3; case TK_BVEC4: return T_BVEC4;
    case TK_MAT2: return T_MAT2; case TK_MAT3: return T_MAT3; case TK_MAT4: return T_MAT4;
    case TK_SAMPLER2D: return T_SAMPLER2D; case TK_SAMPLERCUBE: return T_SAMPLERCUBE;
    case TK_VOID_T: return T_VOID;
    default: return -1;
    }
}

static int is_ctor_type(int k) { return type_from_token(k) > T_VOID; }

/* struct lookup by name */
static int find_struct(parser *P, const char *name)
{
    int i;
    for (i = 0; i < P->st->nstructs; ++i)
        if (!strcmp(P->st->structs[i].name, name)) return i;
    return -1;
}

/* parse "struct Name { fields }" definition (body present) */
static int parse_struct_def(parser *P, char *name_out)
{
    int id, nf = 0;
    expect(P, TK_STRUCT, "'struct'");
    if (at(P, TK_IDENT)) { strncpy(name_out, cur(P)->text, GLSL_NAME_LEN-1); adv(P); }
    else name_out[0] = '\0';
    expect(P, TK_LBRACE, "'{'");
    id = find_struct(P, name_out);
    if (id < 0) {
        if (P->st->nstructs >= 16) perr(P, "too many structs");
        id = P->st->nstructs++;
        strncpy(P->st->structs[id].name, name_out, GLSL_NAME_LEN-1);
    }
    while (!at(P, TK_RBRACE) && !at(P, TK_EOF)) {
        int type, st_id, arr; float off = 0;
        parse_type(P, &type, &st_id, &arr);
        (void)off;
        do {
            char fn[GLSL_NAME_LEN];
            int fa = 0;
            if (!at(P, TK_IDENT)) perr(P, "struct field name");
            strncpy(fn, cur(P)->text, GLSL_NAME_LEN-1); adv(P);
            if (accept(P, TK_LBRACKET)) {
                if (!at(P, TK_INT)) perr(P, "struct array size");
                fa = cur(P)->ival; adv(P);
                expect(P, TK_RBRACKET, "']'");
            }
            if (nf < 16) {
                strncpy(P->st->structs[id].fname[nf], fn, GLSL_NAME_LEN-1);
                P->st->structs[id].ftype[nf] = type;
                P->st->structs[id].fst_id[nf] = st_id;
                P->st->structs[id].farray[nf] = fa;
                P->st->structs[id].foffset[nf] = 0; /* computed at link */
            }
            nf++;
            if (nf > 16) perr(P, "too many struct fields");
        } while (accept(P, TK_COMMA));
        expect(P, TK_SEMI, "';'");
    }
    expect(P, TK_RBRACE, "'}'");
    P->st->structs[id].nfields = nf;
    /* compute float offsets + total */
    {
        int i, total = 0;
        for (i = 0; i < nf; ++i) {
            int t = P->st->structs[id].ftype[i];
            int sz = glsl_type_cells(P->st, t, P->st->structs[id].fst_id[i]);
            int cnt = P->st->structs[id].farray[i];
            P->st->structs[id].foffset[i] = (int)total;
            total += sz * (cnt>0?cnt:1);
        }
        P->st->structs[id].total_floats = (int)total;
    }
    return id;
}

static int is_type_start(parser *P)
{
    int k = tkkind(P);
    if (k == TK_STRUCT) return 1;
    if (k == TK_CONST) return 1;
    if (k == TK_LOWP || k == TK_MEDIUMP || k == TK_HIGHP) return 1;
    if (type_from_token(k) >= 0) return 1;
    if (k == TK_IDENT && find_struct(P, cur(P)->text) >= 0) return 1;
    return 0;
}

/* parse a type specifier; sets *type (glsl_basetype), *st_id, *array_len.
 * Does NOT consume a trailing declarator name. */
static void parse_type(parser *P, int *type, int *st_id, int *array_len)
{
    int t;
    *st_id = -1; *array_len = 0;
    accept(P, TK_CONST);
    while (at(P, TK_LOWP) || at(P, TK_MEDIUMP) || at(P, TK_HIGHP)) adv(P);
    if (at(P, TK_STRUCT)) {
        char nm[GLSL_NAME_LEN];
        /* struct definition or use of an existing one */
        if (P->tk[P->pos+1].kind == TK_IDENT && P->tk[P->pos+2].kind != TK_LBRACE) {
            /* use of existing struct type: "struct S" */
            adv(P);
            strncpy(nm, cur(P)->text, GLSL_NAME_LEN-1); adv(P);
            *st_id = find_struct(P, nm);
            *type = T_STRUCT;
            if (*st_id < 0) perr(P, "unknown struct '%s'", nm);
        } else {
            *st_id = parse_struct_def(P, nm);
            *type = T_STRUCT;
        }
        if (accept(P, TK_LBRACKET)) {
            if (at(P, TK_INT)) *array_len = cur(P)->ival, adv(P);
            expect(P, TK_RBRACKET, "']'");
        }
        return;
    }
    t = type_from_token(tkkind(P));
    if (t >= 0) { adv(P); *type = t; }
    else if (at(P, TK_IDENT) && find_struct(P, cur(P)->text) >= 0) {
        *st_id = find_struct(P, cur(P)->text); adv(P); *type = T_STRUCT;
    } else perr(P, "expected a type");
    if (accept(P, TK_LBRACKET)) {
        if (at(P, TK_INT)) *array_len = cur(P)->ival, adv(P);
        expect(P, TK_RBRACKET, "']'");
    }
}

/* ------------------------------------------------------------------ */
/* local symbol table (function scope) */
static void push_scope(parser *P) { P->cur_depth++; }
static void pop_scope(parser *P)
{
    int i;
    for (i = 0; i < P->nlocals; ) {
        if (P->locals[i].depth >= P->cur_depth) { P->locals[i] = P->locals[P->nlocals-1]; P->nlocals--; }
        else i++;
    }
    P->cur_depth--;
}
static symbol *find_local(parser *P, const char *name)
{
    int i;
    for (i = 0; i < P->nlocals; ++i)
        if (!strcmp(P->locals[i].name, name)) return &P->locals[i];
    return NULL;
}
static symbol *add_local(parser *P, const char *name, int type, int st_id, int array_len)
{
    symbol *s;
    if (P->local_high >= GLSL_MAX_LOCALS) perr(P, "too many locals");
    if (find_local(P, name)) perr(P, "redefinition of '%s'", name);
    s = &P->locals[P->nlocals++];
    strncpy(s->name, name, GLSL_NAME_LEN-1);
    s->scope = VARSCOPE_LOCAL; s->type = type; s->st_id = st_id;
    s->array_len = array_len; s->depth = P->cur_depth;
    s->slot = P->local_high++;
    {
        glsl_func *F = &P->st->funcs[P->cur_func];
        int ncomp = glsl_type_cells(P->st, type, st_id);
        int cells = array_len > 0 ? ncomp * array_len : ncomp;
        if (s->slot < GLSL_MAX_LOCALS) {
            F->local_off[s->slot]   = P->local_cell_high;
            F->local_type[s->slot]  = type;
            F->local_stid[s->slot]  = st_id;
            F->local_ncomp[s->slot] = ncomp;
            F->local_array[s->slot] = array_len;
        }
        P->local_cell_high += cells;
    }
    return s;
}

/* global declaration record + slot */
static int add_global(parser *P, const char *name, int scope, int type, int st_id,
                      int array_len, int is_const)
{
    int slot, i;
    for (i = 0; i < P->st->nglobals; ++i)
        if (!strcmp(P->st->globals[i].name, name) &&
            P->st->globals[i].scope == scope) {
            /* already declared (e.g. same uniform in both stages is separate;
             * within a stage a redeclare of a uniform is an error, but a
             * matching existing entry is reused for prototypes) */
            return P->st->globals[i].slot;
        }
    if (P->st->nglobals >= GLSL_MAX_GLOBALS) perr(P, "too many globals");
    i = P->st->nglobals++;
    strncpy(P->st->globals[i].name, name, GLSL_NAME_LEN-1);
    P->st->globals[i].scope = scope;
    P->st->globals[i].type = type;
    P->st->globals[i].st_id = st_id;
    P->st->globals[i].array_len = array_len;
    P->st->globals[i].is_const = is_const;
    P->st->globals[i].const_init = NULL;
    P->st->globals[i].has_const_val = 0;
    switch (scope) {
    case VARSCOPE_ATTRIB:  slot = P->next_attrib++;  break;
    case VARSCOPE_VARYING: slot = P->next_varying++; break;
    case VARSCOPE_UNIFORM: slot = P->next_uniform++; break;
    default:               slot = P->next_global++;  break;
    }
    P->st->globals[i].slot = slot;
    return slot;
}

static int find_global(parser *P, const char *name, int *scope_out)
{
    int i;
    for (i = 0; i < P->st->nglobals; ++i)
        if (!strcmp(P->st->globals[i].name, name)) {
            if (scope_out) *scope_out = P->st->globals[i].scope;
            return P->st->globals[i].slot;
        }
    return -1;
}

/* ------------------------------------------------------------------ */
/* expressions */
static glsl_expr *parse_primary(parser *P);
static glsl_expr *parse_unary(parser *P);
static glsl_expr *parse_mul(parser *P);
static glsl_expr *parse_add(parser *P);
static glsl_expr *parse_shift(parser *P);
static glsl_expr *parse_rel(parser *P);
static glsl_expr *parse_eq(parser *P);
static glsl_expr *parse_band(parser *P);
static glsl_expr *parse_bxor(parser *P);
static glsl_expr *parse_bor(parser *P);
static glsl_expr *parse_logand(parser *P);
static glsl_expr *parse_logxor(parser *P);
static glsl_expr *parse_logor(parser *P);
static glsl_expr *parse_ternary(parser *P);

static int swz_comp(char c)
{
    switch (c) {
    case 'x': case 'r': case 's': return 0;
    case 'y': case 'g': case 't': return 1;
    case 'z': case 'b': case 'p': return 2;
    case 'w': case 'a': case 'q': return 3;
    default: return -1;
    }
}

static glsl_expr *parse_postfix(parser *P)
{
    glsl_expr *e = parse_primary(P);
    for (;;) {
        if (accept(P, TK_LBRACKET)) {
            glsl_expr *idx = parse_assign(P);
            glsl_expr *n = new_expr(EX_INDEX);
            expect(P, TK_RBRACKET, "']'");
            n->a = e; n->b = idx; e = n;
        } else if (accept(P, TK_DOT)) {
            glsl_expr *n = new_expr(EX_FIELD);
            int allswz = 1, k = 0;
            if (!at(P, TK_IDENT)) perr(P, "field name");
            strncpy(n->var_name, cur(P)->text, GLSL_NAME_LEN-1);
            {
                const char *s = cur(P)->text;
                for (; *s; ++s) { if (swz_comp(*s) < 0) { allswz = 0; break; } }
                if (allswz) k = (int)strlen(n->var_name);
                if (allswz && k >= 1 && k <= 4) {
                    int j; n->field_is_swizzle = 1; n->swz_n = k;
                    for (j = 0; j < k; ++j) n->swz[j] = (unsigned char)swz_comp(n->var_name[j]);
                } else { n->field_is_swizzle = 0; }
            }
            adv(P);
            n->a = e; e = n;
        } else if (accept(P, TK_INC)) {
            glsl_expr *n = new_expr(EX_POSTINC); n->a = e; e = n;
        } else if (accept(P, TK_DEC)) {
            glsl_expr *n = new_expr(EX_POSTDEC); n->a = e; e = n;
        } else break;
    }
    return e;
}

static glsl_expr *parse_primary(parser *P)
{
    const glsl_token *t = cur(P);
    if (t->kind == TK_FLOAT) {
        glsl_expr *e = new_expr(EX_FLOAT); e->lit_f = t->fval; adv(P); return e;
    }
    if (t->kind == TK_INT || t->kind == TK_UINT) {
        glsl_expr *e = new_expr(t->kind == TK_UINT ? EX_UINT : EX_INT);
        e->lit_i = t->ival; adv(P); return e;
    }
    if (t->kind == TK_TRUE || t->kind == TK_FALSE) {
        glsl_expr *e = new_expr(EX_BOOL); e->lit_i = t->ival; adv(P); return e;
    }
    if (t->kind == TK_LPAREN) {
        glsl_expr *e; adv(P); e = parse_assign(P); expect(P, TK_RPAREN, "')'"); return e;
    }
    /* array constructor: type[N](args...)  (GLSL ES 3.00 array literal, e.g.
     * `float[64](...)` / `int[119](...)`).  Recognised before the ordinary
     * constructor because both start with a type token; the discriminator is
     * the '[' that follows the type name.  Only valid in an expression (a
     * declaration's `type name[N]` is consumed by parse_type/declarator). */
    if (is_ctor_type(t->kind) && P->tk[P->pos+1].kind == TK_LBRACKET) {
        int type = type_from_token(t->kind);
        int arr = 0;
        glsl_expr *e;
        adv(P);                              /* consume the type token   */
        expect(P, TK_LBRACKET, "'['");
        if (at(P, TK_INT)) { arr = cur(P)->ival; adv(P); }
        expect(P, TK_RBRACKET, "']'");
        e = new_expr(EX_CTOR);
        e->ctor_type = type;
        e->ctor_array = arr;
        expect(P, TK_LPAREN, "'('");
        if (!at(P, TK_RPAREN)) {
            for (;;) {
                glsl_expr **na;
                e->nargs++;
                na = (glsl_expr **)realloc(e->args, e->nargs * sizeof(glsl_expr*));
                e->args = na;
                e->args[e->nargs-1] = parse_assign(P);
                if (!accept(P, TK_COMMA)) break;
            }
        }
        expect(P, TK_RPAREN, "')'");
        return e;
    }
    /* constructor */
    if (is_ctor_type(t->kind) && P->tk[P->pos+1].kind == TK_LPAREN) {
        int type = type_from_token(t->kind);
        glsl_expr *e = new_expr(EX_CTOR);
        e->ctor_type = type;
        adv(P); expect(P, TK_LPAREN, "'('");
        if (!at(P, TK_RPAREN)) {
            for (;;) {
                glsl_expr **na;
                e->nargs++;
                na = (glsl_expr **)realloc(e->args, e->nargs * sizeof(glsl_expr*));
                e->args = na;
                e->args[e->nargs-1] = parse_assign(P);
                if (!accept(P, TK_COMMA)) break;
            }
        }
        expect(P, TK_RPAREN, "')'");
        return e;
    }
    if (t->kind == TK_IDENT) {
        char name[GLSL_NAME_LEN];
        strncpy(name, t->text, GLSL_NAME_LEN-1); name[GLSL_NAME_LEN-1]='\0';
        adv(P);
        if (at(P, TK_LPAREN)) {
            /* function call */
            glsl_expr *e = new_expr(EX_CALL);
            int i;
            strncpy(e->var_name, name, GLSL_NAME_LEN-1);
            adv(P);
            if (!at(P, TK_RPAREN)) {
                for (;;) {
                    glsl_expr **na;
                    e->nargs++;
                    na = (glsl_expr **)realloc(e->args, e->nargs * sizeof(glsl_expr*));
                    e->args = na;
                    e->args[e->nargs-1] = parse_assign(P);
                    if (!accept(P, TK_COMMA)) break;
                }
            }
            expect(P, TK_RPAREN, "')'");
            for (i = 0; k_bifs[i].n; ++i)
                if (!strcmp(k_bifs[i].n, name)) { e->op_builtin = k_bifs[i].id; e->is_user_fn = 0; return e; }
            for (i = 0; i < P->st->nfuncs; ++i)
                if (!strcmp(P->st->funcs[i].name, name)) { e->op_builtin = i; e->is_user_fn = 1; return e; }
            e->op_builtin = -1; e->is_user_fn = 1; /* resolve at link */
            return e;
        }
        /* variable reference */
        {
            glsl_expr *e = new_expr(EX_VAR);
            symbol *L;
            strncpy(e->var_name, name, GLSL_NAME_LEN-1);
            L = P->in_func ? find_local(P, name) : NULL;
            if (L) { e->var_scope = VARSCOPE_LOCAL; e->var_slot = L->slot; return e; }
            {
                int scope = -1, slot = find_global(P, name, &scope);
                if (slot >= 0) { e->var_scope = scope; e->var_slot = slot; return e; }
            }
            {
                int bi = builtin_var(name);
                if (bi >= 0) { e->var_scope = VARSCOPE_BUILTIN; e->var_slot = bi; return e; }
            }
            e->var_scope = -1; e->var_slot = -1; /* unresolved; link errors */
            return e;
        }
    }
    perr(P, "unexpected token in expression");
    return NULL;
}

static glsl_expr *mkbin(int kind, glsl_expr *a, glsl_expr *b)
{ glsl_expr *e = new_expr(kind); e->a = a; e->b = b; return e; }

static glsl_expr *parse_unary(parser *P)
{
    int k = tkkind(P);
    if (k == TK_INC || k == TK_DEC) {
        glsl_expr *n = new_expr(k==TK_INC?EX_PREINC:EX_PREDEC); adv(P); n->a = parse_unary(P); return n;
    }
    if (k == TK_ADD) { adv(P); return parse_unary(P); }
    if (k == TK_SUB) { glsl_expr *n = new_expr(EX_NEG); adv(P); n->a = parse_unary(P); return n; }
    if (k == TK_BANG){ glsl_expr *n = new_expr(EX_NOT); adv(P); n->a = parse_unary(P); return n; }
    if (k == TK_BNOT){ glsl_expr *n = new_expr(EX_BNOT); adv(P); n->a = parse_unary(P); return n; }
    return parse_postfix(P);
}

static glsl_expr *parse_mul(parser *P)
{
    glsl_expr *a = parse_unary(P);
    for (;;) {
        int k = tkkind(P), op;
        if (k==TK_MUL) op=EX_MUL; else if (k==TK_DIV) op=EX_DIV; else if (k==TK_MOD) op=EX_MOD; else break;
        adv(P); a = mkbin(op, a, parse_unary(P));
    }
    return a;
}
static glsl_expr *parse_add(parser *P)
{
    glsl_expr *a = parse_mul(P);
    for (;;) {
        int k = tkkind(P), op;
        if (k==TK_ADD) op=EX_ADD; else if (k==TK_SUB) op=EX_SUB; else break;
        adv(P); a = mkbin(op, a, parse_mul(P));
    }
    return a;
}
static glsl_expr *parse_shift(parser *P)
{
    glsl_expr *a = parse_add(P);
    for (;;) {
        int k = tkkind(P), op;
        if (k==TK_SHL) op=EX_SHL; else if (k==TK_SHR) op=EX_SHR; else break;
        adv(P); a = mkbin(op, a, parse_add(P));
    }
    return a;
}
static glsl_expr *parse_rel(parser *P)
{
    glsl_expr *a = parse_shift(P);
    for (;;) {
        int k = tkkind(P), op;
        if (k==TK_LT) op=EX_LT; else if (k==TK_GT) op=EX_GT;
        else if (k==TK_LE) op=EX_LE; else if (k==TK_GE) op=EX_GE; else break;
        adv(P); a = mkbin(op, a, parse_shift(P));
    }
    return a;
}
static glsl_expr *parse_eq(parser *P)
{
    glsl_expr *a = parse_rel(P);
    for (;;) {
        int k = tkkind(P), op;
        if (k==TK_EQ) op=EX_EQ; else if (k==TK_NE) op=EX_NE; else break;
        adv(P); a = mkbin(op, a, parse_rel(P));
    }
    return a;
}
static glsl_expr *parse_band(parser *P)
{ glsl_expr *a=parse_eq(P); while(at(P,TK_BAND)){adv(P);a=mkbin(EX_BAND,a,parse_eq(P));} return a; }
static glsl_expr *parse_bxor(parser *P)
{ glsl_expr *a=parse_band(P); while(at(P,TK_BXOR)){adv(P);a=mkbin(EX_BXOR,a,parse_band(P));} return a; }
static glsl_expr *parse_bor(parser *P)
{ glsl_expr *a=parse_bxor(P); while(at(P,TK_BOR)){adv(P);a=mkbin(EX_BOR,a,parse_bxor(P));} return a; }
static glsl_expr *parse_logand(parser *P)
{ glsl_expr *a=parse_bor(P); while(at(P,TK_LOGAND)){adv(P);a=mkbin(EX_LOGAND,a,parse_bor(P));} return a; }
static glsl_expr *parse_logxor(parser *P)
{ glsl_expr *a=parse_logand(P); while(at(P,TK_LOGXOR)){adv(P);a=mkbin(EX_LOGXOR,a,parse_logand(P));} return a; }
static glsl_expr *parse_logor(parser *P)
{ glsl_expr *a=parse_logxor(P); while(at(P,TK_LOGOR)){adv(P);a=mkbin(EX_LOGOR,a,parse_logxor(P));} return a; }

static glsl_expr *parse_ternary(parser *P)
{
    glsl_expr *c = parse_logor(P);
    if (accept(P, TK_QUESTION)) {
        glsl_expr *e = new_expr(EX_TERNARY);
        e->a = c; e->b = parse_assign(P);
        expect(P, TK_COLON, "':'");
        e->cc = parse_assign(P);
        return e;
    }
    return c;
}

static int assign_op(int k)
{
    switch (k) {
    case TK_ASSIGN: return EX_ASSIGN;
    case TK_MUL_ASSIGN: return EX_MUL_ASSIGN;
    case TK_DIV_ASSIGN: return EX_DIV_ASSIGN;
    case TK_MOD_ASSIGN: return EX_MOD_ASSIGN;
    case TK_ADD_ASSIGN: return EX_ADD_ASSIGN;
    case TK_SUB_ASSIGN: return EX_SUB_ASSIGN;
    case TK_SHL_ASSIGN: return EX_SHL_ASSIGN;
    case TK_SHR_ASSIGN: return EX_SHR_ASSIGN;
    case TK_AND_ASSIGN: return EX_AND_ASSIGN;
    case TK_XOR_ASSIGN: return EX_XOR_ASSIGN;
    case TK_OR_ASSIGN: return EX_OR_ASSIGN;
    default: return -1;
    }
}

static glsl_expr *parse_assign(parser *P)
{
    glsl_expr *lhs = parse_ternary(P);
    int op = assign_op(tkkind(P));
    if (op >= 0) {
        glsl_expr *e;
        adv(P);
        e = mkbin(op, lhs, parse_assign(P));
        return e;
    }
    return lhs;
}

static glsl_expr *parse_expression(parser *P)
{
    glsl_expr *e = parse_assign(P);
    while (accept(P, TK_COMMA)) {
        glsl_expr *n = new_expr(EX_COMMA);
        n->a = e; n->b = parse_assign(P); e = n;
    }
    return e;
}

/* ------------------------------------------------------------------ */
/* statements */
static void parse_decl_stmt(parser *P, glsl_stmt **head, glsl_stmt **tail)
{
    int type, st_id, arr, is_const = 0;
    glsl_stmt *first = NULL;
    /* 'const' may precede */
    if (at(P, TK_CONST)) { is_const = 1; }
    parse_type(P, &type, &st_id, &arr);
    (void)is_const;
    for (;;) {
        glsl_stmt *s;
        char nm[GLSL_NAME_LEN];
        int local_arr = arr;
        symbol *sym;
        if (!at(P, TK_IDENT)) perr(P, "declaration name");
        strncpy(nm, cur(P)->text, GLSL_NAME_LEN-1); adv(P);
        if (accept(P, TK_LBRACKET)) {
            if (at(P, TK_INT)) local_arr = cur(P)->ival, adv(P);
            expect(P, TK_RBRACKET, "']'");
        }
        s = (glsl_stmt *)xalloc(sizeof(glsl_stmt));
        s->kind = ST_DECL;
        s->decl_type = type; s->decl_st_id = st_id; s->decl_array = local_arr;
        s->decl_is_const = is_const;
        sym = add_local(P, nm, type, st_id, local_arr);
        s->decl_scope = VARSCOPE_LOCAL; s->decl_slot = sym->slot;
        if (accept(P, TK_ASSIGN)) s->decl_init = parse_assign(P);
        s->next = NULL;
        if (!first) first = s;
        if (*tail) (*tail)->next = s; else *head = s;
        *tail = s;
        if (!accept(P, TK_COMMA)) break;
    }
    (void)first;
    expect(P, TK_SEMI, "';'");
}

static glsl_stmt *parse_statement(parser *P)
{
    int k = tkkind(P);
    if (k == TK_SEMI) { adv(P); return NULL; }
    if (k == TK_LBRACE) return parse_compound(P);
    if (k == TK_IF) {
        glsl_stmt *s = (glsl_stmt*)xalloc(sizeof(glsl_stmt));
        s->kind = ST_IF; adv(P);
        expect(P, TK_LPAREN, "'('"); s->expr = parse_expression(P); expect(P, TK_RPAREN, "')'");
        push_scope(P); s->body = parse_statement(P); pop_scope(P);
        if (accept(P, TK_ELSE)) { push_scope(P); s->els = parse_statement(P); pop_scope(P); }
        return s;
    }
    if (k == TK_WHILE) {
        glsl_stmt *s = (glsl_stmt*)xalloc(sizeof(glsl_stmt));
        s->kind = ST_WHILE; adv(P);
        expect(P, TK_LPAREN, "'('"); s->expr = parse_expression(P); expect(P, TK_RPAREN, "')'");
        push_scope(P); s->body = parse_statement(P); pop_scope(P);
        return s;
    }
    if (k == TK_DO) {
        glsl_stmt *s = (glsl_stmt*)xalloc(sizeof(glsl_stmt));
        s->kind = ST_DOWHILE; adv(P);
        push_scope(P); s->body = parse_statement(P); pop_scope(P);
        expect(P, TK_WHILE, "'while'"); expect(P, TK_LPAREN, "'('");
        s->expr = parse_expression(P); expect(P, TK_RPAREN, "')'"); expect(P, TK_SEMI, "';'");
        return s;
    }
    if (k == TK_FOR) {
        glsl_stmt *s = (glsl_stmt*)xalloc(sizeof(glsl_stmt));
        s->kind = ST_FOR; adv(P);
        expect(P, TK_LPAREN, "'('");
        push_scope(P);
        if (at(P, TK_SEMI)) adv(P);
        else if (is_type_start(P)) {
            /* for-init declaration: parse_decl_stmt consumes the ';'.  The
             * resulting ST_DECL list is kept in s->children and executed once
             * before the loop test. */
            glsl_stmt *h = NULL, *t = NULL;
            parse_decl_stmt(P, &h, &t);
            s->children = h;
        }
        else { s->expr = parse_expression(P); expect(P, TK_SEMI, "';'"); }
        if (!at(P, TK_SEMI)) s->cond2 = parse_expression(P);
        expect(P, TK_SEMI, "';'");
        if (!at(P, TK_RPAREN)) s->post = parse_expression(P);
        expect(P, TK_RPAREN, "')'");
        s->body = parse_statement(P);
        pop_scope(P);
        return s;
    }
    if (k == TK_BREAK)    { glsl_stmt *s=(glsl_stmt*)xalloc(sizeof(glsl_stmt)); s->kind=ST_BREAK; adv(P); expect(P,TK_SEMI,"';'"); return s; }
    if (k == TK_CONTINUE) { glsl_stmt *s=(glsl_stmt*)xalloc(sizeof(glsl_stmt)); s->kind=ST_CONTINUE; adv(P); expect(P,TK_SEMI,"';'"); return s; }
    if (k == TK_DISCARD)  { glsl_stmt *s=(glsl_stmt*)xalloc(sizeof(glsl_stmt)); s->kind=ST_DISCARD; P->st->uses_discard=1; adv(P); expect(P,TK_SEMI,"';'"); return s; }
    if (k == TK_RETURN) {
        glsl_stmt *s=(glsl_stmt*)xalloc(sizeof(glsl_stmt)); s->kind=ST_RETURN; adv(P);
        if (!at(P, TK_SEMI)) s->expr = parse_expression(P);
        expect(P, TK_SEMI, "';'"); return s;
    }
    /* declaration vs expression statement */
    if (is_type_start(P)) {
        glsl_stmt *h=NULL,*t=NULL;
        parse_decl_stmt(P,&h,&t);
        return h; /* linked list of ST_DECL */
    }
    {
        glsl_stmt *s=(glsl_stmt*)xalloc(sizeof(glsl_stmt));
        s->kind = ST_EXPR; s->expr = parse_expression(P); expect(P, TK_SEMI, "';'");
        return s;
    }
}

static glsl_stmt *parse_compound(parser *P)
{
    glsl_stmt *blk = (glsl_stmt*)xalloc(sizeof(glsl_stmt));
    glsl_stmt *tail = NULL;
    blk->kind = ST_BLOCK;
    expect(P, TK_LBRACE, "'{'");
    push_scope(P);
    while (!at(P, TK_RBRACE) && !at(P, TK_EOF)) {
        glsl_stmt *s = parse_statement(P), *it;
        for (it = s; it; it = it->next) tail = it;
        if (s) {
            if (!blk->children) blk->children = s;
            else { /* append after current tail */
                glsl_stmt *p = blk->children;
                while (p->next) p = p->next;
                p->next = s;
            }
        }
        (void)tail;
    }
    pop_scope(P);
    expect(P, TK_RBRACE, "'}'");
    return blk;
}

/* ------------------------------------------------------------------ */
/* top level */
static int func_find(parser *P, const char *name)
{
    int i;
    for (i = 0; i < P->st->nfuncs; ++i)
        if (!strcmp(P->st->funcs[i].name, name)) return i;
    return -1;
}

static void parse_global_decl(parser *P)
{
    int type, st_id, arr, is_const = 0, scope = VARSCOPE_GLOBAL;
    if (at(P, TK_CONST)) is_const = 1;
    if (at(P, TK_ATTRIBUTE)) { scope = VARSCOPE_ATTRIB; adv(P); }
    else if (at(P, TK_VARYING)) { scope = VARSCOPE_VARYING; adv(P); }
    else if (at(P, TK_UNIFORM)) { scope = VARSCOPE_UNIFORM; adv(P); }
    parse_type(P, &type, &st_id, &arr);
    if (scope == VARSCOPE_ATTRIB && !P->is_vertex)
        perr(P, "'attribute' only allowed in vertex shader");
    for (;;) {
        char nm[GLSL_NAME_LEN];
        int a = arr, slot;
        if (!at(P, TK_IDENT)) perr(P, "global declaration name");
        strncpy(nm, cur(P)->text, GLSL_NAME_LEN-1); adv(P);
        if (accept(P, TK_LBRACKET)) {
            if (at(P, TK_INT)) a = cur(P)->ival, adv(P);
            expect(P, TK_RBRACKET, "']'");
        }
        slot = add_global(P, nm, scope, type, st_id, a, is_const);
        (void)slot;
        if (is_const || scope == VARSCOPE_GLOBAL) {
            int gi;
            if (accept(P, TK_ASSIGN)) {
                glsl_expr *init = parse_assign(P);
                for (gi = 0; gi < P->st->nglobals; ++gi)
                    if (!strcmp(P->st->globals[gi].name, nm)) {
                        P->st->globals[gi].const_init = init; break;
                    }
            }
        }
        if (!accept(P, TK_COMMA)) break;
    }
    expect(P, TK_SEMI, "';'");
}

static void parse_function_def(parser *P)
{
    int type, st_id, arr;
    char name[GLSL_NAME_LEN];
    glsl_func *F;
    int idx;
    parse_type(P, &type, &st_id, &arr);
    (void)arr;
    if (!at(P, TK_IDENT)) perr(P, "function name");
    strncpy(name, cur(P)->text, GLSL_NAME_LEN-1); adv(P);
    expect(P, TK_LPAREN, "'('");
    if (P->st->nfuncs >= GLSL_MAX_FUNCS) perr(P, "too many functions");
    idx = func_find(P, name);
    if (idx < 0) idx = P->st->nfuncs++;
    F = &P->st->funcs[idx];
    strncpy(F->name, name, GLSL_NAME_LEN-1);
    F->ret_type = type; F->ret_st_id = st_id;
    F->nparams = 0;
    if (!at(P, TK_RPAREN)) {
        if (at(P, TK_VOID_T) && P->tk[P->pos+1].kind == TK_RPAREN) { adv(P); }
        else {
            for (;;) {
                int ptype, pst, parr = 0, inout = 0;
                char pn[GLSL_NAME_LEN];
                if (at(P, TK_IN)) { inout=0; adv(P); }
                else if (at(P, TK_OUT)) { inout=1; adv(P); }
                else if (at(P, TK_INOUT)) { inout=2; adv(P); }
                parse_type(P, &ptype, &pst, &parr);
                if (!at(P, TK_IDENT)) perr(P, "parameter name");
                strncpy(pn, cur(P)->text, GLSL_NAME_LEN-1); adv(P);
                if (accept(P, TK_LBRACKET)) { if (at(P,TK_INT)) adv(P); expect(P, TK_RBRACKET, "']'"); }
                if (F->nparams >= GLSL_MAX_PARAMS) perr(P, "too many parameters");
                strncpy(F->param_name[F->nparams], pn, GLSL_NAME_LEN-1);
                F->param_type[F->nparams] = ptype;
                F->param_st_id[F->nparams] = pst;
                F->param_array[F->nparams] = parr;
                F->param_inout[F->nparams] = inout;
                F->nparams++;
                if (!accept(P, TK_COMMA)) break;
            }
        }
    }
    expect(P, TK_RPAREN, "')'");
    if (accept(P, TK_SEMI)) { F->body = NULL; return; }  /* prototype */
    /* body */
    P->in_func = 1; P->nlocals = 0; P->local_high = 0; P->cur_depth = 0;
    P->cur_func = idx; P->local_cell_high = 0;
    {
        int i;
        for (i = 0; i < F->nparams && i < GLSL_MAX_PARAMS; ++i)
            add_local(P, F->param_name[i], F->param_type[i], F->param_st_id[i], F->param_array[i]);
    }
    push_scope(P);
    F->body = parse_compound(P);
    pop_scope(P);
    F->nlocals = P->local_high;
    F->frame_cells = P->local_cell_high;
    P->in_func = 0;
    if (!strcmp(name, "main")) P->st->main_idx = idx;
}

static void resolve_pending_calls(parser *P)
{
    /* second pass: bind EX_CALL nodes whose function was defined later */
    int fi;
    for (fi = 0; fi < P->st->nfuncs; ++fi) {
        /* walk is done in link stage instead; here nothing */
    }
}

/* FNV-1a (64-bit) over a NUL-terminated string.  Used to fingerprint a stage's
 * source so the engine can recognise a known shader at link time (see
 * match_shadertoy in pgl_engine.c) without retaining the text.  The identical
 * function generates the reference constants, so the two must stay in lockstep. */
static unsigned long long glsl_fnv1a_str(const char *s)
{
    unsigned long long h = 0xcbf29ce484222325ULL;
    if (!s) return h;
    for (; *s; ++s) { h ^= (unsigned char)*s; h *= 0x100000001b3ULL; }
    return h;
}

glsl_stage *glsl_compile_stage(const char *src, int is_vertex, char *err, int errsz)
{
    parser P;
    glsl_token *toks;
    int ntok = 0;
    glsl_stage *st;

    if (err && errsz) err[0] = '\0';
    toks = glsl_lex(src, &ntok, err, errsz);
    if (!toks) return NULL;

    st = (glsl_stage *)xalloc(sizeof(glsl_stage));
    if (!st) {
        free(toks);
        if (err && errsz) snprintf(err, errsz, "out of memory");
        return NULL;
    }
    if (src) {
        st->source = (char *)malloc(strlen(src) + 1);
        if (!st->source) {
            free(toks);
            glsl_free_stage(st);
            if (err && errsz) snprintf(err, errsz, "out of memory");
            return NULL;
        }
        memcpy(st->source, src, strlen(src) + 1);
    }
    st->is_vertex = is_vertex;
    st->main_idx = -1;
    st->src_hash = glsl_fnv1a_str(src);

    memset(&P, 0, sizeof P);
    P.tk = toks; P.ntok = ntok; P.pos = 0; P.is_vertex = is_vertex; P.st = st;
    P.next_attrib = 0; P.next_varying = 0; P.next_uniform = 0; P.next_global = 0;

    if (setjmp(P.err_jmp)) {
        if (err && errsz) snprintf(err, errsz, "%s", P.err);
        free(toks);
        glsl_free_stage(st);
        return NULL;
    }

    while (!at(&P, TK_EOF)) {
        if (at(&P, TK_PRECISION)) {
            adv(&P);
            while (!at(&P, TK_SEMI) && !at(&P, TK_EOF)) adv(&P);
            accept(&P, TK_SEMI);
            continue;
        }
        if (at(&P, TK_STRUCT) && P.tk[P.pos+1].kind == TK_IDENT &&
            P.tk[P.pos+2].kind == TK_LBRACE) {
            char nm[GLSL_NAME_LEN];
            parse_struct_def(&P, nm);
            accept(&P, TK_SEMI);
            continue;
        }
        /* function definition? type IDENT '(' */
        if (is_type_start(&P)) {
            int save = P.pos;
            int type, st_id, arr;
            parse_type(&P, &type, &st_id, &arr);
            if (at(&P, TK_IDENT) && P.tk[P.pos+1].kind == TK_LPAREN) {
                P.pos = save;
                parse_function_def(&P);
                continue;
            }
            P.pos = save;
        }
        parse_global_decl(&P);
    }
    resolve_pending_calls(&P);

    if (st->main_idx < 0) {
        if (err && errsz) snprintf(err, errsz, "no main() function");
        free(toks);
        glsl_free_stage(st);
        return NULL;
    }
    free(toks);
    return st;
}

void glsl_free_stage(glsl_stage *st)
{
    /* AST nodes are leaked into the stage arena; simple recursive free */
    if (!st) return;
    free(st->source);
    free(st);
}
