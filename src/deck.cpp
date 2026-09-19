// deck.cpp -- reading, checking and writing decks.  Drawing is in slide.cpp.
#include "deck.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "sources.hpp"
#include "util.hpp"

namespace ch {

namespace {

std::string read_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open: " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string dir_of(const std::string &path) {
  std::size_t s = path.find_last_of('/');
  return s == std::string::npos ? "" : path.substr(0, s);
}

std::string resolve(const std::string &dir, const std::string &ref) {
  if (ref.empty() || ref[0] == '/' || dir.empty()) return ref;
  if (ref[0] == '~' && ref.size() > 1 && ref[1] == '/') {
    const char *home = std::getenv("HOME");
    if (home) return std::string(home) + ref.substr(1);
  }
  return dir + "/" + ref;
}

std::size_t edit_distance(const std::string &a, const std::string &b) {
  std::vector<std::size_t> row(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); j++) row[j] = j;
  for (std::size_t i = 1; i <= a.size(); i++) {
    std::size_t prev = row[0];
    row[0] = i;
    for (std::size_t j = 1; j <= b.size(); j++) {
      std::size_t cur = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] == b[j - 1] ? 0 : 1)});
      prev = cur;
    }
  }
  return row[b.size()];
}

std::string nearest(const std::string &word, const std::vector<std::string> &known) {
  std::string best;
  const std::size_t bd = 3;
  std::size_t best_d = 1000;
  std::string w = lower(word), ws = w;
  std::sort(ws.begin(), ws.end());
  for (const auto &k : known) {
    std::string kl = lower(k), ks = kl;
    std::sort(ks.begin(), ks.end());
    std::size_t d = edit_distance(w, kl) * 2;
    if (d > 0 && ks == ws) d--; // the same letters swapped round is the likelier slip
    if (d < bd * 2 && d < best_d) { best_d = d; best = k; }
  }
  return best;
}

const std::vector<std::string> DECK_KEYS = {"title", "theme", "palette", "footer", "slides", "author", "display"};
const std::vector<std::string> SLIDE_KEYS = {"title", "subtitle", "notes", "layout", "blocks"};
const std::vector<std::string> PLACE_KEYS = {"at", "weight"};
const std::vector<std::string> CHART_KEYS = {"data"};
const std::vector<std::string> TEXT_KEYS = {"text", "bullets", "title", "size", "align", "valign", "color", "colour", "box"};
const std::vector<std::string> STAT_KEYS = {"stat", "label", "delta", "color", "colour", "title"};
const std::vector<std::string> LAYOUTS = {"auto", "cols", "rows", "grid"};

bool private_key(const std::string &k) {
  return k.empty() || k[0] == '_' || k[0] == '$' || k == "comment" || k == "comments";
}

bool in(const std::vector<std::string> &v, const std::string &k) {
  return std::find(v.begin(), v.end(), k) != v.end();
}

struct Parser {
  Deck &d;
  const LoadOpts &lo;

  void err(const std::string &path, const std::string &msg) { d.issues.push_back({true, path, msg}); }
  void warn(const std::string &path, const std::string &msg) { d.issues.push_back({false, path, msg}); }

  void unknown_key(const std::string &path, const std::string &key, std::vector<std::string> known, bool chart) {
    if (private_key(key)) return;
    if (chart) {
      static const char *spec_keys[] = {"type",   "palette", "frame",  "title",  "subtitle", "xlabel",
                                        "ylabel", "legend",  "values", "grid",   "shadow",   "explode",
                                        "depth",  "bins",    "prec",   "min",    "max",      "xy",
                                        "transpose", "no_header", "labels_col", "series_col", "label_key",
                                        "delim",  "annotations", "colors", "errors"};
      for (const char *k : spec_keys) known.push_back(k);
    }
    std::string hint = nearest(key, known);
    warn(path + "." + key, "unknown key, ignored" + (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
  }

  void load_data(Block &b, const Json *data) {
    b.error.clear();
    try {
      if (!data) throw std::runtime_error("a chart block needs \"data\": a file path or inline data");
      if (data->is_str()) {
        b.data_ref = data->s;
        b.data_file = resolve(d.dir, data->s);
        b.mtime = file_mtime(b.data_file);
        if (is_dir(b.data_file)) throw std::runtime_error(b.data_ref + " is a directory, not a data file");
        LoadOpts opts = lo;
        apply_spec_to_load(b.spec, opts);
        b.ds = load_path(b.data_file, opts);
      } else if (data->is_obj() || data->is_arr()) {
        LoadOpts opts = lo;
        apply_spec_to_load(b.spec, opts);
        b.ds = from_json_value(*data, opts);
        b.ds.source = "";
      } else {
        throw std::runtime_error("\"data\" must be a file path or inline JSON data");
      }
    } catch (const std::exception &e) {
      b.error = e.what();
      b.ds = Dataset();
      err(b.path + ".data", b.error);
    }
  }

  void check_colors(const Block &b, const std::string &where) {
    for (const auto &c : b.spec.series_colors) {
      if (c.first.empty()) continue;
      std::vector<std::string> names = b.ds.labels;
      bool found = false;
      for (const auto &s : b.ds.series) names.push_back(s.name);
      for (const auto &n : names)
        if (ieq(trim(n), trim(c.first))) found = true;
      if (found) continue;
      std::string hint = nearest(c.first, names);
      warn(where + ".colors." + c.first, "no series or slice called \"" + c.first + "\"" +
                                           (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
    }
  }

  void check_notes(const Block &b, const ChartSpec &spec, const std::string &where) {
    for (std::size_t i = 0; i < spec.notes.size(); i++) {
      const Annotation &a = spec.notes[i];
      std::string p = where + ".annotations[" + std::to_string(i) + "]";
      if (a.kind == Annotation::POINT || a.kind == Annotation::VLINE) {
        bool found = a.index >= 0 && a.index < static_cast<int>(b.ds.nrows());
        for (const auto &l : b.ds.labels)
          if (ieq(trim(l), trim(a.label))) found = true;
        if (!found) {
          std::string hint = nearest(a.label, b.ds.labels);
          warn(p, "no category \"" + a.label + "\" in the data, so this is not drawn" +
                      (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
        }
      }
      if (a.kind == Annotation::POINT && !a.series.empty()) {
        bool found = false;
        std::vector<std::string> names;
        for (const auto &s : b.ds.series) {
          names.push_back(s.name);
          if (ieq(trim(s.name), trim(a.series))) found = true;
        }
        if (!found) {
          std::string hint = nearest(a.series, names);
          warn(p, "no series \"" + a.series + "\"; the first series is used" +
                      (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
        }
      }
    }
  }

  // Error bars name columns of the data; a name that is not there draws nothing.
  void check_errors(const Block &b, const std::string &where) {
    const ChartSpec &spec = b.spec.has_errors ? b.spec : b.ds.spec;
    std::vector<std::string> names;
    for (const auto &s : b.ds.series) names.push_back(s.name);
    auto need = [&](const std::string &name, const std::string &role) {
      if (name.empty()) return;
      for (const auto &n : names)
        if (ieq(trim(n), trim(name))) return;
      std::string hint = nearest(name, names);
      warn(where + ".errors", "no column \"" + name + "\" for the " + role + ", so these error bars are not drawn" +
                                  (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
    };
    for (const auto &e : spec.errors) {
      need(e.series, "series");
      need(e.lo, e.plus_minus ? "+- amount" : "low end");
      if (!e.plus_minus) need(e.hi, "high end");
    }
  }

  Block block(const Json &j, const std::string &path, bool title_taken) {
    Block b;
    b.path = path;
    if (!j.is_obj()) {
      err(path, "a block must be an object");
      b.kind = Block::TEXT;
      return b;
    }
    const Json *rows = j.get("rows"), *cols = j.get("cols");
    // "rows" is also how inline data spells its records, so a container is
    // only a container when there is no "data" beside it.
    const bool has_data = j.get("data") != nullptr;
    if (has_data) b.kind = Block::CHART;
    else if (j.get("text") || j.get("bullets")) b.kind = Block::TEXT;
    else if (j.get("stat")) b.kind = Block::STAT;
    else if (rows && rows->is_arr()) b.kind = Block::ROWS;
    else if (cols && cols->is_arr()) b.kind = Block::COLS;
    else {
      err(path, "a block needs one of: \"data\" (chart), \"text\", \"bullets\", \"stat\", \"rows\", \"cols\"");
      b.kind = Block::TEXT;
      return b;
    }

    for (const auto &kv : j.o) {
      const std::string &k = kv.first;
      const Json &v = kv.second;
      const std::string kp = path + "." + k;
      if (k == "at") {
        bool good = v.is_arr() && v.a.size() == 4;
        if (good)
          for (const auto &e : v.a)
            if (!e.is_num()) good = false;
        if (!good) { err(kp, "wants [x, y, w, h] on a 12 x 12 grid, e.g. [0, 0, 8, 12]"); continue; }
        for (int i = 0; i < 4; i++) b.at[i] = v.a[static_cast<std::size_t>(i)].n;
        if (b.at[0] < 0 || b.at[1] < 0 || b.at[2] <= 0 || b.at[3] <= 0 || b.at[0] + b.at[2] > 12.001 ||
            b.at[1] + b.at[3] > 12.001) {
          err(kp, "falls outside the 12 x 12 grid");
          continue;
        }
        b.has_at = true;
      } else if (k == "weight") {
        if (!v.is_num() || v.n <= 0) err(kp, "wants a positive number");
        else b.weight = v.n;
      } else if (b.kind == Block::CHART) {
        if (k == "data") continue;
        if (k == "title" && title_taken) continue;
        if (!is_spec_key(k)) { unknown_key(path, k, CHART_KEYS, true); continue; }
        Json one = Json::object();
        one.set(k, v);
        try {
          spec_from_json(one, b.spec);
        } catch (const std::exception &e) {
          std::string m = e.what();
          if (starts_with(m, "chart spec: ")) m = m.substr(12);
          if (k == "type") {
            std::string hint = nearest(v.str_or(""), type_names());
            if (!hint.empty()) m += " -- did you mean \"" + hint + "\"?";
          }
          err(kp, m);
        }
      } else if (b.kind == Block::TEXT) {
        if (k == "text") {
          if (v.is_str()) b.lines = split(v.s, '\n');
          else if (v.is_arr()) {
            for (const auto &e : v.a) b.lines.push_back(e.is_str() ? e.s : (e.is_num() ? fmt_val(e.n) : ""));
          } else err(kp, "wants a string or an array of lines");
        } else if (k == "bullets") {
          if (!v.is_arr()) { err(kp, "wants an array of strings"); continue; }
          for (const auto &e : v.a) b.lines.push_back("- " + e.str_or(""));
        } else if (k == "title") { if (!title_taken) b.title = v.str_or(""); }
        else if (k == "size") {
          if (!v.is_num() || v.n < 1 || v.n > 4) err(kp, "wants 1, 2, 3 or 4");
          else b.size = static_cast<int>(v.n);
        } else if (k == "align") {
          std::string a = lower(v.str_or(""));
          if (a == "left") b.align = -1;
          else if (a == "center" || a == "centre") b.align = 0;
          else if (a == "right") b.align = 1;
          else err(kp, "wants \"left\", \"center\" or \"right\"");
        } else if (k == "valign") {
          std::string a = lower(v.str_or(""));
          if (a == "middle" || a == "center" || a == "centre") b.middle = true;
          else if (a != "top") err(kp, "wants \"top\" or \"middle\"");
        } else if (k == "color" || k == "colour") {
          if (!parse_color(v, b.color)) err(kp, "unknown color; use a name like \"yellow\" or 0..15");
        } else if (k == "box") b.box = v.is_bool() ? v.b : true;
        else unknown_key(path, k, TEXT_KEYS, false);
      } else if (b.kind == Block::STAT) {
        if (k == "stat") b.value = v.is_num() ? fmt_val(v.n) : v.str_or("");
        else if (k == "label") b.label = v.str_or("");
        else if (k == "title") { if (!title_taken) b.title = v.str_or(""); }
        else if (k == "delta") b.delta = v.is_num() ? fmt_val(v.n) : v.str_or("");
        else if (k == "color" || k == "colour") {
          if (!parse_color(v, b.color)) err(kp, "unknown color; use a name like \"yellow\" or 0..15");
        } else unknown_key(path, k, STAT_KEYS, false);
      } else if (k != "rows" && k != "cols") {
        unknown_key(path, k, {"rows", "cols"}, false);
      }
    }

    if (b.kind == Block::CHART) {
      load_data(b, j.get("data"));
      if (b.error.empty()) {
        if (!b.ds.hint.empty()) warn(path + ".data", b.ds.hint);
        check_notes(b, b.spec, path);
        check_colors(b, path);
        check_errors(b, path);
        if (b.spec.has_series_col || b.spec.has_label_col) { /* shaped on load */ }
      }
    } else if (b.kind == Block::ROWS || b.kind == Block::COLS) {
      const char *key = b.kind == Block::ROWS ? "rows" : "cols";
      const Json &arr = *j.get(key);
      if (arr.a.empty()) err(path + "." + key, "is empty");
      for (std::size_t i = 0; i < arr.a.size(); i++)
        b.kids.push_back(block(arr.a[i], path + "." + key + "[" + std::to_string(i) + "]", false));
    }
    return b;
  }

  Slide slide(const Json &j, const std::string &path) {
    Slide s;
    s.path = path;
    if (!j.is_obj()) { err(path, "a slide must be an object"); return s; }
    // A slide may be its one block: {"title":..., "type":"bar", "data":...}
    const bool shorthand = !j.get("blocks") && (j.get("data") || j.get("text") || j.get("bullets") || j.get("stat"));
    for (const auto &kv : j.o) {
      const std::string &k = kv.first;
      const Json &v = kv.second;
      if (k == "title") s.title = v.str_or("");
      else if (k == "subtitle") s.subtitle = v.str_or("");
      else if (k == "notes") {
        if (v.is_arr()) {
          std::vector<std::string> l;
          for (const auto &e : v.a) l.push_back(e.str_or(""));
          s.notes = join(l, "\n");
        } else s.notes = v.str_or("");
      } else if (k == "layout") {
        s.layout = lower(v.str_or(""));
        if (s.layout == "columns" || s.layout == "row") s.layout = "cols";
        if (s.layout == "column") s.layout = "rows";
        if (!in(LAYOUTS, s.layout)) {
          err(path + ".layout", "wants one of: auto, cols, rows, grid");
          s.layout = "auto";
        }
      } else if (k == "blocks") {
        if (!v.is_arr()) { err(path + ".blocks", "wants an array"); continue; }
        for (std::size_t i = 0; i < v.a.size(); i++)
          s.blocks.push_back(block(v.a[i], path + ".blocks[" + std::to_string(i) + "]", false));
      } else if (!shorthand) {
        unknown_key(path, k, SLIDE_KEYS, false);
      }
    }
    if (shorthand) {
      Block b = block(j, path, true);
      // the slide already shows these
      b.spec.has_subtitle = false;
      // drop the warnings block() raised about the slide's own keys
      d.issues.erase(std::remove_if(d.issues.begin(), d.issues.end(),
                                    [&](const Issue &i) {
                                      return !i.error && (i.path == path + ".notes" || i.path == path + ".layout" ||
                                                          i.path == path + ".subtitle");
                                    }),
                     d.issues.end());
      s.blocks.push_back(b);
    }
    return s;
  }

  void deck() {
    const Json &j = d.root;
    for (const auto &kv : j.o) {
      const std::string &k = kv.first;
      const Json &v = kv.second;
      if (k == "title") d.title = v.str_or("");
      else if (k == "footer") d.footer = v.str_or("");
      else if (k == "author") { /* free metadata */ }
      else if (k == "theme" && v.is_obj()) {
        // a custom theme: start from "base" and recolour any part of it
        std::string base = "dos";
        if (const Json *b = v.get("base")) base = lower(b->str_or("dos"));
        if (!in(theme_names(), base)) { err("theme.base", "unknown theme \"" + base + "\"; use one of: " + join(theme_names(), ", ")); base = "dos"; }
        Skin keep = S;
        set_theme(base);
        d.skin = S;
        S = keep;
        d.theme = base;
        d.skin.name = "custom";
        for (const auto &tv : v.o) {
          if (tv.first == "base" || private_key(tv.first)) continue;
          int c = -1;
          bool none = tv.first == "slide_bg" && tv.second.is_str() && lower(tv.second.s) == "none";
          if (!none && !parse_color(tv.second, c)) { err("theme." + tv.first, "is not a colour (use a name like \"dark cyan\" or 0..15)"); continue; }
          if (!skin_set(d.skin, tv.first, none ? static_cast<uint8_t>(BG_NONE) : static_cast<uint8_t>(c))) {
            std::string hint = nearest(tv.first, skin_keys());
            warn("theme." + tv.first, "unknown theme colour, ignored" + (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
          }
        }
      } else if (k == "theme") {
        d.theme = lower(v.str_or(""));
        if (!in(theme_names(), d.theme)) {
          err("theme", "unknown theme \"" + d.theme + "\"; use one of: " + join(theme_names(), ", ") + ", or an object of colours");
          d.theme.clear();
        } else {
          Skin keep = S;
          set_theme(d.theme);
          d.skin = S;
          S = keep;
        }
      } else if (k == "display") {
        if (!v.is_obj()) { err("display", "wants an object like {\"scale\": 2, \"gfx\": \"auto\"}"); continue; }
        for (const auto &dv : v.o) {
          const std::string &dk = dv.first;
          const Json &x = dv.second;
          if (dk == "gfx") {
            d.gfx = lower(x.str_or(""));
            static const std::vector<std::string> modes = {"auto", "fb", "kitty", "sixel", "cells", "ascii"};
            if (!in(modes, d.gfx)) { err("display.gfx", "wants one of: " + join(modes, ", ")); d.gfx.clear(); }
          } else if (dk == "scale") {
            if (!x.is_num() || x.n < 1 || x.n > 8) err("display.scale", "wants 1..8");
            else d.scale = static_cast<int>(x.n);
          } else if (dk == "size") {
            if (!x.is_arr() || x.a.size() != 2 || !x.a[0].is_num() || !x.a[1].is_num() || x.a[0].n < 20 || x.a[1].n < 6)
              err("display.size", "wants [columns, rows], at least [20, 6]");
            else { d.cols = static_cast<int>(x.a[0].n); d.rows = static_cast<int>(x.a[1].n); }
          } else if (dk == "ascii") d.ascii = x.is_bool() ? x.b : 1;
          else if (dk == "color" || dk == "colour") d.color = x.is_bool() ? x.b : 1;
          else if (!private_key(dk)) warn("display." + dk, "unknown key, ignored (gfx, scale, size, ascii, color)");
        }
      } else if (k == "palette" && v.is_arr()) {
        try { d.palette = palette_from_json(v); } catch (const std::exception &e) { err("palette", e.what()); }
      } else if (k == "palette") {
        d.palette = v.str_or("");
        bool good = false;
        for (const auto &p : palette_names())
          if (ieq(p, d.palette)) good = true;
        if (!good) {
          err("palette", "unknown palette \"" + d.palette + "\"; use one of: " + join(palette_names(), ", "));
          d.palette.clear();
        }
      } else if (k == "slides") {
        if (!v.is_arr()) { err("slides", "wants an array of slides"); continue; }
        if (v.a.empty()) err("slides", "is empty: a deck needs at least one slide");
        for (std::size_t i = 0; i < v.a.size(); i++)
          d.slides.push_back(slide(v.a[i], "slides[" + std::to_string(i) + "]"));
      } else {
        if (private_key(k)) continue;
        std::string hint = nearest(k, DECK_KEYS);
        warn(k, "unknown key, ignored" + (hint.empty() ? "" : " (did you mean \"" + hint + "\"?)"));
      }
    }
  }
};

void collect(std::vector<Block> &blocks, std::vector<Block *> &out) {
  for (auto &b : blocks) {
    if (b.kind == Block::CHART) out.push_back(&b);
    else collect(b.kids, out);
  }
}

} // namespace

bool Deck::ok() const {
  for (const auto &i : issues)
    if (i.error) return false;
  return true;
}

// "slides[2].blocks[0].cols[1]" -> the node
Json *Deck::node(const std::string &path) {
  Json *cur = &root;
  std::size_t i = 0;
  while (i < path.size() && cur) {
    if (path[i] == '.') { i++; continue; }
    if (path[i] == '[') {
      std::size_t e = path.find(']', i);
      if (e == std::string::npos) return nullptr;
      std::size_t idx = static_cast<std::size_t>(std::atoi(path.substr(i + 1, e - i - 1).c_str()));
      if (!cur->is_arr() || idx >= cur->a.size()) return nullptr;
      cur = &cur->a[idx];
      i = e + 1;
      continue;
    }
    std::size_t e = path.find_first_of(".[", i);
    if (e == std::string::npos) e = path.size();
    cur = cur->find(path.substr(i, e - i));
    i = e;
  }
  return cur;
}

void use_deck_theme(const Deck &d, const std::string &override_name) {
  if (!override_name.empty()) set_theme(override_name);
  else if (!d.theme.empty()) S = d.skin;
}

bool is_deck_json(const Json &j) { return j.is_obj() && j.get("slides") != nullptr; }

bool is_deck_file(const std::string &path) {
  if (path == "-" || is_dir(path)) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  // A deck is JSON with "slides" near the top; do not parse a gigabyte of CSV
  // to find out.
  std::string head(65536, '\0');
  f.read(&head[0], static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(f.gcount()));
  std::string t = trim(head);
  if (t.empty() || t[0] != '{') return false;
  return t.find("\"slides\"") != std::string::npos;
}

Deck load_deck(const std::string &path, const LoadOpts &lo) {
  Deck d;
  d.file = path;
  d.dir = dir_of(path);
  d.mtime = file_mtime(path);
  try {
    d.root = parse_json(read_file(path));
  } catch (const std::exception &e) {
    throw std::runtime_error(path + ": " + e.what());
  }
  if (!is_deck_json(d.root)) throw std::runtime_error(path + ": not a deck (no \"slides\" array)");
  Parser p{d, lo};
  p.deck();
  return d;
}

Deck deck_from_files(const FileSlides &fs, const LoadOpts &lo) {
  Deck d;
  d.implicit = true;
  d.root = Json::object();
  Json slides = Json::array();
  Json tiled = Json::array();
  const std::size_t n = std::max(fs.files.size(), fs.tile ? fs.types.size() : std::size_t(0));
  Json piped; // stdin can only be read once
  for (std::size_t i = 0; i < n; i++) {
    const std::string &f = fs.files[i % fs.files.size()];
    Json b = Json::object();
    if (!fs.types.empty()) b.set("type", Json::string(fs.types[i % fs.types.size()]));
    if (f == "-") {
      if (piped.is_null()) {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        try {
          Dataset ds = load_text(ss.str(), "<stdin>", lo);
          piped = dataset_to_json(ds);
          // carry what the piped file said about itself
          std::string t = trim(ss.str());
          if (!t.empty() && t[0] == '{') {
            Json orig = parse_json(ss.str());
            if (const Json *c = orig.get("chart")) piped.set("chart", *c);
            if (const Json *tt = orig.get("title")) piped.set("title", *tt);
          } else if (ds.spec.any()) {
            Json c = Json::object();
            if (ds.spec.has_type) c.set("type", Json::string(ds.spec.type));
            if (ds.spec.has_title) c.set("title", Json::string(ds.spec.title));
            if (ds.spec.has_palette) c.set("palette", Json::string(ds.spec.palette));
            if (ds.spec.has_values) c.set("values", Json::boolean(ds.spec.values));
            piped.set("chart", c);
          }
        } catch (const std::exception &e) {
          throw std::runtime_error(std::string("<stdin>: ") + e.what());
        }
      }
      b.set("data", piped);
    } else {
      b.set("data", Json::string(f));
    }
    if (fs.tile) tiled.a.push_back(b);
    else slides.a.push_back(b);
  }
  if (fs.tile) {
    Json s = Json::object();
    s.set("layout", Json::string("grid"));
    s.set("blocks", tiled);
    slides.a.push_back(s);
  }
  d.root.set("slides", slides);
  Parser p{d, lo};
  p.deck();
  return d;
}

namespace {
void collect_leaves(std::vector<Block> &blocks, std::vector<Block *> &out) {
  for (auto &b : blocks) {
    if (b.kind == Block::ROWS || b.kind == Block::COLS) collect_leaves(b.kids, out);
    else out.push_back(&b);
  }
}
} // namespace

std::vector<Block *> leaf_blocks(Slide &s) {
  std::vector<Block *> out;
  collect_leaves(s.blocks, out);
  return out;
}

std::vector<Block *> chart_blocks(Slide &s) {
  std::vector<Block *> out;
  collect(s.blocks, out);
  return out;
}

bool refresh_data(Deck &d, const LoadOpts &lo) {
  bool changed = false;
  for (auto &s : d.slides)
    for (Block *b : chart_blocks(s)) {
      if (b->data_file.empty() || b->data_dirty) continue;
      long long m = file_mtime(b->data_file);
      if (m == b->mtime) continue;
      b->mtime = m;
      try {
        LoadOpts opts = lo;
        apply_spec_to_load(b->spec, opts);
        b->ds = load_path(b->data_file, opts);
        b->error.clear();
      } catch (const std::exception &e) {
        b->error = e.what();
        b->ds = Dataset();
      }
      changed = true;
    }
  return changed;
}

// ---- writing -----------------------------------------------------------------

void store_block_spec(Deck &d, Block &b, bool content) {
  Json *n = d.node(b.path);
  if (!n || !n->is_obj()) return;
  const ChartSpec &s = b.spec;
  if (s.has_type) n->set("type", Json::string(s.type));
  if (s.has_palette) n->set("palette", Json::string(s.palette));
  if (s.has_values) n->set("values", Json::boolean(s.values));
  if (s.has_grid) n->set("grid", Json::boolean(s.grid));
  if (s.has_legend) n->set("legend", Json::boolean(s.legend));
  if (s.has_depth) n->set("depth", Json::number(s.depth));
  if (s.has_explode) n->set("explode", s.explode == -2 ? Json::boolean(false) : Json::number(s.explode));
  if (s.has_notes) {
    if (s.notes.empty()) n->erase("annotations");
    else {
      Json arr = Json::array();
      for (const auto &a : s.notes) arr.a.push_back(annotation_to_json(a));
      n->set("annotations", arr);
    }
  }
  (content ? d.dirty : d.tweaked) = true;
}

void store_block_text(Deck &d, Block &b) {
  Json *n = d.node(b.path);
  if (!n || !n->is_obj()) return;
  if (b.kind == Block::TEXT) {
    Json arr = Json::array();
    for (const auto &l : b.lines) arr.a.push_back(Json::string(l));
    n->erase("bullets");
    n->set("text", arr);
  } else if (b.kind == Block::STAT) {
    n->set("stat", Json::string(b.value));
    if (!b.label.empty()) n->set("label", Json::string(b.label));
  }
  d.dirty = true;
}

void reparse(Deck &d, const LoadOpts &lo) {
  d.slides.clear();
  d.issues.clear();
  Parser p{d, lo};
  p.deck();
}

void store_block_data(Deck &d, Block &b) {
  if (!b.data_file.empty()) {
    save_dataset(b.ds, b.data_file);
    b.mtime = file_mtime(b.data_file);
  } else {
    Json *n = d.node(b.path);
    if (!n || !n->is_obj()) throw std::runtime_error("the block is gone from the deck");
    // Inline data read through series_col is a part of what the deck holds;
    // writing it back would throw the rest away.
    if (b.ds.lossy) throw std::runtime_error("not saving the inline data: " + b.ds.lossy_why);
    Json fresh = dataset_to_json(b.ds);
    if (const Json *old = n->get("data"))
      if (old->is_obj())
        if (const Json *c = old->get("chart")) fresh.set("chart", *c);
    n->set("data", fresh);
    d.dirty = true;
  }
  b.data_dirty = false;
}

std::string save_deck(Deck &d) {
  if (d.file.empty()) {
    // An implicit deck gets a name beside its first data file.
    std::string stem = "charts";
    for (auto &s : d.slides)
      for (Block *b : chart_blocks(s))
        if (!b->data_file.empty() && stem == "charts") {
          stem = b->data_file;
          std::size_t dot = stem.find_last_of('.');
          if (dot != std::string::npos && dot > stem.find_last_of('/') + 1) stem = stem.substr(0, dot);
        }
    d.file = stem + ".deck.json";
    d.dir = dir_of(d.file);
    // Data paths were relative to the working directory; make them relative
    // to the deck now that it has a home.
    if (!d.dir.empty())
      for (auto &s : d.slides)
        for (Block *b : chart_blocks(s)) {
          Json *n = d.node(b->path);
          if (!n || b->data_ref.empty()) continue;
          if (starts_with(b->data_ref, d.dir + "/")) {
            b->data_ref = b->data_ref.substr(d.dir.size() + 1);
            n->set("data", Json::string(b->data_ref));
          }
        }
    d.implicit = false;
  }
  std::string tmp = d.file + ".charts-tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write: " + d.file);
    f << json_write(d.root);
    if (!f.good()) throw std::runtime_error("write failed: " + d.file);
  }
  if (std::rename(tmp.c_str(), d.file.c_str()) != 0) {
    std::remove(tmp.c_str());
    throw std::runtime_error("cannot replace: " + d.file);
  }
  d.dirty = d.tweaked = false;
  d.mtime = file_mtime(d.file);
  return d.file;
}

// ---- reporting ---------------------------------------------------------------

namespace {

void outline_blocks(std::ostringstream &o, const std::vector<Block> &blocks, int depth) {
  const std::string pad(static_cast<std::size_t>(depth) * 2 + 4, ' ');
  for (const auto &b : blocks) {
    switch (b.kind) {
    case Block::CHART: {
      std::string type = b.spec.has_type ? b.spec.type : (b.ds.spec.has_type ? b.ds.spec.type : "bar");
      o << pad << "chart " << type;
      if (!b.data_ref.empty()) o << "  " << b.data_ref;
      else o << "  (inline data)";
      if (!b.error.empty()) o << "  ERROR: " << b.error;
      else o << "  " << b.ds.nrows() << " rows x " << b.ds.series.size() << " series";
      std::size_t notes = (b.spec.has_notes ? b.spec.notes : b.ds.spec.notes).size();
      if (notes) o << ", " << notes << " annotation" << (notes == 1 ? "" : "s");
      if (b.spec.has_errors || b.ds.spec.has_errors) o << ", error bars";
      o << "\n";
      break;
    }
    case Block::TEXT:
      o << pad << "text  " << b.lines.size() << " line" << (b.lines.size() == 1 ? "" : "s");
      if (!b.lines.empty()) o << ": " << trunc_to(b.lines[0], 50);
      o << "\n";
      break;
    case Block::STAT: o << pad << "stat  " << b.value << "  " << b.label << "\n"; break;
    case Block::ROWS:
    case Block::COLS:
      o << pad << (b.kind == Block::ROWS ? "rows" : "cols") << "\n";
      outline_blocks(o, b.kids, depth + 1);
      break;
    }
  }
}

std::string json_escape(const std::string &s) {
  std::string out = json_write(Json::string(s));
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

} // namespace

std::string deck_outline(const Deck &d) {
  std::ostringstream o;
  o << "deck:    " << (d.file.empty() ? "(from the command line)" : d.file) << "\n";
  if (!d.title.empty()) o << "title:   " << d.title << "\n";
  o << "theme:   " << (d.theme.empty() ? "dos" : d.theme) << "\n";
  o << "slides:  " << d.slides.size() << "\n";
  for (std::size_t i = 0; i < d.slides.size(); i++) {
    const Slide &s = d.slides[i];
    o << "  " << (i + 1) << ". " << (s.title.empty() ? "(untitled)" : s.title);
    if (s.layout != "auto") o << "  [" << s.layout << "]";
    if (!s.notes.empty()) o << "  (notes)";
    o << "\n";
    outline_blocks(o, s.blocks, 0);
  }
  return o.str();
}

std::string issues_text(const Deck &d) {
  std::ostringstream o;
  for (const auto &i : d.issues) o << (i.error ? "error    " : "warning  ") << i.path << ": " << i.msg << "\n";
  return o.str();
}

std::string issues_json(const Deck &d) {
  std::size_t errors = 0, warnings = 0;
  for (const auto &i : d.issues) (i.error ? errors : warnings)++;
  std::ostringstream o;
  o << "{\"ok\": " << (errors == 0 ? "true" : "false") << ", \"slides\": " << d.slides.size()
    << ", \"errors\": " << errors << ", \"warnings\": " << warnings << ", \"issues\": [";
  for (std::size_t k = 0; k < d.issues.size(); k++) {
    const Issue &i = d.issues[k];
    o << (k ? ", " : "") << "\n  {\"level\": \"" << (i.error ? "error" : "warning") << "\", \"path\": "
      << json_escape(i.path) << ", \"message\": " << json_escape(i.msg) << "}";
  }
  o << (d.issues.empty() ? "" : "\n") << "]}\n";
  return o.str();
}

} // namespace ch
