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

void apply_spec_to_load(const ChartSpec &s, LoadOpts &o) {
  if (s.has_delim && !o.has_delim) { o.delim = s.delim; o.has_delim = true; }
  if (s.has_transpose && !o.has_transpose) { o.transpose = s.transpose; o.has_transpose = true; }
  if (s.has_xy && !o.has_xy) { o.xy = s.xy; o.has_xy = true; }
  if (s.has_no_header && !o.has_no_header) { o.no_header = s.no_header; o.has_no_header = true; }
  if (s.has_label_col && !o.has_label_col) { o.label_col = s.label_col; o.has_label_col = true; }
  if (s.has_series_col && !o.has_series_col) { o.series_col = s.series_col; o.has_series_col = true; }
  if (s.has_label_key && !o.has_label_key) { o.label_key = s.label_key; o.has_label_key = true; }
}

// Pull "#chart ..." / "#charts ..." lines out of a CSV before the table is
// parsed, so a directive can shape how the columns are read.
static ChartSpec scan_directives(const std::string &text, std::string &err) {
  ChartSpec spec;
  std::istringstream in(text);
  std::string l;
  int n = 0;
  while (std::getline(in, l)) {
    std::string t = trim(l);
    if (!t.empty() && t[0] != '#') {
      n++;
      // Directives belong in the header. Do not go hunting through the body:
      // a '#' inside the data is a comment, not a place for settings.
      if (n > 40) break;
    }
    if (t.empty() || t[0] != '#') continue;
    std::string e;
    if (spec_from_directive(t, spec, e) && !e.empty() && err.empty()) err = e;
  }
  return spec;
}

Dataset load_csv(const std::string &text, const LoadOpts &o) {
  Dataset ds;
  if (text.empty()) throw std::runtime_error("empty input");

  std::string spec_err;
  ChartSpec file_spec = scan_directives(text, spec_err);
  if (!spec_err.empty()) throw std::runtime_error("bad #chart line: " + spec_err);

  LoadOpts opts = o;
  apply_spec_to_load(file_spec, opts);

  // first meaningful line, for delimiter sniffing
  std::string first_line;
  {
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
      if (!trim(l).empty() && l[0] != '#') { first_line = l; break; }
    }
  }
  char delim = opts.delim ? opts.delim : sniff_delim(first_line);

  Table t = parse_delimited(text, delim);
  if (opts.transpose) transpose_table(t);
  if (t.rows.empty()) throw std::runtime_error("no rows found");

  std::size_t cols = 0;
  for (auto &r : t.rows) cols = std::max(cols, r.size());
  if (cols == 0) throw std::runtime_error("no columns found");

  for (auto &r : t.rows) r.resize(cols, std::string());

  // header?
  bool header = !opts.no_header;
  {
    auto &r0 = t.rows[0];
    bool any_text = false, any_num = false;
    for (auto &f : r0) {
      double d;
      if (parse_num(f, d)) any_num = true;
      else if (!trim(f).empty()) any_text = true;
    }
    if (opts.no_header) header = false;
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
  int lc = opts.label_col;
  if (lc < 0) {
    if (cols > 1 && !numeric[0]) lc = 0;
  }
  if (lc >= static_cast<int>(cols)) throw std::runtime_error("label column out of range");

  // x column
  int xc = -1;
  if (opts.xy) {
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

  // A first column of rising numbers under a header (years, batch sizes,
  // thread counts) is usually the category, but it is read as a series unless
  // the file says otherwise.  Do not guess; say so.
  if (lc < 0 && xc < 0 && header && cols >= 2 && numeric[0] && opts.series_col < 0) {
    bool rising = ndata >= 2;
    double prev = 0;
    for (std::size_t r = first; r < t.rows.size() && rising; r++) {
      double d = 0;
      if (!parse_num(trim(t.rows[r][0]), d) || (r > first && d <= prev)) rising = false;
      prev = d;
    }
    if (rising)
      ds.hint = "the first column \"" + hdr[0] + "\" is numeric, so it was read as a series, not as the categories; "
                "if it is the category add \"labels_col\": 1 (or \"xy\": true to use it as a numeric X axis)";
  }

  ds.had_header = header;
  if (lc >= 0 && header) ds.label_name = hdr[static_cast<std::size_t>(lc)];
  if (xc >= 0) ds.x_name = hdr[static_cast<std::size_t>(xc)];
  auto lose = [&](const std::string &why) {
    if (!ds.lossy) ds.lossy_why = why;
    ds.lossy = true;
  };
  if (opts.transpose) lose("the file is read transposed");

  // series
  for (std::size_t c = 0; c < cols; c++) {
    int ci = static_cast<int>(c);
    if (ci == lc || ci == xc) continue;
    if (!numeric[c]) {
      if (nonblank[c] > 0) lose("column \"" + hdr[c] + "\" is not numeric and is not shown");
      continue;
    }
    if (opts.series_col >= 0 && ci != opts.series_col) { lose("series_col hides the other columns"); continue; }
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
  ds.spec = file_spec;
  if (ds.spec.has_title && ds.title.empty()) ds.title = ds.spec.title;
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

// ---- writing back ------------------------------------------------------------

namespace {

std::string csv_field(const std::string &f, char delim) {
  bool quote = f.empty() ? false : (f.front() == ' ' || f.back() == ' ' || f[0] == '#');
  for (char c : f)
    if (c == delim || c == '"' || c == '\n' || c == '\r') quote = true;
  if (!quote) return f;
  std::string o = "\"";
  for (char c : f) {
    if (c == '"') o += '"';
    o += c;
  }
  return o + "\"";
}

std::string num_text(double v) {
  if (!std::isfinite(v)) return "";
  char b[40];
  if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%.0f", v);
  else std::snprintf(b, sizeof b, "%.12g", v);
  return b;
}

std::string read_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool is_x(const Series &s) { return !s.name.empty() && s.name[0] == '\x01'; }

} // namespace

std::string dataset_to_csv(const Dataset &ds, const std::string &original, char delim) {
  std::string out;
  {
    std::istringstream in(original);
    std::string l;
    while (std::getline(in, l)) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      std::string t = trim(l);
      if (t.empty()) continue;
      if (t[0] != '#') break;
      out += l + "\n";
    }
  }
  const std::string d(1, delim);
  if (ds.had_header) {
    std::string row = csv_field(ds.label_name.empty() ? "label" : ds.label_name, delim);
    for (const auto &s : ds.series) row += d + csv_field(is_x(s) ? ds.x_name : s.name, delim);
    out += row + "\n";
  }
  for (std::size_t i = 0; i < ds.nrows(); i++) {
    std::string row = csv_field(i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1), delim);
    for (const auto &s : ds.series) row += d + (i < s.v.size() ? num_text(s.v[i]) : "");
    out += row + "\n";
  }
  return out;
}

Json dataset_to_json(const Dataset &ds) {
  Json j = Json::object();
  if (!ds.label_name.empty()) j.set("label_name", Json::string(ds.label_name));
  Json labels = Json::array();
  for (std::size_t i = 0; i < ds.nrows(); i++)
    labels.a.push_back(Json::string(i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1)));
  j.set("labels", labels);
  Json series = Json::array();
  for (const auto &s : ds.series) {
    Json vals = Json::array();
    for (double v : s.v) vals.a.push_back(std::isfinite(v) ? Json::number(v) : Json::null());
    if (is_x(s)) {
      j.set("x", vals);
      if (ds.x_name != "x") j.set("x_name", Json::string(ds.x_name));
      continue;
    }
    Json one = Json::object();
    one.set("name", Json::string(s.name));
    one.set("values", vals);
    series.a.push_back(one);
  }
  j.set("series", series);
  return j;
}

void save_dataset(const Dataset &ds, const std::string &path) {
  if (path.empty() || path == "-" || path == "<stdin>") throw std::runtime_error("this data came from a pipe; there is no file to save to");
  if (ds.lossy) throw std::runtime_error("not saving over " + path + ": " + ds.lossy_why);
  std::string original = read_file(path);
  std::string t = trim(original);
  std::string out;
  if (!t.empty() && (t[0] == '{' || t[0] == '[')) {
    Json fresh = dataset_to_json(ds);
    // Keep the file's own chart block and anything else it carried that is not
    // the data itself.
    Json old = parse_json(original);
    Json j = Json::object();
    if (old.is_obj()) {
      static const char *data_keys[] = {"labels", "series", "rows", "data", "records", "x", "x_name", "label_name"};
      for (const auto &kv : old.o) {
        bool is_data = is_number_array(kv.second) || kv.second.is_num();
        for (const char *k : data_keys)
          if (kv.first == k) is_data = true;
        if (kv.second.is_num() && spec_took_key(kv.first, true)) is_data = false;
        if (!is_data) j.set(kv.first, kv.second);
      }
    }
    for (const auto &kv : fresh.o) j.set(kv.first, kv.second);
    out = json_write(j);
  } else {
    char delim = ds.spec.has_delim ? ds.spec.delim : 0;
    if (!delim) {
      std::istringstream in(original);
      std::string l;
      while (std::getline(in, l))
        if (!trim(l).empty() && l[0] != '#') break;
      delim = l.empty() ? ',' : sniff_delim(l);
      if (path.size() > 4 && lower(path.substr(path.size() - 4)) == ".tsv" && l.empty()) delim = '\t';
    }
    out = dataset_to_csv(ds, original, delim);
  }
  // Write beside the file and rename over it, so a crash cannot leave half.
  std::string tmp = path + ".charts-tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write: " + path);
    f << out;
    if (!f.good()) throw std::runtime_error("write failed: " + path);
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    throw std::runtime_error("cannot replace: " + path);
  }
}

std::string describe(const Dataset &ds) {
  std::ostringstream o;
  o << "source:  " << ds.source << "\n";
  if (!ds.title.empty()) o << "title:   " << ds.title << "\n";
  o << "rows:    " << ds.nrows() << "\n";
  o << "labels:  " << ds.labels.size() << "\n";
  o << "series:  " << ds.series.size() << "\n";

  // What the file itself asked for, so it is obvious where a setting came from.
  if (ds.spec.any()) {
    std::string s;
    auto add = [&](const std::string &k, const std::string &v) {
      if (!s.empty()) s += ", ";
      s += k + "=" + v;
    };
    if (ds.spec.has_type) add("type", ds.spec.type);
    if (ds.spec.has_palette) add("palette", ds.spec.palette);
    if (ds.spec.has_frame) add("frame", ds.spec.frame);
    if (ds.spec.has_title) add("title", "\"" + ds.spec.title + "\"");
    if (ds.spec.has_subtitle) add("subtitle", "\"" + ds.spec.subtitle + "\"");
    if (ds.spec.has_xlabel) add("xlabel", "\"" + ds.spec.xlabel + "\"");
    if (ds.spec.has_ylabel) add("ylabel", "\"" + ds.spec.ylabel + "\"");
    if (ds.spec.has_legend) add("legend", ds.spec.legend ? "true" : "false");
    if (ds.spec.has_values) add("values", ds.spec.values ? "true" : "false");
    if (ds.spec.has_grid) add("grid", ds.spec.grid ? "true" : "false");
    if (ds.spec.has_shadow) add("shadow", ds.spec.shadow ? "true" : "false");
    if (ds.spec.has_color) add("color", ds.spec.color ? "true" : "false");
    if (ds.spec.has_ascii) add("ascii", ds.spec.ascii ? "true" : "false");
    if (ds.spec.has_depth) add("depth", std::to_string(ds.spec.depth));
    if (ds.spec.has_bins) add("bins", std::to_string(ds.spec.bins));
    if (ds.spec.has_prec) add("prec", std::to_string(ds.spec.prec));
    if (ds.spec.has_explode) add("explode", std::to_string(ds.spec.explode));
    if (ds.spec.has_lo) add("min", fmt_val(ds.spec.lo));
    if (ds.spec.has_hi) add("max", fmt_val(ds.spec.hi));
    if (ds.spec.has_width) add("width", std::to_string(ds.spec.width));
    if (ds.spec.has_height) add("height", std::to_string(ds.spec.height));
    if (ds.spec.has_xy) add("xy", ds.spec.xy ? "true" : "false");
    if (ds.spec.has_transpose) add("transpose", ds.spec.transpose ? "true" : "false");
    if (ds.spec.has_no_header) add("no_header", ds.spec.no_header ? "true" : "false");
    if (ds.spec.has_label_col) add("labels_col", std::to_string(ds.spec.label_col + 1));
    if (ds.spec.has_series_col) add("series_col", std::to_string(ds.spec.series_col + 1));
    if (ds.spec.has_label_key) add("label_key", "\"" + ds.spec.label_key + "\"");
    if (ds.spec.has_delim) add("delim", ds.spec.delim == '\t' ? "tab" : std::string(1, ds.spec.delim));
    o << "chart:   " << s << "\n";
  } else {
    o << "chart:   (none in the file; a bare 'charts file' uses the defaults)\n";
  }

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
