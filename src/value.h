#ifndef SPECTRA_VALUE_H
#define SPECTRA_VALUE_H
#include "ast.h"
#include <stdio.h>

/* Forward declarations */
typedef struct Value Value;
typedef struct Env   Env;
typedef struct Interpreter Interpreter;

/* Native C function signature */
typedef Value* (*NativeFn)(Interpreter* interp, Value** args, int argc);

typedef enum {
    VAL_SPECT = 0,
    VAL_INT,
    VAL_FLOAT,
    VAL_STR,
    VAL_BOOL,
    VAL_NULL,
    VAL_ARRAY,           /* SpectArray*   */
    VAL_MATRIX,          /* SpectMatrix*  */
    VAL_LIST,             /* dynamic array of Value* — Python-style list */
    VAL_MAP,              /* string-keyed hash map */
    VAL_FUNC,            /* user-defined  */
    VAL_NATIVE,          /* built-in C fn */
    VAL_STRUCT_INSTANCE,
    VAL_USERDATA,         /* opaque pointer with tag and destructor */
    VAL_TYPE_COUNT
} ValType;

typedef struct SpectList {
    Value** items;
    int     length;
    int     capacity;
} SpectList;

typedef struct SpectMapEntry {
    char*  key;    /* heap-allocated, owned */
    Value* val;    /* retained */
} SpectMapEntry;

typedef struct SpectMap {
    SpectMapEntry* entries;
    int            count;
    int            capacity;
} SpectMap;

struct Value {
    ValType type;
    int     ref_count;
    union {
        Specton      spect;
        int64_t      integer;
        double       number;
        char*        string;      /* heap-allocated, owned */
        int          boolean;
        SpectArray*  array;       /* owned */
        SpectMatrix* matrix;      /* owned */
        SpectList*   list;        /* VAL_LIST, owned */
        SpectMap*    map;         /* VAL_MAP, owned */
        NativeFn     native;
        struct {
            char     name[64];
            char**   param_names;  /* heap-allocated */
            int      param_count;
            ASTNode* body;         /* borrowed — ND_FUNC_DEF node */
            Env*     closure;      /* retained */
            int      is_method;
        } func;
        struct {
            char     type_name[64];
            char**   field_names;   /* heap-allocated */
            Value**  fields;        /* heap-allocated, each retained */
            int      field_count;
            Env*     methods;       /* retained */
        } instance;
        struct {
            void*  ptr;             /* opaque heap pointer */
            char   tag[32];         /* type name, e.g. "NNNetwork" */
            void (*free_fn)(void*); /* destructor, may be NULL */
        } userdata;
    } as;
};

/* ── Constructors (ref_count starts at 1) ── */
Value* val_spect(Specton s);
Value* val_int(int64_t n);
Value* val_float(double f);
Value* val_str(const char* s);         /* copies s */
Value* val_str_own(char* s);           /* takes ownership of s */
Value* val_bool(int b);
Value* val_null(void);
Value* val_array(SpectArray* arr);     /* takes ownership */
Value* val_matrix(SpectMatrix* mat);   /* takes ownership */
Value* val_list_new(void);
Value* val_map_new(void);
Value* val_native(NativeFn fn);
Value* val_userdata(void* ptr, const char* tag, void (*free_fn)(void*));
Value* val_func(const char* name, char** params, int nparams, ASTNode* body, Env* closure, int is_method);

/* ── List operations ── */
void   val_list_append(Value* list, Value* item);
Value* val_list_get(Value* list, int idx);    /* NULL if out of range */
void   val_list_set(Value* list, int idx, Value* item);
void   val_list_insert(Value* list, int idx, Value* item);
Value* val_list_pop(Value* list);              /* pop from end; caller owns result */
Value* val_list_pop_at(Value* list, int idx);  /* pop at index; caller owns result */
int    val_list_contains(Value* list, Value* item);
void   val_list_extend(Value* dest, Value* src);
void   val_list_reverse(Value* list);

/* ── Map operations ── */
void   val_map_set(Value* map, const char* key, Value* val);
Value* val_map_get(Value* map, const char* key);   /* NULL if missing; borrowed ref */
int    val_map_has(Value* map, const char* key);
void   val_map_delete(Value* map, const char* key);
Value* val_map_keys(Value* map);    /* returns VAL_LIST of VAL_STR keys */
Value* val_map_values(Value* map);  /* returns VAL_LIST of values (retained) */
int    val_map_len(Value* map);

/* ── Reference counting ── */
Value* val_retain(Value* v);    /* increment ref_count, return v */
void   val_release(Value* v);   /* decrement; free if 0 */

/* ── Utilities ── */
void   val_print(Value* v);             /* print without newline */
void   val_println(Value* v);           /* print with newline */
char*  val_to_string(Value* v);         /* heap-allocated string, caller frees */
int    val_truthy(Value* v);            /* 0 = falsy */
int    val_equal(Value* a, Value* b);   /* deep equality */
const char* val_type_name(ValType t);

#endif /* SPECTRA_VALUE_H */
