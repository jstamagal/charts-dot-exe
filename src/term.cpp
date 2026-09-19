#include "term.hpp"

#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace ch {

bool is_tty(int fd) { return ::isatty(fd) == 1; }

static bool winsz(int fd, TermSize &t) {
  struct winsize ws;
  std::memset(&ws, 0, sizeof ws);
  if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    t.w = ws.ws_col;
    t.h = ws.ws_row;
    return true;
  }
  return false;
}

TermSize term_size() {
  TermSize t;
  if (winsz(STDOUT_FILENO, t)) return t;
  if (winsz(STDIN_FILENO, t)) return t;
  if (winsz(STDERR_FILENO, t)) return t;
  const char *c = std::getenv("COLUMNS");
  const char *l = std::getenv("LINES");
  if (c && std::atoi(c) > 0) t.w = std::atoi(c);
  if (l && std::atoi(l) > 0) t.h = std::atoi(l);
  if (t.w < 1) t.w = 80;
  if (t.h < 1) t.h = 24;
  return t;
}

RawMode::RawMode(int fd) : fd_(fd) {
  if (!::isatty(fd)) return;
  struct termios *t = new termios();
  if (::tcgetattr(fd, t) != 0) {
    delete t;
    return;
  }
  saved_ = t;
  struct termios raw = *t;
  raw.c_lflag &= ~(ICANON | ECHO);
  // ISIG off so ctrl-c arrives as a byte and takes the clean exit path:
  // cursor shown, screen cleared, colours reset. An outside kill still lands
  // on the signal handlers in ttyguard.cpp.
  raw.c_lflag &= ~ISIG;
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~OPOST; // frames carry their own \r\n
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (::tcsetattr(fd, TCSANOW, &raw) != 0) {
    delete t;
    saved_ = nullptr;
    return;
  }
  ok_ = true;
}

RawMode::~RawMode() {
  if (saved_) {
    ::tcsetattr(fd_, TCSANOW, static_cast<struct termios *>(saved_));
    delete static_cast<struct termios *>(saved_);
  }
}

void cursor_show(bool show) {
  const char *s = show ? "\x1b[?25h" : "\x1b[?25l";
  ssize_t n = ::write(STDOUT_FILENO, s, std::strlen(s));
  (void)n;
}

void screen_clear() {
  const char *s = "\x1b[2J\x1b[H";
  ssize_t n = ::write(STDOUT_FILENO, s, std::strlen(s));
  (void)n;
}

void cursor_home() {
  const char *s = "\x1b[H";
  ssize_t n = ::write(STDOUT_FILENO, s, std::strlen(s));
  (void)n;
}

static int read_byte(int ms) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  struct timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  int r = ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
  if (r <= 0) return -1;
  unsigned char c = 0;
  if (::read(STDIN_FILENO, &c, 1) != 1) return -1;
  return c;
}

std::string read_key(int timeout_ms) {
  int c = read_byte(timeout_ms);
  if (c < 0) return "";
  if (c == 9) return "tab";
  if (c == 13 || c == 10) return "enter";
  if (c == 127 || c == 8) return "backspace";
  if (c < 27) return std::string("ctrl-") + static_cast<char>('a' + c - 1);
  if (c >= 0x80) { // one UTF-8 character, however many bytes it takes
    std::string s(1, static_cast<char>(c));
    int more = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC0 ? 1 : 0));
    for (int i = 0; i < more; i++) {
      int b = read_byte(25);
      if (b < 0) break;
      s += static_cast<char>(b);
    }
    return s;
  }
  if (c != 27) return std::string(1, static_cast<char>(c));

  std::string seq;
  for (int i = 0; i < 12; i++) {
    int b = read_byte(25);
    if (b < 0) break;
    seq += static_cast<char>(b);
    if (seq == "[" || seq == "O" || seq == "[[") continue;
    if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || b == '~') break;
  }
  if (seq.empty()) return "esc";
  static const struct { const char *seq, *name; } keys[] = {
      {"[A", "up"},      {"OA", "up"},      {"[B", "down"},    {"OB", "down"},   {"[C", "right"},  {"OC", "right"},
      {"[D", "left"},    {"OD", "left"},    {"[5~", "pgup"},   {"[6~", "pgdn"},  {"[H", "home"},   {"[1~", "home"},
      {"OH", "home"},    {"[7~", "home"},   {"[F", "end"},     {"[4~", "end"},   {"OF", "end"},    {"[8~", "end"},
      {"[2~", "insert"}, {"[3~", "delete"}, {"[Z", "shift-tab"},
      {"OP", "f1"},      {"[11~", "f1"},    {"[[A", "f1"},     {"OQ", "f2"},     {"[12~", "f2"},   {"[[B", "f2"},
      {"OR", "f3"},      {"[13~", "f3"},    {"[[C", "f3"},     {"OS", "f4"},     {"[14~", "f4"},   {"[[D", "f4"},
      {"[15~", "f5"},    {"[[E", "f5"},     {"[17~", "f6"},    {"[18~", "f7"},   {"[19~", "f8"},   {"[20~", "f9"},
      {"[21~", "f10"},
      {"[1;5C", "ctrl-right"}, {"[1;5D", "ctrl-left"}, {"[1;2C", "shift-right"}, {"[1;2D", "shift-left"},
  };
  for (const auto &k : keys)
    if (seq == k.seq) return k.name;
  if (seq.size() == 1 && seq[0] >= 32) return std::string("alt-") + seq; // Alt+key
  return "unknown";
}

} // namespace ch
