/* vm.c — Bytecode virtual machine for the SPECTRA language */

#include "vm.h"
#include "compiler_bc.h"
#include "builtins.h"
#include "../runtime/specton.h"
#include "../runtime/tensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* =========================================================================
 * Forward declarations
 * ====================================================================== */

static int  vm_execute(VM* vm, Chunk* chunk, Env* call_env, Value** ret_out);
static Value* vm_call_func(VM* vm, Value* fn, Value** args, int argc, int line);

/* =========================================================================
 * Error helper
 * ====================================================================== */

static void vm_error(VM* vm, int line, const char* fmt, ...) {
    if (vm->had_error) return;
    vm->had_error   = 1;
    vm->error_line  = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(vm->error_msg, sizeof(vm->error_msg), fmt, ap);
    va_end(ap);
}

/* =========================================================================
 * Stack helpers
 * ====================================================================== */

static void vm_push(VM* vm, Value* v) {
    if (vm->sp >= VM_STACK_MAX) {
        vm_error(vm, 0, "stack overflow");
        if (v) val_release(v);
        return;
    }
    vm->stack[vm->sp++] = v; /* takes ownership of the reference */
}

static Value* vm_pop(VM* vm) {
    if (vm->sp <= 0) {
        vm_error(vm, 0, "stack underflow");
        return val_null();
    }
    return vm->stack[--vm->sp]; /* caller owns the returned reference */
}

static Value* vm_peek(VM* vm, int offset) {
    int idx = vm->sp - 1 - offset;
    if (idx < 0) {
        vm_error(vm, 0, "stack peek underflow");
        return val_null();
    }
    return vm->stack[idx]; /* borrowed — do NOT release */
}

/* =========================================================================
 * Arithmetic helpers — operate on Value*, return a new Value*
 * ====================================================================== */

static Value* arith_add(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT   && b->type == VAL_INT)
        return val_int(a->as.integer + b->as.integer);
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number
                  : (a->type == VAL_INT)   ? (double)a->as.integer : 0.0;
        double bv = (b->type == VAL_FLOAT) ? b->as.number
                  : (b->type == VAL_INT)   ? (double)b->as.integer : 0.0;
        return val_float(av + bv);
    }
    if (a->type == VAL_SPECT && b->type == VAL_SPECT)
        return val_spect(spect_add(a->as.spect, b->as.spect));
    if (a->type == VAL_STR   && b->type == VAL_STR) {
        size_t la = strlen(a->as.string), lb = strlen(b->as.string);
        char* buf = (char*)malloc(la + lb + 1);
        if (!buf) { vm_error(vm, line, "out of memory"); return val_null(); }
        memcpy(buf, a->as.string, la);
        memcpy(buf + la, b->as.string, lb);
        buf[la + lb] = '\0';
        return val_str_own(buf);
    }
    vm_error(vm, line, "unsupported operand types for '+': %s + %s",
             val_type_name(a->type), val_type_name(b->type));
    return val_null();
}

static Value* arith_sub(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT   && b->type == VAL_INT)
        return val_int(a->as.integer - b->as.integer);
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number : (double)a->as.integer;
        double bv = (b->type == VAL_FLOAT) ? b->as.number : (double)b->as.integer;
        return val_float(av - bv);
    }
    if (a->type == VAL_SPECT && b->type == VAL_SPECT)
        return val_spect(spect_sub(a->as.spect, b->as.spect));
    vm_error(vm, line, "unsupported operand types for '-': %s - %s",
             val_type_name(a->type), val_type_name(b->type));
    return val_null();
}

static Value* arith_mul(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT   && b->type == VAL_INT)
        return val_int(a->as.integer * b->as.integer);
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number : (double)a->as.integer;
        double bv = (b->type == VAL_FLOAT) ? b->as.number : (double)b->as.integer;
        return val_float(av * bv);
    }
    if (a->type == VAL_SPECT && b->type == VAL_SPECT)
        return val_spect(spect_mul(a->as.spect, b->as.spect));
    vm_error(vm, line, "unsupported operand types for '*': %s * %s",
             val_type_name(a->type), val_type_name(b->type));
    return val_null();
}

static Value* arith_div(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT   && b->type == VAL_INT) {
        if (b->as.integer == 0) { vm_error(vm, line, "division by zero"); return val_null(); }
        return val_int(a->as.integer / b->as.integer);
    }
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number : (double)a->as.integer;
        double bv = (b->type == VAL_FLOAT) ? b->as.number : (double)b->as.integer;
        if (bv == 0.0) { vm_error(vm, line, "division by zero"); return val_null(); }
        return val_float(av / bv);
    }
    if (a->type == VAL_SPECT && b->type == VAL_SPECT)
        return val_spect(spect_div(a->as.spect, b->as.spect));
    vm_error(vm, line, "unsupported operand types for '/': %s / %s",
             val_type_name(a->type), val_type_name(b->type));
    return val_null();
}

static Value* arith_mod(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT && b->type == VAL_INT) {
        if (b->as.integer == 0) { vm_error(vm, line, "modulo by zero"); return val_null(); }
        return val_int(a->as.integer % b->as.integer);
    }
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number : (double)a->as.integer;
        double bv = (b->type == VAL_FLOAT) ? b->as.number : (double)b->as.integer;
        return val_float(fmod(av, bv));
    }
    vm_error(vm, line, "unsupported operand types for '%%': %s %% %s",
             val_type_name(a->type), val_type_name(b->type));
    return val_null();
}

static Value* arith_pow(VM* vm, Value* a, Value* b, int line) {
    double av, bv;
    if (a->type == VAL_INT)   av = (double)a->as.integer;
    else if (a->type == VAL_FLOAT) av = a->as.number;
    else { vm_error(vm, line, "unsupported type for '**': %s", val_type_name(a->type)); return val_null(); }
    if (b->type == VAL_INT)   bv = (double)b->as.integer;
    else if (b->type == VAL_FLOAT) bv = b->as.number;
    else { vm_error(vm, line, "unsupported type for '**': %s", val_type_name(b->type)); return val_null(); }
    return val_float(pow(av, bv));
}

/* =========================================================================
 * Comparison helpers
 * ====================================================================== */

static int val_compare_lt(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT   && b->type == VAL_INT)
        return a->as.integer < b->as.integer;
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number : (double)a->as.integer;
        double bv = (b->type == VAL_FLOAT) ? b->as.number : (double)b->as.integer;
        return av < bv;
    }
    if (a->type == VAL_SPECT && b->type == VAL_SPECT)
        return spect_lt(a->as.spect, b->as.spect);
    vm_error(vm, line, "unsupported operand types for '<': %s < %s",
             val_type_name(a->type), val_type_name(b->type));
    return 0;
}

static int val_compare_gt(VM* vm, Value* a, Value* b, int line) {
    if (a->type == VAL_INT   && b->type == VAL_INT)
        return a->as.integer > b->as.integer;
    if (a->type == VAL_FLOAT || b->type == VAL_FLOAT) {
        double av = (a->type == VAL_FLOAT) ? a->as.number : (double)a->as.integer;
        double bv = (b->type == VAL_FLOAT) ? b->as.number : (double)b->as.integer;
        return av > bv;
    }
    if (a->type == VAL_SPECT && b->type == VAL_SPECT)
        return spect_gt(a->as.spect, b->as.spect);
    vm_error(vm, line, "unsupported operand types for '>': %s > %s",
             val_type_name(a->type), val_type_name(b->type));
    return 0;
}

/* =========================================================================
 * OP_INDEX_GET — index into array / string / SpectArray
 * ====================================================================== */

static Value* vm_index_get(VM* vm, Value* obj, Value* idx, int line) {
    if (obj->type == VAL_ARRAY) {
        if (idx->type != VAL_INT) {
            vm_error(vm, line, "array index must be integer, got %s",
                     val_type_name(idx->type));
            return val_null();
        }
        SpectArray* arr = obj->as.array;
        int64_t i = idx->as.integer;
        if (i < 0) i += (int64_t)arr->length;
        if (i < 0 || (size_t)i >= arr->length) {
            vm_error(vm, line, "array index %lld out of range [0, %zu)",
                     (long long)i, arr->length);
            return val_null();
        }
        return val_spect(sarray_get(arr, (size_t)i));
    }

    if (obj->type == VAL_STR) {
        if (idx->type != VAL_INT) {
            vm_error(vm, line, "string index must be integer");
            return val_null();
        }
        int64_t i = idx->as.integer;
        size_t  len = strlen(obj->as.string);
        if (i < 0) i += (int64_t)len;
        if (i < 0 || (size_t)i >= len) {
            vm_error(vm, line, "string index %lld out of range", (long long)i);
            return val_null();
        }
        char buf[2] = { obj->as.string[i], '\0' };
        return val_str(buf);
    }

    if (obj->type == VAL_STRUCT_INSTANCE) {
        /* Allow integer index into fields */
        if (idx->type == VAL_INT) {
            int64_t i = idx->as.integer;
            if (i < 0 || i >= obj->as.instance.field_count) {
                vm_error(vm, line, "struct field index %lld out of range", (long long)i);
                return val_null();
            }
            return val_retain(obj->as.instance.fields[i]);
        }
        if (idx->type == VAL_STR) {
            for (int i = 0; i < obj->as.instance.field_count; i++) {
                if (strcmp(obj->as.instance.field_names[i], idx->as.string) == 0)
                    return val_retain(obj->as.instance.fields[i]);
            }
            vm_error(vm, line, "struct has no field '%s'", idx->as.string);
            return val_null();
        }
    }

    vm_error(vm, line, "cannot index type %s", val_type_name(obj->type));
    return val_null();
}

/* =========================================================================
 * OP_INDEX_SET — set element in array
 * ====================================================================== */

static void vm_index_set(VM* vm, Value* obj, Value* idx, Value* newval, int line) {
    if (obj->type == VAL_ARRAY) {
        if (idx->type != VAL_INT) {
            vm_error(vm, line, "array index must be integer");
            return;
        }
        SpectArray* arr = obj->as.array;
        int64_t i = idx->as.integer;
        if (i < 0) i += (int64_t)arr->length;
        if (i < 0 || (size_t)i >= arr->length) {
            vm_error(vm, line, "array index %lld out of range", (long long)i);
            return;
        }
        Specton s;
        if (newval->type == VAL_SPECT) s = newval->as.spect;
        else if (newval->type == VAL_INT) s = spect_fixed((uint8_t)(newval->as.integer & 9));
        else s = spect_fixed(0);
        sarray_set(arr, (size_t)i, s);
        return;
    }
    if (obj->type == VAL_STRUCT_INSTANCE) {
        if (idx->type == VAL_STR) {
            for (int i = 0; i < obj->as.instance.field_count; i++) {
                if (strcmp(obj->as.instance.field_names[i], idx->as.string) == 0) {
                    val_release(obj->as.instance.fields[i]);
                    obj->as.instance.fields[i] = val_retain(newval);
                    return;
                }
            }
            vm_error(vm, line, "struct has no field '%s'", idx->as.string);
            return;
        }
    }
    vm_error(vm, line, "cannot index-set type %s", val_type_name(obj->type));
}

/* =========================================================================
 * OP_GET_ATTR / OP_SET_ATTR
 * ====================================================================== */

static Value* vm_get_attr(VM* vm, Value* obj, const char* attr, int line) {
    if (obj->type == VAL_STRUCT_INSTANCE) {
        /* Check fields */
        for (int i = 0; i < obj->as.instance.field_count; i++) {
            if (strcmp(obj->as.instance.field_names[i], attr) == 0)
                return val_retain(obj->as.instance.fields[i]);
        }
        /* Check methods */
        Value* meth = env_get(obj->as.instance.methods, attr);
        if (meth) return val_retain(meth);
        vm_error(vm, line, "struct '%s' has no attribute '%s'",
                 obj->as.instance.type_name, attr);
        return val_null();
    }
    if (obj->type == VAL_SPECT) {
        /* Expose a few Specton attributes */
        if (strcmp(attr, "mode") == 0)  return val_int((int64_t)obj->as.spect.mode);
        if (strcmp(attr, "peak") == 0)  return val_int((int64_t)spect_peak(obj->as.spect));
        if (strcmp(attr, "entropy") == 0) return val_float((double)spect_entropy(obj->as.spect));
        vm_error(vm, line, "Specton has no attribute '%s'", attr);
        return val_null();
    }
    vm_error(vm, line, "type %s has no attributes", val_type_name(obj->type));
    return val_null();
}

static void vm_set_attr(VM* vm, Value* obj, const char* attr, Value* newval, int line) {
    if (obj->type == VAL_STRUCT_INSTANCE) {
        for (int i = 0; i < obj->as.instance.field_count; i++) {
            if (strcmp(obj->as.instance.field_names[i], attr) == 0) {
                val_release(obj->as.instance.fields[i]);
                obj->as.instance.fields[i] = val_retain(newval);
                return;
            }
        }
        vm_error(vm, line, "struct '%s' has no field '%s'",
                 obj->as.instance.type_name, attr);
        return;
    }
    vm_error(vm, line, "type %s is not assignable", val_type_name(obj->type));
}

/* =========================================================================
 * OP_CALL_METHOD — dispatch method call on a value
 * ====================================================================== */

static Value* vm_call_method(VM* vm, Value* obj, const char* method,
                             Value** args, int argc, int line) {
    /* --- Specton methods --- */
    if (obj->type == VAL_SPECT) {
        Specton s = obj->as.spect;
        if (strcmp(method, "collapse") == 0)
            return val_int((int64_t)spect_collapse(s));
        if (strcmp(method, "peak") == 0)
            return val_int((int64_t)spect_peak(s));
        if (strcmp(method, "entropy") == 0)
            return val_float((double)spect_entropy(s));
        if (strcmp(method, "to_wave") == 0)
            return val_spect(spect_to_wave(s));
        if (strcmp(method, "invert") == 0)
            return val_spect(spect_invert(s));
        if (strcmp(method, "print_wave") == 0) {
            spect_print_wave(s);
            return val_null();
        }
        if (strcmp(method, "println") == 0) {
            spect_println(s);
            return val_null();
        }
        if (strcmp(method, "resonate") == 0 && argc >= 1 && args[0]->type == VAL_SPECT)
            return val_spect(spect_resonate(s, args[0]->as.spect));
        if (strcmp(method, "spread") == 0 && argc >= 1 && args[0]->type == VAL_SPECT)
            return val_spect(spect_spread(s, args[0]->as.spect));
        if (strcmp(method, "interfere") == 0 && argc >= 1 && args[0]->type == VAL_SPECT)
            return val_spect(spect_interfere(s, args[0]->as.spect));
        vm_error(vm, line, "Specton has no method '%s'", method);
        return val_null();
    }

    /* --- Array methods --- */
    if (obj->type == VAL_ARRAY) {
        SpectArray* arr = obj->as.array;
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0)
            return val_int((int64_t)arr->length);
        if (strcmp(method, "print") == 0) {
            sarray_print(arr);
            return val_null();
        }
        if (strcmp(method, "entropy") == 0)
            return val_float((double)sarray_mean_entropy(arr));
        vm_error(vm, line, "Array has no method '%s'", method);
        return val_null();
    }

    /* --- String methods --- */
    if (obj->type == VAL_STR) {
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0)
            return val_int((int64_t)strlen(obj->as.string));
        if (strcmp(method, "upper") == 0) {
            char* s = strdup(obj->as.string);
            if (!s) return val_null();
            for (int i = 0; s[i]; i++)
                if (s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] - 32);
            return val_str_own(s);
        }
        if (strcmp(method, "lower") == 0) {
            char* s = strdup(obj->as.string);
            if (!s) return val_null();
            for (int i = 0; s[i]; i++)
                if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] + 32);
            return val_str_own(s);
        }
        vm_error(vm, line, "String has no method '%s'", method);
        return val_null();
    }

    /* --- Struct instance methods --- */
    if (obj->type == VAL_STRUCT_INSTANCE) {
        Value* meth = env_get(obj->as.instance.methods, method);
        if (!meth) {
            vm_error(vm, line, "struct '%s' has no method '%s'",
                     obj->as.instance.type_name, method);
            return val_null();
        }
        /* Build argument list with 'self' prepended */
        int total = argc + 1;
        Value** full_args = (Value**)malloc((size_t)total * sizeof(Value*));
        if (!full_args) { vm_error(vm, line, "out of memory"); return val_null(); }
        full_args[0] = obj;
        for (int i = 0; i < argc; i++) full_args[i + 1] = args[i];
        Value* result = vm_call_func(vm, meth, full_args, total, line);
        free(full_args);
        return result ? result : val_null();
    }

    vm_error(vm, line, "type %s has no method '%s'",
             val_type_name(obj->type), method);
    return val_null();
}

/* =========================================================================
 * vm_call_func — call a VAL_FUNC or VAL_NATIVE with given args
 *
 * Returns a new reference (ref_count >= 1).  Caller must val_release().
 * ====================================================================== */

static Value* vm_call_func(VM* vm, Value* fn, Value** args, int argc, int line) {
    if (!fn) { vm_error(vm, line, "attempt to call NULL"); return val_null(); }

    if (fn->type == VAL_NATIVE) {
        /* Native functions expect an Interpreter*.  We pass NULL since the
         * VM does not have one.  Most builtins only use it for error reporting;
         * they receive the args directly.
         * We create a minimal temporary Interpreter wrapper so builtins that
         * call interp_error don't crash. */

        /* Many builtins cast the first arg through ARG_* macros and don't
         * dereference interp at all — passing NULL is fine for those.
         * For safety we still pass NULL and accept that error-reporting inside
         * natives won't propagate back to the VM cleanly in this first
         * implementation. */
        Interpreter stub;
        memset(&stub, 0, sizeof(stub));
        stub.globals      = vm->globals;
        stub.current_env  = vm->current_env;
        stub.signal       = SIG_NONE;
        stub.sim_mode     = vm->sim_mode;

        Value* result = fn->as.native(&stub, args, argc);

        if (stub.signal == SIG_ERROR) {
            vm_error(vm, line, "%s", stub.error_msg);
        }
        return result ? result : val_null();
    }

    if (fn->type == VAL_FUNC) {
        /* Check if this is a struct constructor (body = ND_STRUCT_DEF) */
        ASTNode* body_node = fn->as.func.body;
        if (body_node && body_node->kind == ND_STRUCT_DEF) {
            /* Instantiate struct */
            ASTNode* sdef = body_node;
            int nfields = fn->as.func.param_count;
            char** fnames = (char**)calloc((size_t)(nfields > 0 ? nfields : 1), sizeof(char*));
            Value** fvals  = (Value**)calloc((size_t)(nfields > 0 ? nfields : 1), sizeof(Value*));
            if (!fnames || !fvals) {
                free(fnames); free(fvals);
                vm_error(vm, line, "out of memory creating struct instance");
                return val_null();
            }
            for (int i = 0; i < nfields; i++) {
                fnames[i] = strdup(fn->as.func.param_names[i]);
                if (i < argc) {
                    fvals[i] = val_retain(args[i]);
                } else {
                    /* check for default value in the field_def */
                    ASTNode* fdef_node = (i < sdef->as.struct_def.fields.count)
                                       ? sdef->as.struct_def.fields.items[i] : NULL;
                    if (fdef_node && fdef_node->as.field_def.default_val) {
                        /* Compile and execute the default field value expression.
                         * Wrap it in a synthetic ND_PROGRAM and use bc_compile. */
                        ASTNode* def_expr = fdef_node->as.field_def.default_val;
                        /* Build a synthetic expr-stmt wrapping the default value */
                        ASTNode expr_stmt_node;
                        memset(&expr_stmt_node, 0, sizeof(expr_stmt_node));
                        expr_stmt_node.kind = ND_EXPR_STMT;
                        expr_stmt_node.line = def_expr->line;
                        expr_stmt_node.as.expr_stmt.expr = def_expr;

                        /* Build a synthetic program with a single return statement */
                        ASTNode ret_node;
                        memset(&ret_node, 0, sizeof(ret_node));
                        ret_node.kind = ND_RETURN;
                        ret_node.line = def_expr->line;
                        ret_node.as.ret.value = def_expr;

                        ASTNode* prog_stmts[1] = { &ret_node };
                        ASTNode prog_node;
                        memset(&prog_node, 0, sizeof(prog_node));
                        prog_node.kind = ND_PROGRAM;
                        prog_node.as.program.stmts.items = prog_stmts;
                        prog_node.as.program.stmts.count = 1;
                        prog_node.as.program.stmts.cap   = 1;

                        Chunk tmp_chunk;
                        chunk_init(&tmp_chunk);
                        char tmp_err[256] = {0};
                        int ok2 = bc_compile(&prog_node, &tmp_chunk, tmp_err);
                        if (ok2) {
                            Env* def_env = env_new(vm->globals);
                            Value* def_ret = val_null();
                            vm_execute(vm, &tmp_chunk, def_env, &def_ret);
                            env_release(def_env);
                            chunk_free(&tmp_chunk);
                            if (vm->had_error) {
                                fvals[i] = val_null();
                                vm->had_error = 0; /* reset for field default error */
                            } else {
                                fvals[i] = def_ret;
                            }
                        } else {
                            chunk_free(&tmp_chunk);
                            fvals[i] = val_null();
                        }
                        (void)expr_stmt_node;
                    } else {
                        fvals[i] = val_null();
                    }
                }
            }
            /* Build method env from the struct_def's method list */
            Env* menv = env_new(NULL);
            NodeList* methods = &sdef->as.struct_def.methods;
            for (int i = 0; i < methods->count; i++) {
                ASTNode* mnode = methods->items[i];
                int npm = mnode->as.func_def.params.count;
                char** pnames = (char**)calloc((size_t)(npm > 0 ? npm : 1), sizeof(char*));
                for (int j = 0; j < npm; j++)
                    pnames[j] = strdup(mnode->as.func_def.params.items[j]->as.param.name);
                Value* mfn = val_func(mnode->as.func_def.name,
                                      pnames, npm, mnode, vm->current_env, 1);
                for (int j = 0; j < npm; j++) free(pnames[j]);
                free(pnames);
                env_define(menv, mnode->as.func_def.name, mfn);
                val_release(mfn);
            }
            Value* inst = (Value*)calloc(1, sizeof(Value));
            if (!inst) {
                env_release(menv);
                for (int i = 0; i < nfields; i++) { free(fnames[i]); val_release(fvals[i]); }
                free(fnames); free(fvals);
                vm_error(vm, line, "out of memory");
                return val_null();
            }
            inst->type      = VAL_STRUCT_INSTANCE;
            inst->ref_count = 1;
            strncpy(inst->as.instance.type_name, sdef->as.struct_def.name, 63);
            inst->as.instance.field_names  = fnames;
            inst->as.instance.fields       = fvals;
            inst->as.instance.field_count  = nfields;
            inst->as.instance.methods      = menv;
            return inst;
        }

        /* Regular function: body node is ND_FUNC_DEF — we compile its body */
        if (!body_node) {
            vm_error(vm, line, "function '%s' has no body", fn->as.func.name);
            return val_null();
        }

        /* Retrieve the body statements */
        NodeList* body_stmts = NULL;
        if (body_node->kind == ND_FUNC_DEF) {
            body_stmts = &body_node->as.func_def.body;
        } else {
            vm_error(vm, line, "unexpected function body node kind");
            return val_null();
        }

        /* Compile the function body into a fresh chunk.
         * We synthesize a ND_PROGRAM-like compilation by iterating stmts. */
        Chunk func_chunk;
        chunk_init(&func_chunk);

        /* Build a temporary Compiler and compile each statement */
        {
            /* We use a small struct matching the one in compiler_bc.c.
             * Since that struct is file-scoped, we replicate just what we need. */
            Chunk* ch = &func_chunk;
            int had_error_local = 0;
            char local_err[256] = {0};

            /* We'll re-use bc_compile via a wrapper trick:
             * create a synthetic ND_PROGRAM node pointing to body_stmts. */
            ASTNode prog;
            memset(&prog, 0, sizeof(prog));
            prog.kind = ND_PROGRAM;
            prog.line = body_node->line;
            prog.as.program.stmts.items = body_stmts->items;
            prog.as.program.stmts.count = body_stmts->count;
            prog.as.program.stmts.cap   = body_stmts->cap;

            int ok = bc_compile(&prog, ch, local_err);
            if (!ok) {
                had_error_local = 1;
                strncpy(local_err, local_err, 255);
            }
            if (had_error_local) {
                chunk_free(&func_chunk);
                vm_error(vm, line, "error compiling function body: %s", local_err);
                return val_null();
            }
        }

        /* Create a new environment for the function call */
        Env* call_env;
        if (fn->as.func.closure) {
            call_env = env_new(fn->as.func.closure);
        } else {
            call_env = env_new(vm->globals);
        }

        /* Bind parameters */
        for (int i = 0; i < fn->as.func.param_count; i++) {
            Value* v = (i < argc) ? val_retain(args[i]) : val_null();
            env_define(call_env, fn->as.func.param_names[i], v);
            val_release(v);
        }

        /* Execute the compiled chunk in the new env */
        Value* ret = val_null();
        int ok = vm_execute(vm, &func_chunk, call_env, &ret);
        chunk_free(&func_chunk);
        env_release(call_env);

        if (!ok && vm->had_error) {
            val_release(ret);
            return val_null();
        }
        return ret;
    }

    vm_error(vm, line, "attempt to call non-function value of type %s",
             val_type_name(fn->type));
    return val_null();
}

/* =========================================================================
 * vm_execute — inner execution loop
 *
 * Executes `chunk` in the given `call_env`.
 * Puts the return value (if any) into *ret_out (new reference).
 * Returns 1 on success, 0 on error.
 * ====================================================================== */

static int vm_execute(VM* vm, Chunk* chunk, Env* call_env, Value** ret_out) {
    if (!chunk || chunk->count == 0) {
        *ret_out = val_null();
        return 1;
    }

    /* Save and restore VM state around recursive calls */
    Chunk*   saved_chunk = vm->chunk;
    uint8_t* saved_ip    = vm->ip;
    Env*     saved_env   = vm->current_env;
    int      saved_sp    = vm->sp;

    vm->chunk       = chunk;
    vm->ip          = chunk->code;
    vm->current_env = call_env;

#define READ_BYTE()    (*vm->ip++)
#define READ_UINT16()  (vm->ip += 2, (uint16_t)(vm->ip[-2] | (vm->ip[-1] << 8)))
#define CURRENT_LINE() (chunk->lines[(int)(vm->ip - chunk->code - 1)])

    while (!vm->had_error) {
        if (vm->ip >= chunk->code + chunk->count) break;

        uint8_t op = READ_BYTE();
        int     ln = chunk->lines[(int)(vm->ip - chunk->code - 1)];

        switch ((OpCode)op) {

        /* ---- Stack ---- */
        case OP_PUSH_CONST: {
            uint16_t idx = READ_UINT16();
            if (idx >= (uint16_t)chunk->const_count) {
                vm_error(vm, ln, "invalid constant index %u", (unsigned)idx);
                break;
            }
            vm_push(vm, val_retain(chunk->constants[idx]));
            break;
        }
        case OP_PUSH_NULL:  vm_push(vm, val_null());        break;
        case OP_PUSH_TRUE:  vm_push(vm, val_bool(1));       break;
        case OP_PUSH_FALSE: vm_push(vm, val_bool(0));       break;
        case OP_POP: {
            Value* v = vm_pop(vm);
            val_release(v);
            break;
        }
        case OP_DUP: {
            Value* top = vm_peek(vm, 0);
            vm_push(vm, val_retain(top));
            break;
        }

        /* ---- Variables ---- */
        case OP_LOAD: {
            uint16_t si = READ_UINT16();
            if (si >= (uint16_t)chunk->str_count) {
                vm_error(vm, ln, "invalid string index %u", (unsigned)si);
                break;
            }
            const char* name = chunk->strings[si];
            Value* v = env_get(vm->current_env, name);
            if (!v) {
                vm_error(vm, ln, "undefined variable '%s'", name);
                vm_push(vm, val_null());
                break;
            }
            vm_push(vm, val_retain(v));
            break;
        }
        case OP_STORE: {
            uint16_t si = READ_UINT16();
            if (si >= (uint16_t)chunk->str_count) {
                vm_error(vm, ln, "invalid string index %u", (unsigned)si);
                break;
            }
            const char* name = chunk->strings[si];
            Value* v = vm_peek(vm, 0);  /* leave on stack */
            if (!env_set(vm->current_env, name, v)) {
                env_define(vm->current_env, name, v);
            }
            break;
        }
        case OP_DEFINE: {
            uint16_t si = READ_UINT16();
            if (si >= (uint16_t)chunk->str_count) {
                vm_error(vm, ln, "invalid string index %u", (unsigned)si);
                break;
            }
            const char* name = chunk->strings[si];
            Value* v = vm_pop(vm);
            env_define(vm->current_env, name, v);
            val_release(v);
            break;
        }

        /* ---- Arithmetic ---- */
        case OP_ADD: { Value* b = vm_pop(vm); Value* a = vm_pop(vm);
                       vm_push(vm, arith_add(vm, a, b, ln));
                       val_release(a); val_release(b); break; }
        case OP_SUB: { Value* b = vm_pop(vm); Value* a = vm_pop(vm);
                       vm_push(vm, arith_sub(vm, a, b, ln));
                       val_release(a); val_release(b); break; }
        case OP_MUL: { Value* b = vm_pop(vm); Value* a = vm_pop(vm);
                       vm_push(vm, arith_mul(vm, a, b, ln));
                       val_release(a); val_release(b); break; }
        case OP_DIV: { Value* b = vm_pop(vm); Value* a = vm_pop(vm);
                       vm_push(vm, arith_div(vm, a, b, ln));
                       val_release(a); val_release(b); break; }
        case OP_MOD: { Value* b = vm_pop(vm); Value* a = vm_pop(vm);
                       vm_push(vm, arith_mod(vm, a, b, ln));
                       val_release(a); val_release(b); break; }
        case OP_POW: { Value* b = vm_pop(vm); Value* a = vm_pop(vm);
                       vm_push(vm, arith_pow(vm, a, b, ln));
                       val_release(a); val_release(b); break; }
        case OP_NEG: { Value* a = vm_pop(vm);
                       if (a->type == VAL_INT)   vm_push(vm, val_int(-a->as.integer));
                       else if (a->type == VAL_FLOAT) vm_push(vm, val_float(-a->as.number));
                       else { vm_error(vm, ln, "unary '-' requires numeric type"); vm_push(vm, val_null()); }
                       val_release(a); break; }
        case OP_NOT: { Value* a = vm_pop(vm);
                       vm_push(vm, val_bool(!val_truthy(a)));
                       val_release(a); break; }

        /* ---- Specton ---- */
        case OP_SPECT_RESONATE: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            if (a->type == VAL_SPECT && b->type == VAL_SPECT)
                vm_push(vm, val_spect(spect_resonate(a->as.spect, b->as.spect)));
            else { vm_error(vm, ln, "resonate requires Specton operands"); vm_push(vm, val_null()); }
            val_release(a); val_release(b); break;
        }
        case OP_SPECT_SPREAD: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            if (a->type == VAL_SPECT && b->type == VAL_SPECT)
                vm_push(vm, val_spect(spect_spread(a->as.spect, b->as.spect)));
            else { vm_error(vm, ln, "spread requires Specton operands"); vm_push(vm, val_null()); }
            val_release(a); val_release(b); break;
        }
        case OP_SPECT_INTERFERE: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            if (a->type == VAL_SPECT && b->type == VAL_SPECT)
                vm_push(vm, val_spect(spect_interfere(a->as.spect, b->as.spect)));
            else { vm_error(vm, ln, "interfere requires Specton operands"); vm_push(vm, val_null()); }
            val_release(a); val_release(b); break;
        }
        case OP_SPECT_COLLAPSE: {
            Value* a = vm_pop(vm);
            if (a->type == VAL_SPECT)
                vm_push(vm, val_int((int64_t)spect_collapse(a->as.spect)));
            else { vm_error(vm, ln, "collapse requires Specton operand"); vm_push(vm, val_null()); }
            val_release(a); break;
        }
        case OP_SPECT_TO_WAVE: {
            Value* a = vm_pop(vm);
            if (a->type == VAL_SPECT)
                vm_push(vm, val_spect(spect_to_wave(a->as.spect)));
            else { vm_error(vm, ln, "to_wave requires Specton operand"); vm_push(vm, val_null()); }
            val_release(a); break;
        }

        /* ---- Compare ---- */
        case OP_EQ: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            vm_push(vm, val_bool(val_equal(a, b)));
            val_release(a); val_release(b); break;
        }
        case OP_NEQ: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            vm_push(vm, val_bool(!val_equal(a, b)));
            val_release(a); val_release(b); break;
        }
        case OP_LT: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            vm_push(vm, val_bool(val_compare_lt(vm, a, b, ln)));
            val_release(a); val_release(b); break;
        }
        case OP_GT: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            vm_push(vm, val_bool(val_compare_gt(vm, a, b, ln)));
            val_release(a); val_release(b); break;
        }
        case OP_LTE: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            int lt = val_compare_lt(vm, a, b, ln);
            int eq = val_equal(a, b);
            vm_push(vm, val_bool(lt || eq));
            val_release(a); val_release(b); break;
        }
        case OP_GTE: {
            Value* b = vm_pop(vm); Value* a = vm_pop(vm);
            int gt = val_compare_gt(vm, a, b, ln);
            int eq = val_equal(a, b);
            vm_push(vm, val_bool(gt || eq));
            val_release(a); val_release(b); break;
        }

        /* ---- Jumps ---- */
        case OP_JUMP: {
            uint16_t target = READ_UINT16();
            vm->ip = chunk->code + target;
            break;
        }
        case OP_JUMP_FALSE: {
            uint16_t target = READ_UINT16();
            Value* cond = vm_pop(vm);
            int truthy = val_truthy(cond);
            val_release(cond);
            if (!truthy) vm->ip = chunk->code + target;
            break;
        }
        case OP_JUMP_TRUE: {
            uint16_t target = READ_UINT16();
            Value* cond = vm_pop(vm);
            int truthy = val_truthy(cond);
            val_release(cond);
            if (truthy) vm->ip = chunk->code + target;
            break;
        }

        /* ---- Functions ---- */
        case OP_MAKE_FUNC: {
            uint16_t ci = READ_UINT16();
            if (ci >= (uint16_t)chunk->const_count) {
                vm_error(vm, ln, "invalid constant index for MAKE_FUNC");
                break;
            }
            /* Retrieve the function constant, set its closure to current env */
            Value* fn_template = chunk->constants[ci];
            if (fn_template->type != VAL_FUNC) {
                vm_error(vm, ln, "MAKE_FUNC constant is not a function");
                break;
            }
            /* Create a new value with the closure bound to current env */
            Value* fn = val_func(fn_template->as.func.name,
                                 fn_template->as.func.param_names,
                                 fn_template->as.func.param_count,
                                 fn_template->as.func.body,
                                 vm->current_env,
                                 fn_template->as.func.is_method);
            vm_push(vm, fn);
            break;
        }
        case OP_RETURN: {
            Value* ret_val = vm_pop(vm);
            /* Release inner scope envs (pushed by OP_SCOPE_PUSH), but stop at
             * call_env — that is the frame's base env owned by the caller. */
            while (vm->current_env != call_env && vm->current_env != NULL) {
                Env* inner = vm->current_env;
                Env* outer = inner->parent;
                vm->current_env = outer;
                env_release(inner);
            }
            /* Pop any extra values that were on the stack in this frame */
            while (vm->sp > saved_sp) {
                Value* v = vm_pop(vm);
                val_release(v);
            }
            vm->chunk       = saved_chunk;
            vm->ip          = saved_ip;
            vm->current_env = saved_env;
            *ret_out = ret_val; /* caller owns this */
            return 1;
        }
        case OP_CALL: {
            uint16_t argc_u = READ_UINT16();
            int argc_i = (int)argc_u;

            /* Pop args (last arg at TOS) */
            Value** args_arr = (Value**)malloc((size_t)(argc_i > 0 ? argc_i : 1) * sizeof(Value*));
            if (!args_arr) { vm_error(vm, ln, "out of memory"); break; }
            for (int i = argc_i - 1; i >= 0; i--) {
                args_arr[i] = vm_pop(vm);
            }
            Value* callee = vm_pop(vm);

            Value* result = vm_call_func(vm, callee, args_arr, argc_i, ln);

            for (int i = 0; i < argc_i; i++) val_release(args_arr[i]);
            free(args_arr);
            val_release(callee);

            vm_push(vm, result ? result : val_null());
            break;
        }

        /* ---- Methods ---- */
        case OP_CALL_METHOD: {
            /* Encoding: name_idx (2 bytes) + argc (2 bytes) */
            uint16_t name_idx = (uint16_t)(vm->ip[0] | (vm->ip[1] << 8));
            vm->ip += 2;
            uint16_t argc_u = (uint16_t)(vm->ip[0] | (vm->ip[1] << 8));
            vm->ip += 2;
            int argc_i = (int)argc_u;

            if (name_idx >= (uint16_t)chunk->str_count) {
                vm_error(vm, ln, "invalid string index for CALL_METHOD");
                break;
            }
            const char* method_name = chunk->strings[name_idx];

            /* Pop args */
            Value** margs = (Value**)malloc((size_t)(argc_i > 0 ? argc_i : 1) * sizeof(Value*));
            if (!margs) { vm_error(vm, ln, "out of memory"); break; }
            for (int i = argc_i - 1; i >= 0; i--) {
                margs[i] = vm_pop(vm);
            }
            Value* obj = vm_pop(vm);

            Value* result = vm_call_method(vm, obj, method_name, margs, argc_i, ln);

            for (int i = 0; i < argc_i; i++) val_release(margs[i]);
            free(margs);
            val_release(obj);

            vm_push(vm, result ? result : val_null());
            break;
        }

        /* ---- Collections ---- */
        case OP_MAKE_ARRAY: {
            uint16_t n = READ_UINT16();
            SpectArray* arr = sarray_alloc((size_t)n);
            /* Pop values in reverse order */
            for (int i = (int)n - 1; i >= 0; i--) {
                Value* v = vm_pop(vm);
                Specton s;
                if (v->type == VAL_SPECT)  s = v->as.spect;
                else if (v->type == VAL_INT)    s = spect_fixed((uint8_t)(v->as.integer % 10));
                else if (v->type == VAL_FLOAT)  s = spect_fixed((uint8_t)((int64_t)v->as.number % 10));
                else s = spect_fixed(0);
                sarray_set(arr, (size_t)i, s);
                val_release(v);
            }
            vm_push(vm, val_array(arr));
            break;
        }
        case OP_INDEX_GET: {
            Value* idx = vm_pop(vm);
            Value* obj = vm_pop(vm);
            Value* result = vm_index_get(vm, obj, idx, ln);
            val_release(obj);
            val_release(idx);
            vm_push(vm, result);
            break;
        }
        case OP_INDEX_SET: {
            /* Stack (bottom to top): value, obj, index */
            Value* idx    = vm_pop(vm);
            Value* obj    = vm_pop(vm);
            Value* newval = vm_pop(vm);
            vm_index_set(vm, obj, idx, newval, ln);
            val_release(newval);
            val_release(obj);
            val_release(idx);
            break;
        }
        case OP_GET_ATTR: {
            uint16_t si = READ_UINT16();
            if (si >= (uint16_t)chunk->str_count) {
                vm_error(vm, ln, "invalid string index for GET_ATTR"); break;
            }
            Value* obj = vm_pop(vm);
            Value* result = vm_get_attr(vm, obj, chunk->strings[si], ln);
            val_release(obj);
            vm_push(vm, result);
            break;
        }
        case OP_SET_ATTR: {
            uint16_t si = READ_UINT16();
            if (si >= (uint16_t)chunk->str_count) {
                vm_error(vm, ln, "invalid string index for SET_ATTR"); break;
            }
            Value* obj    = vm_pop(vm);
            Value* newval = vm_pop(vm);
            vm_set_attr(vm, obj, chunk->strings[si], newval, ln);
            val_release(obj);
            val_release(newval);
            break;
        }

        /* ---- Scope ---- */
        case OP_SCOPE_PUSH: {
            Env* child = env_new(vm->current_env);
            /* We store the old env pointer by pushing a NULL sentinel on the
             * value stack is not ideal; instead we track scopes via the env
             * parent chain and restore on SCOPE_POP by walking to parent. */
            /* Push sentinel to mark scope boundary */
            (void)child;
            /* Actually we update current_env and use a sentinel on the stack */
            vm->current_env = child;
            /* Push NULL marker so SCOPE_POP knows where to stop — but that
             * complicates the stack.  Instead, we just use env parent chain: */
            break;
        }
        case OP_SCOPE_POP: {
            Env* inner = vm->current_env;
            Env* outer = inner->parent;
            if (outer) {
                vm->current_env = outer;
                env_release(inner);
            }
            break;
        }

        /* ---- Control ---- */
        case OP_HALT:
            goto done;

        default:
            vm_error(vm, ln, "unknown opcode %d", (int)op);
            break;
        }
    }

done:;
    /* Release inner scope envs pushed by OP_SCOPE_PUSH that weren't popped
     * (e.g. early exit via OP_HALT or error). Stop at call_env — the frame's
     * base env is owned by the caller and must not be released here. */
    while (vm->current_env != call_env && vm->current_env != NULL) {
        Env* inner = vm->current_env;
        Env* outer = inner->parent;
        vm->current_env = outer;
        env_release(inner);
    }

    /* Collect any remaining values on the stack frame and use the top as
     * the implicit return value. */
    Value* implicit_ret = val_null();
    if (vm->sp > saved_sp) {
        implicit_ret = vm_pop(vm);
        /* Discard remaining stack items from this frame */
        while (vm->sp > saved_sp) {
            val_release(vm_pop(vm));
        }
    }

    vm->chunk       = saved_chunk;
    vm->ip          = saved_ip;
    vm->current_env = saved_env;

    *ret_out = implicit_ret;

#undef READ_BYTE
#undef READ_UINT16
#undef CURRENT_LINE

    return vm->had_error ? 0 : 1;
}

/* =========================================================================
 * vm_new / vm_free
 * ====================================================================== */

VM* vm_new(void) {
    VM* vm = (VM*)calloc(1, sizeof(VM));
    if (!vm) {
        fprintf(stderr, "fatal: out of memory allocating VM\n");
        exit(1);
    }
    vm->globals     = env_new(NULL);
    vm->current_env = vm->globals;
    vm->sp          = 0;
    vm->sim_mode    = 0;
    vm->had_error   = 0;
    vm->error_line  = 0;
    vm->error_msg[0] = '\0';
    vm->chunk       = NULL;
    vm->ip          = NULL;

    vm_register_builtins(vm);
    return vm;
}

void vm_free(VM* vm) {
    if (!vm) return;
    /* Release any remaining stack values */
    while (vm->sp > 0) {
        val_release(vm_pop(vm));
    }
    /* Release environments */
    if (vm->current_env && vm->current_env != vm->globals) {
        env_release(vm->current_env);
    }
    env_release(vm->globals);
    free(vm);
}

/* =========================================================================
 * vm_register_builtins
 * ====================================================================== */

void vm_register_builtins(VM* vm) {
    register_builtins(vm->globals);
}

/* =========================================================================
 * vm_run — public entry point
 * ====================================================================== */

int vm_run(VM* vm, Chunk* chunk) {
    vm->had_error   = 0;
    vm->error_msg[0] = '\0';

    Value* ret = NULL;
    int ok = vm_execute(vm, chunk, vm->globals, &ret);
    if (ret) val_release(ret);

    if (ok && !vm->had_error) {
        /* Auto-call main() if defined */
        Value* main_fn = env_get(vm->globals, "main");
        if (main_fn && (main_fn->type == VAL_FUNC || main_fn->type == VAL_NATIVE)) {
            Value* result = vm_call_func(vm, main_fn, NULL, 0, 0);
            if (result) val_release(result);
        }
    }

    if (vm->had_error) {
        fprintf(stderr, "runtime error (line %d): %s\n",
                vm->error_line, vm->error_msg);
        return 0;
    }
    return 1;
}
