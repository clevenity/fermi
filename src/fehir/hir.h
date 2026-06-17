#ifndef HIR_H
#define HIR_H
#include "../feparser/ast.h"
#include "../fearena/arena.h"

typedef struct {
    Arena *arena;
    int had_error;
    int std_io, std_math, std_string, std_mem, std_os, std_collections;
} Hir;

Hir hir_new(Arena *arena);
void hir_lower(Hir *h, AstNode *program);
#endif
