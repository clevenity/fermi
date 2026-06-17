#ifndef PARSER_H
#define PARSER_H
#include "../felexer/lexer.h"
#include "../fearena/arena.h"
#include "ast.h"

typedef struct {
    Lexer lexer;
    Token cur, peek;
    Arena *arena;
    int had_error, no_struct_lit;
    const char *src_path;
} Parser;

Parser parser_new(const char *src, size_t src_len, Arena *arena, const char *src_path);
AstNode *parse_program(Parser *p);
#endif
