#include "ta_types.h"

static const TaType g_int = {TY_INT, NULL, NULL, NULL};
static const TaType g_float = {TY_FLOAT, NULL, NULL, NULL};
static const TaType g_bool = {TY_BOOL, NULL, NULL, NULL};
static const TaType g_char = {TY_CHAR, NULL, NULL, NULL};
static const TaType g_string = {TY_STRING, NULL, NULL, NULL};
static const TaType g_void = {TY_VOID, NULL, NULL, NULL};
static const TaType g_unknown = {TY_UNKNOWN, NULL, NULL, NULL};
static const TaType g_error = {TY_ERROR, NULL, NULL, NULL};

const TaType *ta_ty_int(void) { return &g_int; }
const TaType *ta_ty_float(void) { return &g_float; }
const TaType *ta_ty_bool(void) { return &g_bool; }
const TaType *ta_ty_char(void) { return &g_char; }
const TaType *ta_ty_string(void) { return &g_string; }
const TaType *ta_ty_void(void) { return &g_void; }
const TaType *ta_ty_unknown(void) { return &g_unknown; }
const TaType *ta_ty_error(void) { return &g_error; }

const TaType *ta_ty_list(const TaType *elem) {
    TaType *t = ta_xcalloc(1, sizeof(TaType));
    t->kind = TY_LIST;
    t->elem = elem;
    return t;
}

const TaType *ta_ty_dict(const TaType *key, const TaType *val) {
    TaType *t = ta_xcalloc(1, sizeof(TaType));
    t->kind = TY_DICT;
    t->key = key;
    t->val = val;
    return t;
}

bool ta_ty_equal(const TaType *a, const TaType *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TY_LIST: return ta_ty_equal(a->elem, b->elem);
        case TY_DICT:
            return ta_ty_equal(a->key, b->key) && ta_ty_equal(a->val, b->val);
        default: return true;
    }
}

const char *ta_ty_name(const TaType *t) {
    if (!t) return "?";
    switch (t->kind) {
        case TY_INT: return "முழுஎண்";
        case TY_FLOAT: return "மிதவை";
        case TY_BOOL: return "பூலியன்";
        case TY_CHAR: return "எழுத்து";
        case TY_STRING: return "உரை";
        case TY_VOID: return "வெற்று";
        case TY_LIST: return "பட்டியல்";
        case TY_DICT: return "அகராதி";
        case TY_UNKNOWN: return "தெரியாத";
        case TY_ERROR: return "பிழை";
    }
    return "?";
}

const char *ta_ty_name_en(const TaType *t) {
    if (!t) return "?";
    switch (t->kind) {
        case TY_INT: return "int";
        case TY_FLOAT: return "float";
        case TY_BOOL: return "bool";
        case TY_CHAR: return "char";
        case TY_STRING: return "string";
        case TY_VOID: return "void";
        case TY_LIST: return "list";
        case TY_DICT: return "dict";
        case TY_UNKNOWN: return "unknown";
        case TY_ERROR: return "error";
    }
    return "?";
}
