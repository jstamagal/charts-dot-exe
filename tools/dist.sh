#!/bin/sh
# Build charts for one target and pack it for a release:
#   tools/dist.sh <target>     (or: make dist TARGET=<target>)
#   tools/dist.sh --list       the targets this script knows
# Output: dist/charts-<version>-<target>.tar.gz (.zip for FreeDOS) holding
# the program, README.md, LICENSE, the man page and the agent skill.
#
# Unix targets cross-compile with `zig cc` (static musl on Linux). Set CC to
# use another compiler, e.g. CC="clang -arch arm64" on a Mac.
# FreeDOS builds with Open Watcom v2 from $WATCOM (32-bit, DOS/4GW extender).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGETS="x86_64-linux-musl aarch64-linux-musl armv7-linux-musleabihf i686-linux-musl
riscv64-linux-musl x86_64-freebsd aarch64-macos x86_64-macos i386-freedos"

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
    i386-freedos)            ZIG= ;;
    *) echo "dist: unknown target $TARGET (tools/dist.sh --list)" >&2; exit 2 ;;
esac

if [ "$TARGET" = i386-freedos ]; then
    # Open Watcom v2 on a Unix host. Its headers do not compile under -za
    # (strict ANSI) the way Watcom 1.9's do, so this build keeps extensions
    # on; docs/LEGACY-BUILD.md has the FreeDOS-hosted 1.9 build.
    : "${WATCOM:?dist: set WATCOM to an Open Watcom v2 install}"
    case $(uname -s)-$(uname -m) in
        Linux-x86_64) owbin=$WATCOM/binl64 ;;
        Linux-*) owbin=$WATCOM/binl ;;
        *) owbin=$WATCOM/binnt64 ;;
    esac
    PATH=$owbin:$PATH; INCLUDE=$WATCOM/h; export PATH INCLUDE WATCOM
    mkdir -p "$WORK/obj"
    {
        echo "system dos4g"; echo "option quiet"; echo "option stack=256k"
        echo "name $WORK/CHARTS.EXE"
    } > "$WORK/link.rsp"
    for src in $SOURCES; do
        obj=$WORK/obj/$(basename "$src" .c).obj
        wcl386 -q -bt=dos -3 -fpc -ox -c -i="$ROOT/include" -i="$ROOT/app" -i="$ROOT/lib" \
            -fo="$obj" "$ROOT/$src"
        echo "file $obj" >> "$WORK/link.rsp"
    done
    wlink @"$WORK/link.rsp"
    # 8.3 names throughout: FAT without long names is the target.
    pkg=$WORK/CHARTS
    mkdir -p "$pkg"
    cp "$WORK/CHARTS.EXE" "$pkg/CHARTS.EXE"
    cp "$WATCOM/binw/dos4gw.exe" "$pkg/DOS4GW.EXE"
    cp "$ROOT/README.md" "$pkg/README.MD"
    cp "$ROOT/LICENSE" "$pkg/LICENSE"
    cp "$ROOT/docs/charts.1" "$pkg/CHARTS.1"
    rm -f "$OUT/$NAME.zip"
    (cd "$WORK" && zip -q -X -r "$OUT/$NAME.zip" CHARTS)
    echo "$OUT/$NAME.zip"
    exit 0
fi

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
