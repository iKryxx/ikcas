//
// Created by ikryxxdev on 2/12/26.
//

#ifndef IKCAS_ARENA_H
#define IKCAS_ARENA_H
#include <stddef.h>
#include <stdio.h>

typedef struct arena_block {
    struct arena_block *next;
    size_t used;
    size_t cap;
    unsigned char data[];
} arena_block_t;

typedef struct arena {
    arena_block_t *head;
} arena_t;

char *arena_strdup(arena_t *a, const char *s, ssize_t n);
void arena_init(arena_t *a);
void arena_destroy(arena_t *a);
void *arena_alloc(arena_t *a, size_t size);

#endif // IKCAS_ARENA_H
