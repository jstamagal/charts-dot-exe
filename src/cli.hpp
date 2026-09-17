// cli.hpp -- argument parsing and the help text.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace ch {

struct Args {
  std::vector<std::string> files;
  std::vector<std::string> types; // comma separated -t, may hold several
  std::string palette = "dos";
  std::string title, subtitle, xlabel, ylabel;
  std::string frame; // empty = double
  std::string out;
  std::string example, label_key;

  int width = 0, height = 0;
  int watch_ms = 1000;
  int explode = -2, depth = 2, bins = 10, prec = -1;
  int label_col = -1, series_col = -1;

  double lo = 0, hi = 0;
  bool has_lo = false, has_hi = false;

  char delim = 0;

  bool interactive = false;
  bool legend = true, values = false, grid = true;
  bool color = true, ascii = false, shadow = true;
  bool xy = false, transpose = false, tile = false, watch = false;
  bool describe = false, no_header = false;
  bool list_types = false, list_palettes = false;
  bool help = false, version = false;
  bool color_forced = false;
};

Args parse_args(int argc, char **argv);
void print_help(FILE *f);
void print_types(FILE *f);
void print_palettes(FILE *f);
void print_version(FILE *f);
void print_example(FILE *f, const std::string &what);

} // namespace ch
