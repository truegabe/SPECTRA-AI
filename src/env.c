#include "env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================
 * Hash function (djb2 variant)
 * ========================================================= */

static unsigned int env_hash(const char* key) {
    unsigned int h = 5381;
    while (*key) h = ((h << 5) + h) ^ (unsigned char)*key++;
    return h % ENV_BUCKETS;
}

/* =========================================================
 * env_new
 * ========================================================= */

Env* env_new(Env* parent) {
    Env* e = (Env*)calloc(1, sizeof(Env));
    if (!e) {
        fprintf(stderr, "SPECTRA: out of memory in env_new\n");
        exit(1);
    }
    /* Buckets are already zeroed by calloc */
    e->parent    = parent;   /* NOT retained — avoids circular refs */
    e->ref_count = 1;
    return e;
}

/* =========================================================
 * env_retain
 * ========================================================= */

Env* env_retain(Env* e) {
    if (e) {
        e->ref_count++;
    }
    return e;
}

/* =========================================================
 * env_release
 * ========================================================= */

void env_release(Env* e) {
    if (!e) return;
    e->ref_count--;
    if (e->ref_count > 0) return;

    /* Free all bucket chains */
    for (int i = 0; i < ENV_BUCKETS; i++) {
        EnvEntry* entry = e->buckets[i];
        while (entry) {
            EnvEntry* next = entry->next;
            free(entry->key);
            val_release(entry->val);
            free(entry);
            entry = next;
        }
        e->buckets[i] = NULL;
    }

    free(e);
}

/* =========================================================
 * env_define
 * ========================================================= */

void env_define(Env* e, const char* name, Value* val) {
    unsigned int idx = env_hash(name);
    EnvEntry* entry = e->buckets[idx];

    /* Walk chain — update if already defined in this scope */
    while (entry) {
        if (strcmp(entry->key, name) == 0) {
            val_release(entry->val);
            entry->val = val_retain(val);
            return;
        }
        entry = entry->next;
    }

    /* Not found in this scope — create new entry */
    EnvEntry* new_entry = (EnvEntry*)malloc(sizeof(EnvEntry));
    if (!new_entry) {
        fprintf(stderr, "SPECTRA: out of memory in env_define\n");
        exit(1);
    }
    new_entry->key  = strdup(name);
    if (!new_entry->key) {
        fprintf(stderr, "SPECTRA: out of memory in env_define strdup\n");
        exit(1);
    }
    new_entry->val  = val_retain(val);
    new_entry->next = e->buckets[idx];  /* prepend to chain */
    e->buckets[idx] = new_entry;
}

/* =========================================================
 * env_get
 * ========================================================= */

Value* env_get(Env* e, const char* name) {
    if (!e) return NULL;

    unsigned int idx = env_hash(name);
    EnvEntry* entry = e->buckets[idx];

    while (entry) {
        if (strcmp(entry->key, name) == 0) {
            return entry->val;
        }
        entry = entry->next;
    }

    /* Not found in this scope — walk up to parent */
    if (e->parent) {
        return env_get(e->parent, name);
    }

    return NULL;
}

/* =========================================================
 * env_set
 * ========================================================= */

int env_set(Env* e, const char* name, Value* val) {
    if (!e) return 0;

    unsigned int idx = env_hash(name);
    EnvEntry* entry = e->buckets[idx];

    while (entry) {
        if (strcmp(entry->key, name) == 0) {
            val_release(entry->val);
            entry->val = val_retain(val);
            return 1;
        }
        entry = entry->next;
    }

    /* Not found in this scope — try parent */
    if (e->parent) {
        return env_set(e->parent, name, val);
    }

    return 0;  /* never found */
}

/* =========================================================
 * env_dump
 * ========================================================= */

void env_dump(Env* e) {
    if (!e) {
        printf("[env: NULL]\n");
        return;
    }
    printf("[env: ref_count=%d, parent=%s]\n",
           e->ref_count,
           e->parent ? "(exists)" : "NULL");

    for (int i = 0; i < ENV_BUCKETS; i++) {
        EnvEntry* entry = e->buckets[i];
        while (entry) {
            printf("  [%2d] %s = ", i, entry->key);
            val_print(entry->val);
            printf("\n");
            entry = entry->next;
        }
    }
}
