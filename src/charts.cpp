// charts.cpp -- the chart renderers.  16 colours, dither shading, DOS boxes.
//
// Marks are drawn on a Surface in pixels and text goes to the Scene in cell
// coordinates; the same code serves real pixels and half blocks (see gfx.hpp).
#include "chart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util.hpp"

namespace ch {

static constexpr double PI = 3.14159265358979323846;

// ---- palettes --------------------------------------------------------------

struct Pal {
  const char *name;
  std::vector<int> c;
};

static const std::vector<Pal> &pals() {
  static const std::vector<Pal> p = {
      {"dos", {11, 14, 10, 9, 13, 12, 15, 3, 6, 2, 1, 5, 7}},
      {"ega", {9, 10, 11, 12, 13, 14, 6, 2, 4, 5, 1, 3, 7, 15}},
      {"cga", {14, 13, 15, 11}},
      {"ice", {14, 15, 12, 6, 7, 4, 11}},
      {"fire", {11, 9, 3, 1, 15, 13, 5}},
      {"green", {10, 2, 15, 14, 6, 7}},
      {"amber", {11, 3, 15, 7, 9, 1}},
      {"mono", {15, 7, 15, 7, 15, 7}},
  };
  return p;
}

std::vector<std::string> type_names() {
  return {"bar", "stacked", "hbar", "dumbbell", "line", "area", "pie", "pie3d", "donut", "scatter", "hist", "table"};
}

std::string type_canonical(const std::string &t) {
  std::string s = lower(trim(t));
  if (s == "column" || s == "grouped" || s == "bars" || s == "bar3d") return "bar";
  if (s == "xy" || s == "points") return "scatter";
  if (s == "histogram") return "hist";
  if (s == "lines") return "line";
  if (s == "doughnut" || s == "ring") return "donut";
  if (s == "stack" || s == "stackedbar") return "stacked";
  if (s == "barh" || s == "horizontal") return "hbar";
  if (s == "dumbbells" || s == "before_after" || s == "beforeafter" || s == "change" || s == "dots") return "dumbbell";
  return s;
}

bool type_valid(const std::string &t) {
  std::string c = type_canonical(t);
  for (const auto &x : type_names())
    if (x == c) return true;
  return false;
}

std::vector<std::string> palette_names() {
  std::vector<std::string> v;
  for (const auto &p : pals()) v.push_back(p.name);
  return v;
}

std::vector<int> palette_cols(const std::string &name, std::size_t n) {
  const std::vector<int> *src = &pals()[0].c;
  std::vector<int> custom;
  if (starts_with(name, "custom:")) {
    for (const auto &t : split(name.substr(7), ',')) custom.push_back(std::atoi(t.c_str()) & 15);
    if (!custom.empty()) src = &custom;
  }
  for (const auto &p : pals())
    if (ieq(p.name, name)) { src = &p.c; break; }
  std::vector<int> out;
  out.reserve(n);
  // A colour that vanishes into the panel is no use: on a light panel take
  // the dim partner, on any panel skip the panel's own colour.
  const bool light = contrast_on(S.panel_bg) == 0 && S.slide_bg != BG_NONE;
  for (std::size_t k = 0; out.size() < n && k < n + 64; k++) {
    int c = (*src)[k % src->size()];
    if (light) c = (c == 15) ? 0 : (c == 7 ? 8 : dim_of(static_cast<uint8_t>(c)));
    if (S.slide_bg != BG_NONE && c == S.panel_bg && src->size() > 1) continue;
    out.push_back(c);
  }
  while (out.size() < n) out.push_back(7);
  return out;
}

static bool hidden_series(const Series &s) { return !s.name.empty() && s.name[0] == '\x01'; }

// The colour an author pinned on a name, or on a position; -1 when neither.
static int pinned_color(const RenderOpts &o, const std::string &name, std::size_t position) {
  std::size_t pos = 0;
  int by_pos = -1;
  for (const auto &c : o.colors) {
    if (c.first.empty()) { if (pos++ == position) by_pos = c.second; }
    else if (ieq(trim(c.first), trim(name))) return c.second;
  }
  return by_pos;
}

static void assign_colors(Dataset &ds, const RenderOpts &o) {
  std::vector<int> c = palette_cols(o.palette, ds.series.size());
  std::size_t k = 0;
  for (auto &s : ds.series) {
    if (hidden_series(s)) continue;
    int pin = pinned_color(o, s.name, k);
    s.color = pin >= 0 ? pin : c[k];
    k++;
  }
}

// Without colour (or on the mono palette) the dither has to tell the series
// apart, the way shaded charts did before colour was everywhere.
static bool by_shade(const RenderOpts &o) { return !o.color || ieq(o.palette, "mono"); }

static Ink fill_ink(const RenderOpts &o, int color, std::size_t si) {
  if (by_shade(o)) return Ink(static_cast<uint8_t>(o.color ? color : 15), S.panel_bg == BG_NONE ? 0 : S.panel_bg,
                              static_cast<uint8_t>(si % 4));
  return Ink(static_cast<uint8_t>(color));
}

static uint8_t panel_color() { return S.slide_bg == BG_NONE ? 0 : S.panel_bg; }

// ---- the plot ----------------------------------------------------------------

namespace {

struct Box {
  double x = 0, y = 0, w = 0, h = 0; // cells
  bool hits(const Box &o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

struct Anchor {
  int s = 0, i = 0;
  Pt p;          // surface pixels
  double dir = 0; // pies: which way is "out", radians; NaN elsewhere
};

struct Plot {
  Scene *sc = nullptr;
  Surface *sf = nullptr;
  Rect cells; // where the surface sits
  Rect clip;  // text stays inside this
  int ox = 0, oy = 0, pw = 1, ph = 1;
  double lo = 0, hi = 1;
  bool fine = false;
  int sx = 1, sy = 2;
  std::vector<Anchor> anchors;
  std::vector<double> slot_cx; // category centres, pixels
  std::vector<double> slot_cy; // the same down the side, when the value axis runs across
  bool across = false;         // hbar, dumbbell: values run left to right
  bool numeric_x = false;      // scatter: X is a value, not a category
  double xlo = 0, xhi = 1;
  mutable std::vector<Box> taken; // value labels already down: callouts keep off them

  double X(double v) const { // the value axis of a chart laid on its side
    double f = (v - lo) / (hi - lo);
    return ox + std::max(0.0, std::min(1.0, f)) * (pw - 1);
  }

  double Y(double v) const {
    double f = (v - lo) / (hi - lo);
    if (!(f > 0)) f = 0;
    if (f > 1) f = 1;
    return oy + ph - f * ph;
  }
  double cellx(double px) const { return cells.x + px / sx; }
  double celly(double py) const { return cells.y + py / sy; }

  // Text at (x, y) in cells with its left edge (-1), middle (0) or right
  // edge (1) on x: kept inside the panel, whole cells on cell output, and
  // remembered so callouts keep off it.  Every value label goes through here.
  void put(double x, double y, const std::string &t, uint8_t fg, int align, int bg = -1) const {
    const double len = static_cast<double>(cp_len(t));
    x = align < 0 ? x : (align == 0 ? x - len / 2 : x - len);
    if (!fine) { x = std::floor(x + 0.5); y = std::floor(y + 0.5); }
    x = std::max<double>(clip.x, std::min<double>(x, clip.right() + 1 - len));
    y = std::max<double>(clip.y, std::min<double>(y, clip.bottom()));
    sc->text(x, y, t, fg, bg);
    taken.push_back(Box{x, y, len, 1});
  }
  // Centred on a pixel position.
  void label(double px, double py, const std::string &t, uint8_t fg, int bg = -1) const {
    put(cellx(px), celly(py) - 0.5, t, fg, 0, bg);
  }
  void remember(int s, int i, double x, double y, double dir = std::nan("")) {
    Anchor a;
    a.s = s;
    a.i = i;
    a.p = Pt{x, y};
    a.dir = dir;
    anchors.push_back(a);
  }
  const Anchor *find(int s, int i) const {
    for (const auto &a : anchors)
      if (a.i == i && (a.s == s || s < 0)) return &a;
    return nullptr;
  }
};

// Ticks that land on round numbers, and an axis stretched to meet them unless
// the caller pinned an end.
std::vector<double> nice_range(double &lo, double &hi, int want, bool pin_lo, bool pin_hi, bool whole = false) {
  if (!(hi > lo)) hi = lo + 1;
  // counts are whole numbers: never tick at 0.5 of a thing
  if (whole) want = std::max(2, std::min(want, static_cast<int>(std::ceil(hi - lo)) + 1));
  // Rounding the axis out to a tick can leave a third of the plot empty (1,842
  // on an axis to 4K).  Allow a few more ticks when that buys a tighter fit.
  const double lo0 = lo, hi0 = hi;
  double best_waste = 1e18, best_lo = lo, best_hi = hi;
  std::vector<double> best;
  for (int w = want; w <= want + (whole ? 0 : 4); w++) {
    double l = lo0, h = hi0;
    std::vector<double> t = nice_ticks(l, h, w);
    if (t.size() >= 2) {
      double step = t[1] - t[0];
      if (!pin_lo) l = std::floor(l / step + 1e-9) * step;
      if (!pin_hi) h = std::ceil(h / step - 1e-9) * step;
      t = nice_ticks(l, h, w);
    }
    double waste = ((h - hi0) + (lo0 - l)) / (hi0 - lo0);
    if (waste < best_waste - 1e-9) { best_waste = waste; best = t; best_lo = l; best_hi = h; }
    if (waste <= 0.2) break;
  }
  lo = best_lo;
  hi = best_hi;
  // An axis pinned at 12 should say 12, even when the round ticks are 0 5 10.
  if (pin_hi && best.size() >= 2) {
    double step = best[1] - best[0];
    if (hi - best.back() >= step * 0.35) best.push_back(hi);
  }
  return best;
}

Plot make_axes(Scene &sc, Rect r, Rect clip, double lo, double hi, const std::vector<double> &ticks,
               const RenderOpts &o, const std::vector<std::string> *cats, bool numeric_x, double xlo,
               double xhi) {
  Plot p;
  p.sc = &sc;
  p.clip = clip;
  p.lo = lo;
  p.hi = hi;
  p.fine = sc.mode().pixel;
  p.sx = sc.mode().sx();
  p.sy = sc.mode().sy();

  int tw = 1;
  for (double t : ticks) tw = std::max(tw, static_cast<int>(cp_len(fmt_axis(t))));
  int yl_cols = (o.ylabel.empty() || r.w < 24) ? 0 : 2;
  int left = yl_cols + tw + 1;
  if (left + 6 > r.w) left = std::max(1, r.w - 6);

  bool has_x = numeric_x || (cats && !cats->empty());
  int below = 1 + (has_x ? 1 : 0) + (o.xlabel.empty() ? 0 : 1);
  int top_pad = r.h >= 10 ? 1 : 0;

  int gx = r.x + left, gy = r.y + top_pad;
  int gw = std::max(1, r.right() - gx + 1 - (r.w > 30 ? 1 : 0));
  int gh = std::max(1, r.h - top_pad - below);

  p.cells = Rect{gx - 1, gy, gw + 1, gh + 1};
  p.sf = &sc.surface(p.cells);
  p.ox = p.sx;
  p.oy = 0;
  p.pw = gw * p.sx;
  p.ph = gh * p.sy;
  Surface &sf = *p.sf;

  if (yl_cols > 0) {
    std::string yl = trunc_to(o.ylabel, static_cast<std::size_t>(gh));
    if (static_cast<int>(cp_len(o.ylabel)) > gh)
      sc.fit_note("ylabel \"" + o.ylabel + "\" is cut short: it runs down the axis one letter a row, and there are " +
                  std::to_string(gh) + " rows");
    sc.cv.vtext(r.x, gy + (gh - static_cast<int>(cp_len(yl))) / 2, yl, S.ylabel);
  }

  // spine
  if (p.fine) {
    sf.vline(p.ox - 1, 0, p.ph, Ink(S.axis));
    sf.hline(p.ox - 1, p.ox + p.pw - 1, p.ph, Ink(S.axis));
  } else {
    sc.cv.vline(gx - 1, gy, gh, G.v, S.axis);
    sc.cv.put(gx - 1, gy + gh, G.bl, S.axis);
    sc.cv.hline(gx, gy + gh, gw, G.h, S.axis);
  }

  int last_row = -99;
  double last_ty = 1e9;
  for (double t : ticks) {
    double y = p.Y(t);
    std::string lab = fmt_axis(t);
    int len = static_cast<int>(cp_len(lab));
    if (p.fine) {
      int yi = static_cast<int>(std::lround(y));
      sf.hline(p.ox - 5, p.ox - 2, yi, Ink(S.axis));
      if (o.grid && yi < p.ph - 1) sf.dotted_h(p.ox, p.ox + p.pw - 1, yi, Ink(S.grid), 4);
      // The top label is pulled down into the panel; in a short one that can
      // land it on the label below.  The tick stays, the label goes.
      double ty = std::max<double>(clip.y, p.celly(y) - 0.5);
      if (last_ty - ty < 0.95) continue;
      last_ty = ty;
      sc.text(gx - 1.5 - len, ty, lab, S.tick);
    } else {
      int row = static_cast<int>(std::floor(p.celly(y)));
      row = std::max(gy, std::min(row, gy + gh));
      if (row == last_row) continue;
      last_row = row;
      sc.cv.text_r(r.x + yl_cols, row, left - yl_cols - 1, lab, S.tick);
      if (row < gy + gh) {
        sc.cv.put(gx - 1, row, G.rt, S.axis);
        if (o.grid) sc.cv.hline(gx, row, gw, G.dot, S.grid);
      }
    }
  }

  const double laby = p.fine ? gy + gh + 0.45 : gy + gh + 1;
  if (cats && !cats->empty() && !numeric_x) {
    int n = static_cast<int>(cats->size());
    double slot = static_cast<double>(gw) / n;
    int maxlen = 1;
    for (const auto &c : *cats) maxlen = std::max(maxlen, static_cast<int>(cp_len(c)));
    // When the names do not fit, show every k-th one rather than mush.
    int every = 1;
    if (slot < maxlen + 1) every = std::max(1, static_cast<int>(std::ceil((std::min(maxlen, 10) + 1) / slot)));
    if (every > 1)
      sc.fit_note("only every " + std::to_string(every) + (every == 2 ? "nd" : (every == 3 ? "rd" : "th")) +
                  " category label is shown: " + std::to_string(n) + " labels of up to " + std::to_string(maxlen) +
                  " characters do not fit across " + std::to_string(gw) + " columns (shorten them, or use hbar)");
    bool cut = false;
    for (int i = 0; i < n; i++) {
      double cxp = p.ox + (i + 0.5) * p.pw / n;
      p.slot_cx.push_back(cxp);
      if (p.fine) sf.vline(static_cast<int>(cxp), p.ph + 1, p.ph + 3, Ink(S.axis));
      else if (slot >= 2) sc.cv.put(static_cast<int>(p.cellx(cxp)), gy + gh, G.tt, S.axis);
      if (i % every) continue;
      int room = std::max(1, static_cast<int>(slot * every) - (every > 1 || slot >= 3 ? 1 : 0));
      std::string lab = trunc_to((*cats)[static_cast<std::size_t>(i)], static_cast<std::size_t>(room));
      int len = static_cast<int>(cp_len(lab));
      if (!cut && every == 1 && cp_len((*cats)[static_cast<std::size_t>(i)]) > static_cast<std::size_t>(room)) {
        cut = true;
        sc.fit_note("category label \"" + (*cats)[static_cast<std::size_t>(i)] + "\" is cut to " + std::to_string(room) +
                    " characters (shorten the labels, or use hbar)");
      }
      double x = p.cellx(cxp) - len / 2.0;
      if (!p.fine) x = std::floor(x + 0.5);
      x = std::max<double>(r.x, std::min<double>(x, r.right() + 1 - len));
      sc.text(x, laby, lab, S.label);
    }
  } else if (numeric_x) {
    p.numeric_x = true;
    p.xlo = xlo;
    p.xhi = xhi;
    std::vector<double> xt = nice_ticks(xlo, xhi, std::max(2, std::min(8, gw / 12 + 2)));
    for (double t : xt) {
      double f = (xhi > xlo) ? (t - xlo) / (xhi - xlo) : 0;
      if (f < -1e-9 || f > 1 + 1e-9) continue;
      double xp = p.ox + f * (p.pw - 1);
      std::string lab = fmt_axis(t);
      int len = static_cast<int>(cp_len(lab));
      if (p.fine) {
        sf.vline(static_cast<int>(xp), p.ph + 1, p.ph + 3, Ink(S.axis));
        if (o.grid && f > 0.001) sf.dotted_v(static_cast<int>(xp), 0, p.ph - 1, Ink(S.grid), 4);
      } else {
        sc.cv.put(static_cast<int>(p.cellx(xp)), gy + gh, G.tt, S.axis);
      }
      double x = p.cellx(xp) - len / 2.0;
      if (!p.fine) x = std::floor(x + 0.5);
      x = std::max<double>(r.x, std::min<double>(x, r.right() + 1 - len));
      sc.text(x, laby, lab, S.label);
    }
  }

  if (!o.xlabel.empty()) sc.cv.text_c(gx, r.bottom(), gw, o.xlabel, S.xlabel);
  return p;
}

// ---- legend ------------------------------------------------------------------

struct LegEntry {
  std::string name;
  int color = 7;
  std::string val;
  int level = 0;
};

int legend_width(const std::vector<LegEntry> &e) {
  std::size_t w = 0;
  for (const auto &x : e) {
    std::size_t l = cp_len(x.name) + 3;
    if (!x.val.empty()) l += cp_len(x.val) + 2;
    w = std::max(w, l);
  }
  return static_cast<int>(w);
}

void legend_draw(Scene &sc, Rect r, const std::vector<LegEntry> &e, bool vertical) {
  if (e.empty() || r.w < 4 || r.h < 1) return;
  Canvas &cv = sc.cv;
  auto swatch = [&](int x, int y, const LegEntry &en) {
    char32_t g = G.blk[en.level & 3];
    cv.put(x, y, g, static_cast<uint8_t>(en.color));
    cv.put(x + 1, y, g, static_cast<uint8_t>(en.color));
  };
  if (vertical) {
    int y = r.y;
    for (const auto &en : e) {
      if (y > r.bottom()) return;
      swatch(r.x, y, en);
      int room = r.w - 3;
      if (!en.val.empty()) room -= static_cast<int>(cp_len(en.val)) + 2;
      cv.text(r.x + 3, y, trunc_to(en.name, static_cast<std::size_t>(std::max(1, room))), S.legend);
      if (!en.val.empty() && r.w > static_cast<int>(cp_len(en.val)) + 5) cv.text_r(r.x, y, r.w, en.val, S.value);
      y++;
    }
    return;
  }
  // underneath: an aligned grid, centred as a block
  int colw = 0;
  for (const auto &en : e)
    colw = std::max(colw, static_cast<int>(cp_len(en.name) + (en.val.empty() ? 0 : cp_len(en.val) + 1)) + 5);
  colw = std::min(colw, r.w);
  int ncols = std::max(1, std::min(static_cast<int>(e.size()), r.w / std::max(1, colw)));
  int nrows = (static_cast<int>(e.size()) + ncols - 1) / ncols;
  ncols = (static_cast<int>(e.size()) + nrows - 1) / nrows; // no ragged last column
  int x0 = r.x + std::max(0, (r.w - ncols * colw + 2) / 2);
  for (std::size_t i = 0; i < e.size(); i++) {
    int row = static_cast<int>(i) / ncols, col = static_cast<int>(i) % ncols;
    if (row >= r.h) return;
    int x = x0 + col * colw, y = r.y + row;
    std::string seg = e[i].name + (e[i].val.empty() ? "" : " " + e[i].val);
    swatch(x, y, e[i]);
    cv.text(x + 3, y, trunc_to(seg, static_cast<std::size_t>(std::max(1, colw - 4))), S.legend);
  }
}

int legend_rows_needed(const std::vector<LegEntry> &e, int w) {
  int colw = 0;
  for (const auto &en : e)
    colw = std::max(colw, static_cast<int>(cp_len(en.name) + (en.val.empty() ? 0 : cp_len(en.val) + 1)) + 5);
  int ncols = std::max(1, std::min(static_cast<int>(e.size()), w / std::max(1, colw)));
  return (static_cast<int>(e.size()) + ncols - 1) / ncols;
}

// ---- bars --------------------------------------------------------------------

struct Depth {
  double dx = 0, dy = 0;
};

Depth depth_for(const Plot &p, const RenderOpts &o, double bar_w) {
  Depth d;
  if (!p.fine || o.depth <= 0) return d;
  d.dx = std::min<double>(o.depth * 3.0, std::max(2.0, bar_w * 0.5));
  d.dy = d.dx * 0.7;
  return d;
}

// One extruded bar.  ya/yb are the two ends in pixels, in either order.
void bar3d(const Plot &p, double x0, double x1, double ya, double yb, Ink front, int color, Depth d,
           bool shaded, bool top_face) {
  Surface &sf = *p.sf;
  double top = std::min(ya, yb), bot = std::max(ya, yb);
  if (x1 - x0 < 1) x1 = x0 + 1;
  if (!p.fine) {
    sf.rect(x0, top, x1, bot, front);
    if (shaded && x1 - x0 >= 3) sf.rect(x1 - 1, top, x1, bot, side_ink(static_cast<uint8_t>(color)));
    return;
  }
  const bool solid = d.dx > 0 && shaded;
  if (solid) {
    sf.poly({{x1, top}, {x1 + d.dx, top - d.dy}, {x1 + d.dx, bot - d.dy}, {x1, bot}},
            side_ink(static_cast<uint8_t>(color)));
    if (top_face)
      sf.poly({{x0, top}, {x1, top}, {x1 + d.dx, top - d.dy}, {x0 + d.dx, top - d.dy}},
              top_ink(static_cast<uint8_t>(color)));
  }
  sf.rect(x0, top, x1, bot, front);
  if (x1 - x0 >= 5) {
    Ink edge(panel_color());
    int ix0 = static_cast<int>(std::lround(x0)), ix1 = static_cast<int>(std::lround(x1)) - 1;
    int iy0 = static_cast<int>(std::lround(top)), iy1 = static_cast<int>(std::lround(bot)) - 1;
    sf.vline(ix0, iy0, iy1, edge);
    sf.vline(ix1, iy0, iy1, edge);
    sf.hline(ix0, ix1, iy0, edge);
    if (solid) {
      sf.line(x1 + d.dx, top - d.dy, x1 + d.dx, bot - d.dy, edge);
      if (top_face) {
        sf.line(x0, top, x0 + d.dx, top - d.dy, edge);
        sf.line(x0 + d.dx, top - d.dy, x1 + d.dx, top - d.dy, edge);
        sf.line(x1 - 1, top, x1 + d.dx - 1, top - d.dy, edge);
      }
    }
  }
}

// Bars are measured from zero.  When the axis starts above it they are cut
// short, and the chart says so the way print did: a torn gap across the bars
// just off the baseline and a double slash through the axis.
void axis_break(const Plot &p, bool horizontal) {
  Surface &sf = *p.sf;
  const Ink ink(S.axis);
  const int len = horizontal ? p.ph : p.pw;
  // along the break line from the value axis (-1 is the axis), across it from zero
  auto at = [&](int along, int across) {
    return horizontal ? Pt{static_cast<double>(p.ox + across), static_cast<double>(p.ph - 1 - along)}
                      : Pt{static_cast<double>(p.ox + along), static_cast<double>(p.ph - across)};
  };
  auto clear = [&](Pt q) {
    int x = static_cast<int>(q.x), y = static_cast<int>(q.y);
    sf.erase(x, y, x + 1, y + 1); // the panel shows through
  };
  if (!p.fine) {
    for (int t = 0; t < len; t++) clear(at(t, 2));
    Pt c = at(0, 2);
    int cx = horizontal ? static_cast<int>(p.cellx(c.x)) : p.cells.x;
    int cy = horizontal ? p.cells.bottom() : static_cast<int>(p.celly(c.y));
    p.sc->cv.put(cx, cy, p.sc->mode().ascii ? U'~' : U'≈', S.axis);
    return;
  }
  const int off = std::max(8, std::min(14, len / 10));
  for (int t = 0; t < len; t++) {
    int w = std::abs(t % 8 - 4) - 2; // a zigzag, 2 px either way
    for (int k = -2; k <= 1; k++) clear(at(t, off + w + k));
  }
  // the axis itself: a gap and two slashes across it
  for (int k = -3; k <= 3; k++) clear(at(-1, off + k));
  for (int s : {-2, 2}) {
    Pt a = at(-6, off + s - 2), b = at(4, off + s + 2);
    sf.line(a.x, a.y, b.x, b.y, ink);
  }
}

// ---- error bars --------------------------------------------------------------

bool has_whisker(const RenderOpts &o, std::size_t s, std::size_t i) {
  return s < o.err_lo.size() && i < o.err_lo[s].size() && std::isfinite(o.err_lo[s][i]) && std::isfinite(o.err_hi[s][i]);
}

// Stretch an axis so every whisker fits on it.
void whisker_bounds(const RenderOpts &o, double &lo, double &hi) {
  for (std::size_t s = 0; s < o.err_lo.size(); s++)
    for (std::size_t i = 0; i < o.err_lo[s].size(); i++)
      if (has_whisker(o, s, i)) { lo = std::min(lo, o.err_lo[s][i]); hi = std::max(hi, o.err_hi[s][i]); }
}

// A line between the two ends with a cap on each, outlined in the panel colour
// so it reads over a bar of any colour.  a0/a1 run along the value axis, at is
// the other coordinate; all in surface pixels.
void whisker(const Plot &p, double a0, double a1, double at, bool horizontal) {
  Surface &sf = *p.sf;
  const double cap = p.fine ? 5 : 1;
  auto seg = [&](double u0, double v0, double u1, double v1, Ink k, int w) {
    if (horizontal) sf.line(v0, u0, v1, u1, k, w);
    else sf.line(u0, v0, u1, v1, k, w);
  };
  auto draw = [&](Ink k, int w, double c) {
    seg(at, a0, at, a1, k, w);
    seg(at - c, a0, at + c, a0, k, w);
    seg(at - c, a1, at + c, a1, k, w);
  };
  if (p.fine) draw(Ink(panel_color()), 3, cap + 1);
  draw(Ink(S.value), 1, cap);
}

// Where the editing cursor is: a drop line to the axis and a fat ring.
void ring_cursor(const Plot &p, double x, double y) {
  if (!p.fine) { p.sf->ring(x, y, 1.5, Ink(15)); return; }
  for (int yy = static_cast<int>(y); yy < p.oy + p.ph; yy++)
    if ((yy / 3) % 2 == 0) p.sf->set(static_cast<int>(x), yy, Ink(15));
  p.sf->disc(x, y, 9, Ink(panel_color()));
  p.sf->disc(x, y, 8, Ink(15));
  p.sf->disc(x, y, 5, Ink(panel_color()));
  p.sf->disc(x, y, 3, Ink(S.accent));
}

double cell_at(const Dataset &ds, std::size_t s, std::size_t i) {
  return i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
}

// A value label: prec when the chart sets it, else as many decimals as the
// file wrote (so 62.0 stands beside 63.1, not 62), else the automatic form.
std::string fmt_point(const Series &se, double v, int prec) {
  if (prec < 0 && se.decimals >= 0 && se.decimals <= 3) prec = se.decimals;
  return fmt_val(v, prec);
}

Plot draw_bars(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o, bool stacked, bool tight) {
  std::size_t n = ds.nrows();
  std::size_t ns = ds.series.size();
  const std::vector<std::string> *cats = (ds.labels.size() == n) ? &ds.labels : nullptr;

  double lo = 0, hi = 0;
  if (stacked) {
    for (std::size_t i = 0; i < n; i++) {
      double pos = 0, neg = 0;
      for (std::size_t s = 0; s < ns; s++) {
        double v = cell_at(ds, s, i);
        if (!std::isfinite(v)) continue;
        if (v > 0) pos += v; else neg += v;
      }
      hi = std::max(hi, pos);
      lo = std::min(lo, neg);
    }
  } else {
    ds.bounds(lo, hi);
    whisker_bounds(o, lo, hi);
  }
  if (lo > 0) lo = 0;
  if (hi < 0) hi = 0;
  if (o.has_lo) lo = o.lo;
  if (o.has_hi) hi = o.hi;
  else hi += (hi - lo) * (o.values ? 0.10 : 0.04); // air over the tallest bar
  int want = std::max(3, std::min(9, r.h / 3));
  std::vector<double> ticks = nice_range(lo, hi, want, o.has_lo, o.has_hi, tight);

  Plot p = make_axes(sc, r, clip, lo, hi, ticks, o, cats, false, 0, 1);
  if (n == 0 || ns == 0) return p;
  double base = p.Y(std::max(lo, std::min(hi, 0.0)));
  double slot = static_cast<double>(p.pw) / n;
  double group = tight ? slot : std::max(1.0, std::floor(slot * (ns > 1 ? 0.82 : 0.68)));
  if (!p.fine && !tight) group = std::max(1.0, std::min(slot, std::floor(slot) - (slot >= 3 ? 1 : 0)));
  // Whole pixels, so every bar is exactly as wide as its neighbour.
  double bar_w = stacked ? group : std::max(1.0, std::floor(group / ns));
  if (!tight) group = stacked ? group : bar_w * ns;
  Depth d = depth_for(p, o, bar_w);
  if (tight) d = Depth();
  // Keep the last bar's side face inside the plot.
  double shift = -d.dx / 2;
  struct Pending { double x, y0, y1; };
  std::vector<Pending> whiskers; // drawn over every bar, not under the next one

  for (std::size_t i = 0; i < n; i++) {
    double gx0 = p.ox + i * slot + (slot - group) / 2 + shift;
    if (!tight) gx0 = std::floor(gx0 + 0.5);
    if (stacked) {
      double pos = 0, neg = 0;
      std::size_t last_pos = ns;
      for (std::size_t s = 0; s < ns; s++) {
        double v = cell_at(ds, s, i);
        if (std::isfinite(v) && v > 0) last_pos = s;
      }
      for (std::size_t s = 0; s < ns; s++) {
        double v = cell_at(ds, s, i);
        if (!std::isfinite(v) || v == 0) continue;
        double a = v > 0 ? pos : neg, b = a + v;
        (v > 0 ? pos : neg) = b;
        bar3d(p, gx0, gx0 + bar_w, p.Y(a), p.Y(b), fill_ink(o, ds.series[s].color, s), ds.series[s].color, d,
              !by_shade(o), s == last_pos);
        p.remember(static_cast<int>(s), static_cast<int>(i), gx0 + bar_w / 2, p.Y(b));
      }
      if (o.values && pos > 0)
        p.label(gx0 + bar_w / 2 + d.dx / 2, p.Y(pos) - d.dy - p.sy * 0.6, fmt_val(pos + neg, o.prec), S.value);
    } else {
      for (std::size_t s = 0; s < ns; s++) {
        double v = cell_at(ds, s, i);
        if (!std::isfinite(v)) continue;
        double x0 = gx0 + s * bar_w, x1 = x0 + bar_w;
        if (p.fine && !tight && ns > 1 && bar_w >= 6) x1 -= 1;
        double yv = p.Y(v);
        if (std::fabs(yv - base) < 1 && v != 0) yv = base + (v > 0 ? -1 : 1);
        // One series: a colour pinned on a category paints that one bar.
        int col = ds.series[s].color;
        if (ns == 1 && i < ds.labels.size()) {
          int pin = pinned_color(o, ds.labels[i], static_cast<std::size_t>(-1));
          if (pin >= 0) col = pin;
        }
        bar3d(p, x0, x1, base, yv, fill_ink(o, col, s), col, d, !by_shade(o), true);
        p.remember(static_cast<int>(s), static_cast<int>(i), (x0 + x1) / 2, yv);
        double top = yv, bottom = yv; // the label goes past the whisker too
        if (has_whisker(o, s, i)) {
          double wa = p.Y(o.err_lo[s][i]), wb = p.Y(o.err_hi[s][i]);
          whiskers.push_back({(x0 + x1) / 2, wa, wb});
          top = std::min(top, std::min(wa, wb) - (p.fine ? 2 : 0));
          bottom = std::max(bottom, std::max(wa, wb));
        }
        if (o.values) {
          std::string lab = fmt_point(ds.series[s], v, o.prec);
          if (static_cast<int>(cp_len(lab)) * p.sx <= bar_w + (ns == 1 ? slot - group : 0) + (p.fine ? 2 : 0)) {
            double ly = v >= 0 ? std::min(yv - d.dy, top) - p.sy * 0.6 : bottom + p.sy * 0.6;
            p.label((x0 + x1) / 2 + d.dx / 2, ly, lab, S.value);
          }
        }
      }
    }
  }
  for (const auto &w : whiskers) whisker(p, w.y0, w.y1, w.x, false);
  if (lo > 0) axis_break(p, false);
  return p;
}

// ---- charts on their side ------------------------------------------------------

// The frame of a chart whose value axis runs across (hbar, dumbbell): the
// category names in a gutter down the left, ticks along the bottom.  lo and hi
// come back rounded out to the ticks; gut is the gutter's width.
Plot sideways_axes(Scene &sc, Rect r, Rect clip, const Dataset &ds, const RenderOpts &o, double &lo, double &hi, int &gut) {
  Plot p;
  p.sc = &sc;
  p.clip = clip;
  p.fine = sc.mode().pixel;
  p.sx = sc.mode().sx();
  p.sy = sc.mode().sy();
  p.across = true;
  std::size_t labw = 0;
  for (std::size_t i = 0; i < ds.nrows(); i++)
    labw = std::max(labw, cp_len(i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1)));
  gut = std::min(static_cast<int>(labw) + 1, std::max(4, r.w / 3));
  if (static_cast<int>(labw) + 1 > gut)
    sc.fit_note("category labels are cut to " + std::to_string(gut - 1) + " characters (the longest is " +
                std::to_string(labw) + ")");
  const int gx = r.x + gut + 1, gw = std::max(1, r.right() - gx + 1 - 2);
  const int gy = r.y, gh = std::max(1, r.h - 2 - (o.xlabel.empty() ? 0 : 1));
  std::vector<double> ticks = nice_range(lo, hi, std::max(3, std::min(8, gw / 10)), o.has_lo, o.has_hi);
  p.lo = lo;
  p.hi = hi;
  p.cells = Rect{gx - 1, gy, gw + 1, gh + 1};
  p.sf = &sc.surface(p.cells);
  p.ox = p.sx;
  p.pw = gw * p.sx;
  p.ph = gh * p.sy;
  Surface &sf = *p.sf;
  if (p.fine) sf.hline(p.ox - 1, p.ox + p.pw - 1, p.ph, Ink(S.axis));
  else sc.cv.hline(gx, gy + gh, gw, G.h, S.axis);
  for (double t : ticks) {
    const double x = p.X(t);
    const std::string lab = fmt_axis(t);
    const double len = static_cast<double>(cp_len(lab));
    if (p.fine) {
      sf.vline(static_cast<int>(x), p.ph + 1, p.ph + 3, Ink(S.axis));
      if (o.grid) sf.dotted_v(static_cast<int>(x), 0, p.ph - 1, Ink(S.grid), 4);
    } else {
      sc.cv.put(static_cast<int>(p.cellx(x)), gy + gh, G.tt, S.axis);
      if (o.grid) sc.cv.vline(static_cast<int>(p.cellx(x)), gy, gh, G.dot, S.grid);
    }
    double tx = p.cellx(x) - len / 2;
    if (!p.fine) tx = std::floor(tx + 0.5);
    tx = std::max<double>(r.x, std::min<double>(tx, r.right() + 1 - len));
    sc.text(tx, p.fine ? gy + gh + 0.45 : gy + gh + 1, lab, S.tick);
  }
  if (!o.xlabel.empty()) sc.cv.text_c(gx, r.bottom(), gw, o.xlabel, S.xlabel);
  return p;
}

// ---- horizontal bars ---------------------------------------------------------

Plot draw_hbars(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o) {
  std::size_t n = ds.nrows(), ns = ds.series.size();
  if (n == 0 || ns == 0) return Plot();

  double lo = 0, hi = 0;
  ds.bounds(lo, hi);
  whisker_bounds(o, lo, hi);
  if (lo > 0) lo = 0;
  if (hi < 0) hi = 0;
  if (o.has_lo) lo = o.lo;
  if (o.has_hi) hi = o.hi;
  else hi += (hi - lo) * (o.values ? 0.12 : 0.03);

  int gut = 0;
  Plot p = sideways_axes(sc, r, clip, ds, o, lo, hi, gut);
  Surface &sf = *p.sf;
  auto X = [&](double v) { return p.X(v); };
  const int gx = p.cells.x + 1, gy = p.cells.y, gh = p.cells.h - 1;

  double slot = static_cast<double>(p.ph) / n;
  double group = std::max(1.0, std::floor(slot * (ns > 1 ? 0.8 : 0.66)));
  if (!p.fine) group = std::max(1.0, std::floor(slot) - (slot >= 3 * p.sy ? p.sy : 0));
  double bar_h = group / ns;
  double zero = X(std::max(lo, std::min(hi, 0.0)));
  double dx = (p.fine && o.depth > 0 && !by_shade(o)) ? std::min<double>(o.depth * 3.0, std::max(2.0, bar_h * 0.5)) : 0;
  double dy = dx * 0.7;
  if (p.fine) sf.vline(static_cast<int>(zero) - (lo >= 0 ? 1 : 0), 0, p.ph, Ink(S.axis));
  else sc.cv.vline(gx - 1, gy, gh, G.v, S.axis);

  for (std::size_t i = 0; i < n; i++) {
    double gy0 = i * slot + (slot - group) / 2 + dy / 2;
    p.slot_cy.push_back(gy0 + group / 2);
    std::string lab = i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1);
    lab = trunc_to(lab, static_cast<std::size_t>(gut - 1));
    double ly = p.celly(gy0 + group / 2) - 0.5;
    if (!p.fine) ly = std::floor(ly + 0.5);
    sc.text(r.x + gut - 1 - static_cast<int>(cp_len(lab)), std::min<double>(ly, gy + gh - 1), lab, S.label);
    for (std::size_t s = 0; s < ns; s++) {
      double v = cell_at(ds, s, i);
      if (!std::isfinite(v)) continue;
      double y0 = gy0 + s * bar_h, y1 = y0 + bar_h;
      if (p.fine && ns > 1 && bar_h >= 6) y1 -= 1;
      double xv = X(v);
      double xa = std::min(zero, xv), xb = std::max(zero, xv) + 1;
      uint8_t c = static_cast<uint8_t>(ds.series[s].color);
      if (ns == 1 && i < ds.labels.size()) {
        int pin = pinned_color(o, ds.labels[i], static_cast<std::size_t>(-1));
        if (pin >= 0) c = static_cast<uint8_t>(pin);
      }
      if (dx > 0) {
        sf.poly({{xa, y0}, {xb, y0}, {xb + dx, y0 - dy}, {xa + dx, y0 - dy}}, top_ink(c));
        sf.poly({{xb, y0}, {xb + dx, y0 - dy}, {xb + dx, y1 - dy}, {xb, y1}}, side_ink(c));
      }
      sf.rect(xa, y0, xb, y1, fill_ink(o, c, s));
      if (p.fine && bar_h >= 5) {
        Ink edge(panel_color());
        sf.hline(static_cast<int>(xa), static_cast<int>(xb) - 1, static_cast<int>(std::lround(y0)), edge);
        sf.hline(static_cast<int>(xa), static_cast<int>(xb) - 1, static_cast<int>(std::lround(y1)) - 1, edge);
        sf.vline(static_cast<int>(xb) - 1, static_cast<int>(std::lround(y0)), static_cast<int>(std::lround(y1)) - 1, edge);
        if (dx > 0) {
          sf.line(xa, y0, xa + dx, y0 - dy, edge);
          sf.line(xa + dx, y0 - dy, xb + dx, y0 - dy, edge);
          sf.line(xb + dx, y0 - dy, xb + dx, y1 - dy, edge);
          sf.line(xb - 1, y0, xb + dx - 1, y0 - dy, edge);
        }
      } else if (!p.fine && !by_shade(o) && bar_h >= 2 && xb - xa >= 3) {
        sf.rect(xa, y1 - 1, xb, y1, side_ink(c));
      }
      p.remember(static_cast<int>(s), static_cast<int>(i), xv + dx, (y0 + y1) / 2 - dy / 2, 0.0);
      double reach = xb + dx;
      if (has_whisker(o, s, i)) {
        double wa = X(o.err_lo[s][i]), wb = X(o.err_hi[s][i]);
        whisker(p, wa, wb, (y0 + y1) / 2, true);
        reach = std::max(reach, std::max(wa, wb) + (p.fine ? 6 : 1));
      }
      if (o.values) {
        std::string vl = fmt_point(ds.series[s], v, o.prec);
        double tx = p.cellx(reach) + 0.6;
        if (!p.fine) tx = std::ceil(tx); // clear of the bar's end, never over it
        if (tx + cp_len(vl) <= clip.right() + 1) p.put(tx, p.celly((y0 + y1) / 2 - dy / 2) - 0.5, vl, S.value, -1);
      }
    }
  }
  if (lo > 0) axis_break(p, true);
  return p;
}

// ---- dumbbell ------------------------------------------------------------------

// Before and after: a row per category, a dot per series, joined by a line
// with an arrowhead on the last series so the direction of change reads at a
// glance.  Positions, not lengths, so the axis need not start at zero.
Plot draw_dumbbell(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o) {
  const std::size_t n = ds.nrows(), ns = ds.series.size();
  if (n == 0 || ns == 0) return Plot();

  double lo = 0, hi = 0;
  ds.bounds(lo, hi);
  double pad = std::max((hi - lo) * 0.06, std::fabs(hi) * 1e-3 + 1e-9);
  lo -= pad;
  hi += pad;
  if (lo < 0 && !ds.has_negative()) lo = 0;
  if (o.has_lo) lo = o.lo;
  if (o.has_hi) hi = o.hi;

  int gut = 0;
  Plot p = sideways_axes(sc, r, clip, ds, o, lo, hi, gut);
  Surface &sf = *p.sf;
  auto X = [&](double v) { return p.X(v); };
  const int gy = p.cells.y, gh = p.cells.h - 1;

  const double slot = static_cast<double>(p.ph) / n;
  const double rad = p.fine ? std::max(3.0, std::min(7.0, slot * 0.22)) : 1;
  const Ink bar(p.fine ? 7 : 8);
  for (std::size_t i = 0; i < n; i++) {
    const double cy = std::floor(i * slot + slot / 2);
    p.slot_cy.push_back(cy);
    std::string lab = trunc_to(i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1), static_cast<std::size_t>(gut - 1));
    double ly = p.celly(cy) - 0.5;
    if (!p.fine) ly = std::floor(ly + 0.5);
    sc.text(r.x + gut - 1 - static_cast<int>(cp_len(lab)), std::min<double>(ly, gy + gh - 1), lab, S.label);

    // the span, then the arrowhead where the change ends up
    double first = std::nan(""), last = std::nan(""), a = 1e300, b = -1e300;
    for (std::size_t s = 0; s < ns; s++) {
      double v = cell_at(ds, s, i);
      if (!std::isfinite(v)) continue;
      if (std::isnan(first)) first = v;
      last = v;
      a = std::min(a, X(v));
      b = std::max(b, X(v));
    }
    if (std::isnan(first)) continue;
    if (b > a) sf.line(a, cy, b, cy, bar, p.fine ? 3 : 1);
    const double x0 = X(first), x1 = X(last), dir = x1 > x0 ? 1 : -1;
    const double head = p.fine ? rad + 7 : 2;
    if (p.fine && std::fabs(x1 - x0) > rad * 2 + head) {
      double tip = x1 - dir * (rad + 1);
      sf.poly({{tip, cy}, {tip - dir * 8, cy - 5}, {tip - dir * 8, cy + 5}}, bar);
    }
    for (std::size_t s = 0; s < ns; s++) {
      double v = cell_at(ds, s, i);
      if (!std::isfinite(v)) continue;
      double x = X(v);
      uint8_t c = static_cast<uint8_t>(ds.series[s].color);
      if (p.fine) {
        sf.marker(x, cy, static_cast<int>(s), rad + 1.5, Ink(panel_color()));
        sf.marker(x, cy, static_cast<int>(s), rad, Ink(c));
      } else {
        sf.rect(x - 1, cy, x + 1, cy + 1, Ink(c));
      }
      p.remember(static_cast<int>(s), static_cast<int>(i), x, cy, 0.0);
      if (!o.values || (b - a < 1 && std::isfinite(v) && v != last)) continue; // one label where they coincide
      // the outermost dots label outwards; any in between, above
      std::string vl = fmt_point(ds.series[s], v, o.prec);
      const uint8_t fg = ns == 1 ? S.value : c;
      if (x <= a + 0.5 && ns > 1 && b > a) p.put(p.cellx(x - rad) - 0.6, p.celly(cy) - 0.5, vl, fg, 1);
      else if (x >= b - 0.5) p.put(p.cellx(x + rad) + 0.6, p.celly(cy) - 0.5, vl, fg, -1);
      else p.put(p.cellx(x), p.celly(cy - rad) - 1.2, vl, fg, 0);
    }
  }
  return p;
}

// ---- line / area -------------------------------------------------------------

Plot draw_lines(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o, bool area) {
  std::size_t n = ds.nrows();
  const std::vector<std::string> *cats = (ds.labels.size() == n) ? &ds.labels : nullptr;

  double lo = 0, hi = 0;
  ds.bounds(lo, hi);
  whisker_bounds(o, lo, hi);
  if (area && o.zero_base) {
    if (lo > 0) lo = 0;
    if (hi < 0) hi = 0;
  }
  double pad = (hi - lo) * 0.05;
  if (!(area && lo == 0) && lo != 0) lo -= (lo > 0 && lo - pad < 0) ? lo : pad;
  hi += pad * (o.values ? 2 : 1);
  if (o.has_lo) lo = o.lo;
  if (o.has_hi) hi = o.hi;
  std::vector<double> ticks = nice_range(lo, hi, std::max(3, std::min(9, r.h / 3)), o.has_lo, o.has_hi);

  Plot p = make_axes(sc, r, clip, lo, hi, ticks, o, cats, false, 0, 1);
  if (n == 0 || ds.series.empty()) return p;
  Surface &sf = *p.sf;
  double base = p.Y(std::max(lo, std::min(hi, 0.0)));
  int width = p.fine ? (p.ph > 160 ? 3 : 2) : 1;
  double mr = p.fine ? (n > 40 ? 2.5 : 4) : 0;

  // Areas go down tallest first, so a smaller series is never buried.
  std::vector<std::size_t> order;
  for (std::size_t s = 0; s < ds.series.size(); s++) order.push_back(s);
  if (area)
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
      double nx = std::max<double>(1, ds.series[x].v.size()), ny = std::max<double>(1, ds.series[y].v.size());
      return ds.sum(x) / nx > ds.sum(y) / ny;
    });

  for (int pass = area ? 0 : 1; pass < 2; pass++) {
    for (std::size_t s : order) {
      const Series &se = ds.series[s];
      uint8_t c = static_cast<uint8_t>(se.color);
      std::vector<Pt> pts;
      std::vector<int> idx;
      for (std::size_t i = 0; i < n; i++) {
        double v = cell_at(ds, s, i);
        if (!std::isfinite(v)) continue;
        pts.push_back(Pt{p.ox + (i + 0.5) * p.pw / n, p.Y(v)});
        idx.push_back(static_cast<int>(i));
      }
      if (pts.empty()) continue;
      if (pass == 0) {
        std::vector<Pt> poly = pts;
        poly.push_back(Pt{pts.back().x, base});
        poly.push_back(Pt{pts.front().x, base});
        sf.poly(poly, by_shade(o) ? fill_ink(o, c, s) : Ink(c, panel_color(), 2));
        continue;
      }
      for (std::size_t k = 0; k < pts.size(); k++) {
        std::size_t i = static_cast<std::size_t>(idx[k]);
        if (has_whisker(o, s, i)) whisker(p, p.Y(o.err_lo[s][i]), p.Y(o.err_hi[s][i]), pts[k].x, false);
      }
      for (std::size_t k = 0; k + 1 < pts.size(); k++)
        sf.line(pts[k].x, pts[k].y, pts[k + 1].x, pts[k + 1].y, Ink(c), width);
      for (std::size_t k = 0; k < pts.size(); k++) {
        if (p.fine) {
          sf.marker(pts[k].x, pts[k].y, static_cast<int>(s), mr + 1, Ink(panel_color()));
          sf.marker(pts[k].x, pts[k].y, static_cast<int>(s), mr, Ink(c));
        }
        p.remember(static_cast<int>(s), idx[k], pts[k].x, pts[k].y);
        if (o.values && (ds.series.size() == 1 || n <= 8))
          p.label(pts[k].x, pts[k].y - p.sy * 0.75 - mr, fmt_point(se, cell_at(ds, s, static_cast<std::size_t>(idx[k])), o.prec),
                  ds.series.size() == 1 ? S.value : c);
      }
    }
  }
  return p;
}

// ---- scatter -----------------------------------------------------------------

Plot draw_scatter(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o) {
  std::size_t n = ds.nrows();
  const Series *xs = nullptr;
  std::size_t start = 0;
  if (ds.series.size() >= 2 && (hidden_series(ds.series[0]) || o.xy)) {
    xs = &ds.series[0];
    start = 1;
  }

  double xlo = 1e300, xhi = -1e300, ylo = 1e300, yhi = -1e300;
  auto xval = [&](std::size_t i) {
    return xs ? (i < xs->v.size() ? xs->v[i] : std::nan("")) : static_cast<double>(i + 1);
  };
  for (std::size_t s = start; s < ds.series.size(); s++)
    for (std::size_t i = 0; i < n; i++) {
      double y = cell_at(ds, s, i), x = xval(i);
      if (!std::isfinite(y) || !std::isfinite(x)) continue;
      xlo = std::min(xlo, x);
      xhi = std::max(xhi, x);
      ylo = std::min(ylo, y);
      yhi = std::max(yhi, y);
    }
  if (xhi < xlo) { xlo = ylo = 0; xhi = yhi = 1; }
  whisker_bounds(o, ylo, yhi);
  double padx = (xhi - xlo) * 0.04, pady = (yhi - ylo) * 0.06;
  xlo -= padx; xhi += padx;
  ylo -= pady; yhi += pady;
  if (o.has_lo) ylo = o.lo;
  if (o.has_hi) yhi = o.hi;
  if (!(xhi > xlo)) xhi = xlo + 1;
  std::vector<double> ticks = nice_range(ylo, yhi, std::max(3, std::min(9, r.h / 3)), o.has_lo, o.has_hi);

  Plot p = make_axes(sc, r, clip, ylo, yhi, ticks, o, nullptr, true, xlo, xhi);
  double mr = p.fine ? (n > 60 ? 2 : 3.5) : 0;
  for (std::size_t s = start; s < ds.series.size(); s++) {
    uint8_t c = static_cast<uint8_t>(ds.series[s].color);
    for (std::size_t i = 0; i < n; i++) {
      double y = cell_at(ds, s, i), x = xval(i);
      if (!std::isfinite(y) || !std::isfinite(x)) continue;
      double px = p.ox + (x - xlo) / (xhi - xlo) * (p.pw - 1), py = p.Y(y);
      if (has_whisker(o, s, i)) whisker(p, p.Y(o.err_lo[s][i]), p.Y(o.err_hi[s][i]), px, false);
      if (p.fine) {
        p.sf->marker(px, py, static_cast<int>(s - start), mr + 1, Ink(panel_color()));
        p.sf->marker(px, py, static_cast<int>(s - start), mr, Ink(c));
      } else {
        p.sf->set(static_cast<int>(px), static_cast<int>(std::min<double>(py, p.ph - 1)), Ink(c));
      }
      p.remember(static_cast<int>(s), static_cast<int>(i), px, py);
    }
  }
  return p;
}

// ---- pie ---------------------------------------------------------------------

struct Slice {
  std::string name;
  double val = 0;
  int color = 7;
  int row = 0;          // where it came from, for annotations and the cursor
  double a0 = 0, a1 = 0; // fraction of a turn, clockwise from 12 o'clock
  double cx = 0, cy = 0; // its own centre: an exploded slice is moved out
};

Plot draw_pie(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o, bool solid, double hole) {
  Plot p;
  p.sc = &sc;
  p.clip = clip;
  p.fine = sc.mode().pixel;
  p.sx = sc.mode().sx();
  p.sy = sc.mode().sy();

  std::vector<Slice> sl;
  double total = 0;
  const bool by_row = ds.series.size() == 1 || (ds.series.size() == 2 && hidden_series(ds.series[0]));
  if (by_row) {
    const Series &se = ds.series.back();
    for (std::size_t i = 0; i < se.v.size(); i++) {
      if (!std::isfinite(se.v[i]) || se.v[i] <= 0) continue;
      Slice s;
      s.name = i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1);
      s.val = se.v[i];
      s.row = static_cast<int>(i);
      sl.push_back(s);
      total += s.val;
    }
    std::vector<int> cols = palette_cols(o.palette, sl.size());
    for (std::size_t i = 0; i < sl.size(); i++) {
      int pin = pinned_color(o, sl[i].name, i);
      sl[i].color = pin >= 0 ? pin : cols[i];
    }
  } else {
    for (std::size_t k = 0; k < ds.series.size(); k++) {
      double v = 0;
      for (double x : ds.series[k].v)
        if (std::isfinite(x) && x > 0) v += x;
      if (v <= 0 || hidden_series(ds.series[k])) continue;
      Slice e;
      e.name = ds.series[k].name;
      e.val = v;
      e.color = ds.series[k].color;
      e.row = static_cast<int>(k);
      sl.push_back(e);
      total += v;
    }
  }
  if (sl.empty() || total <= 0) {
    sc.cv.text_c(r.x, r.y + r.h / 2, r.w, "(no positive values for a pie)", S.subtitle);
    return p;
  }
  double acc = 0;
  for (auto &s : sl) {
    s.a0 = acc;
    acc += s.val / total;
    s.a1 = acc;
  }
  sl.back().a1 = 1.0;

  int explode = -1;
  int want = o.cur_index >= 0 ? -3 : o.explode;
  if (want == -1) {
    double best = -1;
    for (std::size_t i = 0; i < sl.size(); i++)
      if (sl[i].val > best) { best = sl[i].val; explode = static_cast<int>(i); }
  } else if (want == -3) {
    for (std::size_t i = 0; i < sl.size(); i++)
      if (sl[i].row == o.cur_index) explode = static_cast<int>(i);
  } else if (want >= 0) {
    for (std::size_t i = 0; i < sl.size(); i++)
      if (sl[i].row == want) explode = static_cast<int>(i);
  }

  // legend first: it decides how much room the pie gets
  std::vector<LegEntry> leg;
  for (std::size_t i = 0; i < sl.size(); i++)
    leg.push_back({sl[i].name, sl[i].color, fmt_val(sl[i].val, o.prec) + "  " + fmt_pct(sl[i].val / total, 1),
                   by_shade(o) ? static_cast<int>(i % 4) : 0});
  Rect area = r;
  if (o.legend) {
    int legw = legend_width(leg) + 1;
    if (r.w - legw >= 22 && static_cast<int>(leg.size()) <= r.h) {
      int ly = r.y + std::max(0, (r.h - static_cast<int>(leg.size())) / 2);
      legend_draw(sc, Rect{r.right() - legw + 1, ly, legw, r.h - (ly - r.y)}, leg, true);
      area.w = r.w - legw - 2;
    } else {
      for (auto &e : leg) e.val = fmt_pct(sl[static_cast<std::size_t>(&e - &leg[0])].val / total, 0);
      int rows = std::min(std::max(1, r.h / 3), legend_rows_needed(leg, r.w));
      legend_draw(sc, Rect{r.x, r.bottom() - rows + 1, r.w, rows}, leg, false);
      area.h = r.h - rows - 1;
    }
  }
  if (area.w < 8 || area.h < 4) return p;

  p.cells = area;
  p.clip = area;
  p.sf = &sc.surface(area);
  Surface &sf = *p.sf;
  p.pw = sf.w();
  p.ph = sf.h();
  const double asp = sf.aspect();
  const double tilt = solid ? 0.56 : 1.0;
  double dp = 0; // wall height, pixels
  if (solid && o.depth > 0) dp = p.fine ? o.depth * 9.0 : std::max(1.0, o.depth * (p.sy == 2 ? 1.0 : 0.5));
  double margin = p.fine ? 6 : 1;
  double room_x = sf.w() / 2.0 - margin, room_y = (sf.h() - dp) / 2.0 - margin / asp;
  double R = std::min(room_x, room_y * asp / tilt);
  if (explode >= 0) R /= 1.12;
  if (R < 2) return p;
  const double ry = R * tilt / asp;
  const double cx = sf.w() / 2.0, cy = (sf.h() - dp) / 2.0;

  for (std::size_t i = 0; i < sl.size(); i++) {
    sl[i].cx = cx;
    sl[i].cy = cy;
    if (static_cast<int>(i) == explode) {
      double ang = (sl[i].a0 + sl[i].a1) * PI - PI / 2;
      sl[i].cx += std::cos(ang) * R * 0.13;
      sl[i].cy += std::sin(ang) * ry * 0.13;
    }
  }

  // Who owns each pixel of the top face.
  const int W = sf.w(), H = sf.h();
  std::vector<int> own(static_cast<std::size_t>(W) * H, -1);
  auto in_ring = [&](int x, int y, double ox, double oy, double &f) {
    double dx = (x + 0.5 - ox) / R, dy = (y + 0.5 - oy) / ry;
    double rr = dx * dx + dy * dy;
    if (rr > 1.0 || rr < hole * hole) return false;
    f = (std::atan2(dy, dx) + PI / 2) / (2 * PI);
    if (f < 0) f += 1;
    return true;
  };
  auto probe = [&](int x, int y) -> int {
    double f = 0;
    if (explode >= 0) { // the moved slice gets first refusal, inside its own wedge
      const Slice &e = sl[static_cast<std::size_t>(explode)];
      if (in_ring(x, y, e.cx, e.cy, f) && f >= e.a0 && f < e.a1) return explode;
    }
    if (!in_ring(x, y, cx, cy, f)) return -1;
    for (std::size_t k = 0; k < sl.size(); k++)
      if (f >= sl[k].a0 && f < sl[k].a1) return static_cast<int>(k) == explode ? -1 : static_cast<int>(k);
    return -1;
  };
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) own[static_cast<std::size_t>(y) * W + x] = probe(x, y);
  auto owner = [&](int x, int y) { return (x < 0 || y < 0 || x >= W || y >= H) ? -1 : own[static_cast<std::size_t>(y) * W + x]; };

  // The wall: every top pixel swept downwards.  The nearest source wins, which
  // draws the rim, the inside of a donut and the cut faces of a moved slice.
  const int idp = static_cast<int>(std::lround(dp));
  std::vector<int> wall(own.size(), -1);
  if (idp > 0)
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        if (owner(x, y) >= 0) continue;
        for (int dz = 1; dz <= idp; dz++) {
          int s = owner(x, y - dz);
          if (s >= 0) { wall[static_cast<std::size_t>(y) * W + x] = s; break; }
        }
      }

  const Ink edge(panel_color());
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int s = own[static_cast<std::size_t>(y) * W + x], ws = wall[static_cast<std::size_t>(y) * W + x];
      if (s >= 0) {
        // A black seam between slices, and along the rim where the wall starts.
        int rgt = owner(x + 1, y), dn = owner(x, y + 1);
        bool rim = dn < 0 && y + 1 < H && wall[static_cast<std::size_t>(y + 1) * W + x] >= 0;
        if (p.fine && ((rgt >= 0 && rgt != s) || (dn >= 0 && dn != s) || rim)) sf.set(x, y, edge);
        else sf.set(x, y, fill_ink(o, sl[static_cast<std::size_t>(s)].color, static_cast<std::size_t>(s)));
      } else if (ws >= 0) {
        int right = (x + 1 < W) ? wall[static_cast<std::size_t>(y) * W + x + 1] : -1;
        if (p.fine && right >= 0 && right != ws) sf.set(x, y, edge);
        else if (by_shade(o)) sf.set(x, y, Ink(15, 0, 3));
        else sf.set(x, y, side_ink(static_cast<uint8_t>(sl[static_cast<std::size_t>(ws)].color)));
      }
    }

  // percentages inside the fat slices; anchors for annotations
  const double lr = hole > 0 ? (1 + hole) / 2 : 0.64;
  for (std::size_t i = 0; i < sl.size(); i++) {
    double frac = sl[i].val / total;
    double ang = (sl[i].a0 + sl[i].a1) * PI - PI / 2;
    double px = sl[i].cx + std::cos(ang) * R * lr, py = sl[i].cy + std::sin(ang) * ry * lr;
    p.remember(by_row ? -1 : sl[i].row, by_row ? sl[i].row : -1, sl[i].cx + std::cos(ang) * R * 0.9,
               sl[i].cy + std::sin(ang) * ry * 0.9, ang);
    if (frac < (p.fine ? 0.04 : 0.07) || R < (p.fine ? 40 : 8)) continue;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f%%", frac * 100);
    uint8_t c = static_cast<uint8_t>(sl[i].color);
    if (by_shade(o)) p.label(px, py, buf, 15, 0);
    else p.label(px, py, buf, contrast_on(c), p.fine ? -1 : c);
  }
  return p;
}

// ---- table -------------------------------------------------------------------

// Columns in the file's own order, words as well as numbers.  A number keeps
// the decimals the file wrote it with (59.0 stays 59.0), unless prec says.
void draw_table(Scene &sc, Rect r, Dataset &ds, const RenderOpts &o) {
  Canvas &cv = sc.cv;
  const std::size_t n = ds.nrows();
  struct Col {
    std::string head;
    int order;
    const Series *s;
    const TextColumn *t;
    int w;
    int dec;
  };
  std::vector<Col> cols;
  const int last = 1 << 20;
  for (const auto &s : ds.series) cols.push_back({hidden_series(s) ? ds.x_name : s.name, s.col > 0 ? s.col : last, &s, nullptr, 0, s.decimals});
  for (const auto &t : ds.text) cols.push_back({t.name, t.col > 0 ? t.col : last, nullptr, &t, 0, -1});
  std::stable_sort(cols.begin(), cols.end(), [](const Col &a, const Col &b) { return a.order < b.order; });
  if (n == 0 || cols.empty()) return;

  // JSON does not say how a number was written: use as few decimals as the
  // column needs (at most 3), the same for every row.
  for (auto &c : cols) {
    if (!c.s || c.s->decimals >= 0 || o.prec >= 0) continue;
    int d = 0;
    for (double v : c.s->v)
      while (std::isfinite(v) && d < 3 && std::fabs(v * std::pow(10, d) - std::round(v * std::pow(10, d))) > 1e-6) d++;
    c.dec = d;
  }
  auto cell = [&](const Col &c, std::size_t i) -> std::string {
    if (c.t) return i < c.t->v.size() ? c.t->v[i] : "";
    double v = i < c.s->v.size() ? c.s->v[i] : std::nan("");
    return std::isfinite(v) ? fmt_val(v, o.prec >= 0 ? o.prec : c.dec) : "-";
  };
  auto label = [&](std::size_t i) { return i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1); };

  int w0 = static_cast<int>(cp_len(ds.label_name));
  for (std::size_t i = 0; i < n; i++) w0 = std::max(w0, static_cast<int>(cp_len(label(i))));
  w0 = std::min(w0, 20);
  for (auto &c : cols) {
    c.w = std::max(3, static_cast<int>(cp_len(c.head)));
    for (std::size_t i = 0; i < n; i++) c.w = std::max(c.w, static_cast<int>(cp_len(cell(c, i))));
    c.w = std::min(c.w, c.t ? 24 : 16);
  }
  std::size_t shown = cols.size();
  auto width = [&]() {
    int t = w0;
    for (std::size_t c = 0; c < shown; c++) t += cols[c].w + 2;
    return t;
  };
  while (shown > 1 && width() > r.w) shown--;
  const int x0 = r.x + std::max(0, (r.w - width()) / 2);

  // text reads from the left, numbers line up on the right
  auto put = [&](const Col &c, int x, int y, const std::string &s, uint8_t fg) {
    std::string t = trunc_to(s, static_cast<std::size_t>(c.w));
    if (c.t) cv.text(x + 2, y, t, fg);
    else cv.text_r(x + 2, y, c.w, t, fg);
  };
  int y = r.y;
  int x = x0 + w0;
  cv.text(x0, y, trunc_to(ds.label_name, static_cast<std::size_t>(w0)), S.table_head);
  for (std::size_t c = 0; c < shown; c++) {
    put(cols[c], x, y, cols[c].head, S.table_head);
    x += cols[c].w + 2;
  }
  y++;
  cv.hline(x0, y, std::min(width(), r.w), G.h, S.table_rule);
  y++;
  int room = r.bottom() - y + 1;
  std::size_t show = n;
  if (static_cast<int>(n) > room) show = static_cast<std::size_t>(std::max(0, room - 1));
  for (std::size_t i = 0; i < show; i++, y++) {
    cv.text(x0, y, trunc_to(label(i), static_cast<std::size_t>(w0)), S.label);
    x = x0 + w0;
    for (std::size_t c = 0; c < shown; c++) {
      put(cols[c], x, y, cell(cols[c], i), S.table_row);
      x += cols[c].w + 2;
    }
  }
  if (show < n) {
    cv.text(x0, y, "... " + std::to_string(n - show) + " more rows", S.subtitle);
    sc.fit_note("the table shows " + std::to_string(show) + " of " + std::to_string(n) + " rows; give it more height or split it");
  }
  if (shown < cols.size()) {
    cv.text_r(r.x, r.y, r.w, "+" + std::to_string(cols.size() - shown) + " cols", S.subtitle);
    sc.fit_note("the table shows " + std::to_string(shown) + " of " + std::to_string(cols.size()) +
                " columns; give it more width or use series_col");
  }
}

// ---- histogram ---------------------------------------------------------------

Plot draw_hist(Scene &sc, Rect r, Rect clip, Dataset &ds, const RenderOpts &o) {
  const Series &s = ds.series[hidden_series(ds.series[0]) && ds.series.size() > 1 ? 1 : 0];
  double lo = 1e300, hi = -1e300;
  std::size_t cnt = 0;
  for (double v : s.v) {
    if (!std::isfinite(v)) continue;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
    cnt++;
  }
  if (cnt == 0) return Plot();
  if (!(hi > lo)) hi = lo + 1;
  // Buckets on round numbers (0, 2, 4 ... not 0.7, 2.46, 4.22), about as many
  // as asked for.  All in halves, so a range as wide as the doubles themselves
  // cannot overflow on the way.
  const int want = std::max(1, std::min(60, o.bins));
  const double half = hi / 2 - lo / 2;
  double step = nice_step(half / want); // half a bucket
  if (!(step > 0) || !std::isfinite(step)) step = half / want;
  double start = std::floor(lo / 2 / step) * step; // half the first edge
  if (!std::isfinite(start)) start = lo / 2;
  const double need = std::ceil((hi / 2 - start) / step - 1e-9);
  int bins = std::isfinite(need) ? static_cast<int>(std::max(1.0, std::min(60.0, need))) : want;
  if (bins < 60 && start + bins * step <= hi / 2) bins++; // buckets are [a, b): the top value needs its own

  Dataset h;
  Series hs;
  hs.name = "count";
  hs.color = s.color;
  hs.v.assign(static_cast<std::size_t>(bins), 0);
  for (double v : s.v) {
    if (!std::isfinite(v)) continue;
    double f = (v / 2 - start) / step;
    if (!std::isfinite(f)) f = 0;
    int b = std::max(0, std::min(bins - 1, static_cast<int>(std::max(0.0, std::min<double>(bins, std::floor(f))))));
    hs.v[static_cast<std::size_t>(b)] += 1;
  }
  for (int b = 0; b < bins; b++) {
    double edge = (start + b * step) * 2;
    h.labels.push_back(fmt_axis(std::isfinite(edge) ? edge : (start + b * step)));
  }
  h.series.push_back(hs);

  RenderOpts o2 = o;
  o2.has_lo = o2.has_hi = false;
  o2.cur_index = o2.cur_series = -1;
  if (o2.xlabel.empty()) o2.xlabel = s.name;
  if (o2.ylabel.empty()) o2.ylabel = "count";
  return draw_bars(sc, r, clip, h, o2, false, true);
}

// ---- annotations -------------------------------------------------------------

void draw_notes(Scene &sc, Plot &p, const Dataset &ds, const RenderOpts &o, bool pie) {
  if (!p.sf) return;
  Surface &sf = *p.sf;
  std::vector<Box> placed;

  auto row_of = [&](const Annotation &a) -> int {
    if (!a.label.empty())
      for (std::size_t i = 0; i < ds.labels.size(); i++)
        if (ieq(trim(ds.labels[i]), trim(a.label))) return static_cast<int>(i);
    if (a.index >= 0 && a.index < static_cast<int>(ds.nrows())) return a.index;
    return -1;
  };
  auto series_of = [&](const Annotation &a) -> int {
    if (!a.series.empty())
      for (std::size_t i = 0; i < ds.series.size(); i++)
        if (ieq(trim(ds.series[i].name), trim(a.series))) return static_cast<int>(i);
    return a.series_i;
  };
  auto text_box = [&](double x, double y, const std::vector<std::string> &lines, uint8_t fg, uint8_t bg) {
    std::size_t w = 0;
    for (const auto &l : lines) w = std::max(w, cp_len(l));
    for (std::size_t k = 0; k < lines.size(); k++)
      sc.text(x, y + k, " " + pad_right(lines[k], w) + " ", fg, bg);
  };

  for (const Annotation &a : o.notes) {
    uint8_t fg = S.note_fg, bg = S.note_bg;
    if (a.color >= 0) { bg = static_cast<uint8_t>(a.color); fg = contrast_on(bg); }
    std::vector<std::string> lines = wrap_words(a.text, 24);
    std::size_t tw = 0;
    for (const auto &l : lines) tw = std::max(tw, cp_len(l));
    Box b;
    b.w = static_cast<double>(tw) + 2;
    b.h = static_cast<double>(lines.size());

    // On a chart laid on its side a value line stands up and a category
    // line lies down: swap them and draw the other one.
    if (p.across && (a.kind == Annotation::HLINE || a.kind == Annotation::VLINE)) {
      int at = -1;
      if (a.kind == Annotation::HLINE) {
        if (a.value < p.lo || a.value > p.hi) continue;
        at = static_cast<int>(std::lround(p.X(a.value)));
      } else {
        int row = row_of(a);
        if (row < 0 || row >= static_cast<int>(p.slot_cy.size())) continue;
        at = static_cast<int>(p.slot_cy[static_cast<std::size_t>(row)]);
      }
      const bool up = a.kind == Annotation::HLINE;
      uint8_t lc = a.color >= 0 ? static_cast<uint8_t>(a.color) : S.note_bg;
      for (int t = 0; t < (up ? p.ph : p.pw); t++)
        if (!p.fine || (t / 6) % 2 == 0) {
          if (up) { sf.set(at, t, Ink(lc)); if (p.fine) sf.set(at + 1, t, Ink(lc)); }
          else { sf.set(p.ox + t, at, Ink(lc)); if (p.fine) sf.set(p.ox + t, at - 1, Ink(lc)); }
        }
      if (!a.text.empty()) {
        if (up) {
          b.x = p.cellx(at) + 0.5;
          if (b.x + b.w > p.clip.right() + 1) b.x = p.cellx(at) - b.w - 0.5;
          b.y = p.cells.y;
        } else {
          b.x = p.cellx(p.ox + p.pw) - b.w;
          b.y = p.celly(at) - b.h - (p.fine ? 0.15 : 0);
          if (b.y < p.clip.y) b.y = p.celly(at) + 0.2;
        }
        if (!p.fine) { b.x = std::floor(b.x + 0.5); b.y = std::floor(b.y + 0.5); }
        text_box(b.x, b.y, lines, fg, bg);
        placed.push_back(b);
      }
      continue;
    }
    if (a.kind == Annotation::HLINE && !pie) {
      if (a.value < p.lo || a.value > p.hi) continue;
      int y = static_cast<int>(std::lround(p.Y(a.value)));
      uint8_t lc = a.color >= 0 ? static_cast<uint8_t>(a.color) : S.note_bg;
      if (!p.fine) y = std::min(y, p.ph - 1);
      for (int x = p.ox; x < p.ox + p.pw; x++)
        if (p.fine ? (x / 6) % 2 == 0 : (x % 2 == 0 && !sf.touched(x, y))) {
          sf.set(x, y, Ink(lc));
          if (p.fine) sf.set(x, y - 1, Ink(lc));
        }
      if (!a.text.empty()) {
        // either end, above or below the line: whichever covers least
        double best = 1e18;
        Box pick = b;
        for (int k = 0; k < 4; k++) {
          Box t = b;
          t.x = (k & 1) ? p.cellx(p.ox) + 0.5 : p.cellx(p.ox + p.pw) - b.w;
          t.y = (k & 2) ? p.celly(y) + 0.2 : p.celly(y) - b.h - (p.fine ? 0.15 : 0);
          if (!p.fine) { t.x = std::floor(t.x); t.y = std::floor(t.y + 0.5); }
          double score = k;
          if (t.y < p.clip.y || t.y + t.h > p.celly(p.ph) + 0.01) score += 1000;
          for (const Box &q : p.taken)
            if (t.hits(q)) score += 50;
          for (const Box &q : placed)
            if (t.hits(q)) score += 200;
          if (score < best) { best = score; pick = t; }
        }
        text_box(pick.x, pick.y, lines, fg, bg);
        placed.push_back(pick);
      }
      continue;
    }
    if (a.kind == Annotation::VLINE && !pie) {
      int x = -1;
      double xv = 0;
      if (p.numeric_x) { // a scatter: "x" is a value on the X axis, not a category
        if (!parse_num(a.label, xv) || xv < p.xlo || xv > p.xhi || !(p.xhi > p.xlo)) continue;
        x = static_cast<int>(std::lround(p.ox + (xv - p.xlo) / (p.xhi - p.xlo) * (p.pw - 1)));
      } else {
        int row = row_of(a);
        if (row < 0 || row >= static_cast<int>(p.slot_cx.size())) continue;
        x = static_cast<int>(p.slot_cx[static_cast<std::size_t>(row)]);
      }
      uint8_t lc = a.color >= 0 ? static_cast<uint8_t>(a.color) : S.note_bg;
      for (int y = 0; y < p.ph; y++)
        if (!p.fine || (y / 6) % 2 == 0) {
          sf.set(x, y, Ink(lc));
          if (p.fine) sf.set(x + 1, y, Ink(lc));
        }
      if (!a.text.empty()) {
        b.x = p.cellx(x) + 0.5;
        if (b.x + b.w > p.clip.right() + 1) b.x = p.cellx(x) - b.w - 0.5;
        b.y = p.cells.y;
        if (!p.fine) b.x = std::floor(b.x + 0.5);
        text_box(b.x, b.y, lines, fg, bg);
        placed.push_back(b);
      }
      continue;
    }
    if (a.kind == Annotation::NOTE) {
      b.x = p.cells.x + a.fx * std::max(0.0, p.cells.w - b.w);
      b.y = p.cells.y + a.fy * std::max(0.0, p.cells.h - b.h);
      if (!p.fine) { b.x = std::floor(b.x + 0.5); b.y = std::floor(b.y + 0.5); }
      text_box(b.x, b.y, lines, fg, bg);
      placed.push_back(b);
      continue;
    }
    if (a.kind != Annotation::POINT) continue;

    int row = row_of(a), ser = series_of(a);
    const Anchor *an = nullptr;
    if (pie) {
      an = p.find(-1, row);
      if (!an && ser >= 0)
        for (const auto &q : p.anchors)
          if (q.s == ser) an = &q;
      if (!an && row >= 0)
        for (const auto &q : p.anchors)
          if (q.s == row) an = &q;
    } else {
      if (row < 0) continue;
      an = p.find(ser, row);
      if (!an) an = p.find(-1, row);
    }
    if (!an) continue;

    // Try the corners around the point, nearest first; keep the one that
    // stays in the panel and off the callouts already down.
    double ax = p.cellx(an->p.x), ay = p.celly(an->p.y);
    double best_score = 1e18;
    Box best = b;
    static const double OFF[12][2] = {{1, -1},     {-1, -1},    {0, -1},   {1, -2.2}, {-1, -2.2}, {0, -2.2},
                                     {1.6, -1.6}, {-1.6, -1.6}, {1, 1},    {-1, 1},   {0, 1},     {1, -3.4}};
    const bool sideways = !pie && std::isfinite(an->dir); // a horizontal bar: go off its end
    for (int k = sideways ? -2 : 0; k < 12; k++) {
      if (k < 0) {
        Box t = b;
        t.x = ax + (k == -2 ? 2.5 : 10);
        t.y = ay - b.h / 2;
        double score = (k + 2) * 0.5;
        if (t.x + t.w > p.clip.right() + 1) score += 500;
        for (const Box &q : placed)
          if (t.hits(q)) score += 200;
        for (const Box &q : p.taken)
          if (t.hits(q)) score += 40;
        if (score < best_score) { best_score = score; best = t; }
        continue;
      }
      double ux = OFF[k][0], uy = OFF[k][1];
      if (pie && std::isfinite(an->dir)) { // push away from the middle of the pie
        double c = std::cos(an->dir), s = std::sin(an->dir);
        if (ux * c + uy * s < -0.2) continue;
      }
      Box t = b;
      t.x = ux > 0 ? ax + 2.5 * ux : (ux < 0 ? ax + 2.5 * ux - b.w : ax - b.w / 2);
      t.y = uy > 0 ? ay + 1.4 * uy : ay + 2.0 * uy - b.h + 0.4;
      double score = k * 0.5;
      if (t.x < p.clip.x) { score += (p.clip.x - t.x) * 50; t.x = p.clip.x; }
      if (t.x + t.w > p.clip.right() + 1) { score += (t.x + t.w - p.clip.right() - 1) * 50; t.x = p.clip.right() + 1 - t.w; }
      if (t.y < p.clip.y) { score += (p.clip.y - t.y) * 50; t.y = p.clip.y; }
      if (t.y + t.h > p.clip.bottom() + 1) { score += (t.y + t.h - p.clip.bottom() - 1) * 50; t.y = p.clip.bottom() + 1 - t.h; }
      for (const Box &q : placed)
        if (t.hits(q)) score += 200;
      for (const Box &q : p.taken)
        if (t.hits(q)) score += 40;
      if (t.x <= ax && ax <= t.x + t.w && t.y <= ay && ay <= t.y + t.h) score += 500; // on top of its own point
      if (score < best_score) { best_score = score; best = t; }
    }
    if (!p.fine) { best.x = std::floor(best.x + 0.5); best.y = std::floor(best.y + 0.5); }

    // leader: from the point to the nearest spot on the box
    double bx = std::max(best.x, std::min(ax, best.x + best.w)), by = std::max(best.y, std::min(ay, best.y + best.h));
    double lx = (bx - p.cells.x) * p.sx, ly = (by - p.cells.y) * p.sy;
    if (p.fine) sf.line(an->p.x + 1, an->p.y + 1, lx + 1, ly + 1, Ink(panel_color()));
    sf.line(an->p.x, an->p.y, lx, ly, Ink(bg));
    if (p.fine) {
      sf.disc(an->p.x, an->p.y, 4, Ink(panel_color()));
      sf.disc(an->p.x, an->p.y, 3, Ink(bg));
    }
    text_box(best.x, best.y, lines, fg, bg);
    placed.push_back(best);
  }
}

// Take the columns "errors" names out of the data and hand them to the
// renderers as whisker ends of the series that stay.  False = nothing to do.
bool split_errors(const Dataset &ds, RenderOpts &o, Dataset &view) {
  if (o.errors.empty()) return false;
  auto find = [&](const std::string &name) {
    for (std::size_t i = 0; i < ds.series.size(); i++)
      if (!hidden_series(ds.series[i]) && ieq(trim(ds.series[i].name), trim(name))) return static_cast<int>(i);
    return -1;
  };
  std::vector<bool> used(ds.series.size(), false);
  for (const auto &e : o.errors) {
    int a = find(e.lo), b = e.plus_minus ? a : find(e.hi);
    if (a >= 0 && b >= 0) used[static_cast<std::size_t>(a)] = used[static_cast<std::size_t>(b)] = true;
  }
  const std::size_t n = ds.nrows();
  std::vector<std::vector<double>> lo(ds.series.size()), hi(ds.series.size());
  for (const auto &e : o.errors) {
    int a = find(e.lo), b = e.plus_minus ? a : find(e.hi), t = e.series.empty() ? -1 : find(e.series);
    if (a < 0 || b < 0) continue;
    if (e.series.empty())
      for (std::size_t i = 0; i < ds.series.size() && t < 0; i++)
        if (!hidden_series(ds.series[i]) && !used[i]) t = static_cast<int>(i);
    if (t < 0 || used[static_cast<std::size_t>(t)]) continue;
    auto &L = lo[static_cast<std::size_t>(t)], &H = hi[static_cast<std::size_t>(t)];
    L.assign(n, std::nan(""));
    H.assign(n, std::nan(""));
    for (std::size_t i = 0; i < n; i++) {
      double v = cell_at(ds, static_cast<std::size_t>(t), i), x = cell_at(ds, static_cast<std::size_t>(a), i),
             y = cell_at(ds, static_cast<std::size_t>(b), i);
      if (e.plus_minus) { L[i] = v - std::fabs(x); H[i] = v + std::fabs(x); }
      else { L[i] = std::min(x, y); H[i] = std::max(x, y); }
    }
  }
  view = ds;
  view.series.clear();
  int cur = -1;
  for (std::size_t i = 0; i < ds.series.size(); i++) {
    if (used[i]) continue;
    if (static_cast<int>(i) == o.cur_series) cur = static_cast<int>(view.series.size());
    view.series.push_back(ds.series[i]);
    o.err_lo.push_back(lo[i]);
    o.err_hi.push_back(hi[i]);
  }
  if (o.cur_series >= 0) o.cur_series = cur;
  return true;
}

} // namespace

// ---- entry point -------------------------------------------------------------

void render_chart(Scene &sc, Rect r, Dataset &data, const RenderOpts &opts) {
  if (r.w < 8 || r.h < 4) return;
  // Error columns come out of what is drawn; the rest is drawn from a copy
  // and its colours handed back for the sheet.
  RenderOpts o = opts;
  Dataset view;
  const bool split = split_errors(data, o, view);
  Dataset &ds = split ? view : data;
  struct Handback {
    Dataset &from, &to;
    bool on;
    ~Handback() {
      if (!on) return;
      for (auto &t : to.series)
        for (const auto &f : from.series)
          if (f.name == t.name) t.color = f.color;
    }
  } handback{view, data, split};
  Canvas &cv = sc.cv;
  assign_colors(ds, o);
  std::string type = type_canonical(o.type);

  Rect inner = r;
  if (o.frame != "none") {
    if (o.shadow && S.slide_bg != BG_NONE) cv.shadow(r.x, r.y, r.w, r.h);
    if (S.slide_bg != BG_NONE) sc.panel(r, S.panel_bg);
    int st = BOX_DOUBLE;
    if (o.frame == "single") st = BOX_SINGLE;
    else if (o.frame == "heavy") st = BOX_HEAVY;
    else if (o.frame == "ascii") st = BOX_ASCII;
    if (sc.mode().ascii) st = BOX_ASCII;
    cv.box(r.x, r.y, r.w, r.h, st, o.frame_color >= 0 ? static_cast<uint8_t>(o.frame_color) : S.frame);
    inner = Rect{r.x + 2, r.y + 1, r.w - 4, r.h - 2};
  } else if (S.slide_bg != BG_NONE) {
    sc.panel(r, S.panel_bg);
    inner = Rect{r.x + 1, r.y, r.w - 2, r.h};
  }
  if (inner.w < 4 || inner.h < 2) return;

  std::string title = o.title.empty() ? ds.title : o.title;
  if (!title.empty()) {
    if (o.frame != "none" && static_cast<int>(cp_len(title)) + 8 < r.w) {
      cv.text_c(r.x + 2, r.y, r.w - 4, " " + title + " ", S.title);
    } else {
      cv.text_c(inner.x, inner.y, inner.w, title, S.title);
      inner.y++;
      inner.h--;
    }
  }
  if (!o.subtitle.empty() && inner.h > 4) {
    cv.text_c(inner.x, inner.y, inner.w, o.subtitle, S.subtitle);
    inner.y++;
    inner.h--;
  }
  if (!o.source.empty() && o.frame != "none" && r.h > 3) {
    std::string bottom = o.source;
    if (static_cast<int>(cp_len(bottom)) + 6 > r.w) {
      std::size_t s = bottom.find_last_of('/');
      if (s != std::string::npos && s + 1 < bottom.size()) bottom = bottom.substr(s + 1);
    }
    cv.text_c(r.x + 2, r.bottom(), r.w - 4, " " + bottom + " ", S.grid == S.panel_bg ? S.dim : 8);
  }
  if (inner.w < 4 || inner.h < 2) return;
  const Rect clip = inner;

  if (ds.empty()) {
    cv.text_c(inner.x, inner.y + inner.h / 2, inner.w, "(no data)", S.subtitle);
    return;
  }
  if (type == "table") {
    draw_table(sc, inner, ds, o);
    return;
  }

  const bool pie_like = type == "pie" || type == "pie3d" || type == "donut";
  std::size_t visible = 0;
  for (const auto &s : ds.series)
    if (!hidden_series(s)) visible++;
  if (!pie_like && type != "hist" && o.legend && visible > 1) {
    std::vector<LegEntry> leg;
    for (std::size_t i = 0; i < ds.series.size(); i++)
      if (!hidden_series(ds.series[i]))
        leg.push_back({ds.series[i].name, ds.series[i].color, "", by_shade(o) ? static_cast<int>(i % 4) : 0});
    int rows = std::min(std::max(1, inner.h / 4), legend_rows_needed(leg, inner.w));
    if (inner.h - rows >= 6) {
      legend_draw(sc, Rect{inner.x, inner.bottom() - rows + 1, inner.w, rows}, leg, false);
      inner.h -= rows;
    }
  }

  Plot p;
  if (type == "stacked") p = draw_bars(sc, inner, clip, ds, o, true, false);
  else if (type == "hbar") p = draw_hbars(sc, inner, clip, ds, o);
  else if (type == "dumbbell") p = draw_dumbbell(sc, inner, clip, ds, o);
  else if (type == "line") p = draw_lines(sc, inner, clip, ds, o, false);
  else if (type == "area") p = draw_lines(sc, inner, clip, ds, o, true);
  else if (type == "scatter") p = draw_scatter(sc, inner, clip, ds, o);
  else if (type == "hist") p = draw_hist(sc, inner, clip, ds, o);
  else if (type == "pie") p = draw_pie(sc, inner, clip, ds, o, false, 0.0);
  else if (type == "pie3d") p = draw_pie(sc, inner, clip, ds, o, true, 0.0);
  else if (type == "donut") p = draw_pie(sc, inner, clip, ds, o, o.depth > 0, 0.5);
  else p = draw_bars(sc, inner, clip, ds, o, false, false);

  if (!pie_like && p.sf && o.cur_index >= 0) {
    const Anchor *a = p.find(o.cur_series, o.cur_index);
    if (a) ring_cursor(p, a->p.x, a->p.y);
  }
  draw_notes(sc, p, ds, o, pie_like);
}

} // namespace ch
