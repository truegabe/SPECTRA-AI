#ifndef SPECTRA_OPCODE_H
#define SPECTRA_OPCODE_H

#include "value.h"
#include <stdint.h>
#include <stdlib.h>

/* =========================================================================
 * OpCode — every instruction the bytecode VM understands.
 *
 * Encoding:
 *   Each instruction is 1 byte (the opcode) + an optional 2-byte
 *   little-endian uint16_t operand.  Instructions that take no operand
 *   still occupy only 1 byte; the operand bytes belong to the next
 *   instruction.
 *
 *   Helpers:
 *     chunk_write(c, byte, line)          — append a raw byte
 *     chunk_write_op(c, op, operand, ln)  — append op + 2 operand bytes
 * ====================================================================== */

typedef enum {
    /* ---- Stack ---- */
    OP_PUSH_CONST   = 0,  /* operand = constant-pool index             */
    OP_PUSH_NULL    = 1,  /* push VAL_NULL                             */
    OP_PUSH_TRUE    = 2,  /* push VAL_BOOL 1                           */
    OP_PUSH_FALSE   = 3,  /* push VAL_BOOL 0                           */
    OP_POP          = 4,  /* discard top of stack                      */
    OP_DUP          = 5,  /* duplicate top of stack                    */

    /* ---- Variables ---- */
    OP_LOAD         = 6,  /* operand = string-pool index (var name)    */
    OP_STORE        = 7,  /* operand = string-pool index               */
    OP_DEFINE       = 8,  /* operand = string-pool index (new binding) */

    /* ---- Arithmetic ---- */
    OP_ADD          = 9,
    OP_SUB          = 10,
    OP_MUL          = 11,
    OP_DIV          = 12,
    OP_MOD          = 13,
    OP_POW          = 14,
    OP_NEG          = 15,  /* unary minus                               */
    OP_NOT          = 16,  /* logical not                               */

    /* ---- Specton ---- */
    OP_SPECT_RESONATE  = 17,
    OP_SPECT_SPREAD    = 18,
    OP_SPECT_INTERFERE = 19,
    OP_SPECT_COLLAPSE  = 20,  /* unary: collapse top-of-stack Specton      */
    OP_SPECT_TO_WAVE   = 21,  /* unary: convert top-of-stack to WAVE       */

    /* ---- Compare ---- */
    OP_EQ           = 22,
    OP_NEQ          = 23,
    OP_LT           = 24,
    OP_GT           = 25,
    OP_LTE          = 26,
    OP_GTE          = 27,

    /* ---- Jump ---- */
    OP_JUMP         = 28,  /* operand = absolute byte offset in chunk   */
    OP_JUMP_FALSE   = 29,  /* pop; jump if falsy                        */
    OP_JUMP_TRUE    = 30,  /* pop; jump if truthy                       */

    /* ---- Functions ---- */
    OP_CALL         = 31,  /* operand = argc; callee below args on stack */
    OP_RETURN       = 32,  /* pop return value; exit current chunk       */
    OP_MAKE_FUNC    = 33,  /* operand = const idx  (VAL_FUNC constant)   */

    /* ---- Methods ---- */
    OP_CALL_METHOD  = 34,  /* two operands: name-str idx (uint16),
                              argc (uint16); obj below args on stack      */

    /* ---- Collections ---- */
    OP_MAKE_ARRAY   = 35,  /* operand = n; pop n values → push array     */
    OP_INDEX_GET    = 36,  /* pop index, pop obj; push obj[index]        */
    OP_INDEX_SET    = 37,  /* pop value, pop index, pop obj; obj[i]=val  */
    OP_GET_ATTR     = 38,  /* operand = name str idx; pop obj; push attr */
    OP_SET_ATTR     = 39,  /* operand = name str idx; pop val, pop obj   */

    /* ---- Scope ---- */
    OP_SCOPE_PUSH   = 40,  /* push a new child environment               */
    OP_SCOPE_POP    = 41,  /* pop back to the parent environment         */

    /* ---- Control ---- */
    OP_HALT         = 42,  /* stop execution                             */

    OP_COUNT        = 43
} OpCode;

/* =========================================================================
 * Chunk — the unit of compiled bytecode.
 * ====================================================================== */

typedef struct {
    uint8_t* code;       /* flat bytecode array                          */
    int      count;      /* bytes used                                   */
    int      cap;        /* bytes allocated                              */

    int*     lines;      /* lines[i] = source line for byte i            */

    Value**  constants;  /* constant pool — each entry is a retained     */
    int      const_count;/*   Value*                                     */
    int      const_cap;

    char**   strings;    /* interned variable-name pool                  */
    int      str_count;
    int      str_cap;
} Chunk;

/* -------------------------------------------------------------------------
 * Chunk API
 * ---------------------------------------------------------------------- */

/* Initialise an empty chunk (all fields zeroed / NULL). */
void chunk_init(Chunk* c);

/* Release all memory owned by the chunk (does NOT free `c` itself). */
void chunk_free(Chunk* c);

/* Append a single raw byte to the bytecode stream. */
void chunk_write(Chunk* c, uint8_t byte, int line);

/* Append an opcode followed by a 2-byte little-endian operand. */
void chunk_write_op(Chunk* c, uint8_t op, uint16_t operand, int line);

/* Add `v` to the constant pool; returns its index.
 * Takes ownership of the reference (does NOT retain). */
int chunk_add_const(Chunk* c, Value* v);

/* Intern a variable/attribute name string; returns its index.
 * The string is copied into the pool. */
int chunk_intern_str(Chunk* c, const char* s);

/* Patch the operand of a previously emitted OP_JUMP* instruction.
 * `offset` is the byte position of the opcode itself;
 * `target` is the absolute byte offset to jump to. */
void chunk_patch_jump(Chunk* c, int offset, int target);

/* -------------------------------------------------------------------------
 * chunk_init / chunk_free implementations (header-inlined for convenience).
 * We use a small .c file (opcode.c) for the real implementations; these
 * prototypes are all that consumers need.
 * ---------------------------------------------------------------------- */

#endif /* SPECTRA_OPCODE_H */
