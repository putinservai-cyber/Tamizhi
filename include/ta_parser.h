#ifndef TA_PARSER_H
#define TA_PARSER_H

#include "ta_ast.h"
#include "ta_common.h"
#include "ta_token.h"

typedef struct {
    bool incomplete;
} TaParseResult;

TaProgram ta_parse_tokens(const char *filename, const TaTokenList *tokens,
                          TaDiagnostics *diag, TaParseResult *result);

#endif
