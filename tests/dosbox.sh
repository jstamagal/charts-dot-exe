#!/bin/sh
# tests/dosbox.sh -- run a FreeDOS release archive in DOSBox, without a window:
# --version, --demo --check, and the six demo slides as PNGs, which must be
# byte-identical to the same export from a host build.
# usage: tests/dosbox.sh dist/charts-<version>-i386-freedos.zip ./charts
set -eu

ZIP=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HOST=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/charts-dosbox.XXXXXX")
cd "$WORK"
unzip -q "$ZIP"
cd CHARTS

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 600 dosbox -noconsole \
    -c "mount c $WORK/CHARTS" -c "c:" \
    -c "CHARTS.EXE --version > VER.TXT" \
    -c "CHARTS.EXE --demo --check > CHECK.TXT" \
    -c "CHARTS.EXE --demo --png-dir HARD --width 80 --height 24" \
    -c "exit" > dosbox.log 2>&1 || { cat dosbox.log; echo "dosbox failed"; exit 1; }

fail=0
version=$("$HOST" --version)
dosver=$(tr -d '\r' < VER.TXT)
echo "DOS: $dosver"
[ "$dosver" = "$version" ] || { echo "FAIL: DOS says [$dosver], host [$version]"; fail=1; }
tr -d '\r' < CHECK.TXT
grep -q '^ok: 6 slides, 0 errors' CHECK.TXT || { echo "FAIL: --demo --check"; fail=1; }

"$HOST" --demo --png-dir "$WORK/host" --width 80 --height 24 > /dev/null
n=0
for f in "$WORK"/host/*.png; do
    dos=HARD/$(basename "$f" | tr a-z A-Z)
    if cmp -s "$f" "$dos"; then n=$((n+1)); else echo "FAIL: $dos differs from the host PNG"; fail=1; fi
done
echo "$n PNGs byte-identical to the host build"
[ "$n" -gt 0 ] || fail=1
exit $fail
