# Slide recipes

Paste, change the data and the words. Every one was rendered and looked at on
the 120 × 33 grid. Paths are relative to the deck.

## Title slide

```json
{"title": "Growth review", "subtitle": "Week 35  -  signups, revenue by region, latency"}
```

## One chart, full slide (the default)

```json
{"title": "Revenue grew every month but June",
 "type": "bar", "values": true, "ylabel": "USD k", "data": "revenue.csv",
 "annotations": [{"at": "Jun", "series": "revenue", "text": "warehouse move"},
                 {"y": 150, "text": "target"}],
 "notes": "What to say out loud."}
```

`values: true` suits ≤ 12 categories × ≤ 2 series; beyond that the numbers
crowd and are dropped where they do not fit.

## KPI strip over a chart, commentary beside it

```json
{"title": "Signups are up 70% since week 27", "subtitle": "weekly new accounts by channel",
 "blocks": [
   {"cols": [
      {"stat": "1,153", "label": "signups, W35", "delta": "+8.5% w/w"},
      {"stat": "70%", "label": "growth since W27", "delta": "+102 pt paid"},
      {"stat": "12%", "label": "share from referral", "delta": "+5 pt"}
    ], "at": [0, 0, 12, 3]},
   {"type": "stacked", "data": "signups.csv", "ylabel": "accounts", "at": [0, 3, 8, 9]},
   {"text": ["# What happened", "", "- **Paid** tripled in W31 and held",
             "- **Referral** compounding: 38 to 140", "- Organic steady at +7% a week", "",
             "> Next: cap paid at 350/wk."], "at": [8, 3, 4, 9]}
 ]}
```

`stat` values of ≤ 6 characters render large. `delta` starting with `+` is
green, with `-` red. The strip wants 3 of the 12 rows.

## Chart and commentary, side by side

```json
{"title": "p99 latency crosses the SLO at 700 requests/s", "layout": "cols",
 "blocks": [
   {"type": "scatter", "xy": true, "data": "latency.csv", "xlabel": "requests/s", "ylabel": "ms",
    "annotations": [{"y": 200, "text": "SLO 200 ms", "color": "red"}], "weight": 2},
   {"text": ["# Reading this", "", "- p50 barely moves", "- p99 climbs **3x faster**", "",
             "> Capacity plan assumes 900 rps."], "weight": 1}
 ]}
```

## Two charts compared

```json
{"title": "Three regions make 84% of revenue", "layout": "cols",
 "blocks": [
   {"type": "hbar", "data": "regions.csv", "values": true, "xlabel": "USD k", "weight": 3},
   {"type": "donut", "data": "regions.csv", "weight": 2}
 ]}
```

`hbar` for the ranking with long names, the donut for share. Sort the CSV
descending first.

## 2 × 2 grid

```json
{"title": "Four services, last 24 hours", "layout": "grid",
 "blocks": [
   {"type": "line", "data": "api.csv", "title": "api"},
   {"type": "line", "data": "db.csv", "title": "db"},
   {"type": "line", "data": "cache.csv", "title": "cache"},
   {"type": "line", "data": "queue.csv", "title": "queue"}
 ]}
```

Give each chart a short `title`; it goes in the frame. Small panels: ≤ 9
categories, skip `values`.

## A build: the same chart twice, written once

```json
{"title": "Returns ran at a dozen a month...", "type": "line", "data": "cabinets.csv",
 "series_col": 3, "min": 0, "max": 40, "colors": ["grey"]},
{"title": "...except June, when a bad batch came back", "like": 3,
 "annotations": [{"at": "Jun", "text": "38 returns: batch J-114", "color": "red"},
                 {"y": 15, "text": "normal month", "color": "green"}]}
```

`like` counts slides as the footer does (this pair is slides 3 and 4). The
second slide inherits every key of the first's chart and adds only the story.
Pin `min`/`max` so nothing moves between the two.

## Error bars: the spread beside the mean

```json
{"title": "A local GPU last is 45% faster, well past the noise",
 "type": "bar", "data": "layouts.csv", "series_col": 2, "values": true,
 "errors": {"tg": ["tg min", "tg max"]}, "ylabel": "tokens/s"}
```

`errors` names columns of the same file: `["low", "high"]` for a range, one
column for ±. They become whiskers, not bars, and `series_col` keeps them.

## Before and after

```json
{"title": "The 2.5 GbE link lifted every RPC layout", "type": "dumbbell",
 "data": "links.csv", "values": true, "xlabel": "tokens/s",
 "colors": {"1 GbE": "grey", "2.5 GbE": "yellow"}}
```

A column per state (`layout,1 GbE,2.5 GbE`). The arrow points at the last
column; grey the "before" so the eye lands on where things ended up.

## How it works: a flow beside the finding

```json
{"title": "8 ms per token is one 1 MB logits vector on 1 GbE", "layout": "cols",
 "blocks": [
   {"flow": ["tokens", "CUDA0", {"id": "yoda", "text": "yoda\nrpc0 + rpc1"},
             {"id": "last", "text": "last device\n(lm_head)", "color": "red"}],
    "edges": [["tokens", "CUDA0"], ["CUDA0", "yoda"], ["yoda", "last"], ["last", "tokens", "1 MB/token"]],
    "dir": "down", "weight": 2},
   {"text": ["# The arithmetic", "", "- 248,320 x 4 B = **0.99 MB**", "- 1 GbE: ~117 MB/s",
             "- predicted **8.5 ms**, measured **8.1**"], "weight": 1}
 ]}
```

Name the steps and the arrows; charts lays them out. Three to five boxes, one
of them coloured (where the cost is), the arrow that matters labelled.

## Free drawing

```json
{"title": "The hits stand by the door",
 "shapes": [
   {"rect": [0.3, 0.3, 11.4, 11.2], "fill": false, "border": "grey", "width": 2},
   {"rect": [0.9, 7.8, 2.4, 2.8], "text": "Laser\nLlama", "color": "green", "depth": 2, "shadow": true},
   {"ellipse": [7.2, 6.4, 2.6, 3.2], "text": "change", "color": "white", "dither": 2},
   {"arrow": [[9.6, 11.4], [9.6, 10.1]], "width": 3},
   {"label": [8.5, 10.6], "text": "door", "align": "right"}
 ]}
```

A 12 × 12 grid over the block. Draw only what makes the point.

## Big-number slide

```json
{"title": "Where we ended the quarter", "layout": "cols",
 "blocks": [
   {"stat": "$4.2M", "label": "ARR", "delta": "+18% q/q"},
   {"stat": "1,204", "label": "paying accounts", "delta": "+96"},
   {"stat": "2.1%", "label": "monthly churn", "delta": "-0.4 pt"}
 ]}
```

## Closing slide

```json
{"title": "What we do next",
 "text": ["- Cap paid spend at **350 signups/week**", "- Double the referral bonus for 30 days",
          "- Re-run this review in week 39"], "size": 2}
```

`size: 2` doubles the text on pixel displays; keep lines under 50 characters.

## Inline data, for small tables

```json
{"type": "pie3d", "explode": 0,
 "data": {"labels": ["payroll", "cloud", "rent", "other"],
          "series": [{"name": "USD k", "values": [620, 310, 140, 95]}]}}
```
