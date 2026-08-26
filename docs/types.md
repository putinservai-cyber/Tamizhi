# வகை அமைப்பு / Type System

## Representation
Every value is a **64-bit cell**:
- `முழுஎண்` two's complement i64 (arithmetic wraps silently)
- `மிதவை` IEEE-754 binary64; stored as raw bits in cells/slots
- `பூலியன்` 0/1 — printed as `உண்மை` / `பொய்`
- `எழுத்து` Unicode code point (UTF-32 value in a cell)
- `உரை` pointer to `{int64 len; uint8 bytes[len]}` (immutable)

## Rules
| Context | Rule |
|---|---|
| vardecl with annotation | RHS must be assignable to annotation |
| vardecl without | type inferred from RHS; empty literal ⇒ error TA4009 |
| assignment | types must match; `முழுஎண் → மிதவை` auto-widens |
| arithmetic | int∘int→int, float involved→float; string+string→string |
| comparisons | numeric pairs, string/string, bool/bool, char/char |
| conditions | must be `பூலியன்` (TA4002) |
| call args | assignable to parameter types (widening allowed) |
| return | checked against declared/inferred type |

## Inference & recursion
Function bodies are checked lazily at first call site; results memoized.
Unannotated returns are unified across all `திருப்பு` statements
(int+float mix → float). Cycles among unannotated recursive functions
require an explicit `-> type` (TA4013).

## Containers
- `[T]`: contiguous array of T-cells after a length header. Indexing is O(1),
  bounds-checked.
- `{K:V}`: open addressing, FNV-1a (strings) / splitmix (ints) hashing,
  grows at 70% load. Keys: `உரை` or `முழுஎண்`, homogeneous per dict.
  Values: one uniform type per dict (mixed-type literals rejected today;
  see roadmap).
