#include "ttyguard.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <termios.h>
#include <unistd.h>

namespace ch {

namespace {
bool g_have = false;
struct termios g_saved;
bool g_installed = false;
void (*volatile g_cleanup)() = nullptr;

void run_cleanup() {
  void (*fn)() = g_cleanup;
  g_cleanup = nullptr;
  if (fn) fn();
}

void on_fatal(int sig) {
  run_cleanup();
  const char *reset = "\x1b[0m\x1b[?25h\x1b[?1049l";
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
  for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGPIPE})
    ::sigaction(sig, &sa, nullptr);
  std::atexit(run_cleanup);
  g_installed = true;
}

void tty_guard_restore() {
  run_cleanup();
  if (g_have) ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
}

void tty_guard_set_cleanup(void (*fn)()) { g_cleanup = fn; }

} // namespace ch
