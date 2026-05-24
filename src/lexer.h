#ifndef SPECTRA_LEXER_H
#define SPECTRA_LEXER_H
#include "ast.h"

typedef struct {
    const char* src;
    int         pos;
    int         line;
    int         col;
    /* indent stack */
    int         indent_stack[256];
    int         indent_top;
    /* token output buffer */
    Token*      tokens;
    int         tok_count;
    int         tok_cap;
    /* pending DEDENTs to emit */
    int         pending_dedents;
    char        error_msg[256];
    int         had_error;
} Lexer;

Lexer* lexer_new(const char* source);
void   lexer_free(Lexer* lex);
int    lexer_tokenize(Lexer* lex);   /* returns 1 on success, 0 on error */
void   lexer_dump(Lexer* lex);       /* print all tokens for debug */

#endif /* SPECTRA_LEXER_H */
