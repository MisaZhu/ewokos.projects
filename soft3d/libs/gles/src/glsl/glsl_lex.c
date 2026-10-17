/* glsl_lex.c - tokenizer for the CPU GLSL ES 1.00 interpreter. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "glsl_lex.h"

typedef struct { const char *name; int kind; } kw;

static const kw k_keywords[] = {
    {"attribute", TK_ATTRIBUTE}, {"varying", TK_VARYING}, {"uniform", TK_UNIFORM},
    {"const", TK_CONST}, {"struct", TK_STRUCT},
    {"bool", TK_BOOL}, {"int", TK_INT_T}, {"float", TK_FLOAT_T}, {"void", TK_VOID_T},
    {"vec2", TK_VEC2}, {"vec3", TK_VEC3}, {"vec4", TK_VEC4},
    {"ivec2", TK_IVEC2}, {"ivec3", TK_IVEC3}, {"ivec4", TK_IVEC4},
    {"bvec2", TK_BVEC2}, {"bvec3", TK_BVEC3}, {"bvec4", TK_BVEC4},
    {"mat2", TK_MAT2}, {"mat3", TK_MAT3}, {"mat4", TK_MAT4},
    {"sampler2D", TK_SAMPLER2D}, {"samplerCube", TK_SAMPLERCUBE},
    {"true", TK_TRUE}, {"false", TK_FALSE},
    {"if", TK_IF}, {"else", TK_ELSE}, {"for", TK_FOR}, {"while", TK_WHILE},
    {"do", TK_DO}, {"break", TK_BREAK}, {"continue", TK_CONTINUE},
    {"return", TK_RETURN}, {"discard", TK_DISCARD},
    {"switch", TK_SWITCH}, {"case", TK_CASE}, {"default", TK_DEFAULT},
    {"in", TK_IN}, {"out", TK_OUT}, {"inout", TK_INOUT},
    {"precision", TK_PRECISION}, {"lowp", TK_LOWP}, {"mediump", TK_MEDIUMP},
    {"highp", TK_HIGHP},
    {NULL, 0}
};

static int kw_lookup(const char *s)
{
    int i;
    for (i = 0; k_keywords[i].name; ++i)
        if (strcmp(k_keywords[i].name, s) == 0)
            return k_keywords[i].kind;
    return TK_IDENT;
}

static void seterr(char *err, int errsz, int line, const char *msg)
{
    if (err && errsz > 0)
        snprintf(err, errsz, "glsl lex error (line %d): %s", line, msg);
}

#define PUSH() do { \
    if (n >= cap) { \
        cap = cap ? cap * 2 : 256; \
        toks = (glsl_token *)realloc(toks, cap * sizeof(glsl_token)); \
        if (!toks) { seterr(err, errsz, line, "out of memory"); return NULL; } \
    } \
    memset(&toks[n], 0, sizeof(toks[n])); \
    toks[n].line = line; \
} while (0)

glsl_token *glsl_lex(const char *src, int *count, char *err, int errsz)
{
    glsl_token *toks = NULL;
    int n = 0, cap = 0, line = 1;
    const char *p = src;

    if (err && errsz > 0) err[0] = '\0';
    if (!src) { seterr(err, errsz, 0, "null source"); return NULL; }

    for (;;) {
        /* whitespace + comments + preprocessor directives */
        while (*p) {
            if (*p == '\n') { line++; p++; }
            else if (isspace((unsigned char)*p)) p++;
            else if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; }
            else if (p[0] == '/' && p[1] == '*') {
                p += 2;
                while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
                if (*p) p += 2;
            }
            else if (*p == '#') {
                /* skip a whole preprocessor line (e.g. #version, #extension,
                 * #define).  Continuation lines are honoured. */
                while (*p) {
                    if (*p == '\\' && p[1] == '\n') { line++; p += 2; continue; }
                    if (*p == '\n') { line++; p++; break; }
                    p++;
                }
            }
            else break;
        }
        if (!*p) break;

        PUSH();

        /* identifier / keyword */
        if (isalpha((unsigned char)*p) || *p == '_') {
            int k = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && k < 63)
                toks[n].text[k++] = *p++;
            toks[n].text[k] = '\0';
            while (isalnum((unsigned char)*p) || *p == '_') p++; /* drain long id */
            toks[n].kind = kw_lookup(toks[n].text);
            if (toks[n].kind == TK_TRUE)  toks[n].ival = 1;
            if (toks[n].kind == TK_FALSE) toks[n].ival = 0;
            n++;
            continue;
        }

        /* number: int / float / hex */
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            char buf[64]; int k = 0; int isfloat = 0;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                buf[k++] = *p++; buf[k++] = *p++;
                while (isxdigit((unsigned char)*p) && k < 62) buf[k++] = *p++;
                buf[k] = '\0';
                toks[n].kind = TK_INT;
                toks[n].ival = (int)strtol(buf, NULL, 16);
                while (*p == 'u' || *p == 'U') p++;
                n++; continue;
            }
            while (k < 62) {
                char c = *p;
                if (isdigit((unsigned char)c)) { buf[k++] = c; p++; }
                else if (c == '.' || c == 'e' || c == 'E') {
                    if (c == '.') isfloat = 1;
                    if ((c == 'e' || c == 'E')) isfloat = 1;
                    buf[k++] = c; p++;
                    if ((c == 'e' || c == 'E') && (*p == '+' || *p == '-')) { buf[k++] = *p; p++; }
                }
                else break;
            }
            buf[k] = '\0';
            while (*p == 'f' || *p == 'F' || *p == 'h' || *p == 'H') { isfloat = 1; p++; }
            if (isfloat) { toks[n].kind = TK_FLOAT; toks[n].fval = (float)strtod(buf, NULL); }
            else         { toks[n].kind = TK_INT;   toks[n].ival = (int)strtol(buf, NULL, 10); }
            n++; continue;
        }

        /* operators / punctuation (longest match first) */
        {
            int kind = -1, adv = 0;
            char c0 = p[0], c1 = p[1], c2 = p[2];
            if (c0=='<' && c1=='<' && c2=='=') { kind=TK_SHL_ASSIGN; adv=3; }
            else if (c0=='>' && c1=='>' && c2=='=') { kind=TK_SHR_ASSIGN; adv=3; }
            else if (c0=='+' && c1=='+') { kind=TK_INC; adv=2; }
            else if (c0=='-' && c1=='-') { kind=TK_DEC; adv=2; }
            else if (c0=='+' && c1=='=') { kind=TK_ADD_ASSIGN; adv=2; }
            else if (c0=='-' && c1=='=') { kind=TK_SUB_ASSIGN; adv=2; }
            else if (c0=='*' && c1=='=') { kind=TK_MUL_ASSIGN; adv=2; }
            else if (c0=='/' && c1=='=') { kind=TK_DIV_ASSIGN; adv=2; }
            else if (c0=='%' && c1=='=') { kind=TK_MOD_ASSIGN; adv=2; }
            else if (c0=='&' && c1=='=') { kind=TK_AND_ASSIGN; adv=2; }
            else if (c0=='|' && c1=='=') { kind=TK_OR_ASSIGN; adv=2; }
            else if (c0=='^' && c1=='=') { kind=TK_XOR_ASSIGN; adv=2; }
            else if (c0=='=' && c1=='=') { kind=TK_EQ; adv=2; }
            else if (c0=='!' && c1=='=') { kind=TK_NE; adv=2; }
            else if (c0=='<' && c1=='=') { kind=TK_LE; adv=2; }
            else if (c0=='>' && c1=='=') { kind=TK_GE; adv=2; }
            else if (c0=='&' && c1=='&') { kind=TK_LOGAND; adv=2; }
            else if (c0=='|' && c1=='|') { kind=TK_LOGOR; adv=2; }
            else if (c0=='^' && c1=='^') { kind=TK_LOGXOR; adv=2; }
            else if (c0=='<' && c1=='<') { kind=TK_SHL; adv=2; }
            else if (c0=='>' && c1=='>') { kind=TK_SHR; adv=2; }
            else if (c0=='(') { kind=TK_LPAREN; adv=1; }
            else if (c0==')') { kind=TK_RPAREN; adv=1; }
            else if (c0=='{') { kind=TK_LBRACE; adv=1; }
            else if (c0=='}') { kind=TK_RBRACE; adv=1; }
            else if (c0=='[') { kind=TK_LBRACKET; adv=1; }
            else if (c0==']') { kind=TK_RBRACKET; adv=1; }
            else if (c0==';') { kind=TK_SEMI; adv=1; }
            else if (c0==',') { kind=TK_COMMA; adv=1; }
            else if (c0=='.') { kind=TK_DOT; adv=1; }
            else if (c0=='?') { kind=TK_QUESTION; adv=1; }
            else if (c0==':') { kind=TK_COLON; adv=1; }
            else if (c0=='+') { kind=TK_ADD; adv=1; }
            else if (c0=='-') { kind=TK_SUB; adv=1; }
            else if (c0=='*') { kind=TK_MUL; adv=1; }
            else if (c0=='/') { kind=TK_DIV; adv=1; }
            else if (c0=='%') { kind=TK_MOD; adv=1; }
            else if (c0=='=') { kind=TK_ASSIGN; adv=1; }
            else if (c0=='<') { kind=TK_LT; adv=1; }
            else if (c0=='>') { kind=TK_GT; adv=1; }
            else if (c0=='!') { kind=TK_BANG; adv=1; }
            else if (c0=='&') { kind=TK_BAND; adv=1; }
            else if (c0=='|') { kind=TK_BOR; adv=1; }
            else if (c0=='^') { kind=TK_BXOR; adv=1; }
            else if (c0=='~') { kind=TK_BNOT; adv=1; }
            if (kind < 0) {
                char m[32]; snprintf(m, sizeof m, "unexpected character '%c'", c0);
                seterr(err, errsz, line, m);
                free(toks);
                return NULL;
            }
            toks[n].kind = kind;
            toks[n].text[0] = c0; toks[n].text[1] = '\0';
            p += adv; n++;
            continue;
        }
    }

    PUSH();
    toks[n].kind = TK_EOF;
    n++;

    if (count) *count = n;
    return toks;
}
