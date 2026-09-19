// gfx.hpp -- the 16-colour pixel layer: inks, surfaces, the bitmap font.
//
// Everything that is not text is drawn onto a Surface.  A surface covers a
// rectangle of character cells and has sx*sy pixels per cell:
//
//   8x16  pixel output (framebuffer, kitty, sixel, png) -- VGA-style
//   1x2   cell output, drawn with half blocks           -- square "pixels"
//   1x1   --ascii
//
// so chart code is written once, in pixels, and looks right at every density.
// Shading is never a 17th colour: an Ink is two of the 16 colours and a dither
// level, the way a VGA card did it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ch {

extern const uint8_t VGA_RGB[16][3];

// The dim partner of a bright colour and vice versa (9 <-> 1, 14 <-> 6 ...).
inline uint8_t dim_of(uint8_t c) { return c == 8 ? 0 : (c == 7 ? 8 : (c > 8 ? c - 8 : c)); }
inline uint8_t bright_of(uint8_t c) { return c == 0 ? 8 : (c < 8 ? c + 8 : c); }
// Black or white, whichever reads on top of c.
uint8_t contrast_on(uint8_t c);

// Two colours and how much of the second is mixed in: 0 none, 1 a quarter,
// 2 half (checkerboard), 3 three quarters.
struct Ink {
  uint8_t a = 7, b = 0, level = 0;
  Ink() = default;
  Ink(uint8_t a_) : a(a_), b(0), level(0) {}
  Ink(uint8_t a_, uint8_t b_, uint8_t l) : a(a_), b(b_), level(l) {}
  bool operator==(const Ink &o) const { return a == o.a && b == o.b && level == o.level; }
  bool operator!=(const Ink &o) const { return !(*this == o); }
};

// The face of a solid seen from the side: the dim partner where there is one,
// otherwise the colour knocked back with black.
Ink side_ink(uint8_t c);
// The lit top face.
Ink top_ink(uint8_t c);

struct Pt {
  double x = 0, y = 0;
};

// Cut the segment a-b to the box [x0, x1] x [y0, y1].  False when none of it
// is inside (or a coordinate is not a finite number).
bool clip_segment(Pt &a, Pt &b, double x0, double y0, double x1, double y1);

class Surface {
public:
  Surface(int cells_w, int cells_h, int sx, int sy);

  int w() const { return W_; }
  int h() const { return H_; }
  int sx() const { return sx_; }
  int sy() const { return sy_; }
  bool fine() const { return sx_ >= 4; } // real pixels, not half blocks
  // Height of one pixel over its width.  A cell is twice as tall as it is
  // wide, so pixels are square (1.0) except under --ascii (2.0).  Round things
  // divide their y radius by this.
  double aspect() const { return 2.0 * sx_ / sy_; }

  bool touched(int x, int y) const { return inside(x, y) && px_[idx(x, y)].level != EMPTY; }
  Ink at(int x, int y) const { return px_[idx(x, y)]; }
  // The colour a pixel finally shows, once the dither is resolved.
  uint8_t resolve(int x, int y) const;

  void set(int x, int y, Ink k);
  void erase(int x0, int y0, int x1, int y1); // back to untouched, [x0,x1) x [y0,y1)
  void rect(double x0, double y0, double x1, double y1, Ink k); // [x0,x1) x [y0,y1)
  void hline(int x0, int x1, int y, Ink k);
  void vline(int x, int y0, int y1, Ink k);
  void dotted_h(int x0, int x1, int y, Ink k, int gap);
  void dotted_v(int x, int y0, int y1, Ink k, int gap);
  void line(double x0, double y0, double x1, double y1, Ink k, int width = 1);
  void poly(const std::vector<Pt> &p, Ink k);
  void disc(double cx, double cy, double r, Ink k); // honours aspect
  void ring(double cx, double cy, double r, Ink k);
  void marker(double cx, double cy, int shape, double r, Ink k);

private:
  enum { EMPTY = 0xFF };
  int W_, H_, sx_, sy_;
  std::vector<Ink> px_;
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < W_ && y < H_; }
  std::size_t idx(int x, int y) const { return static_cast<std::size_t>(y) * W_ + x; }
};

// ---- bitmap font -------------------------------------------------------------

// 16 rows, MSB is the leftmost of 8 pixels.  Unknown codepoints get a box.
const uint8_t *glyph_rows(char32_t cp);
bool glyph_known(char32_t cp);

// ---- indexed image -----------------------------------------------------------

struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> px; // one colour index (0..15) per pixel

  Image() = default;
  Image(int w_, int h_, uint8_t fill) : w(w_), h(h_), px(static_cast<std::size_t>(w_) * h_, fill) {}
  void set(int x, int y, uint8_t c) {
    if (x >= 0 && y >= 0 && x < w && y < h) px[static_cast<std::size_t>(y) * w + x] = c;
  }
  uint8_t get(int x, int y) const { return px[static_cast<std::size_t>(y) * w + x]; }
  void fill_rect(int x, int y, int ww, int hh, uint8_t c);
  // fg-only when bg < 0.  scale multiplies the 8x16 glyph.
  void glyph(int x, int y, char32_t cp, uint8_t fg, int bg, int scale);
  Image scaled(int k) const; // nearest neighbour, k >= 1
};

std::string png_encode(const Image &im);
std::string zlib_compress(const std::string &raw); // fixed-Huffman deflate in a zlib wrapper

} // namespace ch
