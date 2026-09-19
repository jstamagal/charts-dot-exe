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
// and write() only.  Also runs at normal exit, and on a stop (below).
void tty_guard_set_cleanup(void (*fn)());

// Stopped from outside (kill -TSTP, or a job-control shell; the presenter's
// own ctrl-z is off along with ISIG), the terminal is handed back the way it
// was found: cleanup hook, cooked mode, cursor.  On SIGCONT it is taken again
// and this hook runs, under the same rules.
void tty_guard_set_resume(void (*fn)());

// True once after a stop and a continue: whatever is on the screen now is
// not ours, so draw from scratch.
bool tty_guard_continued();

} // namespace ch
