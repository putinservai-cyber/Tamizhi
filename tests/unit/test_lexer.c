#include "ta_lexer.h"
#include "test_util.h"

static TaTokenList lex_of(const char *src) {
    TaDiagnostics *d = ta_diag_new();
    TaTokenList t = ta_lex_source("<test>", src, d, NULL);
    ta_diag_free(d);
    return t;
}

static int has(const TaTokenList *t, TaTokenType tt) {
    for (size_t i = 0; i < t->count; i++)
        if (t->items[i].type == tt) return 1;
    return 0;
}

int main(void) {
    TaDiagnostics *d = ta_diag_new();

    {
        TaTokenList t = lex_of("மாறி x = 10");
        TA_CHECK(has(&t, TK_VAR));
        TA_CHECK(has(&t, TK_INT));
        TA_CHECK(has(&t, TK_NEWLINE));
        TA_CHECK(has(&t, TK_EOF));
        TA_CHECK(!has(&t, TK_INDENT));
        ta_token_list_free(&t);
    }
    {
        TaTokenList t = lex_of("என்றால் x:\n    அச்சிடு(x)\n");
        int indents = 0, dedents = 0, news = 0;
        for (size_t i = 0; i < t.count; i++) {
            if (t.items[i].type == TK_INDENT) indents++;
            if (t.items[i].type == TK_DEDENT) dedents++;
            if (t.items[i].type == TK_NEWLINE) news++;
        }
        TA_CHECK(indents == 1);
        TA_CHECK(dedents == 1);
        TA_CHECK(news == 2);
        ta_token_list_free(&t);
    }
    {
        TaTokenList t = lex_of("மாறி a = 1\n\n\n# comment\nமாறி b = 2\n");
        int bad = 0;
        for (size_t i = 0; i < t.count; i++)
            if (t.items[i].type == TK_INDENT || t.items[i].type == TK_DEDENT) bad++;
        TA_CHECK(bad == 0); /* blank + comment lines produce no layout tokens */
        ta_token_list_free(&t);
    }
    {
        TaTokenList t = lex_of("f(\"\\n\\u{0BB5}\", 'ழ', 3.5e2, 0xFF)");
        int strs = 0, chars = 0, floats = 0;
        for (size_t i = 0; i < t.count; i++) {
            if (t.items[i].type == TK_STRING) {
                strs++;
                TA_CHECK(t.items[i].text_len == 4); /* \n + வ(3 bytes) */
            }
            if (t.items[i].type == TK_CHAR) chars++;
            if (t.items[i].type == TK_FLOAT) floats++;
        }
        TA_CHECK(strs == 1);
        TA_CHECK(chars == 1);
        TA_CHECK(floats == 1);
        ta_token_list_free(&t);
    }
    {
        TaTokenList t = ta_lex_source("<t>", "மாறி x = \"oops", d, NULL);
        TA_CHECK(ta_diag_has_errors(d));
        ta_token_list_free(&t);
    }

    ta_diag_free(d);
    TA_TEST_DONE("lexer");
}
