#ifndef TA_LEXER_H
#define TA_LEXER_H

#include "ta_common.h"
#include "ta_token.h"

typedef struct {
    bool incomplete;
} TaLexResult;

TaTokenList ta_lex_source(const char *filename, const char *src, TaDiagnostics *diag,
                          TaLexResult *result);

#endif
