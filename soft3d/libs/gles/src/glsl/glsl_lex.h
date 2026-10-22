/* glsl_lex.h - tokenizer interface for the CPU GLSL ES 1.00 interpreter. */
#ifndef GLES_GLSL_LEX_H
#define GLES_GLSL_LEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TK_EOF = 0, TK_IDENT, TK_INT, TK_FLOAT,
    /* keywords */
    TK_ATTRIBUTE, TK_VARYING, TK_UNIFORM, TK_CONST, TK_STRUCT,
    TK_BOOL, TK_INT_T, TK_FLOAT_T, TK_VOID_T,
    TK_VEC2, TK_VEC3, TK_VEC4, TK_IVEC2, TK_IVEC3, TK_IVEC4,
    TK_BVEC2, TK_BVEC3, TK_BVEC4, TK_MAT2, TK_MAT3, TK_MAT4,
    TK_SAMPLER2D, TK_SAMPLERCUBE,
    TK_TRUE, TK_FALSE,
    TK_IF, TK_ELSE, TK_FOR, TK_WHILE, TK_DO, TK_BREAK, TK_CONTINUE,
    TK_RETURN, TK_DISCARD, TK_SWITCH, TK_CASE, TK_DEFAULT,
    TK_IN, TK_OUT, TK_INOUT,
    TK_PRECISION, TK_LOWP, TK_MEDIUMP, TK_HIGHP,
    /* punctuation */
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
    TK_SEMI, TK_COMMA, TK_DOT, TK_QUESTION, TK_COLON,
    /* operators */
    TK_INC, TK_DEC, TK_ADD, TK_SUB, TK_MUL, TK_DIV, TK_MOD,
    TK_ASSIGN, TK_ADD_ASSIGN, TK_SUB_ASSIGN, TK_MUL_ASSIGN, TK_DIV_ASSIGN,
    TK_MOD_ASSIGN, TK_AND_ASSIGN, TK_OR_ASSIGN, TK_XOR_ASSIGN,
    TK_SHL_ASSIGN, TK_SHR_ASSIGN,
    TK_EQ, TK_NE, TK_LT, TK_LE, TK_GT, TK_GE,
    TK_LOGAND, TK_LOGOR, TK_LOGXOR, TK_BANG,
    TK_BAND, TK_BOR, TK_BXOR, TK_BNOT, TK_SHL, TK_SHR,
    TK_UINT, TK_UINT_T, TK_UVEC2, TK_UVEC3, TK_UVEC4
} glsl_tokkind;

typedef struct {
    int kind;             /* glsl_tokkind */
    int line;
    float fval;           /* TK_FLOAT */
    int   ival;           /* TK_INT / TK_BOOL-ish */
    char  text[64];       /* TK_IDENT text (also keyword spelling) */
} glsl_token;

/* Tokenize src into a malloc'd array; *count receives the token count
 * (including the trailing TK_EOF).  Returns NULL on lexical error with a
 * message written to err.  The caller frees with free(). */
glsl_token *glsl_lex(const char *src, int *count, char *err, int errsz);

#ifdef __cplusplus
}
#endif

#endif /* GLES_GLSL_LEX_H */
