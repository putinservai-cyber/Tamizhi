#include "ta_common.h"
#include "ta_utf8.h"

TaDiagnostics *ta_diag_new(void) {
    TaDiagnostics *d = ta_xcalloc(1, sizeof(TaDiagnostics));
    return d;
}

void ta_diag_free(TaDiagnostics *d) {
    if (!d) return;
    for (size_t i = 0; i < d->count; i++) {
        free(d->items[i]->message);
        free(d->items[i]->hint);
        free(d->items[i]);
    }
    free(d->items);
    free(d);
}

void ta_diag_report_v(TaDiagnostics *d, int code, const char *file, int line, int col,
                      const char *msg, const char *hint_fmt, va_list ap) {
    TaDiagnostic *di = ta_xcalloc(1, sizeof(TaDiagnostic));
    di->code = code;
    di->line = line;
    di->col = col;
    di->file = file;
    {
        char tmp[768];
        va_list ap1;
        va_copy(ap1, ap);
        vsnprintf(tmp, sizeof(tmp), msg ? msg : "", ap1);
        va_end(ap1);
        di->message = ta_xstrdup(tmp);
    }
    if (hint_fmt) {
        char tmp[512];
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(tmp, sizeof(tmp), hint_fmt, ap2);
        va_end(ap2);
        di->hint = ta_xstrdup(tmp);
    }
    if (d->count == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 8;
        d->items = realloc(d->items, d->cap * sizeof(TaDiagnostic *));
        if (!d->items) {
            fprintf(stderr, "TA5001: out of memory\n");
            exit(70);
        }
    }
    d->items[d->count++] = di;
}

void ta_diag_report(TaDiagnostics *d, int code, const char *file, int line, int col,
                    const char *msg, const char *hint_fmt, ...) {
    va_list ap;
    va_start(ap, hint_fmt);
    ta_diag_report_v(d, code, file, line, col, msg, hint_fmt, ap);
    va_end(ap);
}

bool ta_diag_has_errors(const TaDiagnostics *d) {
    return d && d->count > 0;
}

size_t ta_diag_error_count(const TaDiagnostics *d) {
    return d ? d->count : 0;
}

char *ta_sanitize_utf8(const char *s) {
    if (!s) return ta_xstrdup("");
    size_t cap = strlen(s) * 3 + 16;
    char *out = ta_xmalloc(cap);
    size_t o = 0, i = 0, n = strlen(s);
    while (i < n && o + 8 < cap) {
        size_t adv = 1;
        uint32_t cp = ta_utf8_decode(s + i, n - i, &adv);
        if (adv == 0) adv = 1;
        i += adv;
        if (cp == TA_UTF8_INVALID) {
            out[o++] = (char)0xEF;
            out[o++] = (char)0xBF;
            out[o++] = (char)0xBD;
            while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
        } else if (cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') {
            out[o++] = '?';
        } else {
            o += ta_utf8_encode(cp, out + o);
        }
    }
    out[o] = 0;
    return out;
}

char *ta_source_line(const char *source, int line) {
    if (!source || line < 1) return ta_xstrdup("");
    int cur = 1;
    const char *p = source;
    while (cur < line && *p) {
        if (*p == '\n') cur++;
        p++;
    }
    const char *end = p;
    while (*end && *end != '\n') end++;
    size_t n = (size_t)(end - p);
    while (n > 0 && (p[n - 1] == '\r')) n--;
    char *raw = ta_xstrndup(p, n);
    for (size_t i = 0; raw[i]; i++) {
        if (raw[i] == '\t') raw[i] = ' ';
    }
    char *out = ta_sanitize_utf8(raw);
    free(raw);
    return out;
}

void ta_diag_print_all(const TaDiagnostics *d, FILE *out, const char *source) {
    fflush(stdout);
    for (size_t i = 0; i < d->count; i++) {
        TaDiagnostic *di = d->items[i];
        fprintf(out, "பிழை TA%d [%s:%d:%d]: %s\n", di->code,
                di->file ? di->file : "<நிரல்>", di->line, di->col, di->message);
        char *line = ta_source_line(source, di->line);
        fprintf(out, "  %s\n", line);
        int col = di->col > 0 ? di->col : 1;
        size_t lbytes = line ? strlen(line) : 0;
        int width = ta_utf8_prefix_columns(line, lbytes, col - 1);
        fprintf(out, "  ");
        for (int k = 0; k < width; k++) fputc(' ', out);
        fprintf(out, "^\n");
        if (di->hint) fprintf(out, "  ஆலோசனை: %s\n", di->hint);
        fprintf(out, "\n");
        free(line);
    }
}

void *ta_xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "TA5001: நினைவகம் போதவில்லை (out of memory)\n");
        exit(70);
    }
    return p;
}

void *ta_xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) {
        fprintf(stderr, "TA5001: நினைவகம் போதவில்லை (out of memory)\n");
        exit(70);
    }
    return p;
}

void *ta_xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "TA5001: நினைவகம் போதவில்லை (out of memory)\n");
        exit(70);
    }
    return q;
}

char *ta_xstrdup(const char *s) {
    if (!s) s = "";
    char *p = ta_xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

char *ta_xstrndup(const char *s, size_t n) {
    char *p = ta_xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void ta_sb_init(TaStrBuf *sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = ta_xmalloc(sb->cap);
    sb->data[0] = 0;
}

void ta_sb_free(TaStrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

static void ta_sb_reserve(TaStrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 > sb->cap) {
        while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
        sb->data = ta_xrealloc(sb->data, sb->cap);
    }
}

void ta_sb_putc(TaStrBuf *sb, char c) {
    ta_sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = 0;
}

void ta_sb_puts(TaStrBuf *sb, const char *s) {
    ta_sb_appends(sb, s, strlen(s));
}

void ta_sb_appends(TaStrBuf *sb, const char *s, size_t n) {
    ta_sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = 0;
}

void ta_sb_printf(TaStrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        va_end(ap2);
        return;
    }
    ta_sb_reserve(sb, (size_t)need);
    vsnprintf(sb->data + sb->len, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)need;
}

int ta_edit_distance(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > 64 || lb > 64) return 9999;
    static int dp[65][65];
    for (size_t i = 0; i <= la; i++) dp[i][0] = (int)i;
    for (size_t j = 0; j <= lb; j++) dp[0][j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        for (size_t j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int m = dp[i - 1][j] + 1;
            if (dp[i][j - 1] + 1 < m) m = dp[i][j - 1] + 1;
            if (dp[i - 1][j - 1] + cost < m) m = dp[i - 1][j - 1] + cost;
            dp[i][j] = m;
        }
    }
    return dp[la][lb];
}

char *ta_read_file(const char *path, size_t *out_len, int *err_code) {
    *err_code = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        *err_code = TA_ERR_IO + 1;
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        *err_code = TA_ERR_IO + 1;
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        *err_code = TA_ERR_IO + 1;
        return NULL;
    }
    rewind(f);
    char *buf = ta_xmalloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    if (out_len) *out_len = rd;

    if (rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        /* Strip UTF-8 BOM: move rd-3 content bytes up by 3 positions */
        memmove(buf, buf + 3, rd - 3);
        rd -= 3;
        buf[rd] = 0;
        if (out_len) *out_len = rd;
    }
    return buf;
}

bool ta_write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len;
}
