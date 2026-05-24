#ifndef SPECTRA_PARSER_H
#define SPECTRA_PARSER_H
#include "ast.h"

typedef struct {
    Token*  tokens;   /* token array from lexer */
    int     count;    /* total token count */
    int     pos;      /* current position */
    char    error_msg[512];
    int     had_error;
} Parser;

Parser*   parser_new(Token* tokens, int count);
void      parser_free(Parser* p);
ASTNode*  parser_parse(Parser* p);   /* returns ND_PROGRAM or NULL on error */
void      parser_dump(ASTNode* node, int indent);  /* print AST for debug */

#endif /* SPECTRA_PARSER_H */
