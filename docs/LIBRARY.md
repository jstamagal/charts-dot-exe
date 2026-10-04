# Embedding libcharts

`make` builds `libcharts.a` and the C89 `charts` application. The public header
is `include/charts.h`; `libcharts.h` is a compatibility spelling. The canonical
source and header filenames fit DOS 8.3 filesystems. A C compiler, archiver,
standard C library, and mathematics library are sufficient for the library.

```sh
cc -ansi -Iinclude examples/embed.c libcharts.a -lm -o embed
./embed example.png
```

The example renders a chart into a caller-owned 640×480 indexed pixel buffer
and writes it using the library's PNG encoder. It has no application/parser,
terminal, or operating-system dependencies. `make install` installs both
headers and the static archive alongside the `charts` executable.

## Ownership and lifetime

Create a `lc_context`, then create scenes in it. Pass `NULL` as the allocator
for malloc/free, or provide allocation/free callbacks and your own user data.
Each context has its own state; separate contexts do not share mutable render
state. A single context must not be used concurrently.

Datasets, options, slide structures, strings, and arrays passed to rendering
are borrowed. They must remain valid during the call. Scene text is copied.
`lc_render_slide` writes each block's resulting rectangle for hit testing;
it does not change the dataset or chart options.

A series holds a `double` array and an optional parallel byte validity array.
A zero validity byte marks a gap. `valid == NULL` means that every finite
value is present. Missing data does not require an IEEE NaN representation.

Scratch allocations and numeric/text helper results live until
`lc_scratch_reset` or context destruction. Finish using borrowed scratch data
before resetting it. A normal frame lifetime is: create a scene, prepare
views, render, emit/display the result, destroy the scene, reset scratch.
Destroy all scenes before destroying their context.

`lc_scene_to_image` and `lc_scene_to_cells` allocate their outputs. Release
these with `lc_free(context, pointer)`. `lc_scene_draw_image` instead accepts
caller-owned memory: width must be `scene_cols * 8`, height must be
`scene_rows * 16`, stride must be at least width, and the allocation must hold
`stride * height` bytes. Pixels are indices 0–15 into `lc_vga_rgb`. Padding
bytes are left untouched. A packed indexed framebuffer can use this memory
directly; planar VGA and RGB framebuffer formats need a device adapter.

## Output and errors

PNG, sixel, Kitty, and ANSI cell encoders write through `lc_sink`. Its callback
returns zero on success and nonzero on failure. Encoders propagate failures
as `LC_EIO`. Callbacks can write to a file, a terminal, a serial link, or a
caller-managed buffer. For small callback writes, buffer the transport.

Allocation failures are recorded in the context and scene. Check rendering
return values and `lc_scene_status` before consuming an image. Scratch reset
clears the context's sticky status; scene reset clears scene-owned drawing
state. Failed output is never an implicit successful frame.

The hand-written rasterizer, bitmap font, fixed-Huffman/LZ77 PNG compressor,
sixel run encoding, and Kitty chunk writer need no external graphics or
compression package. The app owns terminal input, signal restoration,
filesystem access, Linux framebuffer/VT handling, and DOS VGA access.

## Portability boundaries

The source language is ISO C89. Eight-bit bytes are required for the image
formats. The code does not require `stdint.h`, `stdbool.h`, C99 math, C++,
exceptions, templates, or a C++ runtime. Allocation sizes are checked against
`size_t` limits. Raster coordinates are expected to stay within the range a
slide layout produces; extreme rectangles near `INT_MAX` are not guarded
against signed overflow. Strict C89
syntax alone does not prove a particular operating system or compiler works;
see [the port record](C89-PORT.md) and [legacy build instructions](LEGACY-BUILD.md)
for the targets actually exercised.

The first DOS target uses a 32-bit protected-mode executable and a DOS
extender. A segmented 16-bit port and performance on a real 386SX remain
separate validation work. Networking is outside the rendering library: a
transferred JSON deck and its data files are ordinary local inputs.
The DOS console keyboard adapter currently accepts ASCII input; it does not
translate OEM codepages into UTF-8. UTF-8 text in transferred decks still uses
the library's bitmap font. The POSIX terminal editor accepts UTF-8 input.

The application's automatic reload check uses file size and modification
time. Nanoseconds are enabled for verified modern glibc and FreeBSD layouts;
other systems use seconds, so a same-size rewrite within one clock tick can
be missed. A platform build can define `CHARTS_STAT_NSEC(st)` to its verified
`struct stat` nanosecond expression without changing library code.
