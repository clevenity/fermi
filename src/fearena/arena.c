#include <stdlib.h>
#include <string.h>
#include "arena.h"

Arena arena_new(size_t block_size) {
    Arena a;
    a.block_size = block_size > 0 ? block_size : 65536;
    ArenaChunk *c = malloc(sizeof(ArenaChunk));
    c->data = malloc(a.block_size);
    c->used = 0;
    c->cap  = a.block_size;
    c->next = NULL;
    a.head  = c;
    return a;
}

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    ArenaChunk *c = a->head;
    if (!c || c->used + n > c->cap) {
        size_t newsz = n > a->block_size ? n : a->block_size;
        ArenaChunk *nc = malloc(sizeof(ArenaChunk));
        nc->data = malloc(newsz);
        nc->used = 0;
        nc->cap  = newsz;
        nc->next = a->head;
        a->head  = nc;
        c = nc;
    }
    void *p = c->data + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p  = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    if (!s) return NULL;
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void arena_free(Arena *a) {
    ArenaChunk *c = a->head;
    while (c) {
        ArenaChunk *next = c->next;
        free(c->data);
        free(c);
        c = next;
    }
    a->head = NULL;
}
