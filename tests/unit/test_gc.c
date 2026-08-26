/* Unit test: conservative mark-sweep GC in tart.c.
   Includes the .c directly so internal counters are assertable. */
#include "../../src/runtime/tart.c"
#include "test_util.h"

int main(void) {
    int64_t cols = 0;
    ta_rt_gc_set_threshold(1 << 20); /* avoid auto-collect during setup */

    /* --- garbage is collected --- */
    for (int i = 0; i < 500; i++) {
        TaRtStr *a = ta_rt_str_new(8);
        memcpy(a->data, "xxxxxxxx", 8);
        TaRtStr *b = ta_rt_str_concat(a, a);   /* dropped immediately */
        (void)b;
    }
    int64_t used_garbage = 0;
    ta_rt_gc_stats(NULL, &used_garbage);

    ta_rt_gc_collect();
    ta_rt_gc_stats(&cols, NULL);
    TA_CHECK(cols >= 1);

    int64_t used_after = 0;
    ta_rt_gc_stats(NULL, &used_after);
    TA_CHECK(used_after < used_garbage);

    /* --- live objects survive --- */
    TaRtList *keep = ta_rt_list_new_n(2);
    TaRtStr *live = ta_rt_str_new(6);
    memcpy(live->data, "\xE0\xAE\x95\xE0\xAE\x95", 6); /* கக */
    memcpy(&keep->cells[0], &live, sizeof(live));

    for (int i = 0; i < 2000; i++) {
        TaRtStr *g = ta_rt_str_concat(live, live);
        (void)g;
    }
    ta_rt_gc_collect();

    TaRtStr *still = NULL;
    memcpy(&still, &keep->cells[0], sizeof(still));
    TA_CHECK(still == live);
    TA_CHECK(still->len == 6);
    TA_CHECK(memcmp(still->data, "\xE0\xAE\x95\xE0\xAE\x95", 6) == 0);

    /* --- dict churn survives & frees --- */
    TaRtDict *d = ta_rt_dict_new();
    TaRtStr *key = ta_rt_str_new(3);
    memcpy(key->data, "abc", 3);
    int64_t v = 42;
    ta_rt_dict_set(d, &key, &v, 1);
    ta_rt_gc_collect();
    void *cell = ta_rt_dict_get(d, &key, 1);
    int64_t got;
    memcpy(&got, cell, 8);
    TA_CHECK(got == 42);

    TA_TEST_DONE("gc");
}
