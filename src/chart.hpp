// chart.hpp -- render options, palettes, annotations and the one entry point
// charts.cpp fills.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data.hpp"
#include "scene.hpp"

namespace ch {

struct RenderOpts {
  std::string type = "bar";
  std::string title, subtitle, xlabel, ylabel;
  std::string palette = "dos";
  std::string frame = "double"; // double | single | heavy | ascii | none
  std::string source;           // printed into the bottom border when set

  bool legend = true;
  bool values = false;
  bool grid = true;
  bool color = true;
  bool shadow = true;
  bool zero_base = true;
  bool xy = false;

  int explode = -2; // -2 never, -1 biggest slice, >=0 index
  int depth = 2;    // 3-D extrusion, 0 = flat
  int bins = 10;
  int prec = -1; // number formatting override

  double lo = 0, hi = 0;
  bool has_lo = false, has_hi = false;

  std::vector<Annotation> notes;
  std::vector<std::pair<std::string, int>> colors; // per series or slice; empty name = by position
  std::vector<ErrorBars> errors;

  // Filled in by render_chart from `errors`: each drawn series' whisker ends
  // per row (NaN = none).  Empty when the chart has no error bars.
  std::vector<std::vector<double>> err_lo, err_hi;

  // The data point under the editing cursor, ringed so it can be found.
  int cur_series = -1, cur_index = -1;
};

// Spec on top of the defaults.  Fields the spec leaves alone keep the value
// already in `o`, so a caller can seed its own defaults first.
void apply_spec(const ChartSpec &s, RenderOpts &o);

std::vector<std::string> type_names();
bool type_valid(const std::string &t);
std::string type_canonical(const std::string &t); // aliases folded, lower case
std::vector<std::string> palette_names();
std::vector<int> palette_cols(const std::string &name, std::size_t n);

// Draws the whole chart (frame, title, axes, data, legend, annotations) in r.
void render_chart(Scene &sc, Rect r, Dataset &ds, const RenderOpts &o);

} // namespace ch
