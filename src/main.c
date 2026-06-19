#if defined(__linux__) && !defined(__ANDROID__)
#define _GNU_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include "fermi/fermi.h"
#include "fearena/arena.h"
#include "felexer/lexer.h"
#include "felexer/token.h"
#include "feparser/parser.h"
#include "fehir/hir.h"
#include "fetc/tc.h"
#include "fesema/sema.h"
#include "fecodegen/codegen.h"
#include "fecodegen/fir.h"
#include "feopt/opt.h"
#include "fellvm/llvm_emit.h"

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec*1e3+(double)ts.tv_nsec/1e6;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f=fopen(path,"rb");
    if(!f)return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=malloc((size_t)sz+2);
    if(!buf){fclose(f);return NULL;}
    size_t r=fread(buf,1,(size_t)sz,f); buf[r]='\0'; fclose(f);
    if(out_len)*out_len=(size_t)r;
    return buf;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Fermi v%d.%d.%d\n"
        "Usage: %s [options] <file.fe> [-- prog_args...]\n\n"
        "Options:\n"
        "  --fir           Dump FIR to stdout\n"
        "  --ast           Parse only\n"
        "  --lex           Lex only (dump tokens)\n"
        "  -o <out>        Output executable (do not run)\n"
        "  --no-opt        Disable FIR optimizer\n"
        "  --time          Print timing info\n"
        "  --target <t>    LLVM target triple\n"
        "  -O0..3          LLVM optimization level\n"
        "  --cache-clear   Clear compiled binary cache\n"
        "  --version,-v    Print version\n\n"
        "Default (no -o): compile, cache in ~/.cache/fermi/, then exec.\n"
        "  Re-runs exec the cached binary directly (stat()-based key).\n",
        V_MAJOR,V_MINOR,V_PATCH,prog);
}

static int make_temp_path(char *buf, size_t buflen, const char *prefix, const char *suffix) {
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    if (suffix && suffix[0]) {
        char pattern[512];
        snprintf(pattern,sizeof(pattern),"%s/%sXXXXXX%s",tmpdir,prefix?prefix:"",suffix);
        int sl=(int)strlen(suffix);
#if defined(__GLIBC__) || defined(__ANDROID__) || defined(__APPLE__)
        int fd=mkstemps(pattern,sl);
#else
        int fd=-1;
        {
            char *x=strstr(pattern,"XXXXXX");
            if(x){
                unsigned seed=(unsigned)time(NULL)^(unsigned)getpid();
                for(int attempt=0;attempt<100&&fd<0;attempt++){
                    unsigned r=seed+(unsigned)attempt*2654435761u;
                    static const char ch[]="abcdefghijklmnopqrstuvwxyz0123456789";
                    for(int j=0;j<6;j++) x[j]=ch[(r>>(j*5))%36];
                    fd=open(pattern,O_RDWR|O_CREAT|O_EXCL,0600);
                }
            }
        }
#endif
        if(fd<0)return -1;
        snprintf(buf,buflen,"%s",pattern);
        return fd;
    } else {
        char pattern[512];
        snprintf(pattern,sizeof(pattern),"%s/%sXXXXXX",tmpdir,prefix?prefix:"");
        int fd=mkstemp(pattern);
        if(fd<0)return -1;
        snprintf(buf,buflen,"%s",pattern);
        return fd;
    }
}

static uint64_t fnv1a64(const void *data, size_t n, uint64_t h) {
    const uint8_t *p=(const uint8_t*)data;
    for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ULL;}
    return h;
}
static uint64_t fnv1a64_str(uint64_t h, const char *s) {
    if(s)for(;*s;s++){h^=(uint8_t)*s;h*=1099511628211ULL;}
    return h;
}
#define FNV1A_SEED 14695981039346656037ULL

static void get_cache_dir(char *buf, size_t len) {
    const char *xdg=getenv("XDG_CACHE_HOME");
    const char *home=getenv("HOME");
    const char *tmp=getenv("TMPDIR");
    if(xdg&&*xdg)        snprintf(buf,len,"%s/fermi",xdg);
    else if(home&&*home)  snprintf(buf,len,"%s/.cache/fermi",home);
    else                  snprintf(buf,len,"%s/fermi_cache",tmp?tmp:"/tmp");
}

static void mkdir_p(const char *path) {
    if(mkdir(path,0755)==0)return;
    char tmp[512]; snprintf(tmp,sizeof(tmp),"%s",path);
    char *sl=strrchr(tmp,'/');
    if(sl&&sl!=tmp){*sl='\0';mkdir(tmp,0755);}
    mkdir(path,0755);
}

static void find_rt_lib(char *buf, size_t len) {
    buf[0]='\0';
    const char *env=getenv("FERMI_RT");
    if(env&&access(env,R_OK)==0){snprintf(buf,len,"%s",env);return;}
    char self[512]={0};
    ssize_t nr=readlink("/proc/self/exe",self,(size_t)(sizeof(self)-1));
    if(nr>0){
        self[nr]='\0';
        char *sl=strrchr(self,'/');
        if(sl){
            *sl='\0';
            char c[600];
            snprintf(c,sizeof(c),"%s/libfermi_rt.a",self);
            if(access(c,R_OK)==0){snprintf(buf,len,"%s",c);return;}
            snprintf(c,sizeof(c),"%s/../lib/libfermi_rt.a",self);
            if(access(c,R_OK)==0){snprintf(buf,len,"%s",c);return;}
        }
    }
    const char *home=getenv("HOME");
    if(home){
        char c[600];
        snprintf(c,sizeof(c),"%s/.local/lib/libfermi_rt.a",home);
        if(access(c,R_OK)==0){snprintf(buf,len,"%s",c);return;}
    }
    static const char *known[]={
        "/data/data/com.termux/files/usr/lib/libfermi_rt.a",
        "/usr/local/lib/libfermi_rt.a",
        "/usr/lib/libfermi_rt.a",
        NULL
    };
    for(int k=0;known[k];k++)
        if(access(known[k],R_OK)==0){snprintf(buf,len,"%s",known[k]);return;}
}

static char **make_exec_argv(const char *binary, char **orig_argv, int prog_start) {
    int orig_argc=0;
    while(orig_argv[orig_argc])orig_argc++;
    int np=orig_argc-prog_start;
    if(np<0)np=0;
    char **ea=malloc((size_t)(np+2)*sizeof(char*));
    if(!ea)return NULL;
    ea[0]=(char*)binary;
    for(int k=0;k<np;k++) ea[k+1]=orig_argv[prog_start+k];
    ea[np+1]=NULL;
    return ea;
}

int main(int argc, char **argv) {
    if(argc<2){usage(argv[0]);return 1;}

    const char *input=NULL,*output=NULL,*target=NULL,*opt_level="-O2";
    int mode_fir=0,mode_ast=0,mode_lex=0;
    int do_opt=1,do_time=0,explicit_output=0;
    int opt_lvl_int=2;

    int prog_argv_start=argc;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--")){prog_argv_start=i+1;argc=i;break;}
    }

    for(int i=1;i<argc;i++){
        if     (!strcmp(argv[i],"--fir"))    mode_fir=1;
        else if(!strcmp(argv[i],"--ast"))    mode_ast=1;
        else if(!strcmp(argv[i],"--lex"))    mode_lex=1;
        else if(!strcmp(argv[i],"--opt"))    do_opt=1;
        else if(!strcmp(argv[i],"--no-opt")) do_opt=0;
        else if(!strcmp(argv[i],"--time"))   do_time=1;
        else if(!strcmp(argv[i],"--version")||!strcmp(argv[i],"-v")){
            printf("fermi %d.%d.%d\n",V_MAJOR,V_MINOR,V_PATCH);return 0;
        }
        else if(!strcmp(argv[i],"--cache-clear")){
            char cdir[512]; get_cache_dir(cdir,sizeof(cdir));
            char cmd[640]; snprintf(cmd,sizeof(cmd),"rm -f '%s'/*",cdir);
            (void)system(cmd);
            fprintf(stderr,"[cache] cleared: %s\n",cdir);
            return 0;
        }
        else if(!strcmp(argv[i],"-o")&&i+1<argc){
            output=argv[++i]; explicit_output=1;
        }
        else if(!strcmp(argv[i],"--target")&&i+1<argc) target=argv[++i];
        else if(!strncmp(argv[i],"-O",2)){
            opt_level=argv[i];
            opt_lvl_int=(int)(argv[i][2]-'0');
            if(opt_lvl_int<0||opt_lvl_int>3)opt_lvl_int=2;
        }
        else if(argv[i][0]=='-')
            fprintf(stderr,"fermi: warning[W0001]: unrecognized option '%s'; option ignored\n",argv[i]);
        else if(!input) input=argv[i];
    }
    if(!input){fprintf(stderr,"fermi: error[E0000]: no input file specified\n");usage(argv[0]);return 1;}

    int run_mode=!mode_fir&&!mode_ast&&!mode_lex&&!explicit_output;

    char cache_dir[512],cache_bin[640];

    if(run_mode){
        struct stat fst;
        if(stat(input,&fst)!=0){
            fprintf(stderr,"fermi: error[E0000]: unable to access source file '%s'; stat() failed\n",input);return 1;
        }
        char ver[32];
        snprintf(ver,sizeof(ver),"%d.%d.%d",V_MAJOR,V_MINOR,V_PATCH);
        uint64_t h=FNV1A_SEED;
        h=fnv1a64(&fst.st_ino,sizeof(fst.st_ino),h);
        h=fnv1a64(&fst.st_size,sizeof(fst.st_size),h);
#if defined(__linux__)
        h=fnv1a64(&fst.st_mtim.tv_sec,sizeof(fst.st_mtim.tv_sec),h);
        h=fnv1a64(&fst.st_mtim.tv_nsec,sizeof(fst.st_mtim.tv_nsec),h);
#else
        h=fnv1a64(&fst.st_mtime,sizeof(fst.st_mtime),h);
#endif
        h=fnv1a64_str(h,ver);
        h=fnv1a64_str(h,opt_level);
        h=fnv1a64_str(h,target?target:"");

        get_cache_dir(cache_dir,sizeof(cache_dir));
        snprintf(cache_bin,sizeof(cache_bin),"%s/%016llx",
                 cache_dir,(unsigned long long)h);

        if(access(cache_bin,X_OK)==0){
            char **ea=make_exec_argv(cache_bin,argv,prog_argv_start);
            if(ea){execv(cache_bin,ea);free(ea);}
            perror("execv");
            return 1;
        }

        mkdir_p(cache_dir);
        output=cache_bin;
    }

    if(!output) output="a.out";

    double t0=now_ms();

    size_t src_len=0;
    char *src=read_file(input,&src_len);
    if(!src){fprintf(stderr,"fermi: error[E0000]: unable to open source file '%s'\n",input);return 1;}

    Arena arena_val=arena_new(64*1024*1024);
    Arena *arena=&arena_val;

    if(mode_lex){
        Lexer l2=lexer_new(src,src_len,arena);
        Token tok;
        do{tok=lexer_next(&l2);token_print(&tok);}while(tok.type!=TOK_EOF);
        free(src);arena_free(arena);return 0;
    }

    Parser p=parser_new(src,src_len,arena,input);
    AstNode *prog=parse_program(&p);
    if(p.had_error){free(src);arena_free(arena);return 1;}

    if(mode_ast){printf("[ast] ok\n");free(src);arena_free(arena);return 0;}

    Hir hir=hir_new(arena); hir_lower(&hir,prog);

    Tc tc=tc_new(arena,input); tc_check(&tc,prog);
    if(tc.had_error){free(src);arena_free(arena);return 1;}

    Sema sema=sema_new(arena,input); sema_check(&sema,prog);
    if(sema.had_error){free(src);arena_free(arena);return 1;}

    double tcg=now_ms();
    Codegen *cg=codegen_new(arena,input);
    codegen_emit(cg,prog);
    FirModule *mod=codegen_module(cg);
    double tcg_end=now_ms();

    if(do_opt) mir_opt(mod,arena);

    if(mode_fir){fir_print_module(mod);free(src);arena_free(arena);return 0;}

    double tllvm=now_ms();
    FirObjBuf obj=fir_to_obj(mod,arena,target?target:"",opt_lvl_int);
    double tllvm_end=now_ms();

    if(!obj.ok){
        fprintf(stderr,"fermi: error[E0011]: LLVM backend code generation failed: %s\n",obj.err);
        free(src);arena_free(arena);return 1;
    }

    if(do_time){
        fprintf(stderr,"[time] parse:     %.3fms\n",tcg-t0);
        fprintf(stderr,"[time] codegen:   %.3fms\n",tcg_end-tcg);
        fprintf(stderr,"[time] llvm-emit: %.3fms\n",tllvm_end-tllvm);
        fprintf(stderr,"[time] total-fe:  %.3fms\n",tllvm_end-t0);
    }

    char tmp_obj[512]; tmp_obj[0]='\0';
    int ofd=make_temp_path(tmp_obj,sizeof(tmp_obj),"fermi_",".o");
    if(ofd<0){
        fir_obj_free(&obj);
        fprintf(stderr,"fermi: error[E0012]: failed to allocate temporary object file path\n");
        free(src);arena_free(arena);return 1;
    }
    ssize_t written=(ssize_t)0;
    size_t remain=obj.size; const char *objp=obj.data;
    while(remain>0){
        ssize_t r=write(ofd,objp,remain);
        if(r<=0)break;
        written+=r; objp+=r; remain-=(size_t)r;
    }
    close(ofd);
    fir_obj_free(&obj);
    if((size_t)written!=obj.size-remain+(size_t)(objp-obj.data)){
        if(remain!=0){
            unlink(tmp_obj);
            fprintf(stderr,"fermi: error[E0013]: incomplete write to object file; short write detected\n");
            free(src);arena_free(arena);return 1;
        }
    }
    (void)written;

    char rt_lib[600]; find_rt_lib(rt_lib,sizeof(rt_lib));
    char cmd[4096];
    if(rt_lib[0])
        snprintf(cmd,sizeof(cmd),"cc -o '%s' '%s' '%s' -lm 2>&1",
                 output,tmp_obj,rt_lib);
    else
        snprintf(cmd,sizeof(cmd),"cc -o '%s' '%s' -lm 2>&1",
                 output,tmp_obj);

    int rc=system(cmd);
    unlink(tmp_obj);
    free(src);arena_free(arena);

    if(rc!=0){fprintf(stderr,"fermi: error[E0014]: linker invocation failed with exit code %d\n",rc>>8);return 1;}

    if(run_mode){
        chmod(output,0755);
        char **ea=make_exec_argv(output,argv,prog_argv_start);
        if(ea){execv(output,ea);free(ea);}
        perror("execv");
        return 1;
    }

    return 0;
}
