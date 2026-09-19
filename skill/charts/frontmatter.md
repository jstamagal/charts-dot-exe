---
name: charts
description: Build chart presentations for the Linux console with the `charts` CLI - DOS-style 16-colour bar, line, pie, scatter, before/after charts, flow diagrams and slide decks the user flips through with the arrow keys like PowerPoint, each slide with its chart and a text box explaining it. Use this whenever the user wants to see data as a chart, graph, plot, dashboard, slides or a presentation in a terminal or on a TTY; asks to "show", "visualize", "chart", "graph", "plot" or "present" numbers, a CSV, query results, benchmark results, sweeps, logs or metrics; wants "the interesting parts" of a pile of data pulled out and shown; or asks to restyle, annotate or fix an existing charts deck (a JSON file with "slides"). Prefer this over matplotlib/gnuplot/HTML whenever the output is for this user's screen. The user never runs charts: you build the deck and hand them a launcher.
---

# charts

Everything below is the output of `charts -h`: the binary is the manual, and
this file is generated from it (`make skill` in the charts source tree), so
they cannot drift. If `charts` is newer than this file, trust `charts -h`.

Three things the help text cannot do for you:

- **Look at your slides.** After `charts deck.json --png-dir DIR`, open every
  PNG with your image-reading tool. Do not hand over a deck you have not seen.
- **Find the story first.** Asked to chart a pile of results, do not plot
  everything. Load the data, compute, and look for what is surprising: the
  winner and by how much, the config that should have won and did not, the
  knee in a curve, the outlier, the setting that turned out not to matter.
  Each finding is one slide whose title states it, a chart that shows it, and
  a text box that explains how to read it. Ten good slides beat forty charts.
- **See what good looks like.** `assets/gallery/deck.json` beside this file is
  a 17-slide showcase (builds with `like`, error bars, a broken axis,
  before/after, a flow diagram, free shapes); each slide's `notes` names the
  technique it demonstrates, and `assets/gallery/png/` has every slide
  rendered. Read a few before designing your own. `references/recipes.md` has
  paste-ready layouts.

If `charts` is not on PATH: `make -C ~/charts-dot-exe install PREFIX=~/.local`.

