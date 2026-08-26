#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.."
TA=./build/ta
pass=0; fail=0
for prog in tests/programs/*.ta; do
    base="${prog%.ta}"
    exp="${base}.out"
    [ -f "$exp" ] || continue
    infile="${base}.in"
    if [ -f "$infile" ]; then
        got=$($TA run "$prog" < "$infile" 2>&1)
    else
        got=$($TA run "$prog" < /dev/null 2>&1)
    fi
    if [ "$got" == "$(cat "$exp")" ]; then
        pass=$((pass+1)); echo "PASS $(basename "$prog")"
    else
        fail=$((fail+1)); echo "FAIL $(basename "$prog")"
        echo "--- expected ---"; cat "$exp"
        echo "--- got ---"; echo "$got"
    fi
done
tmpd=$(mktemp -d)
trap 'rm -rf "$tmpd"' EXIT

printf '%s\n' '#!/usr/bin/env -S tai' 'அச்சிடு("shebang ok")' > "$tmpd/she.ta"
got=$($TA run "$tmpd/she.ta" 2>&1)
if [ "$got" == "shebang ok" ]; then pass=$((pass+1)); echo "PASS shebang-line"; else fail=$((fail+1)); echo "FAIL shebang-line: $got"; fi

cp tests/programs/mvp.ta "$tmpd/தமிழி.த"
got=$("$TA" run "$tmpd/தமிழி.த" 2>&1)
if [ "$got" == "$(cat tests/programs/mvp.out)" ]; then pass=$((pass+1)); echo "PASS unicode-ext"; else fail=$((fail+1)); echo "FAIL unicode-ext: $got"; fi

if [ -x build/tai ]; then
    cp build/tai "$tmpd/tai"
    printf '%s\n' "#!$tmpd/tai" 'அச்சிடு("direct exec")' > "$tmpd/direct.ta"
    chmod +x "$tmpd/direct.ta"
    got=$("$tmpd/direct.ta" 2>&1)
    if [ "$got" == "direct exec" ]; then pass=$((pass+1)); echo "PASS direct-shebang-exec"; else fail=$((fail+1)); echo "FAIL direct-shebang-exec: $got"; fi
fi

$TA doctor >/dev/null 2>&1; drc=$?
if [ $drc -le 1 ]; then pass=$((pass+1)); echo "PASS doctor"; else fail=$((fail+1)); echo "FAIL doctor exit=$drc"; fi

echo "e2e: $pass passed, $fail failed"
exit $([ $fail -eq 0 ] && echo 0 || echo 1)
