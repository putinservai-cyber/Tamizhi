# தமிழி / Tamizhi

**தமிழி** is an independent, statically typed, natively compiled programming
language with Tamil keywords and Python-like readability. This repository
contains the complete C bootstrap compiler: lexer → parser → semantic analysis
→ type checker → TIR → x86-64 code generator, plus the runtime and CLI.

No Python. No transpiling. Real native executables from Tamil source.

## Quick start

```bash
make              # builds build/ta (compiler), tart.o (runtime) and the tai runner
make test         # unit tests + end-to-end suite
sudo make install PREFIX=/usr/local   # or PREFIX=$HOME/.local
```

Files use **`.ta`** or **`.த`**.

### Two ways to execute — C-style *and* Python-style

```bash
# like C: compile once to a real native binary, then run it anywhere
./build/ta build hello.ta -o hello && ./hello

# like Python: run the script directly
./build/ta run hello.த
```

Scripting mode also supports shebangs. After `make install`, `tai` is on PATH:

```tamil
#!/usr/bin/env -S tai
அச்சிடு("வணக்கம் உலகம்")
```

```bash
chmod +x hello.த && ./hello.த
```
(Unix kernels reject spaces in `#!` interpreter paths; keep your tree out of
directories like `New Folder` for direct execution, or use `tai file.த`.)

## Commands

```
ta build <file.ta> [-o out]   compile to a native executable (+ .s assembly)
ta run   <file.ta>            compile to a temp dir and execute
ta check <file.ta>            lex/parse/semantic/type-check only
ta repl                       interactive session (:help inside)
ta fmt   <file.ta>            print canonically formatted source
ta doctor                      சூழல் பரிசோதனை
ta version / ta help
```

## The language in 30 seconds

```tamil
செயலி ஃபிபொ(n: முழுஎண்) -> முழுஎண்:
    என்றால் n <= 1:
        திருப்பு n
    திருப்பு ஃபிபொ(n - 1) + ஃபிபொ(n - 2)

அச்சிடு("fib(10) =", ஃபிபொ(10))

மாறி மதிப்பெண் = {"அருண்": 92, "கவிதா": 88}
அச்சிடு(மதிப்பெண்["அருண்"])

ஒவ்வொன்றும் i இல் வரம்பு(3):
    அச்சிடு("i =", i)
```

## Layout

```
include/    public headers (one per stage)
src/
  common/   diagnostics, UTF-8, buffers, type objects
  lexer/    UTF-8 + INDENT/DEDENT
  parser/   recursive descent
  ast/      AST + formatter
  semantic/ scopes & symbols
  typecheck inference & checking
  ir/       Tamizhi IR (TIR)
  codegen/  x86-64 Intel-syntax emission
  runtime/  tart.c — linked into every program
  cli/      driver commands
tests/      per-stage unit tests + e2e harness (make test)
docs/       language.md grammar.md types.md compiler.md memory.md
            standard-library.md errors.md roadmap.md troubleshooting.md
examples/   hello.ta fibonacci.ta collections.ta
```

## Terminal display

Tamil output needs a UTF-8 locale and a font with Tamil glyphs:

```bash
ta doctor            # diagnoses your terminal + fonts
ta setup-konsole     # one-shot fix inside KDE Konsole
```

See `docs/troubleshooting.md`.

## Status

v0.1.1 — bootstrap milestone reached: the spec's MVP (`mvp.ta` prints `30`)
and the success-criteria program both compile to native binaries.
Self-hosting plan: docs/roadmap.md.

Requirements: Linux x86-64, `cc` or `gcc`, GNU make.
