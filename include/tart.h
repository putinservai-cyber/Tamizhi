#ifndef TZRT_H
#define TZRT_H

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int64_t len;
    char data[];
} TaRtStr;

typedef struct {
    int64_t len;
    uint64_t cells[];
} TaRtList;

typedef struct {
    int64_t count;
    int64_t cap;
    uint8_t *state;
    void *keys;
    void *vals;
} TaRtDict;

void ta_rt_abort_div_zero(void);
void ta_rt_abort_index(int64_t idx, int64_t len);
void ta_rt_abort_key(void);

TaRtList *ta_rt_list_new_n(int64_t n);
void *ta_rt_list_get(TaRtList *l, int64_t i);
void ta_rt_list_set(TaRtList *l, int64_t i, void *src);

TaRtDict *ta_rt_dict_new(void);
void *ta_rt_dict_get(TaRtDict *d, void *key, int64_t key_is_str);
void ta_rt_dict_set(TaRtDict *d, void *key, void *val, int64_t key_is_str);

TaRtStr *ta_rt_str_new(int64_t len);
TaRtStr *ta_rt_str_concat(TaRtStr *a, TaRtStr *b);
int64_t ta_rt_str_eq(TaRtStr *a, TaRtStr *b);
int64_t ta_rt_str_cmp(TaRtStr *a, TaRtStr *b);
int64_t ta_rt_str_at(TaRtStr *s, int64_t i);
TaRtStr *ta_rt_str_sub(TaRtStr *s, int64_t start, int64_t end);
TaRtList *ta_rt_str_split(TaRtStr *s, TaRtStr *sep);
TaRtStr *ta_rt_str_join(TaRtList *l, TaRtStr *sep);
TaRtStr *ta_rt_str_strip(TaRtStr *s);
TaRtStr *ta_rt_str_replace(TaRtStr *s, TaRtStr *old, TaRtStr *new);
TaRtStr *ta_rt_str_upper(TaRtStr *s);
TaRtStr *ta_rt_str_lower(TaRtStr *s);
int64_t ta_rt_str_startswith(TaRtStr *s, TaRtStr *prefix);
int64_t ta_rt_str_endswith(TaRtStr *s, TaRtStr *suffix);

TaRtStr *ta_rt_input(void);
TaRtList *ta_rt_range_1(int64_t n);
TaRtList *ta_rt_range_2(int64_t a, int64_t b);
TaRtList *ta_rt_range_3(int64_t a, int64_t b, int64_t step);

void ta_rt_print_int(int64_t v);
void ta_rt_print_double(double v);
void ta_rt_print_float(double v);
void ta_rt_print_bool(int64_t v);
void ta_rt_print_char(int64_t cp);
void ta_rt_print_str(TaRtStr *s);
void ta_rt_print_space(void);
void ta_rt_print_nl(void);

/* --- garbage collector (conservative mark-sweep) --- */
void ta_rt_gc_collect(void);
void ta_rt_gc_stats(int64_t *collections, int64_t *bytes_in_use);
void ta_rt_gc_set_threshold(int64_t bytes);

int64_t ta_rt_abs_i(int64_t v);
double ta_rt_abs_f(double v);
int64_t ta_rt_floor(double v);
double ta_rt_sqrt(double v);
double ta_rt_pow_f(double a, double b);
int64_t ta_rt_pow_i(int64_t a, int64_t b);

#endif
