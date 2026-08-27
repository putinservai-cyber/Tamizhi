/*
 * Tamizhi compiler — C backend.
 *
 * Emits portable C11 from the typed intermediate representation (TaIrUnit).
 * This complements the x86-64 code generator and lets Tamizhi programs run on
 * any platform with a C compiler (Windows, macOS, Linux, *BSD, Android/Termux).
 *
 * The IR already lowers everything (print, dict, list element stores) into
 * individual TI_* instructions, so the emitter simply translates each one.
 * Operand C types are recovered by a pre-pass that walks the instructions and
 * records the type of every slot / temp that is ever assigned.
 */

#include "ta_ir.h"
#include "ta_semantic.h"
#include "ta_types.h"
#include "ta_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Static type singletons used for constant operands and inferred types. */
static const TaType T_INT   = { .kind = TY_INT };
static const TaType T_FLOAT = { .kind = TY_FLOAT };
static const TaType T_STR   = { .kind = TY_STRING };
static const TaType T_LIST  = { .kind = TY_LIST };
static const TaType T_DICT  = { .kind = TY_DICT };
static const TaType T_UNK   = { .kind = TY_UNKNOWN };

/* Result C type for a runtime call whose IR `ty` is NULL (codegen passes an
 * explicit type to store_res, so the IR instruction often lacks one). */
static const TaType *rt_ret_type(const char *name) {
    if (!name) return &T_INT;
    if (strncmp(name, "ta_rt_str_", 10) == 0) {
        if (strcmp(name, "ta_rt_str_split") == 0) return &T_LIST;
        return &T_STR;
    }
    if (strcmp(name, "ta_rt_list_new_n") == 0)   return &T_LIST;
    if (strcmp(name, "ta_rt_list_push") == 0)    return &T_LIST;
    if (strcmp(name, "ta_rt_dict_new") == 0)     return &T_DICT;
    if (strncmp(name, "ta_rt_range", 10) == 0)   return &T_LIST;
    if (strcmp(name, "ta_rt_input") == 0)        return &T_STR;
    if (strcmp(name, "ta_rt_list_get") == 0 ||
        strcmp(name, "ta_rt_dict_get") == 0 ||
        strcmp(name, "ta_rt_list_pop") == 0)     return &T_UNK;
    if (strcmp(name, "ta_rt_abs_f") == 0 ||
        strcmp(name, "ta_rt_sqrt") == 0 ||
        strcmp(name, "ta_rt_pow_f") == 0)        return &T_FLOAT;
    return &T_INT; /* int-returning / void (dst is NONE) */
}

static const char *c_type(const TaType *t) {
    if (!t) return "int64_t";
    switch (t->kind) {
    case TY_INT:    return "int64_t";
    case TY_FLOAT:  return "double";
    case TY_BOOL:   return "int64_t";
    case TY_CHAR:   return "int32_t";
    case TY_STRING: return "TaRtStr *";
    case TY_LIST:   return "TaRtList *";
    case TY_DICT:   return "TaRtDict *";
    case TY_VOID:   return "void";
    case TY_UNKNOWN:return "void *";
    default:        return "int64_t";
    }
}

/* Cast type used when reading a value out of a GC cell (pointer to value). */
static void c_cell_cast(TaStrBuf *b, const TaType *t) {
    if (!t) { ta_sb_printf(b, "int64_t *"); return; }
    switch (t->kind) {
    case TY_FLOAT:  ta_sb_printf(b, "double *"); break;
    case TY_CHAR:   ta_sb_printf(b, "int32_t *"); break;
    case TY_STRING: ta_sb_printf(b, "TaRtStr **"); break;
    case TY_LIST:   ta_sb_printf(b, "TaRtList **"); break;
    case TY_DICT:   ta_sb_printf(b, "TaRtDict **"); break;
    default:        ta_sb_printf(b, "int64_t *"); break; /* int, bool, unknown */
    }
}

typedef struct {
    const TaType **slot_types;
    const TaType **temp_types;
    size_t nslots, ntemps;
} FuncTypes;

static const TaType *op_type(FuncTypes *ft, const TaOperand *op) {
    switch (op->kind) {
    case TA_OP_SLOT:
        if (op->idx >= 0 && (size_t)op->idx < ft->nslots) return ft->slot_types[op->idx];
        return NULL;
    case TA_OP_TEMP:
        if (op->idx >= 0 && (size_t)op->idx < ft->ntemps) return ft->temp_types[op->idx];
        return NULL;
    case TA_OP_INT:  return &T_INT;
    case TA_OP_FLOAT:return &T_FLOAT;
    case TA_OP_STR:  return &T_STR;
    default:         return NULL;
    }
}

/* Emit an operand as an rvalue (no address taken). */
static void emit_op(TaStrBuf *b, const TaIrUnit *u, const TaOperand *op) {
    switch (op->kind) {
    case TA_OP_SLOT:  ta_sb_printf(b, "s%lld", (long long)op->idx); break;
    case TA_OP_TEMP:  ta_sb_printf(b, "t%lld", (long long)op->idx); break;
    case TA_OP_INT:   ta_sb_printf(b, "(int64_t)%" PRId64, op->i); break;
    case TA_OP_FLOAT: ta_sb_printf(b, "(double)%.17g", op->f); break;
    case TA_OP_STR: {
        const char *d = u->strings[op->str_id];
        size_t len = u->string_lens[op->str_id];
        ta_sb_printf(b, "ta_rt_str_from(\"");
        for (size_t k = 0; k < len; k++)
            ta_sb_printf(b, "\\x%02x", (unsigned char)d[k]);
        ta_sb_printf(b, "\", %zu)", len);
        break;
    }
    default: ta_sb_printf(b, "0"); break;
    }
}

/* Emit an operand that can have its address taken (for ta_rt_*_set calls). */
static void emit_addr_op(TaStrBuf *b, const TaIrUnit *u, const TaOperand *op) {
    switch (op->kind) {
    case TA_OP_SLOT:  ta_sb_printf(b, "&s%lld", (long long)op->idx); break;
    case TA_OP_TEMP:  ta_sb_printf(b, "&t%lld", (long long)op->idx); break;
    case TA_OP_INT:   ta_sb_printf(b, "(int64_t[]){(int64_t)%" PRId64 "}", op->i); break;
    case TA_OP_FLOAT: ta_sb_printf(b, "(double[]){(double)%.17g}", op->f); break;
    case TA_OP_STR: {
        ta_sb_printf(b, "(TaRtStr *[]){");
        emit_op(b, u, op);
        ta_sb_printf(b, "}");
        break;
    }
    default: ta_sb_printf(b, "(int64_t[]){0}"); break;
    }
}

static void emit_strlit(TaStrBuf *b, const TaIrUnit *u, int id) {
    const char *d = u->strings[id];
    size_t len = u->string_lens[id];
    ta_sb_printf(b, "ta_rt_str_from(\"");
    for (size_t k = 0; k < len; k++)
        ta_sb_printf(b, "\\x%02x", (unsigned char)d[k]);
    ta_sb_printf(b, "\", %zu)", len);
}

static const char *binop_str(TaBinOp op) {
    switch (op) {
    case TB_ADD: return "+";
    case TB_SUB: return "-";
    case TB_MUL: return "*";
    case TB_DIV: return "/";
    case TB_MOD: return "%";
    case TB_EQ:  return "==";
    case TB_NE:  return "!=";
    case TB_LT:  return "<";
    case TB_GT:  return ">";
    case TB_LE:  return "<=";
    case TB_GE:  return ">=";
    default:     return "+";
    }
}

static int is_param_slot(const TaSymbol *fn, int64_t slot) {
    if (!fn) return 0;
    for (size_t i = 0; i < (size_t)fn->fn.nparams; i++)
        if (fn->fn.params[i] && fn->fn.params[i]->slot == slot) return 1;
    return 0;
}

static const char *func_name_for(TaSymbol **syms, size_t n, TaSymbol *s) {
    static char buf[32];
    for (size_t i = 0; i < n; i++)
        if (syms[i] == s) { snprintf(buf, sizeof buf, "ta_fn_%zu", i); return buf; }
    return "ta_fn_0";
}

/* Pre-pass: determine the C type of every slot/temp that gets a value. */
static void compute_types(FuncTypes *ft, const TaIrInstr *in, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const TaIrInstr *ins = &in[i];
        const TaType *t = NULL;
        switch (ins->op) {
        case TI_CONST:
            if (ins->args[0].kind == TA_OP_FLOAT) t = &T_FLOAT;
            else if (ins->args[0].kind == TA_OP_STR) t = &T_STR;
            else t = &T_INT;
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps) ft->temp_types[ins->dst.idx] = t;
            break;
        case TI_LOAD:
            t = ins->ty ? ins->ty : op_type(ft, &ins->args[0]);
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps) ft->temp_types[ins->dst.idx] = t;
            break;
        case TI_BINOP:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps) ft->temp_types[ins->dst.idx] = ins->ty;
            break;
        case TI_NEG:
        case TI_CONV_I2F:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps) ft->temp_types[ins->dst.idx] = &T_FLOAT;
            break;
        case TI_NOT:
        case TI_CONV_F2I:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps) ft->temp_types[ins->dst.idx] = &T_INT;
            break;
        case TI_CALL:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps)
                ft->temp_types[ins->dst.idx] = ins->callee ? ins->callee->fn.ret : ins->ty;
            break;
        case TI_RT:
        case TI_LIST_NEW:
        case TI_DICT_NEW:
        case TI_STR_AT:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps)
                ft->temp_types[ins->dst.idx] = ins->ty ? ins->ty : rt_ret_type(ins->rt_name);
            break;
        case TI_IDX_GET:
            /* in->ty is the LIST type; the temp holds the element type. */
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps)
                ft->temp_types[ins->dst.idx] =
                    (ins->ty && ins->ty->elem) ? ins->ty->elem : &T_UNK;
            break;
        case TI_DEREF:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps)
                ft->temp_types[ins->dst.idx] = ins->ty ? ins->ty : &T_INT;
            break;
        case TI_LEN:
            if (ins->dst.kind == TA_OP_TEMP && ins->dst.idx >= 0 &&
                (size_t)ins->dst.idx < ft->ntemps)
                ft->temp_types[ins->dst.idx] = &T_INT;
            break;
        case TI_STORE:
            if (ins->args[0].kind == TA_OP_SLOT && ins->args[0].idx >= 0 &&
                (size_t)ins->args[0].idx < ft->nslots)
                ft->slot_types[ins->args[0].idx] = op_type(ft, &ins->args[1]);
            break;
        default:
            break;
        }
    }
}

static void emit_instr(TaStrBuf *b, const TaIrUnit *u, FuncTypes *ft,
                       const TaIrInstr *in) {
    switch (in->op) {
    case TI_CONST: {
        const TaOperand *v = &in->args[0];
        if (v->kind == TA_OP_FLOAT)
            ta_sb_printf(b, "    t%lld = (double)%.17g;\n", (long long)in->dst.idx, v->f);
        else if (v->kind == TA_OP_STR) {
            ta_sb_printf(b, "    t%lld = ", (long long)in->dst.idx);
            emit_strlit(b, u, v->str_id);
            ta_sb_printf(b, ";\n");
        } else
            ta_sb_printf(b, "    t%lld = (int64_t)%" PRId64 ";\n",
                         (long long)in->dst.idx, v->i);
        break;
    }
    case TI_LOAD:
        ta_sb_printf(b, "    t%lld = (%s)s%lld;\n", (long long)in->dst.idx,
                     c_type(ft->temp_types[in->dst.idx] ? ft->temp_types[in->dst.idx]
                                                      : op_type(ft, &in->args[0])),
                     (long long)in->args[0].idx);
        break;
    case TI_STORE:
        ta_sb_printf(b, "    s%lld = (%s)", (long long)in->args[0].idx,
                     c_type(ft->slot_types[in->args[0].idx]));
        emit_op(b, u, &in->args[1]);
        ta_sb_printf(b, ";\n");
        break;
    case TI_BINOP: {
        const TaType *rt = in->ty;
        const TaType *lt = op_type(ft, &in->args[0]);
        const TaType *rg = op_type(ft, &in->args[1]);
        if ((in->binop == TB_EQ || in->binop == TB_NE) &&
            lt && rg && lt->kind == TY_STRING && rg->kind == TY_STRING) {
            ta_sb_printf(b, "    t%lld = %s(ta_rt_str_eq(", (long long)in->dst.idx,
                         in->binop == TB_EQ ? "" : "!");
            emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", ");
            emit_op(b, u, &in->args[1]); ta_sb_printf(b, ")) ? 1 : 0;\n");
            break;
        }
        if (rt && rt->kind == TY_STRING) {
            ta_sb_printf(b, "    t%lld = ta_rt_str_concat(", (long long)in->dst.idx);
            emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", ");
            emit_op(b, u, &in->args[1]); ta_sb_printf(b, ");\n");
            break;
        }
        int use_double = (rt && rt->kind == TY_FLOAT) ||
                         (lt && lt->kind == TY_FLOAT) || (rg && rg->kind == TY_FLOAT);
        if (in->binop == TB_AND || in->binop == TB_OR) {
            const char *kw = (in->binop == TB_AND) ? "&&" : "||";
            ta_sb_printf(b, "    t%lld = (((", (long long)in->dst.idx);
            emit_op(b, u, &in->args[0]);
            ta_sb_printf(b, ") != 0) %s ((", kw);
            emit_op(b, u, &in->args[1]);
            ta_sb_printf(b, ") != 0)) ? 1 : 0;\n");
            break;
        }
        ta_sb_printf(b, "    t%lld = ", (long long)in->dst.idx);
        if (use_double) {
            ta_sb_printf(b, "((double)(");
            emit_op(b, u, &in->args[0]);
            ta_sb_printf(b, ") %s (double)(", binop_str(in->binop));
            emit_op(b, u, &in->args[1]);
            ta_sb_printf(b, "))");
        } else {
            ta_sb_printf(b, "((int64_t)(");
            emit_op(b, u, &in->args[0]);
            ta_sb_printf(b, ") %s (int64_t)(", binop_str(in->binop));
            emit_op(b, u, &in->args[1]);
            ta_sb_printf(b, "))");
        }
        ta_sb_printf(b, ";\n");
        break;
    }
    case TI_NEG:
        if (in->ty && in->ty->kind == TY_FLOAT)
            ta_sb_printf(b, "    t%lld = -(double)(", (long long)in->dst.idx);
        else
            ta_sb_printf(b, "    t%lld = -(int64_t)(", (long long)in->dst.idx);
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, ");\n");
        break;
    case TI_NOT:
        ta_sb_printf(b, "    t%lld = ((", (long long)in->dst.idx);
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, ") != 0) ? 0 : 1;\n");
        break;
    case TI_CONV_I2F:
        ta_sb_printf(b, "    t%lld = (double)(", (long long)in->dst.idx);
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, ");\n");
        break;
    case TI_CONV_F2I:
        ta_sb_printf(b, "    t%lld = (int64_t)(double)(", (long long)in->dst.idx);
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, ");\n");
        break;
    case TI_JMP:
        ta_sb_printf(b, "    goto L%lld;\n", (long long)in->label);
        break;
    case TI_JZ:
        ta_sb_printf(b, "    if ((int64_t)(");
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, ") == 0) goto L%lld;\n", (long long)in->label);
        break;
    case TI_LABEL:
        ta_sb_printf(b, "  L%lld:;\n", (long long)in->label);
        break;
    case TI_RT: {
        const char *rn = in->rt_name;
        if (rn && strcmp(rn, "ta_rt_list_set") == 0 && in->nargs == 3) {
            ta_sb_printf(b, "    ta_rt_list_set(");
            emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", ");
            emit_op(b, u, &in->args[1]); ta_sb_printf(b, ", (void *)");
            emit_addr_op(b, u, &in->args[2]); ta_sb_printf(b, ");\n");
        } else if (rn && strcmp(rn, "ta_rt_dict_set") == 0 && in->nargs == 4) {
            ta_sb_printf(b, "    ta_rt_dict_set(");
            emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", (void *)");
            emit_addr_op(b, u, &in->args[1]); ta_sb_printf(b, ", (void *)");
            emit_addr_op(b, u, &in->args[2]); ta_sb_printf(b, ", ");
            emit_op(b, u, &in->args[3]); ta_sb_printf(b, ");\n");
        } else if (rn && strcmp(rn, "ta_rt_dict_get") == 0 && in->nargs == 3) {
            ta_sb_printf(b, "    t%lld = (void *)ta_rt_dict_get(", (long long)in->dst.idx);
            emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", (void *)");
            emit_addr_op(b, u, &in->args[1]); ta_sb_printf(b, ", ");
            emit_op(b, u, &in->args[2]); ta_sb_printf(b, ");\n");
        } else if (rn && strcmp(rn, "ta_rt_list_push") == 0 && in->nargs == 2) {
            ta_sb_printf(b, "    t%lld = ta_rt_list_push(", (long long)in->dst.idx);
            emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", (void *)");
            emit_addr_op(b, u, &in->args[1]); ta_sb_printf(b, ");\n");
        } else {
            if (in->dst.kind != TA_OP_NONE)
                ta_sb_printf(b, "    t%lld = %s(", (long long)in->dst.idx, rn ? rn : "((void)0)");
            else
                ta_sb_printf(b, "    %s(", rn ? rn : "((void)0)");
            for (size_t a = 0; a < in->nargs; a++) {
                const TaType *at = op_type(ft, &in->args[a]);
                if (at) ta_sb_printf(b, "(%s)", c_type(at));
                emit_op(b, u, &in->args[a]);
                if (a + 1 < in->nargs) ta_sb_printf(b, ", ");
            }
            ta_sb_printf(b, ");\n");
        }
        break;
    }
    case TI_RET:
        if (in->nargs > 0) {
            ta_sb_printf(b, "    return ");
            emit_op(b, u, &in->args[0]);
            ta_sb_printf(b, ";\n");
        } else {
            ta_sb_printf(b, "    return;\n");
        }
        break;
    case TI_LIST_NEW:
        ta_sb_printf(b, "    t%lld = ta_rt_list_new_n(%lld);\n",
                     (long long)in->dst.idx, (long long)in->nargs);
        for (size_t a = 0; a < in->nargs; a++) {
            ta_sb_printf(b, "    ta_rt_list_set(t%lld, %lld, (void *)",
                         (long long)in->dst.idx, (long long)a);
            emit_addr_op(b, u, &in->args[a]);
            ta_sb_printf(b, ");\n");
        }
        break;
    case TI_DICT_NEW:
        ta_sb_printf(b, "    t%lld = ta_rt_dict_new();\n", (long long)in->dst.idx);
        break;
    case TI_IDX_GET:
        ta_sb_printf(b, "    t%lld = *((", (long long)in->dst.idx);
        c_cell_cast(b, (in->ty && in->ty->elem) ? in->ty->elem : NULL);
        ta_sb_printf(b, ")ta_rt_list_get(");
        emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", ");
        emit_op(b, u, &in->args[1]); ta_sb_printf(b, "));\n");
        break;
    case TI_IDX_SET:
        ta_sb_printf(b, "    ta_rt_list_set(");
        emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", ");
        emit_op(b, u, &in->args[1]); ta_sb_printf(b, ", (void *)");
        emit_addr_op(b, u, &in->args[2]); ta_sb_printf(b, ");\n");
        break;
    case TI_STR_AT:
        ta_sb_printf(b, "    t%lld = ta_rt_str_at(", (long long)in->dst.idx);
        emit_op(b, u, &in->args[0]); ta_sb_printf(b, ", ");
        emit_op(b, u, &in->args[1]); ta_sb_printf(b, ");\n");
        break;
    case TI_LEN: {
        const TaType *bt = op_type(ft, &in->args[0]);
        ta_sb_printf(b, "    t%lld = (int64_t)(", (long long)in->dst.idx);
        if (bt && bt->kind == TY_DICT)
            ta_sb_printf(b, "((const TaRtDict *)(");
        else
            ta_sb_printf(b, "((const TaRtList *)(");
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, "))->%s);\n", (bt && bt->kind == TY_DICT) ? "count" : "len");
        break;
    }
    case TI_DEREF:
        ta_sb_printf(b, "    t%lld = *((", (long long)in->dst.idx);
        c_cell_cast(b, in->ty);
        ta_sb_printf(b, ")(");
        emit_op(b, u, &in->args[0]);
        ta_sb_printf(b, "));\n");
        break;
    default:
        break;
    }
}

/* Emit one IR function (or ta_top) into the output buffer. */
static void emit_func(TaStrBuf *b, const TaIrUnit *u, TaSymbol **syms, size_t nfuncs,
                      const TaIrFunc *f, const char *cname, const TaType *ret,
                      int is_top) {
    /* Determine slot / temp counts. */
    size_t nslots = 0, ntemps = 0;
    for (size_t i = 0; i < f->count; i++) {
        const TaIrInstr *in = &f->items[i];
        for (size_t a = 0; a < in->nargs; a++) {
            if (in->args[a].kind == TA_OP_SLOT && (size_t)in->args[a].idx + 1 > nslots)
                nslots = (size_t)in->args[a].idx + 1;
            if (in->args[a].kind == TA_OP_TEMP && (size_t)in->args[a].idx + 1 > ntemps)
                ntemps = (size_t)in->args[a].idx + 1;
        }
        if (in->dst.kind == TA_OP_SLOT && (size_t)in->dst.idx + 1 > nslots)
            nslots = (size_t)in->dst.idx + 1;
        if (in->dst.kind == TA_OP_TEMP && (size_t)in->dst.idx + 1 > ntemps)
            ntemps = (size_t)in->dst.idx + 1;
    }

    FuncTypes ft;
    ft.slot_types = calloc(nslots ? nslots : 1, sizeof(*ft.slot_types));
    ft.temp_types = calloc(ntemps ? ntemps : 1, sizeof(*ft.temp_types));
    ft.nslots = nslots;
    ft.ntemps = ntemps;
    /* Iterate to a fixpoint: a slot's type may depend on another slot's type. */
    for (int pass = 0; pass < 16; pass++)
        compute_types(&ft, f->items, f->count);

    /* Signature. */
    if (is_top) {
        ta_sb_printf(b, "void ta_top(void) {\n");
    } else {
        ta_sb_printf(b, "%s %s(", c_type(ret), cname);
        const TaSymbol *fn = f->sym;
        for (size_t p = 0; fn && p < (size_t)fn->fn.nparams; p++) {
            const TaSymbol *par = fn->fn.params[p];
            ta_sb_printf(b, "%s s%lld", c_type(par ? par->type : NULL),
                         (long long)(par ? par->slot : 0));
            if (p + 1 < (size_t)fn->fn.nparams) ta_sb_printf(b, ", ");
        }
        ta_sb_printf(b, ") {\n");
    }

    /* Local declarations (skip parameters; default to int64_t if unresolved). */
    for (size_t s = 0; s < nslots; s++) {
        if (!is_param_slot(f->sym, (int64_t)s))
            ta_sb_printf(b, "    %s s%zu;\n",
                         c_type(ft.slot_types[s] ? ft.slot_types[s] : &T_INT), s);
    }
    for (size_t t = 0; t < ntemps; t++) {
        if (ft.temp_types[t])
            ta_sb_printf(b, "    %s t%zu;\n", c_type(ft.temp_types[t]), t);
    }
    ta_sb_printf(b, "\n");

    /* Body. */
    for (size_t i = 0; i < f->count; i++) {
        const TaIrInstr *in = &f->items[i];
        if (in->op == TI_CALL) {
            const char *name = func_name_for(syms, nfuncs, in->callee);
            if (in->dst.kind != TA_OP_NONE)
                ta_sb_printf(b, "    t%lld = %s(", (long long)in->dst.idx, name);
            else
                ta_sb_printf(b, "    %s(", name);
            const TaSymbol *callee = in->callee;
            for (size_t a = 0; a < in->nargs; a++) {
                const TaType *at = (callee && a < (size_t)callee->fn.nparams &&
                                    callee->fn.params[a])
                                       ? callee->fn.params[a]->type : NULL;
                if (at) ta_sb_printf(b, "(%s)", c_type(at));
                emit_op(b, u, &in->args[a]);
                if (a + 1 < in->nargs) ta_sb_printf(b, ", ");
            }
            ta_sb_printf(b, ");\n");
            continue;
        }
        emit_instr(b, u, &ft, in);
    }

    ta_sb_printf(b, "}\n\n");
    free(ft.slot_types);
    free(ft.temp_types);
}

void ta_ir_emit_c(const TaIrUnit *unit, TaStrBuf *out) {
    /* Build symbol -> function-name map. */
    size_t n = unit->nfuncs;
    TaSymbol **syms = NULL;
    if (n) {
        syms = malloc(n * sizeof(*syms));
        for (size_t i = 0; i < n; i++) syms[i] = unit->funcs[i]->sym;
    }

    ta_sb_printf(out, "#include \"tart.h\"\n");
    ta_sb_printf(out, "#include <stdint.h>\n");
    ta_sb_printf(out, "#include <stdio.h>\n");
    ta_sb_printf(out, "#include <string.h>\n");
    ta_sb_printf(out, "#include <math.h>\n\n");

    /* Forward declarations (C requires functions be declared before use). */
    ta_sb_printf(out, "void ta_top(void);\n");
    for (size_t i = 0; i < n; i++) {
        char cname[32];
        snprintf(cname, sizeof cname, "ta_fn_%zu", i);
        const TaSymbol *fn = unit->funcs[i]->sym;
        ta_sb_printf(out, "%s %s(",
                     c_type(unit->funcs[i]->sym ? unit->funcs[i]->sym->fn.ret : NULL),
                     cname);
        for (size_t p = 0; fn && p < (size_t)fn->fn.nparams; p++) {
            const TaSymbol *par = fn->fn.params[p];
            ta_sb_printf(out, "%s s%lld", c_type(par ? par->type : NULL),
                         (long long)(par ? par->slot : 0));
            if (p + 1 < (size_t)fn->fn.nparams) ta_sb_printf(out, ", ");
        }
        if (!fn || fn->fn.nparams == 0) ta_sb_printf(out, "void");
        ta_sb_printf(out, ");\n");
    }
    ta_sb_printf(out, "\n");

    /* Top-level statements. */
    if (unit->top)
        emit_func(out, unit, syms, n, unit->top, "ta_top", NULL, 1);

    /* Functions. */
    for (size_t i = 0; i < n; i++) {
        char cname[32];
        snprintf(cname, sizeof cname, "ta_fn_%zu", i);
        emit_func(out, unit, syms, n, unit->funcs[i], cname,
                  unit->funcs[i]->sym ? unit->funcs[i]->sym->fn.ret : NULL, 0);
    }

    /* main(). */
    ta_sb_printf(out, "int main(void) {\n");
    ta_sb_printf(out, "    ta_rt_init();\n");
    ta_sb_printf(out, "    ta_top();\n");
    if (unit->main_sym) {
        const char *m = func_name_for(syms, n, unit->main_sym);
        ta_sb_printf(out, "    %s();\n", m);
    }
    ta_sb_printf(out, "    return 0;\n");
    ta_sb_printf(out, "}\n");

    free(syms);
}
