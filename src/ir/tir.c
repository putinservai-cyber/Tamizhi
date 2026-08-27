#include "ta_ir.h"

#define TA_MAX_LOOPS 128

typedef struct {
    TaIrUnit *u;
    TaDiagnostics *diag;
    const char *file;
    TaIrFunc *fn;
    int next_label;
    struct {
        int inc_label;
        int end_label;
    } loops[TA_MAX_LOOPS];
    int nloops;
    bool echo;
} TirCtx;

static void tir_error(TirCtx *c, int line, int col, const char *msg, const char *feature) {
    ta_diag_report(c->diag, TA_ERR_INTERNAL + 2, c->file, line, col, msg,
                   "%s \u0b87\u0ba4\u0bb1\u0bcd\u0b95\u0bc1 \u0b95\u0b9f\u0bcd\u0b9f\u0bc1\u0baa\u0bcd\u0baa\u0b9f\u0bbe\u0b95\u0bc8\u0baf\u0bbf\u0bb2\u0bcd \u0b86\u0ba4\u0bb0\u0bb5\u0bc1 \u0b87\u0bb2\u0bcd\u0bb2\u0bc8", feature ? feature : "");
}

static TaIrInstr *emit_instr(TirCtx *c, TaIrOp op) {
    TaIrFunc *f = c->fn;
    if (f->count == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 32;
        f->items = ta_xrealloc(f->items, f->cap * sizeof(TaIrInstr));
    }
    TaIrInstr *in = &f->items[f->count++];
    memset(in, 0, sizeof(*in));
    in->op = op;
    return in;
}

static void set_args(TaIrInstr *in, const TaOperand *args, size_t n) {
    in->nargs = n;
    if (n == 0) {
        in->args = NULL;
        return;
    }
    in->args = ta_xmalloc(n * sizeof(TaOperand));
    memcpy(in->args, args, n * sizeof(TaOperand));
}

static TaOperand temp_of(TirCtx *c, const TaType *ty) {
    TaOperand o;
    memset(&o, 0, sizeof(o));
    o.kind = TA_OP_TEMP;
    o.idx = c->fn->nslots++;
    (void)ty;
    return o;
}

static TaOperand const_int(long long v) {
    TaOperand o;
    memset(&o, 0, sizeof(o));
    o.kind = TA_OP_INT;
    o.i = v;
    return o;
}

static TaOperand const_str(TirCtx *c, const char *s, size_t len) {
    TaOperand o;
    memset(&o, 0, sizeof(o));
    o.kind = TA_OP_STR;
    o.str_id = ta_ir_add_string(c->u, s, len);
    return o;
}

static TaOperand emit_const_temp(TirCtx *c, TaOperand v) {
    TaOperand t = temp_of(c, NULL);
    TaIrInstr *in = emit_instr(c, TI_CONST);
    in->dst = t;
    set_args(in, &v, 1);
    return t;
}

static TaOperand force_temp(TirCtx *c, TaOperand v) {
    switch (v.kind) {
        case TA_OP_TEMP:
        case TA_OP_SLOT:
            return v;
        default:
            return emit_const_temp(c, v);
    }
}

static bool ty_is_float(const TaType *t) {
    return t && t->kind == TY_FLOAT;
}

static bool ty_is_int(const TaType *t) {
    return t && t->kind == TY_INT;
}

static TaOperand conv_to(TirCtx *c, TaOperand v, const TaType *from, const TaType *to) {
    if (!from || !to) return v;
    if (ty_is_int(from) && ty_is_float(to)) {
        TaOperand t = temp_of(c, to);
        TaIrInstr *in = emit_instr(c, TI_CONV_I2F);
        in->dst = t;
        set_args(in, &v, 1);
        return t;
    }
    if (ty_is_float(from) && ty_is_int(to)) {
        TaOperand t = temp_of(c, to);
        TaIrInstr *in = emit_instr(c, TI_CONV_F2I);
        in->dst = t;
        set_args(in, &v, 1);
        return t;
    }
    return v;
}

int ta_ir_add_string(TaIrUnit *u, const char *data, size_t len) {
    for (size_t i = 0; i < u->nstrings; i++) {
        if (u->string_lens[i] == len && memcmp(u->strings[i], data, len) == 0) return (int)i;
    }
    u->strings = ta_xrealloc(u->strings, (u->nstrings + 1) * sizeof(char *));
    u->string_lens = ta_xrealloc(u->string_lens, (u->nstrings + 1) * sizeof(size_t));
    u->strings[u->nstrings] = ta_xstrndup(data, len);
    u->string_lens[u->nstrings] = len;
    return (int)u->nstrings++;
}

static TaIrFunc *new_func(TaIrUnit *u, const char *label, TaSymbol *sym, int base_slots,
                          const TaType *ret) {
    TaIrFunc *f = ta_xcalloc(1, sizeof(TaIrFunc));
    f->label = ta_xstrdup(label);
    f->sym = sym;
    f->nslots = base_slots;
    f->ret = ret;
    u->funcs = ta_xrealloc(u->funcs, (u->nfuncs + 1) * sizeof(TaIrFunc *));
    u->funcs[u->nfuncs++] = f;
    return f;
}

static TaOperand gen_expr(TirCtx *c, TaExpr *e);

static TaOperand force_temp(TirCtx *c, TaOperand v);

static TaOperand gen_binop(TirCtx *c, TaBinOp op, TaOperand l, TaOperand r, const TaType *ty) {
    TaOperand d = temp_of(c, ty);
    TaOperand args[2] = {l, r};
    TaIrInstr *in = emit_instr(c, TI_BINOP);
    in->binop = (int)op;
    in->ty = ty;
    in->dst = d;
    set_args(in, args, 2);
    return d;
}

static void emit_label(TirCtx *c, int lbl) {
    TaIrInstr *in = emit_instr(c, TI_LABEL);
    in->label = lbl;
}

static void emit_jmp_to(TirCtx *c, int lbl) {
    TaIrInstr *in = emit_instr(c, TI_JMP);
    in->label = lbl;
}

static void emit_jz_to(TirCtx *c, TaOperand cond, int lbl) {
    TaIrInstr *in = emit_instr(c, TI_JZ);
    in->label = lbl;
    set_args(in, &cond, 1);
}

static void emit_const_into(TirCtx *c, TaOperand dst, long long v) {
    TaOperand cv = const_int(v);
    TaIrInstr *in = emit_instr(c, TI_CONST);
    in->dst = dst;
    set_args(in, &cv, 1);
}

static TaOperand gen_shortcircuit(TirCtx *c, TaExpr *e) {
    bool is_and = e->as.bin.op == TB_AND;
    TaOperand d = temp_of(c, ta_ty_bool());
    TaOperand a = gen_expr(c, e->as.bin.lhs);

    if (is_and) {
        int lzero = c->next_label++;
        int lend = c->next_label++;
        emit_jz_to(c, a, lzero);
        TaOperand b = gen_expr(c, e->as.bin.rhs);
        emit_jz_to(c, b, lzero);
        emit_const_into(c, d, 1);
        emit_jmp_to(c, lend);
        emit_label(c, lzero);
        emit_const_into(c, d, 0);
        emit_label(c, lend);
    } else {
        int lrhs = c->next_label++;
        int lzero = c->next_label++;
        int lend = c->next_label++;
        emit_jz_to(c, a, lrhs);
        emit_const_into(c, d, 1);
        emit_jmp_to(c, lend);
        emit_label(c, lrhs);
        TaOperand b = gen_expr(c, e->as.bin.rhs);
        emit_jz_to(c, b, lzero);
        emit_const_into(c, d, 1);
        emit_jmp_to(c, lend);
        emit_label(c, lzero);
        emit_const_into(c, d, 0);
        emit_label(c, lend);
    }
    return d;
}

static const TaType *e_ty_elem(const TaType *container);

static void begin_loop_at(TirCtx *c, int inc, int end);

static void gen_stmt(TirCtx *c, TaStmt *st);

static void gen_block(TirCtx *c, TaBlock *b) {
    if (!b) return;
    for (size_t i = 0; i < b->count; i++) gen_stmt(c, b->items[i]);
}

static TaOperand emit_rt(TirCtx *c, const char *name, const TaOperand *args, size_t n,
                         const TaType *ret_ty) {
    TaIrInstr *in = emit_instr(c, TI_RT);
    in->rt_name = name;
    in->ty = ret_ty;
    if (ret_ty && ret_ty->kind != TY_VOID) in->dst = temp_of(c, ret_ty);
    set_args(in, args, n);
    return in->dst;
}

static void lower_print_args(TirCtx *c, TaExpr **args, size_t n) {
    for (size_t i = 0; i < n; i++) {
        TaOperand v = gen_expr(c, args[i]);
        const TaType *t = args[i]->ty;
        const char *fname = "ta_rt_print_int";
        switch (t->kind) {
            case TY_INT: fname = "ta_rt_print_int"; break;
            case TY_FLOAT: fname = "ta_rt_print_float"; break;
            case TY_BOOL: fname = "ta_rt_print_bool"; break;
            case TY_CHAR: fname = "ta_rt_print_char"; break;
            case TY_STRING: fname = "ta_rt_print_str"; break;
            default: fname = "ta_rt_print_int"; break;
        }
        TaOperand one[1] = {v};
        emit_rt(c, fname, one, 1, ta_ty_void());
        if (i + 1 < n) emit_rt(c, "ta_rt_print_space", NULL, 0, ta_ty_void());
    }
    emit_rt(c, "ta_rt_print_nl", NULL, 0, ta_ty_void());
}

static TaOperand gen_call(TirCtx *c, TaExpr *e) {
    TaExpr *cal = e->as.call.callee;
    TaSymbol *sym = cal->sym;

    if (sym && sym->kind == TA_SYM_BUILTIN_FUNC) {
        switch (sym->builtin_id) {
            case TA_BI_PRINT:
                lower_print_args(c, e->as.call.args, e->as.call.nargs);
                return (TaOperand){TA_OP_NONE, 0, 0, 0, 0};
            case TA_BI_INPUT:
                return emit_rt(c, "ta_rt_input", NULL, 0, ta_ty_string());
            case TA_BI_LEN: {
                TaOperand v = gen_expr(c, e->as.call.args[0]);
                TaOperand d = temp_of(c, ta_ty_int());
                TaOperand args1[1] = {v};
                TaIrInstr *in = emit_instr(c, TI_LEN);
                in->dst = d;
                set_args(in, args1, 1);
                return d;
            }
            case TA_BI_RANGE: {
                size_t n = e->as.call.nargs;
                TaOperand a[3] = {{TA_OP_NONE, 0, 0, 0, 0}, {TA_OP_NONE, 0, 0, 0, 0},
                                  {TA_OP_NONE, 0, 0, 0, 0}};
                for (size_t i = 0; i < n; i++) a[i] = gen_expr(c, e->as.call.args[i]);
                const char *nm = n == 1 ? "ta_rt_range_1" : n == 2 ? "ta_rt_range_2"
                                                                   : "ta_rt_range_3";
                const TaType *lt = ta_ty_list(ta_ty_int());
                return emit_rt(c, nm, a, n, lt);
            }
            default:
                break;
        }

        TaSymbol *parent_module = NULL;
        TaExpr *obj = cal->kind == TX_MEMBER ? cal->as.member.obj : NULL;
        if (obj && obj->sym) parent_module = obj->sym;
        (void)parent_module;

        switch (sym->builtin_id) {
            case TA_BI_ABS: {
                TaOperand v = gen_expr(c, e->as.call.args[0]);
                bool isf = ty_is_float(e->as.call.args[0]->ty);
                TaOperand a[1] = {v};
                return emit_rt(c, isf ? "ta_rt_abs_f" : "ta_rt_abs_i", a, 1,
                               isf ? ta_ty_float() : ta_ty_int());
            }
            case TA_BI_FLOOR: {
                TaOperand v = conv_to(c, gen_expr(c, e->as.call.args[0]),
                                      e->as.call.args[0]->ty, ta_ty_float());
                TaOperand a[1] = {v};
                return emit_rt(c, "ta_rt_floor", a, 1, ta_ty_int());
            }
            case TA_BI_SQRT: {
                TaOperand v = conv_to(c, gen_expr(c, e->as.call.args[0]),
                                      e->as.call.args[0]->ty, ta_ty_float());
                TaOperand a[1] = {v};
                return emit_rt(c, "ta_rt_sqrt", a, 1, ta_ty_float());
            }
            case TA_BI_POW: {
                bool both_int = ty_is_int(e->as.call.args[0]->ty) &&
                                ty_is_int(e->as.call.args[1]->ty);
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                if (both_int) {
                    TaOperand a[2] = {a0, a1};
                    return emit_rt(c, "ta_rt_pow_i", a, 2, ta_ty_int());
                }
                TaOperand f0 = conv_to(c, a0, e->as.call.args[0]->ty, ta_ty_float());
                TaOperand f1 = conv_to(c, a1, e->as.call.args[1]->ty, ta_ty_float());
                TaOperand a[2] = {f0, f1};
                return emit_rt(c, "ta_rt_pow_f", a, 2, ta_ty_float());
            }
            case TA_BI_STR_CONCAT: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_concat", a, 2, ta_ty_string());
            }
            case TA_BI_STR_SUB: {
                TaOperand s = gen_expr(c, e->as.call.args[0]);
                TaOperand i1 = gen_expr(c, e->as.call.args[1]);
                TaOperand i2 = gen_expr(c, e->as.call.args[2]);
                TaOperand a[3] = {s, i1, i2};
                return emit_rt(c, "ta_rt_str_sub", a, 3, ta_ty_string());
            }
            case TA_BI_STR_SPLIT: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_split", a, 2, ta_ty_list(ta_ty_string()));
            }
            case TA_BI_STR_JOIN: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_join", a, 2, ta_ty_string());
            }
            case TA_BI_STR_STRIP: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a[1] = {a0};
                return emit_rt(c, "ta_rt_str_strip", a, 1, ta_ty_string());
            }
            case TA_BI_STR_REPLACE: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a2 = gen_expr(c, e->as.call.args[2]);
                TaOperand a[3] = {a0, a1, a2};
                return emit_rt(c, "ta_rt_str_replace", a, 3, ta_ty_string());
            }
            case TA_BI_STR_UPPER: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a[1] = {a0};
                return emit_rt(c, "ta_rt_str_upper", a, 1, ta_ty_string());
            }
            case TA_BI_STR_LOWER: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a[1] = {a0};
                return emit_rt(c, "ta_rt_str_lower", a, 1, ta_ty_string());
            }
            case TA_BI_STR_STARTSWITH: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_startswith", a, 2, ta_ty_bool());
            }
            case TA_BI_STR_ENDSWITH: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_endswith", a, 2, ta_ty_bool());
            }
            case TA_BI_LIST_PUSH: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_list_push", a, 2, ta_ty_list(ta_ty_unknown()));
            }
            case TA_BI_LIST_POP: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a[1] = {a0};
                return emit_rt(c, "ta_rt_list_pop", a, 1, ta_ty_list(ta_ty_unknown()));
            }
            case TA_BI_DICT_KEYS: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a[1] = {a0};
                return emit_rt(c, "ta_rt_dict_keys", a, 1, ta_ty_list(ta_ty_unknown()));
            }
            case TA_BI_DICT_ITEMS: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a[1] = {a0};
                return emit_rt(c, "ta_rt_dict_items", a, 1, ta_ty_list(ta_ty_list(ta_ty_unknown())));
            }
            case TA_BI_STR_FIND: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_find", a, 2, ta_ty_int());
            }
            case TA_BI_STR_COUNT: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                return emit_rt(c, "ta_rt_str_count", a, 2, ta_ty_int());
            }
            case TA_BI_ASSERT: {
                TaOperand a0 = gen_expr(c, e->as.call.args[0]);
                TaOperand a1 = gen_expr(c, e->as.call.args[1]);
                TaOperand a[2] = {a0, a1};
                emit_rt(c, "ta_rt_assert", a, 2, ta_ty_void());
                return (TaOperand){TA_OP_NONE, 0, 0, 0, 0};
            }
            default:
                return (TaOperand){TA_OP_NONE, 0, 0, 0, 0};
        }
    }

    size_t n = e->as.call.nargs;
    TaOperand *args = ta_xmalloc((n ? n : 1) * sizeof(TaOperand));
    for (size_t i = 0; i < n; i++) {
        TaOperand v = gen_expr(c, e->as.call.args[i]);
        if (sym && sym->fn.params[i] && sym->fn.params[i]->type) {
            v = conv_to(c, v, e->as.call.args[i]->ty, sym->fn.params[i]->type);
        }
        args[i] = v;
    }
    TaIrInstr *in = emit_instr(c, TI_CALL);
    in->callee = sym;
    in->dst.kind = TA_OP_NONE;
    if (sym->fn.ret && sym->fn.ret->kind != TY_VOID) {
        in->dst = temp_of(c, sym->fn.ret);
    }
    set_args(in, args, n);
    free(args);
    return in->dst;
}

static TaOperand gen_expr(TirCtx *c, TaExpr *e) {
    switch (e->kind) {
        case TX_INT:
            return const_int(e->as.ival);
        case TX_FLOAT: {
            TaOperand o;
            memset(&o, 0, sizeof(o));
            o.kind = TA_OP_FLOAT;
            o.f = e->as.fval;
            return o;
        }
        case TX_BOOL:
        case TX_CHAR:
            return const_int(e->kind == TX_BOOL ? (long long)e->as.ival : (long long)e->as.cval);
        case TX_STRING:
            return const_str(c, e->as.str.sval, e->as.str.slen);
        case TX_NULL: {
            TaOperand o;
            memset(&o, 0, sizeof(o));
            o.kind = TA_OP_INT;
            return o;
        }
        case TX_IDENT: {
            TaOperand o;
            memset(&o, 0, sizeof(o));
            o.kind = TA_OP_SLOT;
            o.idx = e->sym->slot;
            return o;
        }
        case TX_BINARY: {
            if (e->as.bin.op == TB_AND || e->as.bin.op == TB_OR)
                return gen_shortcircuit(c, e);
            const TaType *rt = e->ty;
            TaOperand l = gen_expr(c, e->as.bin.lhs);
            TaOperand r = gen_expr(c, e->as.bin.rhs);
            if ((rt->kind == TY_FLOAT) &&
                (e->as.bin.lhs->ty->kind == TY_INT || e->as.bin.rhs->ty->kind == TY_INT)) {
                l = conv_to(c, l, e->as.bin.lhs->ty, ta_ty_float());
                r = conv_to(c, r, e->as.bin.rhs->ty, ta_ty_float());
            }
            return gen_binop(c, e->as.bin.op, l, r, rt);
        }
        case TX_UNARY: {
            TaOperand v = gen_expr(c, e->as.un.operand);
            TaOperand d = temp_of(c, e->ty);
            TaOperand args1[1] = {v};
            TaIrInstr *in =
                emit_instr(c, e->as.un.op == TU_NEG ? TI_NEG : TI_NOT);
            in->ty = e->ty;
            in->dst = d;
            set_args(in, args1, 1);
            return d;
        }
        case TX_CALL:
            return gen_call(c, e);
        case TX_INDEX: {
            TaExpr *base = e->as.index.base;
            TaOperand b = gen_expr(c, base);
            TaOperand i = gen_expr(c, e->as.index.index);
            if (base->ty->kind == TY_STRING) {
                TaOperand a[2] = {b, i};
                return emit_rt(c, "ta_rt_str_at", a, 2, ta_ty_char());
            }
            if (base->ty->kind == TY_DICT) {
                bool key_is_str = base->ty->key && base->ty->key->kind == TY_STRING;
                TaOperand kt = force_temp(c, i);
                TaOperand a[3] = {b, kt, const_int(key_is_str ? 1 : 0)};
                TaOperand cellp = emit_rt(c, "ta_rt_dict_get", a, 3, ta_ty_unknown());
                TaOperand val = temp_of(c, e->ty);
                TaOperand da[1] = {cellp};
                TaIrInstr *dr = emit_instr(c, TI_DEREF);
                dr->ty = e->ty;
                dr->dst = val;
                set_args(dr, da, 1);
                return val;
            }
            TaOperand d = temp_of(c, e->ty);
            TaOperand a[2] = {b, i};
            TaIrInstr *in = emit_instr(c, TI_IDX_GET);
            in->ty = base->ty;
            in->dst = d;
            set_args(in, a, 2);
            return d;
        }
        case TX_MEMBER:
            tir_error(c, e->line, e->col, "\u0ba4\u0bc6\u0bb1\u0bcd\u0bb1\u0bbe\u0ba9 \u0b95\u0bcb\u0bb5\u0bc8 \u0b95\u0b9f\u0bcd\u0b9f\u0bae\u0bc8\u0baa\u0bcd\u0baa\u0bc1",
                      "\u0ba4\u0bc6\u0bb1\u0bcd\u0bb1\u0bc1");
            return (TaOperand){TA_OP_NONE, 0, 0, 0, 0};
        case TX_LIST: {
            size_t n = e->as.list.count;
            TaOperand *args = ta_xmalloc((n ? n : 1) * sizeof(TaOperand));
            for (size_t i = 0; i < n; i++) args[i] = gen_expr(c, e->as.list.elems[i]);
            TaIrInstr *in = emit_instr(c, TI_LIST_NEW);
            in->ty = e->ty;
            in->dst = temp_of(c, e->ty);
            set_args(in, args, n);
            free(args);
            return in->dst;
        }
        case TX_DICT: {
            TaOperand d = emit_rt(c, "ta_rt_dict_new", NULL, 0, e->ty);
            for (size_t i = 0; i < e->as.dict.count; i++) {
                TaOperand k = force_temp(c, gen_expr(c, e->as.dict.keys[i]));
                TaOperand v = force_temp(c, gen_expr(c, e->as.dict.vals[i]));
                bool key_is_str =
                    e->as.dict.keys[i]->ty && e->as.dict.keys[i]->ty->kind == TY_STRING;
                TaOperand a[4] = {d, k, v, const_int(key_is_str ? 1 : 0)};
                emit_rt(c, "ta_rt_dict_set", a, 4, ta_ty_void());
            }
            return d;
        }
    }
    return (TaOperand){TA_OP_NONE, 0, 0, 0, 0};
}

static void store_to_var(TirCtx *c, TaSymbol *sym, TaOperand v, const TaType *val_ty) {
    v = conv_to(c, v, val_ty, sym->type);
    TaOperand slot;
    memset(&slot, 0, sizeof(slot));
    slot.kind = TA_OP_SLOT;
    slot.idx = sym->slot;
    TaOperand args[2] = {slot, v};
    TaIrInstr *in = emit_instr(c, TI_STORE);
    set_args(in, args, 2);
}

static int begin_loop(TirCtx *c, int inc_label) {
    if (c->nloops >= TA_MAX_LOOPS) return -1;
    c->loops[c->nloops].inc_label = inc_label;
    c->loops[c->nloops].end_label = -1;
    return c->nloops++;
}

static void gen_stmt(TirCtx *c, TaStmt *st) {
    switch (st->kind) {
        case ST_VARDECL: {
            if (!st->as.vardecl.init) break;
            TaOperand v = gen_expr(c, st->as.vardecl.init);
            store_to_var(c, st->as.vardecl.decl_sym, v, st->as.vardecl.init->ty);
            break;
        }
        case ST_ASSIGN: {
            TaExpr *tgt = st->as.assign.target;
            if (tgt->kind == TX_IDENT) {
                TaOperand v = gen_expr(c, st->as.assign.value);
                if (st->as.assign.op != TA_ASSIGN) {
                    TaOperand cur;
                    memset(&cur, 0, sizeof(cur));
                    cur.kind = TA_OP_SLOT;
                    cur.idx = tgt->sym->slot;
                    TaBinOp bop = TB_ADD;
                    if (st->as.assign.op == TA_MINUSEQ) bop = TB_SUB;
                    else if (st->as.assign.op == TA_STAREQ) bop = TB_MUL;
                    else if (st->as.assign.op == TA_SLASHEQ) bop = TB_DIV;
                    const TaType *resty =
                        tgt->sym->type && tgt->sym->type->kind == TY_FLOAT ? ta_ty_float()
                                                                           : tgt->sym->type;
                    TaOperand res = gen_binop(c, bop, cur, v, resty);
                    store_to_var(c, tgt->sym, res, resty);
                } else {
                    store_to_var(c, tgt->sym, v, st->as.assign.value->ty);
                }
                break;
            }
            if (tgt->kind != TX_INDEX) break;
            TaOperand base = force_temp(c, gen_expr(c, tgt->as.index.base));
            TaOperand idx = force_temp(c, gen_expr(c, tgt->as.index.index));
            TaOperand val = force_temp(c, gen_expr(c, st->as.assign.value));
            const TaType *bt = tgt->as.index.base->ty;

            if (bt->kind == TY_LIST && st->as.assign.op != TA_ASSIGN) {
                TaOperand g[2] = {base, idx};
                TaOperand old = temp_of(c, e_ty_elem(bt));
                TaIrInstr *gi = emit_instr(c, TI_IDX_GET);
                gi->ty = bt;
                gi->dst = old;
                set_args(gi, g, 2);
                TaBinOp bop = TB_ADD;
                if (st->as.assign.op == TA_MINUSEQ) bop = TB_SUB;
                else if (st->as.assign.op == TA_STAREQ) bop = TB_MUL;
                else if (st->as.assign.op == TA_SLASHEQ) bop = TB_DIV;
                TaOperand res = gen_binop(c, bop, old, val,
                                          bt->elem && bt->elem->kind == TY_FLOAT ? ta_ty_float()
                                                                                 : bt->elem);
                val = force_temp(c, res);
            }
            if (bt->kind == TY_DICT) {
                bool key_is_str = bt->key && bt->key->kind == TY_STRING;
                TaOperand a[4] = {base, idx, val, const_int(key_is_str ? 1 : 0)};
                emit_rt(c, "ta_rt_dict_set", a, 4, ta_ty_void());
            } else {
                TaOperand a[3] = {base, idx, val};
                emit_rt(c, "ta_rt_list_set", a, 3, ta_ty_void());
            }
            break;
        }
        case ST_EXPR: {
            TaExpr *ex = st->as.exprstmt.expr;
            TaOperand v = gen_expr(c, ex);
            if (c->echo && ex->ty) {
                const char *fname = NULL;
                switch (ex->ty->kind) {
                    case TY_INT: fname = "ta_rt_print_int"; break;
                    case TY_FLOAT: fname = "ta_rt_print_float"; break;
                    case TY_BOOL: fname = "ta_rt_print_bool"; break;
                    case TY_CHAR: fname = "ta_rt_print_char"; break;
                    case TY_STRING: fname = "ta_rt_print_str"; break;
                    default: break;
                }
                if (fname) {
                    TaOperand vt = force_temp(c, v);
                    TaOperand a[1] = {vt};
                    emit_rt(c, fname, a, 1, ta_ty_void());
                    emit_rt(c, "ta_rt_print_nl", NULL, 0, ta_ty_void());
                }
            }
            break;
        }
        case ST_IF: {
            int l_else = c->next_label++;
            int l_end = c->next_label++;
            TaOperand cond = gen_expr(c, st->as.ifstmt.cond);
            emit_jz_to(c, cond, l_else);
            gen_block(c, st->as.ifstmt.then_body);
            bool has_more = st->as.ifstmt.nelifs > 0 || st->as.ifstmt.else_body;
            if (has_more) emit_jmp_to(c, l_end);
            emit_label(c, l_else);
            for (size_t i = 0; i < st->as.ifstmt.nelifs; i++) {
                int l_next = c->next_label++;
                TaOperand ec = gen_expr(c, st->as.ifstmt.elifs[i]->cond);
                emit_jz_to(c, ec, l_next);
                gen_block(c, st->as.ifstmt.elifs[i]->body);
                bool more = (i + 1 < st->as.ifstmt.nelifs) || st->as.ifstmt.else_body;
                if (more) emit_jmp_to(c, l_end);
                emit_label(c, l_next);
            }
            gen_block(c, st->as.ifstmt.else_body);
            emit_label(c, l_end);
            break;
        }
        case ST_WHILE: {
            int l_cond = c->next_label++;
            int l_end = c->next_label++;
            emit_label(c, l_cond);
            TaOperand cond = gen_expr(c, st->as.whilestmt.cond);
            emit_jz_to(c, cond, l_end);
            int li = begin_loop(c, l_cond);
            c->loops[li].end_label = l_end;
            gen_block(c, st->as.whilestmt.body);
            c->nloops--;
            emit_jmp_to(c, l_cond);
            emit_label(c, l_end);
            break;
        }
        case ST_FOREACH: {
            TaOperand seq = force_temp(c, gen_expr(c, st->as.foreach.iterable));
            const TaType *it_ty = st->as.foreach.iterable->ty;
            bool over_string = it_ty && it_ty->kind == TY_STRING;

            TaOperand ivar = temp_of(c, ta_ty_int());
            TaOperand nvar = temp_of(c, ta_ty_int());
            emit_const_into(c, ivar, 0);
            TaOperand la[1] = {seq};
            TaIrInstr *li = emit_instr(c, TI_LEN);
            li->dst = nvar;
            set_args(li, la, 1);

            int l_cond = c->next_label++;
            int l_inc = c->next_label++;
            int l_end = c->next_label++;

            emit_label(c, l_cond);
            TaOperand cmpres = gen_binop(c, TB_LT, ivar, nvar, ta_ty_bool());
            emit_jz_to(c, cmpres, l_end);

            TaOperand elem;
            if (over_string) {
                TaOperand a[2] = {seq, ivar};
                elem = emit_rt(c, "ta_rt_str_at", a, 2, ta_ty_char());
            } else {
                TaOperand a[2] = {seq, ivar};
                TaIrInstr *gi = emit_instr(c, TI_IDX_GET);
                gi->ty = it_ty;
                gi->dst = temp_of(c, e_ty_elem(it_ty));
                set_args(gi, a, 2);
                elem = gi->dst;
            }
            store_to_var(c, st->as.foreach.decl_sym, elem,
                         over_string ? ta_ty_char() : e_ty_elem(it_ty));

            begin_loop_at(c, l_inc, l_end);
            gen_block(c, st->as.foreach.body);
            c->nloops--;

            emit_label(c, l_inc);
            TaOperand one = const_int(1);
            TaOperand args2[2] = {ivar, one};
            TaIrInstr *inc = emit_instr(c, TI_BINOP);
            inc->binop = TB_ADD;
            inc->ty = ta_ty_int();
            inc->dst = ivar;
            set_args(inc, args2, 2);
            emit_jmp_to(c, l_cond);
            emit_label(c, l_end);
            break;
        }
        case ST_RETURN: {
            if (st->as.ret.value) {
                TaOperand v = gen_expr(c, st->as.ret.value);
                if (c->fn->ret) v = conv_to(c, v, st->as.ret.value->ty, c->fn->ret);
                TaOperand a[1] = {v};
                TaIrInstr *in = emit_instr(c, TI_RET);
                set_args(in, a, 1);
            } else {
                TaIrInstr *in = emit_instr(c, TI_RET);
                in->nargs = 0;
                in->args = NULL;
            }
            break;
        }
        case ST_BREAK: {
            if (c->nloops > 0 && c->loops[c->nloops - 1].end_label >= 0)
                emit_jmp_to(c, c->loops[c->nloops - 1].end_label);
            break;
        }
        case ST_CONTINUE: {
            if (c->nloops > 0) emit_jmp_to(c, c->loops[c->nloops - 1].inc_label);
            break;
        }
        case ST_FUNCDEF:
            break;
    }
}

static const TaType *e_ty_elem(const TaType *container) {
    if (container && container->kind == TY_LIST)
        return container->elem ? container->elem : ta_ty_unknown();
    if (container && container->kind == TY_STRING) return ta_ty_char();
    return ta_ty_int();
}

static void begin_loop_at(TirCtx *c, int inc, int end) {
    if (c->nloops < TA_MAX_LOOPS) {
        c->loops[c->nloops].inc_label = inc;
        c->loops[c->nloops].end_label = end;
        c->nloops++;
    }
}

TaIrUnit *ta_ir_generate(const char *file, TaProgram *prog, TaScope *globals,
                         int top_slots, TaDiagnostics *diag, bool echo_top_exprs) {
    (void)globals;
    TaIrUnit *u = ta_xcalloc(1, sizeof(TaIrUnit));
    TirCtx c;
    memset(&c, 0, sizeof(c));
    c.u = u;
    c.diag = diag;
    c.file = file;
    c.echo = echo_top_exprs;

    char lbl[64];
    for (size_t i = 0; i < prog->count; i++) {
        TaStmt *st = prog->items[i];
        if (st->kind != ST_FUNCDEF) continue;
        TaSymbol *fsym = st->as.funcdef ? ta_scope_lookup(globals, st->as.funcdef->name) : NULL;
        if (!fsym || fsym->kind != TA_SYM_FUNC) continue;
        snprintf(lbl, sizeof(lbl), "ta_fn_%d", fsym->fn.index);
        TaIrFunc *f = new_func(u, lbl, fsym, fsym->fn.nlocals > 0 ? fsym->fn.nlocals : 1,
                               fsym->type);
        c.fn = f;
        c.nloops = 0;
        gen_block(&c, st->as.funcdef->body);
        if (u->main_sym == NULL && fsym->fn.is_main) u->main_sym = fsym;
    }

    u->top = new_func(u, "ta_top", NULL, top_slots > 0 ? top_slots : 1, NULL);
    c.fn = u->top;
    c.nloops = 0;
    for (size_t i = 0; i < prog->count; i++) {
        TaStmt *st = prog->items[i];
        if (st->kind == ST_FUNCDEF) continue;
        gen_stmt(&c, st);
    }

    if (ta_diag_has_errors(diag)) {
        ta_ir_unit_free(u);
        return NULL;
    }
    return u;
}

void ta_ir_unit_free(TaIrUnit *u) {
    if (!u) return;
    for (size_t k = 0; k < u->nfuncs; k++) {
        TaIrFunc *f = u->funcs[k];
        for (size_t i = 0; i < f->count; i++) free(f->items[i].args);
        free(f->items);
        free(f->label);
        free(f);
    }
    free(u->funcs);
    for (size_t i = 0; i < u->nstrings; i++) free(u->strings[i]);
    free(u->strings);
    free(u->string_lens);
    free(u);
}
