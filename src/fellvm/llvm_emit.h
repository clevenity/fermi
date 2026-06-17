#ifndef LLVM_EMIT_H
#define LLVM_EMIT_H
#include "../fecodegen/fir.h"
#include "../fearena/arena.h"
#include <stddef.h>

typedef struct {
    char  *data;
    size_t size;
    int    ok;
    char   err[512];
} FirObjBuf;

FirObjBuf fir_to_obj(FirModule *m, Arena *arena, const char *triple, int opt_level);
void      fir_obj_free(FirObjBuf *b);
#endif
