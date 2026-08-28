# Changelog — v0.2 (bootstrap hardening)

## Fixed (P0)
- **codegen**: emit `.note.GNU-stack` — fixes `ld: missing .note.GNU-stack` warning that broke `make test` (15/17 FAIL via `2>&1` capture) and removes `RWE` executable-stack regression. `readelf -lW` now shows `GNU_STACK RW`. — `src/codegen/codegen.c:644`
- **cli**: `cmd_repl` dynamic `getline()` (was `char line[4096]` truncation), `cmd_run`/`cmd_repl` heap-allocated `exe`/`asmp` paths, `find_helper()` heap-allocated thread-safe (was `static char path[2048]`). — `src/cli/main.c:345,434,733`
- **runtime**: `GC_MALLOC_ATOMIC` for strings (`type 0/1` skip scan), `__builtin_unwind_init()` before `setjmp` for reliable register capture under ASan/LTO. — `src/runtime/tart.c:270,294`

## Audited (PASS)
- Lexer & UTF-8: Tamil 2-3 byte, matra clustering, `ta_utf8_prefix_columns`, shebang `#!/` as comment, `INDENT/DEDENT` correct.
- Parser/AST: no left-recursion, Tamil keywords matched before ident, `ta fmt` canonical.
- Semantic/Type: `{"அருண்":92}`, nested scopes, Tamil identifiers.
- TIR/C11: UTF-8 preserved, `--target=c` escapes via `.byte`, `ta_rt_str_from`.
- Build: `make test` 17 passed, `-Wall -Wextra -Wpedantic -Werror` clean, `PREFIX`, `test-asan` OK.
- Examples: `hello.ta`, `fibonacci.ta`, `collections.ta`, `வணக்கம்.த` all `ta check` PASS.

## Verify
```bash
make clean && make test  # 17 passed, 0 failed
./build/ta build examples/fibonacci.ta -o /tmp/fib && readelf -lW /tmp/fib | grep STACK # RW
```
