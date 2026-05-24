/*
 * lexer.c — SPECTRA language lexer
 *
 * C99.  No external dependencies beyond the standard library and
 * the headers declared in lexer.h / ast.h.
 */

#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>

/* =========================================================
 * Keyword table
 * ========================================================= */

typedef struct { const char* word; TokKind kind; } KwEntry;

static const KwEntry s_keywords[] = {
    { "func",     TOK_FUNC      },
    { "struct",   TOK_STRUCT    },
    { "if",       TOK_IF        },
    { "elif",     TOK_ELIF      },
    { "else",     TOK_ELSE      },
    { "for",      TOK_FOR       },
    { "while",    TOK_WHILE     },
    { "return",   TOK_RETURN    },
    { "import",   TOK_IMPORT    },
    { "from",     TOK_FROM      },
    { "as",       TOK_AS        },
    { "match",    TOK_MATCH     },
    { "wave_if",  TOK_WAVE_IF   },
    { "at",       TOK_AT_KW     },
    { "in",       TOK_IN        },
    { "and",      TOK_AND       },
    { "or",       TOK_OR        },
    { "not",      TOK_NOT       },
    { "true",     TOK_TRUE      },
    { "false",    TOK_FALSE     },
    { "alloc",    TOK_ALLOC     },
    { "free",     TOK_FREE      },
    { "null",     TOK_NULL_KW   },
    { "void",     TOK_VOID      },
    { "self",     TOK_SELF      },
    { "spect",    TOK_TY_SPECT  },
    { "int",      TOK_TY_INT    },
    { "float",    TOK_TY_FLOAT  },
    { "str",      TOK_TY_STR_T  },
    { "bool",     TOK_TY_BOOL   },
    { "byte",     TOK_TY_BYTE   },
    { "decint",   TOK_TY_DECINT },
    { "wavfloat", TOK_TY_WAVFLOAT },
    { "try",      TOK_TRY        },
    { "except",   TOK_EXCEPT     },
    { "break",    TOK_BREAK      },
    { "continue", TOK_CONTINUE   },
    { NULL, 0 }
};

static TokKind keyword_lookup(const char* word) {
    for (int i = 0; s_keywords[i].word; i++) {
        if (strcmp(s_keywords[i].word, word) == 0)
            return s_keywords[i].kind;
    }
    return TOK_IDENT;
}

/* =========================================================
 * Lexer internal helpers
 * ========================================================= */

/* Current character (or '\0' at EOF). */
static char lex_cur(Lexer* lex) {
    return lex->src[lex->pos];
}

/* Peek ahead by `offset` characters. */
static char lex_peek(Lexer* lex, int offset) {
    int p = lex->pos + offset;
    if (lex->src[p] == '\0') return '\0';
    return lex->src[p];
}

/* Advance by one character, updating line/col. */
static char lex_advance(Lexer* lex) {
    char c = lex->src[lex->pos];
    if (c == '\0') return '\0';
    lex->pos++;
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

/* Set an error message (once). */
static void lex_error(Lexer* lex, const char* fmt, ...) {
    if (lex->had_error) return;
    lex->had_error = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lex->error_msg, sizeof(lex->error_msg), fmt, ap);
    va_end(ap);
}

/* Grow token buffer and append a zeroed Token; return pointer to it. */
static Token* emit_tok_raw(Lexer* lex) {
    if (lex->tok_count == lex->tok_cap) {
        int new_cap = (lex->tok_cap == 0) ? 64 : lex->tok_cap * 2;
        lex->tokens = (Token*)realloc(lex->tokens, (size_t)new_cap * sizeof(Token));
        lex->tok_cap = new_cap;
    }
    Token* t = &lex->tokens[lex->tok_count++];
    memset(t, 0, sizeof(Token));
    return t;
}

/* Emit a simple (no-data) token. */
static void emit_tok(Lexer* lex, TokKind kind, int line, int col) {
    Token* t = emit_tok_raw(lex);
    t->kind = kind;
    t->line = line;
    t->col  = col;
    snprintf(t->text, sizeof(t->text), "%s", tok_kind_name(kind));
}

/* =========================================================
 * Skip whitespace (spaces/tabs on the current line only) and comments.
 * Returns 1 if we stopped at a real character, 0 if at newline/EOF.
 * ========================================================= */
static void skip_inline_ws(Lexer* lex) {
    while (lex_cur(lex) == ' ' || lex_cur(lex) == '\t' || lex_cur(lex) == '\r') {
        lex_advance(lex);
    }
}

static void skip_comment(Lexer* lex) {
    /* '#' to end of line */
    while (lex_cur(lex) != '\n' && lex_cur(lex) != '\0') {
        lex_advance(lex);
    }
}

/* =========================================================
 * Count leading spaces on the next non-blank, non-comment line.
 * Called when we are positioned right after a '\n'.
 * Returns the indent level, or -1 if the line is blank/comment-only.
 * ========================================================= */
/* count_leading_spaces: peek at the current position to count spaces.
 * Always restores the lexer state — never consumes characters.
 * Returns -1 for blank/comment-only lines, otherwise the indent count. */
static int count_leading_spaces(Lexer* lex) {
    int save_pos  = lex->pos;
    int save_line = lex->line;
    int save_col  = lex->col;

    int spaces = 0;
    /* Count spaces/tabs without calling lex_advance (keeps it a pure peek) */
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t') {
        spaces++;
        lex->pos++;
        lex->col++;
    }
    /* Check what follows the whitespace */
    char c = lex->src[lex->pos];
    /* Always restore — caller is responsible for consuming */
    lex->pos  = save_pos;
    lex->line = save_line;
    lex->col  = save_col;

    if (c == '\n' || c == '\r' || c == '#' || c == '\0') {
        return -1;
    }
    return spaces;
}

/* =========================================================
 * Indentation management — called at the start of a new (non-blank) line.
 * `new_indent` is the column count of leading spaces.
 * ========================================================= */
static void handle_indent(Lexer* lex, int new_indent, int line) {
    int top = lex->indent_stack[lex->indent_top];

    if (new_indent > top) {
        lex->indent_stack[++lex->indent_top] = new_indent;
        emit_tok(lex, TOK_INDENT, line, 1);
    } else if (new_indent < top) {
        /* Pop until we find a matching level */
        while (lex->indent_top > 0 && lex->indent_stack[lex->indent_top] > new_indent) {
            lex->indent_top--;
            emit_tok(lex, TOK_DEDENT, line, 1);
        }
        if (lex->indent_stack[lex->indent_top] != new_indent) {
            lex_error(lex, "line %d: inconsistent dedent (indent %d has no matching level)",
                      line, new_indent);
        }
    }
    /* Equal: no token needed */
}

/* =========================================================
 * Emit remaining DEDENTs at EOF
 * ========================================================= */
static void flush_dedents_eof(Lexer* lex, int line) {
    while (lex->indent_top > 0) {
        lex->indent_top--;
        emit_tok(lex, TOK_DEDENT, line, 1);
    }
}

/* =========================================================
 * Number lexing
 * ========================================================= */
static void lex_number(Lexer* lex) {
    int start_line = lex->line;
    int start_col  = lex->col;
    char buf[128];
    int  len = 0;
    int  is_float = 0;

    char c = lex_cur(lex);

    /* Check for 0x / 0b / 0o prefixes */
    if (c == '0' && (lex_peek(lex, 1) == 'x' || lex_peek(lex, 1) == 'X')) {
        buf[len++] = lex_advance(lex); /* '0' */
        buf[len++] = lex_advance(lex); /* 'x' */
        while (isxdigit((unsigned char)lex_cur(lex))) {
            buf[len++] = lex_advance(lex);
            if (len >= 126) break;
        }
        buf[len] = '\0';
        Token* t = emit_tok_raw(lex);
        t->kind = TOK_INT_LIT;
        t->line = start_line;
        t->col  = start_col;
        strncpy(t->text, buf, sizeof(t->text) - 1);
        t->data.int_val = (int64_t)strtoll(buf, NULL, 16);
        return;
    }
    if (c == '0' && (lex_peek(lex, 1) == 'b' || lex_peek(lex, 1) == 'B')) {
        buf[len++] = lex_advance(lex); /* '0' */
        buf[len++] = lex_advance(lex); /* 'b' */
        while (lex_cur(lex) == '0' || lex_cur(lex) == '1') {
            buf[len++] = lex_advance(lex);
            if (len >= 126) break;
        }
        buf[len] = '\0';
        Token* t = emit_tok_raw(lex);
        t->kind = TOK_INT_LIT;
        t->line = start_line;
        t->col  = start_col;
        strncpy(t->text, buf, sizeof(t->text) - 1);
        t->data.int_val = (int64_t)strtoll(buf + 2, NULL, 2);
        return;
    }
    if (c == '0' && (lex_peek(lex, 1) == 'o' || lex_peek(lex, 1) == 'O')) {
        buf[len++] = lex_advance(lex); /* '0' */
        buf[len++] = lex_advance(lex); /* 'o' */
        while (lex_cur(lex) >= '0' && lex_cur(lex) <= '7') {
            buf[len++] = lex_advance(lex);
            if (len >= 126) break;
        }
        buf[len] = '\0';
        Token* t = emit_tok_raw(lex);
        t->kind = TOK_INT_LIT;
        t->line = start_line;
        t->col  = start_col;
        strncpy(t->text, buf, sizeof(t->text) - 1);
        t->data.int_val = (int64_t)strtoll(buf + 2, NULL, 8);
        return;
    }

    /* Decimal / float */
    while (isdigit((unsigned char)lex_cur(lex))) {
        buf[len++] = lex_advance(lex);
        if (len >= 126) break;
    }
    if (lex_cur(lex) == '.' && lex_peek(lex, 1) != '.') {
        is_float = 1;
        buf[len++] = lex_advance(lex); /* '.' */
        while (isdigit((unsigned char)lex_cur(lex))) {
            buf[len++] = lex_advance(lex);
            if (len >= 126) break;
        }
    }
    if (lex_cur(lex) == 'e' || lex_cur(lex) == 'E') {
        is_float = 1;
        buf[len++] = lex_advance(lex); /* 'e'/'E' */
        if (lex_cur(lex) == '+' || lex_cur(lex) == '-') {
            buf[len++] = lex_advance(lex);
        }
        while (isdigit((unsigned char)lex_cur(lex))) {
            buf[len++] = lex_advance(lex);
            if (len >= 126) break;
        }
    }
    buf[len] = '\0';

    Token* t = emit_tok_raw(lex);
    t->line = start_line;
    t->col  = start_col;
    strncpy(t->text, buf, sizeof(t->text) - 1);
    if (is_float) {
        t->kind = TOK_FLOAT_LIT;
        t->data.float_val = atof(buf);
    } else {
        t->kind = TOK_INT_LIT;
        t->data.int_val = (int64_t)strtoll(buf, NULL, 10);
    }
}

/* =========================================================
 * String lexing  " or '
 * ========================================================= */
static void lex_string(Lexer* lex, char delim) {
    int start_line = lex->line;
    int start_col  = lex->col;
    lex_advance(lex); /* consume opening delimiter */

    /* Use a dynamic buffer */
    size_t cap = 128;
    size_t len = 0;
    char*  buf = (char*)malloc(cap);

    while (lex_cur(lex) != '\0' && lex_cur(lex) != delim) {
        char c = lex_advance(lex);
        if (c == '\\') {
            char esc = lex_advance(lex);
            switch (esc) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                case '\'': c = '\''; break;
                case '0':  c = '\0'; break;
                case 'r':  c = '\r'; break;
                default:   c = esc;  break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
        }
        buf[len++] = c;
    }
    if (lex_cur(lex) == delim) {
        lex_advance(lex); /* consume closing delimiter */
    } else {
        lex_error(lex, "line %d: unterminated string", start_line);
    }
    buf[len] = '\0';

    Token* t = emit_tok_raw(lex);
    t->kind = TOK_STR_LIT;
    t->line = start_line;
    t->col  = start_col;
    snprintf(t->text, sizeof(t->text), "\"%.*s\"", (int)(len > 30 ? 30 : len), buf);
    /* Store the heap pointer directly; callers that consume tokens are
       responsible for ownership (the Lexer does not free it on lexer_free). */
    /* We reuse data.c_code field since it's just a char*. */
    /* Actually for STR_LIT we store the raw string in data area.
       The Token union has no char* member for strings, but we can store
       via a dedicated path — we cast data.c_code (same slot as wave data).
       This is fine because STR_LIT tokens use only data.c_code. */
    t->data.c_code = buf; /* data.c_code is char* — reused for string content */
}

/* =========================================================
 * Wave literal  ~{ state:weight, ... }~   or  ~!{ ... }~
 * ========================================================= */
static void lex_wave_literal(Lexer* lex) {
    int start_line = lex->line;
    int start_col  = lex->col;

    /* We've already seen '~'.  The next char is '{' or '!' */
    lex_advance(lex); /* consume '~' */
    if (lex_cur(lex) == '!') lex_advance(lex); /* optional '!' */
    if (lex_cur(lex) == '{') lex_advance(lex); else {
        lex_error(lex, "line %d: expected '{' in wave literal", start_line);
        return;
    }

    float weights[10];
    memset(weights, 0, sizeof(weights));

    while (lex_cur(lex) != '\0') {
        /* skip whitespace */
        while (lex_cur(lex) == ' ' || lex_cur(lex) == '\t') lex_advance(lex);
        if (lex_cur(lex) == '}') {
            lex_advance(lex); /* consume '}' */
            if (lex_cur(lex) == '~') lex_advance(lex); /* consume '~' */
            break;
        }
        /* read state (single digit 0-9) */
        if (!isdigit((unsigned char)lex_cur(lex))) {
            lex_error(lex, "line %d: expected digit for wave state", start_line);
            return;
        }
        int state = lex_advance(lex) - '0';
        /* skip ws */
        while (lex_cur(lex) == ' ' || lex_cur(lex) == '\t') lex_advance(lex);
        if (lex_cur(lex) != ':') {
            lex_error(lex, "line %d: expected ':' in wave literal", start_line);
            return;
        }
        lex_advance(lex); /* consume ':' */
        /* skip ws */
        while (lex_cur(lex) == ' ' || lex_cur(lex) == '\t') lex_advance(lex);
        /* read weight (float) */
        char fbuf[64]; int flen = 0;
        if (lex_cur(lex) == '-') fbuf[flen++] = lex_advance(lex);
        while (isdigit((unsigned char)lex_cur(lex)) || lex_cur(lex) == '.') {
            fbuf[flen++] = lex_advance(lex);
            if (flen >= 62) break;
        }
        if (lex_cur(lex) == 'e' || lex_cur(lex) == 'E') {
            fbuf[flen++] = lex_advance(lex);
            if (lex_cur(lex) == '+' || lex_cur(lex) == '-') fbuf[flen++] = lex_advance(lex);
            while (isdigit((unsigned char)lex_cur(lex))) fbuf[flen++] = lex_advance(lex);
        }
        fbuf[flen] = '\0';
        float w = (float)atof(fbuf);
        if (state >= 0 && state <= 9) weights[state] = w;
        /* skip ws */
        while (lex_cur(lex) == ' ' || lex_cur(lex) == '\t') lex_advance(lex);
        if (lex_cur(lex) == ',') lex_advance(lex); /* optional comma */
    }

    Token* t = emit_tok_raw(lex);
    t->kind = TOK_WAVE_LIT;
    t->line = start_line;
    t->col  = start_col;
    snprintf(t->text, sizeof(t->text), "~{...}~");
    memcpy(t->data.wave_weights, weights, sizeof(weights));
}

/* =========================================================
 * Range literal  |[lo..hi]|
 * ========================================================= */
static void lex_range_literal(Lexer* lex) {
    int start_line = lex->line;
    int start_col  = lex->col;

    /* We've already seen '|'.  Next char must be '['. */
    lex_advance(lex); /* consume '|' */
    if (lex_cur(lex) == '[') lex_advance(lex); else {
        lex_error(lex, "line %d: expected '[' in range literal", start_line);
        return;
    }

    /* read lo */
    char lobuf[32]; int lolen = 0;
    while (isdigit((unsigned char)lex_cur(lex))) {
        lobuf[lolen++] = lex_advance(lex);
        if (lolen >= 30) break;
    }
    lobuf[lolen] = '\0';
    int lo = atoi(lobuf);

    /* expect '..' */
    if (lex_cur(lex) == '.' && lex_peek(lex, 1) == '.') {
        lex_advance(lex); lex_advance(lex);
    } else {
        lex_error(lex, "line %d: expected '..' in range literal", start_line);
        return;
    }

    /* read hi */
    char hibuf[32]; int hilen = 0;
    while (isdigit((unsigned char)lex_cur(lex))) {
        hibuf[hilen++] = lex_advance(lex);
        if (hilen >= 30) break;
    }
    hibuf[hilen] = '\0';
    int hi = atoi(hibuf);

    /* expect ']|' */
    if (lex_cur(lex) == ']') lex_advance(lex); else {
        lex_error(lex, "line %d: expected ']' in range literal", start_line);
        return;
    }
    if (lex_cur(lex) == '|') lex_advance(lex); else {
        lex_error(lex, "line %d: expected '|' to close range literal", start_line);
        return;
    }

    Token* t = emit_tok_raw(lex);
    t->kind = TOK_RANGE_LIT;
    t->line = start_line;
    t->col  = start_col;
    snprintf(t->text, sizeof(t->text), "|[%d..%d]|", lo, hi);
    t->data.range.lo = lo;
    t->data.range.hi = hi;
}

/* =========================================================
 * @ handling: decorator or c_inline block
 * ========================================================= */
static void lex_at(Lexer* lex) {
    int start_line = lex->line;
    int start_col  = lex->col;
    lex_advance(lex); /* consume '@' */

    /* Read the identifier that follows */
    char name[128]; int nlen = 0;
    while (isalnum((unsigned char)lex_cur(lex)) || lex_cur(lex) == '_') {
        name[nlen++] = lex_advance(lex);
        if (nlen >= 126) break;
    }
    name[nlen] = '\0';

    if (strcmp(name, "c_inline") == 0) {
        /* skip whitespace, expect ':' */
        skip_inline_ws(lex);
        if (lex_cur(lex) == ':') lex_advance(lex);

        /* Now collect all source lines that belong to this C block.
         * We look at the indentation of lines following.
         * The block ends when we see a line whose indent is <= the
         * indent of the '@c_inline' line itself. */

        /* Determine the indent of the @c_inline line */
        int base_indent = start_col - 1; /* 0-based col of '@' */

        /* Skip to end of current line (past the ':') */
        while (lex_cur(lex) != '\n' && lex_cur(lex) != '\0') {
            lex_advance(lex);
        }
        /* Consume the newline */
        if (lex_cur(lex) == '\n') lex_advance(lex);

        /* Collect lines */
        size_t cap = 512;
        size_t len = 0;
        char*  code = (char*)malloc(cap);
        code[0] = '\0';

        while (lex_cur(lex) != '\0') {
            /* Count leading spaces of this line */
            int spaces = 0;
            int tmp_pos = lex->pos;
            while (lex->src[tmp_pos] == ' ' || lex->src[tmp_pos] == '\t') {
                spaces++;
                tmp_pos++;
            }
            char peek = lex->src[tmp_pos];
            /* blank or comment-only line — include as empty line in block */
            if (peek == '\n' || peek == '\r' || peek == '\0') {
                /* include blank line */
            } else if (spaces <= base_indent && peek != '\0') {
                /* dedented back — block ends */
                break;
            }
            /* Collect the entire line */
            while (lex_cur(lex) != '\n' && lex_cur(lex) != '\0') {
                char c = lex_advance(lex);
                if (len + 2 >= cap) { cap *= 2; code = (char*)realloc(code, cap); }
                code[len++] = c;
            }
            if (lex_cur(lex) == '\n') {
                lex_advance(lex);
                if (len + 2 >= cap) { cap *= 2; code = (char*)realloc(code, cap); }
                code[len++] = '\n';
            }
        }
        code[len] = '\0';

        Token* t = emit_tok_raw(lex);
        t->kind = TOK_C_INLINE_START;
        t->line = start_line;
        t->col  = start_col;
        snprintf(t->text, sizeof(t->text), "@c_inline");
        t->data.c_code = code;
    } else {
        /* Regular decorator */
        Token* t = emit_tok_raw(lex);
        t->kind = TOK_DECORATOR;
        t->line = start_line;
        t->col  = start_col;
        snprintf(t->text, sizeof(t->text), "@%s", name);
        strncpy(t->data.dec_name, name, sizeof(t->data.dec_name) - 1);
    }
}

/* =========================================================
 * Identifier / keyword lexing
 * ========================================================= */
static void lex_ident_or_kw(Lexer* lex) {
    int start_line = lex->line;
    int start_col  = lex->col;
    char buf[256]; int len = 0;

    while (isalnum((unsigned char)lex_cur(lex)) || lex_cur(lex) == '_') {
        buf[len++] = lex_advance(lex);
        if (len >= 254) break;
    }
    buf[len] = '\0';

    TokKind kind = keyword_lookup(buf);
    Token* t = emit_tok_raw(lex);
    t->kind = kind;
    t->line = start_line;
    t->col  = start_col;
    strncpy(t->text, buf, sizeof(t->text) - 1);

    if (kind == TOK_TRUE) {
        t->data.int_val = 1;
    } else if (kind == TOK_FALSE) {
        t->data.int_val = 0;
    }
}

/* =========================================================
 * lexer_tokenize — main entry point
 * ========================================================= */
int lexer_tokenize(Lexer* lex) {
    /* We process the source character by character.
     * The outer loop handles line-level logic (indentation, newlines).
     * The inner loop handles tokens within a line. */

    /* Bootstrap: handle any leading blank/comment lines, then first indent */
    int at_line_start = 1;
    int first_line    = 1;

    while (!lex->had_error) {
        /* --- Beginning of a new line attempt --- */
        if (at_line_start) {
            /* Check if this line is blank or comment-only */
            int sp = count_leading_spaces(lex);
            if (sp == -1) {
                /* blank / comment-only line — skip it */
                /* Advance past leading spaces and through to the newline */
                while (lex_cur(lex) == ' ' || lex_cur(lex) == '\t') lex_advance(lex);
                if (lex_cur(lex) == '#') skip_comment(lex);
                if (lex_cur(lex) == '\n') { lex_advance(lex); continue; }
                if (lex_cur(lex) == '\0') break;
                continue;
            }
            /* Non-blank line: advance past the spaces (count_leading_spaces
               already counted them but didn't consume) */
            for (int i = 0; i < sp; i++) {
                lex_advance(lex); /* consume indent spaces */
            }
            if (!first_line) {
                handle_indent(lex, sp, lex->line);
            }
            first_line    = 0;
            at_line_start = 0;
        }

        if (lex_cur(lex) == '\0') break;

        char c = lex_cur(lex);

        /* End of line */
        if (c == '\n') {
            emit_tok(lex, TOK_NEWLINE, lex->line, lex->col);
            lex_advance(lex);
            at_line_start = 1;
            continue;
        }
        if (c == '\r') { lex_advance(lex); continue; }

        /* Inline whitespace */
        if (c == ' ' || c == '\t') { skip_inline_ws(lex); continue; }

        /* Comment */
        if (c == '#') { skip_comment(lex); continue; }

        int cur_line = lex->line;
        int cur_col  = lex->col;

        /* --- Multi-char operators (longest match) --- */

        /* ~{ or ~!{  →  wave literal */
        if (c == '~' && (lex_peek(lex, 1) == '{' || lex_peek(lex, 1) == '!')) {
            lex_wave_literal(lex);
            continue;
        }
        /* ~> */
        if (c == '~' && lex_peek(lex, 1) == '>') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_COLLAPSE, cur_line, cur_col);
            continue;
        }
        /* ~= */
        if (c == '~' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_FUZZY_EQ, cur_line, cur_col);
            continue;
        }
        /* lone ~ */
        if (c == '~') {
            lex_advance(lex);
            /* treat as unknown / error for now; we'll emit as a single char */
            lex_error(lex, "line %d col %d: unexpected '~'", cur_line, cur_col);
            continue;
        }

        /* <~ */
        if (c == '<' && lex_peek(lex, 1) == '~') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_SPREAD_OP, cur_line, cur_col);
            continue;
        }
        /* <> */
        if (c == '<' && lex_peek(lex, 1) == '>') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_RESONATE, cur_line, cur_col);
            continue;
        }
        /* <= */
        if (c == '<' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_LTE, cur_line, cur_col);
            continue;
        }
        /* < */
        if (c == '<') {
            lex_advance(lex);
            emit_tok(lex, TOK_LT, cur_line, cur_col);
            continue;
        }

        /* >< */
        if (c == '>' && lex_peek(lex, 1) == '<') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_INTERFERE, cur_line, cur_col);
            continue;
        }
        /* >= */
        if (c == '>' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_GTE, cur_line, cur_col);
            continue;
        }
        /* > */
        if (c == '>') {
            lex_advance(lex);
            emit_tok(lex, TOK_GT, cur_line, cur_col);
            continue;
        }

        /* -> */
        if (c == '-' && lex_peek(lex, 1) == '>') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_ARROW, cur_line, cur_col);
            continue;
        }
        /* -= */
        if (c == '-' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_MINUS_ASSIGN, cur_line, cur_col);
            continue;
        }
        /* - */
        if (c == '-') {
            lex_advance(lex);
            emit_tok(lex, TOK_MINUS, cur_line, cur_col);
            continue;
        }

        /* ** */
        if (c == '*' && lex_peek(lex, 1) == '*') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_POWER, cur_line, cur_col);
            continue;
        }
        /* *= */
        if (c == '*' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_STAR_ASSIGN, cur_line, cur_col);
            continue;
        }
        /* * */
        if (c == '*') {
            lex_advance(lex);
            emit_tok(lex, TOK_STAR, cur_line, cur_col);
            continue;
        }

        /* == */
        if (c == '=' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_EQ, cur_line, cur_col);
            continue;
        }
        /* = */
        if (c == '=') {
            lex_advance(lex);
            emit_tok(lex, TOK_ASSIGN, cur_line, cur_col);
            continue;
        }

        /* != */
        if (c == '!' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_NEQ, cur_line, cur_col);
            continue;
        }

        /* += */
        if (c == '+' && lex_peek(lex, 1) == '=') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_PLUS_ASSIGN, cur_line, cur_col);
            continue;
        }
        /* + */
        if (c == '+') {
            lex_advance(lex);
            emit_tok(lex, TOK_PLUS, cur_line, cur_col);
            continue;
        }

        /* .. */
        if (c == '.' && lex_peek(lex, 1) == '.') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_DOTDOT, cur_line, cur_col);
            continue;
        }
        /* . */
        if (c == '.') {
            lex_advance(lex);
            emit_tok(lex, TOK_DOT, cur_line, cur_col);
            continue;
        }

        /* |[ → range literal */
        if (c == '|' && lex_peek(lex, 1) == '[') {
            lex_range_literal(lex);
            continue;
        }

        /* @ */
        if (c == '@') {
            char next = lex_peek(lex, 1);
            if (isalpha((unsigned char)next) || next == '_') {
                lex_at(lex);
                continue;
            }
            /* bare @ = matrix multiply operator */
            lex_advance(lex);
            emit_tok(lex, TOK_MATMUL_OP, cur_line, cur_col);
            continue;
        }

        /* Strings */
        if (c == '"' || c == '\'') {
            lex_string(lex, c);
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)c)) {
            lex_number(lex);
            continue;
        }

        /* f-strings: f"..." or f'...' */
        if ((c == 'f' || c == 'F') && (lex_peek(lex, 1) == '"' || lex_peek(lex, 1) == '\'')) {
            int start_line = lex->line;
            int start_col  = lex->col;
            lex_advance(lex); /* consume 'f' or 'F' */
            char delim = lex_cur(lex);
            lex_advance(lex); /* consume opening quote */

            /* collect raw f-string content (keep {} intact for parser) */
            size_t cap = 256;
            size_t len = 0;
            char*  fbuf = (char*)malloc(cap);

            while (lex_cur(lex) != '\0' && lex_cur(lex) != delim) {
                char ch = lex_advance(lex);
                if (ch == '\\') {
                    char esc = lex_advance(lex);
                    switch (esc) {
                        case 'n':  ch = '\n'; break;
                        case 't':  ch = '\t'; break;
                        case '\\': ch = '\\'; break;
                        case '"':  ch = '"';  break;
                        case '\'': ch = '\''; break;
                        default:
                            if (len + 2 >= cap) { cap *= 2; fbuf = (char*)realloc(fbuf, cap); }
                            fbuf[len++] = '\\';
                            ch = esc;
                            break;
                    }
                }
                if (len + 1 >= cap) { cap *= 2; fbuf = (char*)realloc(fbuf, cap); }
                fbuf[len++] = ch;
            }
            if (lex_cur(lex) == delim) lex_advance(lex);
            else lex_error(lex, "line %d: unterminated f-string", start_line);
            fbuf[len] = '\0';

            Token* t = emit_tok_raw(lex);
            t->kind = TOK_FSTR_LIT;
            t->line = start_line;
            t->col  = start_col;
            snprintf(t->text, sizeof(t->text), "f\"...\"");
            t->data.c_code = fbuf;
            continue;
        }

        /* Identifiers / keywords */
        if (isalpha((unsigned char)c) || c == '_') {
            lex_ident_or_kw(lex);
            continue;
        }

        /* // floor division */
        if (c == '/' && lex_peek(lex, 1) == '/') {
            lex_advance(lex); lex_advance(lex);
            emit_tok(lex, TOK_FLOOR_DIV, cur_line, cur_col);
            continue;
        }

        /* Single-char tokens */
        lex_advance(lex);
        switch (c) {
            case '/': emit_tok(lex, TOK_SLASH,     cur_line, cur_col); break;
            case '%': emit_tok(lex, TOK_PERCENT,   cur_line, cur_col); break;
            case '?': emit_tok(lex, TOK_OBSERVE,   cur_line, cur_col); break;
            case '(': emit_tok(lex, TOK_LPAREN,    cur_line, cur_col); break;
            case ')': emit_tok(lex, TOK_RPAREN,    cur_line, cur_col); break;
            case '[': emit_tok(lex, TOK_LBRACKET,  cur_line, cur_col); break;
            case ']': emit_tok(lex, TOK_RBRACKET,  cur_line, cur_col); break;
            case '{': emit_tok(lex, TOK_LBRACE,    cur_line, cur_col); break;
            case '}': emit_tok(lex, TOK_RBRACE,    cur_line, cur_col); break;
            case ',': emit_tok(lex, TOK_COMMA,     cur_line, cur_col); break;
            case ':': emit_tok(lex, TOK_COLON,     cur_line, cur_col); break;
            case ';': emit_tok(lex, TOK_SEMICOLON, cur_line, cur_col); break;
            default:
                lex_error(lex, "line %d col %d: unexpected character '%c' (0x%02x)",
                          cur_line, cur_col, c, (unsigned char)c);
                break;
        }
    }

    if (lex->had_error) return 0;

    /* Emit remaining DEDENTs at EOF */
    flush_dedents_eof(lex, lex->line);

    /* Final EOF token */
    emit_tok(lex, TOK_EOF, lex->line, lex->col);
    return 1;
}

/* =========================================================
 * Lexer constructor / destructor
 * ========================================================= */

Lexer* lexer_new(const char* source) {
    Lexer* lex = (Lexer*)calloc(1, sizeof(Lexer));
    lex->src          = source;
    lex->pos          = 0;
    lex->line         = 1;
    lex->col          = 1;
    lex->indent_stack[0] = 0;
    lex->indent_top   = 0;
    lex->tokens       = NULL;
    lex->tok_count    = 0;
    lex->tok_cap      = 0;
    lex->pending_dedents = 0;
    lex->had_error    = 0;
    lex->error_msg[0] = '\0';
    return lex;
}

void lexer_free(Lexer* lex) {
    if (!lex) return;
    /* Free heap strings in tokens */
    for (int i = 0; i < lex->tok_count; i++) {
        Token* t = &lex->tokens[i];
        if (t->kind == TOK_C_INLINE_START || t->kind == TOK_STR_LIT || t->kind == TOK_FSTR_LIT) {
            free(t->data.c_code);
            t->data.c_code = NULL;
        }
    }
    free(lex->tokens);
    free(lex);
}

/* =========================================================
 * Debug dump
 * ========================================================= */

void lexer_dump(Lexer* lex) {
    printf("=== SPECTRA lexer dump (%d tokens) ===\n", lex->tok_count);
    for (int i = 0; i < lex->tok_count; i++) {
        Token* t = &lex->tokens[i];
        printf("[%4d]  %-20s  line=%-4d col=%-4d  text='%s'",
               i, tok_kind_name(t->kind), t->line, t->col, t->text);
        switch (t->kind) {
            case TOK_INT_LIT:
                printf("  val=%" PRId64, t->data.int_val);
                break;
            case TOK_FLOAT_LIT:
                printf("  val=%g", t->data.float_val);
                break;
            case TOK_STR_LIT:
                printf("  content='%s'", t->data.c_code ? t->data.c_code : "");
                break;
            case TOK_WAVE_LIT: {
                printf("  weights=[");
                for (int w = 0; w < 10; w++) {
                    if (w) printf(", ");
                    printf("%.3f", t->data.wave_weights[w]);
                }
                printf("]");
                break;
            }
            case TOK_RANGE_LIT:
                printf("  lo=%d hi=%d", t->data.range.lo, t->data.range.hi);
                break;
            case TOK_DECORATOR:
                printf("  name='%s'", t->data.dec_name);
                break;
            case TOK_C_INLINE_START:
                printf("  code_len=%d", t->data.c_code ? (int)strlen(t->data.c_code) : 0);
                break;
            default:
                break;
        }
        printf("\n");
    }
    if (lex->had_error) {
        printf("!!! Lexer error: %s\n", lex->error_msg);
    }
}
