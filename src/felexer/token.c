#include <stdio.h>
#include "token.h"

const char *token_type_str(TokenType t){
    switch(t){
    case TOK_INT_LIT:    return "int_lit";
    case TOK_FLOAT_LIT:  return "float_lit";
    case TOK_DOUBLE_LIT: return "double_lit";
    case TOK_LONG_LIT:   return "long_lit";
    case TOK_STRING_LIT: return "string_lit";
    case TOK_CHAR_LIT:   return "char_lit";
    case TOK_BOOL_LIT:   return "bool_lit";
    case TOK_BYTE:       return "byte";
    case TOK_SHORT:      return "short";
    case TOK_USHORT:     return "ushort";
    case TOK_INT:        return "int";
    case TOK_UINT:       return "uint";
    case TOK_LONG:       return "long";
    case TOK_ULONG:      return "ulong";
    case TOK_FLOAT:      return "float";
    case TOK_DOUBLE:     return "double";
    case TOK_BOOL:       return "bool";
    case TOK_STR:        return "str";
    case TOK_CHAR:       return "char";
    case TOK_LET:        return "let";
    case TOK_MUT:        return "mut";
    case TOK_CONST:      return "const";
    case TOK_FN:         return "fn";
    case TOK_UNIT:       return "unit";
    case TOK_RETURN:     return "return";
    case TOK_IF:         return "if";
    case TOK_ELSE:       return "else";
    case TOK_WHILE:      return "while";
    case TOK_DO:         return "do";
    case TOK_FOR:        return "for";
    case TOK_MATCH:      return "match";
    case TOK_IMPORT:     return "import";
    case TOK_EXPORT:     return "export";
    case TOK_MODULE:     return "module";
    case TOK_STRUCT:     return "struct";
    case TOK_ENUM:       return "enum";
    case TOK_CLASS:      return "class";
    case TOK_PUBLIC:     return "public";
    case TOK_PRIVATE:    return "private";
    case TOK_THIS:       return "this";
    case TOK_NEW:        return "new";
    case TOK_TRUE:       return "true";
    case TOK_FALSE:      return "false";
    case TOK_IN:         return "in";
    case TOK_REGION:     return "region";
    case TOK_BREAK:      return "break";
    case TOK_CONTINUE:   return "continue";
    case TOK_TYPE:       return "type";
    case TOK_DEFER:      return "defer";
    case TOK_EXTERNAL:   return "external";
    case TOK_UNSAFE:     return "unsafe";
    case TOK_NULL:       return "null";
    case TOK_AT:         return "@";
    case TOK_COMPTIME:   return "comptime";
    case TOK_IDENT:      return "ident";
    case TOK_PLUS:       return "+";
    case TOK_MINUS:      return "-";
    case TOK_STAR:       return "*";
    case TOK_SLASH:      return "/";
    case TOK_PERCENT:    return "%";
    case TOK_AMP:        return "&";
    case TOK_PIPE:       return "|";
    case TOK_CARET:      return "^";
    case TOK_TILDE:      return "~";
    case TOK_ASSIGN:     return "=";
    case TOK_EQ:         return "==";
    case TOK_NEQ:        return "!=";
    case TOK_LT:         return "<";
    case TOK_GT:         return ">";
    case TOK_LTE:        return "<=";
    case TOK_GTE:        return ">=";
    case TOK_AND:        return "&&";
    case TOK_OR:         return "||";
    case TOK_NOT:        return "!";
    case TOK_ARROW:      return "->";
    case TOK_FAT_ARROW:  return "=>";
    case TOK_INC:        return "++";
    case TOK_DEC:        return "--";
    case TOK_COLON:      return ":";
    case TOK_DOUBLE_COLON: return "::";
    case TOK_SEMICOLON:  return ";";
    case TOK_COMMA:      return ",";
    case TOK_DOT:        return ".";
    case TOK_RANGE:      return "..";
    case TOK_RANGE_EQ:   return "..=";
    case TOK_QUESTION:   return "?";
    case TOK_HASH:       return "#";
    case TOK_AS:         return "as";
    case TOK_LPAREN:     return "(";
    case TOK_RPAREN:     return ")";
    case TOK_LBRACE:     return "{";
    case TOK_RBRACE:     return "}";
    case TOK_LBRACKET:   return "[";
    case TOK_RBRACKET:   return "]";
    case TOK_INTERP_START: return "%{";
    case TOK_EOF:        return "eof";
    default:             return "unknown";
    }
}

void token_print(Token *t){
    printf("[%s] '%s' @ %d:%d\n", token_type_str(t->type), t->value, t->line, t->col);
}
