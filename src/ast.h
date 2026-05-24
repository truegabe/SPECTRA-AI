#ifndef SPECTRA_AST_H
#define SPECTRA_AST_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/specton.h"
#include "../runtime/tensor.h"

/* =========================================================
 * TokKind — all token types
 * ========================================================= */

typedef enum {
    TOK_INT_LIT=0, TOK_FLOAT_LIT, TOK_STR_LIT,
    TOK_WAVE_LIT,    /* ~{...}~  */
    TOK_RANGE_LIT,   /* |[lo..hi]| */
    /* Keywords */
    TOK_FUNC, TOK_STRUCT, TOK_IF, TOK_ELIF, TOK_ELSE,
    TOK_FOR, TOK_WHILE, TOK_RETURN, TOK_IMPORT, TOK_FROM, TOK_AS,
    TOK_MATCH, TOK_WAVE_IF, TOK_AT_KW, TOK_IN,
    TOK_AND, TOK_OR, TOK_NOT, TOK_TRUE, TOK_FALSE,
    TOK_ALLOC, TOK_FREE, TOK_NULL_KW, TOK_VOID, TOK_SELF,
    /* Type keywords */
    TOK_TY_SPECT, TOK_TY_INT, TOK_TY_FLOAT, TOK_TY_STR_T,
    TOK_TY_BOOL, TOK_TY_BYTE, TOK_TY_DECINT, TOK_TY_WAVFLOAT,
    /* Operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_POWER,      /* ** */
    TOK_FLOOR_DIV,  /* // */
    TOK_FSTR_LIT,   /* f"..." f-string literal */
    TOK_TRY,        /* try */
    TOK_EXCEPT,     /* except */
    TOK_BREAK,       /* break */
    TOK_CONTINUE,    /* continue */
    TOK_MATMUL_OP,  /* @ as matrix multiply operator */
    TOK_COLLAPSE,   /* ~> */
    TOK_SPREAD_OP,  /* <~ */
    TOK_INTERFERE,  /* >< */
    TOK_RESONATE,   /* <> */
    TOK_OBSERVE,    /* ?  */
    TOK_FUZZY_EQ,   /* ~= */
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_ASSIGN,          /* =  */
    TOK_PLUS_ASSIGN,     /* += */
    TOK_MINUS_ASSIGN,    /* -= */
    TOK_STAR_ASSIGN,     /* *= */
    TOK_ARROW,           /* -> */
    TOK_DOTDOT,          /* .. */
    /* Delimiters */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_LBRACE, TOK_RBRACE,
    TOK_COMMA, TOK_DOT, TOK_COLON, TOK_SEMICOLON,
    /* Special */
    TOK_DECORATOR,       /* @name — data.dec_name holds the name */
    TOK_C_INLINE_START,  /* @c_inline: + C block — data.c_code holds it */
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,
    TOK_EOF,
    TOK_IDENT,
    TOK_COUNT
} TokKind;

/* =========================================================
 * Token struct
 * ========================================================= */

typedef struct {
    TokKind kind;
    char    text[256];
    int     line, col;
    union {
        int64_t  int_val;
        double   float_val;
        float    wave_weights[10];
        struct { int lo; int hi; } range;
        char     dec_name[64];
        char*    c_code;           /* heap-allocated for TOK_C_INLINE_START */
    } data;
} Token;

/* =========================================================
 * NodeList — dynamic array of ASTNode*
 * ========================================================= */

typedef struct {
    struct ASTNode** items;
    int count;
    int cap;
} NodeList;

void nodelist_init(NodeList* nl);
void nodelist_push(NodeList* nl, struct ASTNode* node);
void nodelist_free(NodeList* nl);   /* frees the items array but NOT the nodes */

/* =========================================================
 * TypeNode
 * ========================================================= */

typedef struct {
    char name[64];
    int  dims[8];
    int  ndims;
    int  is_ptr;
} TypeNode;

/* =========================================================
 * NodeKind enum
 * ========================================================= */

typedef enum {
    ND_PROGRAM=0, ND_IMPORT,
    ND_FUNC_DEF, ND_STRUCT_DEF, ND_PARAM, ND_FIELD_DEF,
    ND_ASSIGN, ND_AUG_ASSIGN, ND_RETURN,
    ND_IF, ND_WAVE_IF, ND_FOR, ND_WAVE_FOR, ND_WHILE, ND_TRY,
    ND_BREAK, ND_CONTINUE,
    ND_MATCH, ND_MATCH_CASE,
    ND_EXPR_STMT, ND_FREE, ND_C_INLINE,
    ND_BINARY, ND_UNARY, ND_CALL, ND_METHOD_CALL,
    ND_INDEX, ND_ATTRIBUTE, ND_IDENT,
    ND_INT_LIT, ND_FLOAT_LIT, ND_STR_LIT, ND_BOOL_LIT, ND_NULL_LIT,
    ND_WAVE_LIT, ND_RANGE_LIT, ND_ARRAY_LIT, ND_DICT_LIT, ND_FSTR_LIT,
    ND_ALLOC, ND_RANGE_EXPR,
    ND_COUNT
} NodeKind;

/* =========================================================
 * ASTNode struct — complete tagged union
 * ========================================================= */

typedef struct ASTNode {
    NodeKind kind;
    int line, col;
    union {

        struct { NodeList stmts; } program;

        struct {
            char module[128]; char alias[64];
            char names[8][64]; int name_count; int is_from;
        } import;

        struct {
            char name[64];
            NodeList params;
            TypeNode ret_type;
            NodeList body;
            char decorators[8][32]; int dec_count;
            int is_method;
        } func_def;

        struct {
            char name[64];
            NodeList fields;
            NodeList methods;
        } struct_def;

        struct {
            char name[64];
            TypeNode type_annot; int has_type;
            struct ASTNode* default_val;
        } param;

        struct {
            char name[64];
            TypeNode type_annot;
            struct ASTNode* default_val;
        } field_def;

        struct {
            struct ASTNode* target;
            struct ASTNode* value;
            TypeNode type_annot; int has_type;
        } assign;

        struct {
            struct ASTNode* target;
            struct ASTNode* value;
            char op[4];
        } aug_assign;

        struct { struct ASTNode* value; } ret;

        struct {
            struct ASTNode* cond;
            NodeList then_body;
            struct ASTNode* elif_conds[8];
            NodeList elif_bodies[8];
            int elif_count;
            NodeList else_body; int has_else;
        } if_stmt;

        struct {
            struct ASTNode* subject;
            int at_vals[16];
            NodeList at_bodies[16]; int at_count;
            NodeList else_body; int has_else;
        } wave_if;

        struct {
            char target[64];
            struct ASTNode* iterable;
            NodeList body;
        } for_stmt;

        struct {
            char state_var[64]; char weight_var[64];
            struct ASTNode* subject;
            NodeList body;
        } wave_for;

        struct { struct ASTNode* cond; NodeList body; } while_stmt;

        struct {
            NodeList try_body;
            char     exc_var[64];  /* "" if no binding */
            NodeList handler;
            int      has_except;
        } try_stmt;

        struct { struct ASTNode* subject; NodeList cases; } match;

        struct {
            char pattern[16];
            char bindings[4][64]; int binding_count;
            NodeList body;
        } match_case;

        struct { struct ASTNode* expr; } expr_stmt;
        struct { struct ASTNode* target; } free_stmt;
        struct { char* code; } c_inline;

        struct { struct ASTNode* left; struct ASTNode* right; char op[4]; } binary;
        struct { struct ASTNode* operand; char op[4]; } unary;

        struct { struct ASTNode* callee; NodeList args; } call;

        struct {
            struct ASTNode* obj;
            char method[64];
            NodeList args;
        } method_call;

        struct { struct ASTNode* obj; struct ASTNode* index; } index_expr;
        struct { struct ASTNode* obj; char attr[64]; } attribute;
        struct { char name[64]; } ident;
        struct { int64_t value; } int_lit;
        struct { double value; } float_lit;
        struct { char* value; } str_lit;   /* heap-allocated */
        struct { int value; } bool_lit;
        struct { float weights[10]; } wave_lit;
        struct { int lo; int hi; } range_lit;
        struct { NodeList elements; } array_lit;
        struct { NodeList keys; NodeList values; } dict_lit;
        struct {
            char**           parts;       /* string segments between {} */
            int              part_count;
            struct ASTNode** exprs;       /* expressions inside {} */
            int              expr_count;
        } fstr_lit;
        struct { TypeNode type_annot; } alloc;
        struct {
            struct ASTNode* start;  /* NULL if range(n) */
            struct ASTNode* stop;
            struct ASTNode* step;   /* NULL if no step */
        } range_expr;

    } as;
} ASTNode;

/* =========================================================
 * Function declarations
 * ========================================================= */

ASTNode*    node_new(NodeKind kind, int line, int col);
void        node_free(ASTNode* n);   /* recursively frees children */
const char* node_kind_name(NodeKind k);
const char* tok_kind_name(TokKind k);

#endif /* SPECTRA_AST_H */
