#include "data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "util.hpp"

namespace ch {

// ---- model -----------------------------------------------------------------

std::size_t Dataset::nrows() const {
  std::size_t n = 0;
  for (const auto &s : series) n = std::max(n, s.v.size());
  return n;
}

void Dataset::bounds(double &lo, double &hi) const {
  lo = 0;
  hi = 0;
  bool any = false;
  for (const auto &s : series) {
    for (double v : s.v) {
      if (!std::isfinite(v)) continue;
      if (!any) { lo = hi = v; any = true; }
      else { lo = std::min(lo, v); hi = std::max(hi, v); }
    }
  }
  if (!any) { lo = 0; hi = 1; }
}

void Dataset::segment_bounds(int from, int to, double &lo, double &hi) const {
  lo = 0;
  hi = 0;
  bool any = false;
  for (int si = from; si < to && si < static_cast<int>(series.size()); si++) {
    for (double v : series[static_cast<std::size_t>(si)].v) {
      if (!std::isfinite(v)) continue;
      if (!any) { lo = hi = v; any = true; }
      else { lo = std::min(lo, v); hi = std::max(hi, v); }
    }
  }
  if (!any) { lo = 0; hi = 1; }
}

double Dataset::sum(std::size_t si) const {
  if (si >= series.size()) return 0;
  double t = 0;
  for (double v : series[si].v)
    if (std::isfinite(v)) t += v;
  return t;
}

double Dataset::total() const {
  double t = 0;
  for (std::size_t i = 0; i < series.size(); i++) t += sum(i);
  return t;
}

bool Dataset::has_negative() const {
  for (const auto &s : series)
    for (double v : s.v)
      if (std::isfinite(v) && v < 0) return true;
  return false;
}

// ---- ticks -----------------------------------------------------------------

std::vector<double> nice_ticks(double lo, double hi, int want) {
  std::vector<double> t;
  if (!std::isfinite(lo) || !std::isfinite(hi)) return t;
  if (hi < lo) std::swap(lo, hi);
  if (want < 2) want = 2;
  if (want > 16) want = 16;
  double span = hi - lo;
  if (!(span > 0)) {
    t.push_back(lo);
    return t;
  }
  double step = nice_step(span / (want - 1));
  if (!(step > 0)) step = span;
  // A range that happens to fall between two ticks must still get two of them,
  // otherwise a single lonely label shows up on the axis.
  for (int tries = 0; tries < 8; tries++) {
    t.clear();
    double start = std::ceil(lo / step - 1e-9) * step;
    for (double v = start; v <= hi + step * 1e-6; v += step) {
      double r = std::fabs(v) < step * 1e-9 ? 0.0 : v;
      t.push_back(r);
      if (t.size() > 64) break;
    }
    if (t.size() >= 2 || want < 2) break;
    step /= 2;
  }
  if (t.empty()) t.push_back(lo);
  return t;
}

// ---- csv -------------------------------------------------------------------

namespace {

struct Table {
  std::vector<std::vector<std::string>> rows;
};

char sniff_delim(const std::string &line) {
  const char cands[] = {',', ';', '\t', '|'};
  char best = ',';
  int bn = -1;
  for (char c : cands) {
    int n = 0;
    for (char x : line)
      if (x == c) n++;
    if (n > bn) { bn = n; best = c; }
  }
  return best;
}

Table parse_delimited(const std::string &text, char delim) {
  Table t;
  std::vector<std::string> row;
  std::string field;
  bool q = false;
  bool saw = false;
  auto end_field = [&]() { row.push_back(field); field.clear(); };
  auto end_row = [&]() {
    end_field();
    bool all_blank = true;
    for (const auto &f : row)
      if (!trim(f).empty()) { all_blank = false; break; }
    if (!all_blank) t.rows.push_back(row);
    row.clear();
    saw = false;
  };
  for (std::size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (q) {
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; i++; }
        else q = false;
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') { q = true; saw = true; continue; }
    if (c == delim) { end_field(); saw = true; continue; }
    if (c == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') continue; }
    if (c == '\n') { end_row(); continue; }
    // comments only when the line has not started yet
    if (c == '#' && !saw && row.empty() && field.empty()) {
      while (i < text.size() && text[i] != '\n') i++;
      end_row();
      continue;
    }
    field += c;
    saw = true;
  }
  if (!field.empty() || !row.empty()) end_row();
  return t;
}

void transpose_table(Table &t) {
  Table o;
  std::size_t cols = 0;
  for (auto &r : t.rows) cols = std::max(cols, r.size());
  o.rows.resize(cols);
  for (std::size_t c = 0; c < cols; c++) {
    for (auto &r : t.rows) o.rows[c].push_back(c < r.size() ? r[c] : std::string());
  }
  t = o;
}

std::string col_name(const std::vector<std::string> &hdr, std::size_t i) {
  if (i < hdr.size()) {
    std::string s = trim(hdr[i]);
    if (!s.empty()) return s;
  }
  return "col" + std::to_string(i + 1);
}

} // namespace

Dataset load_csv(const std::string &text, const LoadOpts &o) {
  Dataset ds;
  if (text.empty()) throw std::runtime_error("empty input");

  // first meaningful line, for delimiter sniffing
  std::string first_line;
  {
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
      if (!trim(l).empty() && l[0] != '#') { first_line = l; break; }
    }
  }
  char delim = o.delim ? o.delim : sniff_delim(first_line);

  Table t = parse_delimited(text, delim);
  if (o.transpose) transpose_table(t);
  if (t.rows.empty()) throw std::runtime_error("no rows found");

  std::size_t cols = 0;
  for (auto &r : t.rows) cols = std::max(cols, r.size());
  if (cols == 0) throw std::runtime_error("no columns found");

  for (auto &r : t.rows) r.resize(cols, std::string());

  // header?
  bool header = !o.no_header;
  {
    auto &r0 = t.rows[0];
    bool any_text = false, any_num = false;
    for (auto &f : r0) {
      double d;
      if (parse_num(f, d)) any_num = true;
      else if (!trim(f).empty()) any_text = true;
    }
    if (o.no_header) header = false;
    else if (any_text && !any_num) header = true;
    else if (any_text && any_num) header = true; // mixed: treat as header
    else header = false;                          // all numeric
  }

  std::vector<std::string> hdr(cols);
  std::size_t first = 0;
  if (header) {
    for (std::size_t i = 0; i < cols; i++) hdr[i] = col_name(t.rows[0], i);
    first = 1;
  } else {
    for (std::size_t i = 0; i < cols; i++) hdr[i] = "col" + std::to_string(i + 1);
  }
  if (t.rows.size() <= first) throw std::runtime_error("header row with no data under it");

  const std::size_t ndata = t.rows.size() - first;

  // which columns are numeric?
  std::vector<bool> numeric(cols, true);
  std::vector<int> nonblank(cols, 0);
  for (std::size_t r = first; r < t.rows.size(); r++) {
    for (std::size_t c = 0; c < cols; c++) {
      double d;
      std::string f = trim(t.rows[r][c]);
      if (f.empty()) continue;
      nonblank[c]++;
      if (!parse_num(f, d)) numeric[c] = false;
    }
  }
  for (std::size_t c = 0; c < cols; c++)
    if (nonblank[c] == 0) numeric[c] = false;

  // label column
  int lc = o.label_col;
  if (lc < 0) {
    if (cols > 1 && !numeric[0]) lc = 0;
  }
  if (lc >= static_cast<int>(cols)) throw std::runtime_error("label column out of range");

  // x column
  int xc = -1;
  if (o.xy) {
    for (std::size_t c = 0; c < cols; c++) {
      if (static_cast<int>(c) == lc) continue;
      if (numeric[c]) { xc = static_cast<int>(c); break; }
    }
  }

  // labels
  ds.labels.reserve(ndata);
  for (std::size_t r = first; r < t.rows.size(); r++) {
    if (lc >= 0) {
      std::string s = trim(t.rows[r][static_cast<std::size_t>(lc)]);
      ds.labels.push_back(s.empty() ? std::to_string(r - first + 1) : s);
    } else {
      ds.labels.push_back(std::to_string(r - first + 1));
    }
  }

  // series
  for (std::size_t c = 0; c < cols; c++) {
    int ci = static_cast<int>(c);
    if (ci == lc || ci == xc) continue;
    if (!numeric[c]) continue;
    if (o.series_col >= 0 && ci != o.series_col) continue;
    Series s;
    s.name = hdr[c];
    s.v.reserve(ndata);
    for (std::size_t r = first; r < t.rows.size(); r++) {
      double d = std::nan("");
      std::string f = trim(t.rows[r][c]);
      if (!f.empty()) parse_num(f, d);
      s.v.push_back(d);
    }
    ds.series.push_back(s);
  }

  // xy: stash the x column as an extra series named "x" is not what we want, so
  // keep it under a reserved name and let the scatter chart pick it up.
  if (xc >= 0) {
    Series xs;
    xs.name = "\x01x";
    for (std::size_t r = first; r < t.rows.size(); r++) {
      double d = std::nan("");
      std::string f = trim(t.rows[r][static_cast<std::size_t>(xc)]);
      if (!f.empty()) parse_num(f, d);
      xs.v.push_back(d);
    }
    ds.series.insert(ds.series.begin(), xs);
  }

  if (ds.series.empty())
    throw std::runtime_error("no numeric columns found (csv needs a header row and numbers)");
  return ds;
}

Dataset load_text(const std::string &text, const std::string &name, const LoadOpts &o) {
  Dataset ds;
  std::string body = text;
  if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
      static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF)
    body.erase(0, 3);

  std::string t = trim(body);
  if (t.empty()) throw std::runtime_error("empty input: " + name);

  bool looks_json = t[0] == '{' || t[0] == '[';
  if (looks_json) ds = from_json(body, o);
  else ds = load_csv(body, o);

  ds.source = name;
  if (!ds.empty() && ds.labels.empty()) {
    for (std::size_t i = 0; i < ds.nrows(); i++) ds.labels.push_back(std::to_string(i + 1));
  }
  return ds;
}

Dataset load_path(const std::string &path, const LoadOpts &o) {
  std::string text;
  if (path == "-") {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    text = ss.str();
  } else {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    text = ss.str();
  }
  return load_text(text, path == "-" ? std::string("<stdin>") : path, o);
}

std::string describe(const Dataset &ds) {
  std::ostringstream o;
  o << "source:  " << ds.source << "\n";
  if (!ds.title.empty()) o << "title:   " << ds.title << "\n";
  o << "rows:    " << ds.nrows() << "\n";
  o << "labels:  " << ds.labels.size() << "\n";
  o << "series:  " << ds.series.size() << "\n";
  for (std::size_t i = 0; i < ds.series.size(); i++) {
    const Series &s = ds.series[i];
    double lo = 0, hi = 0;
    bool any = false;
    for (double v : s.v) {
      if (!std::isfinite(v)) continue;
      if (!any) { lo = hi = v; any = true; }
      else { lo = std::min(lo, v); hi = std::max(hi, v); }
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "  [%zu] %-16s n=%-4zu min=%-12s max=%-12s sum=%s",
                  i, s.name.c_str(), s.v.size(), fmt_val(lo).c_str(), fmt_val(hi).c_str(),
                  fmt_val(ds.sum(i)).c_str());
    o << buf << "\n";
  }
  if (!ds.labels.empty()) {
    o << "first labels: ";
    for (std::size_t i = 0; i < ds.labels.size() && i < 6; i++) {
      if (i) o << ", ";
      o << ds.labels[i];
    }
    if (ds.labels.size() > 6) o << ", ...";
    o << "\n";
  }
  return o.str();
}

} // namespace ch
