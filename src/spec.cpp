// spec.cpp -- reading a chart description out of a data file.
#include "spec.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "chart.hpp"
#include "json.hpp"
#include "util.hpp"

namespace ch {

namespace {

// Keys are matched case-insensitively and with '-' or '_' treated alike, so
// "show-values", "show_values" and "ShowValues" all land in the same place.
std::string norm_key(const std::string &k) {
  std::string s = lower(trim(k));
  for (char &c : s)
    if (c == '-' || c == ' ') c = '_';
  return s;
}

[[noreturn]] void bad(const std::string &key, const std::string &want) {
  throw std::runtime_error("chart spec: " + key + " wants " + want);
}

bool json_bool(const Json &v, const std::string &key) {
  if (v.is_bool()) return v.b;
  if (v.is_num()) {
    if (v.n == 0) return false;
    if (v.n == 1) return true;
    bad(key, "true or false");
  }
  if (v.is_str()) {
    std::string s = lower(trim(v.s));
    if (s == "true" || s == "yes" || s == "on" || s == "1") return true;
    if (s == "false" || s == "no" || s == "off" || s == "0") return false;
    bad(key, "true or false");
  }
  bad(key, "true or false");
}

double json_num(const Json &v, const std::string &key, double lo, double hi) {
  double d;
  if (v.is_num()) d = v.n;
  else if (v.is_str() && parse_num(v.s, d)) { /* accepted, agents write "5" */ }
  else bad(key, "a number");
  if (!std::isfinite(d) || d < lo || d > hi)
    bad(key, "a number between " + fmt_val(lo) + " and " + fmt_val(hi));
  return d;
}

std::string json_str(const Json &v, const std::string &key) {
  if (v.is_str()) return v.s;
  if (v.is_num()) return fmt_val(v.n);
  if (v.is_bool()) return v.b ? "true" : "false";
  bad(key, "a string");
}

bool valid_frame(const std::string &f) {
  return f == "double" || f == "single" || f == "heavy" || f == "ascii" || f == "none";
}

bool palette_exists(const std::string &p) {
  if (starts_with(p, "custom:")) return true;
  for (const auto &n : palette_names())
    if (ieq(n, p)) return true;
  return false;
}

} // namespace

bool ChartSpec::any() const {
  return has_type || has_palette || has_frame || has_title || has_subtitle || has_xlabel ||
         has_ylabel || has_legend || has_values || has_grid || has_shadow || has_color || has_ascii ||
         has_depth || has_bins || has_prec || has_explode || has_lo || has_hi || has_width ||
         has_height || has_xy || has_transpose || has_no_header || has_label_col || has_series_col ||
         has_label_key || has_delim || has_notes || has_errors;
}

bool is_spec_key(const std::string &key) {
  static const char *keys[] = {
      "type",        "chart",      "kind",       "palette",     "colours",     "colors",
      "annotations", "series_colors", "frame",      "border",     "box",         "title",       "subtitle",
      "sub",         "xlabel",     "xtitle",     "x_label",     "ylabel",      "ytitle",
      "y_label",     "legend",     "show_legend","values",      "show_values", "labels_on_bars",
      "grid",        "show_grid",  "shadow",     "drop_shadow", "color",       "colour",
      "ascii",       "explode",    "depth",      "depth3d",     "extrude",     "bins",
      "buckets",     "prec",       "decimals",   "precision",   "min",         "lo",
      "ymin",        "max",        "hi",         "ymax",        "width",       "w",
      "height",      "h",          "xy",         "transpose",   "no_header",   "header",
      "labels_col",  "label_col",  "series_col", "label_key",   "delim",       "delimiter",
      "errors",      "error",      "error_bars", "errorbars",   "ranges",
  };
  std::string n = norm_key(key);
  for (const char *k : keys)
    if (n == k) return true;
  return false;
}

bool spec_took_key(const std::string &key, bool value_is_number) {
  if (!is_spec_key(key)) return false;
  return value_is_number ? numeric_spec_ok(key) : true;
}

// One place that knows every field, shared by the JSON block and the
// "#chart: key=value" shorthand, so the two can never drift apart.
static void set_field(ChartSpec &s, const std::string &rawkey, const Json &v) {
  std::string key = norm_key(rawkey);

  if (key == "type" || key == "chart" || key == "kind") {
    std::string t = type_canonical(json_str(v, rawkey));
    if (!type_valid(t))
      throw std::runtime_error("chart spec: unknown chart type \"" + t + "\" (see --list-types)");
    s.type = t;
    s.has_type = true;

  } else if (key == "annotations" || key == "notes_on_chart") {
    if (!v.is_arr()) bad(rawkey, "an array of annotation objects");
    s.notes.clear();
    for (std::size_t i = 0; i < v.a.size(); i++) {
      try {
        s.notes.push_back(annotation_from_json(v.a[i]));
      } catch (const std::exception &e) {
        throw std::runtime_error("annotations[" + std::to_string(i) + "]: " + e.what());
      }
    }
    s.has_notes = true;

  } else if ((key == "colours" || key == "colors" || key == "series_colors") && (v.is_obj() || v.is_arr())) {
    s.series_colors.clear();
    auto one = [&](const std::string &name, const Json &cv) {
      int c = -1;
      if (!parse_color(cv, c))
        throw std::runtime_error("chart spec: colors: \"" + cv.str_or("?") + "\" is not a colour (use a name like \"green\", \"dark red\", or 0..15)");
      s.series_colors.emplace_back(name, c);
    };
    if (v.is_obj()) for (const auto &kv : v.o) one(kv.first, kv.second);
    else for (const auto &e : v.a) one("", e);
    s.has_series_colors = true;

  } else if (key == "errors" || key == "error" || key == "error_bars" || key == "errorbars" || key == "ranges") {
    auto one = [&](const std::string &series, const Json &e) {
      ErrorBars b;
      b.series = series;
      if (e.is_str() && !trim(e.s).empty()) {
        b.lo = trim(e.s);
        b.plus_minus = true;
      } else if (e.is_arr() && e.a.size() == 2 && e.a[0].is_str() && e.a[1].is_str()) {
        b.lo = trim(e.a[0].s);
        b.hi = trim(e.a[1].s);
      } else {
        bad(rawkey, "a column name (+-), a [low, high] pair of column names, or an object of those by series");
      }
      s.errors.push_back(b);
    };
    s.errors.clear();
    if (v.is_obj()) for (const auto &kv : v.o) one(kv.first, kv.second);
    else one("", v);
    s.has_errors = true;

  } else if (key == "palette" && v.is_arr()) {
    s.palette = palette_from_json(v);
    s.has_palette = true;

  } else if (key == "palette" || key == "colours" || key == "colors") {
    std::string p = trim(json_str(v, rawkey));
    if (!palette_exists(p))
      throw std::runtime_error("chart spec: unknown palette \"" + p + "\" (see --list-palettes)");
    s.palette = p;
    s.has_palette = true;

  } else if (key == "frame" || key == "border" || key == "box") {
    std::string f = lower(trim(json_str(v, rawkey)));
    if (!valid_frame(f))
      throw std::runtime_error("chart spec: frame \"" + f +
                               "\" is not one of double, single, heavy, ascii, none");
    s.frame = f;
    s.has_frame = true;

  } else if (key == "title") { s.title = json_str(v, rawkey); s.has_title = true; }
  else if (key == "subtitle" || key == "sub") { s.subtitle = json_str(v, rawkey); s.has_subtitle = true; }
  else if (key == "xlabel" || key == "xtitle" || key == "x_label") {
    s.xlabel = json_str(v, rawkey); s.has_xlabel = true;
  } else if (key == "ylabel" || key == "ytitle" || key == "y_label") {
    s.ylabel = json_str(v, rawkey); s.has_ylabel = true;
  }

  else if (key == "legend" || key == "show_legend") { s.legend = json_bool(v, rawkey); s.has_legend = true; }
  else if (key == "values" || key == "show_values" || key == "labels_on_bars") {
    s.values = json_bool(v, rawkey); s.has_values = true;
  } else if (key == "grid" || key == "show_grid") { s.grid = json_bool(v, rawkey); s.has_grid = true; }
  else if (key == "shadow" || key == "drop_shadow") { s.shadow = json_bool(v, rawkey); s.has_shadow = true; }
  else if (key == "color" || key == "colour") { s.color = json_bool(v, rawkey); s.has_color = true; }
  else if (key == "ascii") { s.ascii = json_bool(v, rawkey); s.has_ascii = true; }

  else if (key == "explode") {
    if (v.is_bool()) s.explode = v.b ? -1 : -2;
    else s.explode = static_cast<int>(json_num(v, rawkey, -1, 512));
    s.has_explode = true;
  } else if (key == "depth" || key == "depth3d" || key == "extrude") {
    s.depth = static_cast<int>(json_num(v, rawkey, 0, 6));
    s.has_depth = true;
  } else if (key == "bins" || key == "buckets") {
    s.bins = static_cast<int>(json_num(v, rawkey, 1, 60));
    s.has_bins = true;
  } else if (key == "prec" || key == "decimals" || key == "precision") {
    s.prec = static_cast<int>(json_num(v, rawkey, -1, 8));
    s.has_prec = true;
  } else if (key == "min" || key == "lo" || key == "ymin") {
    s.lo = json_num(v, rawkey, -1e15, 1e15);
    s.has_lo = true;
  } else if (key == "max" || key == "hi" || key == "ymax") {
    s.hi = json_num(v, rawkey, -1e15, 1e15);
    s.has_hi = true;
  } else if (key == "width" || key == "w") {
    s.width = static_cast<int>(json_num(v, rawkey, 20, 1000));
    s.has_width = true;
  } else if (key == "height" || key == "h") {
    s.height = static_cast<int>(json_num(v, rawkey, 6, 500));
    s.has_height = true;
  }

  else if (key == "xy") { s.xy = json_bool(v, rawkey); s.has_xy = true; }
  else if (key == "transpose") { s.transpose = json_bool(v, rawkey); s.has_transpose = true; }
  else if (key == "no_header") { s.no_header = json_bool(v, rawkey); s.has_no_header = true; }
  else if (key == "header") {
    s.no_header = !json_bool(v, rawkey); // "header": false means --no-header
    s.has_no_header = true;
  } else if (key == "labels_col" || key == "label_col") {
    s.label_col = static_cast<int>(json_num(v, rawkey, 1, 1000)) - 1; // 1-based, like --labels-col
    s.has_label_col = true;
  } else if (key == "series_col") {
    s.series_col = static_cast<int>(json_num(v, rawkey, 1, 1000)) - 1;
    s.has_series_col = true;
  } else if (key == "label_key") { s.label_key = json_str(v, rawkey); s.has_label_key = true; }
  else if (key == "delim" || key == "delimiter") {
    std::string d = lower(trim(json_str(v, rawkey)));
    if (d == "tab" || d == "\\t") s.delim = '\t';
    else if (d == "comma") s.delim = ',';
    else if (d == "semi" || d == "semicolon") s.delim = ';';
    else if (d == "pipe") s.delim = '|';
    else if (d.size() == 1) s.delim = d[0];
    else throw std::runtime_error("chart spec: delim \"" + d + "\" is not one character or tab/comma/semi/pipe");
    s.has_delim = true;
  } else {
    // Unknown key: not fatal.  An agent may stash its own notes in the chart
    // block, and a future field should not break an older binary.
  }
}

// A few fields are meaningless as a category name, so a numeric value under
// them is still a setting.  Anything else numeric stays data: an object of
// numbers may legitimately have a category called "palette".
bool numeric_spec_ok(const std::string &key) {
  static const char *keys[] = {"width",    "height",  "depth",  "bins",     "prec",
                               "explode",  "min",     "max",    "lo",       "hi",
                               "ymin",     "ymax",    "w",      "h",        "buckets",
                               "decimals", "precision", "extrude", "depth3d"};
  std::string n = norm_key(key);
  for (const char *k : keys)
    if (n == k) return true;
  return false;
}

void merge_spec(ChartSpec &b, const ChartSpec &o) {
#define M(f) if (o.has_##f) { b.f = o.f; b.has_##f = true; }
  M(type) M(palette) M(frame) M(title) M(subtitle) M(xlabel) M(ylabel)
  M(legend) M(values) M(grid) M(shadow) M(color) M(ascii)
  M(depth) M(bins) M(prec) M(explode) M(lo) M(hi) M(width) M(height)
  M(xy) M(transpose) M(no_header) M(label_col) M(series_col) M(label_key) M(delim) M(notes) M(series_colors)
  M(errors)
#undef M
}

void apply_spec(const ChartSpec &s, RenderOpts &o) {
  if (s.has_type) o.type = s.type;
  if (s.has_palette) o.palette = s.palette;
  if (s.has_frame) o.frame = s.frame;
  if (s.has_title) o.title = s.title;
  if (s.has_subtitle) o.subtitle = s.subtitle;
  if (s.has_xlabel) o.xlabel = s.xlabel;
  if (s.has_ylabel) o.ylabel = s.ylabel;
  if (s.has_legend) o.legend = s.legend;
  if (s.has_values) o.values = s.values;
  if (s.has_grid) o.grid = s.grid;
  if (s.has_shadow) o.shadow = s.shadow;
  if (s.has_color) o.color = s.color;
  if (s.has_depth) o.depth = s.depth;
  if (s.has_bins) o.bins = s.bins;
  if (s.has_prec) o.prec = s.prec;
  if (s.has_explode) o.explode = s.explode;
  if (s.has_lo) { o.lo = s.lo; o.has_lo = true; }
  if (s.has_hi) { o.hi = s.hi; o.has_hi = true; }
  if (s.has_xy) o.xy = s.xy;
  if (s.has_notes) o.notes = s.notes;
  if (s.has_series_colors) o.colors = s.series_colors;
  if (s.has_errors) o.errors = s.errors;
}

// ---- colours and annotations -------------------------------------------------

namespace {
const char *COLOR_NAMES[16] = {"black",     "dark red",   "dark green",   "brown",      "dark blue", "dark magenta",
                               "dark cyan", "grey",       "dark grey",    "red",        "green",     "yellow",
                               "blue",      "magenta",    "cyan",         "white"};
}

std::string color_name(int c) { return (c >= 0 && c < 16) ? COLOR_NAMES[c] : "default"; }

bool parse_color(const Json &v, int &out) {
  if (v.is_num()) {
    if (v.n < 0 || v.n > 15 || v.n != std::floor(v.n)) return false;
    out = static_cast<int>(v.n);
    return true;
  }
  if (!v.is_str()) return false;
  std::string n = lower(trim(v.s));
  for (char &c : n)
    if (c == '_' || c == '-') c = ' ';
  if (n == "gray") n = "grey";
  if (n == "dark gray" || n == "darkgrey" || n == "darkgray") n = "dark grey";
  if (n == "orange") n = "brown";
  if (n == "purple" || n == "pink") n = "magenta";
  if (starts_with(n, "bright ")) n = n.substr(7);
  if (starts_with(n, "light ")) n = n.substr(6);
  for (int i = 0; i < 16; i++) {
    std::string want = COLOR_NAMES[i];
    std::string squashed;
    for (char c : want)
      if (c != ' ') squashed += c;
    if (n == want || n == squashed) { out = i; return true; }
  }
  double d;
  if (parse_num(n, d) && d >= 0 && d <= 15) { out = static_cast<int>(d); return true; }
  return false;
}

std::string palette_from_json(const Json &v) {
  if (!v.is_arr() || v.a.empty()) throw std::runtime_error("palette: a custom palette is a list of colours, e.g. [\"yellow\", \"cyan\", \"red\"]");
  std::string out = "custom:";
  for (std::size_t i = 0; i < v.a.size(); i++) {
    int c = -1;
    if (!parse_color(v.a[i], c))
      throw std::runtime_error("palette: \"" + v.a[i].str_or("?") + "\" is not a colour (use a name like \"green\", \"dark red\", or 0..15)");
    out += (i ? "," : "") + std::to_string(c);
  }
  return out;
}

Annotation annotation_from_json(const Json &obj) {
  if (!obj.is_obj()) throw std::runtime_error("must be an object like {\"at\":\"Mar\",\"text\":\"launch\"}");
  Annotation a;
  bool placed = false;
  // A number is a 0-based row when it is a whole one that fits an int; its
  // text may still name a category ("2.5") either way.
  auto row = [](double n) { return n >= 0 && n < 1e9 && n == std::floor(n) ? static_cast<int>(n) : -1; };
  auto category = [&](const Json &v) {
    if (v.is_str()) a.label = v.s;
    else if (v.is_num()) { a.label = fmt_val(v.n); a.index = row(v.n); }
    else throw std::runtime_error("the category must be a label or a 0-based row number");
  };
  for (const auto &kv : obj.o) {
    std::string k = norm_key(kv.first);
    const Json &v = kv.second;
    if (k == "text" || k == "label" || k == "say") a.text = json_str(v, kv.first);
    else if (k == "at" || k == "point" || k == "category") { category(v); a.kind = Annotation::POINT; placed = true; }
    else if (k == "series") {
      if (v.is_num()) a.series_i = row(v.n);
      else a.series = json_str(v, kv.first);
    } else if (k == "y" || k == "hline" || k == "value") {
      a.value = json_num(v, kv.first, -1e15, 1e15);
      a.kind = Annotation::HLINE;
      placed = true;
    } else if (k == "x" || k == "vline") { category(v); a.kind = Annotation::VLINE; placed = true; }
    else if (k == "note" || k == "pos" || k == "xy") {
      if (!v.is_arr() || v.a.size() != 2 || !v.a[0].is_num() || !v.a[1].is_num())
        throw std::runtime_error(kv.first + " wants [x, y], each 0..1 across the plot");
      a.fx = std::max(0.0, std::min(1.0, v.a[0].n));
      a.fy = std::max(0.0, std::min(1.0, v.a[1].n));
      a.kind = Annotation::NOTE;
      placed = true;
    } else if (k == "color" || k == "colour") {
      if (!parse_color(v, a.color)) throw std::runtime_error("unknown color (use a name like \"red\" or 0..15)");
    }
  }
  if (!placed) a.kind = Annotation::NOTE;
  if (a.text.empty() && a.kind != Annotation::HLINE && a.kind != Annotation::VLINE)
    throw std::runtime_error("needs \"text\"");
  return a;
}

Json annotation_to_json(const Annotation &a) {
  Json j = Json::object();
  auto category = [&](const char *key) {
    if (!a.label.empty()) j.set(key, Json::string(a.label));
    else j.set(key, Json::number(a.index));
  };
  switch (a.kind) {
  case Annotation::POINT:
    category("at");
    if (!a.series.empty()) j.set("series", Json::string(a.series));
    else if (a.series_i >= 0) j.set("series", Json::number(a.series_i));
    break;
  case Annotation::HLINE: j.set("y", Json::number(a.value)); break;
  case Annotation::VLINE: category("x"); break;
  case Annotation::NOTE: {
    Json p = Json::array();
    p.a.push_back(Json::number(a.fx));
    p.a.push_back(Json::number(a.fy));
    j.set("note", p);
    break;
  }
  }
  j.set("text", Json::string(a.text));
  if (a.color >= 0) j.set("color", Json::string(color_name(a.color)));
  return j;
}

void spec_from_json(const Json &obj, ChartSpec &out) {
  if (obj.is_null()) return;
  if (!obj.is_obj()) throw std::runtime_error("chart spec: the chart block must be an object");
  for (const auto &kv : obj.o) set_field(out, kv.first, kv.second);
}

// ---- the "#chart: ..." shorthand ------------------------------------------

namespace {

// "type=pie3d, title=\"Revenue, monthly\", values"
// A bare word is a boolean set to true.  Values may be quoted.
bool parse_kv_list(const std::string &payload, ChartSpec &out, std::string &err) {
  std::size_t i = 0;
  const std::size_t n = payload.size();
  while (i < n) {
    while (i < n && (std::isspace(static_cast<unsigned char>(payload[i])) != 0 || payload[i] == ',')) i++;
    if (i >= n) break;

    std::string key;
    while (i < n && payload[i] != '=' && payload[i] != ',' &&
           std::isspace(static_cast<unsigned char>(payload[i])) == 0) {
      key += payload[i++];
    }
    while (i < n && std::isspace(static_cast<unsigned char>(payload[i])) != 0) i++;

    Json value;
    if (i < n && payload[i] == '=') {
      i++;
      while (i < n && std::isspace(static_cast<unsigned char>(payload[i])) != 0) i++;
      std::string val;
      if (i < n && (payload[i] == '"' || payload[i] == '\'')) {
        char q = payload[i++];
        while (i < n && payload[i] != q) {
          if (payload[i] == '\\' && i + 1 < n) i++;
          val += payload[i++];
        }
        if (i < n) i++; // closing quote
        while (i < n && payload[i] != ',') i++;
        value.t = Json::STR;
        value.s = val;
      } else {
        while (i < n && payload[i] != ',') val += payload[i++];
        val = trim(val);
        double d;
        if (ieq(val, "true") || ieq(val, "yes") || ieq(val, "on")) { value.t = Json::BOOL; value.b = true; }
        else if (ieq(val, "false") || ieq(val, "no") || ieq(val, "off")) { value.t = Json::BOOL; value.b = false; }
        else if (parse_num(val, d)) { value.t = Json::NUM; value.n = d; }
        else if (!val.empty()) { value.t = Json::STR; value.s = val; }
        else { value.t = Json::BOOL; value.b = true; } // "key=" means true
      }
    } else {
      value.t = Json::BOOL;
      value.b = true;
    }
    if (key.empty()) continue;
    try {
      set_field(out, key, value);
    } catch (const std::exception &e) {
      err = e.what();
      return false;
    }
  }
  return true;
}

} // namespace

bool spec_from_directive(const std::string &line, ChartSpec &out, std::string &err) {
  std::string s = trim(line);
  if (s.empty() || s[0] != '#') return false;
  s = trim(s.substr(1));

  if (!starts_with(lower(s), "chart")) return false;
  s = trim(s.substr(5)); // "chart" or "charts"
  if (!s.empty() && (s[0] == 's' || s[0] == 'S')) s = trim(s.substr(1));

  // A prose comment must never be mistaken for settings.  This is the guard:
  // after the word "chart", accept only a colon, a JSON object, or something
  // that starts with a real field name.  "# charts for the daily build" is a
  // comment, "#chart: type=bar" and "#chart type=bar" are directives.
  bool explicit_colon = !s.empty() && s[0] == ':';
  if (explicit_colon) s = trim(s.substr(1));
  if (s.empty()) return explicit_colon; // "#charts:" asks for nothing
  if (s[0] != '{') {
    std::size_t e = 0;
    while (e < s.size() && s[e] != '=' && s[e] != ',' &&
           std::isspace(static_cast<unsigned char>(s[e])) == 0)
      e++;
    std::string first = s.substr(0, e);
    if (!explicit_colon && !is_spec_key(first)) return false; // prose
  }

  if (s[0] == '{') {
    try {
      Json j = parse_json(s);
      if (!j.is_obj()) {
        err = "the chart block must be an object";
      } else {
        for (const auto &kv : j.o) set_field(out, kv.first, kv.second);
      }
    } catch (const std::exception &e) {
      err = e.what();
    }
    return true;
  }
  parse_kv_list(s, out, err);
  return true;
}

} // namespace ch
