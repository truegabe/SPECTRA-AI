#include "memory.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* =========================================================
 * Reference-counted allocator
 * ========================================================= */

void* rc_alloc(size_t size, void (*destructor)(void*))
{
    /*
     * Allocate enough for the RefCounted header plus the user data.
     * Because data[] is a flexible array member the header already
     * contributes offsetof(RefCounted, data) bytes, so we just add
     * the requested payload size.
     */
    RefCounted* hdr = (RefCounted*)malloc(sizeof(RefCounted) + size);
    if (!hdr) {
        fprintf(stderr, "rc_alloc: malloc failed (size=%zu)\n", size);
        return NULL;
    }

    hdr->ref_count  = 1;
    hdr->size       = size;
    hdr->destructor = destructor;

    /* Zero-initialise the user data region for safety */
    memset(hdr->data, 0, size);

    /* Return a pointer to the data area, not to the header */
    return (void*)hdr->data;
}

void* rc_retain(void* ptr)
{
    if (!ptr) {
        fprintf(stderr, "rc_retain: NULL pointer\n");
        return NULL;
    }
    RefCounted* hdr = rc_header(ptr);
    hdr->ref_count++;
    return ptr;
}

void rc_release(void* ptr)
{
    if (!ptr) {
        fprintf(stderr, "rc_release: NULL pointer\n");
        return;
    }
    RefCounted* hdr = rc_header(ptr);

    if (hdr->ref_count == 0) {
        fprintf(stderr, "rc_release: ref_count already 0 (double-free?)\n");
        return;
    }

    hdr->ref_count--;

    if (hdr->ref_count == 0) {
        /* Call the destructor on the data pointer (not on the header) */
        if (hdr->destructor) {
            hdr->destructor(ptr);
        }
        free(hdr);
    }
}

size_t rc_refcount(void* ptr)
{
    if (!ptr) {
        fprintf(stderr, "rc_refcount: NULL pointer\n");
        return 0;
    }
    return rc_header(ptr)->ref_count;
}


/* =========================================================
 * Arena allocator
 * ========================================================= */

/* Round `n` up to the next multiple of 8 (8-byte alignment) */
static size_t align8(size_t n)
{
    return (n + 7u) & ~(size_t)7u;
}

Arena* arena_create(size_t capacity)
{
    Arena* a = (Arena*)malloc(sizeof(Arena));
    if (!a) {
        fprintf(stderr, "arena_create: malloc failed for Arena struct\n");
        return NULL;
    }

    a->buf = (uint8_t*)malloc(capacity);
    if (!a->buf) {
        fprintf(stderr, "arena_create: malloc failed for buffer (capacity=%zu)\n",
                capacity);
        free(a);
        return NULL;
    }

    a->capacity = capacity;
    a->used     = 0;
    return a;
}

void* arena_push(Arena* a, size_t size)
{
    if (!a) {
        fprintf(stderr, "arena_push: NULL arena\n");
        return NULL;
    }

    size_t aligned = align8(size);

    if (a->used + aligned > a->capacity) {
        fprintf(stderr,
                "arena_push: out of space (used=%zu, aligned_request=%zu, capacity=%zu)\n",
                a->used, aligned, a->capacity);
        return NULL;
    }

    void* ptr = a->buf + a->used;
    a->used  += aligned;
    return ptr;
}

void arena_reset(Arena* a)
{
    if (!a) {
        fprintf(stderr, "arena_reset: NULL arena\n");
        return;
    }
    a->used = 0;
}

void arena_destroy(Arena* a)
{
    if (!a) return;
    free(a->buf);
    free(a);
}


/* =========================================================
 * Pool allocator
 * ========================================================= */

Pool* pool_create(size_t block_size, size_t count)
{
    if (count == 0) {
        fprintf(stderr, "pool_create: count must be > 0\n");
        return NULL;
    }

    /* Each block must be at least large enough to hold a PoolBlock pointer
     * so we can use it as a free-list node while it is free. */
    if (block_size < sizeof(PoolBlock)) {
        block_size = sizeof(PoolBlock);
    }

    Pool* p = (Pool*)malloc(sizeof(Pool));
    if (!p) {
        fprintf(stderr, "pool_create: malloc failed for Pool struct\n");
        return NULL;
    }

    p->buf = (uint8_t*)malloc(block_size * count);
    if (!p->buf) {
        fprintf(stderr,
                "pool_create: malloc failed for pool buffer (block_size=%zu, count=%zu)\n",
                block_size, count);
        free(p);
        return NULL;
    }

    p->block_size = block_size;
    p->capacity   = count;

    /*
     * Build the free list by walking through the buffer and linking each
     * block to the next one.  The last block's next pointer is NULL.
     */
    p->free_list = NULL;
    /*
     * Walk backwards so that block 0 ends up at the head of the list
     * (first allocation returns block 0).
     */
    for (size_t i = count; i-- > 0; ) {
        PoolBlock* blk = (PoolBlock*)(p->buf + i * block_size);
        blk->next    = p->free_list;
        p->free_list = blk;
    }

    return p;
}

void* pool_get(Pool* p)
{
    if (!p) {
        fprintf(stderr, "pool_get: NULL pool\n");
        return NULL;
    }
    if (!p->free_list) {
        fprintf(stderr, "pool_get: pool exhausted\n");
        return NULL;
    }

    PoolBlock* blk = p->free_list;
    p->free_list   = blk->next;

    /* Clear the next pointer that was overlaid while the block was free */
    blk->next = NULL;

    return (void*)blk;
}

void pool_put(Pool* p, void* block)
{
    if (!p) {
        fprintf(stderr, "pool_put: NULL pool\n");
        return;
    }
    if (!block) {
        fprintf(stderr, "pool_put: NULL block\n");
        return;
    }

    PoolBlock* blk = (PoolBlock*)block;
    blk->next    = p->free_list;
    p->free_list = blk;
}

void pool_destroy(Pool* p)
{
    if (!p) return;
    free(p->buf);
    free(p);
}
