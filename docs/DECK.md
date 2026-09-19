# The deck format

A deck is one JSON file with a `slides` array. Paths in it are relative to the
deck file. `charts --schema` prints a one-screen version of this page.

Contents: [Deck](#deck) · [Slide](#slide) · [Blocks](#blocks) ·
[Layout](#layout) · [Chart fields](#chart-fields) · [Data](#data) ·
[Annotations](#annotations) · [Checking](#checking) · [What fits](#what-fits)

## Deck

```json
{
  "title": "Q3 review",
  "footer": "ACME  -  Q3 review",
  "theme": "dos",
  "palette": "dos",
  "slides": [ ... ]
}
```

| key | |
| --- | --- |
| `slides` | required, at least one |
| `title` | shown in the footer bar and the overview |
| `footer` | replaces `title` in the footer bar |
| `theme` | `dos` (default), `black`, `light`, or an object: `{"base": "black", "frame": "brown", "title": "yellow", ...}` recolours any part of the chrome. Keys: `slide_bg` (`"none"` allowed) `panel_bg` `frame` `title` `subtitle` `heading` `text` `dim` `accent` `bullet` `axis` `tick` `grid` `label` `xlabel` `ylabel` `legend` `value` `bar_fg` `bar_bg` `bar_key` `note_fg` `note_bg` `table_head` `table_rule` `table_row` `cursor_fg` `cursor_bg` |
| `palette` | default series colours: `dos` `ega` `cga` `ice` `fire` `green` `amber` `mono`, or a list: `["yellow", "brown", "white"]` |
| `display` | `{"gfx": "auto", "scale": 2, "size": [120, 33], "ascii": false, "color": true}`: how the deck wants to be shown, so the launcher needs no flags |

`charts -h` is the complete, current reference (it is what the agent skill is
generated from); this page is the same material with more room.

Keys starting with `_`, and `comment`, are ignored everywhere: JSON has no
comments, so put them there. Any other unknown key is a warning from `--check`,
never an error.

## Slide

```json
{"title": "Signups are up 70%", "subtitle": "weekly, by channel",
 "notes": "speaker notes, shown with n",
 "layout": "auto",
 "blocks": [ ... ]}
```

- **No blocks** makes a title slide: the title large and centred, the subtitle
  under it.
- **One block** can be written straight on the slide. This is the common case:

  ```json
  {"title": "Revenue by month", "type": "line", "data": "revenue.csv"}
  ```

  `title` then belongs to the slide, not the chart frame.

Write the title as the takeaway ("Signups are up 70% since week 27"), not the
topic ("Signups"). It is the one line everybody reads.

## Blocks

What a block is follows from the key it has.

### chart — `data`

```json
{"data": "signups.csv", "type": "stacked", "ylabel": "accounts", "values": true,
 "annotations": [{"at": "W31", "series": "paid", "text": "campaign starts"}]}
```

`data` is a file path or [inline data](#data). Everything else is a
[chart field](#chart-fields). With no `type` the file's own `#chart` line or
`"chart"` block decides, else `bar`.

### text — `text` or `bullets`

```json
{"text": ["# What happened", "", "- **Paid** tripled in W31", "- Organic steady", "",
          "> Next: cap paid at 350 a week."]}
{"bullets": ["Payroll is **53%** of spend", "Cloud doubled since Q1"]}
```

`text` is a string with `\n` or an array of lines. Line markup: `# heading`,
`## subheading`, `- bullet`, `> aside` (dimmed), `**emphasis**`. Long lines wrap.

| key | |
| --- | --- |
| `size` | 1–4, text height in cells on pixel displays (default 1; headings are 2) |
| `align` | `left` `center` `right` |
| `valign` | `top` `middle` |
| `color` | a colour name or 0–15 |
| `box` | `true` draws it in a framed window like a chart; `title` goes in the frame |

### stat — `stat`

```json
{"stat": "1,153", "label": "signups, W35", "delta": "+8.5% w/w"}
```

One big number. `delta` is green with an up arrow when it starts with `+`, red
with a down arrow when it starts with `-`. Three or four in a row make a KPI
strip.

### group — `rows` or `cols`

```json
{"cols": [ {"stat": "..."}, {"stat": "..."}, {"stat": "..."} ]}
{"rows": [ {"stat": "..."}, {"bullets": ["..."]} ]}
```

Blocks side by side, or stacked. Groups nest.

## Layout

`layout` on a slide: `auto` (default: up to three blocks side by side, more in
a grid), `cols`, `rows`, `grid`.

`"weight": 2` on a block gives it twice the share of its row or column.

`"at": [x, y, w, h]` places a block on a 12 × 12 grid over the slide body,
ignoring `layout`. The usual dashboard slide:

```json
"blocks": [
  {"cols": [ {"stat": "..."}, {"stat": "..."}, {"stat": "..."} ], "at": [0, 0, 12, 3]},
  {"data": "signups.csv", "type": "stacked",                      "at": [0, 3, 8, 9]},
  {"text": ["# What happened", "- ..."],                          "at": [8, 3, 4, 9]}
]
```

## Chart fields

| field | |
| --- | --- |
| `type` | `bar` `stacked` `hbar` `line` `area` `pie` `pie3d` `donut` `scatter` `hist` `table` |
| `title` `subtitle` `xlabel` `ylabel` | text |
| `values` | numbers on the bars / points (default false) |
| `legend` `grid` `shadow` | default true |
| `palette` | per chart: a name or a list of colours |
| `colors` | pin colours: `{"revenue": "green"}` by series, `{"Dec": "white"}` one bar of a single-series chart, pie slices by label, or `["grey", "yellow"]` in order |
| `frame` | `double` (default) `single` `heavy` `none` |
| `depth` | 0–6, 3-D extrusion; 0 is flat (default 2) |
| `explode` | pie: 0-based row to pop out, or `true` for the biggest |
| `min` `max` | pin the value axis |
| `prec` | decimals in value labels |
| `bins` | histogram buckets (default 10) |
| `xy` | first numeric column is the X axis (scatter, or a line over numeric X) |
| `transpose` `no_header` `labels_col` `series_col` `label_key` `delim` | how to read the file; see [INPUT.md](INPUT.md) |

Choosing a type:

| the point is | use |
| --- | --- |
| compare categories | `bar`; `hbar` when names are long or it is a ranking |
| parts of a total, per category | `stacked` |
| change over time | `line`; `area` for one or two series where volume matters |
| share of one total, ≤ 6 parts | `pie3d` / `donut`; more parts than that, use `hbar` |
| relation between two measures | `scatter` with `xy` |
| spread of one measure | `hist` |
| the exact numbers | `table` |

A pie of one series has a slice per row; of several series, a slice per series
(each summed).

## Data

A file: CSV, TSV or JSON, in any shape [INPUT.md](INPUT.md) lists. Or inline,
in any of the JSON shapes:

```json
"data": {"labels": ["Q1", "Q2"], "series": [{"name": "north", "values": [120, 145]},
                                             {"name": "south", "values": [88, 91]}]}
"data": [{"label": "Q1", "north": 120, "south": 88}, {"label": "Q2", "north": 145, "south": 91}]
"data": {"payroll": 620, "cloud": 310, "rent": 140}
"data": {"x": [1, 2, 3], "y": [4, 5, 6]}
"data": [["", "north", "south"], ["Q1", 120, 88], ["Q2", 145, 91]]
```

`null` is a gap. Prefer a file when a human will edit the numbers: the sheet
saves a CSV back to its file, and inline data back into the deck.

## Annotations

```json
"annotations": [
  {"at": "Apr", "series": "revenue", "text": "spring launch"},
  {"y": 150, "text": "target"},
  {"x": "Apr", "text": "price change"},
  {"note": [0.98, 0.02], "text": "n = 412"}
]
```

| form | draws |
| --- | --- |
| `at` (+ `series`) | a callout with a leader line to one data point; on a pie, to the slice |
| `y` | a dashed line across the plot at that value, labelled |
| `x` | a dashed line up the plot at that category, labelled |
| `note` | free text; `[x, y]` from 0 to 1 across the plot, `[0,0]` top left |

`at` and `x` take the category label exactly as it is in the data (case does
not matter), or a 0-based row number. `series` is the series name; leave it
out on single-series charts. `"color": "red"` recolours one annotation. Text
wraps at 24 characters. Callouts place themselves clear of the value labels and
of each other; two or three per chart is the useful limit.

`--check` warns when `at` or `series` names something the data does not have,
with the nearest real name.

## Checking

```sh
charts deck.json --check          # human-readable, exit 1 if there are errors
charts deck.json --check --json   # {"ok":…, "slides":…, "errors":…, "warnings":…, "issues":[{"level","path","message"}]}
charts deck.json --describe       # the outline as charts understood it
charts deck.json --png s.png --slide 2   # look at it
charts deck.json --png-dir out/   # every slide: out/slide-01.png …
charts deck.json --print --slide 2 -w 120 -H 33   # as text
```

`--check` also draws every slide at console size and warns about what did not
fit: a title cut short, category labels thinned or truncated, a `ylabel` longer
than its axis, a stat label or text block that overflows.

Every issue carries a JSON path such as `slides[2].blocks[0].annotations[1]`.
Errors stop a chart from drawing (unknown type, missing file, no numeric
data); warnings do not (unknown key, annotation with no matching category).
A deck with errors still opens: the broken block shows its error in place.

`--png` renders 120 × 33 cells, 960 × 528 pixels: what a 1080p console shows.
`--size COLSxROWS` and `--scale K` change that.

## What fits

On a 1080p console the slide is 120 × 33 cells; a chart's plot is roughly its
block minus 10 columns and 6 rows.

- Category labels share the plot width: 12 labels of ≤ 6 characters fit a full-width chart. Longer names → `hbar`. When labels do not fit, every second or third is shown.
- Up to about 6 series stay readable; 40 categories in a line chart; 60 histogram bins.
- A text block a third of the slide wide holds about 35 characters a line and 20 lines.
- A `stat` value of ≤ 6 characters is drawn 3–4 cells tall in a third-width block.
- Slide titles: 55 characters at full size.
- `ylabel` runs down the axis a letter a row: about 18 characters on a full-height chart.
- A `# heading` in a text block is double size when it fits on one line that way, normal size otherwise.
