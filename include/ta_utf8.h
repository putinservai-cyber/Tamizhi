#ifndef TA_UTF8_H
#define TA_UTF8_H

#include "ta_common.h"

#define TA_UTF8_INVALID 0xFFFFFFFFu

uint32_t ta_utf8_decode(const char *s, size_t avail, size_t *advance);
size_t ta_utf8_encode(uint32_t cp, char out[4]);
size_t ta_utf8_cp_count(const char *s, size_t bytes);
bool ta_utf8_is_tamil_matra(uint32_t cp);

size_t ta_utf8_truncate_bytes(const char *s, size_t max_bytes);
int ta_utf8_prefix_columns(const char *line, size_t bytes, int upto_cp);

bool ta_utf8_is_ident_start(uint32_t cp);
bool ta_utf8_is_ident_cont(uint32_t cp);
bool ta_utf8_is_digit_cp(uint32_t cp);

#endif
