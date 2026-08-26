#include "ta_utf8.h"

uint32_t ta_utf8_decode(const char *s, size_t avail, size_t *advance) {
    if (avail == 0) {
        if (advance) *advance = 0;
        return TA_UTF8_INVALID;
    }
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        if (advance) *advance = 1;
        return c;
    }
    size_t n;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
    else {
        if (advance) *advance = 1;
        return TA_UTF8_INVALID;
    }
    if (avail < n) {
        if (advance) *advance = 1;
        return TA_UTF8_INVALID;
    }
    for (size_t i = 1; i < n; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) {
            if (advance) *advance = 1;
            return TA_UTF8_INVALID;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (advance) *advance = n;
    return cp;
}

size_t ta_utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t ta_utf8_cp_count(const char *s, size_t bytes) {
    size_t count = 0, i = 0;
    while (i < bytes) {
        size_t adv = 1;
        ta_utf8_decode(s + i, bytes - i, &adv);
        if (adv == 0) adv = 1; /* guard: never get stuck on invalid byte */
        i += adv;
        count++;
    }
    return count;
}

static bool ta_is_ascii_letter(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

static bool ta_is_tamil_base_letter(uint32_t cp) {
    return cp == 0x0B83 || (cp >= 0x0B85 && cp <= 0x0B8A) ||
           (cp >= 0x0B8E && cp <= 0x0B90) || (cp >= 0x0B92 && cp <= 0x0B95) ||
           cp == 0x0B99 || cp == 0x0B9A || cp == 0x0B9C ||
           (cp >= 0x0B9E && cp <= 0x0B9F) || (cp >= 0x0BA3 && cp <= 0x0BA4) ||
           (cp >= 0x0BA8 && cp <= 0x0BAA) || (cp >= 0x0BAE && cp <= 0x0BB9);
}

bool ta_utf8_is_tamil_matra(uint32_t cp) {
    return (cp >= 0x0BBE && cp <= 0x0BCD) || cp == 0x0BD7 || cp == 0x200C ||
           cp == 0x200D;
}

size_t ta_utf8_truncate_bytes(const char *s, size_t max_bytes) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (max_bytes >= n) return n;
    size_t end = max_bytes;
    while (end > 0 && ((unsigned char)s[end] & 0xC0) == 0x80) end--;
    return end;
}

/*
 * ta_utf8_prefix_columns - count visual terminal columns for the first
 * `upto_cp` codepoints of `line` (byte length `bytes`).
 *
 * Tamil rendering rule: a base letter followed by one or more matras
 * (vowel signs / virama / ZWJ / ZWNJ) forms a single akshar cluster
 * and occupies exactly ONE terminal column.  A standalone matra that is
 * NOT preceded by a base letter in this scan still counts as one column
 * so that the caret never lands before column 1.
 *
 * Algorithm:
 *   - Read the next codepoint and advance cp_index.
 *   - If it is a matra, it was already absorbed by the previous cluster's
 *     look-ahead; treat it as zero additional columns (defensive path).
 *   - Otherwise it is a base / spacing character: count 1 column, then
 *     peek-ahead and silently consume any immediately following matras
 *     (incrementing cp_index for each) without adding columns.
 */
int ta_utf8_prefix_columns(const char *line, size_t bytes, int upto_cp) {
    int cols     = 0;
    int cp_index = 0;
    size_t i     = 0;

    while (i < bytes && (upto_cp < 0 || cp_index < upto_cp)) {
        size_t adv = 1;
        uint32_t cp = ta_utf8_decode(line + i, bytes - i, &adv);
        if (adv == 0) adv = 1; /* guard against invalid byte */
        i        += adv;
        cp_index += 1;

        if (ta_utf8_is_tamil_matra(cp)) {
            /*
             * Orphaned matra (already consumed by look-ahead, or a bare
             * matra at line start).  Count it as one column so we never
             * produce a negative or zero offset.
             */
            cols++;
            continue;
        }

        /* Base / spacing codepoint: one visual column. */
        cols++;

        /*
         * Look-ahead: consume all immediately following matras so they
         * are part of the same cluster and do not add extra columns.
         * We must also respect the upto_cp limit while peeking.
         */
        while (i < bytes && (upto_cp < 0 || cp_index < upto_cp)) {
            size_t a2 = 1;
            uint32_t nx = ta_utf8_decode(line + i, bytes - i, &a2);
            if (a2 == 0) break;
            if (!ta_utf8_is_tamil_matra(nx)) break;
            i        += a2;
            cp_index += 1;
        }
    }
    return cols;
}

bool ta_utf8_is_ident_start(uint32_t cp) {
    return ta_is_ascii_letter(cp) || cp == '_' || ta_is_tamil_base_letter(cp);
}

bool ta_utf8_is_digit_cp(uint32_t cp) {
    return (cp >= '0' && cp <= '9') || (cp >= 0x0BE6 && cp <= 0x0BEF);
}

bool ta_utf8_is_ident_cont(uint32_t cp) {
    return ta_utf8_is_ident_start(cp) || ta_utf8_is_digit_cp(cp) ||
           ta_utf8_is_tamil_matra(cp);
}
