// ttyguard.hpp -- keep the terminal usable even if we are killed mid-frame.
#pragma once

namespace ch {

// Captures the current termios and installs SIGINT/SIGTERM/SIGHUP handlers that
// restore it, show the cursor and reset colours before dying. SIGWINCH is left
// alone on purpose: the main loops poll the size anyway.
void tty_guard_install();
void tty_guard_restore();

} // namespace ch
