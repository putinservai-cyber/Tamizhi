#include "ta_ast.h"
#include "ta_codegen.h"
#include "ta_common.h"
#include "ta_ir.h"
#include "ta_lexer.h"
#include "ta_parser.h"
#include "ta_semantic.h"
#include "ta_typecheck.h"

#include <langinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#include <strings.h>
#include <ctype.h>
#include <locale.h>

#define TA_MAIN_NAME "முதன்மை"

typedef struct {
    char *asm_path;
    char *exe_path;
} BuildOutput;

static int run_pipeline(const char *path, const char *src, TaStrBuf *asm_out,
                        bool *incomplete, bool echo_top_exprs, bool emit_c) {
    TaDiagnostics *diag = ta_diag_new();
    TaLexResult lr;
    memset(&lr, 0, sizeof(lr));
    TaTokenList toks = ta_lex_source(path, src, diag, &lr);
    if (incomplete) *incomplete = false;

    if (!ta_diag_has_errors(diag)) {
        TaParseResult pr;
        memset(&pr, 0, sizeof(pr));
        TaProgram prog = ta_parse_tokens(path, &toks, diag, &pr);
        if (incomplete) *incomplete = pr.incomplete;

        if (!ta_diag_has_errors(diag)) {
            TaScope *globals = NULL;
            int top_slots = 0;
            ta_semantic_run(path, &prog, diag, &globals, &top_slots);
            if (!ta_diag_has_errors(diag)) {
                ta_typecheck_run(path, &prog, globals, diag);
                if (!ta_diag_has_errors(diag)) {
                    TaIrUnit *unit =
                        ta_ir_generate(path, &prog, globals, top_slots, diag,
                                       echo_top_exprs);
                    if (unit) {
                        if (emit_c)
                            ta_ir_emit_c(unit, asm_out);
                        else
                            ta_codegen_emit(unit, asm_out);
                        ta_ir_unit_free(unit);
                    }
                }
            }
            ta_scope_free(globals);
        }
        ta_program_free(&prog);
    }
    ta_token_list_free(&toks);

    int rc = ta_diag_has_errors(diag) ? 1 : 0;
    if (rc != 0) ta_diag_print_all(diag, stderr, src);
    ta_diag_free(diag);
    return rc;
}

static char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return ta_xstrdup(".");
    return ta_xstrndup(path, (size_t)(slash - path));
}

static char *find_file_relative(const char *argv0, const char *rel) {
    char *dir = dir_of(argv0);
    size_t n = strlen(dir) + strlen(rel) + 2;
    char *p = ta_xmalloc(n);
    snprintf(p, n, "%s/%s", dir, rel);
    free(dir);
    return p;
}

static char *first_existing(const char *const *candidates, size_t n) {
    for (size_t i = 0; i < n; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            return ta_xstrdup(candidates[i]);
        }
    }
    return NULL;
}

static char *find_runtime_obj(const char *argv0) {
    char *a = find_file_relative(argv0, "tart.o");
    char *b = find_file_relative(argv0, "../lib/tamizhi/tart.o");
    char *c = find_file_relative(argv0, "../build/tart.o");
    const char *env = getenv("TA_RT_OBJ");
    const char *cands[5];
    size_t n = 0;
    if (env) cands[n++] = env;
    cands[n++] = a;
    cands[n++] = b;
    cands[n++] = c;
    cands[n++] = "build/tart.o";
    char *r = first_existing(cands, n);
    free(a);
    free(b);
    free(c);
    return r;
}

static char *find_runtime_c(const char *argv0) {
    char *a = find_file_relative(argv0, "tart.c");
    char *b = find_file_relative(argv0, "../lib/tamizhi/tart.c");
    char *c = find_file_relative(argv0, "../src/runtime/tart.c");
    const char *env = getenv("TA_RT_C");
    const char *cands[5];
    size_t n = 0;
    if (env) cands[n++] = env;
    cands[n++] = a;
    cands[n++] = b;
    cands[n++] = c;
    cands[n++] = "src/runtime/tart.c";
    char *r = first_existing(cands, n);
    free(a);
    free(b);
    free(c);
    return r;
}

/* Locate the runtime include directory (tart.h). */
static char *find_include_dir(const char *argv0) {
    char *a = find_file_relative(argv0, "../include");
    char *b = find_file_relative(argv0, "../../include");
    const char *env = getenv("TA_INCLUDE");
    const char *cands[5];
    size_t n = 0;
    if (env) cands[n++] = env;
    cands[n++] = a;
    cands[n++] = b;
    cands[n++] = "include";
    char *r = first_existing(cands, n);
    free(a);
    free(b);
    return r;
}

/* Compile generated C together with the portable runtime C source. */
static int compile_c(const char *c_path, const char *rt_c_path,
                    const char *exe_path, const char *inc_dir, char **err_hint) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        const char *idir = inc_dir ? inc_dir : "include";
        execlp("cc", "cc", "-I", idir, c_path, rt_c_path, "-lm", "-o",
               exe_path, (char *)NULL);
        execlp("gcc", "gcc", "-I", idir, c_path, rt_c_path, "-lm", "-o",
               exe_path, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) {
        if (WEXITSTATUS(st) == 0) return 0;
        if (WEXITSTATUS(st) == 127 && err_hint)
            *err_hint = ta_xstrdup("'cc' கிடைக்கவில்லை; C toolchain நிறுவப்பட்டுள்ளதா?");
        return WEXITSTATUS(st);
    }
    return -1;
}

static int link_exe(const char *asm_path, const char *obj_path, const char *exe_path,
                    char **err_hint) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execlp("cc", "cc", asm_path, obj_path, "-lm", "-o", exe_path, (char *)NULL);
        execlp("gcc", "gcc", asm_path, obj_path, "-lm", "-o", exe_path, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) {
        if (WEXITSTATUS(st) == 0) return 0;
        if (WEXITSTATUS(st) == 127 && err_hint)
            *err_hint = ta_xstrdup("'cc' கிடைக்கவில்லை; C toolchain நிறுவப்பட்டுள்ளதா?");
        return WEXITSTATUS(st);
    }
    return -1;
}

static char *default_output_name(const char *src_path) {
    static const char *exts[] = {".ta", ".த"};
    const char *base = strrchr(src_path, '/');
    base = base ? base + 1 : src_path;
    size_t n = strlen(base);
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t el = strlen(exts[i]);
        if (n > el && strcmp(base + n - el, exts[i]) == 0) {
            n -= el;
            break;
        }
    }
    char *out = ta_xmalloc(n + 1);
    memcpy(out, base, n);
    out[n] = 0;
    return out;
}

static void usage(FILE *out, const char *prog);

static int cmd_build_to(int argc, char **argv, const char *out_exe,
                        bool show_banner) {
    const char *argv0 = argv[0];
    const char *src_path = NULL;
    bool c_target = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            c_target = (strcmp(argv[++i], "c") == 0);
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            c_target = (strcmp(argv[i] + 9, "c") == 0);
        } else if (!src_path) {
            src_path = argv[i];
        }
    }
    if (!src_path) {
        usage(stderr, argv0);
        return 2;
    }

    int errc = 0;
    size_t len = 0;
    char *src = ta_read_file(src_path, &len, &errc);
    if (!src) {
        fprintf(stderr, "பிழை TA6001: கோப்பைப் படிக்க முடியவில்லை: %s\n", src_path);
        return 2;
    }
    TaStrBuf sb;
    ta_sb_init(&sb);
    int rc = run_pipeline(src_path, src, &sb, NULL, false, c_target);
    free(src);
    if (rc != 0) {
        ta_sb_free(&sb);
        return rc;
    }

    if (c_target) {
        size_t need = strlen(out_exe) + 3;
        char *cpath = ta_xmalloc(need);
        snprintf(cpath, need, "%s.c", out_exe);
        if (!ta_write_file(cpath, sb.data, sb.len)) {
            fprintf(stderr, "பிழை TA6002: C மூலம் எழுத முடியவில்லை: %s\n", cpath);
            ta_sb_free(&sb);
            free(cpath);
            return 1;
        }
        ta_sb_free(&sb);
        char *rtc = find_runtime_c(argv0);
        if (!rtc) {
            fprintf(stderr,
                    "பிழை TA6004: runtime C (tart.c) கிடைக்கவில்லை; "
                    "TA_RT_C env-இல் பாதையைக் கொடுங்கள்\n");
            free(cpath);
            return 1;
        }
        char *inc = find_include_dir(argv0);
        char *hint = NULL;
        int crc = compile_c(cpath, rtc, out_exe, inc, &hint);
        if (crc != 0) {
            fprintf(stderr, "பிழை TA6003: C உருவாக்க இயலவில்லை (%s)\n",
                    hint ? hint : "'cc' இல்லை");
        } else if (show_banner) {
            printf("✓ %s (C இலக்கு / C target)\n", out_exe);
        }
        free(hint);
        free(inc);
        free(rtc);
        free(cpath);
        return crc == 0 ? 0 : 1;
    }

    size_t need = strlen(out_exe) + 4;
    char *asm_path = ta_xmalloc(need);
    snprintf(asm_path, need, "%s.s", out_exe);
    if (!ta_write_file(asm_path, sb.data, sb.len)) {
        fprintf(stderr, "பிழை TA6002: assembly எழுத முடியவில்லை: %s\n", asm_path);
        ta_sb_free(&sb);
        free(asm_path);
        return 1;
    }
    ta_sb_free(&sb);

    char *obj = find_runtime_obj(argv0);
    if (!obj) {
        fprintf(stderr,
                "பிழை TA6004: runtime object (tart.o) கிடைக்கவில்லை; "
                "TA_RT_OBJ env-இல் பாதையைக் கொடுங்கள்\n");
        free(asm_path);
        return 1;
    }
    char *hint = NULL;
    int lrc = link_exe(asm_path, obj, out_exe, &hint);
    if (lrc != 0) {
        fprintf(stderr, "பிழை TA6003: link செய்ய முடியவில்லை (%s)\n",
                hint ? hint : "'cc' இல்லை");
        unlink(asm_path);
    } else if (show_banner) {
        printf("✓ %s (அதன் கூற்று: %s)\n", out_exe, asm_path);
    }
    free(hint);
    free(obj);
    free(asm_path);
    return lrc == 0 ? 0 : 1;
}

static int cmd_build(int argc, char **argv) {
    const char *argv0 = argv[0];
    const char *out = NULL;
    const char *src_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) out = argv[++i];
        } else if (strncmp(argv[i], "--target", 8) == 0) {
            if (argv[i][8] == '=') { /* --target=c */ }
            else if (i + 1 < argc) i++;
        } else if (!src_path) {
            src_path = argv[i];
        }
    }
    char *def = NULL;
    if (!out) {
        def = default_output_name(src_path ? src_path : "a.ta");
        out = def;
    }
    int rc = cmd_build_to(argc, argv, out, true);
    free(def);
    (void)argv0;
    return rc;
}

static void print_runtime_error_note(void) {}

static int cmd_run(int argc, char **argv) {
    char tmpl[] = "/tmp/tamizhi-run-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        fprintf(stderr, "பிழை TA6002: தற்காலிக அடைவு உருவாக்க முடியவில்லை\n");
        return 1;
    }
    char exe[512];
    snprintf(exe, sizeof(exe), "%s/program", dir);
    int rc = cmd_build_to(argc, argv, exe, false);
    if (rc == 0) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            execv(exe, (char *[]){exe, NULL});
            _exit(127);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFEXITED(st))
            rc = WEXITSTATUS(st);
        else if (WIFSIGNALED(st))
            rc = 128 + WTERMSIG(st);
    }
    char asmpath[600];
    snprintf(asmpath, sizeof(asmpath), "%s.s", exe);
    char cpath[600];
    snprintf(cpath, sizeof(cpath), "%s.c", exe);
    unlink(exe);
    unlink(asmpath);
    unlink(cpath);
    rmdir(dir);
    return rc;
}

static int cmd_check(const char *src_path) {
    int errc = 0;
    char *src = ta_read_file(src_path, NULL, &errc);
    if (!src) {
        fprintf(stderr, "பிழை TA6001: கோப்பைப் படிக்க முடியவில்லை: %s\n", src_path);
        return 2;
    }
    TaStrBuf asm_sb;
    ta_sb_init(&asm_sb);
    int rc = run_pipeline(src_path, src, &asm_sb, NULL, false, false);
    ta_sb_free(&asm_sb);
    free(src);
    if (rc == 0) printf("✓ %s — பிழைகள் இல்லை (no errors)\n", src_path);
    return rc;
}

static int cmd_fmt(const char *src_path) {
    int errc = 0;
    char *src = ta_read_file(src_path, NULL, &errc);
    if (!src) {
        fprintf(stderr, "பிழை TA6001: கோப்பைப் படிக்க முடியவில்லை: %s\n", src_path);
        return 2;
    }
    TaDiagnostics *diag = ta_diag_new();
    TaTokenList toks = ta_lex_source(src_path, src, diag, NULL);
    TaProgram prog = ta_parse_tokens(src_path, &toks, diag, NULL);
    if (ta_diag_has_errors(diag)) {
        ta_diag_print_all(diag, stderr, src);
        ta_program_free(&prog);
        ta_token_list_free(&toks);
        ta_diag_free(diag);
        free(src);
        return 1;
    }
    char *formatted = ta_format_source(&prog);
    fputs(formatted, stdout);
    free(formatted);
    ta_program_free(&prog);
    ta_token_list_free(&toks);
    ta_diag_free(diag);
    free(src);
    return 0;
}

static void repl_help(void) {
    printf("REPL கட்டளைகள்:\n"
           "  :உதவி    — இந்த விளக்கம் (:help)\n"
           "  :காட்டு   — இதுவரை எழுதிய நிரலைக் காட்டு (:show)\n"
           "  :அழி     — நிரலை அழி (:clear)\n"
           "  :வெளியேறு — வெளியேறு (:quit, Ctrl-D)\n"
           "\nஒவ்வொரு உள்ளீட்டிலும் முழு நிரல் compile செய்யப்பட்டு "
           "தொடக்கத்திலிருந்து இயக்கப்படும்.\n");
}

static int cmd_repl(const char *argv0) {
    printf("தமிழி v%s — :உதவி ஐப் பாருங்கள்; வெளியேற: :வெளியேறு\n", TA_VERSION);
    TaStrBuf buf;
    ta_sb_init(&buf);
    char line[4096];

    for (;;) {
        printf("ta» ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t ll = strlen(line);
        while (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = 0;

        if (strcmp(line, ":வெளியேறு") == 0 || strcmp(line, ":quit") == 0 ||
            strcmp(line, ":q") == 0) break;
        if (strcmp(line, ":உதவி") == 0 || strcmp(line, ":help") == 0) { repl_help(); continue; }
        if (strcmp(line, ":காட்டு") == 0 || strcmp(line, ":show") == 0) {
            printf("--- நிரல் ---\n%s--------------\n", buf.len ? buf.data : "(காலியாக உள்ளது)");
            continue;
        }
        if (strcmp(line, ":அழி") == 0 || strcmp(line, ":clear") == 0) {
            ta_sb_free(&buf);
            ta_sb_init(&buf);
            printf("(அழிக்கப்பட்டது)\n");
            continue;
        }
        if (ll == 0) continue;

        ta_sb_puts(&buf, line);
        ta_sb_putc(&buf, '\n');

        bool incomplete = false;
        TaStrBuf asm_sb;
        ta_sb_init(&asm_sb);
        char *snapshot = ta_xstrdup(buf.data);
        int rc = run_pipeline("<repl>", snapshot, &asm_sb, &incomplete, true, false);
        if (rc != 0 && incomplete) {
            free(snapshot);
            ta_sb_free(&asm_sb);
            continue;
        }
        if (rc != 0) {
            free(snapshot);
            ta_sb_free(&asm_sb);
            continue;
        }

        char tmpl[] = "/tmp/tamizhi-repl-XXXXXX";
        char *dir = mkdtemp(tmpl);
        if (!dir) {
            fprintf(stderr, "தற்காலிக அடைவு உருவாகவில்லை\n");
            free(snapshot);
            ta_sb_free(&asm_sb);
            continue;
        }
        char exe[512], asmp[600];
        snprintf(exe, sizeof(exe), "%s/prog", dir);
        snprintf(asmp, sizeof(asmp), "%s.s", exe);
        ta_write_file(asmp, asm_sb.data, asm_sb.len);
        char *obj = find_runtime_obj(argv0);
        if (!obj) {
            fprintf(stderr, "TA6004: tart.o கிடைக்கவில்லை\n");
            free(snapshot);
            ta_sb_free(&asm_sb);
            unlink(asmp);
            rmdir(dir);
            continue;
        }
        char *hint = NULL;
        if (link_exe(asmp, obj, exe, &hint) != 0) {
            fprintf(stderr, "இணைப்பு தோல்வி\n");
        } else {
            fflush(stdout);
            fflush(stderr);
            pid_t pid = fork();
            if (pid == 0) {
                execv(exe, (char *[]){exe, NULL});
                _exit(127);
            }
            int st = 0;
            waitpid(pid, &st, 0);
            fflush(stdout);
            if (WIFEXITED(st) && WEXITSTATUS(st) != 0)
                printf("[exit %d]\n", WEXITSTATUS(st));
        }
        free(hint);
        free(obj);
        free(snapshot);
        ta_sb_free(&asm_sb);
        unlink(exe);
        unlink(asmp);
        rmdir(dir);
    }
    ta_sb_free(&buf);
    printf("\nவணக்கம்!\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* setup-konsole: auto-configure Konsole font for Tamil rendering      */
/* ------------------------------------------------------------------ */
static int cmd_setup_konsole(void) {
    printf("தமிழ௱ — Konsole தமிழ் எழுத்துரு அமைப்பு\n");
    printf("════════════════════════════\n\n");

    /* 1. Must be running inside Konsole */
    const char *kver = getenv("KONSOLE_VERSION");
    const char *kdbus = getenv("KONSOLE_DBUS_SESSION");
    if (!kver && !kdbus) {
        fprintf(stderr,
            "பிழை: இது KDE Konsole-க்குள் இயங்கவில்லை.\n"
            "  Konsole-ஐத் திறந்து மீண்டும் இயக்கவும்:\n"
            "    ta setup-konsole\n");
        return 1;
    }
    printf("[1/4] Konsole கண்டறியப்பட்டது (பதிப்பு %s)\n", kver ? kver : "unknown");

    /* 2. Find best available Tamil font */
    static const char *preferred[] = {
        "Noto Sans Tamil", "Noto Sans Tamil UI",
        "Lohit Tamil",     "Droid Sans Tamil", NULL
    };
    char best[128] = "";
    FILE *fc = popen("fc-list :lang=ta family 2>/dev/null", "r");
    if (fc) {
        char lb[256];
        while (fgets(lb, sizeof(lb), fc)) {
            size_t l = strlen(lb);
            while (l && (lb[l-1]=='\n'||lb[l-1]=='\r'||lb[l-1]==' ')) lb[--l]=0;
            /* fc-list may return "Family1,Family2" — take first token */
            char *comma = strchr(lb, ',');
            if (comma) *comma = 0;
            for (int i = 0; preferred[i]; i++) {
                if (strcasecmp(lb, preferred[i]) == 0 && best[0] == 0)
                    snprintf(best, sizeof(best), "%s", lb);
            }
        }
        pclose(fc);
    }
    if (best[0] == 0) {
        fprintf(stderr,
            "பிழை: இந்த அமைப்புறையில் தமிழ் எழுத்துரு எதுவும் இல்லை.\n"
            "  முதலில் நிறுவவும்:\n"
            "    Debian/Ubuntu : sudo apt install fonts-noto-core\n"
            "    Fedora        : sudo dnf install google-noto-sans-tamil-fonts\n"
            "    Arch          : sudo pacman -S noto-fonts\n");
        return 1;
    }
    printf("[2/4] சிறந்த தமிழ் எழுத்துரு: %s\n", best);

    /* 3. Apply to current session via konsoleprofile */
    {
        char cmd[512];
        /* Konsole font string format: family,size,-1,5,50,0,0,0,0,0 */
        snprintf(cmd, sizeof(cmd),
            "konsoleprofile 'font=%s,12,-1,5,50,0,0,0,0,0' 2>/dev/null", best);
        int rc = system(cmd);
        if (rc == 0)
            printf("[3/4] தற்போதைய Konsole அமர்வுக்கு எழுத்துரு பயன்படுத்தப்பட்டது  ✓\n");
        else
            printf("[3/4] konsoleprofile இயலவில்லை (rc=%d) — நிரந்தரமாக மட்டும் அமைக்கப்படுகிறது\n", rc);
    }

    /* 4. Make permanent: patch the default Konsole profile file */
    int patched = 0;
    const char *home = getenv("HOME");
    if (home) {
        /* Find the default profile name from konsolerc */
        char konsolerc[512];
        snprintf(konsolerc, sizeof(konsolerc), "%s/.config/konsolerc", home);
        char defprofile[256] = "";
        FILE *rc2 = fopen(konsolerc, "r");
        if (rc2) {
            char line[512];
            bool in_desktop = false;
            while (fgets(line, sizeof(line), rc2)) {
                if (strncmp(line, "[Desktop Entry]", 15) == 0) { in_desktop = true; continue; }
                if (line[0] == '[') { in_desktop = false; continue; }
                if (in_desktop && strncmp(line, "DefaultProfile=", 15) == 0) {
                    strncpy(defprofile, line + 15, sizeof(defprofile) - 1);
                    defprofile[sizeof(defprofile) - 1] = 0;
                    size_t l = strlen(defprofile);
                    while (l && (defprofile[l-1]=='\n'||defprofile[l-1]=='\r')) defprofile[--l]=0;
                    break;
                }
            }
            fclose(rc2);
        }

        if (defprofile[0]) {
            char profpath[768];
            snprintf(profpath, sizeof(profpath),
                "%s/.local/share/konsole/%s", home, defprofile);
            /* Read profile, replace or append Font= line */
            FILE *pf = fopen(profpath, "r");
            if (pf) {
                char tmp[800];
                snprintf(tmp, sizeof(tmp), "%s.tamizhi.tmp", profpath);
                FILE *out = fopen(tmp, "w");
                if (out) {
                    char line[512];
                    bool in_appearance = false;
                    bool wrote_font = false;
                    while (fgets(line, sizeof(line), pf)) {
                        if (strncmp(line, "[Appearance]", 12) == 0) {
                            in_appearance = true;
                            fputs(line, out);
                            continue;
                        }
                        if (line[0] == '[') in_appearance = false;
                        if (in_appearance && strncmp(line, "Font=", 5) == 0) {
                            fprintf(out, "Font=%s,12,-1,5,50,0,0,0,0,0\n", best);
                            wrote_font = true;
                            continue;
                        }
                        fputs(line, out);
                    }
                    if (in_appearance && !wrote_font)
                        fprintf(out, "Font=%s,12,-1,5,50,0,0,0,0,0\n", best);
                    fclose(out);
                    fclose(pf);
                    rename(tmp, profpath);
                    patched = 1;
                    printf("[4/4] '%s' சுயவிவரம் நிரந்தரமாகப் புதுப்பிக்கப்பட்டது  ✓\n", defprofile);
                } else {
                    fclose(pf);
                }
            }
        }
    }
    if (!patched)
        printf("[4/4] சுயவிவரத்தைத் தானாக திருத்த முடியவில்லை.\n"
               "      கைமுறை: Settings ▸ Edit Profile ▸ Appearance ▸ Font ▸ '%s'\n", best);

    printf("\n--- முக்கிய குறிப்பு ---\n");
    printf("சரியான எழுத்துரு இருந்தாலும், Konsole-ன் எழுத்து உருவமைப்பு தமிழ்\n");
    printf("எழுத்து+குறில்களை முழுமையாக இணைக்காது.\n");
    printf("முழு தன்மைக்கு kitty அல்லது WezTerm பயன்படுத்துங்கள்:\n");
    printf("  kitty  : https://sw.kovidgoyal.net/kitty/\n");
    printf("  WezTerm: https://wezfurlong.org/wezterm/\n");
                return 0;
}

/* ------------------------------------------------------------------ */
/* check-fonts: report available Tamil fonts                           */
/* ------------------------------------------------------------------ */
static int cmd_check_fonts(void) {
    printf("தமிழ் எழுத்துருக்கள் பரிசோதனை\n");
    printf("══════════════════════════════════\n");

    /* Recommended families we look for (substring match, case-insensitive). */
    const char *want[] = {
        "noto", "lohit", "tamil", "catamaran", "bamini", "suruma", "dv", "freefont"
    };
    size_t nwant = sizeof(want) / sizeof(want[0]);

    FILE *f = popen("fc-list :lang=ta family 2>/dev/null | sort -u", "r");
    char line[256];
    int total = 0;
    bool have_want[nwant];
    for (size_t i = 0; i < nwant; i++) have_want[i] = false;

    if (!f) {
        fprintf(stderr, "fc-list இயக்க முடியவில்லை (fontconfig நிறுவப்பட்டிருக்க வேண்டும்)\n");
        return 1;
    }
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l) continue;
        if (strchr(line, ',')) continue; /* skip multi-family entries */
        total++;
        if (total <= 20)
            printf("  • %s\n", line);
        char low[256];
        size_t k = 0;
        for (k = 0; k < l && k < sizeof(low) - 1; k++)
            low[k] = (line[k] >= 'A' && line[k] <= 'Z') ? line[k] + 32 : line[k];
        low[k] = 0;
        for (size_t i = 0; i < nwant; i++)
            if (strstr(low, want[i])) have_want[i] = true;
    }
    pclose(f);

    printf("\nமொத்தம்: %d தமிழ் எழுத்துருக்கள் கிடைத்தன.\n", total);
    if (total == 0) {
        printf("✗ எந்த தமிழ் எழுத்துருவும் இல்லை.\n");
        printf("  -> ta install-fonts  (அல்லது கைமுறையாக நிறுவவும்)\n");
        return 1;
    }

    printf("பரிந்துரைக்கப்பட்ட குடும்பங்கள்:\n");
    for (size_t i = 0; i < nwant; i++)
        printf("  %s %s\n", have_want[i] ? "✓" : "·", want[i]);
    printf("\nமுடிவு: %s\n", total ? "✓ எழுத்துருக்கள் தயார்" : "✗ எழுத்துரு தேவை");
    return 0;
}

/* Locate a bundled helper script (tools/<name>) relative to the exe. */
static char *find_helper(const char *name) {
    static char path[2048];
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(path, sizeof(path), "%s/../tools/%s", exe, name);
            if (access(path, X_OK) == 0) return path;
            snprintf(path, sizeof(path), "%s/../%s", exe, name);
            if (access(path, X_OK) == 0) return path;
        }
    }
    const char *env = getenv("TA_TOOLS");
    if (env) {
        snprintf(path, sizeof(path), "%s/%s", env, name);
        if (access(path, X_OK) == 0) return path;
    }
    snprintf(path, sizeof(path), "tools/%s", name);
    return path;
}

/* install-fonts: run the bundled Tamil font installer                  */
static int cmd_install_fonts(void) {
    const char *script = find_helper("install-tamil-fonts.sh");
    printf("நிறுவி உருவாக்கம்: %s\n", script);
    char cmd[2176];
    snprintf(cmd, sizeof(cmd), "\"%s\"", script);
    int rc = system(cmd);
    if (rc != 0)
        fprintf(stderr, "எழுத்துரு நிறுவல் தோல்வி (நிலை %d). நிர்வாகி உரிமை தேவையாக இருக்கலாம்.\n", rc);
    return rc ? 1 : 0;
}

/* doctor: environment check                                           */
/* ------------------------------------------------------------------ */
static int cmd_doctor(void) {
    int problems = 0;
    printf("Tamizhi சூழல் பரிசோதனை\n");
    printf("════════════════════════════\n");

    const char *lc_all = getenv("LC_ALL");
    const char *lc_ctype = getenv("LC_CTYPE");
    const char *lang = getenv("LANG");
    const char *eff = lc_all ? lc_all : (lc_ctype ? lc_ctype : lang);
    const char *codeset = nl_langinfo(CODESET);
    bool utf8 = codeset && (strcasecmp(codeset, "UTF-8") == 0 ||
                            strcasecmp(codeset, "UTF8") == 0);
    printf("1) locale      : %s (codeset %s)  %s\n",
           eff ? eff : "(unset)", codeset ? codeset : "?",
           utf8 ? "✓" : "✗ UTF-8 அல்ல");
    if (!utf8) {
        problems++;
        printf("   -> தீர்வு: export LANG=ta_IN.UTF-8\n");
    }

    bool tty = isatty(1);
    printf("2) stdout      : %s\n", tty ? "முனையம் ✓" : "pipe/கோப்பு ✓");

    /* Detect terminal type */
    const char *kver = getenv("KONSOLE_VERSION");
    const char *term = getenv("TERM");
    const char *termapp = getenv("TERM_PROGRAM");
    if (kver) {
        printf("3) terminal    : KDE Konsole %s  "
               "[எச்சரிக்கை: Tamil இணைப்பு வரம்பு குறைவு]\n", kver);
        printf("   -> தீர்வு: ta setup-konsole   (எழுத்துரு தானாக அமையும்)\n");
                printf("   -> சிறந்தது: kitty / WezTerm பயன்படுத்துங்கள்\n");
    } else {
        printf("3) terminal    : %s%s%s\n",
               termapp ? termapp : (term ? term : "unknown"),
               termapp ? "" : "", "");
    }

    /* Tamil fonts */
    char fonts[5][64];
    int nfonts = 0;
    FILE *f = popen("fc-list :lang=ta family 2>/dev/null | sort -u", "r");
    if (f) {
        char lb[128];
        while (fgets(lb, sizeof(lb), f) && nfonts < 5) {
            size_t l = strlen(lb);
            while (l && (lb[l-1]=='\n'||lb[l-1]=='\r')) lb[--l] = 0;
            if (!l || strchr(lb, ',')) continue;
            snprintf(fonts[nfonts], sizeof(fonts[nfonts]), "%s", lb);
            nfonts++;
        }
        pclose(f);
        if (nfonts == 0) f = NULL;
        else {
            printf("4) தமிழ் எழுத்துரு : %d கண்டறியப்பட்டன — ", nfonts);
            for (int i = 0; i < nfonts && i < 3; i++)
                printf("%s%s", fonts[i], i < nfonts-1 && i < 2 ? ", " : "");
            printf("\n");
        }
    }
    if (!f || nfonts == 0) {
        problems++;
        printf("4) தமிழ் எழுத்துரு : ✗ எதுவும் இல்லை\n");
        printf("   -> Fedora       : sudo dnf install google-noto-sans-tamil-fonts\n");
        printf("   -> Debian/Ubuntu: sudo apt install fonts-noto-core\n");
        printf("   -> Arch         : sudo pacman -S noto-fonts\n");
    }

    /* Cluster rendering test */
    printf("5) தமிழ் எழுத்து இணைப்பு சோதனை:\n");
    printf("     குறிப்பு : [க] [கா] [கி] [கீ] [கு] [கூ] [கெ] [கே] [கை] [கொ] [கோ] [க்]\n");
    printf("     முனையம்  :  க    கா   கி   கீ   கு   கூ   கெ   கே   கை   கொ   கோ   க்\n");
    printf("   மேலே ஒவ்வொரு [...] இணையிலும் ஒரே ஒரு இணைந்த எழுத்தே தெரிய வேண்டும்.\n");
    printf("   எழுத்துக்கள் பிரிந்து/முற்றாகத் தெரிந்தால் -> முனைய வரம்பு தோல்வி.\n");
    if (kver)
        printf("   Konsole தீர்வு: ta setup-konsole\n");

    printf("\nமுடிவு: %s (%d பிழை)\n",
           problems ? "✗ சரிசெய்ய வேண்டும்" : "✓ எல்லாம் தயார்", problems);
    return problems ? 1 : 0;
}

static bool ensure_utf8_locale(void) {
    const char *lc_all = getenv("LC_ALL");
    const char *lc_ctype = getenv("LC_CTYPE");
    const char *lang = getenv("LANG");

    if (!lc_all && !lc_ctype && !lang) setlocale(LC_ALL, "C.UTF-8");

    const char *codeset = nl_langinfo(CODESET);
    if (codeset && (strcasecmp(codeset, "UTF-8") == 0 ||
                    strcasecmp(codeset, "UTF8") == 0))
        return true;

    static const char *fallbacks[] = {"en_US.UTF-8", "en_GB.UTF-8", "C.UTF-8",
                                      "ta_IN.UTF-8", "ta_LK.UTF-8", NULL};
    for (int i = 0; fallbacks[i]; i++) {
        if (setlocale(LC_ALL, fallbacks[i])) {
            codeset = nl_langinfo(CODESET);
            if (codeset && strcasecmp(codeset, "UTF-8") == 0) return true;
        }
    }
    return false;
}

static void usage(FILE *out, const char *prog) {
    fprintf(out,
            "Tamizhi v%s — தமிழ் நிரலாக்க மொழி\n\n",
            TA_VERSION);
    fprintf(out, "%s <கட்டளை> [வாதகங்கள்]\n\n", prog);
    fputs(
        "கோப்பு நீட்டிப்பு: .ta அல்லது .த\n"
        "\n"
        "இரு இயக்க முறைகள்: build → C போல native binary; run/repl → Python போல script\n"
        "\n"
        "கட்டளைகள்:\n"
        "  build <file.ta> [-o output]   native executable உருவாக்கு\n"
        "  run <file.ta>                 உடனடக்க இயக்கு\n"
        "  check <file.ta>               வகைப்பார்வை மட்டும்\n"
        "  repl                          இடைச்சேர்க்க உரையாடல்\n"
        "  fmt <file.ta>                 நிரலை வடிவமைத்துக் காட்டு\n"
        "  doctor                        எழுத்துரு/சூழல் பரிசோதனை\n"
        "  check-fonts                   நிறுவப்பட்ட தமிழ் எழுத்துருக்கள் பட்டியல்\n"
        "  install-fonts                 Tamil எழுத்துருக்களை நிறுவு\n"
        "  setup-konsole                 Konsole-இல் தமிழ் எழுத்துரு அமை\n"
        "  version                       பதிப்பு\n"
        "  help                          உதவி\n", out);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setlocale(LC_ALL, "");
    if (!ensure_utf8_locale()) {
        fprintf(stderr,
                "எச்சனமை: UTF-8 locale கண்டற்படவில்லை.\n"
                "  தீர்வு: export LANG=ta_IN.UTF-8   (அல்லது en_US.UTF-8)\n\n");
    }
    const char *prog = argc > 0 ? argv[0] : "ta";
    if (argc < 2) {
        usage(stderr, prog);
        return 2;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 ||
        strcmp(cmd, "-v") == 0) {
        printf("Tamizhi %s\n", TA_VERSION);
        return 0;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(stdout, prog);
        return 0;
    }
    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) {
            usage(stderr, prog);
            return 2;
        }
        return cmd_build(argc, argv);
    }
    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) {
            usage(stderr, prog);
            return 2;
        }
        print_runtime_error_note();
        return cmd_run(argc, argv);
    }
    if (strcmp(cmd, "check") == 0) {
        if (argc < 3) {
            usage(stderr, prog);
            return 2;
        }
        return cmd_check(argv[2]);
    }
    if (strcmp(cmd, "fmt") == 0 || strcmp(cmd, "format") == 0) {
        if (argc < 3) {
            usage(stderr, prog);
            return 2;
        }
        return cmd_fmt(argv[2]);
    }
    if (strcmp(cmd, "doctor") == 0) return cmd_doctor();
    if (strcmp(cmd, "check-fonts") == 0) return cmd_check_fonts();
    if (strcmp(cmd, "install-fonts") == 0) return cmd_install_fonts();
    if (strcmp(cmd, "setup-konsole") == 0) return cmd_setup_konsole();
    if (strcmp(cmd, "repl") == 0) return cmd_repl(prog);

    fprintf(stderr, "தெரியாத கட்டளை: '%s'\n\n", cmd);
    usage(stderr, prog);
    return 2;
}
