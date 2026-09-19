// diagram.cpp -- drawing shapes and laying out flows.  Everything that is a
// mark goes on one Surface over the block, so it is pixels on the console and
// half blocks anywhere else, like the charts.  Flow boxes are cells: DOS
// windows with a frame and a shadow, the same as every other box on a slide.
#include "diagram.hpp"

#include <algorithm>
#include <cmath>

#include "util.hpp"

namespace ch {

namespace {

constexpr double PI = 3.14159265358979323846;

Pt shifted(Pt p, double dx, double dy) { return Pt{p.x + dx, p.y + dy}; }

std::vector<Pt> shifted(const std::vector<Pt> &v, double dx, double dy) {
  std::vector<Pt> o;
  o.reserve(v.size());
  for (const Pt &p : v) o.push_back(shifted(p, dx, dy));
  return o;
}

// Dashes read as "maybe", "later" or "not yet".
void stroke(Surface &sf, Pt a, Pt b, Ink k, int w, bool dash, bool fine) {
  if (!dash) { sf.line(a.x, a.y, b.x, b.y, k, w); return; }
  const double len = std::hypot(b.x - a.x, b.y - a.y), on = fine ? 7 : 2, off = fine ? 5 : 1;
  if (len < 1e-9) return;
  for (double t = 0; t < len; t += on + off) {
    double u = std::min(len, t + on);
    sf.line(a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len, a.x + (b.x - a.x) * u / len,
            a.y + (b.y - a.y) * u / len, k, w);
  }
}

// A filled triangle whose point sits on tip, pointing away from `from`.
void arrowhead(Surface &sf, Pt tip, Pt from, Ink k, double size) {
  double dx = tip.x - from.x, dy = tip.y - from.y, len = std::hypot(dx, dy);
  if (len < 1e-9) return;
  dx /= len;
  dy /= len;
  const Pt base{tip.x - dx * size, tip.y - dy * size};
  const double half = size * 0.55;
  sf.poly({tip, {base.x - dy * half, base.y + dx * half}, {base.x + dy * half, base.y - dx * half}}, k);
}

// A point `by` back from `to` towards `from`: where a line stops so the head
// in front of it keeps its point.
Pt back_off(Pt to, Pt from, double by) {
  double dx = to.x - from.x, dy = to.y - from.y, len = std::hypot(dx, dy);
  if (len <= by) return from;
  return Pt{to.x - dx / len * by, to.y - dy / len * by};
}

// A polyline with optional heads; the line stops short of each head.
void arrow(Surface &sf, std::vector<Pt> p, Ink k, int w, bool dash, bool head, bool tail, bool fine) {
  if (p.size() < 2) return;
  const double hs = fine ? 7 + 2.5 * w : 2;
  const Pt tip = p.back(), before = p[p.size() - 2], tail_tip = p.front(), after = p[1];
  if (head) p.back() = back_off(tip, before, hs * 0.8);
  if (tail) p.front() = back_off(tail_tip, after, hs * 0.8);
  for (std::size_t i = 0; i + 1 < p.size(); i++) stroke(sf, p[i], p[i + 1], k, w, dash, fine);
  if (head) arrowhead(sf, tip, before, k, hs);
  if (tail) arrowhead(sf, tail_tip, after, k, hs);
}

// Faces seen from the upper right, lit from above, as the bars are.
void extrude(Surface &sf, const std::vector<Pt> &o, double dx, double dy, uint8_t c) {
  Pt mid{0, 0};
  for (const Pt &p : o) { mid.x += p.x / o.size(); mid.y += p.y / o.size(); }
  const std::vector<Pt> back = shifted(o, dx, -dy);
  for (std::size_t i = 0; i < o.size(); i++) {
    std::size_t j = (i + 1) % o.size();
    double nx = o[j].y - o[i].y, ny = o[i].x - o[j].x; // a normal to the edge ...
    double cx = (o[i].x + o[j].x) / 2 - mid.x, cy = (o[i].y + o[j].y) / 2 - mid.y;
    if (nx * cx + ny * cy < 0) { nx = -nx; ny = -ny; } // ... pointing outwards
    if (nx * dx - ny * dy <= 0) continue;              // faces away from the eye
    sf.poly({o[i], o[j], back[j], back[i]}, ny < -std::fabs(nx) * 0.5 ? top_ink(c) : side_ink(c));
  }
}

struct Lines {
  std::vector<std::string> v;
  int w = 0;
};

Lines lines_of(const std::string &text, int width) {
  Lines l;
  l.v = wrap_words(text, static_cast<std::size_t>(std::max(1, width)));
  for (const auto &s : l.v) l.w = std::max(l.w, static_cast<int>(cp_len(s)));
  return l;
}

// Text in cells: each line placed by its anchor x (align -1 left, 0 centre,
// 1 right) from row y down.  Cell output rounds to whole cells.
void put_lines(Scene &sc, double x, double y, const Lines &l, uint8_t fg, int bg, int scale, int align) {
  const bool px = sc.mode().pixel;
  const int k = px ? scale : 1;
  for (std::size_t i = 0; i < l.v.size(); i++) {
    double len = static_cast<double>(cp_len(l.v[i])) * k;
    double tx = align < 0 ? x : (align == 0 ? x - len / 2 : x - len), ty = y + static_cast<double>(i) * k;
    if (!px) { tx = std::floor(tx + 0.5); ty = std::floor(ty + 0.5); }
    sc.text(tx, ty, l.v[i], fg, bg, k);
  }
}

void bounds(const std::vector<Pt> &v, Pt &lo, Pt &hi) {
  lo = hi = v.front();
  for (const Pt &p : v) {
    lo.x = std::min(lo.x, p.x);
    lo.y = std::min(lo.y, p.y);
    hi.x = std::max(hi.x, p.x);
    hi.y = std::max(hi.y, p.y);
  }
}

} // namespace

// ---- shapes --------------------------------------------------------------------

void draw_shapes(Scene &sc, Rect r, const std::vector<Shape> &shapes, uint8_t bg) {
  if (r.w < 2 || r.h < 2 || shapes.empty()) return;
  Surface &sf = sc.surface(r);
  const Mode &m = sc.mode();
  const bool fine = m.pixel;
  const double ux = sf.w() / 12.0, uy = sf.h() / 12.0;
  auto px = [&](Pt q) { return Pt{q.x * ux, q.y * uy}; };
  auto cells = [&](Pt p) { return Pt{r.x + p.x / m.sx(), r.y + p.y / m.sy()}; };
  const double sh = fine ? 8 : 1; // how far a drop shadow falls, down and right

  for (const Shape &s : shapes) {
    const int w = fine ? s.width : 1;
    const int scale = std::max(1, s.size);
    if (s.kind == Shape::LABEL) {
      Pt a = cells(px(s.pts[0]));
      uint8_t fg = static_cast<uint8_t>(s.text_color >= 0 ? s.text_color : (s.color >= 0 ? s.color : contrast_on(bg)));
      put_lines(sc, a.x, a.y, lines_of(s.text, 1000), fg, -1, scale, s.align);
      continue;
    }
    if (s.kind == Shape::LINE) {
      std::vector<Pt> p;
      for (const Pt &q : s.pts) p.push_back(px(q));
      Ink ink(static_cast<uint8_t>(s.color >= 0 ? s.color : contrast_on(bg)));
      if (s.shadow) arrow(sf, shifted(p, sh / 2, sh / 2), Ink(S.shadow), w, s.dash, s.head, s.tail, fine);
      arrow(sf, p, ink, w, s.dash, s.head, s.tail, fine);
      if (!s.text.empty()) {
        // above the middle of the middle segment, cutting the line it sits on
        std::size_t k = (p.size() - 1) / 2;
        Pt mid = cells(Pt{(p[k].x + p[k + 1].x) / 2, (p[k].y + p[k + 1].y) / 2});
        Lines l = lines_of(s.text, 24);
        uint8_t fg = static_cast<uint8_t>(s.text_color >= 0 ? s.text_color : ink.a);
        put_lines(sc, mid.x, mid.y - static_cast<double>(l.v.size()) * (fine ? scale : 1), l, fg, bg, scale, 0);
      }
      continue;
    }

    // closed shapes
    std::vector<Pt> o;
    if (s.kind == Shape::POLY) {
      for (const Pt &q : s.pts) o.push_back(px(q));
    } else {
      Pt a = px(s.pts[0]), b = px(s.pts[1]);
      if (s.kind == Shape::RECT) {
        o = {a, {b.x, a.y}, b, {a.x, b.y}};
      } else {
        const Pt c{(a.x + b.x) / 2, (a.y + b.y) / 2};
        const double rx = std::fabs(b.x - a.x) / 2, ry = std::fabs(b.y - a.y) / 2;
        const int n = fine ? 72 : 32;
        for (int i = 0; i < n; i++) o.push_back(Pt{c.x + rx * std::cos(2 * PI * i / n), c.y + ry * std::sin(2 * PI * i / n)});
      }
    }
    // Plain, a shape is a DOS window: panel inside, frame round it.  Given a
    // colour it is a solid of that colour.
    const bool plain = s.color < 0;
    const uint8_t fillc = static_cast<uint8_t>(plain ? S.panel_bg : s.color);
    const int borderc = s.border >= 0 ? s.border : (plain || !s.fill ? (plain ? S.frame : fillc) : -1);
    if (s.shadow) sf.poly(shifted(o, sh, sh), Ink(S.shadow));
    if (s.depth > 0 && fine) extrude(sf, o, s.depth * 3.0, s.depth * 2.1, fillc);
    if (s.fill) sf.poly(o, s.dither ? Ink(fillc, bg, static_cast<uint8_t>(s.dither)) : Ink(fillc));
    if (borderc >= 0)
      for (std::size_t i = 0; i < o.size(); i++)
        stroke(sf, o[i], o[(i + 1) % o.size()], Ink(static_cast<uint8_t>(borderc)), w, s.dash, fine);
    if (s.text.empty()) continue;
    Pt lo, hi;
    bounds(o, lo, hi);
    Pt c0 = cells(lo), c1 = cells(hi);
    const int k = fine ? scale : 1;
    const int room = static_cast<int>((c1.x - c0.x) / k) - 2;
    Lines l = lines_of(s.text, std::max(1, room));
    const double tall = static_cast<double>(l.v.size()) * k;
    if (l.w > room || tall > c1.y - c0.y)
      sc.fit_note("the text \"" + trunc_to(s.text, 30) + "\" does not fit its shape: make the shape bigger or the text shorter");
    uint8_t fg = static_cast<uint8_t>(s.text_color >= 0 ? s.text_color : contrast_on(s.fill && !s.dither ? fillc : bg));
    put_lines(sc, (c0.x + c1.x) / 2, (c0.y + c1.y) / 2 - tall / 2, l, fg, -1, scale, 0);
  }
}

// ---- flow ------------------------------------------------------------------------

namespace {

struct Placed {
  Rect r;     // cells, frame included
  Lines text;
  int layer = 0;
};

// Layers by longest path from the sources, with the edges that close a loop
// left out so a cycle still lays out.  Order within a layer follows the
// order the nodes were listed in, nudged towards their parents.
std::vector<std::vector<int>> layer_nodes(const Flow &f, std::vector<int> &layer, std::vector<bool> &back) {
  const int n = static_cast<int>(f.nodes.size());
  std::vector<std::vector<int>> out(static_cast<std::size_t>(n));
  for (std::size_t e = 0; e < f.edges.size(); e++) out[static_cast<std::size_t>(f.edges[e].from)].push_back(static_cast<int>(e));
  back.assign(f.edges.size(), false);
  std::vector<int> state(static_cast<std::size_t>(n), 0), order;
  // iterative DFS: an edge into a node still on the stack closes a loop
  for (int s = 0; s < n; s++) {
    if (state[static_cast<std::size_t>(s)]) continue;
    std::vector<std::pair<int, std::size_t>> st{{s, 0}};
    state[static_cast<std::size_t>(s)] = 1;
    while (!st.empty()) {
      auto &top = st.back();
      const auto &edges = out[static_cast<std::size_t>(top.first)];
      if (top.second < edges.size()) {
        int e = edges[top.second++];
        int t = f.edges[static_cast<std::size_t>(e)].to;
        if (state[static_cast<std::size_t>(t)] == 1) back[static_cast<std::size_t>(e)] = true;
        else if (state[static_cast<std::size_t>(t)] == 0) {
          state[static_cast<std::size_t>(t)] = 1;
          st.push_back({t, 0});
        }
      } else {
        state[static_cast<std::size_t>(top.first)] = 2;
        order.push_back(top.first);
        st.pop_back();
      }
    }
  }
  std::reverse(order.begin(), order.end()); // a topological order of the forward edges
  layer.assign(static_cast<std::size_t>(n), 0);
  for (int v : order)
    for (int e : out[static_cast<std::size_t>(v)])
      if (!back[static_cast<std::size_t>(e)]) {
        int t = f.edges[static_cast<std::size_t>(e)].to;
        layer[static_cast<std::size_t>(t)] = std::max(layer[static_cast<std::size_t>(t)], layer[static_cast<std::size_t>(v)] + 1);
      }
  int layers = 1;
  for (int l : layer) layers = std::max(layers, l + 1);
  std::vector<std::vector<int>> by(static_cast<std::size_t>(layers));
  for (int v = 0; v < n; v++) by[static_cast<std::size_t>(layer[static_cast<std::size_t>(v)])].push_back(v);
  // one pass of the barycentre heuristic: fewer crossings, same listed order on ties
  std::vector<double> pos(static_cast<std::size_t>(n), 0);
  for (std::size_t l = 0; l < by.size(); l++) {
    if (l > 0) {
      std::vector<double> key(static_cast<std::size_t>(n), 1e9);
      for (int v : by[l]) {
        double sum = 0;
        int cnt = 0;
        for (const auto &e : f.edges)
          if (e.to == v && layer[static_cast<std::size_t>(e.from)] < static_cast<int>(l)) { sum += pos[static_cast<std::size_t>(e.from)]; cnt++; }
        key[static_cast<std::size_t>(v)] = cnt ? sum / cnt : 1e9;
      }
      std::stable_sort(by[l].begin(), by[l].end(), [&](int a, int b) { return key[static_cast<std::size_t>(a)] < key[static_cast<std::size_t>(b)]; });
    }
    for (std::size_t i = 0; i < by[l].size(); i++) pos[static_cast<std::size_t>(by[l][i])] = static_cast<double>(i);
  }
  return by;
}

} // namespace

void draw_flow(Scene &sc, Rect r, const Flow &f, uint8_t bg) {
  const int n = static_cast<int>(f.nodes.size());
  if (n == 0 || r.w < 6 || r.h < 3) return;
  const Mode &m = sc.mode();
  const bool fine = m.pixel, down = f.down;
  const int shw = S.slide_bg == BG_NONE ? 0 : 2, shh = S.slide_bg == BG_NONE ? 0 : 1; // drop shadow

  std::vector<int> layer;
  std::vector<bool> back;
  std::vector<std::vector<int>> by = layer_nodes(f, layer, back);
  const int L = static_cast<int>(by.size());
  std::size_t widest_layer = 1;
  for (const auto &l : by) widest_layer = std::max(widest_layer, l.size());

  // Box sizes: text wrapped to a share of the room, framed, one cell of air.
  const int along = down ? r.h : r.w, cross = down ? r.w : r.h;
  int cap = down ? std::max(8, (r.w - static_cast<int>(widest_layer - 1) * 4) / static_cast<int>(widest_layer)) - shw
                 : std::max(8, (r.w - (L - 1) * 6) / L) - shw;
  std::vector<Placed> box(static_cast<std::size_t>(n));
  for (int v = 0; v < n; v++) {
    Placed &b = box[static_cast<std::size_t>(v)];
    b.text = lines_of(f.nodes[static_cast<std::size_t>(v)].text, cap - 4);
    b.r.w = std::max(6, b.text.w + 4);
    b.r.h = static_cast<int>(b.text.v.size()) + 2;
    b.layer = layer[static_cast<std::size_t>(v)];
  }
  // how deep each layer is along the flow, and the gaps that leaves for arrows
  std::vector<int> depth(static_cast<std::size_t>(L), 0);
  for (int v = 0; v < n; v++) {
    const Placed &b = box[static_cast<std::size_t>(v)];
    int &d = depth[static_cast<std::size_t>(b.layer)];
    d = std::max(d, (down ? b.r.h + shh : b.r.w + shw));
  }
  int used = 0;
  for (int d : depth) used += d;
  std::size_t longest_label = 0;
  for (const auto &e : f.edges) longest_label = std::max(longest_label, cp_len(e.text));
  const int want_gap = down ? std::max(3, static_cast<int>(longest_label > 0 ? 4 : 3))
                            : std::max(6, static_cast<int>(longest_label) + 4);
  int gap = L > 1 ? (along - used) / (L - 1) : 0;
  if (L > 1 && gap < (down ? 2 : 4))
    sc.fit_note("the flow does not fit: " + std::to_string(L) + " steps need more room " + (down ? "down" : "across") +
                " (use \"dir\": \"" + (down ? "right" : "down") + "\", shorter names, or a bigger block)");
  gap = std::max(down ? 2 : 3, std::min(gap, want_gap * 2));
  int total = used + gap * (L - 1);
  int at = (down ? r.y : r.x) + std::max(0, (along - total) / 2);

  for (int l = 0; l < L; l++) {
    const auto &vs = by[static_cast<std::size_t>(l)];
    // across the flow: the layer's boxes spread over the room, centred
    int span = 0;
    for (int v : vs) span += (down ? box[static_cast<std::size_t>(v)].r.w + shw : box[static_cast<std::size_t>(v)].r.h + shh);
    const int k = static_cast<int>(vs.size());
    int cgap = k > 1 ? std::min((cross - span) / (k - 1), down ? 8 : 4) : 0;
    if (k > 1 && cross - span < (k - 1)) {
      cgap = 0;
      sc.fit_note("the flow does not fit: " + std::to_string(k) + " boxes side by side at one step");
    }
    int c = (down ? r.x : r.y) + std::max(0, (cross - span - cgap * (k - 1)) / 2);
    for (int v : vs) {
      Placed &b = box[static_cast<std::size_t>(v)];
      const int d = depth[static_cast<std::size_t>(l)] - (down ? shh : shw);
      if (down) { b.r.y = at + (d - b.r.h) / 2; b.r.x = c; c += b.r.w + shw + cgap; }
      else { b.r.x = at + (d - b.r.w) / 2; b.r.y = c; c += b.r.h + shh + cgap; }
    }
    at += depth[static_cast<std::size_t>(l)] + gap;
  }

  // the boxes, as windows
  const int style = m.ascii ? BOX_ASCII : BOX_DOUBLE;
  for (int v = 0; v < n; v++) {
    const Placed &b = box[static_cast<std::size_t>(v)];
    const int col = f.nodes[static_cast<std::size_t>(v)].color;
    const uint8_t fill = static_cast<uint8_t>(col >= 0 ? col : S.panel_bg);
    const uint8_t fg = col >= 0 ? contrast_on(fill) : contrast_on(S.panel_bg);
    if (shw) sc.cv.shadow(b.r.x, b.r.y, b.r.w, b.r.h);
    sc.panel(b.r, fill);
    sc.cv.box(b.r.x, b.r.y, b.r.w, b.r.h, style, col >= 0 ? fg : S.frame);
    for (std::size_t i = 0; i < b.text.v.size(); i++)
      sc.cv.text_c(b.r.x + 1, b.r.y + 1 + static_cast<int>(i), b.r.w - 2, b.text.v[i], fg);
  }

  // the arrows, in pixels over the lot
  Surface &sf = sc.surface(r);
  const double sx = m.sx(), sy = m.sy();
  auto P = [&](double cx, double cy) { return Pt{(cx - r.x) * sx, (cy - r.y) * sy}; };
  const int w = fine ? 2 : 1;
  for (std::size_t e = 0; e < f.edges.size(); e++) {
    const FlowEdge &fe = f.edges[e];
    const Placed &a = box[static_cast<std::size_t>(fe.from)], &b = box[static_cast<std::size_t>(fe.to)];
    const uint8_t c = static_cast<uint8_t>(fe.color >= 0 ? fe.color : contrast_on(bg));
    std::vector<Pt> path;
    Pt label;
    int label_align = 0;
    const double gap_px = (down ? gap * sy : gap * sx);
    if (!back[e] && b.layer > a.layer) {
      // out of one side, into the facing side, with a bend halfway across the gap
      if (down) {
        Pt s = P(a.r.x + a.r.w / 2.0, a.r.y + a.r.h), t = P(b.r.x + b.r.w / 2.0, b.r.y - (fine ? 0 : 1));
        s.y += fine ? 2 : 0;
        double mid = s.y + std::min(gap_px / 2, t.y - s.y);
        path = {s, {s.x, mid}, {t.x, mid}, t};
        label = Pt{r.x + t.x / sx + 1.5, r.y + (mid + t.y) / 2 / sy - 0.5};
        label_align = -1;
      } else {
        Pt s = P(a.r.x + a.r.w, a.r.y + a.r.h / 2.0), t = P(b.r.x - (fine ? 0 : 1), b.r.y + b.r.h / 2.0);
        s.x += fine ? 2 : 0;
        double mid = s.x + std::min(gap_px / 2, t.x - s.x);
        path = {s, {mid, s.y}, {mid, t.y}, t};
        double lx = (std::fabs(s.y - t.y) < 1 ? (s.x + t.x) / 2 : (mid + t.x) / 2);
        label = Pt{r.x + lx / sx, r.y + t.y / sy - 1.5};
      }
      // Nearly in line (boxes of different heights): run straight at a
      // height, or across at a position, that both boxes have.
      Pt &s0 = path[0], &t0 = path[3];
      if (down && std::fabs(s0.x - t0.x) < sx * 1.5) {
        double lo = std::max(a.r.x + 1, b.r.x + 1) * sx, hi = std::min(a.r.x + a.r.w - 1, b.r.x + b.r.w - 1) * sx;
        if (hi > lo) s0.x = t0.x = std::max(lo, std::min(hi - 1, (s0.x + t0.x) / 2));
      } else if (!down && std::fabs(s0.y - t0.y) < sy * 1.5) {
        double lo = (std::max(a.r.y + 1, b.r.y + 1) - r.y) * sy, hi = (std::min(a.r.y + a.r.h - 1, b.r.y + b.r.h - 1) - r.y) * sy;
        if (hi > lo) s0.y = t0.y = std::floor((std::max(lo, std::min(hi - 1, (s0.y + t0.y) / 2))) / sy) * sy + sy / 2;
        label.y = r.y + t0.y / sy - 1.5;
      }
      if (std::fabs(s0.x - t0.x) < 1 || std::fabs(s0.y - t0.y) < 1) path = {s0, t0};
    } else {
      // back and sideways: straight from the edge of one box to the edge of the other
      auto centre = [&](const Placed &p) { return P(p.r.x + p.r.w / 2.0, p.r.y + p.r.h / 2.0); };
      auto rim = [&](const Placed &p, Pt toward) {
        Pt c = centre(p);
        double dx = toward.x - c.x, dy = toward.y - c.y;
        double hx = p.r.w * sx / 2 + (fine ? 3 : 1), hy = p.r.h * sy / 2 + (fine ? 3 : 1);
        double t = 1.0 / std::max(std::fabs(dx) / hx, std::fabs(dy) / hy);
        return Pt{c.x + dx * t, c.y + dy * t};
      };
      Pt ca = centre(a), cb = centre(b);
      path = {rim(a, cb), rim(b, ca)};
      label = Pt{r.x + (path[0].x + path[1].x) / 2 / sx, r.y + (path[0].y + path[1].y) / 2 / sy - 1.5};
    }
    arrow(sf, path, Ink(c), w, fe.dash, true, false, fine);
    if (!fe.text.empty()) {
      Lines l = lines_of(fe.text, down ? std::max(6, r.w / 3) : std::max(6, gap + 6));
      if (!down && l.w > gap + 4)
        sc.fit_note("the label \"" + fe.text + "\" is wider than the gap it names; shorten it or use \"dir\": \"down\"");
      put_lines(sc, label.x, label.y - (static_cast<double>(l.v.size()) - 1), l, c, bg, 1, label_align);
    }
  }
}

} // namespace ch
