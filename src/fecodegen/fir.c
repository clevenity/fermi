#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fir.h"

static void pv(const char *v) {
    if (!v || !v[0]) { printf("undef"); return; }
    if (v[0]=='%'||v[0]=='@') { printf("%s",v); return; }
    
    char *end; strtoll(v,&end,0);
    if (end!=v && *end=='\0') { printf("%s",v); return; }
    if (!strcmp(v,"null")||!strcmp(v,"nullptr")||!strcmp(v,"undef")||
        !strcmp(v,"true")||!strcmp(v,"false")) { printf("%s",v); return; }
    
    strtod(v,&end);
    if (end!=v && *end=='\0') { printf("%s",v); return; }
    
    printf("%%%s",v);
}

static void pl(const char *l) {
    if (!l) return;
    if (l[0]=='%') printf("%s",l); else printf("%%%s",l);
}

static void fir_print_instr(FirInstr *i) {
    printf("    ");
    switch (i->op) {

    
    case FIR_ALLOCA:
        printf("%%%s = alloca %s", i->dst, i->type ? i->type : "i8");
        break;
    case FIR_STORE:
        
        printf("store %s ", i->type ? i->type : "i32");
        pv(i->src1);
        printf(", ptr ");
        pv(i->dst);
        break;
    case FIR_LOAD:
        
        printf("%%%s = load %s, ptr ", i->dst, i->type ? i->type : "i32");
        pv(i->src1);
        break;

    
    case FIR_ADD:  case FIR_SUB:  case FIR_MUL:
    case FIR_DIV:  case FIR_MOD:  case FIR_UDIV: case FIR_UREM:
    case FIR_AND:  case FIR_OR:   case FIR_XOR:
    case FIR_SHL:  case FIR_SHR:  case FIR_LSHR:
    case FIR_FADD: case FIR_FSUB: case FIR_FMUL: case FIR_FDIV: {
        static const char *nm[] = {
            [FIR_ADD]="add",[FIR_SUB]="sub",[FIR_MUL]="mul",
            [FIR_DIV]="sdiv",[FIR_MOD]="srem",[FIR_UDIV]="udiv",[FIR_UREM]="urem",
            [FIR_AND]="and",[FIR_OR]="or",[FIR_XOR]="xor",
            [FIR_SHL]="shl",[FIR_SHR]="ashr",[FIR_LSHR]="lshr",
            [FIR_FADD]="fadd",[FIR_FSUB]="fsub",[FIR_FMUL]="fmul",[FIR_FDIV]="fdiv",
        };
        printf("%%%s = %s %s ", i->dst, nm[i->op], i->type ? i->type : "i32");
        pv(i->src1); printf(", "); pv(i->src2);
        break;
    }
    case FIR_NEG:
        printf("%%%s = neg %s ", i->dst, i->type ? i->type : "i32");
        pv(i->src1);
        break;
    case FIR_FNEG:
        printf("%%%s = fneg %s ", i->dst, i->type ? i->type : "double");
        pv(i->src1);
        break;
    case FIR_NOT:
        printf("%%%s = not %s ", i->dst, i->type ? i->type : "i1");
        pv(i->src1);
        break;
    case FIR_BITNOT:
        printf("%%%s = xor %s ", i->dst, i->type ? i->type : "i32");
        pv(i->src1); printf(", -1");
        break;

    
    case FIR_ICMP:
        
        printf("%%%s = icmp %s %s ", i->dst,
               i->src3 ? i->src3 : "eq",
               i->type ? i->type : "i32");
        pv(i->src1); printf(", "); pv(i->src2);
        break;
    case FIR_FCMP:
        
        printf("%%%s = fcmp %s %s ", i->dst,
               i->src3 ? i->src3 : "oeq",
               i->type ? i->type : "double");
        pv(i->src1); printf(", "); pv(i->src2);
        break;

    
    case FIR_SEXT: case FIR_ZEXT: case FIR_TRUNC:
    case FIR_FPEXT: case FIR_FPTRUNC:
    case FIR_SITOFP: case FIR_UITOFP: case FIR_FPTOSI: case FIR_FPTOUI:
    case FIR_PTRTOINT: case FIR_INTTOPTR: case FIR_BITCAST: {
        static const char *cn[] = {
            [FIR_SEXT]="sext",[FIR_ZEXT]="zext",[FIR_TRUNC]="trunc",
            [FIR_FPEXT]="fpext",[FIR_FPTRUNC]="fptrunc",
            [FIR_SITOFP]="sitofp",[FIR_UITOFP]="uitofp",
            [FIR_FPTOSI]="fptosi",[FIR_FPTOUI]="fptoui",
            [FIR_PTRTOINT]="ptrtoint",[FIR_INTTOPTR]="inttoptr",
            [FIR_BITCAST]="bitcast",
        };
        
        printf("%%%s = %s %s ", i->dst, cn[i->op],
               i->src2 ? i->src2 : (i->type ? i->type : "i32"));
        pv(i->src1);
        printf(" to %s", i->type ? i->type : "i64");
        break;
    }

    
    case FIR_BR:
        printf("br label "); pl(i->src1);
        break;
    case FIR_BR_COND:
        printf("br i1 "); pv(i->src1);
        printf(", label "); pl(i->src2);
        printf(", label "); pl(i->src3);
        break;
    case FIR_RET:
        printf("ret %s ", i->type ? i->type : "i32");
        pv(i->src1);
        break;
    case FIR_RET_VOID:
        printf("ret void");
        break;
    case FIR_UNREACHABLE:
        printf("unreachable");
        break;

    
    case FIR_CALL:
    case FIR_CALL_INDIRECT: {
        int has_dst = i->dst && i->dst[0];
        int is_void = !i->type || !strcmp(i->type,"void");
        if (has_dst && !is_void) printf("%%%s = ", i->dst);
        printf("call %s ", i->type ? i->type : "void");
        
        const char *fn = i->src1 ? i->src1 : "???";
        if (fn[0]!='@' && fn[0]!='%') printf("@%s", fn);
        else printf("%s", fn);
        printf("(");
        for (int j = 0; j < i->nargs; j++) {
            if (j) printf(", ");
            const char *aty = (i->arg_types && i->arg_types[j]) ? i->arg_types[j] : "i32";
            printf("%s ", aty);
            pv(i->args ? i->args[j] : "undef");
        }
        printf(")");
        break;
    }

    
    case FIR_SELECT:
        printf("%%%s = select i1 ", i->dst);
        pv(i->src1);
        printf(", %s ", i->type ? i->type : "i32");
        pv(i->src2);
        printf(", %s ", i->type ? i->type : "i32");
        pv(i->src3);
        break;
    case FIR_PHI:
        printf("%%%s = phi %s", i->dst, i->type ? i->type : "i32");
        for (int j = 0; j+1 < i->nargs; j += 2) {
            printf(" [ ");
            pv(i->args ? i->args[j] : "undef");
            printf(", "); pl(i->args ? i->args[j+1] : "??");
            printf(" ]");
            if (j+2 < i->nargs) printf(",");
        }
        break;

    
    case FIR_MEMCPY:
        printf("call void @memcpy(ptr "); pv(i->dst);
        printf(", ptr "); pv(i->src1);
        printf(", i64 "); pv(i->src2);
        printf(")");
        break;
    case FIR_MEMSET_I:
        printf("call void @memset(ptr "); pv(i->dst);
        printf(", i32 "); pv(i->src1);
        printf(", i64 "); pv(i->src2);
        printf(")");
        break;

    
    case FIR_GEP:
        printf("%%%s = getelementptr %s, ptr ", i->dst, i->type ? i->type : "i8");
        pv(i->src1); printf(", i64 "); pv(i->src2);
        break;
    case FIR_GEP_STRUCT:
        printf("%%%s = getelementptr inbounds %%struct.%s, ptr ",
               i->dst, i->type ? i->type : "?");
        pv(i->src1);
        printf(", i32 0, i32 %s", i->src3 ? i->src3 : "0");
        break;
    case FIR_PTR_ADD:
        printf("%%%s = getelementptr i8, ptr ", i->dst);
        pv(i->src1); printf(", i64 "); pv(i->src2);
        break;

    
    case FIR_CONST_STR:
        printf("%%%s = const_str \"%s\"", i->dst, i->src1 ? i->src1 : "");
        break;

    
    case FIR_NOP: printf("; nop"); break;
    case FIR_LABEL: printf("; label %s", i->src1 ? i->src1 : ""); break;
    default:
        
        if (i->dst && i->dst[0]) printf("%%%s = ", i->dst);
        printf("??? op=%d", (int)i->op);
        if (i->type)  printf(" type=%s", i->type);
        if (i->src1)  printf(" src1=%s", i->src1);
        if (i->src2)  printf(", src2=%s", i->src2);
        break;
    }
    printf("\n");
}

void fir_print_module(FirModule *m) {
    
    for (FirStructDecl *sd = m->struct_decls; sd; sd = sd->next) {
        printf("%%struct.%s = type { ", sd->name);
        for (int k = 0; k < sd->nfields; k++) {
            if (k) printf(", ");
            printf("%s", sd->field_types[k]);
        }
        printf(" }\n");
    }
    if (m->struct_decls) printf("\n");

    
    for (FirGlobal *g = m->globals; g; g = g->next) {
        if (g->is_const)
            printf("@%s = private constant %s %s\n",
                   g->name, g->type ? g->type : "i8", g->init ? g->init : "zeroinitializer");
        else
            printf("@%s = global %s %s\n",
                   g->name, g->type ? g->type : "i8", g->init ? g->init : "zeroinitializer");
    }
    if (m->globals) printf("\n");

    
    for (FirFn *fn = m->fns; fn; fn = fn->next) {
        if (fn->is_extern) {
            printf("external fn @%s(", fn->name);
            int first = 1;
            for (FirParam *p = fn->params; p; p = p->next) {
                if (!first) printf(", "); first = 0;
                printf("%s", p->type);
            }
            if (fn->is_vararg) { if (!first) printf(", "); printf("..."); }
            printf("): %s\n", fn->ret_type ? fn->ret_type : "void");
            continue;
        }
        printf("fn @%s(", fn->name);
        int first = 1;
        for (FirParam *p = fn->params; p; p = p->next) {
            if (!first) printf(", "); first = 0;
            printf("%s %%%s", p->type, p->name);
        }
        printf("): %s {\n", fn->ret_type ? fn->ret_type : "void");

        for (FirBlock *b = fn->blocks; b; b = b->next) {
            if (b->dead) continue;
            printf("  %s:\n", b->label);
            for (FirInstr *i = b->head; i; i = i->next)
                fir_print_instr(i);
        }
        printf("}\n\n");
    }
}
