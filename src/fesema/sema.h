#ifndef SEMA_H
#define SEMA_H
#include "../feparser/ast.h"
#include "../fearena/arena.h"

typedef struct SymE SymE;
struct SymE { char *name; int is_mut, is_const; SymE *next; };
typedef struct Scope Scope;
struct Scope { SymE *entries; Scope *parent; };
typedef struct { Scope *current; Arena *arena; int had_error; const char *src_path; } Sema;

Sema sema_new(Arena *arena, const char *src_path);
void sema_check(Sema *s, AstNode *program);
#endif
