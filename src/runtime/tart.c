#define _GNU_SOURCE
#include "tart.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

void ta_rt_init(void) {
#if defined(_WIN32)
    /* Switch the console to UTF-8 so Tamil text renders correctly. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    _setmode(_fileno(stdin), _O_U8TEXT);
#endif
    (void)0;
}

static void rt_fail(const char *msg) {
    fprintf(stderr, "\u0b87\u0baf\u0b95\u0bcd\u0b95 \u0ba8\u0bc7\u0bb0\u0bcd \u0baa\u0bbf\u0bb4\u0bc8: %s\n", msg);
    exit(70);
}

void ta_rt_abort_div_zero(void) {
    rt_fail("\u0baa\u0bc2\u0bb0\u0bbf\u0baf\u0bbe\u0bb2\u0bcd \u0bb5\u0b95\u0bc1\u0ba4\u0bcd\u0ba4\u0bae\u0bcd");
}

void ta_rt_abort_index(int64_t idx, int64_t len) {
    fprintf(stderr,
            "\u0b87\u0baf\u0b95\u0bcd\u0b95 \u0ba8\u0bc7\u0bb0\u0bcd \u0baa\u0bbf\u0bb4\u0bc8: \u0b85\u0b9f\u0bc1\u0baf\u0bbe\u0ba3\u0bb5\u0bc1 "
            "%" PRId64 " \u0b8e\u0bb2\u0bcd\u0bb2\u0bc8\u0b95\u0bcd\u0b95\u0bc1 \u0bb5\u0bc6\u0bb3\u0bbf\u0baf\u0bbe\u0ba9\u0ba4\u0bc1 (\u0ba8\u0bc0\u0bb3\u0bae\u0bcd %" PRId64 ")\n",
            idx, len);
    exit(70);
}

void ta_rt_abort_key(void) {
    rt_fail("\u0b95\u0bc1\u0bb1\u0bbf\u0baa\u0bcd\u0baa\u0bbf\u0b9f\u0baa\u0bcd\u0baa\u0b9f\u0bcd\u0b9f \u0b9a\u0bbe\u0bb5\u0bbf \u0b85\u0b95\u0bb0\u0bbe\u0ba4\u0bbf\u0baf\u0bbf\u0bb2\u0bcd \u0b87\u0bb2\u0bcd\u0bb2\u0bc8");
}

/* ================= conservative mark-sweep GC =================
   structure follows "Baby's First Garbage Collector", adapted:
   roots = machine stack + callee-saved regs (setjmp), because the
   generated program keeps every value in rbp frame slots. */

typedef struct TaGcHead {
    struct TaGcHead *next;
    size_t size;
    uint8_t marked;
    uint8_t type;          /* 0 raw, 1 str, 2 list, 3 dict */
} TaGcHead;

#define TA_GC_HDR ((sizeof(TaGcHead) + 15) & ~(size_t)15)

static TaGcHead *gc_objects = NULL;
static int64_t gc_collections = 0;
static size_t gc_in_use = 0;
static size_t gc_threshold = 8u * 1024u * 1024u;
static size_t gc_since = 0;
static void *gc_stack_bottom = NULL;
static int gc_disabled = -1;
static int gc_stats_env = -1;

/* Determine the real top of the stack (conservative scan upper bound).
   Linux: parse /proc/self/maps for the "[stack]" region's end address.
   macOS / *BSD: use the pthread stack-address APIs.
   Other platforms: fall back to the address of a local variable. */
static void gc_init_stack_bounds(void) {
    static atomic_int gc_init_done = 0;
    if (atomic_exchange(&gc_init_done, 1)) return;

#if defined(__APPLE__)
    void *base = pthread_get_stackaddr_np(pthread_self());
    size_t size = pthread_get_stacksize_np(pthread_self());
    /* pthread reports the *lowest* address; the top is base + size. */
    gc_stack_bottom = (void *)((uintptr_t)base + size);
    return;
#endif

    /* Prefer the thread-attribute API: it returns the bounds of the *current*
       thread's stack, which is correct even when /proc/self/maps reports a
       stale or secondary "[stack]" region (this happens under AddressSanitizer,
       where the mapped [stack] does not match the thread actually executing).
       Falls back to /proc/self/maps on Linux, then to a conservative bound. */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    pthread_attr_t a;
    if (pthread_getattr_np(pthread_self(), &a) == 0) {
        void *base = NULL;
        size_t size = 0;
        if (pthread_attr_getstack(&a, &base, &size) == 0 && size) {
            gc_stack_bottom = (void *)((uintptr_t)base + size);
            pthread_attr_destroy(&a);
            return;
        }
        pthread_attr_destroy(&a);
    }
#endif

#if defined(__linux__)
    FILE *m = fopen("/proc/self/maps", "r");
    if (m) {
        char line[512];
        while (fgets(line, sizeof(line), m)) {
            unsigned long long start = 0, end = 0;
            char path[256] = {0};
            if (sscanf(line, "%llx-%llx %*s %*s %*s %*s %255[^\n]",
                        &start, &end, path) == 3) {
                if (strstr(path, "[stack]")) {
                    gc_stack_bottom = (void *)(uintptr_t)end;
                    break;
                }
            }
        }
        fclose(m);
        return;
    }
#endif

    /* Unknown platform: fall back to a conservative bound that covers the
       whole program image (data/BSS + heap + stack). Over-estimation is safe
       for a conservative collector; this branch only runs on unsupported OSes. */
    static int sentinel;
    gc_stack_bottom = (void *)&sentinel;
}

static int gc_enabled(void) {
    if (gc_disabled < 0) {
        const char *e = getenv("TA_GC");
        gc_disabled = (e && strcmp(e, "0") == 0) ? 1 : 0;
    }
    return !gc_disabled;
}

void ta_rt_gc_set_threshold(int64_t bytes) {
    if (bytes > 0) gc_threshold = (size_t)bytes;
}

void ta_rt_gc_stats(int64_t *collections, int64_t *bytes_in_use) {
    if (collections) *collections = gc_collections;
    if (bytes_in_use) *bytes_in_use = (int64_t)gc_in_use;
}

static void gc_trace_object(TaGcHead *h);

static void gc_release_user(void *user) {
    if (!user) return;
    /* header is always at a fixed offset before the user pointer */
    TaGcHead *h = (TaGcHead *)((char *)user - TA_GC_HDR);
    TaGcHead **link = &gc_objects;
    while (*link && *link != h) link = &(*link)->next;
    if (*link) {
        *link = h->next;
        gc_in_use -= h->size;
        free(h);
    }
}

/* ---- O(1) pointer -> object lookup table ----
   Rebuilt once per collection (O(objects)). Every candidate-pointer check
   that used to be a linear O(objects) walk of gc_objects becomes an O(1)
   probe here, removing the quadratic blowup that made collection impossible
   once the heap held hundreds of thousands of live objects. */
static TaGcHead **gc_table = NULL;
static size_t gc_table_cap = 0;

static size_t gc_hash_ptr(uintptr_t k, size_t cap) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k & (cap - 1);
}

static void gc_table_build(void) {
    size_t n = 0;
    for (TaGcHead *h = gc_objects; h; h = h->next) n++;
    size_t cap = 16;
    while (cap < n * 2) cap *= 2;
    gc_table_cap = cap;
    gc_table = malloc(cap * sizeof(TaGcHead *));
    for (size_t i = 0; i < cap; i++) gc_table[i] = NULL;
    for (TaGcHead *h = gc_objects; h; h = h->next) {
        uintptr_t key = (uintptr_t)((char *)h + TA_GC_HDR);
        size_t idx = gc_hash_ptr(key, cap);
        while (gc_table[idx]) idx = (idx + 1) & (cap - 1);
        gc_table[idx] = h;
    }
}

static TaGcHead *gc_table_find(void *user) {
    if (!gc_table) return NULL;
    uintptr_t key = (uintptr_t)user;
    size_t idx = gc_hash_ptr(key, gc_table_cap);
    while (gc_table[idx]) {
        TaGcHead *h = gc_table[idx];
        if ((uintptr_t)((char *)h + TA_GC_HDR) == key) return h;
        idx = (idx + 1) & (gc_table_cap - 1);
    }
    return NULL;
}

/* ---- mark worklist ----
   Instead of recursing (C-stack risk on deep object graphs) or re-walking
   the whole object list to a fixpoint (O(objects^2)), newly-marked objects
   are pushed onto a worklist and drained iteratively. Each object is traced
   exactly once, so total mark work is O(live objects + live bytes). */
static TaGcHead **gc_wl = NULL;
static size_t gc_wl_len = 0, gc_wl_cap = 0;

static void gc_wl_push(TaGcHead *h) {
    if (gc_wl_len == gc_wl_cap) {
        gc_wl_cap = gc_wl_cap ? gc_wl_cap * 2 : 256;
        gc_wl = realloc(gc_wl, gc_wl_cap * sizeof(TaGcHead *));
    }
    gc_wl[gc_wl_len++] = h;
}

static void gc_scan_pointer(void *cand) {
    if (!cand) return;
    TaGcHead *h = gc_table_find(cand);
    if (!h || h->marked) return;
    h->marked = 1;
    gc_wl_push(h);
}

#if defined(__SANITIZE_ADDRESS__)
#  if defined(__clang__)
#    define TA_NO_ASAN __attribute__((no_sanitize("address")))
#  else
#    define TA_NO_ASAN __attribute__((no_sanitize_address))
#  endif
#else
#  define TA_NO_ASAN
#endif

/* Conservative stack/register scans read raw memory that AddressSanitizer
   deliberately poisons (stack redzones). Disable ASan instrumentation here so
   the deliberate conservative scan is not reported as an out-of-bounds access.
   The scan bounds are validated by the caller (gc_collect_inner). */
static void gc_scan_range(const char *lo, const char *hi) {
    for (const char *p = lo; p + sizeof(void *) <= hi; p += sizeof(void *)) {
        void *cand;
        memcpy(&cand, p, sizeof(cand));
        gc_scan_pointer(cand);
    }
}

static void gc_trace_object(TaGcHead *h) {
    void *u = (char *)h + TA_GC_HDR;
    if (h->type == 2) {
        TaRtList *l = (TaRtList *)u;
        for (int64_t i = 0; i < l->len; i++)
            gc_scan_range((const char *)&l->cells[i],
                          (const char *)&l->cells[i] + 8);
    } else if (h->type == 3) {
        TaRtDict *d = (TaRtDict *)u;
        gc_scan_pointer(d->state);
        gc_scan_pointer(d->keys);
        gc_scan_pointer(d->vals);
        if (d->state && d->keys && d->vals) {
            for (int64_t i = 0; i < d->cap; i++) {
                if (!d->state[i]) continue;
                gc_scan_range((char *)d->keys + i * 8,
                              (char *)d->keys + i * 8 + 8);
                gc_scan_range((char *)d->vals + i * 8,
                              (char *)d->vals + i * 8 + 8);
            }
        }
    }
}

static void gc_collect_inner(const char *why) {
    jmp_buf regs;
    setjmp(regs);

    gc_init_stack_bounds();

    char here_addr_buf = 0;
    const char *here_addr = &here_addr_buf;
    /* scan from this (deepest live) frame up to the real stack top */
    const char *lo = here_addr;
    const char *hi = (const char *)gc_stack_bottom;
    if (lo > hi) { const char *t = lo; lo = hi; hi = t; }
    /* Conservative scanning reads 8-byte-aligned pointer slots; the scan
       window must be 8-byte aligned or a slot straddles two windows and is
       never seen as a whole pointer (silently missing live roots). */
    lo = (const char *)((uintptr_t)lo & ~(uintptr_t)7);
    hi = (const char *)(((uintptr_t)hi) & ~(uintptr_t)7);
    /* Build the O(1) lookup table once, then reset the mark worklist. */
    gc_table_build();
    gc_wl = NULL; gc_wl_len = 0; gc_wl_cap = 0;

    /* Scan roots: stack frames + callee-saved registers. */
#if defined(__SANITIZE_ADDRESS__)
    __asan_unpoison_memory_region(lo, (size_t)((char *)hi - (char *)lo));
#endif
    gc_scan_range(lo, hi);
    gc_scan_range((const char *)regs, (const char *)regs + sizeof(jmp_buf));
#if defined(__SANITIZE_ADDRESS__)
    __asan_poison_memory_region(lo, (size_t)((char *)hi - (char *)lo));
#endif

    /* Drain the worklist: every marked object is traced exactly once.
       This replaces the old O(objects^2) fixpoint re-scan with O(live
       objects + live bytes) marking. */
    for (size_t i = 0; i < gc_wl_len; i++)
        gc_trace_object(gc_wl[i]);

    {
        static int dbg = -1;
        if (dbg < 0) { const char *e = getenv("TA_GC_DEBUG"); dbg = e ? 1 : 0; }
        if (dbg) {
            size_t n = 0, mk = 0; for (TaGcHead *q = gc_objects; q; q = q->next) { n++; mk += q->marked; }
            fprintf(stderr, "[ta-gc-debug] %s: %zu/%zu objects live, %zu bytes in use (lo=%p hi=%p)\n",
                    why, mk, n, gc_in_use, (void *)lo, (void *)hi);
        }
    }

    size_t freed_bytes = 0;
    TaGcHead **link = &gc_objects;
    while (*link) {
        TaGcHead *h = *link;
        if (h->marked) {
            h->marked = 0;
            link = &h->next;
        } else {
            *link = h->next;
            freed_bytes += h->size;
            free(h);
        }
    }
    gc_in_use -= freed_bytes;
    gc_since = 0;
    gc_collections++;
    if (gc_stats_env < 0) {
        const char *e = getenv("TA_GC_STATS");
        gc_stats_env = e ? 1 : 0;
    }
    if (gc_stats_env)
        fprintf(stderr, "[ta-gc] %s: %zu bytes live after collect #%lld\n",
                why, gc_in_use, (long long)gc_collections);

    free(gc_table); gc_table = NULL; gc_table_cap = 0;
    free(gc_wl); gc_wl = NULL; gc_wl_len = gc_wl_cap = 0;
}

void ta_rt_gc_collect(void) { gc_collect_inner("manual"); }

static void *rt_alloc(size_t n) {
    if (n < 8) n = 8;
    if (gc_enabled()) {
        TaGcHead *h = malloc(TA_GC_HDR + n);
        if (!h) rt_fail("\u0ba8\u0bbf\u0ba9\u0bc8\u0bb5\u0b95\u0bae\u0bcd \u0baa\u0bcb\u0ba4\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8");
        memset((char *)h + TA_GC_HDR, 0, n);
        h->next = NULL;
        h->size = n;
        h->marked = 0;
        h->type = 0;
        /* Collect BEFORE linking the new block into gc_objects. Otherwise a
           collection triggered by this very allocation would see the fresh
           object in the live set but unrooted (only in a register) and free
           it, leaving the caller with a dangling pointer. */
        if (gc_since + n > gc_threshold) {
            gc_collect_inner("alloc");
            if (gc_since + n > gc_threshold / 2) gc_threshold *= 2;
        }
        h->next = gc_objects;
        gc_objects = h;
        gc_in_use += n;
        gc_since += n;
        return (char *)h + TA_GC_HDR;
    }
    void *p = calloc(1, n ? n : 1);
    if (!p) rt_fail("\u0ba8\u0bbf\u0ba9\u0bc8\u0bb5\u0b95\u0bae\u0bcd \u0baa\u0bcb\u0ba4\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8");
    return p;
}

TaRtList *ta_rt_list_new_n(int64_t n) {
    if (n < 0) n = 0;
    /* Overflow guard: (size_t)n * 8 must not wrap around size_t. */
    if ((uint64_t)n > SIZE_MAX / 8) {
        rt_fail("\u0ba8\u0bbf\u0ba9\u0bc8\u0bb5\u0b95\u0bae\u0bcd \u0baa\u0bcb\u0ba4\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8");
    }
    TaRtList *l = rt_alloc(sizeof(TaRtList) + (size_t)n * 8);
    ((TaGcHead *)((char *)l - TA_GC_HDR))->type = 2;
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

/* Immutable push: returns a NEW list with `elem` appended. */
TaRtList *ta_rt_list_push(TaRtList *l, void *elem) {
    int64_t old = l ? l->len : 0;
    TaRtList *nl = ta_rt_list_new_n(old + 1);
    for (int64_t i = 0; i < old; i++) nl->cells[i] = l->cells[i];
    nl->cells[old] = (uint64_t)(uintptr_t)elem;
    return nl;
}

/* Immutable pop: returns a NEW list with the last element removed. */
TaRtList *ta_rt_list_pop(TaRtList *l) {
    int64_t old = l ? l->len : 0;
    if (old == 0) return ta_rt_list_new_n(0);
    TaRtList *nl = ta_rt_list_new_n(old - 1);
    for (int64_t i = 0; i < old - 1; i++) nl->cells[i] = l->cells[i];
    return nl;
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
    ((TaGcHead *)((char *)d - TA_GC_HDR))->type = 3;
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

TaRtList *ta_rt_dict_keys(TaRtDict *d) {
    if (!d) return ta_rt_list_new_n(0);
    size_t cnt = 0;
    for (int64_t i = 0; i < d->cap; i++) if (d->state[i]) cnt++;
    TaRtList *keys = ta_rt_list_new_n((int64_t)cnt);
    size_t pos = 0;
    for (int64_t i = 0; i < d->cap; i++) {
        if (d->state[i]) {
            keys->cells[pos++] = *(uint64_t *)((char *)d->keys + i * 8);
        }
    }
    return keys;
}

TaRtList *ta_rt_dict_items(TaRtDict *d) {
    if (!d) return ta_rt_list_new_n(0);
    size_t cnt = 0;
    for (int64_t i = 0; i < d->cap; i++) if (d->state[i]) cnt++;
    TaRtList *items = ta_rt_list_new_n((int64_t)cnt);
    size_t pos = 0;
    for (int64_t i = 0; i < d->cap; i++) {
        if (d->state[i]) {
            TaRtList *pair = ta_rt_list_new_n(2);
            pair->cells[0] = *(uint64_t *)((char *)d->keys + i * 8);
            pair->cells[1] = *(uint64_t *)((char *)d->vals + i * 8);
            items->cells[pos++] = (uint64_t)(uintptr_t)pair;
        }
    }
    return items;
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
    gc_release_user(d->state);
    gc_release_user(d->keys);
    gc_release_user(d->vals);
    *d = nd;
}

static void fill_cp_len(TaRtStr *s);

/* Declared here so fill_cp_len (defined above) can use it before its
   definition later in this file. */
static const uint8_t rt_utf8_len[256];

TaRtStr *ta_rt_str_new(int64_t len) {
    if (len < 0) len = 0;
    TaRtStr *s = rt_alloc(sizeof(TaRtStr) + (size_t)len + 1);
    ((TaGcHead *)((char *)s - TA_GC_HDR))->type = 1;
    s->len = len;
    s->data[len] = 0;
    s->cp_len = -1;   /* computed lazily by ta_rt_str_at / ta_rt_str_sub */
    return s;
}

/* Build a TaRtStr by copying `len` bytes from `data`. Used by the C backend. */
TaRtStr *ta_rt_str_from(const char *data, int64_t len) {
    if (len < 0) len = 0;
    TaRtStr *s = ta_rt_str_new(len);
    if (data && len > 0) memcpy(s->data, data, (size_t)len);
    return s;
}

/* Count and cache the number of UTF-8 code points in s->data. */
static void fill_cp_len(TaRtStr *s) {
    if (!s) return;
    int64_t cp = 0;
    size_t pos = 0;
    while (pos < (size_t)s->len) {
        uint8_t c = (uint8_t)s->data[pos];
        size_t cl = rt_utf8_len[c];
        if (cl == 0) cl = 1;
        if ((size_t)(pos + cl) > (size_t)s->len) cl = 1;
        pos += cl;
        cp++;
    }
    s->cp_len = cp;
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
    if (!s) ta_rt_abort_index(i, 0);
    /* Use the cached code-point count (computed once, O(1) on later calls). */
    if (s->cp_len < 0) fill_cp_len(s);
    int64_t ncp = s->cp_len;
    int64_t idx = i;
    if (idx < 0) idx += ncp;
    if (idx < 0 || idx >= ncp) ta_rt_abort_index(i, ncp);

    int64_t cp_index = 0;
    size_t pos = 0;
    while (pos < (size_t)s->len) {
        uint8_t c = (uint8_t)s->data[pos];
        size_t cl = rt_utf8_len[c];
        if (cl == 0) cl = 1;
        if ((size_t)(pos + cl) > (size_t)s->len) cl = 1;
        if (cp_index == idx) {
            if (cl == 1) return c;
            uint32_t cp = c & ((1u << (7 - cl)) - 1);
            for (size_t k = 1; k < cl; k++) cp = (cp << 6) | ((uint8_t)s->data[pos + k] & 0x3F);
            return (int64_t)cp;
        }
        pos += cl;
        cp_index++;
    }
    ta_rt_abort_index(i, ncp);
    return -1;
}

/* Internal helper: slice by raw byte offsets (used by split/strip/replace). */
static TaRtStr *str_sub_bytes(TaRtStr *s, size_t start, size_t end) {
    if (!s) return ta_rt_str_new(0);
    if (end > (size_t)s->len) end = (size_t)s->len;
    if (end <= start) return ta_rt_str_new(0);
    TaRtStr *out = ta_rt_str_new((int64_t)(end - start));
    memcpy(out->data, s->data + start, (size_t)(end - start));
    return out;
}

/* Map a code-point index to its starting byte offset. Returns s->len (one past
   the end) if the index is at or past the end of the string. */
static size_t cp_index_to_byte(const TaRtStr *s, int64_t cp_index) {
    size_t pos = 0;
    int64_t cur = 0;
    while (pos < (size_t)s->len && cur < cp_index) {
        uint8_t c = (uint8_t)s->data[pos];
        size_t cl = rt_utf8_len[c];
        if (cl == 0) cl = 1;
        if ((size_t)(pos + cl) > (size_t)s->len) cl = 1;
        pos += cl;
        cur++;
    }
    return pos;
}

/* Public API: slice by Unicode code-point indices (Python-style semantics). */
TaRtStr *ta_rt_str_sub(TaRtStr *s, int64_t start_cp, int64_t end_cp) {
    if (!s) return ta_rt_str_new(0);
    if (s->cp_len < 0) fill_cp_len(s);
    int64_t n = s->cp_len;
    if (start_cp < 0) start_cp = 0;
    if (end_cp > n) end_cp = n;
    if (end_cp <= start_cp) return ta_rt_str_new(0);
    size_t start_b = cp_index_to_byte(s, start_cp);
    size_t   end_b = cp_index_to_byte(s, end_cp);
    return str_sub_bytes(s, start_b, end_b);
}

TaRtList *ta_rt_str_split(TaRtStr *s, TaRtStr *sep) {
    if (!s) return ta_rt_list_new_n(0);
    if (!sep || sep->len == 0) {
        TaRtList *l = ta_rt_list_new_n(1);
        ta_rt_list_set(l, 0, &s);
        return l;
    }
    size_t slen = (size_t)s->len, plen = (size_t)sep->len;
    size_t cap = 4, n = 0;
    size_t *rng = malloc(cap * 2 * sizeof(size_t));
    size_t prev = 0, i = 0;
    while (i + plen <= slen) {
        if (memcmp(s->data + i, sep->data, plen) == 0) {
            if (n + 1 > cap) { cap *= 2; rng = realloc(rng, cap * 2 * sizeof(size_t)); }
            rng[2 * n] = prev;
            rng[2 * n + 1] = i;
            n++;
            prev = i + plen;
            i = prev;
        } else {
            i++;
        }
    }
    if (n + 1 > cap) { cap *= 2; rng = realloc(rng, cap * 2 * sizeof(size_t)); }
    rng[2 * n] = prev;
    rng[2 * n + 1] = slen;
    n++;
    TaRtList *l = ta_rt_list_new_n((int64_t)n);
    for (size_t k = 0; k < n; k++) {
        TaRtStr *piece = str_sub_bytes(s, rng[2 * k], rng[2 * k + 1]);
        ta_rt_list_set(l, (int64_t)k, &piece);
    }
    free(rng);
    return l;
}

TaRtStr *ta_rt_str_join(TaRtList *l, TaRtStr *sep) {
    if (!l || l->len == 0) return ta_rt_str_new(0);
    TaRtStr *sp = sep ? sep : ta_rt_str_new(0);
    size_t seplen = sp ? (size_t)sp->len : 0;
    size_t total = 0;
    for (int64_t k = 0; k < l->len; k++) {
        TaRtStr *e = (TaRtStr *)(uintptr_t)l->cells[k];
        if (e) total += (size_t)e->len;
    }
    if (l->len > 0) total += seplen * ((size_t)l->len - 1);
    TaRtStr *out = ta_rt_str_new((int64_t)total);
    size_t off = 0;
    for (int64_t k = 0; k < l->len; k++) {
        if (k > 0 && seplen) {
            memcpy(out->data + off, sp->data, seplen);
            off += seplen;
        }
        TaRtStr *e = (TaRtStr *)(uintptr_t)l->cells[k];
        if (e && e->len) {
            memcpy(out->data + off, e->data, (size_t)e->len);
            off += (size_t)e->len;
        }
    }
    return out;
}

TaRtStr *ta_rt_str_strip(TaRtStr *s) {
    if (!s) return ta_rt_str_new(0);
    size_t start = 0, end = (size_t)s->len;
    while (start < end && isspace((unsigned char)s->data[start])) start++;
    while (end > start && isspace((unsigned char)s->data[end - 1])) end--;
    return str_sub_bytes(s, start, end);
}

TaRtStr *ta_rt_str_replace(TaRtStr *s, TaRtStr *old, TaRtStr *new) {
    if (!s) return ta_rt_str_new(0);
    if (!old || old->len == 0) return str_sub_bytes(s, 0, (size_t)s->len);
    size_t slen = (size_t)s->len, olen = (size_t)old->len;
    size_t nlen = (new && new->len) ? (size_t)new->len : 0;
    size_t count = 0, i = 0;
    while (i + olen <= slen) {
        if (memcmp(s->data + i, old->data, olen) == 0) { count++; i += olen; }
        else i++;
    }
    size_t total = slen - count * olen + count * nlen;
    TaRtStr *out = ta_rt_str_new((int64_t)total);
    size_t off = 0;
    i = 0;
    while (i + olen <= slen) {
        if (memcmp(s->data + i, old->data, olen) == 0) {
            if (nlen) memcpy(out->data + off, new->data, nlen);
            off += nlen;
            i += olen;
        } else {
            out->data[off++] = s->data[i++];
        }
    }
    while (i < slen) out->data[off++] = s->data[i++];
    return out;
}

static TaRtStr *ta_rt_str_case(TaRtStr *s, int up) {
    if (!s) return ta_rt_str_new(0);
    TaRtStr *out = ta_rt_str_new(s->len);
    for (int64_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        if (up) { if (c >= 'a' && c <= 'z') c -= 32; }
        else    { if (c >= 'A' && c <= 'Z') c += 32; }
        out->data[i] = (char)c;
    }
    return out;
}

TaRtStr *ta_rt_str_upper(TaRtStr *s) { return ta_rt_str_case(s, 1); }
TaRtStr *ta_rt_str_lower(TaRtStr *s) { return ta_rt_str_case(s, 0); }

int64_t ta_rt_str_startswith(TaRtStr *s, TaRtStr *p) {
    if (!s || !p) return 0;
    if (p->len > s->len) return 0;
    return memcmp(s->data, p->data, (size_t)p->len) == 0 ? 1 : 0;
}

int64_t ta_rt_str_endswith(TaRtStr *s, TaRtStr *p) {
    if (!s || !p) return 0;
    if (p->len > s->len) return 0;
    return memcmp(s->data + (s->len - p->len), p->data, (size_t)p->len) == 0 ? 1 : 0;
}

/* Returns the code-point index of the first occurrence of `sub` in `s`,
   or -1 if not found. */
int64_t ta_rt_str_find(TaRtStr *s, TaRtStr *sub) {
    if (!s || !sub || sub->len == 0) return -1;
    const char *p = memmem(s->data, (size_t)s->len, sub->data, (size_t)sub->len);
    if (!p) return -1;
    size_t off = (size_t)(p - s->data);
    int64_t cp = 0;
    size_t pos = 0;
    while (pos < off) {
        uint8_t c = (uint8_t)s->data[pos];
        size_t cl = rt_utf8_len[c];
        if (cl == 0) cl = 1;
        if (pos + cl > (size_t)s->len) cl = 1;
        pos += cl;
        cp++;
    }
    return cp;
}

/* Counts (non-overlapping) occurrences of `sub` in `s`. */
int64_t ta_rt_str_count(TaRtStr *s, TaRtStr *sub) {
    if (!s || !sub || sub->len == 0) return 0;
    int64_t count = 0;
    size_t i = 0, slen = (size_t)s->len, plen = (size_t)sub->len;
    while (i + plen <= slen) {
        if (memcmp(s->data + i, sub->data, plen) == 0) {
            count++;
            i += plen;
        } else {
            i++;
        }
    }
    return count;
}

void ta_rt_assert(int64_t cond, TaRtStr *msg) {
    if (cond) return;
    fprintf(stderr, "\u0b89\u0bb1\u0b95\u0bcd\u0b95 \u0ba8\u0bbf\u0bb0\u0bc2\u0baa\u0ba3\u0bc8: ");
    if (msg && msg->len) fwrite(msg->data, 1, (size_t)msg->len, stderr);
    else fputs("\u0baa\u0bb0\u0bbf\u0b9a\u0bcb\u0ba4\u0ba9\u0bae\u0bc8", stderr);
    fputc('\n', stderr);
    exit(70);
}

TaRtStr *ta_rt_input(void) {
    size_t cap = 128;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) rt_fail("\u0ba8\u0bbf\u0ba9\u0bc8\u0bb5\u0b95\u0bae\u0bcd \u0baa\u0bcb\u0ba4\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8");
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n' && c != '\r') {
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); rt_fail("\u0ba8\u0bbf\u0ba9\u0bc8\u0bb5\u0b95\u0bae\u0bcd \u0baa\u0bcb\u0ba4\u0bb5\u0bbf\u0bb2\u0bcd\u0bb2\u0bc8"); }
            buf = nb;
        }
        buf[len++] = (char)(unsigned char)c;
    }
    /* Tolerate a following LF after a CR (CRLF). */
    if (c == '\r') {
        int nxt = fgetc(stdin);
        if (nxt != '\n' && nxt != EOF) ungetc(nxt, stdin);
    }
    TaRtStr *s = ta_rt_str_new((int64_t)len);
    if (len) memcpy(s->data, buf, len);
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
