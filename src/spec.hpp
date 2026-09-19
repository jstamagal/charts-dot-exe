// spec.hpp -- the chart's own description of itself, carried by the data file.
//
// A data file may say how it wants to be drawn: type, title, palette, size and
// the rest.  Then `charts file` needs no flags at all, which is the point --
// whoever produced the data knows what it means.
//
// Precedence is always: command line > data file > built-in default.  A flag
// typed by hand must never lose to a file.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ch {

struct Json;

// Something said about the data, drawn on top of it.
//   {"at":"Mar", "series":"revenue", "text":"launch"}   a callout on one point
//   {"y":150, "text":"target"}                          a line across the plot
//   {"x":"Mar", "text":"price change"}                  a line up the plot
//   {"note":[0.8,0.1], "text":"n = 412"}                free text, 0..1 in the plot
struct Annotation {
  enum Kind { POINT, HLINE, VLINE, NOTE } kind = POINT;
  std::string text;
  std::string label;  // POINT / VLINE: the category, by name ...
  int index = -1;     // ... or by 0-based row
  std::string series; // POINT: the series, by name ...
  int series_i = -1;  // ... or by 0-based position
  double value = 0;   // HLINE
  double fx = 0.98, fy = 0.02;
  int color = -1;     // -1 = the theme's note colours
};

struct ChartSpec {
  // ---- how it looks
  std::string type, palette, frame;
  std::string title, subtitle, xlabel, ylabel;
  bool has_type = false, has_palette = false, has_frame = false;
  bool has_title = false, has_subtitle = false, has_xlabel = false, has_ylabel = false;

  bool legend = false, values = false, grid = true, shadow = true, color = true, ascii = false;
  bool has_legend = false, has_values = false, has_grid = false, has_shadow = false;
  bool has_color = false, has_ascii = false;

  int depth = 2, bins = 10, prec = -1, explode = -2;
  bool has_depth = false, has_bins = false, has_prec = false, has_explode = false;

  double lo = 0, hi = 0;
  bool has_lo = false, has_hi = false;

  int width = 0, height = 0;
  bool has_width = false, has_height = false;

  std::vector<Annotation> notes;
  bool has_notes = false;

  // "colors": {"revenue": "green"} by series (or pie slice) name, or
  // ["green", "red"] in order.  A positional entry has an empty name.
  std::vector<std::pair<std::string, int>> series_colors;
  bool has_series_colors = false;

  // ---- how the data should be read (applied before anything is drawn)
  bool xy = false, transpose = false, no_header = false;
  bool has_xy = false, has_transpose = false, has_no_header = false;
  int label_col = -1, series_col = -1;
  bool has_label_col = false, has_series_col = false;
  std::string label_key;
  bool has_label_key = false;
  char delim = 0;
  bool has_delim = false;

  bool any() const;
};

// Every field `over` sets replaces the one in `base`.
void merge_spec(ChartSpec &base, const ChartSpec &over);

// A palette given as a list of colours becomes "custom:11,14,10", which
// palette_cols() understands.  Throws on a bad colour.
std::string palette_from_json(const Json &v);

// "red", "bright blue", "dark green", "grey" or 0..15.  False when unknown.
bool parse_color(const Json &v, int &out);
std::string color_name(int c);

Annotation annotation_from_json(const Json &obj); // throws, naming the problem
Json annotation_to_json(const Annotation &a);

// Read spec fields out of a JSON object into out.  Throws std::runtime_error
// naming the offending field if a value is the wrong shape or out of range.
void spec_from_json(const Json &obj, ChartSpec &out);

// Is this key a spec field rather than data?  Used so that a top-level
// "palette" is not mistaken for a series in the object-of-numbers shape.
bool is_spec_key(const std::string &key);

// The handful of fields where a bare number is a setting rather than a
// category name (width, height, depth, bins, ...).
bool numeric_spec_ok(const std::string &key);

// Has this key been taken as a setting?  A numeric value only counts for the
// fields above, so this decides whether the key is still free to be data.
bool spec_took_key(const std::string &key, bool value_is_number);

// A CSV comment line may carry the spec:
//   #charts {"type":"pie3d","title":"Revenue"}
//   #chart: type=pie3d, title=Revenue, values
// Returns true when the line was a chart directive.  On a malformed payload it
// sets err and still returns true, so the caller can report it.
bool spec_from_directive(const std::string &line, ChartSpec &out, std::string &err);

} // namespace ch
