#ifndef TA_COMMON_H
#define TA_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TA_VERSION "0.2.0"

typedef enum {
    TA_EXIT_OK = 0,
    TA_EXIT_COMPILE = 1,
    TA_EXIT_USAGE = 2,
    TA_EXIT_RUNTIME = 70
} TaExitCode;

typedef enum {
    TA_ERR_NONE = 0,
    TA_ERR_LEX = 1000,
    TA_ERR_PARSE = 2000,
    TA_ERR_SEMANTIC = 3000,
    TA_ERR_TYPE = 4000,
    TA_ERR_INTERNAL = 5000,
    TA_ERR_IO = 6000
} TaErrorClass;

typedef struct {
    int code;
    int line;
    int col;
    const char *file;
    char *message;
    char *hint;
} TaDiagnostic;

typedef struct {
    TaDiagnostic **items;
    size_t count;
    size_t cap;
} TaDiagnostics;

TaDiagnostics *ta_diag_new(void);
void ta_diag_free(TaDiagnostics *d);
void ta_diag_report(TaDiagnostics *d, int code, const char *file, int line, int col,
                    const char *msg, const char *hint_fmt, ...);
void ta_diag_report_v(TaDiagnostics *d, int code, const char *file, int line, int col,
                      const char *msg, const char *hint_fmt, va_list ap);
bool ta_diag_has_errors(const TaDiagnostics *d);
size_t ta_diag_error_count(const TaDiagnostics *d);
void ta_diag_print_all(const TaDiagnostics *d, FILE *out, const char *source);
char *ta_source_line(const char *source, int line);
char *ta_sanitize_utf8(const char *s);

void *ta_xmalloc(size_t n);
void *ta_xcalloc(size_t n, size_t sz);
void *ta_xrealloc(void *p, size_t n);
char *ta_xstrdup(const char *s);
char *ta_xstrndup(const char *s, size_t n);

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TaStrBuf;

void ta_sb_init(TaStrBuf *sb);
void ta_sb_free(TaStrBuf *sb);
void ta_sb_putc(TaStrBuf *sb, char c);
void ta_sb_puts(TaStrBuf *sb, const char *s);
void ta_sb_appends(TaStrBuf *sb, const char *s, size_t n);
void ta_sb_printf(TaStrBuf *sb, const char *fmt, ...);

int ta_edit_distance(const char *a, const char *b);
char *ta_read_file(const char *path, size_t *out_len, int *err_code);
bool ta_write_file(const char *path, const char *data, size_t len);

#endif
