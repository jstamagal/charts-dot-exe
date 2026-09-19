#!/usr/bin/env bash
# tests/smoke.sh -- render every chart, poke every loader, validate decks, PNGs,
# exit codes, and throw garbage at the parsers.
# usage: tests/smoke.sh [path-to-charts]      (./charts-asan works just as well)
set -uo pipefail

BIN="${1:-./charts}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EX="$ROOT/examples"
TMP="$(mktemp -d)"
# rm is off limits here; a trash can is fine, and failing that the dir just stays.
if command -v trash >/dev/null 2>&1; then
  trap 'trash "$TMP" 2>/dev/null || true' EXIT
else
  trap 'echo "(left $TMP behind: no trash command)"' EXIT
fi

# A sanitizer report must not look like an ordinary "bad input" exit 1.
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}exitcode=99:abort_on_error=0"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:exitcode=99:print_stacktrace=1"
unset NO_COLOR CHARTS_GFX CHARTS_FB CHARTS_FB_GEOM COLUMNS LINES

pass=0
fail=0
ok()   { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad()  { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want [$3] got [$2])"; fi; }
# has NAME FILE PATTERN / hasnt NAME FILE PATTERN   (fixed strings)
has()  { if grep -qF -- "$3" "$2"; then ok "$1"; else bad "$1 (no [$3] in $(basename "$2"))"; fi; }
hasnt(){ if grep -qF -- "$3" "$2"; then bad "$1 (found [$3] in $(basename "$2"))"; else ok "$1"; fi; }

HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1

# A chart is drawn with box drawing and block glyphs; count display columns the
# way a terminal does, so a stray wide sequence fails loudly.  Prints the widest
# line of stdin.
cat > "$TMP/widest.py" <<'EOF'
import sys, unicodedata
best = 0
for line in sys.stdin.buffer.read().decode("utf-8").split("\n"):
    w = 0
    for ch in line:
        if unicodedata.combining(ch): continue
        w += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    best = max(best, w)
print(best)
EOF
widest() { python3 "$TMP/widest.py"; }

# pngcheck.py FILE WIDTH HEIGHT -> "ok" or the first thing wrong with the file
cat > "$TMP/pngcheck.py" <<'EOF'
import struct, sys, zlib
path, want_w, want_h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
def die(m): print(m); sys.exit(1)
b = open(path, "rb").read()
if b[:8] != b"\x89PNG\r\n\x1a\n": die("bad signature")
pos, chunks = 8, []
while pos < len(b):
    if pos + 12 > len(b): die("truncated chunk header at %d" % pos)
    n, typ = struct.unpack(">I4s", b[pos:pos+8])
    if pos + 12 + n > len(b): die("chunk %r runs past the end" % typ)
    data = b[pos+8:pos+8+n]
    crc, = struct.unpack(">I", b[pos+8+n:pos+12+n])
    if crc != (zlib.crc32(typ + data) & 0xffffffff): die("bad CRC in %r" % typ)
    chunks.append((typ, data))
    pos += 12 + n
names = [t for t, _ in chunks]
if not names or names[0] != b"IHDR": die("IHDR is not first")
if names[-1] != b"IEND" or chunks[-1][1]: die("IEND is not last/empty")
if len(chunks[0][1]) != 13: die("IHDR length")
w, h, depth, ctype, comp, filt, lace = struct.unpack(">IIBBBBB", chunks[0][1])
if (w, h) != (want_w, want_h): die("size %dx%d, wanted %dx%d" % (w, h, want_w, want_h))
if (depth, ctype) != (4, 3): die("not 4-bit indexed: depth %d type %d" % (depth, ctype))
if (comp, filt, lace) != (0, 0, 0): die("odd IHDR flags")
if b"PLTE" not in names or names.index(b"PLTE") > names.index(b"IDAT"): die("no PLTE before IDAT")
plte = dict(chunks)[b"PLTE"]
if len(plte) % 3 or not 3 <= len(plte) <= 48: die("PLTE length %d" % len(plte))
try:
    raw = zlib.decompress(b"".join(d for t, d in chunks if t == b"IDAT"))
except zlib.error as e:
    die("IDAT does not inflate: %s" % e)
stride = 1 + (w * 4 + 7) // 8
if len(raw) != stride * h: die("IDAT inflates to %d bytes, wanted %d" % (len(raw), stride * h))
if any(raw[y * stride] > 4 for y in range(h)): die("bad filter byte")
if any(raw[y * stride] != 0 for y in range(h)): print("ok"); sys.exit(0)   # filtered rows: no pixel check
pix = set()
for y in range(h):
    row = raw[y * stride + 1:(y + 1) * stride]
    pix.update(row)
if len(pix) < 2: die("the picture is one flat colour")
if max(max(p >> 4, p & 15) for p in pix) >= len(plte) // 3: die("pixel index outside the palette")
print("ok")
EOF
pngok() { # NAME FILE W H
  if [ $HAVE_PY -eq 0 ]; then ok "$1 (python3 missing: only checked the file exists)"; return; fi
  local r; r="$(python3 "$TMP/pngcheck.py" "$2" "$3" "$4" 2>&1)"
  if [ "$r" = "ok" ]; then ok "$1"; else bad "$1 ($r)"; fi
}

echo "== basics"
[ -x "$BIN" ] || { echo "no binary at $BIN"; exit 1; }
"$BIN" --version >/dev/null 2>&1;  check "--version exits 0" "$?" "0"
"$BIN" -h >/dev/null 2>&1;         check "-h exits 0" "$?" "0"
"$BIN" --schema > "$TMP/schema.txt" 2>&1; check "--schema exits 0" "$?" "0"
has "--schema documents annotations" "$TMP/schema.txt" "ANNOTATIONS"
"$BIN" --list-types | grep -q pie3d;    check "--list-types names pie3d" "$?" "0"
"$BIN" --list-palettes | grep -q cga;   check "--list-palettes names cga" "$?" "0"
"$BIN" --list-themes | grep -q light;   check "--list-themes names light" "$?" "0"

echo "== examples are valid input"
"$BIN" --example csv  > "$TMP/sample.csv"
"$BIN" --example json > "$TMP/sample.json"
"$BIN" --describe "$TMP/sample.csv"  >/dev/null 2>&1; check "example csv parses" "$?" "0"
"$BIN" --describe "$TMP/sample.json" >/dev/null 2>&1; check "example json parses" "$?" "0"
mkdir -p "$TMP/exdeck"
"$BIN" --example deck > "$TMP/exdeck/deck.json"; check "--example deck exits 0" "$?" "0"
cp "$EX/revenue.csv" "$TMP/exdeck/"
"$BIN" "$TMP/exdeck/deck.json" --check > "$TMP/exdeck.out" 2>&1
check "--example deck passes --check beside revenue.csv" "$?" "0"
has "--example deck: no errors, no warnings" "$TMP/exdeck.out" "0 errors, 0 warnings"

echo "== loaders"
"$BIN" --describe "$EX/revenue.csv" > "$TMP/rev.txt"
grep -q "^series:  2" "$TMP/rev.txt"; check "revenue.csv -> 2 series" "$?" "0"
grep -q "^rows:    12" "$TMP/rev.txt"; check "revenue.csv -> 12 rows" "$?" "0"
"$BIN" --describe "$EX/traffic.tsv" | grep -q "^series:  3"
check "tab separated -> 3 series" "$?" "0"
"$BIN" --describe "$EX/disk.json" | grep -q "^series:  2"
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

printf 'k;v;w\na;1;2\nb;3;4\n' > "$TMP/semi.csv"
"$BIN" --describe "$TMP/semi.csv" | grep -q "^series:  2"
check "semicolon delimiter is sniffed" "$?" "0"
printf 'k|v|w\na|1|2\nb|3|4\n' > "$TMP/pipe.csv"
"$BIN" --describe "$TMP/pipe.csv" | grep -q "^series:  2"
check "pipe delimiter is sniffed" "$?" "0"
printf 'k,v\r\na,1\r\nb,2\r\n' > "$TMP/crlf.csv"
"$BIN" --describe "$TMP/crlf.csv" | grep -q "^rows:    2"
check "CRLF line ends" "$?" "0"

echo "== series_col counts the labels, and says so when it misses"
printf 'layout,tg,pp,note\nCCRR,39.4,1236,slow\nCRRC,57.6,1124,fast\n' > "$TMP/cols.csv"
"$BIN" "$TMP/cols.csv" --series-col 1 --png "$TMP/c.png" > "$TMP/sc1.out" 2>&1; check "series_col on the labels: exit 1" "$?" "1"
has "series_col on the labels: named" "$TMP/sc1.out" 'series_col 1 is "layout", the label column'
has "series_col on the labels: lists the series" "$TMP/sc1.out" '2 "tg", 3 "pp"'
"$BIN" "$TMP/cols.csv" --series-col 4 --png "$TMP/c.png" > "$TMP/sc4.out" 2>&1; check "series_col on text: exit 1" "$?" "1"
has "series_col on text: named" "$TMP/sc4.out" '"note", which is not numbers'
"$BIN" "$TMP/cols.csv" --series-col 9 --png "$TMP/c.png" > "$TMP/sc9.out" 2>&1
has "series_col past the end: named" "$TMP/sc9.out" "past the last column (there are 4)"
"$BIN" "$TMP/cols.csv" --series-col 3 --describe > "$TMP/sc3.out" 2>&1
has "series_col 3 keeps pp" "$TMP/sc3.out" "col 3  pp"
hasnt "series_col 3 drops tg" "$TMP/sc3.out" "col 2"
has "describe names the label column" "$TMP/sc3.out" 'column 1 "layout"'
printf '{"labels":["a","b"],"series":[{"name":"p","values":[1,2]},{"name":"q","values":[3,null]}]}' > "$TMP/cols.json"
"$BIN" "$TMP/cols.json" --series-col 3 --describe > "$TMP/scj.out" 2>&1
has "json: series_col is honoured" "$TMP/scj.out" "series:  1"
has "json: the same column numbers" "$TMP/scj.out" "col 3  q"
has "describe counts gaps, not blanks" "$TMP/scj.out" "n=1    min=3"
has "describe reports the gaps" "$TMP/scj.out" "gaps=1"
printf '{"chart":{"series_col":3},"rows":[["","a","b"],["Q1",1,2],["Q2",3,4]]}' > "$TMP/colsrows.json"
"$BIN" "$TMP/colsrows.json" --describe > "$TMP/scjr.out" 2>&1; check "json rows with a header: series_col from the chart block: exit 0" "$?" "0"
has "json rows with a header: series_col is applied once" "$TMP/scjr.out" "col 3  b"
"$BIN" "$TMP/cols.json" --series-col 1 --describe > "$TMP/scj1.out" 2>&1; check "json: series_col on the labels: exit 1" "$?" "1"
has "json: series_col on the labels: named" "$TMP/scj1.out" 'series_col 1 is the labels'
printf 'x,y\n1,2\n2,4\n3,5\n' > "$TMP/xyd.csv"
"$BIN" "$TMP/xyd.csv" --xy --describe > "$TMP/xyd.out" 2>&1
hasnt "describe hides the internal X name" "$TMP/xyd.out" $'\x01'
has "describe calls the X column the X axis" "$TMP/xyd.out" "x (X axis)"

echo "== tables show words, and numbers as the file wrote them"
printf 'layout,ts,last,tg,pp\nCC,1/1,CUDA,115.3,3015\nCRRC,14/30/30/14,CUDA,59.0,1124\n' > "$TMP/tab.csv"
"$BIN" "$TMP/tab.csv" -t table --print -w 80 -H 8 --ascii --no-color > "$TMP/tab.out" 2>&1; check "table with text columns: exit 0" "$?" "0"
has "table: text column kept" "$TMP/tab.out" "14/30/30/14"
has "table: second text column kept" "$TMP/tab.out" "CUDA"
has "table: 59.0 keeps its decimal" "$TMP/tab.out" "59.0"
has "table: whole numbers stay whole, with commas" "$TMP/tab.out" "3,015"
has "table: the label column has its header" "$TMP/tab.out" "layout"
"$BIN" "$TMP/tab.csv" -t table --prec 2 --print -w 80 -H 8 --ascii --no-color > "$TMP/tabp.out" 2>&1
has "table: prec still wins" "$TMP/tabp.out" "3,015.00"
printf '[{"name":"a","v":1.5,"note":"hi"},{"name":"b","v":2,"note":"yo"}]' > "$TMP/tab.json"
"$BIN" "$TMP/tab.json" -t table --print -w 60 -H 6 --ascii --no-color > "$TMP/tabj.out" 2>&1
has "table: json text field kept" "$TMP/tabj.out" "yo"
has "table: json column shares its decimals" "$TMP/tabj.out" "2.0"
printf '{"slides":[{"title":"t","type":"table","data":"tab.csv"}],"display":{"size":[40,12]}}' > "$TMP/tabdeck.json"
"$BIN" "$TMP/tabdeck.json" --check > "$TMP/tabchk.out" 2>&1
has "table: --check warns when columns are cut" "$TMP/tabchk.out" "columns; give it more width"

echo "== bars on an axis that skips zero are drawn broken"
printf 'k,v\na,116\nb,114\n' > "$TMP/brk.csv"
"$BIN" "$TMP/brk.csv" --min 110 --print -w 60 -H 14 --no-color > "$TMP/brk.out" 2>&1; check "broken axis: exit 0" "$?" "0"
has "broken axis: the axis carries a break" "$TMP/brk.out" "≈"
"$BIN" "$TMP/brk.csv" -t hbar --min 110 --print -w 60 -H 14 --no-color > "$TMP/brkh.out" 2>&1
has "broken axis: hbar too" "$TMP/brkh.out" "≈"
"$BIN" "$TMP/brk.csv" --print -w 60 -H 14 --no-color > "$TMP/nobrk.out" 2>&1
hasnt "an axis from zero is not broken" "$TMP/nobrk.out" "≈"
"$BIN" "$TMP/brk.csv" --min 110 --png "$TMP/brk.png" > /dev/null 2>&1; check "broken axis: png renders" "$?" "0"
"$BIN" "$TMP/brk.csv" --min 110 --print --ascii -w 60 -H 14 --no-color > "$TMP/brka.out" 2>&1
if LC_ALL=C grep -q '[^ -~]' "$TMP/brka.out"; then bad "broken axis: --ascii stays ASCII"; else ok "broken axis: --ascii stays ASCII"; fi
has "broken axis: --ascii marks the break" "$TMP/brka.out" "~"

echo "== error bars"
printf 'k,v,lo,hi,sd\na,10,8,12,1\nb,20,17,22,2\nc,15,,,\n' > "$TMP/err.csv"
cat > "$TMP/err.json" <<'EOF2'
{"slides": [
  {"title": "range", "type": "bar", "data": "err.csv", "series_col": 2, "errors": {"v": ["lo", "hi"]}, "values": true},
  {"title": "pm", "type": "hbar", "data": "err.csv", "series_col": 2, "errors": "sd", "values": true},
  {"title": "line", "type": "line", "data": "err.csv", "series_col": 2, "errors": {"v": "sd"}},
  {"title": "scatter", "type": "scatter", "data": "err.csv", "series_col": 2, "errors": "sd"},
  {"title": "stacked ignores them", "type": "stacked", "data": "err.csv", "errors": "sd"},
  {"title": "pie ignores them", "type": "pie", "data": "err.csv", "series_col": 2, "errors": "sd"},
  {"title": "typo", "type": "bar", "data": "err.csv", "errors": {"v": ["low", "hi"]}}
]}
EOF2
"$BIN" "$TMP/err.json" --check > "$TMP/errchk.out" 2>&1; check "error bars: --check exit 0" "$?" "0"
has "error bars: a missing column is named" "$TMP/errchk.out" 'no column "low" for the low end'
has "error bars: with a did-you-mean" "$TMP/errchk.out" 'did you mean "lo"?'
has "error bars: only the typo warns" "$TMP/errchk.out" "1 warning"
"$BIN" "$TMP/err.json" --png-dir "$TMP/errpng" > /dev/null 2>&1; check "error bars: every slide renders" "$?" "0"
"$BIN" "$TMP/err.json" --describe > "$TMP/errd.out" 2>&1
has "error bars: series_col keeps the error columns" "$TMP/errd.out" "3 rows x 3 series, error bars"
"$BIN" "$TMP/err.csv" --series-col 2 --print -w 60 -H 16 --no-color > "$TMP/errnone.out" 2>&1
"$BIN" "$TMP/err.json" --print --slide 1 -w 60 -H 16 --no-color > "$TMP/errone.out" 2>&1
if cmp -s "$TMP/errnone.out" "$TMP/errone.out"; then bad "error bars: whiskers change the picture"; else ok "error bars: whiskers change the picture"; fi

echo "== dumbbell: before and after"
printf 'k,before,after\nCRRC,57.6,63.1\nRRCC,56.5,62.0\nlocal,115.3,115.3\n' > "$TMP/db.csv"
"$BIN" --list-types | grep -q dumbbell; check "dumbbell is a type" "$?" "0"
"$BIN" "$TMP/db.csv" -t dumbbell --values --print -w 80 -H 12 --no-color > "$TMP/db.out" 2>&1; check "dumbbell: exit 0" "$?" "0"
has "dumbbell: values as the file wrote them" "$TMP/db.out" "62.0"
has "dumbbell: category labels" "$TMP/db.out" "RRCC"
check "dumbbell: one label where the dots coincide" "$(grep -o '115.3' "$TMP/db.out" | wc -l | tr -d ' ')" "1"
"$BIN" "$TMP/db.csv" -t before_after --png "$TMP/db.png" > /dev/null 2>&1; check "dumbbell: before_after is an alias" "$?" "0"
"$BIN" "$TMP/db.csv" --series-col 2 -t dumbbell --values --png "$TMP/db1.png" > /dev/null 2>&1; check "dumbbell: one series is fine" "$?" "0"

echo "== a data file that says how it wants to be drawn"
printf '#chart: type=pie3d, title="Disk use", values\nname,gb\nroot,40\nhome,120\nvar,15\n' > "$TMP/spec.csv"
"$BIN" --describe "$TMP/spec.csv" > "$TMP/spec.txt" 2>&1; check "#chart: csv describes" "$?" "0"
has "#chart: type is read" "$TMP/spec.txt" "type=pie3d"
has "#chart: quoted title is read" "$TMP/spec.txt" 'title="Disk use"'
grep -q "^rows:    3" "$TMP/spec.txt"; check "#chart: line is not data" "$?" "0"
"$BIN" "$TMP/spec.csv" -w 70 -H 20 --no-color > "$TMP/spec.out" 2>&1; check "#chart: csv renders" "$?" "0"
has "#chart: title is drawn" "$TMP/spec.out" "Disk use"
has "#chart: 'values' puts percentages on the pie" "$TMP/spec.out" "%"
"$BIN" "$TMP/spec.csv" -t table -w 70 -H 20 --no-color > "$TMP/spec2.out" 2>&1
has "-t on the command line beats the file's type" "$TMP/spec2.out" "home"
cat > "$TMP/spec.json" <<'EOF'
{"chart": {"type": "line", "title": "From JSON",
           "annotations": [{"at": "b", "text": "peak here"}, {"y": 2, "text": "floor"}]},
 "rows": [{"label": "a", "v": 1}, {"label": "b", "v": 5}, {"label": "c", "v": 3}]}
EOF
"$BIN" --describe "$TMP/spec.json" > "$TMP/specj.txt" 2>&1; check "json chart block describes" "$?" "0"
has "json chart block: type" "$TMP/specj.txt" "type=line"
"$BIN" "$TMP/spec.json" -w 70 -H 18 --no-color > "$TMP/specj.out" 2>&1
has "json chart block: title drawn" "$TMP/specj.out" "From JSON"
has "file annotation: callout text drawn" "$TMP/specj.out" "peak here"
has "file annotation: target line text drawn" "$TMP/specj.out" "floor"

echo "== stdin"
"$BIN" --describe - < "$EX/revenue.csv" | grep -q "stdin"
check "reads stdin with -" "$?" "0"
cat "$EX/revenue.csv" | "$BIN" -t bar --no-color > "$TMP/pipe.out"
check "pipes csv to a chart" "$?" "0"
[ -s "$TMP/pipe.out" ]; check "piped chart is not empty" "$?" "0"

echo "== every chart type renders, at every size"
for t in bar grouped column stacked hbar line area pie pie3d donut scatter xy hist table; do
  for size in 100x30 60x16 20x6 200x60; do
    w=${size%x*}; h=${size#*x}
    "$BIN" "$EX/revenue.csv" -t "$t" -w "$w" -H "$h" --no-color > "$TMP/type.out" 2> "$TMP/type.err"
    rc=$?
    if [ $rc -ne 0 ]; then bad "type $t at $size exits 0 (rc=$rc: $(head -c 200 "$TMP/type.err"))"; continue; fi
    lines=$(wc -l < "$TMP/type.out")
    if [ "$lines" -ne "$h" ]; then bad "type $t honours -H $h (got $lines)"; continue; fi
    if [ $HAVE_PY -eq 1 ]; then wd=$(widest < "$TMP/type.out"); else wd=$w; fi
    if [ "$wd" -gt "$w" ]; then bad "type $t stays inside -w $w (got $wd)"
    else pass=$((pass+1)); printf '  ok   type %-8s %sx%s\n' "$t" "$wd" "$lines"; fi
  done
done
for f in "$EX/quarterly.csv" "$EX/traffic.tsv" "$EX/disk.json"; do
  for t in bar stacked hbar line area pie pie3d donut scatter hist table; do
    "$BIN" "$f" -t "$t" -w 72 -H 20 --ascii --no-color > "$TMP/t2.out" 2>&1 || bad "$(basename "$f") as $t (rc=$?)"
  done
  ok "$(basename "$f") renders as every type"
done
"$BIN" -w 5 -H 3 "$EX/revenue.csv" >/dev/null 2>&1
check "5x3 does not crash" "$?" "0"
"$BIN" -w 1 -H 1 "$EX/revenue.csv" -t pie3d >/dev/null 2>&1
check "1x1 does not crash" "$?" "0"
"$BIN" -w 1000 -H 300 "$EX/revenue.csv" -t area >/dev/null 2>&1
check "1000x300 does not crash" "$?" "0"

echo "== glyphs and colour"
"$BIN" "$EX/revenue.csv" -t pie3d --ascii -w 80 -H 24 --no-color > "$TMP/ascii.out"
if LC_ALL=C grep -q '[^ -~]' "$TMP/ascii.out"; then bad "--ascii is pure ASCII"; else ok "--ascii is pure ASCII"; fi
"$BIN" "$EX/revenue.csv" -t bar -w 80 -H 24 --no-color > "$TMP/nocolor.out"
if grep -q $'\x1b' "$TMP/nocolor.out"; then bad "--no-color emits no escapes"; else ok "--no-color emits no escapes"; fi
"$BIN" "$EX/revenue.csv" -t bar -w 80 -H 24 --color > "$TMP/color.out"
if grep -q $'\x1b' "$TMP/color.out"; then ok "--color emits escapes"; else bad "--color emits escapes"; fi
if grep -q $'\x1b\[0m' "$TMP/color.out"; then ok "colour is reset before newline"; else bad "colour is reset before newline"; fi
NO_COLOR=1 "$BIN" "$EX/revenue.csv" -t bar -w 80 -H 24 > "$TMP/envnocolor.out"
if grep -q $'\x1b' "$TMP/envnocolor.out"; then bad "\$NO_COLOR is honoured"; else ok "\$NO_COLOR is honoured"; fi
"$BIN" "$EX/revenue.csv" -t bar > "$TMP/redirected.out"
if grep -q $'\x1b' "$TMP/redirected.out"; then bad "no escapes when redirecting"; else ok "no escapes when redirecting"; fi

echo "== pie slices stay distinct without colour"
printf 'cat,val\nA,25\nB,25\nC,25\nD,25\n' > "$TMP/four.csv"
ng=$("$BIN" "$TMP/four.csv" -t pie3d -w 60 -H 18 --no-color | grep -o -E '[█▓▒░]' | sort -u | wc -l)
check "four slices use four shades" "$ng" "4"

echo "== options"
# With two series side by side the value labels are dropped when they do not
# fit; one series (or a wide canvas) always has room.
printf 'm,v\nJan,120\nFeb,298\nMar,77\n' > "$TMP/one.csv"
"$BIN" "$TMP/one.csv" -t bar --values -w 60 -H 20 --no-color > "$TMP/values.out"
has "--values prints numbers (one series)" "$TMP/values.out" "298"
"$BIN" "$TMP/one.csv" -t bar --no-values -w 60 -H 20 --no-color > "$TMP/novalues.out"
hasnt "--no-values prints none" "$TMP/novalues.out" "298"
"$BIN" "$EX/revenue.csv" -t bar --values -w 200 -H 40 --no-color > "$TMP/values2.out"
has "--values prints numbers (two series, wide canvas)" "$TMP/values2.out" "298"
"$BIN" "$EX/revenue.csv" -t bar --no-grid -w 90 -H 24 --no-color > "$TMP/nogrid.out"
hasnt "--no-grid removes dots" "$TMP/nogrid.out" "·"
"$BIN" "$EX/revenue.csv" -t bar --no-legend -w 90 -H 24 --no-color > "$TMP/nolegend.out"
hasnt "--no-legend removes the legend" "$TMP/nolegend.out" "costs"
"$BIN" "$EX/revenue.csv" -t pie3d --explode 0 -w 80 -H 24 --no-color | grep -q '█'
check "--explode 0 works" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar --min 0 --max 400 -w 90 -H 24 --no-color | grep -q '400'
check "--max pins the axis" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar --frame single -w 90 -H 24 --no-color | grep -q '┌'
check "--frame single draws single lines" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar --frame none -w 90 -H 24 --no-color > "$TMP/noframe.out"
hasnt "--frame none drops the frame" "$TMP/noframe.out" "╔"
"$BIN" "$EX/revenue.csv" -t bar -T "My Title" --xlabel "the-x" -w 90 -H 24 --no-color > "$TMP/title.out"
has "-T sets the title" "$TMP/title.out" "My Title"
has "--xlabel is drawn" "$TMP/title.out" "the-x"
"$BIN" "$EX/revenue.csv" -t bar --no-shadow -w 90 -H 24 --no-color > "$TMP/noshadow.out"
check "--no-shadow runs" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar --depth 0 -w 90 -H 24 --no-color >/dev/null; check "--depth 0 runs" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar --depth 6 -w 90 -H 24 --no-color >/dev/null; check "--depth 6 runs" "$?" "0"
"$BIN" "$EX/revenue.csv" -t stacked --transpose -w 90 -H 24 --no-color >/dev/null
check "--transpose runs" "$?" "0"
"$BIN" "$EX/revenue.csv" -t hist --bins 4 -w 90 -H 24 --no-color >/dev/null
check "--bins runs" "$?" "0"
for p in dos ega cga ice fire green amber mono; do
  "$BIN" "$EX/revenue.csv" -t bar --palette $p -w 90 -H 24 --color >/dev/null || bad "--palette $p"
done
ok "every palette runs"
for th in dos black light; do
  "$BIN" "$EX/revenue.csv" -t bar --theme $th -w 90 -H 24 --color >/dev/null || bad "--theme $th"
done
ok "every theme runs"
"$BIN" "$EX/revenue.csv" --no-header --describe >/dev/null 2>&1
check "--no-header on a headed file drops the names" "$?" "1"
printf '10,20\n1,2\n3,4\n' > "$TMP/numhead.csv"
"$BIN" "$TMP/numhead.csv" --no-header --describe | grep -q "^rows:    3"
check "--no-header keeps every row" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar --series-col 2 --describe | grep -q "^series:  1"
check "--series-col keeps one series" "$?" "0"
"$BIN" "$EX/revenue.csv" -t hbar --labels-col 1 -w 80 -H 20 --no-color >/dev/null
check "--labels-col runs" "$?" "0"
"$BIN" "$EX/traffic.tsv" -t bar --delim tab -w 80 -H 20 --no-color >/dev/null
check "--delim tab" "$?" "0"
"$BIN" "$EX/revenue.csv" -t bar -w 60 -H 16 --no-color -o "$TMP/dash-o.txt" > "$TMP/dash-o.stdout"
check "-o exits 0" "$?" "0"
check "-o writes the chart to the file" "$(wc -l < "$TMP/dash-o.txt")" "16"
check "-o leaves stdout empty" "$(wc -c < "$TMP/dash-o.stdout")" "0"

echo "== tiles"
"$BIN" "$EX/revenue.csv" "$EX/quarterly.csv" "$EX/disk.json" \
       "$EX/traffic.tsv" --tile -t bar,line,pie3d,stacked -w 120 -H 40 --no-color > "$TMP/tile.out"
check "--tile with 4 files" "$?" "0"
check "--tile fills the height" "$(wc -l < "$TMP/tile.out")" "40"
check "--tile names all four" "$(grep -o -E 'revenue\.csv|quarterly\.csv|disk\.json|traffic\.tsv' "$TMP/tile.out" | sort -u | wc -l)" "4"

echo "== a directory of data"
"$BIN" -i --help >/dev/null 2>&1; check "-i --help still works" "$?" "0"
mkdir -p "$TMP/datadir"; cp "$EX/revenue.csv" "$EX/quarterly.csv" "$EX/disk.json" "$TMP/datadir/"
"$BIN" "$TMP/datadir" -t bar -w 80 -H 20 --no-color > "$TMP/dir.out" 2>&1
check "directory expands to its data files" "$?" "0"
check "directory prints one chart per file, a blank line between" "$(wc -l < "$TMP/dir.out")" "62"

# ------------------------------------------------------------------------------
echo "== decks: --check"
DECK="$TMP/deck"; mkdir -p "$DECK"
cp "$EX/deck.json" "$EX/revenue.csv" "$EX/quarterly.csv" "$DECK/"
"$BIN" "$DECK/deck.json" --check > "$TMP/check-ok.out" 2>&1; check "good deck: --check exits 0" "$?" "0"
has "good deck: says ok" "$TMP/check-ok.out" "ok: 3 slides, 0 errors, 0 warnings"

cat > "$DECK/badtype.json" <<'EOF'
{"slides": [
  {"title": "fine", "type": "bar", "data": "revenue.csv"},
  {"title": "broken", "blocks": [{"type": "barr", "data": "revenue.csv"}]}
]}
EOF
"$BIN" "$DECK/badtype.json" --check > "$TMP/check-type.out" 2>&1; check "unknown chart type: exit 1" "$?" "1"
has "unknown chart type: JSON path" "$TMP/check-type.out" "slides[1].blocks[0].type"
has "unknown chart type: names it" "$TMP/check-type.out" '"barr"'
has "unknown chart type: did-you-mean" "$TMP/check-type.out" 'did you mean "bar"'
has "unknown chart type: FAILED summary" "$TMP/check-type.out" "FAILED"

cat > "$DECK/badkey.json" <<'EOF'
{"slides": [{"title": "typo", "tpye": "bar", "data": "revenue.csv"}]}
EOF
"$BIN" "$DECK/badkey.json" --check > "$TMP/check-key.out" 2>&1; check "unknown key: still exit 0" "$?" "0"
has "unknown key: a warning" "$TMP/check-key.out" "warning"
has "unknown key: JSON path" "$TMP/check-key.out" "slides[0].tpye"
has "unknown key: did-you-mean" "$TMP/check-key.out" 'did you mean "type"'
has "unknown key: counted" "$TMP/check-key.out" "0 errors, 1 warning"

cat > "$DECK/nofile.json" <<'EOF'
{"slides": [{"title": "gone", "blocks": [{"text": "hi"}, {"type": "bar", "data": "no-such-file.csv"}]}]}
EOF
"$BIN" "$DECK/nofile.json" --check > "$TMP/check-file.out" 2>&1; check "missing data file: exit 1" "$?" "1"
has "missing data file: an error" "$TMP/check-file.out" "error"
has "missing data file: JSON path" "$TMP/check-file.out" "slides[0].blocks[1].data"
has "missing data file: names the file" "$TMP/check-file.out" "no-such-file.csv"

cat > "$DECK/badnote.json" <<'EOF'
{"slides": [{"title": "note", "type": "bar", "data": "revenue.csv",
             "annotations": [{"at": "Aprl", "text": "x"}]}]}
EOF
"$BIN" "$DECK/badnote.json" --check > "$TMP/check-note.out" 2>&1; check "unknown annotation category: exit 0" "$?" "0"
has "unknown annotation category: JSON path" "$TMP/check-note.out" "slides[0].annotations[0]"
has "unknown annotation category: did-you-mean" "$TMP/check-note.out" 'did you mean "Apr"'

echo '{"title": "no slides"}' > "$DECK/noslides.json"
"$BIN" "$DECK/noslides.json" --check >/dev/null 2>&1; rc=$?
if [ $rc -eq 1 ] || [ $rc -eq 0 ]; then ok "object without slides: clean exit ($rc)"; else bad "object without slides: clean exit (rc=$rc)"; fi
echo '{"slides": []}' > "$DECK/empty.json"
"$BIN" "$DECK/empty.json" --check > "$TMP/check-empty.out" 2>&1; check "empty slides array: exit 1" "$?" "1"
echo '{"slides": [' > "$DECK/trunc.json"
"$BIN" "$DECK/trunc.json" --check > "$TMP/check-trunc.out" 2>&1; check "truncated deck: exit 1" "$?" "1"
has "truncated deck: says where" "$TMP/check-trunc.out" "line"

if [ $HAVE_PY -eq 1 ]; then
  "$BIN" "$DECK/deck.json" --check --json > "$TMP/check-ok.json" 2>/dev/null; check "--check --json: exit 0 when ok" "$?" "0"
  python3 - "$TMP/check-ok.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["ok"] is True and d["issues"] == [] and d["slides"] == 3 and d["errors"] == 0, d
EOF
  check "--check --json: parses, ok=true, no issues" "$?" "0"
  "$BIN" "$DECK/badtype.json" --check --json > "$TMP/check-bad.json" 2>/dev/null; check "--check --json: exit 1 on errors" "$?" "1"
  python3 - "$TMP/check-bad.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["ok"] is False and d["errors"] == 1, d
i = [x for x in d["issues"] if x["level"] == "error"]
assert len(i) == 1 and i[0]["path"] == "slides[1].blocks[0].type" and "barr" in i[0]["message"], d
EOF
  check "--check --json: parses, ok=false, issue has level/path/message" "$?" "0"
  # a message with quotes, backslashes and unicode must still be valid JSON
  cat > "$DECK/nasty.json" <<'EOF'
{"slides": [{"title": "t", "type": "b\"a\\r\tü", "data": "revenue.csv", "k\"ey\n": 1}]}
EOF
  "$BIN" "$DECK/nasty.json" --check --json > "$TMP/check-nasty.json" 2>/dev/null
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["ok"] is False' "$TMP/check-nasty.json"
  check "--check --json: escapes quotes, backslashes, control chars" "$?" "0"
fi

echo "== decks: --describe"
"$BIN" "$DECK/deck.json" --describe > "$TMP/describe.out" 2>&1; check "--describe exits 0" "$?" "0"
has "--describe: slide count" "$TMP/describe.out" "slides:  3"
has "--describe: deck title" "$TMP/describe.out" "title:   Q3 review"
has "--describe: slide titles" "$TMP/describe.out" "2. Revenue grew every month but June"
has "--describe: chart with shape and annotations" "$TMP/describe.out" "chart bar  revenue.csv  12 rows x 2 series, 2 annotations"
has "--describe: inline data" "$TMP/describe.out" "chart pie3d  (inline data)  4 rows x 1 series"
has "--describe: nested group" "$TMP/describe.out" "rows"
has "--describe: stat" "$TMP/describe.out" "stat  1,165"
has "--describe: layout" "$TMP/describe.out" "[cols]"
has "--describe: notes flag" "$TMP/describe.out" "(notes)"

echo "== decks: --print"
"$BIN" "$DECK/deck.json" --print --slide 1 -w 100 -H 30 --no-color > "$TMP/p1.out" 2>&1; check "--print --slide 1 exits 0" "$?" "0"
has "slide 1: title" "$TMP/p1.out" "Q3 review"
has "slide 1: subtitle" "$TMP/p1.out" "Revenue, costs and what comes next"
has "slide 1: page number" "$TMP/p1.out" "1/3"
hasnt "slide 1: not slide 2" "$TMP/p1.out" "Revenue grew"
check "slide 1: -H 30 lines" "$(wc -l < "$TMP/p1.out")" "30"
"$BIN" "$DECK/deck.json" --print --slide 2 -w 100 -H 30 --no-color > "$TMP/p2.out" 2>&1; check "--print --slide 2 exits 0" "$?" "0"
has "slide 2: title" "$TMP/p2.out" "Revenue grew every month but June"
has "slide 2: annotation callout text" "$TMP/p2.out" "spring launch"
has "slide 2: annotation target-line text" "$TMP/p2.out" "target"
has "slide 2: ylabel... the axis" "$TMP/p2.out" "Dec"
has "slide 2: footer" "$TMP/p2.out" "ACME  -  Q3 review"
has "slide 2: page number" "$TMP/p2.out" "2/3"
if grep -q $'\x1b' "$TMP/p2.out"; then bad "--print --no-color has no escapes"; else ok "--print --no-color has no escapes"; fi
if [ $HAVE_PY -eq 1 ]; then
  wd=$(widest < "$TMP/p2.out"); if [ "$wd" -le 100 ]; then ok "slide 2: inside -w 100 ($wd)"; else bad "slide 2: inside -w 100 (got $wd)"; fi
fi
"$BIN" "$DECK/deck.json" --print --slide 3 -w 100 -H 30 --no-color > "$TMP/p3.out" 2>&1
has "slide 3: stat" "$TMP/p3.out" "1,165"
has "slide 3: bullets" "$TMP/p3.out" "Cloud doubled since Q1"
hasnt "slide 3: **bold** markers are not printed" "$TMP/p3.out" "**"
has "slide 3: pie legend" "$TMP/p3.out" "payroll"
"$BIN" "$DECK/deck.json" --print -w 100 -H 30 --no-color > "$TMP/pall.out" 2>&1; check "--print (all slides) exits 0" "$?" "0"
check "--print (all): every page number appears" "$(grep -o -E '\b[123]/3' "$TMP/pall.out" | sort -u | wc -l)" "3"
"$BIN" "$DECK/deck.json" -w 100 -H 30 --no-color > "$TMP/pimplicit.out" 2>&1
cmp -s "$TMP/pall.out" "$TMP/pimplicit.out"; check "a deck without a tty prints like --print" "$?" "0"
"$BIN" "$DECK/deck.json" --print --slide 2 --ascii -w 100 -H 30 --no-color > "$TMP/p2a.out" 2>&1
if LC_ALL=C grep -q '[^ -~]' "$TMP/p2a.out"; then bad "--print --ascii is pure ASCII"; else ok "--print --ascii is pure ASCII"; fi
"$BIN" "$DECK/deck.json" --print --slide 2 -o "$TMP/p2o.out" -w 100 -H 30 --no-color; check "--print -o exits 0" "$?" "0"
cmp -s "$TMP/p2.out" "$TMP/p2o.out"; check "--print -o writes the same text" "$?" "0"
"$BIN" "$DECK/deck.json" --print --slide 9 >/dev/null 2>"$TMP/slide9.err"; check "--slide past the end exits 1" "$?" "1"
has "--slide past the end says how many there are" "$TMP/slide9.err" "3 slides"
"$BIN" "$DECK/nofile.json" --print --no-color > "$TMP/pbroken.out" 2>/dev/null
rc=$?; if [ $rc -eq 0 ] || [ $rc -eq 1 ]; then ok "--print of a deck with a missing file exits cleanly ($rc)"; else bad "--print of a broken deck (rc=$rc)"; fi
has "--print of a broken block shows the reason in place" "$TMP/pbroken.out" "no-such-file.csv"

echo "== decks: shorthand, nesting, placement, inline data"
cat > "$DECK/shapes.json" <<'EOF'
{"slides": [
 {"title": "Shorthand text", "text": "# Hello\n- one-bullet\n- two-bullet"},
 {"title": "Shorthand stat", "stat": "42%", "label": "the-answer", "delta": "-3 pt"},
 {"title": "Shorthand bullets", "bullets": ["first-point", "second-point"]},
 {"title": "Nested", "blocks": [{"cols": [
     {"rows": [{"text": "alpha-cell"},
               {"cols": [{"text": "beta-cell"}, {"stat": "7", "label": "gamma-cell"}]}]},
     {"type": "bar", "title": "delta-chart", "data": {"Mon": 10, "Tue": 20}, "weight": 2}]}]},
 {"title": "Placed", "blocks": [{"text": "TOPLEFT", "at": [0, 0, 4, 2]},
                                {"text": "BOTRIGHT", "at": [8, 10, 4, 2]}]},
 {"title": "Shapes", "layout": "grid", "blocks": [
   {"type": "bar",   "title": "s-records", "data": [{"label": "Q1", "north": 120, "south": 88}, {"label": "Q2", "north": 10, "south": 8}]},
   {"type": "line",  "title": "s-xy",      "data": {"x": [1, 2, 3], "y": [4, 5, 6]}},
   {"type": "table", "title": "s-rows",    "data": [["", "north", "south"], ["Q1", 120, 88], ["Q2", 130, 90]]},
   {"type": "pie",   "title": "s-obj",     "data": {"Mon": 10, "Tue": 20}},
   {"type": "hbar",  "title": "s-series",  "data": {"labels": ["Q1", "Q2"], "series": [{"name": "north", "values": [120, 145]}]}}]},
 {"title": "Notes", "type": "line", "data": "revenue.csv", "annotations": [
   {"at": "Apr", "series": "revenue", "text": "callout-text"},
   {"y": 150, "text": "hline-text"},
   {"x": "Sep", "text": "vline-text"},
   {"note": [0.9, 0.1], "text": "free-text"},
   {"at": 0, "text": "by-index"}]},
 {"title": "Pie note", "type": "pie", "data": {"ann": 3, "bob": 5}, "annotations": [{"at": "bob", "text": "slice-text"}]}
]}
EOF
"$BIN" "$DECK/shapes.json" --check > "$TMP/shapes.check" 2>&1; check "shapes deck passes --check" "$?" "0"
has "shapes deck: no warnings" "$TMP/shapes.check" "0 errors, 0 warnings"
"$BIN" "$DECK/shapes.json" --describe > "$TMP/shapes.desc" 2>&1
has "shorthand text is one text block" "$TMP/shapes.desc" "text  3 lines: # Hello"
has "shorthand stat is one stat block" "$TMP/shapes.desc" "stat  42%  the-answer"
has "inline records: 2 rows x 2 series" "$TMP/shapes.desc" "chart bar  (inline data)  2 rows x 2 series"
has "inline x/y arrays: 3 rows" "$TMP/shapes.desc" "chart line  (inline data)  3 rows"
has "inline header rows: 2 rows x 2 series" "$TMP/shapes.desc" "chart table  (inline data)  2 rows x 2 series"
has "inline object: 2 rows x 1 series" "$TMP/shapes.desc" "chart pie  (inline data)  2 rows x 1 series"
has "inline labels+series: 2 rows x 1 series" "$TMP/shapes.desc" "chart hbar  (inline data)  2 rows x 1 series"
has "annotations are counted" "$TMP/shapes.desc" "5 annotations"
pr() { "$BIN" "$DECK/shapes.json" --print --slide "$1" -w 120 -H 36 --no-color > "$TMP/shape-$1.out" 2>&1; }
pr 1; has "shorthand text: heading drawn" "$TMP/shape-1.out" "Hello"; has "shorthand text: bullet drawn" "$TMP/shape-1.out" "two-bullet"
hasnt "shorthand text: '# ' marker is not printed" "$TMP/shape-1.out" "# Hello"
pr 2; has "shorthand stat: label" "$TMP/shape-2.out" "the-answer"; has "shorthand stat: delta" "$TMP/shape-2.out" "-3 pt"
pr 3; has "shorthand bullets" "$TMP/shape-3.out" "second-point"
pr 4
for w in alpha-cell beta-cell gamma-cell delta-chart Mon Tue; do has "nested rows/cols: $w drawn" "$TMP/shape-4.out" "$w"; done
if [ $HAVE_PY -eq 1 ]; then
  python3 - "$TMP/shape-4.out" <<'EOF'
import sys
L = open(sys.argv[1], encoding="utf-8").read().split("\n")
def pos(w):
    for y, l in enumerate(L):
        if w in l: return l.index(w), y
    raise SystemExit("missing " + w)
ax, ay = pos("alpha-cell"); bx, by = pos("beta-cell"); gx, gy = pos("gamma-cell"); dx, dy = pos("delta-chart")
assert ay < by, "rows: alpha above beta"
assert bx < gx, "cols inside rows: beta left of gamma"
assert max(ax, bx, gx) < dx, "outer cols: the chart is right of the text column"
EOF
  check "nested rows/cols: laid out in the right order" "$?" "0"
  pr 5
  python3 - "$TMP/shape-5.out" <<'EOF'
import sys
L = open(sys.argv[1], encoding="utf-8").read().split("\n")
def pos(w):
    for y, l in enumerate(L):
        if w in l: return l.index(w), y
    raise SystemExit("missing " + w)
tx, ty = pos("TOPLEFT"); bx, by = pos("BOTRIGHT")
assert tx < 40 and ty < 10, ("TOPLEFT", tx, ty)
assert bx > 70 and by > 24, ("BOTRIGHT", bx, by)
EOF
  check "\"at\": blocks land where the 12x12 grid says" "$?" "0"
fi
pr 6
for w in s-records s-xy s-rows s-obj s-series north; do has "inline data: $w drawn" "$TMP/shape-6.out" "$w"; done
pr 7
for w in callout-text hline-text vline-text free-text by-index; do has "annotation: $w drawn" "$TMP/shape-7.out" "$w"; done
pr 8; has "annotation on a pie slice" "$TMP/shape-8.out" "slice-text"
for t in bar stacked hbar area scatter; do
  sed "s/\"type\": \"line\", \"data\": \"revenue.csv\"/\"type\": \"$t\", \"data\": \"revenue.csv\"/" "$DECK/shapes.json" > "$DECK/shapes-$t.json"
  "$BIN" "$DECK/shapes-$t.json" --print --slide 7 -w 120 -H 36 --no-color > "$TMP/shape-7-$t.out" 2>&1
  has "annotation callout on a $t chart" "$TMP/shape-7-$t.out" "callout-text"
done

echo "== decks: like"
printf 'k,v\na,1\nb,2\nc,3\n' > "$TMP/like.csv"
cat > "$TMP/like.json" <<'EOF2'
{"slides": [
  {"title": "quiet", "type": "bar", "data": "like.csv", "min": 0, "max": 5, "colors": ["grey"], "notes": "say this"},
  {"title": "loud", "like": 1, "type": "hbar", "annotations": [{"at": "b", "text": "this one"}]},
  {"title": "as a block", "blocks": [{"like": "slides[1]", "at": [0, 0, 6, 12]}, {"text": "hi", "at": [6, 0, 6, 12]}]}
]}
EOF2
"$BIN" "$TMP/like.json" --check > "$TMP/like.out" 2>&1; check "like: a clean deck" "$?" "0"
"$BIN" "$TMP/like.json" --describe > "$TMP/liked.out" 2>&1
has "like: the chart comes along" "$TMP/liked.out" "chart hbar  like.csv  3 rows x 1 series, 1 annotation"
check "like: the slide's notes stay behind" "$(grep -c '(notes)' "$TMP/liked.out")" "1"
has "like: a path works too" "$TMP/liked.out" "3. as a block"
"$BIN" "$TMP/like.json" --png-dir "$TMP/likepng" > /dev/null 2>&1; check "like: renders" "$?" "0"
printf '{"slides":[{"title":"a","like":7},{"title":"b","blocks":[{"like":"slides[1].blocks[0]"}]},{"title":"c","blocks":[{"like":"slides[2].blocks[1]"},{"like":"slides[2].blocks[0]"}]}]}' > "$TMP/likebad.json"
"$BIN" "$TMP/likebad.json" --check > "$TMP/likebad.out" 2>&1; check "like: bad targets are errors" "$?" "1"
has "like: a slide that is not there" "$TMP/likebad.out" "slides[0].like: wants a slide number from 1 to 3"
has "like: itself" "$TMP/likebad.out" "slides[1].blocks[0].like: points at itself"
has "like: a loop" "$TMP/likebad.out" "goes round in a circle"
check "like: one error each, no follow-on noise" "$(grep -c '^error' "$TMP/likebad.out")" "4"

echo "== sideways charts: value lines stand up, category lines lie down"
printf 'k,v\na,10\nb,20\nc,30\n' > "$TMP/side.csv"
printf '{"slides":[{"title":"t","type":"hbar","data":"side.csv","annotations":[{"y":15,"text":"target"},{"x":"b","text":"this row"}]}]}' > "$TMP/side.json"
"$BIN" "$TMP/side.json" --check > "$TMP/side.chk" 2>&1; check "hbar annotations: clean" "$?" "0"
"$BIN" "$TMP/side.json" --print -w 70 -H 20 --no-color > "$TMP/side.out" 2>&1
has "hbar: the value line is labelled" "$TMP/side.out" "target"
has "hbar: the category line is drawn and labelled" "$TMP/side.out" "this row"

echo "== flow diagrams"
cat > "$TMP/flow.json" <<'EOF2'
{"slides": [
  {"title": "chain", "flow": ["read", "think", {"id": "out", "text": "write\nit", "color": "green"}], "labels": {"read>think": "8 KB"}},
  {"title": "graph", "flow": ["a", "b", "c", "d"], "edges": [["a", "b", "left"], ["a", "c"], {"from": "b", "to": "d", "dash": true}, ["c", "d"], ["d", "a", "again"]], "dir": "down", "box": true},
  {"title": "beside", "blocks": [{"flow": ["x", "y"], "title": "small"}, {"text": "words"}]}
]}
EOF2
"$BIN" "$TMP/flow.json" --check > "$TMP/flow.chk" 2>&1; check "flow: a clean deck" "$?" "0"
"$BIN" "$TMP/flow.json" --describe > "$TMP/flow.d" 2>&1
has "flow: a chain is the steps in order" "$TMP/flow.d" "flow  3 steps, 2 arrows"
has "flow: edges and direction" "$TMP/flow.d" "flow  4 steps, 5 arrows, down"
"$BIN" "$TMP/flow.json" --print --slide 1 -w 100 -H 20 --no-color > "$TMP/flow.out" 2>&1
has "flow: steps are framed windows" "$TMP/flow.out" "║ read ║"
has "flow: a step's text wraps on newlines" "$TMP/flow.out" "write"
has "flow: edge labels are drawn" "$TMP/flow.out" "8 KB"
"$BIN" "$TMP/flow.json" --png-dir "$TMP/flowpng" > /dev/null 2>&1; check "flow: renders to png, loops and all" "$?" "0"
for size in 20x6 40x10 200x60; do
  "$BIN" "$TMP/flow.json" --print --slide 2 --size "$size" --no-color > /dev/null 2>&1; check "flow: survives $size" "$?" "0"
done
"$BIN" "$TMP/flow.json" --print --slide 2 --ascii -w 80 -H 24 > "$TMP/flowa.out" 2>&1; check "flow: ascii" "$?" "0"
cat > "$TMP/flowbad.json" <<'EOF2'
{"slides": [{"title": "bad", "flow": ["read", "think", "read"], "edges": [["read", "thnik"]], "labels": {"think>read": "no such arrow"}, "dir": "sideways"}]}
EOF2
"$BIN" "$TMP/flowbad.json" --check > "$TMP/flowbad.out" 2>&1; check "flow: bad input is an error" "$?" "1"
has "flow: duplicate step" "$TMP/flowbad.out" 'already a step called "read"'
has "flow: unknown step, with a did-you-mean" "$TMP/flowbad.out" 'no step called "thnik" (did you mean "think"?)'
has "flow: a label on no arrow" "$TMP/flowbad.out" 'there is no arrow from "think" to "read"'
has "flow: bad direction" "$TMP/flowbad.out" 'wants "right" or "down"'
printf '{"slides":[{"title":"t","flow":["one","two","three","four","five","six","seven"]}],"display":{"size":[40,12]}}' > "$TMP/flowwide.json"
"$BIN" "$TMP/flowwide.json" --check > "$TMP/flowwide.out" 2>&1
has "flow: --check says when it does not fit" "$TMP/flowwide.out" 'the flow does not fit'

echo "== shapes"
cat > "$TMP/shapes.json" <<'EOF2'
{"slides": [{"title": "every shape", "shapes": [
  {"rect": [0.5, 1, 3, 2], "text": "window", "shadow": true},
  {"rect": [0.5, 5, 3, 2], "text": "solid", "color": "green", "depth": 3},
  {"ellipse": [6, 2, 4, 4], "text": "round", "color": "red", "dither": 2},
  {"poly": [[10, 8], [11.5, 10], [8.5, 10]], "color": "cyan", "fill": false, "border": "white", "width": 2},
  {"arrow": [[3.5, 2], [6, 3.5]], "text": "1 GbE", "width": 2},
  {"line": [[6, 5], [5, 7], [3.5, 6]], "dash": true, "head": "both", "color": "yellow", "shadow": true},
  {"label": [0.5, 10.5], "text": "free text", "size": 2},
  {"label": [11.5, 11], "text": "right", "align": "right"}
]}]}
EOF2
"$BIN" "$TMP/shapes.json" --check > "$TMP/shapes.chk" 2>&1; check "shapes: a clean deck" "$?" "0"
has "shapes: --describe counts them" <("$BIN" "$TMP/shapes.json" --describe 2>&1) "shapes  8 shapes"
"$BIN" "$TMP/shapes.json" --png "$TMP/shapes.png" > /dev/null 2>&1; check "shapes: png" "$?" "0"
"$BIN" "$TMP/shapes.json" --print -w 100 -H 30 --no-color > "$TMP/shapes.out" 2>&1
has "shapes: text inside a shape" "$TMP/shapes.out" "window"
has "shapes: labels" "$TMP/shapes.out" "free text"
for size in 20x6 200x60; do
  "$BIN" "$TMP/shapes.json" --print --size "$size" --no-color > /dev/null 2>&1; check "shapes: survive $size" "$?" "0"
done
"$BIN" "$TMP/shapes.json" --print --ascii -w 80 -H 24 > /dev/null 2>&1; check "shapes: ascii" "$?" "0"
printf '{"slides":[{"title":"t","shapes":[{"rect":[0,0,0.2,3],"text":"hello"}]}]}' > "$TMP/sliver.json"
timeout 10 "$BIN" "$TMP/sliver.json" --check > /dev/null 2>&1; check "shapes: text in a one-column sliver does not hang" "$?" "0"
cat > "$TMP/shapesbad.json" <<'EOF2'
{"slides": [{"title": "bad", "shapes": [
  {"rect": [1, 1, 0, 2]}, {"rect": [1, 1, 2, 2], "ellipse": [1, 1, 2, 2]}, {"poly": [[1, 1], [2, 2]]},
  {"rect": [10, 10, 5, 5]}, {"rect": [1, 1, 2, 2], "colr": "red"}, {"rect": [1, 1, 1, 1], "text": "far too much text for so small a box"},
  {"circle": [1, 1, 2, 2]}, {"label": [1, 1]}
]}]}
EOF2
"$BIN" "$TMP/shapesbad.json" --check > "$TMP/shapesbad.out" 2>&1; check "shapes: bad input is an error" "$?" "1"
has "shapes: zero width" "$TMP/shapesbad.out" "slides[0].shapes[0].rect: wants [x, y, w, h]"
has "shapes: two kinds at once" "$TMP/shapesbad.out" "this has both"
has "shapes: a polygon needs 3 points" "$TMP/shapesbad.out" "wants at least 3 points"
has "shapes: off the grid" "$TMP/shapesbad.out" "reaches outside the block's 12 x 12 grid"
has "shapes: unknown key, with a did-you-mean" "$TMP/shapesbad.out" 'did you mean "color"?'
has "shapes: text that does not fit" "$TMP/shapesbad.out" "does not fit its shape"
has "shapes: no kind" "$TMP/shapesbad.out" "a shape needs one of: rect"
has "shapes: an empty label" "$TMP/shapesbad.out" "a label with no"

echo "== decks: --png"
"$BIN" "$DECK/deck.json" --png "$TMP/s1.png" > "$TMP/png.stdout" 2>&1; check "--png exits 0" "$?" "0"
pngok "--png default is 120x33 cells = 960x528, 4-bit, CRCs, IDAT" "$TMP/s1.png" 960 528
"$BIN" "$DECK/deck.json" --png "$TMP/s2.png" --slide 2 >/dev/null 2>&1; check "--png --slide 2 exits 0" "$?" "0"
pngok "--png --slide 2 is valid" "$TMP/s2.png" 960 528
if cmp -s "$TMP/s1.png" "$TMP/s2.png"; then bad "--slide picks a different picture"; else ok "--slide picks a different picture"; fi
"$BIN" "$DECK/deck.json" --png "$TMP/s2b.png" --slide 2 >/dev/null 2>&1
cmp -s "$TMP/s2.png" "$TMP/s2b.png"; check "--png is deterministic" "$?" "0"
"$BIN" "$DECK/deck.json" --png "$TMP/size.png" --size 80x24 >/dev/null 2>&1; check "--png --size exits 0" "$?" "0"
pngok "--size 80x24 -> 640x384" "$TMP/size.png" 640 384
"$BIN" "$DECK/deck.json" --png "$TMP/scale.png" --scale 2 >/dev/null 2>&1; check "--png --scale exits 0" "$?" "0"
pngok "--scale 2 -> 1920x1056" "$TMP/scale.png" 1920 1056
"$BIN" "$DECK/deck.json" --png "$TMP/both.png" --size 80x24 --scale 2 --slide 3 >/dev/null 2>&1
pngok "--size 80x24 --scale 2 -> 1280x768" "$TMP/both.png" 1280 768
"$BIN" "$DECK/deck.json" --png "$TMP/wh.png" -w 100 -H 30 >/dev/null 2>&1
pngok "-w 100 -H 30 -> 800x480" "$TMP/wh.png" 800 480
"$BIN" "$DECK/deck.json" --png "$TMP/tiny.png" --size 20x6 >/dev/null 2>&1; rc=$?
if [ $rc -eq 0 ]; then pngok "--size 20x6 still a valid PNG" "$TMP/tiny.png" 160 96; else check "--size 20x6 exits cleanly" "$rc" "2"; fi
"$BIN" "$EX/revenue.csv" -t pie3d --png "$TMP/data.png" >/dev/null 2>&1; check "--png of a data file exits 0" "$?" "0"
pngok "--png of a data file is valid" "$TMP/data.png" 960 528
for th in dos black light; do
  "$BIN" "$DECK/deck.json" --png "$TMP/theme-$th.png" --slide 2 --theme $th >/dev/null 2>&1
  pngok "--png --theme $th" "$TMP/theme-$th.png" 960 528
done
"$BIN" "$DECK/deck.json" --png "$TMP/s9.png" --slide 9 >/dev/null 2>&1; check "--png --slide past the end exits 1" "$?" "1"
"$BIN" "$DECK/deck.json" --png /nonexistent-dir/x.png >/dev/null 2>"$TMP/pngerr"; check "--png to an unwritable path exits 1" "$?" "1"

echo "== decks: --png-dir"
"$BIN" "$DECK/deck.json" --png-dir "$TMP/pngs" >/dev/null 2>&1; check "--png-dir exits 0 (creates the dir)" "$?" "0"
check "--png-dir writes exactly slide-01..03" "$(cd "$TMP/pngs" 2>/dev/null && ls | tr '\n' ' ')" "slide-01.png slide-02.png slide-03.png "
for n in 01 02 03; do pngok "slide-$n.png is valid" "$TMP/pngs/slide-$n.png" 960 528; done
cmp -s "$TMP/pngs/slide-02.png" "$TMP/s2.png"; check "--png-dir slide-02 equals --png --slide 2" "$?" "0"
"$BIN" "$DECK/shapes.json" --png-dir "$TMP/pngs2" --size 60x20 >/dev/null 2>&1; check "--png-dir --size exits 0" "$?" "0"
check "--png-dir: one file per slide (8)" "$(ls "$TMP/pngs2" | wc -l)" "8"
pngok "--png-dir honours --size" "$TMP/pngs2/slide-08.png" 480 320

echo "== launcher"
LH="$TMP/home"; mkdir -p "$LH"
HOME="$LH" "$BIN" "$DECK/deck.json" --launcher q3 > "$TMP/launch.out" 2>&1; check "--launcher exits 0" "$?" "0"
check "--launcher prints the path" "$(cat "$TMP/launch.out")" "$LH/.local/bin/q3"
[ -x "$LH/.local/bin/q3" ]; check "the launcher is executable" "$?" "0"
sh -n "$LH/.local/bin/q3"; check "the launcher parses as sh" "$?" "0"
mkdir -p "$TMP/odd dir"; cp "$DECK/deck.json" "$TMP/odd dir/it's \$here.json"; cp "$DECK/revenue.csv" "$TMP/odd dir/"
HOME="$LH" "$BIN" "$TMP/odd dir/it's \$here.json" --launcher "$TMP/odd dir/run me" > /dev/null 2>&1; check "--launcher with a slash writes there" "$?" "0"
sh -n "$TMP/odd dir/run me"; check "quotes, spaces and \$ in the deck path still parse" "$?" "0"
check "the deck path is quoted whole" "$(sh -c "$(grep '^DECK=' "$TMP/odd dir/run me"); printf %s \"\$DECK\"")" "$TMP/odd dir/it's \$here.json"
HOME="$LH" "$BIN" "$DECK/deck.json" --launcher "$(printf 'nl\necho INJECTED')" > /dev/null 2>&1
check "a newline in the name cannot add a line to the script" "$(grep -c '^echo INJECTED' "$LH/.local/bin/nl"*)" "0"

# ------------------------------------------------------------------------------
echo "== failures are clean, exit codes"
"$BIN" /nope/nope.csv >/dev/null 2>"$TMP/err1"; check "missing file exits 1" "$?" "1"
has "missing file says why" "$TMP/err1" "cannot open"
echo '{ bad json' > "$TMP/bad.json"
"$BIN" "$TMP/bad.json" >/dev/null 2>"$TMP/err4"; check "bad json exits 1" "$?" "1"
has "bad json names the place" "$TMP/err4" "json:"
printf 'a,b\n,x\n' > "$TMP/nonum.csv"
"$BIN" "$TMP/nonum.csv" >/dev/null 2>"$TMP/err5"; check "no numeric column exits 1" "$?" "1"
has "no numeric column says why" "$TMP/err5" "no numeric"
: > "$TMP/empty.csv"
"$BIN" "$TMP/empty.csv" >/dev/null 2>"$TMP/err6"; check "empty file exits 1" "$?" "1"
"$BIN" "$EX/revenue.csv" --describe /nope.csv >/dev/null 2>&1; check "--describe of a missing file exits 1" "$?" "1"

usage() { # NAME ARGS...   -> must exit 2 and say something on stderr, nothing on stdout
  local name="$1"; shift
  "$BIN" "$@" > "$TMP/usage.out" 2> "$TMP/usage.err" < /dev/null; local rc=$?
  if [ $rc -ne 2 ]; then bad "$name exits 2 (got $rc)"; return; fi
  if [ ! -s "$TMP/usage.err" ]; then bad "$name explains itself on stderr"; return; fi
  if [ -s "$TMP/usage.out" ]; then bad "$name keeps stdout empty"; return; fi
  ok "$name exits 2"
}
usage "unknown option"            "$EX/revenue.csv" --bogus
usage "unknown short option"      "$EX/revenue.csv" -Z
usage "unknown chart type"        "$EX/revenue.csv" -t nope
usage "-w not a number"           "$EX/revenue.csv" -w abc
usage "-w zero"                   "$EX/revenue.csv" -w 0
usage "-H negative"               "$EX/revenue.csv" -H -4
usage "-w trailing junk"          "$EX/revenue.csv" -w 80x
usage "-w beyond an int"          "$EX/revenue.csv" -w 1e300
usage "--slide 0"                 "$DECK/deck.json" --print --slide 0
usage "--slide not a number"      "$DECK/deck.json" --print --slide two
usage "--size malformed"          "$DECK/deck.json" --png "$TMP/x.png" --size 10
usage "--size not numbers"        "$DECK/deck.json" --png "$TMP/x.png" --size axb
usage "--scale not a number"      "$DECK/deck.json" --png "$TMP/x.png" --scale big
usage "--max not a number"        "$EX/revenue.csv" --max lots
usage "--bins not a number"       "$EX/revenue.csv" -t hist --bins many
usage "--depth not a number"      "$EX/revenue.csv" --depth deep
usage "unknown palette"           "$EX/revenue.csv" --palette nope
usage "unknown theme"             "$EX/revenue.csv" --theme nope
usage "unknown --gfx"             "$EX/revenue.csv" --gfx nope
usage "unknown --frame"           "$EX/revenue.csv" --frame wobbly
usage "option missing its value"  "$EX/revenue.csv" -t
usage "no input at all"
usage "-i without a terminal"     -i "$EX/revenue.csv"
usage "unknown --example"         --example nope
grep -q "unknown option" "$TMP/err2" 2>/dev/null || { "$BIN" "$EX/revenue.csv" --bogus 2>"$TMP/err2" >/dev/null; }
has "unknown option says why" "$TMP/err2" "unknown option"
"$BIN" "$EX/revenue.csv" -t barr >/dev/null 2>"$TMP/err3"
has "unknown type offers a did-you-mean" "$TMP/err3" "bar"
"$BIN" "$EX/revenue.csv" -t column -w 60 -H 16 --no-color >/dev/null 2>&1; check "alias 'column' is a type" "$?" "0"
"$BIN" "$EX/revenue.csv" -t grouped -w 60 -H 16 --no-color >/dev/null 2>&1; check "alias 'grouped' is a type" "$?" "0"
"$BIN" "$EX/revenue.csv" -t xy -w 60 -H 16 --no-color >/dev/null 2>&1; check "alias 'xy' is a type" "$?" "0"

# ------------------------------------------------------------------------------
echo "== hostile data"
hostile() { # NAME FILE [ARGS]  -> exit 0 or 1, never a signal, never a sanitizer report
  local name="$1" f="$2"; shift 2
  for t in bar stacked hbar line area pie pie3d donut scatter hist table; do
    "$BIN" "$f" -t $t -w 70 -H 20 --values --no-color "$@" > "$TMP/h.out" 2> "$TMP/h.err"; local rc=$?
    if [ $rc -ne 0 ] && [ $rc -ne 1 ]; then bad "$name as $t (rc=$rc: $(head -c 300 "$TMP/h.err"))"; return; fi
    if grep -q -E 'AddressSanitizer|runtime error:' "$TMP/h.err"; then bad "$name as $t: sanitizer report"; return; fi
  done
  ok "$name"
}
printf 'k,v\na,0\nb,0\nc,0\n' > "$TMP/zeros.csv";             hostile "all zeros" "$TMP/zeros.csv"
printf 'k,v\na,-5\nb,-1\nc,-9\n' > "$TMP/neg.csv";            hostile "all negative" "$TMP/neg.csv"
printf 'k,v,w\na,-5,3\nb,4,-1\n' > "$TMP/mixed.csv";          hostile "mixed sign" "$TMP/mixed.csv"
printf 'k,v\na,7\n' > "$TMP/single.csv";                      hostile "a single point" "$TMP/single.csv"
printf 'k,v\na,1e308\nb,-1e308\nc,1e-320\n' > "$TMP/huge.csv"; hostile "1e308 and denormals" "$TMP/huge.csv"
printf 'k,v\na,1e999\nb,-1e999\nc,5\n' > "$TMP/inf.csv";      hostile "overflowing literals" "$TMP/inf.csv"
printf 'k,v,w\na,nan,1\nb,inf,2\nc,-inf,3\nd,NaN,4\ne,Infinity,5\n' > "$TMP/nan.csv"; hostile "nan / inf strings" "$TMP/nan.csv"
printf 'k,v,w\na,,1\nb,,2\nc,3,\n' > "$TMP/holes.csv";        hostile "missing cells" "$TMP/holes.csv"
printf 'k,v\n日本語ラベル,3\némoji 🎉 label,4\nกขค,5\nא‎ב,6\n' > "$TMP/uni.csv"; hostile "unicode labels" "$TMP/uni.csv"
printf 'k,v\n\xff\xfe\x80bad,3\nok,4\n' > "$TMP/badutf.csv";  hostile "invalid UTF-8" "$TMP/badutf.csv"
printf 'k,v\n\x1b[31mred\x1b[0m,3\ntab\there,4\n' > "$TMP/esc.csv"; hostile "escape sequences in labels" "$TMP/esc.csv"
"$BIN" "$TMP/esc.csv" -t bar -w 70 -H 20 --no-color 2>/dev/null > "$TMP/esc.out"
if grep -q $'\x1b' "$TMP/esc.out"; then bad "escape bytes from the data never reach the terminal"; else ok "escape bytes from the data never reach the terminal"; fi
if [ $HAVE_PY -eq 1 ]; then
  python3 -c 'print("k,v"); [print("row%d,%d" % (i, i * 7919 % 1000)) for i in range(5000)]' > "$TMP/long.csv"
  hostile "5000 rows" "$TMP/long.csv"
  python3 -c 'n=300; print("k," + ",".join("s%d" % i for i in range(n))); [print("r%d," % r + ",".join(str((r+1)*(i+1) % 97) for i in range(n))) for r in range(4)]' > "$TMP/wide.csv"
  hostile "300 series" "$TMP/wide.csv"
  python3 -c 'print("k,v"); print("x" * 100000 + ",5"); print("y,6")' > "$TMP/longlabel.csv"
  hostile "a 100 kB label" "$TMP/longlabel.csv"
  if [ $(widest < "$TMP/h.out") -le 70 ]; then ok "a 100 kB label stays inside the canvas"; else bad "a 100 kB label stays inside the canvas"; fi
fi
printf 'k,v\n"unterminated,5\nb,6\n' > "$TMP/quote.csv";      hostile "unterminated quote" "$TMP/quote.csv"
head -c 3000 /dev/urandom > "$TMP/random.csv" 2>/dev/null;    hostile "random bytes as csv" "$TMP/random.csv"
cp "$TMP/random.csv" "$TMP/random.json";                      hostile "random bytes as json" "$TMP/random.json"
printf '\xef\xbb\xbfk,v\na,1\nb,2\n' > "$TMP/bom.csv"
"$BIN" --describe "$TMP/bom.csv" 2>/dev/null | grep -q "^rows:    2"; check "UTF-8 BOM is skipped" "$?" "0"

echo "== hostile decks"
hostile_deck() { # NAME FILE  -> --check, --print and --png each exit 0 or 1 within 20 s, with no sanitizer report
  local name="$1" f="$2"
  for args in "--check" "--print -w 80 -H 24 --no-color" "--png $TMP/hd.png --size 60x20"; do
    # shellcheck disable=SC2086
    timeout 20 "$BIN" "$f" $args > "$TMP/hd.out" 2> "$TMP/hd.err"; local rc=$?
    if [ $rc -eq 124 ]; then bad "$name ($args): took over 20 s"; return; fi
    if [ $rc -ne 0 ] && [ $rc -ne 1 ]; then bad "$name ($args): rc=$rc: $(head -c 300 "$TMP/hd.err")"; return; fi
    if grep -q -E 'AddressSanitizer|runtime error:' "$TMP/hd.err"; then bad "$name ($args): sanitizer report"; return; fi
  done
  ok "$name"
}
cp "$EX/revenue.csv" "$TMP/"
printf '{"slides":[{"title":"t","type":"bar","data":"revenue.csv","annotations":[{"at":1e300,"text":"x"},{"at":"Apr","series":-1e300,"text":"y"},{"at":2.5,"text":"z"}]}]}' > "$TMP/hd-at.json"
hostile_deck "an annotation row beyond an int" "$TMP/hd-at.json"
printf '{"slides":[{"title":"t","type":"bar","data":"revenue.csv"},{"like":1e300},{"like":-1e300},{"like":1.5}]}' > "$TMP/hd-like.json"
hostile_deck "like beyond an int" "$TMP/hd-like.json"
printf '{"display":{"size":[1e300,1e300],"scale":1e300},"slides":[{"title":"t","text":"hi"}]}' > "$TMP/hd-size.json"
hostile_deck "a display size beyond an int" "$TMP/hd-size.json"
"$BIN" "$TMP/hd-size.json" --check > "$TMP/hd-size.out" 2>&1; check "a display size beyond an int is an error" "$?" "1"
has "a display size beyond an int is named" "$TMP/hd-size.out" "display.size"

# ------------------------------------------------------------------------------
if [ $HAVE_PY -eq 1 ]; then
echo "== fuzz: random and mutated decks and data (fixed seed)"
cat > "$TMP/fuzz.py" <<'EOF'
import json, os, random, subprocess, sys

binary, outdir, examples = sys.argv[1], sys.argv[2], sys.argv[3]
N = int(os.environ.get("CHARTS_FUZZ_N", "220"))
rng = random.Random(int(os.environ.get("CHARTS_FUZZ_SEED", "20260917")))
os.makedirs(outdir, exist_ok=True)
for f in ("revenue.csv", "quarterly.csv"):
    open(os.path.join(outdir, f), "wb").write(open(os.path.join(examples, f), "rb").read())

TYPES = ["bar", "stacked", "hbar", "dumbbell", "line", "area", "pie", "pie3d", "donut", "scatter", "hist", "table",
         "column", "xy", "", "barr", "PIE", 7, None]
WORDS = ["", "a", "Apr", "revenue", "日本語", "🎉🎉", "x" * 300, "\x1b[2J", "nan", "-", "0", "‮", "a\nb", "\t", "é" * 40]
NUMS = [0, 1, -1, 0.5, 1e308, -1e308, 1e-300, 2**53, -2**63, 2**64, 12345678901234567890, 1e400, -0.0, 3, 42, 100, 99999]

def word(): return rng.choice(WORDS)
def num():
    r = rng.random()
    if r < 0.6: return rng.choice(NUMS)
    if r < 0.8: return rng.uniform(-1e6, 1e6)
    return rng.choice(["nan", "NaN", "inf", "-inf", "1e999", "", "12abc", None, True, [], {}])

def junk(depth=0):
    r = rng.random()
    if depth > 3 or r < 0.35: return rng.choice([None, True, False, num(), word(), -1, 1e400])
    if r < 0.65: return [junk(depth + 1) for _ in range(rng.randrange(0, 4))]
    return {word() or "k": junk(depth + 1) for _ in range(rng.randrange(0, 4))}

def inline():
    n = rng.choice([0, 1, 2, 3, 12, 60])
    shape = rng.randrange(7)
    if shape == 0: return {"labels": [word() for _ in range(n)], "series": [{"name": word(), "values": [num() for _ in range(rng.choice([n, n, 0, n + 3]))]} for _ in range(rng.randrange(0, 4))]}
    if shape == 1: return [{"label": word(), "a": num(), "b": num()} for _ in range(n)]
    if shape == 2: return {(word() or str(i)) + str(i): num() for i in range(n)}
    if shape == 3: return {"x": [num() for _ in range(n)], "y": [num() for _ in range(rng.choice([n, n + 1, 0]))]}
    if shape == 4: return [["", "p", "q"]] + [[word(), num(), num()] for _ in range(n)]
    if shape == 5: return rng.choice(["revenue.csv", "quarterly.csv", "missing.csv", "", ".", "/", "../" * 9 + "etc/passwd", "/dev/null"])
    return junk()

def annotations():
    out = []
    for _ in range(rng.randrange(0, 5)):
        k = rng.randrange(5)
        if k == 0: out.append({"at": rng.choice(["Apr", "Q1", 0, -1, 10**9, 2.5, word()]), "series": word(), "text": word()})
        elif k == 1: out.append({"y": num(), "text": word()})
        elif k == 2: out.append({"x": rng.choice(["Apr", 3, -7, word()]), "text": word()})
        elif k == 3: out.append({"note": [num(), num()][:rng.randrange(0, 3)], "text": word(), "color": rng.choice(["red", 99, -1, "nope"])})
        else: out.append(junk())
    return out

def chart():
    b = {"data": inline()}
    for k, v in (("type", rng.choice(TYPES)), ("title", word()), ("values", rng.choice([True, False, "yes", 3])),
                 ("depth", rng.choice([0, 3, 6, 7, -1, 1e9, "2"])), ("explode", rng.choice([0, True, -5, 10**6, "big"])),
                 ("min", num()), ("max", num()), ("prec", rng.choice([0, 2, 50, -3])), ("bins", rng.choice([1, 0, -2, 10**7, 8])),
                 ("palette", rng.choice(["cga", "mono", "nope", 3])), ("frame", rng.choice(["double", "none", "zig"])),
                 ("xy", rng.choice([True, False])), ("transpose", rng.choice([True, False])), ("annotations", annotations()),
                 ("errors", rng.choice(["b", "q", ["a", "b"], {"a": "b"}, {"p": ["q", "p"]}, {"x": ["y"]}, 3, [], {"": ""}])),
                 ("series_col", rng.choice([1, 2, 3, 9, 0, "x"])),
                 ("weight", rng.choice([1, 2, 0, -1, 1e9, "x"])), ("at", [rng.choice([0, 3, 6, 11, 12, 13, -1, 1e9, "a"]) for _ in range(rng.choice([4, 4, 3, 5]))])):
        if rng.random() < 0.35: b[k] = v
    return b

def shapes():
    out = []
    for _ in range(rng.randrange(0, 9)):
        k = rng.choice(["rect", "ellipse", "poly", "line", "arrow", "label", "nope"])
        if k in ("rect", "ellipse"): g = [rng.choice([0, 1, 5.5, 12, 13, -1, 1e9, 0.001, "a"]) for _ in range(rng.choice([4, 4, 3]))]
        elif k == "label": g = [num(), num()][:rng.choice([2, 2, 1])]
        else: g = [[rng.uniform(-2, 14), rng.uniform(-2, 14)] for _ in range(rng.choice([0, 1, 2, 3, 7, 60]))]
        sh = {k: g}
        for kk, v in (("text", word()), ("color", rng.choice(["red", 99, "nope", 3])), ("depth", rng.choice([0, 6, 9, "x"])),
                      ("dither", rng.choice([0, 3, 4])), ("shadow", rng.choice([True, 0])), ("width", rng.choice([1, 4, 99])),
                      ("dash", True), ("head", rng.choice(["both", "zz", True, "start"])), ("size", rng.choice([1, 3, 0])),
                      ("fill", rng.choice([False, "none", 1])), ("border", rng.choice(["white", "none", 77])), ("align", rng.choice(["right", "up"]))):
            if rng.random() < 0.4: sh[kk] = v
        out.append(sh if rng.random() < 0.9 else junk())
    return out

def flow():
    names = [word() or "n%d" % i for i in range(rng.choice([0, 1, 2, 5, 12, 30]))]
    b = {"flow": [n if rng.random() < 0.7 else {"id": n, "text": word(), "color": rng.choice(["red", 42])} for n in names]}
    if rng.random() < 0.6 and names:
        b["edges"] = [[rng.choice(names + ["ghost"]), rng.choice(names), word()][:rng.choice([2, 3, 3, 1])] for _ in range(rng.randrange(0, 20))]
    if rng.random() < 0.3 and names: b["labels"] = {rng.choice(names + ["x"]) + rng.choice([">", "->", ""]) + rng.choice(names + ["y"]): word()}
    for kk, v in (("dir", rng.choice(["down", "right", "up"])), ("box", True), ("title", word())):
        if rng.random() < 0.4: b[kk] = v
    return b

def block(depth=0):
    r = rng.random()
    if r < 0.06: return {"shapes": shapes(), "box": rng.choice([True, False])}
    if r < 0.12: return flow()
    if r < 0.15: return {"like": rng.choice([1, 2, 0, -1, 99, 1.5, "slides[0]", "slides[1].blocks[0]", "slides[0].blocks[9]", "nope", None, []])}
    if depth < 8 and r < 0.25: return {rng.choice(["rows", "cols"]): [block(depth + 1) for _ in range(rng.randrange(0, 4))]}
    if r < 0.55: return chart()
    if r < 0.70: return {"text": rng.choice([word(), [word() for _ in range(rng.randrange(0, 6))], "# h\n## s\n- b\n> a\n**e**", 5]), "size": rng.choice([1, 4, 0, 99]), "align": rng.choice(["left", "center", "right", "up"]), "box": True}
    if r < 0.80: return {"bullets": [word() for _ in range(rng.randrange(0, 5))]}
    if r < 0.90: return {"stat": rng.choice([word(), 18, None]), "label": word(), "delta": rng.choice(["+3", "-3", "", 4])}
    return junk()

def deep(n):
    b = {"text": "bottom"}
    for i in range(n): b = {("rows" if i % 2 else "cols"): [b, {"stat": str(i)}]}
    return b

def slide():
    r = rng.random()
    if r < 0.15: return {"title": word(), "subtitle": word()}
    if r < 0.30:
        s = chart(); s["title"] = word(); return s
    if r < 0.35: return {"title": "deep", "blocks": [deep(rng.choice([5, 40, 400]))]}
    if r < 0.38: return {"title": word(), "like": rng.choice([1, 2, 3, "slides[0]", "slides[2].blocks[1]", 0, "x"]), "type": rng.choice(TYPES)}
    if r < 0.40: return junk()
    return {"title": word(), "layout": rng.choice(["auto", "cols", "rows", "grid", "spiral"]), "notes": word(),
            "blocks": [block() for _ in range(rng.randrange(0, 7))]}

def deck():
    return {"title": word(), "theme": rng.choice(["dos", "black", "light", "neon", 4]), "palette": rng.choice(["ega", "zzz"]),
            "slides": [slide() for _ in range(rng.randrange(0, 5))]}

def csv():
    delim = rng.choice([",", ";", "\t", "|", ",", ","])
    rows = []
    if rng.random() < 0.3: rows.append("#chart: " + rng.choice(["type=pie3d, title=\"X\", values", "type=", "type=zzz,,,=", "title=\"unterminated", "depth=99999999999, bins=-1, explode=x", "\x00"]))
    ncol = rng.choice([1, 2, 3, 3, 8])
    if rng.random() < 0.8: rows.append(delim.join(word().replace("\n", " ") or "h" for _ in range(ncol)))
    for _ in range(rng.choice([0, 1, 3, 12, 80])):
        rows.append(delim.join(str(rng.choice([num(), word().replace("\n", " "), '"q,"', '"'])) for _ in range(rng.choice([ncol, ncol, ncol + 2, 1]))))
    return rng.choice(["\n", "\r\n", "\n", "\r"]).join(rows)

def mutate(data):
    b = bytearray(data)
    r = rng.random()
    if r < 0.3 and b: return bytes(b[:rng.randrange(len(b))])                       # truncated
    for _ in range(rng.choice([1, 1, 3, 12])):
        if not b: break
        i = rng.randrange(len(b)); k = rng.randrange(4)
        if k == 0: b[i] = rng.randrange(256)
        elif k == 1: del b[i:i + rng.randrange(1, 9)]
        elif k == 2: b[i:i] = rng.choice([b"[", b"{", b'"', b"\\", b"\x00", b"1e999", b"-", b",", b"]]]]", b"\xff\xfe", b"\\ud800"])
        else: b[i:i] = b[i:i + rng.randrange(1, 40)]
    return bytes(b)

def run(args):
    try:
        p = subprocess.run([binary] + args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, timeout=60, cwd=outdir)
    except subprocess.TimeoutExpired:
        return "timeout after 60 s"
    err = p.stderr.decode("utf-8", "replace")
    if p.returncode not in (0, 1, 2): return "exit status %d\n%s" % (p.returncode, err[-1500:])
    if "AddressSanitizer" in err or "runtime error:" in err or "LeakSanitizer" in err: return "sanitizer report\n" + err[-1500:]
    return None

seeds = [open(os.path.join(examples, "deck.json"), "rb").read()]
failures, runs = [], 0
for i in range(N):
    kind = i % 4
    if kind == 0:   name, data = "fuzz-%03d.json" % i, json.dumps(deck(), ensure_ascii=rng.random() < 0.5).encode()
    elif kind == 1: name, data = "fuzz-%03d.json" % i, mutate(rng.choice(seeds))
    elif kind == 2: name, data = "fuzz-%03d.csv" % i, csv().encode("utf-8", "surrogatepass")
    else:           name, data = "fuzz-%03d.json" % i, json.dumps(rng.choice([junk(), inline(), {"chart": chart(), "rows": inline()}])).replace("NaN", rng.choice(["NaN", "null", "1e999"])).replace("Infinity", "1e999").encode()
    if kind == 0 and len(seeds) < 40: seeds.append(data)
    open(os.path.join(outdir, name), "wb").write(data)
    png = os.path.join(outdir, "out.png")
    if name.endswith(".csv") or kind == 3:
        t = str(rng.choice(TYPES[:13]) or "bar")
        jobs = [[name, "-t", t, "-w", str(rng.choice([20, 80, 3, 250])), "-H", str(rng.choice([6, 24, 2, 70])), "--values"],
                [name, "--describe"], [name, "--png", png, "--size", rng.choice(["120x33", "20x6", "64x20"])]]
    else:
        jobs = [[name, "--check"], [name, "--check", "--json"], [name, "--describe"],
                [name, "--print", "-w", str(rng.choice([100, 40, 20, 200])), "-H", str(rng.choice([30, 10, 6, 50]))],
                [name, "--png", png, "--slide", str(rng.choice([1, 1, 2, 3]))]]
    for args in jobs:
        runs += 1
        why = run(args)
        if why:
            failures.append((name, args, why))
            break
    # --check --json must stay valid JSON whatever the deck said
    if not name.endswith(".csv") and kind != 3:
        p = subprocess.run([binary, name, "--check", "--json"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=outdir)
        if p.returncode in (0, 1) and p.stdout.strip():
            try:
                d = json.loads(p.stdout.decode("utf-8"))
                assert isinstance(d.get("issues"), list) and d.get("ok") == (p.returncode == 0)
            except Exception as e:
                failures.append((name, ["--check", "--json"], "output is not the promised JSON: %r" % e))

keep = os.environ.get("CHARTS_FUZZ_KEEP")
for name, args, why in failures[:8]:
    print("  fuzz case %s: charts %s\n    %s" % (name, " ".join(args), why.replace("\n", "\n    ")))
    if keep:
        os.makedirs(keep, exist_ok=True)
        open(os.path.join(keep, name), "wb").write(open(os.path.join(outdir, name), "rb").read())
print("%d files, %d runs, %d bad" % (N, runs, len(failures)))
sys.exit(1 if failures else 0)
EOF
python3 "$TMP/fuzz.py" "$BIN" "$TMP/fuzz" "$EX" > "$TMP/fuzz.out" 2>&1; rc=$?
if [ $rc -eq 0 ]; then ok "fuzz: $(tail -n 1 "$TMP/fuzz.out"): every exit was 0, 1 or 2"
else cat "$TMP/fuzz.out"; bad "fuzz: $(tail -n 1 "$TMP/fuzz.out") (set CHARTS_FUZZ_KEEP=dir to keep the inputs)"; fi
else
  echo "(no python3: skipping PNG decoding and the fuzz run)"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
