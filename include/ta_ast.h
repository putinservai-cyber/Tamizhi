#ifndef TA_AST_H
#define TA_AST_H

#include "ta_common.h"

typedef struct TaType TaType;
typedef struct TaSymbol TaSymbol;

typedef enum {
    TS_NAME,
    TS_LIST,
    TS_DICT
} TaTypeSpecKind;

typedef struct TaTypeSpec {
    TaTypeSpecKind kind;
    char *name;
    struct TaTypeSpec *elem;
    struct TaTypeSpec *tk;
    struct TaTypeSpec *tv;
    int line;
    int col;
} TaTypeSpec;

TaTypeSpec *ta_typespec_name(const char *name, int line, int col);
TaTypeSpec *ta_typespec_list(TaTypeSpec *elem, int line, int col);
TaTypeSpec *ta_typespec_dict(TaTypeSpec *k, TaTypeSpec *v, int line, int col);
void ta_typespec_free(TaTypeSpec *ts);

typedef enum {
    TX_INT,
    TX_FLOAT,
    TX_BOOL,
    TX_CHAR,
    TX_STRING,
    TX_NULL,
    TX_IDENT,
    TX_BINARY,
    TX_UNARY,
    TX_CALL,
    TX_INDEX,
    TX_MEMBER,
    TX_LIST,
    TX_DICT
} TaExprKind;

typedef enum {
    TB_ADD, TB_SUB, TB_MUL, TB_DIV, TB_MOD,
    TB_EQ, TB_NE, TB_LT, TB_GT, TB_LE, TB_GE,
    TB_AND, TB_OR
} TaBinOp;

typedef enum { TU_NEG, TU_NOT } TaUnOp;

typedef enum {
    TA_ASSIGN, TA_PLUSEQ, TA_MINUSEQ, TA_STAREQ, TA_SLASHEQ
} TaAssignOp;

const char *ta_binop_symbol(TaBinOp op);

typedef struct TaExpr TaExpr;
typedef struct TaStmt TaStmt;
typedef struct TaBlock TaBlock;

struct TaExpr {
    TaExprKind kind;
    int line;
    int col;
    const TaType *ty;
    TaSymbol *sym;
    union {
        long long ival;
        double fval;
        uint32_t cval;
        struct {
            char *sval;
            size_t slen;
        } str;
        char *name;
        struct {
            TaBinOp op;
            TaExpr *lhs;
            TaExpr *rhs;
        } bin;
        struct {
            TaUnOp op;
            TaExpr *operand;
        } un;
        struct {
            TaExpr *callee;
            TaExpr **args;
            size_t nargs;
        } call;
        struct {
            TaExpr *base;
            TaExpr *index;
        } index;
        struct {
            TaExpr *obj;
            char *member;
        } member;
        struct {
            TaExpr **elems;
            size_t count;
        } list;
        struct {
            TaExpr **keys;
            TaExpr **vals;
            size_t count;
        } dict;
    } as;
};

typedef enum {
    ST_VARDECL,
    ST_ASSIGN,
    ST_EXPR,
    ST_IF,
    ST_WHILE,
    ST_FOREACH,
    ST_RETURN,
    ST_BREAK,
    ST_CONTINUE,
    ST_FUNCDEF
} TaStmtKind;

typedef struct {
    char *name;
    TaTypeSpec *type;
    const TaType *resolved;
    int line;
    int col;
} TaParam;

typedef struct TaFuncDef {
    char *name;
    TaParam *params;
    size_t nparams;
    TaTypeSpec *ret_type;
    const TaType *ret_resolved;
    TaBlock *body;
} TaFuncDef;

struct TaStmt {
    TaStmtKind kind;
    int line;
    int col;
    union {
        struct {
            bool is_const;
            char *name;
            TaTypeSpec *type;
            const TaType *ann_type;
            TaExpr *init;
            TaSymbol *decl_sym;
        } vardecl;
        struct {
            TaExpr *target;
            TaAssignOp op;
            TaExpr *value;
        } assign;
        struct {
            TaExpr *expr;
        } exprstmt;
        struct {
            TaExpr *cond;
            TaBlock *then_body;
            struct {
                TaExpr *cond;
                TaBlock *body;
            } **elifs;
            size_t nelifs;
            TaBlock *else_body;
        } ifstmt;
        struct {
            TaExpr *cond;
            TaBlock *body;
        } whilestmt;
        struct {
            char *varname;
            TaExpr *iterable;
            TaBlock *body;
            int line_var;
            int col_var;
            TaSymbol *decl_sym;
        } foreach;
        struct {
            TaExpr *value;
        } ret;
        TaFuncDef *funcdef;
    } as;
};

struct TaBlock {
    TaStmt **items;
    size_t count;
    size_t cap;
};

typedef struct {
    TaStmt **items;
    size_t count;
    size_t cap;
} TaProgram;

TaExpr *ta_expr_new(TaExprKind kind, int line, int col);
TaStmt *ta_stmt_new(TaStmtKind kind, int line, int col);
TaBlock *ta_block_new(void);
void ta_block_add(TaBlock *b, TaStmt *st);
void ta_program_add(TaProgram *p, TaStmt *st);

void ta_expr_free(TaExpr *e);
void ta_stmt_free(TaStmt *st);
void ta_block_free(TaBlock *b);
void ta_program_free(TaProgram *p);

bool ta_stmt_is_simple(const TaStmt *st);
void ta_print_program(const TaProgram *p, TaStrBuf *out);
char *ta_format_source(const TaProgram *p);

#endif
