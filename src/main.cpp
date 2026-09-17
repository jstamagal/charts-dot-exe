// main.cpp -- charts: one chart, or a screenful, straight to the tty.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "canvas.hpp"
#include "chart.hpp"
#include "cli.hpp"
#include "data.hpp"
#include "sources.hpp"
#include "term.hpp"
#include "ttyguard.hpp"
#include "tui.hpp"
#include "util.hpp"

namespace ch {
namespace {

std::vector<Rect> grid_rects(int x, int y, int w, int h, int n) {
  std::vector<Rect> out;
  if (n <= 1) {
    out.push_back(Rect{x, y, w, h});
    return out;
  }
  int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
  if (cols < 1) cols = 1;
  int rows = (n + cols - 1) / cols;
  for (int i = 0; i < n; i++) {
    int c = i % cols, r = i / cols;
    int x0 = x + static_cast<int>((long long)c * w / cols);
    int x1 = x + static_cast<int>((long long)(c + 1) * w / cols);
    int y0 = y + static_cast<int>((long long)r * h / rows);
    int y1 = y + static_cast<int>((long long)(r + 1) * h / rows);
    out.push_back(Rect{x0, y0, x1 - x0 - 1, y1 - y0 - 1});
  }
  return out;
}

RenderOpts make_opts(const Args &a, const std::string &type, bool tiled) {
  RenderOpts o;
  o.type = type;
  o.palette = a.palette;
  o.title = a.title;
  o.subtitle = a.subtitle;
  o.xlabel = a.xlabel;
  o.ylabel = a.ylabel;
  o.frame = a.frame.empty() ? (tiled ? "single" : "double") : a.frame;
  o.legend = a.legend;
  o.values = a.values;
  o.grid = a.grid;
  o.color = a.color;
  o.ascii = a.ascii;
  o.shadow = a.shadow && !tiled;
  o.xy = a.xy;
  o.explode = a.explode;
  o.depth = a.depth;
  o.bins = a.bins;
  o.prec = a.prec;
  o.lo = a.lo;
  o.hi = a.hi;
  o.has_lo = a.has_lo;
  o.has_hi = a.has_hi;
  return o;
}

LoadOpts load_opts(const Args &a) {
  LoadOpts lo;
  lo.delim = a.delim;
  lo.transpose = a.transpose;
  lo.xy = a.xy;
  lo.no_header = a.no_header;
  lo.label_col = a.label_col;
  lo.series_col = a.series_col;
  lo.label_key = a.label_key;
  return lo;
}

void emit(const Args &a, const std::string &text) {
  if (a.out.empty()) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
    return;
  }
  std::ofstream f(a.out, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write: " + a.out);
  f << text;
}

std::string normalise_type(std::string t) {
  t = lower(t);
  if (t == "column") return "bar";
  if (t == "xy") return "scatter";
  return t;
}

} // namespace
} // namespace ch

int main(int argc, char **argv) {
  using namespace ch;
  try {
    Args a = parse_args(argc, argv);
    if (a.help) { print_help(stdout); return 0; }
    if (a.version) { print_version(stdout); return 0; }
    if (a.list_types) { print_types(stdout); return 0; }
    if (a.list_palettes) { print_palettes(stdout); return 0; }
    if (!a.example.empty()) { print_example(stdout, a.example); return 0; }

    if (a.ascii) use_ascii_glyphs(); else use_unicode_glyphs();
    if (a.interactive) return run_tui(a);

    std::vector<std::string> files = expand_sources(a.files);
    if (files.empty()) {
      if (!is_tty(STDIN_FILENO)) files.push_back("-");
      else {
        std::fprintf(stderr, "charts: no input. try 'charts -h' or 'charts -i examples/'\n");
        return 2;
      }
    }

    std::vector<std::string> types;
    for (const auto &t : a.types) types.push_back(normalise_type(t));
    if (types.empty()) types.push_back("bar");

    for (const auto &t : types)
      if (!type_valid(t)) throw std::runtime_error("unknown chart type: " + t);

    LoadOpts lo = load_opts(a);

    if (a.describe) {
      for (const auto &f : files) {
        Dataset ds = load_path(f, lo);
        std::string d = describe(ds);
        std::fwrite(d.data(), 1, d.size(), stdout);
        if (files.size() > 1) std::fputc('\n', stdout);
      }
      return 0;
    }

    std::vector<Dataset> sets;
    for (std::size_t i = 0; i < files.size(); i++) {
      if (files[i] == "-" && !sets.empty()) {
        sets.push_back(sets[0]); // stdin can only be read once
        continue;
      }
      sets.push_back(load_path(files[i], lo));
    }

    bool color = a.color;
    if (!a.color_forced && !is_tty(STDOUT_FILENO)) color = false;
    a.color = color;

    TermSize ts = term_size();
    int W = a.width > 0 ? a.width : ts.w;
    int H = a.height > 0 ? a.height : ts.h;
    if (a.width == 0 && !is_tty(STDOUT_FILENO) && ts.w <= 80) W = 100;
    if (a.height == 0 && !is_tty(STDOUT_FILENO) && ts.h <= 24) H = 32;

    int n = static_cast<int>(std::max(files.size(), types.size()));
    if (n < 1) n = 1;
    bool tiled = n > 1;

    auto draw = [&](Canvas &cv) {
      std::vector<Rect> rects = grid_rects(0, 0, W, H, n);
      for (int i = 0; i < n; i++) {
        std::size_t si = static_cast<std::size_t>(i) % sets.size();
        const std::string &ty = types[static_cast<std::size_t>(i) % types.size()];
        Dataset work = sets[si];
        RenderOpts o = make_opts(a, ty, tiled);
        render_chart(cv, rects[static_cast<std::size_t>(i)], work, o);
      }
    };

    // ---- one shot -----------------------------------------------------------
    if (!a.watch) {
      Canvas cv(W, H);
      cv.clear(7);
      draw(cv);
      emit(a, cv.dump(color, false));
      return 0;
    }

    // ---- watch: redraw when a source file changes ---------------------------
    if (!is_tty(STDOUT_FILENO)) {
      std::fprintf(stderr, "charts: --watch needs a terminal\n");
      return 2;
    }
    tty_guard_install();
    RawMode rm(STDIN_FILENO);
    cursor_show(false);
    screen_clear();

    std::vector<long long> seen(files.size(), -1);
    bool quit = false;
    while (!quit) {
      bool changed = false;
      for (std::size_t i = 0; i < files.size(); i++) {
        long long m = file_mtime(files[i]);
        if (m != seen[i]) { changed = true; seen[i] = m; }
      }
      if (changed) {
        for (std::size_t i = 0; i < files.size(); i++) {
          if (files[i] == "-" && i > 0) { sets[i] = sets[0]; continue; }
          try {
            sets[i] = load_path(files[i], lo);
          } catch (const std::exception &e) {
            std::fprintf(stderr, "\x1b[%d;1Hcharts: %s        ", H, e.what());
          }
        }
        Canvas cv(W, H);
        cv.clear(7);
        try {
          draw(cv);
          std::string out = cv.dump(color, true);
          cursor_home();
          ssize_t w = ::write(STDOUT_FILENO, out.data(), out.size());
          (void)w;
        } catch (const std::exception &e) {
          std::fprintf(stderr, "\x1b[%d;1Hcharts: %s        ", H, e.what());
        }
      }
      std::string k = read_key(200);
      if (k == "q" || k == "ctrl-c" || k == "ctrl-d" || k == "esc") quit = true;
      else if (k == "r") { for (auto &m : seen) m = -1; }
    }
    cursor_show(true);
    screen_clear();
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "charts: %s\n", e.what());
    return 1;
  }
}
