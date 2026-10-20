#ifndef ARENA_H
#define ARENA_H

#include "misc/allocator.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * Bump allocator on top of a big virtual reservation. Address space is
 * reserved up front with PROT_NONE and made usable page by page as the arena
 * grows, so pointers never move.
 */
typedef struct {
    uint8_t *base;      // start of the reserved range
    size_t   reserved;  // total reserved bytes
    size_t   committed; // bytes made readable and writable so far
    size_t   offset;    // bump pointer, in bytes from base
    size_t   page_size; // cached page size
} arena_t;

/** A saved position, to hand back to arena_release. */
typedef struct {
    size_t offset;
} arena_mark_t;

/** Rounds n up to a multiple of align, which must be a power of two. */
static inline size_t arena_align_up(size_t n, size_t align)
{
    assert((align & (align - 1)) == 0 && "align must be power of two");
    return (n + (align - 1)) & ~(align - 1);
}

/** Reserves reserve_size bytes (rounded to pages) without using any memory yet. Returns -1 on failure. */
static inline int arena_init(arena_t *a, size_t reserve_size)
{
    long ps = sysconf(_SC_PAGESIZE);
    reserve_size = arena_align_up(reserve_size, (size_t)ps);

    void *mem = mmap(NULL, reserve_size, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return -1;

    a->base      = (uint8_t *)mem;
    a->reserved  = reserve_size;
    a->committed = 0;
    a->offset    = 0;
    a->page_size = (size_t)ps;
    return 0;
}

/** Makes the first `needed` bytes usable, a page at a time. Returns -1 if that exceeds the reservation. */
static inline int arena_commit(arena_t *a, size_t needed)
{
    if (needed <= a->committed)
        return 0;
    if (needed > a->reserved)
        return -1;

    size_t new_committed = arena_align_up(needed, a->page_size);
    if (new_committed > a->reserved)
        new_committed = a->reserved;

    if (mprotect(a->base + a->committed,
                 new_committed - a->committed,
                 PROT_READ | PROT_WRITE) != 0)
        return -1;

    a->committed = new_committed;
    return 0;
}

/** Allocates size bytes at the given alignment. Returns NULL when the arena is full. */
static inline void * arena_alloc_aligned(arena_t *a, size_t size, size_t align)
{
    size_t start = arena_align_up(a->offset, align);
    size_t end   = start + size;

    if (end < start)
        return NULL;
    if (end > a->reserved)
        return NULL;
    if (arena_commit(a, end) != 0)
        return NULL;

    a->offset = end;
    return a->base + start;
}

/** Allocates size bytes, 16-byte aligned. */
static inline void* arena_alloc(arena_t *a, size_t size)
{
    return arena_alloc_aligned(a, size, 16);
}

/** Like arena_alloc, but zeroed. */
static inline void* arena_calloc(arena_t *a, size_t size)
{
    void *p = arena_alloc(a, size);
    if (p)
        memset(p, 0, size);
    return p;
}

/**
 * Resizes in place if ptr is the latest allocation. Otherwise allocates a new block
 * and copies, and the old block is not reclaimed. A NULL ptr just allocates.
 */
static inline void *arena_realloc(arena_t *a, void *ptr, size_t old_size,
                                  size_t new_size)
{
    if (!ptr)
        return arena_alloc(a, new_size);

    uint8_t *p = (uint8_t *)ptr;
    if (p + old_size == a->base + a->offset) {
        size_t end = (size_t)(p - a->base) + new_size;
        if (end < new_size || end > a->reserved)
            return NULL;
        if (arena_commit(a, end) != 0)
            return NULL;
        a->offset = end;
        return ptr;
    }

    if (new_size <= old_size)
        return ptr;

    void *np = arena_alloc(a, new_size);
    if (np)
        memcpy(np, ptr, old_size);
    return np;
}

/**
 * Gives memory back only if ptr is the latest allocation. Anything else stays
 * until the arena is reset or released.
 */
static inline void arena_free(arena_t *a, void *ptr, size_t size)
{
    uint8_t *p = (uint8_t *)ptr;
    if (p && p + size == a->base + a->offset)
        a->offset = (size_t)(p - a->base);
}

/** allocator_vtable_t alloc hook for arenas. */
static inline void *arena_alloc_cb(void *ctx, size_t size)
{
    return arena_alloc((arena_t *)ctx, size);
}

/** allocator_vtable_t realloc hook for arenas. */
static inline void *arena_realloc_cb(void *ctx, void *ptr, size_t old_size,
                                     size_t new_size)
{
    return arena_realloc((arena_t *)ctx, ptr, old_size, new_size);
}

/** allocator_vtable_t free hook for arenas. */
static inline void arena_free_cb(void *ctx, void *ptr, size_t size)
{
    arena_free((arena_t *)ctx, ptr, size);
}

static const allocator_vtable_t arena_allocator_vtable = {
    arena_alloc_cb, arena_realloc_cb, arena_free_cb,
};

/** Wraps an arena as a generic allocator_t. */
static inline allocator_t arena_allocator(arena_t *a)
{
    return (allocator_t){ a, &arena_allocator_vtable };
}

/** Growable array whose storage lives in an arena. */
typedef struct {
    arena_t *arena;
    void    *data;
    size_t   elem_size;
    size_t   len;   // elements in use
    size_t   cap;   // elements the storage has room for
} arena_array_t;

/** Allocates room for initial_cap elements. Returns -1 on failure. */
static inline int arena_array_init(arena_array_t *arr, arena_t *a,
                                    size_t elem_size, size_t initial_cap)
{
    void *p = arena_alloc(a, elem_size * initial_cap);
    if (!p)
        return -1;

    arr->arena     = a;
    arr->data      = p;
    arr->elem_size = elem_size;
    arr->len       = 0;
    arr->cap       = initial_cap;
    return 0;
}

/**
 * Grows the storage in place to new_cap elements. That only works while nothing
 * else was allocated in the arena after the array's storage, because growing
 * would overwrite it. If something was, this fails instead of moving the array.
 */
static inline int arena_array_reserve(arena_array_t *arr, size_t new_cap)
{
    if (new_cap <= arr->cap)
        return 0;

    arena_t *a  = arr->arena;
    uint8_t *end = (uint8_t *)arr->data + arr->cap * arr->elem_size;

    if (end != a->base + a->offset)
        return -1; // something else was allocated after the array

    size_t extra          = (new_cap - arr->cap) * arr->elem_size;
    size_t new_end_offset = a->offset + extra;

    if (new_end_offset < a->offset)
        return -1; // size overflow
    if (new_end_offset > a->reserved)
        return -1;
    if (arena_commit(a, new_end_offset) != 0)
        return -1;

    a->offset = new_end_offset;
    arr->cap  = new_cap;
    return 0;
}

/** Appends a copy of elem (or an uninitialized slot if elem is NULL). Returns the slot, NULL on failure. */
static inline void *arena_array_push(arena_array_t *arr, const void *elem)
{
    if (arr->len == arr->cap) {
        size_t new_cap = arr->cap ? arr->cap * 2 : 4;
        if (arena_array_reserve(arr, new_cap) != 0)
            return NULL;
    }

    void *slot = (uint8_t *)arr->data + arr->len * arr->elem_size;
    if (elem)
        memcpy(slot, elem, arr->elem_size);
    arr->len++;
    return slot;
}

/** Pointer to element idx. Asserts that it is in range. */
static inline void *arena_array_get(arena_array_t *arr, size_t idx)
{
    assert(idx < arr->len && "arena_array_get index out of bounds");
    return (uint8_t *)arr->data + idx * arr->elem_size;
}

/** Frees everything. Memory stays committed. */
static inline void arena_reset(arena_t *a)
{
    a->offset = 0;
}

/** Frees everything and tells the kernel it can take the pages back. */
static inline void arena_reset_decommit(arena_t *a)
{
    if (a->committed > 0)
        madvise(a->base, a->committed, MADV_DONTNEED);
    a->offset    = 0;
    a->committed = 0;
}

/** Saves the current position. */
static inline arena_mark_t arena_mark(arena_t *a)
{
    arena_mark_t m;
    m.offset = a->offset;
    return m;
}

/** Frees everything allocated since the mark was taken. */
static inline void arena_release(arena_t *a, arena_mark_t m)
{
    a->offset = m.offset;
}

/** Unmaps the reservation and zeroes the struct. */
static inline void arena_destroy(arena_t *a)
{
    if (a->base)
        munmap(a->base, a->reserved);
    memset(a, 0, sizeof(*a));
}

#endif // ARENA_H
