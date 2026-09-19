// canvas.hpp -- a cell grid with 16-colour ANSI output.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ch {

enum { BG_NONE = 0xFF };

struct Cell {
  char32_t ch = U' ';
  uint8_t fg = 7;
  uint8_t bg = BG_NONE;
};

enum BoxStyle { BOX_SINGLE = 0, BOX_DOUBLE = 1, BOX_HEAVY = 2, BOX_ASCII = 3, BOX_NONE = 4 };

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  int right() const { return x + w - 1; }
  int bottom() const { return y + h - 1; }
  bool has(int ww, int hh) const { return w >= ww && h >= hh; }
};

class Canvas {
public:
  Canvas(int w, int h);

  int w() const { return W_; }
  int h() const { return H_; }

  void clear(uint8_t fg = 7, uint8_t bg = BG_NONE);
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < W_ && y < H_; }

  // The three-argument form keeps whatever background the cell already has,
  // so text written over a panel stays on the panel.
  void put(int x, int y, char32_t ch, uint8_t fg);
  void put(int x, int y, char32_t ch, uint8_t fg, uint8_t bg);
  const Cell &at(int x, int y) const { return c_[static_cast<std::size_t>(y) * W_ + x]; }
  void fill_bg(int x, int y, int w, int h, uint8_t bg); // blanks the cells too
  void text_bg(int x, int y, const std::string &s, uint8_t fg, uint8_t bg);
  void text(int x, int y, const std::string &s, uint8_t fg);
  void text_c(int x, int y, int w, const std::string &s, uint8_t fg); // centred in w
  void text_r(int x, int y, int w, const std::string &s, uint8_t fg); // right aligned in w
  void fill(int x, int y, int w, int h, char32_t ch, uint8_t fg);
  void hline(int x, int y, int len, char32_t ch, uint8_t fg);
  void vline(int x, int y, int len, char32_t ch, uint8_t fg);
  void box(int x, int y, int w, int h, int style, uint8_t fg);
  // DOS drop shadow: the cells right of and below the box go dark, keeping
  // whatever was written there.
  void shadow(int x, int y, int w, int h);
  void vtext(int x, int y, const std::string &s, uint8_t fg); // one letter per row, downward from (x,y)

  // bright_bg: the terminal honours SGR 100-107.  The Linux VT does not, so
  // there a bright background falls back to its dim partner.
  std::string dump(bool color, bool crlf, bool bright_bg = true) const;

private:
  int W_, H_;
  std::vector<Cell> c_;
};

} // namespace ch
