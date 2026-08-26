# தரம்-நூலகம் / Standard Library

All APIs are Tamil-first. These are compiler intrinsics lowered to runtime
calls (bootstrap phase).

## Core builtins (global)
| செயலி | Signature | Notes |
|---|---|---|
| அச்சிடு | (…) -> வெற்று | prints args space-separated + newline |
| உள்ளீடு | () -> உரை | reads one line from stdin (strips newline) |
| நீளம் | (உரை\\|[T]) -> முழுஎண் | byte-length for strings today; element count for lists |
| வரம்பு | (n) \\| (a,b) \\| (a,b,step) -> [முழுஎண்] | half-open range |

## கணிதம் module
| செயலி | Signature | Notes |
|---|---|---|
| கணிதம்.தனிமதிப்பு | (முழுஎண்)->முழுஎண் \\| (மிதவை)->மிதவை | absolute value |
| கணிதம்.முழுமதிப்பு | (மிதவை)->முழுஎண் | floor (toward −∞) |
| கணிதம்.வர்க்கமூலம் | (மிதவை)->மிதவை | square root |
| கணிதம்.சக்தி | (i,i)->i \\| numeric->float | power |

## உரை module
| செயலி | Signature | Notes |
|---|---|---|
| உரை.இணை | (உரை,உரை)->உரை | same as `+` |
| உரை.வெட்டு | (s,start,end)->உரை | byte-indexed slice, clamped |

## Printing behaviour
- floats print shortest-roundtrip (`3.14159`, not `3.1415899...`)
- bools print `உண்மை` / `பொய்`
- chars print as their UTF-8 encoding
