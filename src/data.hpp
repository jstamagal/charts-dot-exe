// data.hpp -- dataset model, loaders, axis maths.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ch {

struct Series {
  std::string name;
  std::vector<double> v;
  int color = 7; // assigned by the chart renderer
};

struct Dataset {
  std::string title;
  std::string source;
  std::vector<std::string> labels; // x categories (may be row numbers)
  std::vector<Series> series;

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
};

Dataset load_text(const std::string &text, const std::string &name, const LoadOpts &o);
Dataset load_path(const std::string &path, const LoadOpts &o);
Dataset load_csv(const std::string &text, const LoadOpts &o);
Dataset from_json(const std::string &text, const LoadOpts &o);

std::vector<double> nice_ticks(double lo, double hi, int want);
std::string describe(const Dataset &ds);

} // namespace ch
