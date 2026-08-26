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

/* TA_LANG=en  →  all CLI messages in English instead of Tamil */
static bool g_english = false;

/* Bilingual helper: pick Tamil or English string at runtime */
#define TL(ta, en) (g_english ? (en) : (ta))

typedef struct {
    char *asm_path;
    char *exe_path;
} BuildOutput;

static int run_pipeline(const char *path, const char *src, TaStrBuf *asm_out,
                        bool *incomplete, bool echo_top_exprs) {
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

static int cmd_build(const char *argv0, const char *src_path, const char *out_exe,
                     bool show_banner) {
    int errc = 0;
    size_t len = 0;
    char *src = ta_read_file(src_path, &len, &errc);
    if (!src) {
        fprintf(stderr, "பிழை TA6001: கோப்பைப் படிக்க முடியவில்லை: %s\n", src_path);
        return 2;
    }
    TaStrBuf asm_sb;
    ta_sb_init(&asm_sb);
    int rc = run_pipeline(src_path, src, &asm_sb, NULL, false);
    free(src);
    if (rc != 0) {
        ta_sb_free(&asm_sb);
        return rc;
    }
    size_t need = strlen(out_exe) + 4;
    char *asm_path = ta_xmalloc(need);
    snprintf(asm_path, need, "%s.s", out_exe);
    if (!ta_write_file(asm_path, asm_sb.data, asm_sb.len)) {
        fprintf(stderr, "பிழை TA6002: assembly எழுத முடியவில்லை: %s\n", asm_path);
        ta_sb_free(&asm_sb);
        free(asm_path);
        return 1;
    }
    ta_sb_free(&asm_sb);

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
        fprintf(stderr, "பிழை TA6003: link செய்ய முடியவில்லை (%s)\n", hint ? hint : "cc failed");
        unlink(asm_path);
    } else if (show_banner) {
        printf("✓ %s (assembly: %s)\n", out_exe, asm_path);
    }
    free(hint);
    free(obj);
    free(asm_path);
    return lrc == 0 ? 0 : 1;
}

static void print_runtime_error_note(void) {}

static int cmd_run(const char *argv0, const char *src_path) {
    char tmpl[] = "/tmp/tamizhi-run-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        fprintf(stderr, "பிழை TA6002: தற்காலிக அடைவு உருவாக்க முடியவில்லை\n");
        return 1;
    }
    char exe[512];
    snprintf(exe, sizeof(exe), "%s/program", dir);
    int rc = cmd_build(argv0, src_path, exe, false);
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
    unlink(exe);
    unlink(asmpath);
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
    int rc = run_pipeline(src_path, src, &asm_sb, NULL, false);
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
           "  :help   — உதவி\n"
           "  :show   — இதுவரை எழுதிய நிரலைக் காட்டு\n"
           "  :clear  — நிரலை அழி\n"
           "  :quit   — வெளியேறு (Ctrl-D)\n"
           "\nஒவ்வொரு உள்ளீட்டிலும் முழு நிரல் compile செய்யப்பட்டு "
           "தொடக்கத்திலிருந்து இயக்கப்படும்.\n");
}

static int cmd_repl(const char *argv0) {
    printf("Tamizhi %s REPL — :help ஐப் பாருங்கள்; வெளியேற: :quit\n", TA_VERSION);
    TaStrBuf buf;
    ta_sb_init(&buf);
    char line[4096];

    for (;;) {
        printf("ta» ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t ll = strlen(line);
        while (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = 0;

        if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) break;
        if (strcmp(line, ":help") == 0) { repl_help(); continue; }
        if (strcmp(line, ":show") == 0) {
            printf("--- நிரல் ---\n%s--------------\n", buf.len ? buf.data : "(காலியாக உள்ளது)");
            continue;
        }
        if (strcmp(line, ":clear") == 0) {
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
        int rc = run_pipeline("<repl>", snapshot, &asm_sb, &incomplete, true);
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
            fprintf(stderr, "repl: temp dir failed\n");
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
            free(obj);
            free(snapshot);
            ta_sb_free(&asm_sb);
            unlink(asmp);
            rmdir(dir);
            continue;
        }
        char *hint = NULL;
        if (link_exe(asmp, obj, exe, &hint) != 0) {
            fprintf(stderr, "link failed\n");
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
    printf("Tamizhi — Konsole Tamil Font Setup\n");
    printf("===================================\n\n");

    /* 1. Must be running inside Konsole */
    const char *kver = getenv("KONSOLE_VERSION");
    const char *kdbus = getenv("KONSOLE_DBUS_SESSION");
    if (!kver && !kdbus) {
        fprintf(stderr,
            "ERROR: Not running inside KDE Konsole.\n"
            "  Open this terminal inside Konsole, then re-run:\n"
            "    ta setup-konsole\n");
        return 1;
    }
    printf("[1/4] Konsole detected (version %s)\n", kver ? kver : "unknown");

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
            "ERROR: No Tamil font found on this system.\n"
            "  Install one first:\n"
            "    Debian/Ubuntu : sudo apt install fonts-noto-core\n"
            "    Fedora        : sudo dnf install google-noto-sans-tamil-fonts\n"
            "    Arch          : sudo pacman -S noto-fonts\n");
        return 1;
    }
    printf("[2/4] Best Tamil font: %s\n", best);

    /* 3. Apply to current session via konsoleprofile */
    {
        char cmd[512];
        /* Konsole font string format: family,size,-1,5,50,0,0,0,0,0 */
        snprintf(cmd, sizeof(cmd),
            "konsoleprofile 'font=%s,12,-1,5,50,0,0,0,0,0' 2>/dev/null", best);
        int rc = system(cmd);
        if (rc == 0)
            printf("[3/4] Font applied to current Konsole session  ✓\n");
        else
            printf("[3/4] konsoleprofile failed (rc=%d) — will try kwriteconfig5\n", rc);
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
                    printf("[4/4] Profile '%s' updated permanently  ✓\n", defprofile);
                } else {
                    fclose(pf);
                }
            }
        }
    }
    if (!patched)
        printf("[4/4] Could not patch profile automatically.\n"
               "      Manual fix: Settings > Edit Profile > Appearance > Font > '%s'\n", best);

    printf("\n--- IMPORTANT NOTE ---\n");
    printf("Even with the correct font, Konsole's text shaper does NOT fully\n");
    printf("combine Tamil base letters + matras into proper akshar clusters.\n");
    printf("For perfect Tamil rendering use kitty or WezTerm instead:\n");
    printf("  kitty  : https://sw.kovidgoyal.net/kitty/\n");
    printf("  WezTerm: https://wezfurlong.org/wezterm/\n");
    printf("\nTo display all Tamizhi CLI messages in English (workaround):\n");
    printf("  export TA_LANG=en\n");
    printf("  (add to ~/.bashrc or ~/.profile for permanent effect)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* doctor: environment check                                           */
/* ------------------------------------------------------------------ */
static int cmd_doctor(void) {
    int problems = 0;
    printf("Tamizhi environment check / சூழல் பரிசோதனை\n");
    printf("=============================================\n");

    const char *lc_all = getenv("LC_ALL");
    const char *lc_ctype = getenv("LC_CTYPE");
    const char *lang = getenv("LANG");
    const char *eff = lc_all ? lc_all : (lc_ctype ? lc_ctype : lang);
    const char *codeset = nl_langinfo(CODESET);
    bool utf8 = codeset && (strcasecmp(codeset, "UTF-8") == 0 ||
                            strcasecmp(codeset, "UTF8") == 0);
    printf("1) locale      : %s (codeset %s)  %s\n",
           eff ? eff : "(unset)", codeset ? codeset : "?",
           utf8 ? "OK" : "FAIL: not UTF-8");
    if (!utf8) {
        problems++;
        printf("   -> fix: export LANG=en_US.UTF-8\n");
    }

    bool tty = isatty(1);
    printf("2) stdout      : %s\n", tty ? "terminal  OK" : "piped/file  OK");

    /* Detect terminal type */
    const char *kver = getenv("KONSOLE_VERSION");
    const char *term = getenv("TERM");
    const char *termapp = getenv("TERM_PROGRAM");
    if (kver) {
        printf("3) terminal    : KDE Konsole %s  "
               "[WARNING: limited Tamil shaping]\n", kver);
        printf("   -> run: ta setup-konsole   (auto-fix font)\n");
        printf("   -> or : export TA_LANG=en  (English output)\n");
        printf("   -> best: use kitty or WezTerm for full Tamil rendering\n");
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
            printf("4) Tamil fonts : %d found — ", nfonts);
            for (int i = 0; i < nfonts && i < 3; i++)
                printf("%s%s", fonts[i], i < nfonts-1 && i < 2 ? ", " : "");
            printf("\n");
        }
    }
    if (!f || nfonts == 0) {
        problems++;
        printf("4) Tamil fonts : NONE FOUND\n");
        printf("   -> Fedora       : sudo dnf install google-noto-sans-tamil-fonts\n");
        printf("   -> Debian/Ubuntu: sudo apt install fonts-noto-core\n");
        printf("   -> Arch         : sudo pacman -S noto-fonts\n");
    }

    /* Cluster rendering test */
    printf("5) Tamil cluster rendering test:\n");
    printf("     Reference : [க] [கா] [கி] [கீ] [கு] [கூ] [கெ] [கே] [கை] [கொ] [கோ] [க்]\n");
    printf("     Terminal  :  க    கா   கி   கீ   கு   கூ   கெ   கே   கை   கொ   கோ   க்\n");
    printf("   Each bracket pair above should show ONE combined Tamil character.\n");
    printf("   If you see broken/separated letters -> terminal shaping is failing.\n");
    if (kver)
        printf("   Konsole fix: ta setup-konsole   OR   export TA_LANG=en\n");

    printf("\nResult: %s (%d problem%s)\n",
           problems ? "NEEDS FIX" : "ALL OK", problems,
           problems == 1 ? "" : "s");
    return problems ? 1 : 0;
}

static void usage(FILE *out, const char *prog) {
    if (g_english) {
        fprintf(out,
            "Tamizhi v%s — Tamil programming language\n\n"
            "Usage: %s <command> [arguments]\n\n"
            "File extensions: .ta or .த\n"
            "Two modes: build (native binary like C); run/repl (scripted like Python)\n\n"
            "Commands:\n"
            "  build <file.ta> [-o output]   compile to native executable\n"
            "  run <file.ta>                 compile and run immediately\n"
            "  check <file.ta>               type-check only\n"
            "  repl                          interactive REPL\n"
            "  fmt <file.ta>                 format source to stdout\n"
            "  doctor                        check Tamil font/locale environment\n"
            "  setup-konsole                 auto-configure KDE Konsole Tamil font\n"
            "  version                       print version\n"
            "  help                          print this help\n"
            "\nTip: set TA_LANG=en to always use English output.\n",
            TA_VERSION, prog);
    } else {
        fprintf(out,
            "Tamizhi v%s — தமிழ் நிரலாக்க மொழி\n\n"
            "பயன்பாடு: %s <கட்டளை> [arguments]\n\n"
            "கோப்பு நீட்டிப்பு: .ta அல்லது .த\n"
            "இரு இயக்க முறைகள்: build (native binary); run/REPL (script)\n\n"
            "கட்டளைகள்:\n"
            "  build <file.ta> [-o output]   native executable உருவாக்கு\n"
            "  run <file.ta>                 compile செய்து உடனே இயக்கு\n"
            "  check <file.ta>               type-check மட்டும்\n"
            "  repl                          interactive REPL\n"
            "  fmt <file.ta>                 source format செய்து stdout-இல் காட்டு\n"
            "  doctor                        Tamil font/locale சூழல் பரிசோதனை\n"
            "  setup-konsole                 KDE Konsole Tamil font auto-setup\n"
            "  version                       பதிப்பு\n"
            "  help                          உதவி\n"
            "\n(TA_LANG=en — English output mode)\n",
            TA_VERSION, prog);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setlocale(LC_ALL, "");
    /* TA_LANG=en → switch all CLI messages to English */
    const char *talang = getenv("TA_LANG");
    if (talang && (strcasecmp(talang, "en") == 0 ||
                   strcasecmp(talang, "english") == 0))
        g_english = true;
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
        const char *out = NULL;
        for (int i = 3; i < argc; i++) {
            if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) &&
                i + 1 < argc) {
                out = argv[++i];
            }
        }
        char *def = NULL;
        if (!out) {
            def = default_output_name(argv[2]);
            out = def;
        }
        int rc = cmd_build(prog, argv[2], out, true);
        free(def);
        return rc;
    }
    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) {
            usage(stderr, prog);
            return 2;
        }
        print_runtime_error_note();
        return cmd_run(prog, argv[2]);
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
    if (strcmp(cmd, "setup-konsole") == 0) return cmd_setup_konsole();
    if (strcmp(cmd, "repl") == 0) return cmd_repl(prog);

    fprintf(stderr, g_english
        ? "Unknown command: '%s'\n\n"
        : "தெரியாத கட்டளை: '%s'\n\n", cmd);
    usage(stderr, prog);
    return 2;
}
