// chart.hpp -- render options, palettes and the one entry point charts.cpp fills.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "canvas.hpp"
#include "data.hpp"

namespace ch {

// Every colour used by the chrome lives here, so a theme is one place.
struct Skin {
  uint8_t frame = 8;       // border / rules
  uint8_t title = 15;
  uint8_t subtitle = 8;
  uint8_t axis = 7;
  uint8_t tick = 15;
  uint8_t grid = 8;
  uint8_t label = 7;
  uint8_t xlabel = 8;
  uint8_t ylabel = 8;
  uint8_t legend = 7;
  uint8_t value = 15;
  uint8_t shadow = 8;
  uint8_t table_head = 15;
  uint8_t table_rule = 8;
  uint8_t table_row = 7;
  uint8_t status = 7;
};

extern Skin S;

struct RenderOpts {
  std::string type = "bar";
  std::string title, subtitle, xlabel, ylabel;
  std::string palette = "dos";
  std::string frame = "double"; // double | single | heavy | ascii | none

  bool legend = true;
  bool values = false;
  bool grid = true;
  bool color = true;
  bool ascii = false;
  bool shadow = true;
  bool zero_base = true;
  bool xy = false;

  int explode = -2; // -2 never, -1 biggest slice, >=0 index
  int depth = 2;    // 3-D extrusion rows
  int bins = 10;
  int prec = -1; // number formatting override

  double lo = 0, hi = 0;
  bool has_lo = false, has_hi = false;
};

std::vector<std::string> type_names();
bool type_valid(const std::string &t);
std::vector<std::string> palette_names();
std::vector<int> palette_cols(const std::string &name, std::size_t n);

// Draws the whole chart (frame, title, axes, data, legend) inside r.
void render_chart(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o);

} // namespace ch
