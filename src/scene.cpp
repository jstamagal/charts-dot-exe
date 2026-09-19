#include "scene.hpp"

#include <algorithm>
#include <cmath>

#include "util.hpp"

namespace ch {

Skin S;

namespace {

Skin theme_dos() { return Skin(); }

// No slide background at all: the look of a chart printed into a shell.
Skin theme_black() {
  Skin s;
  s.name = "black";
  s.slide_bg = BG_NONE;
  s.frame = 8;
  s.title = 15;
  s.subtitle = 8;
  s.xlabel = 8;
  s.ylabel = 8;
  s.shadow = 8;
  s.heading = 15;
  s.text = 7;
  s.dim = 8;
  s.bar_fg = 15;
  s.bar_bg = 4;
  s.bar_key = 11;
  s.table_head = 15;
  return s;
}

Skin theme_light() {
  Skin s;
  s.name = "light";
  s.slide_bg = 7;
  s.panel_bg = 15;
  s.frame = 0;
  s.title = 4;
  s.subtitle = 8;
  s.axis = 0;
  s.tick = 0;
  s.grid = 7;
  s.label = 0;
  s.xlabel = 4;
  s.ylabel = 4;
  s.legend = 0;
  s.value = 0;
  s.heading = 4;
  s.text = 0;
  s.dim = 8;
  s.accent = 1;
  s.bullet = 1;
  s.bar_fg = 15;
  s.bar_bg = 4;
  s.bar_key = 11;
  s.note_fg = 15;
  s.note_bg = 1;
  s.cursor_fg = 15;
  s.cursor_bg = 4;
  s.table_head = 4;
  s.table_rule = 8;
  s.table_row = 0;
  return s;
}

} // namespace

namespace {
struct SkinField { const char *name; uint8_t Skin::*field; };
const SkinField SKIN_FIELDS[] = {
    {"slide_bg", &Skin::slide_bg}, {"panel_bg", &Skin::panel_bg}, {"frame", &Skin::frame}, {"title", &Skin::title},
    {"subtitle", &Skin::subtitle}, {"axis", &Skin::axis}, {"tick", &Skin::tick}, {"grid", &Skin::grid},
    {"label", &Skin::label}, {"xlabel", &Skin::xlabel}, {"ylabel", &Skin::ylabel}, {"legend", &Skin::legend},
    {"value", &Skin::value}, {"heading", &Skin::heading}, {"text", &Skin::text}, {"dim", &Skin::dim},
    {"accent", &Skin::accent}, {"bullet", &Skin::bullet}, {"bar_fg", &Skin::bar_fg}, {"bar_bg", &Skin::bar_bg},
    {"bar_key", &Skin::bar_key}, {"note_fg", &Skin::note_fg}, {"note_bg", &Skin::note_bg},
    {"cursor_fg", &Skin::cursor_fg}, {"cursor_bg", &Skin::cursor_bg}, {"table_head", &Skin::table_head},
    {"table_rule", &Skin::table_rule}, {"table_row", &Skin::table_row},
};
} // namespace

std::vector<std::string> skin_keys() {
  std::vector<std::string> v;
  for (const auto &f : SKIN_FIELDS) v.push_back(f.name);
  return v;
}

bool skin_set(Skin &s, const std::string &key, uint8_t color) {
  for (const auto &f : SKIN_FIELDS)
    if (key == f.name) { s.*(f.field) = color; return true; }
  return false;
}

std::vector<std::string> theme_names() { return {"dos", "black", "light"}; }

bool set_theme(const std::string &name) {
  std::string n = lower(trim(name));
  if (n == "dos" || n == "blue") S = theme_dos();
  else if (n == "black" || n == "dark" || n == "none") S = theme_black();
  else if (n == "light" || n == "paper") S = theme_light();
  else return false;
  return true;
}

// ---- scene ---------------------------------------------------------------------

Scene::Scene(int cols, int rows, Mode m) : cv(cols, rows), mode_(m) { cv.clear(S.text, S.slide_bg); }

Surface &Scene::surface(Rect cells) {
  Layer l;
  l.r = cells;
  l.s.reset(new Surface(cells.w, cells.h, mode_.sx(), mode_.sy()));
  layers_.push_back(std::move(l));
  return *layers_.back().s;
}

void Scene::text(double x, double y, const std::string &s, uint8_t fg, int bg, int scale) {
  if (s.empty()) return;
  TextItem t;
  t.x = x;
  t.y = y;
  for (char32_t cp : utf8_decode(s)) t.s += u32_to_utf8((cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) ? U' ' : cp);
  t.fg = fg;
  t.bg = bg;
  t.scale = scale < 1 ? 1 : scale;
  texts_.push_back(t);
}

void Scene::big(int x, int y, int w, const std::string &s, uint8_t fg, int scale, int align) {
  if (w < 1) return;
  if (scale < 1) scale = 1;
  int k = mode_.pixel ? scale : 1;
  std::string t = trunc_to(s, static_cast<std::size_t>(std::max(1, w / k)));
  int len = static_cast<int>(cp_len(t)) * k;
  int off = align < 0 ? 0 : (align == 0 ? (w - len) / 2 : w - len);
  if (off < 0) off = 0;
  // Cell output has one text size: sit the line in the middle of the band.
  int row = mode_.pixel ? y : y + (scale - 1) / 2;
  text(x + off, row, t, fg, -1, k);
}

void Scene::cover(Rect r) {
  for (Layer &l : layers_)
    l.s->erase((r.x - l.r.x) * mode_.sx(), (r.y - l.r.y) * mode_.sy(), (r.x + r.w - l.r.x) * mode_.sx(),
               (r.y + r.h - l.r.y) * mode_.sy());
  std::vector<TextItem> keep;
  for (const TextItem &t : texts_) {
    double w = static_cast<double>(cp_len(t.s)) * t.scale, h = t.scale;
    bool hit = t.x < r.x + r.w && r.x < t.x + w && t.y < r.y + r.h && r.y < t.y + h;
    if (!hit) keep.push_back(t);
  }
  texts_.swap(keep);
}

void Scene::panel(Rect r, uint8_t bg) { cv.fill_bg(r.x, r.y, r.w, r.h, bg); }

Canvas Scene::to_cells() const {
  Canvas out = cv;
  const bool ascii = mode_.ascii;
  static const char32_t SHADE_U[4] = {U'█', U'▓', U'▒', U'░'};
  static const char32_t SHADE_A[4] = {U'#', U'%', U'=', U'.'};

  for (const Layer &l : layers_) {
    const Surface &s = *l.s;
    if (s.fine()) continue; // a pixel surface cannot be folded into cells
    for (int cy = 0; cy < l.r.h; cy++)
      for (int cx = 0; cx < l.r.w; cx++) {
        int x = l.r.x + cx, y = l.r.y + cy;
        if (!out.inside(x, y)) continue;
        if (s.sy() == 1) {
          if (!s.touched(cx, cy)) continue;
          Ink k = s.at(cx, cy);
          out.put(x, y, (ascii ? SHADE_A : SHADE_U)[k.level & 3], k.a);
          continue;
        }
        bool t = s.touched(cx, cy * 2), b = s.touched(cx, cy * 2 + 1);
        if (!t && !b) continue;
        Ink kt = t ? s.at(cx, cy * 2) : Ink(), kb = b ? s.at(cx, cy * 2 + 1) : Ink();
        if (t && b && (kt == kb || !mode_.color)) {
          if (kt.level == 0) out.put(x, y, SHADE_U[0], kt.a);
          else if (mode_.color) out.put(x, y, SHADE_U[kt.level & 3], kt.a, kt.b);
          else out.put(x, y, SHADE_U[kt.level & 3], kt.a);
        } else if (t && b) {
          // Two colours in one cell: upper half block over a background.  Put
          // the dim one in the background, where the Linux VT can show it.
          if (kb.a >= 8 && kt.a < 8) out.put(x, y, U'▄', kb.a, kt.a);
          else out.put(x, y, U'▀', kt.a, kb.a);
        } else if (t) {
          out.put(x, y, U'▀', kt.a);
        } else {
          out.put(x, y, U'▄', kb.a);
        }
      }
  }

  for (const TextItem &t : texts_) {
    int x = static_cast<int>(std::lround(t.x)), y = static_cast<int>(std::lround(t.y));
    if (t.bg >= 0) out.text_bg(x, y, t.s, t.fg, static_cast<uint8_t>(t.bg));
    else out.text(x, y, t.s, t.fg);
  }
  return out;
}

Image Scene::to_image() const {
  const uint8_t base = S.slide_bg == BG_NONE ? 0 : S.slide_bg;
  Image im(cv.w() * 8, cv.h() * 16, base);
  for (int y = 0; y < cv.h(); y++)
    for (int x = 0; x < cv.w(); x++) {
      const Cell &c = cv.at(x, y);
      uint8_t bg = c.bg == BG_NONE ? base : c.bg;
      if (bg != base) im.fill_rect(x * 8, y * 16, 8, 16, bg);
      if (c.ch != U' ') im.glyph(x * 8, y * 16, c.ch, c.fg, -1, 1);
    }

  for (const Layer &l : layers_) {
    const Surface &s = *l.s;
    int ox = l.r.x * 8, oy = l.r.y * 16;
    int kx = 8 / s.sx(), ky = 16 / s.sy(); // a coarse surface is blown up
    for (int y = 0; y < s.h(); y++)
      for (int x = 0; x < s.w(); x++) {
        if (!s.touched(x, y)) continue;
        uint8_t c = s.resolve(x, y);
        if (kx == 1 && ky == 1) im.set(ox + x, oy + y, c);
        else im.fill_rect(ox + x * kx, oy + y * ky, kx, ky, c);
      }
  }

  for (const TextItem &t : texts_) {
    int x = static_cast<int>(std::lround(t.x * 8)), y = static_cast<int>(std::lround(t.y * 16));
    for (char32_t cp : utf8_decode(t.s)) {
      im.glyph(x, y, cp, t.fg, t.bg, t.scale);
      x += 8 * t.scale;
    }
  }
  return im;
}

} // namespace ch
