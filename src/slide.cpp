// slide.cpp -- laying a slide out and drawing it into a Scene.
#include <algorithm>
#include <cmath>

#include "deck.hpp"
#include "util.hpp"

namespace ch {

namespace {

// ---- layout ------------------------------------------------------------------

// Room a framed block needs beyond its own rectangle for the drop shadow.
int shadow_w() { return S.slide_bg == BG_NONE ? 0 : 2; }
int shadow_h() { return S.slide_bg == BG_NONE ? 0 : 1; }

void place(std::vector<Block> &blocks, Rect body, const std::string &layout);

void place_one(Block &b, Rect r) {
  b.r = r;
  if (b.kind == Block::ROWS) place(b.kids, r, "rows");
  else if (b.kind == Block::COLS) place(b.kids, r, "cols");
}

void split(std::vector<Block *> &flow, Rect body, bool across) {
  const int n = static_cast<int>(flow.size());
  const int gap = across ? 1 + shadow_w() : shadow_h();
  double total = 0;
  for (Block *b : flow) total += b->weight;
  int room = (across ? body.w : body.h) - gap * (n - 1);
  double used = 0;
  int pos = across ? body.x : body.y;
  for (int i = 0; i < n; i++) {
    used += flow[static_cast<std::size_t>(i)]->weight;
    int end = (across ? body.x : body.y) + static_cast<int>(std::lround(room * used / total)) + gap * i;
    Rect r = across ? Rect{pos, body.y, end - pos, body.h} : Rect{body.x, pos, body.w, end - pos};
    place_one(*flow[static_cast<std::size_t>(i)], r);
    pos = end + gap;
  }
}

void place(std::vector<Block> &blocks, Rect body, const std::string &layout) {
  std::vector<Block *> flow;
  for (auto &b : blocks) {
    if (!b.has_at) { flow.push_back(&b); continue; }
    int x0 = body.x + static_cast<int>(std::lround(b.at[0] / 12.0 * body.w));
    int y0 = body.y + static_cast<int>(std::lround(b.at[1] / 12.0 * body.h));
    int x1 = body.x + static_cast<int>(std::lround((b.at[0] + b.at[2]) / 12.0 * body.w));
    int y1 = body.y + static_cast<int>(std::lround((b.at[1] + b.at[3]) / 12.0 * body.h));
    // leave the gutter between neighbours, but not at the slide's own edge
    if (b.at[0] + b.at[2] < 11.99) x1 -= 1 + shadow_w();
    if (b.at[1] + b.at[3] < 11.99) y1 -= shadow_h();
    place_one(b, Rect{x0, y0, x1 - x0, y1 - y0});
  }
  const int n = static_cast<int>(flow.size());
  if (n == 0) return;
  std::string how = layout;
  if (how == "auto") how = n <= 3 ? "cols" : "grid";
  if (how == "cols" || how == "rows") {
    split(flow, body, how == "cols");
    return;
  }
  int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
  int rows = (n + cols - 1) / cols;
  std::vector<Block> bands(static_cast<std::size_t>(rows));
  std::vector<Block *> band_ptrs;
  for (auto &b : bands) band_ptrs.push_back(&b);
  split(band_ptrs, body, false);
  for (int r = 0; r < rows; r++) {
    std::vector<Block *> row;
    for (int c = 0; c < cols && r * cols + c < n; c++) row.push_back(flow[static_cast<std::size_t>(r * cols + c)]);
    split(row, bands[static_cast<std::size_t>(r)].r, true);
  }
}

// ---- text --------------------------------------------------------------------

struct Glyph32 {
  char32_t c;
  bool strong;
};

// "**x**" marks emphasis; the markers themselves are not drawn.
std::vector<Glyph32> inline_marks(const std::string &s) {
  std::vector<Glyph32> out;
  auto cps = utf8_decode(s);
  bool strong = false;
  for (std::size_t i = 0; i < cps.size(); i++) {
    if (cps[i] == U'*' && i + 1 < cps.size() && cps[i + 1] == U'*') { strong = !strong; i++; continue; }
    if (cell_of(cps[i])) out.push_back({cps[i], strong}); // zero width takes no cell
  }
  return out;
}

std::string inline_plain(const std::string &s) {
  std::string out;
  for (const auto &g : inline_marks(s)) out += u32_to_utf8(g.c);
  return out;
}

std::vector<std::vector<Glyph32>> wrap(const std::vector<Glyph32> &g, std::size_t width) {
  std::vector<std::vector<Glyph32>> lines(1);
  if (width < 1) width = 1;
  std::size_t i = 0;
  while (i < g.size()) {
    std::size_t e = i;
    while (e < g.size() && g[e].c != U' ') e++;
    std::size_t wl = e - i;
    auto &cur = lines.back();
    if (!cur.empty() && cur.size() + 1 + wl > width) lines.emplace_back();
    auto &line = lines.back();
    if (!line.empty()) line.push_back({U' ', false});
    for (std::size_t k = i; k < e; k++) {
      if (lines.back().size() >= width) lines.emplace_back();
      lines.back().push_back(g[k]);
    }
    i = e;
    while (i < g.size() && g[i].c == U' ') i++;
  }
  return lines;
}

struct TextLine {
  std::vector<Glyph32> g;
  uint8_t fg = 15;
  int scale = 1;
  int indent = 0;
  char32_t bullet = 0;
};

void draw_text_block(Scene &sc, const Block &b, Rect r, uint8_t frame) {
  const bool px = sc.mode().pixel;
  uint8_t base = b.color >= 0 ? static_cast<uint8_t>(b.color) : S.text;
  if (b.box) {
    if (S.slide_bg != BG_NONE) {
      sc.cv.shadow(r.x, r.y, r.w, r.h);
      sc.panel(r, S.panel_bg);
    }
    sc.cv.box(r.x, r.y, r.w, r.h, sc.mode().ascii ? BOX_ASCII : BOX_SINGLE, frame);
    if (!b.title.empty()) sc.cv.text_c(r.x + 2, r.y, r.w - 4, " " + b.title + " ", S.title);
    r = Rect{r.x + 2, r.y + 1, r.w - 4, r.h - 2};
    if (b.color < 0 && S.slide_bg != BG_NONE) base = contrast_on(S.panel_bg) == 0 ? 0 : 7;
  } else if (!b.title.empty()) {
    sc.cv.text(r.x, r.y, trunc_to(b.title, static_cast<std::size_t>(r.w)), S.heading);
    r.y += 2;
    r.h -= 2;
  }
  if (r.w < 4 || r.h < 1) return;

  std::vector<TextLine> out;
  for (const std::string &raw : b.lines) {
    std::string s = raw;
    TextLine proto;
    proto.fg = base;
    proto.scale = px ? b.size : 1;
    std::string t = trim(s);
    if (starts_with(t, "## ")) { proto.fg = S.accent; s = t.substr(3); }
    else if (starts_with(t, "# ")) {
      proto.fg = S.heading;
      s = t.substr(2);
      // double size when the heading fits on one line that way, else normal
      int want = px ? std::max(2, b.size) : 1;
      proto.scale = (static_cast<int>(cp_len(inline_plain(s))) * want <= r.w) ? want : (px ? b.size : 1);
    }
    else if (starts_with(t, "- ") || starts_with(t, "* ")) {
      proto.bullet = sc.mode().ascii ? U'*' : U'■';
      proto.indent = 2;
      s = t.substr(2);
    } else if (starts_with(t, "> ")) { proto.fg = S.dim; proto.indent = 2; s = t.substr(2); }
    if (t.empty()) { out.push_back(proto); continue; }
    std::size_t width = static_cast<std::size_t>(std::max(1, r.w / proto.scale - proto.indent));
    bool first = true;
    for (auto &line : wrap(inline_marks(s), width)) {
      TextLine l = proto;
      l.g = line;
      if (!first) l.bullet = 0;
      first = false;
      out.push_back(l);
    }
  }

  int total = 0;
  for (const auto &l : out) total += l.scale;
  int y = r.y + (b.middle ? std::max(0, (r.h - total) / 2) : 0);
  for (const auto &l : out) {
    if (y + l.scale > r.y + r.h) {
      sc.cv.text_r(r.x, r.y + r.h - 1, r.w, "...", S.dim);
      sc.fit_note("the text does not fit its block: " + std::to_string(total) + " rows of text, " + std::to_string(r.h) +
                  " rows of room");
      break;
    }
    int len = (static_cast<int>(l.g.size()) + l.indent) * l.scale;
    int x = r.x + (b.align < 0 ? 0 : (b.align == 0 ? (r.w - len) / 2 : r.w - len));
    if (x < r.x) x = r.x;
    if (l.bullet) sc.text(x, y, u32_to_utf8(l.bullet), S.bullet, -1, l.scale);
    x += l.indent * l.scale;
    // runs of one emphasis each
    std::size_t i = 0;
    while (i < l.g.size()) {
      std::size_t e = i;
      std::string run;
      while (e < l.g.size() && l.g[e].strong == l.g[i].strong) run += u32_to_utf8(l.g[e++].c);
      sc.text(x, y, run, l.g[i].strong ? S.accent : l.fg, -1, l.scale);
      x += static_cast<int>(e - i) * l.scale;
      i = e;
    }
    y += l.scale;
  }
}

void draw_stat(Scene &sc, const Block &b, Rect r, uint8_t frame) {
  if (S.slide_bg != BG_NONE) {
    sc.cv.shadow(r.x, r.y, r.w, r.h);
    sc.panel(r, S.panel_bg);
  }
  sc.cv.box(r.x, r.y, r.w, r.h, sc.mode().ascii ? BOX_ASCII : BOX_SINGLE, frame);
  if (!b.title.empty()) sc.cv.text_c(r.x + 2, r.y, r.w - 4, " " + b.title + " ", S.title);
  Rect in{r.x + 2, r.y + 1, r.w - 4, r.h - 2};
  if (in.w < 2 || in.h < 1) return;
  int len = std::max(1, static_cast<int>(cp_len(b.value)));
  int extra = (b.label.empty() ? 0 : 1) + (b.delta.empty() ? 0 : 1);
  int k = 1;
  if (sc.mode().pixel)
    for (int t = 4; t >= 1; t--)
      if (len * t <= in.w && t + extra <= in.h) { k = t; break; }
  int total = k + extra;
  int y = in.y + std::max(0, (in.h - total) / 2);
  uint8_t fg = b.color >= 0 ? static_cast<uint8_t>(b.color) : S.title;
  if (len > in.w) sc.fit_note("stat \"" + b.value + "\" is cut short: " + std::to_string(in.w) + " characters fit");
  if (static_cast<int>(cp_len(b.label)) > in.w)
    sc.fit_note("stat label \"" + b.label + "\" is cut short: " + std::to_string(in.w) + " characters fit");
  sc.big(in.x, y, in.w, b.value, fg, k, 0);
  y += k;
  const bool light = contrast_on(S.panel_bg) == 0 && S.slide_bg != BG_NONE;
  if (!b.label.empty() && y <= in.bottom()) sc.cv.text_c(in.x, y++, in.w, b.label, light ? 0 : 7);
  if (!b.delta.empty() && y <= in.bottom()) {
    std::string t = trim(b.delta);
    bool down = !t.empty() && (t[0] == '-' || starts_with(t, "−") || starts_with(t, "▼"));
    bool up = !t.empty() && (t[0] == '+' || starts_with(t, "▲"));
    uint8_t c = down ? (light ? 1 : 9) : (up ? (light ? 2 : 10) : S.dim);
    std::string arrow = sc.mode().ascii ? "" : (down ? "▼ " : (up ? "▲ " : ""));
    sc.cv.text_c(in.x, y, in.w, arrow + t, c);
  }
}

void draw_blocks(Scene &sc, Deck &d, std::vector<Block> &blocks, const SlideView &v, const Block *focus) {
  for (auto &b : blocks) {
    Rect r = b.r;
    sc.where = b.path;
    if (r.w < 4 || r.h < 2) { sc.fit_note("no room left for this block on the slide"); continue; }
    const uint8_t frame = &b == focus ? S.accent : S.frame; // the focused block stands out
    switch (b.kind) {
    case Block::ROWS:
    case Block::COLS: draw_blocks(sc, d, b.kids, v, focus); break;
    case Block::TEXT:
    case Block::STAT: {
      if (&b == focus && b.kind == Block::TEXT && !b.box)
        sc.cv.box(r.x - 1, r.y - 1, r.w + 2, r.h + 2, sc.mode().ascii ? BOX_ASCII : BOX_SINGLE, S.accent);
      if (b.kind == Block::TEXT) draw_text_block(sc, b, r, frame);
      else draw_stat(sc, b, r, frame);
      break;
    }
    case Block::SHAPES:
    case Block::FLOW: {
      // on the slide itself, or in a window of its own with "box"
      uint8_t under = S.slide_bg == BG_NONE ? 0 : S.slide_bg;
      Rect in = r;
      if (b.box) {
        in.w -= shadow_w();
        in.h -= shadow_h();
        if (S.slide_bg != BG_NONE) {
          sc.cv.shadow(in.x, in.y, in.w, in.h);
          sc.panel(in, S.panel_bg);
          under = S.panel_bg;
        }
        sc.cv.box(in.x, in.y, in.w, in.h, sc.mode().ascii ? BOX_ASCII : BOX_DOUBLE, frame);
        if (!b.title.empty()) sc.cv.text_c(in.x + 2, in.y, in.w - 4, " " + b.title + " ", S.title);
        in = Rect{in.x + 2, in.y + 1, in.w - 4, in.h - 2};
      } else {
        if (&b == focus) sc.cv.box(r.x - 1, r.y - 1, r.w + 2, r.h + 2, sc.mode().ascii ? BOX_ASCII : BOX_SINGLE, S.accent);
        if (!b.title.empty()) {
          sc.cv.text(r.x, r.y, trunc_to(b.title, static_cast<std::size_t>(r.w)), S.heading);
          in.y += 2;
          in.h -= 2;
        }
      }
      if (b.kind == Block::SHAPES) draw_shapes(sc, in, b.shapes, under);
      else draw_flow(sc, in, b.flow, under);
      break;
    }
    case Block::CHART: {
      r.w -= shadow_w();
      r.h -= shadow_h();
      RenderOpts o = block_opts(d, b, v);
      o.color = o.color && sc.mode().color;
      o.frame_color = frame;
      if (&b == focus) {
        o.cur_series = v.cur_series;
        o.cur_index = v.cur_index;
      }
      if (!b.error.empty()) {
        if (S.slide_bg != BG_NONE) sc.panel(r, S.panel_bg);
        sc.cv.box(r.x, r.y, r.w, r.h, sc.mode().ascii ? BOX_ASCII : BOX_DOUBLE, 9);
        sc.cv.text_c(r.x + 2, r.y, r.w - 4, " cannot draw this chart ", 9);
        Block msg;
        if (!b.data_ref.empty()) msg.lines = {"**" + b.data_ref + "**", ""};
        msg.lines.push_back(b.error);
        msg.lines.push_back("");
        msg.lines.push_back("> " + b.path);
        msg.align = 0;
        msg.middle = true;
        msg.color = 15;
        draw_text_block(sc, msg, Rect{r.x + 3, r.y + 1, r.w - 6, r.h - 2}, S.frame);
      } else {
        render_chart(sc, r, b.ds, o);
      }
      break;
    }
    }
  }
}

// The DOS bottom line: a message on the left, "key meaning" hints and the
// slide counter on the right, with the keys picked out in colour.
} // namespace

void draw_status_bar(Scene &sc, const std::string &left, const std::string &hint, const std::string &pos, bool bad) {
  Canvas &cv = sc.cv;
  const int W = cv.w(), y = cv.h() - 1;
  const uint8_t fg = bad ? 15 : S.bar_fg, bg = bad ? 1 : S.bar_bg;
  for (int x = 0; x < W; x++) cv.put(x, y, U' ', fg, bg);
  int px = W - static_cast<int>(cp_len(pos)) - 1;
  cv.text_bg(px, y, pos, fg, bg);

  // hints are "key meaning" pairs separated by two spaces; drop from the right
  // until they fit beside at least a little of the message
  std::vector<std::string> items;
  for (std::size_t i = 0; i < hint.size();) {
    std::size_t e = hint.find("  ", i);
    if (e == std::string::npos) e = hint.size();
    if (e > i) items.push_back(hint.substr(i, e - i));
    i = e + 2;
  }
  auto width = [&]() {
    int w = 0;
    for (const auto &it : items) w += static_cast<int>(cp_len(it)) + 2;
    return w;
  };
  // something just said ("saved ...") outranks the hints; the footer does not
  const int left_min = std::min(static_cast<int>(cp_len(left)), bad || hint.empty() ? 200 : 40);
  while (!items.empty() && width() > px - 2 - left_min) items.pop_back();
  int x = px - 1 - width();
  cv.text_bg(1, y, trunc_to(left, static_cast<std::size_t>(std::max(0, x - 2))), fg, bg);
  for (const auto &it : items) {
    std::size_t sp = it.find(' ');
    std::string key = it.substr(0, sp), rest = sp == std::string::npos ? "" : it.substr(sp);
    cv.text_bg(x, y, key, S.bar_key, bg);
    cv.text_bg(x + static_cast<int>(cp_len(key)), y, rest, fg, bg);
    x += static_cast<int>(cp_len(it)) + 2;
  }
}

RenderOpts block_opts(const Deck &d, const Block &b, const SlideView &v) {
  RenderOpts o;
  if (!d.palette.empty()) o.palette = d.palette;
  apply_spec(b.ds.spec, o);
  apply_spec(b.spec, o);
  if (v.cli) apply_spec(*v.cli, o);
  if (o.title.empty() && !b.spec.has_title && !(v.cli && v.cli->has_title)) o.title = b.ds.title;
  // Files straight off the command line say where they came from, the way a
  // printout carries its file name; a deck has titles for that.
  if (d.implicit && d.file.empty()) o.source = b.data_ref;
  return o;
}

void render_slide(Scene &sc, Deck &d, int index, const SlideView &v) {
  const int W = sc.cols(), H = sc.rows();
  if (d.slides.empty()) {
    sc.cv.text_c(0, H / 2, W, "this deck has no slides", S.text);
    return;
  }
  index = std::max(0, std::min(index, static_cast<int>(d.slides.size()) - 1));
  Slide &s = d.slides[static_cast<std::size_t>(index)];
  const bool px = sc.mode().pixel;
  const bool bare = d.implicit && s.title.empty(); // a data file shown as itself
  const int margin = bare ? (S.slide_bg == BG_NONE ? 0 : 1) : 2;

  int top = bare ? (S.slide_bg == BG_NONE ? 0 : 1) : 1;
  int bottom = H - (v.chrome ? 1 : 0);

  if (s.blocks.empty()) {
    // A title slide.
    int k = px ? (static_cast<int>(cp_len(s.title)) * 3 <= W - 8 ? 3 : 2) : 1;
    int y = std::max(1, (bottom - k - 4) / 2);
    sc.big(2, y, W - 4, s.title, S.heading, k, 0);
    y += k + 1;
    int rule = std::min(W - 12, std::max(20, static_cast<int>(cp_len(s.title)) * k + 8));
    sc.cv.hline((W - rule) / 2, y, rule, sc.mode().ascii ? U'=' : G.dh, S.accent);
    y += 2;
    Block sub;
    sub.lines = split(s.subtitle, '\n');
    sub.align = 0;
    if (!s.subtitle.empty()) draw_text_block(sc, sub, Rect{6, y, W - 12, std::max(1, bottom - y - 1)}, S.frame);
  } else {
    if (!s.title.empty()) {
      int k = px ? 2 : 1;
      sc.where = s.path + ".title";
      int fits = (W - 2 * margin - 2) / k;
      if (static_cast<int>(cp_len(s.title)) > fits)
        sc.fit_note("the title is cut short: " + std::to_string(cp_len(s.title)) + " characters, " + std::to_string(fits) + " fit");
      sc.big(margin + 1, top, W - 2 * margin - 2, s.title, S.heading, k, -1);
      top += k;
      if (!s.subtitle.empty()) {
        sc.cv.text(margin + 1, top, trunc_to(s.subtitle, static_cast<std::size_t>(W - 2 * margin - 2)), S.dim);
        top++;
      }
      top++;
    }
    Rect body{margin, top, W - 2 * margin, bottom - top - (bare ? 0 : 1)};
    if (S.slide_bg == BG_NONE && bare) body = Rect{0, top, W, bottom - top};
    place(s.blocks, body, s.layout);
    draw_blocks(sc, d, s.blocks, v, v.focus);
  }

  if (v.notes && !s.notes.empty()) {
    int h = std::min(H - 4, std::max(6, H / 3));
    Rect r{4, bottom - h - 1, W - 8, h};
    sc.cover(Rect{r.x, r.y, r.w + 2, r.h + 1});
    Block n;
    n.box = true;
    n.title = "notes";
    n.lines = split(s.notes, '\n');
    draw_text_block(sc, n, r, S.frame);
  }

  if (v.chrome) {
    std::string left = v.message;
    if (left.empty()) {
      left = d.footer.empty() ? d.title : d.footer;
      if (left.empty() && !d.file.empty()) left = d.file.substr(d.file.find_last_of('/') + 1);
    }
    std::string pos = std::to_string(index + 1) + "/" + std::to_string(d.slides.size());
    draw_status_bar(sc, left, v.hint, pos, v.message_bad);
  }
}

} // namespace ch
