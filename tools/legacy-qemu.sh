#!/bin/sh
set -eu
BASE=${1:-${TMPDIR:-/tmp}/libcharts-legacy}
QEMU=${QEMU:-qemu-system-i386}
ACCEL=${LEGACY_ACCEL:-kvm}
qemu_display=${LEGACY_DISPLAY:-none}
qemu_memory=${LEGACY_MEMORY:-512}
qemu_cpu=${LEGACY_CPU:-pentium3}
test -f "$BASE/live/FD14LIVE.iso"
DISK=${LEGACY_DISK:-$BASE/freedos-disk.img}
test -f "$DISK"
exec "$QEMU" \
    -name libcharts-freedos-watcom \
    -m "$qemu_memory" -smp 1 -machine pc -accel "$ACCEL" -cpu "$qemu_cpu" \
    -vga std -display "$qemu_display" -no-reboot -no-shutdown \
    -drive "file=$DISK,format=raw,if=ide,index=0" \
    -cdrom "$BASE/live/FD14LIVE.iso" -boot order=d -nic none \
    -monitor "unix:$BASE/monitor.sock,server=on,wait=off" \
    -serial "file:$BASE/serial.log"
