/* compiler_bc.c — AST-to-bytecode compiler for the SPECTRA language */

#include "compiler_bc.h"
#include "../runtime/specton.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* =========================================================================
 * Compiler state
 * ====================================================================== */

typedef struct {
    Chunk*  chunk;
    char    error_msg[256];
    int     had_error;
    int     loop_start;      /* bytecode offset of current loop's condition */
    int     break_patch;     /* bytecode offset of OP_JUMP needing patch (break) */
    int     has_break;       /* 1 if a break was emitted in current loop */
} Compiler;

/* =========================================================================
 * Error helper
 * ====================================================================== */

static void compile_error(Compiler* c, int line, const char* fmt, ...) {
    if (c->had_error) return;
    c->had_error = 1;
    va_list ap;
    va_start(ap, fmt);
    char buf[200];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    snprintf(c->error_msg, 256, "[line %d] compile error: %s", line, buf);
}

/* =========================================================================
 * Forward declarations
 * ====================================================================== */

static void compile_node(Compiler* c, ASTNode* node);
static void compile_expr(Compiler* c, ASTNode* node);
static void compile_stmt(Compiler* c, ASTNode* node);
static void compile_block(Compiler* c, NodeList* stmts);

/* =========================================================================
 * Emit helpers
 * ====================================================================== */

static void emit_byte(Compiler* c, uint8_t byte, int line) {
    chunk_write(c->chunk, byte, line);
}

static void emit_op(Compiler* c, uint8_t op, uint16_t operand, int line) {
    chunk_write_op(c->chunk, op, operand, line);
}

/* Emit a jump instruction; returns the byte offset of the opcode so
   the caller can patch it later with chunk_patch_jump(). */
static int emit_jump(Compiler* c, uint8_t jump_op, int line) {
    int offset = c->chunk->count;
    emit_op(c, jump_op, 0xFFFF, line); /* placeholder operand */
    return offset;
}

/* Current bytecode position (next byte to be written) */
static int current_offset(Compiler* c) {
    return c->chunk->count;
}

/* =========================================================================
 * compile_expr — expression nodes → push result on stack
 * ====================================================================== */

static void compile_expr(Compiler* c, ASTNode* node) {
    if (!node || c->had_error) return;

    switch (node->kind) {

    /* ------------------------------------------------------------------ */
    case ND_INT_LIT: {
        Value* v = val_int(node->as.int_lit.value);
        int idx = chunk_add_const(c->chunk, v);
        emit_op(c, OP_PUSH_CONST, (uint16_t)idx, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FLOAT_LIT: {
        Value* v = val_float(node->as.float_lit.value);
        int idx = chunk_add_const(c->chunk, v);
        emit_op(c, OP_PUSH_CONST, (uint16_t)idx, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_STR_LIT: {
        Value* v = val_str(node->as.str_lit.value);
        int idx = chunk_add_const(c->chunk, v);
        emit_op(c, OP_PUSH_CONST, (uint16_t)idx, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_BOOL_LIT:
        emit_byte(c, node->as.bool_lit.value ? OP_PUSH_TRUE : OP_PUSH_FALSE, node->line);
        break;

    /* ------------------------------------------------------------------ */
    case ND_NULL_LIT:
        emit_byte(c, OP_PUSH_NULL, node->line);
        break;

    /* ------------------------------------------------------------------ */
    case ND_WAVE_LIT: {
        Specton s = spect_wave(node->as.wave_lit.weights);
        Value* v = val_spect(s);
        int idx = chunk_add_const(c->chunk, v);
        emit_op(c, OP_PUSH_CONST, (uint16_t)idx, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_RANGE_LIT: {
        Specton s = spect_range((uint8_t)node->as.range_lit.lo,
                                (uint8_t)node->as.range_lit.hi);
        Value* v = val_spect(s);
        int idx = chunk_add_const(c->chunk, v);
        emit_op(c, OP_PUSH_CONST, (uint16_t)idx, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_IDENT: {
        int si = chunk_intern_str(c->chunk, node->as.ident.name);
        emit_op(c, OP_LOAD, (uint16_t)si, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_BINARY: {
        const char* op = node->as.binary.op;

        /* Short-circuit AND */
        if (strcmp(op, "&&") == 0 || strcmp(op, "and") == 0) {
            compile_expr(c, node->as.binary.left);
            if (c->had_error) return;
            int jf = emit_jump(c, OP_JUMP_FALSE, node->line);
            emit_byte(c, OP_POP, node->line);
            compile_expr(c, node->as.binary.right);
            if (c->had_error) return;
            chunk_patch_jump(c->chunk, jf, current_offset(c));
            break;
        }

        /* Short-circuit OR */
        if (strcmp(op, "||") == 0 || strcmp(op, "or") == 0) {
            compile_expr(c, node->as.binary.left);
            if (c->had_error) return;
            int jt = emit_jump(c, OP_JUMP_TRUE, node->line);
            emit_byte(c, OP_POP, node->line);
            compile_expr(c, node->as.binary.right);
            if (c->had_error) return;
            chunk_patch_jump(c->chunk, jt, current_offset(c));
            break;
        }

        /* Normal binary: compile both operands first */
        compile_expr(c, node->as.binary.left);
        if (c->had_error) return;
        compile_expr(c, node->as.binary.right);
        if (c->had_error) return;

        if      (strcmp(op, "+")  == 0) emit_byte(c, OP_ADD, node->line);
        else if (strcmp(op, "-")  == 0) emit_byte(c, OP_SUB, node->line);
        else if (strcmp(op, "*")  == 0) emit_byte(c, OP_MUL, node->line);
        else if (strcmp(op, "/")  == 0) emit_byte(c, OP_DIV, node->line);
        else if (strcmp(op, "%")  == 0) emit_byte(c, OP_MOD, node->line);
        else if (strcmp(op, "**") == 0) emit_byte(c, OP_POW, node->line);
        else if (strcmp(op, "==") == 0) emit_byte(c, OP_EQ,  node->line);
        else if (strcmp(op, "!=") == 0) emit_byte(c, OP_NEQ, node->line);
        else if (strcmp(op, "<")  == 0) emit_byte(c, OP_LT,  node->line);
        else if (strcmp(op, ">")  == 0) emit_byte(c, OP_GT,  node->line);
        else if (strcmp(op, "<=") == 0) emit_byte(c, OP_LTE, node->line);
        else if (strcmp(op, ">=") == 0) emit_byte(c, OP_GTE, node->line);
        else if (strcmp(op, "<>") == 0) emit_byte(c, OP_SPECT_RESONATE,  node->line);
        else if (strcmp(op, "<~") == 0) emit_byte(c, OP_SPECT_SPREAD,    node->line);
        else if (strcmp(op, "><") == 0) emit_byte(c, OP_SPECT_INTERFERE, node->line);
        else {
            compile_error(c, node->line, "unknown binary operator '%s'", op);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_UNARY: {
        const char* op = node->as.unary.op;
        compile_expr(c, node->as.unary.operand);
        if (c->had_error) return;

        if      (strcmp(op, "-")   == 0) emit_byte(c, OP_NEG,            node->line);
        else if (strcmp(op, "!")   == 0 ||
                 strcmp(op, "not") == 0) emit_byte(c, OP_NOT,            node->line);
        else if (strcmp(op, "~>")  == 0) emit_byte(c, OP_SPECT_COLLAPSE, node->line);
        else if (strcmp(op, "wave")== 0) emit_byte(c, OP_SPECT_TO_WAVE,  node->line);
        else {
            compile_error(c, node->line, "unknown unary operator '%s'", op);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_CALL: {
        /* Push callee, then arguments */
        compile_expr(c, node->as.call.callee);
        if (c->had_error) return;
        int argc = node->as.call.args.count;
        for (int i = 0; i < argc; i++) {
            compile_expr(c, node->as.call.args.items[i]);
            if (c->had_error) return;
        }
        emit_op(c, OP_CALL, (uint16_t)argc, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_METHOD_CALL: {
        /* Push receiver, then arguments */
        compile_expr(c, node->as.method_call.obj);
        if (c->had_error) return;
        int argc = node->as.method_call.args.count;
        for (int i = 0; i < argc; i++) {
            compile_expr(c, node->as.method_call.args.items[i]);
            if (c->had_error) return;
        }
        int name_idx = chunk_intern_str(c->chunk, node->as.method_call.method);
        /* Encoding: emit opcode byte, then name_idx (2 bytes), then argc (2 bytes) */
        emit_byte(c, OP_CALL_METHOD, node->line);
        chunk_write(c->chunk, (uint8_t)(name_idx & 0xFF), node->line);
        chunk_write(c->chunk, (uint8_t)((name_idx >> 8) & 0xFF), node->line);
        chunk_write(c->chunk, (uint8_t)(argc & 0xFF), node->line);
        chunk_write(c->chunk, (uint8_t)((argc >> 8) & 0xFF), node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_INDEX: {
        compile_expr(c, node->as.index_expr.obj);
        if (c->had_error) return;
        compile_expr(c, node->as.index_expr.index);
        if (c->had_error) return;
        emit_byte(c, OP_INDEX_GET, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_ATTRIBUTE: {
        compile_expr(c, node->as.attribute.obj);
        if (c->had_error) return;
        int si = chunk_intern_str(c->chunk, node->as.attribute.attr);
        emit_op(c, OP_GET_ATTR, (uint16_t)si, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_ARRAY_LIT: {
        int n = node->as.array_lit.elements.count;
        for (int i = 0; i < n; i++) {
            compile_expr(c, node->as.array_lit.elements.items[i]);
            if (c->had_error) return;
        }
        emit_op(c, OP_MAKE_ARRAY, (uint16_t)n, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_RANGE_EXPR: {
        /* Compile range_expr as a call to range() at runtime.
           Push a reference to the "range" built-in, then push the args. */
        int si = chunk_intern_str(c->chunk, "range");
        emit_op(c, OP_LOAD, (uint16_t)si, node->line);

        int argc = 0;
        if (node->as.range_expr.start) {
            compile_expr(c, node->as.range_expr.start);
            if (c->had_error) return;
            argc++;
        }
        if (node->as.range_expr.stop) {
            compile_expr(c, node->as.range_expr.stop);
            if (c->had_error) return;
            argc++;
        }
        if (node->as.range_expr.step) {
            compile_expr(c, node->as.range_expr.step);
            if (c->had_error) return;
            argc++;
        }
        emit_op(c, OP_CALL, (uint16_t)argc, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_ALLOC: {
        /* alloc(type) — call the runtime alloc built-in */
        int si = chunk_intern_str(c->chunk, "alloc");
        emit_op(c, OP_LOAD, (uint16_t)si, node->line);
        /* Push type name as string argument */
        Value* v = val_str(node->as.alloc.type_annot.name);
        int ci = chunk_add_const(c->chunk, v);
        emit_op(c, OP_PUSH_CONST, (uint16_t)ci, node->line);
        emit_op(c, OP_CALL, 1, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_WAVE_IF:
    case ND_WAVE_FOR:
    case ND_MATCH: {
        compile_error(c, node->line,
            "wave_if / wave_for / match are not supported in bytecode mode; "
            "use the tree-walk interpreter instead");
        break;
    }

    /* ------------------------------------------------------------------ */
    default:
        compile_error(c, node->line,
            "unexpected expression node kind %d in compiler", (int)node->kind);
        break;
    }
}

/* =========================================================================
 * compile_assign_target — emit store instruction for an lvalue
 * ====================================================================== */

static void compile_assign_target(Compiler* c, ASTNode* target, int define_new) {
    if (!target || c->had_error) return;

    switch (target->kind) {
    case ND_IDENT: {
        int si = chunk_intern_str(c->chunk, target->as.ident.name);
        emit_op(c, define_new ? OP_DEFINE : OP_STORE, (uint16_t)si, target->line);
        break;
    }
    case ND_INDEX: {
        /* value is already on stack; push obj and index, then OP_INDEX_SET */
        /* Stack order for OP_INDEX_SET: value, obj, index */
        /* But we need to rearrange: currently value is TOS.
           Emit obj and index BEFORE calling compile_assign_target,
           but that requires the caller to do it.  Here we assume the
           caller already has value on stack and we emit obj+index. */
        compile_expr(c, target->as.index_expr.obj);
        if (c->had_error) return;
        compile_expr(c, target->as.index_expr.index);
        if (c->had_error) return;
        emit_byte(c, OP_INDEX_SET, target->line);
        break;
    }
    case ND_ATTRIBUTE: {
        /* Stack: value ... push obj then SET_ATTR */
        compile_expr(c, target->as.attribute.obj);
        if (c->had_error) return;
        int si = chunk_intern_str(c->chunk, target->as.attribute.attr);
        emit_op(c, OP_SET_ATTR, (uint16_t)si, target->line);
        break;
    }
    default:
        compile_error(c, target->line, "invalid assignment target");
        break;
    }
}

/* =========================================================================
 * compile_block — compile a NodeList as a scoped block
 * ====================================================================== */

static void compile_block(Compiler* c, NodeList* stmts) {
    if (!stmts || c->had_error) return;
    emit_byte(c, OP_SCOPE_PUSH, 0);
    for (int i = 0; i < stmts->count; i++) {
        compile_stmt(c, stmts->items[i]);
        if (c->had_error) return;
    }
    emit_byte(c, OP_SCOPE_POP, 0);
}

/* =========================================================================
 * compile_stmt — statement nodes
 * ====================================================================== */

static void compile_stmt(Compiler* c, ASTNode* node) {
    if (!node || c->had_error) return;

    switch (node->kind) {

    /* ------------------------------------------------------------------ */
    case ND_EXPR_STMT:
        compile_expr(c, node->as.expr_stmt.expr);
        if (c->had_error) return;
        /* Discard the result unless the expression is a call (which may
           return meaningful values that callers choose to ignore). */
        emit_byte(c, OP_POP, node->line);
        break;

    /* ------------------------------------------------------------------ */
    case ND_ASSIGN: {
        compile_expr(c, node->as.assign.value);
        if (c->had_error) return;
        ASTNode* tgt = node->as.assign.target;
        if (tgt->kind == ND_IDENT) {
            int si = chunk_intern_str(c->chunk, tgt->as.ident.name);
            /* OP_STORE walks the scope chain to update an existing binding,
             * falling back to OP_DEFINE in the current scope if not found.
             * This ensures loop-body assignments update the outer variable
             * rather than shadowing it in the block scope. OP_STORE peeks
             * (leaves value on stack), so we emit OP_POP to discard it. */
            emit_op(c, OP_STORE, (uint16_t)si, node->line);
            emit_byte(c, OP_POP, node->line);
        } else {
            /* Complex target: value is on stack */
            compile_assign_target(c, tgt, 0);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_AUG_ASSIGN: {
        /* Load existing value */
        ASTNode* tgt = node->as.aug_assign.target;
        if (tgt->kind == ND_IDENT) {
            int si = chunk_intern_str(c->chunk, tgt->as.ident.name);
            emit_op(c, OP_LOAD, (uint16_t)si, node->line);
        } else {
            /* For index/attr targets: load old value via index_get / get_attr */
            if (tgt->kind == ND_INDEX) {
                compile_expr(c, tgt->as.index_expr.obj);
                if (c->had_error) return;
                compile_expr(c, tgt->as.index_expr.index);
                if (c->had_error) return;
                emit_byte(c, OP_INDEX_GET, node->line);
            } else if (tgt->kind == ND_ATTRIBUTE) {
                compile_expr(c, tgt->as.attribute.obj);
                if (c->had_error) return;
                int si = chunk_intern_str(c->chunk, tgt->as.attribute.attr);
                emit_op(c, OP_GET_ATTR, (uint16_t)si, node->line);
            } else {
                compile_error(c, node->line, "invalid augmented-assignment target");
                return;
            }
        }

        /* Compile the RHS value */
        compile_expr(c, node->as.aug_assign.value);
        if (c->had_error) return;

        /* Emit the operator */
        const char* op = node->as.aug_assign.op;
        if      (strcmp(op, "+=") == 0) emit_byte(c, OP_ADD, node->line);
        else if (strcmp(op, "-=") == 0) emit_byte(c, OP_SUB, node->line);
        else if (strcmp(op, "*=") == 0) emit_byte(c, OP_MUL, node->line);
        else {
            compile_error(c, node->line, "unknown aug-assign operator '%s'", op);
            return;
        }

        /* Store result back */
        if (tgt->kind == ND_IDENT) {
            int si = chunk_intern_str(c->chunk, tgt->as.ident.name);
            emit_op(c, OP_STORE, (uint16_t)si, node->line);
        } else {
            compile_assign_target(c, tgt, 0);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_RETURN: {
        if (node->as.ret.value) {
            compile_expr(c, node->as.ret.value);
            if (c->had_error) return;
        } else {
            emit_byte(c, OP_PUSH_NULL, node->line);
        }
        emit_byte(c, OP_RETURN, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_IF: {
        /* Compile condition */
        compile_expr(c, node->as.if_stmt.cond);
        if (c->had_error) return;

        int jf_then = emit_jump(c, OP_JUMP_FALSE, node->line);

        /* Then body */
        compile_block(c, &node->as.if_stmt.then_body);
        if (c->had_error) return;

        /* Jump over elif / else at the end of each taken branch */
        int end_jumps[10];
        int end_jump_count = 0;

        int next_patch = jf_then;

        /* Elif chains */
        for (int i = 0; i < node->as.if_stmt.elif_count; i++) {
            /* Jump to end after this branch executes */
            end_jumps[end_jump_count++] = emit_jump(c, OP_JUMP, node->line);
            /* Patch the previous false-jump to land here */
            chunk_patch_jump(c->chunk, next_patch, current_offset(c));

            compile_expr(c, node->as.if_stmt.elif_conds[i]);
            if (c->had_error) return;
            next_patch = emit_jump(c, OP_JUMP_FALSE, node->line);

            compile_block(c, &node->as.if_stmt.elif_bodies[i]);
            if (c->had_error) return;
        }

        /* Jump from last taken branch to end */
        end_jumps[end_jump_count++] = emit_jump(c, OP_JUMP, node->line);
        /* Patch the last false-jump to land here (else or end) */
        chunk_patch_jump(c->chunk, next_patch, current_offset(c));

        /* Else body */
        if (node->as.if_stmt.has_else) {
            compile_block(c, &node->as.if_stmt.else_body);
            if (c->had_error) return;
        }

        /* Patch all end-jumps */
        int end_pos = current_offset(c);
        for (int i = 0; i < end_jump_count; i++) {
            chunk_patch_jump(c->chunk, end_jumps[i], end_pos);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_WHILE: {
        int loop_start = current_offset(c);

        compile_expr(c, node->as.while_stmt.cond);
        if (c->had_error) return;

        int exit_jump = emit_jump(c, OP_JUMP_FALSE, node->line);

        compile_block(c, &node->as.while_stmt.body);
        if (c->had_error) return;

        /* Loop back */
        emit_op(c, OP_JUMP, (uint16_t)loop_start, node->line);

        /* Patch exit */
        chunk_patch_jump(c->chunk, exit_jump, current_offset(c));
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FOR: {
        const char* tgt_name = node->as.for_stmt.target;
        ASTNode* iter_node = node->as.for_stmt.iterable;

        /* Detect  for x in range(...)  and compile as a pure integer loop
         * to avoid the __range__ string hack in builtin_range. */
        int is_range = (iter_node->kind == ND_CALL &&
                        iter_node->as.call.callee->kind == ND_IDENT &&
                        strcmp(iter_node->as.call.callee->as.ident.name, "range") == 0);

        if (is_range) {
            /* Unique hidden var names per line to allow nesting */
            char var_i[80], var_stop[80], var_step[80];
            snprintf(var_i,    sizeof(var_i),    "__ri_%d",   node->line);
            snprintf(var_stop, sizeof(var_stop), "__rs_%d",   node->line);
            snprintf(var_step, sizeof(var_step), "__rstep_%d", node->line);

            NodeList* rargs = &iter_node->as.call.args;
            int argc_r = rargs->count;

            /* Emit: __ri = start (0 if 1 arg, args[0] if 2+ args) */
            if (argc_r >= 2) {
                compile_expr(c, rargs->items[0]);
            } else {
                Value* z = val_int(0);
                emit_op(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, z), node->line);
            }
            if (c->had_error) return;
            int si_i = chunk_intern_str(c->chunk, var_i);
            emit_op(c, OP_DEFINE, (uint16_t)si_i, node->line);

            /* Emit: __rs = stop (args[0] if 1 arg, args[1] if 2+ args) */
            if (argc_r >= 2) {
                compile_expr(c, rargs->items[1]);
            } else if (argc_r == 1) {
                compile_expr(c, rargs->items[0]);
            } else {
                Value* z = val_int(0);
                emit_op(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, z), node->line);
            }
            if (c->had_error) return;
            int si_stop = chunk_intern_str(c->chunk, var_stop);
            emit_op(c, OP_DEFINE, (uint16_t)si_stop, node->line);

            /* Emit: __rstep = step (args[2] if 3 args, else 1) */
            if (argc_r >= 3) {
                compile_expr(c, rargs->items[2]);
            } else {
                Value* one = val_int(1);
                emit_op(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, one), node->line);
            }
            if (c->had_error) return;
            int si_step = chunk_intern_str(c->chunk, var_step);
            emit_op(c, OP_DEFINE, (uint16_t)si_step, node->line);

            /* loop_start: if __ri >= __rs: exit */
            int loop_start = current_offset(c);
            /* __ri < __rs  → stack [__ri, __rs] → OP_LT → __ri < __rs */
            emit_op(c, OP_LOAD, (uint16_t)si_i,    node->line);
            emit_op(c, OP_LOAD, (uint16_t)si_stop,  node->line);
            emit_byte(c, OP_LT, node->line);
            int exit_jump = emit_jump(c, OP_JUMP_FALSE, node->line);

            /* body: target = __ri */
            emit_byte(c, OP_SCOPE_PUSH, node->line);
            emit_op(c, OP_LOAD, (uint16_t)si_i, node->line);
            int si_tgt = chunk_intern_str(c->chunk, tgt_name);
            emit_op(c, OP_DEFINE, (uint16_t)si_tgt, node->line);

            for (int i = 0; i < node->as.for_stmt.body.count; i++) {
                compile_stmt(c, node->as.for_stmt.body.items[i]);
                if (c->had_error) return;
            }
            emit_byte(c, OP_SCOPE_POP, node->line);

            /* __ri += __rstep */
            emit_op(c, OP_LOAD, (uint16_t)si_i,    node->line);
            emit_op(c, OP_LOAD, (uint16_t)si_step,  node->line);
            emit_byte(c, OP_ADD, node->line);
            emit_op(c, OP_STORE, (uint16_t)si_i, node->line);
            emit_byte(c, OP_POP, node->line);

            emit_op(c, OP_JUMP, (uint16_t)loop_start, node->line);
            chunk_patch_jump(c->chunk, exit_jump, current_offset(c));
            break;
        }

        /* General iterable: for target in expr — use index-based loop */
        char iter_name[80], idx_name[80];
        snprintf(iter_name, sizeof(iter_name), "__iter_%d", node->line);
        snprintf(idx_name,  sizeof(idx_name),  "__idx_%d",  node->line);

        /* __iter = iterable */
        compile_expr(c, node->as.for_stmt.iterable);
        if (c->had_error) return;
        int si_iter = chunk_intern_str(c->chunk, iter_name);
        emit_op(c, OP_DEFINE, (uint16_t)si_iter, node->line);

        /* __idx = 0 */
        {
            Value* zero = val_int(0);
            emit_op(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, zero), node->line);
        }
        int si_idx = chunk_intern_str(c->chunk, idx_name);
        emit_op(c, OP_DEFINE, (uint16_t)si_idx, node->line);

        /* loop condition: __idx < len(__iter) */
        int loop_start = current_offset(c);
        emit_op(c, OP_LOAD, (uint16_t)si_idx, node->line);
        int si_len = chunk_intern_str(c->chunk, "len");
        emit_op(c, OP_LOAD, (uint16_t)si_len, node->line);
        emit_op(c, OP_LOAD, (uint16_t)si_iter, node->line);
        emit_op(c, OP_CALL, 1, node->line);
        emit_byte(c, OP_LT, node->line);
        int exit_jump = emit_jump(c, OP_JUMP_FALSE, node->line);

        /* target = __iter[__idx] */
        emit_byte(c, OP_SCOPE_PUSH, node->line);
        emit_op(c, OP_LOAD, (uint16_t)si_iter, node->line);
        emit_op(c, OP_LOAD, (uint16_t)si_idx,  node->line);
        emit_byte(c, OP_INDEX_GET, node->line);
        {
            int si_tgt = chunk_intern_str(c->chunk, tgt_name);
            emit_op(c, OP_DEFINE, (uint16_t)si_tgt, node->line);
        }

        for (int i = 0; i < node->as.for_stmt.body.count; i++) {
            compile_stmt(c, node->as.for_stmt.body.items[i]);
            if (c->had_error) return;
        }
        emit_byte(c, OP_SCOPE_POP, node->line);

        /* __idx += 1 */
        emit_op(c, OP_LOAD, (uint16_t)si_idx, node->line);
        {
            Value* one = val_int(1);
            emit_op(c, OP_PUSH_CONST, (uint16_t)chunk_add_const(c->chunk, one), node->line);
        }
        emit_byte(c, OP_ADD, node->line);
        emit_op(c, OP_STORE, (uint16_t)si_idx, node->line);
        emit_byte(c, OP_POP, node->line);

        emit_op(c, OP_JUMP, (uint16_t)loop_start, node->line);
        chunk_patch_jump(c->chunk, exit_jump, current_offset(c));
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FUNC_DEF: {
        /* Build a VAL_FUNC constant that carries the param names and
           ASTNode* body so the VM can call it.  We store the entire
           func_def ASTNode pointer; the VM compiles the body on demand. */
        ASTNode* fdef = node;
        int nparams = fdef->as.func_def.params.count;

        char** param_names = NULL;
        if (nparams > 0) {
            param_names = (char**)malloc((size_t)nparams * sizeof(char*));
            if (!param_names) {
                compile_error(c, node->line, "out of memory compiling func_def");
                return;
            }
            for (int i = 0; i < nparams; i++) {
                ASTNode* p = fdef->as.func_def.params.items[i];
                param_names[i] = strdup(p->as.param.name);
            }
        }

        /* val_func takes ownership of param_names array via strdup copies;
           body pointer is borrowed (the AST outlives the chunk). */
        Value* fn = val_func(fdef->as.func_def.name,
                             param_names, nparams,
                             fdef,       /* body = the ND_FUNC_DEF node */
                             NULL,       /* closure set at runtime */
                             fdef->as.func_def.is_method);
        /* Free our local copy of param_names (val_func copies them) */
        if (param_names) {
            for (int i = 0; i < nparams; i++) free(param_names[i]);
            free(param_names);
        }

        int ci = chunk_add_const(c->chunk, fn);
        emit_op(c, OP_MAKE_FUNC, (uint16_t)ci, node->line);

        /* Define the function in the current scope */
        int si = chunk_intern_str(c->chunk, fdef->as.func_def.name);
        emit_op(c, OP_DEFINE, (uint16_t)si, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_STRUCT_DEF: {
        /* For structs, emit a constructor function named after the struct.
           The VM stores it; when called the VM instantiates a struct. */
        ASTNode* sdef = node;
        int nfields = sdef->as.struct_def.fields.count;

        char** field_names = NULL;
        if (nfields > 0) {
            field_names = (char**)malloc((size_t)nfields * sizeof(char*));
            if (!field_names) {
                compile_error(c, node->line, "out of memory compiling struct_def");
                return;
            }
            for (int i = 0; i < nfields; i++) {
                ASTNode* f = sdef->as.struct_def.fields.items[i];
                field_names[i] = strdup(f->as.field_def.name);
            }
        }

        /* Build a VAL_FUNC that acts as the constructor; body = struct_def node */
        Value* fn = val_func(sdef->as.struct_def.name,
                             field_names, nfields,
                             sdef,   /* body = ND_STRUCT_DEF; VM detects this */
                             NULL,
                             0);
        if (field_names) {
            for (int i = 0; i < nfields; i++) free(field_names[i]);
            free(field_names);
        }

        int ci = chunk_add_const(c->chunk, fn);
        emit_op(c, OP_MAKE_FUNC, (uint16_t)ci, node->line);

        int si = chunk_intern_str(c->chunk, sdef->as.struct_def.name);
        emit_op(c, OP_DEFINE, (uint16_t)si, node->line);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FREE: {
        /* free(expr) — evaluate expression, pop result (GC handles dealloc) */
        if (node->as.free_stmt.target) {
            compile_expr(c, node->as.free_stmt.target);
            if (c->had_error) return;
            emit_byte(c, OP_POP, node->line);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_C_INLINE: {
        /* C inline blocks cannot be executed in the bytecode VM */
        compile_error(c, node->line,
            "@c_inline blocks are not supported in bytecode mode");
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_IMPORT: {
        /* Silently skip imports in bytecode mode */
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_WAVE_IF:
    case ND_WAVE_FOR:
    case ND_MATCH: {
        compile_error(c, node->line,
            "wave_if / wave_for / match are not supported in bytecode mode; "
            "use the tree-walk interpreter instead");
        break;
    }

    /* ------------------------------------------------------------------ */
    default:
        /* Might be an expression used as a statement */
        compile_expr(c, node);
        if (!c->had_error) {
            emit_byte(c, OP_POP, node->line);
        }
        break;
    }
}

/* =========================================================================
 * bc_compile — public entry point
 * ====================================================================== */

int bc_compile(ASTNode* program, Chunk* chunk, char error_msg[256]) {
    if (!program || program->kind != ND_PROGRAM) {
        snprintf(error_msg, 256, "bc_compile: expected ND_PROGRAM node");
        return 0;
    }

    Compiler c;
    c.chunk       = chunk;
    c.had_error   = 0;
    c.loop_start  = -1;
    c.break_patch = -1;
    c.has_break   = 0;
    c.error_msg[0] = '\0';

    NodeList* stmts = &program->as.program.stmts;
    for (int i = 0; i < stmts->count; i++) {
        compile_stmt(&c, stmts->items[i]);
        if (c.had_error) {
            strncpy(error_msg, c.error_msg, 255);
            error_msg[255] = '\0';
            return 0;
        }
    }

    emit_byte(&c, OP_HALT, 0);

    if (c.had_error) {
        strncpy(error_msg, c.error_msg, 255);
        error_msg[255] = '\0';
        return 0;
    }
    return 1;
}
