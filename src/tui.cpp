#include "tui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unistd.h>

#include "canvas.hpp"
#include "chart.hpp"
#include "data.hpp"
#include "sources.hpp"
#include "term.hpp"
#include "ttyguard.hpp"
#include "util.hpp"

namespace ch {

namespace {

const uint8_t BAR_FG = 15;
const uint8_t BAR_BG = 4; // classic blue DOS bar

void bar_row(Canvas &cv, int x, int y, int w, const std::string &s, uint8_t fg, uint8_t bg) {
  for (int i = 0; i < w; i++) cv.put(x + i, y, U' ', fg, bg);
  int cx = x;
  for (char32_t cp : utf8_decode(s)) {
    if (cx >= x + w) break;
    if (cx >= x) cv.put(cx, y, cp, fg, bg);
    cx++;
  }
}

std::string basename_of(const std::string &p) {
  std::size_t s = p.find_last_of('/');
  std::string n = (s == std::string::npos) ? p : p.substr(s + 1);
  return n.empty() ? p : n;
}

struct State {
  std::vector<std::string> files;
  std::vector<std::string> types;
  std::vector<std::string> pals;
  std::size_t fi = 0;
  int ti = 0, pi = 0;
  int mode = 0; // 0 chart, 1 table, 2 help
  int explode = -2, depth = 2;
  bool values = false, grid = true, legend = true;
  bool autoreload = true;
  int nsl = 0;
  Dataset ds;
  std::string error;
  long long mtime = -1;
  const Args *a = nullptr;
};

void reload(State &st, bool force) {
  const std::string &path = st.files[st.fi];
  long long m = file_mtime(path);
  if (!force && m == st.mtime) return;
  st.mtime = m;
  try {
    LoadOpts lo;
    lo.delim = st.a->delim;
    lo.transpose = st.a->transpose;
    lo.xy = st.a->xy;
    lo.no_header = st.a->no_header;
    lo.label_col = st.a->label_col;
    lo.series_col = st.a->series_col;
    lo.label_key = st.a->label_key;
    st.ds = load_path(path, lo);
    st.error.clear();
    st.nsl = st.ds.series.size() == 1 ? static_cast<int>(st.ds.nrows())
                                      : static_cast<int>(st.ds.series.size());
  } catch (const std::exception &e) {
    st.error = e.what();
    st.ds = Dataset();
    st.nsl = 0;
  }
}

RenderOpts opts_for(const State &st, const std::string &type) {
  const Args &a = *st.a;
  RenderOpts o;
  o.type = type;
  o.palette = st.pals[static_cast<std::size_t>(st.pi)];
  o.title = a.title;
  o.subtitle = a.subtitle;
  o.xlabel = a.xlabel;
  o.ylabel = a.ylabel;
  o.frame = a.frame.empty() ? "double" : a.frame;
  o.legend = st.legend;
  o.values = st.values;
  o.grid = st.grid;
  o.color = a.color;
  o.ascii = a.ascii;
  o.shadow = a.shadow;
  o.xy = a.xy;
  o.explode = st.explode;
  o.depth = st.depth;
  o.bins = a.bins;
  o.prec = a.prec;
  o.lo = a.lo;
  o.hi = a.hi;
  o.has_lo = a.has_lo;
  o.has_hi = a.has_hi;
  return o;
}

void draw_help(Canvas &cv, int W, int H) {
  static const char *lines[] = {
      "c / C      next / previous chart type   (arrows work too)",
      "1-9        jump straight to a chart type",
      "p / P      next / previous palette",
      "t          table view of the numbers",
      "e          explode: off -> biggest slice -> slice 1, 2, 3 ...",
      "v          value labels on the bars",
      "g          grid lines",
      "d / D      deeper / flatter 3-D extrusion",
      "n / N      next / previous file",
      "r          reload now      w  auto-reload on/off",
      "?  / esc   close this box   q  quit",
  };
  int n = static_cast<int>(sizeof lines / sizeof lines[0]);
  int bw = 62;
  if (bw > W - 2) bw = W - 2;
  int bh = n + 4;
  if (bh > H - 2) bh = H - 2;
  int x = (W - bw) / 2, y = (H - bh) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  cv.shadow(x, y, bw, bh, S.shadow, G.blk[1]);
  cv.box(x, y, bw, bh, BOX_DOUBLE, S.frame);
  cv.text_c(x + 2, y, bw - 4, " keys ", S.title);
  int row = y + 2;
  for (int i = 0; i < n && row < y + bh - 1; i++, row++)
    cv.text(x + 3, row, trunc_to(lines[i], static_cast<std::size_t>(bw - 5)), S.label);
}

} // namespace

int run_tui(const Args &a) {
  State st;
  st.a = &a;
  st.files = expand_sources(a.files);
  if (st.files.empty()) {
    std::fprintf(stderr, "charts: -i needs a data file or a directory of them\n");
    return 2;
  }
  st.types = type_names();
  st.pals = palette_names();
  if (!a.types.empty()) {
    std::string want = a.types[0];
    if (want == "column") want = "bar";
    if (want == "xy") want = "scatter";
    for (std::size_t i = 0; i < st.types.size(); i++)
      if (st.types[i] == want) st.ti = static_cast<int>(i);
    for (std::size_t i = 0; i < a.types.size(); i++) {
      std::string t = a.types[i];
      if (t == "column") t = "bar";
      if (t == "xy") t = "scatter";
      std::vector<std::string>::iterator it = std::find(st.types.begin(), st.types.end(), t);
      if (it != st.types.end()) { st.ti = static_cast<int>(it - st.types.begin()); break; }
    }
  }
  for (std::size_t i = 0; i < st.pals.size(); i++)
    if (ieq(st.pals[i], a.palette)) st.pi = static_cast<int>(i);
  st.legend = a.legend;
  st.values = a.values;
  st.grid = a.grid;
  st.depth = a.depth;
  st.explode = a.explode;
  reload(st, true);

  tty_guard_install();
  RawMode rm(STDIN_FILENO);
  if (!rm.ok()) {
    std::fprintf(stderr, "charts: -i needs a terminal on stdin\n");
    return 2;
  }
  cursor_show(false);
  screen_clear();

  bool quit = false;
  bool dirty = true;
  TermSize last{0, 0};

  while (!quit) {
    TermSize ts = term_size();
    if (ts.w != last.w || ts.h != last.h) {
      last = ts;
      dirty = true;
      screen_clear();
    }
    if (st.autoreload) {
      long long m = file_mtime(st.files[st.fi]);
      if (m != st.mtime) {
        reload(st, true);
        dirty = true;
      }
    }

    if (dirty) {
      int W = a.width > 0 ? a.width : ts.w;
      int H = a.height > 0 ? a.height : ts.h;
      if (W < 20) W = 20;
      if (H < 6) H = 6;
      Canvas cv(W, H);
      cv.clear(7);

      // ---- title bar
      std::string left = " charts-dot-exe ";
      left += G.bullet;
      left += " " + basename_of(st.files[st.fi]);
      if (st.files.size() > 1)
        left += "  [" + std::to_string(st.fi + 1) + "/" + std::to_string(st.files.size()) + "]";
      if (st.autoreload) left += "  (auto)";
      std::string type = (st.mode == 1) ? "table" : st.types[static_cast<std::size_t>(st.ti)];
      std::string right = type + " " + std::to_string(st.ti + 1) + "/" +
                          std::to_string(st.types.size()) + "  " + st.pals[static_cast<std::size_t>(st.pi)];
      if (!st.ds.empty())
        right += "  " + std::to_string(st.ds.nrows()) + "x" + std::to_string(st.ds.series.size());
      right += "  " + std::to_string(W) + "x" + std::to_string(H) + " ";
      bar_row(cv, 0, 0, W, left, BAR_FG, BAR_BG);
      bar_row(cv, std::max(0, W - static_cast<int>(cp_len(right))), 0,
              static_cast<int>(cp_len(right)), right, BAR_FG, BAR_BG);

      // ---- body
      Rect cr{0, 1, W, H - 2};
      if (st.ds.empty()) {
        cv.text_c(cr.x, cr.y + cr.h / 2 - 1, cr.w, "cannot read data", S.title);
        if (!st.error.empty())
          cv.text_c(cr.x, cr.y + cr.h / 2, cr.w, trunc_to(st.error, static_cast<std::size_t>(cr.w - 2)),
                    S.axis);
        cv.text_c(cr.x, cr.y + cr.h / 2 + 2, cr.w, "r reload   n/N next file   q quit", S.subtitle);
      } else {
        RenderOpts o = opts_for(st, type);
        Dataset work = st.ds;
        render_chart(cv, cr, work, o);
      }

      if (st.mode == 2) draw_help(cv, W, H);

      // ---- status bar
      std::string keys =
          " q quit  c/C type  p/P palette  t table  e explode  v values  g grid  d/D 3-D  r reload  n/N file  w watch  ? keys ";
      if (static_cast<int>(cp_len(keys)) > W) keys = " q quit  c type  p palette  t table  ? keys ";
      bar_row(cv, 0, H - 1, W, keys, BAR_FG, BAR_BG);

      std::string out = cv.dump(a.color, true);
      cursor_home();
      ssize_t n = ::write(STDOUT_FILENO, out.data(), out.size());
      (void)n;
      dirty = false;
    }

    std::string k = read_key(st.autoreload ? 200 : 250);
    if (k.empty()) continue;

    if (k == "q" || k == "ctrl-c" || k == "ctrl-d") {
      quit = true;
    } else if (k == "c" || k == "right" || k == " ") {
      st.ti = (st.ti + 1) % static_cast<int>(st.types.size());
      st.mode = 0;
      dirty = true;
    } else if (k == "C" || k == "left") {
      st.ti = (st.ti + static_cast<int>(st.types.size()) - 1) % static_cast<int>(st.types.size());
      st.mode = 0;
      dirty = true;
    } else if (k.size() == 1 && k[0] >= '1' && k[0] <= '9') {
      int idx = k[0] - '1';
      if (idx < static_cast<int>(st.types.size())) {
        st.ti = idx;
        st.mode = 0;
        dirty = true;
      }
    } else if (k == "p") {
      st.pi = (st.pi + 1) % static_cast<int>(st.pals.size());
      dirty = true;
    } else if (k == "P") {
      st.pi = (st.pi + static_cast<int>(st.pals.size()) - 1) % static_cast<int>(st.pals.size());
      dirty = true;
    } else if (k == "t") {
      st.mode = (st.mode == 1) ? 0 : 1;
      dirty = true;
    } else if (k == "?") {
      st.mode = (st.mode == 2) ? 0 : 2;
      dirty = true;
    } else if (k == "esc") {
      if (st.mode != 0) { st.mode = 0; dirty = true; }
    } else if (k == "e") {
      if (st.explode == -2) st.explode = -1;
      else if (st.explode == -1) st.explode = 0;
      else if (st.explode + 1 < st.nsl) st.explode++;
      else st.explode = -2;
      dirty = true;
    } else if (k == "v") {
      st.values = !st.values;
      dirty = true;
    } else if (k == "g") {
      st.grid = !st.grid;
      dirty = true;
    } else if (k == "d") {
      if (st.depth < 6) st.depth++;
      dirty = true;
    } else if (k == "D") {
      if (st.depth > 0) st.depth--;
      dirty = true;
    } else if (k == "n") {
      st.fi = (st.fi + 1) % st.files.size();
      reload(st, true);
      dirty = true;
    } else if (k == "N") {
      st.fi = (st.fi + st.files.size() - 1) % st.files.size();
      reload(st, true);
      dirty = true;
    } else if (k == "r") {
      reload(st, true);
      dirty = true;
    } else if (k == "w") {
      st.autoreload = !st.autoreload;
      dirty = true;
    }
  }

  cursor_show(true);
  screen_clear();
  std::fflush(stdout);
  return 0;
}

} // namespace ch
