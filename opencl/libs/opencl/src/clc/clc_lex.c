/*
 * clc_lex.c - scanner for the OpenCL-C subset.  Handles line and block comments,
 * preprocessor directive lines (#... skipped, including line continuations),
 * identifiers/keywords, integer constants (decimal/hex/octal with u/l
 * suffixes), floating constants (with f suffix and exponent), character
 * constants, string literals, and the full operator/punctuation set.
 */
#include "clc_lex.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

static void lexerr(clc_lexer *L, int line, const char *fmt, ...)
{
    va_list ap;
    if (L->has_err) return;
    L->has_err = 1;
    snprintf(L->err, sizeof(L->err), "%d: ", line);
    {
        size_t n = strlen(L->err);
        va_start(ap, fmt);
        vsnprintf(L->err + n, sizeof(L->err) - n, fmt, ap);
        va_end(ap);
    }
}

static int push(clc_lexer *L, clc_token *t)
{
    clc_token *nt;
    if ((L->ntoks & 255) == 0) {
        nt = (clc_token *)realloc(L->toks, sizeof(clc_token) * (L->ntoks + 256));
        if (!nt) return -1;
        L->toks = nt;
    }
    L->toks[L->ntoks++] = *t;
    return 0;
}

static char *dupn(const char *s, int n)
{
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

/* add one punctuation token, matching the longest of up to 3 chars */
static int add_punct(clc_lexer *L, int line, const char *s)
{
    clc_token t;
    int p = P_NONE, len = 1;
    char c0 = s[0], c1 = s[1], c2 = s[2];

    if (c0=='.' && c1=='.' && c2=='.') { p = P_ELLIPSIS; len = 3; }
    else if (c0=='<' && c1=='<' && c2=='=') { p = P_SHL_ASG; len = 3; }
    else if (c0=='>' && c1=='>' && c2=='=') { p = P_SHR_ASG; len = 3; }
    else if (c0=='-' && c1=='>') { p = P_ARROW; len = 2; }
    else if (c0=='<' && c1=='<') { p = P_SHL; len = 2; }
    else if (c0=='>' && c1=='>') { p = P_SHR; len = 2; }
    else if (c0=='<' && c1=='=') { p = P_LE; len = 2; }
    else if (c0=='>' && c1=='=') { p = P_GE; len = 2; }
    else if (c0=='=' && c1=='=') { p = P_EQ; len = 2; }
    else if (c0=='!' && c1=='=') { p = P_NE; len = 2; }
    else if (c0=='&' && c1=='&') { p = P_LAND; len = 2; }
    else if (c0=='|' && c1=='|') { p = P_LOR; len = 2; }
    else if (c0=='+' && c1=='+') { p = P_INC; len = 2; }
    else if (c0=='-' && c1=='-') { p = P_DEC; len = 2; }
    else if (c0=='+' && c1=='=') { p = P_ADD_ASG; len = 2; }
    else if (c0=='-' && c1=='=') { p = P_SUB_ASG; len = 2; }
    else if (c0=='*' && c1=='=') { p = P_MUL_ASG; len = 2; }
    else if (c0=='/' && c1=='=') { p = P_DIV_ASG; len = 2; }
    else if (c0=='%' && c1=='=') { p = P_MOD_ASG; len = 2; }
    else if (c0=='&' && c1=='=') { p = P_AND_ASG; len = 2; }
    else if (c0=='|' && c1=='=') { p = P_OR_ASG; len = 2; }
    else if (c0=='^' && c1=='=') { p = P_XOR_ASG; len = 2; }
    else {
        switch (c0) {
        case '+': p = P_ADD; break;
        case '-': p = P_SUB; break;
        case '*': p = P_MUL; break;
        case '/': p = P_DIV; break;
        case '%': p = P_MOD; break;
        case '=': p = P_ASSIGN; break;
        case '<': p = P_LT; break;
        case '>': p = P_GT; break;
        case '!': p = P_NOT; break;
        case '~': p = P_BNOT; break;
        case '&': p = P_AND; break;
        case '|': p = P_OR; break;
        case '^': p = P_XOR; break;
        case '?': p = P_QUESTION; break;
        case ':': p = P_COLON; break;
        case ';': p = P_SEMI; break;
        case ',': p = P_COMMA; break;
        case '.': p = P_DOT; break;
        case '(': p = P_LPAREN; break;
        case ')': p = P_RPAREN; break;
        case '[': p = P_LBRACK; break;
        case ']': p = P_RBRACK; break;
        case '{': p = P_LBRACE; break;
        case '}': p = P_RBRACE; break;
        default:
            lexerr(L, line, "unexpected character '%c'", c0);
            return -1;
        }
        len = 1;
    }

    memset(&t, 0, sizeof(t));
    t.kind = TK_PUNCT;
    t.punct = p;
    t.line = line;
    if (push(L, &t) < 0) return -1;
    return len;
}

int clc_lex(clc_lexer *L, const char *src)
{
    const char *s = src;
    int line = 1;
    memset(L, 0, sizeof(*L));

    while (*s) {
        /* whitespace */
        if (*s == '\n') { line++; s++; continue; }
        if (isspace((unsigned char)*s)) { s++; continue; }

        /* comments */
        if (s[0]=='/' && s[1]=='/') {
            while (*s && *s != '\n') s++;
            continue;
        }
        if (s[0]=='/' && s[1]=='*') {
            s += 2;
            while (*s && !(s[0]=='*' && s[1]=='/')) { if (*s=='\n') line++; s++; }
            if (*s) s += 2;
            continue;
        }

        /* preprocessor line: skip (handle backslash continuation) */
        if (*s == '#') {
            while (*s) {
                if (*s == '\\') { s++; if (*s=='\n'){line++; s++;} continue; }
                if (*s == '\n') break;
                s++;
            }
            continue;
        }

        /* identifier / keyword */
        if (isalpha((unsigned char)*s) || *s == '_') {
            const char *b = s;
            clc_token t;
            while (isalnum((unsigned char)*s) || *s == '_') s++;
            memset(&t, 0, sizeof(t));
            t.kind = TK_IDENT;
            t.line = line;
            t.text = dupn(b, (int)(s - b));
            if (!t.text) { lexerr(L, line, "out of memory"); return -1; }
            if (push(L, &t) < 0) return -1;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)*s) || (*s=='.' && isdigit((unsigned char)s[1]))) {
            clc_token t;
            memset(&t, 0, sizeof(t));
            t.line = line;
            if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) {
                s += 2;
                t.ival = 0;
                while (isxdigit((unsigned char)*s)) {
                    int d = isdigit((unsigned char)*s) ? (*s-'0') : (tolower((unsigned char)*s)-'a'+10);
                    t.ival = (t.ival << 4) | d;
                    s++;
                }
                t.kind = TK_INT;
            } else {
                const char *b = s;
                int is_float = 0;
                while (isdigit((unsigned char)*s)) s++;
                if (*s == '.') { is_float = 1; s++; while (isdigit((unsigned char)*s)) s++; }
                if (*s=='e' || *s=='E') {
                    is_float = 1; s++;
                    if (*s=='+'||*s=='-') s++;
                    while (isdigit((unsigned char)*s)) s++;
                }
                if (is_float) {
                    char *num = dupn(b, (int)(s - b));
                    t.kind = TK_FLOAT;
                    t.fval = strtod(num, NULL);
                    free(num);
                    if (*s=='f' || *s=='F') s++;
                } else {
                    char *num = dupn(b, (int)(s - b));
                    t.kind = TK_INT;
                    t.ival = strtoll(num, NULL, 10);
                    free(num);
                }
            }
            /* integer suffixes */
            while (*s=='u'||*s=='U'||*s=='l'||*s=='L') {
                if (*s=='u'||*s=='U') t.is_unsigned = 1; else t.is_long = 1;
                s++;
            }
            if (push(L, &t) < 0) return -1;
            continue;
        }

        /* char constant -> integer */
        if (*s == '\'') {
            clc_token t;
            long long v = 0;
            s++;
            memset(&t, 0, sizeof(t));
            if (*s == '\\') {
                s++;
                switch (*s) {
                case 'n': v='\n'; break; case 't': v='\t'; break;
                case 'r': v='\r'; break; case '0': v='\0'; break;
                case '\\': v='\\'; break; case '\'': v='\''; break;
                case '"': v='"'; break; default: v=*s; break;
                }
                s++;
            } else {
                v = (unsigned char)*s; s++;
            }
            if (*s == '\'') s++;
            t.kind = TK_INT; t.line = line; t.ival = v;
            if (push(L, &t) < 0) return -1;
            continue;
        }

        /* string literal */
        if (*s == '"') {
            clc_token t;
            const char *b;
            char *out;
            int oi = 0;
            s++;
            b = s;
            while (*s && *s != '"') { if (*s=='\n') line++; s++; }
            out = (char *)malloc((size_t)(s - b) + 1);
            if (!out) { lexerr(L, line, "out of memory"); return -1; }
            {
                const char *p = b;
                while (p < s) {
                    if (*p == '\\' && p+1 < s) {
                        p++;
                        switch (*p) {
                        case 'n': out[oi++]='\n'; break; case 't': out[oi++]='\t'; break;
                        case 'r': out[oi++]='\r'; break; case '0': out[oi++]='\0'; break;
                        case '\\': out[oi++]='\\'; break; case '"': out[oi++]='"'; break;
                        default: out[oi++]=*p; break;
                        }
                        p++;
                    } else {
                        out[oi++] = *p++;
                    }
                }
            }
            out[oi] = '\0';
            if (*s == '"') s++;
            memset(&t, 0, sizeof(t));
            t.kind = TK_STRING; t.line = line; t.text = out;
            if (push(L, &t) < 0) return -1;
            continue;
        }

        /* punctuation */
        {
            int adv = add_punct(L, line, s);
            if (adv < 0) return -1;
            s += adv;
        }
    }

    {
        clc_token t;
        memset(&t, 0, sizeof(t));
        t.kind = TK_EOF;
        t.line = line;
        if (push(L, &t) < 0) return -1;
    }
    if (L->has_err) return -1;
    return 0;
}

void clc_lex_free(clc_lexer *L)
{
    int i;
    if (!L) return;
    for (i = 0; i < L->ntoks; i++)
        free(L->toks[i].text);
    free(L->toks);
    L->toks = NULL;
    L->ntoks = 0;
}
