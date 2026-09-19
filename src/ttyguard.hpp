// ttyguard.hpp -- keep the terminal usable even if we are killed mid-frame.
#pragma once

namespace ch {

// Captures the current termios and installs handlers for the signals that end
// a process (including SIGSEGV and SIGABRT: a crash must not leave the console
// stuck in graphics mode).  Each restores the tty, shows the cursor, resets
// colours and runs the cleanup hook before dying.
void tty_guard_install();
void tty_guard_restore();

// One extra thing to undo on the way out.  Must be async-signal-safe: ioctl()
// and write() only.  Also runs at normal exit.
void tty_guard_set_cleanup(void (*fn)());

} // namespace ch
