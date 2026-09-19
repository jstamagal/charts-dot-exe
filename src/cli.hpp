// cli.hpp -- argument parsing and the help text.
#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "data.hpp"
#include "spec.hpp"

namespace ch {

// A mistake on the command line, as opposed to bad input: exit status 2.
struct UsageError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Args {
  std::vector<std::string> files;
  std::vector<std::string> types; // comma separated -t, may hold several
  std::string out, png, png_dir, launcher;
  std::string example;
  std::string gfx = "auto", theme;

  // Chart settings typed by hand.  They beat the deck, which beats the data
  // file, which beats the defaults.
  ChartSpec cli;
  LoadOpts load;

  int width = 0, height = 0; // cells
  int scale = 0;             // pixel backends and --png: 0 = choose
  int slide = 0;             // 1-based; 0 = not given

  bool show = false, print = false, check = false, json = false, describe = false;
  bool tile = false, ascii = false, verbose = false;
  bool color = true, color_forced = false;
  bool list_types = false, list_palettes = false, list_themes = false;
  bool help = false, version = false, schema = false;
  bool demo = false; // present the built-in deck
};

Args parse_args(int argc, char **argv);
void print_help(FILE *f);
void print_types(FILE *f);
void print_palettes(FILE *f);
void print_themes(FILE *f);
void print_version(FILE *f);
void print_example(FILE *f, const std::string &what);
// The deck --demo presents: every bit of data inline, so it needs no files.
const char *demo_deck();
void print_schema(FILE *f);

} // namespace ch
