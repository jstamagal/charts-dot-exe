#include "canvas.hpp"

#include "util.hpp"

namespace ch {

Canvas::Canvas(int w, int h) : W_(w < 1 ? 1 : w), H_(h < 1 ? 1 : h), c_(static_cast<std::size_t>(W_) * H_) {}

void Canvas::clear(uint8_t fg) {
  for (auto &cl : c_) { cl.ch = U' '; cl.fg = fg; cl.bg = BG_NONE; }
}

void Canvas::put(int x, int y, char32_t ch, uint8_t fg) {
  if (!inside(x, y) || ch == 0) return;
  Cell &cl = c_[static_cast<std::size_t>(y) * W_ + x];
  cl.ch = ch;
  cl.fg = fg;
  cl.bg = BG_NONE;
}

void Canvas::put(int x, int y, char32_t ch, uint8_t fg, uint8_t bg) {
  if (!inside(x, y) || ch == 0) return;
  Cell &cl = c_[static_cast<std::size_t>(y) * W_ + x];
  cl.ch = ch;
  cl.fg = fg;
  cl.bg = bg;
}

void Canvas::text(int x, int y, const std::string &s, uint8_t fg) {
  if (y < 0 || y >= H_) return;
  int cx = x;
  for (char32_t cp : utf8_decode(s)) {
    if (cx >= W_) break;
    if (cx >= 0) put(cx, y, cp, fg);
    cx++;
  }
}

void Canvas::text_c(int x, int y, int w, const std::string &s, uint8_t fg) {
  std::string t = trunc_to(s, static_cast<std::size_t>(w < 0 ? 0 : w));
  int pad = (w - static_cast<int>(cp_len(t))) / 2;
  if (pad < 0) pad = 0;
  text(x + pad, y, t, fg);
}

void Canvas::text_r(int x, int y, int w, const std::string &s, uint8_t fg) {
  std::string t = trunc_to(s, static_cast<std::size_t>(w < 0 ? 0 : w));
  int pad = w - static_cast<int>(cp_len(t));
  if (pad < 0) pad = 0;
  text(x + pad, y, t, fg);
}

void Canvas::fill(int x, int y, int w, int h, char32_t ch, uint8_t fg) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) put(x + i, y + j, ch, fg);
}

void Canvas::hline(int x, int y, int len, char32_t ch, uint8_t fg) {
  for (int i = 0; i < len; i++) put(x + i, y, ch, fg);
}

void Canvas::vline(int x, int y, int len, char32_t ch, uint8_t fg) {
  for (int j = 0; j < len; j++) put(x, y + j, ch, fg);
}

void Canvas::box(int x, int y, int w, int h, int style, uint8_t fg) {
  if (style == BOX_NONE || w < 2 || h < 2) return;
  wchar_t H1, V1, TL, TR, BL, BR;
  switch (style) {
  case BOX_DOUBLE:
    H1 = G.dh; V1 = G.dv; TL = G.dtl; TR = G.dtr; BL = G.dbl; BR = G.dbr; break;
  case BOX_HEAVY:
    H1 = G.hh; V1 = G.hv; TL = G.htl; TR = G.htr; BL = G.hbl; BR = G.hbr; break;
  case BOX_ASCII:
    H1 = U'-'; V1 = U'|'; TL = TR = BL = BR = U'+'; break;
  default:
    H1 = G.h; V1 = G.v; TL = G.tl; TR = G.tr; BL = G.bl; BR = G.br; break;
  }
  hline(x + 1, y, w - 2, static_cast<char32_t>(H1), fg);
  hline(x + 1, y + h - 1, w - 2, static_cast<char32_t>(H1), fg);
  vline(x, y + 1, h - 2, static_cast<char32_t>(V1), fg);
  vline(x + w - 1, y + 1, h - 2, static_cast<char32_t>(V1), fg);
  put(x, y, static_cast<char32_t>(TL), fg);
  put(x + w - 1, y, static_cast<char32_t>(TR), fg);
  put(x, y + h - 1, static_cast<char32_t>(BL), fg);
  put(x + w - 1, y + h - 1, static_cast<char32_t>(BR), fg);
}

void Canvas::shadow(int x, int y, int w, int h, uint8_t fg, char32_t ch) {
  // classic DOS drop shadow: one column right, one row below, offset +1/+1
  vline(x + w, y + 1, h - 1, ch, fg);
  hline(x + 1, y + h, w, ch, fg);
}

void Canvas::vtext(int x, int y, const std::string &s, uint8_t fg) {
  auto cps = utf8_decode(s);
  int cy = y;
  for (auto cp : cps) { put(x, cy, cp, fg); cy--; }
}

static void sgr_fg(std::string &o, uint8_t n) {
  // linux console safe: 30-37 plus bold for the bright half
  if (n < 8) { o += '3'; o += static_cast<char>('0' + n); }
  else { o += "1;3"; o += static_cast<char>('0' + (n - 8)); }
}

static void sgr_bg(std::string &o, uint8_t n) {
  if (n < 8) { o += '4'; o += static_cast<char>('0' + n); }
  else { o += "10"; o += static_cast<char>('0' + (n - 8)); }
}

std::string Canvas::dump(bool color, bool crlf) const {
  std::string out;
  out.reserve(static_cast<std::size_t>(W_) * H_ * 4);
  for (int y = 0; y < H_; y++) {
    int last = W_ - 1;
    while (last >= 0) {
      const Cell &cl = c_[static_cast<std::size_t>(y) * W_ + last];
      if (cl.ch != U' ' || cl.bg != BG_NONE) break;
      last--;
    }
    int curfg = -1, curbg = -1;
    for (int x = 0; x <= last; x++) {
      const Cell &cl = c_[static_cast<std::size_t>(y) * W_ + x];
      if (color && (cl.fg != curfg || cl.bg != curbg)) {
        std::string s = "\x1b[0;";
        sgr_fg(s, cl.fg);
        if (cl.bg != BG_NONE) { s += ';'; sgr_bg(s, cl.bg); }
        s += 'm';
        out += s;
        curfg = cl.fg;
        curbg = cl.bg;
      }
      out += u32_to_utf8(cl.ch);
    }
    if (color && curfg >= 0) out += "\x1b[0m";
    out += crlf ? "\r\n" : "\n";
  }
  return out;
}

} // namespace ch
