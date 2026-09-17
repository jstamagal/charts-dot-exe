# charts-dot-exe

ANSI charts for the Linux console. One binary, no dependencies, no runtime, no
X, no Wayland. Point it at a CSV or JSON file and it draws a chart that fits the
screen you are actually looking at — a real DRM/fbcon TTY, a framebuffer
console, or any terminal.

The look is deliberate: 16 colours, `█▓▒░` shading, extruded bars, ellipsoid
pies with a swept side wall, double-line DOS frames and drop shadows. 1990 shop
floor, not 2026 dashboard.

```
$ charts revenue.csv -t pie3d -T 'Revenue mix'

╔═══════════════════════════════ Revenue mix ════════════════════════════════╗
║                                                 ██ revenue    2,388  62.9% ║
║                                                 ▓▓ costs      1,408  37.1% ║
║                        █                                                   ║
║                 ▓▓▓▓▓▓▓████████                                            ║
║               ▓▓▓▓▓▓▓▓▓██████████                                          ║
║             ▓▓▓▓▓▓▓▓▓▓▓████████████                                        ║
║            ▓▓▓▓▓▓▓▓▓▓▓▓█████████████                                       ║
║           ▓▓▓▓▓▓▓▓▓▓▓▓▓██████████████                                      ║
║           ▓▓▓▓▓▓▓▓▓▓▓▓▓██████████████                                      ║
║          ▓▓▓▓▓▓▓▓▓▓▓▓▓▓███████████████                                     ║
║           ▓▓▓▓▓▓▓▓▓▓▓████████████████                                      ║
║          ▓▓▓▓▓▓▓▓▓▓██████████████████▓▓                                    ║
║           ▓▓▓▓▓▓▓███████████████████▓▓▓                                    ║
║           ▓▓▓▓▓████████████████████▓▓▓▓                                    ║
║            ▓▓▓███████████████████▓▓▓▓▓                                     ║
║             ▓▓▓▓███████████████▓▓▓▓▓▓▓                                     ║
║               ▓▓▓▓▓▓▓▓▓█▓▓▓▓▓▓▓▓▓▓▓▓                                       ║
║                 ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓                                        ║
║                        ▓▓▓▓▓▓▓▓▓                                           ║
║                         ▓                                                  ║
╚═══════════════════════════ examples/revenue.csv ═══════════════════════════╝
```

## Build

```sh
make              # ./charts
make test         # CLI + interactive pty tests
make install      # PREFIX=/usr/local by default
make static       # fully static binary, if libstdc++.a is around
```

Needs a C++17 compiler and make. Nothing else.

## Use

```sh
charts data.csv                        # bar chart of every numeric column
charts data.csv -t pie3d               # extruded pie
charts data.json -t hbar --values      # horizontal bars with the numbers on them
charts a.csv b.json c.tsv --tile -t bar,line,pie3d
charts -i ./data                       # browse a directory, live
charts -i data.csv                     # flip chart types with the keys
cat data.csv | charts -t stacked       # from a pipe
charts data.csv --watch                # redraw whenever the file changes
```

`charts -h` prints the whole option list. `charts --example json` prints a
sample input file. `charts --describe data.csv` prints what it parsed, without
drawing anything — useful when a chart looks wrong and you want to know whether
the problem is the data or the renderer.

## Chart types

| type | what it is |
| --- | --- |
| `bar` | vertical bars, side by side per series, lit top and shaded right face |
| `stacked` | bars stacked into a total per category, total printed on top |
| `hbar` | horizontal bars, values at the bar end, names down the left |
| `line` | connected markers: `* o + x # @ ^ ~` |
| `area` | line with the space underneath shaded in |
| `pie` | flat pie, percentages inside the fat slices |
| `pie3d` | ellipsoid pie with an extruded side wall and a drop shadow |
| `donut` | 3-D ring with a hole |
| `scatter` | XY points (`--xy` takes the first numeric column as X) |
| `hist` | histogram of the first series, `--bins N` buckets |
| `table` | the raw numbers as a grid |

## Palettes

`dos` (default), `ega`, `cga`, `ice`, `fire`, `green`, `amber`, `mono`.
`mono` and `--no-color` make the fill glyph carry the series instead of the
colour, so charts still read without colour — same trick the shaded prints used
before everyone had a colour terminal.

## Interactive keys

```
c C ← →    cycle chart type            e   pop a slice out of the pie
1-9        jump to a type              v   value labels
p P        cycle palette               g   grid lines
t          table view                  d D deeper / flatter 3-D
n N        next / previous file        r   reload now
w          auto-reload on/off          ?   key list      q  quit
```

The screen redraws on `SIGWINCH`-ish resize polling, so dragging a window
smaller re-fits the chart instead of wrapping it into porridge.

## Console notes

Nothing here assumes 256 colours or truecolor. Colours go out as the 16
classic SGR codes, with the bright eight reached by `bold` — that is what a
`TERM=linux` VT actually renders. `--ascii` swaps every box-drawing and block
glyph for ASCII, for serial consoles, log capture and fonts that have no
Unicode. Colour is dropped automatically when stdout is not a terminal, so
redirecting to a file gives clean text.

`$NO_COLOR` is honoured.

## Documentation

- [`docs/INPUT.md`](docs/INPUT.md) — every accepted CSV and JSON shape
- [`docs/AGENT.md`](docs/AGENT.md) — driving charts from a script or an agent
- [`docs/charts.1`](docs/charts.1) — man page

## Layout

```
src/canvas.*    cell grid, box drawing, 16-colour ANSI dump
src/charts.cpp  every chart renderer
src/data.*      dataset model, CSV reader, axis maths
src/json.*      small JSON reader
src/cli.*       option parsing and help text
src/tui.cpp     the interactive screen
src/term.*      tty size, raw mode, key decoding
src/main.cpp    one-shot, tiled and watch modes
tests/          smoke.sh (CLI) and tui.py (pty)
```

## Licence

MIT. See `LICENSE`.
