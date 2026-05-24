/* repl.c — interactive Read-Eval-Print Loop for SPECTRA */
#include "repl.h"
#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPL_LINE_MAX   4096
#define REPL_BUF_MAX    65536

/* =========================================================
 * Small helper: trim a trailing '\r' and/or '\n'
 * ========================================================= */
static void trim_newline(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

/* =========================================================
 * repl_run
 * ========================================================= */

void repl_run(void) {
    printf("SPECTRA v0.1.0  |  type 'exit' or Ctrl+C to quit\n");

    /* Create a persistent interpreter for the whole session.
     * interp_new() already calls register_builtins internally. */
    Interpreter* interp = interp_new();

    /* Working buffer for accumulated multi-line input */
    char* buf = (char*)malloc(REPL_BUF_MAX);
    if (!buf) {
        fprintf(stderr, "[SPECTRA] out of memory\n");
        interp_free(interp);
        return;
    }

    char line[REPL_LINE_MAX];

    for (;;) {
        /* Reset the accumulation buffer */
        buf[0] = '\0';
        int buf_len = 0;
        int multiline = 0;

        /* Primary prompt */
        printf(">>> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            /* EOF — Ctrl+D / pipe exhausted */
            printf("\n");
            break;
        }
        trim_newline(line);

        /* Blank line at the top level: skip */
        if (line[0] == '\0') continue;

        /* Exit commands */
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }

        /* Append first line to buffer */
        buf_len += snprintf(buf + buf_len, REPL_BUF_MAX - buf_len, "%s\n", line);

        /* Detect multi-line mode: line ends with ':' after stripping spaces */
        {
            size_t len = strlen(line);
            const char* end = line + len;
            while (end > line && (*(end-1) == ' ' || *(end-1) == '\t')) --end;
            if (end > line && *(end-1) == ':') {
                multiline = 1;
            }
        }

        /* Multi-line collection: keep prompting "... " until blank line */
        while (multiline) {
            printf("... ");
            fflush(stdout);

            if (!fgets(line, sizeof(line), stdin)) {
                printf("\n");
                multiline = 0;
                break;
            }
            trim_newline(line);

            /* Empty line ends the block */
            if (line[0] == '\0') {
                multiline = 0;
                break;
            }

            /* Check if this new line also opens a new sub-block */
            {
                size_t len = strlen(line);
                const char* end = line + len;
                while (end > line && (*(end-1) == ' ' || *(end-1) == '\t')) --end;
                if (end > line && *(end-1) == ':') {
                    multiline = 1; /* stay in multi-line */
                }
                /* (if it doesn't end with ':', stay in multiline until blank) */
            }

            buf_len += snprintf(buf + buf_len, REPL_BUF_MAX - buf_len,
                                "%s\n", line);
            if (buf_len >= REPL_BUF_MAX - 2) {
                fprintf(stderr, "[SPECTRA] input too long\n");
                buf_len = 0;
                buf[0] = '\0';
                multiline = 0;
                break;
            }
        }

        if (buf_len == 0 || buf[0] == '\0') continue;

        /* ── Lex ── */
        Lexer* lex = lexer_new(buf);
        if (!lexer_tokenize(lex)) {
            fprintf(stderr, "[SPECTRA] Lex error: %s\n", lex->error_msg);
            lexer_free(lex);
            continue;
        }

        /* ── Parse ── */
        Parser* parser = parser_new(lex->tokens, lex->tok_count);
        ASTNode* program = parser_parse(parser);
        if (!program || parser->had_error) {
            fprintf(stderr, "[SPECTRA] Parse error: %s\n", parser->error_msg);
            parser_free(parser);
            lexer_free(lex);
            continue;
        }

        /* ── Evaluate ──
         *
         * If the program has exactly one ND_EXPR_STMT, evaluate its
         * expression and auto-print the result (REPL echo).
         * Otherwise execute all statements normally.
         */
        int auto_print = 0;
        ASTNode* single_expr = NULL;

        if (program->as.program.stmts.count == 1) {
            ASTNode* stmt = program->as.program.stmts.items[0];
            if (stmt->kind == ND_EXPR_STMT) {
                auto_print = 1;
                single_expr = stmt->as.expr_stmt.expr;
            }
        }

        /* Reset signal from any previous error */
        interp->signal     = SIG_NONE;
        interp->error_msg[0] = '\0';
        interp->error_line = 0;

        if (auto_print && single_expr) {
            Value* result = interp_eval(interp, single_expr);
            if (interp->signal == SIG_ERROR) {
                fprintf(stderr, "[SPECTRA] Error: %s\n", interp->error_msg);
                interp->signal = SIG_NONE;
                interp->error_msg[0] = '\0';
            } else if (result && result->type != VAL_NULL) {
                val_println(result);
            }
            if (result) val_release(result);
        } else {
            int ok = interp_run(interp, program);
            if (!ok) {
                fprintf(stderr, "[SPECTRA] Error at line %d: %s\n",
                        interp->error_line, interp->error_msg);
                interp->signal = SIG_NONE;
                interp->error_msg[0] = '\0';
            }
        }

        /* Clean up this iteration's parse tree */
        parser_free(parser);
        lexer_free(lex);
        node_free(program);
    }

    free(buf);
    interp_free(interp);
}
