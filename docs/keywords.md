# Tamizhi Keywords & Builtins (Tamil → English)

This document maps every reserved keyword and built-in function in Tamizhi to
its English meaning and role. Use it when reading or translating Tamizhi source.

## Declarations

| Tamil        | English (meaning) | Role                          |
|--------------|-------------------|-------------------------------|
| `மாறி`        | variable          | declare a mutable variable    |
| `நிலையான`     | constant          | declare an immutable constant |
| `செயலி`       | function          | declare a function            |

## Control flow

| Tamil          | English (meaning) | Role                                 |
|----------------|-------------------|--------------------------------------|
| `திருப்பு`       | return            | return from a function               |
| `என்றால்`        | if                | conditional branch                   |
| `இல்லையெனில்`     | else              | alternative branch                   |
| `வரை`          | while             | while loop                           |
| `ஒவ்வொன்றும்`     | each one          | for-each loop                        |
| `இல்`          | in                | membership / iteration (`… இல் …`)    |
| `நிறுத்து`       | stop              | `break`                              |
| `தொடர்`         | continue          | `continue`                           |

## Literals & operators

| Tamil     | English (meaning) | Role                          |
|-----------|-------------------|-------------------------------|
| `உண்மை`    | truth             | boolean `true`                |
| `பொய்`     | false             | boolean `false`               |
| `வெற்று`   | empty / null      | `null` value                  |
| `மற்றும்`  | and               | logical AND                   |
| `அல்லது`   | or                | logical OR                    |
| `இல்லை`    | not               | logical NOT                   |

Arithmetic/comparison operators are ASCII: `+ - * / % == != < > <= >=`.

## Types

| Tamil       | English (meaning) | Maps to            |
|-------------|-------------------|--------------------|
| `முழுஎண்`    | whole number      | `int64` (integer)  |
| `மிதவை`     | floating          | `double` (float)   |
| `பூலியன்`    | boolean           | boolean            |
| `எழுத்து`    | letter / character| Unicode code point |
| `உரை`       | text              | string             |
| `வெற்று`     | empty / void      | `void`             |
| `[T]`       | —                 | list of `T`        |
| `{K: V}`    | —                 | dictionary `K → V` |

## Built-in functions

| Tamil          | English (meaning) | Purpose                                            |
|----------------|-------------------|----------------------------------------------------|
| `அச்சிடு`        | print             | print value(s)                                     |
| `உள்ளீடு`        | input             | read a line from stdin                             |
| `நீளம்`         | length            | length of string / list / dict                     |
| `வரம்பு`        | range             | integer range (for loops)                          |
| `பிரி`          | split             | split a string by a separator                      |
| `இணைப்பு`        | join              | join a list of strings                             |
| `நறுக்கு`        | trim              | strip whitespace                                    |
| `மாற்று`         | replace           | string replace                                      |
| `மேலெழுத்து`      | upper-case        | uppercase a string                                  |
| `சிறியெழுத்து`     | lower-case        | lowercase a string                                  |
| `துவங்கிறதா`      | starts-with       | string prefix test                                  |
| `முடிவதா`        | ends-with         | string suffix test                                  |
| `சேர்`          | add               | append to a list                                    |
| `நீக்கு`         | remove            | pop from a list                                     |
| `விசைகள்`        | keys              | dictionary keys                                     |
| `உருப்படிகள்`      | items             | dictionary key/value pairs                          |
| `கண்டுபிடி`       | find              | find substring index                                |
| `எண்ணிக்கை`       | count             | count substring occurrences                         |
| `உறுதிப்படுத்து`    | assert            | assert a condition                                  |

The standard library also exposes math helpers under the `கணிதம்` module
(e.g. `வர்க்கமூலம்` sqrt, `தள்ளை` floor, `அடுக்கு` pow, `தனித்த` abs). See
`docs/standard-library.md`.
