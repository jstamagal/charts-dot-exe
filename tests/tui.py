#!/usr/bin/env python3
"""tests/tui.py -- drive the interactive screen through a real pty.

Runs the TUI against a fixed-size pseudo-terminal, presses keys and checks that
frames actually change and that the process leaves the terminal standing.

usage: tests/tui.py ./charts [examples/revenue.csv]
"""
import os
import pty
import select
import signal
import struct
import sys
import termios
import time
import fcntl

BIN = sys.argv[1] if len(sys.argv) > 1 else "./charts"
DATA = sys.argv[2] if len(sys.argv) > 2 else "examples/revenue.csv"

COLS, ROWS = 100, 28

passed = 0
failed = 0


def ok(msg):
    global passed
    passed += 1
    print("  ok   %s" % msg)


def bad(msg):
    global failed
    failed += 1
    print("  FAIL %s" % msg)


class Screen:
    def __init__(self, argv):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ["TERM"] = "linux"
            os.environ.pop("COLUMNS", None)
            os.environ.pop("LINES", None)
            try:
                os.execv(argv[0], argv)
            except Exception:
                os._exit(127)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", ROWS, COLS, 0, 0))
        self.buf = b""

    def drain(self, seconds=0.6):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if not r:
                continue
            try:
                chunk = os.read(self.fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            self.buf += chunk
        out = self.buf
        self.buf = b""
        return out

    def send(self, keys):
        os.write(self.fd, keys.encode())
        time.sleep(0.35)

    def alive(self):
        pid, status = os.waitpid(self.pid, os.WNOHANG)
        return pid == 0

    def quit_and_wait(self, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid != 0:
                return os.waitstatus_to_exitcode(status)
            time.sleep(0.05)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        return None

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


def text(frame):
    return frame.decode("utf-8", "replace")


print("== interactive screen")

s = Screen([os.path.abspath(BIN), "-i", DATA, "--no-color"])
frame = text(s.drain(1.2))
if not frame:
    bad("interactive draws a frame")
    s.close()
else:
    ok("interactive draws a frame")
    if "╔" in frame or "┌" in frame:
        ok("frame has a border")
    else:
        bad("frame has a border")
    if DATA.split("/")[-1] in frame:
        ok("title bar names the file")
    else:
        bad("title bar names the file (got %r)" % frame[:200])
    if "quit" in frame:
        ok("status bar shows the keys")
    else:
        bad("status bar shows the keys")

# 'c' cycles the chart type and the screen must change
s.send("c")
frame2 = text(s.drain(0.8))
if frame2 and frame2 != frame:
    ok("'c' redraws the screen")
else:
    bad("'c' redraws the screen")

# 'p' cycles the palette, 'v' toggles values, 't' opens the table
s.send("p")
if text(s.drain(0.8)):
    ok("'p' redraws the screen")
else:
    bad("'p' redraws the screen")

s.send("v")
if text(s.drain(0.8)):
    ok("'v' redraws the screen")
else:
    bad("'v' redraws the screen")

s.send("t")
table = text(s.drain(0.8))
if "north" in table or "Jan" in table or "revenue" in table:
    ok("'t' shows the numbers")
else:
    bad("'t' shows the numbers")

s.send("t")

# '?' opens the help box
s.send("?")
helpframe = text(s.drain(0.8))
if "palette" in helpframe and "explode" in helpframe:
    ok("'?' shows the key list")
else:
    bad("'?' shows the key list")
s.send("\x1b")

# a batch of keys must not crash it
s.send("cccccppppvvvvgggddeDDDttt11 9999")
s.send("\x1b[A\x1b[B\x1b[C\x1b[D")
frame3 = text(s.drain(1.0))
if s.alive():
    ok("survives a mashing of keys")
else:
    bad("survives a mashing of keys")

# leaving must restore the cursor
s.send("q")
rc = s.quit_and_wait()
if rc == 0:
    ok("'q' exits cleanly (rc=0)")
else:
    bad("'q' exits cleanly (got rc=%r)" % rc)
s.close()

# Ctrl-C in the middle must also leave cleanly
s2 = Screen([os.path.abspath(BIN), "-i", DATA, "--no-color"])
s2.drain(1.0)
s2.send("\x03")
rc = s2.quit_and_wait()
if rc == 0:
    ok("ctrl-c exits cleanly (rc=0)")
else:
    bad("ctrl-c exits cleanly (got rc=%r)" % rc)
s2.close()

# a broken file in the browser must show the error, not die
open("/tmp/broken-charts.csv", "w").write("a,b\n,x\n")
s3 = Screen([os.path.abspath(BIN), "-i", "/tmp/broken-charts.csv", "--no-color"])
f = text(s3.drain(1.2))
if "cannot read data" in f or "no numeric" in f:
    ok("broken input shows an error in-frame")
else:
    bad("broken input shows an error in-frame (got %r)" % f[:200])
s3.send("q")
s3.quit_and_wait()
s3.close()

# --watch redraws when the file changes
import tempfile
tmp = tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False)
tmp.write("a,b\n1,2\n3,4\n")
tmp.close()
s4 = Screen([os.path.abspath(BIN), tmp.name, "--watch", "--no-color"])
first = text(s4.drain(1.0))
time.sleep(0.3)
with open(tmp.name, "w") as fh:
    fh.write("a,b\n1,2\n3,4\n9,8\n11,12\n")
second = text(s4.drain(1.8))
if s4.alive() and second.strip():
    ok("--watch redraws after the file changes")
else:
    bad("--watch redraws after the file changes")
s4.send("q")
s4.quit_and_wait()
s4.close()
os.unlink(tmp.name)

print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
