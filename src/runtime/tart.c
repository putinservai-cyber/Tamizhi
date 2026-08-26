#include "tart.h"

#include <inttypes.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rt_fail(const char *msg) {
    fprintf(stderr, "\u0b87\u0baf\u0b95\u0bcd\u0b95 \u0ba8\u0bc7\u0bb0\u0bcd \u0baa\u0bbf\u0bb4\u0bc8: %s\n", msg);
    exit(70);
}

void ta_rt_abort_div_zero(void) {
    rt_fail("\u0baa\u0bc2\u0bb0\u0bbf\u0baf\u0bbe\u0bb2\u0bcd \u0bb5\u0b95\u0bc1\u0ba4\u0bcd\u0ba4\u0bae\u0bcd (division by zero)");
}

void ta_rt_abort_index(int64_t idx, int64_t len) {
    fprintf(stderr,
            "\u0b87\u0baf\u0b95\u0bcd\u0b95 \u0ba8\u0bc7\u0bb0\u0bcd \u0baa\u0bbf\u0bb4\u0bc8: \u0b85\u0b9f\u0bc1\u0baf\u0bbe\u0ba3\u0bb5\u0bc1 "
            "%" PRId64 " \u0b8e\u0bb2\u0bcd\u0bb2\u0bc8\u0b95\u0bcd\u0b95\u0bc1 \u0bb5\u0bc6\u0bb3\u0bbf\u0baf\u0bbe\u0ba9\u0ba4\u0bc1 (\u0ba8\u0bc0\u0bb3\u0bae\u0bcd %" PRId64 ")\n",
            idx, len);
    exit(70);
}

void ta_rt_abort_key(void) {
    rt_fail("\u0b95\u0bc1\u0bb1\u0bbf\u0baa\u0bcd\u0baa\u0bbf\u0b9f\u0baa\u0bcd\u0baa\u0b9f\u0bcd\u0b9f \u0b9a\u0bbe\u0bb5\u0bbf \u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf\u0baf\u0bbf\u0bb2\u0bcd \u0b87\u0bb2\u0bcd\u0bb2\u0bc8 (key not found)");
}

static void *rt_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) rt_fail("\u0ba8\u0bbf\u0ba9\u0bc8\u0bb5\u0b95\u0bae\u0bcd \u0baa\u0bcb\u0ba4\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8");
    return p;
}

TaRtList *ta_rt_list_new_n(int64_t n) {
    if (n < 0) n = 0;
    TaRtList *l = rt_alloc(sizeof(TaRtList) + (size_t)n * 8);
    l->len = n;
    return l;
}

void *ta_rt_list_get(TaRtList *l, int64_t i) {
    if (!l || i < 0 || i >= l->len) ta_rt_abort_index(i, l ? l->len : 0);
    return &l->cells[i];
}

void ta_rt_list_set(TaRtList *l, int64_t i, void *src) {
    if (!l || i < 0 || i >= l->len) ta_rt_abort_index(i, l ? l->len : 0);
    memcpy(&l->cells[i], src, 8);
}

static uint64_t rt_hash_str(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t rt_hash_int(int64_t v) {
    uint64_t x = (uint64_t)v;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

static bool rt_key_eq(void *ka, void *kb, int64_t key_is_str) {
    if (key_is_str) {
        TaRtStr *a = *(TaRtStr **)ka;
        TaRtStr *b = *(TaRtStr **)kb;
        if (a == b) return true;
        if (!a || !b || a->len != b->len) return false;
        return memcmp(a->data, b->data, (size_t)a->len) == 0;
    }
    return memcmp(ka, kb, 8) == 0;
}

static uint64_t rt_key_hash(void *k, int64_t key_is_str) {
    if (key_is_str) {
        TaRtStr *s = *(TaRtStr **)k;
        return s ? rt_hash_str(s->data, (size_t)s->len) : 0;
    }
    int64_t v;
    memcpy(&v, k, 8);
    return rt_hash_int(v);
}

TaRtDict *ta_rt_dict_new(void) {
    TaRtDict *d = rt_alloc(sizeof(TaRtDict));
    d->cap = 8;
    d->state = rt_alloc((size_t)d->cap);
    d->keys = rt_alloc((size_t)d->cap * 8);
    d->vals = rt_alloc((size_t)d->cap * 8);
    return d;
}

static void dict_grow(TaRtDict *d, int64_t key_is_str);

void ta_rt_dict_set(TaRtDict *d, void *key, void *val, int64_t key_is_str) {
    if ((d->count + 1) * 10 >= d->cap * 7) dict_grow(d, key_is_str);
    size_t mask = (size_t)d->cap - 1;
    size_t i = (size_t)(rt_key_hash(key, key_is_str) & mask);
    for (;;) {
        if (d->state[i] == 0) break;
        void *kslot = (char *)d->keys + i * 8;
        if (rt_key_eq(kslot, key, key_is_str)) break;
        i = (i + 1) & mask;
    }
    void *kslot = (char *)d->keys + i * 8;
    if (d->state[i] == 0) {
        memcpy(kslot, key, 8);
        d->state[i] = 1;
        d->count++;
    }
    memcpy((char *)d->vals + i * 8, val, 8);
}

void *ta_rt_dict_get(TaRtDict *d, void *key, int64_t key_is_str) {
    size_t mask = (size_t)d->cap - 1;
    size_t i = (size_t)(rt_key_hash(key, key_is_str) & mask);
    for (;;) {
        if (d->state[i] == 0) ta_rt_abort_key();
        void *kslot = (char *)d->keys + i * 8;
        if (rt_key_eq(kslot, key, key_is_str)) return (char *)d->vals + i * 8;
        i = (i + 1) & mask;
    }
}

static void dict_grow(TaRtDict *d, int64_t key_is_str) {
    TaRtDict nd;
    nd.cap = d->cap * 2;
    nd.count = 0;
    nd.state = rt_alloc((size_t)nd.cap);
    nd.keys = rt_alloc((size_t)nd.cap * 8);
    nd.vals = rt_alloc((size_t)nd.cap * 8);
    for (int64_t i = 0; i < d->cap; i++) {
        if (d->state[i]) {
            void *k = (char *)d->keys + i * 8;
            void *v = (char *)d->vals + i * 8;
            ta_rt_dict_set(&nd, k, v, key_is_str);
        }
    }
    free(d->state);
    free(d->keys);
    free(d->vals);
    *d = nd;
}

TaRtStr *ta_rt_str_new(int64_t len) {
    if (len < 0) len = 0;
    TaRtStr *s = rt_alloc(sizeof(TaRtStr) + (size_t)len + 1);
    s->len = len;
    s->data[len] = 0;
    return s;
}

TaRtStr *ta_rt_str_concat(TaRtStr *a, TaRtStr *b) {
    int64_t la = a ? a->len : 0;
    int64_t lb = b ? b->len : 0;
    TaRtStr *s = ta_rt_str_new(la + lb);
    if (la) memcpy(s->data, a->data, (size_t)la);
    if (lb) memcpy(s->data + la, b->data, (size_t)lb);
    return s;
}

int64_t ta_rt_str_eq(TaRtStr *a, TaRtStr *b) {
    if (a == b) return 1;
    if (!a || !b || a->len != b->len) return 0;
    return memcmp(a->data, b->data, (size_t)a->len) == 0 ? 1 : 0;
}

int64_t ta_rt_str_cmp(TaRtStr *a, TaRtStr *b) {
    size_t la = a ? (size_t)a->len : 0;
    size_t lb = b ? (size_t)b->len : 0;
    size_t n = la < lb ? la : lb;
    int r = n ? memcmp(a->data, b->data, n) : 0;
    if (r != 0) return r;
    if (la == lb) return 0;
    return la < lb ? -1 : 1;
}

static const uint8_t rt_utf8_len[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0
};

int64_t ta_rt_str_at(TaRtStr *s, int64_t i) {
    if (!s || i < 0) ta_rt_abort_index(i, s ? s->len : 0);
    int64_t cp_index = 0;
    size_t pos = 0;
    while (pos < (size_t)s->len) {
        uint8_t c = (uint8_t)s->data[pos];
        size_t cl = rt_utf8_len[c];
        if (cl == 0) cl = 1;
        if ((size_t)(pos + cl) > (size_t)s->len) cl = 1;
        if (cp_index == i) {
            if (cl == 1) return c;
            uint32_t cp = c & ((1u << (7 - cl)) - 1);
            for (size_t k = 1; k < cl; k++) cp = (cp << 6) | ((uint8_t)s->data[pos + k] & 0x3F);
            return (int64_t)cp;
        }
        pos += cl;
        cp_index++;
    }
    ta_rt_abort_index(i, cp_index);
    return -1;
}

TaRtStr *ta_rt_str_sub(TaRtStr *s, int64_t start, int64_t end) {
    if (!s) return ta_rt_str_new(0);
    if (start < 0) start = 0;
    if (end > s->len) end = s->len;
    if (end <= start) return ta_rt_str_new(0);
    TaRtStr *out = ta_rt_str_new(end - start);
    memcpy(out->data, s->data + start, (size_t)(end - start));
    return out;
}

TaRtStr *ta_rt_input(void) {
    char *buf = NULL;
    size_t cap = 0;
    ssize_t n = getline(&buf, &cap, stdin);
    if (n < 0) {
        free(buf);
        return ta_rt_str_new(0);
    }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    TaRtStr *s = ta_rt_str_new(n);
    if (n) memcpy(s->data, buf, (size_t)n);
    free(buf);
    return s;
}

static TaRtList *rt_range_fill(int64_t start, int64_t stop, int64_t step) {
    if (step == 0) step = 1;
    int64_t n = 0;
    if (step > 0) {
        for (int64_t v = start; v < stop; v += step) n++;
    } else {
        for (int64_t v = start; v > stop; v += step) n++;
    }
    TaRtList *l = ta_rt_list_new_n(n);
    int64_t k = 0;
    if (step > 0) {
        for (int64_t v = start; v < stop; v += step) l->cells[k++] = (uint64_t)v;
    } else {
        for (int64_t v = start; v > stop; v += step) l->cells[k++] = (uint64_t)v;
    }
    return l;
}

TaRtList *ta_rt_range_1(int64_t n) { return rt_range_fill(0, n, 1); }
TaRtList *ta_rt_range_2(int64_t a, int64_t b) { return rt_range_fill(a, b, 1); }
TaRtList *ta_rt_range_3(int64_t a, int64_t b, int64_t s) { return rt_range_fill(a, b, s); }

void ta_rt_print_int(int64_t v) { printf("%" PRId64, v); }

void ta_rt_print_double(double v) {
    char buf[64];
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, sizeof(buf), "%.*g", prec, v);
        if (strtod(buf, NULL) == v) break;
    }
    fputs(buf, stdout);
}

void ta_rt_print_float(double v) { ta_rt_print_double(v); }

void ta_rt_print_bool(int64_t v) {
    fputs(v ? "\u0b89\u0ba3\u0bcd\u0bae\u0bc8" : "\u0baa\u0bca\u0baf\u0bcd", stdout);
}

void ta_rt_print_char(int64_t cp) {
    char buf[4];
    size_t n = 0;
    uint32_t c = (uint32_t)cp;
    if (c < 0x80) {
        buf[n++] = (char)c;
    } else if (c < 0x800) {
        buf[n++] = (char)(0xC0 | (c >> 6));
        buf[n++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        buf[n++] = (char)(0xE0 | (c >> 12));
        buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (c & 0x3F));
    } else {
        buf[n++] = (char)(0xF0 | (c >> 18));
        buf[n++] = (char)(0x80 | ((c >> 12) & 0x3F));
        buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (c & 0x3F));
    }
    fwrite(buf, 1, n, stdout);
}

void ta_rt_print_str(TaRtStr *s) {
    if (s && s->len) fwrite(s->data, 1, (size_t)s->len, stdout);
}

void ta_rt_print_space(void) { fputc(' ', stdout); }
void ta_rt_print_nl(void) { fputc('\n', stdout); }

int64_t ta_rt_abs_i(int64_t v) { return v < 0 ? -v : v; }
double ta_rt_abs_f(double v) { return fabs(v); }
int64_t ta_rt_floor(double v) { return (int64_t)floor(v); }
double ta_rt_sqrt(double v) { return sqrt(v); }
double ta_rt_pow_f(double a, double b) { return pow(a, b); }

int64_t ta_rt_pow_i(int64_t a, int64_t b) {
    if (b < 0) return (int64_t)pow((double)a, (double)b);
    int64_t r = 1;
    int64_t base = a;
    while (b > 0) {
        if (b & 1) r *= base;
        base *= base;
        b >>= 1;
    }
    return r;
}
