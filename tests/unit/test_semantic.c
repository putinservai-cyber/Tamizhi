#include "ta_lexer.h"
#include "ta_parser.h"
#include "ta_semantic.h"
#include "ta_typecheck.h"
#include "test_util.h"

typedef struct {
    bool ok;
    bool incomplete;
} FullResult;

static FullResult full_pipeline(const char *src, TaDiagnostics *diag) {
    FullResult r;
    r.ok = true;
    r.incomplete = false;
    TaLexResult lr;
    memset(&lr, 0, sizeof(lr));
    TaTokenList toks = ta_lex_source("<test>", src, diag, &lr);
    TaParseResult pr;
    memset(&pr, 0, sizeof(pr));
    TaProgram prog = ta_parse_tokens("<test>", &toks, diag, &pr);
    r.incomplete = pr.incomplete;
    if (!ta_diag_has_errors(diag)) {
        TaScope *g = NULL;
        int slots = 0;
        ta_semantic_run("<test>", &prog, diag, &g, &slots);
        if (!ta_diag_has_errors(diag)) ta_typecheck_run("<test>", &prog, g, diag);
        ta_scope_free(g);
    }
    if (ta_diag_has_errors(diag)) r.ok = false;
    ta_program_free(&prog);
    ta_token_list_free(&toks);
    return r;
}

int main(void) {
    {
        TaDiagnostics *d = ta_diag_new();
        FullResult r = full_pipeline(
            "செயலி fib(n: முழுஎண்) -> முழுஎண்:\n"
            "    என்றால் n <= 1:\n        திருப்பு n\n"
            "    திருப்பு fib(n-1) + fib(n-2)\n"
            "அச்சிடு(fib(10))\n",
            d);
        TA_CHECK(r.ok);
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        full_pipeline("அச்சிடு(undefined_name)", d);
        TA_CHECK(ta_diag_error_count(d) == 1);
        TA_CHECK(d->items[0]->code >= 3000 && d->items[0]->code < 4000);
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        full_pipeline("மாறி x: முழுஎண் = 1.5", d);
        TA_CHECK(ta_diag_error_count(d) >= 1);
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        FullResult r = full_pipeline(
            "நிலையான K = 5\nK = 6\n", d);
        TA_CHECK(!r.ok); /* assignment to constant */
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        FullResult r = full_pipeline(
            "வரை உண்மை:\n    நிறுத்து\nதொடர்\n", d);
        TA_CHECK(!r.ok); /* continue outside loop */
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        FullResult r = full_pipeline(
            "மாறி m = []\n", d);
        TA_CHECK(!r.ok); /* cannot infer empty list type */
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        FullResult r = full_pipeline(
            "மாறி m: [முழுஎண்] = []\nm.இல்லை\n", d);
        TA_CHECK(!r.ok);
        ta_diag_free(d);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        FullResult r = full_pipeline(
            "செயலி f():\n"
            "    திருப்பு g() + 1\n"
            "செயலி g() -> முழுஎண்:\n"
            "    திருப்பு 1\n"
            "அச்சிடு(f())\n",
            d);
        TA_CHECK(r.ok); /* inferred return type via call graph */
        ta_diag_free(d);
    }

    TA_TEST_DONE("semantic_typecheck");
}
