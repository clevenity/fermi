#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "lexer.h"

Lexer lexer_new(const char *src, size_t len, Arena *arena) {
    Lexer l={src,0,1,1,(int)len,arena}; return l;
}
static char pk(Lexer *l){return l->pos<l->len?l->src[l->pos]:'\0';}
static char pk2(Lexer *l){return l->pos+1<l->len?l->src[l->pos+1]:'\0';}
static char pk3(Lexer *l){return l->pos+2<l->len?l->src[l->pos+2]:'\0';}
static char lx_adv(Lexer *l){
    char c=l->src[l->pos++];
    if(c=='\n'){l->line++;l->col=1;}else l->col++;
    return c;
}
static Token mktok(Lexer *l,TokenType t,const char *v,size_t n,int line,int col){
    Token tok; tok.type=t; tok.line=line; tok.col=col;
    tok.value=arena_strndup(l->arena,v,n); return tok;
}
static void skip_ws(Lexer *l){
    while(l->pos<l->len){
        char c=pk(l);
        if(c==' '||c=='\t'||c=='\r'||c=='\n'){lx_adv(l);}
        else if(c=='/'&&pk2(l)=='/'){while(l->pos<l->len&&pk(l)!='\n')lx_adv(l);}
        else if(c=='/'&&pk2(l)=='*'){
            lx_adv(l);lx_adv(l);
            while(l->pos<l->len){
                if(pk(l)=='*'&&pk2(l)=='/'){lx_adv(l);lx_adv(l);break;}
                lx_adv(l);
            }
        }else break;
    }
}
static TokenType kw_check(const char *s,int n){
    switch(n){
    case 2:
        if(s[0]=='f'&&s[1]=='n')return TOK_FN;
        if(s[0]=='i'&&s[1]=='f')return TOK_IF;
        if(s[0]=='i'&&s[1]=='n')return TOK_IN;
        if(s[0]=='d'&&s[1]=='o')return TOK_DO;
        break;
    case 3:
        if(!memcmp(s,"let",3))return TOK_LET;
        if(!memcmp(s,"mut",3))return TOK_MUT;
        if(!memcmp(s,"for",3))return TOK_FOR;
        if(!memcmp(s,"new",3))return TOK_NEW;
        if(!memcmp(s,"int",3))return TOK_INT;
        if(!memcmp(s,"str",3))return TOK_STR;
        break;
    case 4:
        switch(s[0]){
        case 'b':
            if(!memcmp(s,"bool",4))return TOK_BOOL;
            if(!memcmp(s,"byte",4))return TOK_BYTE;
            break;
        case 'c': if(!memcmp(s,"char",4))return TOK_CHAR; break;
        case 'n': if(!memcmp(s,"null",4))return TOK_NULL; break;
        case 'e':
            if(!memcmp(s,"else",4))return TOK_ELSE;
            if(!memcmp(s,"enum",4))return TOK_ENUM;
            break;
        case 'l': if(!memcmp(s,"long",4))return TOK_LONG; break;
        case 't':
            if(!memcmp(s,"this",4))return TOK_THIS;
            if(!memcmp(s,"true",4))return TOK_TRUE;
            if(!memcmp(s,"type",4))return TOK_TYPE;
            break;
        case 'u':
            if(!memcmp(s,"uint",4))return TOK_UINT;
            if(!memcmp(s,"unit",4))return TOK_UNIT;
            break;
        }
        break;
    case 5:
        switch(s[0]){
        case 'b': if(!memcmp(s,"break",5))return TOK_BREAK; break;
        case 'c':
            if(!memcmp(s,"const",5))return TOK_CONST;
            if(!memcmp(s,"class",5))return TOK_CLASS;
            break;
        case 'd': if(!memcmp(s,"defer",5))return TOK_DEFER; break;
        case 'f':
            if(!memcmp(s,"false",5))return TOK_FALSE;
            if(!memcmp(s,"float",5))return TOK_FLOAT;
            break;
        case 'm':
            if(!memcmp(s,"match",5))return TOK_MATCH;
            if(!memcmp(s,"model",5))return TOK_IDENT;
            break;
        case 's': if(!memcmp(s,"short",5))return TOK_SHORT; break;
        case 'u': if(!memcmp(s,"ulong",5))return TOK_ULONG; break;
        case 'w': if(!memcmp(s,"while",5))return TOK_WHILE; break;
        }
        break;
    case 6:
        switch(s[0]){
        case 'd': if(!memcmp(s,"double",6))return TOK_DOUBLE; break;
        case 'e': if(!memcmp(s,"export",6))return TOK_EXPORT; break;
        case 'i': if(!memcmp(s,"import",6))return TOK_IMPORT; break;
        case 'm': if(!memcmp(s,"module",6))return TOK_MODULE; break;
        case 'p': if(!memcmp(s,"public",6))return TOK_PUBLIC; break;
        case 'r':
            if(!memcmp(s,"return",6))return TOK_RETURN;
            if(!memcmp(s,"region",6))return TOK_REGION;
            break;
        case 'u':
            if(!memcmp(s,"ushort",6))return TOK_USHORT;
            if(!memcmp(s,"unsafe",6))return TOK_UNSAFE;
            break;
        }
        break;
    case 7:
        if(!memcmp(s,"private",7))return TOK_PRIVATE;
        break;
    case 8:
        if(!memcmp(s,"continue",8))return TOK_CONTINUE;
        if(!memcmp(s,"external",8))return TOK_EXTERNAL;
        if(!memcmp(s,"comptime",8))return TOK_COMPTIME;
        break;
    }
    return TOK_IDENT;
}
static Token lex_ident(Lexer *l){
    int line=l->line,col=l->col,start=l->pos;
    while(l->pos<l->len&&(isalnum((unsigned char)pk(l))||pk(l)=='_'))lx_adv(l);
    int len=l->pos-start;
    const char *s=l->src+start;
    TokenType t=kw_check(s,len);
    if(t==TOK_TRUE||t==TOK_FALSE)return mktok(l,TOK_BOOL_LIT,s,(size_t)len,line,col);
    return mktok(l,t,s,(size_t)len,line,col);
}

static int hexval(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}

static int encode_utf8(uint32_t cp, char *out){
    if(cp<0x80){out[0]=(char)cp;return 1;}
    if(cp<0x800){out[0]=(char)(0xC0|(cp>>6));out[1]=(char)(0x80|(cp&0x3F));return 2;}
    if(cp<0x10000){out[0]=(char)(0xE0|(cp>>12));out[1]=(char)(0x80|((cp>>6)&0x3F));out[2]=(char)(0x80|(cp&0x3F));return 3;}
    out[0]=(char)(0xF0|(cp>>18));out[1]=(char)(0x80|((cp>>12)&0x3F));out[2]=(char)(0x80|((cp>>6)&0x3F));out[3]=(char)(0x80|(cp&0x3F));return 4;
}

static int lex_escape(Lexer *l, char out[8]){
    if(l->pos>=l->len) return 0;
    char e=lx_adv(l);
    char utf8[4];
    switch(e){
    case 'n':  out[0]='\n'; return 1;
    case 't':  out[0]='\t'; return 1;
    case 'r':  out[0]='\r'; return 1;
    case 'a':  out[0]='\a'; return 1;
    case 'b':  out[0]='\b'; return 1;
    case 'f':  out[0]='\f'; return 1;
    case 'v':  out[0]='\v'; return 1;
    case '\\': out[0]='\\'; return 1;
    case '"':  out[0]='"';  return 1;
    case '\'': out[0]='\''; return 1;
    case '0':  out[0]='\0'; return 1;
    case 'x': {
        char h1=isxdigit((unsigned char)pk(l))?lx_adv(l):'0';
        char h2=isxdigit((unsigned char)pk(l))?lx_adv(l):'0';
        out[0]=(char)((hexval(h1)<<4)|hexval(h2));
        return 1;
    }
    case 'u': {
        uint32_t cp=0;
        if(pk(l)=='{'){
            lx_adv(l);
            while(l->pos<l->len&&isxdigit((unsigned char)pk(l)))
                cp=cp*16+(uint32_t)hexval(lx_adv(l));
            if(pk(l)=='}') lx_adv(l);
        } else {
            for(int k=0;k<4&&l->pos<l->len;k++)
                cp=cp*16+(uint32_t)hexval(lx_adv(l));
        }
        int n=encode_utf8(cp,utf8);
        for(int k=0;k<n;k++) out[k]=utf8[k];
        return n;
    }
    case 'U': {
        uint32_t cp=0;
        for(int k=0;k<8&&l->pos<l->len;k++)
            cp=cp*16+(uint32_t)hexval(lx_adv(l));
        int n=encode_utf8(cp,utf8);
        for(int k=0;k<n;k++) out[k]=utf8[k];
        return n;
    }
    case '\n': return 0;
    default:   out[0]='\\'; out[1]=e; return 2;
    }
}

static Token lex_string(Lexer *l){
    int line=l->line,col=l->col;
    lx_adv(l);
    int triple=0;
    if(pk(l)=='"'&&pk2(l)=='"'){lx_adv(l);lx_adv(l);triple=1;}

    char *buf=(char*)arena_alloc(l->arena,65536);
    int i=0;
    while(l->pos<l->len){
        char c=pk(l);
        if(!triple&&c=='"') break;
        if(triple&&c=='"'&&pk2(l)=='"'&&pk3(l)=='"') break;
        if(c=='\\'){
            lx_adv(l);
            char esc[8]; int n=lex_escape(l,esc);
            for(int k=0;k<n&&i<65530;k++) buf[i++]=esc[k];
        } else {
            buf[i++]=lx_adv(l);
        }
        if(i>=65530) break;
    }
    if(!triple&&pk(l)=='"') lx_adv(l);
    if(triple&&pk(l)=='"') { lx_adv(l);lx_adv(l);lx_adv(l); }
    return mktok(l,TOK_STRING_LIT,buf,(size_t)i,line,col);
}

static Token lex_raw_string(Lexer *l, int line, int col){
    lx_adv(l);
    char *buf=(char*)arena_alloc(l->arena,65536);
    int i=0;
    while(l->pos<l->len&&pk(l)!='"'){
        if(i<65530) buf[i++]=lx_adv(l);
        else { lx_adv(l); }
    }
    if(pk(l)=='"') lx_adv(l);
    return mktok(l,TOK_STRING_LIT,buf,(size_t)i,line,col);
}

static Token lex_char(Lexer *l){
    int line=l->line,col=l->col;
    lx_adv(l);
    char buf[8]; int len=0;
    if(pk(l)=='\\'){
        lx_adv(l);
        char esc[8]; len=lex_escape(l,esc);
        for(int k=0;k<len&&k<7;k++) buf[k]=esc[k];
    } else if(pk(l)!='\''){
        buf[0]=lx_adv(l); len=1;
    }
    if(pk(l)=='\'') lx_adv(l);
    return mktok(l,TOK_CHAR_LIT,buf,(size_t)len,line,col);
}

static Token lex_number(Lexer *l){
    int line=l->line,col=l->col;
    char buf[72]; size_t bi=0;

    if(pk(l)=='0'&&(pk2(l)=='x'||pk2(l)=='X')){
        buf[bi++]=lx_adv(l); buf[bi++]=lx_adv(l);
        while(l->pos<l->len&&(isxdigit((unsigned char)pk(l))||pk(l)=='_')){
            char c=lx_adv(l); if(c!='_'&&bi<68) buf[bi++]=c;
        }
        buf[bi]=0;
        if(pk(l)=='L'){lx_adv(l);return mktok(l,TOK_LONG_LIT,buf,bi,line,col);}
        return mktok(l,TOK_INT_LIT,buf,bi,line,col);
    }
    if(pk(l)=='0'&&(pk2(l)=='b'||pk2(l)=='B')){
        buf[bi++]=lx_adv(l); buf[bi++]=lx_adv(l);
        while(l->pos<l->len&&(pk(l)=='0'||pk(l)=='1'||pk(l)=='_')){
            char c=lx_adv(l); if(c!='_'&&bi<68) buf[bi++]=c;
        }
        buf[bi]=0;
        return mktok(l,TOK_INT_LIT,buf,bi,line,col);
    }
    if(pk(l)=='0'&&(pk2(l)=='o'||pk2(l)=='O')){
        buf[bi++]=lx_adv(l); buf[bi++]=lx_adv(l);
        while(l->pos<l->len&&((pk(l)>='0'&&pk(l)<='7')||pk(l)=='_')){
            char c=lx_adv(l); if(c!='_'&&bi<68) buf[bi++]=c;
        }
        buf[bi]=0;
        return mktok(l,TOK_INT_LIT,buf,bi,line,col);
    }

    int is_float=0;
    while(l->pos<l->len&&(isdigit((unsigned char)pk(l))||pk(l)=='_')){
        char c=lx_adv(l); if(c!='_'&&bi<68) buf[bi++]=c;
    }
    if(pk(l)=='.'&&pk2(l)!='.'&&(isdigit((unsigned char)pk2(l))||pk2(l)=='e'||pk2(l)=='E')){
        is_float=1; buf[bi++]=lx_adv(l);
        while(l->pos<l->len&&(isdigit((unsigned char)pk(l))||pk(l)=='_')){
            char c=lx_adv(l); if(c!='_'&&bi<68) buf[bi++]=c;
        }
    } else if(pk(l)=='.'&&isdigit((unsigned char)pk2(l))){
        is_float=1; buf[bi++]=lx_adv(l);
        while(l->pos<l->len&&(isdigit((unsigned char)pk(l))||pk(l)=='_')){
            char c=lx_adv(l); if(c!='_'&&bi<68) buf[bi++]=c;
        }
    }
    if(pk(l)=='e'||pk(l)=='E'){
        is_float=1; buf[bi++]=lx_adv(l);
        if(pk(l)=='+'||pk(l)=='-') buf[bi++]=lx_adv(l);
        while(l->pos<l->len&&isdigit((unsigned char)pk(l))) buf[bi++]=lx_adv(l);
    }
    buf[bi]=0;
    if(pk(l)=='d'||pk(l)=='D'){lx_adv(l);return mktok(l,TOK_DOUBLE_LIT,buf,bi,line,col);}
    if(pk(l)=='f'||pk(l)=='F'){lx_adv(l);return mktok(l,TOK_FLOAT_LIT,buf,bi,line,col);}
    if(pk(l)=='L'){lx_adv(l);return mktok(l,TOK_LONG_LIT,buf,bi,line,col);}
    if(pk(l)=='u'){
        lx_adv(l);
        if(pk(l)=='L'){lx_adv(l);return mktok(l,TOK_LONG_LIT,buf,bi,line,col);}
        return mktok(l,TOK_INT_LIT,buf,bi,line,col);
    }
    if(is_float) return mktok(l,TOK_FLOAT_LIT,buf,bi,line,col);
    return mktok(l,TOK_INT_LIT,buf,bi,line,col);
}

Token lexer_next(Lexer *l){
    skip_ws(l);
    if(l->pos>=l->len) return mktok(l,TOK_EOF,"",0,l->line,l->col);
    int line=l->line,col=l->col;
    char c=pk(l);
    if(isalpha((unsigned char)c)||c=='_'){
        if((c=='r'||c=='R')&&pk2(l)=='"'){lx_adv(l);return lex_raw_string(l,line,col);}
        return lex_ident(l);
    }
    if(isdigit((unsigned char)c)) return lex_number(l);
    if(c=='"') return lex_string(l);
    if(c=='\'') return lex_char(l);
    lx_adv(l);
    switch(c){
    case '+':
        if(pk(l)=='+'){lx_adv(l);return mktok(l,TOK_INC,"++",2,line,col);}
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_PLUS_ASSIGN,"+=",2,line,col);}
        return mktok(l,TOK_PLUS,"+",1,line,col);
    case '-':
        if(pk(l)=='-'){lx_adv(l);return mktok(l,TOK_DEC,"--",2,line,col);}
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_MINUS_ASSIGN,"-=",2,line,col);}
        if(pk(l)=='>'){lx_adv(l);return mktok(l,TOK_ARROW,"->",2,line,col);}
        return mktok(l,TOK_MINUS,"-",1,line,col);
    case '*':
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_STAR_ASSIGN,"*=",2,line,col);}
        return mktok(l,TOK_STAR,"*",1,line,col);
    case '/':
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_SLASH_ASSIGN,"/=",2,line,col);}
        return mktok(l,TOK_SLASH,"/",1,line,col);
    case '%':
        if(pk(l)=='{'){lx_adv(l);return mktok(l,TOK_INTERP_START,"%{",2,line,col);}
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_PERCENT_ASSIGN,"%=",2,line,col);}
        return mktok(l,TOK_PERCENT,"%",1,line,col);
    case '&':
        if(pk(l)=='&'){lx_adv(l);return mktok(l,TOK_AND,"&&",2,line,col);}
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_AMP_ASSIGN,"&=",2,line,col);}
        return mktok(l,TOK_AMP,"&",1,line,col);
    case '|':
        if(pk(l)=='|'){lx_adv(l);return mktok(l,TOK_OR,"||",2,line,col);}
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_PIPE_ASSIGN,"|=",2,line,col);}
        return mktok(l,TOK_PIPE,"|",1,line,col);
    case '^':
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_CARET_ASSIGN,"^=",2,line,col);}
        return mktok(l,TOK_CARET,"^",1,line,col);
    case '~':
        return mktok(l,TOK_TILDE,"~",1,line,col);
    case '=':
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_EQ,"==",2,line,col);}
        if(pk(l)=='>'){lx_adv(l);return mktok(l,TOK_FAT_ARROW,"=>",2,line,col);}
        return mktok(l,TOK_ASSIGN,"=",1,line,col);
    case '!':
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_NEQ,"!=",2,line,col);}
        return mktok(l,TOK_NOT,"!",1,line,col);
    case '<':
        if(pk(l)=='<'){
            lx_adv(l);
            if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_SHL_ASSIGN,"<<=",3,line,col);}
            return mktok(l,TOK_SHL,"<<",2,line,col);
        }
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_LTE,"<=",2,line,col);}
        return mktok(l,TOK_LT,"<",1,line,col);
    case '>':
        if(pk(l)=='>'){
            lx_adv(l);
            if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_SHR_ASSIGN,">>=",3,line,col);}
            return mktok(l,TOK_SHR,">>",2,line,col);
        }
        if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_GTE,">=",2,line,col);}
        return mktok(l,TOK_GT,">",1,line,col);
    case ':':
        if(pk(l)==':'){lx_adv(l);return mktok(l,TOK_DOUBLE_COLON,"::",2,line,col);}
        return mktok(l,TOK_COLON,":",1,line,col);
    case ';': return mktok(l,TOK_SEMICOLON,";",1,line,col);
    case ',': return mktok(l,TOK_COMMA,",",1,line,col);
    case '.':
        if(pk(l)=='.'){
            lx_adv(l);
            if(pk(l)=='='){lx_adv(l);return mktok(l,TOK_RANGE_EQ,"..=",3,line,col);}
            return mktok(l,TOK_RANGE,"..",2,line,col);
        }
        return mktok(l,TOK_DOT,".",1,line,col);
    case '?': return mktok(l,TOK_QUESTION,"?",1,line,col);
    case '#': return mktok(l,TOK_HASH,"#",1,line,col);
    case '@': return mktok(l,TOK_AT,"@",1,line,col);
    case '(': return mktok(l,TOK_LPAREN,"(",1,line,col);
    case ')': return mktok(l,TOK_RPAREN,")",1,line,col);
    case '{': return mktok(l,TOK_LBRACE,"{",1,line,col);
    case '}': return mktok(l,TOK_RBRACE,"}",1,line,col);
    case '[': return mktok(l,TOK_LBRACKET,"[",1,line,col);
    case ']': return mktok(l,TOK_RBRACKET,"]",1,line,col);
    default:  return mktok(l,TOK_UNKNOWN,&c,1,line,col);
    }
}
