#include "ta_codegen.h"

typedef struct {
    TaStrBuf *o;
    const TaIrUnit *u;
    const TaIrFunc *fn;
    double *fpool;
    uint64_t *fbits;
    size_t nfpool;
    size_t fpool_cap;
} Cg;

static void E(Cg *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ta_sb_puts(c->o, "    ");
    ta_sb_puts(c->o, buf);
    ta_sb_putc(c->o, '\n');
}

static void L(Cg *c, const char *name) {
    ta_sb_printf(c->o, "%s:\n", name);
}

static int float_slot(Cg *c, double d) {
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    for (size_t i = 0; i < c->nfpool; i++) {
        if (c->fbits[i] == bits) return (int)i;
    }
    if (c->nfpool == c->fpool_cap) {
        c->fpool_cap = c->fpool_cap ? c->fpool_cap * 2 : 16;
        c->fpool = ta_xrealloc(c->fpool, c->fpool_cap * sizeof(double));
        c->fbits = ta_xrealloc(c->fbits, c->fpool_cap * sizeof(uint64_t));
    }
    c->fpool[c->nfpool] = d;
    c->fbits[c->nfpool] = bits;
    return (int)c->nfpool++;
}

static long long slot_off(const TaOperand *o) {
    return -8LL * ((long long)o->idx + 1);
}

static void load_int_to(Cg *c, const TaOperand *o, const char *reg) {
    switch (o->kind) {
        case TA_OP_INT:
            if (o->i >= -2147483648LL && o->i <= 2147483647LL)
                E(c, "mov %s, %lld", reg, o->i);
            else
                E(c, "movabs %s, %lld", reg, o->i);
            break;
        case TA_OP_STR:
            E(c, "lea %s, [rip+LS%d]", reg, o->str_id);
            break;
        case TA_OP_TEMP:
        case TA_OP_SLOT:
            E(c, "mov %s, [rbp%+lld]", reg, slot_off(o));
            break;
        case TA_OP_FLOAT: {
            int fi = float_slot(c, o->f);
            E(c, "movsd xmm7, [rip+LCF%d]", fi);
            E(c, "movq %s, xmm7", reg);
            break;
        }
        default:
            E(c, "xor %s, %s", reg, reg);
            break;
    }
}

static void load_flt_to(Cg *c, const TaOperand *o, const char *xreg) {
    switch (o->kind) {
        case TA_OP_FLOAT: {
            int fi = float_slot((Cg *)c, o->f);
            E(c, "movsd %s, [rip+LCF%d]", xreg, fi);
            break;
        }
        case TA_OP_TEMP:
        case TA_OP_SLOT:
            E(c, "movsd %s, [rbp%+lld]", xreg, slot_off(o));
            break;
        default:
            load_int_to(c, o, "rax");
            E(c, "cvtsi2sd %s, rax", xreg);
            break;
    }
}

static void store_res(Cg *c, const TaType *ty, TaOperand dst) {
    if (!dst.kind) return;
    if (ty && ty->kind == TY_FLOAT && dst.kind != TA_OP_NONE) {
        E(c, "movsd [rbp%+lld], xmm0", slot_off(&dst));
    } else if (dst.kind != TA_OP_NONE) {
        E(c, "mov [rbp%+lld], rax", slot_off(&dst));
    }
}

static const char *setcc_for(TaBinOp op) {
    switch (op) {
        case TB_EQ: return "sete";
        case TB_NE: return "setne";
        case TB_LT: return "setl";
        case TB_GT: return "setg";
        case TB_LE: return "setle";
        case TB_GE: return "setge";
        default: return "sete";
    }
}

static void emit_int_cmp(Cg *c, TaBinOp op, const TaOperand *a, const TaOperand *b,
                         TaOperand dst) {
    load_int_to(c, a, "rax");
    load_int_to(c, b, "rcx");
    E(c, "cmp rax, rcx");
    E(c, "%s al", setcc_for(op));
    E(c, "movzx rax, al");
    store_res(c, ta_ty_bool(), dst);
}

static void emit_float_cmp(Cg *c, TaBinOp op, const TaOperand *a, const TaOperand *b,
                           TaOperand dst) {
    load_flt_to(c, a, "xmm0");
    load_flt_to(c, b, "xmm1");
    switch (op) {
        case TB_EQ:
            E(c, "comisd xmm0, xmm1");
            E(c, "sete al");
            E(c, "setnp cl");
            E(c, "and al, cl");
            E(c, "movzx rax, al");
            break;
        case TB_NE:
            E(c, "comisd xmm0, xmm1");
            E(c, "setne al");
            E(c, "setp cl");
            E(c, "or al, cl");
            E(c, "movzx rax, al");
            break;
        case TB_LT:
            E(c, "comisd xmm1, xmm0");
            E(c, "seta al");
            E(c, "movzx rax, al");
            break;
        case TB_LE:
            E(c, "comisd xmm1, xmm0");
            E(c, "setae al");
            E(c, "movzx rax, al");
            break;
        case TB_GT:
            E(c, "comisd xmm0, xmm1");
            E(c, "seta al");
            E(c, "movzx rax, al");
            break;
        case TB_GE:
            E(c, "comisd xmm0, xmm1");
            E(c, "setae al");
            E(c, "movzx rax, al");
            break;
        default:
            E(c, "xor eax, eax");
            break;
    }
    store_res(c, ta_ty_bool(), dst);
}

static bool rt_all_float_args(const char *rt_name);

static void call_with_args(Cg *c, const char *target_label, const TaType **arg_types,
                           const TaOperand *args, size_t nargs, const TaType *ret_ty,
                           TaOperand dst) {
    static const char *iregs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    static const char *xregs[8] = {"xmm0", "xmm1", "xmm2", "xmm3",
                                   "xmm4", "xmm5", "xmm6", "xmm7"};

    size_t nint = 0;
    bool *flt = ta_xmalloc((nargs ? nargs : 1) * sizeof(bool));
    for (size_t i = 0; i < nargs; i++) {
        flt[i] = rt_all_float_args(target_label)
                     ? true
                     : (arg_types && arg_types[i] ? arg_types[i]->kind == TY_FLOAT : false);
        if (!flt[i]) nint++;
    }

    /* Integer args beyond the 6 GP registers, and float args beyond the 8 XMM
       registers, spill to the stack and must be pushed in reverse
       (right-to-left) order per the x86-64 SysV ABI. */
    size_t nstack_int = nint > 6 ? nint - 6 : 0;
    size_t nstack_flt = 0;
    {
        size_t nflt = 0;
        for (size_t i = 0; i < nargs; i++) if (flt[i]) nflt++;
        if (nflt > 8) nstack_flt = nflt - 8;
    }
    size_t nstack = nstack_int + nstack_flt;
    size_t pad = (nstack % 2) != 0 ? 8 : 0;

    if (pad) E(c, "sub rsp, %lld", (long long)pad);

    if (nstack_int) {
        size_t *idx = ta_xmalloc(nstack_int * sizeof(size_t));
        size_t cnt = 0, ipos = 0;
        for (size_t i = 0; i < nargs; i++) {
            if (flt[i]) continue;
            if (ipos >= 6) idx[cnt++] = i;
            ipos++;
        }
        for (size_t s = cnt; s-- > 0;) {
            load_int_to(c, &args[idx[s]], "rax");
            E(c, "push rax");
        }
        free(idx);
    }
    if (nstack_flt) {
        size_t *idx = ta_xmalloc(nstack_flt * sizeof(size_t));
        size_t cnt = 0, xpos = 0;
        for (size_t i = 0; i < nargs; i++) {
            if (!flt[i]) continue;
            if (xpos >= 8) idx[cnt++] = i;
            xpos++;
        }
        for (size_t s = cnt; s-- > 0;) {
            load_flt_to(c, &args[idx[s]], "xmm0");
            E(c, "sub rsp, 8");
            E(c, "movsd [rsp], xmm0");
        }
        free(idx);
    }

    int ii = 0, xi = 0;
    for (size_t i = 0; i < nargs; i++) {
        if (flt[i]) {
            if (xi < 8) load_flt_to(c, &args[i], xregs[xi++]);
        } else if (ii < 6) {
            load_int_to(c, &args[i], iregs[ii++]);
        }
    }

    E(c, "call %s", target_label);

    free(flt);

    if (nstack || pad) E(c, "add rsp, %lld", (long long)(nstack * 8 + pad));

    if (ret_ty && ret_ty->kind == TY_FLOAT) {
        store_res(c, ret_ty, dst);
    } else {
        store_res(c, NULL, dst);
    }
}

static bool rt_all_float_args(const char *rt_name) {
    static const char *names[] = {"ta_rt_print_float", "ta_rt_abs_f", "ta_rt_floor",
                                  "ta_rt_sqrt", "ta_rt_pow_f"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(rt_name, names[i]) == 0) return true;
    }
    return false;
}

static void emit_def_lbl(Cg *c, int lbl) {
    ta_sb_printf(c->o, ".L%d:\n", lbl);
}

static void emit_jmp_lbl(Cg *c, int lbl) {
    E(c, "jmp .L%d", lbl);
}

static void emit_jz_lbl(Cg *c, int lbl) {
    E(c, "jz .L%d", lbl);
}

static void L_label(Cg *c, int n) {
    char b[40];
    snprintf(b, sizeof(b), ".Lok%d", n);
    L(c, b);
}

static void gen_instr(Cg *c, const TaIrInstr *in) {
    switch (in->op) {
        case TI_CONST: {
            const TaOperand *v = &in->args[0];
            if (in->ty && in->ty->kind == TY_FLOAT && v->kind == TA_OP_FLOAT) {
                int fi = float_slot(c, v->f);
                E(c, "movsd xmm0, [rip+LCF%d]", fi);
                E(c, "movsd [rbp%+lld], xmm0", slot_off(&in->dst));
            } else if (v->kind == TA_OP_STR) {
                E(c, "lea rax, [rip+LS%d]", v->str_id);
                store_res(c, NULL, in->dst);
            } else {
                load_int_to(c, v, "rax");
                store_res(c, NULL, in->dst);
            }
            break;
        }
        case TI_LOAD:
            load_int_to(c, &in->args[0], "rax");
            store_res(c, NULL, in->dst);
            break;
        case TI_STORE:
            load_int_to(c, &in->args[1], "rax");
            E(c, "mov [rbp%+lld], rax", slot_off(&in->args[0]));
            break;
        case TI_BINOP: {
            TaBinOp op = (TaBinOp)in->binop;
            const TaType *ty = in->ty;
            if (ty && ty->kind == TY_FLOAT) {
                if (op >= TB_EQ && op <= TB_GE) {
                    emit_float_cmp(c, op, &in->args[0], &in->args[1], in->dst);
                    break;
                }
                load_flt_to(c, &in->args[0], "xmm0");
                load_flt_to(c, &in->args[1], "xmm1");
                switch (op) {
                    case TB_ADD: E(c, "addsd xmm0, xmm1"); break;
                    case TB_SUB: E(c, "subsd xmm0, xmm1"); break;
                    case TB_MUL: E(c, "mulsd xmm0, xmm1"); break;
                    case TB_DIV: E(c, "divsd xmm0, xmm1"); break;
                    default: break;
                }
                store_res(c, ty, in->dst);
                break;
            }
            if (ty && ty->kind == TY_STRING) {
                if (op == TB_ADD) {
                    call_with_args(c, "ta_rt_str_concat", NULL, in->args, 2,
                                   ta_ty_string(), in->dst);
                    break;
                }
                if (op == TB_EQ || op == TB_NE) {
                    call_with_args(c, "ta_rt_str_eq", NULL, in->args, 2, ta_ty_bool(),
                                   (TaOperand){TA_OP_NONE, 0, 0, 0, 0});
                    if (op == TB_NE) E(c, "xor al, 1");
                    E(c, "movzx rax, al");
                    store_res(c, ta_ty_bool(), in->dst);
                    break;
                }
                const TaType *boolty = ta_ty_bool();
                call_with_args(c, "ta_rt_str_cmp", NULL, in->args, 2, ta_ty_int(),
                               (TaOperand){TA_OP_NONE, 0, 0, 0, 0});
                E(c, "cmp rax, 0");
                E(c, "%s al", setcc_for(op));
                E(c, "movzx rax, al");
                store_res(c, boolty, in->dst);
                break;
            }
            if (op >= TB_EQ && op <= TB_GE) {
                emit_int_cmp(c, op, &in->args[0], &in->args[1], in->dst);
                break;
            }
            load_int_to(c, &in->args[0], "rax");
            load_int_to(c, &in->args[1], "rcx");
            switch (op) {
                case TB_ADD: E(c, "add rax, rcx"); break;
                case TB_SUB: E(c, "sub rax, rcx"); break;
                case TB_MUL: E(c, "imul rax, rcx"); break;
                case TB_DIV:
                case TB_MOD: {
                    static int divz = 0;
                    int lok = divz++;
                    E(c, "test rcx, rcx");
                    E(c, "jnz .Lok%d", lok);
                    E(c, "call ta_rt_abort_div_zero");
                    L_label(c, lok);
                    E(c, "cqo");
                    E(c, "idiv rcx");
                    if (op == TB_MOD) E(c, "mov rax, rdx");
                    break;
                }
                default: break;
            }
            store_res(c, ty, in->dst);
            break;
        }
        case TI_NEG: {
            if (in->ty && in->ty->kind == TY_FLOAT) {
                load_flt_to(c, &in->args[0], "xmm0");
                E(c, "pxor xmm1, xmm1");
                E(c, "subsd xmm1, xmm0");
                E(c, "movsd xmm0, xmm1");
                store_res(c, in->ty, in->dst);
            } else {
                load_int_to(c, &in->args[0], "rax");
                E(c, "neg rax");
                store_res(c, in->ty, in->dst);
            }
            break;
        }
        case TI_NOT:
            load_int_to(c, &in->args[0], "rax");
            E(c, "test rax, rax");
            E(c, "sete al");
            E(c, "movzx rax, al");
            store_res(c, ta_ty_bool(), in->dst);
            break;
        case TI_CONV_I2F:
            load_int_to(c, &in->args[0], "rax");
            E(c, "cvtsi2sd xmm0, rax");
            store_res(c, ta_ty_float(), in->dst);
            break;
        case TI_CONV_F2I:
            load_flt_to(c, &in->args[0], "xmm0");
            E(c, "cvttsd2si rax, xmm0");
            store_res(c, ta_ty_int(), in->dst);
            break;
        case TI_JMP:
            emit_jmp_lbl(c, in->label);
            break;
        case TI_JZ:
            load_int_to(c, &in->args[0], "rax");
            E(c, "test rax, rax");
            emit_jz_lbl(c, in->label);
            break;
        case TI_LABEL:
            emit_def_lbl(c, in->label);
            break;
        case TI_CALL: {
            TaSymbol *sym = in->callee;
            char target[64];
            snprintf(target, sizeof(target), "ta_fn_%d", sym->fn.index);
            const TaType **ats = ta_xmalloc((in->nargs ? in->nargs : 1) * sizeof(void *));
            for (size_t i = 0; i < in->nargs; i++)
                ats[i] = sym->fn.params[i] ? sym->fn.params[i]->type : NULL;
            call_with_args(c, target, ats, in->args, in->nargs, sym->fn.ret, in->dst);
            free(ats);
            break;
        }
        case TI_RT:
            if (strcmp(in->rt_name, "ta_rt_list_set") == 0 && in->nargs == 3) {
                load_int_to(c, &in->args[0], "rdi");
                load_int_to(c, &in->args[1], "rsi");
                E(c, "lea rdx, [rbp%+lld]", slot_off(&in->args[2]));
                E(c, "call ta_rt_list_set");
                break;
            }
            if (strcmp(in->rt_name, "ta_rt_dict_set") == 0 && in->nargs == 4) {
                load_int_to(c, &in->args[0], "rdi");
                E(c, "lea rsi, [rbp%+lld]", slot_off(&in->args[1]));
                E(c, "lea rdx, [rbp%+lld]", slot_off(&in->args[2]));
                load_int_to(c, &in->args[3], "rcx");
                E(c, "call ta_rt_dict_set");
                break;
            }
            if (strcmp(in->rt_name, "ta_rt_dict_get") == 0 && in->nargs == 3) {
                load_int_to(c, &in->args[0], "rdi");
                E(c, "lea rsi, [rbp%+lld]", slot_off(&in->args[1]));
                load_int_to(c, &in->args[2], "rdx");
                E(c, "call ta_rt_dict_get");
                store_res(c, NULL, in->dst);
                break;
            }
            call_with_args(c, in->rt_name, NULL, in->args, in->nargs, in->ty, in->dst);
            break;
        case TI_RET: {
            if (in->nargs > 0) {
                if (c->fn->ret && c->fn->ret->kind == TY_FLOAT)
                    load_flt_to(c, &in->args[0], "xmm0");
                else
                    load_int_to(c, &in->args[0], "rax");
            } else if (c->fn->ret && c->fn->ret->kind == TY_FLOAT) {
                E(c, "pxor xmm0, xmm0");
            } else {
                E(c, "xor eax, eax");
            }
            E(c, "leave");
            E(c, "ret");
            break;
        }
        case TI_LIST_NEW: {
            E(c, "mov rdi, %lld", (long long)in->nargs);
            E(c, "call ta_rt_list_new_n");
            E(c, "push rax");
            for (size_t i = 0; i < in->nargs; i++) {
                const TaType *et = in->ty && in->ty->elem ? in->ty->elem : NULL;
                if (et && et->kind == TY_FLOAT) {
                    load_flt_to(c, &in->args[i], "xmm0");
                    E(c, "movq rax, xmm0");
                } else {
                    load_int_to(c, &in->args[i], "rax");
                }
                E(c, "mov rcx, [rsp]");
                E(c, "mov [rcx+%lld], rax", (long long)(8 + i * 8));
            }
            E(c, "pop rax");
            store_res(c, NULL, in->dst);
            break;
        }
        case TI_DICT_NEW:
            call_with_args(c, "ta_rt_dict_new", NULL, NULL, 0, NULL, in->dst);
            break;
        case TI_IDX_GET: {
            const TaOperand *base = &in->args[0];
            const TaOperand *idx = &in->args[1];
            load_int_to(c, base, "rdi");
            load_int_to(c, idx, "rsi");
            E(c, "call ta_rt_list_get");
            if (in->ty && in->ty->elem && in->ty->elem->kind == TY_FLOAT) {
                E(c, "movsd xmm0, [rax]");
                store_res(c, ta_ty_float(), in->dst);
            } else {
                E(c, "mov rax, [rax]");
                store_res(c, NULL, in->dst);
            }
            break;
        }
        case TI_IDX_SET: {
            const TaOperand *base = &in->args[0];
            const TaOperand *idx = &in->args[1];
            const TaOperand *val = &in->args[2];
            load_int_to(c, base, "rdi");
            load_int_to(c, idx, "rsi");
            E(c, "lea rdx, [rbp%+lld]", slot_off(val));
            E(c, "call ta_rt_list_set");
            break;
        }
        case TI_LEN:
            load_int_to(c, &in->args[0], "rax");
            E(c, "mov rax, [rax]");
            store_res(c, ta_ty_int(), in->dst);
            break;
        case TI_STR_AT:
            call_with_args(c, "ta_rt_str_at", NULL, in->args, 2, ta_ty_char(), in->dst);
            break;
        case TI_DEREF:
            load_int_to(c, &in->args[0], "rax");
            if (in->ty && in->ty->kind == TY_FLOAT) {
                E(c, "movsd xmm0, [rax]");
                store_res(c, ta_ty_float(), in->dst);
            } else {
                E(c, "mov rax, [rax]");
                store_res(c, NULL, in->dst);
            }
            break;
        }
}

static void gen_func(Cg *c, const TaIrFunc *f) {
    c->fn = f;
    L(c, f->label);
    E(c, "push rbp");
    E(c, "mov rbp, rsp");
    size_t frame = ((size_t)f->nslots * 8 + 15) & ~(size_t)15;
    if (frame < 16) frame = 16;
    E(c, "sub rsp, %lld", (long long)frame);

    {
        static const char *iregs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        size_t ii = 0, xi = 0;
        if (f->sym)
            for (size_t i = 0; i < f->sym->fn.nparams; i++) {
                long long off = -8LL * ((long long)i + 1);
                const TaType *pt =
                    f->sym->fn.params[i] ? f->sym->fn.params[i]->type : NULL;
                if (pt && pt->kind == TY_FLOAT) {
                    if (xi < 8) {
                        E(c, "movsd [rbp%+lld], xmm%zu", off, xi++);
                    }
                } else if (ii < 6) {
                    E(c, "mov [rbp%+lld], %s", off, iregs[ii++]);
                } else {
                    E(c, "mov rax, [rbp+%lld]", 16LL + 8LL * (long long)(ii - 6));
                    E(c, "mov [rbp%+lld], rax", off);
                    ii++;
                }
            }
    }

    for (size_t i = 0; i < f->count; i++) gen_instr(c, &f->items[i]);

    if (f->ret && f->ret->kind == TY_FLOAT)
        E(c, "pxor xmm0, xmm0");
    else
        E(c, "xor eax, eax");
    E(c, "leave");
    E(c, "ret");
}

static bool rt_all_float_args_fwd(const char *rt_name) {
    return rt_all_float_args(rt_name);
}

bool ta_codegen_emit(TaIrUnit *unit, TaStrBuf *out_asm) {
    Cg c;
    memset(&c, 0, sizeof(c));
    c.o = out_asm;
    c.u = unit;

    ta_sb_puts(out_asm, ".intel_syntax noprefix\n");
    ta_sb_puts(out_asm, ".text\n");

    ta_sb_puts(out_asm, ".globl main\n");
    ta_sb_puts(out_asm, "main:\n");
    ta_sb_puts(out_asm, "    push rbp\n");
    ta_sb_puts(out_asm, "    mov rbp, rsp\n");
    ta_sb_puts(out_asm, "    call ta_top\n");
    if (unit->main_sym) {
        char target[64];
        snprintf(target, sizeof(target), "ta_fn_%d", unit->main_sym->fn.index);
        ta_sb_printf(out_asm, "    call %s\n", target);
    }
    ta_sb_puts(out_asm, "    xor eax, eax\n");
    ta_sb_puts(out_asm, "    leave\n");
    ta_sb_puts(out_asm, "    ret\n\n");

    for (size_t i = 0; i < unit->nfuncs; i++) {
        gen_func(&c, unit->funcs[i]);
        ta_sb_putc(out_asm, '\n');
    }

    if (c.nfpool > 0) {
        ta_sb_puts(out_asm, ".section .rodata\n");
        for (size_t i = 0; i < c.nfpool; i++) {
            ta_sb_printf(out_asm, "LCF%zu:\n    .quad %llu\n", i,
                         (unsigned long long)c.fbits[i]);
        }
        ta_sb_putc(out_asm, '\n');
    }

    if (unit->nstrings > 0) {
        ta_sb_puts(out_asm, ".section .rodata\n");
        for (size_t i = 0; i < unit->nstrings; i++) {
            ta_sb_printf(out_asm, "LS%zu:\n", i);
            const unsigned char *s = (const unsigned char *)unit->strings[i];
            size_t len = unit->string_lens[i];
            ta_sb_printf(out_asm, "    .quad %zu\n", len);
            for (size_t k = 0; k < len; k++) {
                if (k % 16 == 0) ta_sb_puts(out_asm, "    .byte ");
                ta_sb_printf(out_asm, "%u", (unsigned)s[k]);
                if (k + 1 < len && (k + 1) % 16 != 0) ta_sb_puts(out_asm, ", ");
                if ((k + 1) % 16 == 0 || k + 1 == len) ta_sb_putc(out_asm, '\n');
            }
            ta_sb_puts(out_asm, "    .byte 0\n");
        }
    }

    free(c.fpool);
    free(c.fbits);
    (void)rt_all_float_args_fwd;
    return true;
}
