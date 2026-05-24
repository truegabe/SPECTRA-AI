#include "value.h"
#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* =========================================================
 * Internal helpers
 * ========================================================= */

static Value* val_alloc(ValType t) {
    Value* v = (Value*)malloc(sizeof(Value));
    if (!v) {
        fprintf(stderr, "SPECTRA: out of memory in val_alloc\n");
        exit(1);
    }
    v->type      = t;
    v->ref_count = 1;
    return v;
}

/* Forward declaration so val_release can call it */
static void val_free(Value* v);

/* =========================================================
 * Constructors
 * ========================================================= */

Value* val_spect(Specton s) {
    Value* v = val_alloc(VAL_SPECT);
    v->as.spect = s;
    return v;
}

Value* val_int(int64_t n) {
    Value* v = val_alloc(VAL_INT);
    v->as.integer = n;
    return v;
}

Value* val_float(double f) {
    Value* v = val_alloc(VAL_FLOAT);
    v->as.number = f;
    return v;
}

Value* val_str(const char* s) {
    Value* v = val_alloc(VAL_STR);
    v->as.string = strdup(s ? s : "");
    if (!v->as.string) {
        fprintf(stderr, "SPECTRA: out of memory in val_str\n");
        exit(1);
    }
    return v;
}

Value* val_str_own(char* s) {
    Value* v = val_alloc(VAL_STR);
    v->as.string = s;
    return v;
}

Value* val_bool(int b) {
    Value* v = val_alloc(VAL_BOOL);
    v->as.boolean = b ? 1 : 0;
    return v;
}

Value* val_null(void) {
    Value* v = val_alloc(VAL_NULL);
    return v;
}

Value* val_array(SpectArray* arr) {
    Value* v = val_alloc(VAL_ARRAY);
    v->as.array = arr;
    return v;
}

Value* val_matrix(SpectMatrix* mat) {
    Value* v = val_alloc(VAL_MATRIX);
    v->as.matrix = mat;
    return v;
}

Value* val_list_new(void) {
    Value* v = (Value*)calloc(1, sizeof(Value));
    v->type = VAL_LIST;
    v->ref_count = 1;
    SpectList* lst = (SpectList*)calloc(1, sizeof(SpectList));
    lst->capacity = 8;
    lst->items = (Value**)malloc(8 * sizeof(Value*));
    lst->length = 0;
    v->as.list = lst;
    return v;
}

Value* val_map_new(void) {
    Value* v = (Value*)calloc(1, sizeof(Value));
    v->type = VAL_MAP;
    v->ref_count = 1;
    SpectMap* m = (SpectMap*)calloc(1, sizeof(SpectMap));
    m->capacity = 8;
    m->entries = (SpectMapEntry*)calloc(8, sizeof(SpectMapEntry));
    m->count = 0;
    v->as.map = m;
    return v;
}

Value* val_native(NativeFn fn) {
    Value* v = val_alloc(VAL_NATIVE);
    v->as.native = fn;
    return v;
}

Value* val_userdata(void* ptr, const char* tag, void (*free_fn)(void*)) {
    Value* v = val_alloc(VAL_USERDATA);
    v->as.userdata.ptr     = ptr;
    v->as.userdata.free_fn = free_fn;
    strncpy(v->as.userdata.tag, tag ? tag : "userdata", 31);
    v->as.userdata.tag[31] = '\0';
    return v;
}

Value* val_func(const char* name, char** params, int nparams,
                ASTNode* body, Env* closure, int is_method) {
    Value* v = val_alloc(VAL_FUNC);

    /* Copy name (truncate safely) */
    strncpy(v->as.func.name, name ? name : "", 63);
    v->as.func.name[63] = '\0';

    /* Copy param names */
    v->as.func.param_count = nparams;
    if (nparams > 0 && params) {
        v->as.func.param_names = (char**)malloc(sizeof(char*) * (size_t)nparams);
        if (!v->as.func.param_names) {
            fprintf(stderr, "SPECTRA: out of memory in val_func\n");
            exit(1);
        }
        for (int i = 0; i < nparams; i++) {
            v->as.func.param_names[i] = strdup(params[i] ? params[i] : "");
            if (!v->as.func.param_names[i]) {
                fprintf(stderr, "SPECTRA: out of memory in val_func param\n");
                exit(1);
            }
        }
    } else {
        v->as.func.param_names = NULL;
    }

    v->as.func.body      = body;    /* borrowed — not freed by us */
    v->as.func.closure   = env_retain(closure);
    v->as.func.is_method = is_method;

    return v;
}

/* =========================================================
 * Reference counting
 * ========================================================= */

Value* val_retain(Value* v) {
    if (v) {
        v->ref_count++;
    }
    return v;
}

void val_release(Value* v) {
    if (!v) return;
    v->ref_count--;
    if (v->ref_count <= 0) {
        val_free(v);
    }
}

static void val_free(Value* v) {
    if (!v) return;

    switch (v->type) {
        case VAL_STR:
            free(v->as.string);
            break;

        case VAL_ARRAY:
            if (v->as.array) {
                sarray_free(v->as.array);
            }
            break;

        case VAL_MATRIX:
            if (v->as.matrix) {
                smat_free(v->as.matrix);
            }
            break;

        case VAL_FUNC:
            if (v->as.func.param_names) {
                for (int i = 0; i < v->as.func.param_count; i++) {
                    free(v->as.func.param_names[i]);
                }
                free(v->as.func.param_names);
            }
            /* body is borrowed — do not free */
            if (v->as.func.closure) {
                env_release(v->as.func.closure);
            }
            break;

        case VAL_STRUCT_INSTANCE:
            if (v->as.instance.field_names) {
                for (int i = 0; i < v->as.instance.field_count; i++) {
                    free(v->as.instance.field_names[i]);
                }
                free(v->as.instance.field_names);
            }
            if (v->as.instance.fields) {
                for (int i = 0; i < v->as.instance.field_count; i++) {
                    val_release(v->as.instance.fields[i]);
                }
                free(v->as.instance.fields);
            }
            if (v->as.instance.methods) {
                env_release(v->as.instance.methods);
            }
            break;

        case VAL_USERDATA:
            if (v->as.userdata.ptr && v->as.userdata.free_fn)
                v->as.userdata.free_fn(v->as.userdata.ptr);
            break;

        case VAL_LIST: {
            SpectList* lst = v->as.list;
            for (int i = 0; i < lst->length; i++)
                val_release(lst->items[i]);
            free(lst->items);
            free(lst);
            break;
        }
        case VAL_MAP: {
            SpectMap* m = v->as.map;
            for (int i = 0; i < m->count; i++) {
                free(m->entries[i].key);
                val_release(m->entries[i].val);
            }
            free(m->entries);
            free(m);
            break;
        }

        /* Types with no owned heap data */
        case VAL_SPECT:
        case VAL_INT:
        case VAL_FLOAT:
        case VAL_BOOL:
        case VAL_NULL:
        case VAL_NATIVE:
        case VAL_TYPE_COUNT:
        default:
            break;
    }

    free(v);
}

/* =========================================================
 * val_print
 * ========================================================= */

void val_print(Value* v) {
    if (!v) {
        printf("(null-ptr)");
        return;
    }
    switch (v->type) {
        case VAL_SPECT:
            spect_print(v->as.spect);
            break;
        case VAL_INT:
            printf("%" PRId64, v->as.integer);
            break;
        case VAL_FLOAT:
            printf("%g", v->as.number);
            break;
        case VAL_STR:
            printf("%s", v->as.string);
            break;
        case VAL_BOOL:
            printf("%s", v->as.boolean ? "true" : "false");
            break;
        case VAL_NULL:
            printf("null");
            break;
        case VAL_ARRAY:
            printf("[SpectArray len=%zu]",
                   v->as.array ? v->as.array->length : (size_t)0);
            break;
        case VAL_MATRIX:
            printf("[SpectMatrix %zu\xc3\x97%zu]",
                   v->as.matrix ? v->as.matrix->rows : (size_t)0,
                   v->as.matrix ? v->as.matrix->cols : (size_t)0);
            break;
        case VAL_FUNC:
            printf("<func %s>", v->as.func.name);
            break;
        case VAL_NATIVE:
            printf("<native fn>");
            break;
        case VAL_STRUCT_INSTANCE:
            printf("<%s>", v->as.instance.type_name);
            break;
        case VAL_USERDATA:
            printf("<%s>", v->as.userdata.tag);
            break;
        case VAL_LIST: {
            SpectList* lst = v->as.list;
            printf("[");
            for (int i = 0; i < lst->length; i++) {
                if (i > 0) printf(", ");
                val_print(lst->items[i]);
            }
            printf("]");
            break;
        }
        case VAL_MAP: {
            SpectMap* m = v->as.map;
            printf("{");
            int first = 1;
            for (int i = 0; i < m->count; i++) {
                if (!m->entries[i].key) continue;
                if (!first) printf(", ");
                first = 0;
                printf("%s: ", m->entries[i].key);
                val_print(m->entries[i].val);
            }
            printf("}");
            break;
        }
        default:
            printf("<unknown value>");
            break;
    }
}

void val_println(Value* v) {
    val_print(v);
    printf("\n");
}

/* =========================================================
 * val_to_string
 * ========================================================= */

char* val_to_string(Value* v) {
    if (!v) {
        return strdup("(null-ptr)");
    }
    switch (v->type) {
        case VAL_SPECT:
            return spect_to_string(v->as.spect);

        case VAL_INT: {
            /* PRId64 can be at most 20 digits + sign + NUL */
            char* buf = (char*)malloc(32);
            if (buf) snprintf(buf, 32, "%" PRId64, v->as.integer);
            return buf;
        }

        case VAL_FLOAT: {
            char* buf = (char*)malloc(64);
            if (buf) snprintf(buf, 64, "%g", v->as.number);
            return buf;
        }

        case VAL_STR:
            return strdup(v->as.string);

        case VAL_BOOL:
            return strdup(v->as.boolean ? "true" : "false");

        case VAL_NULL:
            return strdup("null");

        case VAL_ARRAY: {
            char* buf = (char*)malloc(64);
            if (buf) snprintf(buf, 64, "[SpectArray len=%zu]",
                              v->as.array ? v->as.array->length : (size_t)0);
            return buf;
        }

        case VAL_MATRIX: {
            char* buf = (char*)malloc(64);
            if (buf) snprintf(buf, 64, "[SpectMatrix %zu\xc3\x97%zu]",
                              v->as.matrix ? v->as.matrix->rows : (size_t)0,
                              v->as.matrix ? v->as.matrix->cols : (size_t)0);
            return buf;
        }

        case VAL_FUNC: {
            char* buf = (char*)malloc(128);
            if (buf) snprintf(buf, 128, "<func %s>", v->as.func.name);
            return buf;
        }

        case VAL_NATIVE:
            return strdup("<native fn>");

        case VAL_STRUCT_INSTANCE: {
            char* buf = (char*)malloc(128);
            if (buf) snprintf(buf, 128, "<%s>", v->as.instance.type_name);
            return buf;
        }

        case VAL_USERDATA: {
            char* buf = (char*)malloc(64);
            if (buf) snprintf(buf, 64, "<%s>", v->as.userdata.tag);
            return buf;
        }

        case VAL_LIST: {
            SpectList* lst = v->as.list;
            /* Build "[a, b, c]" using a growable buffer */
            size_t cap = 64;
            char* buf = (char*)malloc(cap);
            size_t len = 0;
            buf[len++] = '[';
            for (int i = 0; i < lst->length; i++) {
                if (i > 0) {
                    if (len + 2 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                    buf[len++] = ','; buf[len++] = ' ';
                }
                char* item = val_to_string(lst->items[i]);
                size_t item_len = strlen(item);
                while (len + item_len + 2 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                memcpy(buf + len, item, item_len);
                len += item_len;
                free(item);
            }
            if (len + 2 >= cap) { cap += 4; buf = (char*)realloc(buf, cap); }
            buf[len++] = ']';
            buf[len] = '\0';
            return buf;
        }

        case VAL_MAP: {
            SpectMap* m = v->as.map;
            size_t cap = 64;
            char* buf = (char*)malloc(cap);
            size_t len = 0;
            buf[len++] = '{';
            int first = 1;
            for (int i = 0; i < m->count; i++) {
                if (!m->entries[i].key) continue;
                if (!first) {
                    if (len + 2 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                    buf[len++] = ','; buf[len++] = ' ';
                }
                first = 0;
                size_t klen = strlen(m->entries[i].key);
                while (len + klen + 3 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                memcpy(buf + len, m->entries[i].key, klen);
                len += klen;
                buf[len++] = ':'; buf[len++] = ' ';
                char* vstr = val_to_string(m->entries[i].val);
                size_t vlen = strlen(vstr);
                while (len + vlen + 2 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                memcpy(buf + len, vstr, vlen);
                len += vlen;
                free(vstr);
            }
            if (len + 2 >= cap) { cap += 4; buf = (char*)realloc(buf, cap); }
            buf[len++] = '}';
            buf[len] = '\0';
            return buf;
        }

        default:
            return strdup("<unknown value>");
    }
}

/* =========================================================
 * val_truthy
 * ========================================================= */

int val_truthy(Value* v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_NULL:  return 0;
        case VAL_BOOL:  return v->as.boolean;
        case VAL_INT:   return v->as.integer != 0;
        case VAL_FLOAT: return v->as.number  != 0.0;
        case VAL_STR:   return v->as.string && strlen(v->as.string) > 0;
        case VAL_SPECT: return spect_collapse(v->as.spect) != 0;
        default:        return 1;
    }
}

/* =========================================================
 * val_equal
 * ========================================================= */

int val_equal(Value* a, Value* b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;

    /* Allow int == float and float == int numeric comparison */
    if (a->type == VAL_INT && b->type == VAL_FLOAT) {
        return (double)a->as.integer == b->as.number;
    }
    if (a->type == VAL_FLOAT && b->type == VAL_INT) {
        return a->as.number == (double)b->as.integer;
    }

    if (a->type != b->type) return 0;

    switch (a->type) {
        case VAL_INT:    return a->as.integer == b->as.integer;
        case VAL_FLOAT:  return a->as.number  == b->as.number;
        case VAL_STR:    return strcmp(a->as.string, b->as.string) == 0;
        case VAL_BOOL:   return a->as.boolean == b->as.boolean;
        case VAL_NULL:   return 1;  /* null == null */
        case VAL_SPECT:  return spect_eq(a->as.spect, b->as.spect);
        case VAL_LIST: {
            if (b->type != VAL_LIST) return 0;
            if (a->as.list->length != b->as.list->length) return 0;
            for (int i = 0; i < (int)a->as.list->length; i++)
                if (!val_equal(a->as.list->items[i], b->as.list->items[i])) return 0;
            return 1;
        }
        case VAL_MAP:
            return (a == b);  /* identity equality for maps */
        /* For all complex types, fall back to pointer equality */
        default:         return a == b;
    }
}

/* =========================================================
 * val_type_name
 * ========================================================= */

const char* val_type_name(ValType t) {
    switch (t) {
        case VAL_SPECT:           return "spect";
        case VAL_INT:             return "int";
        case VAL_FLOAT:           return "float";
        case VAL_STR:             return "str";
        case VAL_BOOL:            return "bool";
        case VAL_NULL:            return "null";
        case VAL_ARRAY:           return "array";
        case VAL_MATRIX:          return "matrix";
        case VAL_LIST:            return "list";
        case VAL_MAP:             return "map";
        case VAL_FUNC:            return "func";
        case VAL_NATIVE:          return "native";
        case VAL_STRUCT_INSTANCE: return "struct_instance";
        case VAL_USERDATA:        return "userdata";
        default:                  return "<unknown-type>";
    }
}

/* =========================================================
 * List operations
 * ========================================================= */

void val_list_append(Value* list, Value* item) {
    SpectList* lst = list->as.list;
    if (lst->length >= lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->items = (Value**)realloc(lst->items, (size_t)lst->capacity * sizeof(Value*));
    }
    lst->items[lst->length++] = val_retain(item);
}

Value* val_list_get(Value* list, int idx) {
    SpectList* lst = list->as.list;
    if (idx < 0) idx = lst->length + idx;
    if (idx < 0 || idx >= lst->length) return NULL;
    return lst->items[idx];
}

void val_list_set(Value* list, int idx, Value* item) {
    SpectList* lst = list->as.list;
    if (idx < 0 || idx >= lst->length) return;
    val_release(lst->items[idx]);
    lst->items[idx] = val_retain(item);
}

void val_list_insert(Value* list, int idx, Value* item) {
    SpectList* lst = list->as.list;
    if (idx < 0) idx = 0;
    if (idx > lst->length) idx = lst->length;
    if (lst->length >= lst->capacity) {
        lst->capacity = lst->capacity * 2;
        lst->items = (Value**)realloc(lst->items, (size_t)lst->capacity * sizeof(Value*));
    }
    memmove(&lst->items[idx + 1], &lst->items[idx], (size_t)(lst->length - idx) * sizeof(Value*));
    lst->items[idx] = val_retain(item);
    lst->length++;
}

Value* val_list_pop(Value* list) {
    SpectList* lst = list->as.list;
    if (lst->length == 0) return val_null();
    Value* v = lst->items[--lst->length]; /* caller owns this ref */
    return v;
}

Value* val_list_pop_at(Value* list, int idx) {
    SpectList* lst = list->as.list;
    if (idx < 0 || idx >= lst->length) return val_null();
    Value* v = lst->items[idx];
    memmove(&lst->items[idx], &lst->items[idx + 1], (size_t)(lst->length - idx - 1) * sizeof(Value*));
    lst->length--;
    return v; /* caller owns */
}

int val_list_contains(Value* list, Value* item) {
    SpectList* lst = list->as.list;
    for (int i = 0; i < lst->length; i++)
        if (val_equal(lst->items[i], item)) return 1;
    return 0;
}

void val_list_extend(Value* dest, Value* src) {
    SpectList* s = src->as.list;
    for (int i = 0; i < s->length; i++)
        val_list_append(dest, s->items[i]);
}

void val_list_reverse(Value* list) {
    SpectList* lst = list->as.list;
    for (int i = 0, j = lst->length - 1; i < j; i++, j--) {
        Value* tmp = lst->items[i];
        lst->items[i] = lst->items[j];
        lst->items[j] = tmp;
    }
}

/* =========================================================
 * Map operations
 * ========================================================= */

void val_map_set(Value* map, const char* key, Value* val) {
    SpectMap* m = map->as.map;
    /* update existing */
    for (int i = 0; i < m->count; i++) {
        if (m->entries[i].key && strcmp(m->entries[i].key, key) == 0) {
            val_release(m->entries[i].val);
            m->entries[i].val = val_retain(val);
            return;
        }
    }
    /* insert new */
    if (m->count >= m->capacity) {
        m->capacity *= 2;
        m->entries = (SpectMapEntry*)realloc(m->entries, (size_t)m->capacity * sizeof(SpectMapEntry));
    }
    m->entries[m->count].key = (char*)malloc(strlen(key) + 1);
    strcpy(m->entries[m->count].key, key);
    m->entries[m->count].val = val_retain(val);
    m->count++;
}

Value* val_map_get(Value* map, const char* key) {
    SpectMap* m = map->as.map;
    for (int i = 0; i < m->count; i++)
        if (m->entries[i].key && strcmp(m->entries[i].key, key) == 0)
            return m->entries[i].val;
    return NULL;
}

int val_map_has(Value* map, const char* key) {
    return val_map_get(map, key) != NULL;
}

void val_map_delete(Value* map, const char* key) {
    SpectMap* m = map->as.map;
    for (int i = 0; i < m->count; i++) {
        if (m->entries[i].key && strcmp(m->entries[i].key, key) == 0) {
            free(m->entries[i].key);
            val_release(m->entries[i].val);
            /* shift remaining entries down */
            memmove(&m->entries[i], &m->entries[i+1], (size_t)(m->count - i - 1) * sizeof(SpectMapEntry));
            m->count--;
            return;
        }
    }
}

Value* val_map_keys(Value* map) {
    SpectMap* m = map->as.map;
    Value* lst = val_list_new();
    for (int i = 0; i < m->count; i++) {
        if (!m->entries[i].key) continue;
        Value* k = val_str(m->entries[i].key);
        val_list_append(lst, k);
        val_release(k);
    }
    return lst;
}

Value* val_map_values(Value* map) {
    SpectMap* m = map->as.map;
    Value* lst = val_list_new();
    for (int i = 0; i < m->count; i++) {
        if (!m->entries[i].key) continue;
        val_list_append(lst, m->entries[i].val);
    }
    return lst;
}

int val_map_len(Value* map) {
    return map->as.map->count;
}
