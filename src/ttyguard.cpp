#include "ttyguard.hpp"

#include <cerrno>
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
void (*volatile g_resume)() = nullptr;
volatile sig_atomic_t g_continued = 0;

void say(const char *s) {
  ssize_t n = ::write(STDOUT_FILENO, s, std::strlen(s));
  (void)n;
}

void run_cleanup() {
  void (*fn)() = g_cleanup;
  g_cleanup = nullptr;
  if (fn) fn();
}

void on_fatal(int sig) {
  run_cleanup();
  say("\x1b[0m\x1b[?25h");
  if (g_have) ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
  ::signal(sig, SIG_DFL);
  ::raise(sig);
}

void on_stop(int);

void arm_stop() {
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_stop;
  sigemptyset(&sa.sa_mask);
  ::sigaction(SIGTSTP, &sa, nullptr);
}

// Give the terminal back, stop for real (the default action, with the signal
// let through), and take it again once continued.
void on_stop(int) {
  const int saved_errno = errno;
  if (void (*down)() = g_cleanup) down();
  struct termios now;
  const bool had = g_have && ::tcgetattr(STDIN_FILENO, &now) == 0;
  if (g_have) ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
  say("\x1b[0m\x1b[2J\x1b[H\x1b[?25h");
  ::signal(SIGTSTP, SIG_DFL);
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTSTP);
  ::sigprocmask(SIG_UNBLOCK, &set, nullptr);
  ::raise(SIGTSTP);
  // ... SIGCONT
  arm_stop();
  if (had) ::tcsetattr(STDIN_FILENO, TCSANOW, &now);
  say("\x1b[?25l");
  if (void (*up)() = g_resume) up();
  g_continued = 1;
  errno = saved_errno;
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
  arm_stop();
  std::atexit(run_cleanup);
  g_installed = true;
}

void tty_guard_restore() {
  run_cleanup();
  if (g_have) ::tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
}

void tty_guard_set_cleanup(void (*fn)()) { g_cleanup = fn; }

void tty_guard_set_resume(void (*fn)()) { g_resume = fn; }

bool tty_guard_continued() {
  if (!g_continued) return false;
  g_continued = 0;
  return true;
}

} // namespace ch
