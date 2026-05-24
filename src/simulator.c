/* simulator.c — terminal wave-state visualizer for SPECTRA */
#include "simulator.h"
#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================
 * ANSI escape codes
 * On Windows with MSVC the console may not support ANSI by default,
 * but modern Windows 10/11 terminals do.
 * ========================================================= */

#define ANSI_RESET   "\x1b[0m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_DIM     "\x1b[2m"

/* Box-drawing characters (UTF-8) */
#define BOX_TL   "\xe2\x94\x8c"   /* ┌ */
#define BOX_TR   "\xe2\x94\x90"   /* ┐ */
#define BOX_BL   "\xe2\x94\x94"   /* └ */
#define BOX_BR   "\xe2\x94\x98"   /* ┘ */
#define BOX_H    "\xe2\x94\x80"   /* ─ */
#define BOX_V    "\xe2\x94\x82"   /* │ */

/* Block characters */
#define BLOCK_FULL  "\xe2\x96\x88"  /* █ */
#define BLOCK_EMPTY "\xe2\x96\x91"  /* ░ */

#define BAR_WIDTH 20   /* number of bar characters */
#define BOX_INNER 44   /* inner width of the box (content columns) */

/* =========================================================
 * sim_show_banner
 * ========================================================= */

void sim_show_banner(void) {
    printf(ANSI_BOLD ANSI_CYAN
           "\xe2\x95\x94" /* ╔ */
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x97" /* ╗ */
           "\n"
           "\xe2\x95\x91" /* ║ */
           "   SPECTRA v0.1.0  \xe2\x80\x94  Sim Mode        "
           "\xe2\x95\x91" /* ║ */
           "\n"
           "\xe2\x95\x9a" /* ╚ */
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"
           "\xe2\x95\x9d" /* ╝ */
           "\n" ANSI_RESET);
}

/* =========================================================
 * sim_show_specton
 *
 * Draws a box like:
 *
 *  ┌─ name ──────────────────────────── WAVE ─┐
 *  │ 0 ░░░░░░░░░░░░░░░░░░░░  0.00             │
 *  │ ...                                       │
 *  │ entropy: X.XX bits                        │
 *  └───────────────────────────────────────────┘
 *
 * For FIXED: only the fixed_val state is shown as fully filled.
 * For RANGE: states in [lo..hi] show uniform fill = 1/(hi-lo+1).
 * For WAVE:  shows the actual float weights, normalized if needed.
 * ========================================================= */

void sim_show_specton(const char* name, Specton s) {
    /* Build float[10] representing the bar fill level per state [0..1] */
    float levels[SPECT_STATES];
    memset(levels, 0, sizeof(levels));

    const char* mode_label = "FIXED";

    switch (s.mode) {
        case SPECT_FIXED: {
            mode_label = "FIXED";
            uint8_t v = s.data.fixed_val;
            if (v < SPECT_STATES) levels[v] = 1.0f;
            break;
        }
        case SPECT_RANGE: {
            mode_label = "RANGE";
            int lo = s.data.range.lo;
            int hi = s.data.range.hi;
            if (lo > hi) { int tmp = lo; lo = hi; hi = tmp; }
            int count = hi - lo + 1;
            if (count < 1) count = 1;
            float fill = 1.0f / (float)count;
            for (int i = lo; i <= hi && i < SPECT_STATES; i++) {
                levels[i] = fill;
            }
            break;
        }
        case SPECT_WAVE: {
            mode_label = "WAVE";
            /* Find max weight for normalizing display */
            float maxw = 0.0f;
            for (int i = 0; i < SPECT_STATES; i++) {
                if (s.data.wave[i] > maxw) maxw = s.data.wave[i];
            }
            if (maxw <= 0.0f) maxw = 1.0f;
            for (int i = 0; i < SPECT_STATES; i++) {
                levels[i] = s.data.wave[i] / maxw;
            }
            break;
        }
    }

    /* Find peak state */
    uint8_t peak = spect_peak(s);
    float entropy = spect_entropy(s);

    /* ── Top border ── */
    /* " ┌─ name ─────── MODE ─┐" */
    {
        char header[BOX_INNER + 16];
        /* Build: " name " padded then "── MODE ─" */
        /* Total inner width: BOX_INNER characters */
        int name_len = (int)strlen(name);
        int mode_len = (int)strlen(mode_label);
        /* left part: "─ name ─" */
        /* right part: "─ MODE ─" at the right */
        /* total dashes between = BOX_INNER - 2 - name_len - 2 - mode_len - 2 */
        int dashes = BOX_INNER - 2 - name_len - 2 - mode_len - 2;
        if (dashes < 1) dashes = 1;

        printf(" " BOX_TL BOX_H " ");
        printf(ANSI_BOLD "%s" ANSI_RESET, name);
        printf(" ");
        for (int d = 0; d < dashes; d++) printf(BOX_H);
        printf(" " ANSI_CYAN "%s" ANSI_RESET " " BOX_H BOX_TR "\n", mode_label);
        (void)header;
    }

    /* ── State rows ── */
    for (int i = 0; i < SPECT_STATES; i++) {
        float lv = levels[i];
        int filled = (int)(lv * BAR_WIDTH + 0.5f);
        if (filled > BAR_WIDTH) filled = BAR_WIDTH;
        if (filled < 0) filled = 0;
        int empty = BAR_WIDTH - filled;

        int is_peak = (s.mode != SPECT_FIXED) && (i == (int)peak) && (lv > 0.0f);

        /* Actual weight for display */
        float disp_weight = 0.0f;
        switch (s.mode) {
            case SPECT_FIXED:
                disp_weight = (i == (int)s.data.fixed_val) ? 1.0f : 0.0f;
                break;
            case SPECT_RANGE: {
                int lo = s.data.range.lo, hi = s.data.range.hi;
                if (lo > hi) { int t = lo; lo = hi; hi = t; }
                int count = hi - lo + 1;
                disp_weight = (i >= lo && i <= hi) ? 1.0f / (float)count : 0.0f;
                break;
            }
            case SPECT_WAVE:
                disp_weight = s.data.wave[i];
                break;
        }

        printf(" " BOX_V " %d ", i);

        /* Bar */
        if (is_peak) printf(ANSI_YELLOW);
        else         printf(ANSI_BLUE);

        for (int b = 0; b < filled; b++) printf(BLOCK_FULL);
        printf(ANSI_RESET ANSI_DIM);
        for (int b = 0; b < empty;  b++) printf(BLOCK_EMPTY);
        printf(ANSI_RESET);

        /* Weight value */
        printf("  %4.2f", disp_weight);

        if (is_peak) {
            printf(ANSI_YELLOW " \xe2\x97\x84 PEAK" ANSI_RESET);
            /* pad to align right border: "◄ PEAK" = 6 chars */
            printf("             " BOX_V "\n");
        } else {
            /* pad to align right border */
            printf("                   " BOX_V "\n");
        }
    }

    /* ── Entropy line ── */
    printf(" " BOX_V " entropy: " ANSI_CYAN "%.2f bits" ANSI_RESET, (double)entropy);
    /* padding */
    printf("                                  " BOX_V "\n");

    /* ── Bottom border ── */
    printf(" " BOX_BL);
    for (int d = 0; d < BOX_INNER + 2; d++) printf(BOX_H);
    printf(BOX_BR "\n");
}

/* =========================================================
 * sim_run_file
 * ========================================================= */

void sim_run_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[SPECTRA] Error: cannot open '%s'\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* src = (char*)malloc((size_t)sz + 1);
    if (!src) {
        fprintf(stderr, "[SPECTRA] Out of memory\n");
        fclose(f);
        return;
    }
    fread(src, 1, (size_t)sz, f);
    src[sz] = '\0';
    fclose(f);

    /* Lex */
    Lexer* lex = lexer_new(src);
    if (!lexer_tokenize(lex)) {
        fprintf(stderr, "[SPECTRA] Lex error: %s\n", lex->error_msg);
        lexer_free(lex);
        free(src);
        return;
    }

    /* Parse */
    Parser* parser = parser_new(lex->tokens, lex->tok_count);
    ASTNode* program = parser_parse(parser);
    if (!program || parser->had_error) {
        fprintf(stderr, "[SPECTRA] Parse error: %s\n", parser->error_msg);
        parser_free(parser);
        lexer_free(lex);
        free(src);
        return;
    }

    /* Interpret with sim_mode = 1.
     * interp_new() already calls register_builtins internally. */
    Interpreter* interp = interp_new();
    interp->sim_mode = 1;

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
}
