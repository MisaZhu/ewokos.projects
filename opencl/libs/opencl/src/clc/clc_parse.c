/*
 * clc_parse.c - recursive-descent parser for the OpenCL-C subset.  It builds
 * the AST in clc.h, resolves every variable to a frame slot (or a file-scope
 * global), assigns typed byte offsets so pointer arithmetic works against real
 * host memory, folds constant initialisers for globals, and resolves calls to
 * either a user function or a builtin.  Constructs outside the subset produce a
 * diagnostic and a NULL program (-> CL_BUILD_PROGRAM_FAILURE).
 */
#include "clc.h"
#include "clc_lex.h"
#include "clc_builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct {
    char       *name;
    int         is_float;
    long long   ival;
    double      fval;
} clc_const;

typedef struct {
    clc_lexer   *L;
    clc_program *prog;
    clc_function *curfn;
    int          frame_off;
    int          local_off;     /* __local static bump allocator            */
    int          sym_visible;   /* lookup high-water mark (<= nsyms)        */
    int          scope[64];
    int          nscope;
    struct { char *name; clc_type type; } tdefs[64];
    int          ntdefs;
    clc_const    consts[192];
    int          nconsts;
    char         err[1024];
    int          has_err;
} Parser;

/* ---- error / token helpers ---------------------------------------------- */
static void perr(Parser *P, const char *fmt, ...)
{
    va_list ap;
    if (P->has_err) return;
    P->has_err = 1;
    {
        int line = P->L->pos < P->L->ntoks ? P->L->toks[P->L->pos].line : 0;
        snprintf(P->err, sizeof(P->err), "%d: ", line);
    }
    {
        size_t n = strlen(P->err);
        va_start(ap, fmt);
        vsnprintf(P->err + n, sizeof(P->err) - n, fmt, ap);
        va_end(ap);
    }
}

static clc_token *tk(Parser *P) { return &P->L->toks[P->L->pos]; }
static clc_token *tkn(Parser *P, int k) {
    int i = P->L->pos + k;
    if (i >= P->L->ntoks) i = P->L->ntoks - 1;
    return &P->L->toks[i];
}
static void adv(Parser *P) { if (P->L->pos < P->L->ntoks - 1) P->L->pos++; }
static int  is_punct(Parser *P, int p) { return tk(P)->kind == TK_PUNCT && tk(P)->punct == p; }
static int  is_ident(Parser *P, const char *s) {
    return tk(P)->kind == TK_IDENT && !strcmp(tk(P)->text, s);
}
static int  peek_punct(Parser *P, int k, int p) {
    clc_token *t = tkn(P, k);
    return t->kind == TK_PUNCT && t->punct == p;
}
static int eat_punct(Parser *P, int p) { if (is_punct(P, p)) { adv(P); return 1; } return 0; }
static int expect_punct(Parser *P, int p, const char *what) {
    if (!eat_punct(P, p)) { perr(P, "expected %s", what); return 0; }
    return 1;
}
static char *expect_ident(Parser *P, const char *what) {
    if (tk(P)->kind != TK_IDENT) { perr(P, "expected %s", what); return NULL; }
    {
        char *s = tk(P)->text;
        adv(P);
        return s;
    }
}
static char *dupstr(const char *s) { return s ? strdup(s) : NULL; }

/* ---- name tables --------------------------------------------------------- */
static int scalar_from_name(const char *s, clc_base *b)
{
    if (!strcmp(s,"char")||!strcmp(s,"int8_t"))    { *b=CLC_CHAR; return 1; }
    if (!strcmp(s,"uchar")||!strcmp(s,"uint8_t"))  { *b=CLC_UCHAR; return 1; }
    if (!strcmp(s,"short")||!strcmp(s,"int16_t"))  { *b=CLC_SHORT; return 1; }
    if (!strcmp(s,"ushort")||!strcmp(s,"uint16_t")){ *b=CLC_USHORT; return 1; }
    if (!strcmp(s,"int")||!strcmp(s,"int32_t"))    { *b=CLC_INT; return 1; }
    if (!strcmp(s,"uint")||!strcmp(s,"uint32_t"))  { *b=CLC_UINT; return 1; }
    if (!strcmp(s,"long")||!strcmp(s,"int64_t"))   { *b=CLC_LONG; return 1; }
    if (!strcmp(s,"ulong")||!strcmp(s,"uint64_t")) { *b=CLC_ULONG; return 1; }
    if (!strcmp(s,"float"))                        { *b=CLC_FLOAT; return 1; }
    if (!strcmp(s,"double"))                       { *b=CLC_DOUBLE; return 1; }
    if (!strcmp(s,"half"))                         { *b=CLC_HALF; return 1; }
    if (!strcmp(s,"bool"))                         { *b=CLC_BOOL; return 1; }
    if (!strcmp(s,"void"))                         { *b=CLC_VOID; return 1; }
    if (!strcmp(s,"size_t"))                       { *b=CLC_SIZE_T; return 1; }
    if (!strcmp(s,"ptrdiff_t"))                    { *b=CLC_PTRDIFF_T; return 1; }
    if (!strcmp(s,"intptr_t"))                     { *b=CLC_LONG; return 1; }
    if (!strcmp(s,"uintptr_t"))                    { *b=CLC_ULONG; return 1; }
    if (!strcmp(s,"sampler_t"))                    { *b=CLC_SAMPLER_T; return 1; }
    if (!strcmp(s,"image2d_t"))                    { *b=CLC_IMAGE2D_T; return 1; }
    if (!strcmp(s,"image3d_t"))                    { *b=CLC_IMAGE3D_T; return 1; }
    return 0;
}

/* match a (possibly vector) base type name; returns 1 and fills *b,*v */
static int match_base_name(const char *s, clc_base *b, int *v)
{
    int len = (int)strlen(s);
    int digits = 0;
    while (digits < len && isdigit((unsigned char)s[len-1-digits])) digits++;
    if (digits > 0 && digits < len) {
        char pre[40];
        int vec;
        if (len - digits >= (int)sizeof(pre)) return 0;
        memcpy(pre, s, len - digits);
        pre[len - digits] = '\0';
        vec = atoi(s + (len - digits));
        if (vec != 2 && vec != 3 && vec != 4 && vec != 8 && vec != 16) return 0;
        if (!scalar_from_name(pre, b)) return 0;
        if (*b == CLC_VOID) return 0;
        *v = vec;
        return 1;
    }
    *v = 1;
    return scalar_from_name(s, b);
}

static int is_qualifier_ident(const char *s)
{
    static const char *q[] = {
        "__global","global","__local","local","__constant","constant",
        "__private","private","const","__const","restrict","__restrict",
        "volatile","__volatile","unsigned","signed","short","long","struct",
        "inline","__inline","static",NULL
    };
    int i;
    for (i = 0; q[i]; i++) if (!strcmp(s, q[i])) return 1;
    return 0;
}

static int lookup_typedef(Parser *P, const char *s, clc_type *out)
{
    int i;
    for (i = 0; i < P->ntdefs; i++)
        if (!strcmp(P->tdefs[i].name, s)) { *out = P->tdefs[i].type; return 1; }
    return 0;
}

static int lookup_const(Parser *P, const char *s, clc_const *out)
{
    int i;
    for (i = 0; i < P->nconsts; i++)
        if (!strcmp(P->consts[i].name, s)) { *out = P->consts[i]; return 1; }
    return 0;
}

static void add_const_int(Parser *P, const char *n, long long v)
{
    if (P->nconsts >= (int)(sizeof(P->consts)/sizeof(P->consts[0]))) return;
    P->consts[P->nconsts].name = dupstr(n);
    P->consts[P->nconsts].is_float = 0;
    P->consts[P->nconsts].ival = v;
    P->consts[P->nconsts].fval = (double)v;
    P->nconsts++;
}
static void add_const_flt(Parser *P, const char *n, double v)
{
    if (P->nconsts >= (int)(sizeof(P->consts)/sizeof(P->consts[0]))) return;
    P->consts[P->nconsts].name = dupstr(n);
    P->consts[P->nconsts].is_float = 1;
    P->consts[P->nconsts].ival = (long long)v;
    P->consts[P->nconsts].fval = v;
    P->nconsts++;
}

/* does the current token begin a type specifier? */
static int is_type_start(Parser *P)
{
    clc_token *t = tk(P);
    clc_base b; int v;
    clc_type td;
    if (t->kind != TK_IDENT) return 0;
    if (is_qualifier_ident(t->text)) return 1;
    if (lookup_typedef(P, t->text, &td)) return 1;
    if (match_base_name(t->text, &b, &v)) return 1;
    return 0;
}

/* ---- scope / symbol management ------------------------------------------ */
/* Symbol slots are bump-allocated and never reclaimed: AST nodes hold slot
 * indices, so closing a scope only hides the names (sym_visible), it does not
 * free the slots.  A sibling scope redeclaring a name gets a fresh slot. */
static void scope_enter(Parser *P) { P->scope[P->nscope++] = P->sym_visible; }
static void scope_leave(Parser *P) { P->sym_visible = P->scope[--P->nscope]; }

static int find_symbol(Parser *P, const char *name)
{
    int i;
    for (i = P->sym_visible - 1; i >= 0; i--)
        if (P->curfn->syms[i].name && !strcmp(P->curfn->syms[i].name, name))
            return i;
    return -1;
}

static int align_up(int v, int a) { return (v + a - 1) & ~(a - 1); }

static int add_symbol(Parser *P, const char *name, clc_type type, int is_array,
                      int byte_size, int is_param)
{
    clc_symbol *s;
    int idx, al, off;
    if (P->curfn->nsyms >= CLC_MAX_SYMS) { perr(P, "too many local symbols"); return -1; }
    idx = P->curfn->nsyms;
    s = &P->curfn->syms[idx];
    memset(s, 0, sizeof(*s));
    s->name = dupstr(name);
    s->type = type;
    s->is_array = is_array;
    s->is_param = is_param;
    s->param_index = is_param ? idx : -1;
    s->size = byte_size;
    al = clc_type_align(&type);
    if (al < 1) al = 1;
    if (!is_param && !type.pointer && type.as == CLC_AS_LOCAL) {
        /* statically-declared __local object: lives in the work-group block */
        off = align_up(P->local_off, al);
        s->offset = off;
        P->local_off = off + byte_size;
    } else {
        off = align_up(P->frame_off, al);
        s->offset = off;
        P->frame_off = off + byte_size;
    }
    P->curfn->nsyms++;
    P->sym_visible++;
    return idx;
}

/* ---- struct parsing ------------------------------------------------------ */
static clc_struct *register_struct(Parser *P, clc_struct *s)
{
    clc_struct **ns = (clc_struct **)realloc(P->prog->structs,
                                             sizeof(clc_struct *) * (P->prog->nstructs + 1));
    if (!ns) { perr(P, "out of memory"); return NULL; }
    P->prog->structs = ns;
    P->prog->structs[P->prog->nstructs++] = s;
    return s;
}

static clc_struct *find_struct(Parser *P, const char *name)
{
    int i;
    if (!name) return NULL;
    for (i = 0; i < P->prog->nstructs; i++)
        if (P->prog->structs[i]->name && !strcmp(P->prog->structs[i]->name, name))
            return P->prog->structs[i];
    return NULL;
}

/* forward decls for mutual recursion */
static int  parse_type(Parser *P, clc_type *out, int *is_const);
static clc_expr *parse_expr(Parser *P);
static clc_expr *parse_assignment(Parser *P);
static clc_expr *parse_unary_expr(Parser *P);
static clc_stmt *parse_statement(Parser *P);
static clc_stmt *parse_block(Parser *P);
static clc_type clc_builtin_result_type(Parser *P, int id, clc_expr **args, int nargs);
static clc_type clc_decode_convert_type(const char *suffix);
static clc_type clc_decode_vload_type(Parser *P, const char *name, clc_expr **args, int nargs);
static int  clc_find_global(Parser *P, const char *name);
static int  add_string_global(Parser *P, const char *text);

/* parse "struct [Name] { ... }" body; returns the struct (registered) */
static clc_struct *parse_struct_def(Parser *P, char *name)
{
    clc_struct *s = (clc_struct *)calloc(1, sizeof(*s));
    int off = 0, maxal = 1;
    if (!s) { perr(P, "out of memory"); return NULL; }
    s->name = name ? dupstr(name) : NULL;
    if (!expect_punct(P, P_LBRACE, "'{'")) { free(s); return NULL; }
    while (!P->has_err && !is_punct(P, P_RBRACE) && tk(P)->kind != TK_EOF) {
        clc_type ft; int is_const = 0;
        if (!parse_type(P, &ft, &is_const)) { perr(P, "bad struct field type"); free(s); return NULL; }
        do {
            char *fn = expect_ident(P, "field name");
            int fsize, al;
            if (!fn) { free(s); return NULL; }
            /* array field? */
            if (is_punct(P, P_LBRACK)) {
                adv(P);
                if (tk(P)->kind == TK_INT) {
                    int n = (int)tk(P)->ival; adv(P);
                    ft.pointer = 0;
                    fsize = clc_type_size(&ft) * n;
                } else { perr(P, "array field needs constant size"); fsize = clc_type_size(&ft); }
                expect_punct(P, P_RBRACK, "']'");
            } else {
                fsize = clc_type_size(&ft);
            }
            al = clc_type_align(&ft);
            if (al < 1) al = 1;
            if (al > maxal) maxal = al;
            off = align_up(off, al);
            if (s->nfields >= CLC_MAX_FIELDS) { perr(P, "too many fields"); }
            else {
                s->fields[s->nfields].name = dupstr(fn);
                s->fields[s->nfields].type = ft;
                s->fields[s->nfields].offset = off;
                s->fields[s->nfields].size = fsize;
                s->nfields++;
            }
            off += fsize;
        } while (eat_punct(P, P_COMMA));
        if (!expect_punct(P, P_SEMI, "';' after field")) break;
    }
    expect_punct(P, P_RBRACE, "'}'");
    s->size = align_up(off, maxal);
    s->align = maxal;
    return register_struct(P, s);
}

/* ---- type parsing -------------------------------------------------------- */
/* Returns 1 and fills *out if a type was parsed; 0 if the tokens do not begin
 * a type (peek position is unchanged in that case). */
static int parse_type(Parser *P, clc_type *out, int *is_const)
{
    clc_type t;
    clc_as as = CLC_AS_PRIVATE;
    int cconst = 0, sign = 0, nlong = 0, nshort = 0, have_base = 0, vec = 1;
    clc_base base = CLC_INT;
    clc_struct *str = NULL;
    int guard = 0;

    memset(&t, 0, sizeof(t));
    if (is_const) *is_const = 0;

    /* consume leading qualifiers */
    while (guard++ < 16 && tk(P)->kind == TK_IDENT) {
        const char *s = tk(P)->text;
        if (!strcmp(s,"__global")||!strcmp(s,"global")) { as=CLC_AS_GLOBAL; adv(P); continue; }
        if (!strcmp(s,"__local")||!strcmp(s,"local"))   { as=CLC_AS_LOCAL; adv(P); continue; }
        if (!strcmp(s,"__constant")||!strcmp(s,"constant")) { as=CLC_AS_CONSTANT; adv(P); continue; }
        if (!strcmp(s,"__private")||!strcmp(s,"private")) { as=CLC_AS_PRIVATE; adv(P); continue; }
        if (!strcmp(s,"const")||!strcmp(s,"__const"))   { cconst=1; adv(P); continue; }
        if (!strcmp(s,"restrict")||!strcmp(s,"__restrict")) { adv(P); continue; }
        if (!strcmp(s,"volatile")||!strcmp(s,"__volatile")) { adv(P); continue; }
        if (!strcmp(s,"inline")||!strcmp(s,"__inline")||!strcmp(s,"static")) { adv(P); continue; }
        if (!strcmp(s,"unsigned")) { sign=-1; adv(P); continue; }
        if (!strcmp(s,"signed"))   { sign=1; adv(P); continue; }
        if (!strcmp(s,"long"))     { nlong++; adv(P); continue; }
        if (!strcmp(s,"short"))    { nshort=1; adv(P); continue; }
        if (!strcmp(s,"struct")) {
            adv(P);
            {
                char *nm = (tk(P)->kind == TK_IDENT) ? tk(P)->text : NULL;
                if (nm) adv(P);
                if (is_punct(P, P_LBRACE)) {
                    str = parse_struct_def(P, nm);
                } else {
                    str = find_struct(P, nm);
                }
                if (!str) { perr(P, "unknown struct '%s'", nm ? nm : "?"); return 0; }
            }
            base = CLC_STRUCT; have_base = 1; vec = 1;
            break;
        }
        /* typedef name? */
        {
            clc_type td;
            if (lookup_typedef(P, s, &td)) {
                t = td;
                if (cconst && is_const) *is_const = 1;
                t.as = (t.pointer ? as : t.as);
                /* allow trailing '*' */
                while (is_punct(P, P_MUL)) {
                    adv(P);
                    t.pointer = 1;
                    t.as = as;
                    if (is_punct(P, P_MUL)) { perr(P, "multi-level pointers unsupported"); return 0; }
                }
                return 1;
            }
        }
        /* base name? */
        {
            clc_base b; int v;
            if (match_base_name(s, &b, &v)) { base=b; vec=v; have_base=1; adv(P); break; }
        }
        break;
    }

    if (!have_base) {
        if (nshort)      { base = (sign<0)?CLC_USHORT:CLC_SHORT; vec=1; have_base=1; }
        else if (nlong)  { base = (sign<0)?CLC_ULONG:CLC_LONG;   vec=1; have_base=1; }
        else if (sign)   { base = (sign<0)?CLC_UINT:CLC_INT;     vec=1; have_base=1; }
        else return 0;
    } else if (base != CLC_STRUCT) {
        /* apply sign modifiers to the recognised base */
        if (sign < 0) {
            if (base==CLC_CHAR) base=CLC_UCHAR; else if (base==CLC_SHORT) base=CLC_USHORT;
            else if (base==CLC_INT) base=CLC_UINT; else if (base==CLC_LONG) base=CLC_ULONG;
        } else if (sign > 0) {
            if (base==CLC_UCHAR) base=CLC_CHAR; else if (base==CLC_USHORT) base=CLC_SHORT;
            else if (base==CLC_UINT) base=CLC_INT; else if (base==CLC_ULONG) base=CLC_LONG;
        }
    }

    t.base = base;
    t.vec = vec;
    t.pointer = 0;
    t.as = as;
    t.str = str;

    /* trailing pointer stars and qualifiers */
    for (;;) {
        if (is_punct(P, P_MUL)) {
            adv(P);
            if (t.pointer) { perr(P, "multi-level pointers unsupported"); return 0; }
            t.pointer = 1;
            t.as = as;
            continue;
        }
        if (tk(P)->kind == TK_IDENT &&
            (!strcmp(tk(P)->text,"const")||!strcmp(tk(P)->text,"restrict")||
             !strcmp(tk(P)->text,"volatile")||!strcmp(tk(P)->text,"__restrict"))) {
            if (!strcmp(tk(P)->text,"const")) cconst = 1;
            adv(P);
            continue;
        }
        if (tk(P)->kind == TK_IDENT &&
            (!strcmp(tk(P)->text,"__global")||!strcmp(tk(P)->text,"global")||
             !strcmp(tk(P)->text,"__local")||!strcmp(tk(P)->text,"local")||
             !strcmp(tk(P)->text,"__constant")||!strcmp(tk(P)->text,"constant"))) {
            const char *s = tk(P)->text;
            as = (!strcmp(s,"__global")||!strcmp(s,"global")) ? CLC_AS_GLOBAL :
                 (!strcmp(s,"__local")||!strcmp(s,"local")) ? CLC_AS_LOCAL : CLC_AS_CONSTANT;
            t.as = as;
            adv(P);
            continue;
        }
        break;
    }
    if (is_const) *is_const = cconst;
    *out = t;
    return 1;
}


/* ---- expression helpers -------------------------------------------------- */
static clc_expr *new_expr(clc_ekind k)
{
    clc_expr *e = (clc_expr *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = k;
    e->field_index = -1;
    return e;
}

static clc_type zero_type(void) { clc_type t; memset(&t, 0, sizeof(t)); return t; }

static clc_type mk_scalar(clc_base b)
{
    clc_type t = zero_type();
    t.base = b; t.vec = 1;
    return t;
}

static int int_rank(clc_base b)
{
    switch (b) {
    case CLC_BOOL:   return 1;
    case CLC_CHAR: case CLC_UCHAR: return 2;
    case CLC_SHORT: case CLC_USHORT: return 3;
    case CLC_INT: case CLC_UINT: return 4;
    case CLC_LONG: case CLC_ULONG: return 5;
    default: return 0;
    }
}

static clc_type promote(clc_type t)
{
    if (!t.pointer && !clc_type_is_float(&t) && t.vec == 1) {
        if (t.base==CLC_BOOL||t.base==CLC_CHAR||t.base==CLC_UCHAR||
            t.base==CLC_SHORT||t.base==CLC_USHORT)
            t.base = CLC_INT;
    }
    return t;
}

static int is_unsigned_base(clc_base b)
{
    return b==CLC_UCHAR||b==CLC_USHORT||b==CLC_UINT||b==CLC_ULONG||b==CLC_BOOL;
}

/* usual arithmetic conversions for two operand types */
static clc_type bin_result(clc_type a, clc_type b)
{
    clc_type r;
    int vec = a.vec > b.vec ? a.vec : b.vec;
    if (a.pointer) return a;
    if (b.pointer) return b;
    memset(&r, 0, sizeof(r));
    r.vec = vec;
    if (clc_type_is_float(&a) || clc_type_is_float(&b)) {
        r.base = (a.base==CLC_DOUBLE||b.base==CLC_DOUBLE) ? CLC_DOUBLE : CLC_FLOAT;
        return r;
    }
    a = promote(a); b = promote(b);
    {
        int ra = int_rank(a.base), rb = int_rank(b.base);
        int au = is_unsigned_base(a.base), bu = is_unsigned_base(b.base);
        if (ra == rb) {
            r.base = (au||bu) ? (ra>=5?CLC_ULONG:CLC_UINT) : (ra>=5?CLC_LONG:CLC_INT);
        } else {
            clc_base hi = ra>rb ? a.base : b.base;
            int hiu   = ra>rb ? au : bu;
            int hirank= ra>rb ? ra : rb;
            int lorank= ra>rb ? rb : ra;
            if (hiu)               r.base = hi;
            else if (hirank>lorank)r.base = hi;
            else                   r.base = (hirank>=5)?CLC_ULONG:CLC_UINT;
        }
    }
    return r;
}

static clc_expr *mk_binop(Parser *P, clc_op op, clc_expr *a, clc_expr *b)
{
    clc_expr *e = new_expr(E_BINOP);
    if (!e) { perr(P, "out of memory"); return NULL; }
    e->op = op;
    e->a = a; e->b = b;
    if (op==OP_EQ||op==OP_NE||op==OP_LT||op==OP_LE||op==OP_GT||op==OP_GE||
        op==OP_LAND||op==OP_LOR) {
        int av = a ? a->type.vec : 1, bv = b ? b->type.vec : 1;
        clc_type r = mk_scalar(CLC_INT);
        r.vec = (av > bv) ? av : bv;   /* scalar <op> vector -> intN mask */
        if ((op==OP_LAND||op==OP_LOR)) r.vec = 1;
        e->type = r;
    } else {
        e->type = bin_result(a?a->type:zero_type(), b?b->type:zero_type());
    }
    return e;
}

/* ---- primary / postfix / unary ------------------------------------------ */
static clc_expr *parse_postfix(Parser *P, clc_expr *base);

static int parse_swizzle_components(const char *s, char *out, int *n)
{
    int i, len = (int)strlen(s);
    if (len < 1 || len > 4) return 0;
    for (i = 0; i < len; i++) {
        char c = s[i];
        int idx = -1;
        if (c=='x'||c=='r'||c=='s') idx = 0;
        else if (c=='y'||c=='g'||c=='t') idx = 1;
        else if (c=='z'||c=='b'||c=='p') idx = 2;
        else if (c=='w'||c=='a'||c=='q') idx = 3;
        else return 0;
        /* sN form handled separately */
        out[i] = (char)idx;
    }
    *n = len;
    return 1;
}

static int parse_swizzle_hex(const char *s, char *out, int *n)
{
    /* sN / sNM... where N in 0..f */
    int i, len = (int)strlen(s);
    if (len < 2 || s[0] != 's') return 0;
    len -= 1;
    if (len < 1 || len > 4) return 0;
    for (i = 0; i < len; i++) {
        char c = (char)tolower((unsigned char)s[1+i]);
        int v = isdigit((unsigned char)c) ? (c-'0') : (c-'a'+10);
        if (v < 0 || v > 15) return 0;
        out[i] = (char)v;
    }
    *n = len;
    return 1;
}

static clc_expr *parse_primary(Parser *P)
{
    clc_token *t = tk(P);

    if (t->kind == TK_INT) {
        clc_expr *e = new_expr(E_CONST_INT);
        e->li = t->ival;
        e->type = mk_scalar(t->is_long ? (t->is_unsigned?CLC_ULONG:CLC_LONG)
                                       : (t->is_unsigned?CLC_UINT:CLC_INT));
        if (!t->is_unsigned && !t->is_long && t->ival <= 2147483647LL)
            e->type = mk_scalar(CLC_INT);
        e->ld = (double)t->ival;
        adv(P);
        return e;
    }
    if (t->kind == TK_FLOAT) {
        clc_expr *e = new_expr(E_CONST_FLOAT);
        e->ld = t->fval;
        e->li = (long long)t->fval;
        e->type = mk_scalar(CLC_FLOAT);
        adv(P);
        return e;
    }
    if (t->kind == TK_STRING) {
        /* string literal -> a file-scope char[] constant; yields char* */
        int gi = add_string_global(P, t->text);
        clc_expr *e;
        adv(P);
        if (gi < 0) return NULL;
        e = new_expr(E_VAR);
        e->is_global = 1;
        e->gindex = gi;
        e->type = P->prog->globals[gi].type;
        e->type.pointer = 1;
        e->type.as = CLC_AS_CONSTANT;
        return parse_postfix(P, e);
    }
    if (is_punct(P, P_LPAREN)) {
        /* could be a parenthesised expr, a cast, or a compound literal */
        clc_type ty; int is_const = 0;
        int save = P->L->pos;
        adv(P); /* consume '(' */
        if (is_type_start(P) && parse_type(P, &ty, &is_const)) {
            /* we parsed a type: cast or compound literal */
            if (is_punct(P, P_LBRACE)) {
                clc_expr *e = new_expr(E_COMPOUND);
                adv(P);
                e->type = ty;
                if (!is_punct(P, P_RBRACE)) {
                    for (;;) {
                        clc_expr *v = parse_assignment(P);
                        if (!v) return NULL;
                        if (e->ninit < CLC_MAX_VEC) e->init[e->ninit++] = v;
                        if (!eat_punct(P, P_COMMA)) break;
                    }
                }
                expect_punct(P, P_RBRACE, "'}'");
                return parse_postfix(P, e);
            }
            expect_punct(P, P_RPAREN, "')'");
            if (ty.vec > 1 && is_punct(P, P_LPAREN)) {
                /* vector constructor: (float4)(a,b,c,d) */
                clc_expr *e = new_expr(E_COMPOUND);
                e->type = ty;
                adv(P);
                if (!is_punct(P, P_RPAREN)) {
                    for (;;) {
                        clc_expr *v = parse_assignment(P);
                        if (!v) return NULL;
                        if (e->ninit < CLC_MAX_VEC) e->init[e->ninit++] = v;
                        if (!eat_punct(P, P_COMMA)) break;
                    }
                }
                expect_punct(P, P_RPAREN, "')'");
                return parse_postfix(P, e);
            }
            {
                clc_expr *operand = parse_unary_expr(P);
                clc_expr *e;
                if (!operand) return NULL;
                e = new_expr(E_CAST);
                e->type = ty;
                e->a = operand;
                return parse_postfix(P, e);
            }
        }
        /* plain parenthesised expression */
        P->L->pos = save;
        adv(P); /* '(' */
        {
            clc_expr *e = parse_expr(P);
            if (!e) return NULL;
            expect_punct(P, P_RPAREN, "')'");
            return parse_postfix(P, e);
        }
    }
    if (t->kind == TK_IDENT) {
        const char *name = t->text;
        /* boolean / null constants */
        if (!strcmp(name,"true"))  { clc_expr*e=new_expr(E_CONST_INT); e->li=1; e->ld=1; e->type=mk_scalar(CLC_BOOL); adv(P); return e; }
        if (!strcmp(name,"false")) { clc_expr*e=new_expr(E_CONST_INT); e->li=0; e->ld=0; e->type=mk_scalar(CLC_BOOL); adv(P); return e; }
        if (!strcmp(name,"NULL"))  { clc_expr*e=new_expr(E_CONST_INT); e->li=0; e->ld=0; e->type=mk_scalar(CLC_LONG); e->type.pointer=1; adv(P); return e; }
        {
            clc_const c;
            if (lookup_const(P, name, &c)) {
                clc_expr *e;
                if (c.is_float) { e=new_expr(E_CONST_FLOAT); e->ld=c.fval; e->li=(long long)c.fval; e->type=mk_scalar(CLC_FLOAT); }
                else { e=new_expr(E_CONST_INT); e->li=c.ival; e->ld=(double)c.ival; e->type=mk_scalar(CLC_INT); }
                adv(P);
                return e;
            }
        }
        adv(P);
        /* function/builtin call? */
        if (is_punct(P, P_LPAREN)) {
            clc_expr *e = new_expr(E_CALL);
            e->name = dupstr(name);
            e->type = mk_scalar(CLC_INT); /* refined below / by fixup */
            adv(P); /* '(' */
            if (!is_punct(P, P_RPAREN)) {
                for (;;) {
                    clc_expr *a = parse_assignment(P);
                    if (!a) return NULL;
                    if (e->nargs < CLC_MAX_ARGS) e->args[e->nargs++] = a;
                    else perr(P, "too many call arguments");
                    if (!eat_punct(P, P_COMMA)) break;
                }
            }
            expect_punct(P, P_RPAREN, "')'");
            /* resolve builtin vs user function */
            {
                int id = clc_builtin_lookup(name);
                if (id != BI_NONE) {
                    e->is_builtin = 1;
                    e->builtin_id = id;
                    e->type = clc_builtin_result_type(P, id, e->args, e->nargs);
                } else if (!strncmp(name, "convert_", 8)) {
                    e->is_builtin = 1; e->builtin_id = BI_CONVERT;
                    e->type = clc_decode_convert_type(name + 8);
                } else if (!strncmp(name, "as_", 3)) {
                    e->is_builtin = 1; e->builtin_id = BI_AS;
                    e->type = clc_decode_convert_type(name + 3);
                } else if (!strncmp(name, "vload", 5)) {
                    e->is_builtin = 1; e->builtin_id = BI_VLOAD;
                    e->type = clc_decode_vload_type(P, name, e->args, e->nargs);
                } else if (!strncmp(name, "vstore", 6)) {
                    e->is_builtin = 1; e->builtin_id = BI_VSTORE;
                    e->type = mk_scalar(CLC_VOID);
                } else if (!strcmp(name, "printf")) {
                    e->is_builtin = 1; e->builtin_id = BI_PRINTF;
                    e->type = mk_scalar(CLC_INT);
                } else {
                    clc_function *f = clc_program_find_func(P->prog, name);
                    if (f) { e->callee = f; e->type = f->ret; }
                    /* else: resolved in the fixup pass (forward reference) */
                }
            }
            return parse_postfix(P, e);
        }
        /* variable reference */
        {
            int si = find_symbol(P, name);
            clc_expr *e = new_expr(E_VAR);
            if (si >= 0) {
                e->sym = si;
                e->type = P->curfn->syms[si].type;
                if (P->curfn->syms[si].is_array) {
                    /* array decays to a private pointer to its frame storage */
                    e->type = P->curfn->syms[si].type;
                    e->type.pointer = 1;
                    e->type.as = CLC_AS_PRIVATE;
                }
            } else {
                int gi = clc_find_global(P, name);
                if (gi >= 0) {
                    e->is_global = 1;
                    e->gindex = gi;
                    e->type = P->prog->globals[gi].type;
                    if (P->prog->globals[gi].is_array) {
                        e->type.pointer = 1;
                        e->type.as = CLC_AS_PRIVATE;
                    }
                } else {
                    perr(P, "undeclared identifier '%s'", name);
                    return NULL;
                }
            }
            return parse_postfix(P, e);
        }
    }
    perr(P, "unexpected token in expression");
    return NULL;
}

static clc_expr *parse_postfix(Parser *P, clc_expr *base)
{
    for (;;) {
        if (is_punct(P, P_LBRACK)) {
            clc_expr *idx, *e;
            adv(P);
            idx = parse_expr(P);
            expect_punct(P, P_RBRACK, "']'");
            e = new_expr(E_INDEX);
            e->a = base; e->b = idx;
            /* result type = pointee type */
            if (base->type.pointer) {
                e->type = base->type; e->type.pointer = 0;
            } else if (base->type.base == CLC_STRUCT) {
                e->type = base->type; /* struct array element */
            } else {
                e->type = base->type; /* array of scalar: element type */
            }
            base = e;
            continue;
        }
        if (is_punct(P, P_DOT) || is_punct(P, P_ARROW)) {
            int arrow = is_punct(P, P_ARROW);
            char *fld;
            adv(P);
            fld = expect_ident(P, "field/component name");
            if (!fld) return NULL;
            if (arrow) {
                clc_expr *d = new_expr(E_UNOP);
                d->op = OP_DEREF; d->a = base;
                d->type = base->type; d->type.pointer = 0;
                base = d;
            }
            if (base->type.base == CLC_STRUCT && base->type.str) {
                clc_expr *e = new_expr(E_FIELD);
                int i;
                e->a = base; e->name = dupstr(fld);
                e->field_index = -1;
                for (i = 0; i < base->type.str->nfields; i++)
                    if (!strcmp(base->type.str->fields[i].name, fld)) { e->field_index = i; break; }
                if (e->field_index < 0) { perr(P, "no such field '%s'", fld); return NULL; }
                e->type = base->type.str->fields[i].type;
                base = e;
                continue;
            }
            /* vector swizzle / component */
            {
                clc_expr *e = new_expr(E_SWIZZLE);
                char comps[4]; int n = 0;
                e->a = base;
                if (parse_swizzle_hex(fld, comps, &n) || parse_swizzle_components(fld, comps, &n)) {
                    int i;
                    for (i = 0; i < n; i++) e->swizzle[i] = comps[i];
                    e->nswiz = n;
                    e->type = base->type;
                    e->type.vec = n;
                    if (n == 1) e->type.vec = 1;
                } else {
                    perr(P, "bad swizzle '%s'", fld);
                    return NULL;
                }
                base = e;
                continue;
            }
        }
        if (is_punct(P, P_INC) || is_punct(P, P_DEC)) {
            clc_expr *e = new_expr(E_POSTOP);
            e->op = is_punct(P, P_INC) ? OP_POST_INC : OP_POST_DEC;
            e->a = base;
            e->type = base->type;
            adv(P);
            base = e;
            continue;
        }
        break;
    }
    return base;
}

static clc_expr *parse_unary_expr(Parser *P)
{
    if (is_punct(P, P_SUB) || is_punct(P, P_ADD) || is_punct(P, P_NOT) ||
        is_punct(P, P_BNOT) || is_punct(P, P_MUL) || is_punct(P, P_AND) ||
        is_punct(P, P_INC) || is_punct(P, P_DEC)) {
        int p = tk(P)->punct;
        clc_expr *operand, *e;
        adv(P);
        operand = parse_unary_expr(P);
        if (!operand) return NULL;
        if (p == P_ADD) return operand; /* unary + is a no-op */
        e = new_expr(E_UNOP);
        switch (p) {
        case P_SUB: e->op = OP_NEG;  e->type = operand->type; break;
        case P_NOT: e->op = OP_LNOT; e->type = mk_scalar(CLC_INT); e->type.vec = operand->type.vec>1?operand->type.vec:1; break;
        case P_BNOT: e->op = OP_BNOT; e->type = operand->type; break;
        case P_MUL: e->op = OP_DEREF; e->type = operand->type; e->type.pointer = 0; break;
        case P_AND: e->op = OP_ADDR;
                    e->type = operand->type;
                    if (!e->type.pointer) { e->type.pointer = 1; e->type.as = CLC_AS_PRIVATE; }
                    break;
        case P_INC: e->op = OP_PRE_INC; e->type = operand->type; break;
        case P_DEC: e->op = OP_PRE_DEC; e->type = operand->type; break;
        }
        e->a = operand;
        return e;
    }
    {
        clc_expr *base = parse_primary(P);
        if (!base) return NULL;
        return base; /* parse_primary already ran postfix for idents */
    }
}

static clc_expr *parse_mul(Parser *P)
{
    clc_expr *a = parse_unary_expr(P);
    while (a && (is_punct(P,P_MUL)||is_punct(P,P_DIV)||is_punct(P,P_MOD))) {
        clc_op op = is_punct(P,P_MUL)?OP_MUL:(is_punct(P,P_DIV)?OP_DIV:OP_MOD);
        clc_expr *b; adv(P);
        b = parse_unary_expr(P);
        if (!b) return NULL;
        a = mk_binop(P, op, a, b);
    }
    return a;
}
static clc_expr *parse_add(Parser *P)
{
    clc_expr *a = parse_mul(P);
    while (a && (is_punct(P,P_ADD)||is_punct(P,P_SUB))) {
        clc_op op = is_punct(P,P_ADD)?OP_ADD:OP_SUB;
        clc_expr *b; adv(P);
        b = parse_mul(P);
        if (!b) return NULL;
        a = mk_binop(P, op, a, b);
    }
    return a;
}
static clc_expr *parse_shift(Parser *P)
{
    clc_expr *a = parse_add(P);
    while (a && (is_punct(P,P_SHL)||is_punct(P,P_SHR))) {
        clc_op op = is_punct(P,P_SHL)?OP_SHL:OP_SHR;
        clc_expr *b; adv(P);
        b = parse_add(P);
        if (!b) return NULL;
        a = mk_binop(P, op, a, b);
        if (a) a->type = promote(a->a->type), a->type.pointer=0;
    }
    return a;
}
static clc_expr *parse_rel(Parser *P)
{
    clc_expr *a = parse_shift(P);
    while (a && (is_punct(P,P_LT)||is_punct(P,P_GT)||is_punct(P,P_LE)||is_punct(P,P_GE))) {
        clc_op op = is_punct(P,P_LT)?OP_LT:(is_punct(P,P_GT)?OP_GT:(is_punct(P,P_LE)?OP_LE:OP_GE));
        clc_expr *b; adv(P);
        b = parse_shift(P);
        if (!b) return NULL;
        a = mk_binop(P, op, a, b);
    }
    return a;
}
static clc_expr *parse_eq(Parser *P)
{
    clc_expr *a = parse_rel(P);
    while (a && (is_punct(P,P_EQ)||is_punct(P,P_NE))) {
        clc_op op = is_punct(P,P_EQ)?OP_EQ:OP_NE;
        clc_expr *b; adv(P);
        b = parse_rel(P);
        if (!b) return NULL;
        a = mk_binop(P, op, a, b);
    }
    return a;
}
static clc_expr *parse_bitand(Parser *P)
{
    clc_expr *a = parse_eq(P);
    while (a && is_punct(P,P_AND)) { clc_expr*b; adv(P); b=parse_eq(P); if(!b)return NULL; a=mk_binop(P,OP_BAND,a,b); }
    return a;
}
static clc_expr *parse_bitxor(Parser *P)
{
    clc_expr *a = parse_bitand(P);
    while (a && is_punct(P,P_XOR)) { clc_expr*b; adv(P); b=parse_bitand(P); if(!b)return NULL; a=mk_binop(P,OP_BXOR,a,b); }
    return a;
}
static clc_expr *parse_bitor(Parser *P)
{
    clc_expr *a = parse_bitxor(P);
    while (a && is_punct(P,P_OR)) { clc_expr*b; adv(P); b=parse_bitxor(P); if(!b)return NULL; a=mk_binop(P,OP_BOR,a,b); }
    return a;
}
static clc_expr *parse_land(Parser *P)
{
    clc_expr *a = parse_bitor(P);
    while (a && is_punct(P,P_LAND)) { clc_expr*b; adv(P); b=parse_bitor(P); if(!b)return NULL; a=mk_binop(P,OP_LAND,a,b); }
    return a;
}
static clc_expr *parse_lor(Parser *P)
{
    clc_expr *a = parse_land(P);
    while (a && is_punct(P,P_LOR)) { clc_expr*b; adv(P); b=parse_land(P); if(!b)return NULL; a=mk_binop(P,OP_LOR,a,b); }
    return a;
}

static clc_expr *parse_ternary(Parser *P)
{
    clc_expr *c = parse_lor(P);
    if (c && is_punct(P, P_QUESTION)) {
        clc_expr *e, *a, *b;
        adv(P);
        a = parse_expr(P);
        expect_punct(P, P_COLON, "':'");
        b = parse_ternary(P);
        if (!a || !b) return NULL;
        e = new_expr(E_TERNARY);
        e->a = c; e->b = a; e->c = b;
        e->type = bin_result(a->type, b->type);
        return e;
    }
    return c;
}

static int assign_op_from_punct(int p, clc_op *op)
{
    switch (p) {
    case P_ASSIGN:   *op = OP_ASSIGN; return 1;
    case P_ADD_ASG:  *op = OP_ADD_ASG; return 1;
    case P_SUB_ASG:  *op = OP_SUB_ASG; return 1;
    case P_MUL_ASG:  *op = OP_MUL_ASG; return 1;
    case P_DIV_ASG:  *op = OP_DIV_ASG; return 1;
    case P_MOD_ASG:  *op = OP_MOD_ASG; return 1;
    case P_AND_ASG:  *op = OP_BAND_ASG; return 1;
    case P_OR_ASG:   *op = OP_BOR_ASG; return 1;
    case P_XOR_ASG:  *op = OP_BXOR_ASG; return 1;
    case P_SHL_ASG:  *op = OP_SHL_ASG; return 1;
    case P_SHR_ASG:  *op = OP_SHR_ASG; return 1;
    default: return 0;
    }
}

static clc_expr *parse_assignment(Parser *P)
{
    clc_expr *lhs = parse_ternary(P);
    if (lhs && tk(P)->kind == TK_PUNCT) {
        clc_op op;
        if (assign_op_from_punct(tk(P)->punct, &op)) {
            clc_expr *rhs, *e;
            adv(P);
            rhs = parse_assignment(P);
            if (!rhs) return NULL;
            e = new_expr(E_ASSIGN);
            e->op = op;
            e->a = lhs; e->b = rhs;
            e->type = lhs->type;
            return e;
        }
    }
    return lhs;
}

static clc_expr *parse_expr(Parser *P)
{
    return parse_assignment(P);
}


/* ---- builtin result-type helpers ---------------------------------------- */
static clc_type clc_decode_convert_type(const char *suffix)
{
    char buf[64];
    clc_base b; int v;
    char *us;
    snprintf(buf, sizeof(buf), "%s", suffix);
    /* strip rounding mode and _sat suffix */
    if ((us = strstr(buf, "_sat"))) *us = '\0';
    if ((us = strstr(buf, "_rtz"))) *us = '\0';
    if ((us = strstr(buf, "_rte"))) *us = '\0';
    if ((us = strstr(buf, "_rtp"))) *us = '\0';
    if ((us = strstr(buf, "_rtn"))) *us = '\0';
    if (match_base_name(buf, &b, &v)) {
        clc_type t = zero_type();
        t.base = b; t.vec = v;
        return t;
    }
    {
        clc_type t = zero_type();
        t.base = CLC_INT; t.vec = 1;
        return t;
    }
}

static int trailing_int(const char *s)
{
    int len = (int)strlen(s), d = 0, i, v = 0, mul = 1;
    while (d < len && isdigit((unsigned char)s[len-1-d])) d++;
    if (!d) return 1;
    for (i = len - d; i < len; i++) { v = v * 10 + (s[i]-'0'); }
    (void)mul;
    return v;
}

static clc_type clc_decode_vload_type(Parser *P, const char *name, clc_expr **args, int nargs)
{
    clc_type t = zero_type();
    int vec = trailing_int(name + 5);
    t.vec = vec;
    t.base = CLC_INT;
    if (strstr(name, "half")) {
        t.base = CLC_FLOAT;
    } else if (nargs >= 2 && args[1]) {
        clc_type pt = args[1]->type;
        t.base = pt.base;
        if (pt.base == CLC_VOID) t.base = CLC_INT;
    }
    (void)P;
    return t;
}

static clc_type clc_builtin_result_type(Parser *P, int id, clc_expr **args, int nargs)
{
    clc_type a0 = (nargs > 0 && args[0]) ? args[0]->type : mk_scalar(CLC_INT);
    clc_type r;
    (void)P;
    switch (id) {
    /* scalar reductions */
    case BI_DOT: case BI_LENGTH: case BI_DISTANCE: case BI_FAST_LENGTH:
    case BI_FAST_DISTANCE:
        r = a0; r.vec = 1; return r;
    /* predicates -> scalar int */
    case BI_ANY: case BI_ALL:
        return mk_scalar(CLC_INT);
    /* per-lane relational -> intN */
    case BI_ISEQUAL: case BI_ISNOTEQUAL: case BI_ISGREATER: case BI_ISGREATEREQUAL:
    case BI_ISLESS: case BI_ISLESSEQUAL: case BI_ISLESSGREATER: case BI_ISFINITE:
    case BI_ISINF: case BI_ISNAN: case BI_ISNORMAL: case BI_ISORDERED:
    case BI_ISUNORDERED: case BI_SIGNBIT:
        r = mk_scalar(CLC_INT); r.vec = a0.vec; return r;
    /* work-item ids -> size_t scalar */
    case BI_GET_GLOBAL_ID: case BI_GET_GLOBAL_SIZE: case BI_GET_GLOBAL_OFFSET:
    case BI_GET_LOCAL_ID: case BI_GET_LOCAL_SIZE: case BI_GET_GROUP_ID:
    case BI_GET_NUM_GROUPS:
        return mk_scalar(CLC_SIZE_T);
    case BI_GET_WORK_DIM:
        return mk_scalar(CLC_INT);
    case BI_BARRIER: case BI_VSTORE:
        return mk_scalar(CLC_VOID);
    /* select/bitselect -> the value arg type */
    case BI_SELECT: case BI_BITSELECT:
        return (nargs > 0 && args[0]) ? args[0]->type : a0;
    /* everything else: result shape follows the widest argument.  OpenCL-C
     * broadcasts scalar operands against vector ones, so a mixed call such as
     * fma(float, float4, float4) yields float4 -- not the first arg's scalar
     * type.  When args[0] is already the widest this returns args[0] unchanged,
     * so all-scalar and first-arg-is-vector calls behave exactly as before. */
    default: {
        int i;
        r = a0;
        for (i = 1; i < nargs; i++)
            if (args[i] && args[i]->type.vec > r.vec)
                r = args[i]->type;
        return r;
    }
    }
}

/* ---- file-scope globals -------------------------------------------------- */
static int clc_find_global(Parser *P, const char *name)
{
    int i;
    for (i = 0; i < P->prog->nglobals; i++)
        if (!strcmp(P->prog->globals[i].name, name))
            return i;
    return -1;
}

/* Add a file-scope char[] holding a string literal; returns its global index. */
static int add_string_global(Parser *P, const char *text)
{
    clc_global *ng;
    clc_type t;
    int idx, len = (int)strlen(text) + 1;
    char nm[32];
    ng = (clc_global *)realloc(P->prog->globals, sizeof(clc_global) * (P->prog->nglobals + 1));
    if (!ng) { perr(P, "out of memory"); return -1; }
    P->prog->globals = ng;
    idx = P->prog->nglobals++;
    memset(&t, 0, sizeof(t));
    t.base = CLC_CHAR; t.vec = 1; t.as = CLC_AS_CONSTANT;
    snprintf(nm, sizeof(nm), "__str%d", idx);
    memset(&P->prog->globals[idx], 0, sizeof(clc_global));
    P->prog->globals[idx].name = dupstr(nm);
    P->prog->globals[idx].type = t;
    P->prog->globals[idx].size = len;
    P->prog->globals[idx].is_array = 1;
    P->prog->globals[idx].data = calloc(1, len);
    memcpy(P->prog->globals[idx].data, text, len);
    return idx;
}

static int const_int(clc_expr *e, long long *out)
{
    if (!e) return 0;
    if (e->kind == E_CONST_INT) { *out = e->li; return 1; }
    if (e->kind == E_CONST_FLOAT) { *out = (long long)e->ld; return 1; }
    if (e->kind == E_UNOP && e->op == OP_NEG) { long long v; if (const_int(e->a,&v)){*out=-v;return 1;} }
    return 0;
}
static int const_flt(clc_expr *e, double *out)
{
    if (!e) return 0;
    if (e->kind == E_CONST_FLOAT) { *out = e->ld; return 1; }
    if (e->kind == E_CONST_INT) { *out = (double)e->li; return 1; }
    if (e->kind == E_UNOP && e->op == OP_NEG) { double v; if (const_flt(e->a,&v)){*out=-v;return 1;} }
    return 0;
}

static void store_const_scalar(void *dst, clc_type t, clc_expr *e)
{
    double dv; long long iv;
    int gotf = const_flt(e, &dv);
    int goti = const_int(e, &iv);
    if (clc_type_is_float(&t)) {
        double v = gotf ? dv : 0.0;
        if (t.base == CLC_FLOAT) { float f = (float)v; memcpy(dst,&f,4); }
        else { memcpy(dst,&v,8); }
    } else {
        long long v = goti ? iv : (long long)(gotf?dv:0);
        switch (clc_type_size(&t)) {
        case 1: { signed char c=(signed char)v; memcpy(dst,&c,1); break; }
        case 2: { short s=(short)v; memcpy(dst,&s,2); break; }
        case 4: { int i=(int)v; memcpy(dst,&i,4); break; }
        default: { memcpy(dst,&v,8); break; }
        }
    }
}

static void fill_global(Parser *P, clc_global *g, clc_expr *init)
{
    if (!init) return;
    if (init->kind == E_COMPOUND) {
        int i, esz = clc_type_size(&g->type);
        for (i = 0; i < init->ninit && i*esz < g->size; i++)
            store_const_scalar((char*)g->data + i*esz, g->type, init->init[i]);
    } else {
        store_const_scalar(g->data, g->type, init);
    }
    (void)P;
}

static int add_global(Parser *P, const char *name, clc_type t, int is_array,
                      int byte_size, clc_expr *init)
{
    clc_global *ng;
    int idx;
    if (P->prog->nglobals >= CLC_MAX_GLOBALS) { perr(P, "too many globals"); return -1; }
    ng = (clc_global *)realloc(P->prog->globals, sizeof(clc_global) * (P->prog->nglobals + 1));
    if (!ng) { perr(P, "out of memory"); return -1; }
    P->prog->globals = ng;
    idx = P->prog->nglobals++;
    memset(&P->prog->globals[idx], 0, sizeof(clc_global));
    P->prog->globals[idx].name = dupstr(name);
    P->prog->globals[idx].type = t;
    P->prog->globals[idx].size = byte_size;
    P->prog->globals[idx].is_array = is_array;
    P->prog->globals[idx].data = calloc(1, byte_size > 0 ? byte_size : 1);
    fill_global(P, &P->prog->globals[idx], init);
    return idx;
}

/* ---- statements ---------------------------------------------------------- */
static clc_stmt *new_stmt(clc_skind k)
{
    clc_stmt *s = (clc_stmt *)calloc(1, sizeof(*s));
    if (s) s->kind = k;
    return s;
}

static void block_add(clc_stmt *block, clc_stmt *s)
{
    clc_stmt **nb;
    int cap = block->nbody ? (block->nbody * 2) : 8;
    nb = (clc_stmt **)realloc(block->body, sizeof(clc_stmt *) * cap);
    if (!nb) return;
    block->body = nb;
    block->body[block->nbody++] = s;
}

static clc_stmt *parse_local_decl(Parser *P, clc_type base, int is_const)
{
    clc_stmt *decls[64];
    int count = 0;
    (void)is_const;
    for (;;) {
        clc_type t = base;
        char *name;
        int is_array = 0, alen = 0, elem, bytesz, sym;
        clc_stmt *s;
        while (is_punct(P, P_MUL)) {
            adv(P);
            if (t.pointer) { perr(P, "multi-level pointers unsupported"); return NULL; }
            t.pointer = 1;
        }
        name = expect_ident(P, "variable name");
        if (!name) return NULL;
        if (eat_punct(P, P_LBRACK)) {
            is_array = 1;
            if (tk(P)->kind == TK_INT) { alen = (int)tk(P)->ival; adv(P); }
            else { perr(P, "array size must be a constant"); }
            expect_punct(P, P_RBRACK, "']'");
        }
        elem = clc_type_size(&t);
        if (elem <= 0) elem = 1;
        bytesz = is_array ? elem * alen : elem;
        sym = add_symbol(P, name, t, is_array, bytesz, 0);
        if (sym < 0) return NULL;
        s = new_stmt(S_DECL);
        s->type = t; s->name = dupstr(name); s->sym = sym;
        s->is_array = is_array; s->array_len = alen;
        if (eat_punct(P, P_ASSIGN)) {
            if (is_punct(P, P_LBRACE)) {
                clc_expr *e = new_expr(E_COMPOUND);
                e->type = t;
                adv(P);
                if (!is_punct(P, P_RBRACE)) {
                    for (;;) {
                        clc_expr *v = parse_assignment(P);
                        if (!v) return NULL;
                        if (e->ninit < CLC_MAX_VEC) e->init[e->ninit++] = v;
                        if (!eat_punct(P, P_COMMA)) break;
                    }
                }
                expect_punct(P, P_RBRACE, "'}'");
                s->init = e;
            } else {
                s->init = parse_assignment(P);
            }
        }
        if (count < 64) decls[count++] = s;
        if (eat_punct(P, P_COMMA)) continue;
        break;
    }
    expect_punct(P, P_SEMI, "';'");
    if (count == 1) return decls[0];
    {
        clc_stmt *b = new_stmt(S_BLOCK);
        int i;
        for (i = 0; i < count; i++) block_add(b, decls[i]);
        return b;
    }
}

static clc_stmt *parse_block(Parser *P)
{
    clc_stmt *b = new_stmt(S_BLOCK);
    scope_enter(P);
    expect_punct(P, P_LBRACE, "'{'");
    while (!P->has_err && !is_punct(P, P_RBRACE) && tk(P)->kind != TK_EOF) {
        clc_stmt *s = parse_statement(P);
        if (!s) { if (!P->has_err) perr(P, "bad statement"); break; }
        block_add(b, s);
    }
    expect_punct(P, P_RBRACE, "'}'");
    scope_leave(P);
    return b;
}

static clc_stmt *parse_statement(Parser *P)
{
    if (P->has_err) return NULL;
    if (is_punct(P, P_LBRACE)) return parse_block(P);
    if (is_punct(P, P_SEMI)) { adv(P); return new_stmt(S_EXPR); }

    if (tk(P)->kind == TK_IDENT) {
        const char *w = tk(P)->text;
        if (!strcmp(w,"return")) {
            clc_stmt *s = new_stmt(S_RETURN); adv(P);
            if (!is_punct(P,P_SEMI)) s->expr = parse_expr(P);
            expect_punct(P,P_SEMI,"';'");
            return s;
        }
        if (!strcmp(w,"break"))    { adv(P); expect_punct(P,P_SEMI,"';'"); return new_stmt(S_BREAK); }
        if (!strcmp(w,"continue")) { adv(P); expect_punct(P,P_SEMI,"';'"); return new_stmt(S_CONTINUE); }
        if (!strcmp(w,"if")) {
            clc_stmt *s = new_stmt(S_IF); adv(P);
            expect_punct(P,P_LPAREN,"'('");
            s->cond = parse_expr(P);
            expect_punct(P,P_RPAREN,"')'");
            s->thenb = parse_statement(P);
            if (tk(P)->kind==TK_IDENT && !strcmp(tk(P)->text,"else")) { adv(P); s->elseb = parse_statement(P); }
            return s;
        }
        if (!strcmp(w,"while")) {
            clc_stmt *s = new_stmt(S_WHILE); adv(P);
            expect_punct(P,P_LPAREN,"'('");
            s->cond = parse_expr(P);
            expect_punct(P,P_RPAREN,"')'");
            s->thenb = parse_statement(P);
            return s;
        }
        if (!strcmp(w,"do")) {
            clc_stmt *s = new_stmt(S_DOWHILE); adv(P);
            s->thenb = parse_statement(P);
            if (!(tk(P)->kind==TK_IDENT && !strcmp(tk(P)->text,"while"))) { perr(P,"expected 'while'"); return s; }
            adv(P);
            expect_punct(P,P_LPAREN,"'('");
            s->cond = parse_expr(P);
            expect_punct(P,P_RPAREN,"')'");
            expect_punct(P,P_SEMI,"';'");
            return s;
        }
        if (!strcmp(w,"for")) {
            clc_stmt *s = new_stmt(S_FOR); adv(P);
            expect_punct(P,P_LPAREN,"'('");
            if (is_punct(P,P_SEMI)) { adv(P); }
            else if (is_type_start(P)) {
                clc_type t; int c=0;
                if (!parse_type(P,&t,&c)) { perr(P,"bad for-init type"); return NULL; }
                s->sinit = parse_local_decl(P,t,c);
            } else {
                s->sinit = new_stmt(S_EXPR);
                s->sinit->expr = parse_expr(P);
                expect_punct(P,P_SEMI,"';'");
            }
            if (!is_punct(P,P_SEMI)) s->scond = parse_expr(P);
            expect_punct(P,P_SEMI,"';'");
            if (!is_punct(P,P_RPAREN)) s->sinc = parse_expr(P);
            expect_punct(P,P_RPAREN,"')'");
            s->sbody = parse_statement(P);
            return s;
        }
        if (!strcmp(w,"switch")) {
            clc_stmt *s = new_stmt(S_SWITCH); adv(P);
            expect_punct(P,P_LPAREN,"'('");
            s->cond = parse_expr(P);
            expect_punct(P,P_RPAREN,"')'");
            s->thenb = parse_statement(P);
            return s;
        }
        if (!strcmp(w,"case")) {
            clc_stmt *s = new_stmt(S_CASE); adv(P);
            s->expr = parse_expr(P);
            expect_punct(P,P_COLON,"':'");
            return s;
        }
        if (!strcmp(w,"default")) {
            clc_stmt *s = new_stmt(S_DEFAULT); adv(P);
            expect_punct(P,P_COLON,"':'");
            return s;
        }
    }

    if (is_type_start(P)) {
        clc_type t; int c=0;
        if (!parse_type(P,&t,&c)) { perr(P,"bad declaration"); return NULL; }
        return parse_local_decl(P,t,c);
    }
    {
        clc_stmt *s = new_stmt(S_EXPR);
        s->expr = parse_expr(P);
        expect_punct(P,P_SEMI,"';'");
        return s;
    }
}

/* ---- attributes / helpers ----------------------------------------------- */
static void skip_balanced(Parser *P, int open, int close)
{
    int depth = 0;
    if (!is_punct(P, open)) return;
    for (;;) {
        if (is_punct(P, open)) depth++;
        else if (is_punct(P, close)) { depth--; if (depth==0){ adv(P); return; } }
        else if (tk(P)->kind==TK_EOF) return;
        adv(P);
    }
}

static int capture_reqd_wg(Parser *P, clc_function *f)
{
    /* __attribute__((reqd_work_group_size(x,y,z))) - best effort capture */
    (void)P; (void)f;
    return 0;
}

static void skip_attributes(Parser *P, clc_function *f)
{
    while (is_ident(P,"__attribute__") || is_ident(P,"__attribute")) {
        adv(P);
        capture_reqd_wg(P, f);
        skip_balanced(P, P_LPAREN, P_RPAREN);
    }
}

static void add_function(clc_program *prog, clc_function *f)
{
    clc_function **nf = (clc_function **)realloc(prog->funcs, sizeof(clc_function*)*(prog->nfuncs+1));
    if (!nf) return;
    prog->funcs = nf;
    prog->funcs[prog->nfuncs++] = f;
}

/* parse parameter list + body of a function whose return type and name are
 * already known (the '(' has NOT been consumed). */
static clc_function *parse_function_rest(Parser *P, int is_kernel, clc_type ret, char *name)
{
    clc_function *f = (clc_function *)calloc(1, sizeof(*f));
    if (!f) { perr(P, "out of memory"); return NULL; }
    f->name = dupstr(name);
    f->is_kernel = is_kernel;
    f->ret = ret;
    add_function(P->prog, f);   /* register before body: allows recursion */

    P->curfn = f;
    P->frame_off = 0;
    P->local_off = 0;
    P->sym_visible = 0;
    P->nscope = 0;

    expect_punct(P, P_LPAREN, "'('");
    if (is_ident(P,"void") && peek_punct(P,1,P_RPAREN)) {
        adv(P);
    } else if (!is_punct(P,P_RPAREN)) {
        for (;;) {
            clc_type pt; int c=0;
            char *pn;
            int sz;
            if (!parse_type(P,&pt,&c)) { perr(P,"bad parameter type"); break; }
            pn = (tk(P)->kind==TK_IDENT) ? expect_ident(P,"parameter name") : dupstr("_");
            sz = clc_type_size(&pt);
            if (sz <= 0) sz = (int)sizeof(void*);
            if (add_symbol(P, pn, pt, 0, sz, 1) < 0) break;
            f->nparams++;
            if (!eat_punct(P,P_COMMA)) break;
        }
    }
    expect_punct(P, P_RPAREN, "')'");
    skip_attributes(P, f);

    if (is_punct(P, P_SEMI)) {
        adv(P);   /* prototype only */
        f->body = NULL;
        P->curfn = NULL;
        return f;
    }
    f->body = parse_block(P);
    f->frame_size = align_up(P->frame_off, 16);
    f->local_mem_size = align_up(P->local_off, 16);
    P->curfn = NULL;
    return f;
}

/* ---- top level ----------------------------------------------------------- */
static void parse_typedef(Parser *P)
{
    clc_type t; int c=0;
    adv(P); /* 'typedef' */
    if (!parse_type(P,&t,&c)) { perr(P,"bad typedef"); return; }
    while (tk(P)->kind==TK_IDENT) {
        char *nm = expect_ident(P,"typedef name");
        if (!nm) return;
        if (P->ntdefs < 64) { P->tdefs[P->ntdefs].name = dupstr(nm); P->tdefs[P->ntdefs].type = t; P->ntdefs++; }
        if (is_punct(P,P_LBRACK)) { /* array typedef: treat element type */ adv(P); if(tk(P)->kind==TK_INT)adv(P); expect_punct(P,P_RBRACK,"]"); }
        if (!eat_punct(P,P_COMMA)) break;
    }
    expect_punct(P,P_SEMI,"';'");
}

static void parse_enum(Parser *P)
{
    long long auto_v = 0;
    adv(P); /* 'enum' */
    if (tk(P)->kind==TK_IDENT) adv(P); /* optional tag */
    if (!expect_punct(P,P_LBRACE,"'{'")) return;
    while (!P->has_err && !is_punct(P,P_RBRACE) && tk(P)->kind!=TK_EOF) {
        char *nm = expect_ident(P,"enumerator");
        long long v = auto_v;
        if (!nm) return;
        if (eat_punct(P,P_ASSIGN)) {
            clc_expr *e = parse_assignment(P);
            if (!const_int(e,&v)) { perr(P,"enum value must be constant"); v = auto_v; }
        }
        add_const_int(P, nm, v);
        auto_v = v + 1;
        if (!eat_punct(P,P_COMMA)) break;
    }
    expect_punct(P,P_RBRACE,"'}'");
    expect_punct(P,P_SEMI,"';'");
}

static void parse_global_var(Parser *P, clc_type base, char *firstname)
{
    /* base type parsed, first declarator name consumed */
    for (;;) {
        clc_type t = base;
        char *name = firstname;
        int is_array = 0, alen = 0, elem, bytesz;
        clc_expr *init = NULL;
        while (is_punct(P,P_MUL)) { adv(P); t.pointer = 1; }
        if (eat_punct(P,P_LBRACK)) {
            is_array = 1;
            if (tk(P)->kind==TK_INT) { alen=(int)tk(P)->ival; adv(P); }
            expect_punct(P,P_RBRACK,"']'");
        }
        elem = clc_type_size(&t); if (elem<=0) elem=1;
        bytesz = is_array ? elem*alen : elem;
        if (eat_punct(P,P_ASSIGN)) {
            if (is_punct(P,P_LBRACE)) {
                clc_expr *e = new_expr(E_COMPOUND); e->type = t; adv(P);
                if (!is_punct(P,P_RBRACE)) for(;;){ clc_expr*v=parse_assignment(P); if(!v)return; if(e->ninit<CLC_MAX_VEC)e->init[e->ninit++]=v; if(!eat_punct(P,P_COMMA))break; }
                expect_punct(P,P_RBRACE,"'}'");
                init = e;
            } else {
                init = parse_assignment(P);
            }
        }
        add_global(P, name, t, is_array, bytesz, init);
        firstname = NULL;
        if (eat_punct(P,P_COMMA)) {
            firstname = expect_ident(P,"global name");
            if (!firstname) return;
            continue;
        }
        break;
    }
    expect_punct(P,P_SEMI,"';'");
}

static void seed_constants(Parser *P)
{
    add_const_flt(P, "M_PI", 3.14159265358979323846);
    add_const_flt(P, "M_PI_F", 3.14159265358979323846f);
    add_const_flt(P, "M_E", 2.7182818284590452354);
    add_const_flt(P, "M_SQRT2", 1.41421356237309504880);
    add_const_flt(P, "INFINITY", 1e50);
    add_const_flt(P, "NAN", 0.0/0.0);
    add_const_int(P, "INT_MAX", 2147483647);
    add_const_int(P, "INT_MIN", (-2147483647-1));
    add_const_int(P, "UINT_MAX", 0xffffffff);
    add_const_int(P, "CHAR_MAX", 127);
    add_const_int(P, "CHAR_MIN", -128);
    add_const_int(P, "CLK_LOCAL_MEM_FENCE", 1);
    add_const_int(P, "CLK_GLOBAL_MEM_FENCE", 2);
}

static void skip_qualifier_idents(Parser *P, int *is_kernel)
{
    for (;;) {
        if (is_ident(P,"__kernel") || is_ident(P,"kernel")) { *is_kernel = 1; adv(P); continue; }
        if (is_ident(P,"__attribute__") || is_ident(P,"__attribute")) {
            adv(P);
            skip_balanced(P, P_LPAREN, P_RPAREN);
            continue;
        }
        break;
    }
}

/* ---- fixup pass: resolve forward function calls ------------------------- */
static void fixup_expr(clc_program *prog, clc_expr *e)
{
    int i;
    if (!e) return;
    fixup_expr(prog, e->a);
    fixup_expr(prog, e->b);
    fixup_expr(prog, e->c);
    for (i = 0; i < e->nargs; i++) fixup_expr(prog, e->args[i]);
    for (i = 0; i < e->ninit; i++) fixup_expr(prog, e->init[i]);
    if (e->kind == E_CALL && !e->is_builtin && !e->callee && e->name) {
        clc_function *f = clc_program_find_func(prog, e->name);
        if (f) { e->callee = f; e->type = f->ret; }
    }
}

static void fixup_stmt(clc_program *prog, clc_stmt *s)
{
    int i;
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        for (i = 0; i < s->nbody; i++) fixup_stmt(prog, s->body[i]);
        break;
    case S_DECL:   fixup_expr(prog, s->init); break;
    case S_EXPR:   fixup_expr(prog, s->expr); break;
    case S_RETURN: fixup_expr(prog, s->expr); break;
    case S_CASE:   fixup_expr(prog, s->expr); break;
    case S_IF:
        fixup_expr(prog, s->cond); fixup_stmt(prog, s->thenb); fixup_stmt(prog, s->elseb);
        break;
    case S_WHILE: case S_DOWHILE: case S_SWITCH:
        fixup_expr(prog, s->cond); fixup_stmt(prog, s->thenb);
        break;
    case S_FOR:
        fixup_stmt(prog, s->sinit); fixup_expr(prog, s->scond);
        fixup_expr(prog, s->sinc); fixup_stmt(prog, s->sbody);
        break;
    default: break;
    }
}

static void fixup_program(clc_program *prog)
{
    int i;
    for (i = 0; i < prog->nfuncs; i++)
        if (prog->funcs[i]->body)
            fixup_stmt(prog, prog->funcs[i]->body);
}

/* ---- public API ---------------------------------------------------------- */
clc_program *clc_parse(const char *source, char *errbuf, size_t errsz)
{
    clc_lexer lex;
    Parser P;
    clc_program *prog;

    if (errbuf && errsz) errbuf[0] = '\0';
    if (clc_lex(&lex, source) != 0) {
        if (errbuf) snprintf(errbuf, errsz, "%s", lex.err);
        clc_lex_free(&lex);
        return NULL;
    }

    memset(&P, 0, sizeof(P));
    P.L = &lex;
    prog = (clc_program *)calloc(1, sizeof(*prog));
    if (!prog) { clc_lex_free(&lex); return NULL; }
    P.prog = prog;
    seed_constants(&P);

    while (!P.has_err && tk(&P)->kind != TK_EOF) {
        int is_kernel = 0;
        clc_type ret; int c = 0;
        char *name;
        skip_qualifier_idents(&P, &is_kernel);
        if (P.has_err) break;
        if (tk(&P)->kind == TK_EOF) break;
        if (is_ident(&P,"typedef")) { parse_typedef(&P); continue; }
        if (is_ident(&P,"enum"))    { parse_enum(&P); continue; }
        if (is_ident(&P,"struct") && tkn(&P,1)->kind==TK_IDENT && peek_punct(&P,2,P_LBRACE)) {
            char *sn = tkn(&P,1)->text;
            adv(&P); adv(&P);   /* 'struct' name */
            parse_struct_def(&P, sn);
            /* a struct definition may be followed by variable declarations; if
             * the next token is ';' consume it, else fall through to decl. */
            if (is_punct(&P,P_SEMI)) { adv(&P); continue; }
            /* else: struct-defined type used for a following declaration */
            {
                clc_type st = zero_type();
                st.base = CLC_STRUCT; st.str = find_struct(&P, sn); st.vec = 1;
                if (tk(&P)->kind==TK_IDENT) {
                    name = expect_ident(&P,"name");
                    parse_global_var(&P, st, name);
                }
            }
            continue;
        }
        skip_attributes(&P, NULL);
        if (!is_type_start(&P)) { perr(&P, "unexpected token at top level"); break; }
        if (!parse_type(&P, &ret, &c)) { perr(&P, "bad top-level type"); break; }
        if (tk(&P)->kind != TK_IDENT) { perr(&P, "expected declarator name"); break; }
        name = expect_ident(&P, "declarator name");
        if (!name) break;
        if (is_punct(&P, P_LPAREN)) {
            parse_function_rest(&P, is_kernel, ret, name);
        } else {
            if (is_kernel) { perr(&P, "kernel '%s' has no parameter list", name); break; }
            parse_global_var(&P, ret, name);
        }
    }

    if (P.has_err) {
        if (errbuf) snprintf(errbuf, errsz, "%s", P.err);
        clc_program_free(prog);
        clc_lex_free(&lex);
        return NULL;
    }

    fixup_program(prog);
    clc_lex_free(&lex);
    return prog;
}

static void free_expr(clc_expr *e)
{
    int i;
    if (!e) return;
    free_expr(e->a); free_expr(e->b); free_expr(e->c);
    for (i = 0; i < e->nargs; i++) free_expr(e->args[i]);
    for (i = 0; i < e->ninit; i++) free_expr(e->init[i]);
    free(e->name);
    free(e);
}

static void free_stmt(clc_stmt *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < s->nbody; i++) free_stmt(s->body[i]);
    free(s->body);
    free_expr(s->init); free_expr(s->expr); free_expr(s->cond);
    free_expr(s->scond); free_expr(s->sinc);
    free_stmt(s->thenb); free_stmt(s->elseb);
    free_stmt(s->sinit); free_stmt(s->sbody);
    free(s->name);
    free(s);
}

static void free_function(clc_function *f)
{
    int i;
    if (!f) return;
    free_stmt(f->body);
    for (i = 0; i < f->nsyms; i++) free(f->syms[i].name);
    free(f->name);
    free(f);
}

void clc_program_free(clc_program *p)
{
    int i;
    if (!p) return;
    for (i = 0; i < p->nfuncs; i++) free_function(p->funcs[i]);
    free(p->funcs);
    for (i = 0; i < p->nstructs; i++) {
        int j;
        for (j = 0; j < p->structs[i]->nfields; j++) free(p->structs[i]->fields[j].name);
        free(p->structs[i]->name);
        free(p->structs[i]);
    }
    free(p->structs);
    for (i = 0; i < p->nglobals; i++) { free(p->globals[i].name); free(p->globals[i].data); }
    free(p->globals);
    free(p);
}

int clc_program_num_kernels(clc_program *p)
{
    int i, n = 0;
    if (!p) return 0;
    for (i = 0; i < p->nfuncs; i++) if (p->funcs[i]->is_kernel && p->funcs[i]->body) n++;
    return n;
}

clc_function *clc_program_kernel(clc_program *p, int i)
{
    int k, n = 0;
    if (!p) return NULL;
    for (k = 0; k < p->nfuncs; k++) {
        if (p->funcs[k]->is_kernel && p->funcs[k]->body) {
            if (n == i) return p->funcs[k];
            n++;
        }
    }
    return NULL;
}

clc_function *clc_program_find_kernel(clc_program *p, const char *name)
{
    int i;
    if (!p || !name) return NULL;
    for (i = 0; i < p->nfuncs; i++)
        if (p->funcs[i]->is_kernel && p->funcs[i]->body && !strcmp(p->funcs[i]->name, name))
            return p->funcs[i];
    return NULL;
}

clc_function *clc_program_find_func(clc_program *p, const char *name)
{
    int i;
    if (!p || !name) return NULL;
    for (i = 0; i < p->nfuncs; i++)
        if (p->funcs[i]->name && !strcmp(p->funcs[i]->name, name))
            return p->funcs[i];
    return NULL;
}
