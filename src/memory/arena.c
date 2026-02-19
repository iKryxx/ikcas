//
// Created by ikryxxdev on 2/12/26.
//

#include "memory/arena.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE 4096

char * arena_strdup(arena_t *a, const char *s, ssize_t n) {
    size_t len = strlen(s);
    ssize_t mem = n > (ssize_t)len || n < 0 ? (ssize_t)len: n;
    char *p = arena_alloc(a, mem + 1);
    memcpy(p, s, mem);
    p[mem] = '\0';
    return p;
}

void arena_init(arena_t *a) {
    a->head = nullptr;
}

void arena_destroy(arena_t *a) {
    arena_block_t *b = a->head;
    while (b) {
        arena_block_t *next = b->next;
        free(b);
        b = next;
    }
    a->head = nullptr;
}

void * arena_alloc(arena_t *a, size_t size) {
    size = (size + 7) & -7ull; // alignment

    arena_block_t *b = a->head;
    if (!b || b->used + size > b->cap) {
        size_t cap = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        arena_block_t *nb = malloc(sizeof(*nb) + cap);

        if (!nb) return nullptr;
        nb->next = b;
        nb->used = 0;
        nb->cap = cap;
        a->head = nb;
        b = nb;
    }

    void *p = b->data + b->used;
    b->used += size;
    return p;
}
