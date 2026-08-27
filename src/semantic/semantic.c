#include "ta_semantic.h"

TaScope *ta_scope_new(TaScope *parent) {
    TaScope *s = ta_xcalloc(1, sizeof(TaScope));
    s->nbuckets = 32;
    s->buckets = ta_xcalloc(s->nbuckets, sizeof(void *));
    s->parent = parent;
    return s;
}

void ta_scope_free(TaScope *s) {
    if (!s) return;
    for (size_t i = 0; i < s->nbuckets; i++) {
        struct TaScopeEntry *e = s->buckets[i];
        while (e) {
            struct TaScopeEntry *nx = e->next;
            free(e->name);
            free(e);
            e = nx;
        }
    }
    free(s->buckets);
    free(s);
}

static size_t scope_hash(const char *name) {
    size_t h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return h;
}

TaSymbol *ta_scope_lookup_local(TaScope *s, const char *name) {
    size_t i = scope_hash(name) & (s->nbuckets - 1);
    for (struct TaScopeEntry *e = s->buckets[i]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e->sym;
    }
    return NULL;
}

TaSymbol *ta_scope_lookup(TaScope *s, const char *name) {
    for (; s; s = s->parent) {
        TaSymbol *sym = ta_scope_lookup_local(s, name);
        if (sym) return sym;
    }
    return NULL;
}

bool ta_scope_declare(TaScope *s, TaSymbol *sym) {
    if (ta_scope_lookup_local(s, sym->name)) return false;
    size_t i = scope_hash(sym->name) & (s->nbuckets - 1);
    struct TaScopeEntry *e = ta_xmalloc(sizeof(struct TaScopeEntry));
    e->name = ta_xstrdup(sym->name);
    e->sym = sym;
    e->next = s->buckets[i];
    s->buckets[i] = e;
    return true;
}

TaSymbol *ta_symbol_new(const char *name, TaSymKind kind, int line, int col) {
    TaSymbol *sym = ta_xcalloc(1, sizeof(TaSymbol));
    sym->name = ta_xstrdup(name);
    sym->kind = kind;
    sym->line = line;
    sym->col = col;
    return sym;
}

static void register_named_type(TaScope *globals, const char *tamil, const TaType *ty) {
    TaSymbol *sym = ta_symbol_new(tamil, TA_SYM_TYPE, 0, 0);
    sym->type = ty;
    ta_scope_declare(globals, sym);
}

static TaSymbol *register_builtin_fn(TaScope *scope, const char *name, TaBuiltinId id) {
    TaSymbol *sym = ta_symbol_new(name, TA_SYM_BUILTIN_FUNC, 0, 0);
    sym->builtin_id = id;
    ta_scope_declare(scope, sym);
    return sym;
}

static void register_module_fn(TaScope *module_scope, const char *name, TaBuiltinId id,
                               const TaType *ret, size_t nfixed, const TaType **fixed) {
    TaSymbol *sym = register_builtin_fn(module_scope, name, id);
    sym->fn.ret = ret;
    sym->type = ret;
    for (size_t i = 0; i < nfixed; i++) {
        if (sym->fn.nparams == sym->fn.pcap) {
            sym->fn.pcap = sym->fn.pcap ? sym->fn.pcap * 2 : 4;
            sym->fn.params =
                ta_xrealloc(sym->fn.params, sym->fn.pcap * sizeof(TaSymbol *));
        }
        char nm[32];
        snprintf(nm, sizeof(nm), "#%zu", i);
        TaSymbol *ps = ta_symbol_new(nm, TA_SYM_PARAM, 0, 0);
        ps->type = fixed[i];
        sym->fn.params[sym->fn.nparams++] = ps;
    }
}

static void register_builtins(TaScope *globals) {
    register_named_type(globals, "முழுஎண்", ta_ty_int());
    register_named_type(globals, "மிதவை", ta_ty_float());
    register_named_type(globals, "பூலியன்", ta_ty_bool());
    register_named_type(globals, "எழுத்து", ta_ty_char());
    register_named_type(globals, "உரை", ta_ty_string());
    register_named_type(globals, "வெற்று", ta_ty_void());

    register_builtin_fn(globals, "அச்சிடு", TA_BI_PRINT);
    register_builtin_fn(globals, "உள்ளீடு", TA_BI_INPUT);
    register_builtin_fn(globals, "நீளம்", TA_BI_LEN);
    register_builtin_fn(globals, "வரம்பு", TA_BI_RANGE);

    register_builtin_fn(globals, "பிரி", TA_BI_STR_SPLIT);
    register_builtin_fn(globals, "இணைப்பு", TA_BI_STR_JOIN);
    register_builtin_fn(globals, "நறுக்கு", TA_BI_STR_STRIP);
    register_builtin_fn(globals, "மாற்று", TA_BI_STR_REPLACE);
    register_builtin_fn(globals, "மேலெழுத்து", TA_BI_STR_UPPER);
    register_builtin_fn(globals, "சிறியெழுத்து", TA_BI_STR_LOWER);
    register_builtin_fn(globals, "துவங்கிறதா", TA_BI_STR_STARTSWITH);
    register_builtin_fn(globals, "முடிவதா", TA_BI_STR_ENDSWITH);

    register_builtin_fn(globals, "சேர்", TA_BI_LIST_PUSH);
    register_builtin_fn(globals, "நீக்கு", TA_BI_LIST_POP);
    register_builtin_fn(globals, "விசைகள்", TA_BI_DICT_KEYS);
    register_builtin_fn(globals, "உருப்படிகள்", TA_BI_DICT_ITEMS);
    register_builtin_fn(globals, "கண்டுபிடி", TA_BI_STR_FIND);
    register_builtin_fn(globals, "எண்ணிக்கை", TA_BI_STR_COUNT);
    register_builtin_fn(globals, "உறுதிப்படுத்து", TA_BI_ASSERT);

    TaSymbol *mathmod = ta_symbol_new("கணிதம்", TA_SYM_MODULE, 0, 0);
    mathmod->members = ta_scope_new(globals);
    ta_scope_declare(globals, mathmod);

    const TaType *t_float = ta_ty_float();
    const TaType *t_int = ta_ty_int();
    const TaType *t_str = ta_ty_string();

    const TaType *abs_params[1] = {t_float};
    register_module_fn(mathmod->members, "தனிமதிப்பு", TA_BI_ABS, t_float, 1, abs_params);
    const TaType *floor_params[1] = {t_float};
    register_module_fn(mathmod->members, "முழுமதிப்பு", TA_BI_FLOOR, t_int, 1, floor_params);
    const TaType *sqrt_params[1] = {t_float};
    register_module_fn(mathmod->members, "வர்க்கமூலம்", TA_BI_SQRT, t_float, 1, sqrt_params);
    const TaType *pow_params[2] = {t_float, t_float};
    register_module_fn(mathmod->members, "சக்தி", TA_BI_POW, t_float, 2, pow_params);

    TaSymbol *strmod = ta_symbol_new("உரை", TA_SYM_MODULE, 0, 0);
    strmod->members = ta_scope_new(globals);
    ta_scope_declare(globals, strmod);

    const TaType *cat_params[2] = {t_str, t_str};
    register_module_fn(strmod->members, "இணை", TA_BI_STR_CONCAT, t_str, 2, cat_params);
    const TaType *sub_params[3] = {t_str, t_int, t_int};
    register_module_fn(strmod->members, "வெட்டு", TA_BI_STR_SUB, t_str, 3, sub_params);
}

const TaType *ta_lookup_named_type(TaScope *globals, const char *name) {
    TaSymbol *sym = ta_scope_lookup(globals, name);
    if (sym && sym->kind == TA_SYM_TYPE) return sym->type;
    return NULL;
}

const TaType *ta_resolve_type_spec(const TaTypeSpec *ts, TaScope *globals,
                                   TaDiagnostics *diag, const char *file) {
    if (!ts) return NULL;
    switch (ts->kind) {
        case TS_NAME: {
            const TaType *t = ta_lookup_named_type(globals, ts->name);
            if (!t) {
                ta_diag_report(diag, TA_ERR_SEMANTIC + 7, file, ts->line, ts->col,
                               "அறியப்படாத வகை (type)",
                               "'%s' என்ற வகை இல்லை; முழுஎண், மிதவை, பூலியன், எழுத்து, "
                               "உரை, [வகை], {சாவி: மதிப்பு} ஆகியவை மட்டுமே",
                               ts->name);
                return ta_ty_error();
            }
            return t;
        }
        case TS_LIST: {
            const TaType *el = ta_resolve_type_spec(ts->elem, globals, diag, file);
            return ta_ty_list(el);
        }
        case TS_DICT: {
            const TaType *k = ta_resolve_type_spec(ts->tk, globals, diag, file);
            const TaType *v = ta_resolve_type_spec(ts->tv, globals, diag, file);
            return ta_ty_dict(k, v);
        }
    }
    return ta_ty_error();
}

typedef struct {
    TaDiagnostics *diag;
    const char *file;
    TaScope *globals;
    TaScope *cur_scope;
    TaSymbol *cur_func;
    int loop_depth;
    int next_func_index;
    int next_slot;
} SemCtx;

static void sem_error(SemCtx *c, int code, int line, int col, const char *msg,
                      const char *hint_fmt, ...) {
    va_list ap;
    va_start(ap, hint_fmt);
    ta_diag_report_v(c->diag, code, c->file, line, col, msg, hint_fmt, ap);
    va_end(ap);
}

static void resolve_expr_ex(SemCtx *c, TaExpr *e, bool allow_module);

static void resolve_expr(SemCtx *c, TaExpr *e) { resolve_expr_ex(c, e, false); }

static void analyze_stmt(SemCtx *c, TaStmt *st);

static void find_similar(SemCtx *c, const char *name, char *out, size_t outsz) {
    out[0] = 0;
    int best = 4;
    for (TaScope *s = c->cur_scope; s; s = s->parent) {
        for (size_t i = 0; i < s->nbuckets; i++) {
            for (struct TaScopeEntry *e = s->buckets[i]; e; e = e->next) {
                int d = ta_edit_distance(name, e->name);
                if (d > 0 && d <= best && d < 4) {
                    best = d;
                    snprintf(out, outsz, "%s", e->name);
                }
            }
        }
    }
}

static void analyze_block(SemCtx *c, TaBlock *b) {
    if (!b) return;
    for (size_t i = 0; i < b->count; i++) analyze_stmt(c, b->items[i]);
}

static void analyze_block_scoped(SemCtx *c, TaBlock *b) {
    if (!b) return;
    TaScope *outer = c->cur_scope;
    c->cur_scope = ta_scope_new(outer);
    analyze_block(c, b);
    c->cur_scope = outer;
}

static void analyze_func(SemCtx *c, TaFuncDef *fd, TaSymbol *fsym) {
    TaScope *local = ta_scope_new(c->globals);
    TaScope *outer_scope = c->cur_scope;
    TaSymbol *outer_func = c->cur_func;
    int outer_loop = c->loop_depth;

    c->cur_scope = local;
    c->cur_func = fsym;
    c->loop_depth = 0;
    c->next_slot = 0;

    int slot = 0;
    fsym->type = fd->ret_resolved;

    for (size_t i = 0; i < fd->nparams; i++) {
        TaParam *pm = &fd->params[i];
        pm->resolved = ta_resolve_type_spec(pm->type, c->globals, c->diag, c->file);
        if (!pm->type) {
            sem_error(c, TA_ERR_TYPE + 15, pm->line, pm->col,
                      "அளவுரு '%s' க்கு வகை (type) குறிப்பிடப்படவில்லை",
                      "எ.கா.: %s: முழுஎண் — ஒவ்வொரு அளவுருக்கும் வகை தேவை", pm->name);
            pm->resolved = ta_ty_error();
        } else if (pm->resolved == ta_ty_void()) {
            sem_error(c, TA_ERR_TYPE + 15, pm->line, pm->col,
                      "அளவுருவாக 'வெற்று' (void) பயன்படுத்த முடியாது",
                      "சரியான வகையைக் குறிப்பிடுங்கள்: முழுஎண், உரை, ...");
            pm->resolved = ta_ty_error();
        }
        bool dup = false;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(fd->params[j].name, pm->name) == 0) dup = true;
        }
        if (dup) {
            sem_error(c, TA_ERR_SEMANTIC + 2, pm->line, pm->col,
                      "அளவுரு பெயர் '%s' இரண்டு முறை வருகிறது",
                      "ஒவ்வொரு அளவுருக்கும் தனித்துவமான பெயர் கொடுங்கள்", pm->name);
        }
        TaSymbol *ps = ta_symbol_new(pm->name, TA_SYM_PARAM, pm->line, pm->col);
        ps->type = pm->resolved;
        ps->slot = slot++;
        ta_scope_declare(local, ps);
        if (fsym->fn.nparams == fsym->fn.pcap) {
            fsym->fn.pcap = fsym->fn.pcap ? fsym->fn.pcap * 2 : 4;
            fsym->fn.params = ta_xrealloc(fsym->fn.params, fsym->fn.pcap * sizeof(TaSymbol *));
        }
        fsym->fn.params[fsym->fn.nparams++] = ps;
    }
    c->next_slot = slot;

    analyze_block(c, fd->body);

    /* Warn (non-fatal) about unused local variables / parameters. */
    for (size_t b = 0; b < local->nbuckets; b++) {
        struct TaScopeEntry *e = local->buckets[b];
        for (; e; e = e->next) {
            TaSymbol *s = e->sym;
            if (!s->used && (s->kind == TA_SYM_VAR || s->kind == TA_SYM_PARAM)) {
                fprintf(stderr,
                        "%s:%d:%d: \u0b8e\u0b9a\u0bcd\u0b9a\u0bb0\u0bbf\u0b95\u0bcd\u0b95\u0bc8: "
                        "\u0bae\u0bbe\u0bb1\u0bbf '%s' \u0baa\u0baf\u0ba9\u0bcd\u0baa\u0b9f\u0bc1\u0ba4\u0bcd\u0ba4\u0baa\u0bcd\u0baa\u0b9f\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8\n",
                        c->file, s->line, s->col, s->name);
            }
        }
    }

    fsym->fn.nlocals = c->next_slot;

    c->cur_scope = outer_scope;
    c->cur_func = outer_func;
    c->loop_depth = outer_loop;
}

static void analyze_stmt(SemCtx *c, TaStmt *st) {
    switch (st->kind) {
        case ST_VARDECL: {
            if (st->as.vardecl.type) {
                st->as.vardecl.ann_type =
                    ta_resolve_type_spec(st->as.vardecl.type, c->globals, c->diag, c->file);
            }
            if (st->as.vardecl.init) resolve_expr(c, st->as.vardecl.init);
            TaSymbol *vs = ta_symbol_new(st->as.vardecl.name,
                                         st->as.vardecl.is_const ? TA_SYM_CONST : TA_SYM_VAR,
                                         st->line, st->col);
            vs->is_const = st->as.vardecl.is_const;
            vs->slot = c->next_slot++;
            if (!ta_scope_declare(c->cur_scope, vs)) {
                sem_error(c, TA_ERR_SEMANTIC + 2, st->line, st->col,
                          "'%s' ஏற்கனவே இந்த வரம்பில் வரையறுக்கப்பட்டுள்ளது",
                          "ஒரே வரம்பில் ஒரே பெயரை இருமுறை வரையறுக்க முடியாது",
                          st->as.vardecl.name);
            }
            st->as.vardecl.decl_sym = vs;
            break;
        }
        case ST_ASSIGN:
            if (st->as.assign.target->kind == TX_IDENT &&
                st->as.assign.op == TA_ASSIGN &&
                !ta_scope_lookup(c->cur_scope, st->as.assign.target->as.name)) {
                TaSymbol *ns = ta_symbol_new(st->as.assign.target->as.name, TA_SYM_VAR,
                                             st->as.assign.target->line,
                                             st->as.assign.target->col);
                ns->slot = c->next_slot++;
                ta_scope_declare(c->cur_scope, ns);
            }
            resolve_expr(c, st->as.assign.target);
            if (st->as.assign.target->kind == TX_IDENT) {
                TaSymbol *sym = st->as.assign.target->sym;
                if (sym) {
                    if (sym->is_const || sym->kind == TA_SYM_CONST) {
                        sem_error(c, TA_ERR_SEMANTIC + 3, st->line, st->col,
                                  "நிலையான (constant) '%s' க்கு மதிப்பு மாற்ற முடியாது",
                                  "'%s' ஐ மாறி (மாறி) ஆக அறிவித்தால் மட்டுமே மாற்ற முடியும்",
                                  sym->name, sym->name);
                    } else if (sym->kind != TA_SYM_VAR && sym->kind != TA_SYM_PARAM) {
                        sem_error(c, TA_ERR_SEMANTIC + 3, st->line, st->col,
                                  "'%s' க்கு மதிப்பு ஒதுக்க முடியாது",
                                  "மாறிகளுக்கு மட்டுமே மதிப்பு ஒதுக்க முடியும்", sym->name);
                    }
                }
            } else if (st->as.assign.target->kind != TX_INDEX) {
                sem_error(c, TA_ERR_PARSE + 3, st->line, st->col,
                          "இங்கு மதிப்பு சேமிக்க முடியாது",
                          "'=' இன் இடதுபுறம் மாறி அல்லது பட்டியல்/அகராதி அணுகல் மட்டுமே இருக்க வேண்டும்");
            }
            resolve_expr(c, st->as.assign.value);
            break;
        case ST_EXPR:
            resolve_expr(c, st->as.exprstmt.expr);
            break;
        case ST_IF:
            resolve_expr(c, st->as.ifstmt.cond);
            analyze_block_scoped(c, st->as.ifstmt.then_body);
            for (size_t i = 0; i < st->as.ifstmt.nelifs; i++) {
                resolve_expr(c, st->as.ifstmt.elifs[i]->cond);
                analyze_block_scoped(c, st->as.ifstmt.elifs[i]->body);
            }
            analyze_block_scoped(c, st->as.ifstmt.else_body);
            break;
        case ST_WHILE:
            resolve_expr(c, st->as.whilestmt.cond);
            c->loop_depth++;
            analyze_block_scoped(c, st->as.whilestmt.body);
            c->loop_depth--;
            break;
        case ST_FOREACH: {
            resolve_expr(c, st->as.foreach.iterable);
            TaScope *outer = c->cur_scope;
            c->cur_scope = ta_scope_new(outer);
            TaSymbol *vs = ta_symbol_new(st->as.foreach.varname, TA_SYM_VAR,
                                         st->as.foreach.line_var, st->as.foreach.col_var);
            vs->slot = c->next_slot++;
            if (!ta_scope_declare(c->cur_scope, vs)) {
                sem_error(c, TA_ERR_SEMANTIC + 2, st->as.foreach.line_var,
                          st->as.foreach.col_var,
                          "'%s' ஏற்கனவே இந்த வரம்பில் வரையறுக்கப்பட்டுள்ளது",
                          "மடக்கு மாறியின் பெயர் அதே வரம்பில் மறுபயன்படுத்த கூடாது",
                          st->as.foreach.varname);
            }
            st->as.foreach.decl_sym = vs;
            c->loop_depth++;
            analyze_block(c, st->as.foreach.body);
            c->loop_depth--;
            c->cur_scope = outer;
            break;
        }
        case ST_RETURN:
            if (!c->cur_func) {
                sem_error(c, TA_ERR_SEMANTIC + 6, st->line, st->col,
                          "'திருப்பு' செயலிக்கு வெளியே பயன்படுத்த முடியாது",
                          "திருப்பு கூறு ஒரு செயலிக்குள் மட்டுமே இருக்க முடியும்");
            }
            if (st->as.ret.value) resolve_expr(c, st->as.ret.value);
            break;
        case ST_BREAK:
            if (c->loop_depth == 0) {
                sem_error(c, TA_ERR_SEMANTIC + 4, st->line, st->col,
                          "'நிறுத்து' மடக்கு (loop) க்கு வெளியே பயன்படுத்த முடியாது",
                          "நிறுத்து / தொடர் ஆகியவை 'வரை' அல்லது 'ஒவ்வொன்றும்' க்குள் மட்டுமே");
            }
            break;
        case ST_CONTINUE:
            if (c->loop_depth == 0) {
                sem_error(c, TA_ERR_SEMANTIC + 5, st->line, st->col,
                          "'தொடர்' மடக்கு (loop) க்கு வெளியே பயன்படுத்த முடியாது",
                          "நிறுத்து / தொடர் ஆகியவை 'வரை' அல்லது 'ஒவ்வொன்றும்' க்குள் மட்டுமே");
            }
            break;
        case ST_FUNCDEF:
            break;
    }
}

static void resolve_expr_ex(SemCtx *c, TaExpr *e, bool allow_module) {
    switch (e->kind) {
        case TX_INT:
            e->ty = ta_ty_int();
            break;
        case TX_FLOAT:
            e->ty = ta_ty_float();
            break;
        case TX_BOOL:
            e->ty = ta_ty_bool();
            break;
        case TX_CHAR:
            e->ty = ta_ty_char();
            break;
        case TX_STRING:
            e->ty = ta_ty_string();
            break;
        case TX_NULL:
            e->ty = ta_ty_void();
            break;
        case TX_IDENT: {
            TaSymbol *sym = ta_scope_lookup(c->cur_scope, e->as.name);
            if (!sym) {
                char near[256];
                find_similar(c, e->as.name, near, sizeof(near));
                if (near[0]) {
                    sem_error(c, TA_ERR_SEMANTIC + 1, e->line, e->col,
                              "'%s' என்ற பெயர் வரையறுக்கப்படவில்லை",
                              "'%s' என்பதை '%s' என்று எழுத வேண்டுமா?",
                              e->as.name, e->as.name, near);
                } else {
                    sem_error(c, TA_ERR_SEMANTIC + 1, e->line, e->col,
                              "'%s' என்ற பெயர் வரையறுக்கப்படவில்லை",
                              "பயன்படுத்தும் முன் 'மாறி %s = ...' என வரையறுங்கள்",
                              e->as.name, e->as.name);
                }
                e->sym = NULL;
                e->ty = ta_ty_error();
                break;
            }
            if (!allow_module &&
                (sym->kind == TA_SYM_MODULE || sym->kind == TA_SYM_TYPE)) {
                sem_error(c, TA_ERR_SEMANTIC + 8, e->line, e->col,
                          "'%s' ஐ மதிப்பாகப் பயன்படுத்த முடியாது",
                          sym->kind == TA_SYM_MODULE
                              ? "'%s.<செயலி>(...)' என்று அழைக்கவும்"
                              : "'%s' என்பது வகை (type) பெயர்; : க்குப் பிறகு மட்டுமே பயன்படும்",
                          e->as.name);
            }
            e->sym = sym;
            e->ty = sym->type;
            sym->used = true;
            break;
        }
        case TX_BINARY:
            resolve_expr(c, e->as.bin.lhs);
            resolve_expr(c, e->as.bin.rhs);
            break;
        case TX_UNARY:
            resolve_expr(c, e->as.un.operand);
            break;
        case TX_CALL: {
            resolve_expr(c, e->as.call.callee);
            TaExpr *cal = e->as.call.callee;
            if (cal->sym &&
                cal->sym->kind != TA_SYM_FUNC && cal->sym->kind != TA_SYM_BUILTIN_FUNC) {
                sem_error(c, TA_ERR_TYPE + 5, e->line, e->col, "'(...)' மூலம் அழைக்க முடியாதது",
                          "செயலி பெயர் மட்டுமே அழைக்கப்படலாம்");
            }
            for (size_t i = 0; i < e->as.call.nargs; i++) resolve_expr(c, e->as.call.args[i]);
            break;
        }
        case TX_INDEX:
            resolve_expr(c, e->as.index.base);
            resolve_expr(c, e->as.index.index);
            break;
        case TX_MEMBER: {
            TaExpr *obj = e->as.member.obj;
            resolve_expr_ex(c, obj, true);
            if (obj->kind != TX_IDENT || !obj->sym ||
                obj->sym->kind != TA_SYM_MODULE) {
                sem_error(c, TA_ERR_SEMANTIC + 8, e->line, e->col,
                          "'.<பெயர்>' அணுகல் இங்கு பொருந்தாது",
                          "தற்போது கணிதம் மற்றும் உரை தொகுதிகள் மட்டுமே உள்ளன; "
                          "எ.கா.: கணிதம்.வர்க்கமூலம்(x)");
                e->ty = ta_ty_error();
                break;
            }
            TaSymbol *msym = ta_scope_lookup(obj->sym->members, e->as.member.member);
            if (!msym) {
                sem_error(c, TA_ERR_SEMANTIC + 8, e->line, e->col,
                          "'%s' தொகுதியில் '%s' இல்லை",
                          "கிடைக்கும் செயலிகள்: docs/standard-library.md ஐப் பார்க்கவும்",
                          obj->sym->name, e->as.member.member);
                e->ty = ta_ty_error();
                break;
            }
            e->sym = msym;
            break;
        }
        case TX_LIST:
            for (size_t i = 0; i < e->as.list.count; i++) resolve_expr(c, e->as.list.elems[i]);
            break;
        case TX_DICT:
            for (size_t i = 0; i < e->as.dict.count; i++) {
                resolve_expr(c, e->as.dict.keys[i]);
                resolve_expr(c, e->as.dict.vals[i]);
            }
            break;
    }
}

bool ta_semantic_run(const char *file, TaProgram *prog, TaDiagnostics *diag,
                     TaScope **out_globals, int *out_top_slots) {
    SemCtx c;
    memset(&c, 0, sizeof(c));
    c.diag = diag;
    c.file = file;
    c.globals = ta_scope_new(NULL);
    c.cur_scope = c.globals;
    register_builtins(c.globals);

    for (size_t i = 0; i < prog->count; i++) {
        TaStmt *st = prog->items[i];
        if (st->kind != ST_FUNCDEF) continue;
        TaFuncDef *fd = st->as.funcdef;
        TaSymbol *existing = ta_scope_lookup_local(c.globals, fd->name);
        if (existing) {
            ta_diag_report(diag, TA_ERR_SEMANTIC + 2, file, st->line, st->col,
                           "'%s' ஏற்கனவே வரையறுக்கப்பட்டுள்ளது",
                           "%s பெயரில் இரண்டு வரையறைகள் இருக்க முடியாது", fd->name);
            continue;
        }
        TaSymbol *fsym = ta_symbol_new(fd->name, TA_SYM_FUNC, st->line, st->col);
        fsym->fn.has_annotation = (fd->ret_type != NULL);
        fsym->fn.index = c.next_func_index++;
        fsym->fn.decl = st;
        if (strcmp(fd->name, "முதன்மை") == 0 && fd->nparams == 0) fsym->fn.is_main = true;
        ta_scope_declare(c.globals, fsym);
    }

    for (size_t i = 0; i < prog->count; i++) {
        TaStmt *st = prog->items[i];
        if (st->kind == ST_FUNCDEF) {
            TaFuncDef *fd = st->as.funcdef;
            TaSymbol *fsym = ta_scope_lookup_local(c.globals, fd->name);
            if (!fsym || fsym->kind != TA_SYM_FUNC) continue;
            fd->ret_resolved = ta_resolve_type_spec(fd->ret_type, c.globals, diag, file);
            analyze_func(&c, fd, fsym);
        } else {
            analyze_stmt(&c, st);
        }
    }

    if (out_top_slots) *out_top_slots = c.next_slot;
    if (out_globals) *out_globals = c.globals;
    return true;
}
