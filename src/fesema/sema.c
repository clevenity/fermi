#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "sema.h"

static void serr(Sema *s, int line, int col, const char *code, const char *msg){
    fprintf(stderr,"%s:%d:%d: error[%s]: %s\n", s->src_path, line, col, code, msg);
    s->had_error=1;
}
static Scope *sc_push(Sema *s){
    Scope *sc=arena_alloc(s->arena,sizeof(Scope));
    sc->entries=NULL;sc->parent=s->current;s->current=sc;return sc;
}
static void sc_pop(Sema *s){ if(s->current)s->current=s->current->parent; }
static void sc_def(Sema *s, const char *name, int is_mut, int is_const){
    SymE *e=arena_alloc(s->arena,sizeof(SymE));
    e->name=arena_strdup(s->arena,name);
    e->is_mut=is_mut;e->is_const=is_const;
    e->next=s->current->entries;s->current->entries=e;
}
static SymE *sc_lookup(Sema *s, const char *name){
    for(Scope *sc=s->current;sc;sc=sc->parent)
        for(SymE *e=sc->entries;e;e=e->next)
            if(strcmp(e->name,name)==0) return e;
    return NULL;
}
static void chk(Sema *s, AstNode *n);
static void chk_list(Sema *s, AstList *list){
    for(AstList *it=list;it;it=it->next) if(it->node) chk(s,it->node);
}
static void chk(Sema *s, AstNode *n){
    if(!n)return;
    switch(n->kind){
    case NODE_PROGRAM:   chk_list(s,n->program.decls);break;
    case NODE_IMPORT:    break;
    case NODE_EXPORT_MODULE: break;
    case NODE_TYPE_ALIAS: break;
    case NODE_FN_DECL: case NODE_UNIT_DECL:
        sc_push(s);
        sc_def(s,"self",0,0);
        for(AstList *it=n->fn_decl.params;it;it=it->next){
            AstNode *p=it->node;
            if(p&&p->var_decl.name) sc_def(s,p->var_decl.name,p->var_decl.is_mut,0);
        }
        chk(s,n->fn_decl.body);sc_pop(s);break;
    case NODE_VAR_DECL:
        chk(s,n->var_decl.init);
        if(n->var_decl.name) sc_def(s,n->var_decl.name,n->var_decl.is_mut,n->var_decl.is_const);
        break;
    case NODE_BLOCK: sc_push(s);chk_list(s,n->block.stmts);sc_pop(s);break;
    case NODE_IF:
        chk(s,n->if_stmt.cond);chk(s,n->if_stmt.then);chk(s,n->if_stmt.els);break;
    case NODE_WHILE: case NODE_DO_WHILE:
        chk(s,n->while_stmt.cond);chk(s,n->while_stmt.body);break;
    case NODE_FOR_C:
        sc_push(s);chk(s,n->for_c.init);chk(s,n->for_c.cond);
        chk(s,n->for_c.step);chk(s,n->for_c.body);sc_pop(s);break;
    case NODE_FOR_IN:
        sc_push(s);chk(s,n->for_in.iter);
        if(n->for_in.var) sc_def(s,n->for_in.var,0,0);
        chk(s,n->for_in.body);sc_pop(s);break;
    case NODE_MATCH:
        chk(s,n->match_stmt.expr);chk_list(s,n->match_stmt.arms);break;
    case NODE_MATCH_ARM:
        sc_push(s);
        if(n->match_arm.bind_name) sc_def(s,n->match_arm.bind_name,0,0);
        chk(s,n->match_arm.body);sc_pop(s);break;
    case NODE_RETURN: chk(s,n->ret.val);break;
    case NODE_EXPR_STMT: chk(s,n->expr_stmt.expr);break;
    case NODE_ASSIGN:
        chk(s,n->assign.left);chk(s,n->assign.right);
        if(n->assign.left&&n->assign.left->kind==NODE_IDENT){
            SymE *e=sc_lookup(s,n->assign.left->ident.name);
            if(e&&!e->is_mut){
                char msg[256];
                snprintf(msg,sizeof(msg),"reassignment of read-only identifier '%s'",
                         n->assign.left->ident.name);
                serr(s,n->line,n->col,"E0003",msg);
            }
        }
        break;
    case NODE_COMPOUND_ASSIGN:
        chk(s,n->compound_assign.left);chk(s,n->compound_assign.right);
        if(n->compound_assign.left&&n->compound_assign.left->kind==NODE_IDENT){
            SymE *e=sc_lookup(s,n->compound_assign.left->ident.name);
            if(e&&!e->is_mut){
                char msg[256];
                snprintf(msg,sizeof(msg),"reassignment of read-only identifier '%s'",
                         n->compound_assign.left->ident.name);
                serr(s,n->line,n->col,"E0003",msg);
            }
        }
        break;
    case NODE_BINARY: chk(s,n->binary.left);chk(s,n->binary.right);break;
    case NODE_UNARY:  chk(s,n->unary.operand);break;
    case NODE_CALL:
        if(n->call.callee&&n->call.callee->kind==NODE_FIELD)
            chk(s,n->call.callee->field.obj);
        else chk(s,n->call.callee);
        chk_list(s,n->call.args);break;
    case NODE_INDEX:  chk(s,n->index.obj);chk(s,n->index.idx);break;
    case NODE_FIELD:  chk(s,n->field.obj);break;
    case NODE_REF:    chk(s,n->ref_expr.expr);break;
    case NODE_DEREF:  chk(s,n->deref_expr.expr);break;
    case NODE_CAST:   chk(s,n->cast.expr);break;
    case NODE_IDENT: {
        const char *nm=n->ident.name;
        if(strcmp(nm,"null")!=0&&strcmp(nm,"nullptr")!=0&&!sc_lookup(s,nm)){
            char msg[256];
            snprintf(msg,sizeof(msg),"use of undeclared identifier '%s'",nm);
            serr(s,n->line,n->col,"E0002",msg);
        }
        break;
    }
    case NODE_ENUM_DECL:
        sc_def(s,n->enum_decl.name,0,1);
        for(AstList *vl=n->enum_decl.variants;vl;vl=vl->next){
            AstNode *v=vl->node;
            if(v&&v->var_decl.name) sc_def(s,v->var_decl.name,0,1);
        }
        break;
    case NODE_CLASS_DECL:
        sc_def(s,n->class_decl.name,0,1);
        for(AstList *ml=n->class_decl.public_members;ml;ml=ml->next){
            AstNode *m=ml->node;
            if(m&&(m->kind==NODE_FN_DECL||m->kind==NODE_UNIT_DECL)&&m->fn_decl.name)
                sc_def(s,m->fn_decl.name,0,1);
        }
        break;
    case NODE_STRUCT_INIT:
        for(AstList *it=n->struct_init.fields;it;it=it->next){
            if(it->node&&it->node->kind==NODE_ASSIGN) chk(s,it->node->assign.right);
            else chk(s,it->node);
        }
        break;
    case NODE_ARRAY_LIT:  chk_list(s,n->array_lit.elems);break;
    case NODE_INTERP_STR: chk_list(s,n->interp_str.parts);break;
    case NODE_REGION:
        sc_push(s);chk_list(s,n->region_block.body);sc_pop(s);break;
    case NODE_BREAK: case NODE_CONTINUE: case NODE_THIS: break;
    case NODE_LAMBDA:
        sc_push(s);
        for(AstList *pl=n->lambda.params;pl;pl=pl->next){
            AstNode *pp=pl->node;if(pp&&pp->var_decl.name) sc_def(s,pp->var_decl.name,1,0);
        }
        chk(s,n->lambda.body);sc_pop(s);break;
    case NODE_DEFER: chk(s,n->defer_stmt.expr);break;
    case NODE_TRY:   chk(s,n->try_expr.expr);break;
    case NODE_UNSAFE_BLOCK: chk(s,n->unsafe_block.body);break;
    default: break;
    }
}

static const char *std_builtins[]={
    
    "Ok","Err","Some","None",
    NULL
};

Sema sema_new(Arena *arena, const char *src_path){
    Sema s; s.arena=arena; s.had_error=0; s.src_path=src_path?src_path:"<unknown>";
    Scope *root=arena_alloc(arena,sizeof(Scope));
    root->entries=NULL;root->parent=NULL;
    s.current=root;
    for(int i=0;std_builtins[i];i++){
        SymE *e=arena_alloc(arena,sizeof(SymE));
        e->name=arena_strdup(arena,std_builtins[i]);
        e->is_mut=0;e->is_const=1;
        e->next=root->entries;root->entries=e;
    }
    return s;
}

static void predef_fn(Sema *s, AstNode *prog){
    for(AstList *it=prog->program.decls;it;it=it->next){
        AstNode *n=it->node;if(!n)continue;
        switch(n->kind){
        case NODE_FN_DECL: case NODE_UNIT_DECL:
            if(n->fn_decl.name) sc_def(s,n->fn_decl.name,0,1);
            break;
        case NODE_ENUM_DECL:
            sc_def(s,n->enum_decl.name,0,1);
            for(AstList *vl=n->enum_decl.variants;vl;vl=vl->next){
                AstNode *v=vl->node;
                if(v&&v->var_decl.name) sc_def(s,v->var_decl.name,0,1);
            }
            break;
        case NODE_CLASS_DECL:
            sc_def(s,n->class_decl.name,0,1);
            break;
        case NODE_VAR_DECL:
            if(n->var_decl.name) sc_def(s,n->var_decl.name,n->var_decl.is_mut,n->var_decl.is_const);
            break;
        case NODE_COMPTIME_LET:
            if(n->comptime_let.name) sc_def(s,n->comptime_let.name,0,1);
            break;
        default:break;
        }
    }
}

void sema_check(Sema *s, AstNode *prog){
    predef_fn(s,prog);
    chk(s,prog);
}
