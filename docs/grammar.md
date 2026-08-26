# இலக்கணம் / Formal Grammar (EBNF)

```
program        = { statement } ;
statement      = funcdef | vardecl | assign | exprstmt | ifstmt
               | whilestmt | foreach | returnstmt | breakstmt
               | continuestmt | NEWLINE ;

funcdef        = "செயலி" IDENT "(" [ param { "," param } [","] ] ")"
                 [ "->" type ] ":" suite ;
param          = IDENT [ ":" type ] ;
vardecl        = ( "மாறி" | "நிலையான" ) IDENT [ ":" type ] "=" expr ;
assign         = lvalue ( "=" | "+=" | "-=" | "*=" | "/=" ) expr ;
lvalue         = IDENT | postfix "[" expr "]" ;
exprstmt       = expr ;
ifstmt         = "என்றால்" expr ":" suite
                 { "இல்லையெனில்" "என்றால்" expr ":" suite }
                 [ "இல்லையெனில்" ":" suite ] ;
whilestmt      = "வரை" expr ":" suite ;
foreach        = "ஒவ்வொன்றும்" IDENT "இல்" expr ":" suite ;
returnstmt     = "திருப்பு" [ expr ] ;
breakstmt      = "நிறுத்து" ;
continuestmt   = "தொடர்" ;

suite          = NEWLINE INDENT { statement } DEDENT | simple_statement ;

type           = IDENT | "[" type "]" | "{" type ":" type "}" ;

expr           = or_expr ;
or_expr        = and_expr { "அல்லது" and_expr } ;
and_expr       = not_expr { "மற்றும்" not_expr } ;
not_expr       = "இல்லை" not_expr | comparison ;
comparison     = additive [ ( "==" | "!=" | "<" | ">" | "<=" | ">=" ) additive ] ;
additive       = term { ( "+" | "-" ) term } ;
term           = unary { ( "*" | "/" | "%" ) unary } ;
unary          = "-" unary | "+" unary | postfix ;
postfix        = primary { "(" [ arglist ] ")" | "[" expr "]"
                           | "." IDENT } ;
arglist        = expr { "," expr } [","] ;
primary        = INT | FLOAT | STRING | CHAR | "உண்மை" | "பொய்"
               | "வெற்று" | IDENT | "(" expr ")"
               | "[" [ elem_list ] "]"
               | "{" [ pair_list ] "}" ;
elem_list      = expr { "," expr } [","] ;
pair_list      = expr ":" expr { "," expr ":" expr } [","] ;
```

## Lexical rules

```
INDENT / DEDENT : generated at line start from space/tab width (tab = next multiple of 4)
NEWLINE         : significant only at paren-depth 0
COMMENT         = "#" { any - newline }
STRING          = '"' { escape | utf8_char } '"'
CHAR            = "'" ( escape | single utf8_char ) "'"
escape          = \n \t \r \0 \" \' \\  |  "\u{" hex{1,6} "}"
INT             = digit{1,} | "0x" hexdigit{1,} | "0b" bindigit{1,}   ('_' separators allowed)
FLOAT           = digit{1,} "." digit{1,} [ exponent ]
exponent        = ("e"|"E") ["+"|"-"] digit{1,}
IDENT           = ident_start { ident_cont }
ident_start     = ASCII letter | "_" | Tamil letter (U+0B83..U+0BB9, no matras)
ident_cont      = ident_start | digit | Tamil matra/digit (U+0BBE..U+0BEF)
```
