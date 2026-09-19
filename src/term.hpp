// term.hpp -- raw tty handling: size, raw mode, key decode.
#pragma once

#include <string>

namespace ch {

struct TermSize {
  int w = 80;
  int h = 24;
};

bool is_tty(int fd);
TermSize term_size();

// RAII: put the tty in raw mode, restore on destruction.
class RawMode {
public:
  explicit RawMode(int fd);
  ~RawMode();
  RawMode(const RawMode &) = delete;
  RawMode &operator=(const RawMode &) = delete;
  bool ok() const { return ok_; }

private:
  int fd_;
  bool ok_ = false;
  void *saved_ = nullptr;
};

void cursor_show(bool show);
void screen_clear();
void cursor_home();

// Returns "" on timeout.  Named keys: up down left right pgup pgdn home end
// insert delete tab shift-tab enter backspace esc f1..f10 ctrl-a..ctrl-z
// alt-X.  Anything printable comes back as itself, as one UTF-8 character.
std::string read_key(int timeout_ms);

} // namespace ch
