#!/usr/bin/env bash
# tests/smoke.sh -- render every chart, poke every loader, check the edges.
# usage: tests/smoke.sh [path-to-charts]
set -uo pipefail

BIN="${1:-./charts}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
ok()   { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad()  { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want [$3] got [$2])"; fi; }

# A chart is drawn with box drawing and block glyphs; count display columns the
# way a terminal does, so a stray wide sequence fails loudly.
wcol() { python3 -c '
import sys,unicodedata
s=sys.stdin.readline().rstrip("\n")
w=0
for ch in s:
    if unicodedata.combining(ch): continue
    w+=2 if unicodedata.east_asian_width(ch) in ("W","F") else 1
print(w)'; }

echo "== basics"
[ -x "$BIN" ] || { echo "no binary at $BIN"; exit 1; }
"$BIN" --version >/dev/null 2>&1;  check "--version exits 0" "$?" "0"
"$BIN" -h >/dev/null 2>&1;         check "-h exits 0" "$?" "0"
"$BIN" --list-types | grep -q pie3d;    check "--list-types names pie3d" "$?" "0"
"$BIN" --list-palettes | grep -q cga;   check "--list-palettes names cga" "$?" "0"

echo "== examples are valid input"
"$BIN" --example csv  > "$TMP/sample.csv"
"$BIN" --example json > "$TMP/sample.json"
"$BIN" --describe "$TMP/sample.csv"  >/dev/null 2>&1; check "example csv parses" "$?" "0"
"$BIN" --describe "$TMP/sample.json" >/dev/null 2>&1; check "example json parses" "$?" "0"

echo "== loaders"
"$BIN" --describe "$ROOT/examples/revenue.csv" > "$TMP/rev.txt"
grep -q "^series:  2" "$TMP/rev.txt"; check "revenue.csv -> 2 series" "$?" "0"
grep -q "^rows:    12" "$TMP/rev.txt"; check "revenue.csv -> 12 rows" "$?" "0"
"$BIN" --describe "$ROOT/examples/traffic.tsv" | grep -q "^series:  3"
check "tab separated -> 3 series" "$?" "0"
"$BIN" --describe "$ROOT/examples/disk.json" | grep -q "^series:  2"
check "json records -> 2 series" "$?" "0"

# json shapes that agents actually emit
cat > "$TMP/shapes.json" <<'EOF'
{ "title": "shapes",
  "labels": ["a","b","c"],
  "series": [ {"name":"one","values":[1,2,3]}, {"name":"two","values":[3,2,1]} ] }
EOF
"$BIN" --describe "$TMP/shapes.json" | grep -q "^series:  2"
check "json labels+series" "$?" "0"

echo '{"Mon":10,"Tue":20,"Wed":5}' > "$TMP/obj.json"
"$BIN" --describe "$TMP/obj.json" | grep -q "^rows:    3"
check "json object of numbers" "$?" "0"

echo '[1,4,9,16,25]' > "$TMP/arr.json"
"$BIN" --describe "$TMP/arr.json" | grep -q "^series:  1"
check "json flat array" "$?" "0"

echo '[{"x":1,"y":10},{"x":2,"y":20}]' > "$TMP/xy.json"
"$BIN" --describe "$TMP/xy.json" | grep -q "^series:  2"
check "json numeric x stays a data series" "$?" "0"
"$BIN" "$TMP/xy.json" -t scatter --xy -w 70 -H 18 --no-color >/dev/null
check "json x/y scatters" "$?" "0"
echo '[{"date":"2025-01","v":3},{"date":"2025-02","v":4}]' > "$TMP/dates.json"
"$BIN" --describe "$TMP/dates.json" | grep -q "2025-01"
check "json text date becomes the label" "$?" "0"

printf 'a,b\n1,2\n3,4\n' > "$TMP/numbers-first.csv"
"$BIN" --describe "$TMP/numbers-first.csv" | grep -q "^rows:    2"
check "all-numeric csv has no header" "$?" "0"

printf 'k,v\n"two, with comma",5\nplain,6\n' > "$TMP/quotes.csv"
"$BIN" --describe "$TMP/quotes.csv" | grep -q "two, with comma"
check "quoted field with comma" "$?" "0"

printf 'a\n1\n\n2\n' > "$TMP/blank.csv"
"$BIN" --describe "$TMP/blank.csv" | grep -q "^rows:    2"
check "blank line skipped" "$?" "0"

printf '# comment\na,b\n1,2\n' > "$TMP/comment.csv"
"$BIN" --describe "$TMP/comment.csv" | grep -q "^series:  2"
check "leading # comment skipped" "$?" "0"

echo "== stdin"
"$BIN" --describe - < "$ROOT/examples/revenue.csv" | grep -q "stdin"
check "reads stdin with -" "$?" "0"
cat "$ROOT/examples/revenue.csv" | "$BIN" -t bar --no-color > "$TMP/pipe.out"
check "pipes csv to a chart" "$?" "0"

echo "== every chart type renders"
for t in bar grouped stacked hbar line area pie pie3d donut scatter hist table; do
  out="$("$BIN" "$ROOT/examples/revenue.csv" -t "$t" -w 100 -H 30 --no-color 2>&1)"
  rc=$?
  if [ $rc -ne 0 ]; then bad "type $t exits 0 (rc=$rc: $out)"; continue; fi
  lines=$(printf '%s\n' "$out" | wc -l)
  if [ "$lines" -ne 30 ]; then bad "type $t honours -H 30 (got $lines)"; continue; fi
  widest=0
  while IFS= read -r l; do
    w=$(printf '%s\n' "$l" | wcol)
    [ "$w" -gt "$widest" ] && widest=$w
  done <<< "$out"
  if [ "$widest" -gt 100 ]; then bad "type $t stays inside -w 100 (got $widest)"; else pass=$((pass+1)); printf '  ok   type %-8s %sx%s\n' "$t" "$widest" "$lines"; fi
done

echo "== glyphs and colour"
"$BIN" "$ROOT/examples/revenue.csv" -t pie3d --ascii -w 80 -H 24 --no-color > "$TMP/ascii.out"
if LC_ALL=C grep -q '[^ -~]' "$TMP/ascii.out"; then bad "--ascii is pure ASCII"; else ok "--ascii is pure ASCII"; fi
"$BIN" "$ROOT/examples/revenue.csv" -t bar -w 80 -H 24 --no-color > "$TMP/nocolor.out"
if grep -q $'\x1b' "$TMP/nocolor.out"; then bad "--no-color emits no escapes"; else ok "--no-color emits no escapes"; fi
"$BIN" "$ROOT/examples/revenue.csv" -t bar -w 80 -H 24 --color > "$TMP/color.out"
if grep -q $'\x1b' "$TMP/color.out"; then ok "--color emits escapes"; else bad "--color emits escapes"; fi
if grep -q $'\x1b\[0m' "$TMP/color.out"; then ok "colour is reset before newline"; else bad "colour is reset before newline"; fi

echo "== pie slices stay distinct without colour"
"$BIN" /tmp/none 2>/dev/null
printf 'cat,val\nA,25\nB,25\nC,25\nD,25\n' > "$TMP/four.csv"
ng=$("$BIN" "$TMP/four.csv" -t pie3d -w 60 -H 18 --no-color | grep -o -E '[█▓▒░]' | sort -u | wc -l)
check "four slices use four shades" "$ng" "4"

echo "== options"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --values -w 90 -H 24 --no-color | grep -q '298'
check "--values prints numbers" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --no-grid -w 90 -H 24 --no-color > "$TMP/nogrid.out"
if grep -q '·' "$TMP/nogrid.out"; then bad "--no-grid removes dots"; else ok "--no-grid removes dots"; fi
"$BIN" "$ROOT/examples/revenue.csv" -t pie3d --explode 0 -w 80 -H 24 --no-color | grep -q '█'
check "--explode 0 works" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --min 0 --max 400 -w 90 -H 24 --no-color | grep -q '400'
check "--max pins the axis" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --frame single -w 90 -H 24 --no-color | grep -q '┌'
check "--frame single draws single lines" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --frame none -w 90 -H 24 --no-color | grep -q '╔'
if [ $? -eq 0 ]; then bad "--frame none drops the frame"; else ok "--frame none drops the frame"; fi
"$BIN" "$ROOT/examples/revenue.csv" -t bar --no-shadow -w 90 -H 24 --no-color > "$TMP/noshadow.out"
check "--no-shadow runs" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t stacked --transpose -w 90 -H 24 --no-color >/dev/null
check "--transpose runs" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t hist --bins 4 -w 90 -H 24 --no-color >/dev/null
check "--bins runs" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --palette cga -w 90 -H 24 --no-color >/dev/null
check "--palette known name" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" --no-header --describe >/dev/null 2>&1
check "--no-header on a headed file drops the names" "$?" "1"
printf '10,20\n1,2\n3,4\n' > "$TMP/numhead.csv"
"$BIN" "$TMP/numhead.csv" --no-header --describe | grep -q "^rows:    3"
check "--no-header keeps every row" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t bar --series-col 2 --describe | grep -q "^series:  1"
check "--series-col keeps one series" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t hbar --labels-col 1 -w 80 -H 20 --no-color >/dev/null
check "--labels-col runs" "$?" "0"
"$BIN" "$ROOT/examples/traffic.tsv" -t bar --delim tab -w 80 -H 20 --no-color >/dev/null
check "--delim tab" "$?" "0"

echo "== tiles"
"$BIN" "$ROOT/examples/revenue.csv" "$ROOT/examples/quarterly.csv" "$ROOT/examples/disk.json" \
       "$ROOT/examples/traffic.tsv" --tile -t bar,line,pie3d,stacked -w 120 -H 40 --no-color > "$TMP/tile.out"
check "--tile with 4 files" "$?" "0"
check "--tile fills the height" "$(wc -l < "$TMP/tile.out")" "40"
check "--tile names all four" "$(grep -o -E 'revenue\.csv|quarterly\.csv|disk\.json|traffic\.tsv' "$TMP/tile.out" | sort -u | wc -l)" "4"

echo "== a directory of data"
"$BIN" -i --help >/dev/null 2>&1; check "-i --help still works" "$?" "0"
"$BIN" "$ROOT/examples" -t bar -w 80 -H 20 --no-color >/dev/null 2>&1
check "directory expands to its data files" "$?" "0"

echo "== failures are clean"
"$BIN" /nope/nope.csv 2>"$TMP/err1"; check "missing file exits 1" "$?" "1"
grep -q "cannot open" "$TMP/err1"; check "missing file says why" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" --bogus 2>"$TMP/err2"; check "unknown option exits 1" "$?" "1"
grep -q "unknown option" "$TMP/err2"; check "unknown option says why" "$?" "0"
"$BIN" "$ROOT/examples/revenue.csv" -t nope 2>"$TMP/err3"; check "unknown type exits 1" "$?" "1"
echo '{ bad json' > "$TMP/bad.json"
"$BIN" "$TMP/bad.json" 2>"$TMP/err4"; check "bad json exits 1" "$?" "1"
grep -q "json:" "$TMP/err4"; check "bad json names the place" "$?" "0"
printf 'a,b\n,x\n' > "$TMP/nonum.csv"
"$BIN" "$TMP/nonum.csv" 2>"$TMP/err5"; check "no numeric column exits 1" "$?" "1"
grep -q "no numeric" "$TMP/err5"; check "no numeric column says why" "$?" "0"
: > "$TMP/empty.csv"
"$BIN" "$TMP/empty.csv" 2>"$TMP/err6"; check "empty file exits 1" "$?" "1"
"$BIN" -i - < "$ROOT/examples/revenue.csv" >/dev/null 2>"$TMP/err7"
check "-i without a terminal exits 2" "$?" "2"
"$BIN" "$ROOT/examples/revenue.csv" --watch >/dev/null 2>"$TMP/err8"
check "--watch without a terminal exits 2" "$?" "2"
"$BIN" -w 5 -H 3 "$ROOT/examples/revenue.csv" >/dev/null 2>&1
check "tiny terminal does not crash" "$?" "0"

echo "== colour is dropped when stdout is a file"
"$BIN" "$ROOT/examples/revenue.csv" -t bar > "$TMP/redirected.out"
if grep -q $'\x1b' "$TMP/redirected.out"; then bad "no escapes when redirecting"; else ok "no escapes when redirecting"; fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
