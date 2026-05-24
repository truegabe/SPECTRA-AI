#ifndef SPECTRA_INTERP_H
#define SPECTRA_INTERP_H
#include "value.h"
#include "env.h"
#include "ast.h"

typedef enum { SIG_NONE=0, SIG_RETURN, SIG_BREAK, SIG_CONTINUE, SIG_ERROR } Signal;

typedef struct Interpreter {
    Env*    globals;
    Env*    current_env;
    Signal  signal;
    Value*  return_val;    /* val_retain'd when set */
    char    error_msg[512];
    int     error_line;
    int     call_depth;
    int     sim_mode;      /* 1 = simulator mode — print wave visualizations */
} Interpreter;

Interpreter* interp_new(void);
void         interp_free(Interpreter* interp);
int          interp_run(Interpreter* interp, ASTNode* program);  /* 1=ok, 0=error */
Value*       interp_eval(Interpreter* interp, ASTNode* expr);
void         interp_exec(Interpreter* interp, ASTNode* stmt);
void         interp_exec_block(Interpreter* interp, NodeList* stmts, Env* block_env);
void         interp_error(Interpreter* interp, int line, const char* fmt, ...);

#endif /* SPECTRA_INTERP_H */
