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
  if (c == 3) return "ctrl-c";
  if (c == 4) return "ctrl-d";
  if (c != 27) return std::string(1, static_cast<char>(c));

  std::string seq;
  for (int i = 0; i < 8; i++) {
    int b = read_byte(25);
    if (b < 0) break;
    seq += static_cast<char>(b);
    if ((b >= 'A' && b <= 'Z') || b == '~') break;
  }
  if (seq.empty()) return "esc";
  if (seq == "[A" || seq == "OA") return "up";
  if (seq == "[B" || seq == "OB") return "down";
  if (seq == "[C" || seq == "OC") return "right";
  if (seq == "[D" || seq == "OD") return "left";
  if (seq == "[5~") return "pgup";
  if (seq == "[6~") return "pgdn";
  if (seq == "[H" || seq == "[1~") return "home";
  if (seq == "[F" || seq == "[4~") return "end";
  if (seq == "[Z") return "shift-tab";
  return "esc";
}

} // namespace ch
