#ifndef LEXER_H
#define LEXER_H
#include "token.h"
#include "../fearena/arena.h"

typedef struct {
    const char *src;
    int pos, line, col, len;
    Arena *arena;
} Lexer;

Lexer lexer_new(const char *src, size_t len, Arena *arena);
Token lexer_next(Lexer *l);
#endif
