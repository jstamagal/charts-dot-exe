// main.cpp -- charts: present a deck, print a chart, or render either to PNG.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "chart.hpp"
#include "cli.hpp"
#include "deck.hpp"
#include "sources.hpp"
#include "term.hpp"
#include "tui.hpp"
#include "util.hpp"

namespace ch {
namespace {

void write_file(const std::string &path, const std::string &bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) throw std::runtime_error("cannot write: " + path);
  f << bytes;
  if (!f.good()) throw std::runtime_error("write failed: " + path);
}

void emit(const Args &a, const std::string &text) {
  if (a.out.empty()) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
  } else {
    write_file(a.out, text);
  }
}

// A data file shown alone may name its own size; anything else takes the
// screen, or a fixed page when there is no screen to measure.
void text_size(const Args &a, const Deck &d, int &W, int &H) {
  TermSize ts = term_size();
  const bool tty = is_tty(STDOUT_FILENO) && a.out.empty();
  W = tty ? ts.w : 100;
  H = tty ? ts.h - 1 : 32; // leave the shell its prompt line
  if (d.implicit && d.slides.size() == 1) {
    std::vector<const Block *> c = chart_blocks(d.slides[0]);
    if (c.size() == 1) {
      const ChartSpec &s = c[0]->ds.spec;
      if (s.has_width) W = s.width;
      if (s.has_height) H = s.height;
    }
  }
  if (a.width > 0) W = a.width;
  if (a.height > 0) H = a.height;
  W = std::max(W, 20);
  H = std::max(H, 6);
}

std::string absolute(const std::string &p) {
  if (!p.empty() && p[0] == '/') return p;
  char buf[4096];
  if (!::getcwd(buf, sizeof buf)) return p;
  return std::string(buf) + "/" + p;
}

std::string sh_quote(const std::string &s) {
  std::string o = "'";
  for (char c : s) o += (c == '\'') ? std::string("'\\''") : std::string(1, c);
  return o + "'";
}

// For a comment line: a newline in a name would end the comment and start a
// command, so control characters are blanked.
std::string one_line(std::string s) {
  for (char &c : s)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
  return s;
}

// The script decides nothing about looks: that is all in the deck.  It finds
// charts, finds a terminal when started without one (a desktop icon, a
// keybinding), and hands over; charts itself then picks framebuffer, kitty,
// sixel or cells from where it finds itself.
std::string write_launcher(const std::string &deck, const std::string &name) {
  std::string path = name;
  if (name.find('/') == std::string::npos) {
    const char *home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/.local/bin";
    ::mkdir((std::string(home ? home : ".") + "/.local").c_str(), 0755);
    ::mkdir(dir.c_str(), 0755);
    path = dir + "/" + name;
  }
  std::string self = "charts";
  char exe[4096];
  ssize_t n = ::readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (n > 0) { exe[n] = 0; self = exe; }
  std::string s =
      "#!/bin/sh\n"
      "# Made by: charts " + one_line(deck) + " --launcher " + one_line(name) + "\n"
      "# Presents the deck.  Left/right to page, ? for keys, q to quit.\n"
      "DECK=" + sh_quote(absolute(deck)) + "\n"
      "CHARTS=" + sh_quote(self) + "\n"
      "[ -x \"$CHARTS\" ] || CHARTS=$(command -v charts) || { echo 'charts is not installed' >&2; exit 127; }\n"
      "[ -r \"$DECK\" ] || { echo \"deck not found: $DECK\" >&2; exit 1; }\n"
      "if [ -t 0 ] && [ -t 1 ]; then\n"
      "  exec \"$CHARTS\" \"$DECK\" \"$@\"   # console: framebuffer; kitty/ghostty/wezterm/foot: graphics; else cells\n"
      "fi\n"
      "# No terminal (started from a menu or a key binding): open one.\n"
      "if [ -n \"$WAYLAND_DISPLAY$DISPLAY\" ]; then\n"
      "  for T in kitty ghostty foot wezterm alacritty xterm; do\n"
      "    command -v $T >/dev/null 2>&1 || continue\n"
      "    case $T in\n"
      "      wezterm) exec wezterm start -- \"$CHARTS\" \"$DECK\" \"$@\" ;;\n"
      "      *)       exec $T -e \"$CHARTS\" \"$DECK\" \"$@\" ;;\n"
      "    esac\n"
      "  done\n"
      "fi\n"
      "echo 'run this from a terminal or a console' >&2\n"
      "exit 1\n";
  write_file(path, s);
  ::chmod(path.c_str(), 0755);
  return path;
}

int report(const Deck &d, bool to_stderr) {
  if (d.issues.empty()) return 0;
  std::string t = issues_text(d);
  std::fwrite(t.data(), 1, t.size(), to_stderr ? stderr : stdout);
  return d.ok() ? 0 : 1;
}

} // namespace
} // namespace ch

int main(int argc, char **argv) {
  using namespace ch;
  try {
    Args a = parse_args(argc, argv);
    if (a.help) { print_help(stdout); return 0; }
    if (a.version) { print_version(stdout); return 0; }
    if (a.schema) { print_schema(stdout); return 0; }
    if (a.list_types) { print_types(stdout); return 0; }
    if (a.list_palettes) { print_palettes(stdout); return 0; }
    if (a.list_themes) { print_themes(stdout); return 0; }
    if (!a.example.empty()) { print_example(stdout, a.example); return 0; }

    if (a.ascii) use_ascii_glyphs(); else use_unicode_glyphs();

    // A directory stands for the data files in it; a deck that lives beside
    // them is not one of those.
    std::vector<std::string> files;
    for (const auto &arg : a.files) {
      bool dir = is_dir(arg);
      for (const auto &f : expand_sources({arg}))
        if (!dir || !is_deck_file(f)) files.push_back(f);
    }
    if (a.demo && !files.empty()) throw UsageError("--demo shows its own deck: give it no files");
    if (files.empty() && !a.demo) {
      // Nothing named: take a pipe if there is one with something in it.
      bool piped = false;
      if (!is_tty(STDIN_FILENO)) {
        int c = std::getchar();
        if (c != EOF) { std::ungetc(c, stdin); piped = true; }
      }
      if (piped) files.push_back("-");
      else {
        std::fputs("charts: no input.  To start:\n"
                   "  charts --demo                       a short deck, built in: arrows to page, q to quit\n"
                   "  charts --example deck > deck.json   a deck to start from (charts --example csv > revenue.csv beside it)\n"
                   "  charts deck.json                    present it\n"
                   "  charts data.csv                     one chart, printed into the shell\n"
                   "  charts -h                           the whole manual, written for the agents that make decks\n",
                   stderr);
        return 2;
      }
    }

    // ---- what are we looking at
    std::size_t decks = a.demo ? 1 : 0;
    for (const auto &f : files)
      if (is_deck_file(f)) decks++;
    if (decks > 1 || (decks == 1 && files.size() > 1))
      throw UsageError("give one deck, or data files, not a mix: put the data files in the deck instead");

    Deck deck;
    if (a.demo) {
      deck = deck_from_text(demo_deck(), a.load);
    } else if (decks == 1) {
      deck = load_deck(files[0], a.load);
    } else {
      FileSlides fs;
      fs.files = files;
      fs.types = a.types;
      fs.tile = a.tile || (a.types.size() > 1 && files.size() == 1);
      deck = deck_from_files(fs, a.load);
    }
    ChartSpec cli = a.cli;
    if (decks == 1 && a.types.size() == 1) { cli.type = a.types[0]; cli.has_type = true; }

    const bool to_png = !a.png.empty() || !a.png_dir.empty();
    const bool tty_out = is_tty(STDOUT_FILENO) && a.out.empty();
    const bool wants_screen = !a.check && !a.describe && !to_png && !a.print;
    if (wants_screen && a.show && !(tty_out && is_tty(STDIN_FILENO))) {
      std::fprintf(stderr, "charts: -i needs a terminal on stdin and stdout (use --print or --png without one)\n");
      return 2;
    }
    const bool present = wants_screen && tty_out && is_tty(STDIN_FILENO) && (a.show || decks == 1);

    // Printed into a shell a chart has no backdrop; everywhere else it is DOS.
    if (!a.theme.empty()) set_theme(a.theme);
    else if (!deck.theme.empty()) use_deck_theme(deck, "");
    else set_theme((present || to_png || decks == 1) ? "dos" : "black");

    // What the deck says about its own display fills in whatever the command
    // line left alone.
    if (a.gfx == "auto" && !deck.gfx.empty()) a.gfx = deck.gfx;
    if (a.scale == 0) a.scale = deck.scale;
    if (a.width == 0) a.width = deck.cols;
    if (a.height == 0) a.height = deck.rows;
    if (!a.ascii && deck.ascii == 1) { a.ascii = true; use_ascii_glyphs(); }
    if (!a.color_forced && deck.color == 0) a.color = false;

    // ---- a launcher: the one thing the human runs
    if (!a.launcher.empty()) {
      if (decks != 1 || a.demo) throw UsageError("--launcher wants a deck file: charts DECK.json --launcher NAME");
      std::string path = write_launcher(files[0], a.launcher);
      std::printf("%s\n", path.c_str());
      return report(deck, true);
    }

    // ---- check / describe
    if (a.check) {
      // Valid is not the same as legible: draw every slide the size a 1080p
      // console shows it, and report what had to be cut.
      Mode pm;
      pm.pixel = true;
      SlideView cv;
      cv.cli = &cli;
      for (std::size_t i = 0; i < deck.slides.size(); i++) {
        Scene sc(a.width > 0 ? a.width : 120, a.height > 0 ? a.height : 33, pm);
        render_slide(sc, deck, static_cast<int>(i), cv);
        for (const auto &f : sc.fit) deck.issues.push_back({false, f.first, f.second});
      }
      if (a.json) {
        std::string j = issues_json(deck);
        std::fwrite(j.data(), 1, j.size(), stdout);
      } else {
        report(deck, false);
        std::size_t errors = 0;
        for (const auto &i : deck.issues)
          if (i.error) errors++;
        std::printf("%s: %zu slide%s, %zu error%s, %zu warning%s\n", deck.ok() ? "ok" : "FAILED", deck.slides.size(),
                    deck.slides.size() == 1 ? "" : "s", errors, errors == 1 ? "" : "s", deck.issues.size() - errors,
                    deck.issues.size() - errors == 1 ? "" : "s");
      }
      return deck.ok() ? 0 : 1;
    }
    if (a.describe) {
      std::string out;
      if (decks == 1) out = deck_outline(deck);
      else
        for (auto &s : deck.slides)
          for (Block *b : chart_blocks(s)) {
            if (!b->error.empty()) continue;
            if (b->ds.source.empty()) b->ds.source = "<stdin>";
            out += describe(b->ds) + (deck.slides.size() > 1 ? "\n" : "");
          }
      std::fwrite(out.data(), 1, out.size(), stdout);
      return report(deck, true);
    }

    if (deck.slides.empty()) { report(deck, true); return 1; }
    if (a.slide > static_cast<int>(deck.slides.size()))
      throw std::runtime_error("--slide " + std::to_string(a.slide) + ": the deck has " + std::to_string(deck.slides.size()) + " slides");

    // ---- present
    if (present) {
      PresentOpts po;
      po.gfx = a.ascii ? "ascii" : a.gfx;
      po.theme = a.theme;
      po.scale = a.scale;
      po.slide = a.slide > 0 ? a.slide - 1 : 0;
      po.color = a.color;
      po.verbose = a.verbose;
      po.lo = a.load;
      po.cli = cli;
      return run_presenter(deck, po);
    }

    SlideView view;
    view.cli = &cli;
    view.chrome = decks == 1; // a bare chart needs no footer bar

    // ---- png
    if (to_png) {
      Mode m;
      m.pixel = true;
      int W = a.width > 0 ? a.width : 120, H = a.height > 0 ? a.height : 33; // a 1080p console at scale 2
      auto render = [&](int i) {
        Scene sc(W, H, m);
        render_slide(sc, deck, i, view);
        return png_encode(sc.to_image().scaled(a.scale > 0 ? a.scale : 1));
      };
      if (!a.png.empty()) {
        write_file(a.png, render(a.slide > 0 ? a.slide - 1 : 0));
        if (a.verbose) std::fprintf(stderr, "charts: wrote %s\n", a.png.c_str());
      } else {
        ::mkdir(a.png_dir.c_str(), 0777);
        for (std::size_t i = 0; i < deck.slides.size(); i++) {
          if (a.slide > 0 && static_cast<int>(i) != a.slide - 1) continue;
          char name[32];
          std::snprintf(name, sizeof name, "/slide-%02zu.png", i + 1);
          write_file(a.png_dir + name, render(static_cast<int>(i)));
          std::printf("%s%s\n", a.png_dir.c_str(), name);
        }
      }
      return report(deck, true);
    }

    // ---- print
    bool color = a.color;
    if (!a.color_forced && !tty_out) color = false;
    Mode m;
    m.ascii = a.ascii;
    m.color = color;
    int W = 0, H = 0;
    text_size(a, deck, W, H);
    std::string out;
    for (std::size_t i = 0; i < deck.slides.size(); i++) {
      if (a.slide > 0 && static_cast<int>(i) != a.slide - 1) continue;
      Scene sc(W, H, m);
      render_slide(sc, deck, static_cast<int>(i), view);
      if (!out.empty()) out += "\n";
      out += sc.to_cells().dump(color, false, std::string(std::getenv("TERM") ? std::getenv("TERM") : "") != "linux");
    }
    emit(a, out);
    return report(deck, true);
  } catch (const UsageError &e) {
    std::fprintf(stderr, "charts: %s\n", e.what());
    return 2;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "charts: %s\n", e.what());
    return 1;
  }
}
