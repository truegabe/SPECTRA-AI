/* ast.c — ASTNode and NodeList utilities for SPECTRA */
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================
 * NodeList
 * ========================================================= */

void nodelist_init(NodeList* nl) {
    nl->items = NULL;
    nl->count = 0;
    nl->cap   = 0;
}

void nodelist_push(NodeList* nl, ASTNode* node) {
    if (nl->count >= nl->cap) {
        nl->cap   = nl->cap ? nl->cap * 2 : 8;
        nl->items = (ASTNode**)realloc(nl->items, (size_t)nl->cap * sizeof(ASTNode*));
    }
    nl->items[nl->count++] = node;
}

/* Frees the dynamic array itself; does NOT free the nodes inside. */
void nodelist_free(NodeList* nl) {
    free(nl->items);
    nl->items = NULL;
    nl->count = 0;
    nl->cap   = 0;
}

/* =========================================================
 * node_new
 * ========================================================= */

ASTNode* node_new(NodeKind kind, int line, int col) {
    ASTNode* n = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!n) return NULL;
    n->kind = kind;
    n->line = line;
    n->col  = col;
    return n;
}

/* =========================================================
 * node_free — recursively free a node and all children
 * ========================================================= */

void node_free(ASTNode* n) {
    if (!n) return;

    switch (n->kind) {

        case ND_PROGRAM:
            for (int i = 0; i < n->as.program.stmts.count; i++)
                node_free(n->as.program.stmts.items[i]);
            nodelist_free(&n->as.program.stmts);
            break;

        case ND_IMPORT:
            /* no heap children */
            break;

        case ND_FUNC_DEF:
            for (int i = 0; i < n->as.func_def.params.count; i++)
                node_free(n->as.func_def.params.items[i]);
            nodelist_free(&n->as.func_def.params);
            for (int i = 0; i < n->as.func_def.body.count; i++)
                node_free(n->as.func_def.body.items[i]);
            nodelist_free(&n->as.func_def.body);
            break;

        case ND_STRUCT_DEF:
            for (int i = 0; i < n->as.struct_def.fields.count; i++)
                node_free(n->as.struct_def.fields.items[i]);
            nodelist_free(&n->as.struct_def.fields);
            for (int i = 0; i < n->as.struct_def.methods.count; i++)
                node_free(n->as.struct_def.methods.items[i]);
            nodelist_free(&n->as.struct_def.methods);
            break;

        case ND_PARAM:
            node_free(n->as.param.default_val);
            break;

        case ND_FIELD_DEF:
            node_free(n->as.field_def.default_val);
            break;

        case ND_ASSIGN:
            node_free(n->as.assign.target);
            node_free(n->as.assign.value);
            break;

        case ND_AUG_ASSIGN:
            node_free(n->as.aug_assign.target);
            node_free(n->as.aug_assign.value);
            break;

        case ND_RETURN:
            node_free(n->as.ret.value);
            break;

        case ND_IF:
            node_free(n->as.if_stmt.cond);
            for (int i = 0; i < n->as.if_stmt.then_body.count; i++)
                node_free(n->as.if_stmt.then_body.items[i]);
            nodelist_free(&n->as.if_stmt.then_body);
            for (int e = 0; e < n->as.if_stmt.elif_count; e++) {
                node_free(n->as.if_stmt.elif_conds[e]);
                for (int i = 0; i < n->as.if_stmt.elif_bodies[e].count; i++)
                    node_free(n->as.if_stmt.elif_bodies[e].items[i]);
                nodelist_free(&n->as.if_stmt.elif_bodies[e]);
            }
            if (n->as.if_stmt.has_else) {
                for (int i = 0; i < n->as.if_stmt.else_body.count; i++)
                    node_free(n->as.if_stmt.else_body.items[i]);
                nodelist_free(&n->as.if_stmt.else_body);
            }
            break;

        case ND_WAVE_IF:
            node_free(n->as.wave_if.subject);
            for (int c = 0; c < n->as.wave_if.at_count; c++) {
                for (int i = 0; i < n->as.wave_if.at_bodies[c].count; i++)
                    node_free(n->as.wave_if.at_bodies[c].items[i]);
                nodelist_free(&n->as.wave_if.at_bodies[c]);
            }
            if (n->as.wave_if.has_else) {
                for (int i = 0; i < n->as.wave_if.else_body.count; i++)
                    node_free(n->as.wave_if.else_body.items[i]);
                nodelist_free(&n->as.wave_if.else_body);
            }
            break;

        case ND_FOR:
            node_free(n->as.for_stmt.iterable);
            for (int i = 0; i < n->as.for_stmt.body.count; i++)
                node_free(n->as.for_stmt.body.items[i]);
            nodelist_free(&n->as.for_stmt.body);
            break;

        case ND_WAVE_FOR:
            node_free(n->as.wave_for.subject);
            for (int i = 0; i < n->as.wave_for.body.count; i++)
                node_free(n->as.wave_for.body.items[i]);
            nodelist_free(&n->as.wave_for.body);
            break;

        case ND_WHILE:
            node_free(n->as.while_stmt.cond);
            for (int i = 0; i < n->as.while_stmt.body.count; i++)
                node_free(n->as.while_stmt.body.items[i]);
            nodelist_free(&n->as.while_stmt.body);
            break;

        case ND_BREAK:
        case ND_CONTINUE:
            /* no heap children */
            break;

        case ND_TRY:
            for (int i = 0; i < n->as.try_stmt.try_body.count; i++)
                node_free(n->as.try_stmt.try_body.items[i]);
            nodelist_free(&n->as.try_stmt.try_body);
            for (int i = 0; i < n->as.try_stmt.handler.count; i++)
                node_free(n->as.try_stmt.handler.items[i]);
            nodelist_free(&n->as.try_stmt.handler);
            break;

        case ND_MATCH:
            node_free(n->as.match.subject);
            for (int i = 0; i < n->as.match.cases.count; i++)
                node_free(n->as.match.cases.items[i]);
            nodelist_free(&n->as.match.cases);
            break;

        case ND_MATCH_CASE:
            for (int i = 0; i < n->as.match_case.body.count; i++)
                node_free(n->as.match_case.body.items[i]);
            nodelist_free(&n->as.match_case.body);
            break;

        case ND_EXPR_STMT:
            node_free(n->as.expr_stmt.expr);
            break;

        case ND_FREE:
            node_free(n->as.free_stmt.target);
            break;

        case ND_C_INLINE:
            free(n->as.c_inline.code);
            break;

        case ND_BINARY:
            node_free(n->as.binary.left);
            node_free(n->as.binary.right);
            break;

        case ND_UNARY:
            node_free(n->as.unary.operand);
            break;

        case ND_CALL:
            node_free(n->as.call.callee);
            for (int i = 0; i < n->as.call.args.count; i++)
                node_free(n->as.call.args.items[i]);
            nodelist_free(&n->as.call.args);
            break;

        case ND_METHOD_CALL:
            node_free(n->as.method_call.obj);
            for (int i = 0; i < n->as.method_call.args.count; i++)
                node_free(n->as.method_call.args.items[i]);
            nodelist_free(&n->as.method_call.args);
            break;

        case ND_INDEX:
            node_free(n->as.index_expr.obj);
            node_free(n->as.index_expr.index);
            break;

        case ND_ATTRIBUTE:
            node_free(n->as.attribute.obj);
            break;

        case ND_IDENT:
            /* no heap children */
            break;

        case ND_INT_LIT:
        case ND_FLOAT_LIT:
        case ND_BOOL_LIT:
        case ND_NULL_LIT:
        case ND_WAVE_LIT:
        case ND_RANGE_LIT:
            /* no heap children */
            break;

        case ND_STR_LIT:
            free(n->as.str_lit.value);
            break;

        case ND_ARRAY_LIT:
            for (int i = 0; i < n->as.array_lit.elements.count; i++)
                node_free(n->as.array_lit.elements.items[i]);
            nodelist_free(&n->as.array_lit.elements);
            break;

        case ND_ALLOC:
            /* type_annot is embedded, no heap */
            break;

        case ND_RANGE_EXPR:
            node_free(n->as.range_expr.start);
            node_free(n->as.range_expr.stop);
            node_free(n->as.range_expr.step);
            break;

        default:
            break;
    }

    free(n);
}

/* =========================================================
 * node_kind_name
 * ========================================================= */

const char* node_kind_name(NodeKind k) {
    static const char* names[ND_COUNT] = {
        "PROGRAM",     "IMPORT",
        "FUNC_DEF",    "STRUCT_DEF",   "PARAM",       "FIELD_DEF",
        "ASSIGN",      "AUG_ASSIGN",   "RETURN",
        "IF",          "WAVE_IF",      "FOR",          "WAVE_FOR",   "WHILE",    "TRY",
        "BREAK",       "CONTINUE",
        "MATCH",       "MATCH_CASE",
        "EXPR_STMT",   "FREE",         "C_INLINE",
        "BINARY",      "UNARY",        "CALL",         "METHOD_CALL",
        "INDEX",       "ATTRIBUTE",    "IDENT",
        "INT_LIT",     "FLOAT_LIT",    "STR_LIT",      "BOOL_LIT",   "NULL_LIT",
        "WAVE_LIT",    "RANGE_LIT",    "ARRAY_LIT",    "DICT_LIT",   "FSTR_LIT",
        "ALLOC",       "RANGE_EXPR"
    };
    if (k >= 0 && k < ND_COUNT) return names[k];
    return "?";
}

/* =========================================================
 * tok_kind_name
 * ========================================================= */

const char* tok_kind_name(TokKind k) {
    static const char* names[TOK_COUNT] = {
        /* literals */
        "INT_LIT", "FLOAT_LIT", "STR_LIT", "WAVE_LIT", "RANGE_LIT",
        /* keywords */
        "func", "struct", "if", "elif", "else",
        "for", "while", "return", "import", "from", "as",
        "match", "wave_if", "at", "in",
        "and", "or", "not", "true", "false",
        "alloc", "free", "null", "void", "self",
        /* type keywords */
        "spect", "int", "float", "str", "bool", "byte", "decint", "wavfloat",
        /* operators */
        "+", "-", "*", "/", "%", "**", "//", "f\"...\"", "try", "except", "break", "continue", "@", "~>", "<~", "><", "<>", "?", "~=",
        "==", "!=", "<", ">", "<=", ">=",
        "=", "+=", "-=", "*=", "->", "..",
        /* delimiters */
        "(", ")", "[", "]", "{", "}",
        ",", ".", ":", ";",
        /* special */
        "@decorator", "@c_inline",
        "NEWLINE", "INDENT", "DEDENT", "EOF", "IDENT"
    };
    if (k >= 0 && k < TOK_COUNT) return names[k];
    return "?";
}
