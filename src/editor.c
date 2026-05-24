/* editor.c — SPECTRA TUI code editor for Windows
 * Uses Windows Console API + ANSI escape codes.
 * Compile: gcc -std=c99 -D_WIN32_WINNT=0x0600 editor.c -o editor
 */

#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "editor.h"

/* =========================================================
 * Constants
 * ========================================================= */

#define EDITOR_MAX_LINES  65536
#define EDITOR_UNDO_MAX   50
#define TAB_SIZE          4

#define HL_NORMAL    0   /* white */
#define HL_KEYWORD   1   /* cyan */
#define HL_TYPE      2   /* green */
#define HL_STRING    3   /* yellow */
#define HL_NUMBER    4   /* magenta */
#define HL_COMMENT   5   /* dark gray */
#define HL_OPERATOR  6   /* bright white */

/* =========================================================
 * Data structures
 * ========================================================= */

typedef struct {
    char* data;   /* heap-allocated line content (no \n) */
    int   len;
    int   cap;
    int*  hl;     /* highlight type per char, same size as data */
} ERow;

typedef struct {
    ERow* rows;
    int   num_rows;
    int   cur_row;
    int   cur_col;
} UndoState;

typedef struct {
    HANDLE hIn, hOut;
    DWORD  orig_in_mode, orig_out_mode;
    int    screen_rows, screen_cols;

    ERow*  rows;
    int    num_rows;
    int    row_cap;

    int    cur_row, cur_col;
    int    view_row, view_col;

    char   filename[260];
    int    dirty;

    char   status_msg[256];
    char   find_query[128];
    int    find_active;

    UndoState undo[EDITOR_UNDO_MAX];
    int       undo_head;
    int       undo_count;

    int    quit_confirm; /* 1 = already warned about unsaved */
} Editor;

/* Global editor (needed for atexit cleanup) */
static Editor* g_editor = NULL;

/* =========================================================
 * Output buffer
 * ========================================================= */

typedef struct {
    char* buf;
    int   len;
    int   cap;
} OutBuf;

static void ob_init(OutBuf* ob) {
    ob->buf = (char*)malloc(65536);
    ob->len = 0;
    ob->cap = 65536;
}

static void ob_free(OutBuf* ob) {
    free(ob->buf);
    ob->buf = NULL;
    ob->len = 0;
    ob->cap = 0;
}

static void ob_append(OutBuf* ob, const char* s, int slen) {
    if (slen <= 0) return;
    while (ob->len + slen >= ob->cap) {
        ob->cap *= 2;
        ob->buf = (char*)realloc(ob->buf, (size_t)ob->cap);
    }
    memcpy(ob->buf + ob->len, s, (size_t)slen);
    ob->len += slen;
}

static void ob_appendz(OutBuf* ob, const char* s) {
    ob_append(ob, s, (int)strlen(s));
}

static void ob_flush(Editor* E, OutBuf* ob) {
    DWORD written;
    WriteConsoleA(E->hOut, ob->buf, (DWORD)ob->len, &written, NULL);
}

/* =========================================================
 * Row helpers
 * ========================================================= */

static void row_ensure_cap(ERow* row, int needed) {
    if (needed >= row->cap) {
        int new_cap = (needed + 1) * 2;
        row->data = (char*)realloc(row->data, (size_t)new_cap);
        row->hl   = (int*)realloc(row->hl,   (size_t)new_cap * sizeof(int));
        row->cap  = new_cap;
    }
}

static void row_init(ERow* row) {
    row->data = (char*)malloc(16);
    row->hl   = (int*)malloc(16 * sizeof(int));
    row->len  = 0;
    row->cap  = 16;
    row->data[0] = '\0';
}

static void row_free(ERow* row) {
    free(row->data);
    free(row->hl);
    row->data = NULL;
    row->hl   = NULL;
}

static void row_set(ERow* row, const char* s, int slen) {
    row_ensure_cap(row, slen + 1);
    memcpy(row->data, s, (size_t)slen);
    row->data[slen] = '\0';
    row->len = slen;
}

static void row_insert_char(ERow* row, int at, char c) {
    if (at < 0) at = 0;
    if (at > row->len) at = row->len;
    row_ensure_cap(row, row->len + 2);
    memmove(row->data + at + 1, row->data + at, (size_t)(row->len - at));
    memmove(row->hl   + at + 1, row->hl   + at, (size_t)(row->len - at) * sizeof(int));
    row->data[at] = c;
    row->hl[at]   = HL_NORMAL;
    row->len++;
    row->data[row->len] = '\0';
}

static void row_delete_char(ERow* row, int at) {
    if (at < 0 || at >= row->len) return;
    memmove(row->data + at, row->data + at + 1, (size_t)(row->len - at));
    memmove(row->hl   + at, row->hl   + at + 1, (size_t)(row->len - at) * sizeof(int));
    row->len--;
}

static void row_append(ERow* row, const char* s, int slen) {
    row_ensure_cap(row, row->len + slen + 1);
    memcpy(row->data + row->len, s, (size_t)slen);
    row->len += slen;
    row->data[row->len] = '\0';
}

/* =========================================================
 * Editor row management
 * ========================================================= */

static void editor_ensure_row_cap(Editor* E, int needed) {
    if (needed >= E->row_cap) {
        int new_cap = (needed + 1) * 2;
        E->rows = (ERow*)realloc(E->rows, (size_t)new_cap * sizeof(ERow));
        E->row_cap = new_cap;
    }
}

static void editor_insert_row(Editor* E, int at, const char* s, int slen) {
    if (at < 0 || at > E->num_rows) at = E->num_rows;
    editor_ensure_row_cap(E, E->num_rows + 1);
    memmove(&E->rows[at + 1], &E->rows[at],
            (size_t)(E->num_rows - at) * sizeof(ERow));
    row_init(&E->rows[at]);
    row_set(&E->rows[at], s, slen);
    E->num_rows++;
}

static void editor_delete_row(Editor* E, int at) {
    if (at < 0 || at >= E->num_rows) return;
    row_free(&E->rows[at]);
    memmove(&E->rows[at], &E->rows[at + 1],
            (size_t)(E->num_rows - at - 1) * sizeof(ERow));
    E->num_rows--;
}

/* =========================================================
 * Syntax highlighting
 * ========================================================= */

static const char* keywords[] = {
    "func", "struct", "if", "elif", "else", "for", "while", "return",
    "import", "from", "as", "match", "wave_if", "and", "or", "not",
    "true", "false", "null", "self", "in", "wave_for", NULL
};

static const char* types[] = {
    "spect", "int", "float", "str", "bool", "byte", NULL
};

/* Multi-char operators to highlight */
static const char* operators[] = {
    "~>", "<>", "><", "~=", "~{", "}~", "|[", "]|", NULL
};

static int is_separator(char c) {
    return c == '\0' || isspace((unsigned char)c) ||
           strchr("(){}[].,;:!&|^%*/+-=<>~#\"'", c) != NULL;
}

static void highlight_row(ERow* row) {
    if (!row->data) return;
    /* Ensure hl buffer is large enough */
    row_ensure_cap(row, row->len + 1);
    memset(row->hl, HL_NORMAL, (size_t)row->len * sizeof(int));

    int in_string = 0;
    int i = 0;

    while (i < row->len) {
        char c = row->data[i];

        /* Comment: # to end of line */
        if (!in_string && c == '#') {
            for (int j = i; j < row->len; j++)
                row->hl[j] = HL_COMMENT;
            break;
        }

        /* String literal */
        if (!in_string && c == '"') {
            in_string = 1;
            row->hl[i] = HL_STRING;
            i++;
            continue;
        }
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->len) {
                row->hl[i + 1] = HL_STRING;
                i += 2;
            } else {
                if (c == '"') in_string = 0;
                i++;
            }
            continue;
        }

        /* Multi-char operators */
        int found_op = 0;
        for (int oi = 0; operators[oi]; oi++) {
            int oplen = (int)strlen(operators[oi]);
            if (i + oplen <= row->len &&
                strncmp(row->data + i, operators[oi], (size_t)oplen) == 0) {
                for (int j = 0; j < oplen; j++)
                    row->hl[i + j] = HL_OPERATOR;
                i += oplen;
                found_op = 1;
                break;
            }
        }
        if (found_op) continue;

        /* Number */
        if (isdigit((unsigned char)c) &&
            (i == 0 || is_separator(row->data[i - 1]))) {
            while (i < row->len && (isdigit((unsigned char)row->data[i]) ||
                                    row->data[i] == '.')) {
                row->hl[i] = HL_NUMBER;
                i++;
            }
            continue;
        }

        /* Keywords and types */
        if (isalpha((unsigned char)c) || c == '_') {
            int start = i;
            while (i < row->len &&
                   (isalnum((unsigned char)row->data[i]) || row->data[i] == '_'))
                i++;
            int wlen = i - start;
            int matched = 0;

            for (int ki = 0; keywords[ki]; ki++) {
                int klen = (int)strlen(keywords[ki]);
                if (klen == wlen &&
                    strncmp(row->data + start, keywords[ki], (size_t)klen) == 0) {
                    for (int j = start; j < i; j++)
                        row->hl[j] = HL_KEYWORD;
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                for (int ti = 0; types[ti]; ti++) {
                    int tlen = (int)strlen(types[ti]);
                    if (tlen == wlen &&
                        strncmp(row->data + start, types[ti], (size_t)tlen) == 0) {
                        for (int j = start; j < i; j++)
                            row->hl[j] = HL_TYPE;
                        matched = 1;
                        break;
                    }
                }
            }
            /* HL_NORMAL already set for non-keyword words */
            continue;
        }

        i++;
    }
}

static void highlight_all(Editor* E) {
    for (int i = 0; i < E->num_rows; i++)
        highlight_row(&E->rows[i]);
}

/* Also highlight find matches in every row */
static void highlight_find(Editor* E) {
    if (!E->find_active || E->find_query[0] == '\0') return;
    int qlen = (int)strlen(E->find_query);
    for (int r = 0; r < E->num_rows; r++) {
        ERow* row = &E->rows[r];
        char* p = row->data;
        while ((p = strstr(p, E->find_query)) != NULL) {
            int off = (int)(p - row->data);
            for (int j = 0; j < qlen && off + j < row->len; j++)
                row->hl[off + j] = HL_OPERATOR; /* reuse bright-white for match */
            p += qlen;
        }
    }
}

/* =========================================================
 * Undo
 * ========================================================= */

static ERow* copy_rows(ERow* src, int n) {
    if (n == 0) return NULL;
    ERow* dst = (ERow*)malloc((size_t)n * sizeof(ERow));
    for (int i = 0; i < n; i++) {
        dst[i].len  = src[i].len;
        dst[i].cap  = src[i].len + 1;
        dst[i].data = (char*)malloc((size_t)dst[i].cap);
        dst[i].hl   = (int*)malloc((size_t)dst[i].cap * sizeof(int));
        memcpy(dst[i].data, src[i].data, (size_t)(src[i].len + 1));
        if (src[i].hl)
            memcpy(dst[i].hl, src[i].hl, (size_t)src[i].len * sizeof(int));
    }
    return dst;
}

static void free_undo_rows(UndoState* u) {
    if (!u->rows) return;
    for (int i = 0; i < u->num_rows; i++) {
        free(u->rows[i].data);
        free(u->rows[i].hl);
    }
    free(u->rows);
    u->rows = NULL;
    u->num_rows = 0;
}

static void editor_push_undo(Editor* E) {
    /* Free oldest entry if buffer full */
    if (E->undo_count == EDITOR_UNDO_MAX) {
        int oldest = (E->undo_head - E->undo_count + EDITOR_UNDO_MAX) % EDITOR_UNDO_MAX;
        free_undo_rows(&E->undo[oldest]);
        E->undo_count--;
    }
    int slot = E->undo_head;
    E->undo_head = (E->undo_head + 1) % EDITOR_UNDO_MAX;
    E->undo_count++;
    E->undo[slot].rows     = copy_rows(E->rows, E->num_rows);
    E->undo[slot].num_rows = E->num_rows;
    E->undo[slot].cur_row  = E->cur_row;
    E->undo[slot].cur_col  = E->cur_col;
}

static void editor_do_undo(Editor* E) {
    if (E->undo_count == 0) {
        snprintf(E->status_msg, sizeof(E->status_msg), "Nothing to undo.");
        return;
    }
    E->undo_head = (E->undo_head - 1 + EDITOR_UNDO_MAX) % EDITOR_UNDO_MAX;
    E->undo_count--;
    UndoState* u = &E->undo[E->undo_head];

    /* Free current rows */
    for (int i = 0; i < E->num_rows; i++)
        row_free(&E->rows[i]);

    E->num_rows = u->num_rows;
    editor_ensure_row_cap(E, u->num_rows);
    for (int i = 0; i < u->num_rows; i++) {
        row_init(&E->rows[i]);
        row_set(&E->rows[i], u->rows[i].data, u->rows[i].len);
    }
    E->cur_row = u->cur_row;
    E->cur_col = u->cur_col;

    free_undo_rows(u);
    highlight_all(E);
    E->dirty = 1;
    snprintf(E->status_msg, sizeof(E->status_msg), "Undo.");
}

/* =========================================================
 * Terminal setup / teardown
 * ========================================================= */

static void editor_restore_terminal(void) {
    if (!g_editor) return;
    SetConsoleMode(g_editor->hIn,  g_editor->orig_in_mode);
    SetConsoleMode(g_editor->hOut, g_editor->orig_out_mode);
    /* Show cursor and reset colors */
    DWORD w;
    const char* reset = "\x1b[?25h\x1b[0m\x1b[2J\x1b[H";
    WriteConsoleA(g_editor->hOut, reset, (DWORD)strlen(reset), &w, NULL);
}

static void editor_setup_terminal(Editor* E) {
    E->hIn  = GetStdHandle(STD_INPUT_HANDLE);
    E->hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleMode(E->hIn,  &E->orig_in_mode);
    GetConsoleMode(E->hOut, &E->orig_out_mode);

    /* Raw input mode */
    DWORD in_mode = E->orig_in_mode;
    in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(E->hIn, in_mode);

    /* Enable ANSI / VT processing on output */
    DWORD out_mode = E->orig_out_mode;
    out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(E->hOut, out_mode);

    g_editor = E;
    atexit(editor_restore_terminal);

    /* Get terminal size */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(E->hOut, &csbi)) {
        E->screen_cols = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        E->screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    }
    if (E->screen_cols <= 0) E->screen_cols = 80;
    if (E->screen_rows <= 0) E->screen_rows = 24;

    SetConsoleTitleA("SPECTRA Editor v0.1");
}

/* =========================================================
 * File I/O
 * ========================================================= */

static void editor_open(Editor* E, const char* filename) {
    strncpy(E->filename, filename, sizeof(E->filename) - 1);
    E->filename[sizeof(E->filename) - 1] = '\0';

    FILE* f = fopen(filename, "r");
    if (!f) {
        /* New file — start with one empty row */
        editor_insert_row(E, 0, "", 0);
        snprintf(E->status_msg, sizeof(E->status_msg),
                 "New file: %s", filename);
        return;
    }

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        /* Strip \r\n / \n */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        editor_insert_row(E, E->num_rows, line, len);
    }
    fclose(f);

    if (E->num_rows == 0)
        editor_insert_row(E, 0, "", 0);

    highlight_all(E);
    snprintf(E->status_msg, sizeof(E->status_msg), "Opened %s", filename);
}

static void editor_save(Editor* E) {
    if (E->filename[0] == '\0') {
        snprintf(E->status_msg, sizeof(E->status_msg),
                 "No filename. Use Ctrl+S after setting filename.");
        return;
    }

    FILE* f = fopen(E->filename, "w");
    if (!f) {
        snprintf(E->status_msg, sizeof(E->status_msg),
                 "Error saving: %s", E->filename);
        return;
    }
    for (int i = 0; i < E->num_rows; i++) {
        fwrite(E->rows[i].data, 1, (size_t)E->rows[i].len, f);
        fputc('\n', f);
    }
    fclose(f);
    E->dirty = 0;
    snprintf(E->status_msg, sizeof(E->status_msg), "Saved: %s", E->filename);
}

/* =========================================================
 * Rendering
 * ========================================================= */

/* ANSI color for a highlight type */
static const char* hl_color(int hl) {
    switch (hl) {
        case HL_KEYWORD:  return "\x1b[36m";   /* cyan */
        case HL_TYPE:     return "\x1b[32m";   /* green */
        case HL_STRING:   return "\x1b[33m";   /* yellow */
        case HL_NUMBER:   return "\x1b[35m";   /* magenta */
        case HL_COMMENT:  return "\x1b[90m";   /* dark gray */
        case HL_OPERATOR: return "\x1b[97m";   /* bright white */
        default:          return "\x1b[0m";    /* reset / normal */
    }
}

/* Convert a column in the logical row to a render column (tab expansion) */
static int col_to_render(ERow* row, int col) {
    int rx = 0;
    for (int i = 0; i < col && i < row->len; i++) {
        if (row->data[i] == '\t')
            rx += TAB_SIZE - (rx % TAB_SIZE);
        else
            rx++;
    }
    return rx;
}

static void editor_draw_rows(Editor* E, OutBuf* ob) {
    /* Line-number column width: 4 digits + '|' + ' ' = 6 chars */
    const int LNUM_WIDTH = 6;
    const int text_cols  = E->screen_cols - LNUM_WIDTH;

    for (int screen_r = 0; screen_r < E->screen_rows - 2; screen_r++) {
        int file_r = screen_r + E->view_row;

        /* Line number */
        char lnum[16];
        if (file_r < E->num_rows)
            snprintf(lnum, sizeof(lnum), "\x1b[90m%4d\x1b[0m\xc2\xb7 ", file_r + 1);
        else
            snprintf(lnum, sizeof(lnum), "     \x1b[90m~\x1b[0m");
        ob_appendz(ob, lnum);

        if (file_r < E->num_rows) {
            ERow* row = &E->rows[file_r];
            int cur_hl = HL_NORMAL;
            int drawn  = 0;

            /* Render characters with tab expansion, respecting view_col */
            int rx = 0; /* render column */
            for (int ci = 0; ci < row->len && drawn < text_cols; ci++) {
                int next_rx = (row->data[ci] == '\t')
                              ? rx + TAB_SIZE - (rx % TAB_SIZE)
                              : rx + 1;

                if (next_rx <= E->view_col) {
                    /* Still before visible area */
                    rx = next_rx;
                    continue;
                }

                /* Apply color change if needed */
                int this_hl = row->hl ? row->hl[ci] : HL_NORMAL;
                if (this_hl != cur_hl) {
                    ob_appendz(ob, hl_color(this_hl));
                    cur_hl = this_hl;
                }

                if (row->data[ci] == '\t') {
                    int spaces = TAB_SIZE - (rx % TAB_SIZE);
                    while (spaces-- > 0 && drawn < text_cols) {
                        ob_append(ob, " ", 1);
                        drawn++;
                    }
                } else {
                    ob_append(ob, &row->data[ci], 1);
                    drawn++;
                }
                rx = next_rx;
            }
            /* Reset color at end of line */
            if (cur_hl != HL_NORMAL)
                ob_appendz(ob, "\x1b[0m");
        }

        /* Clear to end of line */
        ob_appendz(ob, "\x1b[K\r\n");
    }
}

static void editor_draw_status(Editor* E, OutBuf* ob) {
    /* Status bar: inverted colors */
    ob_appendz(ob, "\x1b[7m"); /* reverse video */

    char left[128], right[128], line[512];
    const char* mod = E->dirty ? " [modified]" : "";
    const char* fname = E->filename[0] ? E->filename : "[No Name]";

    snprintf(left,  sizeof(left),  " %.40s%s", fname, mod);
    snprintf(right, sizeof(right), "Row %d/%d  Col %d | SPECTRA ",
             E->cur_row + 1, E->num_rows, E->cur_col + 1);

    int llen = (int)strlen(left);
    int rlen = (int)strlen(right);
    int padding = E->screen_cols - llen - rlen;

    snprintf(line, sizeof(line), "%s", left);
    int written = llen;
    while (written < E->screen_cols - rlen && written < (int)sizeof(line) - 2) {
        line[written++] = ' ';
    }
    line[written] = '\0';
    /* Append right-aligned portion */
    int buf_left = (int)sizeof(line) - written - 1;
    if (buf_left > 0)
        strncat(line, right, (size_t)buf_left);

    /* Truncate to screen width */
    line[E->screen_cols < (int)sizeof(line) ? E->screen_cols : (int)sizeof(line) - 1] = '\0';

    ob_appendz(ob, line);
    ob_appendz(ob, "\x1b[0m\r\n"); /* reset */
    (void)padding;
}

static void editor_draw_message(Editor* E, OutBuf* ob) {
    ob_appendz(ob, "\x1b[K"); /* clear line */
    if (E->find_active) {
        char msg[160];
        snprintf(msg, sizeof(msg), "\x1b[33mFind: \x1b[0m%.120s", E->find_query);
        ob_appendz(ob, msg);
    } else if (E->status_msg[0]) {
        /* Show message, truncated to screen width */
        char msg[300];
        snprintf(msg, sizeof(msg), "%.250s", E->status_msg);
        msg[E->screen_cols < (int)sizeof(msg) ? E->screen_cols : (int)sizeof(msg) - 1] = '\0';
        ob_appendz(ob, msg);
    }
}

static void editor_scroll(Editor* E) {
    /* Vertical scroll */
    if (E->cur_row < E->view_row)
        E->view_row = E->cur_row;
    if (E->cur_row >= E->view_row + E->screen_rows - 2)
        E->view_row = E->cur_row - (E->screen_rows - 3);

    /* Horizontal scroll (render column) */
    int rx = 0;
    if (E->cur_row < E->num_rows)
        rx = col_to_render(&E->rows[E->cur_row], E->cur_col);

    if (rx < E->view_col)
        E->view_col = rx;
    const int LNUM_WIDTH = 6;
    const int text_cols  = E->screen_cols - LNUM_WIDTH;
    if (rx >= E->view_col + text_cols)
        E->view_col = rx - text_cols + 1;
}

static void editor_refresh(Editor* E) {
    /* Re-highlight with any active search */
    highlight_all(E);
    if (E->find_active)
        highlight_find(E);

    editor_scroll(E);

    OutBuf ob;
    ob_init(&ob);

    /* Hide cursor while drawing */
    ob_appendz(&ob, "\x1b[?25l");
    /* Move to top-left */
    ob_appendz(&ob, "\x1b[H");

    editor_draw_rows(E, &ob);
    editor_draw_status(E, &ob);
    editor_draw_message(E, &ob);

    /* Position cursor */
    const int LNUM_WIDTH = 6;
    int rx = 0;
    if (E->cur_row < E->num_rows)
        rx = col_to_render(&E->rows[E->cur_row], E->cur_col);

    int screen_c = rx - E->view_col + LNUM_WIDTH + 1; /* 1-based */
    int screen_r = E->cur_row - E->view_row + 1;       /* 1-based */

    char cursor_pos[32];
    snprintf(cursor_pos, sizeof(cursor_pos), "\x1b[%d;%dH", screen_r, screen_c);
    ob_appendz(&ob, cursor_pos);

    /* Show cursor */
    ob_appendz(&ob, "\x1b[?25h");

    ob_flush(E, &ob);
    ob_free(&ob);
}

/* =========================================================
 * Cursor movement
 * ========================================================= */

static void editor_clamp_col(Editor* E) {
    if (E->cur_row >= E->num_rows) return;
    int maxcol = E->rows[E->cur_row].len;
    if (E->cur_col > maxcol) E->cur_col = maxcol;
    if (E->cur_col < 0)      E->cur_col = 0;
}

static void editor_move_cursor(Editor* E, WORD vk) {
    switch (vk) {
        case VK_UP:
            if (E->cur_row > 0) E->cur_row--;
            editor_clamp_col(E);
            break;
        case VK_DOWN:
            if (E->cur_row < E->num_rows - 1) E->cur_row++;
            editor_clamp_col(E);
            break;
        case VK_LEFT:
            if (E->cur_col > 0) {
                E->cur_col--;
            } else if (E->cur_row > 0) {
                E->cur_row--;
                E->cur_col = E->rows[E->cur_row].len;
            }
            break;
        case VK_RIGHT:
            if (E->cur_row < E->num_rows) {
                if (E->cur_col < E->rows[E->cur_row].len) {
                    E->cur_col++;
                } else if (E->cur_row < E->num_rows - 1) {
                    E->cur_row++;
                    E->cur_col = 0;
                }
            }
            break;
        case VK_HOME:
            E->cur_col = 0;
            break;
        case VK_END:
            if (E->cur_row < E->num_rows)
                E->cur_col = E->rows[E->cur_row].len;
            break;
        case VK_PRIOR: /* Page Up */
            E->cur_row -= (E->screen_rows - 2);
            if (E->cur_row < 0) E->cur_row = 0;
            editor_clamp_col(E);
            break;
        case VK_NEXT: /* Page Down */
            E->cur_row += (E->screen_rows - 2);
            if (E->cur_row >= E->num_rows) E->cur_row = E->num_rows - 1;
            editor_clamp_col(E);
            break;
    }
}

/* =========================================================
 * Editing operations
 * ========================================================= */

static void editor_insert_char(Editor* E, char c) {
    if (E->cur_row == E->num_rows)
        editor_insert_row(E, E->num_rows, "", 0);
    row_insert_char(&E->rows[E->cur_row], E->cur_col, c);
    E->cur_col++;
    E->dirty = 1;
    highlight_row(&E->rows[E->cur_row]);
}

static void editor_insert_newline(Editor* E) {
    if (E->cur_row >= E->num_rows) {
        editor_insert_row(E, E->num_rows, "", 0);
        E->cur_row++;
        E->cur_col = 0;
        return;
    }
    ERow* row = &E->rows[E->cur_row];
    /* Text after cursor goes to new row */
    editor_insert_row(E, E->cur_row + 1,
                      row->data + E->cur_col,
                      row->len  - E->cur_col);
    /* Truncate current row */
    row->len = E->cur_col;
    row->data[row->len] = '\0';
    E->cur_row++;
    E->cur_col = 0;
    E->dirty = 1;
    highlight_row(&E->rows[E->cur_row - 1]);
    highlight_row(&E->rows[E->cur_row]);
}

static void editor_delete_char(Editor* E) {
    if (E->cur_row >= E->num_rows) return;
    if (E->cur_col == 0 && E->cur_row == 0) return;

    if (E->cur_col > 0) {
        row_delete_char(&E->rows[E->cur_row], E->cur_col - 1);
        E->cur_col--;
        highlight_row(&E->rows[E->cur_row]);
    } else {
        /* Merge this row into previous */
        int prev_len = E->rows[E->cur_row - 1].len;
        row_append(&E->rows[E->cur_row - 1],
                   E->rows[E->cur_row].data,
                   E->rows[E->cur_row].len);
        editor_delete_row(E, E->cur_row);
        E->cur_row--;
        E->cur_col = prev_len;
        highlight_row(&E->rows[E->cur_row]);
    }
    E->dirty = 1;
}

static void editor_delete_forward(Editor* E) {
    if (E->cur_row >= E->num_rows) return;
    ERow* row = &E->rows[E->cur_row];
    if (E->cur_col == row->len) {
        /* Delete newline: merge with next row */
        if (E->cur_row < E->num_rows - 1) {
            row_append(row, E->rows[E->cur_row + 1].data,
                           E->rows[E->cur_row + 1].len);
            editor_delete_row(E, E->cur_row + 1);
            highlight_row(&E->rows[E->cur_row]);
            E->dirty = 1;
        }
    } else {
        row_delete_char(row, E->cur_col);
        highlight_row(row);
        E->dirty = 1;
    }
}

/* =========================================================
 * Find
 * ========================================================= */

static void editor_find_exec(Editor* E) {
    if (E->find_query[0] == '\0') return;
    int qlen = (int)strlen(E->find_query);

    /* Search forward from cursor position */
    int start_row = E->cur_row;
    int start_col = E->cur_col + 1; /* skip current match */

    for (int pass = 0; pass < 2; pass++) {
        int r_start = (pass == 0) ? start_row : 0;
        for (int r = r_start; r < E->num_rows; r++) {
            ERow* row = &E->rows[r];
            int   col_start = (pass == 0 && r == start_row) ? start_col : 0;
            char* p = (col_start < row->len)
                      ? strstr(row->data + col_start, E->find_query)
                      : NULL;
            if (!p && pass == 0 && r == start_row && col_start > 0) {
                /* Also check from beginning of same line */
                p = strstr(row->data, E->find_query);
                if (p && (int)(p - row->data) >= start_col) p = NULL;
            }
            if (p) {
                E->cur_row = r;
                E->cur_col = (int)(p - row->data);
                snprintf(E->status_msg, sizeof(E->status_msg),
                         "Found '%s' at line %d", E->find_query, r + 1);
                return;
            }
        }
        start_row = 0;
        start_col = 0;
    }
    snprintf(E->status_msg, sizeof(E->status_msg),
             "Not found: '%s'", E->find_query);
}

/* =========================================================
 * Input processing
 * ========================================================= */

/* Returns 1 if editor should quit */
static int editor_process_key(Editor* E, INPUT_RECORD* ir) {
    if (ir->EventType != KEY_EVENT) return 0;
    KEY_EVENT_RECORD* ke = &ir->Event.KeyEvent;
    if (!ke->bKeyDown) return 0;

    WORD  vk    = ke->wVirtualKeyCode;
    DWORD ctrl  = ke->dwControlKeyState;
    char  ch    = ke->uChar.AsciiChar;
    int   ctrl_down = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

    /* ── Find mode ── */
    if (E->find_active) {
        if (vk == VK_ESCAPE) {
            E->find_active = 0;
            E->find_query[0] = '\0';
            snprintf(E->status_msg, sizeof(E->status_msg), "Find cancelled.");
        } else if (vk == VK_RETURN) {
            editor_find_exec(E);
            E->find_active = 0;
        } else if (vk == VK_BACK) {
            int qlen = (int)strlen(E->find_query);
            if (qlen > 0) E->find_query[qlen - 1] = '\0';
        } else if (ch >= 0x20 && ch < 0x7f) {
            int qlen = (int)strlen(E->find_query);
            if (qlen < (int)sizeof(E->find_query) - 1) {
                E->find_query[qlen]     = ch;
                E->find_query[qlen + 1] = '\0';
            }
        }
        return 0;
    }

    /* ── Ctrl combos ── */
    if (ctrl_down) {
        switch (vk) {
            case 'Q':
                if (E->dirty && !E->quit_confirm) {
                    snprintf(E->status_msg, sizeof(E->status_msg),
                             "Unsaved changes! Press Ctrl+Q again to quit.");
                    E->quit_confirm = 1;
                    return 0;
                }
                return 1; /* quit */

            case 'S':
                editor_push_undo(E);
                editor_save(E);
                E->quit_confirm = 0;
                return 0;

            case 'Z':
                editor_do_undo(E);
                E->quit_confirm = 0;
                return 0;

            case 'F':
                E->find_active = 1;
                E->find_query[0] = '\0';
                snprintf(E->status_msg, sizeof(E->status_msg),
                         "Find: (type query, Enter=go, Esc=cancel)");
                return 0;

            case 'H': /* Ctrl+H = backspace in some terminals */
                editor_push_undo(E);
                editor_delete_char(E);
                E->quit_confirm = 0;
                return 0;
        }
        /* Other Ctrl combos: ignore */
        return 0;
    }

    /* ── Navigation keys ── */
    switch (vk) {
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
            editor_move_cursor(E, vk);
            E->quit_confirm = 0;
            return 0;

        case VK_BACK:
            editor_push_undo(E);
            editor_delete_char(E);
            E->dirty = 1;
            E->quit_confirm = 0;
            return 0;

        case VK_DELETE:
            editor_push_undo(E);
            editor_delete_forward(E);
            E->quit_confirm = 0;
            return 0;

        case VK_RETURN:
            editor_push_undo(E);
            editor_insert_newline(E);
            E->quit_confirm = 0;
            return 0;

        case VK_TAB:
            /* Insert spaces for tab */
            editor_push_undo(E);
            for (int t = 0; t < TAB_SIZE; t++)
                editor_insert_char(E, ' ');
            E->quit_confirm = 0;
            return 0;

        case VK_ESCAPE:
            E->status_msg[0] = '\0';
            E->quit_confirm = 0;
            return 0;
    }

    /* ── Printable characters ── */
    if (ch >= 0x20 && ch < 0x7f) {
        editor_push_undo(E);
        editor_insert_char(E, ch);
        E->quit_confirm = 0;
    }

    return 0;
}

/* =========================================================
 * editor_run — public entry point
 * ========================================================= */

void editor_run(const char* filename) {
    Editor E;
    memset(&E, 0, sizeof(E));

    /* Allocate row storage */
    E.row_cap = 64;
    E.rows    = (ERow*)malloc((size_t)E.row_cap * sizeof(ERow));
    if (!E.rows) return;

    editor_setup_terminal(&E);

    if (filename && filename[0]) {
        editor_open(&E, filename);
    } else {
        editor_insert_row(&E, 0, "", 0);
        snprintf(E.status_msg, sizeof(E.status_msg),
                 "New buffer | Ctrl+S save | Ctrl+Q quit | Ctrl+F find | Ctrl+Z undo");
    }

    /* Main event loop */
    for (;;) {
        editor_refresh(&E);

        INPUT_RECORD ir;
        DWORD count = 0;
        if (!ReadConsoleInput(E.hIn, &ir, 1, &count)) break;
        if (count == 0) continue;

        /* Handle window resize */
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (GetConsoleScreenBufferInfo(E.hOut, &csbi)) {
                E.screen_cols = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
                E.screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
            }
            if (E.screen_cols <= 0) E.screen_cols = 80;
            if (E.screen_rows <= 0) E.screen_rows = 24;
            continue;
        }

        if (editor_process_key(&E, &ir)) break; /* quit */
    }

    /* Cleanup */
    editor_restore_terminal();
    g_editor = NULL;

    /* Free rows */
    for (int i = 0; i < E.num_rows; i++)
        row_free(&E.rows[i]);
    free(E.rows);

    /* Free undo states */
    for (int i = 0; i < EDITOR_UNDO_MAX; i++)
        free_undo_rows(&E.undo[i]);
}
