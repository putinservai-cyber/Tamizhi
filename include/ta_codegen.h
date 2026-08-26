#ifndef TA_CODEGEN_H
#define TA_CODEGEN_H

#include "ta_common.h"
#include "ta_ir.h"

bool ta_codegen_emit(TaIrUnit *unit, TaStrBuf *out_asm);

#endif
