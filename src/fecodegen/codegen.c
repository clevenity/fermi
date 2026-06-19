#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "codegen.h"
#include "../fearena/arena.h"

#define MAX_VARS   256
#define MAX_ENUMS  32
#define MAX_ENUM_VARS 64
#define MAX_STRUCTS 64
#define MAX_DEFERS 64

typedef struct VarEntry { char *name; char *type; char *slot; int is_mut; } VarEntry;
typedef struct EnumInfo {
    char *name;
    char *variants[MAX_ENUM_VARS];
    char *payload_types[MAX_ENUM_VARS];
    int  nv;
} EnumInfo;
typedef struct StructInfo {
    char *name;
    char *fields[64];
    char *field_types[64];
    int   nfields;
    int   is_class;
} StructInfo;

struct Codegen {
    Arena *arena;
    const char *src_path;
    FirModule mod;
    int tmp;
    int lbl;
    VarEntry vars[MAX_VARS];
    int nvars;
    int scope_base[64];
    int scope_depth;
    EnumInfo enums[MAX_ENUMS];
    int nenum;
    StructInfo structs[MAX_STRUCTS];
    int nstruct;
    int in_loop;
    char *break_label;
    char *cont_label;
    char *cur_ret_type;
    AstNode *defer_stack[MAX_DEFERS];
    int ndefers;
    int fn_defer_base;
    int in_region;
    int in_unsafe;
    const char *cur_struct;
    char *comptime_names[64];
    long long comptime_values[64];
    int n_comptime;
};

Codegen *codegen_new(Arena *arena, const char *src_path) {
    Codegen *cg = arena_alloc(arena, sizeof(Codegen));
    memset(cg, 0, sizeof(Codegen));
    cg->arena = arena;
    cg->src_path = src_path ? src_path : "<unknown>";
    return cg;
}
FirModule *codegen_module(Codegen *cg) { return &cg->mod; }

static char *fmt_int(Arena *a, const char *pfx, size_t pfxlen, int n) {
    char digits[12];
    char *p = digits + sizeof(digits);
    unsigned u = (unsigned)n;
    do { *--p = (char)('0' + u % 10u); u /= 10u; } while (u);
    size_t dlen = (size_t)(digits + sizeof(digits) - p);
    size_t total = pfxlen + dlen;
    char *dst = arena_alloc(a, total + 1);
    memcpy(dst, pfx, pfxlen);
    memcpy(dst + pfxlen, p, dlen);
    dst[total] = '\0';
    return dst;
}
static char *newtmp(Codegen *cg) {
    return fmt_int(cg->arena, "t", 1, cg->tmp++);
}
static char *newlbl(Codegen *cg, const char *prefix) {
    return fmt_int(cg->arena, prefix, strlen(prefix), cg->lbl++);
}

static FirBlock *new_block(Codegen *cg, const char *label) {
    FirBlock *b = arena_alloc(cg->arena, sizeof(FirBlock));
    memset(b, 0, sizeof(FirBlock));
    b->label = arena_strdup(cg->arena, label);
    FirFn *fn = cg->mod.cur_fn;
    if (!fn->blocks) fn->blocks = b;
    else {
        FirBlock *last = fn->blocks;
        while (last->next) last = last->next;
        last->next = b;
    }
    fn->cur_block = b;
    return b;
}

static void emit_instr(Codegen *cg, FirInstr *i) {
    FirBlock *b = cg->mod.cur_fn->cur_block;
    if (!b->head) { b->head = b->tail = i; }
    else { b->tail->next = i; b->tail = i; }
}

static FirInstr *mk_instr(Codegen *cg, FirOp op) {
    FirInstr *i = arena_alloc(cg->arena, sizeof(FirInstr));
    memset(i, 0, sizeof(FirInstr));
    i->op = op;
    return i;
}

static void emit_alloca(Codegen *cg, const char *dst, const char *type) {
    FirInstr *i = mk_instr(cg, FIR_ALLOCA);
    i->dst = arena_strdup(cg->arena, dst);
    i->type = arena_strdup(cg->arena, type);
    emit_instr(cg, i);
}
static void emit_store(Codegen *cg, const char *val, const char *slot, const char *type) {
    FirInstr *i = mk_instr(cg, FIR_STORE);
    i->src1 = arena_strdup(cg->arena, val);
    i->dst = arena_strdup(cg->arena, slot);
    i->type = arena_strdup(cg->arena, type);
    emit_instr(cg, i);
}
static char *emit_load(Codegen *cg, const char *slot, const char *type) {
    char *dst = newtmp(cg);
    FirInstr *i = mk_instr(cg, FIR_LOAD);
    i->dst = dst; i->src1 = arena_strdup(cg->arena, slot);
    i->type = arena_strdup(cg->arena, type);
    emit_instr(cg, i);
    return dst;
}
static void emit_br(Codegen *cg, const char *label) {
    FirInstr *i = mk_instr(cg, FIR_BR);
    i->src1 = arena_strdup(cg->arena, label);
    emit_instr(cg, i);
}
static void emit_br_cond(Codegen *cg, const char *cond, const char *t, const char *f) {
    FirInstr *i = mk_instr(cg, FIR_BR_COND);
    i->src1 = arena_strdup(cg->arena, cond);
    i->src2 = arena_strdup(cg->arena, t);
    i->src3 = arena_strdup(cg->arena, f);
    emit_instr(cg, i);
}

static int last_instr_is_term(Codegen *cg) {
    FirBlock *b = cg->mod.cur_fn->cur_block;
    if (!b || !b->tail) return 0;
    FirOp op = b->tail->op;
    return op==FIR_BR||op==FIR_BR_COND||op==FIR_RET||op==FIR_RET_VOID||op==FIR_UNREACHABLE;
}

static void scope_push(Codegen *cg) {
    cg->scope_base[cg->scope_depth++] = cg->nvars;
}
static void scope_pop(Codegen *cg) {
    if (cg->scope_depth > 0) cg->nvars = cg->scope_base[--cg->scope_depth];
}
static void var_def(Codegen *cg, const char *name, const char *type, const char *slot) {
      if (cg->nvars < MAX_VARS) {
          cg->vars[cg->nvars].name = arena_strdup(cg->arena, name);
          cg->vars[cg->nvars].type = arena_strdup(cg->arena, type);
          cg->vars[cg->nvars].slot = arena_strdup(cg->arena, slot);
          cg->vars[cg->nvars].is_mut = 0;
          cg->nvars++;
      }
  }
  static void var_def_mut(Codegen *cg, const char *name, const char *type, const char *slot) {
      if (cg->nvars < MAX_VARS) {
          cg->vars[cg->nvars].name = arena_strdup(cg->arena, name);
          cg->vars[cg->nvars].type = arena_strdup(cg->arena, type);
          cg->vars[cg->nvars].slot = arena_strdup(cg->arena, slot);
          cg->vars[cg->nvars].is_mut = 1;
          cg->nvars++;
      }
  }
  
  static char *emit_mut_str_copy(Codegen *cg, char *v) {
      
      char *len_v = newtmp(cg);
      FirInstr *sl = mk_instr(cg, FIR_CALL);
      sl->type = arena_strdup(cg->arena, "i64");
      sl->src1 = arena_strdup(cg->arena, "strlen");
      sl->dst  = len_v; sl->nargs = 1;
      sl->args = arena_alloc(cg->arena, sizeof(char*));
      sl->arg_types = arena_alloc(cg->arena, sizeof(char*));
      sl->args[0] = arena_strdup(cg->arena, v);
      sl->arg_types[0] = arena_strdup(cg->arena, "ptr");
      emit_instr(cg, sl);
      
      char *len1 = newtmp(cg);
      FirInstr *ad = mk_instr(cg, FIR_ADD);
      ad->dst  = len1; ad->type = arena_strdup(cg->arena, "i64");
      ad->src1 = arena_strdup(cg->arena, len_v);
      ad->src2 = arena_strdup(cg->arena, "1"); emit_instr(cg, ad);
      
      char *heap = newtmp(cg);
      FirInstr *ml = mk_instr(cg, FIR_CALL);
      ml->type = arena_strdup(cg->arena, "ptr");
      ml->src1 = arena_strdup(cg->arena, "malloc");
      ml->dst  = heap; ml->nargs = 1;
      ml->args = arena_alloc(cg->arena, sizeof(char*));
      ml->arg_types = arena_alloc(cg->arena, sizeof(char*));
      ml->args[0] = arena_strdup(cg->arena, len1);
      ml->arg_types[0] = arena_strdup(cg->arena, "i64");
      emit_instr(cg, ml);
      
      char *sc_dst = newtmp(cg);
      FirInstr *sc = mk_instr(cg, FIR_CALL);
      sc->type = arena_strdup(cg->arena, "ptr");
      sc->src1 = arena_strdup(cg->arena, "strcpy");
      sc->dst  = sc_dst; sc->nargs = 2;
      sc->args = arena_alloc(cg->arena, 2*sizeof(char*));
      sc->arg_types = arena_alloc(cg->arena, 2*sizeof(char*));
      sc->args[0] = arena_strdup(cg->arena, heap); sc->arg_types[0] = arena_strdup(cg->arena, "ptr");
      sc->args[1] = arena_strdup(cg->arena, v);    sc->arg_types[1] = arena_strdup(cg->arena, "ptr");
      emit_instr(cg, sc);
      return heap;
  }
  
  static char *emit_mut_str_replace(Codegen *cg, char *old_v, char *new_v) {
      char *fd = newtmp(cg);
      FirInstr *fr = mk_instr(cg, FIR_CALL);
      fr->type = arena_strdup(cg->arena, "void");
      fr->src1 = arena_strdup(cg->arena, "free");
      fr->dst  = fd; fr->nargs = 1;
      fr->args = arena_alloc(cg->arena, sizeof(char*));
      fr->arg_types = arena_alloc(cg->arena, sizeof(char*));
      fr->args[0] = arena_strdup(cg->arena, old_v);
      fr->arg_types[0] = arena_strdup(cg->arena, "ptr");
      emit_instr(cg, fr);
      return emit_mut_str_copy(cg, new_v);
  }
static VarEntry *var_lookup(Codegen *cg, const char *name) {
    for (int i = cg->nvars-1; i >= 0; i--)
        if (strcmp(cg->vars[i].name, name)==0) return &cg->vars[i];
    return NULL;
}

static EnumInfo *find_enum(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->nenum; i++)
        if (strcmp(cg->enums[i].name, name)==0) return &cg->enums[i];
    return NULL;
}
static int find_enum_variant(Codegen *cg, const char *variant_name, EnumInfo **out_enum) {
    for (int i = 0; i < cg->nenum; i++) {
        EnumInfo *e = &cg->enums[i];
        for (int j = 0; j < e->nv; j++) {
            if (strcmp(e->variants[j], variant_name)==0) {
                if (out_enum) *out_enum = e;
                return j;
            }
        }
    }
    if (out_enum) *out_enum = NULL;
    return -1;
}
static StructInfo *find_struct(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->nstruct; i++)
        if (strcmp(cg->structs[i].name, name)==0) return &cg->structs[i];
    return NULL;
}

static const char *typekind_to_llvm(TypeKind k, const char *name) {
    switch(k) {
    case TY_VOID:   return "void";
    case TY_BOOL:   return "i1";
    case TY_BYTE:   return "i8";
    case TY_SHORT: case TY_USHORT: return "i16";
    case TY_INT: case TY_UINT:    return "i32";
    case TY_LONG: case TY_ULONG:  return "i64";
    case TY_FLOAT:  return "float";
    case TY_DOUBLE: return "double";
    case TY_CHAR:   return "i8";
    case TY_STR:    return "ptr";
    case TY_REF: case TY_RAW_PTR: return "ptr";
    case TY_FN_PTR: return "ptr";
    case TY_NAMED:  return "ptr";
    default: return "i32";
    }
    (void)name;
}
static const char *typenode_to_llvm(Codegen *cg, TypeNode *tn) {
    if (!tn) return "i32";
    if (tn->kind == TY_NAMED && tn->name) {
        StructInfo *s = find_struct(cg, tn->name);
        if (s) {
            char buf[128]; snprintf(buf, sizeof(buf), "%%struct.%s", tn->name);
            return arena_strdup(cg->arena, buf);
        }
        EnumInfo *e = find_enum(cg, tn->name);
        if (e) return "i64";
        return "ptr";
    }
    if (tn->kind == TY_ARRAY_FIXED && tn->inner) {
        char buf[128]; snprintf(buf, sizeof(buf), "[%d x %s]", tn->array_size, typenode_to_llvm(cg, tn->inner));
        return arena_strdup(cg->arena, buf);
    }
    if (tn->kind == TY_ARRAY_DYN) return "ptr";
    if (tn->kind == TY_REF || tn->kind == TY_RAW_PTR) return "ptr";
    if (tn->kind == TY_FN_PTR) return "ptr";
    return typekind_to_llvm(tn->kind, tn->name);
}

static const char *ast_type_str(Codegen *cg, AstNode *n) {
    if (n->ty_name) {
        StructInfo *s = find_struct(cg, n->ty_name);
        if (s) {
            char buf[128]; snprintf(buf, sizeof(buf), "%%struct.%s", n->ty_name);
            return arena_strdup(cg->arena, buf);
        }
        EnumInfo *e = find_enum(cg, n->ty_name);
        if (e) return "i64";
        return "ptr";
    }
    return typekind_to_llvm(n->ty, NULL);
}

static char *emit_expr(Codegen *cg, AstNode *n);
static void emit_block(Codegen *cg, AstNode *n);
static void emit_stmt(Codegen *cg, AstNode *n);

static const char *infer_val_type(Codegen *cg, const char *v, AstNode *n) {
    if (!v) return "i32";
    if (v[0]=='%') {
        VarEntry *e = var_lookup(cg, v+1);
        if (e) return e->type;
    }
    if (n) return ast_type_str(cg, n);
    return "i32";
}

static const char *type_for_op(Codegen *cg, AstNode *left, AstNode *right) {
    const char *lt = "i32", *rt = "i32";
    if (left) lt = ast_type_str(cg, left);
    if (right) rt = ast_type_str(cg, right);
    if (strcmp(lt,"double")==0||strcmp(rt,"double")==0) return "double";
    if (strcmp(lt,"float")==0||strcmp(rt,"float")==0) return "float";
    if (strcmp(lt,"i64")==0||strcmp(rt,"i64")==0) return "i64";
    if (strcmp(lt,"ptr")==0||strcmp(rt,"ptr")==0) return "ptr";
    if (strcmp(lt,"i8")==0&&strcmp(rt,"i8")==0) return "i8";
    if (strcmp(lt,"i16")==0||strcmp(rt,"i16")==0) return "i16";
    return "i32";
}

static int is_float_type(const char *t) {
    return t && (strcmp(t,"float")==0||strcmp(t,"double")==0);
}
static int is_int_type(const char *t) {
    return t && (strcmp(t,"i8")==0||strcmp(t,"i16")==0||strcmp(t,"i32")==0||
                 strcmp(t,"i64")==0||strcmp(t,"i1")==0);
}

static char *coerce_to(Codegen *cg, char *val, const char *from, const char *to) {
    if (!from || !to || strcmp(from,to)==0) return val;
    char *dst = newtmp(cg);
    FirInstr *i = mk_instr(cg, FIR_NOP);
    if (is_float_type(from) && is_int_type(to)) {
        i->op = FIR_FPTOSI;
        i->dst = dst; i->type = arena_strdup(cg->arena, to);
        i->src1 = arena_strdup(cg->arena, val);
        i->src2 = arena_strdup(cg->arena, from);
        emit_instr(cg, i); return dst;
    }
    if (is_int_type(from) && is_float_type(to)) {
        i->op = FIR_SITOFP;
        i->dst = dst; i->type = arena_strdup(cg->arena, to);
        i->src1 = arena_strdup(cg->arena, val);
        i->src2 = arena_strdup(cg->arena, from);
        emit_instr(cg, i); return dst;
    }
    if (strcmp(from,"float")==0 && strcmp(to,"double")==0) {
        i->op = FIR_FPEXT;
        i->dst = dst; i->type = arena_strdup(cg->arena, to);
        i->src1 = arena_strdup(cg->arena, val);
        i->src2 = arena_strdup(cg->arena, from);
        emit_instr(cg, i); return dst;
    }
    if (strcmp(from,"double")==0 && strcmp(to,"float")==0) {
        i->op = FIR_FPTRUNC;
        i->dst = dst; i->type = arena_strdup(cg->arena, to);
        i->src1 = arena_strdup(cg->arena, val);
        i->src2 = arena_strdup(cg->arena, from);
        emit_instr(cg, i); return dst;
    }
    if (is_int_type(from) && is_int_type(to)) {
        int fw = (strcmp(from,"i64")==0)?64:(strcmp(from,"i32")==0)?32:
                 (strcmp(from,"i16")==0)?16:(strcmp(from,"i8")==0)?8:1;
        int tw = (strcmp(to,"i64")==0)?64:(strcmp(to,"i32")==0)?32:
                 (strcmp(to,"i16")==0)?16:(strcmp(to,"i8")==0)?8:1;
        if (fw < tw) { i->op = FIR_SEXT; }
        else { i->op = FIR_TRUNC; }
        i->dst = dst; i->type = arena_strdup(cg->arena, to);
        i->src1 = arena_strdup(cg->arena, val);
        i->src2 = arena_strdup(cg->arena, from);
        emit_instr(cg, i); return dst;
    }
    if ((strcmp(from,"i1")==0) && strcmp(to,"i32")==0) {
        i->op = FIR_ZEXT;
        i->dst = dst; i->type = arena_strdup(cg->arena, to);
        i->src1 = arena_strdup(cg->arena, val);
        i->src2 = arena_strdup(cg->arena, from);
        emit_instr(cg, i); return dst;
    }
    return val;
}

static long long tynode_sizeof(TypeNode *tn) {
    if (!tn) return 8;
    switch (tn->kind) {
    case TY_BOOL: case TY_BYTE: case TY_CHAR: return 1;
    case TY_SHORT: case TY_USHORT: return 2;
    case TY_INT: case TY_UINT: case TY_FLOAT: return 4;
    case TY_LONG: case TY_ULONG: case TY_DOUBLE: return 8;
    case TY_STR: case TY_REF: case TY_RAW_PTR: return 8;
    default: return 8;
    }
}
static long long eval_comptime(Codegen *cg, AstNode *n) {
    if (!n) return 0;
    switch (n->kind) {
    case NODE_INT_LIT: return n->int_lit.val;
    case NODE_LONG_LIT: return n->long_lit.val;
    case NODE_IDENT: {
        for (int _i=0;_i<cg->n_comptime;_i++)
            if (strcmp(cg->comptime_names[_i],n->ident.name)==0)
                return cg->comptime_values[_i];
        return 0;
    }
    case NODE_BINARY: {
        long long _l=eval_comptime(cg,n->binary.left);
        long long _r=eval_comptime(cg,n->binary.right);
        if (!strcmp(n->binary.op,"+")) return _l+_r;
        if (!strcmp(n->binary.op,"-")) return _l-_r;
        if (!strcmp(n->binary.op,"*")) return _l*_r;
        if (!strcmp(n->binary.op,"/")) return _r?_l/_r:0;
        if (!strcmp(n->binary.op,"%")) return _r?_l%_r:0;
        if (!strcmp(n->binary.op,"<<")) return _l<<(int)_r;
        if (!strcmp(n->binary.op,">>")) return _l>>(int)_r;
        if (!strcmp(n->binary.op,"&")) return _l&_r;
        if (!strcmp(n->binary.op,"|")) return _l|_r;
        if (!strcmp(n->binary.op,"^")) return _l^_r;
        return 0;
    }
    case NODE_UNARY: {
        long long _v=eval_comptime(cg,n->unary.operand);
        if (!strcmp(n->unary.op,"-")) return -_v;
        if (!strcmp(n->unary.op,"~")) return ~_v;
        return _v;
    }
    case NODE_AT_CALL: {
        if (!strcmp(n->at_call.name,"sizeof")||!strcmp(n->at_call.name,"alignof"))
            return tynode_sizeof(n->at_call.type_arg);
        return 0;
    }
    default: return 0;
    }
}

static char *emit_rt_call(Codegen *cg, const char *fn, const char *ret_type,
                          int nargs, char **args, const char **atypes) {
    FirInstr *ci = mk_instr(cg, FIR_CALL);
    ci->type = arena_strdup(cg->arena, ret_type);
    ci->src1 = arena_strdup(cg->arena, fn);
    ci->nargs = nargs;
    ci->args = nargs ? arena_alloc(cg->arena,(size_t)nargs*sizeof(char*)) : NULL;
    ci->arg_types = nargs ? arena_alloc(cg->arena,(size_t)nargs*sizeof(char*)) : NULL;
    for (int k = 0; k < nargs; k++) {
        ci->args[k] = arena_strdup(cg->arena, args[k]);
        ci->arg_types[k] = arena_strdup(cg->arena, atypes[k]);
    }
    ci->dst = newtmp(cg);
    emit_instr(cg, ci);
    return ci->dst;
}

static char *emit_io_print(Codegen *cg, AstList *args) {
      if (!args) return arena_strdup(cg->arena,"0");
      AstNode *anode = args->node;
      
      if (anode->kind == NODE_INTERP_STR) {
          emit_expr(cg, anode);
          return arena_strdup(cg->arena,"0");
      }
      char *aval = emit_expr(cg, anode);
      const char *aty = ast_type_str(cg, anode);
      
      const char *fmt;
      if      (strcmp(aty,"i64")==0)    fmt="%ld";
      else if (strcmp(aty,"float")==0)  { aval=coerce_to(cg,aval,"float","double"); aty="double"; fmt="%g"; }
      else if (strcmp(aty,"double")==0) fmt="%g";
      else if (strcmp(aty,"ptr")==0)    fmt="%s";
      else if (strcmp(aty,"i1")==0)     { aval=coerce_to(cg,aval,"i1","i32"); aty="i32"; fmt="%d"; }
      else if (strcmp(aty,"i8")==0)     fmt="%c";
      else                              fmt="%d";
      char *fmtdst = newtmp(cg);
      FirInstr *fs = mk_instr(cg, FIR_CONST_STR);
      fs->dst = fmtdst; fs->src1 = arena_strdup(cg->arena, fmt); emit_instr(cg, fs);
      char *pd = newtmp(cg);
      FirInstr *pc = mk_instr(cg, FIR_CALL);
      pc->type = arena_strdup(cg->arena, "i32");
      pc->src1 = arena_strdup(cg->arena, "printf");
      pc->dst = pd; pc->nargs = 2;
      pc->args      = arena_alloc(cg->arena, 2*sizeof(char*));
      pc->arg_types = arena_alloc(cg->arena, 2*sizeof(char*));
      pc->args[0] = arena_strdup(cg->arena, fmtdst); pc->arg_types[0] = arena_strdup(cg->arena, "ptr");
      pc->args[1] = arena_strdup(cg->arena, aval);   pc->arg_types[1] = arena_strdup(cg->arena, aty);
      emit_instr(cg, pc);
      return arena_strdup(cg->arena, "0");
  }

static void set_node_ty_from_ir(AstNode *n, const char *ir_ty) {
    if (!n || !ir_ty) return;
    if (strcmp(ir_ty,"double")==0)      n->ty=TY_DOUBLE;
    else if (strcmp(ir_ty,"float")==0)  n->ty=TY_FLOAT;
    else if (strcmp(ir_ty,"i64")==0)    n->ty=TY_LONG;
    else if (strcmp(ir_ty,"ptr")==0)    n->ty=TY_STR;
    else if (strcmp(ir_ty,"i1")==0)     n->ty=TY_BOOL;
    else if (strcmp(ir_ty,"void")==0)   n->ty=TY_VOID;
    else                                n->ty=TY_INT;
}

static const struct { const char *fe; const char *c; const char *ret; } s_math_map[] = {
    {"sqrt","sqrt","double"},{"sqrtf","sqrtf","float"},
    {"pow","pow","double"},  {"powf","powf","float"},
    {"abs","abs","i32"},     {"fabs","fabs","double"}, {"fabsf","fabsf","float"},
    {"floor","floor","double"},{"ceil","ceil","double"},{"round","round","double"},
    {"floorf","floorf","float"},{"ceilf","ceilf","float"},{"roundf","roundf","float"},
    {"sin","sin","double"},  {"cos","cos","double"},  {"tan","tan","double"},
    {"sinf","sinf","float"}, {"cosf","cosf","float"}, {"tanf","tanf","float"},
    {"log","log","double"},  {"log2","log2","double"},{"log10","log10","double"},
    {"exp","exp","double"},  {"hypot","hypot","double"},
    {"atan","atan","double"},{"atan2","atan2","double"},
    {"min","fmin","double"}, {"max","fmax","double"},
    {NULL,NULL,NULL}
};

static const struct { const char *fe; const char *c; const char *ret; } s_array_map[] = {
    {"new","fe_array_new","ptr"},
    {"push","fe_array_push","void"},
    {"pop","fe_array_pop","i64"},
    {"get","fe_array_get","i64"},
    {"set","fe_array_set","void"},
    {"len","fe_array_len","i64"},
    {"free","fe_array_free","void"},
    {NULL,NULL,NULL}
};

static const struct { const char *fe; const char *c; const char *ret; } s_map_map[] = {
    {"new","fe_map_new","ptr"},
    {"set","fe_map_set","void"},
    {"get","fe_map_get","i64"},
    {"has","fe_map_has","i32"},
    {"delete","fe_map_delete","void"},
    {"free","fe_map_free","void"},
    {NULL,NULL,NULL}
};

static const struct { const char *fe; const char *c; const char *ret; } s_mem_map[] = {
    {"alloc","malloc","ptr"},
    {"realloc","realloc","ptr"},
    {"free","free","void"},
    {"copy","memcpy","ptr"},
    {"set","memset","ptr"},
    {"cmp","memcmp","i32"},
    {"region_alloc","fe_region_alloc","ptr"},
    {NULL,NULL,NULL}
};

static char *emit_io_println(Codegen *cg, AstList *args) {
    if (!args) {
        char *fmtdst=newtmp(cg);
        FirInstr *fs=mk_instr(cg,FIR_CONST_STR);
        fs->dst=fmtdst; fs->src1=arena_strdup(cg->arena,"\n"); emit_instr(cg,fs);
        char *pd=newtmp(cg);
        FirInstr *pc=mk_instr(cg,FIR_CALL);
        pc->type=arena_strdup(cg->arena,"i32");
        pc->src1=arena_strdup(cg->arena,"printf");
        pc->dst=pd; pc->nargs=1;
        pc->args=arena_alloc(cg->arena,sizeof(char*));
        pc->arg_types=arena_alloc(cg->arena,sizeof(char*));
        pc->args[0]=arena_strdup(cg->arena,fmtdst);
        pc->arg_types[0]=arena_strdup(cg->arena,"ptr");
        emit_instr(cg,pc);
        return arena_strdup(cg->arena,"0");
    }
    AstNode *anode=args->node;
    if (anode->kind==NODE_INTERP_STR) {
        emit_expr(cg,anode);
        return arena_strdup(cg->arena,"0");
    }
    char *aval=emit_expr(cg,anode);
    const char *aty=ast_type_str(cg,anode);
    const char *fmt;
    if      (strcmp(aty,"i64")==0)    fmt="%ld\n";
    else if (strcmp(aty,"float")==0)  { aval=coerce_to(cg,aval,"float","double"); aty="double"; fmt="%g\n"; }
    else if (strcmp(aty,"double")==0) fmt="%g\n";
    else if (strcmp(aty,"ptr")==0)    fmt="%s\n";
    else if (strcmp(aty,"i1")==0)     { aval=coerce_to(cg,aval,"i1","i32"); aty="i32"; fmt="%d\n"; }
    else if (strcmp(aty,"i8")==0)     fmt="%c\n";
    else                              fmt="%d\n";
    char *fmtdst=newtmp(cg);
    FirInstr *fs=mk_instr(cg,FIR_CONST_STR);
    fs->dst=fmtdst; fs->src1=arena_strdup(cg->arena,fmt); emit_instr(cg,fs);
    char *pd=newtmp(cg);
    FirInstr *pc=mk_instr(cg,FIR_CALL);
    pc->type=arena_strdup(cg->arena,"i32");
    pc->src1=arena_strdup(cg->arena,"printf");
    pc->dst=pd; pc->nargs=2;
    pc->args=arena_alloc(cg->arena,2*sizeof(char*));
    pc->arg_types=arena_alloc(cg->arena,2*sizeof(char*));
    pc->args[0]=arena_strdup(cg->arena,fmtdst); pc->arg_types[0]=arena_strdup(cg->arena,"ptr");
    pc->args[1]=arena_strdup(cg->arena,aval);   pc->arg_types[1]=arena_strdup(cg->arena,aty);
    emit_instr(cg,pc);
    return arena_strdup(cg->arena,"0");
}

static char *emit_ns_call(Codegen *cg, AstNode *caller, const char *ns, const char *func, AstList *args) {
    (void)caller;
    if (strcmp(ns,"io")==0 && strcmp(func,"print")==0)   return emit_io_print(cg, args);
    if (strcmp(ns,"io")==0 && strcmp(func,"println")==0) return emit_io_println(cg, args);

    
    int na=0; char *argv[16]; const char *tyv[16];
    for(AstList *al=args;al&&na<16;al=al->next){
        argv[na]=emit_expr(cg,al->node); tyv[na]=ast_type_str(cg,al->node); na++;
    }

    
    if (strcmp(ns,"math")==0) {
        for (int mi=0; s_math_map[mi].fe; mi++) {
            if (strcmp(func, s_math_map[mi].fe)==0) {
                set_node_ty_from_ir(caller, s_math_map[mi].ret);
                return emit_rt_call(cg, s_math_map[mi].c, s_math_map[mi].ret, na, argv, tyv);
            }
        }
    }

    
    if (strcmp(ns,"array")==0) {
        for (int mi=0; s_array_map[mi].fe; mi++) {
            if (strcmp(func, s_array_map[mi].fe)==0) {
                set_node_ty_from_ir(caller, s_array_map[mi].ret);
                return emit_rt_call(cg, s_array_map[mi].c, s_array_map[mi].ret, na, argv, tyv);
            }
        }
    }

    
    if (strcmp(ns,"map")==0) {
        for (int mi=0; s_map_map[mi].fe; mi++) {
            if (strcmp(func, s_map_map[mi].fe)==0) {
                set_node_ty_from_ir(caller, s_map_map[mi].ret);
                return emit_rt_call(cg, s_map_map[mi].c, s_map_map[mi].ret, na, argv, tyv);
            }
        }
    }

    
    if (strcmp(ns,"mem")==0) {
        for (int mi=0; s_mem_map[mi].fe; mi++) {
            if (strcmp(func, s_mem_map[mi].fe)==0) {
                set_node_ty_from_ir(caller, s_mem_map[mi].ret);
                return emit_rt_call(cg, s_mem_map[mi].c, s_mem_map[mi].ret, na, argv, tyv);
            }
        }
    }

    char mangled[256]; snprintf(mangled,sizeof(mangled),"%s__%s",ns,func);
    const char *ret_ty="i32";
    for(FirFn *fn=cg->mod.fns;fn;fn=fn->next)
        if(strcmp(fn->name,mangled)==0){ret_ty=fn->ret_type?fn->ret_type:"void";break;}
    return emit_rt_call(cg,mangled,ret_ty,na,argv,tyv);
}

static void emit_stmt(Codegen *cg, AstNode *n);

static void emit_defers(Codegen *cg) {
    for (int i = cg->ndefers-1; i >= cg->fn_defer_base; i--)
        emit_stmt(cg, cg->defer_stack[i]);
}

static void emit_ret_with_defers(Codegen *cg, const char *val, const char *type) {
    emit_defers(cg);
    FirInstr *i = mk_instr(cg, val ? FIR_RET : FIR_RET_VOID);
    if (val) {
        i->src1 = arena_strdup(cg->arena, val);
        i->type = arena_strdup(cg->arena, type);
    }
    emit_instr(cg, i);
    char *dead = newlbl(cg, "dead");
    new_block(cg, dead);
}

static char *emit_expr(Codegen *cg, AstNode *n) {
    if (!n) return arena_strdup(cg->arena, "0");
    switch (n->kind) {
    case NODE_INT_LIT: {
        n->ty = TY_INT;
        char ibuf[32]; snprintf(ibuf,sizeof(ibuf),"%lld",n->int_lit.val);
        return arena_strdup(cg->arena,ibuf);
    }
    case NODE_LONG_LIT: {
        n->ty = TY_LONG;
        char lbuf[32]; snprintf(lbuf,sizeof(lbuf),"%lld",n->long_lit.val);
        return arena_strdup(cg->arena,lbuf);
    }
    case NODE_FLOAT_LIT: {
        n->ty = TY_FLOAT;
        char fbuf[64]; snprintf(fbuf,sizeof(fbuf),"%.9g",n->float_lit.val);
        return arena_strdup(cg->arena,fbuf);
    }
    case NODE_DOUBLE_LIT: {
        n->ty = TY_DOUBLE;
        char dbuf[64]; snprintf(dbuf,sizeof(dbuf),"%.17g",n->double_lit.val);
        return arena_strdup(cg->arena,dbuf);
    }
    case NODE_BOOL_LIT:
        n->ty = TY_BOOL;
        return arena_strdup(cg->arena, n->bool_lit.val ? "1" : "0");
    case NODE_CHAR_LIT: {
        n->ty = TY_CHAR;
        char cbuf[16]; snprintf(cbuf,sizeof(cbuf),"%d",(unsigned char)n->char_lit.val);
        return arena_strdup(cg->arena,cbuf);
    }
    case NODE_STRING_LIT: {
        n->ty = TY_STR;
        char *dst = newtmp(cg);
        FirInstr *i = mk_instr(cg, FIR_CONST_STR);
        i->dst = dst; i->src1 = arena_strdup(cg->arena, n->string_lit.val);
        emit_instr(cg, i);
        return dst;
    }
    case NODE_NULL:
        n->ty = TY_RAW_PTR;
        return arena_strdup(cg->arena, "null");
    case NODE_IDENT: {
        for(int _ci=0;_ci<cg->n_comptime;_ci++){
            if(strcmp(cg->comptime_names[_ci],n->ident.name)==0){
                char _cb[32]; snprintf(_cb,sizeof(_cb),"%lld",cg->comptime_values[_ci]);
                n->ty=TY_LONG; return arena_strdup(cg->arena,_cb);
            }
        }
        VarEntry *e = var_lookup(cg, n->ident.name);
        if (e) {
            char *v = emit_load(cg, e->slot, e->type);
            n->ty = TY_UNKNOWN;
            if (strcmp(e->type,"i32")==0) n->ty=TY_INT;
            else if (strcmp(e->type,"i64")==0) n->ty=TY_LONG;
            else if (strcmp(e->type,"float")==0) n->ty=TY_FLOAT;
            else if (strcmp(e->type,"double")==0) n->ty=TY_DOUBLE;
            else if (strcmp(e->type,"i1")==0) n->ty=TY_BOOL;
            else if (strcmp(e->type,"i8")==0) n->ty=TY_CHAR;
            else if (strcmp(e->type,"ptr")==0) n->ty=TY_STR;
            else { n->ty=TY_NAMED; n->ty_name=e->type; }
            return v;
        }
        for (FirFn *fn=cg->mod.fns; fn; fn=fn->next) {
            if (strcmp(fn->name, n->ident.name)==0) {
                n->ty = TY_FN_PTR;
                char buf[256]; snprintf(buf, sizeof(buf), "@%s", n->ident.name);
                return arena_strdup(cg->arena, buf);
            }
        }
        {
            EnumInfo *ei = NULL;
            int vi = find_enum_variant(cg, n->ident.name, &ei);
            if (vi >= 0 && ei) {
                int tag = vi * 2;
                char tag_s[32]; snprintf(tag_s, sizeof(tag_s), "%d", tag);
                char *dst = newtmp(cg);
                FirInstr *li = mk_instr(cg, FIR_SEXT);
                li->dst = dst; li->type = arena_strdup(cg->arena, "i64");
                li->src1 = arena_strdup(cg->arena, tag_s);
                li->src2 = arena_strdup(cg->arena, "i32"); emit_instr(cg, li);
                n->ty = TY_LONG; return dst;
            }
        }
        n->ty = TY_INT;
        return arena_strdup(cg->arena, "0");
    }
    case NODE_UNARY: {
        char *v = emit_expr(cg, n->unary.operand);
        const char *oty = ast_type_str(cg, n->unary.operand);
        char *dst = newtmp(cg);
        if (!strcmp(n->unary.op,"!")) {
            FirInstr *i = mk_instr(cg, FIR_ICMP);
            i->dst=dst; i->type=arena_strdup(cg->arena,"i1");
            i->src1=arena_strdup(cg->arena,v);
            i->src2=arena_strdup(cg->arena,"0");
            i->src3=arena_strdup(cg->arena,"eq"); emit_instr(cg,i);
            n->ty=TY_BOOL; return dst;
        }
        if (!strcmp(n->unary.op,"-")) {
            FirInstr *i = mk_instr(cg, is_float_type(oty)?FIR_FNEG:FIR_NEG);
            i->dst=dst; i->type=arena_strdup(cg->arena,oty);
            i->src1=arena_strdup(cg->arena,v); emit_instr(cg,i);
            n->ty=n->unary.operand->ty; return dst;
        }
        if (!strcmp(n->unary.op,"~")) {
            FirInstr *i = mk_instr(cg, FIR_BITNOT);
            i->dst=dst; i->type=arena_strdup(cg->arena,oty);
            i->src1=arena_strdup(cg->arena,v); emit_instr(cg,i);
            n->ty=n->unary.operand->ty; return dst;
        }
        if (!strcmp(n->unary.op,"++")) {
            char *one=arena_strdup(cg->arena,"1");
            FirInstr *i=mk_instr(cg,FIR_ADD); i->dst=dst;
            i->type=arena_strdup(cg->arena,oty);
            i->src1=arena_strdup(cg->arena,v); i->src2=one; emit_instr(cg,i);
            if (n->unary.operand->kind==NODE_IDENT) {
                VarEntry *e=var_lookup(cg,n->unary.operand->ident.name);
                if(e) emit_store(cg,dst,e->slot,oty);
            }
            n->ty=n->unary.operand->ty;
            return n->unary.prefix ? dst : v;
        }
        if (!strcmp(n->unary.op,"--")) {
            FirInstr *i=mk_instr(cg,FIR_SUB); i->dst=dst;
            i->type=arena_strdup(cg->arena,oty);
            i->src1=arena_strdup(cg->arena,v); i->src2=arena_strdup(cg->arena,"1"); emit_instr(cg,i);
            if (n->unary.operand->kind==NODE_IDENT) {
                VarEntry *e=var_lookup(cg,n->unary.operand->ident.name);
                if(e) emit_store(cg,dst,e->slot,oty);
            }
            n->ty=n->unary.operand->ty;
            return n->unary.prefix ? dst : v;
        }
        n->ty=TY_INT; return v;
    }
    case NODE_BINARY: {
        char *op = n->binary.op;
        if (!strcmp(op,"&&")||!strcmp(op,"||")) {
            char *lv = emit_expr(cg, n->binary.left);
            const char *lty = ast_type_str(cg, n->binary.left);
            char *lcmp = newtmp(cg);
            FirInstr *ci = mk_instr(cg, FIR_ICMP);
            ci->dst=lcmp; ci->type=arena_strdup(cg->arena,"i1");
            ci->src1=arena_strdup(cg->arena,lv);
            ci->src2=arena_strdup(cg->arena,"0");
            ci->src3=arena_strdup(cg->arena,"ne"); emit_instr(cg,ci);
            if (strcmp(lty,"i1")!=0) lv=coerce_to(cg,lv,lty,"i1");
            char *rl=newlbl(cg,"sc_r"),*sl=newlbl(cg,"sc_s"),*el=newlbl(cg,"sc_e");
            if (!strcmp(op,"&&")) emit_br_cond(cg,lcmp,sl,rl);
            else emit_br_cond(cg,lcmp,rl,sl);
            new_block(cg,sl);
            char *rv = emit_expr(cg, n->binary.right);
            const char *rty = ast_type_str(cg, n->binary.right);
            if (strcmp(rty,"i1")!=0) rv=coerce_to(cg,rv,rty,"i1");
            emit_br(cg,el);
            new_block(cg,rl);
            char *shortval = !strcmp(op,"&&") ? "0" : "1";
            emit_br(cg,el);
            new_block(cg,el);
            char *phi_dst = newtmp(cg);
            FirInstr *phi = mk_instr(cg, FIR_PHI);
            phi->dst=phi_dst; phi->type=arena_strdup(cg->arena,"i1");
            phi->src1=arena_strdup(cg->arena,rv);
            phi->src2=arena_strdup(cg->arena,sl);
            phi->src3=arena_strdup(cg->arena,shortval);
            phi->args=arena_alloc(cg->arena,sizeof(char*));
            phi->args[0]=arena_strdup(cg->arena,rl);
            phi->nargs=1;
            emit_instr(cg,phi);
            n->ty=TY_BOOL; return phi_dst;
        }
        if (!strcmp(op,"+") && (n->binary.left->ty==TY_STR || n->binary.right->ty==TY_STR)) {
            char *lv=emit_expr(cg,n->binary.left);
            char *rv=emit_expr(cg,n->binary.right);
            FirInstr *ci=mk_instr(cg,FIR_CALL); ci->type=arena_strdup(cg->arena,"ptr");
            ci->src1=arena_strdup(cg->arena,"__fermi_concat"); ci->nargs=2;
            ci->args=arena_alloc(cg->arena,2*sizeof(char*)); ci->arg_types=arena_alloc(cg->arena,2*sizeof(char*));
            ci->args[0]=arena_strdup(cg->arena,lv); ci->arg_types[0]=arena_strdup(cg->arena,"ptr");
            ci->args[1]=arena_strdup(cg->arena,rv); ci->arg_types[1]=arena_strdup(cg->arena,"ptr");
            ci->dst=newtmp(cg); emit_instr(cg,ci);
            n->ty=TY_STR; return ci->dst;
        }
        char *lv = emit_expr(cg, n->binary.left);
        if (strcmp(ast_type_str(cg,n->binary.left),"ptr")==0 &&
            (!strcmp(op,"+") || !strcmp(op,"-"))) {
            if (!cg->in_unsafe) {
                fprintf(stderr,"%s:%d:%d: error[E0005]: pointer arithmetic on type 'ptr' requires enclosing 'unsafe' block\n",
                        cg->src_path,n->line,n->col);
                n->ty=TY_RAW_PTR; return arena_strdup(cg->arena,"null");
            }
            char *rv2=emit_expr(cg,n->binary.right);
            rv2=coerce_to(cg,rv2,ast_type_str(cg,n->binary.right),"i64");
            if (!strcmp(op,"-")) {
                char *neg=newtmp(cg);
                FirInstr *ni=mk_instr(cg,FIR_NEG);
                ni->dst=neg; ni->type=arena_strdup(cg->arena,"i64");
                ni->src1=arena_strdup(cg->arena,rv2); emit_instr(cg,ni);
                rv2=neg;
            }
            char *pdst=newtmp(cg);
            FirInstr *pa=mk_instr(cg,FIR_PTR_ADD);
            pa->dst=pdst; pa->type=arena_strdup(cg->arena,"ptr");
            pa->src1=arena_strdup(cg->arena,lv);
            pa->src2=arena_strdup(cg->arena,rv2); emit_instr(cg,pa);
            n->ty=TY_RAW_PTR; return pdst;
        }
        char *rv = emit_expr(cg, n->binary.right);
        const char *ty = type_for_op(cg, n->binary.left, n->binary.right);
        lv = coerce_to(cg, lv, ast_type_str(cg, n->binary.left), ty);
        rv = coerce_to(cg, rv, ast_type_str(cg, n->binary.right), ty);
        char *dst = newtmp(cg);
        FirOp fop = FIR_NOP;
        int is_cmp = 0;
        const char *cmp_pred = NULL;
        if (!strcmp(op,"+"))   fop=is_float_type(ty)?FIR_FADD:FIR_ADD;
        else if (!strcmp(op,"-")) fop=is_float_type(ty)?FIR_FSUB:FIR_SUB;
        else if (!strcmp(op,"*")) fop=is_float_type(ty)?FIR_FMUL:FIR_MUL;
        else if (!strcmp(op,"/")) fop=is_float_type(ty)?FIR_FDIV:FIR_DIV;
        else if (!strcmp(op,"%")) fop=FIR_MOD;
        else if (!strcmp(op,"&")) fop=FIR_AND;
        else if (!strcmp(op,"|")) fop=FIR_OR;
        else if (!strcmp(op,"^")) fop=FIR_XOR;
        else if (!strcmp(op,"<<")) fop=FIR_SHL;
        else if (!strcmp(op,">>")) fop=FIR_SHR;
        else if (!strcmp(op,">>>")) fop=FIR_LSHR;
        else if (!strcmp(op,"==")) { is_cmp=1; cmp_pred=is_float_type(ty)?"oeq":"eq"; }
        else if (!strcmp(op,"!=")) { is_cmp=1; cmp_pred=is_float_type(ty)?"one":"ne"; }
        else if (!strcmp(op,"<"))  { is_cmp=1; cmp_pred=is_float_type(ty)?"olt":"slt"; }
        else if (!strcmp(op,">"))  { is_cmp=1; cmp_pred=is_float_type(ty)?"ogt":"sgt"; }
        else if (!strcmp(op,"<=")) { is_cmp=1; cmp_pred=is_float_type(ty)?"ole":"sle"; }
        else if (!strcmp(op,">=")) { is_cmp=1; cmp_pred=is_float_type(ty)?"oge":"sge"; }
        else if (!strcmp(op,"..") || !strcmp(op,"..=")) {
            n->ty=TY_INT; return lv;
        }
        if (is_cmp) {
            FirInstr *i = mk_instr(cg, is_float_type(ty)?FIR_FCMP:FIR_ICMP);
            i->dst=dst; i->type=arena_strdup(cg->arena,ty);
            i->src1=arena_strdup(cg->arena,lv);
            i->src2=arena_strdup(cg->arena,rv);
            i->src3=arena_strdup(cg->arena,cmp_pred);
            emit_instr(cg,i); n->ty=TY_BOOL; return dst;
        }
        FirInstr *i = mk_instr(cg, fop);
        i->dst=dst; i->type=arena_strdup(cg->arena,ty);
        i->src1=arena_strdup(cg->arena,lv);
        i->src2=arena_strdup(cg->arena,rv);
        emit_instr(cg,i);
        if (!strcmp(ty,"i64")) n->ty=TY_LONG;
        else if (!strcmp(ty,"float")) n->ty=TY_FLOAT;
        else if (!strcmp(ty,"double")) n->ty=TY_DOUBLE;
        else n->ty=TY_INT;
        return dst;
    }
    case NODE_ASSIGN: {
          AstNode *lhs = n->assign.left;
          char *rv = emit_expr(cg, n->assign.right);
          const char *rty = ast_type_str(cg, n->assign.right);
          if (lhs->kind==NODE_IDENT) {
              VarEntry *e = var_lookup(cg, lhs->ident.name);
              if (e) {
                  if (e->is_mut && strcmp(e->type,"ptr")==0 && strcmp(rty,"ptr")==0) {
                      
                      char *old_v = emit_load(cg, e->slot, "ptr");
                      char *new_heap = emit_mut_str_replace(cg, old_v, rv);
                      emit_store(cg, new_heap, e->slot, "ptr");
                  } else {
                      rv = coerce_to(cg, rv, rty, e->type);
                      emit_store(cg, rv, e->slot, e->type);
                  }
                  n->ty = TY_VOID; return rv;
              }
          }
        if (lhs->kind==NODE_FIELD) {
            AstNode *obj=lhs->field.obj;
            const char *obj_ty_name=NULL;
            char *op;
            if(obj->kind==NODE_IDENT) {
                VarEntry *ev=var_lookup(cg,obj->ident.name);
                if(ev && strncmp(ev->type,"%struct.",8)==0) {
                    obj_ty_name=ev->type+8;
                    op=arena_strdup(cg->arena,ev->slot);
                } else if(ev && strcmp(ev->type,"ptr")==0 &&
                          strcmp(obj->ident.name,"self")==0 && cg->cur_struct) {
                    obj_ty_name=cg->cur_struct;
                    op=emit_expr(cg,obj);
                } else {
                    op=emit_expr(cg,obj);
                    const char *n_ty=obj->ty_name;
                    if(n_ty && strncmp(n_ty,"%struct.",8)==0) obj_ty_name=n_ty+8;
                }
            } else {
                op=emit_expr(cg,obj);
                const char *n_ty=obj->ty_name;
                if(n_ty && strncmp(n_ty,"%struct.",8)==0) obj_ty_name=n_ty+8;
            }
            if(obj_ty_name){
                StructInfo *si=find_struct(cg,obj_ty_name);
                if(si){
                    int fidx=-1;
                    for(int k=0;k<si->nfields;k++)
                        if(strcmp(si->fields[k],lhs->field.field)==0){fidx=k;break;}
                    if(fidx>=0){
                        char fidx_s[16]; snprintf(fidx_s,sizeof(fidx_s),"%d",fidx);
                        char *fptr=newtmp(cg);
                        FirInstr *gep=mk_instr(cg,FIR_GEP_STRUCT);
                        gep->dst=fptr; gep->type=arena_strdup(cg->arena,obj_ty_name);
                        gep->src1=arena_strdup(cg->arena,op);
                        gep->src3=arena_strdup(cg->arena,fidx_s); emit_instr(cg,gep);
                        const char *fty=si->field_types[fidx];
                        rv=coerce_to(cg,rv,rty,fty);
                        emit_store(cg,rv,fptr,fty);
                        n->ty=TY_VOID; return rv;
                    }
                }
            }
        }
        if (lhs->kind==NODE_DEREF) {
            char *ptr=emit_expr(cg,lhs->deref_expr.expr);
            if (!cg->in_unsafe) {
                fprintf(stderr,"%s:%d:%d: error[E0006]: dereference-assignment of raw pointer requires enclosing 'unsafe' block\n",
                        cg->src_path,n->line,n->col);
            }
            rv=coerce_to(cg,rv,rty,"i32");
            emit_store(cg,rv,ptr,"i32");
            n->ty=TY_VOID; return rv;
        }
        if (lhs->kind==NODE_INDEX) {
            char *arr=emit_expr(cg,lhs->index.obj);
            char *idx=emit_expr(cg,lhs->index.idx);
            const char *ety="i8";
            if(lhs->index.obj->kind==NODE_IDENT){
                VarEntry *e=var_lookup(cg,lhs->index.obj->ident.name);
                if(e) ety=e->type;
            }
            char *ptr=newtmp(cg);
            FirInstr *gep=mk_instr(cg,FIR_GEP);
            gep->dst=ptr; gep->type=arena_strdup(cg->arena,ety);
            gep->src1=arena_strdup(cg->arena,arr);
            gep->src2=arena_strdup(cg->arena,idx); emit_instr(cg,gep);
            rv=coerce_to(cg,rv,rty,ety);
            emit_store(cg,rv,ptr,ety);
            n->ty=TY_VOID; return rv;
        }
        n->ty=TY_VOID; return rv;
    }
    case NODE_COMPOUND_ASSIGN: {
        AstNode *lhs=n->compound_assign.left;
        VarEntry *e=(lhs->kind==NODE_IDENT)?var_lookup(cg,lhs->ident.name):NULL;
        char *lv=e?emit_load(cg,e->slot,e->type):emit_expr(cg,lhs);
        char *rv=emit_expr(cg,n->compound_assign.right);
        const char *ty=e?e->type:ast_type_str(cg,lhs);
        rv=coerce_to(cg,rv,ast_type_str(cg,n->compound_assign.right),ty);
        char *dst=newtmp(cg);
        char *op=n->compound_assign.op;
        FirOp fop=FIR_ADD;
        if (!strcmp(op,"+=")||!strcmp(op,"+=")) fop=is_float_type(ty)?FIR_FADD:FIR_ADD;
        else if (!strcmp(op,"-=")) fop=is_float_type(ty)?FIR_FSUB:FIR_SUB;
        else if (!strcmp(op,"*=")) fop=is_float_type(ty)?FIR_FMUL:FIR_MUL;
        else if (!strcmp(op,"/=")) fop=is_float_type(ty)?FIR_FDIV:FIR_DIV;
        else if (!strcmp(op,"%=")) fop=FIR_MOD;
        else if (!strcmp(op,"&=")) fop=FIR_AND;
        else if (!strcmp(op,"|=")) fop=FIR_OR;
        else if (!strcmp(op,"^=")) fop=FIR_XOR;
        else if (!strcmp(op,"<<=")) fop=FIR_SHL;
        else if (!strcmp(op,">>=")) fop=FIR_SHR;
        FirInstr *i=mk_instr(cg,fop); i->dst=dst; i->type=arena_strdup(cg->arena,ty);
        i->src1=arena_strdup(cg->arena,lv); i->src2=arena_strdup(cg->arena,rv);
        emit_instr(cg,i);
        if (e) emit_store(cg,dst,e->slot,ty);
        n->ty=TY_VOID; return dst;
    }
    case NODE_CALL: {
        AstNode *callee = n->call.callee;
        if (callee->kind==NODE_FIELD) {
            AstNode *obj=callee->field.obj;
            const char *method=callee->field.field;
            char *op=emit_expr(cg,obj);
            const char *obj_ty_name=obj->ty_name;
            if(obj_ty_name && strncmp(obj_ty_name,"%struct.",8)==0) obj_ty_name+=8;
            else if(obj_ty_name && strncmp(obj_ty_name,"struct.",7)==0) obj_ty_name+=7;
            if(!obj_ty_name && obj->kind==NODE_IDENT) {
                VarEntry *ev=var_lookup(cg,obj->ident.name);
                if(ev&&strncmp(ev->type,"struct.",7)==0) obj_ty_name=ev->type+7;
                else if(ev&&strncmp(ev->type,"%struct.",8)==0) obj_ty_name=ev->type+8;
            }
            char fn_name[256];
            if (obj_ty_name) snprintf(fn_name,sizeof(fn_name),"%s__%s",obj_ty_name,method);
            else snprintf(fn_name,sizeof(fn_name),"__method_%s",method);
            int na=1; char *argv[64]; const char *tyv[64];
            if (obj->kind==NODE_IDENT) {
                VarEntry *ev=var_lookup(cg,obj->ident.name);
                if(ev) argv[0]=arena_strdup(cg->arena,ev->slot);
                else argv[0]=arena_strdup(cg->arena,op);
            } else {
                argv[0]=arena_strdup(cg->arena,op);
            }
            tyv[0]="ptr";
            for(AstList *al=n->call.args;al&&na<64;al=al->next){
                argv[na]=emit_expr(cg,al->node);
                tyv[na]=ast_type_str(cg,al->node);
                na++;
            }
            const char *ret_ty="void";
            for(FirFn *fn=cg->mod.fns;fn;fn=fn->next)
                if(strcmp(fn->name,fn_name)==0){ret_ty=fn->ret_type?fn->ret_type:"void";break;}
            FirInstr *ci=mk_instr(cg,FIR_CALL);
            ci->type=arena_strdup(cg->arena,ret_ty);
            ci->src1=arena_strdup(cg->arena,fn_name);
            ci->nargs=na;
            ci->args=arena_alloc(cg->arena,(size_t)na*sizeof(char*));
            ci->arg_types=arena_alloc(cg->arena,(size_t)na*sizeof(char*));
            for(int j=0;j<na;j++){
                ci->args[j]=arena_strdup(cg->arena,argv[j]);
                ci->arg_types[j]=arena_strdup(cg->arena,tyv[j]);
            }
            ci->dst=newtmp(cg); emit_instr(cg,ci);
            if(strcmp(ret_ty,"void")==0){n->ty=TY_VOID;return arena_strdup(cg->arena,"0");}
            if(strcmp(ret_ty,"i32")==0) n->ty=TY_INT;
            else if(strcmp(ret_ty,"i64")==0) n->ty=TY_LONG;
            else if(strcmp(ret_ty,"float")==0) n->ty=TY_FLOAT;
            else if(strcmp(ret_ty,"double")==0) n->ty=TY_DOUBLE;
            else if(strcmp(ret_ty,"i1")==0) n->ty=TY_BOOL;
            else if(strcmp(ret_ty,"ptr")==0) n->ty=TY_STR;
            else n->ty=TY_INT;
            return ci->dst;
        }
        if (callee->kind==NODE_NS_CALL) {
            return emit_ns_call(cg, n, callee->ns_call.ns, callee->ns_call.func, n->call.args);
        }
        if (callee->kind==NODE_IDENT) {
            const char *fn = callee->ident.name;
            AstNode *a0=n->call.args?n->call.args->node:NULL;
            AstNode *a1=(n->call.args&&n->call.args->next)?n->call.args->next->node:NULL;
            (void)a1;
            if (!strcmp(fn,"Ok")) {
                char *v=a0?emit_expr(cg,a0):arena_strdup(cg->arena,"0");
                char *dst=newtmp(cg);
                FirInstr *i=mk_instr(cg,FIR_SEXT); i->dst=dst;
                i->type=arena_strdup(cg->arena,"i64");
                i->src1=arena_strdup(cg->arena,v);
                i->src2=arena_strdup(cg->arena,a0?ast_type_str(cg,a0):"i32");
                emit_instr(cg,i); n->ty=TY_LONG; return dst;
            }
            if (!strcmp(fn,"Err")) {
                char *v=a0?emit_expr(cg,a0):arena_strdup(cg->arena,"0");
                char *ext=newtmp(cg);
                FirInstr *se=mk_instr(cg,FIR_SEXT); se->dst=ext;
                se->type=arena_strdup(cg->arena,"i64");
                se->src1=arena_strdup(cg->arena,v);
                se->src2=arena_strdup(cg->arena,a0?ast_type_str(cg,a0):"i32"); emit_instr(cg,se);
                char *shifted=newtmp(cg);
                FirInstr *sh=mk_instr(cg,FIR_SHL); sh->dst=shifted;
                sh->type=arena_strdup(cg->arena,"i64");
                sh->src1=arena_strdup(cg->arena,ext);
                sh->src2=arena_strdup(cg->arena,"32"); emit_instr(cg,sh);
                char *tagged=newtmp(cg);
                FirInstr *oi=mk_instr(cg,FIR_OR); oi->dst=tagged;
                oi->type=arena_strdup(cg->arena,"i64");
                oi->src1=arena_strdup(cg->arena,shifted);
                oi->src2=arena_strdup(cg->arena,"1"); emit_instr(cg,oi);
                n->ty=TY_LONG; return tagged;
            }
            {
                EnumInfo *ei = NULL;
                int vi = find_enum_variant(cg, fn, &ei);
                if (vi >= 0 && ei) {
                    char tag_s[32]; snprintf(tag_s, sizeof(tag_s), "%d", vi * 2);
                    if (a0) {
                        char *v = emit_expr(cg, a0);
                        const char *aty = ast_type_str(cg, a0);
                        char *ext = newtmp(cg);
                        FirInstr *se = mk_instr(cg, FIR_SEXT);
                        se->dst = ext; se->type = arena_strdup(cg->arena, "i64");
                        se->src1 = arena_strdup(cg->arena, v);
                        se->src2 = arena_strdup(cg->arena, aty); emit_instr(cg, se);
                        char *shifted = newtmp(cg);
                        FirInstr *sh = mk_instr(cg, FIR_SHL); sh->dst = shifted;
                        sh->type = arena_strdup(cg->arena, "i64");
                        sh->src1 = arena_strdup(cg->arena, ext);
                        sh->src2 = arena_strdup(cg->arena, "32"); emit_instr(cg, sh);
                        char *tagged = newtmp(cg);
                        FirInstr *oi = mk_instr(cg, FIR_OR); oi->dst = tagged;
                        oi->type = arena_strdup(cg->arena, "i64");
                        oi->src1 = arena_strdup(cg->arena, shifted);
                        oi->src2 = arena_strdup(cg->arena, tag_s); emit_instr(cg, oi);
                        n->ty = TY_LONG; return tagged;
                    } else {
                        char *dst = newtmp(cg);
                        FirInstr *li = mk_instr(cg, FIR_SEXT);
                        li->dst = dst; li->type = arena_strdup(cg->arena, "i64");
                        li->src1 = arena_strdup(cg->arena, tag_s);
                        li->src2 = arena_strdup(cg->arena, "i32"); emit_instr(cg, li);
                        n->ty = TY_LONG; return dst;
                    }
                }
            }
        }
        if (callee->kind==NODE_IDENT) {
            VarEntry *fpe = var_lookup(cg, callee->ident.name);
            if (fpe && strcmp(fpe->type,"ptr")==0) {
                char *fp = emit_load(cg, fpe->slot, "ptr");
                int na=0; char *argv[64]; const char *tyv[64];
                for(AstList *al=n->call.args;al&&na<64;al=al->next){
                    argv[na]=emit_expr(cg,al->node);
                    tyv[na]=ast_type_str(cg,al->node);
                    na++;
                }
                FirInstr *ci=mk_instr(cg,FIR_CALL_INDIRECT);
                ci->type=arena_strdup(cg->arena,"i64");
                ci->src1=arena_strdup(cg->arena,fp);
                ci->nargs=na;
                ci->args=na?arena_alloc(cg->arena,(size_t)na*sizeof(char*)):NULL;
                ci->arg_types=na?arena_alloc(cg->arena,(size_t)na*sizeof(char*)):NULL;
                for(int j=0;j<na;j++){
                    ci->args[j]=arena_strdup(cg->arena,argv[j]);
                    ci->arg_types[j]=arena_strdup(cg->arena,tyv[j]);
                }
                ci->dst=newtmp(cg); emit_instr(cg,ci);
                n->ty=TY_UNKNOWN; return ci->dst;
            }
        }
        {
            const char *fn_name=NULL;
            if(callee->kind==NODE_IDENT) fn_name=callee->ident.name;
            if(!fn_name){n->ty=TY_INT;return arena_strdup(cg->arena,"0");}
            int na=0; char *argv[64]; const char *tyv[64];
            for(AstList *al=n->call.args;al&&na<64;al=al->next){
                argv[na]=emit_expr(cg,al->node);
                tyv[na]=ast_type_str(cg,al->node);
                na++;
            }
            
            const char *actual_fn=fn_name;
            if(!strcmp(fn_name,"alloc")){
                actual_fn=cg->in_region?"fe_region_alloc":"fe_alloc";
            } else if(!strcmp(fn_name,"realloc")){
                actual_fn=cg->in_region?"fe_region_alloc":"fe_realloc";
            }
            
            static const struct { const char *name; const char *ret; } rt_types[]={
                {"fe_alloc","ptr"},{"fe_realloc","ptr"},{"fe_region_alloc","ptr"},
                {"malloc","ptr"},{"calloc","ptr"},{"realloc","ptr"},
                {"fe_input","ptr"},{"fe_concat","ptr"},{"fe_to_upper","ptr"},
                {"fe_to_lower","ptr"},{"fe_trim","ptr"},{"fe_slice","ptr"},
                {"fe_replace","ptr"},{"fe_int_to_str","ptr"},{"fe_float_to_str","ptr"},
                {"fe_getcwd","ptr"},{"fe_read_file","ptr"},
                {"fe_str_split","ptr"},{"fe_split","ptr"},
                {"memcpy","ptr"},{"memmove","ptr"},{"memset","ptr"},
                {"strcpy","ptr"},{"strcat","ptr"},{"strstr","ptr"},
                {"getenv","ptr"},{"fgets","ptr"},{"fopen","ptr"},
                {"fe_len","i32"},{"fe_contains","i32"},{"fe_starts_with","i32"},
                {"fe_ends_with","i32"},{"fe_index_of","i32"},{"fe_parse_int","i32"},
                {"fe_time","i64"},{"fe_sleep","void"},{"fe_flush","void"},
                {"free","void"},{"exit","void"},
                {NULL,NULL}
            };
            const char *ret_ty="i32";
            for(FirFn *fn=cg->mod.fns;fn;fn=fn->next)
                if(strcmp(fn->name,fn_name)==0){ret_ty=fn->ret_type?fn->ret_type:"void";break;}
            if(!strcmp(ret_ty,"i32")){
                for(int _ri=0;rt_types[_ri].name;_ri++)
                    if(!strcmp(actual_fn,rt_types[_ri].name)){ret_ty=rt_types[_ri].ret;break;}
            }
            fn_name=actual_fn;
            
            for(FirFn *fn=cg->mod.fns;fn;fn=fn->next) {
                if(strcmp(fn->name,fn_name)!=0) continue;
                FirParam *pm=fn->params; int pi=0;
                for(;pm&&pi<na;pm=pm->next,pi++) {
                    if(pm->type&&tyv[pi]&&strcmp(pm->type,tyv[pi])!=0) {
                        argv[pi]=coerce_to(cg,argv[pi],tyv[pi],pm->type);
                        tyv[pi]=pm->type;
                    }
                }
                break;
            }
            FirInstr *ci=mk_instr(cg,FIR_CALL);
            ci->type=arena_strdup(cg->arena,ret_ty);
            ci->src1=arena_strdup(cg->arena,fn_name);
            ci->nargs=na;
            ci->args=na?arena_alloc(cg->arena,(size_t)na*sizeof(char*)):NULL;
            ci->arg_types=na?arena_alloc(cg->arena,(size_t)na*sizeof(char*)):NULL;
            for(int j=0;j<na;j++){
                ci->args[j]=arena_strdup(cg->arena,argv[j]);
                ci->arg_types[j]=arena_strdup(cg->arena,tyv[j]);
            }
            ci->dst=newtmp(cg); emit_instr(cg,ci);
            if(strcmp(ret_ty,"void")==0){n->ty=TY_VOID;return arena_strdup(cg->arena,"0");}
            if(strcmp(ret_ty,"i32")==0) n->ty=TY_INT;
            else if(strcmp(ret_ty,"i64")==0) n->ty=TY_LONG;
            else if(strcmp(ret_ty,"float")==0) n->ty=TY_FLOAT;
            else if(strcmp(ret_ty,"double")==0) n->ty=TY_DOUBLE;
            else if(strcmp(ret_ty,"i1")==0) n->ty=TY_BOOL;
            else if(strcmp(ret_ty,"ptr")==0) n->ty=TY_STR;
            else { n->ty=TY_NAMED; n->ty_name=(char*)ret_ty; }
            return ci->dst;
        }
    }
    case NODE_AT_CALL: {
        const char *_an = n->at_call.name;
        AstNode *_a0 = n->at_call.args ? n->at_call.args->node : NULL;
        AstNode *_a1 = (n->at_call.args && n->at_call.args->next) ? n->at_call.args->next->node : NULL;
        if (!strcmp(_an,"panic")) {
            char *_msg;
            if (_a0) { _msg=emit_expr(cg,_a0); }
            else {
                _msg=newtmp(cg);
                FirInstr *_si=mk_instr(cg,FIR_CONST_STR); _si->dst=_msg;
                _si->src1=arena_strdup(cg->arena,"panic"); emit_instr(cg,_si);
            }
            char *_pa[1]={_msg}; const char *_pt[1]={"ptr"};
            emit_rt_call(cg,"__fermi_panic","void",1,_pa,_pt);
            FirInstr *_ur=mk_instr(cg,FIR_UNREACHABLE); emit_instr(cg,_ur);
            char *_dl=newlbl(cg,"panicd"); new_block(cg,_dl);
            n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
        }
        if (!strcmp(_an,"assert")) {
            char *_cond=_a0?emit_expr(cg,_a0):arena_strdup(cg->arena,"0");
            char *_okl=newlbl(cg,"aok"); char *_fll=newlbl(cg,"afail");
            emit_br_cond(cg,_cond,_okl,_fll);
            new_block(cg,_fll);
            char *_msg;
            if (_a1) { _msg=emit_expr(cg,_a1); }
            else {
                _msg=newtmp(cg);
                FirInstr *_si=mk_instr(cg,FIR_CONST_STR); _si->dst=_msg;
                _si->src1=arena_strdup(cg->arena,"assertion failed"); emit_instr(cg,_si);
            }
            char *_pa[1]={_msg}; const char *_pt[1]={"ptr"};
            emit_rt_call(cg,"__fermi_panic","void",1,_pa,_pt);
            FirInstr *_ur=mk_instr(cg,FIR_UNREACHABLE); emit_instr(cg,_ur);
            new_block(cg,_okl);
            n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
        }
        if (!strcmp(_an,"unreachable")) {
            FirInstr *_ur=mk_instr(cg,FIR_UNREACHABLE); emit_instr(cg,_ur);
            char *_dl=newlbl(cg,"unrd"); new_block(cg,_dl);
            n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
        }
        if (!strcmp(_an,"sizeof")||!strcmp(_an,"alignof")) {
            long long _sz=tynode_sizeof(n->at_call.type_arg);
            char _buf[32]; snprintf(_buf,sizeof(_buf),"%lld",_sz);
            n->ty=TY_LONG; return arena_strdup(cg->arena,_buf);
        }
        if (!strcmp(_an,"typeof")) {
            const char *_tn="unknown";
            if (n->at_call.type_arg) {
                switch(n->at_call.type_arg->kind){
                case TY_INT: _tn="int"; break; case TY_UINT: _tn="uint"; break;
                case TY_LONG: _tn="long"; break; case TY_ULONG: _tn="ulong"; break;
                case TY_FLOAT: _tn="float"; break; case TY_DOUBLE: _tn="double"; break;
                case TY_BOOL: _tn="bool"; break; case TY_BYTE: _tn="byte"; break;
                case TY_CHAR: _tn="char"; break; case TY_SHORT: _tn="short"; break;
                case TY_STR: _tn="str"; break; case TY_RAW_PTR: _tn="ptr"; break;
                default: _tn="unknown"; break;
                }
            }
            char *_dst=newtmp(cg);
            FirInstr *_si=mk_instr(cg,FIR_CONST_STR); _si->dst=_dst;
            _si->src1=arena_strdup(cg->arena,_tn); emit_instr(cg,_si);
            n->ty=TY_STR; return _dst;
        }
        if (!strcmp(_an,"memcpy") || !strcmp(_an,"memcopy")) {
            AstNode *a2=(n->at_call.args&&n->at_call.args->next&&n->at_call.args->next->next)
                        ?n->at_call.args->next->next->node:NULL;
            char *dst_ptr=_a0?emit_expr(cg,_a0):arena_strdup(cg->arena,"null");
            char *src_ptr=_a1?emit_expr(cg,_a1):arena_strdup(cg->arena,"null");
            char *sz_v=a2?emit_expr(cg,a2):arena_strdup(cg->arena,"0");
            sz_v=coerce_to(cg,sz_v,a2?ast_type_str(cg,a2):"i32","i64");
            char *pa[3]={dst_ptr,src_ptr,sz_v};
            const char *pt[3]={"ptr","ptr","i64"};
            emit_rt_call(cg,"memcpy","ptr",3,pa,pt);
            n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
        }
        if (!strcmp(_an,"memset")) {
            AstNode *a2=(n->at_call.args&&n->at_call.args->next&&n->at_call.args->next->next)
                        ?n->at_call.args->next->next->node:NULL;
            char *dst_ptr=_a0?emit_expr(cg,_a0):arena_strdup(cg->arena,"null");
            char *val_v=_a1?emit_expr(cg,_a1):arena_strdup(cg->arena,"0");
            val_v=coerce_to(cg,val_v,_a1?ast_type_str(cg,_a1):"i32","i32");
            char *sz_v=a2?emit_expr(cg,a2):arena_strdup(cg->arena,"0");
            sz_v=coerce_to(cg,sz_v,a2?ast_type_str(cg,a2):"i32","i64");
            char *pa[3]={dst_ptr,val_v,sz_v};
            const char *pt[3]={"ptr","i32","i64"};
            emit_rt_call(cg,"memset","ptr",3,pa,pt);
            n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
        }
        if (!strcmp(_an,"select")) {
            if (!_a0 || !_a1) { n->ty=TY_INT; return arena_strdup(cg->arena,"0"); }
            AstNode *a2=(n->at_call.args&&n->at_call.args->next&&n->at_call.args->next->next)
                        ?n->at_call.args->next->next->node:NULL;
            char *cond_v=emit_expr(cg,_a0);
            cond_v=coerce_to(cg,cond_v,ast_type_str(cg,_a0),"i1");
            char *tv=emit_expr(cg,_a1);
            char *fv=a2?emit_expr(cg,a2):arena_strdup(cg->arena,"0");
            const char *sty=ast_type_str(cg,_a1);
            char *dst=newtmp(cg);
            FirInstr *sel=mk_instr(cg,FIR_SELECT);
            sel->dst=dst; sel->type=arena_strdup(cg->arena,sty);
            sel->src1=arena_strdup(cg->arena,cond_v);
            sel->src2=arena_strdup(cg->arena,tv);
            sel->src3=arena_strdup(cg->arena,fv);
            emit_instr(cg,sel);
            n->ty=TY_INT; return dst;
        }
        fprintf(stderr,"%s:%d:%d: error[E0007]: unknown intrinsic identifier '@%s'\n",cg->src_path,n->line,n->col,_an);
        n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
    }
    case NODE_NS_CALL:
        return emit_ns_call(cg, n, n->ns_call.ns, n->ns_call.func, n->ns_call.args);
    case NODE_CAST: {
        char *v=emit_expr(cg,n->cast.expr);
        const char *from=ast_type_str(cg,n->cast.expr);
        const char *to=typenode_to_llvm(cg,n->cast.type);
        n->ty=n->cast.type?n->cast.type->kind:TY_INT;
        if (strcmp(from,"ptr")==0 && is_int_type(to)) {
            if (!cg->in_unsafe) {
                fprintf(stderr,"%s:%d:%d: error[E0008]: pointer-to-integer cast requires enclosing 'unsafe' block\n",
                        cg->src_path,n->line,n->col);
                return arena_strdup(cg->arena,"0");
            }
            char *dst=newtmp(cg);
            FirInstr *ci=mk_instr(cg,FIR_PTRTOINT);
            ci->dst=dst; ci->type=arena_strdup(cg->arena,to);
            ci->src1=arena_strdup(cg->arena,v);
            emit_instr(cg,ci); n->ty=TY_LONG; return dst;
        }
        if (is_int_type(from) && strcmp(to,"ptr")==0) {
            if (!cg->in_unsafe) {
                fprintf(stderr,"%s:%d:%d: error[E0009]: integer-to-pointer cast requires enclosing 'unsafe' block\n",
                        cg->src_path,n->line,n->col);
                return arena_strdup(cg->arena,"null");
            }
            char *dst=newtmp(cg);
            FirInstr *ci=mk_instr(cg,FIR_INTTOPTR);
            ci->dst=dst; ci->type=arena_strdup(cg->arena,"ptr");
            ci->src1=arena_strdup(cg->arena,v);
            ci->src2=arena_strdup(cg->arena,from);
            emit_instr(cg,ci); n->ty=TY_RAW_PTR; return dst;
        }
        return coerce_to(cg,v,from,to);
    }
    case NODE_STRUCT_INIT: {
        StructInfo *si=find_struct(cg,n->struct_init.type_name);
        if(!si){n->ty=TY_INT;return arena_strdup(cg->arena,"0");}
        char slot[128]; snprintf(slot,sizeof(slot),"tmp.struct.%d",cg->tmp++);
        char stype[128]; snprintf(stype,sizeof(stype),"%%struct.%s",n->struct_init.type_name);
        emit_alloca(cg,slot,stype);
        int fi=0;
        for(AstList *fl=n->struct_init.fields;fl;fl=fl->next,fi++){
            AstNode *fv=fl->node; if(!fv)continue;
            if(fi>=si->nfields)break;
            char *val=emit_expr(cg,fv);
            const char *vty=ast_type_str(cg,fv);
            const char *fty=si->field_types[fi];
            val=coerce_to(cg,val,vty,fty);
            char fidx_s[16]; snprintf(fidx_s,sizeof(fidx_s),"%d",fi);
            char *fptr=newtmp(cg);
            FirInstr *gep=mk_instr(cg,FIR_GEP_STRUCT);
            gep->dst=fptr; gep->type=arena_strdup(cg->arena,n->struct_init.type_name);
            gep->src1=arena_strdup(cg->arena,slot);
            gep->src3=arena_strdup(cg->arena,fidx_s); emit_instr(cg,gep);
            emit_store(cg,val,fptr,fty);
        }
        n->ty=TY_NAMED; n->ty_name=n->struct_init.type_name;
        return arena_strdup(cg->arena,slot);
    }
    case NODE_ARRAY_LIT: {
        int nelem=0;
        for(AstList *el=n->array_lit.elems;el;el=el->next) nelem++;
        const char *ety="i32";
        if(n->array_lit.elems) {
            emit_expr(cg,n->array_lit.elems->node);
            ety=ast_type_str(cg,n->array_lit.elems->node);
        }
        char slot[128]; snprintf(slot,sizeof(slot),"tmp.arr.%d",cg->tmp++);
        char arrty[128]; snprintf(arrty,sizeof(arrty),"[%d x %s]",nelem>0?nelem:1,ety);
        emit_alloca(cg,slot,arrty);
        int idx=0;
        for(AstList *el=n->array_lit.elems;el;el=el->next,idx++){
            char *ev=emit_expr(cg,el->node);
            const char *evty=ast_type_str(cg,el->node);
            ev=coerce_to(cg,ev,evty,ety);
            char *eptr=newtmp(cg);
            FirInstr *gep=mk_instr(cg,FIR_GEP);
            gep->dst=eptr; gep->type=arena_strdup(cg->arena,ety);
            gep->src1=arena_strdup(cg->arena,slot);
            char idxs[16]; snprintf(idxs,sizeof(idxs),"%d",idx);
            gep->src2=arena_strdup(cg->arena,idxs); emit_instr(cg,gep);
            emit_store(cg,ev,eptr,ety);
        }
        n->ty=TY_ARRAY_DYN; return arena_strdup(cg->arena,slot);
    }
    case NODE_FIELD: {
        AstNode *obj=n->field.obj;
        const char *obj_ty_name=NULL;
        char *op;
        if(obj->kind==NODE_IDENT) {
            VarEntry *ev=var_lookup(cg,obj->ident.name);
            if(ev && strncmp(ev->type,"%struct.",8)==0) {
                obj_ty_name=ev->type+8;
                op=arena_strdup(cg->arena,ev->slot);
                obj->ty=TY_NAMED; obj->ty_name=ev->type;
            } else if(ev && strcmp(ev->type,"ptr")==0 &&
                      strcmp(obj->ident.name,"self")==0 && cg->cur_struct) {
                obj_ty_name=cg->cur_struct;
                op=emit_expr(cg,obj);
            } else {
                op=emit_expr(cg,obj);
                const char *n_ty=obj->ty_name;
                if(n_ty && strncmp(n_ty,"%struct.",8)==0) obj_ty_name=n_ty+8;
            }
        } else {
            op=emit_expr(cg,obj);
            const char *n_ty=obj->ty_name;
            if(n_ty && strncmp(n_ty,"%struct.",8)==0) obj_ty_name=n_ty+8;
        }
        if(obj_ty_name){
            StructInfo *si=find_struct(cg,obj_ty_name);
            if(si){
                int fidx=-1;
                for(int k=0;k<si->nfields;k++)
                    if(strcmp(si->fields[k],n->field.field)==0){fidx=k;break;}
                if(fidx>=0){
                    char fidx_s[16]; snprintf(fidx_s,sizeof(fidx_s),"%d",fidx);
                    char *fptr=newtmp(cg);
                    FirInstr *gep=mk_instr(cg,FIR_GEP_STRUCT);
                    gep->dst=fptr; gep->type=arena_strdup(cg->arena,obj_ty_name);
                    gep->src1=arena_strdup(cg->arena,op);
                    gep->src3=arena_strdup(cg->arena,fidx_s); emit_instr(cg,gep);
                    const char *fty=si->field_types[fidx];
                    char *fv=emit_load(cg,fptr,fty);
                    if(strcmp(fty,"i32")==0)n->ty=TY_INT;
                    else if(strcmp(fty,"i64")==0)n->ty=TY_LONG;
                    else if(strcmp(fty,"float")==0)n->ty=TY_FLOAT;
                    else if(strcmp(fty,"double")==0)n->ty=TY_DOUBLE;
                    else if(strcmp(fty,"ptr")==0)n->ty=TY_STR;
                    else if(strcmp(fty,"i1")==0)n->ty=TY_BOOL;
                    else { n->ty=TY_NAMED; }
                    return fv;
                }
            }
        }
        n->ty=TY_INT;
        return arena_strdup(cg->arena,"0");
    }
    case NODE_INDEX: {
        char *arr=emit_expr(cg,n->index.obj);
        char *idx=emit_expr(cg,n->index.idx);
        const char *ety="i8";
        if(n->index.obj->kind==NODE_IDENT){
            VarEntry *e=var_lookup(cg,n->index.obj->ident.name);
            if(e) ety=e->type;
        }
        char *ptr=newtmp(cg);
        FirInstr *gep=mk_instr(cg,FIR_GEP);
        gep->dst=ptr; gep->type=arena_strdup(cg->arena,ety);
        gep->src1=arena_strdup(cg->arena,arr);
        gep->src2=arena_strdup(cg->arena,idx); emit_instr(cg,gep);
        char *v=emit_load(cg,ptr,ety);
        n->ty=TY_CHAR; return v;
    }
    case NODE_REF: {
        AstNode *e=n->ref_expr.expr;
        if(e->kind==NODE_IDENT){
            VarEntry *ev=var_lookup(cg,e->ident.name);
            if(ev){ n->ty=TY_RAW_PTR; return arena_strdup(cg->arena,ev->slot); }
        }
        char *v=emit_expr(cg,e); n->ty=TY_RAW_PTR; return v;
    }
    case NODE_DEREF: {
        char *ptr=emit_expr(cg,n->deref_expr.expr);
        const char *pty=ast_type_str(cg,n->deref_expr.expr);
        const char *inner_ty=(strcmp(pty,"ptr")==0)?"i32":pty;
        if (!cg->in_unsafe) {
            fprintf(stderr,"%s:%d:%d: error[E0010]: raw pointer dereference requires enclosing 'unsafe' block\n",
                    cg->src_path,n->line,n->col);
        }
        char *v=emit_load(cg,ptr,inner_ty);
        n->ty=TY_INT; return v;
    }
    case NODE_INTERP_STR: {
          
          for(AstList *part=n->interp_str.parts;part;part=part->next){
              AstNode *pn=part->node;
              char *pv=emit_expr(cg,pn);
              const char *pty=ast_type_str(cg,pn);
              const char *fmt;
              const char *arg_ty=pty;
              if(pn->kind==NODE_STRING_LIT){
                  fmt="%s";
              } else if(strcmp(pty,"i64")==0)   { fmt="%ld"; }
              else if(strcmp(pty,"float")==0)   { pv=coerce_to(cg,pv,"float","double"); arg_ty="double"; fmt="%g"; }
              else if(strcmp(pty,"double")==0)  { fmt="%g"; }
              else if(strcmp(pty,"ptr")==0)     { fmt="%s"; }
              else if(strcmp(pty,"i1")==0)      { pv=coerce_to(cg,pv,"i1","i32"); arg_ty="i32"; fmt="%d"; }
              else if(strcmp(pty,"i8")==0)      { fmt="%c"; }
              else                              { fmt="%d"; }
              char *fmtdst=newtmp(cg);
              FirInstr *fs=mk_instr(cg,FIR_CONST_STR);
              fs->dst=fmtdst; fs->src1=arena_strdup(cg->arena,fmt); emit_instr(cg,fs);
              char *pd=newtmp(cg);
              FirInstr *pc=mk_instr(cg,FIR_CALL);
              pc->type=arena_strdup(cg->arena,"i32");
              pc->src1=arena_strdup(cg->arena,"printf");
              pc->dst=pd; pc->nargs=2;
              pc->args=arena_alloc(cg->arena,2*sizeof(char*));
              pc->arg_types=arena_alloc(cg->arena,2*sizeof(char*));
              pc->args[0]=arena_strdup(cg->arena,fmtdst); pc->arg_types[0]=arena_strdup(cg->arena,"ptr");
              pc->args[1]=arena_strdup(cg->arena,pv);     pc->arg_types[1]=arena_strdup(cg->arena,arg_ty);
              emit_instr(cg,pc);
          }
          n->ty=TY_STR;
          return arena_strdup(cg->arena,"0");
      }
    case NODE_LAMBDA: {
        char fn_name[64]; snprintf(fn_name,sizeof(fn_name),"__lambda_%d",cg->tmp++);
        FirFn *saved_fn = cg->mod.cur_fn;
        char *saved_ret = cg->cur_ret_type;
        int saved_defer_base = cg->fn_defer_base;
        int saved_ndefers = cg->ndefers;
        cg->fn_defer_base = cg->ndefers;

        FirFn *lfn=arena_alloc(cg->arena,sizeof(FirFn));
        memset(lfn,0,sizeof(FirFn));
        lfn->name=arena_strdup(cg->arena,fn_name);
        lfn->ret_type=n->lambda.ret_type?(char*)typenode_to_llvm(cg,n->lambda.ret_type):"i64";
        cg->cur_ret_type=lfn->ret_type;

        FirFn **last=&cg->mod.fns;
        while(*last)last=&(*last)->next;
        *last=lfn; cg->mod.cur_fn=lfn;

        new_block(cg,"entry");
        scope_push(cg);

        FirParam **plast=&lfn->params;
        for(AstList *pl=n->lambda.params;pl;pl=pl->next){
            AstNode *pm=pl->node; if(!pm)continue;
            const char *pname=pm->var_decl.name;
            const char *pty=pm->var_decl.type?typenode_to_llvm(cg,pm->var_decl.type):"i64";
            FirParam *fp=arena_alloc(cg->arena,sizeof(FirParam));
            fp->name=arena_strdup(cg->arena,pname);
            fp->type=arena_strdup(cg->arena,pty); fp->next=NULL;
            *plast=fp; plast=&fp->next;
            char slot[128]; snprintf(slot,sizeof(slot),"var.%s",pname);
            emit_alloca(cg,slot,pty);
            char argref[128]; snprintf(argref,sizeof(argref),"%%arg.%s",pname);
            emit_store(cg,argref,slot,pty);
            var_def(cg,pname,pty,slot);
        }

        if(n->lambda.body&&n->lambda.body->kind==NODE_BLOCK) {
            for(AstList *it=n->lambda.body->block.stmts;it;it=it->next)
                if(it->node) emit_stmt(cg,it->node);
        } else if(n->lambda.body) {
            char *rv=emit_expr(cg,n->lambda.body);
            const char *rty=ast_type_str(cg,n->lambda.body);
            rv=coerce_to(cg,rv,rty,lfn->ret_type);
            if(!last_instr_is_term(cg)){
                FirInstr *ri=mk_instr(cg,FIR_RET);
                ri->src1=arena_strdup(cg->arena,rv);
                ri->type=arena_strdup(cg->arena,lfn->ret_type);
                emit_instr(cg,ri);
            }
        }
        if(!last_instr_is_term(cg)){
            if(strcmp(lfn->ret_type,"void")==0){
                FirInstr *ri=mk_instr(cg,FIR_RET_VOID); emit_instr(cg,ri);
            } else {
                FirInstr *ri=mk_instr(cg,FIR_RET);
                ri->src1=arena_strdup(cg->arena,"0");
                ri->type=arena_strdup(cg->arena,lfn->ret_type);
                emit_instr(cg,ri);
            }
        }
        scope_pop(cg);
        cg->ndefers=saved_ndefers;
        cg->fn_defer_base=saved_defer_base;
        cg->mod.cur_fn=saved_fn;
        cg->cur_ret_type=saved_ret;

        char fpname[128]; snprintf(fpname,sizeof(fpname),"@%s",fn_name);
        n->ty=TY_FN_PTR;
        return arena_strdup(cg->arena,fpname);
    }
    case NODE_IF: {
        char *cv=emit_expr(cg,n->if_stmt.cond);
        const char *cty=ast_type_str(cg,n->if_stmt.cond);
        if(strcmp(cty,"i1")!=0) cv=coerce_to(cg,cv,cty,"i1");
        char *icmp=newtmp(cg);
        FirInstr *ci=mk_instr(cg,FIR_ICMP);
        ci->dst=icmp; ci->type=arena_strdup(cg->arena,"i1");
        ci->src1=arena_strdup(cg->arena,cv);
        ci->src2=arena_strdup(cg->arena,"0");
        ci->src3=arena_strdup(cg->arena,"ne"); emit_instr(cg,ci);
        char *tl=newlbl(cg,"if_t"),*fl=newlbl(cg,"if_f"),*ml=newlbl(cg,"if_m");
        emit_br_cond(cg,icmp,tl,n->if_stmt.els?fl:ml);
        new_block(cg,tl);
        if(n->if_stmt.then&&n->if_stmt.then->kind==NODE_BLOCK)
            emit_block(cg,n->if_stmt.then);
        else if(n->if_stmt.then) emit_stmt(cg,n->if_stmt.then);
        if(!last_instr_is_term(cg)) emit_br(cg,ml);
        if(n->if_stmt.els){
            new_block(cg,fl);
            if(n->if_stmt.els->kind==NODE_BLOCK) emit_block(cg,n->if_stmt.els);
            else emit_stmt(cg,n->if_stmt.els);
            if(!last_instr_is_term(cg)) emit_br(cg,ml);
        }
        new_block(cg,ml);
        n->ty=TY_VOID; return arena_strdup(cg->arena,"0");
    }
    default:
        emit_stmt(cg,n);
        n->ty=TY_VOID;
        return arena_strdup(cg->arena,"0");
    }
}

static void emit_block(Codegen *cg, AstNode *n) {
    if (!n) return;
    if (n->kind == NODE_BLOCK) {
        scope_push(cg);
        for(AstList *it=n->block.stmts;it;it=it->next)
            if(it->node) emit_stmt(cg,it->node);
        scope_pop(cg);
    } else {
        emit_stmt(cg,n);
    }
}

static void emit_stmt(Codegen *cg, AstNode *n) {
    if (!n) return;
    switch (n->kind) {
    case NODE_VAR_DECL: {
          
          int is_mut = n->var_decl.is_mut;
          const char *ty;
          if (n->var_decl.type) {
              ty = typenode_to_llvm(cg, n->var_decl.type);
          } else if (n->var_decl.init) {
              
              char *v=emit_expr(cg,n->var_decl.init);
              const char *vty=ast_type_str(cg,n->var_decl.init);
              ty = vty ? vty : "i32";
              
              if(is_mut && strcmp(ty,"ptr")==0) v=emit_mut_str_copy(cg,v);
              const char *vn = n->var_decl.name ? n->var_decl.name : "_";
              char slot[128];
              if (strcmp(vn,"_")==0)
                  snprintf(slot,sizeof(slot),"var._.%d",cg->tmp++);
              else
                  snprintf(slot,sizeof(slot),"var.%s",vn);
              emit_alloca(cg,slot,ty);
              emit_store(cg,v,slot,ty);
              if(n->var_decl.name && strcmp(n->var_decl.name,"_")!=0){
                  if(is_mut) var_def_mut(cg,n->var_decl.name,ty,slot);
                  else       var_def(cg,n->var_decl.name,ty,slot);
              }
              break;
          } else {
              ty = "i32";
          }
          const char *vname = n->var_decl.name ? n->var_decl.name : "_";
          char slot[128];
          if (strcmp(vname,"_")==0)
              snprintf(slot,sizeof(slot),"var._.%d",cg->tmp++);
          else
              snprintf(slot,sizeof(slot),"var.%s",vname);
          emit_alloca(cg,slot,ty);
          if (n->var_decl.init) {
              char *v=emit_expr(cg,n->var_decl.init);
              const char *vty=ast_type_str(cg,n->var_decl.init);
              v=coerce_to(cg,v,vty,ty);
              
              if(is_mut && strcmp(ty,"ptr")==0) v=emit_mut_str_copy(cg,v);
              emit_store(cg,v,slot,ty);
          } else {
              if (strcmp(ty,"ptr")==0) emit_store(cg,"null",slot,"ptr");
              else if(is_float_type(ty)) emit_store(cg,"0.0",slot,ty);
              else emit_store(cg,"0",slot,ty);
          }
          if(n->var_decl.name){
              if(is_mut) var_def_mut(cg,n->var_decl.name,ty,slot);
              else       var_def(cg,n->var_decl.name,ty,slot);
          }
          break;
      }
    case NODE_ASSIGN:
    case NODE_COMPOUND_ASSIGN:
        emit_expr(cg,n); break;
    case NODE_EXPR_STMT:
        emit_expr(cg,n->expr_stmt.expr); break;
    case NODE_RETURN: {
        const char *rty=cg->cur_ret_type?cg->cur_ret_type:"i32";
        if(!n->ret.val||strcmp(rty,"void")==0){
            emit_ret_with_defers(cg,NULL,NULL);
        }else{
            char *rv=emit_expr(cg,n->ret.val);
            const char *vty=ast_type_str(cg,n->ret.val);
            rv=coerce_to(cg,rv,vty,rty);
            emit_ret_with_defers(cg,rv,rty);
        }
        break;
    }
    case NODE_DEFER: {
        if (cg->ndefers < MAX_DEFERS)
            cg->defer_stack[cg->ndefers++] = n->defer_stmt.expr;
        break;
    }
    case NODE_IF: {
        char *cv=emit_expr(cg,n->if_stmt.cond);
        const char *cty=ast_type_str(cg,n->if_stmt.cond);
        if(strcmp(cty,"i1")!=0) cv=coerce_to(cg,cv,cty,"i1");
        char *icmp=newtmp(cg);
        FirInstr *ci=mk_instr(cg,FIR_ICMP);
        ci->dst=icmp; ci->type=arena_strdup(cg->arena,"i1");
        ci->src1=arena_strdup(cg->arena,cv);
        ci->src2=arena_strdup(cg->arena,"0");
        ci->src3=arena_strdup(cg->arena,"ne"); emit_instr(cg,ci);
        char *tl=newlbl(cg,"if_t"),*fl=newlbl(cg,"if_f"),*ml=newlbl(cg,"if_m");
        emit_br_cond(cg,icmp,tl,n->if_stmt.els?fl:ml);
        new_block(cg,tl);
        emit_block(cg,n->if_stmt.then);
        if(!last_instr_is_term(cg)) emit_br(cg,ml);
        if(n->if_stmt.els){
            new_block(cg,fl);
            emit_block(cg,n->if_stmt.els);
            if(!last_instr_is_term(cg)) emit_br(cg,ml);
        }
        new_block(cg,ml);
        break;
    }
    case NODE_WHILE: {
        char *hl=newlbl(cg,"wh"),*bl=newlbl(cg,"wb"),*el=newlbl(cg,"we");
        char *prev_brk=cg->break_label,*prev_cnt=cg->cont_label;
        cg->break_label=el; cg->cont_label=hl;
        if(!last_instr_is_term(cg)) emit_br(cg,hl);
        new_block(cg,hl);
        char *cv=emit_expr(cg,n->while_stmt.cond);
        const char *cty=ast_type_str(cg,n->while_stmt.cond);
        if(strcmp(cty,"i1")!=0) cv=coerce_to(cg,cv,cty,"i1");
        char *icmp=newtmp(cg);
        FirInstr *ci=mk_instr(cg,FIR_ICMP);
        ci->dst=icmp; ci->type=arena_strdup(cg->arena,"i1");
        ci->src1=arena_strdup(cg->arena,cv);
        ci->src2=arena_strdup(cg->arena,"0");
        ci->src3=arena_strdup(cg->arena,"ne"); emit_instr(cg,ci);
        emit_br_cond(cg,icmp,bl,el);
        new_block(cg,bl);
        emit_block(cg,n->while_stmt.body);
        if(!last_instr_is_term(cg)) emit_br(cg,hl);
        new_block(cg,el);
        cg->break_label=prev_brk; cg->cont_label=prev_cnt;
        break;
    }
    case NODE_DO_WHILE: {
        char *bl=newlbl(cg,"dw"),*hl=newlbl(cg,"dwh"),*el=newlbl(cg,"dwe");
        char *prev_brk=cg->break_label,*prev_cnt=cg->cont_label;
        cg->break_label=el; cg->cont_label=hl;
        if(!last_instr_is_term(cg)) emit_br(cg,bl);
        new_block(cg,bl);
        emit_block(cg,n->while_stmt.body);
        if(!last_instr_is_term(cg)) emit_br(cg,hl);
        new_block(cg,hl);
        char *cv=emit_expr(cg,n->while_stmt.cond);
        const char *cty=ast_type_str(cg,n->while_stmt.cond);
        if(strcmp(cty,"i1")!=0) cv=coerce_to(cg,cv,cty,"i1");
        char *icmp=newtmp(cg);
        FirInstr *ci=mk_instr(cg,FIR_ICMP);
        ci->dst=icmp; ci->type=arena_strdup(cg->arena,"i1");
        ci->src1=arena_strdup(cg->arena,cv);
        ci->src2=arena_strdup(cg->arena,"0");
        ci->src3=arena_strdup(cg->arena,"ne"); emit_instr(cg,ci);
        emit_br_cond(cg,icmp,bl,el);
        new_block(cg,el);
        cg->break_label=prev_brk; cg->cont_label=prev_cnt;
        break;
    }
    case NODE_FOR_C: {
        char *hl=newlbl(cg,"fh"),*bl=newlbl(cg,"fb"),*sl=newlbl(cg,"fs"),*el=newlbl(cg,"fe");
        char *prev_brk=cg->break_label,*prev_cnt=cg->cont_label;
        cg->break_label=el; cg->cont_label=sl;
        scope_push(cg);
        if(n->for_c.init) emit_stmt(cg,n->for_c.init);
        if(!last_instr_is_term(cg)) emit_br(cg,hl);
        new_block(cg,hl);
        if(n->for_c.cond){
            char *cv=emit_expr(cg,n->for_c.cond);
            const char *cty=ast_type_str(cg,n->for_c.cond);
            if(strcmp(cty,"i1")!=0) cv=coerce_to(cg,cv,cty,"i1");
            char *icmp=newtmp(cg);
            FirInstr *ci=mk_instr(cg,FIR_ICMP);
            ci->dst=icmp; ci->type=arena_strdup(cg->arena,"i1");
            ci->src1=arena_strdup(cg->arena,cv);
            ci->src2=arena_strdup(cg->arena,"0");
            ci->src3=arena_strdup(cg->arena,"ne"); emit_instr(cg,ci);
            emit_br_cond(cg,icmp,bl,el);
        }else emit_br(cg,bl);
        new_block(cg,bl);
        emit_block(cg,n->for_c.body);
        if(!last_instr_is_term(cg)) emit_br(cg,sl);
        new_block(cg,sl);
        if(n->for_c.step) emit_expr(cg,n->for_c.step);
        if(!last_instr_is_term(cg)) emit_br(cg,hl);
        new_block(cg,el);
        scope_pop(cg);
        cg->break_label=prev_brk; cg->cont_label=prev_cnt;
        break;
    }
    case NODE_FOR_IN: {
        char *hl=newlbl(cg,"fih"),*bl=newlbl(cg,"fib"),*el=newlbl(cg,"fie");
        char *prev_brk=cg->break_label,*prev_cnt=cg->cont_label;
        cg->break_label=el; cg->cont_label=hl;
        scope_push(cg);
        AstNode *iter=n->for_in.iter;
        char *start=arena_strdup(cg->arena,"0"),*end_v=arena_strdup(cg->arena,"0");
        if(iter->kind==NODE_BINARY&&(strcmp(iter->binary.op,"..")==0||strcmp(iter->binary.op,"..=")==0)){
            start=emit_expr(cg,iter->binary.left);
            end_v=emit_expr(cg,iter->binary.right);
        }else{
            end_v=emit_expr(cg,iter);
        }
        char idx_slot[64]; snprintf(idx_slot,sizeof(idx_slot),"var.%s",n->for_in.var?n->for_in.var:"_i");
        emit_alloca(cg,idx_slot,"i32");
        emit_store(cg,start,idx_slot,"i32");
        if(n->for_in.var) var_def(cg,n->for_in.var,"i32",idx_slot);
        if(!last_instr_is_term(cg)) emit_br(cg,hl);
        new_block(cg,hl);
        char *iv=emit_load(cg,idx_slot,"i32");
        int incl=(iter->kind==NODE_BINARY&&strcmp(iter->binary.op,"..=")==0)?1:0;
        char *cmp=newtmp(cg);
        FirInstr *ci=mk_instr(cg,FIR_ICMP);
        ci->dst=cmp; ci->type=arena_strdup(cg->arena,"i1");
        ci->src1=arena_strdup(cg->arena,iv);
        ci->src2=arena_strdup(cg->arena,end_v);
        ci->src3=arena_strdup(cg->arena,incl?"sle":"slt"); emit_instr(cg,ci);
        emit_br_cond(cg,cmp,bl,el);
        new_block(cg,bl);
        emit_block(cg,n->for_in.body);
        if(!last_instr_is_term(cg)){
            char *iv2=emit_load(cg,idx_slot,"i32");
            char *inc=newtmp(cg);
            FirInstr *ai=mk_instr(cg,FIR_ADD);
            ai->dst=inc; ai->type=arena_strdup(cg->arena,"i32");
            ai->src1=arena_strdup(cg->arena,iv2);
            ai->src2=arena_strdup(cg->arena,"1"); emit_instr(cg,ai);
            emit_store(cg,inc,idx_slot,"i32");
            emit_br(cg,hl);
        }
        new_block(cg,el);
        scope_pop(cg);
        cg->break_label=prev_brk; cg->cont_label=prev_cnt;
        break;
    }
    case NODE_MATCH: {
        char *mv=emit_expr(cg,n->match_stmt.expr);
        const char *mty=ast_type_str(cg,n->match_stmt.expr);
        char *end_l=newlbl(cg,"mend");
        int narms=0;
        for(AstList *al=n->match_stmt.arms;al;al=al->next) narms++;
        char **arm_labels=arena_alloc(cg->arena,(size_t)narms*sizeof(char*));
        int ai=0;
        for(AstList *al=n->match_stmt.arms;al;al=al->next)
            arm_labels[ai++]=newlbl(cg,"marm");
        ai=0;
        for(AstList *al=n->match_stmt.arms;al;al=al->next,ai++){
            AstNode *arm=al->node;
            if(!arm){emit_br(cg,arm_labels[ai]);new_block(cg,arm_labels[ai]);continue;}
            if(arm->match_arm.is_wildcard){
                emit_br(cg,arm_labels[ai]);
                new_block(cg,arm_labels[ai]);
                scope_push(cg);
                emit_stmt(cg,arm->match_arm.body);
                scope_pop(cg);
                if(!last_instr_is_term(cg)) emit_br(cg,end_l);
                continue;
            }
            if(arm->match_arm.is_string) {
                char *slit=newtmp(cg);
                FirInstr *ssi=mk_instr(cg,FIR_CONST_STR);
                ssi->dst=slit; ssi->src1=arena_strdup(cg->arena,arm->match_arm.str_val?arm->match_arm.str_val:"");
                emit_instr(cg,ssi);
                FirInstr *cmpci=mk_instr(cg,FIR_CALL); cmpci->type=arena_strdup(cg->arena,"i32");
                cmpci->src1=arena_strdup(cg->arena,"strcmp"); cmpci->nargs=2;
                cmpci->args=arena_alloc(cg->arena,2*sizeof(char*));
                cmpci->arg_types=arena_alloc(cg->arena,2*sizeof(char*));
                cmpci->args[0]=arena_strdup(cg->arena,mv); cmpci->arg_types[0]=arena_strdup(cg->arena,"ptr");
                cmpci->args[1]=arena_strdup(cg->arena,slit); cmpci->arg_types[1]=arena_strdup(cg->arena,"ptr");
                cmpci->dst=newtmp(cg); emit_instr(cg,cmpci);
                char *scond=newtmp(cg);
                FirInstr *scmp=mk_instr(cg,FIR_ICMP);
                scmp->dst=scond; scmp->type=arena_strdup(cg->arena,"i1");
                scmp->src1=cmpci->dst; scmp->src2=arena_strdup(cg->arena,"0");
                scmp->src3=arena_strdup(cg->arena,"eq"); emit_instr(cg,scmp);
                char *next_l=newlbl(cg,"mcnext");
                emit_br_cond(cg,scond,arm_labels[ai],next_l);
                new_block(cg,arm_labels[ai]);
                scope_push(cg);
                emit_stmt(cg,arm->match_arm.body);
                scope_pop(cg);
                if(!last_instr_is_term(cg)) emit_br(cg,end_l);
                new_block(cg,next_l);
                continue;
            }
            char *next_l=newlbl(cg,"mcnext");
            char *cond=newtmp(cg);
            FirInstr *cii=mk_instr(cg,FIR_ICMP);
            cii->dst=cond; cii->type=arena_strdup(cg->arena,mty);
            cii->src1=arena_strdup(cg->arena,mv);
            if(arm->match_arm.is_variant){
                EnumInfo *ei=NULL;
                int vi=find_enum_variant(cg,arm->match_arm.variant_name,&ei);
                char tag_v_name[32]; snprintf(tag_v_name,sizeof(tag_v_name),"%d",vi>=0?vi*2:0);
                char *tag_v=newtmp(cg);
                FirInstr *land=mk_instr(cg,FIR_AND);
                land->dst=tag_v; land->type=arena_strdup(cg->arena,mty);
                land->src1=arena_strdup(cg->arena,mv);
                land->src2=arena_strdup(cg->arena,"4294967295"); emit_instr(cg,land);
                cii->src1=arena_strdup(cg->arena,tag_v);
                cii->src2=arena_strdup(cg->arena,tag_v_name);
            }else if(arm->match_arm.is_range){
                char lo_s[32],hi_s[32];
                snprintf(lo_s,sizeof(lo_s),"%lld",arm->match_arm.lit_lo);
                snprintf(hi_s,sizeof(hi_s),"%lld",arm->match_arm.lit_hi);
                char *clo=newtmp(cg),*chi=newtmp(cg);
                FirInstr *iclo=mk_instr(cg,FIR_ICMP);
                iclo->dst=clo; iclo->type=arena_strdup(cg->arena,mty);
                iclo->src1=arena_strdup(cg->arena,mv);
                iclo->src2=arena_strdup(cg->arena,lo_s);
                iclo->src3=arena_strdup(cg->arena,"sge"); emit_instr(cg,iclo);
                FirInstr *ichi=mk_instr(cg,FIR_ICMP);
                ichi->dst=chi; ichi->type=arena_strdup(cg->arena,mty);
                ichi->src1=arena_strdup(cg->arena,mv);
                ichi->src2=arena_strdup(cg->arena,hi_s);
                ichi->src3=arena_strdup(cg->arena,"sle"); emit_instr(cg,ichi);
                cond=newtmp(cg);
                FirInstr *land=mk_instr(cg,FIR_AND);
                land->dst=cond; land->type=arena_strdup(cg->arena,"i1");
                land->src1=arena_strdup(cg->arena,clo);
                land->src2=arena_strdup(cg->arena,chi); emit_instr(cg,land);
                emit_br_cond(cg,cond,arm_labels[ai],next_l);
                new_block(cg,arm_labels[ai]);
                scope_push(cg);
                emit_stmt(cg,arm->match_arm.body);
                scope_pop(cg);
                if(!last_instr_is_term(cg)) emit_br(cg,end_l);
                new_block(cg,next_l);
                continue;
            }else{
                char lit_s[64]; snprintf(lit_s,sizeof(lit_s),"%lld",arm->match_arm.lit_lo);
                cii->src2=arena_strdup(cg->arena,lit_s);
            }
            cii->src3=arena_strdup(cg->arena,"eq"); emit_instr(cg,cii);
            emit_br_cond(cg,cond,arm_labels[ai],next_l);
            new_block(cg,arm_labels[ai]);
            scope_push(cg);
            if(arm->match_arm.bind_name && arm->match_arm.is_variant){
                char *payload=newtmp(cg);
                FirInstr *sh=mk_instr(cg,FIR_SHR);
                sh->dst=payload; sh->type=arena_strdup(cg->arena,mty);
                sh->src1=arena_strdup(cg->arena,mv);
                sh->src2=arena_strdup(cg->arena,"32"); emit_instr(cg,sh);
                char bslot[128]; snprintf(bslot,sizeof(bslot),"var.%s",arm->match_arm.bind_name);
                emit_alloca(cg,bslot,"i32");
                char *trunc=coerce_to(cg,payload,mty,"i32");
                emit_store(cg,trunc,bslot,"i32");
                var_def(cg,arm->match_arm.bind_name,"i32",bslot);
            }
            emit_stmt(cg,arm->match_arm.body);
            scope_pop(cg);
            if(!last_instr_is_term(cg)) emit_br(cg,end_l);
            new_block(cg,next_l);
        }
        if(!last_instr_is_term(cg)) emit_br(cg,end_l);
        new_block(cg,end_l);
        break;
    }
    case NODE_BREAK:
        emit_defers(cg);
        if(cg->break_label) emit_br(cg,cg->break_label);
        else{ FirInstr *ur=mk_instr(cg,FIR_UNREACHABLE); emit_instr(cg,ur); }
        {char *dl=newlbl(cg,"brkd"); new_block(cg,dl);}
        break;
    case NODE_CONTINUE:
        if(cg->cont_label) emit_br(cg,cg->cont_label);
        else{ FirInstr *ur=mk_instr(cg,FIR_UNREACHABLE); emit_instr(cg,ur); }
        {char *dl=newlbl(cg,"cntd"); new_block(cg,dl);}
        break;
    case NODE_BLOCK:
        emit_block(cg,n);
        break;
    case NODE_REGION: {
        emit_rt_call(cg, "fe_region_push", "void", 0, NULL, NULL);
        int saved_in_region = cg->in_region;
        cg->in_region = 1;
        scope_push(cg);
        for(AstList *it=n->region_block.body;it;it=it->next)
            if(it->node) emit_stmt(cg,it->node);
        scope_pop(cg);
        cg->in_region = saved_in_region;
        emit_rt_call(cg, "fe_region_pop", "void", 0, NULL, NULL);
        break;
    }
    case NODE_UNSAFE_BLOCK: {
        int saved = cg->in_unsafe;
        cg->in_unsafe = 1;
        emit_block(cg, n->unsafe_block.body);
        cg->in_unsafe = saved;
        break;
    }
    case NODE_ENUM_DECL: case NODE_CLASS_DECL:
        break;
    case NODE_COMPTIME_LET:
        if(cg->n_comptime<64){
            long long _cv=eval_comptime(cg,n->comptime_let.init);
            cg->comptime_names[cg->n_comptime]=n->comptime_let.name;
            cg->comptime_values[cg->n_comptime]=_cv;
            cg->n_comptime++;
        }
        break;
    case NODE_FN_DECL: case NODE_UNIT_DECL:
        break;
    default:
        break;
    }
}

static void collect_top_decls(Codegen *cg, AstNode *prog) {
    for (AstList *it=prog->program.decls;it;it=it->next) {
        AstNode *n=it->node; if(!n)continue;
        if(n->kind==NODE_ENUM_DECL){
            if(cg->nenum<MAX_ENUMS){
                EnumInfo *ei=&cg->enums[cg->nenum++];
                ei->name=n->enum_decl.name;
                ei->nv=0;
                for(AstList *vl=n->enum_decl.variants;vl&&ei->nv<MAX_ENUM_VARS;vl=vl->next){
                    AstNode *v=vl->node; if(!v)continue;
                    ei->variants[ei->nv]=v->var_decl.name;
                    ei->payload_types[ei->nv]=v->var_decl.type?
                        (char*)typenode_to_llvm(cg,v->var_decl.type):"i32";
                    ei->nv++;
                }
            }
        }else if(n->kind==NODE_CLASS_DECL){
            if(cg->nstruct<MAX_STRUCTS){
                StructInfo *si=&cg->structs[cg->nstruct++];
                si->name=n->class_decl.name;
                si->nfields=0;
                si->is_class=1;
                for(AstList *ml=n->class_decl.private_members;ml&&si->nfields<64;ml=ml->next){
                    AstNode *m=ml->node; if(!m)continue;
                    if(m->kind==NODE_VAR_DECL){
                        si->fields[si->nfields]=m->var_decl.name;
                        si->field_types[si->nfields]=m->var_decl.type?
                            (char*)typenode_to_llvm(cg,m->var_decl.type):"i32";
                        si->nfields++;
                    }
                }
                for(AstList *ml=n->class_decl.public_members;ml&&si->nfields<64;ml=ml->next){
                    AstNode *m=ml->node; if(!m)continue;
                    if(m->kind==NODE_VAR_DECL){
                        si->fields[si->nfields]=m->var_decl.name;
                        si->field_types[si->nfields]=m->var_decl.type?
                            (char*)typenode_to_llvm(cg,m->var_decl.type):"i32";
                        si->nfields++;
                    }
                }
                if(si->nfields>0){
                    FirStructDecl *fsd=arena_alloc(cg->arena,sizeof(FirStructDecl));
                    memset(fsd,0,sizeof(FirStructDecl));
                    fsd->name=n->class_decl.name;
                    fsd->nfields=si->nfields;
                    for(int k=0;k<si->nfields;k++)
                        fsd->field_types[k]=si->field_types[k];
                    FirStructDecl **last=&cg->mod.struct_decls;
                    while(*last)last=&(*last)->next;
                    *last=fsd;
                }
            }
        }
    }
}

static void emit_fn_decl(Codegen *cg, AstNode *n, const char *struct_name) {
    if(!n||(n->kind!=NODE_FN_DECL&&n->kind!=NODE_UNIT_DECL))return;
    int is_unit=n->fn_decl.is_unit;
    char fn_name[256];
    if(struct_name)
        snprintf(fn_name,sizeof(fn_name),"%s__%s",struct_name,n->fn_decl.name);
    else
        snprintf(fn_name,sizeof(fn_name),"%s",n->fn_decl.name);

    if(n->fn_decl.is_extern && !n->fn_decl.body) {
        FirFn *fn=arena_alloc(cg->arena,sizeof(FirFn));
        memset(fn,0,sizeof(FirFn));
        fn->name=arena_strdup(cg->arena,fn_name);
        fn->ret_type=is_unit?"void":(n->fn_decl.ret_type?
            (char*)typenode_to_llvm(cg,n->fn_decl.ret_type):"i32");
        fn->is_extern=1;
        FirParam **plast=&fn->params;
        for(AstList *pl=n->fn_decl.params;pl;pl=pl->next){
            AstNode *pm=pl->node; if(!pm)continue;
            const char *pname=pm->var_decl.name;
            const char *pty=pm->var_decl.type?typenode_to_llvm(cg,pm->var_decl.type):"i32";
            FirParam *fp=arena_alloc(cg->arena,sizeof(FirParam));
            fp->name=arena_strdup(cg->arena,pname);
            fp->type=arena_strdup(cg->arena,pty); fp->next=NULL;
            *plast=fp; plast=&fp->next;
        }
        FirFn **last=&cg->mod.fns;
        while(*last)last=&(*last)->next;
        *last=fn;
        return;
    }

    FirFn *fn=arena_alloc(cg->arena,sizeof(FirFn));
    memset(fn,0,sizeof(FirFn));
    fn->name=arena_strdup(cg->arena,fn_name);
    fn->ret_type=is_unit?"void":(n->fn_decl.ret_type?
        (char*)typenode_to_llvm(cg,n->fn_decl.ret_type):"i32");
    cg->cur_ret_type=fn->ret_type;

    int saved_defer_base = cg->fn_defer_base;
    int saved_ndefers    = cg->ndefers;
    cg->fn_defer_base    = cg->ndefers;

    FirFn **last=&cg->mod.fns;
    while(*last)last=&(*last)->next;
    *last=fn; cg->mod.cur_fn=fn;

    new_block(cg,"entry");
    scope_push(cg);

    const char *prev_struct = cg->cur_struct;
    cg->cur_struct = struct_name;
    if(struct_name){
        FirParam *sp=arena_alloc(cg->arena,sizeof(FirParam));
        sp->name="self"; sp->type="ptr"; sp->next=NULL;
        fn->params=sp;
        char *slot=arena_strdup(cg->arena,"var.self");
        emit_alloca(cg,slot,"ptr");
        emit_store(cg,"%arg.self",slot,"ptr");
        var_def(cg,"self","ptr",slot);
    }

    FirParam **plast=struct_name?&fn->params->next:&fn->params;
    for(AstList *pl=n->fn_decl.params;pl;pl=pl->next){
        AstNode *pm=pl->node; if(!pm)continue;
        const char *pname=pm->var_decl.name;
        if(struct_name && strcmp(pname,"self")==0) continue;
        const char *pty=pm->var_decl.type?typenode_to_llvm(cg,pm->var_decl.type):"i32";
        FirParam *fp=arena_alloc(cg->arena,sizeof(FirParam));
        fp->name=arena_strdup(cg->arena,pname);
        fp->type=arena_strdup(cg->arena,pty); fp->next=NULL;
        *plast=fp; plast=&fp->next;
        char slot[128]; snprintf(slot,sizeof(slot),"var.%s",pname);
        emit_alloca(cg,slot,pty);
        char argref[128]; snprintf(argref,sizeof(argref),"%%arg.%s",pname);
        emit_store(cg,argref,slot,pty);
        var_def(cg,pname,pty,slot);
    }

    if(n->fn_decl.body) emit_block(cg,n->fn_decl.body);
    if(!last_instr_is_term(cg)){
        emit_defers(cg);
        if(strcmp(fn->ret_type,"void")==0) {
            FirInstr *ri=mk_instr(cg,FIR_RET_VOID); emit_instr(cg,ri);
        } else {
            FirInstr *ri=mk_instr(cg,FIR_RET);
            ri->src1=arena_strdup(cg->arena,"0");
            ri->type=arena_strdup(cg->arena,fn->ret_type);
            emit_instr(cg,ri);
        }
    }
    scope_pop(cg);

    cg->ndefers    = saved_ndefers;
    cg->fn_defer_base = saved_defer_base;
    cg->mod.cur_fn=NULL;
    cg->cur_struct = prev_struct;
}

void codegen_emit(Codegen *cg, AstNode *program) {
    collect_top_decls(cg, program);
    for(AstList *it=program->program.decls;it;it=it->next){
        AstNode *n=it->node; if(!n)continue;
        switch(n->kind){
        case NODE_COMPTIME_LET:
            if(cg->n_comptime<64){
                long long _cv=eval_comptime(cg,n->comptime_let.init);
                cg->comptime_names[cg->n_comptime]=n->comptime_let.name;
                cg->comptime_values[cg->n_comptime]=_cv;
                cg->n_comptime++;
            }
            break;
        case NODE_FN_DECL: case NODE_UNIT_DECL:
            emit_fn_decl(cg,n,NULL); break;
        case NODE_CLASS_DECL: {
            for(AstList *ml=n->class_decl.private_members;ml;ml=ml->next){
                AstNode *m=ml->node; if(!m)continue;
                if(m->kind==NODE_FN_DECL||m->kind==NODE_UNIT_DECL)
                    emit_fn_decl(cg,m,n->class_decl.name);
            }
            for(AstList *ml=n->class_decl.public_members;ml;ml=ml->next){
                AstNode *m=ml->node; if(!m)continue;
                if(m->kind==NODE_FN_DECL||m->kind==NODE_UNIT_DECL)
                    emit_fn_decl(cg,m,n->class_decl.name);
            }
            break;
        }
        default: break;
        }
    }
}
