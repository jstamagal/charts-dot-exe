#!/bin/sh
# tests/png_parity.sh NEW ORIGINAL -- every example deck, data file, multi-file
# and tiled run exported to PNG by both executables must match byte for byte.
# usage: tests/png_parity.sh ./charts build/charts-oracle
NEW=$1 OLD=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EX=$ROOT/examples
TMP=$(mktemp -d)
# rm is off limits here, as in smoke.sh; a trash can is fine
if command -v trash >/dev/null 2>&1; then trap 'trash "$TMP" 2>/dev/null || true' EXIT; fi
n=0 bad=0 run=0
compare() { # SIZE-ARGS RUN-ARGS...
  size=$1; shift
  run=$((run+1)); new=$TMP/$run/new old=$TMP/$run/old; mkdir -p "$TMP/$run"
  # shellcheck disable=SC2086
  "$NEW" "$@" --png-dir "$new" $size >/dev/null 2>&1
  # shellcheck disable=SC2086
  "$OLD" "$@" --png-dir "$old" $size >/dev/null 2>&1
  for f in "$old"/*.png; do
    [ -f "$f" ] || { bad=$((bad+1)); echo "  FAIL original wrote nothing: $*"; return; }
    n=$((n+1))
    cmp -s "$f" "$new/$(basename "$f")" || { bad=$((bad+1)); echo "  FAIL $* $size $(basename "$f")"; }
  done
}
for size in "" "--width 80 --height 24"; do
  compare "$size" --demo
  for f in deck.json demo/deck.json disk.json quarterly.csv revenue.csv traffic.tsv \
           demo/latency.csv demo/regions.csv demo/signups.csv; do
    compare "$size" "$EX/$f"
  done
  compare "$size" "$EX/quarterly.csv" "$EX/revenue.csv"
  compare "$size" --tile "$EX/quarterly.csv" "$EX/revenue.csv"
done
echo "png parity: $n complete PNGs compared, $bad differ"
[ "$bad" -eq 0 ]
