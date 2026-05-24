/* main.c — SPECTRA interpreter entry point */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
#include "repl.h"
#include "simulator.h"

/* =========================================================
 * print_help
 * ========================================================= */

static void print_help(void) {
    printf(
        "SPECTRA v0.1.0 \xe2\x80\x94 Spectral Language Runtime\n"
        "\n"
        "Usage:\n"
        "  spectra                    Start interactive REPL\n"
        "  spectra repl               Start interactive REPL (explicit)\n"
        "  spectra run <file.sp>      Interpret and run a file\n"
        "  spectra sim <file.sp>      Run with wave state visualizer\n"
        "  spectra tokens <file.sp>   Dump token stream\n"
        "  spectra ast <file.sp>      Dump abstract syntax tree\n"
        "  spectra version            Show version\n"
        "  spectra help               Show this help\n"
        "\n"
        "Examples:\n"
        "  spectra run examples/hello.sp\n"
        "  spectra sim examples/wave_demo.sp\n"
        "  spectra\n"
    );
}

/* =========================================================
 * run_file — lex, parse, and interpret a .sp source file
 * sim = 1 enables the wave visualizer
 * Returns 0 on success, 1 on any error.
 * ========================================================= */

static int run_file(const char* path, int sim) {
    /* ── Read ── */
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[SPECTRA] Error: cannot open '%s'\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* src = (char*)malloc((size_t)sz + 1);
    if (!src) {
        fprintf(stderr, "[SPECTRA] Out of memory\n");
        fclose(f);
        return 1;
    }
    fread(src, 1, (size_t)sz, f);
    src[sz] = '\0';
    fclose(f);

    /* ── Lex ── */
    Lexer* lex = lexer_new(src);
    if (!lexer_tokenize(lex)) {
        fprintf(stderr, "[SPECTRA] Lex error: %s\n", lex->error_msg);
        lexer_free(lex);
        free(src);
        return 1;
    }

    /* ── Parse ── */
    Parser* parser = parser_new(lex->tokens, lex->tok_count);
    ASTNode* program = parser_parse(parser);
    if (!program || parser->had_error) {
        fprintf(stderr, "[SPECTRA] Parse error: %s\n", parser->error_msg);
        parser_free(parser);
        lexer_free(lex);
        free(src);
        return 1;
    }

    /* ── Interpret ── */
    /* interp_new() already calls register_builtins internally. */
    Interpreter* interp = interp_new();
    interp->sim_mode = sim;

    int ok = interp_run(interp, program);
    if (!ok) {
        fprintf(stderr, "[SPECTRA] Runtime error at line %d: %s\n",
                interp->error_line, interp->error_msg);
    }

    interp_free(interp);
    parser_free(parser);
    lexer_free(lex);
    node_free(program);
    free(src);
    return ok ? 0 : 1;
}

/* =========================================================
 * main
 * ========================================================= */

int main(int argc, char** argv) {
    /* No arguments → interactive REPL */
    if (argc == 1) {
        repl_run();
        return 0;
    }

    const char* cmd = argv[1];

    /* ── help ── */
    if (strcmp(cmd, "help")     == 0 ||
        strcmp(cmd, "--help")   == 0 ||
        strcmp(cmd, "-h")       == 0) {
        print_help();
        return 0;
    }

    /* ── version ── */
    if (strcmp(cmd, "version")   == 0 ||
        strcmp(cmd, "--version") == 0 ||
        strcmp(cmd, "-v")        == 0) {
        printf("SPECTRA v0.1.0\n");
        return 0;
    }

    /* ── repl ── */
    if (strcmp(cmd, "repl") == 0) {
        repl_run();
        return 0;
    }

    /* ── run <file> ── */
    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: spectra run <file.sp>\n");
            return 1;
        }
        return run_file(argv[2], 0);
    }

    /* ── sim <file> ── */
    if (strcmp(cmd, "sim") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: spectra sim <file.sp>\n");
            return 1;
        }
        sim_show_banner();
        return run_file(argv[2], 1);
    }

    /* ── tokens <file> ── */
    if (strcmp(cmd, "tokens") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: spectra tokens <file.sp>\n");
            return 1;
        }
        FILE* f = fopen(argv[2], "r");
        if (!f) {
            fprintf(stderr, "[SPECTRA] Cannot open '%s'\n", argv[2]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char* src = (char*)malloc((size_t)sz + 1);
        if (!src) {
            fprintf(stderr, "[SPECTRA] Out of memory\n");
            fclose(f);
            return 1;
        }
        fread(src, 1, (size_t)sz, f);
        src[sz] = '\0';
        fclose(f);
        Lexer* lex = lexer_new(src);
        if (!lexer_tokenize(lex)) {
            fprintf(stderr, "[SPECTRA] Lex error: %s\n", lex->error_msg);
            lexer_free(lex);
            free(src);
            return 1;
        }
        lexer_dump(lex);
        lexer_free(lex);
        free(src);
        return 0;
    }

    /* ── ast <file> ── */
    if (strcmp(cmd, "ast") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: spectra ast <file.sp>\n");
            return 1;
        }
        FILE* f = fopen(argv[2], "r");
        if (!f) {
            fprintf(stderr, "[SPECTRA] Cannot open '%s'\n", argv[2]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char* src = (char*)malloc((size_t)sz + 1);
        if (!src) {
            fprintf(stderr, "[SPECTRA] Out of memory\n");
            fclose(f);
            return 1;
        }
        fread(src, 1, (size_t)sz, f);
        src[sz] = '\0';
        fclose(f);
        Lexer* lex = lexer_new(src);
        if (!lexer_tokenize(lex)) {
            fprintf(stderr, "[SPECTRA] Lex error: %s\n", lex->error_msg);
            lexer_free(lex);
            free(src);
            return 1;
        }
        Parser* p = parser_new(lex->tokens, lex->tok_count);
        ASTNode* prog = parser_parse(p);
        if (!prog || p->had_error) {
            fprintf(stderr, "[SPECTRA] Parse error: %s\n", p->error_msg);
            parser_free(p);
            lexer_free(lex);
            free(src);
            return 1;
        }
        parser_dump(prog, 0);
        node_free(prog);
        parser_free(p);
        lexer_free(lex);
        free(src);
        return 0;
    }

    /* ── Bare filename (ends in .sp) → treat as 'run' ── */
    {
        int len = (int)strlen(cmd);
        if (len > 3 && strcmp(cmd + len - 3, ".sp") == 0) {
            return run_file(cmd, 0);
        }
    }

    fprintf(stderr, "[SPECTRA] Unknown command '%s'. Run 'spectra help'.\n", cmd);
    return 1;
}
