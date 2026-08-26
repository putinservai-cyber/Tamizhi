# நினைவக மாதிரி / Memory Model (bootstrap)

## Compile time
The compiler allocates through checked wrappers (`ta_xmalloc*`) and aborts
with TA5001 on exhaustion. AST/TIR/symbols live for the whole compilation and
are freed by explicit `*_free` walkers where practical; freeing before exit
is best-effort, not a correctness requirement.

## Runtime program model (bootstrap phase)
- Every value cell is 8 bytes; scalars inline, floats as raw bits, heap
  objects as pointers.
- `உரை`, lists, dicts are `calloc`'d and **never freed** during program run:
  bootstrap programs rely on process exit to reclaim memory. This keeps the
  generated assembly free of ownership bugs; a real allocator/GC is planned
  for the self-hosted phase (roadmap.md).
- Strings are immutable: every concatenation/subscript produces a new object.
- Lists store elements inline after `{i64 len}`; dicts store keys/values in
  parallel arrays with a state map, rehashing on growth.
- Out-of-range indexing, missing dict keys, division by zero abort with a
  Tamil message and exit status 70.
- Stack discipline: all locals/temps live in the fixed frame; no alloca;
  recursion depth bounded only by OS stack.
