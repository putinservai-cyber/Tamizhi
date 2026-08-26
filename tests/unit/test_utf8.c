#include "ta_utf8.h"
#include "test_util.h"

int main(void) {
    size_t adv = 0;
    TA_CHECK(ta_utf8_decode("a", 1, &adv) == 'a' && adv == 1);

    const char *tamil = "\xE0\xAE\x95"; /* க */
    adv = 0;
    TA_CHECK(ta_utf8_decode(tamil, 3, &adv) == 0x0B95 && adv == 3);

    const char *broken = "\xE0\x0A";
    adv = 0;
    TA_CHECK(ta_utf8_decode(broken, 2, &adv) == TA_UTF8_INVALID);

    char out[4];
    TA_CHECK(ta_utf8_encode(0x0BB4, out) == 3); /* ழ */
    TA_CHECK((unsigned char)out[0] == 0xE0);
    TA_CHECK(ta_utf8_encode('Z', out) == 1 && out[0] == 'Z');

    TA_CHECK(ta_utf8_cp_count("\xE0\xAE\xA4\xE0\xAE\xAE", 6) == 2);
    TA_CHECK(ta_utf8_cp_count("abc", 3) == 3);

    TA_CHECK(ta_utf8_is_ident_start(0x0BAE));      /* ம */
    TA_CHECK(ta_utf8_is_ident_start('_'));
    TA_CHECK(!ta_utf8_is_ident_start('5'));
    TA_CHECK(!ta_utf8_is_ident_start(0x0BC1));     /* matra: cont only */
    TA_CHECK(ta_utf8_is_ident_cont(0x0BC1));
    TA_CHECK(ta_utf8_is_ident_cont('7'));

    {
        extern char *ta_sanitize_utf8(const char *);
        char *ok = ta_sanitize_utf8("\xE0\xAE\x95"); /* க */
        TA_CHECK_STR(ok, "\xE0\xAE\x95");
        free(ok);
        char *bad = ta_sanitize_utf8("a\xFF" "b");
        TA_CHECK_STR(bad, "a\xEF\xBF\xBD" "b");
        free(bad);
        char *cut = ta_sanitize_utf8("\xE0\xAE"); /* cut mid-codepoint */
        TA_CHECK_STR(cut, "\xEF\xBF\xBD");
        free(cut);
        char *ctl = ta_sanitize_utf8("a\x01" "b");
        TA_CHECK_STR(ctl, "a?b");
        free(ctl);
    }

    TA_TEST_DONE("utf8");
}
