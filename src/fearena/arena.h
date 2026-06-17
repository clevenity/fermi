#ifndef ARENA_H
#define ARENA_H
#include <stddef.h>
typedef struct ArenaChunk { char *data; size_t used; size_t cap; struct ArenaChunk *next; } ArenaChunk;
typedef struct { ArenaChunk *head; size_t block_size; } Arena;
Arena  arena_new(size_t block_size);
void  *arena_alloc(Arena *a, size_t n);
char  *arena_strdup(Arena *a, const char *s);
char  *arena_strndup(Arena *a, const char *s, size_t n);
void   arena_free(Arena *a);
#endif
