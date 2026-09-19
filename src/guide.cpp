// guide.cpp -- `charts -h`: the whole manual, written for the agent that will
// build the deck.  The human never reads this; they run the launcher.
// skill/charts/SKILL.md is generated from this text (make skill), so there is
// one source of truth.
#include "cli.hpp"

namespace ch {

static const char *GUIDE = R"GUIDE(charts -- DOS-style chart decks for the Linux console.  A tool for agents.

You (the agent) build a deck: one JSON file of slides holding charts, text and
big numbers, plus the CSV files it draws from.  The human never runs charts or
reads this.  They run a LAUNCHER you generate, page through with the arrow
keys, fix numbers in a built-in spreadsheet, pin notes on bars.  Everything
about how the deck looks is in the deck file; there are no flags to remember.

The look is fixed and deliberate: 16 VGA colours, hard pixels, dithered
shading, a bitmap font, double-line frames, drop shadows.  1991 business
graphics.  On the bare console it is drawn in real pixels through the
framebuffer; in kitty/ghostty/wezterm/foot through their graphics protocols;
anywhere else in half-block characters.  charts works out which at run time.

==============================================================================
THE LOOP
==============================================================================
  1. DATA      one tidy CSV per chart beside the deck: header row, one label
               column, one numeric column per series.  Aggregate, sort and
               round upstream (SQL, awk, python); charts draws what it is given.
  2. DECK      write deck.json (format below).
  3. CHECK     charts deck.json --check
               Every problem with its JSON path and a did-you-mean.  It also
               DRAWS every slide at console size and warns about whatever did
               not fit (title cut, labels thinned, ylabel too long, annotation
               naming a category that is not in the data).  Exit 1 = errors.
               Get it to zero warnings.
  4. LOOK      charts deck.json --png-dir DIR     then open every PNG.
               Valid is not the same as good.  Only the picture shows a callout
               on the wrong bar, a pie of 14 slivers, a slide that says nothing.
               Each PNG is pixel-for-pixel the human's screen, a few KB.
  5. LAUNCHER  charts deck.json --launcher NAME
               Writes ~/.local/bin/NAME (or the path, if NAME has a slash) and
               prints it.  Tell the human to run NAME.  That is all they do.
               The launcher finds charts, opens a terminal if started without
               one (menu, key binding), and charts picks framebuffer / kitty /
               sixel / cells from where it finds itself.
  If the human already has the deck open, skip 5: the presenter watches the
  deck and its data files and redraws when you rewrite them.

  charts deck.json --describe      the outline as parsed (slides, blocks, rows x series)
  charts data.csv --describe       how a data file's columns were read
  charts deck.json --check --json  {"ok","slides","errors","warnings","issues":[{"level","path","message"}]}
  charts deck.json --print --slide 2     a slide as text (for a log or a reply)
  charts data.csv -t line --png c.png    one chart, no deck
  charts --example deck            a working deck to start from
  Exit status: 0 fine (warnings allowed), 1 the input has errors, 2 bad usage.

==============================================================================
DECK
==============================================================================
{
  "title":   "shown in the footer bar and the slide overview",
  "footer":  "replaces title in the footer bar",
  "theme":   "dos" | "black" | "light" | { custom, see THEMES },
  "palette": "dos" | ... | ["yellow", "cyan", ...]      see PALETTES
  "display": {"gfx": "auto", "scale": 2, "size": [120, 33], "ascii": false, "color": true},
  "slides":  [ SLIDE, ... ]                              required
}
Paths are relative to the deck file.  JSON has no comments: keys starting with
"_" are ignored, put notes to yourself there.  Unknown keys are warnings.

SLIDE
  {"title": "...", "subtitle": "...", "notes": "speaker notes (key n)",
   "layout": "auto" | "cols" | "rows" | "grid",  "blocks": [ BLOCK, ... ]}
  no blocks    a title slide: title triple size and centred, subtitle under a rule
  one block    write its keys straight on the slide (the common case):
               {"title": "Revenue by month", "type": "line", "data": "rev.csv"}

BLOCK   the key it has decides what it is
  chart   {"data": "file.csv" | INLINE, "type": "bar", ...CHART FIELDS}
  text    {"text": "line\nline" | ["line", ...]}   or   {"bullets": ["...", ...]}
          line markup:  "# heading"  "## subheading"  "- bullet"  "> aside (dim)"  "**emphasis**"
          "size": 1-4 (cell height of the text)   "align": left|center|right
          "valign": top|middle    "color": COLOUR    "box": true (framed window; "title" in the frame)
          One string per bullet or paragraph; long lines wrap with a hanging indent.
  stat    {"stat": "1,153", "label": "signups, W35", "delta": "+8.5% w/w", "color": COLOUR}
          one big number.  delta starting "+" is green with an up arrow, "-" red and down.
  group   {"cols": [BLOCK, ...]}  or  {"rows": [BLOCK, ...]}     nests to any depth
  any     "weight": 2            share of its row/column (default 1)
          "at": [x, y, w, h]     explicit place on a 12 x 12 grid over the slide body

LAYOUT   auto = up to three blocks side by side, more in a grid.  cols / rows /
  grid force it.  "at" ignores layout.  The dashboard slide:
     {"cols": [stat, stat, stat], "at": [0, 0, 12, 3]},
     {"data": "x.csv", "type": "stacked", "at": [0, 3, 8, 9]},
     {"text": ["# What happened", "- ..."], "at": [8, 3, 4, 9]}

==============================================================================
CHART FIELDS   (all optional; all also valid in a data file's own chart block)
==============================================================================
  type       bar stacked hbar line area pie pie3d donut scatter hist table
  title subtitle xlabel ylabel
  values     numbers on the bars / points          (default false)
  legend grid shadow                               (default true)
  frame      double | single | heavy | none
  depth      0..6  3-D extrusion of bars, pies, donuts; 0 = flat   (default 2)
  explode    pie: 0-based row to pop out, or true = the biggest
  palette    name or list, for this chart only
  colors     pin colours by name, or in order:
               {"revenue": "green", "costs": "red"}     by series name
               {"Dec": "white"}                          one bar of a single-series bar/hbar
               {"ok": "green", "down": "red"}            pie slices by label
               ["grey", "yellow"]                        in series order
  min max    pin the value axis (max gets its own tick)
  prec       decimals in value labels              bins   histogram buckets (default 10)
  xy         first numeric column is the X axis (scatter; line over numeric X)
  transpose no_header labels_col series_col label_key delim     how to read the file
             series_col N keeps only column N as the series: one file, many charts
  annotations  [ ... ]  see ANNOTATIONS

  Which type:
    compare categories ................ bar      (hbar: long names, rankings)
    parts of a total per category ..... stacked
    change over time .................. line     (area: 1-2 series where volume matters)
    share of one total, <= 6 parts .... pie3d / donut    (more parts: hbar)
    two measures against each other ... scatter + xy
    spread of one measure ............. hist
    the exact numbers ................. table
  A pie of one series has a slice per row; of several series, a slice per series.

COLOUR  a name or 0..15:  black blue green cyan red magenta brown yellow white
  grey, each also "dark X" (dark grey, dark red ...).  Plain names are the
  bright ones; brown is dark yellow.  16 colours is all there is: shading is
  dither, never a 17th colour.

DATA   a CSV/TSV/JSON file, or inline in any of these shapes:
  {"labels": ["Q1","Q2"], "series": [{"name": "north", "values": [120, 145]}, ...]}
  [{"label": "Q1", "north": 120, "south": 88}, ...]
  {"payroll": 620, "cloud": 310}             {"x": [1,2,3], "y": [4,5,6]}
  [["", "north", "south"], ["Q1", 120, 88], ...]        null = a gap
  CSV: delimiter , ; TAB | is sniffed; '#' lines are comments; plain numbers
  only (no units, no thousands separators unless the field is quoted).
  A file can say how it is drawn:   #chart: type=line, title="Load", values
                                    {"chart": {"type": "pie3d"}, "rows": [...]}
  Precedence: command line > deck > data file > defaults.
  Use files when the human may edit the numbers: the sheet saves CSVs in place
  (comment and #chart lines kept) and inline data back into the deck.

ANNOTATIONS   say what the chart means, on the chart
  {"at": "Apr", "series": "revenue", "text": "spring launch"}   callout + leader to a point (pie: a slice)
  {"y": 150, "text": "target"}                                  dashed line across the plot
  {"x": "Apr", "text": "price change"}                          dashed line up the plot
  {"note": [0.02, 0.02], "text": "n = 412"}                     free text, [x,y] 0..1 from top left
  "at"/"x" = the category label as in the data (or a 0-based row); "series"
  optional.  "color": COLOUR recolours one.  Text wraps at 24 characters.
  Callouts place themselves clear of value labels and each other.

==============================================================================
THEMES, PALETTES, DISPLAY   -- everything about presentation is in the file
==============================================================================
THEMES   "dos"    blue desktop, black chart windows, grey status bar (default)
         "black"  no backdrop at all          "light"  grey desktop, white windows
  Custom: an object.  Start from a base and recolour any part:
  "theme": {"base": "black", "slide_bg": "black", "panel_bg": "black", "frame": "brown",
            "title": "yellow", "heading": "yellow", "text": "brown", "axis": "brown", ...}
    slide_bg   the desktop ("none" = leave the terminal's own background)
    panel_bg   inside chart / stat / boxed-text windows
    frame      window borders                 accent    focus ring, ## subheadings, **emphasis**
    title      chart titles in the frame      heading   slide titles, # headings
    subtitle   chart subtitles                text      body text on the desktop
    dim        slide subtitles, > asides      bullet    the bullet squares
    axis tick grid label xlabel ylabel legend value     the parts of a plot
    bar_fg bar_bg bar_key                     the footer bar, and the key hints in it
    note_fg note_bg                           annotation callouts
    table_head table_rule table_row           tables and the data sheet
    cursor_fg cursor_bg                       the sheet's cell cursor
  Series colours that match panel_bg are skipped; on a light panel bright
  colours are swapped for their dark partners so they stay readable.

PALETTES  series colours, in order
  dos    yellow cyan green red magenta blue white ...   (default)
  ega    red green yellow blue magenta cyan ...         cga    cyan magenta white yellow
  ice    cyans, blues, white        fire   yellow red brown       green  phosphor greens
  amber  amber terminal             mono   white/grey, series told apart by DITHER
  Custom: a list.  "palette": ["yellow", "brown", "white", "dark grey"]
  Deck-level sets the default; a chart's own "palette" or "colors" overrides.

DISPLAY   "display": {...} in the deck; the launcher needs no flags
  gfx     auto (default) | fb | kitty | sixel | cells | ascii
  scale   pixel multiplier on pixel displays.  auto = screen width / 960, so a
          1080p console is scale 2 = a 120 x 33 cell slide.  1 = twice as much
          room (240 x 67) and half-size text; 3 = a big-print 80 x 22.
  size    [cols, rows] for --png and --print (default 120 x 33)
  ascii   true = plain ASCII only          color   false = no colour (dither instead)
  Design for 120 x 33 unless you set scale; --check and --png use the same grid.

==============================================================================
WHAT FITS   (120 x 33 cells; text that does not fit is cut, never shrunk)
==============================================================================
  slide title ........ 55 characters
  category labels .... share the plot width.  Full-width chart: 12 labels of
                       <= 6 chars, or 6 of <= 14.  Two-thirds width: 4 of <= 10.
                       Longer -> hbar (labels get up to a third of the chart),
                       or shorten in the CSV ("qwen-27b", "Q1 25").
  ylabel ............. one letter per row down the axis: ~18 chars on a
                       full-height chart, ~12 under a KPI strip.  xlabel: the chart's width.
  series ............. up to ~6.   pie slices: up to ~6, then hbar.
  side column text ... a third of the slide = ~35 chars a line, ~20 lines.
                       A "# heading" there is double size up to 17 chars, normal beyond.
  stat ............... value <= 6 chars draws large; label <= 30 in a third-width tile
  values: true ....... good to ~12 categories x 2 series; numbers that do not fit are dropped
  line charts ........ ~40 points.   hist: up to 60 bins.

==============================================================================
CRAFT   the look is fixed; whether a slide says something is up to you
==============================================================================
  TITLE = THE TAKEAWAY.  "Signups are up 70% since week 27", not "Signups".
    The deck should tell its story through the titles alone.
  ONE IDEA PER SLIDE.  A chart and 2-4 bullets, or a KPI strip over a chart.
    More than that is another slide.
  AN ARC.  Title slide -> the headline number -> how we got there -> the
    exception -> what it cost -> the receipts (tables) -> what to do next.
  COLOUR IS MEANING, NOT DECORATION.  Grey out the context and light up the
    one thing:  "colors": {"West": "yellow", "North": "grey", "South": "dark grey"}.
    One bar in a ranking:  "colors": {"Dec": "white"}.  Status colours on a
    pie: green / yellow / red.  Red and green annotations for bad and baseline.
  ANNOTATE THE CLAIM.  If the title says June was the problem, put a callout
    on June.  at = the point, y = the target or threshold, x = the moment
    something changed, note = sample size and caveats.  One to three per chart.
  BUILDS.  Two slides, the same chart: first quiet (grey, no notes), then
    again with the annotations and the conclusion as its title.  Pin min/max
    on both so nothing jumps between them.
  SMALL MULTIPLES.  layout grid + series_col to split one file into a panel
    per series, the SAME min/max on every panel, one panel in another colour.
  PIN THE AXIS when slides or panels are compared (min/max), and when a chart
    should start at zero or stop at a meaningful ceiling (100%, a quota).
  EXPLODE the slice you are talking about.  DEPTH 3-4 for a hero chart alone
    on a slide, 1 or 0 for small panels and dense data.
  KPI STRIP.  Three stat tiles across the top ("at": [0,0,12,3]); deltas with
    + and - colour themselves.  A slide of only stats makes a strong opener.
  BOXED TEXT ("box": true) sits beside a chart as an equal; plain text reads
    as commentary.  "size": 2 text for a closing slide of 3-5 short lines.
  THEME AND PALETTE SET THE MOOD.  dos = the boardroom.  black + amber or
    green palette = the terminal.  light = paper.  A custom theme in two or
    three colours (one hue + white + grey) looks designed; all 16 at once
    looks like a test card.  mono when it may be printed.
  NOTES.  Put what the presenter should say in "notes"; the human presses n.
  SORT rankings descending, order time left to right, keep category order the
    same on every slide.

  The human, in the presenter:  arrows/space page, o overview, n notes, tab
  focus, e edit data (a spreadsheet) or text, a annotate, t/p/v/l/g/d/x change
  type/palette/values/legend/grid/depth/explode, i/c/N/E/X add text/chart/
  slide, edit title, delete block, y theme, s save, r reload, q quit.  Their
  edits land in the same files: re-read the deck and CSVs before changing them.

==============================================================================
WHEN SOMETHING IS OFF
==============================================================================
  "cannot open" ............ data paths are relative to the DECK, not your cwd
  "no numeric columns" ..... units or thousands separators in the CSV; write plain numbers
  wrong labels / series .... charts file.csv --describe; then labels_col / series_col / transpose
  numeric categories ....... a first column of years, sizes or counts is read as a SERIES.
                             Say "labels_col": 1 to make it the categories, or "xy": true for a numeric X axis.
  labels thinned or cut .... too many or too long: hbar, shorter names, fewer categories
  annotation not drawn ..... "at" must match a label in the data exactly (--check says which)
  a chart is an error box .. the block's error is printed in place; --check has the path

OPTIONS  (agents only; the launcher passes none)
  --check [--json]   --describe   --png FILE   --png-dir DIR   --print   --slide N
  --launcher NAME    --example [csv|json|deck]   -o FILE   -i (open data files in the presenter)
  -t TYPE  -T TITLE  -p PALETTE  --theme NAME  --values --legend --grid --shadow (--no-...)
  --frame S  --depth N  --explode [N]  --min V --max V  --prec N  --bins N
  -w N -H N --size CxR  --scale K  --gfx MODE  --ascii  --no-color  --tile
  --xy --transpose --no-header --delim C --labels-col N --series-col N --label-key K
  --list-types --list-palettes --list-themes --version
  Environment: CHARTS_GFX, CHARTS_FB (default /dev/fb0), NO_COLOR.
)GUIDE";

void print_help(FILE *f) {
  std::fputs(GUIDE, f);
}

void print_schema(FILE *f) { print_help(f); }

} // namespace ch
