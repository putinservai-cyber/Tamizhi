#ifndef TA_TESTUTIL_H
#define TA_TESTUTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ta_test_failures = 0;
static int ta_test_count = 0;

#define TA_CHECK(cond)                                                        \
    do {                                                                      \
        ta_test_count++;                                                      \
        if (!(cond)) {                                                        \
            ta_test_failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define TA_CHECK_STR(a, b)                                                    \
    do {                                                                      \
        ta_test_count++;                                                      \
        const char *_a = (a);                                                 \
        const char *_b = (b);                                                 \
        if (!_a || !_b || strcmp(_a, _b) != 0) {                              \
            ta_test_failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,       \
                    __LINE__, _a ? _a : "(null)", _b ? _b : "(null)");        \
        }                                                                     \
    } while (0)

#define TA_TEST_DONE(name)                                                    \
    do {                                                                      \
        if (ta_test_failures == 0) {                                          \
            printf("PASS %s (%d checks)\n", name, ta_test_count);             \
            return 0;                                                         \
        }                                                                     \
        fprintf(stderr, "FAIL %s: %d/%d checks failed\n", name,               \
                ta_test_failures, ta_test_count);                             \
        return 1;                                                             \
    } while (0)

#endif
