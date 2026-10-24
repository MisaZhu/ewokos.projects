/*
 * clc_lex.h - token model for the OpenCL-C subset scanner.  The lexer turns a
 * whole source string into a flat token array up front (kernel sources are
 * small), which keeps the recursive-descent parser simple: it only ever peeks
 * and advances.
 */
#ifndef OPENCL_CLC_LEX_H
#define OPENCL_CLC_LEX_H

typedef enum {
    TK_EOF = 0, TK_IDENT, TK_INT, TK_FLOAT, TK_STRING,
    TK_PUNCT
} clc_tk_kind;

typedef enum {
    P_NONE = 0,
    P_ADD, P_SUB, P_MUL, P_DIV, P_MOD, P_ASSIGN,
    P_LT, P_GT, P_NOT, P_BNOT, P_AND, P_OR, P_XOR,
    P_QUESTION, P_COLON, P_SEMI, P_COMMA, P_DOT,
    P_LPAREN, P_RPAREN, P_LBRACK, P_RBRACK, P_LBRACE, P_RBRACE,
    P_SHL, P_SHR, P_LE, P_GE, P_EQ, P_NE, P_LAND, P_LOR,
    P_INC, P_DEC,
    P_ADD_ASG, P_SUB_ASG, P_MUL_ASG, P_DIV_ASG, P_MOD_ASG,
    P_AND_ASG, P_OR_ASG, P_XOR_ASG, P_SHL_ASG, P_SHR_ASG,
    P_ARROW, P_ELLIPSIS
} clc_punct;

typedef struct clc_token {
    clc_tk_kind kind;
    int         punct;      /* clc_punct when kind == TK_PUNCT          */
    int         line;
    long long   ival;       /* TK_INT                                   */
    int         is_unsigned;/* TK_INT 'u' suffix                        */
    int         is_long;    /* TK_INT 'l' suffix                        */
    double      fval;       /* TK_FLOAT                                 */
    char       *text;       /* TK_IDENT / TK_STRING (owned)             */
} clc_token;

typedef struct clc_lexer {
    clc_token *toks;
    int        ntoks;
    int        pos;
    char       err[512];
    int        has_err;
} clc_lexer;

/* Tokenize `src` into lex->toks.  Returns 0 on success, -1 on a lexical error
 * (message in lex->err). */
int  clc_lex(clc_lexer *lex, const char *src);
void clc_lex_free(clc_lexer *lex);

#endif /* OPENCL_CLC_LEX_H */
