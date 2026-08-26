# வரைபடம் / Roadmap

## Done (v0.1.0 — C bootstrap)
- [x] UTF-8 lexer with INDENT/DEDENT, Tamil identifiers, comments, escapes
- [x] Recursive-descent parser → AST; `ta fmt`
- [x] Scopes/symbols; hoisted functions; slots
- [x] Static types + inference; int→float widening; lazy recursive checking
- [x] TIR; x86-64 codegen (SysV ABI); string pool; PIE-safe asm
- [x] Runtime: strings/lists/dicts/print/input/range/math
- [x] CLI: build/run/check/repl/fmt/version/help
- [x] Unit tests per stage + end-to-end suite (`make test`)
- [x] Bilingual documentation

## Next (bootstrap hardening)
- [ ] String methods beyond வெட்டு/இணை (find, split, join, trim)
- [ ] Dict iteration & key views; list push/pop builtins
- [ ] Heterogeneous dict values via tagged cells (spec §2.4 example)
- [ ] Compound-assignment on dict values; chained comparisons?
- [ ] Better float formatting edge cases; locale independence
- [ ] Compiler: free-all-paths memory audit under ASAN in CI

## Self-hosting path (§8 of spec)
1. Phase 2 target subset: enough to express lexer+parser (strings, structs/tuples,
   enums/variants, first-class function refs or vtables).
2. Write `tzc.ta` (lexer/parser/typecheck) compiled by the C bootstrap.
3. Phase 3: `tzc.ta` compiles itself; diff outputs vs bootstrap.
4. Phase 4: freeze C compiler as historical artifact.

## Eventually
- Structs (`அமைப்பு`) and user modules/imports (`பயன்படுத்து`)
- Ownership/borrowing or RC for heap objects replacing leak-by-design
- More targets: ARM64 backend behind the same TIR
