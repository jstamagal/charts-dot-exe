#!/bin/sh
# Build charts for one target and pack it for a release:
#   tools/dist.sh <target>     (or: make dist TARGET=<target>)
#   tools/dist.sh --list       the targets this script knows
# Output: dist/charts-<version>-<target>.tar.gz holding the program,
# README.md, LICENSE, the man page and the agent skill.
#
# Targets cross-compile with `zig cc` (static musl on Linux). Set CC to use
# another compiler, e.g. CC="clang -arch arm64" on a Mac.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGETS="x86_64-linux-musl aarch64-linux-musl armv7-linux-musleabihf i686-linux-musl
riscv64-linux-musl x86_64-freebsd aarch64-macos x86_64-macos"

if [ "${1:-}" = --list ]; then
    for t in $TARGETS; do echo "$t"; done
    exit 0
fi
TARGET=${1:?usage: tools/dist.sh <target> | --list}

VERSION=$(sed -n 's/^#define LC_VERSION "\(.*\)"$/\1/p' "$ROOT/include/charts.h")
[ -n "$VERSION" ] || { echo "dist: no LC_VERSION in include/charts.h" >&2; exit 1; }
SOURCES=$(make -s -C "$ROOT" --no-print-directory print-sources)
OUT=${DIST_DIR:-$ROOT/dist}
NAME=charts-$VERSION-$TARGET
WORK=$(mktemp -d "${TMPDIR:-/tmp}/charts-dist.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$OUT"

# The 32-bit x86 target uses SSE2 (Pentium 4 and later). With x87 maths
# (-mcpu=i686) clang's extended precision fails two smoke tests: "1,234.5"
# prints as "1,234", and --max 400 does not pin the axis.
CFLAGS="-O2 -ansi -pedantic -Wall -W"
EXE=charts
STRIP=-s
case $TARGET in
    x86_64-linux-musl)       ZIG=x86_64-linux-musl ;;
    aarch64-linux-musl)      ZIG=aarch64-linux-musl ;;
    armv7-linux-musleabihf)  ZIG=arm-linux-musleabihf ;;
    i686-linux-musl)         ZIG="x86-linux-musl -mcpu=pentium4" ;;
    riscv64-linux-musl)      ZIG=riscv64-linux-musl ;;
    x86_64-freebsd)          ZIG=x86_64-freebsd ;;
    aarch64-macos)           ZIG=aarch64-macos ;;
    x86_64-macos)            ZIG=x86_64-macos ;;
    *) echo "dist: unknown target $TARGET (tools/dist.sh --list)" >&2; exit 2 ;;
esac

if [ -n "${CC:-}" ] && [ "$CC" != cc ]; then
    COMPILE=$CC; STRIP=
else
    command -v zig >/dev/null 2>&1 || { echo "dist: needs zig (or set CC)" >&2; exit 1; }
    COMPILE="zig cc -target $ZIG"
fi
case $TARGET in *-linux-*) LINK=-static ;; *) LINK= ;; esac

pkg=$WORK/$NAME
mkdir -p "$pkg"
(cd "$ROOT" && $COMPILE -Iinclude -Iapp $CFLAGS $LINK $STRIP -o "$pkg/$EXE" $SOURCES -lm)
[ -n "$STRIP" ] || { command -v strip >/dev/null 2>&1 && strip "$pkg/$EXE" 2>/dev/null || true; }
cp "$ROOT/README.md" "$ROOT/LICENSE" "$pkg/"
cp "$ROOT/docs/charts.1" "$pkg/charts.1"
mkdir -p "$pkg/skill"
cp -R "$ROOT/skill/charts" "$pkg/skill/charts"

COPYFILE_DISABLE=1 tar -C "$WORK" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"
