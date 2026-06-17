#if defined(__linux__)
#define _GNU_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include "llvm_emit.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define VM_CAP  2048
#define BM_CAP  512
#define FM_CAP  512
#define TM_CAP  128
#define SM_CAP  512
#define PH_CAP  256
#define PH_PAIR 64

typedef struct { const char *k; LLVMValueRef     v; } KV;
typedef struct { const char *k; LLVMBasicBlockRef v; } KB;
typedef struct { const char *k; LLVMTypeRef      v; } KT;

typedef struct {
    LLVMValueRef     phi_val;
    LLVMTypeRef      phi_ty;
    const char      *vals[PH_PAIR];
    const char      *lbls[PH_PAIR];
    int              npairs;
} DPhi;

typedef struct {
    LLVMContextRef ctx;
    LLVMModuleRef  mod;
    LLVMBuilderRef bld;
    FirModule     *fm;
    Arena         *arena;
    KV   fnmap[FM_CAP]; int nfn;
    KT   tymap[TM_CAP]; int nty;
    KV   strmap[SM_CAP]; int nstr;
    KV   vmap[VM_CAP];  int nvm;
    KB   bmap[BM_CAP];  int nbm;
    DPhi phis[PH_CAP];  int nphi;
} Cx;

static LLVMTypeRef rty(Cx *cx, const char *s);
static LLVMValueRef rval(Cx *cx, const char *s, LLVMTypeRef ty);

static LLVMTypeRef rty(Cx *cx, const char *s) {
    if(!s||!s[0]) return LLVMInt32TypeInContext(cx->ctx);
    if(!strcmp(s,"i1"))     return LLVMInt1TypeInContext(cx->ctx);
    if(!strcmp(s,"i8"))     return LLVMInt8TypeInContext(cx->ctx);
    if(!strcmp(s,"i16"))    return LLVMInt16TypeInContext(cx->ctx);
    if(!strcmp(s,"i32"))    return LLVMInt32TypeInContext(cx->ctx);
    if(!strcmp(s,"i64"))    return LLVMInt64TypeInContext(cx->ctx);
    if(!strcmp(s,"i128"))   return LLVMInt128TypeInContext(cx->ctx);
    if(!strcmp(s,"float"))  return LLVMFloatTypeInContext(cx->ctx);
    if(!strcmp(s,"double")) return LLVMDoubleTypeInContext(cx->ctx);
    if(!strcmp(s,"void"))   return LLVMVoidTypeInContext(cx->ctx);
    if(!strcmp(s,"ptr"))    return LLVMPointerTypeInContext(cx->ctx,0);
    const char *nm=s;
    if(strncmp(nm,"%struct.",8)==0) nm+=8;
    else if(strncmp(nm,"struct.",7)==0) nm+=7;
    if(nm!=s){
        for(int i=0;i<cx->nty;i++)
            if(strcmp(cx->tymap[i].k,nm)==0) return cx->tymap[i].v;
        return LLVMPointerTypeInContext(cx->ctx,0);
    }
    if(s[0]=='['){
        long n=strtol(s+1,NULL,10);
        const char *xp=strchr(s,'x');
        if(xp){
            xp++; while(*xp==' ')xp++;
            char buf[128];
            int len=(int)strlen(xp);
            while(len>0&&(xp[len-1]==']'||xp[len-1]==' '))len--;
            snprintf(buf,sizeof(buf),"%.*s",len,xp);
            LLVMTypeRef elem=rty(cx,buf);
            return LLVMArrayType2(elem,(uint64_t)n);
        }
    }
    return LLVMPointerTypeInContext(cx->ctx,0);
}

static void vset(Cx *cx, const char *k, LLVMValueRef v) {
    for(int i=0;i<cx->nvm;i++)
        if(strcmp(cx->vmap[i].k,k)==0){cx->vmap[i].v=v;return;}
    if(cx->nvm<VM_CAP){cx->vmap[cx->nvm].k=k;cx->vmap[cx->nvm].v=v;cx->nvm++;}
}

static LLVMValueRef coerce_v(Cx *cx, LLVMValueRef v, LLVMTypeRef want) {
    if(!v||!want||!cx->bld) return v;
    LLVMTypeRef got=LLVMTypeOf(v);
    if(got==want) return v;
    LLVMTypeKind gk=LLVMGetTypeKind(got);
    LLVMTypeKind wk=LLVMGetTypeKind(want);
    LLVMBuilderRef B=cx->bld;
    LLVMContextRef C=cx->ctx;
    if(gk==LLVMIntegerTypeKind&&wk==LLVMPointerTypeKind)
        return LLVMBuildIntToPtr(B,v,want,"");
    if(gk==LLVMPointerTypeKind&&wk==LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(B,v,want,"");
    if(gk==LLVMIntegerTypeKind&&wk==LLVMIntegerTypeKind){
        unsigned gb=LLVMGetIntTypeWidth(got),wb=LLVMGetIntTypeWidth(want);
        if(gb>wb) return LLVMBuildTrunc(B,v,want,"");
        if(gb<wb) return LLVMBuildSExt(B,v,want,"");
        return v;
    }
    if(gk==LLVMFloatTypeKind&&wk==LLVMDoubleTypeKind)
        return LLVMBuildFPExt(B,v,want,"");
    if(gk==LLVMDoubleTypeKind&&wk==LLVMFloatTypeKind)
        return LLVMBuildFPTrunc(B,v,want,"");
    if(gk==LLVMIntegerTypeKind&&(wk==LLVMFloatTypeKind||wk==LLVMDoubleTypeKind))
        return LLVMBuildSIToFP(B,v,want,"");
    if((gk==LLVMFloatTypeKind||gk==LLVMDoubleTypeKind)&&wk==LLVMIntegerTypeKind)
        return LLVMBuildFPToSI(B,v,want,"");
    (void)C;
    return v;
}

static LLVMValueRef rval(Cx *cx, const char *s, LLVMTypeRef ty) {
    if(!s||!s[0]){
        if(!ty)ty=LLVMInt32TypeInContext(cx->ctx);
        return LLVMConstNull(ty);
    }
    if(s[0]=='%'){
        const char *nm=s+1;
        for(int i=0;i<cx->nvm;i++)
            if(strcmp(cx->vmap[i].k,nm)==0)
                return ty?coerce_v(cx,cx->vmap[i].v,ty):cx->vmap[i].v;
        if(!ty)ty=LLVMInt32TypeInContext(cx->ctx);
        return LLVMGetUndef(ty);
    }
    if(s[0]=='@'){
        const char *nm=s+1;
        for(int i=0;i<cx->nfn;i++)
            if(strcmp(cx->fnmap[i].k,nm)==0)return cx->fnmap[i].v;
        LLVMValueRef gv=LLVMGetNamedGlobal(cx->mod,nm);
        if(!gv) gv=LLVMGetNamedFunction(cx->mod,nm);
        if(gv) return gv;
        if(!ty)ty=LLVMPointerTypeInContext(cx->ctx,0);
        return LLVMGetUndef(ty);
    }
    if(strncmp(s,"%%arg.",6)==0){
        const char *nm=s+2;
        for(int i=0;i<cx->nvm;i++)
            if(strcmp(cx->vmap[i].k,nm)==0)
                return ty?coerce_v(cx,cx->vmap[i].v,ty):cx->vmap[i].v;
        if(!ty)ty=LLVMPointerTypeInContext(cx->ctx,0);
        return LLVMGetUndef(ty);
    }
    if(!strcmp(s,"null")||!strcmp(s,"nullptr")){
        if(!ty)ty=LLVMPointerTypeInContext(cx->ctx,0);
        return LLVMConstNull(ty);
    }
    if(!strcmp(s,"undef")){
        if(!ty)ty=LLVMInt32TypeInContext(cx->ctx);
        return LLVMGetUndef(ty);
    }
    if(!strcmp(s,"true"))  return LLVMConstInt(LLVMInt1TypeInContext(cx->ctx),1,0);
    if(!strcmp(s,"false")) return LLVMConstInt(LLVMInt1TypeInContext(cx->ctx),0,0);
    for(int i=0;i<cx->nvm;i++)
        if(strcmp(cx->vmap[i].k,s)==0)
            return ty?coerce_v(cx,cx->vmap[i].v,ty):cx->vmap[i].v;
    if(!ty)ty=LLVMInt32TypeInContext(cx->ctx);
    LLVMTypeKind kind=LLVMGetTypeKind(ty);
    if(kind==LLVMFloatTypeKind||kind==LLVMDoubleTypeKind)
        return LLVMConstReal(ty,atof(s));
    char *end; long long iv=strtoll(s,&end,0);
    if(*end=='\0')
        return LLVMConstInt(ty,(unsigned long long)iv,iv<0?1:0);
    return LLVMGetUndef(ty);
}

static LLVMBasicBlockRef bget(Cx *cx, const char *label) {
    const char *lbl=label;
    if(lbl&&lbl[0]=='%')lbl++;
    for(int i=0;i<cx->nbm;i++)
        if(cx->bmap[i].k&&strcmp(cx->bmap[i].k,lbl)==0)return cx->bmap[i].v;
    return NULL;
}

static LLVMIntPredicate icmp_pred(const char *s) {
    if(!s)return LLVMIntEQ;
    if(!strcmp(s,"eq"))  return LLVMIntEQ;
    if(!strcmp(s,"ne"))  return LLVMIntNE;
    if(!strcmp(s,"slt")) return LLVMIntSLT;
    if(!strcmp(s,"sle")) return LLVMIntSLE;
    if(!strcmp(s,"sgt")) return LLVMIntSGT;
    if(!strcmp(s,"sge")) return LLVMIntSGE;
    if(!strcmp(s,"ult")) return LLVMIntULT;
    if(!strcmp(s,"ule")) return LLVMIntULE;
    if(!strcmp(s,"ugt")) return LLVMIntUGT;
    if(!strcmp(s,"uge")) return LLVMIntUGE;
    return LLVMIntEQ;
}

static LLVMRealPredicate fcmp_pred(const char *s) {
    if(!s)return LLVMRealOEQ;
    if(!strcmp(s,"oeq"))return LLVMRealOEQ;
    if(!strcmp(s,"one"))return LLVMRealONE;
    if(!strcmp(s,"olt"))return LLVMRealOLT;
    if(!strcmp(s,"ole"))return LLVMRealOLE;
    if(!strcmp(s,"ogt"))return LLVMRealOGT;
    if(!strcmp(s,"oge"))return LLVMRealOGE;
    if(!strcmp(s,"ord"))return LLVMRealORD;
    if(!strcmp(s,"uno"))return LLVMRealUNO;
    if(!strcmp(s,"ueq"))return LLVMRealUEQ;
    if(!strcmp(s,"une"))return LLVMRealUNE;
    if(!strcmp(s,"ult"))return LLVMRealULT;
    if(!strcmp(s,"ule"))return LLVMRealULE;
    if(!strcmp(s,"ugt"))return LLVMRealUGT;
    if(!strcmp(s,"uge"))return LLVMRealUGE;
    return LLVMRealOEQ;
}

static LLVMTypeRef build_fn_type(Cx *cx, FirFn *fn) {
    LLVMTypeRef ret=fn->ret_type?rty(cx,fn->ret_type):LLVMVoidTypeInContext(cx->ctx);
    LLVMTypeRef params[256]; int np=0;
    for(FirParam *p=fn->params;p&&np<256;p=p->next)
        params[np++]=rty(cx,p->type);
    return LLVMFunctionType(ret,params,(unsigned)np,fn->is_vararg?1:0);
}

static LLVMValueRef get_or_decl_fn(Cx *cx, const char *name, LLVMTypeRef fty) {
    for(int i=0;i<cx->nfn;i++)
        if(strcmp(cx->fnmap[i].k,name)==0)return cx->fnmap[i].v;
    LLVMValueRef fv=LLVMGetNamedFunction(cx->mod,name);
    if(!fv)fv=LLVMAddFunction(cx->mod,name,fty);
    if(cx->nfn<FM_CAP){cx->fnmap[cx->nfn].k=name;cx->fnmap[cx->nfn].v=fv;cx->nfn++;}
    return fv;
}

static LLVMValueRef intern_str(Cx *cx, const char *content) {
    if(!content)content="";
    for(int i=0;i<cx->nstr;i++)
        if(strcmp(cx->strmap[i].k,content)==0)return cx->strmap[i].v;
    size_t len=strlen(content);
    LLVMTypeRef i8=LLVMInt8TypeInContext(cx->ctx);
    LLVMTypeRef arr_ty=LLVMArrayType2(i8,len+1);
    char gname[64]; snprintf(gname,sizeof(gname),".str.%d",cx->nstr);
    LLVMValueRef gv=LLVMAddGlobal(cx->mod,arr_ty,gname);
    LLVMSetLinkage(gv,LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv,1);
    LLVMSetUnnamedAddress(gv,LLVMGlobalUnnamedAddr);
    LLVMValueRef *chars=malloc((len+1)*sizeof(LLVMValueRef));
    for(size_t j=0;j<len;j++)
        chars[j]=LLVMConstInt(i8,(unsigned char)content[j],0);
    chars[len]=LLVMConstInt(i8,0,0);
    LLVMSetInitializer(gv,LLVMConstArray2(i8,chars,len+1));
    free(chars);
    LLVMValueRef zero=LLVMConstInt(LLVMInt64TypeInContext(cx->ctx),0,0);
    LLVMValueRef idxs[2]={zero,zero};
    LLVMValueRef ptr=LLVMConstGEP2(arr_ty,gv,idxs,2);
    if(cx->nstr<SM_CAP){cx->strmap[cx->nstr].k=content;cx->strmap[cx->nstr].v=ptr;cx->nstr++;}
    return ptr;
}

static void emit_instr(Cx *cx, FirInstr *ins) {
    LLVMBuilderRef B=cx->bld;
    LLVMContextRef C=cx->ctx;
    LLVMTypeRef i8 =LLVMInt8TypeInContext(C);
    LLVMTypeRef i32=LLVMInt32TypeInContext(C);
    LLVMTypeRef i64=LLVMInt64TypeInContext(C);
    LLVMTypeRef ptr=LLVMPointerTypeInContext(C,0);

    switch(ins->op){
    case FIR_NOP: case FIR_LABEL: break;

    case FIR_ALLOCA:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildAlloca(B,ty,ins->dst?ins->dst:"");
        LLVMSetAlignment(v,8);
        if(ins->dst)vset(cx,ins->dst,v);
        break;
    }
    case FIR_STORE:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef val=rval(cx,ins->src1,ty);
        LLVMValueRef dst=rval(cx,ins->dst,ptr);
        LLVMValueRef si=LLVMBuildStore(B,val,dst);
        LLVMSetAlignment(si,8);
        break;
    }
    case FIR_LOAD:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef src=rval(cx,ins->src1,ptr);
        LLVMValueRef v=LLVMBuildLoad2(B,ty,src,ins->dst?ins->dst:"");
        LLVMSetAlignment(v,8);
        if(ins->dst)vset(cx,ins->dst,v);
        break;
    }

#define IBINOP(OP,FN) case OP:{ \
    LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32; \
    LLVMValueRef v=FN(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),""); \
    if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);} \
    break;}
#define FBINOP(OP,FN) case OP:{ \
    LLVMTypeRef ty=ins->type?rty(cx,ins->type):LLVMDoubleTypeInContext(C); \
    LLVMValueRef v=FN(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),""); \
    if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);} \
    break;}

    IBINOP(FIR_ADD,LLVMBuildAdd)
    IBINOP(FIR_SUB,LLVMBuildSub)
    IBINOP(FIR_MUL,LLVMBuildMul)
    IBINOP(FIR_AND,LLVMBuildAnd)
    IBINOP(FIR_OR, LLVMBuildOr)
    IBINOP(FIR_XOR,LLVMBuildXor)
    IBINOP(FIR_SHL,LLVMBuildShl)
    FBINOP(FIR_FADD,LLVMBuildFAdd)
    FBINOP(FIR_FSUB,LLVMBuildFSub)
    FBINOP(FIR_FMUL,LLVMBuildFMul)
    FBINOP(FIR_FDIV,LLVMBuildFDiv)

    case FIR_DIV:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildSDiv(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_MOD:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildSRem(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_UDIV:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildUDiv(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_UREM:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildURem(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_SHR:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildAShr(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_LSHR:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildLShr(B,rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_NEG:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildNeg(B,rval(cx,ins->src1,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_FNEG:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):LLVMDoubleTypeInContext(C);
        LLVMValueRef v=LLVMBuildFNeg(B,rval(cx,ins->src1,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_NOT:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):LLVMInt1TypeInContext(C);
        LLVMValueRef v=LLVMBuildICmp(B,LLVMIntEQ,rval(cx,ins->src1,ty),LLVMConstInt(ty,0,0),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_BITNOT:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildNot(B,rval(cx,ins->src1,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}

    case FIR_ICMP:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildICmp(B,icmp_pred(ins->src3),
            rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_FCMP:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):LLVMDoubleTypeInContext(C);
        LLVMValueRef v=LLVMBuildFCmp(B,fcmp_pred(ins->src3),
            rval(cx,ins->src1,ty),rval(cx,ins->src2,ty),"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}

    case FIR_BR:{
        LLVMBasicBlockRef dest=bget(cx,ins->src1);
        if(dest)LLVMBuildBr(B,dest); else LLVMBuildUnreachable(B);
        break;}
    case FIR_BR_COND:{
        LLVMValueRef cond=rval(cx,ins->src1,LLVMInt1TypeInContext(C));
        LLVMBasicBlockRef bb_t=bget(cx,ins->src2);
        LLVMBasicBlockRef bb_f=bget(cx,ins->src3);
        if(bb_t&&bb_f)LLVMBuildCondBr(B,cond,bb_t,bb_f);
        else if(bb_t) LLVMBuildBr(B,bb_t);
        else          LLVMBuildUnreachable(B);
        break;}
    case FIR_RET:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMBuildRet(B,rval(cx,ins->src1,ty));
        break;}
    case FIR_RET_VOID:
        LLVMBuildRetVoid(B); break;
    case FIR_UNREACHABLE:
        LLVMBuildUnreachable(B); break;

#define CAST1(OP,FN,DFROM,DTO) case OP:{ \
    LLVMTypeRef from=ins->src2?rty(cx,ins->src2):(DFROM); \
    LLVMTypeRef to  =ins->type?rty(cx,ins->type):(DTO); \
    LLVMValueRef v=FN(B,rval(cx,ins->src1,from),to,""); \
    if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);} \
    break;}
    CAST1(FIR_ZEXT,   LLVMBuildZExt,    i32,i64)
    CAST1(FIR_SEXT,   LLVMBuildSExt,    i32,i64)
    CAST1(FIR_TRUNC,  LLVMBuildTrunc,   i64,i32)
    CAST1(FIR_FPEXT,  LLVMBuildFPExt,   LLVMFloatTypeInContext(C),LLVMDoubleTypeInContext(C))
    CAST1(FIR_FPTRUNC,LLVMBuildFPTrunc, LLVMDoubleTypeInContext(C),LLVMFloatTypeInContext(C))
    CAST1(FIR_SITOFP, LLVMBuildSIToFP,  i32,LLVMDoubleTypeInContext(C))
    CAST1(FIR_UITOFP, LLVMBuildUIToFP,  i32,LLVMDoubleTypeInContext(C))
    CAST1(FIR_FPTOSI, LLVMBuildFPToSI,  LLVMDoubleTypeInContext(C),i32)
    CAST1(FIR_FPTOUI, LLVMBuildFPToUI,  LLVMDoubleTypeInContext(C),i32)

    case FIR_PTRTOINT:{
        LLVMTypeRef to=ins->type?rty(cx,ins->type):i64;
        LLVMValueRef v=LLVMBuildPtrToInt(B,rval(cx,ins->src1,ptr),to,"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_INTTOPTR:{
        LLVMTypeRef from=ins->src2?rty(cx,ins->src2):i64;
        LLVMValueRef v=LLVMBuildIntToPtr(B,rval(cx,ins->src1,from),ptr,"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_BITCAST:{
        LLVMTypeRef to=ins->type?rty(cx,ins->type):ptr;
        LLVMValueRef src=rval(cx,ins->src1,to);
        LLVMTypeKind sk=LLVMGetTypeKind(LLVMTypeOf(src));
        LLVMTypeKind dk=LLVMGetTypeKind(to);
        LLVMValueRef v;
        if(sk==LLVMPointerTypeKind&&dk==LLVMPointerTypeKind) v=src;
        else v=LLVMBuildBitCast(B,src,to,"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}

    case FIR_GEP:{
        LLVMTypeRef et=ins->type?rty(cx,ins->type):i8;
        LLVMValueRef base=rval(cx,ins->src1,ptr);
        LLVMValueRef idx =rval(cx,ins->src2,i64);
        LLVMValueRef idxs[1]={idx};
        LLVMValueRef v=LLVMBuildGEP2(B,et,base,idxs,1,"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_GEP_STRUCT:{
        const char *sname=ins->type?ins->type:"";
        LLVMTypeRef sty=NULL;
        for(int k=0;k<cx->nty;k++)
            if(strcmp(cx->tymap[k].k,sname)==0){sty=cx->tymap[k].v;break;}
        if(!sty)sty=LLVMPointerTypeInContext(C,0);
        LLVMValueRef base=rval(cx,ins->src1,ptr);
        unsigned fidx=ins->src3?(unsigned)atoi(ins->src3):0;
        LLVMValueRef z=LLVMConstInt(i32,0,0);
        LLVMValueRef fi=LLVMConstInt(i32,fidx,0);
        LLVMValueRef v;
        if(LLVMGetTypeKind(sty)==LLVMStructTypeKind){
            LLVMValueRef idxs[2]={z,fi};
            v=LLVMBuildGEP2(B,sty,base,idxs,2,"");
        } else {
            v=base;
        }
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}
    case FIR_PTR_ADD:{
        LLVMValueRef base=rval(cx,ins->src1,ptr);
        LLVMValueRef off =rval(cx,ins->src2,i64);
        LLVMValueRef idxs[1]={off};
        LLVMValueRef v=LLVMBuildGEP2(B,i8,base,idxs,1,"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}

    case FIR_CONST_STR:{
        LLVMValueRef v=intern_str(cx,ins->src1?ins->src1:"");
        if(ins->dst)vset(cx,ins->dst,v);
        break;}

    case FIR_SELECT:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef cond=rval(cx,ins->src1,LLVMInt1TypeInContext(C));
        LLVMValueRef tv  =rval(cx,ins->src2,ty);
        LLVMValueRef fv  =rval(cx,ins->src3,ty);
        LLVMValueRef v=LLVMBuildSelect(B,cond,tv,fv,"");
        if(ins->dst){LLVMSetValueName2(v,ins->dst,strlen(ins->dst));vset(cx,ins->dst,v);}
        break;}

    case FIR_PHI:{
        LLVMTypeRef ty=ins->type?rty(cx,ins->type):i32;
        LLVMValueRef v=LLVMBuildPhi(B,ty,ins->dst?ins->dst:"");
        if(ins->dst)vset(cx,ins->dst,v);
        if(cx->nphi<PH_CAP){
            DPhi *dp=&cx->phis[cx->nphi++];
            dp->phi_val=v; dp->phi_ty=ty; dp->npairs=0;
            int pairs=ins->nargs/2;
            if(pairs>PH_PAIR)pairs=PH_PAIR;
            for(int j=0;j<pairs;j++){
                dp->vals[j]=ins->args?ins->args[j*2]:NULL;
                dp->lbls[j]=ins->args?ins->args[j*2+1]:NULL;
                dp->npairs++;
            }
        }
        break;}

    case FIR_MEMCPY:{
        LLVMValueRef d=rval(cx,ins->dst,ptr);
        LLVMValueRef s=rval(cx,ins->src1,ptr);
        LLVMValueRef n=rval(cx,ins->src2,i64);
        LLVMBuildMemCpy(B,d,1,s,1,n);
        break;}
    case FIR_MEMSET_I:{
        LLVMValueRef d=rval(cx,ins->dst,ptr);
        LLVMValueRef val=rval(cx,ins->src1,i8);
        LLVMValueRef n=rval(cx,ins->src2,i64);
        LLVMBuildMemSet(B,d,val,n,1);
        break;}

    case FIR_CALL:
    case FIR_CALL_INDIRECT:{
        LLVMTypeRef ret=ins->type?rty(cx,ins->type):LLVMVoidTypeInContext(C);
        LLVMTypeRef arg_tys[256]; int na=ins->nargs<256?ins->nargs:256;
        for(int j=0;j<na;j++)
            arg_tys[j]=(ins->arg_types&&ins->arg_types[j])?
                rty(cx,ins->arg_types[j]):i32;
        int is_vararg=0;
        if(ins->src1){
            for(FirFn *fn2=cx->fm->fns;fn2;fn2=fn2->next)
                if(strcmp(fn2->name,ins->src1)==0){is_vararg=fn2->is_vararg;break;}
        }
        LLVMTypeRef fty=LLVMFunctionType(ret,arg_tys,(unsigned)na,is_vararg);
        LLVMValueRef fval=NULL;
        if(ins->op==FIR_CALL&&ins->src1){
            fval=get_or_decl_fn(cx,ins->src1,fty);
        } else if(ins->src1){
            fval=rval(cx,ins->src1,ptr);
        }
        if(!fval)break;
        LLVMValueRef args[256];
        for(int j=0;j<na;j++)
            args[j]=rval(cx,ins->args?ins->args[j]:NULL,arg_tys[j]);
        int is_void=!ins->type||!strcmp(ins->type,"void");
        int has_dst=ins->dst&&ins->dst[0];
        const char *rname=(has_dst&&!is_void)?ins->dst:"";
        LLVMValueRef v=LLVMBuildCall2(B,fty,fval,args,(unsigned)na,rname);
        if(ins->is_tail)LLVMSetTailCall(v,1);
        if(has_dst&&!is_void)vset(cx,ins->dst,v);
        break;}

    default: break;
    }
}

static void emit_fn_body(Cx *cx, FirFn *fn, LLVMValueRef fn_val) {
    cx->nvm=0; cx->nbm=0; cx->nphi=0;

    for(FirBlock *b=fn->blocks;b;b=b->next){
        if(b->dead)continue;
        const char *lbl=b->label;
        if(!lbl||!lbl[0])lbl="blk";
        LLVMBasicBlockRef bb=LLVMAppendBasicBlockInContext(cx->ctx,fn_val,lbl);
        if(cx->nbm<BM_CAP){
            cx->bmap[cx->nbm].k=lbl;
            cx->bmap[cx->nbm].v=bb;
            cx->nbm++;
        }
    }

    unsigned pi=0;
    for(FirParam *par=fn->params;par;par=par->next,pi++){
        LLVMValueRef pv=LLVMGetParam(fn_val,pi);
        char pname[256];
        snprintf(pname,sizeof(pname),"arg.%s",par->name);
        char *k=arena_strdup(cx->arena,pname);
        vset(cx,k,pv);
        LLVMSetValueName2(pv,pname,strlen(pname));
    }

    for(FirBlock *b=fn->blocks;b;b=b->next){
        if(b->dead)continue;
        const char *lbl=b->label;
        if(!lbl||!lbl[0])lbl="blk";
        LLVMBasicBlockRef bb=bget(cx,lbl);
        if(!bb)continue;
        LLVMPositionBuilderAtEnd(cx->bld,bb);
        if(!b->head){LLVMBuildUnreachable(cx->bld);continue;}
        int has_term=0;
        for(FirInstr *ins=b->head;ins&&!has_term;ins=ins->next){
            emit_instr(cx,ins);
            FirOp op=ins->op;
            if(op==FIR_BR||op==FIR_BR_COND||op==FIR_RET||
               op==FIR_RET_VOID||op==FIR_UNREACHABLE) has_term=1;
        }
        if(!has_term)LLVMBuildUnreachable(cx->bld);
    }

    for(int pi2=0;pi2<cx->nphi;pi2++){
        DPhi *dp=&cx->phis[pi2];
        LLVMValueRef in_vals[PH_PAIR];
        LLVMBasicBlockRef in_bbs[PH_PAIR];
        int cnt=0;
        for(int j=0;j<dp->npairs;j++){
            LLVMBasicBlockRef bb=bget(cx,dp->lbls[j]);
            if(!bb)continue;
            in_vals[cnt]=rval(cx,dp->vals[j],dp->phi_ty);
            in_bbs[cnt]=bb;
            cnt++;
        }
        if(cnt>0)
            LLVMAddIncoming(dp->phi_val,in_vals,in_bbs,(unsigned)cnt);
    }
}

typedef struct { const char *name; const char *ret; const char *p[8]; int np; int va; } LibcSig;
static const LibcSig LIBC[]={
    {"printf",  "i32",{"ptr"},1,1},
    {"fprintf", "i32",{"ptr","ptr"},2,1},
    {"sprintf", "i32",{"ptr","ptr"},2,1},
    {"snprintf","i32",{"ptr","i64","ptr"},3,1},
    {"scanf",   "i32",{"ptr"},1,1},
    {"sscanf",  "i32",{"ptr","ptr"},2,1},
    {"puts",    "i32",{"ptr"},1,0},
    {"putchar", "i32",{"i32"},1,0},
    {"getchar", "i32",{0},0,0},
    {"fgets",   "ptr",{"ptr","i32","ptr"},3,0},
    {"fputc",   "i32",{"i32","ptr"},2,0},
    {"fputs",   "i32",{"ptr","ptr"},2,0},
    {"fopen",   "ptr",{"ptr","ptr"},2,0},
    {"fclose",  "i32",{"ptr"},1,0},
    {"fread",   "i64",{"ptr","i64","i64","ptr"},4,0},
    {"fwrite",  "i64",{"ptr","i64","i64","ptr"},4,0},
    {"malloc",  "ptr",{"i64"},1,0},
    {"calloc",  "ptr",{"i64","i64"},2,0},
    {"realloc", "ptr",{"ptr","i64"},2,0},
    {"free",    "void",{"ptr"},1,0},
    {"memcpy",  "ptr",{"ptr","ptr","i64"},3,0},
    {"memmove", "ptr",{"ptr","ptr","i64"},3,0},
    {"memset",  "ptr",{"ptr","i32","i64"},3,0},
    {"memcmp",  "i32",{"ptr","ptr","i64"},3,0},
    {"strlen",  "i64",{"ptr"},1,0},
    {"strcmp",  "i32",{"ptr","ptr"},2,0},
    {"strncmp", "i32",{"ptr","ptr","i64"},3,0},
    {"strcpy",  "ptr",{"ptr","ptr"},2,0},
    {"strncpy", "ptr",{"ptr","ptr","i64"},3,0},
    {"strcat",  "ptr",{"ptr","ptr"},2,0},
    {"strncat", "ptr",{"ptr","ptr","i64"},3,0},
    {"strstr",  "ptr",{"ptr","ptr"},2,0},
    {"strchr",  "ptr",{"ptr","i32"},2,0},
    {"strrchr", "ptr",{"ptr","i32"},2,0},
    {"strtok",  "ptr",{"ptr","ptr"},2,0},
    {"strerror","ptr",{"i32"},1,0},
    {"atoi",    "i32",{"ptr"},1,0},
    {"atol",    "i64",{"ptr"},1,0},
    {"atof",    "double",{"ptr"},1,0},
    {"strtol",  "i64",{"ptr","ptr","i32"},3,0},
    {"strtod",  "double",{"ptr","ptr"},2,0},
    {"sqrt",    "double",{"double"},1,0},
    {"pow",     "double",{"double","double"},2,0},
    {"exp",     "double",{"double"},1,0},
    {"log",     "double",{"double"},1,0},
    {"log2",    "double",{"double"},1,0},
    {"log10",   "double",{"double"},1,0},
    {"sin",     "double",{"double"},1,0},
    {"cos",     "double",{"double"},1,0},
    {"tan",     "double",{"double"},1,0},
    {"asin",    "double",{"double"},1,0},
    {"acos",    "double",{"double"},1,0},
    {"atan",    "double",{"double"},1,0},
    {"atan2",   "double",{"double","double"},2,0},
    {"floor",   "double",{"double"},1,0},
    {"ceil",    "double",{"double"},1,0},
    {"round",   "double",{"double"},1,0},
    {"fabs",    "double",{"double"},1,0},
    {"fmod",    "double",{"double","double"},2,0},
    {"sqrtf",   "float",{"float"},1,0},
    {"powf",    "float",{"float","float"},2,0},
    {"fabsf",   "float",{"float"},1,0},
    {"floorf",  "float",{"float"},1,0},
    {"ceilf",   "float",{"float"},1,0},
    {"sinf",    "float",{"float"},1,0},
    {"cosf",    "float",{"float"},1,0},
    {"abs",     "i32",{"i32"},1,0},
    {"labs",    "i64",{"i64"},1,0},
    {"exit",    "void",{"i32"},1,0},
    {"_exit",   "void",{"i32"},1,0},
    {"system",  "i32",{"ptr"},1,0},
    {"getenv",  "ptr",{"ptr"},1,0},
    {"setenv",  "i32",{"ptr","ptr","i32"},3,0},
    {"rand",    "i32",{0},0,0},
    {"srand",   "void",{"i32"},1,0},
    {"read",    "i64",{"i32","ptr","i64"},3,0},
    {"write",   "i64",{"i32","ptr","i64"},3,0},
    {"clock_gettime","i32",{"i32","ptr"},2,0},
    {"nanosleep","i32",{"ptr","ptr"},2,0},
    {"getcwd",  "ptr",{"ptr","i64"},2,0},
    {"chdir",   "i32",{"ptr"},1,0},
    {NULL,NULL,{0},0,0}
};

static int module_uses_fn(FirModule *m, const char *name) {
    for(FirFn *fn=m->fns;fn;fn=fn->next)
        for(FirBlock *b=fn->blocks;b;b=b->next)
            for(FirInstr *ins=b->head;ins;ins=ins->next)
                if((ins->op==FIR_CALL||ins->op==FIR_CALL_INDIRECT)&&
                   ins->src1&&!strcmp(ins->src1,name)) return 1;
    return 0;
}

FirObjBuf fir_to_obj(FirModule *m, Arena *arena, const char *triple, int opt_level) {
    FirObjBuf result={NULL,0,0,{0}};

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    Cx cx_val; memset(&cx_val,0,sizeof(cx_val));
    Cx *cx=&cx_val;
    cx->ctx  =LLVMContextCreate();
    cx->mod  =LLVMModuleCreateWithNameInContext("fermi",cx->ctx);
    cx->bld  =LLVMCreateBuilderInContext(cx->ctx);
    cx->fm   =m;
    cx->arena=arena;

    char *default_triple=NULL;
    if(triple&&triple[0]){
        LLVMSetTarget(cx->mod,triple);
    } else {
        default_triple=LLVMGetDefaultTargetTriple();
        LLVMSetTarget(cx->mod,default_triple);
    }

    for(FirStructDecl *sd=m->struct_decls;sd;sd=sd->next){
        if(sd->nfields==0)continue;
        LLVMTypeRef sty=LLVMStructCreateNamed(cx->ctx,sd->name);
        LLVMTypeRef fields[64]; int nf=0;
        for(int i=0;i<sd->nfields&&nf<64;i++)
            fields[nf++]=rty(cx,sd->field_types[i]);
        LLVMStructSetBody(sty,fields,(unsigned)nf,0);
        if(cx->nty<TM_CAP){cx->tymap[cx->nty].k=sd->name;cx->tymap[cx->nty].v=sty;cx->nty++;}
    }

    for(FirGlobal *g=m->globals;g;g=g->next){
        LLVMTypeRef gty=g->type?rty(cx,g->type):LLVMInt8TypeInContext(cx->ctx);
        LLVMValueRef gv=LLVMAddGlobal(cx->mod,gty,g->name);
        if(g->is_const){LLVMSetLinkage(gv,LLVMPrivateLinkage);LLVMSetGlobalConstant(gv,1);}
        LLVMSetInitializer(gv,LLVMConstNull(gty));
        if(cx->nfn<FM_CAP){cx->fnmap[cx->nfn].k=g->name;cx->fnmap[cx->nfn].v=gv;cx->nfn++;}
    }

    static const char *ext_gvars[]={"stderr","stdin","stdout",NULL};
    for(int i=0;ext_gvars[i];i++){
        LLVMTypeRef pty=LLVMPointerTypeInContext(cx->ctx,0);
        LLVMValueRef gv=LLVMAddGlobal(cx->mod,pty,ext_gvars[i]);
        LLVMSetLinkage(gv,LLVMExternalLinkage);
        if(cx->nfn<FM_CAP){cx->fnmap[cx->nfn].k=ext_gvars[i];cx->fnmap[cx->nfn].v=gv;cx->nfn++;}
    }

    for(int i=0;LIBC[i].name;i++){
        const LibcSig *sig=&LIBC[i];
        if(!module_uses_fn(m,sig->name))continue;
        LLVMTypeRef ret=rty(cx,sig->ret);
        LLVMTypeRef params[8]; int np=sig->np;
        for(int j=0;j<np;j++)params[j]=rty(cx,sig->p[j]);
        LLVMTypeRef fty=LLVMFunctionType(ret,params,(unsigned)np,sig->va);
        LLVMValueRef fv=LLVMAddFunction(cx->mod,sig->name,fty);
        LLVMSetLinkage(fv,LLVMExternalLinkage);
        if(cx->nfn<FM_CAP){cx->fnmap[cx->nfn].k=sig->name;cx->fnmap[cx->nfn].v=fv;cx->nfn++;}
    }

    for(FirFn *fn=m->fns;fn;fn=fn->next){
        if(!fn->is_extern)continue;
        LLVMTypeRef fty=build_fn_type(cx,fn);
        LLVMValueRef fv=LLVMAddFunction(cx->mod,fn->name,fty);
        LLVMSetLinkage(fv,LLVMExternalLinkage);
        if(cx->nfn<FM_CAP){cx->fnmap[cx->nfn].k=fn->name;cx->fnmap[cx->nfn].v=fv;cx->nfn++;}
    }

    for(FirFn *fn=m->fns;fn;fn=fn->next){
        if(fn->is_extern)continue;
        LLVMTypeRef fty=build_fn_type(cx,fn);
        LLVMValueRef fv=LLVMAddFunction(cx->mod,fn->name,fty);
        LLVMSetLinkage(fv,LLVMExternalLinkage);
        if(cx->nfn<FM_CAP){cx->fnmap[cx->nfn].k=fn->name;cx->fnmap[cx->nfn].v=fv;cx->nfn++;}
    }

    for(FirFn *fn=m->fns;fn;fn=fn->next){
        if(fn->is_extern)continue;
        LLVMValueRef fn_val=NULL;
        for(int i=0;i<cx->nfn;i++)
            if(strcmp(cx->fnmap[i].k,fn->name)==0){fn_val=cx->fnmap[i].v;break;}
        if(!fn_val)continue;
        emit_fn_body(cx,fn,fn_val);
    }

    char *errmsg=NULL;
    if(LLVMVerifyModule(cx->mod,LLVMReturnStatusAction,&errmsg)){
        snprintf(result.err,sizeof(result.err),"verify: %s",errmsg?errmsg:"?");
        if(errmsg)LLVMDisposeMessage(errmsg);
        goto cleanup;
    }
    if(errmsg)LLVMDisposeMessage(errmsg);

    {
        const char *use_triple=triple&&triple[0]?triple:LLVMGetTarget(cx->mod);
        LLVMTargetRef target_ref;
        char *terr=NULL;
        if(LLVMGetTargetFromTriple(use_triple,&target_ref,&terr)){
            snprintf(result.err,sizeof(result.err),"target: %s",terr?terr:"?");
            if(terr)LLVMDisposeMessage(terr);
            goto cleanup;
        }

        LLVMCodeGenOptLevel cg_opt=
            opt_level>=3?LLVMCodeGenLevelAggressive:
            opt_level==2?LLVMCodeGenLevelDefault:
            opt_level==1?LLVMCodeGenLevelLess:
                          LLVMCodeGenLevelNone;

        char *cpu=LLVMGetHostCPUName();
        char *feat=LLVMGetHostCPUFeatures();
        LLVMTargetMachineRef tm=LLVMCreateTargetMachine(
            target_ref,use_triple,cpu,feat,
            cg_opt,LLVMRelocPIC,LLVMCodeModelDefault);
        LLVMDisposeMessage(cpu);
        LLVMDisposeMessage(feat);

        LLVMTargetDataRef dl=LLVMCreateTargetDataLayout(tm);
        char *dlstr=LLVMCopyStringRepOfTargetData(dl);
        LLVMSetDataLayout(cx->mod,dlstr);
        LLVMDisposeMessage(dlstr);
        LLVMDisposeTargetData(dl);

        if(opt_level>0){
            LLVMPassBuilderOptionsRef pbo=LLVMCreatePassBuilderOptions();
            char ps[16]; snprintf(ps,sizeof(ps),"default<O%d>",opt_level>3?3:opt_level);
            LLVMErrorRef perr=LLVMRunPasses(cx->mod,ps,tm,pbo);
            if(perr)LLVMConsumeError(perr);
            LLVMDisposePassBuilderOptions(pbo);
        }

        LLVMMemoryBufferRef obj_buf;
        char *eerr=NULL;
        if(LLVMTargetMachineEmitToMemoryBuffer(tm,cx->mod,LLVMObjectFile,&eerr,&obj_buf)){
            snprintf(result.err,sizeof(result.err),"emit: %s",eerr?eerr:"?");
            if(eerr)LLVMDisposeMessage(eerr);
            LLVMDisposeTargetMachine(tm);
            goto cleanup;
        }
        if(eerr)LLVMDisposeMessage(eerr);

        result.size=LLVMGetBufferSize(obj_buf);
        result.data=malloc(result.size);
        if(result.data){
            memcpy(result.data,LLVMGetBufferStart(obj_buf),result.size);
            result.ok=1;
        } else {
            snprintf(result.err,sizeof(result.err),"out of memory");
        }
        LLVMDisposeMemoryBuffer(obj_buf);
        LLVMDisposeTargetMachine(tm);
    }

cleanup:
    LLVMDisposeBuilder(cx->bld);
    LLVMDisposeModule(cx->mod);
    LLVMContextDispose(cx->ctx);
    if(default_triple)LLVMDisposeMessage(default_triple);
    return result;
}

void fir_obj_free(FirObjBuf *b) {
    if(b&&b->data){free(b->data);b->data=NULL;b->size=0;}
}
