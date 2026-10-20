#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/**
 * Allocator callbacks. realloc and free get the size, so allocators that
 * don't track sizes (like the arena) need no header in front of each block.
 */
typedef struct {
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t new_size);
    void  (*free)(void *ctx, void *ptr, size_t size);
} allocator_vtable_t;

/** An allocator handle. vt == NULL means plain malloc, realloc and free. */
typedef struct {
    void *ctx;
    const allocator_vtable_t *vt;
} allocator_t;

/** The heap allocator. A zeroed allocator_t is the same thing, so zero-initialized structs holding one just work. */
static inline allocator_t allocator_heap(void)
{
    return (allocator_t){ NULL, NULL };
}

/** Allocates size bytes. */
static inline void *allocator_alloc(allocator_t a, size_t size)
{
    return a.vt ? a.vt->alloc(a.ctx, size) : malloc(size);
}

/** Resizes a block. ptr may be NULL, in which case old_size is ignored. */
static inline void *allocator_realloc(allocator_t a, void *ptr,
                                      size_t old_size, size_t new_size)
{
    return a.vt ? a.vt->realloc(a.ctx, ptr, old_size, new_size)
                : realloc(ptr, new_size);
}

/** Frees a block of the given size. */
static inline void allocator_free(allocator_t a, void *ptr, size_t size)
{
    if (a.vt)
        a.vt->free(a.ctx, ptr, size);
    else
        free(ptr);
}

#endif // ALLOCATOR_H
