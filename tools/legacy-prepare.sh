#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BASE=${1:-${TMPDIR:-/tmp}/libcharts-legacy}
mkdir -p "$BASE" "$BASE/live" "$BASE/bonus" "$BASE/bonus-files" "$BASE/share"

fetch() {
    url=$1
    file=$2
    if [ ! -f "$file" ]; then
        curl --fail --location --retry 3 --output "$file" "$url"
    fi
}

fetch https://download.freedos.org/1.4/FD14-LiveCD.zip "$BASE/FD14-LiveCD.zip"
fetch https://download.freedos.org/1.4/FD14-BonusCD.zip "$BASE/FD14-BonusCD.zip"

printf '%s  %s\n' 2020ff6bb681967fd6eff8f51ad2e5cd5ab4421165948cef4246e4f7fcaf6339 "$BASE/FD14-LiveCD.zip" | sha256sum --check
printf '%s  %s\n' 59ef3c2a3011862f408f2f5dba725c08532674b99f0e16451e06cb6de73defa8 "$BASE/FD14-BonusCD.zip" | sha256sum --check

if [ ! -f "$BASE/live/FD14LIVE.iso" ]; then
    7z x -y -o"$BASE/live" "$BASE/FD14-LiveCD.zip" FD14LIVE.iso
fi
if [ ! -f "$BASE/bonus/FD14BNS.iso" ]; then
    7z x -y -o"$BASE/bonus" "$BASE/FD14-BonusCD.zip" FD14BNS.iso
fi
if [ ! -f "$BASE/bonus-files/packages/devel/watcomc.zip" ]; then
    7z x -y -o"$BASE/bonus-files" "$BASE/bonus/FD14BNS.iso" packages/devel/watcomc.zip
fi
if [ ! -f "$BASE/share/DEVEL/WATCOMC/BINW/WCL386.EXE" ]; then
    7z x -y -o"$BASE/share" "$BASE/bonus-files/packages/devel/watcomc.zip"
fi

python3 "$ROOT/tools/legacy-stage.py" "$ROOT" "$BASE/share"

DISK_SIZE=${LEGACY_DISK_SIZE:-256M}
DISK="$BASE/freedos-disk.img"
MOUNT="$BASE/disk-mount"
if mountpoint -q "$MOUNT"; then
    echo "refusing to replace mounted legacy disk: $MOUNT" >&2
    exit 1
fi
if pgrep -f '[q]emu-system.*freedos-disk.img' >/dev/null; then
    echo "refusing to replace legacy disk while QEMU is running" >&2
    exit 1
fi
mkdir -p "$MOUNT"
rm -f "$DISK"
truncate -s "$DISK_SIZE" "$DISK"
disk_sectors=$(( $(stat -c %s "$DISK") / 512 ))
partition_sectors=$((disk_sectors - 2048))
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%s, type=6, bootable\n' \
    "$partition_sectors" | sfdisk --wipe always "$DISK"
loop_bytes=$(( $(stat -c %s "$DISK") - 1048576 ))
loopdev=$(sudo losetup --find --show --offset 1048576 --sizelimit "$loop_bytes" "$DISK")
cleanup() {
    if mountpoint -q "$MOUNT"; then
        sudo umount "$MOUNT"
    fi
    if [ -n "${loopdev:-}" ]; then
        sudo losetup -d "$loopdev"
        loopdev=
    fi
}
trap cleanup EXIT HUP INT TERM
sudo mkfs.vfat -F 16 -n CHARTS -h 2048 "$loopdev"
sudo mount "$loopdev" "$MOUNT"
sudo cp -r "$BASE/share/DEVEL" "$MOUNT/"
sudo cp -r "$BASE/share/CHARTS" "$MOUNT/"
cleanup
loopdev=$(sudo losetup --find --show --offset 1048576 --sizelimit "$loop_bytes" "$DISK")
sudo fsck.fat -vn "$loopdev" >/dev/null
cleanup
trap - EXIT HUP INT TERM
printf 'Prepared FreeDOS/Open Watcom disk image at %s\n' "$DISK"
