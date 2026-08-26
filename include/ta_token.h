#ifndef TA_TOKEN_H
#define TA_TOKEN_H

#include "ta_common.h"

typedef enum {
    TK_EOF = 0,
    TK_NEWLINE,
    TK_INDENT,
    TK_DEDENT,

    TK_IDENT,
    TK_INT,
    TK_FLOAT,
    TK_STRING,
    TK_CHAR,

    TK_VAR,
    TK_CONST,
    TK_FUNC,
    TK_RETURN,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    TK_FOR,
    TK_IN,
    TK_BREAK,
    TK_CONTINUE,
    TK_TRUE,
    TK_FALSE,
    TK_NULL,
    TK_AND,
    TK_OR,
    TK_NOT,

    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_LBRACE,
    TK_RBRACE,
    TK_COLON,
    TK_COMMA,
    TK_DOT,
    TK_ARROW,
    TK_ASSIGN,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_PERCENT,
    TK_EQ,
    TK_NE,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,
    TK_PLUSEQ,
    TK_MINUSEQ,
    TK_STAREQ,
    TK_SLASHEQ
} TaTokenType;

typedef struct {
    TaTokenType type;
    int line;
    int col;
    char *text;
    size_t text_len;
    union {
        long long i;
        double f;
        uint32_t cp;
    } v;
} TaToken;

typedef struct {
    TaToken *items;
    size_t count;
    size_t cap;
} TaTokenList;

const char *ta_token_type_name(TaTokenType t);
const char *ta_token_type_display(TaTokenType t);
void ta_token_list_free(TaTokenList *list);

#endif
