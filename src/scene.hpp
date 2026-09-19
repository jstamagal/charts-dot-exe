// scene.hpp -- one screenful: a grid of text cells, pixel surfaces laid over
// rectangles of it, and free text placed on top.
//
// A scene is built once and shown two ways.  to_image() rasterises it with the
// bitmap font for the framebuffer, kitty, sixel and PNG.  to_cells() folds the
// surfaces down to half blocks for any terminal at all.  What an agent sees in
// a PNG is what the human sees on the console.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "gfx.hpp"

namespace ch {

// Every colour the chrome uses, so a theme is one place.
struct Skin {
  std::string name = "dos";
  uint8_t slide_bg = 4;  // BG_NONE = leave the terminal's own background
  uint8_t panel_bg = 0;  // inside a chart frame
  uint8_t frame = 15;
  uint8_t title = 11;
  uint8_t subtitle = 7;
  uint8_t axis = 7;
  uint8_t tick = 15;
  uint8_t grid = 8;
  uint8_t label = 7;
  uint8_t xlabel = 14;
  uint8_t ylabel = 14;
  uint8_t legend = 7;
  uint8_t value = 15;
  uint8_t shadow = 0;
  uint8_t heading = 11;  // slide titles
  uint8_t text = 15;     // body text on the slide background
  uint8_t dim = 7;
  uint8_t accent = 14;
  uint8_t bullet = 11;
  uint8_t bar_fg = 0, bar_bg = 7, bar_key = 1; // top and bottom bars
  uint8_t note_fg = 0, note_bg = 11;           // annotation callouts
  uint8_t cursor_fg = 0, cursor_bg = 14;       // sheet cursor
  uint8_t table_head = 11, table_rule = 8, table_row = 7;
};

extern Skin S;
// The names a custom theme may set, e.g. {"base":"dos","slide_bg":"dark cyan"}.
std::vector<std::string> skin_keys();
// color 0..15, or BG_NONE for slide_bg.  False when the key is unknown.
bool skin_set(Skin &s, const std::string &key, uint8_t color);
std::vector<std::string> theme_names();
bool set_theme(const std::string &name); // false when unknown

// How the scene will be shown; chart code asks this instead of guessing.
struct Mode {
  bool pixel = false; // 8x16 surfaces and free text placement
  bool ascii = false;
  bool color = true;
  int sx() const { return pixel ? 8 : 1; }
  int sy() const { return pixel ? 16 : (ascii ? 1 : 2); }
};

struct TextItem {
  double x = 0, y = 0; // cells; fractional only means something in pixel mode
  std::string s;
  uint8_t fg = 7;
  int bg = -1; // -1 = leave what is underneath
  int scale = 1;
};

struct Layer {
  Rect r;
  std::unique_ptr<Surface> s;
};

class Scene {
public:
  Scene(int cols, int rows, Mode m);

  int cols() const { return cv.w(); }
  int rows() const { return cv.h(); }
  const Mode &mode() const { return mode_; }

  Canvas cv;

  // A new surface over these cells.  The reference stays valid.
  Surface &surface(Rect cells);
  void text(double x, double y, const std::string &s, uint8_t fg, int bg = -1, int scale = 1);
  // Text `scale` cells tall, aligned in a box w cells wide (-1 left, 0 centre,
  // 1 right).  Cell output prints it at normal size in the same box.
  void big(int x, int y, int w, const std::string &s, uint8_t fg, int scale, int align);
  // Make room for something drawn over the slide (a dialog): pixels and free
  // text inside r go away, so only the cells show there.
  void cover(Rect r);
  // Fill cells with a background colour (a panel).
  void panel(Rect r, uint8_t bg);

  // Things that did not fit, for --check: a truncated title, thinned labels.
  // `where` is the JSON path of whatever is being drawn.
  std::string where;
  std::vector<std::pair<std::string, std::string>> fit; // path, message
  void fit_note(const std::string &msg) {
    for (const auto &f : fit)
      if (f.first == where && f.second == msg) return;
    fit.emplace_back(where, msg);
  }

  Canvas to_cells() const;
  Image to_image() const;

private:
  Mode mode_;
  std::vector<Layer> layers_;
  std::vector<TextItem> texts_;
};

} // namespace ch
