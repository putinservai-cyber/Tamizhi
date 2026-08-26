#include "ta_ast.h"
#include "ta_utf8.h"

typedef enum {
    PREC_NONE = 0,
    PREC_OR = 1,
    PREC_AND = 2,
    PREC_NOT = 3,
    PREC_CMP = 4,
    PREC_ADD = 5,
    PREC_MUL = 6,
    PREC_UNARY = 7
} Prec;

static Prec binop_prec(TaBinOp op) {
    switch (op) {
        case TB_OR: return PREC_OR;
        case TB_AND: return PREC_AND;
        case TB_EQ: case TB_NE: case TB_LT: case TB_GT: case TB_LE: case TB_GE:
            return PREC_CMP;
        case TB_ADD: case TB_SUB: return PREC_ADD;
        default: return PREC_MUL;
    }
}

static void print_type_spec(const TaTypeSpec *ts, TaStrBuf *out);

static void print_expr(const TaExpr *e, int parent_prec, TaStrBuf *out) {
    if (!e) {
        ta_sb_puts(out, "<வெற்று>");
        return;
    }
    switch (e->kind) {
        case TX_INT:
            ta_sb_printf(out, "%lld", e->as.ival);
            break;
        case TX_FLOAT:
            ta_sb_printf(out, "%g", e->as.fval);
            break;
        case TX_BOOL:
            ta_sb_puts(out, e->as.ival ? "உண்மை" : "பொய்");
            break;
        case TX_CHAR: {
            uint32_t cp = e->as.cval;
            if (cp == '\'') ta_sb_puts(out, "'\\''");
            else if (cp == '\\') ta_sb_puts(out, "'\\\\'");
            else if (cp == '\n') ta_sb_puts(out, "'\\n'");
            else if (cp == '\t') ta_sb_puts(out, "'\\t'");
            else {
                char buf[8];
                size_t n = ta_utf8_encode(cp, buf);
                buf[n] = 0;
                ta_sb_printf(out, "'%s'", buf);
            }
            break;
        }
        case TX_STRING: {
            ta_sb_putc(out, '"');
            const unsigned char *s = (const unsigned char *)e->as.str.sval;
            size_t i = 0;
            while (s && i < e->as.str.slen) {
                unsigned char c = s[i];
                if (c == '"') ta_sb_puts(out, "\\\"");
                else if (c == '\\') ta_sb_puts(out, "\\\\");
                else if (c == '\n') ta_sb_puts(out, "\\n");
                else if (c == '\t') ta_sb_puts(out, "\\t");
                else if (c == '\r') ta_sb_puts(out, "\\r");
                else ta_sb_putc(out, (char)c);
                i++;
            }
            ta_sb_putc(out, '"');
            break;
        }
        case TX_NULL:
            ta_sb_puts(out, "வெற்று");
            break;
        case TX_IDENT:
            ta_sb_puts(out, e->as.name);
            break;
        case TX_BINARY: {
            Prec mp = (Prec)binop_prec(e->as.bin.op);
            bool paren = parent_prec != (int)PREC_NONE && mp <= (Prec)parent_prec;
            if (paren) ta_sb_putc(out, '(');
            print_expr(e->as.bin.lhs, mp, out);
            ta_sb_printf(out, " %s ", ta_binop_symbol(e->as.bin.op));
            print_expr(e->as.bin.rhs, mp, out);
            if (paren) ta_sb_putc(out, ')');
            break;
        }
        case TX_UNARY:
            if (e->as.un.op == TU_NEG) {
                ta_sb_putc(out, '-');
                print_expr(e->as.un.operand, PREC_UNARY + 1, out);
            } else {
                ta_sb_puts(out, "இல்லை ");
                print_expr(e->as.un.operand, PREC_NOT + 1, out);
            }
            break;
        case TX_CALL:
            print_expr(e->as.call.callee, 9, out);
            ta_sb_putc(out, '(');
            for (size_t i = 0; i < e->as.call.nargs; i++) {
                if (i) ta_sb_puts(out, ", ");
                print_expr(e->as.call.args[i], PREC_NONE, out);
            }
            ta_sb_putc(out, ')');
            break;
        case TX_INDEX:
            print_expr(e->as.index.base, 9, out);
            ta_sb_putc(out, '[');
            print_expr(e->as.index.index, PREC_NONE, out);
            ta_sb_putc(out, ']');
            break;
        case TX_MEMBER:
            print_expr(e->as.member.obj, 9, out);
            ta_sb_printf(out, ".%s", e->as.member.member);
            break;
        case TX_LIST:
            ta_sb_putc(out, '[');
            for (size_t i = 0; i < e->as.list.count; i++) {
                if (i) ta_sb_puts(out, ", ");
                print_expr(e->as.list.elems[i], PREC_NONE, out);
            }
            ta_sb_putc(out, ']');
            break;
        case TX_DICT:
            ta_sb_putc(out, '{');
            for (size_t i = 0; i < e->as.dict.count; i++) {
                if (i) ta_sb_puts(out, ", ");
                print_expr(e->as.dict.keys[i], PREC_NONE, out);
                ta_sb_puts(out, ": ");
                print_expr(e->as.dict.vals[i], PREC_NONE, out);
            }
            ta_sb_putc(out, '}');
            break;
    }
}

static void print_type_spec(const TaTypeSpec *ts, TaStrBuf *out) {
    if (!ts) return;
    switch (ts->kind) {
        case TS_NAME:
            ta_sb_puts(out, ts->name);
            break;
        case TS_LIST:
            ta_sb_putc(out, '[');
            print_type_spec(ts->elem, out);
            ta_sb_putc(out, ']');
            break;
        case TS_DICT:
            ta_sb_putc(out, '{');
            print_type_spec(ts->tk, out);
            ta_sb_puts(out, ": ");
            print_type_spec(ts->tv, out);
            ta_sb_putc(out, '}');
            break;
    }
}

static const char *assign_op_text(TaAssignOp op) {
    switch (op) {
        case TA_PLUSEQ: return "+=";
        case TA_MINUSEQ: return "-=";
        case TA_STAREQ: return "*=";
        case TA_SLASHEQ: return "/=";
        default: return "=";
    }
}

static void print_stmt(const TaStmt *st, int indent, TaStrBuf *out) {
    for (int i = 0; i < indent; i++) ta_sb_puts(out, "    ");
    switch (st->kind) {
        case ST_VARDECL:
            ta_sb_printf(out, "%s %s", st->as.vardecl.is_const ? "நிலையான" : "மாறி",
                         st->as.vardecl.name);
            if (st->as.vardecl.type) {
                ta_sb_puts(out, ": ");
                print_type_spec(st->as.vardecl.type, out);
            }
            ta_sb_puts(out, " = ");
            print_expr(st->as.vardecl.init, PREC_NONE, out);
            ta_sb_putc(out, '\n');
            break;
        case ST_ASSIGN:
            print_expr(st->as.assign.target, PREC_NONE, out);
            ta_sb_printf(out, " %s ", assign_op_text(st->as.assign.op));
            print_expr(st->as.assign.value, PREC_NONE, out);
            ta_sb_putc(out, '\n');
            break;
        case ST_EXPR:
            print_expr(st->as.exprstmt.expr, PREC_NONE, out);
            ta_sb_putc(out, '\n');
            break;
        case ST_IF:
            ta_sb_puts(out, "என்றால் ");
            print_expr(st->as.ifstmt.cond, PREC_NONE, out);
            ta_sb_puts(out, ":\n");
            for (size_t i = 0; i < st->as.ifstmt.then_body->count; i++)
                print_stmt(st->as.ifstmt.then_body->items[i], indent + 1, out);
            for (size_t k = 0; k < st->as.ifstmt.nelifs; k++) {
                for (int i = 0; i < indent; i++) ta_sb_puts(out, "    ");
                ta_sb_puts(out, "இல்லையெனில் என்றால் ");
                print_expr(st->as.ifstmt.elifs[k]->cond, PREC_NONE, out);
                ta_sb_puts(out, ":\n");
                for (size_t i = 0; i < st->as.ifstmt.elifs[k]->body->count; i++)
                    print_stmt(st->as.ifstmt.elifs[k]->body->items[i], indent + 1, out);
            }
            if (st->as.ifstmt.else_body) {
                for (int i = 0; i < indent; i++) ta_sb_puts(out, "    ");
                ta_sb_puts(out, "இல்லையெனில்:\n");
                for (size_t i = 0; i < st->as.ifstmt.else_body->count; i++)
                    print_stmt(st->as.ifstmt.else_body->items[i], indent + 1, out);
            }
            break;
        case ST_WHILE:
            ta_sb_puts(out, "வரை ");
            print_expr(st->as.whilestmt.cond, PREC_NONE, out);
            ta_sb_puts(out, ":\n");
            for (size_t i = 0; i < st->as.whilestmt.body->count; i++)
                print_stmt(st->as.whilestmt.body->items[i], indent + 1, out);
            break;
        case ST_FOREACH:
            ta_sb_printf(out, "ஒவ்வொன்றும் %s இல் ", st->as.foreach.varname);
            print_expr(st->as.foreach.iterable, PREC_NONE, out);
            ta_sb_puts(out, ":\n");
            for (size_t i = 0; i < st->as.foreach.body->count; i++)
                print_stmt(st->as.foreach.body->items[i], indent + 1, out);
            break;
        case ST_RETURN:
            ta_sb_puts(out, "திருப்பு");
            if (st->as.ret.value) {
                ta_sb_putc(out, ' ');
                print_expr(st->as.ret.value, PREC_NONE, out);
            }
            ta_sb_putc(out, '\n');
            break;
        case ST_BREAK:
            ta_sb_puts(out, "நிறுத்து\n");
            break;
        case ST_CONTINUE:
            ta_sb_puts(out, "தொடர்\n");
            break;
        case ST_FUNCDEF: {
            const TaFuncDef *fd = st->as.funcdef;
            ta_sb_printf(out, "செயலி %s(", fd->name);
            for (size_t i = 0; i < fd->nparams; i++) {
                if (i) ta_sb_puts(out, ", ");
                ta_sb_puts(out, fd->params[i].name);
                if (fd->params[i].type) {
                    ta_sb_puts(out, ": ");
                    print_type_spec(fd->params[i].type, out);
                }
            }
            ta_sb_puts(out, ")");
            if (fd->ret_type) {
                ta_sb_puts(out, " -> ");
                print_type_spec(fd->ret_type, out);
            }
            ta_sb_puts(out, ":\n");
            for (size_t i = 0; i < fd->body->count; i++)
                print_stmt(fd->body->items[i], indent + 1, out);
            break;
        }
    }
}

void ta_print_program(const TaProgram *p, TaStrBuf *out) {
    for (size_t i = 0; i < p->count; i++) print_stmt(p->items[i], 0, out);
}

char *ta_format_source(const TaProgram *p) {
    TaStrBuf sb;
    ta_sb_init(&sb);
    ta_print_program(p, &sb);
    return sb.data;
}
