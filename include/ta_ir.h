#ifndef TA_IR_H
#define TA_IR_H

#include "ta_ast.h"
#include "ta_common.h"
#include "ta_semantic.h"
#include "ta_types.h"

typedef enum {
    TA_OP_NONE,
    TA_OP_TEMP,
    TA_OP_SLOT,
    TA_OP_INT,
    TA_OP_FLOAT,
    TA_OP_STR
} TaOperandKind;

typedef struct {
    TaOperandKind kind;
    int idx;
    long long i;
    double f;
    int str_id;
} TaOperand;

typedef enum {
    TI_CONST,
    TI_LOAD,
    TI_STORE,
    TI_BINOP,
    TI_NEG,
    TI_NOT,
    TI_CONV_I2F,
    TI_CONV_F2I,
    TI_JMP,
    TI_JZ,
    TI_LABEL,
    TI_CALL,
    TI_RT,
    TI_RET,
    TI_LIST_NEW,
    TI_DICT_NEW,
    TI_IDX_GET,
    TI_IDX_SET,
    TI_LEN,
    TI_STR_AT,
    TI_DEREF
} TaIrOp;

typedef struct TaIrInstr {
    TaIrOp op;
    int binop;
    int label;
    const TaType *ty;
    TaOperand dst;
    TaOperand *args;
    size_t nargs;
    TaSymbol *callee;
    const char *rt_name;
} TaIrInstr;

typedef struct TaIrFunc {
    char *label;
    TaSymbol *sym;
    TaIrInstr *items;
    size_t count;
    size_t cap;
    int nslots;
    int nparams;
    const TaType *ret;
} TaIrFunc;

typedef struct {
    TaIrFunc **funcs;
    size_t nfuncs;
    char **strings;
    size_t *string_lens;
    size_t nstrings;
    TaIrFunc *top;
    TaSymbol *main_sym;
} TaIrUnit;

TaIrUnit *ta_ir_generate(const char *file, TaProgram *prog, TaScope *globals,
                         int top_slots, TaDiagnostics *diag, bool echo_top_exprs);
void ta_ir_unit_free(TaIrUnit *u);

/* Emit portable C11 source (complements the x86-64 code generator). */
void ta_ir_emit_c(const TaIrUnit *unit, TaStrBuf *out);

int ta_ir_add_string(TaIrUnit *u, const char *data, size_t len);

#endif
