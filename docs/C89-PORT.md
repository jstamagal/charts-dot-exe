# C89 port work record

The requested result is a C89 `libcharts` and a C89 `charts` application.
The application remains a JSON/CSV-driven presenter and editor. The library
must be usable without a terminal, operating-system headers, C++ runtime,
or external graphics/compression libraries.

The baseline is commit `8dfe182`. The original C++ source stays in `src/` as
the comparison oracle: `make test-oracle` builds it and runs the
`tests/*_compare.cpp` differential checks against the C89 library.

## Boundaries

* `include/charts.h`: C89 public API, explicit ownership, borrowed input
  arrays and strings, allocator and byte-output callbacks, status codes.
* `lib/`: raster primitives, bitmap font, cells, scene composition, chart and
  diagram rendering, slide layout, and hand-written output encoders.
* `app/`: owned JSON/data/deck models, validation, file loading and safe saving,
  CLI, presenter/editor, terminal discovery and platform display adapters.
* Linux framebuffer/VT operations remain an optional platform adapter.
  Portable raster output accepts caller memory and pitch; no device file or
  Linux ioctl is part of the library interface.

Library state belongs to a context or scene. Rendering must not alter the
caller's data, open files, manipulate signals, call `exit`, or hide an output
callback failure. Memory products and capacity growth are checked before
allocation. C89 portability means avoiding C99 headers, syntax, formatting
and math functions, as well as accidental assumptions about `int` width.
Octet image formats require an explicitly documented eight-bit byte.

## Preservation oracle

The baseline includes all twelve chart kinds, annotations/error bars,
palettes/themes, diagrams, nested slide layout, CSV and JSON shapes, deck
diagnostics, PNG output, and interactive editing/save/reload behavior.
The existing smoke, random-input, and PTY suites remain regression checks.
Rendered images and command output are compared with the original executable.
Any intentional output or behavior difference must be recorded with its
reason and a regression check; passing compilation alone is insufficient.

## Verification gates

* GCC and Clang strict C89 compilation with warnings enabled.
* Standalone public-header and C client builds without a C++ linker.
* Existing CLI, random-input, and PTY tests.
* Address/undefined-behavior sanitizers on parsers, rendering, and editing.
* Allocation failure, checked size arithmetic, and failed-output checks.
* Original/ported output comparisons, including image pixels.
* Library symbol/dependency inspection and install/use check.
* Representative timings and memory/binary measurements; no performance
  improvement claim without measured before/after evidence.

Historical BSD, segmented-memory targets, real VGA hardware, and other
architectures require their own toolchain or runtime checks. A portable
source design or a host C89 build must not be presented as having tested them.

## Migration status

The original baseline passed 481 CLI/fuzz checks and 90 PTY checks. The C89
library and entire C89 application are implemented. The default build uses
only C, and `charts` links against `libcharts.a`. The original C++ source in
`src/` is retained solely as a differential test oracle.

Host verification on Linux x86-64 (GCC 14.2 and Clang):

* All production translation units build with `-std=c89 -pedantic-errors
  -Wall -Wextra -Wdeclaration-after-statement -Werror` under both compilers.
* `make test`: CLI/fuzz 490 passed, presenter/editor 101 passed, plus the core,
  output and platform checks. `make test-asan` runs the same CLI and PTY suites
  under ASan/UBSan with leak checks. They include same-second/same-size deck and
  CSV rewrites, Unicode editing, series insertion/deletion/undo, save/reload,
  terminal signals, Linux framebuffer 16/32, Kitty and sixel.
* `make test-oracle` compares against the original: nine primitive scenes with
  1,800 randomized operations, 756 chart scenes, 21 diagram scenes, 225
  slide/layout scenes, 66 word-wrap cases and 50 generated PNG codec cases.
  `tests/png_parity.sh` then exports 48 complete PNGs from the demo, every
  example deck and data file, a multi-file run and `--tile`, at two sizes, with
  both executables; all match byte for byte.
* `make test-stress` runs 1,200 chart allocation-failure positions and 576
  extreme numeric cases (the latter check for crashes only). Sanitizer runs of
  these were made during the port; there is no sanitizer target for them.
* `tests/fuzz.py` passes its fixed seed in `make test`; seeds 424242, 7 and 99
  at 3,000 files each also pass.
* An installed standalone C89 client builds against the installed headers and
  archive. Dynamic `charts` depends on libc and libm, with no C++ runtime.

Intentional differences from the original, each with a regression check in
`tests/smoke.sh`: block weights whose sum overflows a double are compared
relative to the largest (the original squeezed every such block out with "no
room left"), and scatter data spanning most of the double range is drawn
without padding instead of computing an infinite axis. On DOS, `--check` and
`--png` default to the VGA slide area (80 x 29 cells) instead of 120 x 33.

FreeDOS verification uses the stock FreeDOS 1.4 BonusCD Open Watcom C 1.9
compiler and DOS/4GW 1.97. All 24 production translation units compile in the
guest with `-bt=dos -3 -fpc -za -ox`; the app links the separate `CHARTS.LIB`
with a 256 KiB stack. `TEST.BAT` checks the built-in demo and, from files, the
demo deck with its three CSVs, a deck with a CSV, a CSV and a TSV; it exports
the demo with normal and `NO87` floating point and a deck file to PNG, and runs
the DOS safe-save test. All 15 exported PNGs match the Linux build byte for
byte. A bootable 420 MiB FAT16 disk (CHS 854/16/63) with FreeDOS and the
program then passed the same checks and the VGA presenter booted on its own
with 4 MiB RAM and QEMU's TCG `486,-fpu` CPU. QEMU still executes x87
instructions under `-fpu`, so that run does not prove execution without a
coprocessor; see [LEGACY-BUILD.md](LEGACY-BUILD.md) for the hardware status.

The DOS build deliberately selects Watcom's standard `-fpc` library-call
mode. Its `-fpi` software x87 emulator hung on an unsigned-integer conversion;
the supported alternative needed no compiler patch or chart arithmetic
workaround. The DOS keyboard adapter uses BIOS input so Ctrl-C reaches the
editor. Keyboard character entry is currently ASCII-only; transferred UTF-8
deck text still renders. The legacy guide records the configuration and
observed failures as well as the successful checks.

## Resource work

The pixel compositor clips once and uses an integer-only inner loop for
native-resolution layers. Callers can reuse an indexed pixel buffer with
padding via `lc_scene_draw_image`. The Linux framebuffer adapter uses a row
buffer instead of allocating another full RGB frame. PNG match history uses
32,768 circular entries, bounded by the deflate window, instead of one entry
per input byte. CRC uses a small immutable table; Adler reductions are batched.
All output parity checks above include these changes.

The host executable is 528,552 bytes versus 1,130,248 bytes for the original
optimized build. A sample benchmark renders the six demo slides to PNG in
20 alternating warmed runs per executable (`tools/bench.c`, GCC `-O2`). The
host was shared with other work, so elapsed wall time is scheduling-sensitive:

* original: wall 30.70 ms, peak RSS 8464 KiB, CPU 30.37 ms
* C89: wall 27.34 ms, peak RSS 4836 KiB, CPU 27.03 ms

These are measurements on the development host, not speed or memory claims
for a 386SX. DOS execution and historical compiler results are recorded in
[LEGACY-BUILD.md](LEGACY-BUILD.md). Real 386 hardware, segmented 16-bit builds,
historical BSD, and a GCC 2.95 Linux guest still need their own validation.
