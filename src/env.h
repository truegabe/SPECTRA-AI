#ifndef SPECTRA_ENV_H
#define SPECTRA_ENV_H
#include "value.h"

#define ENV_BUCKETS 64

typedef struct EnvEntry {
    char*            key;    /* heap-allocated */
    Value*           val;    /* retained */
    struct EnvEntry* next;
} EnvEntry;

typedef struct Env {
    EnvEntry*    buckets[ENV_BUCKETS];
    struct Env*  parent;    /* enclosing scope (NOT retained) */
    int          ref_count;
} Env;

Env*   env_new(Env* parent);       /* create new scope, ref_count=1 */
Env*   env_retain(Env* e);         /* increment ref_count, return e */
void   env_release(Env* e);        /* decrement; free if 0 */

void   env_define(Env* e, const char* name, Value* val);  /* define in this scope */
Value* env_get(Env* e, const char* name);    /* walk up chain, NULL if not found */
int    env_set(Env* e, const char* name, Value* val);  /* set existing (walks up), 0 if not found */

void   env_dump(Env* e);   /* debug: print all bindings in this scope */

#endif /* SPECTRA_ENV_H */
