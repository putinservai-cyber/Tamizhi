# தொகுப்பி கட்டமைப்பு / Compiler Architecture

```
.ta / .த source ─▶ Lexer ─▶ Parser ─▶ Semantic ─▶ TypeCheck ─▶ TIR ─▶ CodeGen ─▶ .s
              (1xxx)   (2xxx)     (3xxx)       (4xxx)              (5xxx)
                                                        runtime: tart.o + libc (-lm)
```

Every stage is a separate module under `src/` with a header in `include/`.
Stages are gated: if stage *n* reports diagnostics, stage *n+1* does not run.

## 1. Lexer (`src/lexer/`)
UTF-8 aware; decodes codepoints, classifies Tamil identifiers, emits layout
tokens `INDENT`/`DEDENT` using an indent-stack algorithm at paren-depth 0.
Blank and comment-only lines produce no tokens. Emits codes 1xxx.

## 2. Parser (`src/parser/`, AST in `src/ast/`)
Recursive descent, one function per precedence level. Builds the typed-shape
AST in `include/ta_ast.h`. Error recovery: skip to NEWLINE per statement,
capped at 50 diagnostics. Sets `incomplete` when a block is left open
(used by the REPL). The AST printer doubles as `ta fmt`.

## 3. Semantic analyzer (`src/semantic/`)
- Builds scope chain: globals (with builtins & modules `கணிதம்`, `உரை`),
  one scope per function, child scopes per block / for-each.
- Hoists top-level functions first, then walks statements.
- Resolves every identifier to a `TaSymbol*` attached to the AST node;
  resolves type annotations to canonical `TaType*`; assigns frame **slots**
  (params first, then locals in declaration order).
- Checks: undefined names (with did-you-mean), duplicates, const assignment,
  break/continue placement, return placement, module member access.

## 4. Type checker (`src/typecheck/`)
Annotates every expression with `const TaType*`. Implements inference
(unification of list/dict literals and return types), implicit int→float
widening, overload selection for `கணிதம்` builtins, arity/arg checks.
Lazy function checking with three states (unchecked/checking/checked).

## 5. TIR (`src/ir/`)
Flat instruction list per function: CONST/LOAD/STORE/BINOP/NEG/NOT/
CONV_I2F/CONV_F2I/JMP/JZ/LABEL/CALL(user)/RT(runtime)/RET/LIST_NEW/
IDX_GET/LEN/STR_AT/DEREF. Operands are temps or slots — everything lives in
stack slots, so register allocation is trivial and correct. Short-circuit
logic and loops lower to explicit labels; for-each lowers to index loop.
String pool is deduplicated here.

## 6. Code generator (`src/codegen/`)
Emits x86-64 **Intel-syntax** assembly (PIE-clean: RIP-relative addressing,
PLT-safe calls).
- Frame: `[rbp - 8*(slot+1)]`; size rounded to 16 bytes → stack aligned at calls.
- SysV ABI: int-like args in rdi,rsi,rdx,rcx,r8,r9 (+ reverse-order pushes),
  floats in xmm0..7; results via rax/xmm0.
- Entry wrapper `main`: calls `ta_top`, then `ta_fn_N` of `முதன்மை` if present.
- Comparisons use setcc; float ordering uses reversed-operand comisd tricks
  that are NaN-safe; equality uses sete+setnp.
- Division/mod by zero guards call `ta_rt_abort_div_zero`.

## 7. Runtime (`src/runtime/tart.c`)
Strings/lists/dicts on malloc'd memory (no GC; see memory.md), printing,
input, ranges, math builtins. Linked statically as `build/tart.o` with `-lm`.

## 8. Driver (`src/cli/main.c`)
`ta build/run/check/repl/fmt/version/help`. Locates `tart.o` relative to the
executable (or `$TA_RT_OBJ`), invokes `cc out.s tart.o -lm -o out`,
`run` builds into a mkdtemp dir, executes, propagates the exit code.
