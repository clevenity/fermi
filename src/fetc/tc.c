#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "tc.h"

static TypeKind infer(Tc *t, AstNode *n);
static TcScope *sc_push(Tc *t){
    TcScope *s=arena_alloc(t->arena,sizeof(TcScope));
    s->entries=NULL;s->parent=t->scope;t->scope=s;return s;
}
static void sc_pop(Tc *t){if(t->scope)t->scope=t->scope->parent;}
static void sc_def(Tc *t,const char *name,TypeKind ty,const char *ty_name){
    TcEntry *e=arena_alloc(t->arena,sizeof(TcEntry));
    e->name=arena_strdup(t->arena,name);
    e->ty=ty;e->ty_name=ty_name?arena_strdup(t->arena,ty_name):NULL;
    e->next=t->scope->entries;t->scope->entries=e;
}
static TcEntry *sc_lookup(Tc *t,const char *name){
    for(TcScope *s=t->scope;s;s=s->parent)
        for(TcEntry *e=s->entries;e;e=e->next)
            if(strcmp(e->name,name)==0) return e;
    return NULL;
}
static TypeKind typenode_kind(TypeNode *tn){if(!tn)return TY_INT;return tn->kind;}
static TypeKind promote(TypeKind a,TypeKind b){
    if(a==TY_DOUBLE||b==TY_DOUBLE)return TY_DOUBLE;
    if(a==TY_FLOAT||b==TY_FLOAT)return TY_FLOAT;
    if(a==TY_LONG||b==TY_LONG)return TY_LONG;
    if(a==TY_ULONG||b==TY_ULONG)return TY_ULONG;
    if(a==TY_UINT||b==TY_UINT)return TY_UINT;
    return TY_INT;
}
static int is_numeric(TypeKind k){
    return k==TY_BYTE||k==TY_SHORT||k==TY_USHORT||k==TY_INT||k==TY_UINT||
           k==TY_LONG||k==TY_ULONG||k==TY_FLOAT||k==TY_DOUBLE;
}
static int is_integral(TypeKind k){
    
    return k==TY_BOOL||k==TY_BYTE||k==TY_SHORT||k==TY_USHORT||k==TY_INT||k==TY_UINT||
           k==TY_LONG||k==TY_ULONG||k==TY_UNKNOWN;
}
static void terr(Tc *t,int line,int col,const char *msg){
    fprintf(stderr,"%s:%d:%d: error[E0004]: %s\n",t->src_path,line,col,msg);
    t->had_error=1;
}

static void def_builtins(Tc *t){
    static const char *names[]={
        
        "Ok","Err","Some","None",
        NULL
    };
    for(int i=0;names[i];i++) sc_def(t,names[i],TY_VOID,NULL);
}
static void predef_top(Tc *t,AstNode *prog){
    for(AstList *it=prog->program.decls;it;it=it->next){
        AstNode *n=it->node;if(!n)continue;
        switch(n->kind){
        case NODE_FN_DECL:case NODE_UNIT_DECL:{
            TypeKind ret=n->fn_decl.is_unit?TY_VOID:
                (n->fn_decl.ret_type?typenode_kind(n->fn_decl.ret_type):TY_INT);
            const char *ty_name=(n->fn_decl.ret_type&&n->fn_decl.ret_type->name)?
                n->fn_decl.ret_type->name:NULL;
            sc_def(t,n->fn_decl.name,ret,ty_name);break;
        }
        case NODE_STRUCT_DECL:
            sc_def(t,n->struct_decl.name,TY_NAMED,n->struct_decl.name);
            for(AstList *ml=n->struct_decl.methods;ml;ml=ml->next){
                AstNode *m=ml->node;if(!m||m->kind!=NODE_FN_DECL)continue;
                char mname[256];
                snprintf(mname,sizeof(mname),"%s__%s",n->struct_decl.name,m->fn_decl.name);
                TypeKind ret=m->fn_decl.is_unit?TY_VOID:
                    (m->fn_decl.ret_type?typenode_kind(m->fn_decl.ret_type):TY_VOID);
                sc_def(t,mname,ret,NULL);
            }
            break;
        case NODE_ENUM_DECL:
            sc_def(t,n->enum_decl.name,TY_NAMED,n->enum_decl.name);
            for(AstList *vl=n->enum_decl.variants;vl;vl=vl->next){
                AstNode *v=vl->node;
                if(v&&v->kind==NODE_VAR_DECL)
                    sc_def(t,v->var_decl.name,TY_NAMED,n->enum_decl.name);
            }
            break;
        case NODE_CLASS_DECL: sc_def(t,n->class_decl.name,TY_NAMED,n->class_decl.name);break;
        case NODE_VAR_DECL:
            if(n->var_decl.is_const) sc_def(t,n->var_decl.name,
                n->var_decl.type?typenode_kind(n->var_decl.type):TY_INT,NULL);
            break;
        case NODE_COMPTIME_LET:
            if(n->comptime_let.name) sc_def(t,n->comptime_let.name,TY_LONG,NULL);
            break;
        default:break;
        }
    }
}
static TypeKind infer(Tc *t,AstNode *n){
    if(!n)return TY_VOID;
    switch(n->kind){
    case NODE_INT_LIT: n->ty=TY_INT;return TY_INT;
    case NODE_LONG_LIT: n->ty=TY_LONG;return TY_LONG;
    case NODE_FLOAT_LIT: n->ty=TY_FLOAT;return TY_FLOAT;
    case NODE_DOUBLE_LIT: n->ty=TY_DOUBLE;return TY_DOUBLE;
    case NODE_BOOL_LIT: n->ty=TY_BOOL;return TY_BOOL;
    case NODE_CHAR_LIT: n->ty=TY_CHAR;return TY_CHAR;
    case NODE_STRING_LIT: n->ty=TY_STR;return TY_STR;
    case NODE_INTERP_STR: n->ty=TY_STR;return TY_STR;
    case NODE_THIS: n->ty=TY_NAMED;return TY_NAMED;
    case NODE_IDENT:{
        TcEntry *e=sc_lookup(t,n->ident.name);
        if(e){n->ty=e->ty;n->ty_name=e->ty_name?e->ty_name:NULL;return e->ty;}
        n->ty=TY_UNKNOWN;return TY_UNKNOWN;
    }
    case NODE_UNARY:{
        TypeKind ot=infer(t,n->unary.operand);
        char *op=n->unary.op;
        if(!strcmp(op,"!")){ n->ty=TY_BOOL;return TY_BOOL; }
        if(!strcmp(op,"~")){
            if(!is_integral(ot)){terr(t,n->line,n->col,"'~' requires integral type");}
            n->ty=ot;return ot;
        }
        n->ty=ot;return ot;
    }
    case NODE_BINARY:{
        TypeKind lt=infer(t,n->binary.left),rt=infer(t,n->binary.right);
        char *op=n->binary.op;
        if(!strcmp(op,"&")||!strcmp(op,"|")||!strcmp(op,"^")||
           !strcmp(op,"<<")||!strcmp(op,">>")){
            if(!is_integral(lt)||!is_integral(rt)){
                terr(t,n->line,n->col,"bitwise op requires integral types");
            }
            TypeKind r=promote(lt,rt);n->ty=r;return r;
        }
        if(!strcmp(op,"==")||!strcmp(op,"!=")||
           !strcmp(op,"<")||!strcmp(op,">")||
           !strcmp(op,"<=")||!strcmp(op,">=")||
           !strcmp(op,"&&")||!strcmp(op,"||")){
            n->ty=TY_BOOL;return TY_BOOL;
        }
        TypeKind r=promote(lt,rt);n->ty=r;return r;
    }
    case NODE_CALL:{
        if(n->call.callee->kind==NODE_IDENT){
            TcEntry *e=sc_lookup(t,n->call.callee->ident.name);
            for(AstList *al=n->call.args;al;al=al->next) infer(t,al->node);
            if(e){n->ty=e->ty;return e->ty;}
        }
        for(AstList *al=n->call.args;al;al=al->next) infer(t,al->node);
        n->ty=TY_UNKNOWN;return TY_UNKNOWN;
    }
    case NODE_CAST:{
        infer(t,n->cast.expr);
        TypeKind tk=n->cast.type?typenode_kind(n->cast.type):TY_INT;
        n->ty=tk;return tk;
    }
    case NODE_FIELD:{
        infer(t,n->field.obj);n->ty=TY_UNKNOWN;return TY_UNKNOWN;
    }
    case NODE_INDEX:{
        infer(t,n->index.obj);infer(t,n->index.idx);n->ty=TY_CHAR;return TY_CHAR;
    }
    case NODE_REF: infer(t,n->ref_expr.expr);n->ty=TY_RAW_PTR;return TY_RAW_PTR;
    case NODE_DEREF: infer(t,n->deref_expr.expr);n->ty=TY_UNKNOWN;return TY_UNKNOWN;
    case NODE_ASSIGN:{
        TypeKind rt=infer(t,n->assign.right);
        infer(t,n->assign.left);
        n->ty=rt;return rt;
    }
    case NODE_COMPOUND_ASSIGN:{
        infer(t,n->compound_assign.left);infer(t,n->compound_assign.right);
        n->ty=TY_VOID;return TY_VOID;
    }
    case NODE_STRUCT_INIT: n->ty=TY_NAMED;return TY_NAMED;
    case NODE_ARRAY_LIT: n->ty=TY_ARRAY_DYN;return TY_ARRAY_DYN;
    case NODE_TRY: infer(t,n->try_expr.expr);n->ty=TY_UNKNOWN;return TY_UNKNOWN;
    default: return TY_VOID;
    }
}
static void check_fn(Tc *t,AstNode *n);
static void check_stmt(Tc *t,AstNode *n){
    if(!n)return;
    switch(n->kind){
    case NODE_VAR_DECL:
        if(n->var_decl.init){
            TypeKind it=infer(t,n->var_decl.init);
            TypeKind dt=n->var_decl.type?typenode_kind(n->var_decl.type):it;
            sc_def(t,n->var_decl.name,dt,n->var_decl.type&&n->var_decl.type->name?n->var_decl.type->name:NULL);
        }else{
            TypeKind dt=n->var_decl.type?typenode_kind(n->var_decl.type):TY_INT;
            sc_def(t,n->var_decl.name,dt,n->var_decl.type&&n->var_decl.type->name?n->var_decl.type->name:NULL);
        }
        break;
    case NODE_RETURN: infer(t,n->ret.val);break;
    case NODE_EXPR_STMT: infer(t,n->expr_stmt.expr);break;
    case NODE_ASSIGN: case NODE_COMPOUND_ASSIGN: infer(t,n);break;
    case NODE_IF:
        infer(t,n->if_stmt.cond);
        check_stmt(t,n->if_stmt.then);check_stmt(t,n->if_stmt.els);break;
    case NODE_WHILE:case NODE_DO_WHILE:
        infer(t,n->while_stmt.cond);check_stmt(t,n->while_stmt.body);break;
    case NODE_FOR_C:
        sc_push(t);
        check_stmt(t,n->for_c.init);infer(t,n->for_c.cond);
        infer(t,n->for_c.step);check_stmt(t,n->for_c.body);
        sc_pop(t);break;
    case NODE_FOR_IN:
        sc_push(t);
        infer(t,n->for_in.iter);
        if(n->for_in.var) sc_def(t,n->for_in.var,TY_INT,NULL);
        check_stmt(t,n->for_in.body);sc_pop(t);break;
    case NODE_MATCH:
        infer(t,n->match_stmt.expr);
        for(AstList *al=n->match_stmt.arms;al;al=al->next){
            if(!al->node)continue;
            sc_push(t);
            if(al->node->match_arm.bind_name)
                sc_def(t,al->node->match_arm.bind_name,TY_INT,NULL);
            check_stmt(t,al->node->match_arm.body);sc_pop(t);
        }
        break;
    case NODE_BLOCK:
        sc_push(t);
        for(AstList *it=n->block.stmts;it;it=it->next) check_stmt(t,it->node);
        sc_pop(t);break;
    case NODE_FN_DECL:case NODE_UNIT_DECL: check_fn(t,n);break;
    case NODE_REGION:
        sc_push(t);
        for(AstList *it=n->region_block.body;it;it=it->next) check_stmt(t,it->node);
        sc_pop(t);break;
    case NODE_DEFER: check_stmt(t,n->defer_stmt.expr);break;
    default:break;
    }
}
static void check_fn(Tc *t,AstNode *n){
    if(!n)return;
    sc_push(t);
    for(AstList *pl=n->fn_decl.params;pl;pl=pl->next){
        AstNode *p=pl->node;if(!p)continue;
        TypeKind pty=p->var_decl.type?typenode_kind(p->var_decl.type):TY_INT;
        sc_def(t,p->var_decl.name,pty,p->var_decl.type&&p->var_decl.type->name?p->var_decl.type->name:NULL);
    }
    if(n->fn_decl.body) check_stmt(t,n->fn_decl.body);
    sc_pop(t);
}
Tc tc_new(Arena *arena, const char *src_path){
    Tc t;t.arena=arena;t.had_error=0;t.scope=NULL;
    t.src_path=src_path?src_path:"<unknown>";
    sc_push(&t);def_builtins(&t);return t;
}
void tc_check(Tc *t,AstNode *program){
    predef_top(t,program);
    for(AstList *it=program->program.decls;it;it=it->next){
        AstNode *n=it->node;if(!n)continue;
        switch(n->kind){
        case NODE_FN_DECL:case NODE_UNIT_DECL: check_fn(t,n);break;
        case NODE_STRUCT_DECL:
            sc_def(t,n->struct_decl.name,TY_NAMED,n->struct_decl.name);
            for(AstList *ml=n->struct_decl.methods;ml;ml=ml->next) check_fn(t,ml->node);
            break;
        case NODE_VAR_DECL:
            if(n->var_decl.init) infer(t,n->var_decl.init);
            break;
        default:break;
        }
    }
}

