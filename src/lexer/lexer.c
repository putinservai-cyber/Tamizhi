#include "ta_lexer.h"
#include "ta_utf8.h"

#include <errno.h>
#include <math.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int col;
    int *indents;
    size_t icount;
    size_t icap;
    int paren_depth;
    bool at_line_start;
    bool eof_done;
    bool line_has_tokens;
    TaToken *queue;
    size_t qcount;
    size_t qcap;
    TaDiagnostics *diag;
    const char *file;
    bool incomplete;
} Lexer;

static const struct {
    const char *kw;
    TaTokenType t;
} g_keywords[] = {
    {"மாறி", TK_VAR},       {"நிலையான", TK_CONST},   {"செயலி", TK_FUNC},
    {"திருப்பு", TK_RETURN}, {"என்றால்", TK_IF},       {"இல்லையெனில்", TK_ELSE},
    {"வரை", TK_WHILE},      {"ஒவ்வொன்றும்", TK_FOR},  {"இல்", TK_IN},
    {"நிறுத்து", TK_BREAK},  {"தொடர்", TK_CONTINUE},   {"உண்மை", TK_TRUE},
    {"பொய்", TK_FALSE},     {"வெற்று", TK_NULL},      {"மற்றும்", TK_AND},
    {"அல்லது", TK_OR},      {"இல்லை", TK_NOT},
};

const char *ta_token_type_name(TaTokenType t) {
    switch (t) {
        case TK_EOF: return "EOF";
        case TK_NEWLINE: return "NEWLINE";
        case TK_INDENT: return "INDENT";
        case TK_DEDENT: return "DEDENT";
        case TK_IDENT: return "IDENT";
        case TK_INT: return "INT";
        case TK_FLOAT: return "FLOAT";
        case TK_STRING: return "STRING";
        case TK_CHAR: return "CHAR";
        case TK_VAR: return "var";
        case TK_CONST: return "const";
        case TK_FUNC: return "func";
        case TK_RETURN: return "return";
        case TK_IF: return "if";
        case TK_ELSE: return "else";
        case TK_WHILE: return "while";
        case TK_FOR: return "foreach";
        case TK_IN: return "in";
        case TK_BREAK: return "break";
        case TK_CONTINUE: return "continue";
        case TK_TRUE: return "true";
        case TK_FALSE: return "false";
        case TK_NULL: return "null";
        case TK_AND: return "and";
        case TK_OR: return "or";
        case TK_NOT: return "not";
        case TK_LPAREN: return "(";
        case TK_RPAREN: return ")";
        case TK_LBRACKET: return "[";
        case TK_RBRACKET: return "]";
        case TK_LBRACE: return "{";
        case TK_RBRACE: return "}";
        case TK_COLON: return ":";
        case TK_COMMA: return ",";
        case TK_DOT: return ".";
        case TK_ARROW: return "->";
        case TK_ASSIGN: return "=";
        case TK_PLUS: return "+";
        case TK_MINUS: return "-";
        case TK_STAR: return "*";
        case TK_SLASH: return "/";
        case TK_PERCENT: return "%";
        case TK_EQ: return "==";
        case TK_NE: return "!=";
        case TK_LT: return "<";
        case TK_GT: return ">";
        case TK_LE: return "<=";
        case TK_GE: return ">=";
        case TK_PLUSEQ: return "+=";
        case TK_MINUSEQ: return "-=";
        case TK_STAREQ: return "*=";
        case TK_SLASHEQ: return "/=";
    }
    return "?";
}

void ta_token_list_free(TaTokenList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i].text);
    free(list->items);
    list->items = NULL;
    list->count = list->cap = 0;
}

static unsigned char lx_byte(const Lexer *lx) {
    return lx->pos < lx->len ? (unsigned char)lx->src[lx->pos] : 0;
}

static uint32_t lx_peek_cp(const Lexer *lx) {
    size_t adv = 0;
    return ta_utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &adv);
}

static void lx_advance(Lexer *lx) {
    size_t adv = 0;
    uint32_t cp = ta_utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &adv);
    lx->pos += adv ? adv : 1;
    if (cp == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
}

static TaToken mk_tok(TaTokenType t, int line, int col) {
    TaToken tk;
    memset(&tk, 0, sizeof(tk));
    tk.type = t;
    tk.line = line;
    tk.col = col;
    return tk;
}

static void enqueue(Lexer *lx, TaToken tk) {
    if (lx->qcount == lx->qcap) {
        lx->qcap = lx->qcap ? lx->qcap * 2 : 16;
        lx->queue = ta_xrealloc(lx->queue, lx->qcap * sizeof(TaToken));
    }
    lx->queue[lx->qcount++] = tk;
    if (tk.type != TK_NEWLINE && tk.type != TK_INDENT && tk.type != TK_DEDENT &&
        tk.type != TK_EOF) {
        lx->line_has_tokens = true;
    }
}

static void lex_error(Lexer *lx, int code, int line, int col, const char *msg, const char *hint) {
    ta_diag_report(lx->diag, code, lx->file, line, col, msg, "%s", hint ? hint : "");
}

static void skip_inline_space(Lexer *lx) {
    for (;;) {
        unsigned char c = lx_byte(lx);
        if (c == ' ' || c == '\t' || c == '\r') lx_advance(lx);
        else break;
    }
}

static void skip_comment(Lexer *lx) {
    while (lx->pos < lx->len && lx_byte(lx) != '\n') lx_advance(lx);
}

static void process_indent(Lexer *lx) {
    for (;;) {
        int width = 0;
        for (;;) {
            if (lx->pos >= lx->len) break;
            unsigned char c = lx_byte(lx);
            if (c == '\r') {
                lx_advance(lx);
                continue;
            }
            if (c == ' ') {
                width++;
                lx_advance(lx);
                continue;
            }
            if (c == '\t') {
                width += 4 - (width % 4);
                lx_advance(lx);
                continue;
            }
            break;
        }

        unsigned char c = lx_byte(lx);
        if (lx->pos >= lx->len) {
            lx->at_line_start = false;
            return;
        }
        if (c == '#' || c == '\n') {
            while (lx->pos < lx->len && lx_byte(lx) != '\n') lx_advance(lx);
            if (lx->pos < lx->len) lx_advance(lx);
            continue;
        }

        int top = lx->indents[lx->icount - 1];
        if (width > top) {
            if (lx->icount == lx->icap) {
                lx->icap = lx->icap ? lx->icap * 2 : 8;
                lx->indents = ta_xrealloc(lx->indents, (size_t)lx->icap * sizeof(int));
            }
            lx->indents[lx->icount++] = width;
            enqueue(lx, mk_tok(TK_INDENT, lx->line, 1));
        } else if (width < top) {
            while (lx->icount > 1 && lx->indents[lx->icount - 1] > width) {
                lx->icount--;
                enqueue(lx, mk_tok(TK_DEDENT, lx->line, 1));
            }
            if (lx->indents[lx->icount - 1] != width) {
                lex_error(lx, TA_ERR_LEX + 3, lx->line, 1,
                          "உள்தள்ல் முந்தைய வரிகளுடன் பொருந்தவில்லை",
                          "ஒரே அளவிலான இடைவெளி (4 இடைவெளிகள்) மட்டும் பயன்படுத்துங்கள்");
            }
        }
        lx->at_line_start = false;
        return;
    }
}

static void finalize_eof(Lexer *lx) {
    if (lx->paren_depth > 0) {
        lex_error(lx, TA_ERR_PARSE + 5, lx->line, lx->col,
                  "மூடப்படாத அடைப்புக்குறி உள்ளது",
                  "'(' , '[' , '{' அனைத்திற்கும் இணையான மூடும் குறிகள் தேவை");
        lx->incomplete = true;
    }
    if (lx->line_has_tokens && lx->paren_depth == 0) {
        enqueue(lx, mk_tok(TK_NEWLINE, lx->line, lx->col));
        lx->line_has_tokens = false;
    }
    while (lx->icount > 1) {
        lx->icount--;
        enqueue(lx, mk_tok(TK_DEDENT, lx->line, 1));
    }
    lx->eof_done = true;
}

static bool read_escape(Lexer *lx, TaStrBuf *sb, uint32_t *cp_out, bool want_cp) {
    lx_advance(lx);
    uint32_t c = lx_peek_cp(lx);
    switch (c) {
        case 'n': lx_advance(lx); if (want_cp) *cp_out = '\n'; else ta_sb_putc(sb, '\n'); return true;
        case 't': lx_advance(lx); if (want_cp) *cp_out = '\t'; else ta_sb_putc(sb, '\t'); return true;
        case 'r': lx_advance(lx); if (want_cp) *cp_out = '\r'; else ta_sb_putc(sb, '\r'); return true;
        case '0': lx_advance(lx); if (want_cp) *cp_out = 0; else ta_sb_putc(sb, 0); return true;
        case '"': lx_advance(lx); if (want_cp) *cp_out = '"'; else ta_sb_putc(sb, '"'); return true;
        case '\'': lx_advance(lx); if (want_cp) *cp_out = '\''; else ta_sb_putc(sb, '\''); return true;
        case '\\': lx_advance(lx); if (want_cp) *cp_out = '\\'; else ta_sb_putc(sb, '\\'); return true;
        case 'u': {
            lx_advance(lx);
            if (lx_peek_cp(lx) != '{') {
                lex_error(lx, TA_ERR_LEX + 5, lx->line, lx->col,
                          "தவறான escape வரிசை",
                          "'\\u{...}' என்ற வடிவத்தில் யுனிகோடு எழுத்தை எழுதுங்கள், எ.கா. \\u{0BB5}");
                return false;
            }
            lx_advance(lx);
            uint32_t val = 0;
            int ndig = 0;
            for (;;) {
                uint32_t d = lx_peek_cp(lx);
                int hv;
                if (d >= '0' && d <= '9') hv = (int)(d - '0');
                else if (d >= 'a' && d <= 'f') hv = (int)(d - 'a' + 10);
                else if (d >= 'A' && d <= 'F') hv = (int)(d - 'A' + 10);
                else break;
                val = val * 16 + (uint32_t)hv;
                ndig++;
                if (ndig > 6) break;
                lx_advance(lx);
            }
            if (lx_peek_cp(lx) != '}') {
                lex_error(lx, TA_ERR_LEX + 5, lx->line, lx->col,
                          "தவறான escape வரிசை",
                          "'\\u{...}' இன் முடிவில் '}' இருக்க வேண்டும்");
                return false;
            }
            lx_advance(lx);
            if (ndig == 0 || val > 0x10FFFF) {
                lex_error(lx, TA_ERR_LEX + 5, lx->line, lx->col,
                          "\\u{} இல் தவறான யுனிகோடு மதிப்பு",
                          "1 முதல் 6 இலக்கங்கள் கொண்ட சரியான hex மதிப்பைக் கொடுங்கள்");
                return false;
            }
            if (want_cp) *cp_out = val;
            else {
                char buf[4];
                size_t n = ta_utf8_encode(val, buf);
                ta_sb_appends(sb, buf, n);
            }
            return true;
        }
        default:
            lex_error(lx, TA_ERR_LEX + 5, lx->line, lx->col,
                      "அறியப்படாத escape வரிசை",
                      "'\\n', '\\t', '\\r', '\\\\', '\\\"', \"'\" அல்லது '\\u{...}' மட்டுமே ஆதரவு");
            return false;
    }
}

static void lex_string(Lexer *lx) {
    int sline = lx->line, scol = lx->col;
    lx_advance(lx);
    TaStrBuf sb;
    ta_sb_init(&sb);
    for (;;) {
        if (lx->pos >= lx->len || lx_byte(lx) == '\n') {
            lex_error(lx, TA_ERR_LEX + 2, sline, scol, "முடிக்கப்படாத சரம் (string)",
                      "சரத்தின் இரு முனைகளிலும் '\"' இருக்க வேண்டும்");
            lx->incomplete = true;
            break;
        }
        if (lx_byte(lx) == '"') {
            lx_advance(lx);
            break;
        }
        if (lx_byte(lx) == '\\') {
            read_escape(lx, &sb, NULL, false);
            continue;
        }
        size_t adv = 0;
        uint32_t cp = ta_utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &adv);
        if (adv == 0) adv = 1;
        lx_advance(lx);
        if (cp == TA_UTF8_INVALID) {
            lex_error(lx, TA_ERR_LEX + 1, lx->line, lx->col, "தவறான UTF-8 எழுத்து",
                      "கோப்பு UTF-8 குறியீட்டில் சேமிக்கப்பட வேண்டும்");
            continue;
        }
        char buf[4];
        size_t n = ta_utf8_encode(cp, buf);
        ta_sb_appends(&sb, buf, n);
    }
    TaToken tk = mk_tok(TK_STRING, sline, scol);
    tk.text = sb.data;
    tk.text_len = sb.len;
    enqueue(lx, tk);
}

static void lex_char(Lexer *lx) {
    int sline = lx->line, scol = lx->col;
    lx_advance(lx);
    uint32_t cp = 0;
    bool ok = false;
    if (lx_byte(lx) == '\\') {
        ok = read_escape(lx, NULL, &cp, true);
    } else if (lx->pos < lx->len && lx_byte(lx) != '\'' && lx_byte(lx) != '\n') {
        size_t adv = 0;
        cp = ta_utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &adv);
        if (adv == 0) adv = 1;
        lx_advance(lx);
        ok = (cp != TA_UTF8_INVALID);
        if (!ok) {
            lex_error(lx, TA_ERR_LEX + 1, lx->line, lx->col, "தவறான UTF-8 எழுத்து",
                      "கோப்பு UTF-8 குறியீட்டில் சேமிக்கப்பட வேண்டும்");
        }
    }
    if (lx_byte(lx) != '\'') {
        lex_error(lx, TA_ERR_LEX + 6, sline, scol,
                  "எழுத்து literal இல் ஒரே ஒரு எழுத்து மட்டுமே இருக்க வேண்டும்",
                  "'அ' அல்லது '\\n' போல ஒற்றை எழுத்தை ஒற்றைமேற்கோளில் எழுதுங்கள்");
        while (lx->pos < lx->len && lx_byte(lx) != '\'' && lx_byte(lx) != '\n') lx_advance(lx);
        if (lx_byte(lx) == '\'') lx_advance(lx);
    } else {
        lx_advance(lx);
    }
    TaToken tk = mk_tok(TK_CHAR, sline, scol);
    tk.v.cp = ok ? cp : '?';
    enqueue(lx, tk);
}

static void lex_number(Lexer *lx) {
    int sline = lx->line, scol = lx->col;
    TaStrBuf sb;
    ta_sb_init(&sb);
    bool is_float = false;

    if (lx_byte(lx) == '0' && (lx->pos + 1 < lx->len) &&
        (lx->src[lx->pos + 1] == 'x' || lx->src[lx->pos + 1] == 'X')) {
        lx_advance(lx);
        lx_advance(lx);
        int ndig = 0;
        for (;;) {
            unsigned char c = lx_byte(lx);
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                ta_sb_putc(&sb, (char)c);
                lx_advance(lx);
                ndig++;
            } else if (c == '_') {
                lx_advance(lx);
            } else {
                break;
            }
        }
        if (ndig == 0) {
            lex_error(lx, TA_ERR_LEX + 4, sline, scol, "தவறான எண்",
                      "'0x' க்குப் பிறகு குறைந்தது ஒரு hex இலக்கம் தேவை");
        }
        errno = 0;
        long long val = ndig ? strtoll(sb.data, NULL, 16) : 0;
        if (ndig && errno == ERANGE) {
            ta_sb_free(&sb);
            lex_error(lx, TA_ERR_LEX + 4, sline, scol, "எண் எல்லை மீறியது",
                      "முழுஎண் வரம்பை விட பெரிய hex எண்");
            return;
        }
        ta_sb_free(&sb);
        TaToken tk = mk_tok(TK_INT, sline, scol);
        tk.v.i = val;
        enqueue(lx, tk);
        return;
    }
    if (lx_byte(lx) == '0' && (lx->pos + 1 < lx->len) &&
        (lx->src[lx->pos + 1] == 'b' || lx->src[lx->pos + 1] == 'B')) {
        lx_advance(lx);
        lx_advance(lx);
        int ndig = 0;
        for (;;) {
            unsigned char c = lx_byte(lx);
            if (c == '0' || c == '1') {
                ta_sb_putc(&sb, (char)c);
                lx_advance(lx);
                ndig++;
            } else if (c == '_') {
                lx_advance(lx);
            } else {
                break;
            }
        }
        if (ndig == 0) {
            lex_error(lx, TA_ERR_LEX + 4, sline, scol, "தவறான எண்",
                      "'0b' க்குப் பிறகு குறைந்தது ஒரு பைனரி இலக்கம் தேவை");
        }
        errno = 0;
        long long val = ndig ? strtoll(sb.data, NULL, 2) : 0;
        if (ndig && errno == ERANGE) {
            ta_sb_free(&sb);
            lex_error(lx, TA_ERR_LEX + 4, sline, scol, "எண் எல்லை மீறியது",
                      "முழுஎண் வரம்பை விட பெரிய binary எண்");
            return;
        }
        ta_sb_free(&sb);
        TaToken tk = mk_tok(TK_INT, sline, scol);
        tk.v.i = val;
        enqueue(lx, tk);
        return;
    }

    for (;;) {
        unsigned char c = lx_byte(lx);
        if (c >= '0' && c <= '9') {
            ta_sb_putc(&sb, (char)c);
            lx_advance(lx);
        } else if (c == '_') {
            lx_advance(lx);
        } else {
            break;
        }
    }
    if (lx_byte(lx) == '.' && lx->pos + 1 < lx->len && lx->src[lx->pos + 1] >= '0' &&
        lx->src[lx->pos + 1] <= '9') {
        is_float = true;
        ta_sb_putc(&sb, '.');
        lx_advance(lx);
        for (;;) {
            unsigned char c = lx_byte(lx);
            if (c >= '0' && c <= '9') {
                ta_sb_putc(&sb, (char)c);
                lx_advance(lx);
            } else if (c == '_') {
                lx_advance(lx);
            } else {
                break;
            }
        }
    }
    if (lx_byte(lx) == 'e' || lx_byte(lx) == 'E') {
        size_t save_pos = lx->pos;
        int save_line = lx->line, save_col = lx->col;
        lx_advance(lx);
        bool neg = false;
        if (lx_byte(lx) == '+' || lx_byte(lx) == '-') {
            neg = lx_byte(lx) == '-';
            lx_advance(lx);
        }
        if (lx_byte(lx) >= '0' && lx_byte(lx) <= '9') {
            is_float = true;
            ta_sb_putc(&sb, 'e');
            if (neg) ta_sb_putc(&sb, '-');
            while (lx_byte(lx) >= '0' && lx_byte(lx) <= '9') {
                ta_sb_putc(&sb, lx_byte(lx));
                lx_advance(lx);
            }
        } else {
            lx->pos = save_pos;
            lx->line = save_line;
            lx->col = save_col;
        }
    }

    TaToken tk = mk_tok(is_float ? TK_FLOAT : TK_INT, sline, scol);
    if (is_float) {
        errno = 0;
        tk.v.f = strtod(sb.data, NULL);
        if (errno == ERANGE && !isfinite(tk.v.f)) {
            ta_sb_free(&sb);
            lex_error(lx, TA_ERR_LEX + 4, sline, scol, "எண் எல்லை மீறியது",
                      "மிதவிழிப்பு எண் வரம்பை விட பெரியது");
            return;
        }
    } else {
        errno = 0;
        tk.v.i = strtoll(sb.data, NULL, 10);
        if (errno == ERANGE) {
            ta_sb_free(&sb);
            lex_error(lx, TA_ERR_LEX + 4, sline, scol, "எண் எல்லை மீறியது",
                      "முழுஎண் வரம்பை விட பெரிய எண்");
            return;
        }
    }
    ta_sb_free(&sb);
    enqueue(lx, tk);
}

static void lex_ident(Lexer *lx) {
    int sline = lx->line, scol = lx->col;
    TaStrBuf sb;
    ta_sb_init(&sb);
    for (;;) {
        uint32_t cp = lx_peek_cp(lx);
        if (!ta_utf8_is_ident_cont(cp)) break;
        size_t adv = 0;
        ta_utf8_decode(lx->src + lx->pos, lx->len - lx->pos, &adv);
        if (adv == 0) adv = 1;
        ta_sb_appends(&sb, lx->src + lx->pos, adv);
        lx_advance(lx);
    }
    TaTokenType tt = TK_IDENT;
    for (size_t i = 0; i < sizeof(g_keywords) / sizeof(g_keywords[0]); i++) {
        if (strcmp(sb.data, g_keywords[i].kw) == 0) {
            tt = g_keywords[i].t;
            break;
        }
    }
    TaToken tk = mk_tok(tt, sline, scol);
    tk.text = sb.data;
    tk.text_len = sb.len;
    enqueue(lx, tk);
}

static void lex_one(Lexer *lx) {
    skip_inline_space(lx);

    if (lx->pos >= lx->len) return;

    unsigned char c = lx_byte(lx);
    int line = lx->line, col = lx->col;

    if (c == '\n') {
        lx_advance(lx);
        if (lx->paren_depth > 0) return;
        lx->at_line_start = true;
        lx->line_has_tokens = false;
        enqueue(lx, mk_tok(TK_NEWLINE, line, col));
        return;
    }
    if (c == '#') {
        skip_comment(lx);
        return;
    }
    if (c == '"') {
        lex_string(lx);
        return;
    }
    if (c == '\'') {
        lex_char(lx);
        return;
    }
    if (c >= '0' && c <= '9') {
        lex_number(lx);
        return;
    }

    uint32_t cp = lx_peek_cp(lx);
    if (ta_utf8_is_ident_start(cp)) {
        lex_ident(lx);
        return;
    }

    lx_advance(lx);
    switch (c) {
        case '(': lx->paren_depth++; enqueue(lx, mk_tok(TK_LPAREN, line, col)); return;
        case ')':
            if (lx->paren_depth > 0) lx->paren_depth--;
            enqueue(lx, mk_tok(TK_RPAREN, line, col));
            return;
        case '[': lx->paren_depth++; enqueue(lx, mk_tok(TK_LBRACKET, line, col)); return;
        case ']':
            if (lx->paren_depth > 0) lx->paren_depth--;
            enqueue(lx, mk_tok(TK_RBRACKET, line, col));
            return;
        case '{': lx->paren_depth++; enqueue(lx, mk_tok(TK_LBRACE, line, col)); return;
        case '}':
            if (lx->paren_depth > 0) lx->paren_depth--;
            enqueue(lx, mk_tok(TK_RBRACE, line, col));
            return;
        case ':': enqueue(lx, mk_tok(TK_COLON, line, col)); return;
        case ',': enqueue(lx, mk_tok(TK_COMMA, line, col)); return;
        case '+':
            if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_PLUSEQ, line, col)); }
            else enqueue(lx, mk_tok(TK_PLUS, line, col));
            return;
        case '*':
            if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_STAREQ, line, col)); }
            else enqueue(lx, mk_tok(TK_STAR, line, col));
            return;
        case '/':
            if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_SLASHEQ, line, col)); }
            else enqueue(lx, mk_tok(TK_SLASH, line, col));
            return;
        case '%': enqueue(lx, mk_tok(TK_PERCENT, line, col)); return;
        case '^': case '~': case '$': case '@': case '!': case '&': case '|': {
            if (c == '!') {
                if (lx_byte(lx) == '=') {
                    lx_advance(lx);
                    enqueue(lx, mk_tok(TK_NE, line, col));
                    return;
                }
                lex_error(lx, TA_ERR_LEX + 1, line, col, "தவறான எழுத்து '!'", "'!=' என்று எழுத வேண்டும்");
                return;
            }
            lex_error(lx, TA_ERR_LEX + 1, line, col, "இந்த எழுத்து Tamizhi இல் பயன்படுத்த முடியாது",
                      "சரியான operator ஐத் தேர்ந்தெடுங்கள்: + - * / % == != < > <= >= மற்றும் அல்லது இல்லை");
            return;
        }
        case '-':
            if (lx_byte(lx) == '>') { lx_advance(lx); enqueue(lx, mk_tok(TK_ARROW, line, col)); }
            else if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_MINUSEQ, line, col)); }
            else enqueue(lx, mk_tok(TK_MINUS, line, col));
            return;
        case '<':
            if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_LE, line, col)); }
            else enqueue(lx, mk_tok(TK_LT, line, col));
            return;
        case '>':
            if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_GE, line, col)); }
            else enqueue(lx, mk_tok(TK_GT, line, col));
            return;
        case '=':
            if (lx_byte(lx) == '=') { lx_advance(lx); enqueue(lx, mk_tok(TK_EQ, line, col)); }
            else enqueue(lx, mk_tok(TK_ASSIGN, line, col));
            return;
        case '.': enqueue(lx, mk_tok(TK_DOT, line, col)); return;
        default: {
            char msgbuf[128];
            snprintf(msgbuf, sizeof(msgbuf), "அறியப்படாத எழுத்து U+%04X", (unsigned)c);
            lex_error(lx, TA_ERR_LEX + 1, line, col, msgbuf,
                      "Tamil அல்லது ASCII எழுத்துக்களை மட்டும் பயன்படுத்துங்கள்");
            return;
        }
    }
}

static void lex_fill(Lexer *lx) {
    for (;;) {
        if (lx->qcount > 0) return;
        if (lx->eof_done) return;
        if (lx->pos >= lx->len) {
            finalize_eof(lx);
            continue;
        }
        if (lx->paren_depth == 0 && lx->at_line_start) {
            process_indent(lx);
            continue;
        }
        lex_one(lx);
    }
}

TaTokenList ta_lex_source(const char *filename, const char *src, TaDiagnostics *diag,
                          TaLexResult *result) {
    Lexer lx;
    memset(&lx, 0, sizeof(lx));
    lx.src = src;
    lx.len = strlen(src);
    lx.line = 1;
    lx.col = 1;
    lx.at_line_start = true;
    lx.diag = diag;
    lx.file = filename;
    lx.icap = 8;
    lx.indents = ta_xmalloc((size_t)lx.icap * sizeof(int));
    lx.indents[0] = 0;
    lx.icount = 1;

    TaTokenList out;
    memset(&out, 0, sizeof(out));

    for (;;) {
        lex_fill(&lx);
        if (lx.qcount == 0) break;
        TaToken tk = lx.queue[0];
        memmove(lx.queue, lx.queue + 1, (lx.qcount - 1) * sizeof(TaToken));
        lx.qcount--;
        if (out.count == out.cap) {
            out.cap = out.cap ? out.cap * 2 : 64;
            out.items = ta_xrealloc(out.items, out.cap * sizeof(TaToken));
        }
        out.items[out.count++] = tk;
        if (tk.type == TK_EOF) break;
    }
    TaToken eof = mk_tok(TK_EOF, lx.line, lx.col);
    if (out.count == out.cap) {
        out.cap = out.cap ? out.cap * 2 : 64;
        out.items = ta_xrealloc(out.items, out.cap * sizeof(TaToken));
    }
    out.items[out.count++] = eof;
    if (result) result->incomplete = lx.incomplete;
    free(lx.queue);
    free(lx.indents);
    return out;
}
