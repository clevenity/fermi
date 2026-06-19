#ifndef AST_H
#define AST_H

typedef enum {
    TY_VOID, TY_BOOL, TY_BYTE, TY_SHORT, TY_USHORT,
    TY_INT, TY_UINT, TY_LONG, TY_ULONG,
    TY_FLOAT, TY_DOUBLE, TY_CHAR, TY_STR,
    TY_REF, TY_RAW_PTR, TY_ARRAY_FIXED, TY_ARRAY_DYN, TY_NAMED, TY_RESULT, TY_UNKNOWN,
    TY_TUPLE, TY_FN_PTR,
} TypeKind;

typedef struct TypeNode TypeNode;
struct TypeNode {
    TypeKind kind;
    TypeNode *inner;
    TypeNode *inner2;
    int array_size;
    char *name;
    TypeNode **tuple_types;
    int tuple_count;
};

typedef enum {
    NODE_PROGRAM, NODE_IMPORT, NODE_EXPORT_MODULE, NODE_FN_DECL, NODE_UNIT_DECL,
    NODE_ENUM_DECL, NODE_CLASS_DECL, NODE_TYPE_ALIAS,
    NODE_VAR_DECL, NODE_CONST_DECL, NODE_BLOCK,
    NODE_IF, NODE_WHILE, NODE_DO_WHILE, NODE_FOR_C, NODE_FOR_IN,
    NODE_MATCH, NODE_MATCH_ARM, NODE_RETURN, NODE_BREAK, NODE_CONTINUE,
    NODE_DEFER,
    NODE_EXPR_STMT, NODE_ASSIGN, NODE_COMPOUND_ASSIGN,
    NODE_BINARY, NODE_UNARY, NODE_CALL, NODE_INDEX, NODE_FIELD,
    NODE_CAST, NODE_IDENT, NODE_INT_LIT, NODE_FLOAT_LIT, NODE_DOUBLE_LIT,
    NODE_LONG_LIT, NODE_BOOL_LIT, NODE_CHAR_LIT, NODE_STRING_LIT,
    NODE_STRUCT_INIT, NODE_ARRAY_LIT, NODE_REF, NODE_DEREF, NODE_THIS,
    NODE_INTERP_STR, NODE_REGION, NODE_TRY, NODE_LAMBDA,
    NODE_NS_CALL, NODE_UNSAFE_BLOCK, NODE_NULL,
    NODE_AT_CALL, NODE_COMPTIME_LET,
    NODE_PTR_DEREF, NODE_PTR_ADDR,
} NodeKind;

typedef struct AstNode AstNode;
typedef struct AstList AstList;

struct AstList { AstNode *node; AstList *next; };

struct AstNode {
    NodeKind kind;
    int line, col;
    TypeKind ty;
    char *ty_name;
    union {
        struct { AstList *decls; int is_module; char *module_name; } program;
        struct { char *path; int is_std; } import;
        struct {
            char *name; AstList *params; TypeNode *ret_type;
            AstNode *body; int is_unit; int is_extern; int is_exported;
        } fn_decl;
        struct { char *name; AstList *variants; } enum_decl;
        struct { char *name; AstList *private_members; AstList *public_members; } class_decl;
        struct { char *name; TypeNode *type; } type_alias;
        struct { char *name; TypeNode *type; AstNode *init; int is_mut, is_const, is_exported; } var_decl;
        struct { AstList *stmts; } block;
        struct { AstNode *cond, *then, *els; } if_stmt;
        struct { AstNode *cond, *body; } while_stmt;
        struct { AstNode *init, *cond, *step, *body; } for_c;
        struct { char *var; AstNode *iter, *body; } for_in;
        struct { AstNode *expr; AstList *arms; } match_stmt;
        struct {
            int is_wildcard, is_range, is_variant, is_string;
            char *variant_name, *bind_name, *str_val;
            long long lit_lo, lit_hi;
            AstNode *body;
        } match_arm;
        struct { AstNode *val; } ret;
        struct { AstNode *expr; } expr_stmt;
        struct { AstNode *left, *right; } assign;
        struct { char *op; AstNode *left, *right; } compound_assign;
        struct { char *op; AstNode *left, *right; } binary;
        struct { char *op; AstNode *operand; int prefix; } unary;
        struct { AstNode *callee; AstList *args; } call;
        struct { AstNode *obj, *idx; } index;
        struct { AstNode *obj; char *field; } field;
        struct { AstNode *expr; TypeNode *type; } cast;
        struct { char *name; } ident;
        struct { long long val; } int_lit;
        struct { double val; } float_lit;
        struct { double val; } double_lit;
        struct { long long val; } long_lit;
        struct { int val; } bool_lit;
        struct { char val; } char_lit;
        struct { char *val; } string_lit;
        struct { char *type_name; AstList *fields; } struct_init;
        struct { AstList *elems; } array_lit;
        struct { AstList *parts; } interp_str;
        struct { AstNode *expr; } ref_expr;
        struct { AstNode *expr; } deref_expr;
        struct { AstNode *expr; } ptr_deref;
        struct { AstNode *expr; } ptr_addr;
        struct { AstNode *expr; } region;
        struct { AstNode *expr; } try_expr;
        struct { AstNode *expr; } defer_stmt;
        struct { AstList *params; TypeNode *ret_type; AstNode *body; } lambda;
        struct { char *ns; char *func; AstList *args; } ns_call;
        struct { AstNode *body; } unsafe_block;
        struct { char *name; AstList *args; TypeNode *type_arg; } at_call;
        struct { char *name; AstNode *init; } comptime_let;
        struct { AstList *body; char *name; } region_block;
    };
};

typedef struct { char *name; TypeNode *type; } Param;
typedef struct { char *name; TypeNode *type; AstNode *init; } StructField;

#endif
