#ifndef TA_TYPECHECK_H
#define TA_TYPECHECK_H

#include "ta_ast.h"
#include "ta_common.h"
#include "ta_semantic.h"

bool ta_typecheck_run(const char *file, TaProgram *prog, TaScope *globals,
                      TaDiagnostics *diag);

#endif
