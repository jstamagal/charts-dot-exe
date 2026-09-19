#include "gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ch {

// The IBM VGA text-mode palette, brown and all.
const uint8_t VGA_RGB[16][3] = {
    {0x00, 0x00, 0x00}, {0xAA, 0x00, 0x00}, {0x00, 0xAA, 0x00}, {0xAA, 0x55, 0x00},
    {0x00, 0x00, 0xAA}, {0xAA, 0x00, 0xAA}, {0x00, 0xAA, 0xAA}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0xFF, 0x55, 0x55}, {0x55, 0xFF, 0x55}, {0xFF, 0xFF, 0x55},
    {0x55, 0x55, 0xFF}, {0xFF, 0x55, 0xFF}, {0x55, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
};

uint8_t contrast_on(uint8_t c) {
  const uint8_t *p = VGA_RGB[c & 15];
  int luma = (p[0] * 3 + p[1] * 6 + p[2]) / 10;
  return luma > 110 ? 0 : 15;
}

Ink side_ink(uint8_t c) {
  if (c == 15) return Ink(7);
  if (c >= 9) return Ink(static_cast<uint8_t>(c - 8));
  return Ink(c, 0, 2);
}

Ink top_ink(uint8_t c) {
  if (c == 15) return Ink(15, 7, 1);
  if (c >= 9) return Ink(c, 15, 2);
  if (c == 0 || c == 8) return Ink(7, c, 2);
  return Ink(static_cast<uint8_t>(c + 8));
}

// ---- geometry ------------------------------------------------------------------

// A double to an int in [lo, hi]: clamped before the cast, which is only
// defined for values that fit.  NaN goes to lo.
static int clamp_int(double v, int lo, int hi) {
  if (!(v > lo)) return lo;
  if (v > hi) return hi;
  return static_cast<int>(v);
}

// Liang-Barsky.
bool clip_segment(Pt &a, Pt &b, double x0, double y0, double x1, double y1) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(a.x) || !std::isfinite(a.y)) return false;
  const double p[4] = {-dx, dx, -dy, dy}, q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
  double t0 = 0, t1 = 1;
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
      continue;
    }
    const double t = q[i] / p[i];
    if (p[i] < 0) { if (t > t1) return false; t0 = std::max(t0, t); }
    else { if (t < t0) return false; t1 = std::min(t1, t); }
  }
  const Pt start{a.x + t0 * dx, a.y + t0 * dy};
  b = Pt{a.x + t1 * dx, a.y + t1 * dy};
  a = start;
  return true;
}

// ---- surface -----------------------------------------------------------------

Surface::Surface(int cells_w, int cells_h, int sx, int sy)
    : W_(std::max(1, cells_w) * sx), H_(std::max(1, cells_h) * sy), sx_(sx), sy_(sy),
      px_(static_cast<std::size_t>(W_) * H_, Ink(0, 0, EMPTY)) {}

// Ordered 2x2 dither.  Level 2 is the plain checkerboard.
uint8_t Surface::resolve(int x, int y) const {
  const Ink &k = px_[idx(x, y)];
  if (k.level == 0 || k.level == EMPTY) return k.a;
  static const uint8_t rank[2][2] = {{0, 2}, {3, 1}};
  return rank[y & 1][x & 1] < k.level ? k.b : k.a;
}

void Surface::set(int x, int y, Ink k) {
  if (inside(x, y)) px_[idx(x, y)] = k;
}

void Surface::erase(int x0, int y0, int x1, int y1) {
  for (int y = std::max(0, y0); y < std::min(H_, y1); y++)
    for (int x = std::max(0, x0); x < std::min(W_, x1); x++) px_[idx(x, y)] = Ink(0, 0, EMPTY);
}

void Surface::rect(double x0, double y0, double x1, double y1, Ink k) {
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  const int ax = clamp_int(std::round(x0), 0, W_), ay = clamp_int(std::round(y0), 0, H_);
  const int bx = clamp_int(std::round(x1), 0, W_), by = clamp_int(std::round(y1), 0, H_);
  for (int y = ay; y < by; y++)
    for (int x = ax; x < bx; x++) px_[idx(x, y)] = k;
}

// The runs below are cut to the surface before they are walked: set() would
// drop the pixels anyway, and a line to 1e9 should not cost 1e9 steps.
void Surface::hline(int x0, int x1, int y, Ink k) {
  if (x1 < x0) std::swap(x0, x1);
  x0 = std::max(x0, 0);
  x1 = std::min(x1, W_ - 1);
  for (int x = x0; x <= x1; x++) set(x, y, k);
}

void Surface::vline(int x, int y0, int y1, Ink k) {
  if (y1 < y0) std::swap(y0, y1);
  y0 = std::max(y0, 0);
  y1 = std::min(y1, H_ - 1);
  for (int y = y0; y <= y1; y++) set(x, y, k);
}

void Surface::dotted_h(int x0, int x1, int y, Ink k, int gap) {
  if (gap < 1) gap = 1;
  for (int x = std::max(x0, 0); x <= std::min(x1, W_ - 1); x++)
    if (x % gap == 0) set(x, y, k);
}

void Surface::dotted_v(int x, int y0, int y1, Ink k, int gap) {
  if (gap < 1) gap = 1;
  for (int y = std::max(y0, 0); y <= std::min(y1, H_ - 1); y++)
    if (y % gap == 0) set(x, y, k);
}

void Surface::line(double x0, double y0, double x1, double y1, Ink k, int width) {
  // Only the part over the surface is walked: a line to (1e9, 0) is short work.
  Pt a{x0, y0}, b{x1, y1};
  const double m = width + 2.0;
  if (!clip_segment(a, b, -m, -m, W_ + m, H_ + m)) return;
  int ax = static_cast<int>(std::lround(a.x)), ay = static_cast<int>(std::lround(a.y));
  int bx = static_cast<int>(std::lround(b.x)), by = static_cast<int>(std::lround(b.y));
  int dx = std::abs(bx - ax), dy = -std::abs(by - ay);
  int stepx = ax < bx ? 1 : -1, stepy = ay < by ? 1 : -1;
  int err = dx + dy;
  if (width < 1) width = 1;
  int lo = -(width - 1) / 2, hi = width / 2;
  // Guard against a runaway loop on absurd coordinates.
  long budget = static_cast<long>(dx) - dy + 4;
  while (budget-- > 0) {
    for (int j = lo; j <= hi; j++)
      for (int i = lo; i <= hi; i++) set(ax + i, ay + j, k);
    if (ax == bx && ay == by) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; ax += stepx; }
    if (e2 <= dx) { err += dx; ay += stepy; }
  }
}

void Surface::poly(const std::vector<Pt> &p, Ink k) {
  if (p.size() < 3) return;
  double miny = p[0].y, maxy = p[0].y;
  for (const auto &q : p) { miny = std::min(miny, q.y); maxy = std::max(maxy, q.y); }
  if (!(maxy >= 0) || !(miny < H_)) return; // off the surface, or not numbers
  const int y0 = clamp_int(std::floor(miny), 0, H_ - 1), y1 = clamp_int(std::ceil(maxy), 0, H_ - 1);
  std::vector<double> xs;
  for (int y = y0; y <= y1; y++) {
    double sy = y + 0.5;
    xs.clear();
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
      const Pt &a = p[i], &b = p[j];
      if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy))
        xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
    for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
      const int xa = clamp_int(std::round(xs[i]), 0, W_), xb = clamp_int(std::round(xs[i + 1]), 0, W_);
      for (int x = xa; x < xb; x++) px_[idx(x, y)] = k;
    }
  }
}

void Surface::disc(double cx, double cy, double r, Ink k) {
  if (!(r > 0) || !std::isfinite(cx) || !std::isfinite(cy)) return;
  const double ry = r / aspect();
  const int y0 = clamp_int(std::floor(cy - ry), 0, H_ - 1), y1 = clamp_int(std::ceil(cy + ry), 0, H_ - 1);
  const int x0 = clamp_int(std::floor(cx - r), 0, W_ - 1), x1 = clamp_int(std::ceil(cx + r), 0, W_ - 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      double dx = (x + 0.5 - cx) / r, dy = (y + 0.5 - cy) / ry;
      if (dx * dx + dy * dy <= 1.0) set(x, y, k);
    }
}

void Surface::ring(double cx, double cy, double r, Ink k) {
  if (!(r > 0) || !std::isfinite(cx) || !std::isfinite(cy)) return;
  const double ry = r / aspect(), in = std::max(0.0, r - std::max(1.0, r * 0.35));
  const int y0 = clamp_int(std::floor(cy - ry), 0, H_ - 1), y1 = clamp_int(std::ceil(cy + ry), 0, H_ - 1);
  const int x0 = clamp_int(std::floor(cx - r), 0, W_ - 1), x1 = clamp_int(std::ceil(cx + r), 0, W_ - 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      double dx = (x + 0.5 - cx), dy = (y + 0.5 - cy) * aspect();
      double d = std::sqrt(dx * dx + dy * dy);
      if (d <= r && d >= in) set(x, y, k);
    }
}

// 0 disc, 1 square, 2 diamond, 3 triangle, 4 cross, 5 ring, 6 inverted triangle
void Surface::marker(double cx, double cy, int shape, double r, Ink k) {
  if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r)) return;
  if (r < 1) {
    set(static_cast<int>(std::lround(cx - 0.5)), static_cast<int>(std::lround(cy - 0.5)), k);
    return;
  }
  double ry = r / aspect();
  switch (shape % 7) {
  case 0: disc(cx, cy, r, k); break;
  case 1: rect(cx - r * 0.85, cy - ry * 0.85, cx + r * 0.85, cy + ry * 0.85, k); break;
  case 2: poly({{cx, cy - ry * 1.2}, {cx + r * 1.2, cy}, {cx, cy + ry * 1.2}, {cx - r * 1.2, cy}}, k); break;
  case 3: poly({{cx, cy - ry * 1.1}, {cx + r * 1.15, cy + ry * 0.9}, {cx - r * 1.15, cy + ry * 0.9}}, k); break;
  case 4: {
    int w = std::max(1, static_cast<int>(r * 0.6));
    line(cx - r, cy - ry, cx + r, cy + ry, k, w);
    line(cx - r, cy + ry, cx + r, cy - ry, k, w);
    break;
  }
  case 5: ring(cx, cy, r * 1.1, k); break;
  default: poly({{cx - r * 1.15, cy - ry * 0.9}, {cx + r * 1.15, cy - ry * 0.9}, {cx, cy + ry * 1.1}}, k); break;
  }
}

// ---- font ----------------------------------------------------------------------

namespace {
struct Glyph {
  uint32_t cp;
  uint8_t rows[16];
};
const Glyph FONT[] = {
#include "font_data.inc"
};
const std::size_t NFONT = sizeof FONT / sizeof FONT[0];
const uint8_t TOFU[16] = {0, 0, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7E, 0, 0, 0, 0};

const Glyph *find_glyph(char32_t cp) {
  std::size_t lo = 0, hi = NFONT;
  while (lo < hi) {
    std::size_t mid = (lo + hi) / 2;
    if (FONT[mid].cp < cp) lo = mid + 1;
    else hi = mid;
  }
  return (lo < NFONT && FONT[lo].cp == cp) ? &FONT[lo] : nullptr;
}
} // namespace

bool glyph_known(char32_t cp) { return find_glyph(cp) != nullptr; }

const uint8_t *glyph_rows(char32_t cp) {
  const Glyph *g = find_glyph(cp);
  return g ? g->rows : TOFU;
}

// ---- image ---------------------------------------------------------------------

void Image::fill_rect(int x, int y, int ww, int hh, uint8_t c) {
  int x1 = std::min(w, x + ww), y1 = std::min(h, y + hh);
  for (int j = std::max(0, y); j < y1; j++)
    for (int i = std::max(0, x); i < x1; i++) px[static_cast<std::size_t>(j) * w + i] = c;
}

void Image::glyph(int x, int y, char32_t cp, uint8_t fg, int bg, int scale) {
  if (scale < 1) scale = 1;
  if (bg >= 0) fill_rect(x, y, 8 * scale, 16 * scale, static_cast<uint8_t>(bg));
  if (cp == U' ') return;
  const uint8_t *rows = glyph_rows(cp);
  for (int r = 0; r < 16; r++) {
    uint8_t bits = rows[r];
    if (!bits) continue;
    for (int c = 0; c < 8; c++)
      if (bits & (0x80 >> c)) fill_rect(x + c * scale, y + r * scale, scale, scale, fg);
  }
}

Image Image::scaled(int k) const {
  if (k <= 1) return *this;
  Image o(w * k, h * k, 0);
  for (int y = 0; y < h; y++) {
    uint8_t *dst = &o.px[static_cast<std::size_t>(y) * k * o.w];
    const uint8_t *src = &px[static_cast<std::size_t>(y) * w];
    for (int x = 0; x < w; x++)
      for (int i = 0; i < k; i++) dst[x * k + i] = src[x];
    for (int j = 1; j < k; j++)
      std::copy(dst, dst + o.w, dst + static_cast<std::size_t>(j) * o.w);
  }
  return o;
}

} // namespace ch
