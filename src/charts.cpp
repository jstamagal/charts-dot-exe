// charts.cpp -- the ANSI chart renderers. 16 colours, block shading, DOS boxes.
#include "chart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "util.hpp"

namespace ch {

Skin S;

static constexpr double PI = 3.14159265358979323846;

// ---- palettes --------------------------------------------------------------

struct Pal {
  const char *name;
  std::vector<int> c;
};

static const std::vector<Pal> &pals() {
  static const std::vector<Pal> p = {
      {"dos", {9, 10, 11, 12, 13, 14, 6, 2, 4, 5, 1, 3, 7, 15}},
      {"ega", {4, 2, 6, 12, 13, 11, 1, 5, 3, 7, 14, 9}},
      {"cga", {14, 13, 11, 15}},
      {"ice", {14, 11, 15, 10, 9, 12, 13, 7}},
      {"fire", {9, 3, 1, 13, 11, 5, 7}},
      {"green", {10, 2, 6, 15, 14, 3, 7}},
      {"amber", {11, 3, 9, 5, 13, 1, 15}},
      {"mono", {15, 7, 15, 7, 15, 7}},
  };
  return p;
}

std::vector<std::string> type_names() {
  return {"bar",     "grouped", "stacked", "hbar",    "line", "area",
          "pie",     "pie3d",   "donut",   "scatter", "hist", "table"};
}

bool type_valid(const std::string &t) {
  for (const auto &x : type_names())
    if (ieq(x, t)) return true;
  return false;
}

std::vector<std::string> palette_names() {
  std::vector<std::string> v;
  for (const auto &p : pals()) v.push_back(p.name);
  return v;
}

std::vector<int> palette_cols(const std::string &name, std::size_t n) {
  const std::vector<int> *src = &pals()[0].c;
  for (const auto &p : pals())
    if (ieq(p.name, name)) { src = &p.c; break; }
  std::vector<int> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; i++) out.push_back((*src)[i % src->size()]);
  return out;
}

static void assign_colors(Dataset &ds, const std::string &pal) {
  std::vector<int> c = palette_cols(pal, ds.series.size());
  for (std::size_t i = 0; i < ds.series.size(); i++) ds.series[i].color = c[i];
}

static char32_t marker_for(std::size_t i) {
  static const char32_t m[] = {U'*', U'o', U'+', U'x', U'#', U'@', U'^', U'~', U'=', U'&'};
  return m[i % (sizeof m / sizeof m[0])];
}

// Without colour (or on the mono palette) the fill glyph has to tell series
// apart on its own, the way shaded charts did before colour was everywhere.
static char32_t series_char(const RenderOpts &o, std::size_t si) {
  if (!o.color || ieq(o.palette, "mono")) return G.blk[si % 4];
  return G.blk[0];
}

static uint8_t contrast_on(uint8_t bg) { return bg >= 8 ? 0 : 15; }

// ---- axes ------------------------------------------------------------------

struct Axes {
  double lo = 0, hi = 1;
  int gx = 0, gy = 0, gw = 1, gh = 1; // plot region (rows gy .. gy+gh-1)
  int ac = 0;                          // axis column
  int by = 0;                          // x-axis row

  int row_of(double v) const {
    if (gh <= 1) return gy;
    double f = (v - lo) / (hi - lo);
    if (!(f > 0)) f = (v > lo) ? f : 0; // also catches NaN -> 0
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return gy + (gh - 1) - static_cast<int>(std::lround(f * (gh - 1)));
  }
  int slot_x(int i, int n) const { return gx + static_cast<int>((long long)i * gw / (n > 0 ? n : 1)); }
  int slot_w(int i, int n) const { return slot_x(i + 1, n) - slot_x(i, n); }
  int col_center(int i, int n) const { return slot_x(i, n) + slot_w(i, n) / 2; }
};

static Axes make_axes(Canvas &cv, Rect r, double lo, double hi, const RenderOpts &o,
                      const std::vector<std::string> *cats, const std::string &ylabel,
                      bool numeric_x, double xlo, double xhi) {
  Axes ax;
  ax.lo = lo;
  ax.hi = hi;

  int cat_rows = 0;
  if (numeric_x) cat_rows = 1;
  else if (cats && !cats->empty()) cat_rows = 1;
  int xlab = o.xlabel.empty() ? 0 : 1;

  ax.gh = r.h - 1 - cat_rows - xlab;
  if (ax.gh < 1) ax.gh = 1;
  ax.gy = r.y;
  ax.by = r.y + ax.gh;

  std::vector<double> ticks = nice_ticks(lo, hi, std::max(2, std::min(10, ax.gh / 3 + 2)));

  int tw = 1;
  for (double t : ticks) tw = std::max(tw, static_cast<int>(cp_len(fmt_axis(t))));

  int yl_cols = (ylabel.empty() || r.w < 20) ? 0 : 2;
  int lw = tw + yl_cols;
  if (lw + 4 > r.w) lw = std::max(1, r.w - 4);
  ax.ac = r.x + lw;
  ax.gx = ax.ac + 1;
  ax.gw = r.right() - ax.gx + 1;
  if (ax.gw < 1) ax.gw = 1;

  if (yl_cols > 0) {
    int len = static_cast<int>(cp_len(ylabel));
    int y0 = ax.gy + (ax.gh + len - 1) / 2;
    int last = ax.gy + ax.gh - 1;
    if (y0 > last) y0 = last;
    cv.vtext(r.x, y0, ylabel, S.ylabel);
  }

  // the spine
  cv.put(ax.ac, ax.gy, G.tl, S.axis);
  if (ax.gh > 1) cv.vline(ax.ac, ax.gy + 1, ax.gh - 1, G.v, S.axis);
  cv.put(ax.ac, ax.by, G.bl, S.axis);
  cv.hline(ax.ac + 1, ax.by, r.right() - ax.ac, G.h, S.axis);

  int last_row = -99;
  for (double t : ticks) {
    int row = ax.row_of(t);
    if (row < ax.gy || row > ax.gy + ax.gh - 1) continue;
    if (row == last_row) continue;
    last_row = row;
    cv.text_r(r.x + yl_cols, row, lw - yl_cols, fmt_axis(t), S.tick);
    cv.put(ax.ac, row, std::fabs(t) < (ax.hi - ax.lo) * 1e-9 ? G.cross : G.lt, S.axis);
    if (o.grid) cv.hline(ax.gx, row, ax.gw, G.dot, S.grid);
  }

  if (cats && !cats->empty() && !numeric_x) {
    int n = static_cast<int>(cats->size());
    for (int i = 0; i < n; i++) {
      int sx = ax.slot_x(i, n), sw = ax.slot_w(i, n);
      const std::string &lab = (*cats)[static_cast<std::size_t>(i)];
      int len = static_cast<int>(cp_len(lab));
      if (sw >= len + 1) {
        cv.text_c(sx, ax.by + 1, sw, lab, S.label);
        cv.put(ax.col_center(i, n), ax.by, G.tt, S.axis);
      } else if (sw >= 3) {
        cv.text_c(sx, ax.by + 1, sw, trunc_to(lab, static_cast<std::size_t>(sw - 1)), S.label);
      } else if (i % 2 == 0) {
        cv.text_c(sx - 1, ax.by + 1, std::max(2, sw * 2), trunc_to(lab, 3), S.label);
      }
    }
  } else if (numeric_x) {
    std::vector<double> xt = nice_ticks(xlo, xhi, std::max(2, std::min(8, ax.gw / 14 + 2)));
    for (double t : xt) {
      double f = (xhi > xlo) ? (t - xlo) / (xhi - xlo) : 0;
      int x = ax.gx + static_cast<int>(std::lround(f * (ax.gw - 1)));
      if (x < ax.gx || x > r.right()) continue;
      std::string lab = fmt_axis(t);
      int len = static_cast<int>(cp_len(lab));
      cv.text_c(x - len / 2, ax.by + 1, len + 1, lab, S.label);
      cv.put(x, ax.by, G.tt, S.axis);
    }
  }

  if (xlab) cv.text_c(r.x, ax.by + 1 + cat_rows, r.w, o.xlabel, S.xlabel);
  return ax;
}

// ---- legend ----------------------------------------------------------------

struct LegEntry {
  std::string name;
  int color = 7;
  std::string val;
  char32_t ch = 0; // 0 = solid block
};

static int legend_width(const std::vector<LegEntry> &e) {
  std::size_t w = 0;
  for (const auto &x : e) {
    std::size_t l = cp_len(x.name) + 4;
    if (!x.val.empty()) l += cp_len(x.val) + 1;
    w = std::max(w, l);
  }
  return static_cast<int>(w);
}

static void legend_draw(Canvas &cv, Rect r, const std::vector<LegEntry> &e, bool vertical) {
  if (e.empty() || r.w < 4 || r.h < 1) return;
  if (vertical) {
    int y = r.y;
    for (const auto &en : e) {
      if (y > r.bottom()) return;
      cv.put(r.x, y, en.ch ? en.ch : G.blk[0], static_cast<uint8_t>(en.color));
      cv.put(r.x + 1, y, en.ch ? en.ch : G.blk[0], static_cast<uint8_t>(en.color));
      int room = r.w - 4;
      if (!en.val.empty()) room -= static_cast<int>(cp_len(en.val)) + 2;
      cv.text(r.x + 3, y, trunc_to(en.name, static_cast<std::size_t>(std::max(1, room))), S.legend);
      if (!en.val.empty() && r.w > static_cast<int>(cp_len(en.val)) + 5)
        cv.text_r(r.x, y, r.w, en.val, S.value);
      y++;
    }
  } else {
    int x = r.x, y = r.y;
    for (const auto &en : e) {
      std::string seg = en.name + (en.val.empty() ? "" : " " + en.val);
      int need = static_cast<int>(cp_len(seg)) + 5;
      if (x + need > r.right() + 1) {
        x = r.x;
        y++;
        if (y > r.bottom()) return;
      }
      cv.put(x, y, en.ch ? en.ch : G.blk[0], static_cast<uint8_t>(en.color));
      cv.put(x + 1, y, en.ch ? en.ch : G.blk[0], static_cast<uint8_t>(en.color));
      cv.text(x + 3, y, seg, S.legend);
      x += need + 1;
    }
  }
}

static std::vector<LegEntry> series_entries(const Dataset &ds, const RenderOpts &o) {
  std::vector<LegEntry> e;
  for (std::size_t i = 0; i < ds.series.size(); i++) {
    if (!ds.series[i].name.empty() && ds.series[i].name[0] == '\x01') continue; // x column
    e.push_back({ds.series[i].name, ds.series[i].color, fmt_val(ds.sum(i)), series_char(o, i)});
  }
  return e;
}

// ---- bars ------------------------------------------------------------------

static void bar3d(Canvas &cv, int bx, int bw, int row_a, int row_b, int color, int depth,
                  char32_t front_ch) {
  if (bw < 1) return;
  if (row_b < row_a) std::swap(row_a, row_b);
  int h = row_b - row_a + 1;
  uint8_t c = static_cast<uint8_t>(color);
  if (front_ch != G.blk[0] || depth <= 0 || bw < 3) {
    // Flat bar: without colour the glyph already carries the series.
    cv.fill(bx, row_a, bw, h, front_ch, c);
    return;
  }
  int front = bw - 1;
  cv.fill(bx, row_a, front, h, G.blk[0], c);
  if (h >= 2) cv.hline(bx, row_a, front, G.blk[2], c); // lit top face
  cv.vline(bx + front, row_a, h, G.blk[1], c);         // shaded right side
}

static void draw_bars(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o, bool stacked) {
  std::size_t n = ds.nrows();
  if (n == 0 || ds.series.empty()) return;
  const std::vector<std::string> *cats = (ds.labels.size() == n) ? &ds.labels : nullptr;

  double lo = 0, hi = 0;
  if (stacked) {
    lo = 0;
    hi = 0;
    for (std::size_t i = 0; i < n; i++) {
      double p = 0, m = 0;
      for (const auto &s : ds.series) {
        double v = i < s.v.size() ? s.v[i] : std::nan("");
        if (!std::isfinite(v)) continue;
        if (v > 0) p += v; else m += v;
      }
      hi = std::max(hi, p);
      lo = std::min(lo, m);
    }
  } else {
    ds.bounds(lo, hi);
  }
  if (o.has_lo) lo = o.lo;
  if (o.has_hi) hi = o.hi;
  else if (!o.has_lo) hi += (hi - lo) * 0.06; // a little air over the tallest bar
  if (lo > 0) lo = 0;
  if (hi < 0) hi = 0;
  if (!(hi > lo)) hi = lo + 1;

  Axes ax = make_axes(cv, r, lo, hi, o, cats, o.ylabel, false, 0, 1);
  int base = ax.row_of(0);
  std::size_t ns = ds.series.size();

  for (std::size_t i = 0; i < n; i++) {
    int sx = ax.slot_x(static_cast<int>(i), static_cast<int>(n));
    int sw = ax.slot_w(static_cast<int>(i), static_cast<int>(n));
    if (sw < 1) continue;

    if (stacked) {
      int bw = sw - 1;
      if (sw >= 4 && bw < 1) bw = sw - 1;
      if (sw < 4) bw = sw;
      double cum = 0;
      for (std::size_t s = 0; s < ns; s++) {
        double v = i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
        if (!std::isfinite(v) || v == 0) continue;
        double a = cum, b = cum + v;
        cum = b;
        int ra = ax.row_of(a), rb = ax.row_of(b);
        if (ra == rb) continue;
        bar3d(cv, sx, bw, ra, rb, ds.series[s].color, o.depth, series_char(o, s));
      }
      if (o.values && std::fabs(cum) > 0) {
        std::string lab = fmt_val(cum, o.prec);
        int lw = static_cast<int>(cp_len(lab));
        int top = ax.row_of(cum);
        int row = top - 1 >= ax.gy ? top - 1 : top + 1;
        if (row <= ax.by - 1 && lw <= sw) cv.text(sx + (sw - lw) / 2, row, lab, S.value);
      }
    } else {
      int gap = (sw >= 7) ? 1 : 0;
      int inner = sw - gap;
      if (inner < 1) inner = 1;
      for (std::size_t s = 0; s < ns; s++) {
        int bx = sx + static_cast<int>((long long)s * inner / ns);
        int bw = sx + static_cast<int>((long long)(s + 1) * inner / ns) - bx;
        if (bw < 1) bw = 1;
        double v = i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
        if (!std::isfinite(v)) continue;
        int rb = ax.row_of(v);
        if (rb == base) continue;
        bar3d(cv, bx, bw, base, rb, ds.series[s].color, bw >= 3 ? o.depth : 0, series_char(o, s));
        if (o.values) {
          std::string lab = fmt_val(v, o.prec);
          int lw = static_cast<int>(cp_len(lab));
          int top = std::min(base, rb);
          int row = top - 1 >= ax.gy ? top - 1 : top + 1;
          if (row <= ax.by - 1 && lw <= bw) cv.text(bx + (bw - lw) / 2, row, lab, S.value);
        }
      }
    }
  }
}

// ---- horizontal bars -------------------------------------------------------

static void draw_hbars(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o, bool stacked) {
  std::size_t n = ds.nrows();
  if (n == 0 || ds.series.empty()) return;
  std::size_t ns = ds.series.size();

  double hi = 0, lo = 0;
  if (stacked) {
    for (std::size_t i = 0; i < n; i++) {
      double t = 0;
      for (const auto &s : ds.series) {
        double v = i < s.v.size() ? s.v[i] : std::nan("");
        if (std::isfinite(v) && v > 0) t += v;
      }
      hi = std::max(hi, t);
    }
  } else {
    ds.bounds(lo, hi);
  }
  if (o.has_hi) hi = o.hi;
  else if (!o.has_lo) hi += (hi - lo) * 0.06; // a little air over the tallest bar
  if (hi <= 0) hi = 1;

  // left gutter for category names
  std::size_t labw = 0;
  for (std::size_t i = 0; i < n; i++) {
    std::string lab = (i < ds.labels.size()) ? ds.labels[i] : std::to_string(i + 1);
    labw = std::max(labw, cp_len(lab));
  }
  int gutw = static_cast<int>(labw) + 1;
  int maxgut = std::max(4, r.w / 3);
  if (gutw > maxgut) gutw = maxgut;

  int rows_per = 2;
  int gap = 1;
  int need = static_cast<int>(n) * (static_cast<int>(ns) * rows_per + gap);
  if (need > r.h) { rows_per = 1; gap = 0; }
  need = static_cast<int>(n) * (static_cast<int>(ns) * rows_per + gap);
  if (need > r.h) { rows_per = 1; }
  int avail_rows = r.h - 1; // the axis lives on the last row
  int group_h = static_cast<int>(ns) * rows_per + gap;
  if (group_h < 1) group_h = 1;

  int plotx = r.x + gutw;
  int plotw = r.right() - plotx + 1;
  if (plotw < 4) return;

  std::vector<double> ticks = nice_ticks(0, hi, std::max(2, std::min(8, plotw / 12 + 2)));

  int y = r.y;
  for (std::size_t i = 0; i < n && y + group_h - 1 < r.y + avail_rows; i++) {
    int gy = y;
    std::string lab = (i < ds.labels.size()) ? ds.labels[i] : std::to_string(i + 1);
    cv.text_r(r.x, gy, gutw - 1, lab, S.label);
    if (stacked) {
      double cum = 0;
      for (std::size_t s = 0; s < ns; s++) {
        double v = i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
        if (!std::isfinite(v) || v <= 0) continue;
        int x0 = plotx + static_cast<int>(std::lround(cum / hi * (plotw - 1)));
        cum += v;
        int x1 = plotx + static_cast<int>(std::lround(cum / hi * (plotw - 1)));
        x1 = std::min(x1, r.right());
        if (x1 <= x0) continue;
        char32_t fc = series_char(o, s);
        cv.hline(x0, gy, x1 - x0, fc, static_cast<uint8_t>(ds.series[s].color));
        if (rows_per >= 2 && gy + 1 <= r.y + avail_rows - 1)
          cv.hline(x0, gy + 1, std::max(1, x1 - x0 - 1), G.blk[1], static_cast<uint8_t>(ds.series[s].color));
      }
      if (o.values) {
        std::string vl = fmt_val(cum, o.prec);
        int x = plotx + static_cast<int>(std::lround(cum / hi * (plotw - 1))) + 1;
        int len = static_cast<int>(cp_len(vl));
        if (x + len <= r.right() + 1) cv.text(x, gy, vl, S.value);
      }
    } else {
      for (std::size_t s = 0; s < ns; s++) {
        double v = i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
        if (!std::isfinite(v)) continue;
        int brow = gy + static_cast<int>(s) * rows_per;
        if (brow > r.y + avail_rows - 1) break;
        int len = static_cast<int>(std::lround(v / hi * (plotw - 1)));
        if (len < 0) len = 0;
        uint8_t c = static_cast<uint8_t>(ds.series[s].color);
        if (len > 0) {
          cv.hline(plotx, brow, len, series_char(o, s), c);
          if (rows_per >= 2 && brow + 1 <= r.y + avail_rows - 1)
            cv.hline(plotx, brow + 1, std::max(1, len - 1), G.blk[1], c);
        }
        if (o.values) {
          std::string vl = fmt_val(v, o.prec);
          int x = plotx + len + 1;
          int vlen = static_cast<int>(cp_len(vl));
          if (x + vlen <= r.right() + 1) cv.text(x, brow, vl, S.value);
        }
      }
    }
    y += group_h;
  }

  // bottom scale
  int ay = std::min(r.bottom(), y);
  cv.hline(plotx, ay, plotw, G.h, S.axis);
  for (double t : ticks) {
    int x = plotx + static_cast<int>(std::lround(t / hi * (plotw - 1)));
    if (x < plotx || x > r.right()) continue;
    cv.put(x, ay, G.tt, S.axis);
    std::string lab = fmt_axis(t);
    int len = static_cast<int>(cp_len(lab));
    cv.text_c(x - len / 2, ay + 1, len + 1, lab, S.tick);
  }
}

// ---- line / area -----------------------------------------------------------

static void connect(Canvas &cv, int x0, int y0, int x1, int y1, uint8_t color) {
  if (x1 <= x0) return;
  int prev = y0;
  for (int x = x0 + 1; x <= x1; x++) {
    double f = static_cast<double>(x - x0) / (x1 - x0);
    int y = static_cast<int>(std::lround(y0 + f * (y1 - y0)));
    if (y == prev) {
      cv.put(x, y, G.h, color);
    } else {
      int step = (y > prev) ? 1 : -1;
      for (int yy = prev + step; yy != y; yy += step) cv.put(x, yy, G.v, color);
      cv.put(x, y, step > 0 ? U'\\' : U'/', color);
    }
    prev = y;
  }
}

static void draw_lines(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o, bool area) {
  std::size_t n = ds.nrows();
  if (n == 0 || ds.series.empty()) return;
  const std::vector<std::string> *cats = (ds.labels.size() == n) ? &ds.labels : nullptr;

  double lo = 0, hi = 0;
  ds.bounds(lo, hi);
  if (o.has_lo) lo = o.lo;
  if (o.has_hi) hi = o.hi;
  if (area && o.zero_base) {
    if (lo > 0) lo = 0;
    if (hi < 0) hi = 0;
  }
  if (!(hi > lo)) hi = lo + 1;

  Axes ax = make_axes(cv, r, lo, hi, o, cats, o.ylabel, false, 0, 1);
  int base = ax.row_of(0);
  bool do_fill = area && ds.series.size() <= 2;

  for (std::size_t s = 0; s < ds.series.size(); s++) {
    const Series &se = ds.series[s];
    uint8_t c = static_cast<uint8_t>(se.color);
    std::vector<int> px, py;
    for (std::size_t i = 0; i < n; i++) {
      double v = i < se.v.size() ? se.v[i] : std::nan("");
      if (!std::isfinite(v)) continue;
      px.push_back(ax.col_center(static_cast<int>(i), static_cast<int>(n)));
      py.push_back(ax.row_of(v));
    }
    if (px.empty()) continue;

    if (do_fill) {
      char32_t sh = (!o.color || ieq(o.palette, "mono"))
                        ? series_char(o, s)
                        : ((s == 0) ? G.blk[2] : G.blk[3]);
      for (std::size_t k = 0; k + 1 < px.size(); k++) {
        int x0 = px[k], x1 = px[k + 1];
        for (int x = x0; x <= x1; x++) {
          double f = (x1 == x0) ? 0 : static_cast<double>(x - x0) / (x1 - x0);
          int y = static_cast<int>(std::lround(py[k] + f * (py[k + 1] - py[k])));
          int a = std::min(y, base), b = std::max(y, base);
          for (int yy = a; yy <= b; yy++)
            if (yy >= ax.gy && yy < ax.by) cv.put(x, yy, sh, c);
        }
      }
    }

    for (std::size_t k = 0; k + 1 < px.size(); k++) connect(cv, px[k], py[k], px[k + 1], py[k + 1], c);
    for (std::size_t k = 0; k < px.size(); k++) cv.put(px[k], py[k], marker_for(s), c);
  }
}

// ---- scatter ---------------------------------------------------------------

static void draw_scatter(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o) {
  std::size_t n = ds.nrows();
  if (n == 0 || ds.series.empty()) return;

  const Series *xs = nullptr;
  std::size_t start = 0;
  if (ds.series.size() >= 2 && ds.series[0].name == "\x01x") {
    xs = &ds.series[0];
    start = 1;
  } else if (o.xy && ds.series.size() >= 2) {
    xs = &ds.series[0];
    start = 1;
  }

  double xlo = 1e300, xhi = -1e300, ylo = 1e300, yhi = -1e300;
  for (std::size_t s = start; s < ds.series.size(); s++) {
    for (std::size_t i = 0; i < n; i++) {
      if (i >= ds.series[s].v.size()) break;
      double y = ds.series[s].v[i];
      if (!std::isfinite(y)) continue;
      double x = xs ? (i < xs->v.size() ? xs->v[i] : std::nan("")) : static_cast<double>(i);
      if (!std::isfinite(x)) continue;
      xlo = std::min(xlo, x);
      xhi = std::max(xhi, x);
      ylo = std::min(ylo, y);
      yhi = std::max(yhi, y);
    }
  }
  if (xhi < xlo) return;
  if (o.has_lo) ylo = o.lo;
  if (o.has_hi) yhi = o.hi;
  if (!(yhi > ylo)) yhi = ylo + 1;
  if (!(xhi > xlo)) xhi = xlo + 1;

  Axes ax = make_axes(cv, r, ylo, yhi, o, nullptr, o.ylabel, true, xlo, xhi);

  for (std::size_t s = start; s < ds.series.size(); s++) {
    uint8_t c = static_cast<uint8_t>(ds.series[s].color);
    char32_t mk = marker_for(s - start);
    for (std::size_t i = 0; i < n; i++) {
      if (i >= ds.series[s].v.size()) break;
      double y = ds.series[s].v[i];
      if (!std::isfinite(y)) continue;
      double x = xs ? (i < xs->v.size() ? xs->v[i] : std::nan("")) : static_cast<double>(i);
      if (!std::isfinite(x)) continue;
      int cx = ax.gx + static_cast<int>(std::lround((x - xlo) / (xhi - xlo) * (ax.gw - 1)));
      cv.put(cx, ax.row_of(y), mk, c);
    }
  }
}

// ---- pie -------------------------------------------------------------------

struct Slice {
  std::string name;
  double val = 0;
  int color = 7;
  double a0 = 0, a1 = 0; // normalised angle range [0,1) clockwise from 12 o'clock
};

static double norm_angle(double dx, double dy) {
  double a = std::atan2(dy, dx);           // -pi..pi, 0 = +x
  double f = (a + PI / 2) / (2 * PI);      // rotate so 0 = up
  while (f < 0) f += 1;
  while (f >= 1) f -= 1;
  return f;
}

static bool in_ellipse(int x, int y, int cx, int cy, double rx, double ry, double hole) {
  if (rx <= 0 || ry <= 0) return false;
  double dx = (x - cx) / rx;
  double dy = (y - cy) / ry;
  double r = std::sqrt(dx * dx + dy * dy);
  return r <= 1.0 && r >= hole;
}

// widest cells of an ellipse at row y; orr < ol means the row misses it
static void ellipse_span(int y, int cx, int cy, int rx, int ry, int &ol, int &orr) {
  ol = 0;
  orr = -1;
  if (ry <= 0) return;
  double dy = static_cast<double>(y - cy) / ry;
  if (dy < -1 || dy > 1) return;
  double w = rx * std::sqrt(std::max(0.0, 1 - dy * dy));
  ol = cx - static_cast<int>(std::lround(w));
  orr = cx + static_cast<int>(std::lround(w));
}

static void draw_pie(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o, int depth, double hole) {
  std::vector<Slice> sl;
  double total = 0;
  if (ds.series.size() == 1) {
    for (std::size_t i = 0; i < ds.nrows(); i++) {
      double v = i < ds.series[0].v.size() ? ds.series[0].v[i] : 0;
      if (!std::isfinite(v) || v <= 0) continue;
      Slice s;
      s.name = (i < ds.labels.size()) ? ds.labels[i] : std::to_string(i + 1);
      s.val = v;
      s.color = ds.series[0].color;
      sl.push_back(s);
      total += v;
    }
    // give each slice its own colour
    std::vector<int> cols = palette_cols(o.palette, sl.size());
    for (std::size_t i = 0; i < sl.size(); i++) sl[i].color = cols[i];
  } else {
    for (const auto &s : ds.series) {
      double v = 0;
      for (double x : s.v)
        if (std::isfinite(x) && x > 0) v += x;
      if (v <= 0) continue;
      Slice e;
      e.name = s.name;
      e.val = v;
      e.color = s.color;
      sl.push_back(e);
      total += v;
    }
  }
  if (sl.empty() || total <= 0) {
    cv.text_c(r.x, r.y + r.h / 2, r.w, "(no positive values for a pie)", S.subtitle);
    return;
  }

  double acc = 0;
  for (auto &s : sl) {
    s.a0 = acc;
    acc += s.val / total;
    s.a1 = acc;
  }

  int explode = -1;
  if (o.explode == -1) {
    double best = -1;
    for (std::size_t i = 0; i < sl.size(); i++)
      if (sl[i].val > best) { best = sl[i].val; explode = static_cast<int>(i); }
  } else if (o.explode >= 0 && o.explode < static_cast<int>(sl.size())) {
    explode = o.explode;
  }

  // legend first: it decides how much room the pie gets
  int legend_rows = 0;
  std::vector<LegEntry> leg;
  for (std::size_t i = 0; i < sl.size(); i++)
    leg.push_back({sl[i].name, sl[i].color, fmt_val(sl[i].val) + "  " + fmt_pct(sl[i].val / total, 1),
                   series_char(o, i)});
  int legw = legend_width(leg) + 2;
  bool leg_right = (r.w - legw >= 18) && static_cast<int>(leg.size()) <= r.h;
  Rect pie_area = r;
  if (leg_right) {
    legend_draw(cv, Rect{r.right() - legw + 1, r.y, legw, r.h}, leg, true);
    pie_area.w = r.w - legw - 1;
  } else {
    legend_rows = std::min<int>(3, static_cast<int>((leg.size() + 2) / 3));
    legend_draw(cv, Rect{r.x, r.bottom() - legend_rows + 1, r.w, legend_rows}, leg, false);
    pie_area.h = r.h - legend_rows - 1;
  }
  if (pie_area.w < 8 || pie_area.h < 5) return;

  int span = pie_area.h - 2; // one row of shadow, one of air
  int rx_w = pie_area.w / 2 - 2;
  int ry_max = std::max(1, (span - depth - 1) / 2);
  int rx = std::min(rx_w, ry_max * 2);
  if (rx < 2) rx = 2;
  int ry = std::max(1, std::min(rx / 2, ry_max));
  int cx = pie_area.x + pie_area.w / 2;
  int cy = pie_area.y + 1 + ry + std::max(0, (span - (2 * ry + depth)) / 2);

  // never paint over the frame, whatever the explode offset does
  auto pput = [&](int x, int y, char32_t ch, uint8_t col) {
    if (x < pie_area.x || x > pie_area.right() || y < pie_area.y || y > pie_area.bottom()) return;
    cv.put(x, y, ch, col);
  };

  double ox = 0, oy = 0;
  if (explode >= 0) {
    double mid = (sl[static_cast<std::size_t>(explode)].a0 + sl[static_cast<std::size_t>(explode)].a1) / 2;
    double ang = mid * 2 * PI - PI / 2;
    ox = std::lround(std::cos(ang) * rx * 0.18);
    oy = std::lround(std::sin(ang) * ry * 0.55);
    if (ox == 0 && oy == 0) ox = 1;
  }

  // Which slice owns this cell? The popped-out slice is checked first, but only
  // inside its own wedge, otherwise it would swallow the neighbours; and its
  // old wedge is left empty so the gap reads as a pop-out.
  auto probe = [&](int x, int y) -> int {
    if (explode >= 0) {
      int ecx = cx + static_cast<int>(ox), ecy = cy + static_cast<int>(oy);
      if (in_ellipse(x, y, ecx, ecy, rx, ry, hole)) {
        double f = norm_angle(static_cast<double>(x - ecx) / rx, static_cast<double>(y - ecy) / ry);
        const Slice &e = sl[static_cast<std::size_t>(explode)];
        if (f >= e.a0 && f < e.a1) return explode;
      }
    }
    if (!in_ellipse(x, y, cx, cy, rx, ry, hole)) return -1;
    double f = norm_angle(static_cast<double>(x - cx) / rx, static_cast<double>(y - cy) / ry);
    for (std::size_t i = 0; i < sl.size(); i++) {
      if (f >= sl[i].a0 && f < sl[i].a1) return static_cast<int>(i) == explode ? -1 : static_cast<int>(i);
    }
    return static_cast<int>(sl.size()) - 1 == explode ? -1 : static_cast<int>(sl.size()) - 1;
  };

  // drop shadow: the silhouette shifted one cell right, a few rows down, and
  // clipped so it only ever shows to the right of and below the pie itself.
  for (int y = cy - ry; y <= cy + ry + depth + 1; y++) {
    int sl = 0, sr = -1;
    ellipse_span(y - depth - 1, cx, cy, rx, ry, sl, sr);
    if (sr < sl) continue;
    sl += 1;
    sr += 1;
    int ol = 0, orr = -1;
    if (y <= cy + ry) ellipse_span(y, cx, cy, rx, ry, ol, orr);
    else ellipse_span(y - depth, cx, cy, rx, ry, ol, orr);
    int x0 = (orr >= ol) ? std::max(sl, orr + 1) : sl;
    for (int x = x0; x <= sr; x++) pput(x, y, G.blk[1], S.shadow);
  }

  // 3-D wall: the swept side, drawn from the feet of the top face
  if (depth > 0) {
    for (int y = cy + 1; y <= cy + ry + depth; y++)
      for (int x = cx - rx; x <= cx + rx; x++) {
        int s = probe(x, y - depth);
        if (s < 0) continue;
        pput(x, y, G.blk[1], static_cast<uint8_t>(sl[static_cast<std::size_t>(s)].color));
      }
  }

  // the top face
  for (int y = cy - ry; y <= cy + ry; y++)
    for (int x = cx - rx; x <= cx + rx; x++) {
      int s = probe(x, y);
      if (s < 0) continue;
      pput(x, y, series_char(o, static_cast<std::size_t>(s)),
           static_cast<uint8_t>(sl[static_cast<std::size_t>(s)].color));
    }

  // percentage inside fat slices
  if (rx >= 10 && o.color) {
    for (std::size_t i = 0; i < sl.size(); i++) {
      double pct = sl[i].val / total * 100;
      if (pct < 6) continue;
      double mid = (sl[i].a0 + sl[i].a1) / 2;
      double ang = mid * 2 * PI - PI / 2;
      int px = cx + static_cast<int>(std::lround(std::cos(ang) * rx * 0.62));
      int py = cy + static_cast<int>(std::lround(std::sin(ang) * ry * 0.62));
      int ecx = cx + static_cast<int>(ox), ecy = cy + static_cast<int>(oy);
      if (static_cast<int>(i) == explode) { px = ecx + static_cast<int>(std::lround(std::cos(ang) * rx * 0.62)); py = ecy + static_cast<int>(std::lround(std::sin(ang) * ry * 0.62)); }
      char buf[32];
      std::snprintf(buf, sizeof buf, "%.0f%%", pct);
      std::string t(buf);
      int len = static_cast<int>(cp_len(t));
      int tx = px - len / 2;
      bool clear = true;
      for (int k = 0; k < len; k++) {
        int s = probe(tx + k, py);
        if (s != static_cast<int>(i)) clear = false;
      }
      if (!clear) continue;
      cv.text(tx, py, t, contrast_on(static_cast<uint8_t>(sl[i].color)));
    }
  }
}

// ---- table -----------------------------------------------------------------

static void draw_table(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o) {
  std::size_t n = ds.nrows();
  std::size_t ncol = ds.series.size() + 1;
  if (n == 0 || ncol == 0) return;

  std::vector<std::vector<std::string>> cells(n, std::vector<std::string>(ncol));
  std::vector<std::string> hdr(ncol);
  hdr[0] = "#";
  for (std::size_t s = 0; s < ds.series.size(); s++) hdr[s + 1] = ds.series[s].name;
  for (std::size_t i = 0; i < n; i++) {
    cells[i][0] = (i < ds.labels.size()) ? ds.labels[i] : std::to_string(i + 1);
    for (std::size_t s = 0; s < ds.series.size(); s++) {
      double v = i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
      cells[i][s + 1] = std::isfinite(v) ? fmt_val(v, o.prec) : "-";
    }
  }

  std::vector<int> w(ncol, 1);
  for (std::size_t c = 0; c < ncol; c++) {
    w[c] = std::max(1, static_cast<int>(cp_len(hdr[c])));
    for (std::size_t i = 0; i < n; i++) w[c] = std::max(w[c], static_cast<int>(cp_len(cells[i][c])));
    w[c] = std::min(w[c], 18);
  }
  int frames = static_cast<int>(ncol) * 3 + 1;
  int avail = r.w - frames;
  while (avail < static_cast<int>(ncol) * 3 && ncol > 1) {
    ncol--;
    cells.assign(n, std::vector<std::string>(ncol));
    hdr.resize(ncol);
    w.resize(ncol);
    frames = static_cast<int>(ncol) * 3 + 1;
    avail = r.w - frames;
    break;
  }
  int sum = 0;
  for (std::size_t c = 0; c < ncol; c++) sum += w[c];
  while (sum > avail) {
    int widest = 0;
    for (std::size_t c = 1; c < ncol; c++)
      if (w[c] > w[widest]) widest = static_cast<int>(c);
    if (w[static_cast<std::size_t>(widest)] <= 4) break;
    w[static_cast<std::size_t>(widest)]--;
    sum--;
  }

  int y = r.y;
  // header
  std::string rule;
  {
    int x = r.x;
    for (std::size_t c = 0; c < ncol && x < r.right(); c++) {
      std::string t = trunc_to(hdr[c], static_cast<std::size_t>(w[c]));
      if (c == 0) cv.text(x, y, pad_right(t, static_cast<std::size_t>(w[c])) + " ", S.table_head);
      else cv.text(x, y, " " + pad_left(t, static_cast<std::size_t>(w[c])), S.table_head);
      x += w[c] + 2;
    }
    y++;
    cv.hline(r.x, y, r.w, G.h, S.table_rule);
    y++;
  }

  int room = r.bottom() - y + 1;
  std::size_t show = n;
  bool more = false;
  if (static_cast<int>(n) > room) {
    show = static_cast<std::size_t>(std::max(0, room - 1));
    more = true;
  }
  for (std::size_t i = 0; i < show; i++) {
    int x = r.x;
    for (std::size_t c = 0; c < ncol; c++) {
      std::string t = trunc_to(cells[i][c], static_cast<std::size_t>(w[c]));
      if (c == 0) cv.text(x, y, pad_right(t, static_cast<std::size_t>(w[c])) + " ", S.table_row);
      else cv.text(x, y, " " + pad_left(t, static_cast<std::size_t>(w[c])), S.table_row);
      x += w[c] + 2;
    }
    y++;
  }
  if (more) {
    cv.text(r.x, y, "... " + std::to_string(n - show) + " more rows", S.subtitle);
  }
}

// ---- histogram -------------------------------------------------------------

static void draw_hist(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o) {
  if (ds.series.empty()) return;
  const Series &s = ds.series[0];
  double lo = 1e300, hi = -1e300;
  std::size_t cnt = 0;
  for (double v : s.v) {
    if (!std::isfinite(v)) continue;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
    cnt++;
  }
  if (cnt == 0) return;
  if (!(hi > lo)) { hi = lo + 1; }

  int bins = std::max(1, std::min(60, o.bins));
  std::vector<double> count(static_cast<std::size_t>(bins), 0);
  double step = (hi - lo) / bins;
  for (double v : s.v) {
    if (!std::isfinite(v)) continue;
    int b = static_cast<int>((v - lo) / step);
    if (b >= bins) b = bins - 1;
    if (b < 0) b = 0;
    count[static_cast<std::size_t>(b)] += 1;
  }

  Dataset h;
  h.source = ds.source;
  h.title = o.title.empty() ? ("histogram of " + s.name) : o.title;
  Series hs;
  hs.name = "count";
  for (int b = 0; b < bins; b++) {
    h.labels.push_back(fmt_axis(lo + b * step));
    hs.v.push_back(count[static_cast<std::size_t>(b)]);
  }
  hs.color = s.color;
  h.series.push_back(hs);

  RenderOpts o2 = o;
  o2.type = "bar";
  o2.legend = false;
  o2.title = h.title;
  o2.ylabel = o.ylabel;
  o2.xlabel = o.xlabel;
  draw_bars(cv, r, h, o2, false);
}

// ---- entry point -----------------------------------------------------------

void render_chart(Canvas &cv, Rect r, Dataset &ds, const RenderOpts &o) {
  if (r.w < 8 || r.h < 4) return;
  assign_colors(ds, o.palette);
  std::string type = lower(o.type);

  Rect inner = r;
  if (o.frame != "none") {
    if (o.shadow) cv.shadow(r.x, r.y, r.w, r.h, S.shadow, G.blk[1]);
    int st = BOX_DOUBLE;
    if (o.frame == "single") st = BOX_SINGLE;
    else if (o.frame == "heavy") st = BOX_HEAVY;
    else if (o.frame == "ascii") st = BOX_ASCII;
    cv.box(r.x, r.y, r.w, r.h, st, S.frame);
    inner = Rect{r.x + 2, r.y + 1, r.w - 4, r.h - 2};
  }
  if (inner.w < 4 || inner.h < 2) return;

  std::string title = o.title.empty() ? ds.title : o.title;
  std::string bottom = ds.source;

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
  if (!bottom.empty() && o.frame != "none" && r.h > 3) {
    // A long path gets truncated to fit; the file's own name is what matters,
    // so fall back to the basename before giving up on naming it at all.
    if (static_cast<int>(cp_len(bottom)) + 6 > r.w) {
      std::size_t s = bottom.find_last_of('/');
      if (s != std::string::npos && s + 1 < bottom.size()) bottom = bottom.substr(s + 1);
    }
    std::string b = " " + bottom + " ";
    cv.text_c(r.x + 2, r.bottom(), r.w - 4, b, S.subtitle);
  }
  if (inner.w < 4 || inner.h < 2) return;

  bool pie_like = (type == "pie" || type == "pie3d" || type == "donut");
  bool table_like = (type == "table");

  if (table_like) {
    draw_table(cv, inner, ds, o);
    return;
  }

  // legend reservation for series charts
  if (!pie_like && o.legend && ds.series.size() > 1) {
    std::vector<LegEntry> leg = series_entries(ds, o);
    int lw = legend_width(leg) + 2;
    if (inner.w - lw >= 24 && static_cast<int>(leg.size()) <= inner.h) {
      legend_draw(cv, Rect{inner.right() - lw + 1, inner.y, lw, inner.h}, leg, true);
      inner.w -= lw + 1;
    } else {
      int rows = std::min<int>(3, static_cast<int>((leg.size() + 2) / 3));
      Rect lr{inner.x, inner.bottom() - rows + 1, inner.w, rows};
      legend_draw(cv, lr, leg, false);
      inner.h -= rows + 1;
    }
  }

  if (type == "bar" || type == "grouped" || type == "column") draw_bars(cv, inner, ds, o, false);
  else if (type == "stacked") draw_bars(cv, inner, ds, o, true);
  else if (type == "hbar") draw_hbars(cv, inner, ds, o, false);
  else if (type == "line") draw_lines(cv, inner, ds, o, false);
  else if (type == "area") draw_lines(cv, inner, ds, o, true);
  else if (type == "scatter" || type == "xy") draw_scatter(cv, inner, ds, o);
  else if (type == "hist") draw_hist(cv, inner, ds, o);
  else if (type == "pie") draw_pie(cv, inner, ds, o, 0, 0.0);
  else if (type == "pie3d") draw_pie(cv, inner, ds, o, std::max(1, o.depth), 0.0);
  else if (type == "donut") draw_pie(cv, inner, ds, o, std::max(1, o.depth), 0.45);
  else draw_bars(cv, inner, ds, o, false);
}

} // namespace ch
