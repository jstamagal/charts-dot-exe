// display.hpp -- where a Scene ends up.
//
//   fb     the Linux console's framebuffer (/dev/fb0), real pixels, no X
//   kitty  the kitty graphics protocol (kitty, ghostty, wezterm, konsole)
//   sixel  foot, wezterm, xterm -ti vt340, mlterm ...
//   cells  half blocks and 16 ANSI colours: works anywhere
//   ascii  plain characters only
//
// open_display() picks the best one the terminal can do.
#pragma once

#include <memory>
#include <string>

#include "scene.hpp"

namespace ch {

class Display {
public:
  virtual ~Display() {}
  virtual std::string name() const = 0;
  virtual Mode mode() const = 0;
  // The grid a scene should be built for, right now.
  virtual void grid(int &cols, int &rows) = 0;
  virtual void show(const Scene &sc) = 0;
  // Something outside wiped the screen (a VT switch): draw again.
  virtual bool needs_redraw() { return false; }
  // Forget anything remembered about what is on the screen: after a stop and
  // continue, someone else has drawn there.
  virtual void invalidate() {}
  virtual void close() {}
};

struct DisplayOpts {
  std::string want = "auto"; // auto | fb | kitty | sixel | cells | ascii
  int scale = 0;             // pixel backends: 0 = pick from the screen size
  bool color = true;
};

// Never fails: the last resort is cells.  `why` says what was chosen and why,
// for --verbose and for the curious.
std::unique_ptr<Display> open_display(const DisplayOpts &o, std::string &why);

bool gfx_name_valid(const std::string &n);

} // namespace ch
