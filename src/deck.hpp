// deck.hpp -- a presentation: slides of charts, text and big numbers.
//
// A deck is one JSON file, written by an agent and paged through by a human.
// A bare data file is a deck too -- one slide, one chart -- so there is a
// single viewer and a single renderer.  See docs/DECK.md for the format.
#pragma once

#include <string>
#include <vector>

#include "chart.hpp"
#include "data.hpp"
#include "diagram.hpp"
#include "json.hpp"

namespace ch {

// Something wrong with a deck, addressed the way the JSON is:
// "slides[2].blocks[0].type".
struct Issue {
  bool error = true; // false = warning
  std::string path, msg;
};

struct Block {
  enum Kind { CHART, TEXT, STAT, ROWS, COLS, SHAPES, FLOW };
  Kind kind = CHART;
  std::string path; // into the deck's JSON
  double weight = 1;
  bool has_at = false;
  double at[4] = {0, 0, 12, 12}; // x y w h on a 12 x 12 grid over the slide body
  Rect r;                        // where it was last drawn

  // chart
  std::string data_ref;  // as written in the deck; empty for inline data
  std::string data_file; // resolved against the deck's directory
  Dataset ds;
  std::string error; // why the data could not be loaded
  long long mtime = -1;
  bool data_dirty = false; // edited in the sheet, not yet saved
  ChartSpec spec;          // what the block itself asks for

  // text
  std::string title;
  std::vector<std::string> lines;
  int size = 1, align = -1, color = -1;
  bool box = false, middle = false;

  // stat
  std::string value, label, delta;

  // shapes / flow (title and box as for text)
  std::vector<Shape> shapes;
  Flow flow;

  std::vector<Block> kids; // ROWS / COLS
};

struct Slide {
  std::string path;
  std::string title, subtitle, notes, layout = "auto";
  std::vector<Block> blocks;
};

struct Deck {
  std::string file; // empty until an implicit deck is first saved
  std::string dir;  // data paths resolve against this
  Json root;
  bool implicit = false; // made from data files on the command line
  bool dirty = false;    // content changed (text, data, annotations): ask before quitting
  bool tweaked = false;  // only how a chart is shown changed: saved with the rest, never nagged about
  long long mtime = -1;

  std::string title, theme, palette, footer;
  Skin skin;             // theme, with any custom colours applied
  // "display": how the deck wants to be shown.  The command line still wins.
  std::string gfx;       // "" = not said
  int scale = 0, cols = 0, rows = 0;
  int ascii = -1, color = -1; // -1 = not said
  std::vector<Slide> slides;
  std::vector<Issue> issues;

  bool ok() const;
  Json *node(const std::string &path); // nullptr when the path is gone
};

// Is this JSON a deck rather than a data file?  A deck has "slides".
// Make the deck's theme (or `override`, when not empty) the current one.
void use_deck_theme(const Deck &d, const std::string &override_name);

bool is_deck_json(const Json &j);
bool is_deck_file(const std::string &path);

// Content problems never throw: they land in deck.issues, so an agent sees all
// of them at once.  Only an unreadable file or broken JSON throws.
Deck load_deck(const std::string &path, const LoadOpts &lo);

struct FileSlides {
  std::vector<std::string> files;
  std::vector<std::string> types; // cycled over the files; may be empty
  bool tile = false;              // everything on one slide
};
Deck deck_from_files(const FileSlides &fs, const LoadOpts &lo);

// Every chart block of a slide, depth first.
std::vector<Block *> chart_blocks(Slide &s);
// Every block that holds something (chart, text, stat): the focus order.
std::vector<Block *> leaf_blocks(Slide &s);

// Reload any data file that changed on disk.  True when something did.
bool refresh_data(Deck &d, const LoadOpts &lo);

// Write root to d.file (choosing a name beside the first data file when the
// deck is implicit).  Returns the path written.
std::string save_deck(Deck &d);

// Record a block's spec and annotations into the JSON, ready for save_deck.
// `content` is false for a change of view (type, palette, a toggle).
void store_block_spec(Deck &d, Block &b, bool content);
// Record an edited text block or stat.
void store_block_text(Deck &d, Block &b);
// Rebuild slides and issues from root, after the JSON was edited in place.
// Data files are read again, so unsaved sheet edits must be stored first.
void reparse(Deck &d, const LoadOpts &lo);
// Move a block's edited data to where it lives: its file, or the deck.
void store_block_data(Deck &d, Block &b);

std::string deck_outline(const Deck &d);
std::string issues_text(const Deck &d);
std::string issues_json(const Deck &d);

// ---- drawing -----------------------------------------------------------------

struct SlideView {
  const Block *focus = nullptr; // block ringed as the target of the keys
  int cur_series = -1;  // data point under the annotate cursor
  int cur_index = -1;
  bool notes = false;   // speaker notes over the slide
  bool chrome = true;   // footer bar
  std::string hint;     // right side of the footer bar
  std::string message;  // replaces the footer text, e.g. "saved"
  bool message_bad = false;
  const ChartSpec *cli = nullptr; // command-line flags beat the deck
};

// The options a block is finally drawn with: defaults, deck, data file, block,
// then command line.
RenderOpts block_opts(const Deck &d, const Block &b, const SlideView &v);

void render_slide(Scene &sc, Deck &d, int index, const SlideView &v);

// The DOS bottom line: a message on the left, "key meaning" hints (pairs split
// by two spaces, keys picked out in colour) and a position on the right.
void draw_status_bar(Scene &sc, const std::string &left, const std::string &hint, const std::string &pos, bool bad);

} // namespace ch
