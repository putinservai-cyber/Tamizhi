#ifndef TA_TYPES_H
#define TA_TYPES_H

#include "ta_common.h"

typedef enum {
    TY_INT,
    TY_FLOAT,
    TY_BOOL,
    TY_CHAR,
    TY_STRING,
    TY_VOID,
    TY_LIST,
    TY_DICT,
    TY_UNKNOWN,
    TY_ERROR
} TaTypeKind;

typedef struct TaType TaType;

struct TaType {
    TaTypeKind kind;
    const struct TaType *elem;
    const struct TaType *key;
    const struct TaType *val;
};

const TaType *ta_ty_int(void);
const TaType *ta_ty_float(void);
const TaType *ta_ty_bool(void);
const TaType *ta_ty_char(void);
const TaType *ta_ty_string(void);
const TaType *ta_ty_void(void);
const TaType *ta_ty_unknown(void);
const TaType *ta_ty_error(void);
const TaType *ta_ty_list(const TaType *elem);
const TaType *ta_ty_dict(const TaType *key, const TaType *val);

bool ta_ty_equal(const TaType *a, const TaType *b);
const char *ta_ty_name(const TaType *t);
const char *ta_ty_name_en(const TaType *t);

#endif
