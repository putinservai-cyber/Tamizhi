#include "ta_typecheck.h"

typedef struct {
    TaDiagnostics *diag;
    const char *file;
    TaScope *globals;
    TaSymbol *cur_func;
    const TaType **ret_types;
    size_t nrets;
    size_t retcap;
} TcCtx;

static void tc_error(TcCtx *c, int code, int line, int col, const char *msg,
                     const char *hint_fmt, ...) {
    va_list ap;
    va_start(ap, hint_fmt);
    ta_diag_report_v(c->diag, code, c->file, line, col, msg, hint_fmt, ap);
    va_end(ap);
}

static bool wildcard_ok(const TaType *sub, const TaType *sup) {
    if (!sup || !sub) return false;
    if (ta_ty_equal(sub, sup)) return true;
    if (sub->kind == TY_INT && sup->kind == TY_FLOAT) return true;
    if (sub->kind == TY_UNKNOWN || sup->kind == TY_UNKNOWN) {
        if (sub->kind == TY_LIST && sup->kind == TY_LIST) return true;
        if (sub->kind == TY_DICT && sup->kind == TY_DICT) return true;
        if (sub->kind == sup->kind) return true;
        return sub->kind == TY_UNKNOWN && sup->kind != TY_VOID;
    }
    return false;
}

static bool assignable(const TaType *src, const TaType *dst) {
    if (!dst || !src) return false;
    if (dst->kind == TY_ERROR || src->kind == TY_ERROR) return true;
    if (wildcard_ok(src, dst)) return true;
    if (ta_ty_equal(src, dst)) return true;
    return false;
}

static const TaType *unify(TcCtx *c, const TaType *a, const TaType *b, int line, int col,
                           int err_code, const char *what) {
    if (!a || a->kind == TY_ERROR) return b ? b : ta_ty_error();
    if (!b || b->kind == TY_ERROR) return a;
    if (ta_ty_equal(a, b)) return a;
    if (a->kind == TY_UNKNOWN) return b;
    if (b->kind == TY_UNKNOWN) return a;
    if (a->kind == TY_INT && b->kind == TY_FLOAT) return ta_ty_float();
    if (a->kind == TY_FLOAT && b->kind == TY_INT) return ta_ty_float();
    tc_error(c, err_code, line, col, what, "%s \u0bae\u0bb1\u0bcd\u0bb1\u0bc1\u0bae\u0bcd %s \u0bb5\u0b95\u0bc8\u0b95\u0bb3\u0bc8 \u0b87\u0ba3\u0bc8\u0b95\u0bcd\u0b95 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8",
             ta_ty_name(a), ta_ty_name(b));
    return ta_ty_error();
}

static bool is_numeric(const TaType *t) {
    return t && (t->kind == TY_INT || t->kind == TY_FLOAT);
}

static bool is_printable(const TaType *t) {
    if (!t) return false;
    switch (t->kind) {
        case TY_INT:
        case TY_FLOAT:
        case TY_BOOL:
        case TY_CHAR:
        case TY_STRING: return true;
        default: return false;
    }
}

static bool is_key_type(const TaType *t) {
    return t && (t->kind == TY_STRING || t->kind == TY_INT);
}

static const TaType *check_expr(TcCtx *c, TaExpr *e);

static void check_block(TcCtx *c, TaBlock *b);
static void check_stmt(TcCtx *c, TaStmt *st);
static void ensure_func_checked(TcCtx *c, TaSymbol *fsym);

static const TaType *builtin_result_type(TcCtx *c, TaSymbol *sym, TaExpr **args, size_t nargs,
                                         int line, int col) {
    switch (sym->builtin_id) {
        case TA_BI_PRINT:
            for (size_t i = 0; i < nargs; i++) {
                const TaType *t = check_expr(c, args[i]);
                if (!is_printable(t)) {
                    tc_error(c, TA_ERR_TYPE + 4, args[i]->line, args[i]->col,
                             "\u0b85\u0b9a\u0bcd\u0b9a\u0bbf\u0b9f\u0bc1() \u0b87\u0bb2\u0bcd %s \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0ba9\u0bc8 \u0b85\u0b9a\u0bcd\u0b9a\u0bbf\u0b9f \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                             "\u0b8e\u0ba3\u0bcd, \u0bae\u0bbf\u0ba4\u0bb5\u0bc8, \u0baa\u0bc2\u0bb2\u0bbf\u0baf\u0ba9\u0bcd, \u0b8e\u0bb4\u0bc1\u0ba4\u0bcd\u0ba4\u0bc1, \u0b89\u0bb0\u0bc8 \u0b86\u0b95\u0bbf\u0baf\u0bb5\u0bc8 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 \u0b85\u0b9a\u0bcd\u0b9a\u0bbf\u0b9f\u0b95\u0bcd\u0b95\u0bc2\u0b9f\u0bbf\u0baf\u0bb5\u0bc8",
                             ta_ty_name(t));
                }
            }
            return ta_ty_void();
        case TA_BI_INPUT:
            if (nargs != 0) {
                tc_error(c, TA_ERR_TYPE + 3, line, col,
                         "\u0b89\u0bb3\u0bcd\u0bb3\u0bc0\u0b9f\u0bc1() \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bc1 \u0b87\u0bb2\u0b99\u0bcd\u0b95\u0bc8 \u0b8e\u0ba4\u0bbf\u0bb0\u0bcd\u0baa\u0bbe\u0bb0\u0bcd\u0b95\u0bcd\u0b95\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8",
                         "\u0b89\u0bb3\u0bcd\u0bb3\u0bc0\u0b9f\u0bc1() \u0b8e\u0ba9 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bcd \u0b85\u0bb4\u0bc8\u0b95\u0bcd\u0b95\u0bb5\u0bc1\u0bae\u0bcd");
            }
            return ta_ty_string();
        case TA_BI_LEN: {
            if (nargs != 1) {
                tc_error(c, TA_ERR_TYPE + 3, line, col,
                         "\u0ba8\u0bc0\u0bb3\u0bae\u0bcd() \u0b95\u0bcd\u0b95\u0bc1 \u0b92\u0bb0\u0bc1 \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bcd \u0ba4\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                         "\u0b8e.\u0b95.: \u0ba8\u0bc0\u0bb3\u0bae\u0bcd(\u0b89\u0bb0\u0bc8), \u0ba8\u0bc0\u0bb3\u0bae\u0bcd(\u0baa\u0b9f\u0bcd\u0b9f\u0bbf\u0baf\u0bb2\u0bcd)");
                return ta_ty_int();
            }
            const TaType *t = check_expr(c, args[0]);
            if (t->kind != TY_STRING && t->kind != TY_LIST && t->kind != TY_ERROR &&
                t->kind != TY_UNKNOWN) {
                tc_error(c, TA_ERR_TYPE + 4, args[0]->line, args[0]->col,
                         "\u0ba8\u0bc0\u0bb3\u0bae\u0bcd(): %s \u0bb5\u0b95\u0bc8\u0b95\u0bcd\u0b95\u0bc1 \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                         "\u0b89\u0bb0\u0bc8 \u0b85\u0bb2\u0bcd\u0bb2\u0ba4\u0bc1 \u0baa\u0b9f\u0bcd\u0b9f\u0bbf\u0baf\u0bb2\u0bcd \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bcd", ta_ty_name(t));
            }
            return ta_ty_int();
        }
        case TA_BI_RANGE: {
            if (nargs < 1 || nargs > 3) {
                tc_error(c, TA_ERR_TYPE + 3, line, col,
                         "\u0bb5\u0bb0\u0bae\u0bcd\u0baa\u0bc1() \u0b95\u0bcd\u0b95\u0bc1 1 \u0bae\u0bc1\u0ba4\u0bb2\u0bcd 3 \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb3\u0bcd",
                         "\u0bb5\u0bb0\u0bae\u0bcd\u0baa\u0bc1(n), \u0bb5\u0bb0\u0bae\u0bcd\u0baa\u0bc1(\u0ba4\u0bca\u0b9f\u0b95\u0bcd\u0b95\u0bae\u0bcd, \u0bae\u0bc1\u0bb1\u0bc1\u0ba4\u0bcd\u0ba4\u0bae\u0bcd), \u0bb5\u0bb0\u0bae\u0bcd\u0baa\u0bc1(\u0ba4\u0bca\u0b9f\u0b95\u0bcd\u0b95\u0bae\u0bcd, \u0bae\u0bc1\u0bb1\u0bc1\u0ba4\u0bcd\u0ba4\u0bae\u0bcd, \u0b87\u0b9f\u0bc8\u0bb5\u0bc6\u0bb3\u0bbf)");
                return ta_ty_list(ta_ty_int());
            }
            for (size_t i = 0; i < nargs; i++) {
                const TaType *t = check_expr(c, args[i]);
                if (!assignable(t, ta_ty_int())) {
                    tc_error(c, TA_ERR_TYPE + 4, args[i]->line, args[i]->col,
                             "\u0bb5\u0bb0\u0bae\u0bcd\u0baa\u0bc1() \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bc1\u0b95\u0bb3\u0bcd \u0bae\u0bc1\u0bb4\u0bc1\u0b8e\u0ba3\u0bcd \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0bb2\u0bcd \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95 \u0bb5\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                             "%s \u0b95\u0bcd\u0b95\u0baa\u0ba4\u0bbf\u0bb2\u0bbe\u0b95 \u0bae\u0bc1\u0bb4\u0bc1\u0b8e\u0ba3\u0bcd \u0b95\u0bca\u0b9f\u0bc1\u0b99\u0bcd\u0b95\u0bb3\u0bcd", ta_ty_name(t));
                }
            }
            return ta_ty_list(ta_ty_int());
        }
        case TA_BI_ABS: {
            if (nargs != 1) {
                tc_error(c, TA_ERR_TYPE + 3, line, col,
                         "\u0ba4\u0ba9\u0bbf\u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc1() \u0b95\u0bcd\u0b95\u0bc1 \u0b92\u0bb0\u0bc1 \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1",
                         "\u0b8e.\u0b95.: \u0b95\u0ba3\u0bbf\u0ba4\u0bae\u0bcd.\u0ba4\u0ba9\u0bbf\u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc1(-5)");
                return ta_ty_error();
            }
            const TaType *t = check_expr(c, args[0]);
            if (!is_numeric(t)) {
                tc_error(c, TA_ERR_TYPE + 4, args[0]->line, args[0]->col,
                         "\u0ba4\u0ba9\u0bbf\u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc1(): \u0b8e\u0ba3\u0bcd \u0ba4\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                         "%s \u0bb5\u0b95\u0bc8 \u0b86\u0ba4\u0bb0\u0bbf\u0b95\u0bcd\u0b95\u0baa\u0bcd\u0baa\u0b9f\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8",
                         ta_ty_name(t));
                return ta_ty_error();
            }
            return t->kind == TY_INT ? ta_ty_int() : ta_ty_float();
        }
        case TA_BI_POW: {
            if (nargs != 2) {
                tc_error(c, TA_ERR_TYPE + 3, line, col,
                         "\u0b9a\u0b95\u0bcd\u0ba4\u0bbf() \u0b95\u0bcd\u0b95\u0bc1 \u0b87\u0bb0\u0ba3\u0bcd\u0b9f\u0bc1 \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb3\u0bcd",
                         "\u0b8e.\u0b95.: \u0b95\u0ba3\u0bbf\u0ba4\u0bae\u0bcd.\u0b9a\u0b95\u0bcd\u0ba4\u0bbf(2, 10)");
                return ta_ty_error();
            }
            const TaType *t0 = check_expr(c, args[0]);
            const TaType *t1 = check_expr(c, args[1]);
            if (!is_numeric(t0) || !is_numeric(t1)) {
                tc_error(c, TA_ERR_TYPE + 4, line, col,
                         "\u0b9a\u0b95\u0bcd\u0ba4\u0bbf(): \u0b8e\u0ba3\u0bcd\u0b95\u0bb3\u0bcd \u0ba4\u0bc7\u0ba9\u0bcd\u0bb1\u0bc1\u0bae\u0bcd",
                         "\u0b87\u0bb0\u0bc1 \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb3\u0bc1\u0bae\u0bcd \u0b8e\u0ba3\u0bcd\u0b95\u0bb3\u0bbe\u0b95 \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb5\u0bc1\u0bae\u0bcd");
                return ta_ty_error();
            }
            bool both_int = t0->kind == TY_INT && t1->kind == TY_INT;
            return both_int ? ta_ty_int() : ta_ty_float();
        }
        default:
            break;
    }

    size_t np = sym->fn.nparams;
    if (nargs != np) {
        tc_error(c, TA_ERR_TYPE + 3, line, col,
                 "'%s' \u0b9a\u0bc6\u0baf\u0bb2\u0bbf\u0b95\u0bcd\u0b95\u0bc1 %zu \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb3\u0bcd; %zu \u0b95\u0bca\u0b9f\u0bc1\u0b95\u0bcd\u0b95\u0baa\u0bcd\u0baa\u0b9f\u0bcd\u0b9f\u0ba9",
                 "\u0b9a\u0bc6\u0baf\u0bb2\u0bbf \u0bb5\u0bb0\u0bc8\u0baf\u0bb1\u0bc1\u0baa\u0bcd\u0baa\u0bc8 \u0b9a\u0bb0\u0bbf\u0baa\u0bbe\u0bb0\u0bcd\u0b95\u0bcd\u0b95\u0bb5\u0bc1\u0bae\u0bcd",
                 sym->name, np, nargs);
        return sym->fn.ret ? sym->fn.ret : ta_ty_error();
    }
    for (size_t i = 0; i < nargs; i++) {
        const TaType *at = check_expr(c, args[i]);
        const TaType *pt = sym->fn.params[i] ? sym->fn.params[i]->type : NULL;
        if (pt && !assignable(at, pt)) {
            tc_error(c, TA_ERR_TYPE + 4, args[i]->line, args[i]->col,
                     "'%s' \u0b87\u0ba9\u0bcd %zu-\u0bae\u0bcd \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1 \u0bb5\u0b95\u0bc8 \u0ba4\u0bb5\u0bb1\u0bc1: %s \u0b8e\u0ba4\u0bbf\u0bb0\u0bcd\u0baa\u0bbe\u0bb0\u0bcd\u0baa\u0bcd\u0baa\u0bc1, %s \u0b95\u0bbf\u0b9f\u0bc8\u0ba4\u0bcd\u0ba4\u0ba4\u0bc1",
                     "\u0b9a\u0bb0\u0bbf\u0baf \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0bb2\u0bbe\u0ba9 \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc8\u0b95\u0bcd \u0b95\u0bca\u0b9f\u0bc1\u0b99\u0bcd\u0b95\u0bb3\u0bcd",
                     sym->name, i + 1, ta_ty_name(pt), ta_ty_name(at));
        }
    }
    return sym->fn.ret ? sym->fn.ret : ta_ty_error();
}

static bool contains_unknown(const TaType *t) {
    if (!t) return true;
    switch (t->kind) {
        case TY_UNKNOWN: return true;
        case TY_LIST: return contains_unknown(t->elem);
        case TY_DICT: return contains_unknown(t->key) || contains_unknown(t->val);
        default: return false;
    }
}

static const TaType *index_result_type(TcCtx *c, TaExpr *e) {
    TaExpr *base = e->as.index.base;
    TaExpr *idx = e->as.index.index;
    const TaType *bt = check_expr(c, base);
    const TaType *it = check_expr(c, idx);
    switch (bt->kind) {
        case TY_ERROR:
            return ta_ty_error();
        case TY_LIST:
            if (!assignable(it, ta_ty_int())) {
                tc_error(c, TA_ERR_TYPE + 7, idx->line, idx->col,
                         "\u0baa\u0b9f\u0bcd\u0b9f\u0bbf\u0baf\u0bb2\u0bcd \u0b85\u0b9f\u0bc1\u0baf\u0bbe\u0ba3\u0bb5\u0bc1 \u0b8e\u0ba3\u0bcd (\u0bae\u0bc1\u0bb4\u0bc1\u0b8e\u0ba3\u0bcd) \u0b86\u0b95 \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95 \u0bb5\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                         "\u0b8e.\u0b95.: \u0b8e\u0ba3\u0bcd\u0b95\u0bb3\u0bcd[0] \u2014 \u0baa\u0b9f\u0bcd\u0b9f\u0bbf\u0baf\u0bb2\u0bcd 0 \u0b87\u0bb2\u0bcd \u0ba4\u0bca\u0b9f\u0b99\u0bcd\u0b95\u0bc1\u0bae\u0bcd",
                         ta_ty_name(it));
            }
            return bt->elem ? bt->elem : ta_ty_unknown();
        case TY_DICT:
            if (bt->key && !assignable(it, bt->key)) {
                tc_error(c, TA_ERR_TYPE + 8, idx->line, idx->col,
                         "\u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf \u0b9a\u0bbe\u0bb5\u0bbf \u0bb5\u0b95\u0bc8 \u0ba4\u0bb5\u0bb1\u0bc1: %s \u0b8e\u0ba4\u0bbf\u0bb0\u0bcd\u0baa\u0bbe\u0bb0\u0bcd\u0baa\u0bcd\u0baa\u0bc1, %s \u0b95\u0bbf\u0b9f\u0bc8\u0ba4\u0bcd\u0ba4\u0ba4\u0bc1",
                         "\u0b9a\u0bbe\u0bb5\u0bbf \u0b89\u0bb0\u0bc8 \u0b85\u0bb2\u0bcd\u0bb2\u0ba4\u0bc1 \u0bae\u0bc1\u0bb4\u0bc1\u0b8e\u0ba3\u0bcd \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0bb2\u0bcd \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95 \u0bb5\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                         ta_ty_name(bt->key), ta_ty_name(it));
                return ta_ty_error();
            }
            return bt->val ? bt->val : ta_ty_unknown();
        case TY_STRING:
            if (!assignable(it, ta_ty_int())) {
                tc_error(c, TA_ERR_TYPE + 7, idx->line, idx->col,
                         "\u0b89\u0bb0\u0bc8 \u0b85\u0b9f\u0bc1\u0baf\u0bbe\u0ba3\u0bb5\u0bc1 \u0b8e\u0ba3\u0bcd \u0b86\u0b95 \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95 \u0bb5\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                         "\u0b8e.\u0b95.: \u0b89\u0bb0\u0bc8[0] \u0b86\u0ba9\u0ba4\u0bc1 \u0bae\u0bc1\u0ba4\u0bb2\u0bcd \u0b8e\u0bb4\u0bc1\u0ba4\u0bcd\u0ba4\u0bc1");
                return ta_ty_error();
            }
            return ta_ty_char();
        default:
            tc_error(c, TA_ERR_TYPE + 6, e->line, e->col,
                     "%s \u0bb5\u0b95\u0bc8\u0baf\u0bc8 [..] \u0bae\u0bc1\u0bb1\u0bc8\u0baf\u0bbf\u0bb2\u0bcd \u0b85\u0ba3\u0bc1\u0b95 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                     "\u0baa\u0b9f\u0bcd\u0b9f\u0bbf\u0baf\u0bb2\u0bcd, \u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf, \u0b89\u0bb0\u0bc8 \u0b86\u0b95\u0bbf\u0baf\u0bb5\u0bb1\u0bcd\u0bb1\u0bc8 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 \u0b85\u0ba3\u0bc1\u0b95 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bc1\u0bae\u0bcd",
                     ta_ty_name(bt));
            return ta_ty_error();
    }
}

static const TaType *arith_combine(const TaType *a, const TaType *b) {
    if (is_numeric(a) && is_numeric(b))
        return (a->kind == TY_FLOAT || b->kind == TY_FLOAT) ? ta_ty_float() : ta_ty_int();
    return NULL;
}

static const TaType *check_expr(TcCtx *c, TaExpr *e) {
    if (!e) return ta_ty_void();
    if (e->ty) return e->ty;
    switch (e->kind) {
        case TX_INT: e->ty = ta_ty_int(); break;
        case TX_FLOAT: e->ty = ta_ty_float(); break;
        case TX_BOOL: e->ty = ta_ty_bool(); break;
        case TX_CHAR: e->ty = ta_ty_char(); break;
        case TX_STRING: e->ty = ta_ty_string(); break;
        case TX_NULL: e->ty = ta_ty_void(); break;
        case TX_IDENT: {
            TaSymbol *sym = e->sym;
            if (!sym) {
                e->ty = ta_ty_error();
                break;
            }
            if (sym->kind == TA_SYM_FUNC || sym->kind == TA_SYM_BUILTIN_FUNC) {
                tc_error(c, TA_ERR_TYPE + 5, e->line, e->col,
                         "\u0b9a\u0bc6\u0baf\u0bb2\u0bbf '%s' \u0baa\u0bc6\u0baf\u0bb0\u0bc8 \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bbe\u0b95\u0baa\u0bcd \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                         "'%s(...)' \u0b8e\u0ba9\u0bcd\u0bb1\u0bc1 \u0b85\u0bb4\u0bc8\u0ba4\u0bcd\u0ba4\u0bc1 \u0b85\u0ba4\u0ba9\u0bcd \u0bae\u0bc1\u0b9f\u0bbf\u0bb5\u0bc8\u0baa\u0bcd \u0baa\u0bc6\u0bb1\u0bb5\u0bc1\u0bae\u0bcd",
                         sym->name);
                e->ty = ta_ty_error();
                break;
            }
            if (!sym->type) {
                tc_error(c, TA_ERR_SEMANTIC + 10, e->line, e->col,
                         "'%s' \u0b85\u0bb1\u0bbf\u0bb5\u0bbf\u0b95\u0bcd\u0b95\u0baa\u0bcd\u0baa\u0b9f\u0bc1\u0bb5\u0ba4\u0bb1\u0bcd\u0b95\u0bc1 \u0bae\u0bc1\u0ba9\u0bcd\u0baa\u0bc7 \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4\u0baa\u0bcd\u0baa\u0b9f\u0bcd\u0b9f\u0ba4\u0bc1",
                         "\u0b92\u0bb0\u0bc1 \u0bae\u0bbe\u0bb1\u0bbf\u0baf\u0bc8 \u0b85\u0bb1\u0bbf\u0bb5\u0bbf\u0baa\u0bcd\u0baa\u0ba4\u0bb1\u0bcd\u0b95\u0bc1 \u0bae\u0bc1\u0ba9\u0bcd \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                         sym->name);
                e->ty = ta_ty_error();
                break;
            }
            e->ty = sym->type;
            break;
        }
        case TX_BINARY: {
            const TaType *lt = check_expr(c, e->as.bin.lhs);
            const TaType *rt = check_expr(c, e->as.bin.rhs);
            TaBinOp op = e->as.bin.op;
            if (lt->kind == TY_ERROR || rt->kind == TY_ERROR) {
                e->ty = ta_ty_error();
                break;
            }
            switch (op) {
                case TB_ADD:
                    if (lt->kind == TY_STRING && rt->kind == TY_STRING) {
                        e->ty = ta_ty_string();
                        break;
                    }
                    /* fall through */
                case TB_SUB:
                case TB_MUL:
                case TB_DIV:
                case TB_MOD: {
                    const TaType *res = arith_combine(lt, rt);
                    if (res) {
                        e->ty = res;
                        break;
                    }
                    tc_error(c, TA_ERR_TYPE + 1, e->line, e->col,
                             "'%s' \u0b90 %s \u0bae\u0bb1\u0bcd\u0bb1\u0bc1\u0bae\u0bcd %s \u0bb5\u0b95\u0bc8\u0b95\u0bb3\u0bbf\u0bb2\u0bcd \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                             "\u0b87\u0bb0\u0bc1 \u0baa\u0b95\u0bcd\u0b95\u0b99\u0bcd\u0b95\u0bb3\u0bc1\u0bae\u0bcd \u0b8e\u0ba3\u0bcd\u0b95\u0bb3\u0bbe\u0b95 \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb5\u0bc1\u0bae\u0bcd; \u0b89\u0bb0\u0bc8\u0b95\u0bb3\u0bc8 '+' \u0b86\u0bb2\u0bcd \u0b87\u0ba3\u0bc8\u0b95\u0bcd\u0b95\u0bb2\u0bbe\u0bae\u0bcd",
                             ta_binop_symbol(op), ta_ty_name(lt), ta_ty_name(rt));
                    e->ty = ta_ty_error();
                    break;
                }
                case TB_EQ:
                case TB_NE: {
                    bool ok = (is_numeric(lt) && is_numeric(rt)) ||
                              (lt->kind == TY_STRING && rt->kind == TY_STRING) ||
                              (lt->kind == TY_BOOL && rt->kind == TY_BOOL) ||
                              (lt->kind == TY_CHAR && rt->kind == TY_CHAR);
                    if (!ok) {
                        tc_error(c, TA_ERR_TYPE + 1, e->line, e->col,
                                 "%s \u0bae\u0bb1\u0bcd\u0bb1\u0bc1\u0bae\u0bcd %s \u0bb5\u0b95\u0bc8\u0b95\u0bb3\u0bc8 '%s' \u0b86\u0bb2\u0bcd \u0b92\u0baa\u0bcd\u0baa\u0bbf\u0b9f \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                                 "\u0b92\u0bb0\u0bc7 \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0ba9 \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc1\u0b95\u0bb3\u0bc8 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bcd \u0b92\u0baa\u0bcd\u0baa\u0bbf\u0b9f \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bc1\u0bae\u0bcd",
                                 ta_ty_name(lt), ta_ty_name(rt), ta_binop_symbol(op));
                        e->ty = ta_ty_error();
                    } else {
                        e->ty = ta_ty_bool();
                    }
                    break;
                }
                case TB_LT:
                case TB_GT:
                case TB_LE:
                case TB_GE: {
                    bool ok = (is_numeric(lt) && is_numeric(rt)) ||
                              (lt->kind == TY_STRING && rt->kind == TY_STRING);
                    if (!ok) {
                        tc_error(c, TA_ERR_TYPE + 1, e->line, e->col,
                                 "'%s' \u0b92\u0baa\u0bcd\u0baa\u0bc0\u0b9f\u0bcd\u0b9f\u0bc1\u0b95\u0bcd\u0b95\u0bc1 \u0b8e\u0ba3\u0bcd\u0b95\u0bb3\u0bcd \u0b85\u0bb2\u0bcd\u0bb2\u0ba4\u0bc1 \u0b89\u0bb0\u0bc8 \u0ba4\u0bc7\u0bb5\u0bc8; %s, %s \u0b95\u0bbf\u0b9f\u0bc8\u0ba4\u0bcd\u0ba4\u0ba4\u0bc1",
                                 "\u0b8e\u0ba3\u0bcd\u0b95\u0bb3\u0bcd/\u0b89\u0bb0\u0bc8\u0b95\u0bb3\u0bc8 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 \u0bb5\u0bb0\u0bbf\u0b9a\u0bc8\u0baf\u0bbe\u0b95 \u0b92\u0baa\u0bcd\u0baa\u0bbf\u0b9f \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bc1\u0bae\u0bcd",
                                 ta_binop_symbol(op), ta_ty_name(lt), ta_ty_name(rt));
                        e->ty = ta_ty_error();
                    } else {
                        e->ty = ta_ty_bool();
                    }
                    break;
                }
                case TB_AND:
                case TB_OR:
                    if (lt->kind != TY_BOOL || rt->kind != TY_BOOL) {
                        tc_error(c, TA_ERR_TYPE + 1, e->line, e->col,
                                 "'%s' \u0b95\u0bcd\u0b95\u0bc1 \u0b87\u0bb0\u0bc1 \u0baa\u0b95\u0bcd\u0b95\u0b99\u0bcd\u0b95\u0bb3\u0bc1\u0bae\u0bcd \u0baa\u0bc2\u0bb2\u0bbf\u0baf\u0ba9\u0bcd \u0bb5\u0b95\u0bc8\u0baf\u0bbe\u0b95 \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95 \u0bb5\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                                 "\u0b89\u0ba3\u0bcd\u0bae\u0bc8/\u0baa\u0bca\u0baf\u0bcd \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc1\u0b95\u0bb3\u0bc8 \u0b9a\u0bc7\u0bb0\u0bcd\u0b95\u0bcd\u0b95 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 '\u0bae\u0bb1\u0bcd\u0bb1\u0bc1\u0bae\u0bcd'/'\u0b85\u0bb2\u0bcd\u0bb2\u0ba4\u0bc1' \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0bae\u0bcd",
                                 ta_binop_symbol(op));
                        e->ty = ta_ty_error();
                    } else {
                        e->ty = ta_ty_bool();
                    }
                    break;
            }
            break;
        }
        case TX_UNARY: {
            const TaType *t = check_expr(c, e->as.un.operand);
            if (t->kind == TY_ERROR) {
                e->ty = ta_ty_error();
                break;
            }
            if (e->as.un.op == TU_NEG) {
                if (!is_numeric(t)) {
                    tc_error(c, TA_ERR_TYPE + 1, e->line, e->col,
                             "'-' \u0b95\u0bc1\u0bb1\u0bbf\u0baf\u0bbf\u0b9f\u0bbf\u0bb1\u0bcd\u0b95\u0bc1 \u0b8e\u0ba3\u0bcd \u0ba4\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd; %s \u0b95\u0bbf\u0b9f\u0bc8\u0ba4\u0bcd\u0ba4\u0ba4\u0bc1",
                             "\u0b95\u0bc2\u0b9f\u0bcd\u0b9f\u0bb2\u0bcd \u0b8e\u0ba3\u0bcd\u0ba3\u0bc8 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 '-' \u0b95\u0bc1\u0bb1\u0bbf\u0baf\u0bbf\u0b9f\u0bcd\u0b9f\u0bc1 \u0b8e\u0bb4\u0bc1\u0ba4\u0bb2\u0bbe\u0bae\u0bcd",
                             ta_ty_name(t));
                    e->ty = ta_ty_error();
                } else {
                    e->ty = t;
                }
            } else {
                if (t->kind != TY_BOOL) {
                    tc_error(c, TA_ERR_TYPE + 1, e->line, e->col,
                             "'\u0b87\u0bb2\u0bcd\u0bb2\u0bc8' \u0b95\u0bcd\u0b95\u0bc1 \u0baa\u0bc2\u0bb2\u0bbf\u0baf\u0ba9\u0bcd \u0ba4\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd; %s \u0b95\u0bbf\u0b9f\u0bc8\u0ba4\u0bcd\u0ba4\u0ba4\u0bc1",
                             "\u0b89\u0ba3\u0bcd\u0bae\u0bc8/\u0baa\u0bca\u0baf\u0bcd \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc8 \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 '\u0b87\u0bb2\u0bcd\u0bb2\u0bc8' \u0b86\u0bb2\u0bcd \u0bae\u0b9f\u0b95\u0bcd\u0b95\u0bb2\u0bbe\u0bae\u0bcd",
                             ta_ty_name(t));
                    e->ty = ta_ty_error();
                } else {
                    e->ty = ta_ty_bool();
                }
            }
            break;
        }
        case TX_CALL: {
            TaExpr *cal = e->as.call.callee;
            TaSymbol *sym = cal->sym;
            if (!sym || (sym->kind != TA_SYM_FUNC && sym->kind != TA_SYM_BUILTIN_FUNC)) {
                tc_error(c, TA_ERR_TYPE + 5, e->line, e->col,
                         "\u0b87\u0ba4\u0bc8\u0b9a\u0bcd \u0b9a\u0bc6\u0baf\u0bb2\u0bbf\u0baf\u0bbe\u0b95 \u0b85\u0bb4\u0bc8\u0b95\u0bcd\u0b95 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                         "\u0b9a\u0bc6\u0baf\u0bb2\u0bbf \u0baa\u0bc6\u0baf\u0bb0\u0bcd\u0b95\u0bb3\u0bcd \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bc7 '(...)' \u0b89\u0b9f\u0ba9\u0bcd \u0b85\u0bb4\u0bc8\u0b95\u0bcd\u0b95\u0baa\u0bcd\u0baa\u0b9f\u0bc1\u0bae\u0bcd");
                for (size_t i = 0; i < e->as.call.nargs; i++) check_expr(c, e->as.call.args[i]);
                e->ty = ta_ty_error();
                break;
            }
            if (sym->kind == TA_SYM_BUILTIN_FUNC) {
                e->ty = builtin_result_type(c, sym, e->as.call.args, e->as.call.nargs,
                                            e->line, e->col);
                break;
            }
            for (size_t i = 0; i < e->as.call.nargs; i++) {
                const TaType *at0 = check_expr(c, e->as.call.args[i]);
                (void)at0;
            }
            ensure_func_checked(c, sym);
            if (e->as.call.nargs != sym->fn.nparams) {
                tc_error(c, TA_ERR_TYPE + 3, e->line, e->col,
                         "'%s' \u0b9a\u0bc6\u0baf\u0bb2\u0bbf\u0b95\u0bcd\u0b95\u0bc1 %zu \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb3\u0bcd \u0ba4\u0bc7\u0bb5\u0bc8; %zu \u0b95\u0bca\u0b9f\u0bc1\u0b95\u0bcd\u0b95\u0baa\u0bcd\u0baa\u0b9f\u0bcd\u0b9f\u0ba9",
                         "\u0b9a\u0bc6\u0baf\u0bb2\u0bbf \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1\u0b95\u0bcd\u0b95\u0bb3\u0bcd \u0b8e\u0ba3\u0bcd\u0ba3\u0bbf\u0b95\u0bcd\u0b95\u0bc8\u0baf\u0bc8 \u0b9a\u0bb0\u0bbf\u0baa\u0bbe\u0bb0\u0bcd\u0b95\u0bcd\u0b95\u0bb5\u0bc1\u0bae\u0bcd",
                         sym->name, sym->fn.nparams, e->as.call.nargs);
            } else {
                for (size_t i = 0; i < e->as.call.nargs; i++) {
                    const TaType *at = e->as.call.args[i]->ty;
                    const TaType *pt = sym->fn.params[i] ? sym->fn.params[i]->type : NULL;
                    if (pt && at && !assignable(at, pt)) {
                        tc_error(c, TA_ERR_TYPE + 4, e->as.call.args[i]->line,
                                 e->as.call.args[i]->col,
                                 "'%s' \u0b87\u0ba9\u0bcd %zu-\u0bae\u0bcd \u0b85\u0bb3\u0bb5\u0bc1\u0bb0\u0bc1: %s \u0b8e\u0ba4\u0bbf\u0bb0\u0bcd\u0baa\u0bbe\u0bb0\u0bcd\u0baa\u0bcd\u0baa\u0bc1, %s \u0b95\u0bbf\u0b9f\u0bc8\u0ba4\u0bcd\u0ba4\u0ba4\u0bc1",
                                 "\u0b9a\u0bb0\u0bbf\u0baf \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0bb2\u0bbe\u0ba9 \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc8\u0b95\u0bcd \u0b95\u0bca\u0b9f\u0bc1\u0b99\u0bcd\u0b95\u0bb3\u0bcd",
                                 sym->name, i + 1, ta_ty_name(pt), ta_ty_name(at));
                    }
                }
            }
            e->ty = sym->fn.ret ? sym->fn.ret : ta_ty_void();
            break;
        }
        case TX_INDEX:
            e->ty = index_result_type(c, e);
            break;
        case TX_MEMBER:
            if (e->sym && e->sym->kind == TA_SYM_BUILTIN_FUNC) {
                tc_error(c, TA_ERR_SEMANTIC + 8, e->line, e->col,
                         "\u0ba4\u0bca\u0b95\u0bc1\u0ba4\u0bbf \u0b9a\u0bc6\u0baf\u0bb2\u0bbf\u0baf\u0bc8 '\u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bbe\u0b95' \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4 \u0bae\u0bc1\u0b9f\u0bbf\u0baf\u0bbe\u0ba4\u0bc1",
                         "\u0b85\u0ba4\u0bc8 (...) \u0b89\u0b9f\u0ba9\u0bcd \u0b85\u0bb4\u0bc8\u0ba4\u0bcd\u0ba4\u0bc1\u0baa\u0bcd \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4\u0bb5\u0bc1\u0bae\u0bcd");
            }
            e->ty = ta_ty_error();
            break;
        case TX_LIST: {
            size_t n = e->as.list.count;
            const TaType *el = NULL;
            for (size_t i = 0; i < n; i++) {
                const TaType *t = check_expr(c, e->as.list.elems[i]);
                el = el ? unify(c, el, t, e->line, e->col, TA_ERR_TYPE + 1,
                                "\u0baa\u0b9f\u0bcd\u0b9f\u0bbf\u0baf\u0bb2\u0bcd \u0b89\u0bb1\u0bc1\u0baa\u0bcd\u0baa\u0bc1\u0b95\u0bb3\u0bbf\u0ba9\u0bcd \u0bb5\u0b95\u0bc8\u0b95\u0bb3\u0bcd \u0bb5\u0bc7\u0bb1\u0bc1\u0baa\u0b9f\u0bc1\u0b95\u0bbf\u0ba9\u0bcd\u0bb1\u0ba9")
                        : t;
            }
            e->ty = ta_ty_list(el ? el : ta_ty_unknown());
            break;
        }
        case TX_DICT: {
            size_t n = e->as.dict.count;
            const TaType *kt = NULL;
            const TaType *vt = NULL;
            for (size_t i = 0; i < n; i++) {
                const TaType *k = check_expr(c, e->as.dict.keys[i]);
                const TaType *v = check_expr(c, e->as.dict.vals[i]);
                kt = kt ? unify(c, kt, k, e->line, e->col, TA_ERR_TYPE + 8,
                                "\u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf \u0b9a\u0bbe\u0bb5\u0bbf\u0b95\u0bb3\u0bbf\u0ba9\u0bcd \u0bb5\u0b95\u0bc8\u0b95\u0bb3\u0bcd \u0bb5\u0bc7\u0bb1\u0bc1\u0baa\u0b9f\u0bc1\u0b95\u0bbf\u0ba9\u0bcd\u0bb1\u0ba9")
                        : k;
                vt = vt ? unify(c, vt, v, e->line, e->col, TA_ERR_TYPE + 1,
                                "\u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf \u0bae\u0ba4\u0bbf\u0baa\u0bcd\u0baa\u0bc1\u0b95\u0bb3\u0bbf\u0ba9\u0bcd \u0bb5\u0b95\u0bc8\u0b95\u0bb3\u0bcd \u0bb5\u0bc7\u0bb1\u0bc1\u0baa\u0b9f\u0bc1\u0b95\u0bbf\u0ba9\u0bcd\u0bb1\u0ba9")
                        : v;
            }
            if (kt && kt->kind != TY_UNKNOWN && kt->kind != TY_ERROR && !is_key_type(kt)) {
                tc_error(c, TA_ERR_TYPE + 8, e->line, e->col,
                         "\u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf \u0b9a\u0bbe\u0bb5\u0bbf\u0b95\u0bb3\u0bcd \u0b89\u0bb0\u0bc8 \u0b85\u0bb2\u0bcd\u0bb2\u0ba4\u0bc1 \u0bae\u0bc1\u0bb4\u0bc1\u0b8e\u0ba3\u0bcd \u0bb5\u0b95\u0bc8\u0baf\u0bbf\u0bb2\u0bcd \u0bae\u0b9f\u0bcd\u0b9f\u0bc1\u0bae\u0bcd \u0b87\u0bb0\u0bc1\u0b95\u0bcd\u0b95 \u0bb5\u0bc7\u0ba3\u0bcd\u0b9f\u0bc1\u0bae\u0bcd",
                         "%s \u0b9a\u0bbe\u0bb5\u0bbf \u0bb5\u0b95\u0bc8 \u0b86\u0ba4\u0bb0\u0bbf\u0b95\u0bcd\u0b95\u0baa\u0bcd\u0baa\u0b9f\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8",
                         ta_ty_name(kt));
            }
            e->ty = ta_ty_dict(kt ? kt : ta_ty_unknown(), vt ? vt : ta_ty_unknown());
            break;
        }
    }
    if (!e->ty) e->ty = ta_ty_error();
    return e->ty;
}

static void record_return(TcCtx *c, const TaType *t) {
    if (t->kind == TY_ERROR) return;
    if (c->nrets == c->retcap) {
        c->retcap = c->retcap ? c->retcap * 2 : 8;
        c->ret_types = ta_xrealloc((void *)c->ret_types, c->retcap * sizeof(TaType *));
    }
    c->ret_types[c->nrets++] = t;
}

static void check_stmt(TcCtx *c, TaStmt *st);

static void check_block(TcCtx *c, TaBlock *b) {
    if (!b) return;
    for (size_t i = 0; i < b->count; i++) check_stmt(c, b->items[i]);
}

static void ensure_func_checked(TcCtx *c, TaSymbol *fsym) {
    if (fsym->fn.check_state == 2) return;
    TaStmt *decl = fsym->fn.decl;
    if (fsym->fn.check_state == 1) {
        if (!fsym->fn.has_annotation) {
            tc_error(c, TA_ERR_TYPE + 13, decl ? decl->line : 0, decl ? decl->col : 0,
                     "செயலி '%s' சுழற்சியாக (recursive) அழைக்கப்படுகிறது; திரும்பும் வகை தெரியவில்லை",
                     "'செயலி %s(...) -> வகை:' என்று திரும்பும் வகையைக் குறிப்பிடுங்கள் (எ.கா.: -> முழுஎண்)",
                     fsym->name);
            fsym->fn.ret = ta_ty_error();
            fsym->type = ta_ty_error();
            fsym->fn.check_state = 2;
        }
        return;
    }
    fsym->fn.check_state = 1;

    TcCtx saved = *c;
    TaSymbol *prev_func = c->cur_func;
    c->cur_func = fsym;

    if (!fsym->fn.has_annotation) {
        c->nrets = 0;
    }

    if (fsym->fn.has_annotation) {
        fsym->fn.ret = fsym->type ? fsym->type : ta_ty_void();
        fsym->type = fsym->fn.ret;
    }

    if (decl && decl->as.funcdef->body) {
        check_block(c, decl->as.funcdef->body);
    }

    if (!fsym->fn.has_annotation) {
        const TaType *acc = NULL;
        bool saw_value = false;
        for (size_t i = 0; i < c->nrets; i++) {
            const TaType *t = c->ret_types[i];
            if (!t || t->kind == TY_VOID) continue;
            saw_value = true;
            acc = acc ? unify(c, acc, t, decl ? decl->line : 0, decl ? decl->col : 0,
                              TA_ERR_TYPE + 10,
                              "வேறுபட்ட வகைகளில் 'திருப்பு' கூறுகள் உள்ளன")
                      : t;
        }
        const TaType *ret = saw_value ? (acc ? acc : ta_ty_unknown()) : ta_ty_void();
        if (contains_unknown(ret)) ret = ta_ty_unknown();
        fsym->fn.ret = ret;
        fsym->type = ret;
    }

    *c = saved;
    c->cur_func = prev_func;
    (void)saved;
    fsym->fn.check_state = 2;
}

static void check_stmt(TcCtx *c, TaStmt *st) {
    switch (st->kind) {
        case ST_VARDECL: {
            TaSymbol *vsym = st->as.vardecl.decl_sym;
            const TaType *ann = st->as.vardecl.ann_type;
            const TaType *it = NULL;
            if (st->as.vardecl.init) it = check_expr(c, st->as.vardecl.init);
            if (it && it->kind == TY_ERROR) break;
            if (ann) {
                if (it && !assignable(it, ann)) {
                    tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                             "%s வகையான மதிப்பை %s வகை மாறியில் சேமிக்க முடியாது",
                             "வகை குறிப்புக்கு ஏற்ப மதிப்பை மாற்றுங்கள், அல்லது வகைக் "
                             "குறிப்பை நீக்கி ஊகிக்க விடுங்கள்",
                             ta_ty_name(it), ta_ty_name(ann));
                    if (vsym) vsym->type = ann;
                    break;
                }
                if (it && contains_unknown(ann) && !contains_unknown(it)) {
                    if (ann->kind != TY_LIST && ann->kind != TY_DICT) {
                        tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                                 "வகைக் குறிப்பு '%s' முழுமையான வகையாக இருக்க வேண்டும்",
                                 "[..] / {..:..} க்குள் துல்லியமான வகையை எழுதுங்கள்",
                                 ta_ty_name(ann));
                        break;
                    }
                }
                if (vsym) vsym->type = ann;
                break;
            }
            if (!it) break;
            if (it->kind == TY_UNKNOWN || contains_unknown(it)) {
                tc_error(c, TA_ERR_TYPE + 9, st->line, st->col,
                         "வெற்று தொகுப்பின் வகையை ஊகிக்க முடியவில்லை",
                         "வகைக் குறிப்பு சேர்க்கவும்; எ.கா.: மாறி x: [முழுஎண்] = []");
                if (vsym) vsym->type = ta_ty_error();
                break;
            }
            if (it->kind == TY_VOID) {
                tc_error(c, TA_ERR_TYPE + 9, st->line, st->col,
                         "மதிப்பே தராத கோவையை மாறியில் சேமிக்க முடியாது",
                         "ஒரு மதிப்பு தரும் கோவையைப் பயன்படுத்துங்கள்");
                if (vsym) vsym->type = ta_ty_error();
                break;
            }
            if (vsym) vsym->type = it;
            break;
        }
        case ST_ASSIGN: {
            TaExpr *tgt = st->as.assign.target;
            const TaExpr *dummy_unused = NULL;
            (void)dummy_unused;
            if (tgt->kind == TX_IDENT) {
                TaSymbol *sym = tgt->sym;
                if (sym && !sym->type) {
                    const TaType *vt0 = check_expr(c, st->as.assign.value);
                    if (vt0->kind == TY_VOID || contains_unknown(vt0)) {
                        tc_error(c, TA_ERR_TYPE + 9, st->line, st->col,
                                 "மதிப்பை ஊகிக்க முடியவில்லை",
                                 "வகைக் குறிப்புடன் அறிவிக்கவும்");
                        sym->type = ta_ty_error();
                    } else {
                        sym->type = vt0;
                    }
                    break;
                }
                if (!sym || !sym->type) {
                    check_expr(c, st->as.assign.value);
                    break;
                }
                const TaType *vt = check_expr(c, st->as.assign.value);
                if (vt->kind == TY_ERROR) break;
                if (st->as.assign.op != TA_ASSIGN) {
                    TaBinOp bop = TB_ADD;
                    if (st->as.assign.op == TA_PLUSEQ) bop = TB_ADD;
                    else if (st->as.assign.op == TA_MINUSEQ) bop = TB_SUB;
                    else if (st->as.assign.op == TA_STAREQ) bop = TB_MUL;
                    else if (st->as.assign.op == TA_SLASHEQ) bop = TB_DIV;
                    const char *optext =
                        st->as.assign.op == TA_PLUSEQ ? "+=" :
                        st->as.assign.op == TA_MINUSEQ ? "-=" :
                        st->as.assign.op == TA_STAREQ ? "*=" : "/=";
                    const TaType *res = NULL;
                    if (bop == TB_ADD && sym->type->kind == TY_STRING &&
                        vt->kind == TY_STRING) {
                        res = ta_ty_string();
                    } else {
                        res = arith_combine(sym->type, vt);
                    }
                    if (!res || !assignable(res, sym->type)) {
                        tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                                 "'%s' (%s) செயலால் %s வகையில் சேமிக்க முடியாது",
                                 "%s %s <மதிப்பு> இணக்கமான வகைகளுடன் மட்டுமே",
                                 sym->name, optext, ta_ty_name(sym->type), optext);
                        break;
                    }
                } else if (!assignable(vt, sym->type)) {
                    tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                             "%s வகையான மதிப்பை %s வகை மாறி '%s' இல் சேமிக்க முடியாது",
                             "வகைகள் பொருந்த வேண்டும் (முழுஎண் → மிதவை தானாக மாறும்)",
                             ta_ty_name(vt), ta_ty_name(sym->type), sym->name);
                }
            } else if (tgt->kind == TX_INDEX) {
                const TaType *bt = check_expr(c, tgt->as.index.base);
                const TaType *vt = check_expr(c, st->as.assign.value);
                if (bt->kind == TY_ERROR || vt->kind == TY_ERROR) break;
                if (bt->kind == TY_STRING) {
                    tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                             "உரை (string) மாற்றக்கூடியது அல்ல; உரை[i] = ... சாத்தியமில்லை",
                             "புதிய உரையை உருவாக்கி மாறியில் சேமிக்கவும்");
                    break;
                }
                if (bt->kind == TY_LIST) {
                    const TaType *idx_t = check_expr(c, tgt->as.index.index);
                    if (!assignable(idx_t, ta_ty_int())) {
                        tc_error(c, TA_ERR_TYPE + 7, tgt->line, tgt->col,
                                 "பட்டியல் அடுயாணவு முழுஎண் ஆக இருக்க வேண்டும்",
                                 "list index must be int");
                    }
                    if (!assignable(vt, bt->elem ? bt->elem : ta_ty_unknown())) {
                        tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                                 "பட்டியல் உறுப்பு வகை %s; %s சேமிக்க முடியாது",
                                 "சரியான வகையின் மதிப்பை சேமிக்கவும்",
                                 ta_ty_name(bt->elem ? bt->elem : ta_ty_unknown()),
                                 ta_ty_name(vt));
                    }
                } else if (bt->kind == TY_DICT) {
                    const TaType *idx_t = check_expr(c, tgt->as.index.index);
                    if (bt->key && !assignable(idx_t, bt->key)) {
                        tc_error(c, TA_ERR_TYPE + 8, tgt->line, tgt->col,
                                 "சாவி வகை தவறு: %s எதிர்பார்க்கப்பட்டது, %s கிடைத்தது",
                                 "சாவி வகையை சரிபார்க்கவும்",
                                 ta_ty_name(bt->key), ta_ty_name(idx_t));
                    }
                    if (!assignable(vt, bt->val ? bt->val : ta_ty_unknown())) {
                        tc_error(c, TA_ERR_TYPE + 12, st->line, st->col,
                                 "அகராதி மதிப்பு வகை %s; %s சேமிக்க முடியாது",
                                 "சரியான வகையின் மதிப்பை சேமிக்கவும்",
                                 ta_ty_name(bt->val ? bt->val : ta_ty_unknown()),
                                 ta_ty_name(vt));
                    }
                } else {
                    tc_error(c, TA_ERR_TYPE + 6, st->line, st->col,
                             "%s வகையில் [..] மூலம் சேமிக்க முடியாது",
                             "பட்டியல்/அகராதி மட்டுமே", ta_ty_name(bt));
                }
            }
            break;
        }
        case ST_EXPR:
            check_expr(c, st->as.exprstmt.expr);
            break;
        case ST_IF:
            if (check_expr(c, st->as.ifstmt.cond)->kind != TY_BOOL &&
                st->as.ifstmt.cond->ty->kind != TY_ERROR) {
                tc_error(c, TA_ERR_TYPE + 2, st->as.ifstmt.cond->line, st->as.ifstmt.cond->col,
                         "'என்றால்' நிபந்தனை பூலியன் வகையாக இருக்க வேண்டும்; %s கிடைத்தது",
                         "ஒப்பீட்டுக் கோவையைப் பயன்படுத்துங்கள்; எ.கா.: x > 3",
                         ta_ty_name(st->as.ifstmt.cond->ty));
            }
            check_block(c, st->as.ifstmt.then_body);
            for (size_t i = 0; i < st->as.ifstmt.nelifs; i++) {
                if (check_expr(c, st->as.ifstmt.elifs[i]->cond)->kind != TY_BOOL &&
                    st->as.ifstmt.elifs[i]->cond->ty->kind != TY_ERROR) {
                    tc_error(c, TA_ERR_TYPE + 2, st->as.ifstmt.elifs[i]->cond->line,
                             st->as.ifstmt.elifs[i]->cond->col,
                             "'இல்லையெனில் என்றால்' நிபந்தனை பூலியன் வகையாக இருக்க வேண்டும்",
                             "ஒப்பீட்டுக் கோவையைப் பயன்படுத்துங்கள்");
                }
                check_block(c, st->as.ifstmt.elifs[i]->body);
            }
            check_block(c, st->as.ifstmt.else_body);
            break;
        case ST_WHILE:
            if (check_expr(c, st->as.whilestmt.cond)->kind != TY_BOOL &&
                st->as.whilestmt.cond->ty->kind != TY_ERROR) {
                tc_error(c, TA_ERR_TYPE + 2, st->as.whilestmt.cond->line,
                         st->as.whilestmt.cond->col,
                         "'வரை' நிபந்தனை பூலியன் வகையாக இருக்க வேண்டும்; %s கிடைத்தது",
                         "ஒப்பீட்டுக் கோவையைப் பயன்படுத்துங்கள்; எ.கா.: i < 10",
                         ta_ty_name(st->as.whilestmt.cond->ty));
            }
            check_block(c, st->as.whilestmt.body);
            break;
        case ST_FOREACH: {
            const TaType *it = check_expr(c, st->as.foreach.iterable);
            TaSymbol *vsym = st->as.foreach.decl_sym;
            if (it->kind == TY_ERROR) break;
            if (it->kind == TY_LIST) {
                if (vsym) vsym->type = it->elem ? it->elem : ta_ty_unknown();
            } else if (it->kind == TY_STRING) {
                if (vsym) vsym->type = ta_ty_char();
            } else {
                tc_error(c, TA_ERR_TYPE + 14, st->as.foreach.iterable->line,
                         st->as.foreach.iterable->col,
                         "%s வகையை 'ஒவ்வொன்றும்' மூலம் மடக்க முடியாது",
                         "பட்டியல் அல்லது உரையை மட்டுமே மடக்க முடியும்; எண்களுக்கு வரம்பு() பயன்படுத்துங்கள்",
                         ta_ty_name(it));
            }
            check_block(c, st->as.foreach.body);
            break;
        }
        case ST_RETURN: {
            if (!c->cur_func) break;
            const TaType *vt =
                st->as.ret.value ? check_expr(c, st->as.ret.value) : ta_ty_void();
            if (vt->kind == TY_ERROR) break;
            if (c->cur_func->fn.has_annotation) {
                const TaType *rt = c->cur_func->type;
                if (!rt) rt = ta_ty_void();
                if (rt->kind == TY_VOID && st->as.ret.value) {
                    tc_error(c, TA_ERR_TYPE + 10, st->line, st->col,
                             "'%s' எதிர்பார்ப்பை மடக்கு ஏதும் திருப்பாத செயலி; மதிப்புடன் திருப்ப முடியாது",
                             "மதிப்பு திருப்ப 'செயலி f() -> வகை:' என அறிவிக்கவும்",
                             c->cur_func->name);
                } else if (rt->kind != TY_VOID && !st->as.ret.value) {
                    tc_error(c, TA_ERR_TYPE + 10, st->line, st->col,
                             "'%s' செயலி %s வகை மதிப்பை திருப்ப வேண்டும்",
                             "'திருப்பு <மதிப்பு>' என எழுதுங்கள்",
                             c->cur_func->name, ta_ty_name(rt));
                } else if (st->as.ret.value && !assignable(vt, rt)) {
                    tc_error(c, TA_ERR_TYPE + 10, st->line, st->col,
                             "%s வகை மதிப்பு திருப்பப்பட்டது; '%s' க்கு %s எதிர்பார்க்கப்படுகிறது",
                             "சரியான வகையின் மதிப்பைத் திருப்புங்கள்",
                             ta_ty_name(vt), c->cur_func->name, ta_ty_name(rt));
                }
            } else {
                record_return(c, vt);
            }
            break;
        }
        case ST_BREAK:
        case ST_CONTINUE:
            break;
        case ST_FUNCDEF:
            break;
    }
}

bool ta_typecheck_run(const char *file, TaProgram *prog, TaScope *globals,
                      TaDiagnostics *diag) {
    TcCtx c;
    memset(&c, 0, sizeof(c));
    c.diag = diag;
    c.file = file;
    c.globals = globals;

    for (size_t i = 0; i < prog->count; i++) {
        TaStmt *st = prog->items[i];
        if (st->kind == ST_FUNCDEF) continue;
        check_stmt(&c, st);
    }
    for (size_t i = 0; i < prog->count; i++) {
        TaStmt *st = prog->items[i];
        if (st->kind != ST_FUNCDEF) continue;
        TaSymbol *fsym = ta_scope_lookup_local(globals, st->as.funcdef->name);
        if (fsym && fsym->kind == TA_SYM_FUNC) ensure_func_checked(&c, fsym);
    }

    free(c.ret_types);
    return true;
}
