#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"
#include "ast.h"
#include "../felexer/lexer.h"
#include "../fearena/arena.h"

static AstNode *parse_expr(Parser *p);
static AstNode *parse_stmt(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_fn_decl(Parser *p, int is_unit, int is_extern, int is_exported);

static AstNode *nd(Parser *p, NodeKind k){
    AstNode *n=arena_alloc(p->arena,sizeof(AstNode));
    memset(n,0,sizeof(AstNode));
    n->kind=k; n->line=p->cur.line; n->col=p->cur.col;
    n->ty=TY_UNKNOWN; return n;
}
static int chk(Parser *p, TokenType t){ return p->cur.type==t; }
static int chkp(Parser *p, TokenType t){ return p->peek.type==t; }
static Token adv(Parser *p){
    Token t=p->cur; p->cur=p->peek; p->peek=lexer_next(&p->lexer); return t;
}
static int mat(Parser *p, TokenType t){ if(p->cur.type==t){adv(p);return 1;}return 0; }
static void err(Parser *p, const char *msg){
    fprintf(stderr,"%s:%d:%d: error[E0001]: %s, found '%s'\n",
        p->src_path, p->cur.line, p->cur.col, msg, p->cur.value);
    p->had_error=1;
}
static void expect(Parser *p, TokenType t, const char *msg){
    if(!mat(p,t)) err(p,msg);
}
static AstList *lapp(Parser *p, AstList *head, AstList **tail, AstNode *node){
    AstList *item=arena_alloc(p->arena,sizeof(AstList));
    item->node=node; item->next=NULL;
    if(!head){ *tail=item; return item; }
    (*tail)->next=item; *tail=item; return head;
}

static TypeNode *parse_type(Parser *p){
    TypeNode *t=arena_alloc(p->arena,sizeof(TypeNode));
    memset(t,0,sizeof(TypeNode));
    if(chk(p,TOK_AMP)){adv(p);t->kind=TY_REF;t->inner=parse_type(p);return t;}
    if(chk(p,TOK_STAR)){adv(p);t->kind=TY_RAW_PTR;t->inner=parse_type(p);return t;}
    if(chk(p,TOK_LBRACKET)){
        adv(p);t->kind=TY_ARRAY_DYN;
        if(!chk(p,TOK_RBRACKET)){
            t->inner=parse_type(p);
            if(chk(p,TOK_SEMICOLON)){
                adv(p);
                t->array_size=(int)strtol(p->cur.value,NULL,10);
                if(chk(p,TOK_INT_LIT))adv(p);
                t->kind=TY_ARRAY_FIXED;
            }
        }
        expect(p,TOK_RBRACKET,"expected ']'");return t;
    }
    if(chk(p,TOK_FN)){
        adv(p);t->kind=TY_FN_PTR;
        if(chk(p,TOK_LPAREN)){
            adv(p);
            while(!chk(p,TOK_RPAREN)&&!chk(p,TOK_EOF)){
                parse_type(p);
                if(!mat(p,TOK_COMMA))break;
            }
            expect(p,TOK_RPAREN,"expected ')'");
        }
        if(chk(p,TOK_COLON)||chk(p,TOK_ARROW)){adv(p);t->inner=parse_type(p);}
        return t;
    }
    switch(p->cur.type){
    case TOK_BOOL:   t->kind=TY_BOOL;   adv(p);break;
    case TOK_BYTE:   t->kind=TY_BYTE;   adv(p);break;
    case TOK_SHORT:  t->kind=TY_SHORT;  adv(p);break;
    case TOK_USHORT: t->kind=TY_USHORT; adv(p);break;
    case TOK_INT:    t->kind=TY_INT;    adv(p);break;
    case TOK_UINT:   t->kind=TY_UINT;   adv(p);break;
    case TOK_LONG:   t->kind=TY_LONG;   adv(p);break;
    case TOK_ULONG:  t->kind=TY_ULONG;  adv(p);break;
    case TOK_FLOAT:  t->kind=TY_FLOAT;  adv(p);break;
    case TOK_DOUBLE: t->kind=TY_DOUBLE; adv(p);break;
    case TOK_CHAR:   t->kind=TY_CHAR;   adv(p);break;
    case TOK_STR:    t->kind=TY_STR;    adv(p);break;
    case TOK_UNIT:   t->kind=TY_VOID;   adv(p);break;
    case TOK_IDENT:
        t->kind=TY_NAMED;
        t->name=arena_strdup(p->arena,p->cur.value);adv(p);
        if(chk(p,TOK_LT)){
            adv(p);int depth=1;
            while(!chk(p,TOK_EOF)&&depth>0){
                if(chk(p,TOK_LT))depth++;
                else if(chk(p,TOK_GT))depth--;
                adv(p);
            }
        }
        break;
    default: t->kind=TY_UNKNOWN;break;
    }
    return t;
}

static AstList *parse_params(Parser *p){
    AstList *head=NULL,*tail=NULL;
    while(!chk(p,TOK_RPAREN)&&!chk(p,TOK_EOF)){
        AstNode *param=nd(p,NODE_VAR_DECL);
        if(chk(p,TOK_MUT)){adv(p);param->var_decl.is_mut=1;}
        if(!chk(p,TOK_IDENT)){err(p,"expected identifier as parameter name");break;}
        param->var_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
        if(mat(p,TOK_COLON))param->var_decl.type=parse_type(p);
        if(mat(p,TOK_ASSIGN))param->var_decl.init=parse_expr(p);
        head=lapp(p,head,&tail,param);
        if(!mat(p,TOK_COMMA))break;
    }
    return head;
}

static AstNode *parse_primary(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_postfix(Parser *p);

static AstNode *parse_primary(Parser *p){
    if(chk(p,TOK_INT_LIT)){
        AstNode *n=nd(p,NODE_INT_LIT);
        n->int_lit.val=(long long)strtoll(p->cur.value,NULL,0);
        n->ty=TY_INT;adv(p);return n;
    }
    if(chk(p,TOK_LONG_LIT)){
        AstNode *n=nd(p,NODE_LONG_LIT);
        n->long_lit.val=(long long)strtoll(p->cur.value,NULL,0);
        n->ty=TY_LONG;adv(p);return n;
    }
    if(chk(p,TOK_FLOAT_LIT)){
        AstNode *n=nd(p,NODE_FLOAT_LIT);
        n->float_lit.val=strtod(p->cur.value,NULL);
        n->ty=TY_FLOAT;adv(p);return n;
    }
    if(chk(p,TOK_DOUBLE_LIT)){
        AstNode *n=nd(p,NODE_DOUBLE_LIT);
        n->double_lit.val=strtod(p->cur.value,NULL);
        n->ty=TY_DOUBLE;adv(p);return n;
    }
    if(chk(p,TOK_NULL)){AstNode *n=nd(p,NODE_NULL);adv(p);n->ty=TY_RAW_PTR;return n;}
    if(chk(p,TOK_BOOL_LIT)){
        AstNode *n=nd(p,NODE_BOOL_LIT);
        n->bool_lit.val=strcmp(p->cur.value,"true")==0?1:0;
        n->ty=TY_BOOL;adv(p);return n;
    }
    if(chk(p,TOK_STRING_LIT)){
        const char *raw=p->cur.value;
        
        int has_interp=0;
        for(const char *s=raw;*s;s++){
            if(s[0]=='%'&&s[1]=='{'){has_interp=1;break;}
        }
        if(!has_interp){
            AstNode *n=nd(p,NODE_STRING_LIT);
            n->string_lit.val=arena_strdup(p->arena,raw);
            n->ty=TY_STR;adv(p);return n;
        }
        
        AstNode *n=nd(p,NODE_INTERP_STR);adv(p);
        AstList *head=NULL,*tail=NULL;
        const char *src=raw;
        char *seg=(char*)arena_alloc(p->arena,65536);
        while(*src){
            
            int slen=0;
            while(*src&&!(src[0]=='%'&&src[1]=='{')){
                seg[slen++]=*src++;
            }
            
            {
                AstNode *lit=arena_alloc(p->arena,sizeof(AstNode));
                memset(lit,0,sizeof(AstNode));
                lit->kind=NODE_STRING_LIT; lit->ty=TY_STR;
                char *sv=arena_alloc(p->arena,(size_t)slen+1);
                memcpy(sv,seg,(size_t)slen); sv[slen]='\0';
                lit->string_lit.val=sv;
                head=lapp(p,head,&tail,lit);
            }
            if(!*src) break;  
            src+=2; 
            
            int depth=1; const char *expr_start=src;
            while(*src&&depth>0){
                if(*src=='{')depth++;
                else if(*src=='}')depth--;
                if(depth>0)src++;else break;
            }
            size_t expr_len=(size_t)(src-expr_start);
            if(*src=='}')src++; 
            
            char *expr_src=(char*)arena_alloc(p->arena,expr_len+1);
            memcpy(expr_src,expr_start,expr_len); expr_src[expr_len]='\0';
            Parser sub=parser_new(expr_src,expr_len,p->arena,p->src_path);
            AstNode *expr=parse_expr(&sub);
            head=lapp(p,head,&tail,expr);
        }
        n->interp_str.parts=head;
        n->ty=TY_STR;return n;
    }
    if(chk(p,TOK_CHAR_LIT)){
        AstNode *n=nd(p,NODE_CHAR_LIT);
        n->char_lit.val=p->cur.value[0];
        n->ty=TY_CHAR;adv(p);return n;
    }
    if(chk(p,TOK_THIS)){AstNode *n=nd(p,NODE_THIS);adv(p);return n;}
    if(chk(p,TOK_LBRACKET)){
        AstNode *n=nd(p,NODE_ARRAY_LIT);adv(p);
        AstList *head=NULL,*tail=NULL;
        while(!chk(p,TOK_RBRACKET)&&!chk(p,TOK_EOF)){
            AstNode *e=parse_expr(p);
            head=lapp(p,head,&tail,e);
            if(!mat(p,TOK_COMMA))break;
        }
        expect(p,TOK_RBRACKET,"expected ']'");
        n->array_lit.elems=head;n->ty=TY_ARRAY_DYN;return n;
    }
    if(chk(p,TOK_PIPE)){
        AstNode *n=nd(p,NODE_LAMBDA);adv(p);
        AstList *head=NULL,*tail=NULL;
        while(!chk(p,TOK_PIPE)&&!chk(p,TOK_EOF)){
            AstNode *param=nd(p,NODE_VAR_DECL);
            if(!chk(p,TOK_IDENT)){err(p,"expected identifier as parameter name");break;}
            param->var_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
            if(mat(p,TOK_COLON))param->var_decl.type=parse_type(p);
            head=lapp(p,head,&tail,param);
            if(!mat(p,TOK_COMMA))break;
        }
        mat(p,TOK_PIPE);
        if(mat(p,TOK_ARROW)||mat(p,TOK_COLON))n->lambda.ret_type=parse_type(p);
        n->lambda.params=head;
        n->lambda.body=chk(p,TOK_LBRACE)?parse_block(p):parse_expr(p);
        return n;
    }
    if(chk(p,TOK_AT)){
        adv(p);
        if(!chk(p,TOK_IDENT)){
            err(p,"expected identifier as intrinsic name after '@'");
            AstNode *n=nd(p,NODE_INT_LIT);n->int_lit.val=0;n->ty=TY_INT;return n;
        }
        AstNode *n=nd(p,NODE_AT_CALL);
        n->at_call.name=arena_strdup(p->arena,p->cur.value);adv(p);
        n->at_call.type_arg=NULL;n->at_call.args=NULL;
        expect(p,TOK_LPAREN,"expected '(' after intrinsic identifier");
        if(!strcmp(n->at_call.name,"sizeof")||!strcmp(n->at_call.name,"alignof")||!strcmp(n->at_call.name,"typeof")){
            if(!chk(p,TOK_RPAREN))n->at_call.type_arg=parse_type(p);
        } else {
            AstList *head=NULL,*tail=NULL;
            while(!chk(p,TOK_RPAREN)&&!chk(p,TOK_EOF)){
                AstNode *arg=parse_expr(p);
                head=lapp(p,head,&tail,arg);
                if(!mat(p,TOK_COMMA))break;
            }
            n->at_call.args=head;
        }
        expect(p,TOK_RPAREN,"expected ')'");
        return n;
    }
    if(chk(p,TOK_IDENT)&&chkp(p,TOK_DOUBLE_COLON)){
        char *ns=arena_strdup(p->arena,p->cur.value);
        adv(p);adv(p);
        if(chk(p,TOK_EOF)||chk(p,TOK_SEMICOLON)){
            err(p,"expected identifier after '::'");
            AstNode *n=nd(p,NODE_INT_LIT);n->int_lit.val=0;n->ty=TY_INT;return n;
        }
        char *fname=arena_strdup(p->arena,p->cur.value);adv(p);
        if(chk(p,TOK_LPAREN)){
            AstNode *n=nd(p,NODE_NS_CALL);adv(p);
            AstList *head=NULL,*tail=NULL;
            while(!chk(p,TOK_RPAREN)&&!chk(p,TOK_EOF)){
                AstNode *arg=parse_expr(p);
                head=lapp(p,head,&tail,arg);
                if(!mat(p,TOK_COMMA))break;
            }
            expect(p,TOK_RPAREN,"expected ')'");
            n->ns_call.ns=ns;n->ns_call.func=fname;n->ns_call.args=head;
            return n;
        }
        AstNode *n=nd(p,NODE_IDENT);
        char buf[256];snprintf(buf,sizeof(buf),"%s::%s",ns,fname);
        n->ident.name=arena_strdup(p->arena,buf);return n;
    }
    if(chk(p,TOK_IDENT)&&chkp(p,TOK_LBRACE)&&!p->no_struct_lit){
        AstNode *n=nd(p,NODE_STRUCT_INIT);
        n->struct_init.type_name=arena_strdup(p->arena,p->cur.value);adv(p);
        expect(p,TOK_LBRACE,"expected '{'");
        AstList *head=NULL,*tail=NULL;
        while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
            if(!chk(p,TOK_IDENT)){err(p,"expected identifier as field name");break;}
            AstNode *fa=nd(p,NODE_ASSIGN);
            AstNode *lv=nd(p,NODE_IDENT);
            lv->ident.name=arena_strdup(p->arena,p->cur.value);adv(p);
            fa->assign.left=lv;
            expect(p,TOK_COLON,"expected ':'");
            fa->assign.right=parse_expr(p);
            head=lapp(p,head,&tail,fa);
            if(!mat(p,TOK_COMMA))break;
        }
        expect(p,TOK_RBRACE,"expected '}'");
        n->struct_init.fields=head;return n;
    }
    if(chk(p,TOK_IDENT)){
        AstNode *n=nd(p,NODE_IDENT);
        n->ident.name=arena_strdup(p->arena,p->cur.value);adv(p);return n;
    }
    if(chk(p,TOK_LPAREN)){
        adv(p);AstNode *n=parse_expr(p);
        expect(p,TOK_RPAREN,"expected ')'");return n;
    }
    if(chk(p,TOK_AMP)){
        AstNode *n=nd(p,NODE_REF);adv(p);
        n->ref_expr.expr=parse_unary(p);return n;
    }
    if(chk(p,TOK_STAR)){
        AstNode *n=nd(p,NODE_DEREF);adv(p);
        n->deref_expr.expr=parse_unary(p);return n;
    }
    if(chk(p,TOK_INTERP_START)){
        AstNode *n=nd(p,NODE_INTERP_STR);adv(p);
        AstList *head=NULL,*tail=NULL;
        while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
            AstNode *part=parse_expr(p);
            head=lapp(p,head,&tail,part);
            mat(p,TOK_COMMA);
        }
        mat(p,TOK_RBRACE);
        n->interp_str.parts=head;return n;
    }
    err(p,"unexpected token in expression context");adv(p);
    AstNode *n=nd(p,NODE_INT_LIT);n->int_lit.val=0;n->ty=TY_INT;return n;
}

static AstNode *parse_postfix(Parser *p){
    AstNode *n=parse_primary(p);
    for(;;){
        if(chk(p,TOK_DOT)){
            adv(p);
            if(!chk(p,TOK_IDENT)){err(p,"expected identifier as field name after '.'");break;}
            if(chkp(p,TOK_LPAREN)){
                AstNode *callee=nd(p,NODE_FIELD);
                callee->field.obj=n;
                callee->field.field=arena_strdup(p->arena,p->cur.value);adv(p);
                AstNode *call=nd(p,NODE_CALL);
                call->call.callee=callee;adv(p);
                AstList *ah=NULL,*at=NULL;
                while(!chk(p,TOK_RPAREN)&&!chk(p,TOK_EOF)){
                    AstNode *arg=parse_expr(p);
                    ah=lapp(p,ah,&at,arg);
                    if(!mat(p,TOK_COMMA))break;
                }
                expect(p,TOK_RPAREN,"expected ')'");
                call->call.args=ah;n=call;
            }else{
                AstNode *f=nd(p,NODE_FIELD);
                f->field.obj=n;
                f->field.field=arena_strdup(p->arena,p->cur.value);adv(p);
                n=f;
            }
        }else if(chk(p,TOK_LPAREN)){
            adv(p);
            AstNode *call=nd(p,NODE_CALL);
            call->call.callee=n;
            AstList *head=NULL,*tail=NULL;
            while(!chk(p,TOK_RPAREN)&&!chk(p,TOK_EOF)){
                AstNode *arg=parse_expr(p);
                head=lapp(p,head,&tail,arg);
                if(!mat(p,TOK_COMMA))break;
            }
            expect(p,TOK_RPAREN,"expected ')'");
            call->call.args=head;n=call;
        }else if(chk(p,TOK_LBRACKET)){
            adv(p);
            AstNode *idx=nd(p,NODE_INDEX);
            idx->index.obj=n;idx->index.idx=parse_expr(p);
            expect(p,TOK_RBRACKET,"expected ']'");n=idx;
        }else if(chk(p,TOK_INC)){
            AstNode *u=nd(p,NODE_UNARY);adv(p);
            u->unary.op=arena_strdup(p->arena,"++");
            u->unary.operand=n;u->unary.prefix=0;n=u;
        }else if(chk(p,TOK_DEC)){
            AstNode *u=nd(p,NODE_UNARY);adv(p);
            u->unary.op=arena_strdup(p->arena,"--");
            u->unary.operand=n;u->unary.prefix=0;n=u;
        }else if(chk(p,TOK_QUESTION)){
            adv(p);
            if(chk(p,TOK_SEMICOLON)||chk(p,TOK_COMMA)||chk(p,TOK_RPAREN)||
               chk(p,TOK_RBRACE)||chk(p,TOK_RBRACKET)||chk(p,TOK_EOF)){
                AstNode *t=nd(p,NODE_TRY);t->try_expr.expr=n;n=t;
            }else{
                AstNode *then=parse_expr(p);
                expect(p,TOK_COLON,"expected ':'");
                AstNode *els=parse_expr(p);
                AstNode *ifn=nd(p,NODE_IF);
                ifn->if_stmt.cond=n;ifn->if_stmt.then=then;ifn->if_stmt.els=els;
                n=ifn;
            }
        }else if(chk(p,TOK_AS)||(chk(p,TOK_IDENT)&&strcmp(p->cur.value,"as")==0)){
            adv(p);
            AstNode *cast=nd(p,NODE_CAST);
            cast->cast.expr=n;cast->cast.type=parse_type(p);n=cast;
        }else break;
    }
    return n;
}

static AstNode *parse_unary(Parser *p){
    if(chk(p,TOK_MINUS)){
        AstNode *n=nd(p,NODE_UNARY);adv(p);
        n->unary.op=arena_strdup(p->arena,"-");
        n->unary.operand=parse_unary(p);n->unary.prefix=1;return n;
    }
    if(chk(p,TOK_NOT)){
        AstNode *n=nd(p,NODE_UNARY);adv(p);
        n->unary.op=arena_strdup(p->arena,"!");
        n->unary.operand=parse_unary(p);n->unary.prefix=1;return n;
    }
    if(chk(p,TOK_TILDE)){
        AstNode *n=nd(p,NODE_UNARY);adv(p);
        n->unary.op=arena_strdup(p->arena,"~");
        n->unary.operand=parse_unary(p);n->unary.prefix=1;return n;
    }
    if(chk(p,TOK_INC)){
        AstNode *n=nd(p,NODE_UNARY);adv(p);
        n->unary.op=arena_strdup(p->arena,"++");
        n->unary.operand=parse_unary(p);n->unary.prefix=1;return n;
    }
    if(chk(p,TOK_DEC)){
        AstNode *n=nd(p,NODE_UNARY);adv(p);
        n->unary.op=arena_strdup(p->arena,"--");
        n->unary.operand=parse_unary(p);n->unary.prefix=1;return n;
    }
    if(chk(p,TOK_AMP)){
        AstNode *n=nd(p,NODE_REF);adv(p);
        n->ref_expr.expr=parse_unary(p);return n;
    }
    if(chk(p,TOK_STAR)){
        AstNode *n=nd(p,NODE_DEREF);adv(p);
        n->deref_expr.expr=parse_unary(p);return n;
    }
    return parse_postfix(p);
}

static int bin_prec(TokenType t){
    switch(t){
    case TOK_RANGE: case TOK_RANGE_EQ: return 0;
    case TOK_OR:    return 1;
    case TOK_AND:   return 2;
    case TOK_PIPE:  return 3;
    case TOK_CARET: return 4;
    case TOK_AMP:   return 5;
    case TOK_EQ: case TOK_NEQ: return 6;
    case TOK_LT: case TOK_GT: case TOK_LTE: case TOK_GTE: return 7;
    case TOK_SHL: case TOK_SHR: return 8;
    case TOK_PLUS: case TOK_MINUS: return 9;
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 10;
    default: return -1;
    }
}

static AstNode *parse_binary(Parser *p, int min_prec){
    AstNode *left=parse_unary(p);
    for(;;){
        int prec=bin_prec(p->cur.type);
        if(prec<min_prec)break;
        char *op=arena_strdup(p->arena,p->cur.value);
        adv(p);
        AstNode *right=parse_binary(p,prec+1);
        AstNode *b=nd(p,NODE_BINARY);
        b->binary.op=op; b->binary.left=left; b->binary.right=right;
        left=b;
    }
    return left;
}

static AstNode *parse_expr(Parser *p){
    AstNode *left=parse_binary(p,0);
    TokenType ct=p->cur.type;
    if(ct==TOK_ASSIGN||ct==TOK_PLUS_ASSIGN||ct==TOK_MINUS_ASSIGN||
       ct==TOK_STAR_ASSIGN||ct==TOK_SLASH_ASSIGN||ct==TOK_PERCENT_ASSIGN||
       ct==TOK_AMP_ASSIGN||ct==TOK_PIPE_ASSIGN||ct==TOK_CARET_ASSIGN||
       ct==TOK_SHL_ASSIGN||ct==TOK_SHR_ASSIGN){
        char *op=arena_strdup(p->arena,p->cur.value);adv(p);
        AstNode *right=parse_expr(p);
        if(ct==TOK_ASSIGN){
            AstNode *a=nd(p,NODE_ASSIGN);
            a->assign.left=left;a->assign.right=right;return a;
        }
        AstNode *ca=nd(p,NODE_COMPOUND_ASSIGN);
        ca->compound_assign.op=op;
        ca->compound_assign.left=left;ca->compound_assign.right=right;return ca;
    }
    return left;
}

static AstNode *parse_var_decl(Parser *p, int is_mut, int is_const){
    adv(p);
    AstNode *n=nd(p,NODE_VAR_DECL);
    n->var_decl.is_mut=is_mut; n->var_decl.is_const=is_const;
    if(!chk(p,TOK_IDENT)){err(p,"expected variable name");return n;}
    n->var_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
    if(mat(p,TOK_COLON))n->var_decl.type=parse_type(p);
    if(mat(p,TOK_ASSIGN))n->var_decl.init=parse_expr(p);
    expect(p,TOK_SEMICOLON,"expected ';'");
    return n;
}

static AstNode *parse_enum_decl(Parser *p){
    adv(p);
    if(!chk(p,TOK_IDENT)){err(p,"expected enum name");return NULL;}
    AstNode *n=nd(p,NODE_ENUM_DECL);
    n->enum_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
    expect(p,TOK_LBRACE,"expected '{'");
    AstList *head=NULL,*tail=NULL;
    while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
        if(!chk(p,TOK_IDENT)){err(p,"expected variant name");adv(p);continue;}
        AstNode *v=nd(p,NODE_VAR_DECL);
        v->var_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
        if(chk(p,TOK_LPAREN)){
            adv(p);
            v->var_decl.type=parse_type(p);
            expect(p,TOK_RPAREN,"expected ')'");
        }
        head=lapp(p,head,&tail,v);
        if(!mat(p,TOK_COMMA))break;
    }
    expect(p,TOK_RBRACE,"expected '}'");
    n->enum_decl.variants=head;return n;
}

static AstNode *parse_if(Parser *p){
    AstNode *n=nd(p,NODE_IF);adv(p);
    expect(p,TOK_LPAREN,"expected '(' after 'if'");
    p->no_struct_lit++;
    n->if_stmt.cond=parse_expr(p);
    p->no_struct_lit--;
    expect(p,TOK_RPAREN,"expected ')'");
    n->if_stmt.then=parse_block(p);
    if(mat(p,TOK_ELSE)){
        if(chk(p,TOK_IF)) n->if_stmt.els=parse_if(p);
        else n->if_stmt.els=parse_block(p);
    }
    return n;
}

static AstNode *parse_while(Parser *p){
    AstNode *n=nd(p,NODE_WHILE);adv(p);
    expect(p,TOK_LPAREN,"expected '(' after 'while'");
    p->no_struct_lit++;
    n->while_stmt.cond=parse_expr(p);
    p->no_struct_lit--;
    expect(p,TOK_RPAREN,"expected ')'");
    n->while_stmt.body=parse_block(p);return n;
}

static AstNode *parse_do_while(Parser *p){
    AstNode *n=nd(p,NODE_DO_WHILE);adv(p);
    n->while_stmt.body=parse_block(p);
    mat(p,TOK_FAT_ARROW); 
    expect(p,TOK_WHILE,"expected 'while'");
    int has_paren=chk(p,TOK_LPAREN);
    if(has_paren) adv(p);
    p->no_struct_lit++;
    n->while_stmt.cond=parse_expr(p);
    p->no_struct_lit--;
    if(has_paren) expect(p,TOK_RPAREN,"expected ')'");
    expect(p,TOK_SEMICOLON,"expected ';'");return n;
}

static AstNode *parse_for(Parser *p){
    adv(p);
    if(!chk(p,TOK_LPAREN)){err(p,"expected '(' after 'for'");return NULL;}
    adv(p);
    if((chk(p,TOK_LET)||chk(p,TOK_MUT)||chk(p,TOK_IDENT))&&chkp(p,TOK_IN)){
        AstNode *n=nd(p,NODE_FOR_IN);
        n->for_in.var=arena_strdup(p->arena,p->cur.value);adv(p);
        expect(p,TOK_IN,"expected 'in'");
        p->no_struct_lit++;
        n->for_in.iter=parse_expr(p);
        p->no_struct_lit--;
        expect(p,TOK_RPAREN,"expected ')'");
        n->for_in.body=parse_block(p);return n;
    }
    AstNode *n=nd(p,NODE_FOR_C);
    if(!chk(p,TOK_SEMICOLON)){
        if(chk(p,TOK_LET)||chk(p,TOK_MUT)){
            n->for_c.init=parse_var_decl(p,chk(p,TOK_MUT),0);
        } else {
            AstNode *es=nd(p,NODE_EXPR_STMT);
            p->no_struct_lit++;
            es->expr_stmt.expr=parse_expr(p);
            p->no_struct_lit--;
            expect(p,TOK_SEMICOLON,"expected ';'");
            n->for_c.init=es;
        }
    } else mat(p,TOK_SEMICOLON);
    if(!chk(p,TOK_SEMICOLON)){
        p->no_struct_lit++;
        n->for_c.cond=parse_expr(p);
        p->no_struct_lit--;
    }
    expect(p,TOK_SEMICOLON,"expected ';'");
    if(!chk(p,TOK_RPAREN)){
        p->no_struct_lit++;
        n->for_c.step=parse_expr(p);
        p->no_struct_lit--;
    }
    expect(p,TOK_RPAREN,"expected ')'");
    n->for_c.body=parse_block(p);return n;
}

static AstNode *parse_match_arm(Parser *p){
    AstNode *arm=nd(p,NODE_MATCH_ARM);
    if(chk(p,TOK_IDENT)&&strcmp(p->cur.value,"_")==0){
        arm->match_arm.is_wildcard=1;adv(p);
    } else if(chk(p,TOK_STRING_LIT)){
        arm->match_arm.is_string=1;
        arm->match_arm.str_val=arena_strdup(p->arena,p->cur.value);adv(p);
    } else if(chk(p,TOK_BOOL_LIT)){
        
        arm->match_arm.lit_lo=strcmp(p->cur.value,"true")==0?1:0;
        arm->match_arm.lit_hi=arm->match_arm.lit_lo;
        adv(p);
    } else if(chk(p,TOK_IDENT)&&(chkp(p,TOK_LPAREN)||chkp(p,TOK_FAT_ARROW)||chkp(p,TOK_COMMA))){
        arm->match_arm.is_variant=1;
        arm->match_arm.variant_name=arena_strdup(p->arena,p->cur.value);adv(p);
        if(chk(p,TOK_LPAREN)){
            adv(p);
            if(chk(p,TOK_IDENT)){
                arm->match_arm.bind_name=arena_strdup(p->arena,p->cur.value);adv(p);
            }
            expect(p,TOK_RPAREN,"expected ')'");
        }
    } else {
        long long lo=(long long)strtoll(p->cur.value,NULL,0);
        if(chk(p,TOK_INT_LIT)||chk(p,TOK_LONG_LIT)||chk(p,TOK_CHAR_LIT)){
            if(chk(p,TOK_CHAR_LIT)) lo=(unsigned char)p->cur.value[0];
            adv(p);
        }
        if(chk(p,TOK_RANGE)||chk(p,TOK_RANGE_EQ)){
            int incl=chk(p,TOK_RANGE_EQ);adv(p);
            long long hi=(long long)strtoll(p->cur.value,NULL,0);
            if(chk(p,TOK_INT_LIT)||chk(p,TOK_LONG_LIT)){adv(p);}
            arm->match_arm.is_range=1;
            arm->match_arm.lit_lo=lo;
            arm->match_arm.lit_hi=incl?hi:hi-1;
        } else {
            arm->match_arm.lit_lo=lo;arm->match_arm.lit_hi=lo;
        }
    }
    expect(p,TOK_FAT_ARROW,"expected '=>'");
    arm->match_arm.body=chk(p,TOK_LBRACE)?parse_block(p):parse_stmt(p);
    mat(p,TOK_COMMA);
    return arm;
}

static AstNode *parse_match(Parser *p){
    AstNode *n=nd(p,NODE_MATCH);adv(p);
    expect(p,TOK_LPAREN,"expected '(' after 'match'");
    p->no_struct_lit++;
    n->match_stmt.expr=parse_expr(p);
    p->no_struct_lit--;
    expect(p,TOK_RPAREN,"expected ')'");
    expect(p,TOK_LBRACE,"expected '{'");
    AstList *head=NULL,*tail=NULL;
    while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
        AstNode *arm=parse_match_arm(p);
        head=lapp(p,head,&tail,arm);
    }
    expect(p,TOK_RBRACE,"expected '}'");
    n->match_stmt.arms=head;return n;
}

static AstNode *parse_region(Parser *p){
    AstNode *n=nd(p,NODE_REGION);adv(p);
    if(!chk(p,TOK_IDENT)){err(p,"expected region name");return NULL;}
    n->region.expr=(AstNode*)(void*)arena_strdup(p->arena,p->cur.value);
    adv(p);
    expect(p,TOK_LBRACE,"expected '{'");
    AstList *head=NULL,*tail=NULL;
    while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
        AstNode *s=parse_stmt(p);
        if(s)head=lapp(p,head,&tail,s);
    }
    expect(p,TOK_RBRACE,"expected '}'");
    n->region_block.body=head;return n;
}

static AstNode *parse_unsafe(Parser *p){
    AstNode *n=nd(p,NODE_UNSAFE_BLOCK);adv(p);
    n->unsafe_block.body=parse_block(p);return n;
}

static int is_fn_name_tok(Parser *p){
    
    switch(p->cur.type){
    case TOK_IDENT: case TOK_NEW: case TOK_LET: case TOK_MUT:
    case TOK_INT: case TOK_UINT: case TOK_LONG: case TOK_ULONG:
    case TOK_FLOAT: case TOK_DOUBLE: case TOK_BOOL: case TOK_BYTE:
    case TOK_CHAR: case TOK_STR: case TOK_SHORT: case TOK_USHORT:
        return 1;
    default: return 0;
    }
}
static AstNode *parse_fn_decl(Parser *p, int is_unit, int is_extern, int is_exported){
    adv(p);
    if(!is_fn_name_tok(p)){err(p,"expected function name");return NULL;}
    AstNode *n=nd(p,NODE_FN_DECL);
    n->fn_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
    n->fn_decl.is_unit=is_unit;
    n->fn_decl.is_extern=is_extern;
    n->fn_decl.is_exported=is_exported;
    expect(p,TOK_LPAREN,"expected '(' after function name");
    n->fn_decl.params=parse_params(p);
    expect(p,TOK_RPAREN,"expected ')'");
    if(chk(p,TOK_COLON)||chk(p,TOK_ARROW)){
        adv(p);n->fn_decl.ret_type=parse_type(p);
    }
    if(is_extern){
        if(chk(p,TOK_LBRACE)){
            
            adv(p); 
            if(chk(p,TOK_RBRACE)) adv(p); 
            else { 
                int depth=1;
                while(!chk(p,TOK_EOF)&&depth>0){
                    if(chk(p,TOK_LBRACE))depth++;
                    else if(chk(p,TOK_RBRACE))depth--;
                    adv(p);
                }
            }
        } else mat(p,TOK_SEMICOLON);
        n->fn_decl.body=NULL;return n;
    }
    n->fn_decl.body=parse_block(p);return n;
}

static AstList *parse_class_body(Parser *p){
    AstList *head=NULL,*tail=NULL;
    while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
        AstNode *member=NULL;
        if(chk(p,TOK_FN)) member=parse_fn_decl(p,0,0,0);
        else if(chk(p,TOK_UNIT)) member=parse_fn_decl(p,1,0,0);
        else if(chk(p,TOK_MUT)||chk(p,TOK_LET))
            member=parse_var_decl(p,chk(p,TOK_MUT),0);
        else if(chk(p,TOK_IDENT)){
            member=nd(p,NODE_VAR_DECL);
            member->var_decl.name=arena_strdup(p->arena,p->cur.value);
            member->var_decl.is_mut=1;adv(p);
            expect(p,TOK_COLON,"expected ':'");
            member->var_decl.type=parse_type(p);
            expect(p,TOK_SEMICOLON,"expected ';'");
        } else { err(p,"unexpected token in class body"); adv(p); continue; }
        if(member)head=lapp(p,head,&tail,member);
    }
    return head;
}

static AstNode *parse_stmt(Parser *p){
    switch(p->cur.type){
    case TOK_LET:    return parse_var_decl(p,0,0);
    case TOK_MUT:    return parse_var_decl(p,1,0);
    case TOK_CONST:  return parse_var_decl(p,0,1);
    case TOK_FN:     return parse_fn_decl(p,0,0,0);
    case TOK_UNIT:   return parse_fn_decl(p,1,0,0);
    case TOK_UNSAFE: return parse_unsafe(p);
    case TOK_EXTERNAL: {
        adv(p);
        int is_unit=chk(p,TOK_UNIT);
        if(chk(p,TOK_FN)||chk(p,TOK_UNIT)) return parse_fn_decl(p,is_unit,1,0);
        err(p,"expected 'fn' or 'unit' after 'external'");return NULL;
    }
    case TOK_IF:     return parse_if(p);
    case TOK_WHILE:  return parse_while(p);
    case TOK_DO:     return parse_do_while(p);
    case TOK_FOR:    return parse_for(p);
    case TOK_MATCH:  return parse_match(p);
    case TOK_RETURN: {
        AstNode *n=nd(p,NODE_RETURN);adv(p);
        if(!chk(p,TOK_SEMICOLON))n->ret.val=parse_expr(p);
        expect(p,TOK_SEMICOLON,"expected ';'");return n;
    }
    case TOK_ENUM:   return parse_enum_decl(p);
    case TOK_LBRACE: return parse_block(p);
    case TOK_REGION: return parse_region(p);
    case TOK_DEFER: {
        AstNode *n=nd(p,NODE_DEFER);adv(p);
        n->defer_stmt.expr=parse_stmt(p);return n;
    }
    case TOK_BREAK: {
        AstNode *n=nd(p,NODE_BREAK);adv(p);
        expect(p,TOK_SEMICOLON,"expected ';'");return n;
    }
    case TOK_CONTINUE: {
        AstNode *n=nd(p,NODE_CONTINUE);adv(p);
        expect(p,TOK_SEMICOLON,"expected ';'");return n;
    }
    case TOK_COMPTIME: {
        adv(p);
        if(!chk(p,TOK_LET)){err(p,"expected 'let' after 'comptime'");return NULL;}
        adv(p);
        AstNode *n=nd(p,NODE_COMPTIME_LET);
        if(!chk(p,TOK_IDENT)){err(p,"expected identifier as binding name");return NULL;}
        n->comptime_let.name=arena_strdup(p->arena,p->cur.value);adv(p);
        if(mat(p,TOK_COLON))parse_type(p);
        expect(p,TOK_ASSIGN,"expected '='");
        n->comptime_let.init=parse_expr(p);
        expect(p,TOK_SEMICOLON,"expected ';'");return n;
    }
    default: {
        AstNode *es=nd(p,NODE_EXPR_STMT);
        es->expr_stmt.expr=parse_expr(p);
        expect(p,TOK_SEMICOLON,"expected ';'");return es;
    }
    }
}

static AstNode *parse_block(Parser *p){
    AstNode *n=nd(p,NODE_BLOCK);
    expect(p,TOK_LBRACE,"expected '{'");
    AstList *head=NULL,*tail=NULL;
    while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
        AstNode *s=parse_stmt(p);
        if(s)head=lapp(p,head,&tail,s);
    }
    expect(p,TOK_RBRACE,"expected '}'");
    n->block.stmts=head;return n;
}

static AstNode *parse_import(Parser *p){
    adv(p);AstNode *n=nd(p,NODE_IMPORT);
    if(chk(p,TOK_LT)){
        adv(p);
        char buf[256];buf[0]='\0';
        while(!chk(p,TOK_GT)&&!chk(p,TOK_EOF)){
            size_t cl=strlen(buf),vl=strlen(p->cur.value);
            if(cl+vl<255){memcpy(buf+cl,p->cur.value,vl);buf[cl+vl]='\0';}
            adv(p);
        }
        expect(p,TOK_GT,"expected '>'");
        n->import.path=arena_strdup(p->arena,buf);n->import.is_std=1;
    }else if(chk(p,TOK_STRING_LIT)){
        n->import.path=arena_strdup(p->arena,p->cur.value);n->import.is_std=0;adv(p);
    }else err(p,"expected string literal or '<...>' as import path");
    mat(p,TOK_SEMICOLON);
    return n;
}

static AstNode *parse_top_level(Parser *p){
    if(chk(p,TOK_EXPORT)){
        adv(p);
        if(chk(p,TOK_MODULE)){
            adv(p);
            AstNode *n=nd(p,NODE_EXPORT_MODULE);
            mat(p,TOK_SEMICOLON);
            return n;
        }
        int is_unit=chk(p,TOK_UNIT);
        if(chk(p,TOK_FN)||chk(p,TOK_UNIT)){
            AstNode *fn=parse_fn_decl(p,is_unit,0,1);
            return fn;
        }
        if(chk(p,TOK_EXTERNAL)){
            adv(p);
            is_unit=chk(p,TOK_UNIT);
            if(chk(p,TOK_FN)||chk(p,TOK_UNIT)){
                AstNode *fn=parse_fn_decl(p,is_unit,1,1);
                return fn;
            }
        }
        err(p,"expected 'module', 'fn', 'unit', or 'external' after 'export'");
        return NULL;
    }
    switch(p->cur.type){
    case TOK_IMPORT: return parse_import(p);
    case TOK_FN:     return parse_fn_decl(p,0,0,0);
    case TOK_UNIT:   return parse_fn_decl(p,1,0,0);
    case TOK_EXTERNAL: {
        adv(p);
        int is_unit=chk(p,TOK_UNIT);
        if(chk(p,TOK_FN)||chk(p,TOK_UNIT)) return parse_fn_decl(p,is_unit,1,0);
        err(p,"expected 'fn' or 'unit' after 'external'");return NULL;
    }
    case TOK_ENUM:   return parse_enum_decl(p);
    case TOK_CLASS: {
        adv(p);
        if(!chk(p,TOK_IDENT)){err(p,"expected identifier as class declaration name");return NULL;}
        AstNode *n=nd(p,NODE_CLASS_DECL);
        n->class_decl.name=arena_strdup(p->arena,p->cur.value);adv(p);
        expect(p,TOK_LBRACE,"expected '{'");
        while(!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)){
            if(chk(p,TOK_PRIVATE)){
                adv(p);
                expect(p,TOK_LBRACE,"expected '{'");
                n->class_decl.private_members=parse_class_body(p);
                expect(p,TOK_RBRACE,"expected '}'");
            } else if(chk(p,TOK_PUBLIC)){
                adv(p);
                expect(p,TOK_LBRACE,"expected '{'");
                n->class_decl.public_members=parse_class_body(p);
                expect(p,TOK_RBRACE,"expected '}'");
            } else { err(p,"expected 'private' or 'public' access specifier in class body"); adv(p); }
        }
        expect(p,TOK_RBRACE,"expected '}'");return n;
    }
    case TOK_CONST:  return parse_var_decl(p,0,1);
    case TOK_REGION: return parse_region(p);
    case TOK_COMPTIME: {
        adv(p);
        if(!chk(p,TOK_LET)){err(p,"expected 'let' after 'comptime'");return NULL;}
        adv(p);
        AstNode *n=nd(p,NODE_COMPTIME_LET);
        if(!chk(p,TOK_IDENT)){err(p,"expected identifier as binding name");return NULL;}
        n->comptime_let.name=arena_strdup(p->arena,p->cur.value);adv(p);
        if(mat(p,TOK_COLON))parse_type(p);
        expect(p,TOK_ASSIGN,"expected '='");
        n->comptime_let.init=parse_expr(p);
        expect(p,TOK_SEMICOLON,"expected ';'");return n;
    }
    case TOK_TYPE: {
        adv(p);
        if(!chk(p,TOK_IDENT)){err(p,"expected identifier as type alias name");return NULL;}
        AstNode *n=nd(p,NODE_TYPE_ALIAS);
        n->type_alias.name=arena_strdup(p->arena,p->cur.value);adv(p);
        expect(p,TOK_ASSIGN,"expected '='");
        n->type_alias.type=parse_type(p);
        mat(p,TOK_SEMICOLON);return n;
    }
    default:
        err(p,"unexpected token at top-level declaration context");adv(p);return NULL;
    }
}

Parser parser_new(const char *src, size_t src_len, Arena *arena, const char *src_path){
    Parser p;
    p.lexer=lexer_new(src,src_len,arena);
    p.arena=arena;p.had_error=0;p.no_struct_lit=0;
    p.src_path=src_path?src_path:"<unknown>";
    p.cur=lexer_next(&p.lexer);
    p.peek=lexer_next(&p.lexer);
    return p;
}

AstNode *parse_program(Parser *p){
    AstNode *n=nd(p,NODE_PROGRAM);
    AstList *head=NULL,*tail=NULL;
    n->program.is_module=0;
    while(!chk(p,TOK_EOF)){
        AstNode *decl=parse_top_level(p);
        if(decl){
            if(decl->kind==NODE_EXPORT_MODULE){
                n->program.is_module=1;
            } else {
                head=lapp(p,head,&tail,decl);
            }
        }
    }
    n->program.decls=head;return n;
}
