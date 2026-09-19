// tui.hpp -- the full-screen presenter.
#pragma once

#include <string>

#include "deck.hpp"
#include "display.hpp"

namespace ch {

struct PresentOpts {
  std::string gfx = "auto";
  std::string theme; // set on the command line: the deck's own theme stands down
  int scale = 0;
  int slide = 0; // 0-based slide to open on
  bool color = true;
  bool verbose = false;
  LoadOpts lo;
  ChartSpec cli; // chart flags typed on the command line
};

int run_presenter(Deck &deck, const PresentOpts &o);

} // namespace ch
