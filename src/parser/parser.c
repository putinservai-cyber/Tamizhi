#include "ta_parser.h"
#include "ta_utf8.h"

typedef struct {
    const TaTokenList *toks;
    size_t pos;
    const char *file;
    TaDiagnostics *diag;
    bool incomplete;
    int depth;
    bool too_many;
} Parser;

static TaToken *peek(Parser *p) {
    return &p->toks->items[p->pos];
}

static bool check(Parser *p, TaTokenType t) {
    return peek(p)->type == t;
}

static TaToken advance(Parser *p) {
    TaToken tk = *peek(p);
    if (p->pos + 1 < p->toks->count) p->pos++;
    return tk;
}

static bool match(Parser *p, TaTokenType t) {
    if (check(p, t)) {
        advance(p);
        return true;
    }
    return false;
}

static char *safe_trunc(const char *s, size_t max_bytes) {
    static _Thread_local char tbuf[192];
    size_t n = ta_utf8_truncate_bytes(s ? s : "", max_bytes);
    if (n >= sizeof(tbuf)) n = sizeof(tbuf) - 1;
    memcpy(tbuf, s ? s : "", n);
    tbuf[n] = 0;
    return tbuf;
}

static char *describe_token(const TaToken *tk) {
    static _Thread_local char buf[256];
    switch (tk->type) {
        case TK_EOF: snprintf(buf, sizeof(buf), "'நிரல் முடிவு'"); break;
        case TK_NEWLINE: snprintf(buf, sizeof(buf), "'வரி முடிவு'"); break;
        case TK_INDENT: snprintf(buf, sizeof(buf), "'உள்தள்ளல்'"); break;
        case TK_DEDENT: snprintf(buf, sizeof(buf), "'வெளிதள்ளல்'"); break;
        case TK_INT: snprintf(buf, sizeof(buf), "எண் '%lld'", tk->v.i); break;
        case TK_FLOAT: snprintf(buf, sizeof(buf), "பின்ன எண்"); break;
        case TK_STRING:
            snprintf(buf, sizeof(buf), "சரம் \"%s\"%s", safe_trunc(tk->text, 60),
                     tk->text_len > 60 ? "…" : "");
            break;
        case TK_CHAR: snprintf(buf, sizeof(buf), "எழுத்து"); break;
        case TK_IDENT:
            snprintf(buf, sizeof(buf), "'%s'%s", safe_trunc(tk->text, 80),
                     tk->text && strlen(tk->text) > 80 ? "…" : "");
            break;
        default: snprintf(buf, sizeof(buf), "'%s'", ta_token_type_name(tk->type)); break;
    }
    return buf;
}

static void syn_error(Parser *p, int code, const TaToken *at, const char *msg, const char *hint) {
    if (p->too_many) return;
    if (ta_diag_error_count(p->diag) >= 50) {
        p->too_many = true;
        return;
    }
    ta_diag_report(p->diag, code, p->file, at->line, at->col, msg, "%s", hint ? hint : "");
}

static void expected_error(Parser *p, const char *what, const char *hint) {
    TaToken *tk = peek(p);
    char msg[512];
    snprintf(msg, sizeof(msg), "%s எதிர்பார்க்கப்பட்டது, ஆனால் %s கிடைத்தது", what,
             describe_token(tk));
    if (!hint && tk->type == TK_EOF) hint = "நிரல் திடீரென முடிந்துவிட்டது; ஒரு வரி அல்லது அடைப்புக்குறி விடுபட்டிருக்கலாம்";
    syn_error(p, TA_ERR_PARSE + 1, tk, msg, hint);
}

static bool expect(Parser *p, TaTokenType t, const char *what, const char *hint) {
    if (check(p, t)) {
        advance(p);
        return true;
    }
    expected_error(p, what, hint);
    return false;
}

static void panic_sync_stmt(Parser *p) {
    int guard = 0;
    for (;;) {
        TaToken *tk = peek(p);
        TaTokenType t = tk->type;
        if (t == TK_NEWLINE) {
            advance(p);
            return;
        }
        if (t == TK_DEDENT || t == TK_EOF || t == TK_INDENT) return;
        /* Resume at the next top-level definition so a syntax error in one
           function does not abort parsing of the rest of the file. */
        if (tk->col == 0 &&
            (t == TK_FUNC || t == TK_VAR || t == TK_CONST || t == TK_IF ||
             t == TK_WHILE || t == TK_FOR || t == TK_RETURN || t == TK_BREAK ||
             t == TK_CONTINUE)) {
            return;
        }
        advance(p);
        if (++guard > 4096) return;
    }
}

TaExpr *ta_expr_new(TaExprKind kind, int line, int col);
TaStmt *ta_stmt_new(TaStmtKind kind, int line, int col);
TaBlock *ta_block_new(void);
void ta_block_add(TaBlock *b, TaStmt *st);
void ta_program_add(TaProgram *p, TaStmt *st);

static TaExpr *parse_expr(Parser *p);

static TaTypeSpec *parse_type(Parser *p) {
    TaToken *tk = peek(p);
    int line = tk->line, col = tk->col;
    if (match(p, TK_LBRACKET)) {
        TaTypeSpec *elem = parse_type(p);
        if (!expect(p, TK_RBRACKET, "']'", NULL)) {
            ta_typespec_free(elem);
            return NULL;
        }
        return ta_typespec_list(elem, line, col);
    }
    if (match(p, TK_LBRACE)) {
        TaTypeSpec *k = parse_type(p);
        if (!k || !expect(p, TK_COLON, "':'", NULL)) {
            ta_typespec_free(k);
            return NULL;
        }
        TaTypeSpec *v = parse_type(p);
        if (!v || !expect(p, TK_RBRACE, "'}'", NULL)) {
            ta_typespec_free(k);
            ta_typespec_free(v);
            return NULL;
        }
        return ta_typespec_dict(k, v, line, col);
    }
    if (check(p, TK_IDENT)) {
        TaToken id = advance(p);
        return ta_typespec_name(id.text, id.line, id.col);
    }
    if (check(p, TK_NULL)) {
        advance(p);
        return ta_typespec_name("வெற்று", line, col);
    }
    expected_error(p, "வகை (type) பெயர்",
                   "எ.கா.: முழுஎண், மிதவை, பூலியன், எழுத்து, உரை, [முழுஎண்], {உரை: முழுஎண்}");
    return NULL;
}

static TaBlock *parse_suite(Parser *p);

static TaStmt *parse_statement(Parser *p);

static TaBlock *parse_suite(Parser *p) {
    if (match(p, TK_NEWLINE)) {
        if (!expect(p, TK_INDENT, "உள்தள்ளல் (indented block)",
                    "':' க்குப் பிறகு அடுத்த வரியில் 4 இடைவெளி உள்தள்ளல் தேவை")) {
            p->incomplete = true;
            return NULL;
        }
        TaBlock *block = ta_block_new();
        size_t guard = 0;
        while (!check(p, TK_DEDENT) && !check(p, TK_EOF)) {
            size_t before = p->pos;
            TaStmt *st = parse_statement(p);
            if (st) ta_block_add(block, st);
            if (p->too_many) break;
            if (p->pos == before) {
                if (++guard > 64) break;
                advance(p);
            }
        }
        if (!match(p, TK_DEDENT)) {
            p->incomplete = true;
        }
        return block;
    }
    TaStmt *st = parse_statement(p);
    if (!st) return NULL;
    if (!ta_stmt_is_simple(st)) {
        syn_error(p, TA_ERR_PARSE + 1, peek(p),
                  "இந்த வாக்கியத்தை ஒரே வரியில் எழுத முடியாது",
                  "பல வரி கூறுகளுக்கு ':' க்குப் பிறகு புதிய வரியில் உள்தள்ளலுடன் எழுதுங்கள்");
        ta_stmt_free(st);
        return NULL;
    }
    TaBlock *block = ta_block_new();
    ta_block_add(block, st);
    return block;
}

static TaExpr *parse_primary(Parser *p) {
    TaToken *tk = peek(p);
    int line = tk->line, col = tk->col;
    switch (tk->type) {
        case TK_INT: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_INT, line, col);
            e->as.ival = tk->v.i;
            return e;
        }
        case TK_FLOAT: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_FLOAT, line, col);
            e->as.fval = tk->v.f;
            return e;
        }
        case TK_CHAR: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_CHAR, line, col);
            e->as.cval = tk->v.cp;
            return e;
        }
        case TK_TRUE:
        case TK_FALSE: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_BOOL, line, col);
            e->as.ival = (tk->type == TK_TRUE);
            return e;
        }
        case TK_NULL: {
            advance(p);
            return ta_expr_new(TX_NULL, line, col);
        }
        case TK_STRING: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_STRING, line, col);
            e->as.str.sval = ta_xstrdup(tk->text ? tk->text : "");
            e->as.str.slen = tk->text_len;
            return e;
        }
        case TK_IDENT: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_IDENT, line, col);
            e->as.name = ta_xstrdup(tk->text);
            return e;
        }
        case TK_LPAREN: {
            advance(p);
            TaExpr *e = parse_expr(p);
            if (!expect(p, TK_RPAREN, "')'", NULL)) {
                ta_expr_free(e);
                return NULL;
            }
            return e;
        }
        case TK_LBRACKET: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_LIST, line, col);
            size_t cap = 0;
            if (!check(p, TK_RBRACKET)) {
                for (;;) {
                    if (match(p, TK_COMMA)) continue;
                    if (check(p, TK_RBRACKET)) break;
                    TaExpr *el = parse_expr(p);
                    if (!el) {
                        ta_expr_free(e);
                        return NULL;
                    }
                    if (e->as.list.count == cap) {
                        cap = cap ? cap * 2 : 4;
                        e->as.list.elems = ta_xrealloc(e->as.list.elems, cap * sizeof(TaExpr *));
                    }
                    e->as.list.elems[e->as.list.count++] = el;
                    if (!match(p, TK_COMMA)) break;
                }
            }
            if (!expect(p, TK_RBRACKET, "']'", "பட்டியலை ']' ஆல் மூடுங்கள்")) {
                ta_expr_free(e);
                return NULL;
            }
            return e;
        }
        case TK_LBRACE: {
            advance(p);
            TaExpr *e = ta_expr_new(TX_DICT, line, col);
            size_t cap = 0;
            if (!check(p, TK_RBRACE)) {
                for (;;) {
                    if (match(p, TK_COMMA)) continue;
                    if (check(p, TK_RBRACE)) break;
                    TaExpr *k = parse_expr(p);
                    if (!k || !expect(p, TK_COLON, "':'", "அகராதி உறுப்பு: {\"சாவி\": மதிப்பு}")) {
                        ta_expr_free(k);
                        ta_expr_free(e);
                        return NULL;
                    }
                    TaExpr *v = parse_expr(p);
                    if (!v) {
                        ta_expr_free(k);
                        ta_expr_free(e);
                        return NULL;
                    }
                    if (e->as.dict.count == cap) {
                        cap = cap ? cap * 2 : 4;
                        e->as.dict.keys = ta_xrealloc(e->as.dict.keys, cap * sizeof(TaExpr *));
                        e->as.dict.vals = ta_xrealloc(e->as.dict.vals, cap * sizeof(TaExpr *));
                    }
                    e->as.dict.keys[e->as.dict.count] = k;
                    e->as.dict.vals[e->as.dict.count] = v;
                    e->as.dict.count++;
                    if (!match(p, TK_COMMA)) break;
                }
            }
            if (!expect(p, TK_RBRACE, "'}'", "அகராதியை '}' ஆல் மூடுங்கள்")) {
                ta_expr_free(e);
                return NULL;
            }
            return e;
        }
        default:
            expected_error(p, "ஒரு கோவை (expression)", NULL);
            return NULL;
    }
}

static TaExpr *parse_postfix(Parser *p) {
    TaExpr *e = parse_primary(p);
    if (!e) return NULL;
    for (;;) {
        if (check(p, TK_LPAREN)) {
            TaToken lp = advance(p);
            if (e->kind != TX_IDENT && e->kind != TX_MEMBER) {
                syn_error(p, TA_ERR_PARSE + 1, &lp, "இதை செயலியாக (function) அழைக்க முடியாது",
                          "செயலி பெயருக்குப் பிறகு மட்டுமே '(...)' வரும்");
                ta_expr_free(e);
                return NULL;
            }
            TaExpr *call = ta_expr_new(TX_CALL, lp.line, lp.col);
            call->as.call.callee = e;
            size_t cap = 0;
            if (!check(p, TK_RPAREN)) {
                for (;;) {
                    if (match(p, TK_COMMA)) continue;
                    if (check(p, TK_RPAREN)) break;
                    TaExpr *arg = parse_expr(p);
                    if (!arg) {
                        ta_expr_free(call);
                        return NULL;
                    }
                    if (call->as.call.nargs == cap) {
                        cap = cap ? cap * 2 : 4;
                        call->as.call.args =
                            ta_xrealloc(call->as.call.args, cap * sizeof(TaExpr *));
                    }
                    call->as.call.args[call->as.call.nargs++] = arg;
                    if (!match(p, TK_COMMA)) break;
                }
            }
            if (!expect(p, TK_RPAREN, "')'", "செயலி அழைப்பை ')' ஆல் மூடுங்கள்")) {
                ta_expr_free(call);
                return NULL;
            }
            e = call;
            continue;
        }
        if (check(p, TK_LBRACKET)) {
            TaToken lb = advance(p);
            TaExpr *idx = parse_expr(p);
            if (!idx || !expect(p, TK_RBRACKET, "']'", "அணுகலை ']' ஆல் மூடுங்கள்")) {
                ta_expr_free(idx);
                ta_expr_free(e);
                return NULL;
            }
            TaExpr *ix = ta_expr_new(TX_INDEX, lb.line, lb.col);
            ix->as.index.base = e;
            ix->as.index.index = idx;
            e = ix;
            continue;
        }
        if (check(p, TK_DOT)) {
            TaToken dot = advance(p);
            if (!check(p, TK_IDENT)) {
                expected_error(p, "உறுப்பு பெயர்", "'.' க்குப் பிறகு ஒரு பெயர் வர வேண்டும்");
                ta_expr_free(e);
                return NULL;
            }
            TaToken id = advance(p);
            TaExpr *m = ta_expr_new(TX_MEMBER, dot.line, dot.col);
            m->as.member.obj = e;
            m->as.member.member = ta_xstrdup(id.text);
            e = m;
            continue;
        }
        break;
    }
    return e;
}

static TaExpr *parse_unary(Parser *p) {
    if (check(p, TK_MINUS)) {
        TaToken tk = advance(p);
        TaExpr *op = parse_unary(p);
        if (!op) return NULL;
        if (op->kind == TX_INT) {
            op->as.ival = -op->as.ival;
            op->line = tk.line;
            op->col = tk.col;
            return op;
        }
        if (op->kind == TX_FLOAT) {
            op->as.fval = -op->as.fval;
            op->line = tk.line;
            op->col = tk.col;
            return op;
        }
        TaExpr *e = ta_expr_new(TX_UNARY, tk.line, tk.col);
        e->as.un.op = TU_NEG;
        e->as.un.operand = op;
        return e;
    }
    if (check(p, TK_PLUS)) {
        advance(p);
        return parse_unary(p);
    }
    return parse_postfix(p);
}

static TaExpr *parse_term(Parser *p) {
    TaExpr *lhs = parse_unary(p);
    if (!lhs) return NULL;
    for (;;) {
        TaBinOp op;
        if (check(p, TK_STAR)) op = TB_MUL;
        else if (check(p, TK_SLASH)) op = TB_DIV;
        else if (check(p, TK_PERCENT)) op = TB_MOD;
        else break;
        advance(p);
        TaExpr *rhs = parse_unary(p);
        if (!rhs) {
            ta_expr_free(lhs);
            return NULL;
        }
        TaExpr *bin = ta_expr_new(TX_BINARY, lhs->line, lhs->col);
        bin->as.bin.op = op;
        bin->as.bin.lhs = lhs;
        bin->as.bin.rhs = rhs;
        lhs = bin;
    }
    return lhs;
}

static TaExpr *parse_additive(Parser *p) {
    TaExpr *lhs = parse_term(p);
    if (!lhs) return NULL;
    for (;;) {
        TaBinOp op;
        if (check(p, TK_PLUS)) op = TB_ADD;
        else if (check(p, TK_MINUS)) op = TB_SUB;
        else break;
        advance(p);
        TaExpr *rhs = parse_term(p);
        if (!rhs) {
            ta_expr_free(lhs);
            return NULL;
        }
        TaExpr *bin = ta_expr_new(TX_BINARY, lhs->line, lhs->col);
        bin->as.bin.op = op;
        bin->as.bin.lhs = lhs;
        bin->as.bin.rhs = rhs;
        lhs = bin;
    }
    return lhs;
}

static TaExpr *parse_comparison(Parser *p) {
    TaExpr *lhs = parse_additive(p);
    if (!lhs) return NULL;
    for (;;) {
        TaBinOp op;
        switch (peek(p)->type) {
            case TK_EQ: op = TB_EQ; break;
            case TK_NE: op = TB_NE; break;
            case TK_LT: op = TB_LT; break;
            case TK_GT: op = TB_GT; break;
            case TK_LE: op = TB_LE; break;
            case TK_GE: op = TB_GE; break;
            default: return lhs;
        }
        advance(p);
        TaExpr *rhs = parse_additive(p);
        if (!rhs) {
            ta_expr_free(lhs);
            return NULL;
        }
        TaExpr *bin = ta_expr_new(TX_BINARY, lhs->line, lhs->col);
        bin->as.bin.op = op;
        bin->as.bin.lhs = lhs;
        bin->as.bin.rhs = rhs;
        lhs = bin;
    }
}

static TaExpr *parse_not(Parser *p) {
    if (check(p, TK_NOT)) {
        TaToken tk = advance(p);
        TaExpr *op = parse_not(p);
        if (!op) return NULL;
        TaExpr *e = ta_expr_new(TX_UNARY, tk.line, tk.col);
        e->as.un.op = TU_NOT;
        e->as.un.operand = op;
        return e;
    }
    return parse_comparison(p);
}

static TaExpr *parse_and(Parser *p) {
    TaExpr *lhs = parse_not(p);
    if (!lhs) return NULL;
    while (check(p, TK_AND)) {
        advance(p);
        TaExpr *rhs = parse_not(p);
        if (!rhs) {
            ta_expr_free(lhs);
            return NULL;
        }
        TaExpr *bin = ta_expr_new(TX_BINARY, lhs->line, lhs->col);
        bin->as.bin.op = TB_AND;
        bin->as.bin.lhs = lhs;
        bin->as.bin.rhs = rhs;
        lhs = bin;
    }
    return lhs;
}

static TaExpr *parse_expr(Parser *p) {
    TaExpr *lhs = parse_and(p);
    if (!lhs) return NULL;
    while (check(p, TK_OR)) {
        advance(p);
        TaExpr *rhs = parse_and(p);
        if (!rhs) {
            ta_expr_free(lhs);
            return NULL;
        }
        TaExpr *bin = ta_expr_new(TX_BINARY, lhs->line, lhs->col);
        bin->as.bin.op = TB_OR;
        bin->as.bin.lhs = lhs;
        bin->as.bin.rhs = rhs;
        lhs = bin;
    }
    return lhs;
}

static TaStmt *parse_funcdef(Parser *p) {
    TaToken kw = advance(p);
    if (p->depth > 0) {
        syn_error(p, TA_ERR_PARSE + 4, &kw,
                  "செயலி வரையறை வேறு செயலிக்குள் இருக்க முடியாது",
                  "அனைத்து செயலிகளையும் நிரலின் மேல் நிலையில் (top level) எழுதுங்கள்");
    }
    TaFuncDef *fd = ta_xcalloc(1, sizeof(TaFuncDef));
    TaToken name;
    if (check(p, TK_IDENT)) {
        name = advance(p);
        fd->name = ta_xstrdup(name.text);
    } else {
        expected_error(p, "செயலி பெயர்", "எ.கா.: செயலி கூட்டு(அ, ஆ)");
        free(fd);
        return NULL;
    }
    if (!expect(p, TK_LPAREN, "'('", "செயலி பெயருக்குப் பிறகு அளவுருக்கள் வைக்கவும்")) {
        free(fd->name);
        free(fd);
        return NULL;
    }
    size_t pcap = 0;
    if (!check(p, TK_RPAREN)) {
        for (;;) {
            if (match(p, TK_COMMA)) continue;
            if (check(p, TK_RPAREN)) break;
            if (!check(p, TK_IDENT)) {
                expected_error(p, "அளவுரு பெயர்", NULL);
                free(fd->name);
                free(fd);
                return NULL;
            }
            TaToken pn = advance(p);
            TaTypeSpec *pt = NULL;
            if (match(p, TK_COLON)) {
                pt = parse_type(p);
                if (!pt) {
                    for (size_t i = 0; i < fd->nparams; i++) {
                        free(fd->params[i].name);
                        ta_typespec_free(fd->params[i].type);
                    }
                    free(fd->params);
                    free(fd->name);
                    free(fd);
                    return NULL;
                }
            }
            if (fd->nparams == pcap) {
                pcap = pcap ? pcap * 2 : 4;
                fd->params = ta_xrealloc(fd->params, pcap * sizeof(TaParam));
            }
            fd->params[fd->nparams].name = ta_xstrdup(pn.text);
            fd->params[fd->nparams].type = pt;
            fd->params[fd->nparams].line = pn.line;
            fd->params[fd->nparams].col = pn.col;
            fd->nparams++;
            if (!match(p, TK_COMMA)) break;
        }
    }
    if (!expect(p, TK_RPAREN, "')'", NULL)) {
        for (size_t i = 0; i < fd->nparams; i++) {
            free(fd->params[i].name);
            ta_typespec_free(fd->params[i].type);
        }
        free(fd->params);
        free(fd->name);
        free(fd);
        return NULL;
    }
    if (match(p, TK_ARROW)) {
        fd->ret_type = parse_type(p);
        if (!fd->ret_type) {
            for (size_t i = 0; i < fd->nparams; i++) {
                free(fd->params[i].name);
                ta_typespec_free(fd->params[i].type);
            }
            free(fd->params);
            free(fd->name);
            free(fd);
            return NULL;
        }
    }
    if (!expect(p, TK_COLON, "':'",
                "செயலி தலைப்பின் இறுதியில் ':' இருக்க வேண்டும், அல்லது '-> வகை' மூலம் திரும்பும் வகையைக் குறிக்கவும்")) {
        for (size_t i = 0; i < fd->nparams; i++) {
            free(fd->params[i].name);
            ta_typespec_free(fd->params[i].type);
        }
        free(fd->params);
        free(fd->name);
        ta_typespec_free(fd->ret_type);
        free(fd);
        return NULL;
    }
    p->depth++;
    fd->body = parse_suite(p);
    p->depth--;
    if (!fd->body) {
        for (size_t i = 0; i < fd->nparams; i++) {
            free(fd->params[i].name);
            ta_typespec_free(fd->params[i].type);
        }
        free(fd->params);
        free(fd->name);
        ta_typespec_free(fd->ret_type);
        free(fd);
        return NULL;
    }
    TaStmt *st = ta_stmt_new(ST_FUNCDEF, kw.line, kw.col);
    st->as.funcdef = fd;
    return st;
}

static TaStmt *parse_vardecl(Parser *p) {
    TaToken kw = advance(p);
    if (!check(p, TK_IDENT)) {
        expected_error(p, "மாறி பெயர்", NULL);
        return NULL;
    }
    TaToken name = advance(p);
    TaTypeSpec *ty = NULL;
    if (match(p, TK_COLON)) {
        ty = parse_type(p);
        if (!ty) return NULL;
    }
    if (!check(p, TK_ASSIGN)) {
        expected_error(p, "'=' மற்றும் ஆரம்ப மதிப்பு",
                       "எ.கா.: மாறி x = 10  அல்லது  மாறி x: முழுஎண் = 10");
        ta_typespec_free(ty);
        return NULL;
    }
    advance(p);
    TaExpr *init = parse_expr(p);
    if (!init) {
        ta_typespec_free(ty);
        return NULL;
    }
    TaStmt *st = ta_stmt_new(ST_VARDECL, kw.line, kw.col);
    st->as.vardecl.is_const = (kw.type == TK_CONST);
    st->as.vardecl.name = ta_xstrdup(name.text);
    st->as.vardecl.type = ty;
    st->as.vardecl.init = init;
    return st;
}

static TaStmt *parse_if(Parser *p) {
    TaToken kw = advance(p);
    TaExpr *cond = parse_expr(p);
    if (!cond) return NULL;
    if (!expect(p, TK_COLON, "':'", "என்றால் நிபந்தனைக்குப் பிறகு ':' இருக்க வேண்டும்")) {
        ta_expr_free(cond);
        return NULL;
    }
    TaBlock *then_body = parse_suite(p);
    if (!then_body) {
        ta_expr_free(cond);
        return NULL;
    }
    TaStmt *st = ta_stmt_new(ST_IF, kw.line, kw.col);
    st->as.ifstmt.cond = cond;
    st->as.ifstmt.then_body = then_body;

    typedef struct {
        TaExpr *cond;
        TaBlock *body;
    } Elif;

    Elif **elifs = NULL;
    size_t nelifs = 0, cap = 0;
    TaBlock *else_body = NULL;
    for (;;) {
        if (!check(p, TK_ELSE)) break;
        advance(p);
        if (check(p, TK_IF)) {
            advance(p);
            TaExpr *ec = parse_expr(p);
            if (!ec) break;
            if (!expect(p, TK_COLON, "':'", "இல்லையெனில் என்றால் நிபந்தனைக்குப் பிறகு ':' இருக்க வேண்டும்")) {
                ta_expr_free(ec);
                break;
            }
            TaBlock *eb = parse_suite(p);
            if (!eb) {
                ta_expr_free(ec);
                break;
            }
            if (nelifs == cap) {
                cap = cap ? cap * 2 : 4;
                elifs = ta_xrealloc(elifs, cap * sizeof(Elif *));
            }
            {
                Elif *el = ta_xmalloc(sizeof(Elif));
                el->cond = ec;
                el->body = eb;
                elifs[nelifs++] = el;
            }
            continue;
        }
        if (!expect(p, TK_COLON, "':'", "இல்லையெனில் க்குப் பிறகு ':' இருக்க வேண்டும்")) break;
        else_body = parse_suite(p);
        break;
    }
    st->as.ifstmt.elifs = (void *)elifs;
    st->as.ifstmt.nelifs = nelifs;
    st->as.ifstmt.else_body = else_body;
    return st;
}

static TaStmt *parse_while(Parser *p) {
    TaToken kw = advance(p);
    TaExpr *cond = parse_expr(p);
    if (!cond) return NULL;
    if (!expect(p, TK_COLON, "':'", "வரை நிபந்தனைக்குப் பிறகு ':' இருக்க வேண்டும்")) {
        ta_expr_free(cond);
        return NULL;
    }
    TaBlock *body = parse_suite(p);
    if (!body) {
        ta_expr_free(cond);
        return NULL;
    }
    TaStmt *st = ta_stmt_new(ST_WHILE, kw.line, kw.col);
    st->as.whilestmt.cond = cond;
    st->as.whilestmt.body = body;
    return st;
}

static TaStmt *parse_foreach(Parser *p) {
    TaToken kw = advance(p);
    if (!check(p, TK_IDENT)) {
        expected_error(p, "மாறி பெயர்", "எ.கா.: ஒவ்வொன்றும் எண் இல் வரம்பு(10)");
        return NULL;
    }
    TaToken name = advance(p);
    if (!expect(p, TK_IN, "'இல்'",
                "ஒவ்வொன்றும் <பெயர்> இல் <தொகுப்பு> என்ற வடிவத்தைப் பயன்படுத்துங்கள்")) {
        return NULL;
    }
    TaExpr *iter = parse_expr(p);
    if (!iter) return NULL;
    if (!expect(p, TK_COLON, "':'", NULL)) {
        ta_expr_free(iter);
        return NULL;
    }
    TaBlock *body = parse_suite(p);
    if (!body) {
        ta_expr_free(iter);
        return NULL;
    }
    TaStmt *st = ta_stmt_new(ST_FOREACH, kw.line, kw.col);
    st->as.foreach.varname = ta_xstrdup(name.text);
    st->as.foreach.iterable = iter;
    st->as.foreach.body = body;
    st->as.foreach.line_var = name.line;
    st->as.foreach.col_var = name.col;
    return st;
}

bool ta_stmt_is_simple(const TaStmt *st) {
    switch (st->kind) {
        case ST_VARDECL:
        case ST_ASSIGN:
        case ST_EXPR:
        case ST_RETURN:
        case ST_BREAK:
        case ST_CONTINUE:
            return true;
        default:
            return false;
    }
}

static TaStmt *parse_statement(Parser *p) {
    if (check(p, TK_NEWLINE)) {
        advance(p);
        return NULL;
    }
    TaTokenType t = peek(p)->type;
    switch (t) {
        case TK_FUNC: return parse_funcdef(p);
        case TK_VAR:
        case TK_CONST: return parse_vardecl(p);
        case TK_IF: return parse_if(p);
        case TK_WHILE: return parse_while(p);
        case TK_FOR: return parse_foreach(p);
        case TK_RETURN: {
            TaToken kw = advance(p);
            TaExpr *val = NULL;
            if (!check(p, TK_NEWLINE) && !check(p, TK_EOF) && !check(p, TK_DEDENT)) {
                val = parse_expr(p);
                if (!val) {
                    panic_sync_stmt(p);
                    return NULL;
                }
            }
            TaStmt *st = ta_stmt_new(ST_RETURN, kw.line, kw.col);
            st->as.ret.value = val;
            return st;
        }
        case TK_BREAK: {
            TaToken kw = advance(p);
            TaStmt *st = ta_stmt_new(ST_BREAK, kw.line, kw.col);
            return st;
        }
        case TK_CONTINUE: {
            TaToken kw = advance(p);
            TaStmt *st = ta_stmt_new(ST_CONTINUE, kw.line, kw.col);
            return st;
        }
        default: break;
    }

    int start_line = peek(p)->line;
    TaExpr *e = parse_expr(p);
    if (!e) {
        panic_sync_stmt(p);
        return NULL;
    }
    if (check(p, TK_ASSIGN) || check(p, TK_PLUSEQ) || check(p, TK_MINUSEQ) ||
        check(p, TK_STAREQ) || check(p, TK_SLASHEQ)) {
        TaAssignOp op = TA_ASSIGN;
        switch (peek(p)->type) {
            case TK_PLUSEQ: op = TA_PLUSEQ; break;
            case TK_MINUSEQ: op = TA_MINUSEQ; break;
            case TK_STAREQ: op = TA_STAREQ; break;
            case TK_SLASHEQ: op = TA_SLASHEQ; break;
            default: op = TA_ASSIGN; break;
        }
        advance(p);
        if (e->kind != TX_IDENT && e->kind != TX_INDEX) {
            syn_error(p, TA_ERR_PARSE + 3, peek(p),
                      "இங்கு மதிப்பு சேமிக்க முடியாது (invalid assignment target)",
                      "மாறி பெயர் அல்லது பட்டியல்/அகராதி அணுகல் மட்டுமே '=' இன் இடதுபுறம் வர முடியும்");
            ta_expr_free(e);
            panic_sync_stmt(p);
            return NULL;
        }
        TaExpr *val = parse_expr(p);
        if (!val) {
            ta_expr_free(e);
            panic_sync_stmt(p);
            return NULL;
        }
        TaStmt *st = ta_stmt_new(ST_ASSIGN, start_line, e->col);
        st->as.assign.target = e;
        st->as.assign.op = op;
        st->as.assign.value = val;
        return st;
    }
    TaStmt *st = ta_stmt_new(ST_EXPR, e->line, e->col);
    st->as.exprstmt.expr = e;
    return st;
}

TaProgram ta_parse_tokens(const char *filename, const TaTokenList *tokens,
                          TaDiagnostics *diag, TaParseResult *result) {
    Parser p;
    memset(&p, 0, sizeof(p));
    p.toks = tokens;
    p.file = filename;
    p.diag = diag;

    TaProgram prog;
    memset(&prog, 0, sizeof(prog));

    size_t guard = 0;
    while (!check(&p, TK_EOF)) {
        if (match(&p, TK_DEDENT)) continue;
        size_t before = p.pos;
        TaStmt *st = parse_statement(&p);
        if (st) ta_program_add(&prog, st);
        if (p.too_many) break;
        if (p.pos == before) {
            if (++guard > 64) break;
            advance(&p);
        }
    }
    if (result) result->incomplete = p.incomplete;
    return prog;
}
