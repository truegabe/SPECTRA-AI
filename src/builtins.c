/* builtins.c — built-in functions for the SPECTRA interpreter */
#include "builtins.h"
#include "interpreter.h"
#include "value.h"
#include "env.h"
#include "../runtime/specton.h"
#include "../runtime/tensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

/* =========================================================
 * Helper macros
 * ========================================================= */

#define NATIVE(name) static Value* builtin_##name(Interpreter* interp, Value** args, int argc)

#define CHECK_ARGC(fn, n) \
    do { if (argc < (n)) { \
        interp_error(interp, 0, "%s expects at least %d arg(s), got %d", #fn, (n), argc); \
        return NULL; \
    } } while(0)

/* Collapse a Specton to an int — works for any mode */
#define ARG_SPECT(i)  (args[i]->type == VAL_SPECT ? args[i]->as.spect : spect_fixed(0))

#define ARG_INT(i) \
    (args[i]->type == VAL_INT   ? (int)args[i]->as.integer \
   : args[i]->type == VAL_FLOAT ? (int)args[i]->as.number  \
   : args[i]->type == VAL_BOOL  ? (int)args[i]->as.boolean \
   : args[i]->type == VAL_SPECT ? (int)spect_collapse(args[i]->as.spect) \
   : 0)

#define ARG_FLOAT(i) \
    (args[i]->type == VAL_FLOAT ? args[i]->as.number       \
   : args[i]->type == VAL_INT   ? (double)args[i]->as.integer \
   : args[i]->type == VAL_BOOL  ? (double)args[i]->as.boolean \
   : args[i]->type == VAL_SPECT ? (double)spect_collapse(args[i]->as.spect) \
   : 0.0)

/* =========================================================
 * I/O built-ins
 * ========================================================= */

/* print(...) — print values space-separated with newline */
NATIVE(print) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        val_print(args[i]);
    }
    printf("\n");
    return val_null();
}

/* println(x) — alias for print */
NATIVE(println) {
    if (argc == 0) {
        printf("\n");
        return val_null();
    }
    val_println(args[0]);
    return val_null();
}

/* input(prompt?) — read a line from stdin, return val_str */
NATIVE(input) {
    if (argc > 0) {
        /* print prompt without newline */
        char* s = val_to_string(args[0]);
        printf("%s", s);
        free(s);
        fflush(stdout);
    }
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) {
        return val_str("");
    }
    /* strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    if (len > 1 && buf[len - 2] == '\r') buf[len - 2] = '\0';
    return val_str(buf);
}

/* =========================================================
 * Type-inspection / conversion built-ins
 * ========================================================= */

/* type(x) — return the type name as a string */
NATIVE(type) {
    CHECK_ARGC(type, 1);
    return val_str(val_type_name(args[0]->type));
}

/* int(x) — convert to VAL_INT */
NATIVE(int_cast) {
    CHECK_ARGC(int, 1);
    Value* v = args[0];
    switch (v->type) {
        case VAL_INT:   return val_int(v->as.integer);
        case VAL_FLOAT: return val_int((int64_t)v->as.number);
        case VAL_BOOL:  return val_int((int64_t)v->as.boolean);
        case VAL_STR:   return val_int((int64_t)atoll(v->as.string));
        case VAL_SPECT: return val_int((int64_t)spect_collapse(v->as.spect));
        case VAL_NULL:  return val_int(0);
        default:
            interp_error(interp, 0, "int(): cannot convert type '%s'",
                         val_type_name(v->type));
            return NULL;
    }
}

/* float(x) — convert to VAL_FLOAT */
NATIVE(float_cast) {
    CHECK_ARGC(float, 1);
    Value* v = args[0];
    switch (v->type) {
        case VAL_FLOAT: return val_float(v->as.number);
        case VAL_INT:   return val_float((double)v->as.integer);
        case VAL_BOOL:  return val_float((double)v->as.boolean);
        case VAL_STR:   return val_float(atof(v->as.string));
        case VAL_SPECT: return val_float((double)spect_collapse(v->as.spect));
        case VAL_NULL:  return val_float(0.0);
        default:
            interp_error(interp, 0, "float(): cannot convert type '%s'",
                         val_type_name(v->type));
            return NULL;
    }
}

/* str(x) — convert to VAL_STR */
NATIVE(str_cast) {
    CHECK_ARGC(str, 1);
    char* s = val_to_string(args[0]);
    return val_str_own(s);
}

/* bool(x) — convert to VAL_BOOL */
NATIVE(bool_cast) {
    CHECK_ARGC(bool, 1);
    return val_bool(val_truthy(args[0]));
}

/* =========================================================
 * Container utilities
 * ========================================================= */

/* len(x) — length of string, array, list, map, or matrix */
NATIVE(len) {
    CHECK_ARGC(len, 1);
    if (args[0]->type == VAL_STR)
        return val_int((int64_t)strlen(args[0]->as.string ? args[0]->as.string : ""));
    if (args[0]->type == VAL_ARRAY)
        return val_int((int64_t)args[0]->as.array->length);
    if (args[0]->type == VAL_LIST)
        return val_int((int64_t)args[0]->as.list->length);
    if (args[0]->type == VAL_MAP)
        return val_int((int64_t)val_map_len(args[0]));
    if (args[0]->type == VAL_MATRIX)
        return val_int((int64_t)(args[0]->as.matrix->rows * args[0]->as.matrix->cols));
    interp_error(interp, 0, "len(): unsupported type %s", val_type_name(args[0]->type));
    return NULL;
}

/* =========================================================
 * range built-in
 * range(stop)
 * range(start, stop)
 * range(start, stop, step)
 *
 * Returns a VAL_STR with the special prefix "__range__start__stop__step"
 * which the for-loop handler in interpreter.c detects and iterates over.
 * ========================================================= */

NATIVE(range) {
    int start = 0, stop = 0, step = 1;
    if (argc == 0) {
        interp_error(interp, 0, "range() requires at least 1 argument");
        return NULL;
    }
    if (argc == 1) {
        stop = ARG_INT(0);
    } else if (argc >= 2) {
        start = ARG_INT(0);
        stop  = ARG_INT(1);
    }
    if (argc >= 3) {
        step = ARG_INT(2);
    }
    if (step == 0) step = 1;

    char buf[128];
    snprintf(buf, sizeof(buf), "__range__%d__%d__%d", start, stop, step);
    return val_str(buf);
}

/* =========================================================
 * Array / matrix factory built-ins
 * ========================================================= */

/* zeros(n) or zeros([r, c]) */
NATIVE(zeros) {
    if (argc < 1) {
        interp_error(interp, 0, "zeros() requires 1 argument");
        return NULL;
    }
    Value* v = args[0];
    if (v->type == VAL_INT) {
        size_t n = (size_t)(v->as.integer < 0 ? 0 : v->as.integer);
        return val_array(sarray_zeros(n));
    }
    if (v->type == VAL_ARRAY) {
        SpectArray* shape = v->as.array;
        if (shape->length == 1) {
            int n = (int)spect_collapse(sarray_get(shape, 0));
            if (n < 0) n = 0;
            return val_array(sarray_zeros((size_t)n));
        }
        if (shape->length >= 2) {
            int r = (int)spect_collapse(sarray_get(shape, 0));
            int c = (int)spect_collapse(sarray_get(shape, 1));
            if (r < 0) r = 0;
            if (c < 0) c = 0;
            return val_matrix(smat_zeros((size_t)r, (size_t)c));
        }
    }
    interp_error(interp, 0, "zeros(): argument must be int or array");
    return NULL;
}

/* ones(n) or ones([r, c]) */
NATIVE(ones) {
    if (argc < 1) {
        interp_error(interp, 0, "ones() requires 1 argument");
        return NULL;
    }
    Value* v = args[0];
    if (v->type == VAL_INT) {
        size_t n = (size_t)(v->as.integer < 0 ? 0 : v->as.integer);
        return val_array(sarray_ones(n));
    }
    if (v->type == VAL_ARRAY) {
        SpectArray* shape = v->as.array;
        if (shape->length == 1) {
            int n = (int)spect_collapse(sarray_get(shape, 0));
            if (n < 0) n = 0;
            return val_array(sarray_ones((size_t)n));
        }
        if (shape->length >= 2) {
            int r = (int)spect_collapse(sarray_get(shape, 0));
            int c = (int)spect_collapse(sarray_get(shape, 1));
            if (r < 0) r = 0;
            if (c < 0) c = 0;
            return val_matrix(smat_ones((size_t)r, (size_t)c));
        }
    }
    interp_error(interp, 0, "ones(): argument must be int or array");
    return NULL;
}

/* random_wave(n) or random_wave([r, c]) */
NATIVE(random_wave) {
    if (argc < 1) {
        interp_error(interp, 0, "random_wave() requires 1 argument");
        return NULL;
    }
    Value* v = args[0];
    if (v->type == VAL_INT) {
        size_t n = (size_t)(v->as.integer < 0 ? 0 : v->as.integer);
        return val_array(sarray_random_wave(n));
    }
    if (v->type == VAL_ARRAY) {
        SpectArray* shape = v->as.array;
        if (shape->length == 1) {
            int n = (int)spect_collapse(sarray_get(shape, 0));
            if (n < 0) n = 0;
            return val_array(sarray_random_wave((size_t)n));
        }
        if (shape->length >= 2) {
            int r = (int)spect_collapse(sarray_get(shape, 0));
            int c = (int)spect_collapse(sarray_get(shape, 1));
            if (r < 0) r = 0;
            if (c < 0) c = 0;
            return val_matrix(smat_random_wave((size_t)r, (size_t)c));
        }
    }
    interp_error(interp, 0, "random_wave(): argument must be int or array");
    return NULL;
}

/* fill(shape, val) — fill with a Specton value */
NATIVE(fill) {
    if (argc < 2) {
        interp_error(interp, 0, "fill() requires 2 arguments: fill(shape, val)");
        return NULL;
    }
    Value* shape_val = args[0];
    Value* fill_val  = args[1];

    /* Determine the Specton to fill with */
    Specton sp;
    if (fill_val->type == VAL_SPECT) {
        sp = fill_val->as.spect;
    } else if (fill_val->type == VAL_INT) {
        sp = spect_fixed((uint8_t)(fill_val->as.integer % 10));
    } else if (fill_val->type == VAL_FLOAT) {
        sp = spect_fixed((uint8_t)((int)fill_val->as.number % 10));
    } else {
        sp = spect_fixed(0);
    }

    if (shape_val->type == VAL_INT) {
        size_t n = (size_t)(shape_val->as.integer < 0 ? 0 : shape_val->as.integer);
        return val_array(sarray_fill(n, sp));
    }
    if (shape_val->type == VAL_ARRAY) {
        SpectArray* shape = shape_val->as.array;
        if (shape->length == 1) {
            int n = (int)spect_collapse(sarray_get(shape, 0));
            if (n < 0) n = 0;
            return val_array(sarray_fill((size_t)n, sp));
        }
        if (shape->length >= 2) {
            int r = (int)spect_collapse(sarray_get(shape, 0));
            int c = (int)spect_collapse(sarray_get(shape, 1));
            if (r < 0) r = 0;
            if (c < 0) c = 0;
            return val_matrix(smat_fill((size_t)r, (size_t)c, sp));
        }
    }
    interp_error(interp, 0, "fill(): first argument must be int or array");
    return NULL;
}

/* sizeof(x) — return size of the underlying Specton struct, or type size */
NATIVE(sizeof_fn) {
    /* Without a type-system reflection, we return sizeof(Specton) as a useful constant */
    if (argc == 0) {
        return val_int((int64_t)sizeof(Specton));
    }
    Value* v = args[0];
    switch (v->type) {
        case VAL_INT:   return val_int(sizeof(int64_t));
        case VAL_FLOAT: return val_int(sizeof(double));
        case VAL_SPECT: return val_int(sizeof(Specton));
        case VAL_BOOL:  return val_int(sizeof(int));
        case VAL_ARRAY:
            return val_int((int64_t)(v->as.array->length * sizeof(Specton)));
        case VAL_MATRIX:
            return val_int((int64_t)(v->as.matrix->rows *
                                     v->as.matrix->cols * sizeof(Specton)));
        default:
            return val_int(0);
    }
}

/* =========================================================
 * Math built-ins
 * ========================================================= */

NATIVE(sqrt) {
    CHECK_ARGC(sqrt, 1);
    return val_float(sqrt(ARG_FLOAT(0)));
}

NATIVE(abs_fn) {
    CHECK_ARGC(abs, 1);
    Value* v = args[0];
    if (v->type == VAL_INT) {
        int64_t n = v->as.integer;
        return val_int(n < 0 ? -n : n);
    }
    return val_float(fabs(ARG_FLOAT(0)));
}

NATIVE(min_fn) {
    CHECK_ARGC(min, 2);
    /* Works for int, float, or mixed */
    if (args[0]->type == VAL_INT && args[1]->type == VAL_INT) {
        int64_t a = args[0]->as.integer;
        int64_t b = args[1]->as.integer;
        return val_int(a < b ? a : b);
    }
    double a = ARG_FLOAT(0);
    double b = ARG_FLOAT(1);
    return val_float(a < b ? a : b);
}

NATIVE(max_fn) {
    CHECK_ARGC(max, 2);
    if (args[0]->type == VAL_INT && args[1]->type == VAL_INT) {
        int64_t a = args[0]->as.integer;
        int64_t b = args[1]->as.integer;
        return val_int(a > b ? a : b);
    }
    double a = ARG_FLOAT(0);
    double b = ARG_FLOAT(1);
    return val_float(a > b ? a : b);
}

NATIVE(floor_fn) {
    CHECK_ARGC(floor, 1);
    return val_float(floor(ARG_FLOAT(0)));
}

NATIVE(ceil_fn) {
    CHECK_ARGC(ceil, 1);
    return val_float(ceil(ARG_FLOAT(0)));
}

NATIVE(pow_fn) {
    CHECK_ARGC(pow, 2);
    return val_float(pow(ARG_FLOAT(0), ARG_FLOAT(1)));
}

/* =========================================================
 * Specton / Wave built-ins
 * ========================================================= */

/* entropy_sum(arr) — sum of entropy across all Spectons in an array */
NATIVE(entropy_sum) {
    if (argc < 1 || args[0]->type != VAL_ARRAY) {
        return val_float(0.0);
    }
    double sum = 0.0;
    SpectArray* arr = args[0]->as.array;
    for (size_t i = 0; i < arr->length; i++) {
        sum += (double)spect_entropy(sarray_get(arr, i));
    }
    return val_float(sum);
}

/* collapse_all(arr) — collapse all Spectons in an array to fixed values */
NATIVE(collapse_all) {
    if (argc < 1 || args[0]->type != VAL_ARRAY) {
        interp_error(interp, 0, "collapse_all(): expected array argument");
        return NULL;
    }
    SpectArray* result = sarray_collapse_all(args[0]->as.array);
    return val_array(result);
}

/* mean_entropy(arr) — mean entropy across all Spectons */
NATIVE(mean_entropy) {
    if (argc < 1 || args[0]->type != VAL_ARRAY) {
        return val_float(0.0);
    }
    return val_float((double)sarray_mean_entropy(args[0]->as.array));
}

/* seed(n) — seed the Specton RNG */
NATIVE(seed) {
    CHECK_ARGC(seed, 1);
    unsigned int s = (unsigned int)ARG_INT(0);
    spect_seed_rng(s);
    return val_null();
}

/* =========================================================
 * Miscellaneous built-ins
 * ========================================================= */

/* exit(code?) — terminate the process */
NATIVE(exit_fn) {
    int code = 0;
    if (argc > 0) code = ARG_INT(0);
    exit(code);
    return val_null(); /* unreachable */
}

/* assert(cond, msg?) — abort if cond is falsy */
NATIVE(assert_fn) {
    CHECK_ARGC(assert, 1);
    if (!val_truthy(args[0])) {
        if (argc > 1) {
            char* msg = val_to_string(args[1]);
            fprintf(stderr, "[SPECTRA] AssertionError: %s\n", msg);
            free(msg);
        } else {
            fprintf(stderr, "[SPECTRA] AssertionError\n");
        }
        exit(1);
    }
    return val_null();
}

/* =========================================================
 * Higher-order and collection built-ins
 * ========================================================= */

/* list(...) — create a VAL_LIST from arguments */
NATIVE(list_new) {
    Value* lst = val_list_new();
    for (int i = 0; i < argc; i++)
        val_list_append(lst, args[i]);
    return lst;
}

/* dict() — create an empty VAL_MAP */
NATIVE(dict_new) {
    (void)interp; (void)args; (void)argc;
    return val_map_new();
}

/* tostr(x) — convert to string (alias for str cast since "str" is a keyword) */
NATIVE(tostr) {
    if (argc < 1) return val_str("");
    char* s = val_to_string(args[0]);
    Value* r = val_str(s);
    free(s);
    return r;
}

/* toint(x) — convert to int; throws on non-numeric strings */
NATIVE(toint) {
    if (argc < 1) return val_int(0);
    Value* v = args[0];
    if (v->type == VAL_INT)   return val_int(v->as.integer);
    if (v->type == VAL_FLOAT) return val_int((int64_t)v->as.number);
    if (v->type == VAL_BOOL)  return val_int((int64_t)v->as.boolean);
    if (v->type == VAL_SPECT) return val_int((int64_t)spect_collapse(v->as.spect));
    if (v->type == VAL_STR) {
        const char* s = v->as.string ? v->as.string : "";
        /* skip leading whitespace */
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\0') {
            interp_error(interp, 0, "toint(): cannot convert empty string to int");
            return NULL;
        }
        char* end;
        int64_t result = (int64_t)strtoll(s, &end, 10);
        /* skip trailing whitespace */
        while (*end == ' ' || *end == '\t') end++;
        if (*end != '\0') {
            interp_error(interp, 0, "toint(): invalid literal for int: '%s'", v->as.string);
            return NULL;
        }
        return val_int(result);
    }
    interp_error(interp, 0, "toint(): cannot convert '%s' to int", val_type_name(v->type));
    return NULL;
}

/* tofloat(x) — convert to float; throws on non-numeric strings */
NATIVE(tofloat) {
    if (argc < 1) return val_float(0.0);
    Value* v = args[0];
    if (v->type == VAL_FLOAT) return val_float(v->as.number);
    if (v->type == VAL_INT)   return val_float((double)v->as.integer);
    if (v->type == VAL_BOOL)  return val_float((double)v->as.boolean);
    if (v->type == VAL_SPECT) return val_float((double)spect_collapse(v->as.spect));
    if (v->type == VAL_STR) {
        const char* s = v->as.string ? v->as.string : "";
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\0') {
            interp_error(interp, 0, "tofloat(): cannot convert empty string to float");
            return NULL;
        }
        char* end;
        double result = strtod(s, &end);
        while (*end == ' ' || *end == '\t') end++;
        if (*end != '\0') {
            interp_error(interp, 0, "tofloat(): invalid literal for float: '%s'", v->as.string);
            return NULL;
        }
        return val_float(result);
    }
    interp_error(interp, 0, "tofloat(): cannot convert '%s' to float", val_type_name(v->type));
    return NULL;
}

/* sorted(list) — return a new sorted VAL_LIST */
NATIVE(sorted) {
    CHECK_ARGC(sorted, 1);
    if (args[0]->type != VAL_LIST) {
        interp_error(interp, 0, "sorted(): argument must be a list"); return NULL;
    }
    SpectList* src = args[0]->as.list;
    int n = src->length;
    if (n == 0) return val_list_new();

    /* determine sort mode */
    int all_num = 1, all_str = 1;
    for (int i = 0; i < n; i++) {
        ValType t = src->items[i]->type;
        if (t != VAL_INT && t != VAL_FLOAT && t != VAL_SPECT) all_num = 0;
        if (t != VAL_STR) all_str = 0;
    }

    /* working copy */
    Value** tmp = (Value**)malloc((size_t)n * sizeof(Value*));
    for (int i = 0; i < n; i++) tmp[i] = src->items[i];

    /* insertion sort */
    for (int i = 1; i < n; i++) {
        Value* key = tmp[i]; int j = i - 1;
        if (all_num) {
            double kv = (key->type == VAL_INT) ? (double)key->as.integer
                       :(key->type == VAL_FLOAT) ? key->as.number
                       : (double)spect_collapse(key->as.spect);
            while (j >= 0) {
                double cv = (tmp[j]->type == VAL_INT) ? (double)tmp[j]->as.integer
                           :(tmp[j]->type == VAL_FLOAT) ? tmp[j]->as.number
                           : (double)spect_collapse(tmp[j]->as.spect);
                if (cv > kv) { tmp[j+1] = tmp[j]; j--; } else break;
            }
        } else if (all_str) {
            while (j >= 0 && strcmp(tmp[j]->as.string, key->as.string) > 0) {
                tmp[j+1] = tmp[j]; j--;
            }
        }
        tmp[j+1] = key;
    }
    Value* out = val_list_new();
    for (int i = 0; i < n; i++) val_list_append(out, tmp[i]);
    free(tmp);
    return out;
}

/* sum(list_or_array) */
NATIVE(sum_fn) {
    CHECK_ARGC(sum, 1);
    Value* v = args[0];
    if (v->type == VAL_ARRAY) {
        int64_t s = 0;
        for (size_t i = 0; i < v->as.array->length; i++)
            s += (int64_t)spect_collapse(sarray_get(v->as.array, i));
        return val_int(s);
    }
    if (v->type != VAL_LIST) {
        interp_error(interp, 0, "sum(): argument must be list or array"); return NULL;
    }
    SpectList* lst = v->as.list;
    int all_int = 1;
    int64_t isum = 0; double fsum = 0.0;
    for (int i = 0; i < lst->length; i++) {
        Value* item = lst->items[i];
        if (item->type == VAL_INT)        { isum += item->as.integer; fsum += (double)item->as.integer; }
        else if (item->type == VAL_FLOAT) { all_int = 0; fsum += item->as.number; }
        else if (item->type == VAL_SPECT) { int64_t c = (int64_t)spect_collapse(item->as.spect); isum += c; fsum += c; }
    }
    return all_int ? val_int(isum) : val_float(fsum);
}

/* enumerate(list) → [[0,item0],[1,item1],...] */
NATIVE(enumerate_fn) {
    CHECK_ARGC(enumerate, 1);
    if (args[0]->type != VAL_LIST) { interp_error(interp, 0, "enumerate(): needs a list"); return NULL; }
    SpectList* src = args[0]->as.list;
    Value* out = val_list_new();
    for (int i = 0; i < src->length; i++) {
        Value* pair = val_list_new();
        Value* idx  = val_int((int64_t)i);
        val_list_append(pair, idx); val_release(idx);
        val_list_append(pair, src->items[i]);
        val_list_append(out, pair); val_release(pair);
    }
    return out;
}

/* zip(l1, l2) → [[a0,b0],[a1,b1],...] */
NATIVE(zip_fn) {
    CHECK_ARGC(zip, 2);
    if (args[0]->type != VAL_LIST || args[1]->type != VAL_LIST) {
        interp_error(interp, 0, "zip(): both args must be lists"); return NULL;
    }
    SpectList* a = args[0]->as.list, *b = args[1]->as.list;
    int n = (a->length < b->length) ? a->length : b->length;
    Value* out = val_list_new();
    for (int i = 0; i < n; i++) {
        Value* pair = val_list_new();
        val_list_append(pair, a->items[i]);
        val_list_append(pair, b->items[i]);
        val_list_append(out, pair); val_release(pair);
    }
    return out;
}

/* map(fn, list) — apply builtin fn to each element */
NATIVE(map_fn) {
    CHECK_ARGC(map, 2);
    if (args[0]->type != VAL_NATIVE) { interp_error(interp, 0, "map(): first arg must be a builtin function"); return NULL; }
    if (args[1]->type != VAL_LIST)   { interp_error(interp, 0, "map(): second arg must be a list"); return NULL; }
    SpectList* src = args[1]->as.list;
    Value* out = val_list_new();
    for (int i = 0; i < src->length; i++) {
        Value* r = args[0]->as.native(interp, &src->items[i], 1);
        if (!r) { val_release(out); return NULL; }
        val_list_append(out, r); val_release(r);
    }
    return out;
}

/* filter(fn, list) */
NATIVE(filter_fn) {
    CHECK_ARGC(filter, 2);
    if (args[0]->type != VAL_NATIVE) { interp_error(interp, 0, "filter(): first arg must be a builtin function"); return NULL; }
    if (args[1]->type != VAL_LIST)   { interp_error(interp, 0, "filter(): second arg must be a list"); return NULL; }
    SpectList* src = args[1]->as.list;
    Value* out = val_list_new();
    for (int i = 0; i < src->length; i++) {
        Value* r = args[0]->as.native(interp, &src->items[i], 1);
        if (!r) { val_release(out); return NULL; }
        int keep = val_truthy(r); val_release(r);
        if (keep) val_list_append(out, src->items[i]);
    }
    return out;
}

/* format(val, spec) — basic number formatting */
NATIVE(format_fn) {
    CHECK_ARGC(format, 2);
    char* spec = val_to_string(args[1]);
    char buf[64];
    if (args[0]->type == VAL_FLOAT) snprintf(buf, sizeof(buf), "%g", args[0]->as.number);
    else if (args[0]->type == VAL_INT) snprintf(buf, sizeof(buf), "%lld", (long long)args[0]->as.integer);
    else { char* s = val_to_string(args[0]); strncpy(buf, s, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0'; free(s); }
    free(spec);
    return val_str(buf);
}

/* =========================================================
 * register_builtins — install everything into the global env
 * ========================================================= */

void register_builtins(Env* env) {
    env_define(env, "print",        val_native(builtin_print));
    env_define(env, "println",      val_native(builtin_println));
    env_define(env, "input",        val_native(builtin_input));
    env_define(env, "type",         val_native(builtin_type));
    env_define(env, "int",          val_native(builtin_int_cast));
    env_define(env, "float",        val_native(builtin_float_cast));
    env_define(env, "str",          val_native(builtin_str_cast));
    env_define(env, "bool",         val_native(builtin_bool_cast));
    env_define(env, "len",          val_native(builtin_len));
    env_define(env, "range",        val_native(builtin_range));
    env_define(env, "zeros",        val_native(builtin_zeros));
    env_define(env, "ones",         val_native(builtin_ones));
    env_define(env, "random_wave",  val_native(builtin_random_wave));
    env_define(env, "fill",         val_native(builtin_fill));
    env_define(env, "sizeof",       val_native(builtin_sizeof_fn));
    env_define(env, "sqrt",         val_native(builtin_sqrt));
    env_define(env, "abs",          val_native(builtin_abs_fn));
    env_define(env, "min",          val_native(builtin_min_fn));
    env_define(env, "max",          val_native(builtin_max_fn));
    env_define(env, "floor",        val_native(builtin_floor_fn));
    env_define(env, "ceil",         val_native(builtin_ceil_fn));
    env_define(env, "pow",          val_native(builtin_pow_fn));
    env_define(env, "entropy_sum",  val_native(builtin_entropy_sum));
    env_define(env, "collapse_all", val_native(builtin_collapse_all));
    env_define(env, "mean_entropy", val_native(builtin_mean_entropy));
    env_define(env, "seed",         val_native(builtin_seed));
    env_define(env, "exit",         val_native(builtin_exit_fn));
    env_define(env, "assert",       val_native(builtin_assert_fn));
    env_define(env, "list",         val_native(builtin_list_new));
    env_define(env, "dict",         val_native(builtin_dict_new));
    env_define(env, "tostr",        val_native(builtin_tostr));
    env_define(env, "toint",        val_native(builtin_toint));
    env_define(env, "tofloat",      val_native(builtin_tofloat));
    env_define(env, "sorted",       val_native(builtin_sorted));
    env_define(env, "sum",          val_native(builtin_sum_fn));
    env_define(env, "enumerate",    val_native(builtin_enumerate_fn));
    env_define(env, "zip",          val_native(builtin_zip_fn));
    env_define(env, "map",          val_native(builtin_map_fn));
    env_define(env, "filter",       val_native(builtin_filter_fn));
    env_define(env, "format",       val_native(builtin_format_fn));
}
