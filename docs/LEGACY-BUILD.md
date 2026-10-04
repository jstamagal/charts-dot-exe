# FreeDOS and Open Watcom build

This describes the reproducible **32-bit DOS protected-mode** build procedure
for the C89 application and static library. It uses FreeDOS 1.4 from the
LiveCD, Open Watcom C 1.9 from the FreeDOS BonusCD, and the DOS/4GW extender
supplied with Watcom. It is not a segmented 16-bit build or a test on a
physical 386SX.

## Prepare the host

The staging script runs on Linux. It needs `curl`, `sha256sum`, `7z`, Python 3,
`truncate`, `stat`, `sfdisk`, `losetup`, `mkfs.vfat`, `fsck.fat`, `mount`,
`umount`, `mountpoint`, `pgrep`, `sudo`, and `qemu-system-i386`, plus basic
shell utilities such as `cp` and `mkdir`. The disk setup uses root privileges
through `sudo`. KVM is the default QEMU accelerator; set
`LEGACY_ACCEL=tcg` if KVM is unavailable. The default guest has 512 MiB RAM
and a Pentium III CPU. A separate 16 MiB QEMU run is described below;
neither setting measures performance on a physical 386SX.

From the repository root:

```sh
LEGACY_FPFLAG=fpc tools/legacy-prepare.sh /tmp/libcharts-legacy
LEGACY_DISPLAY=gtk tools/legacy-qemu.sh /tmp/libcharts-legacy
```

The first command downloads these official FreeDOS archives if absent and
checks their SHA-256 digests before extracting them:

| Archive | URL | SHA-256 |
| --- | --- | --- |
| LiveCD | `https://download.freedos.org/1.4/FD14-LiveCD.zip` | `2020ff6bb681967fd6eff8f51ad2e5cd5ab4421165948cef4246e4f7fcaf6339` |
| BonusCD | `https://download.freedos.org/1.4/FD14-BonusCD.zip` | `59ef3c2a3011862f408f2f5dba725c08532674b99f0e16451e06cb6de73defa8` |

It extracts `packages/devel/watcomc.zip` from the BonusCD. The FreeDOS
package identifies its compiler as Open Watcom C 1.9. The script copies the
current C sources and short headers into a DOS 8.3-safe `CHARTS` tree, then
creates a 256 MiB raw IDE disk with a standard MBR, one bootable FAT16
partition starting at sector 2048, and volume label `CHARTS`. It copies
`DEVEL` and `CHARTS` onto that partition and runs a read-only FAT check.
The LiveCD boots FreeDOS; the IDE disk appears as `C:`.

**Preparing again replaces `freedos-disk.img` and any guest output on it.**
The script refuses to replace the disk while mounted or while its QEMU process
is running. Copy out any results before another preparation.

QEMU defaults to `LEGACY_DISPLAY=none`; `gtk` above requests a visible guest
screen and requires a QEMU build with GTK display support. For a headless
guest, run `tools/legacy-qemu.sh /tmp/libcharts-legacy` and, after FreeDOS
reaches its prompt, type commands via the local monitor socket:

```sh
python3 tools/legacy-send.py /tmp/libcharts-legacy/monitor.sock \
  'C:'
# Wait for the guest to reach its C:\ prompt before sending the next commands.
python3 tools/legacy-send.py /tmp/libcharts-legacy/monitor.sock \
  'CD \CHARTS' 'BUILD.BAT'
```

`LEGACY_ACCEL`, `LEGACY_CPU`, `LEGACY_MEMORY`, `LEGACY_DISPLAY`,
`LEGACY_DISK`, and `QEMU` override the emulator settings in
`tools/legacy-qemu.sh`. The default CPU is `pentium3`; setting a different
CPU does not by itself establish that the build runs acceptably on that CPU.
`LEGACY_FPFLAG=fpc` selects Watcom's library-call floating-point mode when
staging, and is the default. An explicit `LEGACY_FPFLAG=fpi` is available for
hardware-FPU comparisons, but do not run its forced-`NO87` numeric test:
that path hangs in the Watcom 1.9 emulator. The verified full-demo procedure
uses `fpc`. The flag is recorded in the generated
`BUILD.BAT`; preparing a new disk replaces the old one.

## Build and test in FreeDOS

At the FreeDOS prompt, run:

```text
C:
CD \CHARTS
FPCPROBE.BAT
TYPE C:\FPC.TXT
BUILD.BAT
TYPE C:\BUILD.TXT
TEST.BAT
TYPE C:\TEST.TXT
```

`BUILD.BAT` sets Watcom's `PATH` and `INCLUDE`, compiles every library and
application translation unit with `wcl386 -bt=dos -3 -fpc -za -ox`, archives
the library with `wlib`, and links `CHARTS.EXE` with `wlink system dos4g`
and a 256 KiB stack (`option stack=256k`; the DOS/4GW default is 64 KiB).
The linker response uses `file` directives for all application objects and
`CHARTS.LIB`. The extender `DOS4GW.EXE` is in Watcom's `BINW` directory on
the batch file's `PATH`. The `-3` target permits 386 instructions; the
generated binary still needs DOS/4GW and a DOS runtime environment. The
guest writes `C:\BUILD.TXT` with a PASS or FAIL marker.

For use on a separate DOS computer, copy `CHARTS.EXE` and `DOS4GW.EXE`
together (or put the extender on `PATH`), plus the deck and data files.
`CHARTS.LIB` is for linking other programs; `CHARTS.MAP` is for inspection.

The current `TEST.BAT` runs `CHARTS.EXE --demo --check`, renders all six demo
slides at an 80-by-24 character canvas into `HARD` with normal math, checks
the staged `EXAMPLES` files (`DEMO.JSN` with its three CSVs, `DECK.JSN` with
`REVENUE.CSV`, `QUARTER.CSV` and `TRAFFIC.TSV`), exports `DECK.JSN` into
`FILE`, renders the demo again into `SOFT` with `NO87=1`, then compiles,
links, and runs the DOS safe-save test.
It writes `C:\TEST.TXT` with a PASS marker only after those commands return
successfully. It does **not** decode or compare the PNGs, so copy both sets
from the stopped guest and compare them on the host. Its `SLIDE-01.PNG`
through `SLIDE-06.PNG` names fit FAT 8.3. To isolate the floating-point mode
before a full build, `FPCPROBE.BAT` compiles and runs a small `-fpc`/`NO87`
probe, then writes `C:\FPC.TXT` with a PASS or FAIL marker.

For a separate manual VGA check, run:

```text
SET NO87=1
CHARTS.EXE --demo --gfx vga
SET NO87=
```

Exit the presenter with `q`.

### Run with 16 MiB

The LiveCD's default Live Environment boots a small FreeDOS image, but its
`FDAUTO.BAT` then tries RAM disks from 384 MiB down to 1 MiB and, on its
large-RAM path, starts package setup. At 16 MiB, that normal startup path
left too little memory for DOS/4GW (`1307`); it is not a measurement of the
chart application's minimum memory. The LiveCD menu has no separate
minimal-live entry.

The following run booted a minimal FreeDOS prompt by using F5, which
[FreeDOS documents](https://help.fdos.org/en/hhstndrd/cnfigsys/index.htm)
as skipping `FDCONFIG.SYS` and `FDAUTO.BAT`:

```sh
LEGACY_MEMORY=16 LEGACY_ACCEL=tcg LEGACY_CPU='486,-fpu' \
  LEGACY_DISPLAY=none tools/legacy-qemu.sh /tmp/libcharts-legacy
```

At the ISOLINUX menu, choose the default **Live Environment** and press
Enter. Press F5 repeatedly during the transition to the FreeDOS kernel;
the key is accepted after BIOS keyboard setup and before startup files are
read. Confirm that the screen says `Skipping CONFIG.SYS/AUTOEXEC.BAT` and
lands at an `A:` prompt without the RAM-disk or package setup. Then run:

```text
C:
CD \CHARTS
SET PATH=C:\DEVEL\WATCOMC\BINW
SET NO87=1
CHARTS.EXE --demo --check
TEST.BAT
SET NO87=1
CHARTS.EXE --demo --gfx vga
```

`TEST.BAT` sets its own paths and resets `NO87` while running both normal
and software-floating-point exports; set `NO87=1` again for VGA. In
the verified 16 MiB guest, the check reported six slides with no errors or
warnings, the test reached its save PASS marker, and the numeric VGA chart
rendered; `q` returned to the DOS prompt. This run used QEMU TCG with its
`486,-fpu` CPU setting. A separate boot probe showed that QEMU can still
execute x87 arithmetic with `-fpu`, so that setting alone is not a no-FPU
test. `NO87=1` exercised Watcom's software floating-point path; physical
386SX behavior and speed remain untested.

The [Open Watcom 1.9 guide](https://open-watcom.github.io/open-watcom-1.9/cguide.html#The_NO87_Environment_Variable)
states that `-fpi` links the 80x87 emulator, `-fpc` uses math-library calls,
and `NO87` directs supported math runtimes to ignore a present coprocessor.
In a separate QEMU 11.1.1 TCG boot probe, `-cpu 486,-fpu` reported
CPUID.FPU=0 but an x87 addition still computed 1+1=2; the QEMU switch alone
therefore cannot prove execution without x87 instructions. The `NO87` test
also cannot replace a run on a physical 386SX without an FPU. For a `-fpi`
build, inspect `C:\CHARTS\CHARTS.MAP` for `DOS\EMU387.LIB` to verify that
the emulator was linked, and treat its presence separately from a successful
runtime test.

The DOS application reads keys through the BIOS keyboard service, preserving
Ctrl-C for the presenter instead of letting DOS terminate the process. It
accepts ASCII characters from the keyboard: DOS text input uses an OEM code
page, not UTF-8, and no code-page conversion is implemented. The adapter
maps scan codes for arrow and function keys; right-arrow slide navigation,
Ctrl-C cancellation of a title edit, and `q` exit have been exercised in the
`-fpc` guest. UTF-8 deck text belongs in transferred files, which the parser
and renderer can read. The automated keyboard sender also handles ASCII DOS
commands. Source and guest build filenames stay within 8.3. `charts.h` is
the public DOS header, while the longer `libcharts.h` is only a compatibility
wrapper for other filesystems. Deck and data files transferred to FAT should
likewise use 8.3 names. DOS defaults to `.DCK` for an implicit deck;
explicit JSON deck files can use `.DCK` or `.JSN`.

## Read the results

Stop QEMU before mounting the image on the host. On Linux, a read-only mount
of its partition can then expose `BUILD.TXT`, `TEST.TXT`, `FPC.TXT`,
`CHARTS.EXE`, `CHARTS.LIB`, `CHECK.TXT`, and the `HARD`/`SOFT` PNG directories:

```sh
mkdir -p /tmp/libcharts-legacy/inspect
sudo mount -o ro,loop,offset=1048576 \
  /tmp/libcharts-legacy/freedos-disk.img /tmp/libcharts-legacy/inspect
cat /tmp/libcharts-legacy/inspect/BUILD.TXT
cat /tmp/libcharts-legacy/inspect/TEST.TXT
sudo umount /tmp/libcharts-legacy/inspect
```

An earlier writable `vvfat` staging attempt was discarded after a guest
readback of an archive showed zero-filled bytes after offset 8192 and invalid
object data, while the host archive was intact. That observation does not
isolate a QEMU backend fault. The normal raw MBR/FAT16 IDE disk above avoids
relying on that staging path.

## Verification status

The downloads, archive digests, source staging, MBR/FAT16 preparation, and
LiveCD boot to a `C:` prompt have been exercised. The first complete `-fpi`
guest build wrote `WATCOM_19_WCL386_DOS32_BUILD_PASS` and produced
`CHARTS.EXE` (418,096 bytes), `CHARTS.LIB` (166,912 bytes), and `CHARTS.MAP`.
Its basic demo check reported six slides with zero errors or warnings. Its
title-card `DEMO.PNG` and a `NO87` title-card `SOFT.PNG` were read back and
decoded on the host: both are 640-by-384 indexed PNGs and byte-identical to
the host title-card export. The `-fpi` build then hung under `NO87` on the
first numeric chart inside Open Watcom 1.9's `EMU387` integer-to-float
conversion routine (`__I8LD`); source inspection and guest instruction
samples agree.

A separate full 24-translation-unit `-fpc` guest build has now completed.
It wrote `WATCOM_19_WCL386_DOS32_BUILD_PASS`, with a 414,140-byte executable
and 180,736-byte library. Its expanded `TEST.BAT` wrote
`WATCOM_19_DOS32_DEMO_CHECK_HARD_SOFT_SAVE_PASS`: `CHECK.TXT` says
`ok: 6 slides, 0 errors, 0 warnings`, and the DOS save test printed
`SAVE_REPLACE_PASS`. The separate `-fpc`/`NO87` probe printed
`COUNT 12 SQRT 12`. All six normal-math PNGs and all six `NO87` PNGs were
read back from the guest, independently decoded as 640-by-384 indexed PNGs,
and are byte-identical to each other and to six host exports. Their respective
byte lengths are 3,079; 5,876; 5,034; 4,632;
4,217; and 3,578. Under `NO87`, the VGA presenter displayed a chart,
right-arrow advanced from slide 2/6 to 3/6, and `q` restored the DOS prompt.

The original DOS `getch()` adapter let Ctrl-C in the title editor terminate
the process. With the BIOS keyboard adapter, a guest retest showed Ctrl-C
canceling the edit while leaving slide 2 unchanged; the before and after
screen captures were byte-identical. Right-arrow advanced from slide 2 to 3,
and `q` returned to the DOS prompt. Those full-build runs used 512 MiB and
QEMU's Pentium III KVM setting. A separate 16 MiB TCG `486,-fpu` run, booted
with F5 to bypass the LiveCD's RAM-disk startup, passed the six-slide check,
the HARD/SOFT PNG and DOS save test, and a numeric VGA display with `NO87=1`.
There is no claim here that the program has run on physical VGA hardware,
a 386SX without an FPU, historical BSD, or a GCC 2.95 Linux guest.

The BIOS-keyboard build, including the portable floating-point range
sentinel, was 414,124 bytes for `CHARTS.EXE` and 180,736 bytes for `CHARTS.LIB`.
It passed another six-slide check, six PNG exports, and numeric VGA display
with `NO87=1`.

Every run above used `--demo`. A review then found that any deck or data file
on the command line aborted with `Stack Overflow!`: the deck sniffer kept a
64 KiB buffer on the stack, which is the whole DOS/4GW default stack. That was
reproduced in the guest with a one-kilobyte deck. The buffer is now static, the
link sets `option stack=256k`, and `TEST.BAT` checks file inputs. With 4 MiB
of RAM, `--check` on the demo deck from files also ran out of memory at its
default 120 x 33 canvas; on DOS that default is now the VGA slide area,
80 x 29.

That build was 414,160 bytes for `CHARTS.EXE`. Its `TEST.BAT` wrote
`WATCOM_19_DOS32_DEMO_FILES_HARD_SOFT_SAVE_PASS`. `CHECK.TXT` reports the
demo (6 slides), the demo deck from files (5 slides, 7 label-fit warnings at
80 columns), a deck with a CSV (3 slides), a CSV and a TSV, all with no errors.
The six normal, six `NO87` and three deck-file PNGs are byte-identical to the
Linux exports. `SYS` and `FDISK /MBR` from the LiveCD then made a 420 MiB
FAT16 image with CHS 854/16/63 bootable, partition at sector 63. Booted alone
as the IDE master with 4 MiB RAM, no HIMEM, and QEMU TCG `486,-fpu`, it passed
the same four checks and presented all five slides of the demo deck from files
in VGA. QEMU still executes x87 instructions with `-fpu`, so the target
configuration, an 86Box 386SX at 25 MHz with no coprocessor, is the
acceptance test for a 386SX without an FPU.

### A 386SX without an FPU

The disk image above then ran in 86Box on an emulated 386SX at 25 MHz with
no coprocessor: an Acer-style 325AX board with an AMI BIOS (1991), 4 MiB of
RAM, a Trident TVGA8900 and an XT-IDE Universal BIOS r631 serving the IDE
disk. The BIOS reports "Numeric Processor: None". With hard disk C in CMOS set
to type 47 for the same drive, FreeDOS listed it twice, as `C:` and `D:`,
because the AMI BIOS and XT-IDE both answered for it; setting CMOS hard
disk C to "Not Installed" leaves XT-IDE alone and only `C:`. All four
`--check` runs passed and the VGA presenter showed every slide of both decks.
Its full 640 x 480 frames match the earlier builds pixel for pixel.

Without an FPU every double operation is a library routine, and that made
it slow: a pie slide took two to three minutes under `--check`, and a VGA
page turn of the demo deck from files 13 to 33 s. A build that logged the
time of each phase of a page showed four costs: an `atan2` and a dozen
double operations for every pixel of a pie; a VGA blit that tested every
pixel against every plane (11 s a page); double clipping for every lit
pixel of text; and, on 3-D pies, an upward search from every empty pixel for
the wall above it. Each was replaced by integer or per-row work with the
same pixels, checked against the oracle and random decks. A page turn now
takes 1.7 to 8 s on that machine: about 1.4 s of blit, 1.3 to 1.5 s of
composing the image and up to 5 s of rendering, most of it for the 3-D
donut. A pie slide under `--check` takes about 20 s.

QEMU can stand in for that machine when timing: TCG with `-icount shift=7`
and `NO87=1` set in the guest ran the same build's phases a steady 2.9
times faster than the 86Box 386SX-25, slide by slide, so the guest's
`clock()` times scaled by 2.9 predicted the 86Box times to within 0.1 s.
