#ifndef FIR_H
#define FIR_H
typedef enum {
    FIR_ALLOCA, FIR_STORE, FIR_LOAD,
    FIR_ADD, FIR_SUB, FIR_MUL, FIR_DIV, FIR_MOD, FIR_UDIV, FIR_UREM,
    FIR_FADD, FIR_FSUB, FIR_FMUL, FIR_FDIV,
    FIR_ICMP, FIR_FCMP,
    FIR_AND, FIR_OR, FIR_XOR, FIR_NOT, FIR_BITNOT,
    FIR_SHL, FIR_SHR, FIR_LSHR,
    FIR_BR, FIR_BR_COND,
    FIR_CALL, FIR_CALL_INDIRECT, FIR_RET, FIR_RET_VOID,
    FIR_LABEL, FIR_ZEXT, FIR_SEXT, FIR_TRUNC,
    FIR_FPEXT, FIR_FPTRUNC, FIR_SITOFP, FIR_UITOFP, FIR_FPTOSI, FIR_FPTOUI,
    FIR_GEP, FIR_GEP_STRUCT, FIR_PTR_ADD,
    FIR_PTRTOINT, FIR_INTTOPTR, FIR_BITCAST,
    FIR_NEG, FIR_FNEG,
    FIR_CONST_STR, FIR_PHI, FIR_SELECT,
    FIR_NOP, FIR_UNREACHABLE,
    FIR_MEMCPY, FIR_MEMSET_I,
} FirOp;

typedef struct FirInstr FirInstr;
struct FirInstr {
    FirOp   op;
    char   *dst;
    char   *type;
    char   *src1, *src2, *src3;
    char  **args;
    char  **arg_types;
    int     nargs;
    int     is_tail;
    FirInstr *next;
};

typedef struct FirBlock FirBlock;
struct FirBlock {
    char     *label;
    FirInstr *head, *tail;
    FirBlock *next;
    int       dead;
};

typedef struct FirParam FirParam;
struct FirParam { char *name; char *type; FirParam *next; };

typedef struct FirFn FirFn;
struct FirFn {
    char     *name;
    char     *ret_type;
    FirParam *params;
    FirBlock *blocks;
    FirBlock *cur_block;
    FirFn    *next;
    int       is_extern;
    int       is_vararg;
};

typedef struct FirStructDecl FirStructDecl;
struct FirStructDecl {
    char *name;
    char *field_types[64];
    int   nfields;
    FirStructDecl *next;
};

typedef struct FirGlobal FirGlobal;
struct FirGlobal {
    char *name;
    char *type;
    char *init;
    int   is_const;
    FirGlobal *next;
};

typedef struct {
    FirFn         *fns;
    FirFn         *cur_fn;
    FirStructDecl *struct_decls;
    FirGlobal     *globals;
} FirModule;

void fir_print_module(FirModule *m);
#endif
