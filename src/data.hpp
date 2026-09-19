// data.hpp -- dataset model, loaders, axis maths.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "json.hpp"
#include "spec.hpp"

namespace ch {

struct Series {
  std::string name;
  std::vector<double> v;
  int color = 7; // assigned by the chart renderer
  int col = 0;   // column in the source, counted from 1 with the labels: what series_col takes
  int decimals = -1; // most digits after the point as the file wrote them; -1 = not known
};

// Words in a column no chart can draw, kept so a table can show them.
struct TextColumn {
  std::string name;
  int col = 0; // as Series::col; 0 = after the numbers
  std::vector<std::string> v;
};

struct Dataset {
  std::string title;
  std::string source;
  std::vector<std::string> labels; // x categories (may be row numbers)
  std::string label_name;          // header of the label column, for writing back
  int label_col = 0;               // which column that was, from 1; 0 = none or not known
  std::string x_name = "x";        // header of the X column under xy
  // The loader threw something away (a text column, a transpose, a series
  // filter), so writing this dataset over its file would lose data.
  bool had_header = true;
  // Something that was probably not meant, for --check to pass on.
  std::string hint;
  bool lossy = false;
  std::string lossy_why;
  std::vector<Series> series;
  std::vector<TextColumn> text;
  ChartSpec spec; // how the file asked to be drawn, if it said so

  bool empty() const { return series.empty() || nrows() == 0; }
  std::size_t nrows() const;
  void bounds(double &lo, double &hi) const; // finite values only
  void segment_bounds(int from, int to, double &lo, double &hi) const;
  double sum(std::size_t si) const;
  double total() const;
  bool has_negative() const;
};

struct LoadOpts {
  char delim = 0;        // 0 = sniff
  bool transpose = false;
  bool xy = false;       // first numeric column is the X axis
  bool no_header = false;
  int label_col = -1;    // -1 = auto
  int series_col = -1;   // -1 = all numeric
  std::string label_key; // json: record field to use as label

  // Which of the above the caller set on purpose.  A value the command line
  // set must never be overwritten by the data file's own spec.
  bool has_delim = false, has_transpose = false, has_xy = false, has_no_header = false;
  bool has_label_col = false, has_series_col = false, has_label_key = false;
};

// Fill in anything LoadOpts left unset from the file's own chart spec.
void apply_spec_to_load(const ChartSpec &s, LoadOpts &o);

Dataset load_text(const std::string &text, const std::string &name, const LoadOpts &o);
Dataset load_path(const std::string &path, const LoadOpts &o);
Dataset load_csv(const std::string &text, const LoadOpts &o);
Dataset from_json(const std::string &text, const LoadOpts &o);
Dataset from_json_value(const Json &j, const LoadOpts &o);

// Number the series the way series_col counts (the labels are column 1) and,
// when series_col is set, keep only that one.  Throws naming what is there
// when it points at nothing drawable.  The CSV loader does its own.
void number_series(Dataset &ds, int series_col);

// ---- writing back ----------------------------------------------------------
// The leading '#' lines of `original` (comments, #chart directives) are kept.
std::string dataset_to_csv(const Dataset &ds, const std::string &original, char delim);
// {"labels":[..],"series":[{"name":..,"values":[..]}]}, plus "x" under xy.
Json dataset_to_json(const Dataset &ds);
// Write ds over the file it came from, in the file's own format.  Throws when
// that would lose data (see Dataset::lossy) or the file cannot be written.
void save_dataset(const Dataset &ds, const std::string &path);

std::vector<double> nice_ticks(double lo, double hi, int want);
std::string describe(const Dataset &ds);

} // namespace ch
