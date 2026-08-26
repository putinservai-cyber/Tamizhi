# பிழைக் குறியீடுகள் / Error Codes

Format: `பிழை TA<code> [file:line:col]: message` + source line + caret + hint.

## 1xxx — Lexical
| Code | Meaning |
|---|---|
| TA1001 | invalid/unknown character |
| TA1002 | unterminated string |
| TA1003 | indentation does not match any outer level |
| TA1004 | malformed number literal |
| TA1005 | invalid escape sequence |
| TA1006 | char literal must hold exactly one character |

## 2xxx — Syntax (parser)
| Code | Meaning |
|---|---|
| TA2001 | unexpected token (expected … got …) |
| TA2002 | expected an expression |
| TA2003 | invalid assignment target |
| TA2004 | nested function definitions are not allowed |

## 3xxx — Semantic
| Code | Meaning |
|---|---|
| TA3001 | undefined name (with did-you-mean when close) |
| TA3002 | duplicate declaration in the same scope |
| TA3003 | assignment to constant / non-variable |
| TA3004 | நிறுத்து outside a loop |
| TA3005 | தொடர் outside a loop |
| TA3006 | திருப்பு outside a function |
| TA3007 | unknown type name in annotation |
| TA3008 | module/member misuse |
| TA3010 | use of variable before its declaration point |

## 4xxx — Type errors
| Code | Meaning |
|---|---|
| TA4001 | operator not defined for operand types |
| TA4002 | condition must be பூலியன் |
| TA4003 | wrong argument count |
| TA4004 | argument type mismatch |
| TA4005 | calling a non-function / function used as value |
| TA4006 | indexing unsupported for this type |
| TA4007 | index must be முழுஎண் |
| TA4008 | dictionary key type mismatch |
| TA4009 | cannot infer type of empty collection — add annotation |
| TA4010 | return type mismatch |
| TA4012 | assignment type mismatch |
| TA4013 | recursive function needs explicit return type |
| TA4014 | iterating this type is not supported |
| TA4015 | parameter missing a type annotation |

## 5xxx — Internal (compiler bugs)
TA5001 out of memory · TA5002 unimplemented construct · TA5003 invariant violation

## 6xxx — Environment / IO
| Code | Meaning |
|---|---|
| TA6001 | cannot read source file |
| TA6002 | cannot write output |
| TA6003 | C toolchain (`cc`) failed or missing |
| TA6004 | runtime object `tart.o` not found (set `TA_RT_OBJ`) |

## Runtime aborts (exit code 70)
- division/modulo by zero
- index out of bounds (message shows index and length)
- dictionary key not found
- out of memory
