/* opcode.c — Chunk implementation for the SPECTRA bytecode VM */

#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * chunk_init
 * ====================================================================== */

void chunk_init(Chunk* c) {
    c->code        = NULL;
    c->count       = 0;
    c->cap         = 0;
    c->lines       = NULL;
    c->constants   = NULL;
    c->const_count = 0;
    c->const_cap   = 0;
    c->strings     = NULL;
    c->str_count   = 0;
    c->str_cap     = 0;
}

/* =========================================================================
 * chunk_free
 * ====================================================================== */

void chunk_free(Chunk* c) {
    if (!c) return;

    free(c->code);
    free(c->lines);

    for (int i = 0; i < c->const_count; i++) {
        if (c->constants[i]) {
            val_release(c->constants[i]);
        }
    }
    free(c->constants);

    for (int i = 0; i < c->str_count; i++) {
        free(c->strings[i]);
    }
    free(c->strings);

    chunk_init(c);
}

/* =========================================================================
 * chunk_write — append a single raw byte
 * ====================================================================== */

void chunk_write(Chunk* c, uint8_t byte, int line) {
    if (c->count >= c->cap) {
        int new_cap = (c->cap < 8) ? 8 : c->cap * 2;
        c->code  = (uint8_t*)realloc(c->code,  (size_t)new_cap * sizeof(uint8_t));
        c->lines = (int*)    realloc(c->lines, (size_t)new_cap * sizeof(int));
        if (!c->code || !c->lines) {
            fprintf(stderr, "fatal: out of memory in chunk_write\n");
            exit(1);
        }
        c->cap = new_cap;
    }
    c->code[c->count]  = byte;
    c->lines[c->count] = line;
    c->count++;
}

/* =========================================================================
 * chunk_write_op — append opcode + 2-byte little-endian operand
 * ====================================================================== */

void chunk_write_op(Chunk* c, uint8_t op, uint16_t operand, int line) {
    chunk_write(c, op, line);
    chunk_write(c, (uint8_t)(operand & 0xFF), line);
    chunk_write(c, (uint8_t)((operand >> 8) & 0xFF), line);
}

/* =========================================================================
 * chunk_add_const — add value to constant pool, return index
 * ====================================================================== */

int chunk_add_const(Chunk* c, Value* v) {
    if (c->const_count >= c->const_cap) {
        int new_cap = (c->const_cap < 8) ? 8 : c->const_cap * 2;
        c->constants = (Value**)realloc(c->constants, (size_t)new_cap * sizeof(Value*));
        if (!c->constants) {
            fprintf(stderr, "fatal: out of memory in chunk_add_const\n");
            exit(1);
        }
        c->const_cap = new_cap;
    }
    c->constants[c->const_count] = v;  /* takes ownership — caller must NOT release */
    return c->const_count++;
}

/* =========================================================================
 * chunk_intern_str — intern a variable/attribute name, return index
 * ====================================================================== */

int chunk_intern_str(Chunk* c, const char* s) {
    /* Check for an existing entry first */
    for (int i = 0; i < c->str_count; i++) {
        if (strcmp(c->strings[i], s) == 0) {
            return i;
        }
    }

    if (c->str_count >= c->str_cap) {
        int new_cap = (c->str_cap < 8) ? 8 : c->str_cap * 2;
        c->strings = (char**)realloc(c->strings, (size_t)new_cap * sizeof(char*));
        if (!c->strings) {
            fprintf(stderr, "fatal: out of memory in chunk_intern_str\n");
            exit(1);
        }
        c->str_cap = new_cap;
    }

    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        fprintf(stderr, "fatal: out of memory in chunk_intern_str\n");
        exit(1);
    }
    memcpy(copy, s, len + 1);
    c->strings[c->str_count] = copy;
    return c->str_count++;
}

/* =========================================================================
 * chunk_patch_jump — back-patch a jump instruction's operand
 *
 * The jump instruction occupies bytes [offset, offset+2].
 * byte offset+0 = opcode
 * byte offset+1 = low byte of operand
 * byte offset+2 = high byte of operand
 * ====================================================================== */

void chunk_patch_jump(Chunk* c, int offset, int target) {
    uint16_t t = (uint16_t)target;
    c->code[offset + 1] = (uint8_t)(t & 0xFF);
    c->code[offset + 2] = (uint8_t)((t >> 8) & 0xFF);
}
