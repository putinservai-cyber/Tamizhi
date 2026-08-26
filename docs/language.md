# தமிழி — மொழி அறிமுகம் / Tamizhi Language Reference

தமிழி ஒரு சுயேட்ச்சமான, statically-typed, native-compiled நிரலாக்க மொழி.
Tamizhi is an independent, statically typed, natively compiled programming language.

## 1. கோப்புகள் / Source files

- நீட்டிப்பு / extension: **`.ta`** அல்லது **`.த`**, UTF-8
- Blocks are created by **indentation** (4 spaces or one tab); no braces, no semicolons.
- A first-line shebang (`#!...`) is treated as a comment, so scripts can be
  executed directly via the `tai` runner (Python style) as well as compiled to
  native binaries (C style).
- Identifiers: Tamil letters (U+0B83–U+0BD7), ASCII letters, `_`; digits after the first char.
- `எ.கா.:` `மாறி வணக்கம் = "hello"`, `மாறி total_1 = 0`

## 2. சொற்கள் / Keywords

| தமிழ் | English | |
|---|---|---|
| மாறி | var | mutable binding |
| நிலையான | const | immutable binding |
| செயலி | function | function definition |
| திருப்பு | return | return value |
| என்றால் | if | conditional |
| இல்லையெனில் | else | else / else-if (`இல்லையெனில் என்றால்`) |
| வரை | while | loop |
| ஒவ்வொன்றும் | for-each | iteration |
| இல் | in | separator in for-each |
| நிறுத்து | break | exit loop |
| தொடர் | continue | next iteration |
| உண்மை / பொய் | true / false | booleans |
| வெற்று | null/void | unit type & literal |
| மற்றும் / அல்லது / இல்லை | and / or / not | logic (short-circuit) |

## 3. வகைகள் / Types

| தமிழ் | English | Representation |
|---|---|---|
| முழுஎண் | int | signed 64-bit (two's complement, wrapping) |
| மிதவை | float | IEEE-754 double |
| பூலியன் | bool | 0 / 1 |
| எழுத்து | char | Unicode code point |
| உரை | string | UTF-8, immutable, heap |
| வெற்று | void | unit |
| [T] | list of T | growable array of 8-byte cells |
| {K: V} | dict K→V | open-addressing hash table; K ∈ {உரை, முழுஎண்} |

Type inference: `மாறி x = 10` ⇒ `x: முழுஎண்`. Explicit: `மாறி x: முழுஎண் = 10`.
`int → float` widening is implicit everywhere a float is expected.

## 4. மாறிகள் / Variables

```tamil
மாறி x = 10            # inferred முழுஎண்
நிலையான PI = 3.14       # cannot be reassigned
மாறி l: [முழுஎண்] = []   # empty list needs annotation
x += 5                  # compound: += -= *= /=
```

Assignments to an undeclared name declare it implicitly (type of RHS).
Empty collections require a type annotation (error TA4009 otherwise).

## 5. செயலிகள் / Functions

```tamil
செயலி கூட்டு(a: முழுஎண், b: முழுஎண்) -> முழுஎண்:
    திருப்பு a + b
```
- Parameter annotations are required; return type may be omitted (inferred).
- Recursion requires an explicit return type (TA4013).
- All functions are defined at top level; definitions are visible program-wide.
- If a function named `முதன்மை()` exists it runs after top-level statements.

## 6. Operators (highest → lowest)

`-` unary, `* / %`, `+ -`, `< > <= >=`, `== !=`, `இல்லை`, `மற்றும்`, `அல்லது`.
`+` concatenates உரை. Comparisons yield பூலியன். `மற்றும்/அல்லது` short-circuit.
Integer division truncates toward zero; `%` takes the C sign. Division/modulo by
zero aborts at runtime with a Tamil error.

## 7. Control flow

```tamil
என்றால் x > 10:
    ...
இல்லையெனில் என்றால் x > 5:
    ...
இல்லையெனில்:
    ...

வரை i < 10:
    i += 1

ஒவ்வொன்றும் e இல் list:      # elements
ஒவ்வொன்றும் c இல் text:      # எழுத்துs (Unicode)
ஒவ்வொன்றும் n இல் வரம்பு(5): # 0..4
```

## 8. Collections

```tamil
மாறி nums = [10, 20, 30]
nums[0] = 99
மாறி d = {"key": "value"}
d["new"] = 1
```
Indexing out of bounds / missing keys abort at runtime (exit code 70).

## 9. Entry point

Top-level statements run first (as `ta_top`), then `முதன்மை()` if present,
then the process exits 0.
