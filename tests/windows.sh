#!/usr/bin/env bash
# tests/windows.sh -- test a Windows release archive on Windows (Git Bash):
# --version, --demo --check, PNG export, a deck from files, the .cmd
# launcher, and the presenter in a real console (tests/conpty.py).
# usage: tests/windows.sh dist/charts-<version>-<target>.zip
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
unzip -q "$1" -d "$WORK"
dir=$(echo "$WORK"/charts-*)
exe=$dir/charts.exe
fail=0
step() { echo "== $*"; }
bad() { echo "FAIL $*"; fail=1; }

step version; "$exe" --version
step demo check; "$exe" --demo --check || bad "--demo --check"
step png export
"$exe" --demo --png-dir "$WORK/png" > /dev/null || bad "--png-dir"
n=$(ls "$WORK/png" | wc -l); [ "$n" -eq 6 ] || bad "6 PNGs, got $n"
head -c 8 "$WORK/png/slide-01.png" | od -An -tx1 | grep -q '89 50 4e 47 0d 0a 1a 0a' || bad "slide-01.png is not a PNG"
step deck from files; "$exe" "$ROOT/examples/deck.json" --check || bad "examples/deck.json --check"
step launcher
"$exe" "$ROOT/examples/deck.json" --launcher "$WORK/mydeck" || bad "--launcher"
[ -f "$WORK/mydeck.cmd" ] || bad "the launcher is not mydeck.cmd"
cmd //c "$(cygpath -w "$WORK/mydeck.cmd")" --check || bad "the .cmd launcher does not run charts"
step presenter in ConPTY
python "$ROOT/tests/conpty.py" "$(cygpath -w "$exe")" || bad "presenter"
exit $fail
