# Driving charts from a script or an agent

`charts` is built to be the last step of a pipeline: something produces data,
you ask for a chart, the chart appears on the console. No interpreter, no
daemon, no config, no state between runs.

## The short version

```sh
# 1. write the data
printf 'month,revenue\nJan,120\nFeb,145\nMar,132\n' > /tmp/r.csv

# 2. draw it
charts /tmp/r.csv -t bar -T 'revenue by month'

# 3. look at it
```

That is the whole loop. `/tmp` is fine for scratch data; a chart you care about
belongs where the rest of your notes live.

## Pin the size

On a TTY the chart fills the screen. In a script or a log, pass `-w` and `-H`
so the output is the same every time:

```sh
charts data.csv -t bar -w 100 -H 30
```

Without a TTY and without `-w/-H` it falls back to 100x32 — a safe size for a
pager or a paste. Lines are never longer than `-w` cells, so a chart survives
being piped into anything.

## Check before you draw

`--describe` parses and prints what it found without rendering. Use it to check
your assumption about the data instead of guessing from a picture:

```sh
charts --describe data.csv
```

If a chart looks wrong, this tells you in one run whether the loader or the
renderer is at fault.

## Iterate without re-running

`--watch` redraws in place whenever a source file changes. Leave it running,
rewrite the CSV, and the picture updates:

```sh
charts /tmp/live.csv -t bar --watch
```

Press `q` or Ctrl-C to leave. Good for watching a build, a crawl, or a
benchmark that appends rows.

## Pick a type from the shape of the data

| data shape | type |
| --- | --- |
| categories, one number each | `bar` or `pie3d` |
| categories, several numbers each | `stacked` or `bar` |
| long category names | `hbar` |
| a sequence over time | `line` or `area` |
| two numbers per row, correlated | `scatter --xy` |
| distribution of one column | `hist` |
| a handful of whole/part figures | `pie3d` or `donut` |
| just show me the numbers | `table` |

Type names are checked against a fixed list; `charts --list-types` prints it,
and an unknown name exits 1 with the name in the message.

## Exit codes

| code | meaning |
| --- | --- |
| 0 | drew the chart |
| 1 | bad data, bad option, unreadable file — message on stderr |
| 2 | no input, or `-i`/`--watch` without a terminal |

Every failure prints one line to stderr beginning `charts: `. Nothing partial
is ever written to stdout on failure, so `charts ... > out.txt` either gets a
whole chart or an empty file.

## Colour

Colour is dropped automatically when stdout is not a terminal, and `--no-color`
forces it off. That means:

```sh
charts data.csv -t bar > chart.txt        # plain text, no escapes
charts data.csv -t bar --color | less -R  # keep the colour, page it
```

`$NO_COLOR` is honoured. `--ascii` replaces the box-drawing and block glyphs
with ASCII, for serial consoles, `tee`d logs and terminal fonts without
Unicode.

## The interactive screen

```sh
charts -i ./data          # every .csv/.tsv/.json in a directory
charts -i a.csv b.csv     # a fixed list
```

Keys are listed in `README.md` and in `-h`. Two are worth knowing for agent
work: `r` reloads the current file now, and `w` toggles auto-reload, which is
the same thing as `--watch` but already running.

## Notes for whoever comes next

- Output is deterministic for a given file, type, palette and size. Two runs
  with the same arguments produce identical bytes.
- A chart is drawn once, top to bottom, and stays put. `--watch` redraws when a
  source file changes; `-i` redraws when a key is pressed or a file changes.
- Files are opened read-only. There is no cache or config file to find
  afterwards, so cleaning up means deleting the data you made and the binary.
- The binary is self-contained: copy it to another box of the same architecture
  and it runs.
