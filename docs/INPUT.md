# Input

`charts` reads CSV, TSV and JSON. The rule it follows everywhere is: **text
columns become labels, numeric columns become series.** Get that right and any
file sensible enough to hold chart data will just work.

Check what it made of a file without drawing anything:

```sh
charts --describe data.csv
```

```
source:  examples/quarterly.csv
rows:    5
labels:  5
series:  4
  [0] north            n=5    min=120          max=201          sum=776
  [1] south            n=5    min=88           max=118          sum=511
  [2] west             n=5    min=64           max=91           sum=395
  [3] east             n=5    min=88           max=133          sum=538
first labels: Q1 2025, Q2 2025, Q3 2025, Q4 2025, Q1 2026
```

## CSV / TSV

```csv
month,revenue,costs
Jan,120,80
Feb,145,88
```

- **Delimiter** is sniffed from the first non-blank line: `,` `;` TAB `|`.
  Override with `--delim C` (`--delim tab` also works).
- **Header**: if the first row contains any text, it is a header. If every cell
  in the first row is a number, the file has no header and columns are named
  `col1`, `col2`, ...  Force it either way with `--no-header`, or by hand.
- **Labels**: if the first column is not numeric it becomes the category labels.
  Otherwise labels are the row numbers.
- **Series**: every other numeric column becomes one series.
- **Blank lines** are skipped. A line starting with `#` is a comment.
- **Quoting**: `"a,b"` keeps its comma, `""` inside a quoted field is one quote.
- Numbers may carry thousands separators and a trailing `%`: `1,234.5`, `12%`,
  `$40` all parse. A field that cannot parse becomes a gap in that series, not
  an error.

```csv
# columns can be reordered and skipped freely
day,requests,errors,p99_ms
Mon,48210,37,118
Tue,51190,44,131
```

Useful flags when the defaults guess wrong:

| flag | effect |
| --- | --- |
| `--delim C` | force the delimiter |
| `--no-header` | treat row 1 as data |
| `--labels-col N` | column N (1-based) holds the labels |
| `--series-col N` | keep only column N as a series |
| `--transpose` | swap rows and columns before anything else |
| `--xy` | the first numeric column is the X axis, not a series |

Tabs, pipes and semicolons are ordinary business:

```sh
charts export.tsv -t area
charts semis.csv --delim ';' -t bar
```

## JSON

Six shapes are understood. All of them are things a script naturally emits.

**Array of records** — the field named `label`, `name`, `key`, `category`,
`date`, `time`, `month` or `year` becomes the labels, if it holds text. Every
other numeric field becomes a series.

```json
[
  {"label": "nvme0n1", "used_gb": 412, "free_gb": 500},
  {"label": "nvme1n1", "used_gb": 288, "free_gb": 624}
]
```

A numeric field called `x` stays a series rather than being eaten as a label, so
`[{"x":1,"y":10}, ...]` still works as a scatter with `-t scatter --xy`.

**Named series**

```json
{
  "title": "weekly",
  "labels": ["Mon", "Tue", "Wed"],
  "series": [
    {"name": "requests", "values": [48210, 51190, 49830]},
    {"name": "errors",   "values": [37, 44, 29]}
  ]
}
```

`values` may also be spelled `data` or `v`. `title` is picked up as the chart
title.

**Object of numbers** — one series, the keys become the labels.

```json
{"Mon": 10, "Tue": 20, "Wed": 5}
```

**Flat array of numbers**

```json
[1, 4, 9, 16, 25]
```

**Rows with a header row** — array of arrays, first row all text.

```json
[[null, "cpu", "mem"], ["Jan", 12, 40], ["Feb", 15, 44]]
```

**x/y form**

```json
{"x": [1, 2, 3], "y": [4, 5, 6]}
```

Any pair of numeric arrays next to `x` becomes series.

### Notes

- `//` and `/* */` comments are allowed; so is a trailing comma-free strict
  style — the reader is strict about structure but tolerant about whitespace.
- `null` and missing fields become gaps in a series.
- Bad JSON fails with a line and column: `json: expected '}' at line 4 col 1`.
- `--label-key K` forces which record field supplies the labels.

## Stdin

```sh
cat data.csv | charts -t bar
some-tool --json | charts -t pie3d
```

`-` means stdin. It can only be read once, so combining `-` with other files
reuses the first dataset for later tiles.

## What a series can be

Each of these is drawn sensibly:

- **one series, many rows** — a bar/pie per row, each row a category
- **several series, many rows** — grouped bars, multi-line, multi-slice pies
- **negative values** — bars hang below the zero line, lines and areas follow
- **missing values** — the gap is skipped, the line jumps the hole
- **a single row** — still draws; labels fall back to column names

## Limits

`charts` is a renderer, not a spreadsheet. It does not sort, filter, aggregate
or pivot. Shape the data upstream with whatever you already use; give `charts`
something tidy and it will make it look like 1990.
