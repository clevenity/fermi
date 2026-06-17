#ifndef CODEGEN_H
#define CODEGEN_H
#include "fir.h"
#include "../feparser/ast.h"
#include "../fearena/arena.h"
typedef struct Codegen Codegen;
Codegen *codegen_new(Arena *arena, const char *src_path);
void codegen_emit(Codegen *cg, AstNode *program);
FirModule *codegen_module(Codegen *cg);
#endif
