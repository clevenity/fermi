#ifndef TC_H
#define TC_H
#include "../feparser/ast.h"
#include "../fearena/arena.h"

typedef struct TcEntry TcEntry;
struct TcEntry {
    char *name;
    TypeKind ty;
    char *ty_name;
    TcEntry *next;
};

typedef struct TcScope TcScope;
struct TcScope {
    TcEntry *entries;
    TcScope *parent;
};

typedef struct {
    TcScope *scope;
    Arena *arena;
    int had_error;
    TypeKind cur_ret;
    const char *src_path;
} Tc;

Tc tc_new(Arena *arena, const char *src_path);
void tc_check(Tc *t, AstNode *program);
#endif
