# Driving charts from an agent

The division of labour: **the agent builds the deck, the human presents it.**
An agent has no screen and cannot take over the human's console, so it writes
files, checks them, looks at PNGs of them, and hands over one command.

## The loop

```sh
# 1. data: one tidy CSV per chart, a label column and numeric columns
# 2. deck: write deck.json            (charts --schema, charts --example deck)
charts deck.json --check              # 3. every problem, by JSON path; exit 1 on errors
charts deck.json --png-dir /tmp/deck  # 4. render, then LOOK at the PNGs
charts deck.json --launcher q3        # 5. writes ~/.local/bin/q3, a small sh script
```

Hand the human the command (`q3`) and the path it printed. The launcher finds
charts, opens a terminal if it was started without one, and lets charts pick
framebuffer, kitty, sixel or cells from wherever it lands.

Step 4 is not optional. `--check` knows the deck is valid; only the picture
shows a legend crowding a pie, a callout over the wrong bar, twelve labels
where six fit. Each PNG is exactly the 120 × 33 cell screen of a 1080p
console, a few kilobytes, and what the human will see pixel for pixel.

If the human already has the deck open, skip step 5: the presenter watches the
deck and its data files and redraws when they change.

## Machine-readable checking

```sh
charts deck.json --check --json
```

```json
{"ok": false, "slides": 3, "errors": 1, "warnings": 1, "issues": [
  {"level": "error", "path": "slides[1].type", "message": "unknown chart type \"barr\" (see --list-types) -- did you mean \"bar\"?"},
  {"level": "warning", "path": "slides[1].annotations[0]", "message": "no category \"Arp\" in the data, so this is not drawn (did you mean \"Apr\"?)"}
]}
```

Exit status: `0` fine (warnings allowed), `1` the input has errors, `2` the
command line is wrong. `--describe` prints the outline as parsed — slide
titles, block kinds, rows × series — which is the quick way to confirm a data
file was read the way you meant. On a data file it prints the columns.

## Without a deck

```sh
charts data.csv -t line --print -w 100 -H 30 --no-color   # text, for a log or a reply
charts data.csv -t pie3d --png chart.png                  # one chart as an image
some-tool --json | charts -t hbar --values
```

A data file can carry its own settings, so whoever produces it decides how it
is drawn and `charts file` needs no flags:

```
#chart: type=stacked, title="Weekly requests", ylabel=count, values
day,ok,errors
Mon,4821,37
```

```json
{"chart": {"type": "pie3d", "title": "Disk use", "explode": 2},
 "rows": [{"label": "sda", "used_gb": 1860}, {"label": "sdb", "used_gb": 96}]}
```

Precedence is always command line > deck > data file > defaults.

## What the human can do, so you need not

In the presenter the human can edit the numbers (`e`), add callouts and target
lines (`a`), add text and slides (`i`, `N`), change chart types (`t`) and save
(`s`). Their edits land in the same files: CSVs are rewritten in place,
annotations and text go into the deck JSON. Re-read the deck before editing it
again.

## Environment

| | |
| --- | --- |
| `CHARTS_GFX` | `auto` `fb` `kitty` `sixel` `cells` `ascii`, same as `--gfx` |
| `CHARTS_FB` | framebuffer device (default `/dev/fb0`, then `$FRAMEBUFFER`) |
| `CHARTS_FB_GEOM` | `WxHxBPP`: treat `CHARTS_FB` as a plain file of that shape (tests, screenshots) |
| `NO_COLOR` | honoured |
