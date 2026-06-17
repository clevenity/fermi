#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "opt.h"

static int is_int_const(const char *s) {
    if (!s || !*s) return 0;
    const char *p = s;
    if (*p == '-') p++;
    if (!*p) return 0;
    while (*p) { if (*p < '0' || *p > '9') return 0; p++; }
    return 1;
}
static int is_float_const(const char *s) {
    if (!s || !*s) return 0;
    char *e; strtod(s, &e);
    return *e == '\0' && e != s;
}
static int is_const_val(const char *s) {
    return is_int_const(s) || is_float_const(s);
}

#define MAX_DEFS 1024
typedef struct { const char *name; const char *val; const char *type; } Def;
static int def_count;
static Def defs[MAX_DEFS];

static void def_set(const char *name, const char *val, const char *type) {
    for (int i = 0; i < def_count; i++) {
        if (strcmp(defs[i].name, name)==0) {
            defs[i].val = val; defs[i].type = type; return;
        }
    }
    if (def_count < MAX_DEFS) {
        defs[def_count].name = name;
        defs[def_count].val  = val;
        defs[def_count].type = type;
        def_count++;
    }
}
static const char *def_get(const char *name) {
    for (int i = 0; i < def_count; i++)
        if (strcmp(defs[i].name, name)==0) return defs[i].val;
    return NULL;
}
static void def_kill(const char *name) {
    for (int i = 0; i < def_count; i++) {
        if (strcmp(defs[i].name, name)==0) {
            defs[i] = defs[--def_count]; return;
        }
    }
}

static int is_float_ty(const char *t) {
    return t && (strcmp(t,"float")==0||strcmp(t,"double")==0);
}

static int fold_block(FirBlock *b, Arena *arena) {
    int changed = 0;
    def_count = 0;
    for (FirInstr *i = b->head; i; i = i->next) {
        if (i->op == FIR_ALLOCA) continue;
        if (i->op == FIR_STORE && i->dst && i->src1 && is_const_val(i->src1)) {
            def_set(i->dst, i->src1, i->type); continue;
        }
        if (i->op == FIR_STORE && i->dst) { def_kill(i->dst); continue; }
        if (i->op == FIR_LOAD && i->src1) {
            const char *cv = def_get(i->src1);
            if (cv) {
                if (is_float_ty(i->type)) {
                    i->op = FIR_FADD; i->src2 = arena_strdup(arena, "0.0");
                } else {
                    i->op = FIR_ADD; i->src2 = arena_strdup(arena, "0");
                }
                i->src1 = arena_strdup(arena, cv); changed = 1;
            }
            continue;
        }
        if (!i->src1 || !i->src2) continue;
        const char *s1 = i->src1, *s2 = i->src2;
        int c1 = is_int_const(s1), c2 = is_int_const(s2);
        int fc1 = is_float_const(s1), fc2 = is_float_const(s2);
        int is_fp = is_float_ty(i->type);
        if (is_fp && fc1 && fc2) {
            double a = strtod(s1,NULL), bv = strtod(s2,NULL), r = 0;
            int ok = 1;
            switch(i->op){
            case FIR_FADD: r=a+bv; break; case FIR_FSUB: r=a-bv; break;
            case FIR_FMUL: r=a*bv; break; case FIR_FDIV: r=bv?a/bv:0; break;
            default: ok=0; break;
            }
            if (ok && i->dst) {
                char buf[64]; snprintf(buf,sizeof(buf),"%.17g",r);
                i->op=FIR_FADD; i->src1=arena_strdup(arena,buf);
                i->src2=arena_strdup(arena,"0.0"); changed=1; continue;
            }
        }
        if (!is_fp && c1 && c2) {
            long long a=atoll(s1), bv=atoll(s2), r=0; int ok=1;
            switch(i->op){
            case FIR_ADD: r=a+bv; break; case FIR_SUB: r=a-bv; break;
            case FIR_MUL: r=a*bv; break; case FIR_DIV: r=bv?a/bv:0; break;
            case FIR_MOD: r=bv?a%bv:0; break;
            case FIR_AND: r=a&bv; break; case FIR_OR: r=a|bv; break;
            case FIR_XOR: r=a^bv; break;
            case FIR_SHL: r=a<<bv; break; case FIR_SHR: r=a>>bv; break;
            default: ok=0; break;
            }
            if (ok && i->dst) {
                char buf[32]; snprintf(buf,sizeof(buf),"%lld",r);
                i->op=FIR_ADD; i->src1=arena_strdup(arena,buf);
                i->src2=arena_strdup(arena,"0"); changed=1; continue;
            }
        }
        
        if (i->op==FIR_ADD && c2 && atoll(s2)==0) continue;
        if (i->op==FIR_SUB && c2 && atoll(s2)==0) { i->op=FIR_ADD; i->src2=arena_strdup(arena,"0"); changed=1; continue; }
        if (i->op==FIR_MUL) {
            if (c2) {
                long long bv=atoll(s2);
                if(bv==1){i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue;}
                if(bv==0){i->op=FIR_ADD;i->src1=arena_strdup(arena,"0");i->src2=arena_strdup(arena,"0");changed=1;continue;}
                if(bv==2){i->op=FIR_ADD;i->src2=arena_strdup(arena,s1);changed=1;continue;}
            }
            if (c1) {
                long long av=atoll(s1);
                if(av==1){i->op=FIR_ADD;i->src1=arena_strdup(arena,s2);i->src2=arena_strdup(arena,"0");changed=1;continue;}
                if(av==0){i->op=FIR_ADD;i->src1=arena_strdup(arena,"0");i->src2=arena_strdup(arena,"0");changed=1;continue;}
            }
        }
        if (i->op==FIR_DIV && c2 && atoll(s2)==1) { i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue; }
        if (i->op==FIR_AND && c2 && atoll(s2)==-1) { i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue; }
        if (i->op==FIR_OR  && c2 && atoll(s2)==0 ) { i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue; }
        if (i->op==FIR_XOR && c2 && atoll(s2)==0 ) { i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue; }
        if (i->op==FIR_SHL && c2 && atoll(s2)==0 ) { i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue; }
        if (i->op==FIR_SHR && c2 && atoll(s2)==0 ) { i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;continue; }
        
        if (i->op==FIR_SUB && i->src1 && i->src2 && strcmp(i->src1,i->src2)==0 && i->src1[0]=='t') {
            i->op=FIR_ADD;i->src1=arena_strdup(arena,"0");i->src2=arena_strdup(arena,"0");changed=1;
        }
        
        if (i->op==FIR_XOR && i->src1 && i->src2 && strcmp(i->src1,i->src2)==0 && i->src1[0]=='t') {
            i->op=FIR_ADD;i->src1=arena_strdup(arena,"0");i->src2=arena_strdup(arena,"0");changed=1;
        }
        
        if ((i->op==FIR_AND||i->op==FIR_OR) && i->src1 && i->src2 && strcmp(i->src1,i->src2)==0 && i->src1[0]=='t') {
            i->op=FIR_ADD;i->src2=arena_strdup(arena,"0");changed=1;
        }
    }
    return changed;
}

#define MAX_LOADS 1024
typedef struct { const char *var; int loaded; } LoadInfo;
static LoadInfo loads[MAX_LOADS];
static int load_count;

static void dse_block(FirBlock *b) {
    load_count = 0;
    for (FirInstr *i = b->head; i; i = i->next) {
        if (i->op==FIR_LOAD && i->src1) {
            int found=0;
            for (int k=0;k<load_count;k++) {
                if (strcmp(loads[k].var,i->src1)==0) { loads[k].loaded=1;found=1;break; }
            }
            if(!found && load_count<MAX_LOADS) {
                loads[load_count].var=i->src1;loads[load_count].loaded=1;load_count++;
            }
        }
    }
    FirInstr *prev=NULL;
    for (FirInstr *i=b->head;i;) {
        FirInstr *next=i->next;
        if (i->op==FIR_STORE && i->dst) {
            int used=0;
            for (int k=0;k<load_count;k++)
                if(strcmp(loads[k].var,i->dst)==0&&loads[k].loaded){used=1;break;}
            if (!used) {
                if(prev)prev->next=next; else b->head=next;
                if(!next)b->tail=prev;
                i=next; continue;
            }
        }
        prev=i; i=next;
    }
}

static void remove_trailing_after_term(FirBlock *b) {
    FirInstr *prev=NULL; int saw_term=0;
    for (FirInstr *i=b->head;i;) {
        FirInstr *next=i->next;
        if (saw_term) {
            if(prev){ prev->next=NULL; } b->tail=prev; return;
        }
        if(i->op==FIR_BR||i->op==FIR_BR_COND||i->op==FIR_RET||i->op==FIR_RET_VOID||i->op==FIR_UNREACHABLE)
            saw_term=1;
        prev=i; i=next;
    }
}

static void mark_dead_blocks(FirFn *fn) {
    for (FirBlock *b=fn->blocks;b;b=b->next) b->dead=1;
    if (!fn->blocks) return;
    fn->blocks->dead=0;
    int changed=1;
    while (changed) {
        changed=0;
        for (FirBlock *b=fn->blocks;b;b=b->next) {
            if (b->dead) continue;
            FirInstr *last=b->tail;
            if(!last) continue;
            if(last->op==FIR_BR&&last->src1) {
                for(FirBlock *t=fn->blocks;t;t=t->next)
                    if(t->dead&&strcmp(t->label,last->src1)==0){t->dead=0;changed=1;}
            }else if(last->op==FIR_BR_COND&&last->src2&&last->src3) {
                for(FirBlock *t=fn->blocks;t;t=t->next)
                    if(t->dead&&(strcmp(t->label,last->src2)==0||strcmp(t->label,last->src3)==0))
                        {t->dead=0;changed=1;}
            }
        }
    }
}

static void peephole_block(FirBlock *b, Arena *arena) {
    for (FirInstr *i=b->head;i;i=i->next) {
        
        if ((i->op==FIR_ZEXT||i->op==FIR_SEXT) && i->src1 && is_int_const(i->src1)) {
            long long v=atoll(i->src1);
            char buf[32]; snprintf(buf,sizeof(buf),"%lld",v);
            i->op=FIR_ADD; i->src1=arena_strdup(arena,buf);
            i->src2=arena_strdup(arena,"0");
        }
        
        
        if (i->op==FIR_ICMP && i->src1 && i->src2 && strcmp(i->src1,i->src2)==0
            && i->src3 && strcmp(i->src3,"eq")==0 && i->src1[0]=='t') {
            i->op=FIR_ADD; i->src1=arena_strdup(arena,"1");
            i->src2=arena_strdup(arena,"0"); i->type=arena_strdup(arena,"i1");
        }
    }
}

static void simplify_br(FirFn *fn) {
    for (FirBlock *b=fn->blocks;b;b=b->next) {
        if (b->dead) continue;
        FirInstr *last=b->tail;
        if (!last || last->op!=FIR_BR_COND) continue;
        if (!last->src1 || !is_int_const(last->src1)) continue;
        long long cv=atoll(last->src1);
        last->op=FIR_BR;
        last->src1=cv?last->src2:last->src3;
        last->src2=NULL; last->src3=NULL;
    }
}

void mir_opt(FirModule *m, Arena *arena) {
    for (FirFn *fn=m->fns;fn;fn=fn->next) {
        for (FirBlock *b=fn->blocks;b;b=b->next)
            remove_trailing_after_term(b);
        mark_dead_blocks(fn);
        int changed=1;
        for (int iter=0;iter<16&&changed;iter++) {
            changed=0;
            for (FirBlock *b=fn->blocks;b;b=b->next) {
                if(b->dead) continue;
                changed |= fold_block(b,arena);
            }
        }
        for (FirBlock *b=fn->blocks;b;b=b->next) {
            if(!b->dead) {
                peephole_block(b,arena);
            }
        }
        simplify_br(fn);
        mark_dead_blocks(fn);
    }
}
