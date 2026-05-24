/*
 * interpreter.c — Tree-walk interpreter for the SPECTRA programming language.
 *
 * Dispatch model:
 *   interp_exec  — statement-level nodes (side-effects, no return value)
 *   interp_eval  — expression-level nodes (returns a Value*, ref_count >= 1)
 *
 * Ownership rules:
 *   - interp_eval always returns a NEW reference (caller must val_release).
 *   - env_define/env_set both retain the value they store.
 *   - When a signal is set, functions return NULL / return early; the caller
 *     must propagate by checking interp->signal after every call.
 */

#include "interpreter.h"
#include "builtins.h"
#include "modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>

/* =========================================================================
 * Internal helpers
 * ====================================================================== */

#define CHECK_ERR(interp)  do { if ((interp)->signal == SIG_ERROR) return; } while(0)
#define CHECK_ERR_NULL(interp)  do { if ((interp)->signal == SIG_ERROR) return NULL; } while(0)

/* Forward declarations */
static Value* call_func(Interpreter* interp, Value* fn, Value** args, int argc, int line);
static Value* eval_binary(Interpreter* interp, ASTNode* node);
static Value* eval_unary(Interpreter* interp, ASTNode* node);
static Value* eval_call(Interpreter* interp, ASTNode* node);
static Value* eval_method_call(Interpreter* interp, ASTNode* node);
static Value* eval_index(Interpreter* interp, ASTNode* node);
static void   exec_assign(Interpreter* interp, ASTNode* node);
static void   exec_aug_assign(Interpreter* interp, ASTNode* node);
static void   exec_for(Interpreter* interp, ASTNode* node);
static void   exec_wave_for(Interpreter* interp, ASTNode* node);
static void   exec_match(Interpreter* interp, ASTNode* node);
static void   exec_func_def(Interpreter* interp, ASTNode* node);
static void   exec_struct_def(Interpreter* interp, ASTNode* node);
static Value* eval_alloc(Interpreter* interp, ASTNode* node);
static Value* eval_array_lit(Interpreter* interp, ASTNode* node);

/* Push a new scope, save/restore current_env around block execution */
static Env* push_scope(Interpreter* interp) {
    Env* outer = interp->current_env;
    Env* inner = env_new(outer);
    interp->current_env = inner;
    return outer; /* return outer so caller can restore */
}

static void pop_scope(Interpreter* interp, Env* outer) {
    Env* inner = interp->current_env;
    interp->current_env = outer;
    env_release(inner);
}

/* =========================================================================
 * Error
 * ====================================================================== */

void interp_error(Interpreter* interp, int line, const char* fmt, ...) {
    interp->signal     = SIG_ERROR;
    interp->error_line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(interp->error_msg, sizeof(interp->error_msg), fmt, ap);
    va_end(ap);
}

/* =========================================================================
 * interp_new / interp_free
 * ====================================================================== */

Interpreter* interp_new(void) {
    Interpreter* interp = (Interpreter*)calloc(1, sizeof(Interpreter));
    if (!interp) {
        fprintf(stderr, "fatal: out of memory allocating Interpreter\n");
        exit(1);
    }
    interp->globals     = env_new(NULL);
    interp->current_env = interp->globals;
    interp->signal      = SIG_NONE;
    interp->return_val  = NULL;
    interp->call_depth  = 0;
    interp->sim_mode    = 0;

    register_builtins(interp->globals);
    return interp;
}

void interp_free(Interpreter* interp) {
    if (!interp) return;
    if (interp->return_val) {
        val_release(interp->return_val);
        interp->return_val = NULL;
    }
    /* current_env is either globals or a scope on the stack that should
     * already have been cleaned up. Release globals last. */
    env_release(interp->globals);
    free(interp);
}

/* =========================================================================
 * interp_run
 * ====================================================================== */

int interp_run(Interpreter* interp, ASTNode* program) {
    if (!program || program->kind != ND_PROGRAM) {
        interp_error(interp, 0, "interp_run: expected ND_PROGRAM node");
        return 0;
    }
    NodeList* stmts = &program->as.program.stmts;
    for (int i = 0; i < stmts->count; i++) {
        interp_exec(interp, stmts->items[i]);
        if (interp->signal == SIG_ERROR)  return 0;
        if (interp->signal == SIG_RETURN) interp->signal = SIG_NONE;
        if (interp->signal == SIG_BREAK || interp->signal == SIG_CONTINUE)
            interp->signal = SIG_NONE;
    }
    /* Auto-call main() if defined */
    Value* main_fn = env_get(interp->globals, "main");
    if (main_fn && (main_fn->type == VAL_FUNC || main_fn->type == VAL_NATIVE)) {
        Value* result = call_func(interp, main_fn, NULL, 0, 0);
        if (result) val_release(result);
        if (interp->signal == SIG_RETURN) interp->signal = SIG_NONE;
    }
    return (interp->signal != SIG_ERROR);
}

/* =========================================================================
 * interp_exec_block
 * ====================================================================== */

void interp_exec_block(Interpreter* interp, NodeList* stmts, Env* block_env) {
    Env* outer = interp->current_env;
    Env* scope;

    if (block_env) {
        /* Use the provided env directly (e.g. function call site) */
        scope = env_retain(block_env);
        interp->current_env = scope;
    } else {
        /* Create a fresh child scope */
        scope = env_new(outer);
        interp->current_env = scope;
    }

    for (int i = 0; i < stmts->count; i++) {
        interp_exec(interp, stmts->items[i]);
        if (interp->signal != SIG_NONE) break;
    }

    interp->current_env = outer;
    env_release(scope);
}

/* =========================================================================
 * interp_exec — statement dispatch
 * ====================================================================== */

void interp_exec(Interpreter* interp, ASTNode* stmt) {
    if (!stmt) return;
    if (interp->signal != SIG_NONE) return;

    switch (stmt->kind) {

    /* ------------------------------------------------------------------ */
    case ND_IMPORT: {
        char err[256] = {0};
        const char* names[8];
        int nc = stmt->as.import.name_count;
        for (int i = 0; i < nc && i < 8; i++)
            names[i] = stmt->as.import.names[i];
        if (!modules_load(interp->current_env, stmt->as.import.module,
                          nc > 0 ? names : NULL, nc, err)) {
            interp_error(interp, stmt->line, "import '%s' failed: %s",
                         stmt->as.import.module, err);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FUNC_DEF:
        exec_func_def(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_STRUCT_DEF:
        exec_struct_def(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_ASSIGN:
        exec_assign(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_AUG_ASSIGN:
        exec_aug_assign(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_RETURN: {
        Value* v = NULL;
        if (stmt->as.ret.value) {
            v = interp_eval(interp, stmt->as.ret.value);
            if (interp->signal == SIG_ERROR) return;
        } else {
            v = val_null();
        }
        /* Release any previously stored return value */
        if (interp->return_val) {
            val_release(interp->return_val);
        }
        interp->return_val = val_retain(v);
        val_release(v);
        interp->signal = SIG_RETURN;
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_IF: {
        Value* cond = interp_eval(interp, stmt->as.if_stmt.cond);
        if (interp->signal == SIG_ERROR) return;
        int truthy = val_truthy(cond);
        val_release(cond);

        if (truthy) {
            interp_exec_block(interp, &stmt->as.if_stmt.then_body, NULL);
        } else {
            /* Try elif clauses */
            int matched = 0;
            for (int i = 0; i < stmt->as.if_stmt.elif_count; i++) {
                Value* ec = interp_eval(interp, stmt->as.if_stmt.elif_conds[i]);
                if (interp->signal == SIG_ERROR) return;
                int et = val_truthy(ec);
                val_release(ec);
                if (et) {
                    interp_exec_block(interp, &stmt->as.if_stmt.elif_bodies[i], NULL);
                    matched = 1;
                    break;
                }
            }
            if (!matched && stmt->as.if_stmt.has_else) {
                interp_exec_block(interp, &stmt->as.if_stmt.else_body, NULL);
            }
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_WAVE_IF: {
        Value* subj = interp_eval(interp, stmt->as.wave_if.subject);
        if (interp->signal == SIG_ERROR) return;
        if (subj->type != VAL_SPECT) {
            interp_error(interp, stmt->line,
                "wave_if: subject must be a Specton, got %s",
                val_type_name(subj->type));
            val_release(subj);
            return;
        }
        uint8_t peak = spect_peak(subj->as.spect);
        val_release(subj);

        int matched = 0;
        for (int i = 0; i < stmt->as.wave_if.at_count; i++) {
            if (stmt->as.wave_if.at_vals[i] == (int)peak) {
                interp_exec_block(interp, &stmt->as.wave_if.at_bodies[i], NULL);
                matched = 1;
                break;
            }
        }
        if (!matched && stmt->as.wave_if.has_else) {
            interp_exec_block(interp, &stmt->as.wave_if.else_body, NULL);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FOR:
        exec_for(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_WAVE_FOR:
        exec_wave_for(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_WHILE: {
        while (1) {
            Value* cond = interp_eval(interp, stmt->as.while_stmt.cond);
            if (interp->signal == SIG_ERROR) return;
            int truthy = val_truthy(cond);
            val_release(cond);
            if (!truthy) break;

            interp_exec_block(interp, &stmt->as.while_stmt.body, NULL);

            if (interp->signal == SIG_BREAK) {
                interp->signal = SIG_NONE;
                break;
            }
            if (interp->signal == SIG_CONTINUE) {
                interp->signal = SIG_NONE;
                continue;
            }
            if (interp->signal != SIG_NONE) break; /* RETURN or ERROR */
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_TRY: {
        /* Execute try body, catching runtime errors */
        for (int i = 0; i < stmt->as.try_stmt.try_body.count; i++) {
            interp_exec(interp, stmt->as.try_stmt.try_body.items[i]);
            /* If we get an error signal, break and run handler */
            if (interp->signal == SIG_ERROR) {
                if (stmt->as.try_stmt.has_except) {
                    /* bind error message to exc_var if provided */
                    if (stmt->as.try_stmt.exc_var[0] != '\0') {
                        Value* err_val = val_str(interp->error_msg);
                        env_define(interp->current_env,
                                   stmt->as.try_stmt.exc_var, err_val);
                        val_release(err_val);
                    }
                    interp->signal = SIG_NONE;
                    interp->error_msg[0] = '\0';
                    for (int j = 0; j < stmt->as.try_stmt.handler.count; j++)
                        interp_exec(interp, stmt->as.try_stmt.handler.items[j]);
                } else {
                    interp->signal = SIG_NONE;
                    interp->error_msg[0] = '\0';
                }
                break;
            }
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_MATCH:
        exec_match(interp, stmt);
        break;

    /* ------------------------------------------------------------------ */
    case ND_EXPR_STMT: {
        Value* v = interp_eval(interp, stmt->as.expr_stmt.expr);
        if (interp->signal == SIG_ERROR) return;
        val_release(v);
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_FREE: {
        Value* v = interp_eval(interp, stmt->as.free_stmt.target);
        if (interp->signal == SIG_ERROR) return;
        /* Release the value — if it was the only reference it is destroyed */
        val_release(v);
        /* Also attempt to undefine the variable so subsequent access fails cleanly */
        if (stmt->as.free_stmt.target->kind == ND_IDENT) {
            /* Re-bind to null so the name still exists but holds nothing */
            Value* null_v = val_null();
            env_set(interp->current_env,
                    stmt->as.free_stmt.target->as.ident.name,
                    null_v);
            val_release(null_v);
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case ND_BREAK:
        interp->signal = SIG_BREAK;
        break;

    /* ------------------------------------------------------------------ */
    case ND_CONTINUE:
        interp->signal = SIG_CONTINUE;
        break;

    /* ------------------------------------------------------------------ */
    case ND_C_INLINE:
        fprintf(stderr, "warning: c_inline not supported in interpreter mode (line %d)\n",
                stmt->line);
        break;

    /* ------------------------------------------------------------------ */
    default:
        /* Any expression node used as a statement (shouldn't happen after
         * well-formed parse, but handle gracefully). */
        {
            Value* v = interp_eval(interp, stmt);
            if (interp->signal == SIG_ERROR) return;
            if (v) val_release(v);
        }
        break;
    }
}

/* =========================================================================
 * exec_func_def — define a user function in current_env
 * ====================================================================== */

static void exec_func_def(Interpreter* interp, ASTNode* node) {
    NodeList* params = &node->as.func_def.params;
    int nparams = params->count;

    char** param_names = (char**)calloc(nparams, sizeof(char*));
    if (nparams > 0 && !param_names) {
        interp_error(interp, node->line, "out of memory allocating param names");
        return;
    }
    for (int i = 0; i < nparams; i++) {
        ASTNode* p = params->items[i];
        param_names[i] = strdup(p->as.param.name);
    }

    Value* fn = val_func(
        node->as.func_def.name,
        param_names,
        nparams,
        node,                        /* body reference (borrowed) */
        interp->current_env,         /* closure */
        node->as.func_def.is_method
    );

    /* val_func copies param_names strings, but param_names itself is ours */
    for (int i = 0; i < nparams; i++) free(param_names[i]);
    free(param_names);

    env_define(interp->current_env, node->as.func_def.name, fn);
    val_release(fn);
}

/* =========================================================================
 * exec_struct_def — register a constructor native for the struct type
 *
 * We implement the struct as a VAL_NATIVE that, when called, allocates a
 * VAL_STRUCT_INSTANCE and initialises all declared fields to null (or their
 * default values if provided).  Methods are stored in the instance's
 * `methods` env so method-call dispatch can find them.
 * ====================================================================== */

/* Heap-allocated context passed to the native constructor closure */
typedef struct {
    char   type_name[64];
    char** field_names;
    int    field_count;
    /* Pointers to ND_FIELD_DEF default_val nodes (may be NULL).
     * These are borrowed from the AST which outlives the interpreter. */
    ASTNode** field_defaults;
    /* Method ND_FUNC_DEF nodes */
    ASTNode** method_nodes;
    int       method_count;
} StructCtx;

/* We store struct contexts in a global name-keyed table.
 * Each struct type name maps to its StructCtx.
 * The constructor native uses a thread-local (global) "active_ctx" pointer
 * that is set immediately before the native call dispatch, allowing multiple
 * struct types to share the same C function pointer. */

typedef struct StructCtxEntry {
    char                   type_name[64]; /* key = struct type name */
    StructCtx*             ctx;
    struct StructCtxEntry* next;
} StructCtxEntry;

static StructCtxEntry* g_struct_ctx_list = NULL;
/* Set by exec_struct_def before registering the native; read by struct_constructor */
static StructCtx*      g_active_ctor_ctx = NULL;

static StructCtx* struct_ctx_by_name(const char* name) {
    for (StructCtxEntry* e = g_struct_ctx_list; e; e = e->next)
        if (strcmp(e->type_name, name) == 0) return e->ctx;
    return NULL;
}

/* Forward declare the generic constructor function signature */
static Value* struct_constructor(Interpreter* interp, Value** args, int argc);

static void exec_struct_def(Interpreter* interp, ASTNode* node) {
    NodeList* fields  = &node->as.struct_def.fields;
    NodeList* methods = &node->as.struct_def.methods;

    StructCtx* ctx = (StructCtx*)calloc(1, sizeof(StructCtx));
    if (!ctx) { interp_error(interp, node->line, "out of memory"); return; }

    strncpy(ctx->type_name, node->as.struct_def.name, 63);
    ctx->field_count = fields->count;
    ctx->method_count = methods->count;

    if (ctx->field_count > 0) {
        ctx->field_names    = (char**)calloc(ctx->field_count, sizeof(char*));
        ctx->field_defaults = (ASTNode**)calloc(ctx->field_count, sizeof(ASTNode*));
        if (!ctx->field_names || !ctx->field_defaults) {
            interp_error(interp, node->line, "out of memory"); free(ctx); return;
        }
        for (int i = 0; i < ctx->field_count; i++) {
            ASTNode* f = fields->items[i];
            ctx->field_names[i]    = strdup(f->as.field_def.name);
            ctx->field_defaults[i] = f->as.field_def.default_val; /* may be NULL */
        }
    }

    if (ctx->method_count > 0) {
        ctx->method_nodes = (ASTNode**)calloc(ctx->method_count, sizeof(ASTNode*));
        if (!ctx->method_nodes) {
            interp_error(interp, node->line, "out of memory"); free(ctx); return;
        }
        for (int i = 0; i < ctx->method_count; i++)
            ctx->method_nodes[i] = methods->items[i];
    }

    /* Register in global name-keyed table */
    StructCtxEntry* entry = (StructCtxEntry*)malloc(sizeof(StructCtxEntry));
    if (!entry) { interp_error(interp, node->line, "out of memory"); free(ctx); return; }
    strncpy(entry->type_name, ctx->type_name, 63);
    entry->ctx  = ctx;
    entry->next = g_struct_ctx_list;
    g_struct_ctx_list = entry;

    /* Store a hidden binding "__struct_ctx_<Name>" in the environment so the
     * constructor call can retrieve the correct StructCtx without relying on
     * a single shared global that would be clobbered by multiple struct defs.
     * We encode the pointer as a VAL_NULL placeholder — the real retrieval
     * uses the env binding name to look up the ctx from the name-keyed table. */

    /* Actually: store the struct name itself as a VAL_STR under the mangled key.
     * The native constructor reads this from the env to look up its ctx. */
    char mangle[80];
    snprintf(mangle, sizeof(mangle), "__sctx_%s", ctx->type_name);
    Value* name_val = val_str(ctx->type_name);
    env_define(interp->current_env, mangle, name_val);
    val_release(name_val);

    Value* ctor = val_native(struct_constructor);
    env_define(interp->current_env, node->as.struct_def.name, ctor);
    val_release(ctor);
}

/* The native constructor — called when user writes  Foo(a, b, ...)
 *
 * Finding the right StructCtx: at call time the interpreter's current_env
 * is the call-site scope.  We resolve the callee name from the call node,
 * but NativeFn receives no such info.  Instead we walk current_env looking
 * for a "__sctx_<Name>" binding whose value matches, or use the fact that
 * the callee Value* was looked up by name — the name IS known to env_get.
 *
 * Simplest robust approach: we set g_active_ctor_ctx just before calling
 * any VAL_NATIVE that is struct_constructor, inside eval_call. */
static Value* struct_constructor(Interpreter* interp, Value** args, int argc) {
    StructCtx* ctx = g_active_ctor_ctx;
    if (!ctx) {
        interp_error(interp, 0, "internal: struct context not found");
        return NULL;
    }

    /* Build the instance value */
    char**  fnames = (char**)calloc(ctx->field_count, sizeof(char*));
    Value** fvals  = (Value**)calloc(ctx->field_count, sizeof(Value*));
    if (ctx->field_count > 0 && (!fnames || !fvals)) {
        interp_error(interp, 0, "out of memory creating struct instance");
        free(fnames); free(fvals);
        return NULL;
    }
    for (int i = 0; i < ctx->field_count; i++) {
        fnames[i] = strdup(ctx->field_names[i]);
        if (i < argc) {
            fvals[i] = val_retain(args[i]);
        } else if (ctx->field_defaults[i]) {
            fvals[i] = interp_eval(interp, ctx->field_defaults[i]);
            if (interp->signal == SIG_ERROR) {
                for (int j = 0; j < i; j++) { free(fnames[j]); val_release(fvals[j]); }
                free(fnames); free(fvals);
                return NULL;
            }
            /* interp_eval returned ref_count=1; instance owns this reference. */
        } else {
            fvals[i] = val_null();
        }
    }

    /* Build method env */
    Env* menv = env_new(NULL);
    for (int i = 0; i < ctx->method_count; i++) {
        ASTNode* mnode = ctx->method_nodes[i];
        NodeList* mparams = &mnode->as.func_def.params;
        int npm = mparams->count;
        char** pnames = (char**)calloc(npm, sizeof(char*));
        for (int j = 0; j < npm; j++)
            pnames[j] = strdup(mparams->items[j]->as.param.name);
        Value* mfn = val_func(mnode->as.func_def.name, pnames, npm,
                              mnode, interp->current_env, 1);
        for (int j = 0; j < npm; j++) free(pnames[j]);
        free(pnames);
        env_define(menv, mnode->as.func_def.name, mfn);
        val_release(mfn);
    }

    /* Allocate the Value */
    Value* inst = (Value*)calloc(1, sizeof(Value));
    if (!inst) { interp_error(interp, 0, "out of memory"); env_release(menv); return NULL; }
    inst->type      = VAL_STRUCT_INSTANCE;
    inst->ref_count = 1;
    strncpy(inst->as.instance.type_name, ctx->type_name, 63);
    inst->as.instance.field_names  = fnames;
    inst->as.instance.fields       = fvals;
    inst->as.instance.field_count  = ctx->field_count;
    inst->as.instance.methods      = menv; /* retained inside instance; released by val_release */

    /* Fix up field values: each fvals[i] is already ref_count=1 from
     * val_null / val_retain / interp_eval.  The instance *owns* them —
     * no extra retain needed. */

    return inst;
}

/* =========================================================================
 * exec_assign
 * ====================================================================== */

static void exec_assign(Interpreter* interp, ASTNode* node) {
    Value* val = interp_eval(interp, node->as.assign.value);
    if (interp->signal == SIG_ERROR) return;

    ASTNode* target = node->as.assign.target;

    if (target->kind == ND_IDENT) {
        const char* name = target->as.ident.name;
        /* Try set (update existing) first, then define (create new) */
        if (!env_set(interp->current_env, name, val)) {
            env_define(interp->current_env, name, val);
        }
        val_release(val);

    } else if (target->kind == ND_INDEX) {
        /* array[i] = val  or  matrix[r,c] = val */
        Value* obj = interp_eval(interp, target->as.index_expr.obj);
        if (interp->signal == SIG_ERROR) { val_release(val); return; }

        if (obj->type == VAL_ARRAY) {
            Value* idx = interp_eval(interp, target->as.index_expr.index);
            if (interp->signal == SIG_ERROR) { val_release(val); val_release(obj); return; }
            if (idx->type != VAL_INT) {
                interp_error(interp, target->line, "array index must be INT");
                val_release(idx); val_release(obj); val_release(val);
                return;
            }
            int64_t i = idx->as.integer;
            val_release(idx);
            if (val->type != VAL_SPECT) {
                interp_error(interp, target->line, "array element must be Specton");
                val_release(obj); val_release(val);
                return;
            }
            sarray_set(obj->as.array, (size_t)i, val->as.spect);

        } else if (obj->type == VAL_MATRIX) {
            /* index must be array_lit [r, c] */
            Value* idx = interp_eval(interp, target->as.index_expr.index);
            if (interp->signal == SIG_ERROR) { val_release(val); val_release(obj); return; }
            if (idx->type != VAL_ARRAY || idx->as.array->length < 2) {
                interp_error(interp, target->line,
                    "matrix index must be [row, col]");
                val_release(idx); val_release(obj); val_release(val);
                return;
            }
            size_t r = (size_t)spect_collapse(sarray_get(idx->as.array, 0));
            size_t c = (size_t)spect_collapse(sarray_get(idx->as.array, 1));
            val_release(idx);
            if (val->type != VAL_SPECT) {
                interp_error(interp, target->line, "matrix element must be Specton");
                val_release(obj); val_release(val);
                return;
            }
            smat_set(obj->as.matrix, r, c, val->as.spect);

        } else {
            interp_error(interp, target->line,
                "indexed assignment requires Array or Matrix, got %s",
                val_type_name(obj->type));
        }
        val_release(obj);
        val_release(val);

    } else if (target->kind == ND_ATTRIBUTE) {
        Value* obj = interp_eval(interp, target->as.attribute.obj);
        if (interp->signal == SIG_ERROR) { val_release(val); return; }
        if (obj->type != VAL_STRUCT_INSTANCE) {
            interp_error(interp, target->line,
                "attribute assignment on non-instance type %s",
                val_type_name(obj->type));
            val_release(obj); val_release(val);
            return;
        }
        const char* attr = target->as.attribute.attr;
        int found = 0;
        for (int i = 0; i < obj->as.instance.field_count; i++) {
            if (strcmp(obj->as.instance.field_names[i], attr) == 0) {
                val_release(obj->as.instance.fields[i]);
                obj->as.instance.fields[i] = val_retain(val);
                found = 1;
                break;
            }
        }
        if (!found) {
            interp_error(interp, target->line,
                "struct '%s' has no field '%s'",
                obj->as.instance.type_name, attr);
        }
        val_release(obj);
        val_release(val);

    } else {
        interp_error(interp, node->line, "invalid assignment target");
        val_release(val);
    }
}

/* =========================================================================
 * exec_aug_assign  (+=, -=, *=)
 * ====================================================================== */

static void exec_aug_assign(Interpreter* interp, ASTNode* node) {
    ASTNode* target = node->as.aug_assign.target;

    /* Only ND_IDENT supported for aug_assign in current spec */
    if (target->kind != ND_IDENT) {
        interp_error(interp, node->line,
            "augmented assignment only supported for simple identifiers");
        return;
    }
    const char* name = target->as.ident.name;
    Value* cur = env_get(interp->current_env, name);
    if (!cur) {
        interp_error(interp, node->line,
            "undefined variable '%s' in augmented assignment", name);
        return;
    }

    Value* rhs = interp_eval(interp, node->as.aug_assign.value);
    if (interp->signal == SIG_ERROR) return;

    const char* op = node->as.aug_assign.op;
    Value* result = NULL;

    if (strcmp(op, "+=") == 0) {
        if (cur->type == VAL_SPECT && rhs->type == VAL_SPECT)
            result = val_spect(spect_add(cur->as.spect, rhs->as.spect));
        else if (cur->type == VAL_INT && rhs->type == VAL_INT)
            result = val_int(cur->as.integer + rhs->as.integer);
        else if (cur->type == VAL_FLOAT || rhs->type == VAL_FLOAT) {
            double a = (cur->type == VAL_FLOAT) ? cur->as.number :
                       (cur->type == VAL_INT)   ? (double)cur->as.integer :
                       (double)spect_collapse(cur->as.spect);
            double b = (rhs->type == VAL_FLOAT) ? rhs->as.number :
                       (rhs->type == VAL_INT)   ? (double)rhs->as.integer :
                       (double)spect_collapse(rhs->as.spect);
            result = val_float(a + b);
        } else if (cur->type == VAL_STR && rhs->type == VAL_STR) {
            size_t la = strlen(cur->as.string), lb = strlen(rhs->as.string);
            char* s = (char*)malloc(la + lb + 1);
            memcpy(s, cur->as.string, la);
            memcpy(s + la, rhs->as.string, lb);
            s[la + lb] = '\0';
            result = val_str_own(s);
        } else {
            interp_error(interp, node->line, "+= type mismatch");
        }
    } else if (strcmp(op, "-=") == 0) {
        if (cur->type == VAL_SPECT && rhs->type == VAL_SPECT)
            result = val_spect(spect_sub(cur->as.spect, rhs->as.spect));
        else if (cur->type == VAL_INT && rhs->type == VAL_INT)
            result = val_int(cur->as.integer - rhs->as.integer);
        else if (cur->type == VAL_FLOAT || rhs->type == VAL_FLOAT) {
            double a = (cur->type == VAL_FLOAT) ? cur->as.number :
                       (cur->type == VAL_INT)   ? (double)cur->as.integer :
                       (double)spect_collapse(cur->as.spect);
            double b = (rhs->type == VAL_FLOAT) ? rhs->as.number :
                       (rhs->type == VAL_INT)   ? (double)rhs->as.integer :
                       (double)spect_collapse(rhs->as.spect);
            result = val_float(a - b);
        } else {
            interp_error(interp, node->line, "-= type mismatch");
        }
    } else if (strcmp(op, "*=") == 0) {
        if (cur->type == VAL_SPECT && rhs->type == VAL_SPECT)
            result = val_spect(spect_mul(cur->as.spect, rhs->as.spect));
        else if (cur->type == VAL_INT && rhs->type == VAL_INT)
            result = val_int(cur->as.integer * rhs->as.integer);
        else if (cur->type == VAL_FLOAT || rhs->type == VAL_FLOAT) {
            double a = (cur->type == VAL_FLOAT) ? cur->as.number :
                       (cur->type == VAL_INT)   ? (double)cur->as.integer :
                       (double)spect_collapse(cur->as.spect);
            double b = (rhs->type == VAL_FLOAT) ? rhs->as.number :
                       (rhs->type == VAL_INT)   ? (double)rhs->as.integer :
                       (double)spect_collapse(rhs->as.spect);
            result = val_float(a * b);
        } else {
            interp_error(interp, node->line, "*= type mismatch");
        }
    } else {
        interp_error(interp, node->line, "unknown aug_assign op '%s'", op);
    }

    val_release(rhs);
    if (interp->signal == SIG_ERROR) return;

    if (result) {
        env_set(interp->current_env, name, result);
        val_release(result);
    }
}

/* =========================================================================
 * exec_for
 * ====================================================================== */

static void exec_for(Interpreter* interp, ASTNode* node) {
    const char* tgt = node->as.for_stmt.target;
    ASTNode*    it  = node->as.for_stmt.iterable;

    /* Special-case: ND_RANGE_EXPR iterable — range(n) or range(a,b) or range(a,b,step) */
    if (it->kind == ND_RANGE_EXPR) {
        int64_t start = 0, stop = 0, step = 1;

        if (it->as.range_expr.start) {
            Value* sv = interp_eval(interp, it->as.range_expr.start);
            if (interp->signal == SIG_ERROR) return;
            start = (sv->type == VAL_INT) ? sv->as.integer
                  : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                  : (int64_t)spect_collapse(sv->as.spect);
            val_release(sv);
        }
        {
            Value* sv = interp_eval(interp, it->as.range_expr.stop);
            if (interp->signal == SIG_ERROR) return;
            stop = (sv->type == VAL_INT) ? sv->as.integer
                 : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                 : (int64_t)spect_collapse(sv->as.spect);
            val_release(sv);
        }
        if (it->as.range_expr.step) {
            Value* sv = interp_eval(interp, it->as.range_expr.step);
            if (interp->signal == SIG_ERROR) return;
            step = (sv->type == VAL_INT) ? sv->as.integer
                 : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                 : (int64_t)spect_collapse(sv->as.spect);
            val_release(sv);
            if (step == 0) {
                interp_error(interp, node->line, "for range step cannot be zero");
                return;
            }
        }

        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) {
                Env* outer = push_scope(interp);
                Value* iv = val_int(i);
                env_define(interp->current_env, tgt, iv);
                val_release(iv);

                for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                    interp_exec(interp, node->as.for_stmt.body.items[si]);
                    if (interp->signal != SIG_NONE) break;
                }
                pop_scope(interp, outer);

                if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; return; }
                if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
                if (interp->signal != SIG_NONE) return;
            }
        } else {
            for (int64_t i = start; i > stop; i += step) {
                Env* outer = push_scope(interp);
                Value* iv = val_int(i);
                env_define(interp->current_env, tgt, iv);
                val_release(iv);

                for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                    interp_exec(interp, node->as.for_stmt.body.items[si]);
                    if (interp->signal != SIG_NONE) break;
                }
                pop_scope(interp, outer);

                if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; return; }
                if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
                if (interp->signal != SIG_NONE) return;
            }
        }
        return;
    }

    /* ND_CALL with callee ND_IDENT "range" — also common form */
    if (it->kind == ND_CALL && it->as.call.callee->kind == ND_IDENT &&
        strcmp(it->as.call.callee->as.ident.name, "range") == 0)
    {
        NodeList* rargs = &it->as.call.args;
        int64_t start = 0, stop = 0, step = 1;

        if (rargs->count == 1) {
            Value* sv = interp_eval(interp, rargs->items[0]);
            if (interp->signal == SIG_ERROR) return;
            stop = (sv->type == VAL_INT) ? sv->as.integer
                 : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                 : (int64_t)spect_collapse(sv->as.spect);
            val_release(sv);
        } else if (rargs->count >= 2) {
            Value* sv = interp_eval(interp, rargs->items[0]);
            if (interp->signal == SIG_ERROR) return;
            start = (sv->type == VAL_INT) ? sv->as.integer
                  : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                  : (int64_t)spect_collapse(sv->as.spect);
            val_release(sv);
            sv = interp_eval(interp, rargs->items[1]);
            if (interp->signal == SIG_ERROR) return;
            stop = (sv->type == VAL_INT) ? sv->as.integer
                 : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                 : (int64_t)spect_collapse(sv->as.spect);
            val_release(sv);
            if (rargs->count >= 3) {
                sv = interp_eval(interp, rargs->items[2]);
                if (interp->signal == SIG_ERROR) return;
                step = (sv->type == VAL_INT) ? sv->as.integer
                     : (sv->type == VAL_FLOAT) ? (int64_t)sv->as.number
                     : (int64_t)spect_collapse(sv->as.spect);
                val_release(sv);
                if (step == 0) {
                    interp_error(interp, node->line, "range() step cannot be zero");
                    return;
                }
            }
        }

        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) {
                Env* outer = push_scope(interp);
                Value* iv = val_int(i);
                env_define(interp->current_env, tgt, iv);
                val_release(iv);
                for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                    interp_exec(interp, node->as.for_stmt.body.items[si]);
                    if (interp->signal != SIG_NONE) break;
                }
                pop_scope(interp, outer);
                if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; return; }
                if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
                if (interp->signal != SIG_NONE) return;
            }
        } else {
            for (int64_t i = start; i > stop; i += step) {
                Env* outer = push_scope(interp);
                Value* iv = val_int(i);
                env_define(interp->current_env, tgt, iv);
                val_release(iv);
                for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                    interp_exec(interp, node->as.for_stmt.body.items[si]);
                    if (interp->signal != SIG_NONE) break;
                }
                pop_scope(interp, outer);
                if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; return; }
                if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
                if (interp->signal != SIG_NONE) return;
            }
        }
        return;
    }

    /* General iterable: evaluate and iterate */
    Value* itval = interp_eval(interp, it);
    if (interp->signal == SIG_ERROR) return;

    if (itval->type == VAL_ARRAY) {
        SpectArray* arr = itval->as.array;
        for (size_t i = 0; i < arr->length; i++) {
            Env* outer = push_scope(interp);
            Value* elem = val_spect(sarray_get(arr, i));
            env_define(interp->current_env, tgt, elem);
            val_release(elem);
            for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                interp_exec(interp, node->as.for_stmt.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);
            if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; break; }
            if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
            if (interp->signal != SIG_NONE) break;
        }
    } else if (itval->type == VAL_MATRIX) {
        SpectMatrix* mat = itval->as.matrix;
        for (size_t r = 0; r < mat->rows; r++) {
            /* Yield each row as a SpectArray */
            SpectArray* row = smat_row(mat, r);
            Env* outer = push_scope(interp);
            Value* rv = val_array(row);
            env_define(interp->current_env, tgt, rv);
            val_release(rv);
            for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                interp_exec(interp, node->as.for_stmt.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);
            if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; break; }
            if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
            if (interp->signal != SIG_NONE) break;
        }
    } else if (itval->type == VAL_INT) {
        /* for i in n: → range(n) */
        int64_t stop = itval->as.integer;
        for (int64_t i = 0; i < stop; i++) {
            Env* outer = push_scope(interp);
            Value* iv = val_int(i);
            env_define(interp->current_env, tgt, iv);
            val_release(iv);
            for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                interp_exec(interp, node->as.for_stmt.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);
            if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; break; }
            if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
            if (interp->signal != SIG_NONE) break;
        }
    } else if (itval->type == VAL_LIST) {
        SpectList* lst = itval->as.list;
        for (int i = 0; i < lst->length; i++) {
            Env* outer = push_scope(interp);
            env_define(interp->current_env, tgt, lst->items[i]);
            for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                interp_exec(interp, node->as.for_stmt.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);
            if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; break; }
            if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
            if (interp->signal != SIG_NONE) break;
        }
    } else if (itval->type == VAL_STR) {
        const char* str = itval->as.string ? itval->as.string : "";
        size_t slen = strlen(str);
        for (size_t i = 0; i < slen; i++) {
            char ch[2] = { str[i], '\0' };
            Env* outer = push_scope(interp);
            Value* cv = val_str(ch);
            env_define(interp->current_env, tgt, cv);
            val_release(cv);
            for (int si = 0; si < node->as.for_stmt.body.count; si++) {
                interp_exec(interp, node->as.for_stmt.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);
            if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; break; }
            if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
            if (interp->signal != SIG_NONE) break;
        }
    } else {
        interp_error(interp, node->line,
            "for: cannot iterate over %s", val_type_name(itval->type));
    }

    val_release(itval);
}

/* =========================================================================
 * exec_wave_for
 * ====================================================================== */

static void exec_wave_for(Interpreter* interp, ASTNode* node) {
    Value* subj = interp_eval(interp, node->as.wave_for.subject);
    if (interp->signal == SIG_ERROR) return;
    if (subj->type != VAL_SPECT) {
        interp_error(interp, node->line,
            "wave_for: subject must be a Specton, got %s",
            val_type_name(subj->type));
        val_release(subj);
        return;
    }
    Specton s = subj->as.spect;
    val_release(subj);

    /* Convert to wave for uniform iteration */
    Specton wave = spect_to_wave(s);

    for (int state = 0; state < SPECT_STATES; state++) {
        float w = spect_weight_at(wave, (uint8_t)state);
        if (w <= 0.0f) continue;

        Env* outer = push_scope(interp);
        Value* sv = val_int((int64_t)state);
        Value* wv = val_float((double)w);
        env_define(interp->current_env, node->as.wave_for.state_var,  sv);
        env_define(interp->current_env, node->as.wave_for.weight_var, wv);
        val_release(sv);
        val_release(wv);

        for (int si = 0; si < node->as.wave_for.body.count; si++) {
            interp_exec(interp, node->as.wave_for.body.items[si]);
            if (interp->signal != SIG_NONE) break;
        }
        pop_scope(interp, outer);

        if (interp->signal == SIG_BREAK) { interp->signal = SIG_NONE; break; }
        if (interp->signal == SIG_CONTINUE) { interp->signal = SIG_NONE; continue; }
        if (interp->signal != SIG_NONE) break;
    }
}

/* =========================================================================
 * exec_match
 * ====================================================================== */

static void exec_match(Interpreter* interp, ASTNode* node) {
    Value* subj = interp_eval(interp, node->as.match.subject);
    if (interp->signal == SIG_ERROR) return;
    if (subj->type != VAL_SPECT) {
        interp_error(interp, node->line,
            "match: subject must be a Specton, got %s",
            val_type_name(subj->type));
        val_release(subj);
        return;
    }
    Specton s = subj->as.spect;
    val_release(subj);

    NodeList* cases = &node->as.match.cases;
    for (int ci = 0; ci < cases->count; ci++) {
        ASTNode* mc = cases->items[ci];
        const char* pat = mc->as.match_case.pattern;
        int matched = 0;

        if (strcmp(pat, "Fixed") == 0 && s.mode == SPECT_FIXED) {
            matched = 1;
            Env* outer = push_scope(interp);
            /* Bind the fixed value to the first binding name (conventionally "v") */
            if (mc->as.match_case.binding_count >= 1) {
                Value* bv = val_int((int64_t)s.data.fixed_val);
                env_define(interp->current_env,
                           mc->as.match_case.bindings[0], bv);
                val_release(bv);
            }
            for (int si = 0; si < mc->as.match_case.body.count; si++) {
                interp_exec(interp, mc->as.match_case.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);

        } else if (strcmp(pat, "Range") == 0 && s.mode == SPECT_RANGE) {
            matched = 1;
            Env* outer = push_scope(interp);
            if (mc->as.match_case.binding_count >= 1) {
                Value* lv = val_int((int64_t)s.data.range.lo);
                env_define(interp->current_env,
                           mc->as.match_case.bindings[0], lv);
                val_release(lv);
            }
            if (mc->as.match_case.binding_count >= 2) {
                Value* hv = val_int((int64_t)s.data.range.hi);
                env_define(interp->current_env,
                           mc->as.match_case.bindings[1], hv);
                val_release(hv);
            }
            for (int si = 0; si < mc->as.match_case.body.count; si++) {
                interp_exec(interp, mc->as.match_case.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);

        } else if (strcmp(pat, "Wave") == 0 && s.mode == SPECT_WAVE) {
            matched = 1;
            Env* outer = push_scope(interp);
            if (mc->as.match_case.binding_count >= 1) {
                Value* wv = val_spect(s);
                env_define(interp->current_env,
                           mc->as.match_case.bindings[0], wv);
                val_release(wv);
            }
            for (int si = 0; si < mc->as.match_case.body.count; si++) {
                interp_exec(interp, mc->as.match_case.body.items[si]);
                if (interp->signal != SIG_NONE) break;
            }
            pop_scope(interp, outer);
        }

        if (matched) break;
        if (interp->signal != SIG_NONE) break;
    }
}

/* =========================================================================
 * call_func — invoke a VAL_FUNC value
 * ====================================================================== */

static Value* call_func(Interpreter* interp, Value* fn, Value** args, int argc, int line) {
    if (interp->call_depth >= 500) {
        interp_error(interp, line, "stack overflow (call depth > 500)");
        return NULL;
    }

    /* The body node is the original ND_FUNC_DEF */
    ASTNode* fnode  = fn->as.func.body;
    int      nparams = fn->as.func.param_count;
    Env*     closure = fn->as.func.closure;

    /* New execution environment: child of the closure (lexical scoping) */
    Env* call_env = env_new(closure);

    /* Bind parameters */
    for (int i = 0; i < nparams; i++) {
        Value* arg;
        if (i < argc) {
            arg = args[i];
        } else {
            /* Use default value if the param node provides one */
            ASTNode* pnode = fnode->as.func_def.params.items[i];
            if (pnode->as.param.default_val) {
                /* Evaluate default in the caller's environment */
                arg = interp_eval(interp, pnode->as.param.default_val);
                if (interp->signal == SIG_ERROR) {
                    env_release(call_env);
                    return NULL;
                }
                /* arg ref = 1, will be released after define */
                env_define(call_env, fn->as.func.param_names[i], arg);
                val_release(arg);
                continue;
            } else {
                arg = val_null();
            }
        }
        env_define(call_env, fn->as.func.param_names[i], arg);
    }

    /* Execute body */
    interp->call_depth++;
    Env* saved_env = interp->current_env;
    interp->current_env = call_env;

    NodeList* body = &fnode->as.func_def.body;
    for (int i = 0; i < body->count; i++) {
        interp_exec(interp, body->items[i]);
        if (interp->signal != SIG_NONE) break;
    }

    interp->current_env = saved_env;
    interp->call_depth--;
    env_release(call_env);

    /* Handle return signal */
    if (interp->signal == SIG_RETURN) {
        interp->signal = SIG_NONE;
        Value* rv = interp->return_val;
        interp->return_val = NULL;
        /* rv is already retained; caller takes ownership, must release */
        if (!rv) rv = val_null();
        return rv;
    }
    if (interp->signal == SIG_ERROR) return NULL;

    /* No explicit return → return null */
    return val_null();
}

/* =========================================================================
 * eval_binary
 * ====================================================================== */

/* Helper: coerce a Value* to double for mixed-mode arithmetic */
static double val_to_double(Value* v) {
    if (v->type == VAL_FLOAT)  return v->as.number;
    if (v->type == VAL_INT)    return (double)v->as.integer;
    if (v->type == VAL_SPECT)  return (double)spect_collapse(v->as.spect);
    if (v->type == VAL_BOOL)   return (double)v->as.boolean;
    return 0.0;
}

static int64_t val_to_int(Value* v) {
    if (v->type == VAL_INT)    return v->as.integer;
    if (v->type == VAL_FLOAT)  return (int64_t)v->as.number;
    if (v->type == VAL_SPECT)  return (int64_t)spect_collapse(v->as.spect);
    if (v->type == VAL_BOOL)   return (int64_t)v->as.boolean;
    return 0;
}

static Value* eval_binary(Interpreter* interp, ASTNode* node) {
    const char* op = node->as.binary.op;

    /* Short-circuit for "and" / "or" */
    if (strcmp(op, "and") == 0) {
        Value* L = interp_eval(interp, node->as.binary.left);
        if (interp->signal == SIG_ERROR) return NULL;
        if (!val_truthy(L)) { val_release(L); return val_bool(0); }
        val_release(L);
        Value* R = interp_eval(interp, node->as.binary.right);
        if (interp->signal == SIG_ERROR) return NULL;
        int t = val_truthy(R);
        val_release(R);
        return val_bool(t);
    }
    if (strcmp(op, "or") == 0) {
        Value* L = interp_eval(interp, node->as.binary.left);
        if (interp->signal == SIG_ERROR) return NULL;
        if (val_truthy(L)) { val_release(L); return val_bool(1); }
        val_release(L);
        Value* R = interp_eval(interp, node->as.binary.right);
        if (interp->signal == SIG_ERROR) return NULL;
        int t = val_truthy(R);
        val_release(R);
        return val_bool(t);
    }

    Value* L = interp_eval(interp, node->as.binary.left);
    if (interp->signal == SIG_ERROR) return NULL;
    Value* R = interp_eval(interp, node->as.binary.right);
    if (interp->signal == SIG_ERROR) { val_release(L); return NULL; }

    Value* result = NULL;

    /* ---- Specton-specific operators ---- */
    if (strcmp(op, "><") == 0) {
        /* Destructive interference */
        if (L->type != VAL_SPECT || R->type != VAL_SPECT) goto type_err;
        result = val_spect(spect_interfere(L->as.spect, R->as.spect));
    } else if (strcmp(op, "<>") == 0) {
        /* Resonance */
        if (L->type != VAL_SPECT || R->type != VAL_SPECT) goto type_err;
        result = val_spect(spect_resonate(L->as.spect, R->as.spect));
    } else if (strcmp(op, "~>") == 0) {
        /* Collapse: left operand collapsed to int */
        if (L->type != VAL_SPECT) goto type_err;
        result = val_int((int64_t)spect_collapse(L->as.spect));
    } else if (strcmp(op, "<~") == 0) {
        /* Spread: left operand converted to wave */
        if (L->type != VAL_SPECT) goto type_err;
        result = val_spect(spect_to_wave(L->as.spect));
    } else if (strcmp(op, "~=") == 0) {
        if (L->type != VAL_SPECT || R->type != VAL_SPECT) goto type_err;
        result = val_bool(spect_fuzzy_eq(L->as.spect, R->as.spect, 0.1f));
    } else if (strcmp(op, "@") == 0) {
        /* Matrix multiply */
        if (L->type != VAL_MATRIX || R->type != VAL_MATRIX) goto type_err;
        SpectMatrix* m = smat_matmul(L->as.matrix, R->as.matrix);
        if (!m) {
            interp_error(interp, node->line, "matmul: dimension mismatch");
            goto cleanup;
        }
        result = val_matrix(m);

    /* ---- Arithmetic ---- */
    } else if (strcmp(op, "+") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_spect(spect_add(L->as.spect, R->as.spect));
        else if (L->type == VAL_STR && R->type == VAL_STR) {
            size_t la = strlen(L->as.string), lb = strlen(R->as.string);
            char* s = (char*)malloc(la + lb + 1);
            memcpy(s, L->as.string, la);
            memcpy(s + la, R->as.string, lb);
            s[la + lb] = '\0';
            result = val_str_own(s);
        } else if (L->type == VAL_INT && R->type == VAL_INT)
            result = val_int(L->as.integer + R->as.integer);
        else if (L->type == VAL_FLOAT || R->type == VAL_FLOAT)
            result = val_float(val_to_double(L) + val_to_double(R));
        else goto type_err;

    } else if (strcmp(op, "-") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_spect(spect_sub(L->as.spect, R->as.spect));
        else if (L->type == VAL_INT && R->type == VAL_INT)
            result = val_int(L->as.integer - R->as.integer);
        else if (L->type == VAL_FLOAT || R->type == VAL_FLOAT)
            result = val_float(val_to_double(L) - val_to_double(R));
        else goto type_err;

    } else if (strcmp(op, "*") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_spect(spect_mul(L->as.spect, R->as.spect));
        else if (L->type == VAL_INT && R->type == VAL_INT)
            result = val_int(L->as.integer * R->as.integer);
        else if (L->type == VAL_FLOAT || R->type == VAL_FLOAT)
            result = val_float(val_to_double(L) * val_to_double(R));
        else goto type_err;

    } else if (strcmp(op, "/") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_spect(spect_div(L->as.spect, R->as.spect));
        else if ((L->type == VAL_INT || L->type == VAL_FLOAT) &&
                 (R->type == VAL_INT || R->type == VAL_FLOAT)) {
            double denom = val_to_double(R);
            if (denom == 0.0) {
                interp_error(interp, node->line, "division by zero");
                goto cleanup;
            }
            if (L->type == VAL_INT && R->type == VAL_INT)
                result = val_int(L->as.integer / R->as.integer);
            else
                result = val_float(val_to_double(L) / denom);
        } else goto type_err;

    } else if (strcmp(op, "//") == 0) {
        if (L->type == VAL_INT && R->type == VAL_INT) {
            int64_t lv = L->as.integer, rv = R->as.integer;
            if (rv == 0) { interp_error(interp, node->line, "division by zero"); val_release(L); val_release(R); return NULL; }
            int64_t q = lv / rv;
            if ((lv ^ rv) < 0 && q * rv != lv) q--;
            result = val_int(q);
        } else {
            double lf = (L->type == VAL_INT) ? (double)L->as.integer : L->as.number;
            double rf = (R->type == VAL_INT) ? (double)R->as.integer : R->as.number;
            if (rf == 0.0) { interp_error(interp, node->line, "division by zero"); val_release(L); val_release(R); return NULL; }
            result = val_int((int64_t)floor(lf / rf));
        }

    } else if (strcmp(op, "%") == 0) {
        if (L->type == VAL_INT && R->type == VAL_INT) {
            if (R->as.integer == 0) {
                interp_error(interp, node->line, "modulo by zero");
                goto cleanup;
            }
            result = val_int(L->as.integer % R->as.integer);
        } else goto type_err;

    } else if (strcmp(op, "**") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_spect(spect_pow(L->as.spect, R->as.spect));
        else
            result = val_float(pow(val_to_double(L), val_to_double(R)));

    /* ---- Comparison ---- */
    } else if (strcmp(op, "==") == 0) {
        result = val_bool(val_equal(L, R));
    } else if (strcmp(op, "!=") == 0) {
        result = val_bool(!val_equal(L, R));
    } else if (strcmp(op, "<") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_bool(spect_lt(L->as.spect, R->as.spect));
        else
            result = val_bool(val_to_double(L) < val_to_double(R));
    } else if (strcmp(op, ">") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_bool(spect_gt(L->as.spect, R->as.spect));
        else
            result = val_bool(val_to_double(L) > val_to_double(R));
    } else if (strcmp(op, "<=") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_bool(!spect_gt(L->as.spect, R->as.spect));
        else
            result = val_bool(val_to_double(L) <= val_to_double(R));
    } else if (strcmp(op, ">=") == 0) {
        if (L->type == VAL_SPECT && R->type == VAL_SPECT)
            result = val_bool(!spect_lt(L->as.spect, R->as.spect));
        else
            result = val_bool(val_to_double(L) >= val_to_double(R));

    } else if (strcmp(op, "in") == 0) {
        int found = 0;
        if (R->type == VAL_STR && L->type == VAL_STR) {
            found = (strstr(R->as.string, L->as.string) != NULL);
        } else if (R->type == VAL_LIST) {
            found = val_list_contains(R, L);
        } else if (R->type == VAL_MAP) {
            char* k = val_to_string(L); found = val_map_has(R, k); free(k);
        }
        result = val_bool(found);
    } else if (strcmp(op, "ni") == 0) {   /* not in */
        int found = 0;
        if (R->type == VAL_STR && L->type == VAL_STR) {
            found = (strstr(R->as.string, L->as.string) != NULL);
        } else if (R->type == VAL_LIST) {
            found = val_list_contains(R, L);
        } else if (R->type == VAL_MAP) {
            char* k = val_to_string(L); found = val_map_has(R, k); free(k);
        }
        result = val_bool(!found);
    } else {
        interp_error(interp, node->line, "unknown binary operator '%s'", op);
        goto cleanup;
    }

    val_release(L);
    val_release(R);
    return result;

type_err:
    interp_error(interp, node->line,
        "operator '%s': type mismatch (%s, %s)",
        op, val_type_name(L->type), val_type_name(R->type));
cleanup:
    val_release(L);
    val_release(R);
    return NULL;
}

/* =========================================================================
 * eval_unary
 * ====================================================================== */

static Value* eval_unary(Interpreter* interp, ASTNode* node) {
    const char* op = node->as.unary.op;
    Value* operand = interp_eval(interp, node->as.unary.operand);
    if (interp->signal == SIG_ERROR) return NULL;

    Value* result = NULL;

    if (strcmp(op, "-") == 0) {
        if (operand->type == VAL_SPECT)
            result = val_spect(spect_invert(operand->as.spect));
        else if (operand->type == VAL_INT)
            result = val_int(-operand->as.integer);
        else if (operand->type == VAL_FLOAT)
            result = val_float(-operand->as.number);
        else {
            interp_error(interp, node->line,
                "unary '-' not supported for %s", val_type_name(operand->type));
        }
    } else if (strcmp(op, "not") == 0) {
        result = val_bool(!val_truthy(operand));
    } else if (strcmp(op, "?") == 0) {
        if (operand->type != VAL_SPECT) {
            interp_error(interp, node->line,
                "observe '?' requires Specton operand, got %s",
                val_type_name(operand->type));
        } else {
            result = val_int((int64_t)spect_observe(operand->as.spect));
        }
    } else {
        interp_error(interp, node->line, "unknown unary operator '%s'", op);
    }

    val_release(operand);
    return result;
}

/* =========================================================================
 * eval_call
 * ====================================================================== */

static Value* eval_call(Interpreter* interp, ASTNode* node) {
    Value* callee = interp_eval(interp, node->as.call.callee);
    if (interp->signal == SIG_ERROR) return NULL;

    NodeList* arg_nodes = &node->as.call.args;
    int argc = arg_nodes->count;
    Value** args = (argc > 0) ? (Value**)calloc(argc, sizeof(Value*)) : NULL;

    for (int i = 0; i < argc; i++) {
        args[i] = interp_eval(interp, arg_nodes->items[i]);
        if (interp->signal == SIG_ERROR) {
            for (int j = 0; j < i; j++) val_release(args[j]);
            free(args);
            val_release(callee);
            return NULL;
        }
    }

    Value* result = NULL;
    if (callee->type == VAL_NATIVE) {
        /* If this is a struct constructor, look up its StructCtx by name before
         * calling, so struct_constructor can find the right context. */
        if (callee->as.native == struct_constructor &&
            node->as.call.callee->kind == ND_IDENT)
        {
            const char* sname = node->as.call.callee->as.ident.name;
            g_active_ctor_ctx = struct_ctx_by_name(sname);
        }
        result = callee->as.native(interp, args, argc);
        g_active_ctor_ctx = NULL; /* reset after call */
    } else if (callee->type == VAL_FUNC) {
        result = call_func(interp, callee, args, argc, node->line);
    } else {
        interp_error(interp, node->line,
            "cannot call value of type %s", val_type_name(callee->type));
    }

    for (int i = 0; i < argc; i++) val_release(args[i]);
    free(args);
    val_release(callee);
    return result;
}

/* =========================================================================
 * eval_method_call
 * ====================================================================== */

static Value* eval_method_call(Interpreter* interp, ASTNode* node) {
    Value* obj = interp_eval(interp, node->as.method_call.obj);
    if (interp->signal == SIG_ERROR) return NULL;

    const char* method = node->as.method_call.method;
    NodeList*   arg_nodes = &node->as.method_call.args;
    int argc = arg_nodes->count;

    Value** args = (argc > 0) ? (Value**)calloc(argc, sizeof(Value*)) : NULL;
    for (int i = 0; i < argc; i++) {
        args[i] = interp_eval(interp, arg_nodes->items[i]);
        if (interp->signal == SIG_ERROR) {
            for (int j = 0; j < i; j++) val_release(args[j]);
            free(args); val_release(obj);
            return NULL;
        }
    }

    Value* result = NULL;

    /* ---- Specton methods ---- */
    if (obj->type == VAL_SPECT) {
        Specton s = obj->as.spect;

        if (strcmp(method, "collapse") == 0) {
            result = val_int((int64_t)spect_collapse(s));
        } else if (strcmp(method, "observe") == 0) {
            result = val_int((int64_t)spect_observe(s));
        } else if (strcmp(method, "peak") == 0) {
            result = val_int((int64_t)spect_peak(s));
        } else if (strcmp(method, "entropy") == 0) {
            result = val_float((double)spect_entropy(s));
        } else if (strcmp(method, "normalize") == 0) {
            result = val_spect(spect_normalize(s));
        } else if (strcmp(method, "amplify") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "amplify() needs 1 arg"); goto mc_done; }
            result = val_spect(spect_amplify(s, (float)val_to_double(args[0])));
        } else if (strcmp(method, "attenuate") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "attenuate() needs 1 arg"); goto mc_done; }
            result = val_spect(spect_attenuate(s, (float)val_to_double(args[0])));
        } else if (strcmp(method, "invert") == 0) {
            result = val_spect(spect_invert(s));
        } else if (strcmp(method, "to_wave") == 0) {
            result = val_spect(spect_to_wave(s));
        } else if (strcmp(method, "to_fixed") == 0) {
            result = val_spect(spect_to_fixed(s));
        } else if (strcmp(method, "spread") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "spread() needs 1 arg"); goto mc_done; }
            if (args[0]->type != VAL_SPECT) { interp_error(interp, node->line, "spread() arg must be Specton"); goto mc_done; }
            result = val_spect(spect_spread(s, args[0]->as.spect));
        } else if (strcmp(method, "interfere") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "interfere() needs 1 arg"); goto mc_done; }
            if (args[0]->type != VAL_SPECT) { interp_error(interp, node->line, "interfere() arg must be Specton"); goto mc_done; }
            result = val_spect(spect_interfere(s, args[0]->as.spect));
        } else if (strcmp(method, "resonate") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "resonate() needs 1 arg"); goto mc_done; }
            if (args[0]->type != VAL_SPECT) { interp_error(interp, node->line, "resonate() arg must be Specton"); goto mc_done; }
            result = val_spect(spect_resonate(s, args[0]->as.spect));
        } else if (strcmp(method, "weight_at") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "weight_at() needs 1 arg"); goto mc_done; }
            result = val_float((double)spect_weight_at(s, (uint8_t)val_to_int(args[0])));
        } else if (strcmp(method, "print") == 0) {
            spect_print(s);
            result = val_null();
        } else if (strcmp(method, "print_wave") == 0) {
            spect_print_wave(s);
            result = val_null();
        } else if (strcmp(method, "println") == 0) {
            spect_println(s);
            result = val_null();
        } else if (strcmp(method, "states") == 0) {
            /* wave_for calls .states() — just return self; loop handles iteration */
            result = val_spect(s);
        } else {
            interp_error(interp, node->line,
                "Specton has no method '%s'", method);
        }

    /* ---- String methods ---- */
    } else if (obj->type == VAL_STR) {
        const char* s = obj->as.string ? obj->as.string : "";
        size_t slen = strlen(s);

        if (strcmp(method, "len") == 0) {
            result = val_int((int64_t)slen);
        } else if (strcmp(method, "upper") == 0) {
            char* r = (char*)malloc(slen + 1);
            for (size_t i = 0; i <= slen; i++) r[i] = (char)toupper((unsigned char)s[i]);
            result = val_str_own(r);
        } else if (strcmp(method, "lower") == 0) {
            char* r = (char*)malloc(slen + 1);
            for (size_t i = 0; i <= slen; i++) r[i] = (char)tolower((unsigned char)s[i]);
            result = val_str_own(r);
        } else if (strcmp(method, "strip") == 0) {
            const char* start = s;
            while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
            const char* end = s + slen;
            while (end > start && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\n' || *(end-1) == '\r')) end--;
            size_t n = (size_t)(end - start);
            char* r = (char*)malloc(n + 1);
            memcpy(r, start, n); r[n] = '\0';
            result = val_str_own(r);
        } else if (strcmp(method, "contains") == 0) {
            if (argc < 1) { result = val_bool(0); }
            else { char* sub = val_to_string(args[0]); result = val_bool(strstr(s, sub) != NULL); free(sub); }
        } else if (strcmp(method, "find") == 0) {
            if (argc < 1) { result = val_int(-1); }
            else { char* sub = val_to_string(args[0]); const char* p = strstr(s, sub); result = val_int(p ? (int64_t)(p - s) : -1); free(sub); }
        } else if (strcmp(method, "startswith") == 0) {
            if (argc < 1) { result = val_bool(0); }
            else { char* pre = val_to_string(args[0]); result = val_bool(strncmp(s, pre, strlen(pre)) == 0); free(pre); }
        } else if (strcmp(method, "endswith") == 0) {
            if (argc < 1) { result = val_bool(0); }
            else { char* suf = val_to_string(args[0]); size_t suflen = strlen(suf); result = val_bool(slen >= suflen && strcmp(s + slen - suflen, suf) == 0); free(suf); }
        } else if (strcmp(method, "replace") == 0) {
            if (argc < 2) { result = val_retain(obj); }
            else {
                char* old_s = val_to_string(args[0]); char* new_s = val_to_string(args[1]);
                size_t olen = strlen(old_s), nlen = strlen(new_s);
                /* count occurrences */
                int count = 0; const char* p = s;
                while ((p = strstr(p, old_s))) { count++; p += olen; }
                size_t rsz = slen + (size_t)count * (nlen > olen ? nlen - olen : 0) + 1;
                char* r = (char*)malloc(rsz); r[0] = '\0';
                const char* cur = s;
                while ((p = strstr(cur, old_s))) {
                    strncat(r, cur, (size_t)(p - cur));
                    strcat(r, new_s); cur = p + olen;
                }
                strcat(r, cur);
                free(old_s); free(new_s);
                result = val_str_own(r);
            }
        } else if (strcmp(method, "split") == 0) {
            char* sep = (argc >= 1) ? val_to_string(args[0]) : NULL;
            Value* lst = val_list_new();
            if (!sep || strlen(sep) == 0) {
                /* split on any whitespace, skip runs */
                const char* p = s;
                while (*p) {
                    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                    if (!*p) break;
                    const char* start = p;
                    while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
                    char* word = (char*)malloc((size_t)(p - start) + 1);
                    memcpy(word, start, (size_t)(p - start)); word[p - start] = '\0';
                    Value* ws = val_str_own(word);
                    val_list_append(lst, ws); val_release(ws);
                }
            } else {
                size_t seplen = strlen(sep);
                const char* cur = s;
                const char* p2;
                while ((p2 = strstr(cur, sep))) {
                    char* piece = (char*)malloc((size_t)(p2 - cur) + 1);
                    memcpy(piece, cur, (size_t)(p2 - cur)); piece[p2 - cur] = '\0';
                    Value* ws2 = val_str_own(piece);
                    val_list_append(lst, ws2); val_release(ws2);
                    cur = p2 + seplen;
                }
                Value* tail = val_str(cur);
                val_list_append(lst, tail); val_release(tail);
            }
            if (sep) free(sep);
            result = lst;
        } else if (strcmp(method, "join") == 0) {
            if (argc < 1 || args[0]->type != VAL_LIST) { result = val_str(""); }
            else {
                SpectList* lst2 = args[0]->as.list;
                size_t total = 0;
                for (int i = 0; i < lst2->length; i++) {
                    char* ps = val_to_string(lst2->items[i]); total += strlen(ps) + slen; free(ps);
                }
                char* r = (char*)malloc(total + 1); r[0] = '\0';
                for (int i = 0; i < lst2->length; i++) {
                    if (i > 0) strcat(r, s);
                    char* ps = val_to_string(lst2->items[i]); strcat(r, ps); free(ps);
                }
                result = val_str_own(r);
            }
        } else {
            interp_error(interp, node->line, "str has no method '%s'", method);
        }

    /* ---- SpectArray methods ---- */
    } else if (obj->type == VAL_ARRAY) {
        SpectArray* arr = obj->as.array;

        if (strcmp(method, "length") == 0 || strcmp(method, "shape") == 0) {
            result = val_int((int64_t)arr->length);
        } else if (strcmp(method, "get") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "get() needs 1 arg"); goto mc_done; }
            result = val_spect(sarray_get(arr, (size_t)val_to_int(args[0])));
        } else if (strcmp(method, "set") == 0) {
            if (argc < 2) { interp_error(interp, node->line, "set() needs 2 args"); goto mc_done; }
            if (args[1]->type != VAL_SPECT) { interp_error(interp, node->line, "set() value must be Specton"); goto mc_done; }
            sarray_set(arr, (size_t)val_to_int(args[0]), args[1]->as.spect);
            result = val_null();
        } else if (strcmp(method, "dot") == 0) {
            if (argc < 1 || args[0]->type != VAL_ARRAY) {
                interp_error(interp, node->line, "dot() needs a SpectArray arg");
                goto mc_done;
            }
            result = val_spect(sarray_dot(arr, args[0]->as.array));
        } else if (strcmp(method, "print") == 0) {
            sarray_print(arr);
            result = val_null();
        } else {
            interp_error(interp, node->line,
                "SpectArray has no method '%s'", method);
        }

    /* ---- SpectMatrix methods ---- */
    } else if (obj->type == VAL_MATRIX) {
        SpectMatrix* mat = obj->as.matrix;

        if (strcmp(method, "rows") == 0) {
            result = val_int((int64_t)mat->rows);
        } else if (strcmp(method, "cols") == 0) {
            result = val_int((int64_t)mat->cols);
        } else if (strcmp(method, "shape") == 0) {
            /* Return a two-element SpectArray with [rows, cols] */
            SpectArray* sh = sarray_alloc(2);
            sarray_set(sh, 0, spect_fixed((uint8_t)mat->rows));
            sarray_set(sh, 1, spect_fixed((uint8_t)mat->cols));
            result = val_array(sh);
        } else if (strcmp(method, "get") == 0) {
            if (argc < 2) { interp_error(interp, node->line, "get() needs 2 args"); goto mc_done; }
            result = val_spect(smat_get(mat,
                (size_t)val_to_int(args[0]),
                (size_t)val_to_int(args[1])));
        } else if (strcmp(method, "set") == 0) {
            if (argc < 3) { interp_error(interp, node->line, "matrix.set() needs 3 args: row, col, specton"); goto mc_done; }
            size_t r = (size_t)val_to_int(args[0]);
            size_t c = (size_t)val_to_int(args[1]);
            Specton sv = (args[2]->type == VAL_SPECT) ? args[2]->as.spect : spect_fixed((uint8_t)val_to_int(args[2]));
            smat_set(mat, r, c, sv);
            result = val_null();
        } else if (strcmp(method, "transpose") == 0) {
            result = val_matrix(smat_transpose(mat));
        } else if (strcmp(method, "matmul") == 0) {
            if (argc < 1 || args[0]->type != VAL_MATRIX) {
                interp_error(interp, node->line, "matmul() needs a SpectMatrix arg");
                goto mc_done;
            }
            SpectMatrix* m = smat_matmul(mat, args[0]->as.matrix);
            if (!m) { interp_error(interp, node->line, "matmul: dimension mismatch"); goto mc_done; }
            result = val_matrix(m);
        } else if (strcmp(method, "print") == 0) {
            smat_print(mat);
            result = val_null();
        } else {
            interp_error(interp, node->line,
                "SpectMatrix has no method '%s'", method);
        }

    /* ---- VAL_LIST methods ---- */
    } else if (obj->type == VAL_LIST) {
        if (strcmp(method, "append") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "append() needs 1 arg"); goto mc_done; }
            val_list_append(obj, args[0]); result = val_null();
        } else if (strcmp(method, "pop") == 0) {
            result = val_list_pop(obj);
            if (!result) result = val_null();
        } else if (strcmp(method, "insert") == 0) {
            if (argc < 2) { interp_error(interp, node->line, "insert() needs 2 args"); goto mc_done; }
            val_list_insert(obj, (int)val_to_int(args[0]), args[1]); result = val_null();
        } else if (strcmp(method, "remove") == 0) {
            if (argc < 1) { interp_error(interp, node->line, "remove() needs 1 arg"); goto mc_done; }
            SpectList* lst = obj->as.list;
            int found = -1;
            for (int i = 0; i < lst->length; i++) { if (val_equal(lst->items[i], args[0])) { found = i; break; } }
            if (found >= 0) val_list_pop_at(obj, found);
            result = val_null();
        } else if (strcmp(method, "contains") == 0) {
            if (argc < 1) { result = val_bool(0); }
            else { result = val_bool(val_list_contains(obj, args[0])); }
        } else if (strcmp(method, "reverse") == 0) {
            val_list_reverse(obj); result = val_null();
        } else if (strcmp(method, "extend") == 0) {
            if (argc < 1 || args[0]->type != VAL_LIST) { interp_error(interp, node->line, "extend() needs a list arg"); goto mc_done; }
            val_list_extend(obj, args[0]); result = val_null();
        } else if (strcmp(method, "get") == 0) {
            if (argc < 1) { result = val_null(); }
            else { Value* v = val_list_get(obj, (int)val_to_int(args[0])); result = v ? val_retain(v) : val_null(); }
        } else if (strcmp(method, "set") == 0) {
            if (argc < 2) { interp_error(interp, node->line, "set() needs 2 args"); goto mc_done; }
            val_list_set(obj, (int)val_to_int(args[0]), args[1]); result = val_null();
        } else if (strcmp(method, "len") == 0) {
            result = val_int((int64_t)obj->as.list->length);
        } else {
            interp_error(interp, node->line, "list has no method '%s'", method);
        }

    /* ---- VAL_MAP methods ---- */
    } else if (obj->type == VAL_MAP) {
        if (strcmp(method, "get") == 0) {
            if (argc < 1) { result = val_null(); }
            else { char* k = val_to_string(args[0]); Value* v = val_map_get(obj, k); result = v ? val_retain(v) : val_null(); free(k); }
        } else if (strcmp(method, "set") == 0) {
            if (argc < 2) { interp_error(interp, node->line, "set() needs 2 args"); goto mc_done; }
            char* k = val_to_string(args[0]); val_map_set(obj, k, args[1]); free(k); result = val_null();
        } else if (strcmp(method, "has") == 0) {
            if (argc < 1) { result = val_bool(0); }
            else { char* k = val_to_string(args[0]); result = val_bool(val_map_has(obj, k)); free(k); }
        } else if (strcmp(method, "delete") == 0) {
            if (argc < 1) { result = val_null(); }
            else { char* k = val_to_string(args[0]); val_map_delete(obj, k); free(k); result = val_null(); }
        } else if (strcmp(method, "keys") == 0) {
            result = val_map_keys(obj);
        } else if (strcmp(method, "values") == 0) {
            result = val_map_values(obj);
        } else if (strcmp(method, "len") == 0) {
            result = val_int((int64_t)val_map_len(obj));
        } else {
            interp_error(interp, node->line, "map has no method '%s'", method);
        }

    /* ---- struct instance methods ---- */
    } else if (obj->type == VAL_STRUCT_INSTANCE) {
        Value* mfn = env_get(obj->as.instance.methods, method);
        if (!mfn) {
            interp_error(interp, node->line,
                "struct '%s' has no method '%s'",
                obj->as.instance.type_name, method);
            goto mc_done;
        }
        if (mfn->type != VAL_FUNC) {
            interp_error(interp, node->line,
                "'%s' is not a function in struct '%s'",
                method, obj->as.instance.type_name);
            goto mc_done;
        }

        /* Build args array with self prepended */
        int total = argc + 1;
        Value** full_args = (Value**)calloc(total, sizeof(Value*));
        full_args[0] = obj; /* self — not retained here; call_func only reads it */
        for (int i = 0; i < argc; i++) full_args[i+1] = args[i];

        result = call_func(interp, mfn, full_args, total, node->line);
        free(full_args);

    } else {
        interp_error(interp, node->line,
            "value of type %s has no methods", val_type_name(obj->type));
    }

mc_done:
    for (int i = 0; i < argc; i++) val_release(args[i]);
    free(args);
    val_release(obj);
    return result;
}

/* =========================================================================
 * eval_index
 * ====================================================================== */

static Value* eval_index(Interpreter* interp, ASTNode* node) {
    Value* obj = interp_eval(interp, node->as.index_expr.obj);
    if (interp->signal == SIG_ERROR) return NULL;
    Value* idx = interp_eval(interp, node->as.index_expr.index);
    if (interp->signal == SIG_ERROR) { val_release(obj); return NULL; }

    Value* result = NULL;

    if (obj->type == VAL_ARRAY) {
        if (idx->type != VAL_INT) {
            interp_error(interp, node->line, "array index must be INT, got %s",
                val_type_name(idx->type));
            goto idx_done;
        }
        int64_t i = idx->as.integer;
        SpectArray* arr = obj->as.array;
        if (i < 0 || (size_t)i >= arr->length) {
            interp_error(interp, node->line,
                "array index %lld out of bounds (length %zu)",
                (long long)i, arr->length);
            goto idx_done;
        }
        result = val_spect(sarray_get(arr, (size_t)i));

    } else if (obj->type == VAL_MATRIX) {
        /* idx should be a 2-element SpectArray from [r,c] literal */
        if (idx->type != VAL_ARRAY || idx->as.array->length < 2) {
            interp_error(interp, node->line,
                "matrix index must be [row, col]");
            goto idx_done;
        }
        size_t r = (size_t)spect_collapse(sarray_get(idx->as.array, 0));
        size_t c = (size_t)spect_collapse(sarray_get(idx->as.array, 1));
        result = val_spect(smat_get(obj->as.matrix, r, c));

    } else if (obj->type == VAL_STRUCT_INSTANCE) {
        /* Allow integer field indexing */
        if (idx->type != VAL_INT) {
            interp_error(interp, node->line,
                "struct instance index must be INT");
            goto idx_done;
        }
        int64_t i = idx->as.integer;
        if (i < 0 || i >= obj->as.instance.field_count) {
            interp_error(interp, node->line,
                "struct field index %lld out of range", (long long)i);
            goto idx_done;
        }
        result = val_retain(obj->as.instance.fields[i]);

    } else if (obj->type == VAL_LIST) {
        int lidx = (int)val_to_int(idx);
        Value* v = val_list_get(obj, lidx);
        val_release(idx); val_release(obj);
        return v ? val_retain(v) : val_null();
    } else if (obj->type == VAL_STR) {
        const char* sv = obj->as.string ? obj->as.string : "";
        int sidx = (int)val_to_int(idx);
        if (sidx < 0) sidx = (int)strlen(sv) + sidx;
        val_release(idx); val_release(obj);
        if (sidx < 0 || sidx >= (int)strlen(sv)) return val_str("");
        char ch[2] = { sv[sidx], '\0' };
        return val_str(ch);
    } else if (obj->type == VAL_MAP) {
        char* k = val_to_string(idx);
        Value* v = val_map_get(obj, k);
        free(k); val_release(idx); val_release(obj);
        return v ? val_retain(v) : val_null();
    } else {
        interp_error(interp, node->line,
            "cannot index value of type %s", val_type_name(obj->type));
    }

idx_done:
    val_release(idx);
    val_release(obj);
    return result;
}

/* =========================================================================
 * eval_array_lit
 * ====================================================================== */

static Value* eval_array_lit(Interpreter* interp, ASTNode* node) {
    NodeList* elems = &node->as.array_lit.elements;
    int n = elems->count;

    /* Check if all elements are Spectons — if so build SpectArray */
    Value** vals = (Value**)calloc(n, sizeof(Value*));
    int all_spect = 1;

    for (int i = 0; i < n; i++) {
        vals[i] = interp_eval(interp, elems->items[i]);
        if (interp->signal == SIG_ERROR) {
            for (int j = 0; j < i; j++) val_release(vals[j]);
            free(vals);
            return NULL;
        }
        if (vals[i]->type != VAL_SPECT) all_spect = 0;
    }

    /* Check if any element is non-Specton (string, list, etc.) — use VAL_LIST */
    int has_nonnumeric = 0;
    for (int i = 0; i < n; i++) {
        if (vals[i]->type != VAL_SPECT && vals[i]->type != VAL_INT &&
            vals[i]->type != VAL_FLOAT) {
            has_nonnumeric = 1; break;
        }
    }

    Value* result;
    if (has_nonnumeric) {
        Value* lst = val_list_new();
        for (int i = 0; i < n; i++) {
            val_list_append(lst, vals[i]);
            val_release(vals[i]);
        }
        free(vals);
        return lst;
    } else if (all_spect || n == 0) {
        SpectArray* arr = sarray_alloc((size_t)n);
        for (int i = 0; i < n; i++) {
            sarray_set(arr, (size_t)i, vals[i]->as.spect);
            val_release(vals[i]);
        }
        result = val_array(arr);
    } else {
        /* Mixed numeric array — use a SpectArray, coercing non-Spectons to fixed Spectons */
        SpectArray* arr = sarray_alloc((size_t)n);
        for (int i = 0; i < n; i++) {
            Specton s;
            if (vals[i]->type == VAL_SPECT) {
                s = vals[i]->as.spect;
            } else if (vals[i]->type == VAL_INT) {
                uint8_t v = (uint8_t)(vals[i]->as.integer % 10);
                s = spect_fixed(v);
            } else if (vals[i]->type == VAL_FLOAT) {
                uint8_t v = (uint8_t)((int64_t)vals[i]->as.number % 10);
                s = spect_fixed(v);
            } else {
                s = spect_fixed(0);
            }
            sarray_set(arr, (size_t)i, s);
            val_release(vals[i]);
        }
        result = val_array(arr);
    }

    free(vals);
    return result;
}

/* =========================================================================
 * eval_alloc
 * ====================================================================== */

static Value* eval_alloc(Interpreter* interp, ASTNode* node) {
    TypeNode* ty = &node->as.alloc.type_annot;

    if (strcmp(ty->name, "SpectArray") == 0) {
        size_t n = (ty->ndims >= 1 && ty->dims[0] > 0) ? (size_t)ty->dims[0] : 0;
        return val_array(sarray_zeros(n));
    }

    if (strcmp(ty->name, "SpectMatrix") == 0) {
        size_t rows = (ty->ndims >= 1) ? (size_t)ty->dims[0] : 0;
        size_t cols = (ty->ndims >= 2) ? (size_t)ty->dims[1] : 0;
        return val_matrix(smat_zeros(rows, cols));
    }

    /* For unknown alloc types, return null and warn */
    fprintf(stderr, "warning: alloc of unknown type '%s' at line %d\n",
            ty->name, node->line);
    return val_null();
}

/* =========================================================================
 * interp_eval — expression dispatch
 * ====================================================================== */

Value* interp_eval(Interpreter* interp, ASTNode* expr) {
    if (!expr) return val_null();
    if (interp->signal != SIG_NONE) return NULL;

    switch (expr->kind) {

    case ND_INT_LIT:
        return val_int(expr->as.int_lit.value);

    case ND_FLOAT_LIT:
        return val_float(expr->as.float_lit.value);

    case ND_STR_LIT:
        return val_str(expr->as.str_lit.value);

    case ND_BOOL_LIT:
        return val_bool(expr->as.bool_lit.value);

    case ND_NULL_LIT:
        return val_null();

    case ND_WAVE_LIT:
        return val_spect(spect_wave(expr->as.wave_lit.weights));

    case ND_RANGE_LIT:
        return val_spect(spect_range(
            (uint8_t)expr->as.range_lit.lo,
            (uint8_t)expr->as.range_lit.hi));

    case ND_ARRAY_LIT:
        return eval_array_lit(interp, expr);

    case ND_DICT_LIT: {
        Value* map = val_map_new();
        for (int i = 0; i < expr->as.dict_lit.keys.count; i++) {
            Value* key = interp_eval(interp, expr->as.dict_lit.keys.items[i]);
            if (interp->signal == SIG_ERROR) { val_release(map); return NULL; }
            Value* val = interp_eval(interp, expr->as.dict_lit.values.items[i]);
            if (interp->signal == SIG_ERROR) { val_release(key); val_release(map); return NULL; }
            char* key_str = val_to_string(key);
            val_map_set(map, key_str, val);
            free(key_str);
            val_release(key);
            val_release(val);
        }
        return map;
    }

    case ND_FSTR_LIT: {
        /* f-string: parts[] are literal string segments, exprs[] are interpolated
         * expressions. The layout is: parts[0] expr[0] parts[1] expr[1] ... parts[n] */
        int part_count = expr->as.fstr_lit.part_count;
        int expr_count = expr->as.fstr_lit.expr_count;

        /* Estimate initial buffer */
        size_t out_cap = 256;
        size_t out_len = 0;
        char*  out     = (char*)malloc(out_cap);
        out[0] = '\0';

#define FSTR_APPEND(src, src_len) do {                                      \
    size_t _n = (src_len);                                                  \
    while (out_len + _n + 1 >= out_cap) { out_cap *= 2;                     \
        out = (char*)realloc(out, out_cap); }                               \
    memcpy(out + out_len, (src), _n);                                       \
    out_len += _n; out[out_len] = '\0';                                     \
} while(0)

        for (int i = 0; i < part_count || i < expr_count; i++) {
            /* Append literal part i (if it exists) */
            if (i < part_count && expr->as.fstr_lit.parts && expr->as.fstr_lit.parts[i]) {
                const char* seg = expr->as.fstr_lit.parts[i];
                FSTR_APPEND(seg, strlen(seg));
            }
            /* Append evaluated expression i (if it exists) */
            if (i < expr_count && expr->as.fstr_lit.exprs && expr->as.fstr_lit.exprs[i]) {
                Value* ev = interp_eval(interp, expr->as.fstr_lit.exprs[i]);
                if (interp->signal == SIG_ERROR) { free(out); return NULL; }
                char* es = val_to_string(ev);
                FSTR_APPEND(es, strlen(es));
                free(es);
                val_release(ev);
            }
        }
#undef FSTR_APPEND

        Value* result = (Value*)calloc(1, sizeof(Value));
        result->ref_count = 1;
        result->type      = VAL_STR;
        result->as.string = out;
        return result;
    }

    case ND_IDENT: {
        const char* name = expr->as.ident.name;
        Value* v = env_get(interp->current_env, name);
        if (!v) {
            interp_error(interp, expr->line,
                "undefined variable '%s'", name);
            return NULL;
        }
        return val_retain(v);
    }

    case ND_BINARY:
        return eval_binary(interp, expr);

    case ND_UNARY:
        return eval_unary(interp, expr);

    case ND_CALL:
        return eval_call(interp, expr);

    case ND_METHOD_CALL:
        return eval_method_call(interp, expr);

    case ND_INDEX:
        return eval_index(interp, expr);

    case ND_ATTRIBUTE: {
        Value* obj = interp_eval(interp, expr->as.attribute.obj);
        if (interp->signal == SIG_ERROR) return NULL;
        const char* attr = expr->as.attribute.attr;

        if (obj->type == VAL_STRUCT_INSTANCE) {
            /* Search fields */
            for (int i = 0; i < obj->as.instance.field_count; i++) {
                if (strcmp(obj->as.instance.field_names[i], attr) == 0) {
                    Value* fv = val_retain(obj->as.instance.fields[i]);
                    val_release(obj);
                    return fv;
                }
            }
            /* Search methods env */
            Value* mv = env_get(obj->as.instance.methods, attr);
            if (mv) {
                Value* retained = val_retain(mv);
                val_release(obj);
                return retained;
            }
            interp_error(interp, expr->line,
                "struct '%s' has no field or method '%s'",
                obj->as.instance.type_name, attr);
            val_release(obj);
            return NULL;
        }

        /* Allow length / rows / cols shorthand on array/matrix */
        if (obj->type == VAL_ARRAY) {
            if (strcmp(attr, "length") == 0) {
                Value* r = val_int((int64_t)obj->as.array->length);
                val_release(obj);
                return r;
            }
        }
        if (obj->type == VAL_MATRIX) {
            if (strcmp(attr, "rows") == 0) {
                Value* r = val_int((int64_t)obj->as.matrix->rows);
                val_release(obj);
                return r;
            }
            if (strcmp(attr, "cols") == 0) {
                Value* r = val_int((int64_t)obj->as.matrix->cols);
                val_release(obj);
                return r;
            }
        }

        interp_error(interp, expr->line,
            "attribute '%s' not found on %s value",
            attr, val_type_name(obj->type));
        val_release(obj);
        return NULL;
    }

    case ND_ALLOC:
        return eval_alloc(interp, expr);

    case ND_RANGE_EXPR:
        /* ND_RANGE_EXPR is handled by exec_for; if reached here return null. */
        return val_null();

    default:
        interp_error(interp, expr->line,
            "interp_eval: unhandled node kind %d", (int)expr->kind);
        return NULL;
    }
}
