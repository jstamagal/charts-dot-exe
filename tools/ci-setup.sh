#!/bin/sh
# Install pinned build tools on a GitHub Actions Ubuntu runner:
#   tools/ci-setup.sh zig qemu watcom dosbox    (any of them)
# Each download is checked against its SHA-256 before use.
set -eu

ZIG_VERSION=0.16.0
ZIG_SHA256=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00
OW_BUILD=2026-10-01-Build
OW_SHA256=e6aa1b1e40ac8bbf97658d2c70fff8a4242d6ca4a1c60806f2baa5317083d4fe

TOOLS=${RUNNER_TEMP:-/tmp}/charts-tools
mkdir -p "$TOOLS"
fetch() { # url sha256 file
    curl --fail --location --retry 3 --silent --show-error --output "$3" "$1"
    printf '%s  %s\n' "$2" "$3" | sha256sum --check --quiet
}
addpath() { echo "$1" >> "${GITHUB_PATH:-/dev/null}"; PATH=$1:$PATH; }
apt_updated=
apt() {
    [ -n "$apt_updated" ] || { sudo apt-get update -qq; apt_updated=1; }
    sudo apt-get install -y -qq "$@"
}

for tool in "$@"; do
    case $tool in
        zig)
            fetch "https://ziglang.org/download/$ZIG_VERSION/zig-x86_64-linux-$ZIG_VERSION.tar.xz" \
                "$ZIG_SHA256" "$TOOLS/zig.tar.xz"
            tar -xJf "$TOOLS/zig.tar.xz" -C "$TOOLS"
            addpath "$TOOLS/zig-x86_64-linux-$ZIG_VERSION"
            zig version ;;
        qemu)
            # binfmt registration lets the smoke test run foreign binaries directly.
            apt qemu-user-static binfmt-support ;;
        watcom)
            fetch "https://github.com/open-watcom/open-watcom-v2/releases/download/$OW_BUILD/ow-snapshot.tar.xz" \
                "$OW_SHA256" "$TOOLS/ow.tar.xz"
            mkdir -p "$TOOLS/watcom"
            tar -xJf "$TOOLS/ow.tar.xz" -C "$TOOLS/watcom"
            echo "WATCOM=$TOOLS/watcom" >> "${GITHUB_ENV:-/dev/null}" ;;
        dosbox)
            apt dosbox ;;
        *) echo "ci-setup: unknown tool $tool" >&2; exit 2 ;;
    esac
done
