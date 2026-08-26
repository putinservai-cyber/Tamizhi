#include "ta_ast.h"

TaTypeSpec *ta_typespec_name(const char *name, int line, int col) {
    TaTypeSpec *ts = ta_xcalloc(1, sizeof(TaTypeSpec));
    ts->kind = TS_NAME;
    ts->name = ta_xstrdup(name);
    ts->line = line;
    ts->col = col;
    return ts;
}

TaTypeSpec *ta_typespec_list(TaTypeSpec *elem, int line, int col) {
    TaTypeSpec *ts = ta_xcalloc(1, sizeof(TaTypeSpec));
    ts->kind = TS_LIST;
    ts->elem = elem;
    ts->line = line;
    ts->col = col;
    return ts;
}

TaTypeSpec *ta_typespec_dict(TaTypeSpec *k, TaTypeSpec *v, int line, int col) {
    TaTypeSpec *ts = ta_xcalloc(1, sizeof(TaTypeSpec));
    ts->kind = TS_DICT;
    ts->tk = k;
    ts->tv = v;
    ts->line = line;
    ts->col = col;
    return ts;
}

void ta_typespec_free(TaTypeSpec *ts) {
    if (!ts) return;
    free(ts->name);
    ta_typespec_free(ts->elem);
    ta_typespec_free(ts->tk);
    ta_typespec_free(ts->tv);
    free(ts);
}

const char *ta_binop_symbol(TaBinOp op) {
    switch (op) {
        case TB_ADD: return "+";
        case TB_SUB: return "-";
        case TB_MUL: return "*";
        case TB_DIV: return "/";
        case TB_MOD: return "%";
        case TB_EQ: return "==";
        case TB_NE: return "!=";
        case TB_LT: return "<";
        case TB_GT: return ">";
        case TB_LE: return "<=";
        case TB_GE: return ">=";
        case TB_AND: return "மற்றும்";
        case TB_OR: return "அல்லது";
    }
    return "?";
}

TaExpr *ta_expr_new(TaExprKind kind, int line, int col) {
    TaExpr *e = ta_xcalloc(1, sizeof(TaExpr));
    e->kind = kind;
    e->line = line;
    e->col = col;
    return e;
}

TaStmt *ta_stmt_new(TaStmtKind kind, int line, int col) {
    TaStmt *st = ta_xcalloc(1, sizeof(TaStmt));
    st->kind = kind;
    st->line = line;
    st->col = col;
    return st;
}

TaBlock *ta_block_new(void) {
    return ta_xcalloc(1, sizeof(TaBlock));
}

void ta_block_add(TaBlock *b, TaStmt *st) {
    if (b->count == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->items = ta_xrealloc(b->items, b->cap * sizeof(TaStmt *));
    }
    b->items[b->count++] = st;
}

void ta_program_add(TaProgram *p, TaStmt *st) {
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 8;
        p->items = ta_xrealloc(p->items, p->cap * sizeof(TaStmt *));
    }
    p->items[p->count++] = st;
}

void ta_expr_free(TaExpr *e) {
    if (!e) return;
    switch (e->kind) {
        case TX_STRING:
            free(e->as.str.sval);
            break;
        case TX_IDENT:
            free(e->as.name);
            break;
        case TX_BINARY:
            ta_expr_free(e->as.bin.lhs);
            ta_expr_free(e->as.bin.rhs);
            break;
        case TX_UNARY:
            ta_expr_free(e->as.un.operand);
            break;
        case TX_CALL:
            ta_expr_free(e->as.call.callee);
            for (size_t i = 0; i < e->as.call.nargs; i++) ta_expr_free(e->as.call.args[i]);
            free(e->as.call.args);
            break;
        case TX_INDEX:
            ta_expr_free(e->as.index.base);
            ta_expr_free(e->as.index.index);
            break;
        case TX_MEMBER:
            ta_expr_free(e->as.member.obj);
            free(e->as.member.member);
            break;
        case TX_LIST:
            for (size_t i = 0; i < e->as.list.count; i++) ta_expr_free(e->as.list.elems[i]);
            free(e->as.list.elems);
            break;
        case TX_DICT:
            for (size_t i = 0; i < e->as.dict.count; i++) {
                ta_expr_free(e->as.dict.keys[i]);
                ta_expr_free(e->as.dict.vals[i]);
            }
            free(e->as.dict.keys);
            free(e->as.dict.vals);
            break;
        default:
            break;
    }
    free(e);
}

void ta_stmt_free(TaStmt *st) {
    if (!st) return;
    switch (st->kind) {
        case ST_VARDECL:
            free(st->as.vardecl.name);
            ta_typespec_free(st->as.vardecl.type);
            ta_expr_free(st->as.vardecl.init);
            break;
        case ST_ASSIGN:
            ta_expr_free(st->as.assign.target);
            ta_expr_free(st->as.assign.value);
            break;
        case ST_EXPR:
            ta_expr_free(st->as.exprstmt.expr);
            break;
        case ST_IF:
            ta_expr_free(st->as.ifstmt.cond);
            ta_block_free(st->as.ifstmt.then_body);
            for (size_t i = 0; i < st->as.ifstmt.nelifs; i++) {
                ta_expr_free(st->as.ifstmt.elifs[i]->cond);
                ta_block_free(st->as.ifstmt.elifs[i]->body);
                free(st->as.ifstmt.elifs[i]);
            }
            free(st->as.ifstmt.elifs);
            ta_block_free(st->as.ifstmt.else_body);
            break;
        case ST_WHILE:
            ta_expr_free(st->as.whilestmt.cond);
            ta_block_free(st->as.whilestmt.body);
            break;
        case ST_FOREACH:
            free(st->as.foreach.varname);
            ta_expr_free(st->as.foreach.iterable);
            ta_block_free(st->as.foreach.body);
            break;
        case ST_RETURN:
            ta_expr_free(st->as.ret.value);
            break;
        case ST_FUNCDEF: {
            TaFuncDef *fd = st->as.funcdef;
            free(fd->name);
            for (size_t i = 0; i < fd->nparams; i++) {
                free(fd->params[i].name);
                ta_typespec_free(fd->params[i].type);
            }
            free(fd->params);
            ta_typespec_free(fd->ret_type);
            ta_block_free(fd->body);
            free(fd);
            break;
        }
        default:
            break;
    }
    free(st);
}

void ta_block_free(TaBlock *b) {
    if (!b) return;
    for (size_t i = 0; i < b->count; i++) ta_stmt_free(b->items[i]);
    free(b->items);
    free(b);
}

void ta_program_free(TaProgram *p) {
    for (size_t i = 0; i < p->count; i++) ta_stmt_free(p->items[i]);
    free(p->items);
    memset(p, 0, sizeof(*p));
}
