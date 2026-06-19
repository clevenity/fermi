#include <string.h>
#include "hir.h"

Hir hir_new(Arena *arena) {
    Hir h; h.arena=arena; h.had_error=0;
    h.std_io=h.std_math=h.std_string=h.std_mem=h.std_os=h.std_collections=0;
    return h;
}
static void lower_node(Hir *h, AstNode *n);
static void lower_list(Hir *h, AstList *list) {
    for(AstList *it=list;it;it=it->next) if(it->node) lower_node(h,it->node);
}
static void lower_node(Hir *h, AstNode *n) {
    if(!n) return;
    switch(n->kind) {
    case NODE_PROGRAM: lower_list(h,n->program.decls); break;
    case NODE_IMPORT:
        if(n->import.is_std){
            if(!strcmp(n->import.path,"std.io")||!strcmp(n->import.path,"io")) h->std_io=1;
            else if(!strcmp(n->import.path,"std.math")||!strcmp(n->import.path,"math")) h->std_math=1;
            else if(!strcmp(n->import.path,"std.string")||!strcmp(n->import.path,"str")) h->std_string=1;
            else if(!strcmp(n->import.path,"std.mem")||!strcmp(n->import.path,"mem")) h->std_mem=1;
            else if(!strcmp(n->import.path,"std.os")||!strcmp(n->import.path,"os")) h->std_os=1;
            else if(!strcmp(n->import.path,"std.collections")) h->std_collections=1;
        }
        break;
    case NODE_FN_DECL: case NODE_UNIT_DECL:
        lower_list(h,n->fn_decl.params); lower_node(h,n->fn_decl.body); break;
    case NODE_VAR_DECL: lower_node(h,n->var_decl.init); break;
    case NODE_BLOCK: lower_list(h,n->block.stmts); break;
    case NODE_IF: lower_node(h,n->if_stmt.cond); lower_node(h,n->if_stmt.then); lower_node(h,n->if_stmt.els); break;
    case NODE_WHILE: case NODE_DO_WHILE: lower_node(h,n->while_stmt.cond); lower_node(h,n->while_stmt.body); break;
    case NODE_FOR_C: lower_node(h,n->for_c.init); lower_node(h,n->for_c.cond); lower_node(h,n->for_c.step); lower_node(h,n->for_c.body); break;
    case NODE_FOR_IN: lower_node(h,n->for_in.iter); lower_node(h,n->for_in.body); break;
    case NODE_MATCH: lower_node(h,n->match_stmt.expr); lower_list(h,n->match_stmt.arms); break;
    case NODE_MATCH_ARM: lower_node(h,n->match_arm.body); break;
    case NODE_RETURN: lower_node(h,n->ret.val); break;
    case NODE_EXPR_STMT: lower_node(h,n->expr_stmt.expr); break;
    case NODE_ASSIGN: lower_node(h,n->assign.left); lower_node(h,n->assign.right); break;
    case NODE_COMPOUND_ASSIGN: lower_node(h,n->compound_assign.left); lower_node(h,n->compound_assign.right); break;
    case NODE_BINARY: lower_node(h,n->binary.left); lower_node(h,n->binary.right); break;
    case NODE_UNARY: lower_node(h,n->unary.operand); break;
    case NODE_CALL: lower_node(h,n->call.callee); lower_list(h,n->call.args); break;
    case NODE_FIELD: lower_node(h,n->field.obj); break;
    case NODE_INDEX: lower_node(h,n->index.obj); lower_node(h,n->index.idx); break;
    case NODE_REF: lower_node(h,n->ref_expr.expr); break;
    case NODE_DEREF: lower_node(h,n->deref_expr.expr); break;
    case NODE_STRUCT_INIT: lower_list(h,n->struct_init.fields); break;
    case NODE_REGION: lower_list(h,n->region_block.body); break;
    case NODE_CLASS_DECL: lower_list(h,n->class_decl.private_members); lower_list(h,n->class_decl.public_members); break;
    case NODE_DEFER: lower_node(h,n->defer_stmt.expr); break;
    default: break;
    }
}
void hir_lower(Hir *h, AstNode *program) { lower_node(h,program); }
