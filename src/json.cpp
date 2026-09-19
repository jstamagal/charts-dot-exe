#include "json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "data.hpp"
#include "util.hpp"

namespace ch {

const Json *Json::get(const std::string &key) const {
  for (const auto &kv : o)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

// An array whose every member is a number (or a null gap).
bool is_number_array(const Json &j) {
  if (!j.is_arr()) return false;
  for (const auto &e : j.a)
    if (!e.is_num() && !e.is_null()) return false;
  return true;
}

namespace {

struct Parser {
  const std::string &s;
  std::size_t i = 0;
  int depth = 0;

  explicit Parser(const std::string &text) : s(text) {}

  [[noreturn]] void fail(const std::string &msg) const {
    int line = 1, col = 1;
    for (std::size_t k = 0; k < i && k < s.size(); k++) {
      if (s[k] == '\n') { line++; col = 1; }
      else col++;
    }
    throw std::runtime_error("json: " + msg + " at line " + std::to_string(line) +
                             " col " + std::to_string(col));
  }

  void skip() {
    while (i < s.size()) {
      char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
      if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
        while (i < s.size() && s[i] != '\n') i++;
        continue;
      }
      if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
        i += 2;
        while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
        i += 2;
        continue;
      }
      break;
    }
  }

  char peek() {
    skip();
    if (i >= s.size()) fail("unexpected end of input");
    return s[i];
  }

  void expect(char c) {
    skip();
    if (i >= s.size() || s[i] != c) fail(std::string("expected '") + c + "'");
    i++;
  }

  Json value() {
    if (++depth > 96) fail("too deeply nested");
    char c = peek();
    Json v;
    switch (c) {
    case '{': v = object(); break;
    case '[': v = array(); break;
    case '"': v.t = Json::STR; v.s = string(); break;
    case 't':
      lit("true");
      v.t = Json::BOOL;
      v.b = true;
      break;
    case 'f':
      lit("false");
      v.t = Json::BOOL;
      v.b = false;
      break;
    case 'n':
      lit("null");
      v.t = Json::NUL;
      break;
    default: v = number(); break;
    }
    depth--;
    return v;
  }

  void lit(const char *w) {
    skip();
    std::size_t n = std::char_traits<char>::length(w);
    if (s.compare(i, n, w) != 0) fail("bad literal");
    i += n;
  }

  Json number() {
    skip();
    std::size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
    bool digits = false;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0)) { i++; digits = true; }
    if (i < s.size() && s[i] == '.') {
      i++;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) { i++; digits = true; }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      i++;
      if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) i++;
    }
    if (!digits) fail("expected a value");
    Json v;
    v.t = Json::NUM;
    v.n = std::strtod(s.substr(start, i - start).c_str(), nullptr);
    if (!std::isfinite(v.n)) fail("number out of range");
    return v;
  }

  std::string string() {
    expect('"');
    std::string out;
    while (true) {
      if (i >= s.size()) fail("unterminated string");
      char c = s[i++];
      if (c == '"') break;
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) fail("unterminated escape");
      char e = s[i++];
      switch (e) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case '/': out += '/'; break;
      case '\\': out += '\\'; break;
      case '"': out += '"'; break;
      case 'u': {
        if (i + 4 > s.size()) fail("bad \\u escape");
        unsigned cp = 0;
        for (int k = 0; k < 4; k++) {
          char h = s[i++];
          cp <<= 4;
          if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
          else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
          else fail("bad hex digit in \\u escape");
        }
        char32_t out_cp = cp;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
          unsigned lo = 0;
          std::size_t j = i + 2;
          bool ok = true;
          for (int k = 0; k < 4 && j + k < s.size(); k++) {
            char h = s[j + k];
            lo <<= 4;
            if (h >= '0' && h <= '9') lo |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') lo |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') lo |= static_cast<unsigned>(h - 'A' + 10);
            else { ok = false; break; }
          }
          if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
            out_cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 6;
          }
        }
        out += u32_to_utf8(out_cp);
        break;
      }
      default: fail("unknown escape");
      }
    }
    return out;
  }

  Json object() {
    Json v;
    v.t = Json::OBJ;
    expect('{');
    if (peek() == '}') { i++; return v; }
    while (true) {
      std::string key = string();
      expect(':');
      v.o.emplace_back(key, value());
      char c = peek();
      if (c == ',') { i++; continue; }
      if (c == '}') { i++; break; }
      fail("expected ',' or '}'");
    }
    return v;
  }

  Json array() {
    Json v;
    v.t = Json::ARR;
    expect('[');
    if (peek() == ']') { i++; return v; }
    while (true) {
      v.a.push_back(value());
      char c = peek();
      if (c == ',') { i++; continue; }
      if (c == ']') { i++; break; }
      fail("expected ',' or ']'");
    }
    return v;
  }
};

const char *kLabelKeys[] = {"label", "name", "key",  "category", "cat",
                            "x",     "date", "time", "month",    "year"};

bool is_label_key(const std::string &k) {
  for (const char *c : kLabelKeys)
    if (ieq(k, c)) return true;
  return false;
}

std::vector<double> numbers_of(const Json &j, bool &ok) {
  std::vector<double> out;
  ok = true;
  if (!j.is_arr()) { ok = false; return out; }
  for (const auto &e : j.a) {
    if (e.is_num()) out.push_back(e.n);
    else if (e.is_null()) out.push_back(std::nan(""));
    else { ok = false; out.push_back(std::nan("")); }
  }
  return out;
}

// Read loose top-level spec fields, next to the data, the way a quick script
// tends to emit them.  A numeric value only counts as a setting for the few
// fields that could never be a category name.
void loose_spec(const Json &j, Dataset &ds) {
  for (const auto &kv : j.o) {
    if (kv.first == "chart" || !is_spec_key(kv.first)) continue;
    if (kv.second.is_num() && !numeric_spec_ok(kv.first)) continue;
    Json one;
    one.t = Json::OBJ;
    one.o.emplace_back(kv.first, kv.second);
    spec_from_json(one, ds.spec);
  }
}

void add_series_from_obj(Dataset &ds, const Json &obj) {
  for (const auto &kv : obj.o) {
    bool ok = false;
    std::vector<double> v = numbers_of(kv.second, ok);
    if (!ok) continue;
    Series s;
    s.name = kv.first;
    s.v = v;
    ds.series.push_back(s);
  }
}

bool add_series_from_arr(Dataset &ds, const Json &arr) {
  std::size_t added = 0;
  for (const auto &e : arr.a) {
    if (!e.is_obj()) continue;
    const Json *nm = e.get("name");
    if (!nm) nm = e.get("label");
    const Json *vals = e.get("values");
    if (!vals) vals = e.get("data");
    if (!vals) vals = e.get("v");
    if (!vals) continue;
    bool ok = false;
    std::vector<double> v = numbers_of(*vals, ok);
    if (!ok) continue;
    Series s;
    s.name = nm ? nm->str_or("series " + std::to_string(added + 1))
                : "series " + std::to_string(added + 1);
    s.v = v;
    ds.series.push_back(s);
    added++;
  }
  return added > 0;
}

void labels_from(const Json &j, Dataset &ds) {
  ds.labels.clear();
  for (const auto &e : j.a) {
    if (e.is_str()) ds.labels.push_back(e.s);
    else if (e.is_num()) ds.labels.push_back(fmt_val(e.n));
    else ds.labels.push_back("?");
  }
}

// array of records: [ {..}, {..} ]
bool dataset_from_records(const Json &arr, Dataset &ds, const LoadOpts &o) {
  std::vector<std::string> keys;
  for (const auto &rec : arr.a) {
    if (!rec.is_obj()) return false;
    for (const auto &kv : rec.o) {
      bool seen = false;
      for (const auto &k : keys)
        if (k == kv.first) { seen = true; break; }
      if (!seen) keys.push_back(kv.first);
    }
  }
  if (keys.empty()) return false;

  std::string label_key = o.label_key;
  if (label_key.empty()) {
    // A field called "x" or "date" is only a label if it is not numbers.
    // Otherwise [{"x":1,"y":10}] would lose its x values to the label slot
    // and could never be drawn as a scatter.
    for (const auto &k : keys) {
      if (!is_label_key(k)) continue;
      std::size_t nn = 0, tot = 0;
      for (const auto &rec : arr.a) {
        const Json *v = rec.get(k);
        if (!v || v->is_null()) continue;
        tot++;
        if (v->is_num()) nn++;
      }
      if (tot > 0 && nn * 2 > tot) continue; // mostly numbers: it is data
      label_key = k;
      break;
    }
  }

  std::vector<std::string> numkeys;
  for (const auto &k : keys) {
    if (k == label_key) continue;
    std::size_t nn = 0, tot = 0;
    for (const auto &rec : arr.a) {
      const Json *v = rec.get(k);
      if (!v) continue;
      tot++;
      if (v->is_num()) nn++;
    }
    if (tot > 0 && nn * 2 >= tot) numkeys.push_back(k);
  }
  if (numkeys.empty()) return false;

  ds.labels.clear();
  for (std::size_t r = 0; r < arr.a.size(); r++) {
    const Json *v = label_key.empty() ? nullptr : arr.a[r].get(label_key);
    if (v && v->is_str()) ds.labels.push_back(v->s);
    else if (v && v->is_num()) ds.labels.push_back(fmt_val(v->n));
    else ds.labels.push_back(std::to_string(r + 1));
  }
  for (const auto &k : numkeys) {
    Series s;
    s.name = k;
    for (const auto &rec : arr.a) {
      const Json *v = rec.get(k);
      s.v.push_back(v && v->is_num() ? v->n : std::nan(""));
    }
    ds.series.push_back(s);
  }
  return true;
}

} // namespace

Json *Json::find(const std::string &key) {
  if (t != OBJ) return nullptr;
  for (auto &kv : o)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

Json &Json::set(const std::string &key, const Json &v) {
  t = OBJ;
  if (Json *old = find(key)) { *old = v; return *old; }
  o.emplace_back(key, v);
  return o.back().second;
}

void Json::erase(const std::string &key) {
  for (std::size_t i = 0; i < o.size(); i++)
    if (o[i].first == key) { o.erase(o.begin() + static_cast<long>(i)); return; }
}

namespace {

void write_string(std::string &out, const std::string &raw) {
  const std::string s = clean_utf8(raw); // what we write is always valid JSON
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
      else out += static_cast<char>(c);
    }
  }
  out += '"';
}

void write_number(std::string &out, double n) {
  if (!std::isfinite(n)) { out += "null"; return; }
  char b[40];
  if (n == std::floor(n) && std::fabs(n) < 1e15) std::snprintf(b, sizeof b, "%.0f", n);
  else std::snprintf(b, sizeof b, "%.12g", n);
  out += b;
}

bool scalar(const Json &j) { return !j.is_arr() && !j.is_obj(); }

void write_value(std::string &out, const Json &j, int depth) {
  const std::string pad(static_cast<std::size_t>(depth + 1) * 2, ' ');
  const std::string close(static_cast<std::size_t>(depth) * 2, ' ');
  switch (j.t) {
  case Json::NUL: out += "null"; break;
  case Json::BOOL: out += j.b ? "true" : "false"; break;
  case Json::NUM: write_number(out, j.n); break;
  case Json::STR: write_string(out, j.s); break;
  case Json::ARR: {
    if (j.a.empty()) { out += "[]"; break; }
    bool flat = true;
    for (const auto &e : j.a)
      if (!scalar(e)) flat = false;
    if (flat) {
      out += '[';
      for (std::size_t i = 0; i < j.a.size(); i++) {
        if (i) out += ", ";
        write_value(out, j.a[i], depth + 1);
      }
      out += ']';
      break;
    }
    out += "[\n";
    for (std::size_t i = 0; i < j.a.size(); i++) {
      out += pad;
      write_value(out, j.a[i], depth + 1);
      out += i + 1 < j.a.size() ? ",\n" : "\n";
    }
    out += close + "]";
    break;
  }
  case Json::OBJ: {
    if (j.o.empty()) { out += "{}"; break; }
    // A small object of scalars (an annotation, a series header) reads better
    // on one line.
    bool flat = j.o.size() <= 6 && depth > 0;
    std::size_t len = 0;
    for (const auto &kv : j.o) {
      if (!scalar(kv.second)) flat = false;
      len += kv.first.size() + kv.second.s.size() + 8;
    }
    if (flat && len < 90) {
      out += '{';
      for (std::size_t i = 0; i < j.o.size(); i++) {
        if (i) out += ", ";
        write_string(out, j.o[i].first);
        out += ": ";
        write_value(out, j.o[i].second, depth + 1);
      }
      out += '}';
      break;
    }
    out += "{\n";
    for (std::size_t i = 0; i < j.o.size(); i++) {
      out += pad;
      write_string(out, j.o[i].first);
      out += ": ";
      write_value(out, j.o[i].second, depth + 1);
      out += i + 1 < j.o.size() ? ",\n" : "\n";
    }
    out += close + "}";
    break;
  }
  }
}

} // namespace

std::string json_write(const Json &j) {
  std::string out;
  write_value(out, j, 0);
  out += '\n';
  return out;
}

Json parse_json(const std::string &text) {
  Parser p(text);
  p.skip();
  if (p.i >= text.size()) throw std::runtime_error("json: empty input");
  Json v = p.value();
  p.skip();
  if (p.i < text.size()){
    p.fail("trailing junk after value");
  }
  return v;
}

Dataset from_json(const std::string &text, const LoadOpts &o) { return from_json_value(parse_json(text), o); }

Dataset from_json_value(const Json &j, const LoadOpts &o) {
  Dataset ds;

  // ---- read the chart's description of itself first.
  // It has to come before the data is shaped: a spec may say which column holds
  // the labels, or that the file has no header, and that changes the parse.
  if (j.is_obj()) {
    if (const Json *block = j.get("chart")) spec_from_json(*block, ds.spec);
    loose_spec(j, ds);
  }

  LoadOpts opts = o;
  apply_spec_to_load(ds.spec, opts);

  auto set_title = [&](const Json &obj) {
    if (const Json *t = obj.get("title")) ds.title = t->str_or("");
  };

  if (j.is_obj()) {
    set_title(j);
    const Json *xlab = j.get("x");
    const Json *rows = j.get("rows");
    if (!rows) rows = j.get("data");
    if (!rows) rows = j.get("records");
    const Json *series = j.get("series");

    if (rows && rows->is_arr()) {
      if (!rows->a.empty() && rows->a[0].is_arr()) {
        Json copy = *rows; // rows with a header: the top-level array shape
        Dataset inner = from_json_value(copy, opts);
        ds.labels = inner.labels;
        ds.series = inner.series;
      } else {
        dataset_from_records(*rows, ds, opts);
      }
    } else if (series) {
      if (series->is_arr()) {
        if (!add_series_from_arr(ds, *series)) {
          // array of arrays: first row may be a header
          bool allstr = true, anysym = false;
          for (const auto &e : series->a) {
            if (e.is_arr()) anysym = true;
            for (const auto &c : e.a)
              if (!c.is_str()) allstr = false;
          }
          if (anysym && allstr && series->a.size() > 1) {
            std::vector<std::string> hdr;
            for (const auto &c : series->a[0].a) hdr.push_back(c.str_or(""));
            for (std::size_t c = 1; c < series->a[0].a.size(); c++) {
              Series s;
              s.name = c < hdr.size() && !hdr[c].empty() ? hdr[c] : "series " + std::to_string(c);
              for (std::size_t r = 1; r < series->a.size(); r++) {
                const Json &row = series->a[r];
                s.v.push_back(c < row.a.size() && row.a[c].is_num() ? row.a[c].n : std::nan(""));
              }
              ds.series.push_back(s);
            }
            ds.labels.clear();
            for (std::size_t r = 1; r < series->a.size(); r++) {
              const Json &row = series->a[r];
              ds.labels.push_back(!row.a.empty() ? row.a[0].str_or(fmt_val(row.a[0].n))
                                                 : std::to_string(r));
            }
          }
        }
      } else if (series->is_obj()) {
        add_series_from_obj(ds, *series);
      }
    } else if (xlab && xlab->is_arr()) {
      // {x:[...], y:[...]} or {x:[..], <name>:[..]}
      for (const auto &kv : j.o) {
        if (kv.first == "x" || !is_number_array(kv.second)) continue;
        bool ok = false;
        std::vector<double> v = numbers_of(kv.second, ok);
        if (!ok) continue;
        Series s;
        s.name = kv.first;
        s.v = v;
        ds.series.push_back(s);
      }
      if (!ds.series.empty()) labels_from(*xlab, ds);
    } else {
      // object of numbers -> one series, keys become labels
      std::vector<std::string> keys;
      for (const auto &kv : j.o) {
        if (!kv.second.is_num()) continue;
        if (spec_took_key(kv.first, true)) continue; // already a setting
        keys.push_back(kv.first);
        ds.labels.push_back(kv.first);
      }
      if (!keys.empty()) {
        Series s;
        s.name = "value";
        for (const auto &kv : j.o) {
          if (!kv.second.is_num() || spec_took_key(kv.first, true)) continue;
          s.v.push_back(kv.second.n);
        }
        ds.series.push_back(s);
      }
    }
  } else if (j.is_arr()) {
    if (j.a.empty()) throw std::runtime_error("json: empty array");
    if (j.a[0].is_num() || j.a[0].is_null()) {
      Series s;
      s.name = "value";
      for (const auto &e : j.a) s.v.push_back(e.is_num() ? e.n : std::nan(""));
      ds.series.push_back(s);
    } else if (j.a[0].is_obj()) {
      if (!dataset_from_records(j, ds, opts)) throw std::runtime_error("json: no numeric fields in records");
    } else if (j.a[0].is_arr()) {
      bool allstr = true;
      for (const auto &c : j.a[0].a)
        if (!c.is_str()) allstr = false;
      std::size_t start = 0;
      std::vector<std::string> hdr;
      if (allstr) {
        for (const auto &c : j.a[0].a) hdr.push_back(c.str_or(""));
        start = 1;
      }
      std::size_t ncol = j.a[0].a.size();
      for (std::size_t c = 1; c < ncol; c++) {
        Series s;
        s.name = c < hdr.size() && !hdr[c].empty() ? hdr[c] : "col" + std::to_string(c + 1);
        for (std::size_t r = start; r < j.a.size(); r++) {
          const Json &row = j.a[r];
          s.v.push_back(c < row.a.size() && row.a[c].is_num() ? row.a[c].n : std::nan(""));
        }
        ds.series.push_back(s);
      }
      for (std::size_t r = start; r < j.a.size(); r++) {
        const Json &row = j.a[r];
        if (!row.a.empty()) {
          if (row.a[0].is_str()) ds.labels.push_back(row.a[0].s);
          else if (row.a[0].is_num()) ds.labels.push_back(fmt_val(row.a[0].n));
          else ds.labels.push_back(std::to_string(r - start + 1));
        } else {
          ds.labels.push_back(std::to_string(r - start + 1));
        }
      }
    } else {
      throw std::runtime_error("json: unsupported array shape");
    }
  } else {
    throw std::runtime_error("json: top level must be an object or array");
  }

  // "labels" beside the data always names the rows, and a numeric "x" beside
  // named series makes it an XY chart.
  if (j.is_obj()) {
    const Json *labels = j.get("labels");
    if (labels && labels->is_arr() && !labels->a.empty()) labels_from(*labels, ds);
    const Json *x = j.get("x");
    if (x && is_number_array(*x) && j.get("series") && !ds.series.empty()) {
      bool ok = false;
      Series xs;
      xs.name = "\x01x";
      xs.v = numbers_of(*x, ok);
      ds.series.insert(ds.series.begin(), xs);
      if (const Json *xn = j.get("x_name")) ds.x_name = xn->str_or("x");
    }
    if (const Json *ln = j.get("label_name")) ds.label_name = ln->str_or("");
  }

  if (ds.series.empty()) throw std::runtime_error("json: no numeric series found");
  if (ds.spec.has_title && ds.title.empty()) ds.title = ds.spec.title;
  return ds;
}

} // namespace ch
