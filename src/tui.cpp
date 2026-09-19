// tui.cpp -- the presenter: page through a deck, edit its data, annotate it.
#include "tui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <unistd.h>

#include "sources.hpp"
#include "term.hpp"
#include "ttyguard.hpp"
#include "util.hpp"

namespace ch {

namespace {

// ---- a line of text being typed ------------------------------------------------

struct LineEdit {
  std::vector<std::string> c; // one UTF-8 character each
  std::size_t at = 0;

  void set(const std::string &s) {
    c.clear();
    for (char32_t cp : utf8_decode(s)) c.push_back(u32_to_utf8(cp));
    at = c.size();
  }
  std::string str() const {
    std::string s;
    for (const auto &x : c) s += x;
    return s;
  }
  // True when the key was one for the line.
  bool key(const std::string &k) {
    if (k == "left") { if (at > 0) at--; }
    else if (k == "right") { if (at < c.size()) at++; }
    else if (k == "home" || k == "ctrl-a") at = 0;
    else if (k == "end" || k == "ctrl-e") at = c.size();
    else if (k == "backspace") { if (at > 0) c.erase(c.begin() + static_cast<long>(--at)); }
    else if (k == "delete") { if (at < c.size()) c.erase(c.begin() + static_cast<long>(at)); }
    else if (k == "ctrl-u") { c.erase(c.begin(), c.begin() + static_cast<long>(at)); at = 0; }
    else if (k == "ctrl-k") c.erase(c.begin() + static_cast<long>(at), c.end());
    else if (printable(k)) c.insert(c.begin() + static_cast<long>(at++), k);
    else return false;
    return true;
  }
  static bool printable(const std::string &k) {
    if (k.empty()) return false;
    if (k.size() == 1) return static_cast<unsigned char>(k[0]) >= 32;
    return static_cast<unsigned char>(k[0]) >= 0xC0; // a UTF-8 character, not a key name
  }
  // Draw into w cells, scrolling so the caret stays visible.
  void draw(Canvas &cv, int x, int y, int w, uint8_t fg, uint8_t bg) const {
    std::size_t first = at >= static_cast<std::size_t>(w) ? at - static_cast<std::size_t>(w) + 1 : 0;
    for (int i = 0; i < w; i++) {
      std::size_t k = first + static_cast<std::size_t>(i);
      bool caret = k == at;
      std::string ch = k < c.size() ? c[k] : " ";
      cv.text_bg(x + i, y, ch, caret ? S.cursor_fg : fg, caret ? S.cursor_bg : bg);
    }
  }
};

// ---- dialogs -------------------------------------------------------------------

const uint8_t DLG_BG = 7, DLG_FG = 0, DLG_FIELD_BG = 4, DLG_FIELD_FG = 15;

Rect dialog_box(Scene &sc, int w, int h, const std::string &title) {
  Canvas &cv = sc.cv;
  w = std::min(w, cv.w() - 4);
  h = std::min(h, cv.h() - 2);
  int x = (cv.w() - w) / 2, y = (cv.h() - h) / 2;
  sc.cover(Rect{x, y, w + 2, h + 1}); // the shadow too
  cv.shadow(x, y, w, h);
  cv.fill_bg(x, y, w, h, DLG_BG);
  cv.box(x, y, w, h, sc.mode().ascii ? BOX_ASCII : BOX_DOUBLE, DLG_FG);
  if (!title.empty()) cv.text_c(x + 2, y, w - 4, " " + title + " ", DLG_FG);
  return Rect{x + 2, y + 1, w - 4, h - 2};
}

// ---- the presenter -------------------------------------------------------------

class Presenter {
public:
  Presenter(Deck &d, const PresentOpts &o) : deck_(d), o_(o), cli_(o.cli) {}

  int run() {
    std::string why;
    DisplayOpts dopt;
    dopt.want = o_.gfx;
    dopt.scale = o_.scale;
    dopt.color = o_.color;
    tty_guard_install();
    disp_ = open_display(dopt, why);
    RawMode raw(STDIN_FILENO);
    if (!raw.ok()) {
      disp_->close();
      std::fprintf(stderr, "charts: the presenter needs a terminal on stdin\n");
      return 2;
    }
    cursor_show(false);
    if (disp_->name() == "cells" || disp_->name() == "ascii") screen_clear();
    slide_ = std::max(0, std::min(o_.slide, static_cast<int>(deck_.slides.size()) - 1));
    if (!deck_.ok()) say(std::to_string(error_count()) + " problem(s) in this deck: run charts --check", true);
    else if (o_.verbose) say(why, false);

    int cols = 0, rows = 0;
    while (!quit_) {
      int c = 0, r = 0;
      disp_->grid(c, r);
      if (c != cols || r != rows) { cols = c; rows = r; dirty_ = true; }
      if (must_redraw()) dirty_ = true;
      poll_files();
      if (message_ttl_ > 0 && --message_ttl_ == 0) { message_.clear(); dirty_ = true; }
      if (dirty_) { draw(); dirty_ = false; }
      std::string k = read_key(250);
      if (k.empty() || k == "unknown") continue;
      handle(k);
      dirty_ = true;
    }
    disp_->close();
    cursor_show(true);
    if (disp_->name() == "cells" || disp_->name() == "ascii") screen_clear();
    return 0;
  }

private:
  enum Mode { VIEW, SHEET, ANNOTATE, HELP, OVERVIEW };

  Deck &deck_;
  PresentOpts o_;
  ChartSpec cli_;
  std::unique_ptr<Display> disp_;
  Mode mode_ = VIEW;
  int slide_ = 0, focus_ = -1;
  bool quit_ = false, dirty_ = true, notes_ = false;
  std::string message_;
  bool message_bad_ = false;
  int message_ttl_ = 0;
  std::string digits_; // a slide number being typed

  // sheet
  int row_ = 0, col_ = 0, top_ = 0, left_ = 0; // row -1 is the header, col 0 the labels
  bool editing_ = false;
  LineEdit edit_;
  std::vector<Dataset> undo_;

  // annotate
  int cur_s_ = 0, cur_i_ = 0;

  // overview
  int pick_ = 0;

  // a dialog in front of everything (ask / confirm / edit_text)
  std::string dlg_title_, dlg_label_;
  const LineEdit *dlg_line_ = nullptr;
  const std::vector<LineEdit> *dlg_lines_ = nullptr;
  std::size_t dlg_row_ = 0;
  std::string dlg_keys_;

  // ---- small things

  std::size_t error_count() const {
    std::size_t n = 0;
    for (const auto &i : deck_.issues)
      if (i.error) n++;
    return n;
  }
  void say(const std::string &m, bool bad) {
    message_ = m;
    message_bad_ = bad;
    message_ttl_ = bad ? 40 : 14; // ticks of 250 ms
  }
  Slide *slide() { return deck_.slides.empty() ? nullptr : &deck_.slides[static_cast<std::size_t>(slide_)]; }
  Block *focused() {
    Slide *s = slide();
    if (!s) return nullptr;
    std::vector<Block *> leaves = leaf_blocks(*s);
    if (leaves.empty()) return nullptr;
    if (focus_ < 0 || focus_ >= static_cast<int>(leaves.size())) return nullptr;
    return leaves[static_cast<std::size_t>(focus_)];
  }
  // The block a key is meant for: the focused one, else the slide's first of
  // that kind, which saves a Tab on the usual one-chart slide.
  Block *target(bool chart_only) {
    Block *b = focused();
    if (b && (!chart_only || b->kind == Block::CHART)) return b;
    Slide *s = slide();
    if (!s) return nullptr;
    std::vector<Block *> leaves = leaf_blocks(*s);
    for (std::size_t i = 0; i < leaves.size(); i++)
      if (!chart_only || leaves[i]->kind == Block::CHART) { focus_ = static_cast<int>(i); return leaves[i]; }
    return nullptr;
  }
  bool unsaved() const {
    if (deck_.dirty) return true;
    for (const auto &s : deck_.slides)
      for (Block *b : chart_blocks(const_cast<Slide &>(s)))
        if (b->data_dirty) return true;
    return false;
  }
  void go(int n) {
    n = std::max(0, std::min(n, static_cast<int>(deck_.slides.size()) - 1));
    if (n != slide_) { slide_ = n; focus_ = -1; }
  }

  void poll_files() {
    if (!deck_.file.empty() && !deck_.implicit) {
      long long m = file_mtime(deck_.file);
      if (m != deck_.mtime && m != 0) {
        if (unsaved()) {
          deck_.mtime = m;
          say("the deck changed on disk; you have unsaved edits (r reloads and drops them)", true);
        } else {
          reload();
        }
        dirty_ = true;
      }
    }
    if (refresh_data(deck_, o_.lo)) dirty_ = true;
  }

  void reload() {
    if (deck_.implicit || deck_.file.empty()) {
      for (auto &s : deck_.slides)
        for (Block *b : chart_blocks(s)) { b->data_dirty = false; b->mtime = -1; }
      refresh_data(deck_, o_.lo);
      say("reloaded", false);
      return;
    }
    try {
      Deck fresh = load_deck(deck_.file, o_.lo);
      deck_ = fresh;
      use_deck_theme(deck_, o_.theme);
      slide_ = std::max(0, std::min(slide_, static_cast<int>(deck_.slides.size()) - 1));
      focus_ = -1;
      mode_ = VIEW;
      if (deck_.ok()) say("reloaded", false);
      else say(std::to_string(error_count()) + " problem(s) after reload: run charts --check", true);
    } catch (const std::exception &e) {
      deck_.mtime = file_mtime(deck_.file);
      say(e.what(), true);
    }
  }

  static std::string base(const std::string &p) {
    std::size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
  }

  void save() {
    std::vector<std::string> wrote;
    try {
      for (auto &s : deck_.slides)
        for (Block *b : chart_blocks(s))
          if (b->data_dirty) {
            store_block_data(deck_, *b);
            if (!b->data_file.empty()) wrote.push_back(base(b->data_file));
          }
      if (deck_.dirty || deck_.tweaked) wrote.push_back(base(save_deck(deck_)));
      if (wrote.empty()) say("nothing to save", false);
      else say("saved " + join(wrote, ", "), false);
    } catch (const std::exception &e) {
      say(e.what(), true);
    }
  }

  // ---- drawing

  std::string view_hint() const {
    if (deck_.slides.size() > 1)
      return "←→ slide  tab focus  e edit  a annotate  t type  s save  ? help  q quit";
    return "e edit  a annotate  t type  p palette  v values  s save  ? help  q quit";
  }

  // The display lost its picture: a VT switch, or a stop and continue.
  bool must_redraw() {
    if (tty_guard_continued()) {
      disp_->invalidate();
      return true;
    }
    return disp_->needs_redraw();
  }

  void draw() {
    int cols = 0, rows = 0;
    disp_->grid(cols, rows);
    cols = std::max(cols, 40);
    rows = std::max(rows, 10);
    Scene sc(cols, rows, disp_->mode());
    SlideView v;
    v.cli = &cli_;
    v.notes = notes_;
    v.message = message_;
    v.message_bad = message_bad_;
    if (!digits_.empty()) v.message = "go to slide " + digits_ + "  (enter)";

    Block *f = focused();
    if (mode_ == SHEET && f && sheet_ok(*f)) {
      draw_sheet(sc, v);
    } else {
      v.focus = f;
      if (mode_ == ANNOTATE && f && annotate_ok(*f)) {
        v.cur_series = cur_s_;
        v.cur_index = cur_i_;
        if (message_.empty()) v.message = cursor_text(*v.focus);
        v.hint = "←→ point  ↑↓ series  enter note  h line  v mark  del remove  esc done";
      } else {
        v.hint = view_hint();
      }
      render_slide(sc, deck_, slide_, v);
      if (mode_ == HELP) draw_help(sc);
      if (mode_ == OVERVIEW) draw_overview(sc);
    }
    if (dlg_line_ || dlg_lines_) draw_dialog(sc);
    disp_->show(sc);
  }

  std::string cursor_text(const Block &b) const {
    const Dataset &ds = b.ds;
    if (ds.empty()) return "";
    std::size_t i = static_cast<std::size_t>(std::max(0, cur_i_)), s = static_cast<std::size_t>(std::max(0, cur_s_));
    std::string lab = i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1);
    if (s >= ds.series.size()) return lab;
    double val = i < ds.series[s].v.size() ? ds.series[s].v[i] : std::nan("");
    return lab + " · " + ds.series[s].name + " = " + (std::isfinite(val) ? fmt_val(val) : "-");
  }

  void draw_help(Scene &sc) {
    static const char *lines[] = {
        "← →  space  pgup pgdn     previous / next slide",
        "home  end  12 enter         first / last / go to slide 12",
        "o                           overview: pick a slide from a list",
        "n                           speaker notes",
        "tab  shift-tab              focus the next / previous block",
        "",
        "e                           edit: the data sheet of a chart, or a text block",
        "a                           annotate the chart: callouts, target lines",
        "t T   p P                   chart type, palette",
        "v  l  g                     values, legend, grid",
        "d D   x                     3-D depth, pop a pie slice out",
        "",
        "E                           edit the slide title",
        "i   c                       add a text block / a chart to this slide",
        "N   X                       new slide after this one / delete the focused block",
        "y                           next theme",
        "",
        "s                           save the deck and any edited data",
        "r                           reload from disk, dropping unsaved edits",
        "q                           quit",
    };
    int n = static_cast<int>(sizeof lines / sizeof lines[0]);
    Rect in = dialog_box(sc, 78, n + 4, "keys");
    for (int i = 0; i < n && i < in.h; i++) {
      // key column in blue, meaning in black
      const std::size_t keycols = 28;
      auto cps = utf8_decode(lines[i]);
      std::string keys, rest;
      for (std::size_t k = 0; k < cps.size(); k++) (k < keycols ? keys : rest) += u32_to_utf8(cps[k]);
      sc.cv.text_bg(in.x + 1, in.y + 1 + i, keys, 4, DLG_BG);
      sc.cv.text_bg(in.x + 1 + static_cast<int>(cp_len(keys)), in.y + 1 + i, trunc_to(rest, static_cast<std::size_t>(std::max(0, in.w - 29))), DLG_FG, DLG_BG);
    }
  }

  void draw_overview(Scene &sc) {
    int n = static_cast<int>(deck_.slides.size());
    int h = std::min(n + 4, sc.rows() - 4);
    Rect in = dialog_box(sc, 70, h, deck_.title.empty() ? "slides" : deck_.title);
    int room = in.h - 1;
    int first = std::max(0, std::min(pick_ - room / 2, n - room));
    for (int i = 0; i < room && first + i < n; i++) {
      int k = first + i;
      const Slide &s = deck_.slides[static_cast<std::size_t>(k)];
      std::string t = s.title.empty() ? "(untitled)" : s.title;
      std::string line = pad_left(std::to_string(k + 1), 3) + "  " + t;
      line = pad_right(trunc_to(line, static_cast<std::size_t>(in.w)), static_cast<std::size_t>(in.w));
      bool sel = k == pick_;
      sc.cv.text_bg(in.x, in.y + i, line, sel ? DLG_FIELD_FG : DLG_FG, sel ? DLG_FIELD_BG : DLG_BG);
    }
    sc.cv.text_bg(in.x, in.bottom(), trunc_to("↑↓ pick   enter go   esc close", static_cast<std::size_t>(in.w)), 8, DLG_BG);
  }

  void draw_dialog(Scene &sc) {
    if (dlg_lines_) {
      int h = std::min(sc.rows() - 4, std::max(10, static_cast<int>(dlg_lines_->size()) + 6));
      Rect in = dialog_box(sc, std::min(sc.cols() - 6, 90), h, dlg_title_);
      int room = in.h - 2;
      std::size_t first = dlg_row_ >= static_cast<std::size_t>(room) ? dlg_row_ - static_cast<std::size_t>(room) + 1 : 0;
      for (int i = 0; i < room; i++) {
        std::size_t k = first + static_cast<std::size_t>(i);
        if (k >= dlg_lines_->size()) {
          sc.cv.text_bg(in.x, in.y + i, std::string(static_cast<std::size_t>(in.w), ' '), DLG_FIELD_FG, DLG_FIELD_BG);
          continue;
        }
        const LineEdit &le = (*dlg_lines_)[k];
        if (k == dlg_row_) le.draw(sc.cv, in.x, in.y + i, in.w, DLG_FIELD_FG, DLG_FIELD_BG);
        else sc.cv.text_bg(in.x, in.y + i, pad_right(trunc_to(le.str(), static_cast<std::size_t>(in.w)), static_cast<std::size_t>(in.w)), DLG_FIELD_FG, DLG_FIELD_BG);
      }
      sc.cv.text_bg(in.x, in.bottom(), trunc_to(dlg_keys_, static_cast<std::size_t>(in.w)), 8, DLG_BG);
      return;
    }
    Rect in = dialog_box(sc, std::min(sc.cols() - 6, 72), 7, dlg_title_);
    sc.cv.text_bg(in.x, in.y, trunc_to(dlg_label_, static_cast<std::size_t>(in.w)), DLG_FG, DLG_BG);
    dlg_line_->draw(sc.cv, in.x, in.y + 2, in.w, DLG_FIELD_FG, DLG_FIELD_BG);
    sc.cv.text_bg(in.x, in.bottom(), trunc_to(dlg_keys_, static_cast<std::size_t>(in.w)), 8, DLG_BG);
  }

  // ---- modal helpers: each runs its own little key loop

  std::string wait_key() {
    for (;;) {
      if (must_redraw()) draw();
      std::string k = read_key(250);
      if (!k.empty() && k != "unknown") return k;
    }
  }

  bool ask(const std::string &title, const std::string &label, std::string &value) {
    LineEdit le;
    le.set(value);
    dlg_title_ = title;
    dlg_label_ = label;
    dlg_keys_ = "enter accept   esc cancel";
    dlg_line_ = &le;
    bool ok = false;
    for (;;) {
      draw();
      std::string k = wait_key();
      if (k == "enter") { ok = true; break; }
      if (k == "esc" || k == "ctrl-c") break;
      le.key(k);
    }
    dlg_line_ = nullptr;
    if (ok) value = le.str();
    return ok;
  }

  // Returns the key pressed, one of `keys` (single characters) or "esc".
  std::string confirm(const std::string &title, const std::string &label, const std::string &keys_help,
                      const std::string &keys) {
    LineEdit none;
    dlg_title_ = title;
    dlg_label_ = label;
    dlg_keys_ = keys_help;
    dlg_line_ = &none;
    std::string got = "esc";
    for (;;) {
      draw();
      std::string k = wait_key();
      if (k == "esc" || k == "ctrl-c") break;
      if (k.size() == 1 && keys.find(k[0]) != std::string::npos) { got = k; break; }
    }
    dlg_line_ = nullptr;
    return got;
  }

  bool edit_text(const std::string &title, std::vector<std::string> &lines) {
    std::vector<LineEdit> ed;
    for (const auto &l : lines) { ed.emplace_back(); ed.back().set(l); }
    if (ed.empty()) ed.emplace_back();
    dlg_title_ = title;
    dlg_keys_ = "enter new line   esc done   ctrl-c cancel     # heading   - bullet   **bold**";
    dlg_lines_ = &ed;
    dlg_row_ = ed.size() - 1;
    bool ok = false;
    for (;;) {
      draw();
      std::string k = wait_key();
      LineEdit &cur = ed[dlg_row_];
      if (k == "esc") { ok = true; break; }
      if (k == "ctrl-c") break;
      if (k == "enter") {
        LineEdit next;
        next.c.assign(cur.c.begin() + static_cast<long>(cur.at), cur.c.end());
        cur.c.erase(cur.c.begin() + static_cast<long>(cur.at), cur.c.end());
        ed.insert(ed.begin() + static_cast<long>(dlg_row_ + 1), next);
        dlg_row_++;
      } else if (k == "up") {
        if (dlg_row_ > 0) { dlg_row_--; ed[dlg_row_].at = std::min(ed[dlg_row_].c.size(), cur.at); }
      } else if (k == "down") {
        if (dlg_row_ + 1 < ed.size()) { dlg_row_++; ed[dlg_row_].at = std::min(ed[dlg_row_].c.size(), cur.at); }
      } else if (k == "backspace" && cur.at == 0) {
        if (dlg_row_ > 0) {
          LineEdit &prev = ed[dlg_row_ - 1];
          std::size_t join_at = prev.c.size();
          prev.c.insert(prev.c.end(), cur.c.begin(), cur.c.end());
          prev.at = join_at;
          ed.erase(ed.begin() + static_cast<long>(dlg_row_));
          dlg_row_--;
        }
      } else {
        cur.key(k);
      }
    }
    dlg_lines_ = nullptr;
    if (!ok) return false;
    lines.clear();
    for (const auto &l : ed) lines.push_back(l.str());
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
    return true;
  }

  // ---- keys

  void handle(const std::string &k) {
    switch (mode_) {
    case HELP: mode_ = VIEW; return;
    case OVERVIEW: key_overview(k); return;
    case SHEET: key_sheet(k); return;
    case ANNOTATE: key_annotate(k); return;
    case VIEW: key_view(k); return;
    }
  }

  // A change to how the focused chart is shown.  A flag typed on the command
  // line must not pin the setting the key is trying to change.
  template <typename F> void tweak(F f) {
    Block *b = target(true);
    if (!b) { say("no chart on this slide", true); return; }
    RenderOpts cur = block_opts(deck_, *b, SlideView());
    f(*b, cur);
    store_block_spec(deck_, *b, false);
  }

  void key_view(const std::string &k) {
    if (k.size() == 1 && k[0] >= '0' && k[0] <= '9') {
      if (digits_.size() < 4) digits_ += k;
      return;
    }
    if (!digits_.empty()) {
      int n = std::atoi(digits_.c_str());
      digits_.clear();
      if (k == "enter" || k == "g") { go(n - 1); return; }
      if (k == "esc" || k == "backspace") return;
    }

    if (k == "q" || k == "ctrl-c" || k == "ctrl-d") {
      if (!unsaved()) { quit_ = true; return; }
      std::string a = confirm("unsaved changes", "There are edits that have not been saved.",
                              "s save and quit   q quit without saving   esc stay", "sq");
      if (a == "s") { save(); quit_ = !unsaved(); }
      else if (a == "q") quit_ = true;
    } else if (k == "right" || k == " " || k == "pgdn" || k == "down" || k == "j" || k == "enter") go(slide_ + 1);
    else if (k == "left" || k == "pgup" || k == "up" || k == "k" || k == "backspace") go(slide_ - 1);
    else if (k == "home") go(0);
    else if (k == "end") go(static_cast<int>(deck_.slides.size()) - 1);
    else if (k == "o") { mode_ = OVERVIEW; pick_ = slide_; }
    else if (k == "?" || k == "f1") mode_ = HELP;
    else if (k == "n") {
      notes_ = !notes_;
      if (notes_ && slide() && slide()->notes.empty()) { notes_ = false; say("this slide has no notes", false); }
    } else if (k == "tab" || k == "shift-tab") {
      Slide *s = slide();
      int n = s ? static_cast<int>(leaf_blocks(*s).size()) : 0;
      if (n == 0) return;
      if (k == "tab") focus_ = focus_ + 1 >= n ? (n == 1 ? 0 : -1) : focus_ + 1;
      else focus_ = focus_ < 0 ? n - 1 : focus_ - 1;
    } else if (k == "esc") { focus_ = -1; notes_ = false; }
    else if (k == "s" || k == "ctrl-s") save();
    else if (k == "r" || k == "ctrl-l") {
      if (k == "r") {
        if (unsaved() && confirm("reload", "Reloading drops your unsaved edits.", "r reload   esc keep editing", "r") != "r") return;
        reload();
      }
    } else if (k == "e") open_editor();
    else if (k == "a") open_annotate();
    else if (k == "t" || k == "T") cycle_type(k == "T" ? -1 : 1);
    else if (k == "p" || k == "P") {
      tweak([&](Block &b, const RenderOpts &cur) {
        std::vector<std::string> pals = palette_names();
        int at = 0;
        for (std::size_t i = 0; i < pals.size(); i++)
          if (ieq(pals[i], cur.palette)) at = static_cast<int>(i);
        int n = static_cast<int>(pals.size());
        b.spec.palette = pals[static_cast<std::size_t>((at + (k == "p" ? 1 : n - 1)) % n)];
        b.spec.has_palette = true;
        cli_.has_palette = false;
        say("palette: " + b.spec.palette, false);
      });
    } else if (k == "v") tweak([&](Block &b, const RenderOpts &cur) { b.spec.values = !cur.values; b.spec.has_values = true; cli_.has_values = false; });
    else if (k == "l") tweak([&](Block &b, const RenderOpts &cur) { b.spec.legend = !cur.legend; b.spec.has_legend = true; cli_.has_legend = false; });
    else if (k == "g") tweak([&](Block &b, const RenderOpts &cur) { b.spec.grid = !cur.grid; b.spec.has_grid = true; cli_.has_grid = false; });
    else if (k == "d" || k == "D") {
      tweak([&](Block &b, const RenderOpts &cur) {
        b.spec.depth = std::max(0, std::min(6, cur.depth + (k == "d" ? 1 : -1)));
        b.spec.has_depth = true;
        cli_.has_depth = false;
        say("depth " + std::to_string(b.spec.depth), false);
      });
    } else if (k == "x") {
      tweak([&](Block &b, const RenderOpts &cur) {
        int n = static_cast<int>(b.ds.series.size() == 1 ? b.ds.nrows() : b.ds.series.size());
        int e = cur.explode;
        e = e == -2 ? -1 : (e + 1 < n ? e + 1 : -2);
        b.spec.explode = e;
        b.spec.has_explode = true;
        cli_.has_explode = false;
      });
    } else if (k == "y") {
      std::vector<std::string> names = theme_names();
      std::size_t at = 0;
      for (std::size_t i = 0; i < names.size(); i++)
        if (names[i] == S.name) at = i;
      set_theme(names[(at + 1) % names.size()]);
      deck_.root.set("theme", Json::string(S.name));
      deck_.theme = S.name;
      deck_.tweaked = true;
      say("theme: " + S.name, false);
    } else if (k == "E") edit_title();
    else if (k == "i") add_text();
    else if (k == "c") add_chart();
    else if (k == "N") add_slide();
    else if (k == "X") delete_block();
  }

  void cycle_type(int dir) {
    tweak([&](Block &b, const RenderOpts &cur) {
      std::vector<std::string> types = type_names();
      int at = 0;
      for (std::size_t i = 0; i < types.size(); i++)
        if (types[i] == type_canonical(cur.type)) at = static_cast<int>(i);
      int n = static_cast<int>(types.size());
      b.spec.type = types[static_cast<std::size_t>((at + dir + n) % n)];
      b.spec.has_type = true;
      cli_.has_type = false;
      say("type: " + b.spec.type, false);
    });
  }

  void key_overview(const std::string &k) {
    int n = static_cast<int>(deck_.slides.size());
    if (k == "esc" || k == "o" || k == "q") mode_ = VIEW;
    else if (k == "up" || k == "k") pick_ = std::max(0, pick_ - 1);
    else if (k == "down" || k == "j") pick_ = std::min(n - 1, pick_ + 1);
    else if (k == "pgup") pick_ = std::max(0, pick_ - 10);
    else if (k == "pgdn") pick_ = std::min(n - 1, pick_ + 10);
    else if (k == "home") pick_ = 0;
    else if (k == "end") pick_ = n - 1;
    else if (k == "enter" || k == " ") { go(pick_); mode_ = VIEW; }
  }

  // ---- structure: titles, text, new slides

  bool structural_ok() {
    for (auto &s : deck_.slides)
      for (Block *b : chart_blocks(s))
        if (b->data_dirty) { say("save the edited data first (s)", true); return false; }
    return true;
  }

  // An implicit deck's slides are bare chart blocks; give one a "blocks"
  // array before anything is added beside the chart.
  Json *slide_blocks(Json &sj) {
    if (Json *b = sj.find("blocks")) return b;
    Json block = Json::object();
    std::vector<std::string> moved;
    for (const auto &kv : sj.o)
      if (kv.first != "title" && kv.first != "subtitle" && kv.first != "notes" && kv.first != "layout") {
        block.set(kv.first, kv.second);
        moved.push_back(kv.first);
      }
    for (const auto &key : moved) sj.erase(key);
    Json arr = Json::array();
    if (!block.o.empty()) arr.a.push_back(block);
    return &sj.set("blocks", arr);
  }

  void edit_title() {
    Slide *s = slide();
    if (!s) return;
    std::string t = s->title;
    if (!ask("slide title", "Title of slide " + std::to_string(slide_ + 1) + ":", t)) return;
    if (!structural_ok()) return;
    Json *sj = deck_.node(s->path);
    if (!sj) return;
    // On a one-block shorthand slide "title" is the slide's, so this is safe.
    if (t.empty()) sj->erase("title");
    else sj->set("title", Json::string(t));
    deck_.dirty = true;
    reparse(deck_, o_.lo);
  }

  void add_text() {
    Slide *s = slide();
    if (!s || !structural_ok()) return;
    std::vector<std::string> lines;
    if (!edit_text("new text block", lines) || lines.empty()) return;
    Json *sj = deck_.node(s->path);
    if (!sj) return;
    Json b = Json::object();
    Json arr = Json::array();
    for (const auto &l : lines) arr.a.push_back(Json::string(l));
    b.set("text", arr);
    slide_blocks(*sj)->a.push_back(b);
    deck_.dirty = true;
    reparse(deck_, o_.lo);
  }

  void add_chart() {
    Slide *s = slide();
    if (!s || !structural_ok()) return;
    std::string path;
    if (!ask("add a chart", "Data file (csv, tsv or json), relative to the deck:", path) || trim(path).empty()) return;
    Json *sj = deck_.node(s->path);
    if (!sj) return;
    Json b = Json::object();
    b.set("data", Json::string(trim(path)));
    slide_blocks(*sj)->a.push_back(b);
    deck_.dirty = true;
    reparse(deck_, o_.lo);
    if (!deck_.ok()) say(deck_.issues.back().msg, true);
  }

  void add_slide() {
    if (!structural_ok()) return;
    std::string t;
    if (!ask("new slide", "Title of the new slide:", t)) return;
    Json *arr = deck_.root.find("slides");
    if (!arr || !arr->is_arr()) return;
    Json sj = Json::object();
    sj.set("title", Json::string(t));
    sj.set("blocks", Json::array());
    arr->a.insert(arr->a.begin() + std::min<long>(static_cast<long>(arr->a.size()), slide_ + 1), sj);
    deck_.dirty = true;
    reparse(deck_, o_.lo);
    go(slide_ + 1);
    say("i adds text, c adds a chart", false);
  }

  void delete_block() {
    Block *b = focused();
    if (!b) { say("tab to a block first", false); return; }
    if (!structural_ok()) return;
    if (confirm("delete block", "Remove the focused block from this slide?", "y delete   esc keep", "y") != "y") return;
    std::string path = b->path;
    std::size_t open = path.find_last_of('[');
    if (open == std::string::npos || path.back() != ']') { say("this slide is its one block; delete the slide in the JSON", true); return; }
    Json *arr = deck_.node(path.substr(0, open));
    std::size_t idx = static_cast<std::size_t>(std::atoi(path.substr(open + 1).c_str()));
    if (!arr || !arr->is_arr() || idx >= arr->a.size() || path.find("slides[") != 0 || path.find(".") == std::string::npos) {
      say("this slide is its one block; delete the slide in the JSON", true);
      return;
    }
    arr->a.erase(arr->a.begin() + static_cast<long>(idx));
    deck_.dirty = true;
    focus_ = -1;
    reparse(deck_, o_.lo);
  }

  // ---- the editor: a sheet for charts, a text box for text

  void open_editor() {
    Block *b = target(false);
    if (!b) { say("nothing to edit on this slide (i adds text, c a chart)", false); return; }
    if (b->kind == Block::TEXT) {
      std::vector<std::string> lines = b->lines;
      if (edit_text("text", lines)) { b->lines = lines; store_block_text(deck_, *b); }
    } else if (b->kind == Block::STAT) {
      std::string v = b->value;
      if (ask("big number", "Value:", v)) { b->value = v; store_block_text(deck_, *b); }
    } else if (b->kind != Block::CHART) {
      say("shapes and flows are edited in the deck file; the presenter redraws when it changes", false);
    } else {
      if (!b->error.empty()) { say(b->error, true); return; }
      mode_ = SHEET;
      row_ = 0;
      col_ = 1;
      top_ = left_ = 0;
      editing_ = false;
      undo_.clear();
    }
  }

  int sheet_cols(const Dataset &ds) const { return static_cast<int>(ds.series.size()) + 1; }
  int sheet_rows(const Dataset &ds) const { return static_cast<int>(ds.nrows()); }

  // The data under the sheet or the annotate cursor may have been read again
  // from disk since the mode was entered (the presenter follows the file), so
  // before every key and every frame: still editable, and the cursor inside it.
  bool sheet_ok(Block &b) {
    if (!b.error.empty() || b.ds.series.empty()) {
      mode_ = VIEW;
      editing_ = false;
      say(b.error.empty() ? "the data has no series to edit" : "the data changed on disk: " + b.error, true);
      return false;
    }
    row_ = std::max(-1, std::min(row_, sheet_rows(b.ds) - 1));
    col_ = std::max(0, std::min(col_, sheet_cols(b.ds) - 1));
    return true;
  }
  bool annotate_ok(Block &b) {
    if (b.kind != Block::CHART || !b.error.empty() || b.ds.empty()) {
      mode_ = VIEW;
      return false;
    }
    cur_i_ = std::max(0, std::min(cur_i_, static_cast<int>(b.ds.nrows()) - 1));
    cur_s_ = std::max(0, std::min(cur_s_, static_cast<int>(b.ds.series.size()) - 1));
    return true;
  }

  std::string cell_text(const Dataset &ds, int r, int c, bool raw) const {
    if (r < 0) {
      if (c == 0) return ds.label_name.empty() ? "label" : ds.label_name;
      const Series &s = ds.series[static_cast<std::size_t>(c - 1)];
      return (!s.name.empty() && s.name[0] == '\x01') ? ds.x_name : s.name;
    }
    std::size_t i = static_cast<std::size_t>(r);
    if (c == 0) return i < ds.labels.size() ? ds.labels[i] : std::to_string(i + 1);
    const Series &s = ds.series[static_cast<std::size_t>(c - 1)];
    double v = i < s.v.size() ? s.v[i] : std::nan("");
    return raw ? fmt_raw(v) : (std::isfinite(v) ? fmt_val(v) : "");
  }

  void snapshot(Block &b) {
    undo_.push_back(b.ds);
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    b.data_dirty = true;
  }

  bool commit_cell(Block &b) {
    Dataset &ds = b.ds;
    std::string text = trim(edit_.str());
    if (text == cell_text(ds, row_, col_, true)) return true;
    if (row_ >= 0 && col_ > 0 && !text.empty()) {
      double d;
      if (!parse_num(text, d)) { say("\"" + text + "\" is not a number", true); return false; }
    }
    snapshot(b);
    if (row_ < 0) {
      if (col_ == 0) ds.label_name = text;
      else {
        Series &s = ds.series[static_cast<std::size_t>(col_ - 1)];
        if (!s.name.empty() && s.name[0] == '\x01') ds.x_name = text.empty() ? "x" : text;
        else s.name = text.empty() ? "series " + std::to_string(col_) : text;
      }
      return true;
    }
    std::size_t i = static_cast<std::size_t>(row_);
    if (col_ == 0) {
      if (ds.labels.size() <= i) ds.labels.resize(i + 1);
      ds.labels[i] = text;
      return true;
    }
    Series &s = ds.series[static_cast<std::size_t>(col_ - 1)];
    if (s.v.size() <= i) s.v.resize(i + 1, std::nan(""));
    double d = std::nan("");
    if (!text.empty()) parse_num(text, d);
    s.v[i] = d;
    return true;
  }

  void key_sheet(const std::string &k) {
    Block *bp = focused();
    if (!bp || !sheet_ok(*bp)) { mode_ = VIEW; return; }
    Block &b = *bp;
    Dataset &ds = b.ds;
    const int R = sheet_rows(ds), C = sheet_cols(ds);

    if (editing_) {
      if (k == "esc" || k == "ctrl-c") { editing_ = false; return; }
      if (k == "enter" || k == "tab" || k == "down" || k == "up" || k == "shift-tab") {
        if (!commit_cell(b)) return;
        editing_ = false;
        if (k == "enter" || k == "down") row_ = std::min(R - 1, row_ + 1);
        else if (k == "up") row_ = std::max(-1, row_ - 1);
        else if (k == "tab") col_ = std::min(C - 1, col_ + 1);
        else col_ = std::max(0, col_ - 1);
        return;
      }
      edit_.key(k);
      return;
    }

    if (k == "esc" || k == "q") { mode_ = VIEW; return; }
    if (k == "up") row_ = std::max(-1, row_ - 1);
    else if (k == "down") row_ = std::min(R - 1, row_ + 1);
    else if (k == "left" || k == "shift-tab") col_ = std::max(0, col_ - 1);
    else if (k == "right" || k == "tab") col_ = std::min(C - 1, col_ + 1);
    else if (k == "pgup") row_ = std::max(-1, row_ - 10);
    else if (k == "pgdn") row_ = std::min(R - 1, row_ + 10);
    else if (k == "home") col_ = 0;
    else if (k == "end") col_ = C - 1;
    else if (k == "ctrl-home" || k == "ctrl-t") { row_ = 0; col_ = 1; }
    else if (k == "enter" || k == "f2") {
      editing_ = true;
      edit_.set(cell_text(ds, row_, col_, true));
    } else if (k == "delete" || k == "backspace") {
      if (row_ >= 0 && col_ > 0) {
        snapshot(b);
        Series &s = ds.series[static_cast<std::size_t>(col_ - 1)];
        if (static_cast<std::size_t>(row_) < s.v.size()) s.v[static_cast<std::size_t>(row_)] = std::nan("");
      }
    } else if (k == "insert" || k == "ctrl-n") {
      snapshot(b);
      std::size_t at = static_cast<std::size_t>(std::max(0, row_ + 1));
      const std::size_t n = ds.nrows();
      if (ds.labels.size() < n) ds.labels.resize(n);
      at = std::min(at, n);
      ds.labels.insert(ds.labels.begin() + static_cast<long>(at), "new");
      for (auto &s : ds.series) {
        s.v.resize(n, std::nan(""));
        s.v.insert(s.v.begin() + static_cast<long>(at), std::nan(""));
      }
      row_ = static_cast<int>(at);
      col_ = 0;
      editing_ = true;
      edit_.set("");
    } else if (k == "ctrl-d") {
      if (row_ >= 0 && R > 1) {
        snapshot(b);
        std::size_t at = static_cast<std::size_t>(row_);
        if (at < ds.labels.size()) ds.labels.erase(ds.labels.begin() + static_cast<long>(at));
        for (auto &s : ds.series)
          if (at < s.v.size()) s.v.erase(s.v.begin() + static_cast<long>(at));
        row_ = std::min(row_, sheet_rows(ds) - 1);
        prune_notes(b);
      } else say("a chart needs at least one row", true);
    } else if (k == "ctrl-a") {
      snapshot(b);
      Series s;
      s.name = "series " + std::to_string(ds.series.size() + 1);
      s.v.assign(ds.nrows(), std::nan(""));
      ds.series.push_back(s);
      col_ = sheet_cols(ds) - 1;
      row_ = -1;
      editing_ = true;
      edit_.set(s.name);
    } else if (k == "ctrl-x") {
      if (col_ > 0 && ds.series.size() > 1) {
        snapshot(b);
        ds.series.erase(ds.series.begin() + (col_ - 1));
        col_ = std::min(col_, sheet_cols(ds) - 1);
      } else say(col_ == 0 ? "the label column stays" : "a chart needs at least one series", true);
    } else if (k == "ctrl-z" || k == "u") {
      if (undo_.empty()) say("nothing to undo", false);
      else {
        ds = undo_.back();
        undo_.pop_back();
        row_ = std::min(row_, sheet_rows(ds) - 1);
        col_ = std::min(col_, sheet_cols(ds) - 1);
      }
    } else if (k == "ctrl-s" || k == "s") save();
    else if (k == "t") cycle_type(1);
    else if (k == "T") cycle_type(-1);
    else if (LineEdit::printable(k)) {
      // typing over a cell replaces it, as in any spreadsheet
      editing_ = true;
      edit_.set("");
      edit_.key(k);
    }
  }

  // Annotations that named a row which no longer exists would silently stop
  // drawing; nothing to do for them here, but say so.
  void prune_notes(Block &b) {
    const std::vector<Annotation> &notes = b.spec.has_notes ? b.spec.notes : b.ds.spec.notes;
    for (const auto &a : notes) {
      if (a.kind != Annotation::POINT && a.kind != Annotation::VLINE) continue;
      bool found = false;
      for (const auto &l : b.ds.labels)
        if (ieq(trim(l), trim(a.label))) found = true;
      if (!found && !a.label.empty()) { say("the annotation on \"" + a.label + "\" has lost its row", true); return; }
    }
  }

  void draw_sheet(Scene &sc, SlideView &v) {
    Block &b = *focused();
    Dataset &ds = b.ds;
    const int W = sc.cols(), H = sc.rows();
    const int R = sheet_rows(ds), C = sheet_cols(ds);

    // chart on top, live; sheet underneath
    int sheet_h = std::max(7, std::min(R + 4, H * 2 / 5));
    int chart_h = H - 1 - sheet_h - 1;
    RenderOpts o = block_opts(deck_, b, v);
    o.color = o.color && sc.mode().color;
    if (row_ >= 0) {
      o.cur_index = row_;
      o.cur_series = col_ > 0 ? col_ - 1 : -1;
    }
    Dataset work = ds;
    if (chart_h >= 8) render_chart(sc, Rect{1, 0, W - 2 - (S.slide_bg == BG_NONE ? 0 : 2), chart_h}, work, o);

    Canvas &cv = sc.cv;
    int y0 = H - 1 - sheet_h;
    Rect box{1, y0, W - 2 - (S.slide_bg == BG_NONE ? 0 : 2), sheet_h};
    if (S.slide_bg != BG_NONE) { cv.shadow(box.x, box.y, box.w, box.h); sc.panel(box, S.panel_bg); }
    cv.box(box.x, box.y, box.w, box.h, sc.mode().ascii ? BOX_ASCII : BOX_SINGLE, S.accent);
    std::string name = b.data_ref.empty() ? "data (kept in the deck)" : b.data_ref;
    if (b.data_dirty) name += " *";
    cv.text(box.x + 2, box.y, " " + name + " ", S.title);
    if (ds.lossy) cv.text_r(box.x + 2, box.y, box.w - 4, " read-only on disk: " + ds.lossy_why + " ", 9);

    // column widths
    std::vector<int> w(static_cast<std::size_t>(C), 6);
    for (int c = 0; c < C; c++) {
      int m = static_cast<int>(cp_len(cell_text(ds, -1, c, false)));
      for (int r = 0; r < R; r++) m = std::max(m, static_cast<int>(cp_len(cell_text(ds, r, c, false))));
      w[static_cast<std::size_t>(c)] = std::max(6, std::min(m, c == 0 ? 20 : 14)) + 2;
    }
    Rect in{box.x + 1, box.y + 1, box.w - 2, box.h - 2};
    int body_rows = in.h - 1; // one for the header

    // keep the cursor on screen
    if (row_ >= 0) {
      if (row_ < top_) top_ = row_;
      if (row_ >= top_ + body_rows) top_ = row_ - body_rows + 1;
    }
    top_ = std::max(0, std::min(top_, std::max(0, R - body_rows)));
    if (col_ > 0 && col_ - 1 < left_) left_ = col_ - 1;
    for (;;) {
      int used = w[0];
      int last = left_;
      for (int c = left_ + 1; c < C && used + w[static_cast<std::size_t>(c)] <= in.w; c++) { used += w[static_cast<std::size_t>(c)]; last = c; }
      if (col_ <= last || left_ >= C - 2 || col_ == 0) break;
      left_++;
    }

    const bool light = contrast_on(S.panel_bg) == 0 && S.slide_bg != BG_NONE;
    auto draw_cell = [&](int r, int c, int x, int y) {
      int cw = w[static_cast<std::size_t>(c)];
      bool cur = r == row_ && c == col_;
      if (cur && editing_) { edit_.draw(cv, x, y, cw - 1, DLG_FIELD_FG, DLG_FIELD_BG); return; }
      std::string t = trunc_to(cell_text(ds, r, c, false), static_cast<std::size_t>(cw - 1));
      t = c == 0 ? pad_right(t, static_cast<std::size_t>(cw - 1)) : pad_left(t, static_cast<std::size_t>(cw - 1));
      uint8_t fg = r < 0 ? S.table_head : (c == 0 ? S.label : S.table_row);
      if (r < 0 && c > 0 && !light) fg = static_cast<uint8_t>(ds.series[static_cast<std::size_t>(c - 1)].color);
      if (cur) cv.text_bg(x, y, t, S.cursor_fg, S.cursor_bg);
      else cv.text(x, y, t, fg);
    };
    const char32_t rule = sc.mode().ascii ? U'|' : G.v;
    auto draw_row = [&](int r, int y) {
      int x = in.x;
      draw_cell(r, 0, x, y);
      x += w[0];
      for (int c = left_ + 1; c < C; c++) {
        if (x + w[static_cast<std::size_t>(c)] > in.x + in.w) { cv.put(in.x + in.w - 1, y, sc.mode().ascii ? U'>' : U'►', S.dim); break; }
        cv.put(x - 1, y, rule, S.table_rule);
        draw_cell(r, c, x, y);
        x += w[static_cast<std::size_t>(c)];
      }
      if (x <= in.x + in.w) cv.put(x - 1, y, rule, S.table_rule);
    };
    // work holds the colours render_chart assigned
    for (std::size_t s = 0; s < ds.series.size() && s < work.series.size(); s++) ds.series[s].color = work.series[s].color;
    draw_row(-1, in.y);
    for (int i = 0; i < body_rows && top_ + i < R; i++) draw_row(top_ + i, in.y + 1 + i);
    if (R > body_rows) {
      std::string pos = " " + std::to_string(std::max(0, row_) + 1) + "/" + std::to_string(R) + " ";
      cv.text_r(box.x + 2, box.bottom(), box.w - 4, pos, S.dim);
    }

    SlideView bar = v;
    bar.hint = editing_ ? "enter ok  tab next  esc cancel"
                        : "enter edit  ins row  ^D del row  ^A series  ^X del series  u undo  s save  esc back";
    if (bar.message.empty()) bar.message = cell_text(ds, -1, col_, false) + (row_ >= 0 ? " · " + cell_text(ds, row_, 0, false) : " (header)");
    draw_bar(sc, bar);
  }

  void draw_bar(Scene &sc, const SlideView &v) {
    draw_status_bar(sc, v.message, v.hint, "", v.message_bad);
  }

  // ---- annotate

  // Annotations are edited on the block, so they are saved with the deck even
  // when they first came from the data file.
  std::vector<Annotation> &notes_of(Block &b) {
    if (!b.spec.has_notes) {
      b.spec.notes = b.ds.spec.notes;
      b.spec.has_notes = true;
    }
    return b.spec.notes;
  }

  void open_annotate() {
    Block *b = target(true);
    if (!b) { say("no chart on this slide", true); return; }
    if (!b->error.empty() || b->ds.empty()) { say("this chart has no data to annotate", true); return; }
    mode_ = ANNOTATE;
    cur_i_ = std::min(cur_i_, static_cast<int>(b->ds.nrows()) - 1);
    cur_s_ = first_series(b->ds);
  }

  static bool is_x(const Series &s) { return !s.name.empty() && s.name[0] == '\x01'; }
  int first_series(const Dataset &ds) const { return (!ds.series.empty() && is_x(ds.series[0]) && ds.series.size() > 1) ? 1 : 0; }

  Annotation *note_at_cursor(Block &b) {
    const Dataset &ds = b.ds;
    std::string lab = static_cast<std::size_t>(cur_i_) < ds.labels.size() ? ds.labels[static_cast<std::size_t>(cur_i_)] : "";
    for (auto &a : notes_of(b)) {
      if (a.kind != Annotation::POINT) continue;
      bool same_row = (!a.label.empty() && ieq(trim(a.label), trim(lab))) || (a.label.empty() && a.index == cur_i_);
      bool same_series = a.series.empty() || (static_cast<std::size_t>(cur_s_) < ds.series.size() &&
                                              ieq(a.series, ds.series[static_cast<std::size_t>(cur_s_)].name));
      if (same_row && same_series) return &a;
    }
    return nullptr;
  }

  void key_annotate(const std::string &k) {
    Block *bp = focused();
    if (!bp || !annotate_ok(*bp)) { mode_ = VIEW; return; }
    Block &b = *bp;
    const Dataset &ds = b.ds;
    const int R = static_cast<int>(ds.nrows()), NS = static_cast<int>(ds.series.size());
    const int s0 = first_series(ds);
    std::string lab = static_cast<std::size_t>(cur_i_) < ds.labels.size() ? ds.labels[static_cast<std::size_t>(cur_i_)] : "";
    std::string ser = static_cast<std::size_t>(cur_s_) < ds.series.size() ? ds.series[static_cast<std::size_t>(cur_s_)].name : "";

    if (k == "esc" || k == "q" || k == "a") { mode_ = VIEW; return; }
    if (k == "left") cur_i_ = (cur_i_ + R - 1) % R;
    else if (k == "right") cur_i_ = (cur_i_ + 1) % R;
    else if (k == "up") cur_s_ = cur_s_ + 1 >= NS ? s0 : cur_s_ + 1;
    else if (k == "down") cur_s_ = cur_s_ - 1 < s0 ? NS - 1 : cur_s_ - 1;
    else if (k == "home") cur_i_ = 0;
    else if (k == "end") cur_i_ = R - 1;
    else if (k == "enter") {
      Annotation *old = note_at_cursor(b);
      std::string text = old ? old->text : "";
      if (!ask(old ? "edit note" : "note on " + lab, "What should it say?", text)) return;
      if (trim(text).empty()) {
        if (old) erase_note(b, old);
      } else if (old) {
        old->text = text;
      } else {
        Annotation a;
        a.kind = Annotation::POINT;
        a.label = lab;
        a.index = cur_i_;
        if (NS - s0 > 1) a.series = ser;
        a.text = text;
        notes_of(b).push_back(a);
      }
      store_block_spec(deck_, b, true);
    } else if (k == "h") {
      double val = static_cast<std::size_t>(cur_s_) < ds.series.size() && static_cast<std::size_t>(cur_i_) < ds.series[static_cast<std::size_t>(cur_s_)].v.size()
                       ? ds.series[static_cast<std::size_t>(cur_s_)].v[static_cast<std::size_t>(cur_i_)]
                       : 0;
      std::string at = fmt_raw(val), text;
      if (!ask("line across the plot", "At what value?", at)) return;
      double d;
      if (!parse_num(at, d)) { say("\"" + at + "\" is not a number", true); return; }
      if (!ask("line across the plot", "Label (may be empty):", text)) return;
      Annotation a;
      a.kind = Annotation::HLINE;
      a.value = d;
      a.text = text;
      notes_of(b).push_back(a);
      store_block_spec(deck_, b, true);
    } else if (k == "v") {
      std::string text;
      if (!ask("mark " + lab, "Label (may be empty):", text)) return;
      Annotation a;
      a.kind = Annotation::VLINE;
      a.label = lab;
      a.index = cur_i_;
      a.text = text;
      notes_of(b).push_back(a);
      store_block_spec(deck_, b, true);
    } else if (k == "delete" || k == "backspace" || k == "x") {
      if (Annotation *old = note_at_cursor(b)) {
        erase_note(b, old);
      } else {
        // no callout here: take the newest line or mark on this category, else the newest of anything
        std::vector<Annotation> &notes = notes_of(b);
        if (notes.empty()) { say("no annotations on this chart", false); return; }
        long hit = static_cast<long>(notes.size()) - 1;
        for (long i = static_cast<long>(notes.size()) - 1; i >= 0; i--)
          if (notes[static_cast<std::size_t>(i)].kind == Annotation::VLINE && ieq(trim(notes[static_cast<std::size_t>(i)].label), trim(lab))) { hit = i; break; }
        std::string what = notes[static_cast<std::size_t>(hit)].text;
        if (confirm("remove annotation", "Remove \"" + trunc_to(what, 40) + "\"?", "y remove   esc keep", "y") != "y") return;
        notes.erase(notes.begin() + hit);
      }
      store_block_spec(deck_, b, true);
    }
  }

  void erase_note(Block &b, Annotation *a) {
    std::vector<Annotation> &notes = notes_of(b);
    notes.erase(notes.begin() + (a - &notes[0]));
  }
};

} // namespace

int run_presenter(Deck &deck, const PresentOpts &o) {
  Presenter p(deck, o);
  return p.run();
}

} // namespace ch
