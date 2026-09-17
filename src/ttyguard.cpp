#include "ttyguard.hpp"

#include <csignal>
#include <cstring>
#include <termios.h>
#include <unistd.h>

namespace ch {

namespace {
bool g_have = false;
struct termios g_saved;
bool g_installed = false;

void on_fatal(int sig) {
  const char *reset = "\x1b[0m\x1b[?25h";
  ssize_t n = ::write(STDOUT_FILENO, reset, std::strlen(reset));
  (void)n;
  if (g_have) ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
  ::signal(sig, SIG_DFL);
  ::raise(sig);
}
} // namespace

void tty_guard_install() {
  if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &g_saved) == 0) g_have = true;
  if (g_installed) return;
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_fatal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGTERM, &sa, nullptr);
  ::sigaction(SIGHUP, &sa, nullptr);
  g_installed = true;
}

void tty_guard_restore() {
  if (g_have) ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
}

} // namespace ch
