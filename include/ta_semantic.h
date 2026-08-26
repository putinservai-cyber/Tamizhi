#ifndef TA_SEMANTIC_H
#define TA_SEMANTIC_H

#include "ta_ast.h"
#include "ta_common.h"
#include "ta_types.h"

typedef enum {
    TA_SYM_VAR,
    TA_SYM_CONST,
    TA_SYM_PARAM,
    TA_SYM_FUNC,
    TA_SYM_MODULE,
    TA_SYM_TYPE,
    TA_SYM_BUILTIN_FUNC
} TaSymKind;

typedef enum {
    TA_BI_NONE = 0,
    TA_BI_PRINT,
    TA_BI_INPUT,
    TA_BI_LEN,
    TA_BI_RANGE,
    TA_BI_ABS,
    TA_BI_FLOOR,
    TA_BI_SQRT,
    TA_BI_POW,
    TA_BI_STR_CONCAT,
    TA_BI_STR_SUB,
    TA_BI_STR_SPLIT,
    TA_BI_STR_JOIN,
    TA_BI_STR_STRIP,
    TA_BI_STR_REPLACE,
    TA_BI_STR_UPPER,
    TA_BI_STR_LOWER,
    TA_BI_STR_STARTSWITH,
    TA_BI_STR_ENDSWITH
} TaBuiltinId;

typedef struct TaScope TaScope;

typedef struct TaSymbol {
    char *name;
    TaSymKind kind;
    const TaType *type;
    int slot;
    int line;
    int col;
    bool is_const;
    TaBuiltinId builtin_id;
    TaScope *members;
    struct {
        struct TaSymbol **params;
        size_t nparams;
        size_t pcap;
        const TaType *ret;
        bool has_annotation;
        int check_state;
        int index;
        bool is_main;
        struct TaStmt *decl;
        int nlocals;
    } fn;
} TaSymbol;

struct TaScope {
    struct TaScopeEntry {
        char *name;
        TaSymbol *sym;
        struct TaScopeEntry *next;
    } **buckets;
    size_t nbuckets;
    TaScope *parent;
};

TaScope *ta_scope_new(TaScope *parent);
void ta_scope_free(TaScope *scope);
TaSymbol *ta_scope_lookup_local(TaScope *s, const char *name);
TaSymbol *ta_scope_lookup(TaScope *s, const char *name);
bool ta_scope_declare(TaScope *s, TaSymbol *sym);

TaSymbol *ta_symbol_new(const char *name, TaSymKind kind, int line, int col);

const TaType *ta_resolve_type_spec(const TaTypeSpec *ts, TaScope *globals,
                                   TaDiagnostics *diag, const char *file);
const TaType *ta_lookup_named_type(TaScope *globals, const char *name);

bool ta_semantic_run(const char *file, TaProgram *prog, TaDiagnostics *diag,
                     TaScope **out_globals, int *out_top_slots);

#endif
