#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* =========================================================
 * Reference-counted allocator
 * =========================================================
 *
 * Layout in memory:
 *   [ RefCounted header | ... user data ... ]
 *                         ^
 *                         rc_alloc returns a pointer here (to data[])
 *
 * Use rc_header(ptr) to recover the header from a data pointer.
 */

typedef struct RefCounted {
    size_t   ref_count;
    size_t   size;
    void   (*destructor)(void*);
    uint8_t  data[];   /* flexible array member — actual user data follows */
} RefCounted;

/* Recover the RefCounted header from a user-data pointer */
#define rc_header(ptr) \
    ((RefCounted*)((uint8_t*)(ptr) - offsetof(RefCounted, data)))

/*
 * rc_alloc — allocate a reference-counted block of `size` bytes.
 *   - ref_count initialised to 1.
 *   - destructor (may be NULL) is called with the data pointer when
 *     ref_count reaches 0 before the memory is freed.
 *   - Returns a pointer to the data area (NOT to the RefCounted header).
 */
void* rc_alloc(size_t size, void (*destructor)(void*));

/*
 * rc_retain — increment ref_count by 1.
 *   - ptr must have been obtained from rc_alloc or rc_retain.
 *   - Returns ptr for convenience (allows chaining).
 */
void* rc_retain(void* ptr);

/*
 * rc_release — decrement ref_count by 1.
 *   - If ref_count reaches 0: calls destructor(ptr) if set, then frees
 *     the entire RefCounted block.
 *   - ptr must not be used after rc_release if it was the last reference.
 */
void rc_release(void* ptr);

/*
 * rc_refcount — return current reference count (for diagnostics).
 */
size_t rc_refcount(void* ptr);


/* =========================================================
 * Arena allocator (bump / linear allocator)
 * =========================================================
 *
 * All allocations are bump-allocated from a single pre-allocated buffer.
 * Individual allocations cannot be freed; call arena_reset() to reclaim
 * the entire buffer at once.
 */

typedef struct {
    uint8_t* buf;
    size_t   capacity;
    size_t   used;
} Arena;

/*
 * arena_create — allocate an arena with the given capacity (bytes).
 *   Returns NULL on allocation failure.
 */
Arena* arena_create(size_t capacity);

/*
 * arena_push — bump-allocate `size` bytes (aligned to 8 bytes).
 *   Returns NULL if not enough space remains.
 */
void* arena_push(Arena* a, size_t size);

/*
 * arena_reset — reset the used counter to 0.
 *   The buffer is reused from the beginning; previously allocated
 *   pointers become invalid.
 */
void arena_reset(Arena* a);

/*
 * arena_destroy — free the underlying buffer and the Arena struct itself.
 */
void arena_destroy(Arena* a);


/* =========================================================
 * Pool allocator (fixed-size object pool)
 * =========================================================
 *
 * A slab of memory is divided into equal-sized blocks.  Free blocks are
 * linked together in a singly-linked free list using the PoolBlock
 * overlay (the first sizeof(PoolBlock*) bytes of each block are
 * overwritten while the block is free).
 *
 * All blocks must be >= sizeof(PoolBlock) bytes.
 */

typedef struct PoolBlock { struct PoolBlock* next; } PoolBlock;

typedef struct {
    uint8_t*   buf;
    PoolBlock* free_list;
    size_t     block_size;
    size_t     capacity;   /* total number of blocks */
} Pool;

/*
 * pool_create — pre-allocate a pool of `count` blocks, each `block_size`
 *   bytes.  block_size will be rounded up to at least sizeof(PoolBlock*).
 *   Returns NULL on allocation failure.
 */
Pool* pool_create(size_t block_size, size_t count);

/*
 * pool_get — pop a free block from the pool.
 *   Returns NULL if the pool is exhausted.
 */
void* pool_get(Pool* p);

/*
 * pool_put — return a previously obtained block to the pool.
 */
void pool_put(Pool* p, void* block);

/*
 * pool_destroy — free the pool buffer and the Pool struct itself.
 */
void pool_destroy(Pool* p);

#endif /* MEMORY_H */
