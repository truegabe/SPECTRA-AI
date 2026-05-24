#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>

/* =========================================================
 * Parser lifecycle
 * ========================================================= */

Parser* parser_new(Token* tokens, int count) {
    Parser* p = (Parser*)malloc(sizeof(Parser));
    if (!p) return NULL;
    p->tokens    = tokens;
    p->count     = count;
    p->pos       = 0;
    p->had_error = 0;
    p->error_msg[0] = '\0';
    return p;
}

void parser_free(Parser* p) {
    if (p) free(p);
}

/* =========================================================
 * Cursor helpers
 * ========================================================= */

static Token* peek(Parser* p, int offset) {
    int idx = p->pos + offset;
    if (idx < 0) idx = 0;
    if (idx >= p->count) idx = p->count - 1;  /* clamp to last (EOF) */
    return &p->tokens[idx];
}

static Token* advance(Parser* p) {
    Token* t = &p->tokens[p->pos];
    if (p->pos < p->count - 1) p->pos++;
    return t;
}

static int check(Parser* p, TokKind k) {
    return peek(p, 0)->kind == k;
}

static void parse_error(Parser* p, const char* fmt, ...) {
    if (p->had_error) return;   /* only report first error */
    p->had_error = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->error_msg, sizeof(p->error_msg), fmt, ap);
    va_end(ap);
}

static Token* expect(Parser* p, TokKind k) {
    if (!check(p, k)) {
        Token* cur = peek(p, 0);
        parse_error(p, "line %d:%d: expected token %d but got %d ('%s')",
                    cur->line, cur->col, (int)k, (int)cur->kind, cur->text);
        return NULL;
    }
    return advance(p);
}

static int match(Parser* p, TokKind k) {
    if (check(p, k)) { advance(p); return 1; }
    return 0;
}

static void skip_newlines(Parser* p) {
    while (check(p, TOK_NEWLINE)) advance(p);
}

/* =========================================================
 * Forward declarations
 * ========================================================= */

static ASTNode* parse_top_level(Parser* p);
static ASTNode* parse_import(Parser* p);
static ASTNode* parse_func(Parser* p, int dec_count, char decorators[][32], int is_method);
static ASTNode* parse_struct(Parser* p, int dec_count, char decorators[][32]);
static NodeList parse_block(Parser* p);
static ASTNode* parse_stmt(Parser* p);
static ASTNode* parse_if(Parser* p);
static ASTNode* parse_wave_if(Parser* p);
static ASTNode* parse_for(Parser* p);
static ASTNode* parse_while(Parser* p);
static ASTNode* parse_match(Parser* p);
static ASTNode* parse_try(Parser* p);
static ASTNode* parse_return(Parser* p);
static ASTNode* parse_assignment_or_expr(Parser* p);
static TypeNode parse_type(Parser* p);
static NodeList parse_params(Parser* p);
static ASTNode* parse_param(Parser* p);

static ASTNode* parse_expr(Parser* p);
static ASTNode* parse_or_expr(Parser* p);
static ASTNode* parse_and_expr(Parser* p);
static ASTNode* parse_not_expr(Parser* p);
static ASTNode* parse_comparison(Parser* p);
static ASTNode* parse_wave_ops(Parser* p);
static ASTNode* parse_additive(Parser* p);
static ASTNode* parse_multiplicative(Parser* p);
static ASTNode* parse_power(Parser* p);
static ASTNode* parse_unary(Parser* p);
static ASTNode* parse_postfix(Parser* p);
static ASTNode* parse_primary(Parser* p);
static NodeList parse_call_args(Parser* p);
static ASTNode* parse_array_literal(Parser* p);

/* =========================================================
 * Block parsing: NEWLINE INDENT stmts DEDENT
 * ========================================================= */

static NodeList parse_block(Parser* p) {
    NodeList body;
    nodelist_init(&body);

    /* consume trailing newline(s) before INDENT */
    skip_newlines(p);

    if (!expect(p, TOK_INDENT)) return body;

    skip_newlines(p);

    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->had_error) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) break;
        ASTNode* s = parse_stmt(p);
        if (s) nodelist_push(&body, s);
    }

    expect(p, TOK_DEDENT);
    return body;
}

/* =========================================================
 * Type parsing
 * ========================================================= */

static TypeNode parse_type(Parser* p) {
    TypeNode tn;
    memset(&tn, 0, sizeof(tn));

    /* pointer prefix */
    if (match(p, TOK_STAR)) {
        tn.is_ptr = 1;
    }

    Token* t = peek(p, 0);
    switch (t->kind) {
        case TOK_TY_SPECT:    strncpy(tn.name, "spect",    63); advance(p); break;
        case TOK_TY_INT:      strncpy(tn.name, "int",      63); advance(p); break;
        case TOK_TY_FLOAT:    strncpy(tn.name, "float",    63); advance(p); break;
        case TOK_TY_STR_T:    strncpy(tn.name, "str",      63); advance(p); break;
        case TOK_TY_BOOL:     strncpy(tn.name, "bool",     63); advance(p); break;
        case TOK_TY_BYTE:     strncpy(tn.name, "byte",     63); advance(p); break;
        case TOK_TY_DECINT:   strncpy(tn.name, "decint",   63); advance(p); break;
        case TOK_TY_WAVFLOAT: strncpy(tn.name, "wavfloat", 63); advance(p); break;
        case TOK_VOID:        strncpy(tn.name, "void",     63); advance(p); break;
        case TOK_IDENT:
            strncpy(tn.name, t->text, 63);
            advance(p);
            break;
        default:
            parse_error(p, "line %d:%d: expected type name, got '%s'",
                        t->line, t->col, t->text);
            return tn;
    }

    /* optional dimension brackets: SpectArray[N] / SpectMatrix[R,C] etc. */
    if (check(p, TOK_LBRACKET)) {
        advance(p); /* consume [ */
        while (!check(p, TOK_RBRACKET) && !check(p, TOK_EOF) && !p->had_error) {
            Token* dim_tok = peek(p, 0);
            if (dim_tok->kind == TOK_INT_LIT) {
                if (tn.ndims < 8)
                    tn.dims[tn.ndims++] = (int)dim_tok->data.int_val;
                advance(p);
            } else {
                parse_error(p, "line %d:%d: expected integer dimension in type, got '%s'",
                            dim_tok->line, dim_tok->col, dim_tok->text);
                break;
            }
            if (!match(p, TOK_COMMA)) break;
        }
        expect(p, TOK_RBRACKET);
    }

    return tn;
}

/* =========================================================
 * Param parsing
 * ========================================================= */

static ASTNode* parse_param(Parser* p) {
    Token* name_tok = peek(p, 0);
    ASTNode* nd = node_new(ND_PARAM, name_tok->line, name_tok->col);
    if (!nd) return NULL;
    nd->as.param.has_type    = 0;
    nd->as.param.default_val = NULL;

    if (name_tok->kind == TOK_SELF) {
        strncpy(nd->as.param.name, "self", 63);
        advance(p);
        return nd;
    }

    if (name_tok->kind != TOK_IDENT) {
        parse_error(p, "line %d:%d: expected parameter name, got '%s'",
                    name_tok->line, name_tok->col, name_tok->text);
        node_free(nd);
        return NULL;
    }
    strncpy(nd->as.param.name, name_tok->text, 63);
    advance(p);

    if (match(p, TOK_COLON)) {
        nd->as.param.type_annot = parse_type(p);
        nd->as.param.has_type   = 1;
    }

    if (match(p, TOK_ASSIGN)) {
        nd->as.param.default_val = parse_expr(p);
    }

    return nd;
}

static NodeList parse_params(Parser* p) {
    NodeList params;
    nodelist_init(&params);

    expect(p, TOK_LPAREN);
    if (p->had_error) return params;

    skip_newlines(p);
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF) && !p->had_error) {
        ASTNode* param = parse_param(p);
        if (param) nodelist_push(&params, param);
        skip_newlines(p);
        if (!match(p, TOK_COMMA)) break;
        skip_newlines(p);
    }

    expect(p, TOK_RPAREN);
    return params;
}

/* =========================================================
 * Import
 * ========================================================= */

static ASTNode* parse_import(Parser* p) {
    Token* tok = expect(p, TOK_IMPORT);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_IMPORT, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.import.is_from    = 0;
    nd->as.import.name_count = 0;
    nd->as.import.alias[0]   = '\0';
    nd->as.import.module[0]  = '\0';

    Token* mod = peek(p, 0);
    if (mod->kind == TOK_IDENT || mod->kind == TOK_STR_LIT) {
        strncpy(nd->as.import.module, mod->text, 127);
        advance(p);
    } else {
        parse_error(p, "line %d:%d: expected module name after import", mod->line, mod->col);
        node_free(nd);
        return NULL;
    }

    if (match(p, TOK_AS)) {
        Token* alias = peek(p, 0);
        if (alias->kind == TOK_IDENT) {
            strncpy(nd->as.import.alias, alias->text, 63);
            advance(p);
        }
    }

    return nd;
}

static ASTNode* parse_from_import(Parser* p) {
    Token* tok = expect(p, TOK_FROM);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_IMPORT, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.import.is_from    = 1;
    nd->as.import.name_count = 0;
    nd->as.import.alias[0]   = '\0';
    nd->as.import.module[0]  = '\0';

    Token* mod = peek(p, 0);
    if (mod->kind == TOK_IDENT || mod->kind == TOK_STR_LIT) {
        strncpy(nd->as.import.module, mod->text, 127);
        advance(p);
    } else {
        parse_error(p, "line %d:%d: expected module name after from", mod->line, mod->col);
        node_free(nd);
        return NULL;
    }

    expect(p, TOK_IMPORT);
    if (p->had_error) { node_free(nd); return NULL; }

    /* parse name list */
    while (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF) && !check(p, TOK_SEMICOLON)
           && !p->had_error) {
        Token* name_tok = peek(p, 0);
        if (name_tok->kind == TOK_IDENT && nd->as.import.name_count < 8) {
            strncpy(nd->as.import.names[nd->as.import.name_count], name_tok->text, 63);
            nd->as.import.name_count++;
            advance(p);
        } else break;
        if (!match(p, TOK_COMMA)) break;
    }

    return nd;
}

/* =========================================================
 * Function definition
 * ========================================================= */

static ASTNode* parse_func(Parser* p, int dec_count, char decorators[][32], int is_method) {
    Token* tok = expect(p, TOK_FUNC);
    if (!tok) return NULL;

    Token* name_tok = peek(p, 0);
    if (name_tok->kind != TOK_IDENT) {
        parse_error(p, "line %d:%d: expected function name after 'func'",
                    name_tok->line, name_tok->col);
        return NULL;
    }

    ASTNode* nd = node_new(ND_FUNC_DEF, tok->line, tok->col);
    if (!nd) return NULL;
    strncpy(nd->as.func_def.name, name_tok->text, 63);
    advance(p);

    nd->as.func_def.is_method = is_method;
    nd->as.func_def.dec_count = (dec_count > 8) ? 8 : dec_count;
    for (int i = 0; i < nd->as.func_def.dec_count; i++)
        strncpy(nd->as.func_def.decorators[i], decorators[i], 31);

    /* optional type params: <T, U, ...> */
    if (match(p, TOK_LT)) {
        /* just consume type param names until > — we don't build type-param nodes here */
        while (!check(p, TOK_GT) && !check(p, TOK_EOF) && !p->had_error) {
            advance(p);
            if (!match(p, TOK_COMMA)) break;
        }
        expect(p, TOK_GT);
    }

    /* parameters */
    nd->as.func_def.params = parse_params(p);
    if (p->had_error) { node_free(nd); return NULL; }

    /* optional return type: -> type */
    memset(&nd->as.func_def.ret_type, 0, sizeof(TypeNode));
    if (match(p, TOK_ARROW)) {
        nd->as.func_def.ret_type = parse_type(p);
    }

    /* : */
    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }

    /* body */
    nd->as.func_def.body = parse_block(p);
    return nd;
}

/* =========================================================
 * Struct definition
 * ========================================================= */

static ASTNode* parse_struct(Parser* p, int dec_count, char decorators[][32]) {
    Token* tok = expect(p, TOK_STRUCT);
    if (!tok) return NULL;

    Token* name_tok = peek(p, 0);
    if (name_tok->kind != TOK_IDENT) {
        parse_error(p, "line %d:%d: expected struct name after 'struct'",
                    name_tok->line, name_tok->col);
        return NULL;
    }

    ASTNode* nd = node_new(ND_STRUCT_DEF, tok->line, tok->col);
    if (!nd) return NULL;
    strncpy(nd->as.struct_def.name, name_tok->text, 63);
    advance(p);
    nodelist_init(&nd->as.struct_def.fields);
    nodelist_init(&nd->as.struct_def.methods);

    (void)dec_count; (void)decorators; /* stored if needed later */

    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }

    skip_newlines(p);
    expect(p, TOK_INDENT);
    if (p->had_error) { node_free(nd); return NULL; }

    skip_newlines(p);
    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->had_error) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) break;

        /* collect decorators for methods */
        int mdec_count = 0;
        char mdecs[8][32];
        memset(mdecs, 0, sizeof(mdecs));
        while (check(p, TOK_DECORATOR) && mdec_count < 8) {
            Token* dt = advance(p);
            strncpy(mdecs[mdec_count++], dt->data.dec_name, 31);
            skip_newlines(p);
        }

        if (check(p, TOK_FUNC)) {
            ASTNode* method = parse_func(p, mdec_count, mdecs, 1);
            if (method) nodelist_push(&nd->as.struct_def.methods, method);
        } else if (check(p, TOK_IDENT)) {
            /* field definition: name: type [= default] */
            Token* fname = advance(p);
            ASTNode* field = node_new(ND_FIELD_DEF, fname->line, fname->col);
            if (!field) break;
            strncpy(field->as.field_def.name, fname->text, 63);
            field->as.field_def.default_val = NULL;

            expect(p, TOK_COLON);
            if (!p->had_error) {
                field->as.field_def.type_annot = parse_type(p);
            }
            if (!p->had_error && match(p, TOK_ASSIGN)) {
                field->as.field_def.default_val = parse_expr(p);
            }
            nodelist_push(&nd->as.struct_def.fields, field);
        } else {
            /* skip unknown token to avoid infinite loop */
            advance(p);
        }

        skip_newlines(p);
    }

    expect(p, TOK_DEDENT);
    return nd;
}

/* =========================================================
 * if / elif / else
 * ========================================================= */

static ASTNode* parse_if(Parser* p) {
    Token* tok = expect(p, TOK_IF);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_IF, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.if_stmt.has_else  = 0;
    nd->as.if_stmt.elif_count = 0;
    for (int i = 0; i < 8; i++) {
        nd->as.if_stmt.elif_conds[i] = NULL;
        nodelist_init(&nd->as.if_stmt.elif_bodies[i]);
    }
    nodelist_init(&nd->as.if_stmt.else_body);

    nd->as.if_stmt.cond = parse_expr(p);
    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }
    nd->as.if_stmt.then_body = parse_block(p);

    /* elif / else chains */
    while (!p->had_error) {
        skip_newlines(p);
        if (check(p, TOK_ELIF)) {
            advance(p);
            int ec = nd->as.if_stmt.elif_count;
            if (ec < 8) {
                nd->as.if_stmt.elif_conds[ec] = parse_expr(p);
                expect(p, TOK_COLON);
                if (p->had_error) break;
                nd->as.if_stmt.elif_bodies[ec] = parse_block(p);
                nd->as.if_stmt.elif_count++;
            }
        } else if (check(p, TOK_ELSE)) {
            advance(p);
            expect(p, TOK_COLON);
            if (p->had_error) break;
            nd->as.if_stmt.else_body = parse_block(p);
            nd->as.if_stmt.has_else  = 1;
            break;
        } else {
            break;
        }
    }

    return nd;
}

/* =========================================================
 * wave_if
 * ========================================================= */

static ASTNode* parse_wave_if(Parser* p) {
    Token* tok = expect(p, TOK_WAVE_IF);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_WAVE_IF, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.wave_if.at_count = 0;
    nd->as.wave_if.has_else = 0;
    nodelist_init(&nd->as.wave_if.else_body);
    for (int i = 0; i < 16; i++)
        nodelist_init(&nd->as.wave_if.at_bodies[i]);

    nd->as.wave_if.subject = parse_expr(p);
    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }

    skip_newlines(p);
    expect(p, TOK_INDENT);
    if (p->had_error) { node_free(nd); return NULL; }
    skip_newlines(p);

    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->had_error) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) break;

        if (check(p, TOK_AT_KW)) {
            advance(p); /* consume 'at' */
            Token* val_tok = peek(p, 0);
            int at_val = 0;
            if (val_tok->kind == TOK_INT_LIT) {
                at_val = (int)val_tok->data.int_val;
                advance(p);
            } else {
                parse_error(p, "line %d:%d: expected integer after 'at'",
                            val_tok->line, val_tok->col);
                break;
            }
            expect(p, TOK_COLON);
            if (p->had_error) break;

            int idx = nd->as.wave_if.at_count;
            if (idx < 16) {
                nd->as.wave_if.at_vals[idx] = at_val;
                nd->as.wave_if.at_bodies[idx] = parse_block(p);
                nd->as.wave_if.at_count++;
            }
        } else if (check(p, TOK_ELSE)) {
            advance(p);
            expect(p, TOK_COLON);
            if (p->had_error) break;
            nd->as.wave_if.else_body = parse_block(p);
            nd->as.wave_if.has_else  = 1;
        } else {
            advance(p); /* skip unexpected */
        }

        skip_newlines(p);
    }

    expect(p, TOK_DEDENT);
    return nd;
}

/* =========================================================
 * for loop (range-based or wave-for)
 * ========================================================= */

static ASTNode* parse_for(Parser* p) {
    Token* tok = expect(p, TOK_FOR);
    if (!tok) return NULL;

    /* peek: if we see "ident, ident in" -> wave_for */
    Token* t0 = peek(p, 0);
    Token* t1 = peek(p, 1);
    Token* t2 = peek(p, 2);

    if (t0->kind == TOK_IDENT && t1->kind == TOK_COMMA && t2->kind == TOK_IDENT) {
        /* wave_for: state, weight in subject: */
        ASTNode* nd = node_new(ND_WAVE_FOR, tok->line, tok->col);
        if (!nd) return NULL;
        strncpy(nd->as.wave_for.state_var,  t0->text, 63);
        advance(p); /* state */
        advance(p); /* , */
        strncpy(nd->as.wave_for.weight_var, t2->text, 63);
        advance(p); /* weight */

        expect(p, TOK_IN);
        if (p->had_error) { node_free(nd); return NULL; }

        nd->as.wave_for.subject = parse_expr(p);
        expect(p, TOK_COLON);
        if (p->had_error) { node_free(nd); return NULL; }
        nd->as.wave_for.body = parse_block(p);
        return nd;
    }

    /* regular for */
    ASTNode* nd = node_new(ND_FOR, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.for_stmt.target[0] = '\0';

    Token* tgt = peek(p, 0);
    if (tgt->kind == TOK_IDENT) {
        strncpy(nd->as.for_stmt.target, tgt->text, 63);
        advance(p);
    } else {
        parse_error(p, "line %d:%d: expected loop variable after 'for'",
                    tgt->line, tgt->col);
        node_free(nd);
        return NULL;
    }

    expect(p, TOK_IN);
    if (p->had_error) { node_free(nd); return NULL; }

    nd->as.for_stmt.iterable = parse_expr(p);
    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }
    nd->as.for_stmt.body = parse_block(p);
    return nd;
}

/* =========================================================
 * while loop
 * ========================================================= */

static ASTNode* parse_while(Parser* p) {
    Token* tok = expect(p, TOK_WHILE);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_WHILE, tok->line, tok->col);
    if (!nd) return NULL;

    nd->as.while_stmt.cond = parse_expr(p);
    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }
    nd->as.while_stmt.body = parse_block(p);
    return nd;
}

/* =========================================================
 * try / except
 * ========================================================= */

static ASTNode* parse_try(Parser* p) {
    Token* tok = expect(p, TOK_TRY);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_TRY, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.try_stmt.has_except = 0;
    nd->as.try_stmt.exc_var[0] = '\0';

    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }
    nd->as.try_stmt.try_body = parse_block(p);

    /* optional except clause */
    skip_newlines(p);
    if (check(p, TOK_EXCEPT)) {
        advance(p); /* consume 'except' */
        nd->as.try_stmt.has_except = 1;

        /* optional binding: except e: or except SomeError as e: */
        if (check(p, TOK_IDENT)) {
            Token* exc_tok = peek(p, 0);
            /* check if next is 'as' or ':' */
            if (peek(p, 1)->kind == TOK_AS) {
                advance(p); /* skip error type */
                advance(p); /* skip 'as' */
                Token* var_tok = peek(p, 0);
                if (var_tok->kind == TOK_IDENT) {
                    strncpy(nd->as.try_stmt.exc_var, var_tok->text, 63);
                    advance(p);
                }
            } else if (peek(p, 1)->kind == TOK_COLON) {
                /* bare name binding: except e: */
                strncpy(nd->as.try_stmt.exc_var, exc_tok->text, 63);
                advance(p);
            }
        }

        expect(p, TOK_COLON);
        if (p->had_error) { node_free(nd); return NULL; }
        nd->as.try_stmt.handler = parse_block(p);
    }

    return nd;
}

/* =========================================================
 * match statement
 * ========================================================= */

static ASTNode* parse_match(Parser* p) {
    Token* tok = expect(p, TOK_MATCH);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_MATCH, tok->line, tok->col);
    if (!nd) return NULL;
    nodelist_init(&nd->as.match.cases);

    nd->as.match.subject = parse_expr(p);
    expect(p, TOK_COLON);
    if (p->had_error) { node_free(nd); return NULL; }

    skip_newlines(p);
    expect(p, TOK_INDENT);
    if (p->had_error) { node_free(nd); return NULL; }
    skip_newlines(p);

    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF) && !p->had_error) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) break;

        Token* pat_tok = peek(p, 0);
        if (pat_tok->kind != TOK_IDENT) {
            parse_error(p, "line %d:%d: expected pattern name in match case",
                        pat_tok->line, pat_tok->col);
            break;
        }

        ASTNode* cas = node_new(ND_MATCH_CASE, pat_tok->line, pat_tok->col);
        if (!cas) break;
        strncpy(cas->as.match_case.pattern, pat_tok->text, 15);
        cas->as.match_case.binding_count = 0;
        nodelist_init(&cas->as.match_case.body);
        advance(p); /* consume pattern name */

        /* optional bindings: (a, b, ...) */
        if (match(p, TOK_LPAREN)) {
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF) && !p->had_error) {
                Token* bind = peek(p, 0);
                if (bind->kind == TOK_IDENT && cas->as.match_case.binding_count < 4) {
                    strncpy(cas->as.match_case.bindings[cas->as.match_case.binding_count],
                            bind->text, 63);
                    cas->as.match_case.binding_count++;
                    advance(p);
                } else break;
                if (!match(p, TOK_COMMA)) break;
            }
            expect(p, TOK_RPAREN);
        }

        expect(p, TOK_COLON);
        if (p->had_error) { node_free(cas); break; }
        cas->as.match_case.body = parse_block(p);
        nodelist_push(&nd->as.match.cases, cas);

        skip_newlines(p);
    }

    expect(p, TOK_DEDENT);
    return nd;
}

/* =========================================================
 * return statement
 * ========================================================= */

static ASTNode* parse_return(Parser* p) {
    Token* tok = expect(p, TOK_RETURN);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_RETURN, tok->line, tok->col);
    if (!nd) return NULL;
    nd->as.ret.value = NULL;

    /* return with no value: followed by newline, DEDENT, or EOF */
    if (!check(p, TOK_NEWLINE) && !check(p, TOK_DEDENT) && !check(p, TOK_EOF)
        && !check(p, TOK_SEMICOLON)) {
        nd->as.ret.value = parse_expr(p);
    }

    return nd;
}

/* =========================================================
 * Assignment or expression statement
 * ========================================================= */

static ASTNode* parse_assignment_or_expr(Parser* p) {
    Token* cur = peek(p, 0);
    Token* nxt = peek(p, 1);

    /* typed assignment: ident : type = expr */
    if (cur->kind == TOK_IDENT && nxt->kind == TOK_COLON) {
        /* check if this is actually a typed assignment (not just a block label) */
        /* we treat "ident : type = expr" as typed assignment */
        Token* saved = cur;
        ASTNode* target = node_new(ND_IDENT, cur->line, cur->col);
        if (!target) return NULL;
        strncpy(target->as.ident.name, cur->text, 63);
        advance(p); /* ident */
        advance(p); /* : */

        TypeNode tn = parse_type(p);
        if (p->had_error) { node_free(target); return NULL; }

        if (!match(p, TOK_ASSIGN)) {
            parse_error(p, "line %d:%d: expected '=' after typed variable declaration",
                        saved->line, saved->col);
            node_free(target);
            return NULL;
        }

        ASTNode* val = parse_expr(p);
        ASTNode* nd  = node_new(ND_ASSIGN, saved->line, saved->col);
        if (!nd) { node_free(target); node_free(val); return NULL; }
        nd->as.assign.target   = target;
        nd->as.assign.value    = val;
        nd->as.assign.type_annot = tn;
        nd->as.assign.has_type   = 1;
        return nd;
    }

    /* plain assignment: ident = expr  (target may be more complex via parse_expr) */
    if (cur->kind == TOK_IDENT && nxt->kind == TOK_ASSIGN) {
        ASTNode* target = node_new(ND_IDENT, cur->line, cur->col);
        if (!target) return NULL;
        strncpy(target->as.ident.name, cur->text, 63);
        advance(p); /* ident */
        advance(p); /* = */

        ASTNode* val = parse_expr(p);
        ASTNode* nd  = node_new(ND_ASSIGN, cur->line, cur->col);
        if (!nd) { node_free(target); node_free(val); return NULL; }
        nd->as.assign.target   = target;
        nd->as.assign.value    = val;
        nd->as.assign.has_type = 0;
        memset(&nd->as.assign.type_annot, 0, sizeof(TypeNode));
        return nd;
    }

    /* augmented assignment: ident op= expr */
    if (cur->kind == TOK_IDENT && (nxt->kind == TOK_PLUS_ASSIGN ||
                                    nxt->kind == TOK_MINUS_ASSIGN ||
                                    nxt->kind == TOK_STAR_ASSIGN)) {
        ASTNode* target = node_new(ND_IDENT, cur->line, cur->col);
        if (!target) return NULL;
        strncpy(target->as.ident.name, cur->text, 63);
        advance(p); /* ident */
        Token* op_tok = advance(p); /* op= */

        const char* op_str = "+=";
        if (op_tok->kind == TOK_MINUS_ASSIGN) op_str = "-=";
        else if (op_tok->kind == TOK_STAR_ASSIGN) op_str = "*=";

        ASTNode* val = parse_expr(p);
        ASTNode* nd  = node_new(ND_AUG_ASSIGN, cur->line, cur->col);
        if (!nd) { node_free(target); node_free(val); return NULL; }
        nd->as.aug_assign.target = target;
        nd->as.aug_assign.value  = val;
        strncpy(nd->as.aug_assign.op, op_str, 3);
        return nd;
    }

    /* expression statement — but we need to check for complex assignment targets
     * like a.b = expr or a[i] = expr after we parse the LHS */
    ASTNode* lhs = parse_expr(p);
    if (p->had_error || !lhs) return lhs;

    if (check(p, TOK_ASSIGN)) {
        advance(p);
        ASTNode* val = parse_expr(p);
        ASTNode* nd  = node_new(ND_ASSIGN, lhs->line, lhs->col);
        if (!nd) { node_free(lhs); node_free(val); return NULL; }
        nd->as.assign.target   = lhs;
        nd->as.assign.value    = val;
        nd->as.assign.has_type = 0;
        memset(&nd->as.assign.type_annot, 0, sizeof(TypeNode));
        return nd;
    }

    if (check(p, TOK_PLUS_ASSIGN) || check(p, TOK_MINUS_ASSIGN) || check(p, TOK_STAR_ASSIGN)) {
        Token* op_tok = advance(p);
        const char* op_str = "+=";
        if (op_tok->kind == TOK_MINUS_ASSIGN) op_str = "-=";
        else if (op_tok->kind == TOK_STAR_ASSIGN) op_str = "*=";

        ASTNode* val = parse_expr(p);
        ASTNode* nd  = node_new(ND_AUG_ASSIGN, lhs->line, lhs->col);
        if (!nd) { node_free(lhs); node_free(val); return NULL; }
        nd->as.aug_assign.target = lhs;
        nd->as.aug_assign.value  = val;
        strncpy(nd->as.aug_assign.op, op_str, 3);
        return nd;
    }

    /* plain expression statement */
    ASTNode* nd = node_new(ND_EXPR_STMT, lhs->line, lhs->col);
    if (!nd) { node_free(lhs); return NULL; }
    nd->as.expr_stmt.expr = lhs;
    return nd;
}

/* =========================================================
 * Statement dispatcher
 * ========================================================= */

static ASTNode* parse_stmt(Parser* p) {
    skip_newlines(p);
    Token* cur = peek(p, 0);

    switch (cur->kind) {
        case TOK_IF:      return parse_if(p);
        case TOK_WAVE_IF: return parse_wave_if(p);
        case TOK_FOR:     return parse_for(p);
        case TOK_WHILE:   return parse_while(p);
        case TOK_MATCH:   return parse_match(p);
        case TOK_TRY:     return parse_try(p);
        case TOK_BREAK: {
            Token* t = advance(p);
            return node_new(ND_BREAK, t->line, t->col);
        }
        case TOK_CONTINUE: {
            Token* t = advance(p);
            return node_new(ND_CONTINUE, t->line, t->col);
        }
        case TOK_RETURN:  return parse_return(p);
        case TOK_IMPORT:  return parse_import(p);
        case TOK_FROM:    return parse_from_import(p);
        case TOK_FUNC: {
            char empty[8][32];
            memset(empty, 0, sizeof(empty));
            return parse_func(p, 0, empty, 0);
        }
        case TOK_STRUCT: {
            char empty[8][32];
            memset(empty, 0, sizeof(empty));
            return parse_struct(p, 0, empty);
        }
        case TOK_FREE: {
            advance(p);
            ASTNode* nd = node_new(ND_FREE, cur->line, cur->col);
            if (!nd) return NULL;
            expect(p, TOK_LPAREN);
            nd->as.free_stmt.target = parse_expr(p);
            expect(p, TOK_RPAREN);
            return nd;
        }
        case TOK_C_INLINE_START: {
            advance(p);
            ASTNode* nd = node_new(ND_C_INLINE, cur->line, cur->col);
            if (!nd) return NULL;
            nd->as.c_inline.code = cur->data.c_code; /* pointer from lexer */
            return nd;
        }
        case TOK_NEWLINE:
        case TOK_SEMICOLON:
            advance(p);
            return NULL; /* empty statement — caller loops */
        default:
            return parse_assignment_or_expr(p);
    }
}

/* =========================================================
 * Top-level: handles decorators then func/struct/stmt
 * ========================================================= */

static ASTNode* parse_top_level(Parser* p) {
    skip_newlines(p);
    if (check(p, TOK_EOF)) return NULL;

    /* collect decorators */
    int dec_count = 0;
    char decorators[8][32];
    memset(decorators, 0, sizeof(decorators));

    while (check(p, TOK_DECORATOR) && dec_count < 8) {
        Token* dt = advance(p);
        strncpy(decorators[dec_count++], dt->data.dec_name, 31);
        skip_newlines(p);
    }

    if (check(p, TOK_FUNC))
        return parse_func(p, dec_count, decorators, 0);
    if (check(p, TOK_STRUCT))
        return parse_struct(p, dec_count, decorators);

    /* if decorators were collected but no func/struct follows, error */
    if (dec_count > 0) {
        Token* cur = peek(p, 0);
        parse_error(p, "line %d:%d: decorator must precede 'func' or 'struct'",
                    cur->line, cur->col);
        return NULL;
    }

    return parse_stmt(p);
}

/* =========================================================
 * Expression parsing — precedence climbing
 * ========================================================= */

static ASTNode* parse_expr(Parser* p) {
    return parse_or_expr(p);
}

/* Level 1: or */
static ASTNode* parse_or_expr(Parser* p) {
    ASTNode* left = parse_and_expr(p);
    while (!p->had_error && check(p, TOK_OR)) {
        Token* op = advance(p);
        ASTNode* right = parse_and_expr(p);
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return left;
        strncpy(nd->as.binary.op, "or", 3);
        nd->as.binary.left  = left;
        nd->as.binary.right = right;
        left = nd;
    }
    return left;
}

/* Level 2: and */
static ASTNode* parse_and_expr(Parser* p) {
    ASTNode* left = parse_not_expr(p);
    while (!p->had_error && check(p, TOK_AND)) {
        Token* op = advance(p);
        ASTNode* right = parse_not_expr(p);
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return left;
        strncpy(nd->as.binary.op, "and", 3);
        nd->as.binary.left  = left;
        nd->as.binary.right = right;
        left = nd;
    }
    return left;
}

/* Level 3: not */
static ASTNode* parse_not_expr(Parser* p) {
    if (check(p, TOK_NOT)) {
        Token* op = advance(p);
        ASTNode* operand = parse_not_expr(p);
        ASTNode* nd = node_new(ND_UNARY, op->line, op->col);
        if (!nd) return operand;
        strncpy(nd->as.unary.op, "not", 3);
        nd->as.unary.operand = operand;
        return nd;
    }
    return parse_comparison(p);
}

/* Level 4: ==, !=, <, >, <=, >=, ~=, in, not in */
static ASTNode* parse_comparison(Parser* p) {
    ASTNode* left = parse_wave_ops(p);
    while (!p->had_error) {
        Token* cur = peek(p, 0);
        const char* op_str = NULL;
        switch (cur->kind) {
            case TOK_EQ:       op_str = "=="; break;
            case TOK_NEQ:      op_str = "!="; break;
            case TOK_LT:       op_str = "<";  break;
            case TOK_GT:       op_str = ">";  break;
            case TOK_LTE:      op_str = "<="; break;
            case TOK_GTE:      op_str = ">="; break;
            case TOK_FUZZY_EQ: op_str = "~="; break;
            case TOK_IN:       op_str = "in"; break;
            case TOK_NOT: {
                /* not in */
                if (peek(p, 1)->kind == TOK_IN) {
                    advance(p); /* consume 'not' */
                    advance(p); /* consume 'in' */
                    ASTNode* right = parse_wave_ops(p);
                    ASTNode* nd = node_new(ND_BINARY, cur->line, cur->col);
                    if (!nd) return left;
                    strncpy(nd->as.binary.op, "ni", 3);
                    nd->as.binary.left  = left;
                    nd->as.binary.right = right;
                    left = nd;
                    continue;
                }
                break;
            }
            default: break;
        }
        if (!op_str) break;
        Token* op = advance(p);
        ASTNode* right = parse_wave_ops(p);
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return left;
        strncpy(nd->as.binary.op, op_str, 3);
        nd->as.binary.left  = left;
        nd->as.binary.right = right;
        left = nd;
    }
    return left;
}

/* Level 5: wave ops ~>, <~, ><, <> */
static ASTNode* parse_wave_ops(Parser* p) {
    ASTNode* left = parse_additive(p);
    while (!p->had_error) {
        Token* cur = peek(p, 0);
        const char* op_str = NULL;
        switch (cur->kind) {
            case TOK_COLLAPSE:  op_str = "~>"; break;
            case TOK_SPREAD_OP: op_str = "<~"; break;
            case TOK_INTERFERE: op_str = "><"; break;
            case TOK_RESONATE:  op_str = "<>"; break;
            default: break;
        }
        if (!op_str) break;
        Token* op = advance(p);
        ASTNode* right = parse_additive(p);
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return left;
        strncpy(nd->as.binary.op, op_str, 3);
        nd->as.binary.left  = left;
        nd->as.binary.right = right;
        left = nd;
    }
    return left;
}

/* Level 6: +, - */
static ASTNode* parse_additive(Parser* p) {
    ASTNode* left = parse_multiplicative(p);
    while (!p->had_error && (check(p, TOK_PLUS) || check(p, TOK_MINUS))) {
        Token* op = advance(p);
        ASTNode* right = parse_multiplicative(p);
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return left;
        nd->as.binary.op[0] = (op->kind == TOK_PLUS) ? '+' : '-';
        nd->as.binary.op[1] = '\0';
        nd->as.binary.left  = left;
        nd->as.binary.right = right;
        left = nd;
    }
    return left;
}

/* Level 7: *, /, %, @ (matmul) */
static ASTNode* parse_multiplicative(Parser* p) {
    ASTNode* left = parse_power(p);
    while (!p->had_error) {
        Token* cur = peek(p, 0);
        const char* op_str = NULL;
        switch (cur->kind) {
            case TOK_STAR:       op_str = "*"; break;
            case TOK_SLASH:      op_str = "/"; break;
            case TOK_PERCENT:    op_str = "%"; break;
            case TOK_MATMUL_OP:  op_str = "@"; break;
            case TOK_FLOOR_DIV:  op_str = "//"; break;
            default: break;
        }
        if (!op_str) break;
        Token* op = advance(p);
        ASTNode* right = parse_power(p);
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return left;
        strncpy(nd->as.binary.op, op_str, 3);
        nd->as.binary.left  = left;
        nd->as.binary.right = right;
        left = nd;
    }
    return left;
}

/* Level 8: ** (right-associative) */
static ASTNode* parse_power(Parser* p) {
    ASTNode* base = parse_unary(p);
    if (!p->had_error && check(p, TOK_POWER)) {
        Token* op = advance(p);
        ASTNode* exp = parse_power(p); /* right-associative recursion */
        ASTNode* nd = node_new(ND_BINARY, op->line, op->col);
        if (!nd) return base;
        strncpy(nd->as.binary.op, "**", 3);
        nd->as.binary.left  = base;
        nd->as.binary.right = exp;
        return nd;
    }
    return base;
}

/* Level 9: unary -, not, ? */
static ASTNode* parse_unary(Parser* p) {
    if (check(p, TOK_MINUS)) {
        Token* op = advance(p);
        ASTNode* operand = parse_unary(p);
        ASTNode* nd = node_new(ND_UNARY, op->line, op->col);
        if (!nd) return operand;
        strncpy(nd->as.unary.op, "-", 2);
        nd->as.unary.operand = operand;
        return nd;
    }
    if (check(p, TOK_NOT)) {
        Token* op = advance(p);
        ASTNode* operand = parse_unary(p);
        ASTNode* nd = node_new(ND_UNARY, op->line, op->col);
        if (!nd) return operand;
        strncpy(nd->as.unary.op, "not", 4);
        nd->as.unary.operand = operand;
        return nd;
    }
    if (check(p, TOK_OBSERVE)) {
        Token* op = advance(p);
        ASTNode* operand = parse_unary(p);
        ASTNode* nd = node_new(ND_UNARY, op->line, op->col);
        if (!nd) return operand;
        strncpy(nd->as.unary.op, "?", 2);
        nd->as.unary.operand = operand;
        return nd;
    }
    return parse_postfix(p);
}

/* Level 10: postfix — . () [] */
static ASTNode* parse_postfix(Parser* p) {
    ASTNode* node = parse_primary(p);
    if (!node) return NULL;

    while (!p->had_error) {
        if (check(p, TOK_DOT)) {
            Token* dot = advance(p);
            Token* attr_tok = peek(p, 0);
            if (attr_tok->kind != TOK_IDENT) {
                parse_error(p, "line %d:%d: expected attribute name after '.'",
                            attr_tok->line, attr_tok->col);
                return node;
            }
            advance(p); /* consume attr name */

            if (check(p, TOK_LPAREN)) {
                /* method call */
                ASTNode* mc = node_new(ND_METHOD_CALL, dot->line, dot->col);
                if (!mc) return node;
                mc->as.method_call.obj = node;
                strncpy(mc->as.method_call.method, attr_tok->text, 63);
                mc->as.method_call.args = parse_call_args(p);
                node = mc;
            } else {
                /* attribute access */
                ASTNode* attr = node_new(ND_ATTRIBUTE, dot->line, dot->col);
                if (!attr) return node;
                attr->as.attribute.obj = node;
                strncpy(attr->as.attribute.attr, attr_tok->text, 63);
                node = attr;
            }
        } else if (check(p, TOK_LPAREN)) {
            /* function call */
            Token* tok = peek(p, 0);
            ASTNode* call = node_new(ND_CALL, tok->line, tok->col);
            if (!call) return node;
            call->as.call.callee = node;
            call->as.call.args   = parse_call_args(p);
            node = call;
        } else if (check(p, TOK_LBRACKET)) {
            Token* tok = advance(p); /* [ */
            ASTNode* idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            ASTNode* index = node_new(ND_INDEX, tok->line, tok->col);
            if (!index) { node_free(idx); return node; }
            index->as.index_expr.obj   = node;
            index->as.index_expr.index = idx;
            node = index;
        } else {
            break;
        }
    }

    return node;
}

/* parse_call_args: ( arg, arg, ... ) */
static NodeList parse_call_args(Parser* p) {
    NodeList args;
    nodelist_init(&args);
    expect(p, TOK_LPAREN);
    if (p->had_error) return args;

    skip_newlines(p);
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF) && !p->had_error) {
        ASTNode* arg = parse_expr(p);
        if (arg) nodelist_push(&args, arg);
        skip_newlines(p);
        if (!match(p, TOK_COMMA)) break;
        skip_newlines(p);
    }
    expect(p, TOK_RPAREN);
    return args;
}

/* Array literal: [ expr, expr, ... ] */
static ASTNode* parse_array_literal(Parser* p) {
    Token* tok = expect(p, TOK_LBRACKET);
    if (!tok) return NULL;

    ASTNode* nd = node_new(ND_ARRAY_LIT, tok->line, tok->col);
    if (!nd) return NULL;
    nodelist_init(&nd->as.array_lit.elements);

    skip_newlines(p);
    while (!check(p, TOK_RBRACKET) && !check(p, TOK_EOF) && !p->had_error) {
        ASTNode* elem = parse_expr(p);
        if (elem) nodelist_push(&nd->as.array_lit.elements, elem);
        skip_newlines(p);
        if (!match(p, TOK_COMMA)) break;
        skip_newlines(p);
    }
    expect(p, TOK_RBRACKET);
    return nd;
}

/* Helper: lex and parse a single expression from a C string.
   Returns a heap-allocated ASTNode* or NULL on error. */
static ASTNode* fstr_parse_expr(const char* src) {
    Lexer* lex = lexer_new(src);
    if (!lexer_tokenize(lex)) { lexer_free(lex); return NULL; }
    Parser* sub = parser_new(lex->tokens, lex->tok_count);
    ASTNode* nd = NULL;
    /* Use a throwaway Parser to call the static parse_expr */
    /* We can't call parse_expr directly since it's static, but we
       can call parser_parse and take the first statement's expression */
    ASTNode* prog = parser_parse(sub);
    if (prog && prog->as.program.stmts.count > 0) {
        ASTNode* stmt = prog->as.program.stmts.items[0];
        if (stmt->kind == ND_EXPR_STMT) {
            nd = stmt->as.expr_stmt.expr;
            stmt->as.expr_stmt.expr = NULL; /* detach so node_free doesn't kill it */
        }
    }
    node_free(prog);
    parser_free(sub);
    lexer_free(lex);
    return nd;
}

/* Level 11: primary */
static ASTNode* parse_primary(Parser* p) {
    Token* cur = peek(p, 0);

    switch (cur->kind) {
        case TOK_INT_LIT: {
            ASTNode* nd = node_new(ND_INT_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            nd->as.int_lit.value = cur->data.int_val;
            advance(p);
            return nd;
        }
        case TOK_FLOAT_LIT: {
            ASTNode* nd = node_new(ND_FLOAT_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            nd->as.float_lit.value = cur->data.float_val;
            advance(p);
            return nd;
        }
        case TOK_STR_LIT: {
            ASTNode* nd = node_new(ND_STR_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            const char* src_str = cur->data.c_code ? cur->data.c_code : cur->text;
            nd->as.str_lit.value = (char*)malloc(strlen(src_str) + 1);
            if (nd->as.str_lit.value)
                strcpy(nd->as.str_lit.value, src_str);
            advance(p);
            return nd;
        }
        case TOK_FSTR_LIT: {
            ASTNode* nd = node_new(ND_FSTR_LIT, cur->line, cur->col);
            if (!nd) { advance(p); return NULL; }
            const char* raw = cur->data.c_code ? cur->data.c_code : "";

            /* Count the number of interpolation expressions */
            int n_exprs = 0;
            for (const char* s = raw; *s; s++) {
                if (*s == '{') {
                    if (*(s+1) == '{') { s++; continue; } /* {{ escape */
                    n_exprs++;
                }
            }

            nd->as.fstr_lit.part_count = n_exprs + 1;
            nd->as.fstr_lit.parts = (char**)calloc((size_t)(n_exprs + 1), sizeof(char*));
            nd->as.fstr_lit.expr_count = n_exprs;
            nd->as.fstr_lit.exprs = n_exprs > 0
                ? (ASTNode**)calloc((size_t)n_exprs, sizeof(ASTNode*)) : NULL;

            /* Split raw into alternating literal parts and expressions */
            int pi = 0, ei = 0;
            const char* pos = raw;
            while (pi < n_exprs + 1) {
                /* collect literal up to next un-escaped '{' */
                size_t lit_cap = 64, lit_len = 0;
                char* lit = (char*)malloc(lit_cap);
                const char* scan = pos;
                while (*scan) {
                    if (*scan == '{' && *(scan+1) != '{') break;   /* interpolation start */
                    if (*scan == '{' && *(scan+1) == '{') { /* {{ -> { */
                        if (lit_len + 1 >= lit_cap) { lit_cap *= 2; lit = (char*)realloc(lit, lit_cap); }
                        lit[lit_len++] = '{'; scan += 2; continue;
                    }
                    if (*scan == '}' && *(scan+1) == '}') { /* }} -> } */
                        if (lit_len + 1 >= lit_cap) { lit_cap *= 2; lit = (char*)realloc(lit, lit_cap); }
                        lit[lit_len++] = '}'; scan += 2; continue;
                    }
                    if (lit_len + 1 >= lit_cap) { lit_cap *= 2; lit = (char*)realloc(lit, lit_cap); }
                    lit[lit_len++] = *scan++;
                }
                lit[lit_len] = '\0';
                nd->as.fstr_lit.parts[pi++] = lit;
                pos = scan;

                if (!*pos || pi > n_exprs) break;

                /* pos is at '{': skip it and find matching '}' */
                pos++; /* skip '{' */
                const char* close = pos;
                int depth = 1;
                while (*close && depth > 0) {
                    if (*close == '{') depth++;
                    else if (*close == '}') depth--;
                    if (depth > 0) close++;
                }
                /* [pos .. close) is the expression source */
                size_t elen = (size_t)(close - pos);
                char* expr_src = (char*)malloc(elen + 1);
                memcpy(expr_src, pos, elen);
                expr_src[elen] = '\0';

                nd->as.fstr_lit.exprs[ei++] = fstr_parse_expr(expr_src);
                free(expr_src);

                pos = close; /* points at '}' */
                if (*pos == '}') pos++; /* skip '}' */
            }
            /* fill any remaining parts with empty strings */
            while (pi < n_exprs + 1) {
                nd->as.fstr_lit.parts[pi++] = strdup("");
            }

            advance(p);
            return nd;
        }
        case TOK_TRUE: {
            ASTNode* nd = node_new(ND_BOOL_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            nd->as.bool_lit.value = 1;
            advance(p);
            return nd;
        }
        case TOK_FALSE: {
            ASTNode* nd = node_new(ND_BOOL_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            nd->as.bool_lit.value = 0;
            advance(p);
            return nd;
        }
        case TOK_NULL_KW: {
            ASTNode* nd = node_new(ND_NULL_LIT, cur->line, cur->col);
            advance(p);
            return nd;
        }
        case TOK_WAVE_LIT: {
            ASTNode* nd = node_new(ND_WAVE_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            memcpy(nd->as.wave_lit.weights, cur->data.wave_weights,
                   sizeof(float) * 10);
            advance(p);
            return nd;
        }
        case TOK_RANGE_LIT: {
            ASTNode* nd = node_new(ND_RANGE_LIT, cur->line, cur->col);
            if (!nd) return NULL;
            nd->as.range_lit.lo = cur->data.range.lo;
            nd->as.range_lit.hi = cur->data.range.hi;
            advance(p);
            return nd;
        }
        case TOK_ALLOC: {
            Token* tok = advance(p);
            expect(p, TOK_LPAREN);
            ASTNode* nd = node_new(ND_ALLOC, tok->line, tok->col);
            if (!nd) return NULL;
            nd->as.alloc.type_annot = parse_type(p);
            expect(p, TOK_RPAREN);
            return nd;
        }
        case TOK_LBRACE: {
            /* dict literal: {key: val, key: val, ...} */
            Token* tok = advance(p); /* consume '{' */
            ASTNode* nd = node_new(ND_DICT_LIT, tok->line, tok->col);
            if (!nd) return NULL;
            nodelist_init(&nd->as.dict_lit.keys);
            nodelist_init(&nd->as.dict_lit.values);

            skip_newlines(p);
            while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF) && !p->had_error) {
                ASTNode* key = parse_expr(p);
                if (key) nodelist_push(&nd->as.dict_lit.keys, key);
                expect(p, TOK_COLON);
                ASTNode* val = parse_expr(p);
                if (val) nodelist_push(&nd->as.dict_lit.values, val);
                skip_newlines(p);
                if (!match(p, TOK_COMMA)) break;
                skip_newlines(p);
            }
            expect(p, TOK_RBRACE);
            return nd;
        }
        case TOK_LBRACKET:
            return parse_array_literal(p);
        case TOK_LPAREN: {
            advance(p); /* ( */
            ASTNode* inner = parse_expr(p);
            expect(p, TOK_RPAREN);
            return inner;
        }
        case TOK_SELF: {
            ASTNode* nd = node_new(ND_IDENT, cur->line, cur->col);
            if (!nd) return NULL;
            strncpy(nd->as.ident.name, "self", 63);
            advance(p);
            return nd;
        }
        case TOK_IDENT: {
            /* Check for range expression: ident(..) or just identifier */
            ASTNode* nd = node_new(ND_IDENT, cur->line, cur->col);
            if (!nd) return NULL;
            strncpy(nd->as.ident.name, cur->text, 63);
            advance(p);
            return nd;
        }
        default: {
            parse_error(p, "line %d:%d: unexpected token '%s' in expression",
                        cur->line, cur->col, cur->text);
            return NULL;
        }
    }
}

/* =========================================================
 * parser_parse — entry point
 * ========================================================= */

ASTNode* parser_parse(Parser* p) {
    ASTNode* prog = node_new(ND_PROGRAM, 0, 0);
    if (!prog) return NULL;
    nodelist_init(&prog->as.program.stmts);

    while (!check(p, TOK_EOF) && !p->had_error) {
        skip_newlines(p);
        if (check(p, TOK_EOF)) break;
        ASTNode* stmt = parse_top_level(p);
        if (stmt) nodelist_push(&prog->as.program.stmts, stmt);
    }

    if (p->had_error) {
        node_free(prog);
        return NULL;
    }
    return prog;
}

/* =========================================================
 * parser_dump — debug AST printer
 * ========================================================= */

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static void dump_typnode(TypeNode* t, int indent) {
    print_indent(indent);
    printf("TypeNode: %s%s", t->is_ptr ? "*" : "", t->name);
    if (t->ndims > 0) {
        printf("[");
        for (int i = 0; i < t->ndims; i++) {
            if (i) printf(",");
            printf("%d", t->dims[i]);
        }
        printf("]");
    }
    printf("\n");
}

void parser_dump(ASTNode* node, int indent) {
    if (!node) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);

    switch (node->kind) {
        case ND_PROGRAM:
            printf("Program [%d stmts]\n", node->as.program.stmts.count);
            for (int i = 0; i < node->as.program.stmts.count; i++)
                parser_dump(node->as.program.stmts.items[i], indent + 1);
            break;

        case ND_IMPORT:
            if (node->as.import.is_from) {
                printf("Import from '%s' names=%d\n", node->as.import.module,
                       node->as.import.name_count);
            } else {
                printf("Import '%s'%s\n", node->as.import.module,
                       node->as.import.alias[0] ? " as ..." : "");
            }
            break;

        case ND_FUNC_DEF:
            printf("FuncDef '%s' params=%d decs=%d method=%d\n",
                   node->as.func_def.name,
                   node->as.func_def.params.count,
                   node->as.func_def.dec_count,
                   node->as.func_def.is_method);
            print_indent(indent + 1);
            printf("RetType: ");
            dump_typnode(&node->as.func_def.ret_type, 0);
            for (int i = 0; i < node->as.func_def.params.count; i++)
                parser_dump(node->as.func_def.params.items[i], indent + 1);
            for (int i = 0; i < node->as.func_def.body.count; i++)
                parser_dump(node->as.func_def.body.items[i], indent + 1);
            break;

        case ND_STRUCT_DEF:
            printf("StructDef '%s' fields=%d methods=%d\n",
                   node->as.struct_def.name,
                   node->as.struct_def.fields.count,
                   node->as.struct_def.methods.count);
            for (int i = 0; i < node->as.struct_def.fields.count; i++)
                parser_dump(node->as.struct_def.fields.items[i], indent + 1);
            for (int i = 0; i < node->as.struct_def.methods.count; i++)
                parser_dump(node->as.struct_def.methods.items[i], indent + 1);
            break;

        case ND_PARAM:
            printf("Param '%s' has_type=%d default=%s\n",
                   node->as.param.name, node->as.param.has_type,
                   node->as.param.default_val ? "yes" : "no");
            if (node->as.param.has_type)
                dump_typnode(&node->as.param.type_annot, indent + 1);
            if (node->as.param.default_val)
                parser_dump(node->as.param.default_val, indent + 1);
            break;

        case ND_FIELD_DEF:
            printf("FieldDef '%s'\n", node->as.field_def.name);
            dump_typnode(&node->as.field_def.type_annot, indent + 1);
            if (node->as.field_def.default_val)
                parser_dump(node->as.field_def.default_val, indent + 1);
            break;

        case ND_ASSIGN:
            printf("Assign has_type=%d\n", node->as.assign.has_type);
            if (node->as.assign.has_type)
                dump_typnode(&node->as.assign.type_annot, indent + 1);
            parser_dump(node->as.assign.target, indent + 1);
            parser_dump(node->as.assign.value,  indent + 1);
            break;

        case ND_AUG_ASSIGN:
            printf("AugAssign op='%s'\n", node->as.aug_assign.op);
            parser_dump(node->as.aug_assign.target, indent + 1);
            parser_dump(node->as.aug_assign.value,  indent + 1);
            break;

        case ND_RETURN:
            printf("Return\n");
            if (node->as.ret.value)
                parser_dump(node->as.ret.value, indent + 1);
            break;

        case ND_IF:
            printf("If elif_count=%d has_else=%d\n",
                   node->as.if_stmt.elif_count, node->as.if_stmt.has_else);
            print_indent(indent + 1); printf("Cond:\n");
            parser_dump(node->as.if_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("Then:\n");
            for (int i = 0; i < node->as.if_stmt.then_body.count; i++)
                parser_dump(node->as.if_stmt.then_body.items[i], indent + 2);
            for (int e = 0; e < node->as.if_stmt.elif_count; e++) {
                print_indent(indent + 1); printf("Elif[%d]:\n", e);
                parser_dump(node->as.if_stmt.elif_conds[e], indent + 2);
                for (int i = 0; i < node->as.if_stmt.elif_bodies[e].count; i++)
                    parser_dump(node->as.if_stmt.elif_bodies[e].items[i], indent + 2);
            }
            if (node->as.if_stmt.has_else) {
                print_indent(indent + 1); printf("Else:\n");
                for (int i = 0; i < node->as.if_stmt.else_body.count; i++)
                    parser_dump(node->as.if_stmt.else_body.items[i], indent + 2);
            }
            break;

        case ND_WAVE_IF:
            printf("WaveIf at_count=%d has_else=%d\n",
                   node->as.wave_if.at_count, node->as.wave_if.has_else);
            print_indent(indent + 1); printf("Subject:\n");
            parser_dump(node->as.wave_if.subject, indent + 2);
            for (int a = 0; a < node->as.wave_if.at_count; a++) {
                print_indent(indent + 1);
                printf("At %d:\n", node->as.wave_if.at_vals[a]);
                for (int i = 0; i < node->as.wave_if.at_bodies[a].count; i++)
                    parser_dump(node->as.wave_if.at_bodies[a].items[i], indent + 2);
            }
            if (node->as.wave_if.has_else) {
                print_indent(indent + 1); printf("Else:\n");
                for (int i = 0; i < node->as.wave_if.else_body.count; i++)
                    parser_dump(node->as.wave_if.else_body.items[i], indent + 2);
            }
            break;

        case ND_FOR:
            printf("For target='%s'\n", node->as.for_stmt.target);
            print_indent(indent + 1); printf("Iterable:\n");
            parser_dump(node->as.for_stmt.iterable, indent + 2);
            print_indent(indent + 1); printf("Body:\n");
            for (int i = 0; i < node->as.for_stmt.body.count; i++)
                parser_dump(node->as.for_stmt.body.items[i], indent + 2);
            break;

        case ND_WAVE_FOR:
            printf("WaveFor state='%s' weight='%s'\n",
                   node->as.wave_for.state_var, node->as.wave_for.weight_var);
            print_indent(indent + 1); printf("Subject:\n");
            parser_dump(node->as.wave_for.subject, indent + 2);
            print_indent(indent + 1); printf("Body:\n");
            for (int i = 0; i < node->as.wave_for.body.count; i++)
                parser_dump(node->as.wave_for.body.items[i], indent + 2);
            break;

        case ND_WHILE:
            printf("While\n");
            print_indent(indent + 1); printf("Cond:\n");
            parser_dump(node->as.while_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("Body:\n");
            for (int i = 0; i < node->as.while_stmt.body.count; i++)
                parser_dump(node->as.while_stmt.body.items[i], indent + 2);
            break;

        case ND_MATCH:
            printf("Match cases=%d\n", node->as.match.cases.count);
            print_indent(indent + 1); printf("Subject:\n");
            parser_dump(node->as.match.subject, indent + 2);
            for (int i = 0; i < node->as.match.cases.count; i++)
                parser_dump(node->as.match.cases.items[i], indent + 1);
            break;

        case ND_MATCH_CASE:
            printf("MatchCase pattern='%s' bindings=%d\n",
                   node->as.match_case.pattern, node->as.match_case.binding_count);
            for (int b = 0; b < node->as.match_case.binding_count; b++) {
                print_indent(indent + 1);
                printf("Bind: %s\n", node->as.match_case.bindings[b]);
            }
            for (int i = 0; i < node->as.match_case.body.count; i++)
                parser_dump(node->as.match_case.body.items[i], indent + 1);
            break;

        case ND_EXPR_STMT:
            printf("ExprStmt\n");
            parser_dump(node->as.expr_stmt.expr, indent + 1);
            break;

        case ND_FREE:
            printf("Free\n");
            parser_dump(node->as.free_stmt.target, indent + 1);
            break;

        case ND_C_INLINE:
            printf("CInline: %s\n",
                   node->as.c_inline.code ? node->as.c_inline.code : "(null)");
            break;

        case ND_BINARY:
            printf("Binary op='%s'\n", node->as.binary.op);
            parser_dump(node->as.binary.left,  indent + 1);
            parser_dump(node->as.binary.right, indent + 1);
            break;

        case ND_UNARY:
            printf("Unary op='%s'\n", node->as.unary.op);
            parser_dump(node->as.unary.operand, indent + 1);
            break;

        case ND_CALL:
            printf("Call args=%d\n", node->as.call.args.count);
            parser_dump(node->as.call.callee, indent + 1);
            for (int i = 0; i < node->as.call.args.count; i++)
                parser_dump(node->as.call.args.items[i], indent + 1);
            break;

        case ND_METHOD_CALL:
            printf("MethodCall method='%s' args=%d\n",
                   node->as.method_call.method, node->as.method_call.args.count);
            parser_dump(node->as.method_call.obj, indent + 1);
            for (int i = 0; i < node->as.method_call.args.count; i++)
                parser_dump(node->as.method_call.args.items[i], indent + 1);
            break;

        case ND_INDEX:
            printf("Index\n");
            parser_dump(node->as.index_expr.obj,   indent + 1);
            parser_dump(node->as.index_expr.index, indent + 1);
            break;

        case ND_ATTRIBUTE:
            printf("Attribute attr='%s'\n", node->as.attribute.attr);
            parser_dump(node->as.attribute.obj, indent + 1);
            break;

        case ND_IDENT:
            printf("Ident '%s'\n", node->as.ident.name);
            break;

        case ND_INT_LIT:
            printf("IntLit %" PRId64 "\n", node->as.int_lit.value);
            break;

        case ND_FLOAT_LIT:
            printf("FloatLit %g\n", node->as.float_lit.value);
            break;

        case ND_STR_LIT:
            printf("StrLit \"%s\"\n",
                   node->as.str_lit.value ? node->as.str_lit.value : "");
            break;

        case ND_BOOL_LIT:
            printf("BoolLit %s\n", node->as.bool_lit.value ? "true" : "false");
            break;

        case ND_NULL_LIT:
            printf("NullLit\n");
            break;

        case ND_WAVE_LIT: {
            printf("WaveLit [");
            for (int i = 0; i < 10; i++) {
                if (i) printf(", ");
                printf("%.3f", node->as.wave_lit.weights[i]);
            }
            printf("]\n");
            break;
        }

        case ND_RANGE_LIT:
            printf("RangeLit %d..%d\n",
                   node->as.range_lit.lo, node->as.range_lit.hi);
            break;

        case ND_ARRAY_LIT:
            printf("ArrayLit elems=%d\n", node->as.array_lit.elements.count);
            for (int i = 0; i < node->as.array_lit.elements.count; i++)
                parser_dump(node->as.array_lit.elements.items[i], indent + 1);
            break;

        case ND_ALLOC:
            printf("Alloc\n");
            dump_typnode(&node->as.alloc.type_annot, indent + 1);
            break;

        case ND_RANGE_EXPR:
            printf("RangeExpr\n");
            if (node->as.range_expr.start)
                parser_dump(node->as.range_expr.start, indent + 1);
            parser_dump(node->as.range_expr.stop,  indent + 1);
            if (node->as.range_expr.step)
                parser_dump(node->as.range_expr.step, indent + 1);
            break;

        default:
            printf("ASTNode(kind=%d) line=%d col=%d\n",
                   (int)node->kind, node->line, node->col);
            break;
    }
}
