/* modules.c — Built-in module registry for SPECTRA */

#include "modules.h"
#include "builtins.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"
#include "env.h"
#include "../runtime/neural.h"
#include "../runtime/specton.h"
#include "../runtime/tensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* =========================================================
 * Helper macros (same style as builtins.c)
 * ========================================================= */

#define NATIVE(name) static Value* mod_##name(Interpreter* interp, Value** args, int argc)
#define CHECK_ARGC(fn, n) \
    do { if (argc < (n)) { \
        interp_error(interp, 0, "%s expects %d arg(s)", #fn, (n)); \
        return NULL; \
    } } while(0)

#define ARG_FLOAT(i) \
    (args[i]->type == VAL_FLOAT ? args[i]->as.number \
   : args[i]->type == VAL_INT   ? (double)args[i]->as.integer \
   : args[i]->type == VAL_BOOL  ? (double)args[i]->as.boolean \
   : 0.0)

#define ARG_INT(i) \
    (args[i]->type == VAL_INT   ? (int)args[i]->as.integer \
   : args[i]->type == VAL_FLOAT ? (int)args[i]->as.number \
   : 0)

/* =========================================================
 * neural module
 * =========================================================
 * net = neural.create([4, 5, 3], lr=0.01)
 * out = neural.forward(net, input_row_matrix)
 * neural.train(net, inputs_matrix, targets_matrix, epochs=10)
 * neural.predict(net, input_row_matrix)
 * neural.summary(net)
 * neural.mse(pred_matrix, target_matrix)
 * ========================================================= */

static void nn_free_wrapper(void* ptr) { nn_free((NNNetwork*)ptr); }

/* neural.create(sizes_array, lr) */
NATIVE(neural_create) {
    CHECK_ARGC(neural.create, 1);

    /* sizes: VAL_ARRAY of Spectons treated as ints */
    if (args[0]->type != VAL_ARRAY) {
        interp_error(interp, 0, "neural.create: first arg must be array of sizes");
        return NULL;
    }
    SpectArray* shape = args[0]->as.array;
    int n = (int)shape->length;
    if (n < 2) {
        interp_error(interp, 0, "neural.create: need at least 2 sizes [in, out]");
        return NULL;
    }

    int* sizes = (int*)malloc(sizeof(int) * (size_t)n);
    if (!sizes) { interp_error(interp, 0, "out of memory"); return NULL; }
    for (int i = 0; i < n; i++) {
        int v = (int)spect_collapse(sarray_get(shape, (size_t)i));
        if (v <= 0) v = 1;
        sizes[i] = v;
    }

    float lr = 0.01f;
    if (argc >= 2) lr = (float)ARG_FLOAT(1);

    NNNetwork* net = nn_create(sizes, n, lr);
    free(sizes);
    if (!net) { interp_error(interp, 0, "neural.create: allocation failed"); return NULL; }

    return val_userdata(net, "NNNetwork", nn_free_wrapper);
}

/* neural.forward(net, input_matrix) → output_matrix */
NATIVE(neural_forward) {
    CHECK_ARGC(neural.forward, 2);
    if (args[0]->type != VAL_USERDATA ||
        strcmp(args[0]->as.userdata.tag, "NNNetwork") != 0) {
        interp_error(interp, 0, "neural.forward: first arg must be NNNetwork");
        return NULL;
    }
    if (args[1]->type != VAL_MATRIX) {
        interp_error(interp, 0, "neural.forward: second arg must be matrix");
        return NULL;
    }
    NNNetwork* net = (NNNetwork*)args[0]->as.userdata.ptr;
    SpectMatrix* input = args[1]->as.matrix;
    SpectMatrix* out = nn_forward_mat(net, input);
    if (!out) { interp_error(interp, 0, "neural.forward: failed"); return NULL; }
    return val_matrix(out);
}

/* neural.train(net, inputs_matrix, targets_matrix, epochs) → float (final loss) */
NATIVE(neural_train) {
    CHECK_ARGC(neural.train, 3);
    if (args[0]->type != VAL_USERDATA ||
        strcmp(args[0]->as.userdata.tag, "NNNetwork") != 0) {
        interp_error(interp, 0, "neural.train: first arg must be NNNetwork");
        return NULL;
    }
    NNNetwork* net    = (NNNetwork*)args[0]->as.userdata.ptr;
    SpectMatrix* inp  = (args[1]->type == VAL_MATRIX) ? args[1]->as.matrix : NULL;
    SpectMatrix* tgt  = (args[2]->type == VAL_MATRIX) ? args[2]->as.matrix : NULL;
    int epochs = (argc >= 4) ? ARG_INT(3) : 10;
    if (epochs < 1) epochs = 1;

    if (!inp || !tgt) {
        interp_error(interp, 0, "neural.train: inputs/targets must be matrices");
        return NULL;
    }
    if (inp->rows != tgt->rows) {
        interp_error(interp, 0, "neural.train: input/target row count mismatch");
        return NULL;
    }

    int n_samples = (int)inp->rows;
    /* Convert SpectMatrix rows to float arrays */
    float* in_f  = (float*)malloc(sizeof(float) * (size_t)(n_samples * net->input_size));
    float* tgt_f = (float*)malloc(sizeof(float) * (size_t)(n_samples * net->output_size));
    if (!in_f || !tgt_f) {
        free(in_f); free(tgt_f);
        interp_error(interp, 0, "out of memory");
        return NULL;
    }

    int in_cols  = (int)inp->cols  < net->input_size  ? (int)inp->cols  : net->input_size;
    int out_cols = (int)tgt->cols < net->output_size ? (int)tgt->cols : net->output_size;

    for (int s = 0; s < n_samples; s++) {
        for (int j = 0; j < net->input_size; j++) {
            if (j < in_cols)
                in_f[s * net->input_size + j] =
                    (float)spect_collapse(smat_get(inp, (size_t)s, (size_t)j)) / 9.0f;
            else
                in_f[s * net->input_size + j] = 0.0f;
        }
        for (int j = 0; j < net->output_size; j++) {
            if (j < out_cols)
                tgt_f[s * net->output_size + j] =
                    (float)spect_collapse(smat_get(tgt, (size_t)s, (size_t)j)) / 9.0f;
            else
                tgt_f[s * net->output_size + j] = 0.0f;
        }
    }

    float last_loss = 0.0f;
    for (int e = 0; e < epochs; e++) {
        last_loss = nn_train_epoch(net, in_f, tgt_f, n_samples);
        if ((e + 1) % 10 == 0 || epochs <= 5)
            printf("  epoch %d/%d  loss=%.6f\n", e+1, epochs, (double)last_loss);
    }

    free(in_f);
    free(tgt_f);
    return val_float((double)last_loss);
}

/* neural.predict(net, input_matrix) → output_matrix */
NATIVE(neural_predict) {
    /* Same as forward */
    return mod_neural_forward(interp, args, argc);
}

/* neural.predict_specton(net, input_matrix) → Specton wave */
NATIVE(neural_predict_spect) {
    CHECK_ARGC(neural.predict_spect, 2);
    if (args[0]->type != VAL_USERDATA ||
        strcmp(args[0]->as.userdata.tag, "NNNetwork") != 0) {
        interp_error(interp, 0, "neural.predict_spect: first arg must be NNNetwork");
        return NULL;
    }
    NNNetwork* net = (NNNetwork*)args[0]->as.userdata.ptr;
    if (args[1]->type != VAL_MATRIX) {
        interp_error(interp, 0, "neural.predict_spect: second arg must be matrix");
        return NULL;
    }
    SpectMatrix* inp = args[1]->as.matrix;
    float* in_f = (float*)calloc((size_t)net->input_size, sizeof(float));
    if (!in_f) return val_spect(spect_fixed(0));
    int in_cols = (int)inp->cols < net->input_size ? (int)inp->cols : net->input_size;
    for (int j = 0; j < in_cols; j++)
        in_f[j] = (float)spect_collapse(smat_get(inp, 0, (size_t)j)) / 9.0f;
    Specton s = nn_predict_specton(net, in_f);
    free(in_f);
    return val_spect(s);
}

/* neural.summary(net) */
NATIVE(neural_summary) {
    CHECK_ARGC(neural.summary, 1);
    if (args[0]->type != VAL_USERDATA ||
        strcmp(args[0]->as.userdata.tag, "NNNetwork") != 0) {
        interp_error(interp, 0, "neural.summary: arg must be NNNetwork");
        return NULL;
    }
    nn_print_summary((NNNetwork*)args[0]->as.userdata.ptr);
    return val_null();
}

/* neural.mse(pred_matrix, target_matrix) */
NATIVE(neural_mse) {
    CHECK_ARGC(neural.mse, 2);
    if (args[0]->type != VAL_MATRIX || args[1]->type != VAL_MATRIX) {
        interp_error(interp, 0, "neural.mse: both args must be matrices");
        return NULL;
    }
    SpectMatrix* pred = args[0]->as.matrix;
    SpectMatrix* tgt  = args[1]->as.matrix;
    size_t n = pred->rows * pred->cols;
    if (n == 0) return val_float(0.0);
    float* pf = (float*)malloc(sizeof(float) * n);
    float* tf = (float*)malloc(sizeof(float) * n);
    if (!pf || !tf) { free(pf); free(tf); return val_float(0.0); }
    for (size_t i = 0; i < n; i++) {
        pf[i] = (float)spect_collapse(pred->data[i]) / 9.0f;
        tf[i] = (float)spect_collapse(tgt->data[i])  / 9.0f;
    }
    float mse = nn_mse(pf, tf, (int)n);
    free(pf); free(tf);
    return val_float((double)mse);
}

void modules_register_neural(Env* env) {
    /* Create a sub-namespace env (accessible as "neural.create" etc.)
     * by registering each function with a "neural_" prefix so SPECTRA
     * code can call them as neural.create(), neural.train(), etc.
     * The import system binds them as: neural = { create: fn, ... }
     * For simplicity: bind them in the top-level env under their full
     * dotted names as flat VAL_NATIVE functions, and also bind a
     * struct-like "neural" object. */

    /* Flat bindings accessible after: from neural import create, train, ... */
    env_define(env, "neural_create",        val_native(mod_neural_create));
    env_define(env, "neural_forward",       val_native(mod_neural_forward));
    env_define(env, "neural_train",         val_native(mod_neural_train));
    env_define(env, "neural_predict",       val_native(mod_neural_predict));
    env_define(env, "neural_predict_spect", val_native(mod_neural_predict_spect));
    env_define(env, "neural_summary",       val_native(mod_neural_summary));
    env_define(env, "neural_mse",           val_native(mod_neural_mse));
}

/* =========================================================
 * math module
 * ========================================================= */

NATIVE(math_sin)   { CHECK_ARGC(math.sin,   1); return val_float(sin(ARG_FLOAT(0)));  }
NATIVE(math_cos)   { CHECK_ARGC(math.cos,   1); return val_float(cos(ARG_FLOAT(0)));  }
NATIVE(math_tan)   { CHECK_ARGC(math.tan,   1); return val_float(tan(ARG_FLOAT(0)));  }
NATIVE(math_log)   { CHECK_ARGC(math.log,   1); return val_float(log(ARG_FLOAT(0)));  }
NATIVE(math_log2)  { CHECK_ARGC(math.log2,  1); return val_float(log2(ARG_FLOAT(0))); }
NATIVE(math_log10) { CHECK_ARGC(math.log10, 1); return val_float(log10(ARG_FLOAT(0)));}
NATIVE(math_exp)   { CHECK_ARGC(math.exp,   1); return val_float(exp(ARG_FLOAT(0)));  }
NATIVE(math_sqrt)  { CHECK_ARGC(math.sqrt,  1); return val_float(sqrt(ARG_FLOAT(0))); }
NATIVE(math_pi)    { (void)interp; (void)args; (void)argc; return val_float(3.14159265358979323846); }
NATIVE(math_e)     { (void)interp; (void)args; (void)argc; return val_float(2.71828182845904523536); }
NATIVE(math_clamp) {
    CHECK_ARGC(math.clamp, 3);
    double v  = ARG_FLOAT(0);
    double lo = ARG_FLOAT(1);
    double hi = ARG_FLOAT(2);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return val_float(v);
}
NATIVE(math_lerp) {
    CHECK_ARGC(math.lerp, 3);
    double a = ARG_FLOAT(0);
    double b = ARG_FLOAT(1);
    double t = ARG_FLOAT(2);
    return val_float(a + t * (b - a));
}

void modules_register_math(Env* env) {
    env_define(env, "sin",   val_native(mod_math_sin));
    env_define(env, "cos",   val_native(mod_math_cos));
    env_define(env, "tan",   val_native(mod_math_tan));
    env_define(env, "log",   val_native(mod_math_log));
    env_define(env, "log2",  val_native(mod_math_log2));
    env_define(env, "log10", val_native(mod_math_log10));
    env_define(env, "exp",   val_native(mod_math_exp));
    env_define(env, "sqrt",  val_native(mod_math_sqrt));
    env_define(env, "pi",    val_native(mod_math_pi));
    env_define(env, "e",     val_native(mod_math_e));
    env_define(env, "clamp", val_native(mod_math_clamp));
    env_define(env, "lerp",  val_native(mod_math_lerp));
}

/* =========================================================
 * random module
 * ========================================================= */

NATIVE(rng_random)  {
    (void)interp; (void)args; (void)argc;
    return val_float((double)rand() / ((double)RAND_MAX + 1.0));
}
NATIVE(rng_randint) {
    CHECK_ARGC(random.randint, 2);
    int lo = ARG_INT(0), hi = ARG_INT(1);
    if (hi <= lo) return val_int(lo);
    return val_int((int64_t)(lo + rand() % (hi - lo)));
}
NATIVE(rng_seed_rng) {
    CHECK_ARGC(random.seed, 1);
    srand((unsigned int)ARG_INT(0));
    spect_seed_rng((unsigned int)ARG_INT(0));
    return val_null();
}
NATIVE(rng_choice) {
    CHECK_ARGC(random.choice, 1);
    if (args[0]->type != VAL_ARRAY) {
        interp_error(interp, 0, "random.choice: arg must be array");
        return NULL;
    }
    SpectArray* arr = args[0]->as.array;
    if (arr->length == 0) return val_null();
    size_t idx = (size_t)(rand() % (int)arr->length);
    return val_spect(sarray_get(arr, idx));
}
NATIVE(rng_shuffle) {
    CHECK_ARGC(random.shuffle, 1);
    if (args[0]->type != VAL_ARRAY) {
        interp_error(interp, 0, "random.shuffle: arg must be array");
        return NULL;
    }
    SpectArray* arr = args[0]->as.array;
    /* Fisher-Yates */
    for (int i = (int)arr->length - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Specton tmp = sarray_get(arr, (size_t)i);
        sarray_set(arr, (size_t)i, sarray_get(arr, (size_t)j));
        sarray_set(arr, (size_t)j, tmp);
    }
    return val_null();
}

void modules_register_random(Env* env) {
    env_define(env, "random",   val_native(mod_rng_random));
    env_define(env, "randint",  val_native(mod_rng_randint));
    env_define(env, "rseed",    val_native(mod_rng_seed_rng));
    env_define(env, "choice",   val_native(mod_rng_choice));
    env_define(env, "shuffle",  val_native(mod_rng_shuffle));
}

/* =========================================================
 * io module
 * io.read(path)            → string (full file content)
 * io.write(path, content)  → null (overwrite)
 * io.append(path, content) → null
 * io.lines(path)           → list of strings
 * io.exists(path)          → bool
 * ========================================================= */

NATIVE(io_read) {
    CHECK_ARGC(io.read, 1);
    char* path = (args[0]->type == VAL_STR) ? args[0]->as.string : NULL;
    if (!path) { interp_error(interp, 0, "io.read: path must be string"); return NULL; }
    FILE* f = fopen(path, "r");
    if (!f) { interp_error(interp, 0, "io.read: cannot open '%s'", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); interp_error(interp, 0, "io.read: out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    return val_str_own(buf);
}

NATIVE(io_write) {
    CHECK_ARGC(io.write, 2);
    char* path = (args[0]->type == VAL_STR) ? args[0]->as.string : NULL;
    if (!path) { interp_error(interp, 0, "io.write: path must be string"); return NULL; }
    FILE* f = fopen(path, "w");
    if (!f) { interp_error(interp, 0, "io.write: cannot open '%s'", path); return NULL; }
    char* content = val_to_string(args[1]);
    fputs(content, f);
    free(content);
    fclose(f);
    return val_null();
}

NATIVE(io_append) {
    CHECK_ARGC(io.append, 2);
    char* path = (args[0]->type == VAL_STR) ? args[0]->as.string : NULL;
    if (!path) { interp_error(interp, 0, "io.append: path must be string"); return NULL; }
    FILE* f = fopen(path, "a");
    if (!f) { interp_error(interp, 0, "io.append: cannot open '%s'", path); return NULL; }
    char* content = val_to_string(args[1]);
    fputs(content, f);
    free(content);
    fclose(f);
    return val_null();
}

NATIVE(io_lines) {
    CHECK_ARGC(io.lines, 1);
    char* path = (args[0]->type == VAL_STR) ? args[0]->as.string : NULL;
    if (!path) { interp_error(interp, 0, "io.lines: path must be string"); return NULL; }
    FILE* f = fopen(path, "r");
    if (!f) { interp_error(interp, 0, "io.lines: cannot open '%s'", path); return NULL; }
    Value* lst = val_list_new();
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
        if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
        val_list_append(lst, val_str(buf));
    }
    fclose(f);
    return lst;
}

NATIVE(io_exists) {
    CHECK_ARGC(io.exists, 1);
    char* path = (args[0]->type == VAL_STR) ? args[0]->as.string : NULL;
    if (!path) return val_bool(0);
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return val_bool(1); }
    return val_bool(0);
}

void modules_register_io(Env* env) {
    env_define(env, "read",   val_native(mod_io_read));
    env_define(env, "write",  val_native(mod_io_write));
    env_define(env, "append", val_native(mod_io_append));
    env_define(env, "lines",  val_native(mod_io_lines));
    env_define(env, "exists", val_native(mod_io_exists));
}

/* Copy all local bindings from src env (not parent chain) into dst */
static void env_copy_to(Env* src, Env* dst) {
    for (int b = 0; b < ENV_BUCKETS; b++) {
        for (EnvEntry* e = src->buckets[b]; e; e = e->next)
            env_define(dst, e->key, e->val);
    }
}

/* =========================================================
 * modules_load — main entry point called by interpreter ND_IMPORT
 * ========================================================= */

int modules_load(Env* env, const char* module_name,
                 const char* names[], int name_count,
                 char error_msg[256]) {
    (void)names; (void)name_count;

    if (strcmp(module_name, "neural") == 0) {
        modules_register_neural(env);
        return 1;
    }
    if (strcmp(module_name, "math") == 0) {
        modules_register_math(env);
        return 1;
    }
    if (strcmp(module_name, "random") == 0) {
        modules_register_random(env);
        return 1;
    }
    if (strcmp(module_name, "io") == 0) {
        modules_register_io(env);
        return 1;
    }

    /* Try loading from file: <module_name>.sp in current directory */
    char path[512];
    snprintf(path, sizeof(path), "%s.sp", module_name);
    FILE* f = fopen(path, "r");
    if (!f) {
        /* Try ~/.spectra/lib/<module>.sp */
        const char* home = getenv("USERPROFILE");
        if (!home) home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.spectra/lib/%s.sp", home, module_name);
            f = fopen(path, "r");
        }
    }
    if (f) {
        /* Read entire file */
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char* src = (char*)malloc((size_t)sz + 1);
        if (!src) { fclose(f); snprintf(error_msg, 256, "out of memory loading '%s'", path); return 0; }
        size_t got = fread(src, 1, (size_t)sz, f);
        src[got] = '\0';
        fclose(f);

        Lexer* lex = lexer_new(src);
        free(src);
        if (!lexer_tokenize(lex)) {
            snprintf(error_msg, 256, "lex error in module '%s': %s", module_name, lex->error_msg);
            lexer_free(lex);
            return 0;
        }
        Parser* par = parser_new(lex->tokens, lex->tok_count);
        ASTNode* prog = parser_parse(par);
        parser_free(par);
        lexer_free(lex);
        if (!prog) {
            snprintf(error_msg, 256, "parse error in module '%s'", module_name);
            return 0;
        }
        Interpreter* sub = interp_new();
        interp_run(sub, prog);
        env_copy_to(sub->globals, env);
        interp_free(sub);
        node_free(prog);
        return 1;
    }

    snprintf(error_msg, 256, "unknown module '%s'", module_name);
    return 0;
}
