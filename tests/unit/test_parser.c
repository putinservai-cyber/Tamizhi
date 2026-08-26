#include "ta_lexer.h"
#include "ta_parser.h"
#include "ta_ast.h"
#include "test_util.h"

static TaProgram parse_of(const char *src) {
    TaDiagnostics *d = ta_diag_new();
    TaTokenList t = ta_lex_source("<test>", src, d, NULL);
    TaProgram p = ta_parse_tokens("<test>", &t, d, NULL);
    ta_token_list_free(&t);
    ta_diag_free(d);
    return p;
}

int main(void) {
    {
        TaProgram p = parse_of("மாறி x = 1 + 2 * 3");
        TA_CHECK(p.count == 1);
        TA_CHECK(p.items[0]->kind == ST_VARDECL);
        ta_program_free(&p);
    }
    {
        TaProgram p =
            parse_of("செயலி f(a: முழுஎண், b: உரை) -> முழுஎண்:\n"
                     "    திருப்பு a\n");
        TA_CHECK(p.count == 1 && p.items[0]->kind == ST_FUNCDEF);
        TA_CHECK(p.items[0]->as.funcdef->nparams == 2);
        TA_CHECK(p.items[0]->as.funcdef->ret_type != NULL);
        TA_CHECK(p.items[0]->as.funcdef->body->count == 1);
        ta_program_free(&p);
    }
    {
        TaProgram p =
            parse_of("என்றால் a > 1:\n    x()\nஇல்லையெனில் என்றால் a > 0:\n    y()\n"
                     "இல்லையெனில்:\n    z()\n");
        TA_CHECK(p.count == 1 && p.items[0]->kind == ST_IF);
        TA_CHECK(p.items[0]->as.ifstmt.nelifs == 1);
        TA_CHECK(p.items[0]->as.ifstmt.else_body != NULL);
        ta_program_free(&p);
    }
    {
        TaProgram p = parse_of("ஒவ்வொன்றும் i இல் வரம்பு(10):\n    நிறுத்து\n");
        TA_CHECK(p.count == 1 && p.items[0]->kind == ST_FOREACH);
        ta_program_free(&p);
    }
    {
        TaProgram p =
            parse_of("மாறி l = [1, 2, 3]\nமாறி d = {\"a\": 1}\nl[0] = 9\nd[\"a\"] += 2\n");
        TA_CHECK(p.count == 4);
        TA_CHECK(p.items[2]->kind == ST_ASSIGN);
        ta_program_free(&p);
    }
    {
        TaDiagnostics *d = ta_diag_new();
        TaLexResult lr;
        memset(&lr, 0, sizeof(lr));
        TaTokenList t = ta_lex_source("<t>", "என்றால் x:", d, &lr);
        TaParseResult pr;
        memset(&pr, 0, sizeof(pr));
        TaProgram p = ta_parse_tokens("<t>", &t, d, &pr);
        TA_CHECK(pr.incomplete);
        ta_program_free(&p);
        ta_token_list_free(&t);
        ta_diag_free(d);
    }

    TA_TEST_DONE("parser");
}
