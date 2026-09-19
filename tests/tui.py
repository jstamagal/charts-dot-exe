#!/usr/bin/env python3
"""tests/tui.py -- drive the presenter through a real pty.

Pages through a deck, edits data in the sheet, annotates, saves, and checks
what landed on disk; then checks each display backend produces a well-formed
stream, and that the terminal is left standing however the program ends.

usage: tests/tui.py ./charts [examples-dir]
"""
import base64
import fcntl
import json
import os
import pty
import re
import select
import shutil
import signal
import struct
import sys
import tempfile
import termios
import time
import zlib

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./charts")
EXAMPLES = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "examples")

passed = failed = 0


def ok(msg):
    global passed
    passed += 1
    print("  ok   %s" % msg)


def bad(msg):
    global failed
    failed += 1
    print("  FAIL %s" % msg)


def check(msg, cond, detail=""):
    ok(msg) if cond else bad(msg + (" (%s)" % detail if detail else ""))


KEYS = {"LEFT": "\x1b[D", "RIGHT": "\x1b[C", "UP": "\x1b[A", "DOWN": "\x1b[B", "ENTER": "\r", "ESC": "\x1b",
        "TAB": "\t", "INS": "\x1b[2~", "DEL": "\x1b[3~", "BS": "\x7f", "HOME": "\x1b[H", "END": "\x1b[F"}


class Session:
    """The program on a pty of a fixed size, with a crude screen model: the
    text of the last full frame, which is all these tests need."""

    def __init__(self, args, cols=110, rows=32, xpix=0, ypix=0, env=None):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            e = dict(os.environ, TERM="xterm-256color")
            e.pop("TMUX", None)
            e.pop("CHARTS_GFX", None)
            e.pop("NO_COLOR", None)
            e.update(env or {})
            os.execve(BIN, [BIN] + args, e)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, xpix, ypix))
        self.raw = b""
        self.read(0.7)

    def read(self, secs):
        end = time.time() + secs
        got = b""
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if not r:
                continue
            try:
                chunk = os.read(self.fd, 1 << 16)
            except OSError:
                break
            if not chunk:
                break
            got += chunk
        self.raw += got
        return got

    def keys(self, *tokens, settle=0.35):
        out = b""
        for t in tokens:
            if t in KEYS:
                data = KEYS[t]
            elif t.startswith("^") and len(t) == 2:
                data = chr(ord(t[1].upper()) - 64)
            else:
                data = t
            os.write(self.fd, data.encode())
            out += self.read(0.3 if t == "ESC" else 0.12)
        out += self.read(settle)
        return out

    def screen(self, frame=None):
        """Plain text of the last frame drawn."""
        data = self.raw if frame is None else frame
        last = data.rfind(b"\x1b[H")
        text = data[last:].decode("utf-8", "replace") if last >= 0 else ""
        return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)

    def alive(self):
        try:
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
            return pid == 0
        except ChildProcessError:
            return False

    def wait_exit(self, secs=3.0):
        end = time.time() + secs
        while time.time() < end:
            try:
                pid, status = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                return 0
            if pid:
                return status
            self.read(0.05)
        return None

    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except (OSError, ChildProcessError):
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


class JobSession(Session):
    """The presenter the way a job-control shell runs it: its own process
    group, in the foreground, under a parent in the same session.  Only there
    is a stop honoured; in an orphaned group (a bare pty.fork) the kernel
    drops SIGTSTP so nobody is left stopped with no one to continue them."""

    def __init__(self, args, cols=110, rows=32):
        r, w = os.pipe()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.close(r)
            job = os.fork()
            if job == 0:
                os.setpgid(0, 0)
                while os.tcgetpgrp(0) != os.getpgrp():  # not yet the foreground: wait
                    time.sleep(0.01)
                e = dict(os.environ, TERM="xterm-256color")
                for k in ("TMUX", "CHARTS_GFX", "NO_COLOR"):
                    e.pop(k, None)
                os.execve(BIN, [BIN] + args, e)
            try:
                os.setpgid(job, job)
            except OSError:
                pass
            os.tcsetpgrp(0, job)
            os.write(w, str(job).encode())
            os.close(w)
            while True:  # a shell's wait: stops come and go, the exit ends it
                _, st = os.waitpid(job, os.WUNTRACED)
                if os.WIFEXITED(st) or os.WIFSIGNALED(st):
                    os._exit(0)
        os.close(w)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.job = int(os.read(r, 32) or b"0")
        os.close(r)
        self.raw = b""
        self.read(0.7)

    def stopped(self):
        try:
            with open("/proc/%d/stat" % self.job) as f:
                return f.read().rsplit(")", 1)[1].split()[0] == "T"
        except OSError:
            return False

    def kill(self):
        for sig in (signal.SIGCONT, signal.SIGKILL):
            try:
                os.kill(self.job, sig)
            except OSError:
                pass
        super().kill()


def workdir():
    d = tempfile.mkdtemp(prefix="charts-tui-")
    for name in os.listdir(EXAMPLES):
        src = os.path.join(EXAMPLES, name)
        if os.path.isfile(src):
            shutil.copy(src, d)
    return d


def cleanup(d):
    trash = shutil.which("trash")
    if trash:
        os.spawnv(os.P_WAIT, trash, [trash, d])


# ---------------------------------------------------------------------------
print("== paging through a deck")
d = workdir()
deck = os.path.join(d, "deck.json")
s = Session([deck, "--gfx", "cells"])
check("opens on the title slide", "Q3 review" in s.screen())
check("counter says 1/3", "1/3" in s.screen())
s.keys("RIGHT")
check("right arrow goes to slide 2", "Revenue grew every month" in s.screen() and "2/3" in s.screen())
check("the annotation is drawn", "spring launch" in s.screen())
s.keys(" ")
check("space goes to slide 3", "Where the money went" in s.screen())
s.keys("RIGHT")
check("stops at the last slide", "3/3" in s.screen())
s.keys("LEFT")
check("left arrow goes back", "2/3" in s.screen())
s.keys("1", "ENTER")
check("1 enter jumps to slide 1", "1/3" in s.screen())
s.keys("END")
check("end jumps to the last", "3/3" in s.screen())
s.keys("o")
check("o opens the overview", "Where the money went" in s.screen() and "pick" in s.screen())
s.keys("UP", "ENTER")
check("overview: up, enter lands on slide 2", "2/3" in s.screen())
s.keys("?")
check("? opens the key list", "annotate the chart" in s.screen())
s.keys("x")
check("any key closes it", "annotate the chart" not in s.screen())
s.keys("n")
check("n shows speaker notes", "warehouse move" in s.screen())
s.keys("n")
s.keys("t")
check("t changes the chart type", "type:" in s.screen())
s.keys("q")
st = s.wait_exit()
check("q quits without a fuss when only the view changed", st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0,
      "status %r" % (st,))
s.kill()

# ---------------------------------------------------------------------------
print("== the sheet: edit, undo, rows, save")
csv_path = os.path.join(d, "revenue.csv")
with open(csv_path) as f:
    body = f.read()
with open(csv_path, "w") as f:
    f.write("#chart: ylabel=USD\n# a comment that must survive\n" + body)
s = Session([deck, "--gfx", "cells", "--slide", "2"])
s.keys("e")
check("e opens the sheet", "revenue.csv" in s.screen() and "ins row" in s.screen())
s.keys("DOWN", "DOWN", "4", "3", "2", "1", "ENTER")
check("typing replaces the cell", "4,321" in s.screen())
check("the file is marked dirty", "revenue.csv *" in s.screen())
s.keys("u")
check("u undoes it", "4,321" not in s.screen())
s.keys("UP", "7", "7", "7", "ENTER")
s.keys("a", "b", "c", "ENTER")
check("letters in a number cell are refused", "not a number" in s.screen())
s.keys("ESC")
s.keys("INS", "D", "e", "c", "2", "TAB", "5", "5", "5", "ENTER")
check("ins adds a row", "Dec2" in s.screen())
s.keys("s")
check("s reports the save", "saved" in s.screen())
with open(csv_path) as f:
    saved = f.read()
check("the edit reached the file", re.search(r"^Mar,777,", saved, re.M) is not None, saved[:200])
check("the new row reached the file", re.search(r"^Dec2,555", saved, re.M) is not None)
check("the #chart line survived", saved.startswith("#chart: ylabel=USD\n# a comment that must survive\n"))
check("the header survived", "month,revenue,costs" in saved)
s.keys("^D")
with_row = s.screen()
s.keys("ESC", "q")
check("quitting with unsaved edits asks first", "unsaved" in s.screen().lower(), s.screen()[-300:])
s.keys("ESC")
check("esc stays", s.alive())
s.keys("q", "q")
st = s.wait_exit()
check("q again quits anyway", st is not None)
s.kill()
with open(csv_path) as f:
    check("unsaved edits did not reach the file", f.read() == saved)

# ---------------------------------------------------------------------------
print("== annotate and save into the deck")
s = Session([deck, "--gfx", "cells", "--slide", "2"])
s.keys("a")
check("a enters annotate mode", "enter note" in s.screen())
s.keys("RIGHT", "RIGHT")
check("the status line follows the cursor", "Mar" in s.screen())
s.keys("ENTER")
s.keys(*"dip here", "ENTER")
check("the note is drawn", "dip here" in s.screen())
s.keys("ESC", "s")
check("save reports the deck", "deck.json" in s.screen())
with open(deck) as f:
    j = json.load(f)
notes = j["slides"][1].get("annotations", [])
mine = [n for n in notes if n.get("text") == "dip here"]
check("the note is in the deck's JSON", len(mine) == 1, json.dumps(notes))
check("it names the category", mine and mine[0].get("at") == "Mar")
check("the agent's annotations are still there", any(n.get("text") == "spring launch" for n in notes))
s.keys("q")
s.wait_exit()
s.kill()

# ---------------------------------------------------------------------------
print("== text, titles, new slides")
s = Session([deck, "--gfx", "cells", "--slide", "3"])
s.keys("N")
s.keys(*"Next steps", "ENTER")
check("N makes a slide", "Next steps" in s.screen() and "4/4" in s.screen())
s.keys("i")
s.keys(*"- hire two", "ENTER", *"- ship it", "ESC")
check("i adds a text block", "hire two" in s.screen() and "ship it" in s.screen())
s.keys("E")
s.keys(*" now", "ENTER")
check("E edits the title", "Next steps now" in s.screen())
s.keys("s")
with open(deck) as f:
    j = json.load(f)
check("the new slide is saved", len(j["slides"]) == 4 and j["slides"][3]["title"] == "Next steps now")
check("with its text", j["slides"][3]["blocks"][0]["text"] == ["- hire two", "- ship it"], json.dumps(j["slides"][3]))

print("== an agent rewrites the deck while it is open")
j["slides"][3]["title"] = "Rewritten from outside"
time.sleep(1.1)
with open(deck, "w") as f:
    json.dump(j, f)
s.read(1.5)
check("the screen follows the file", "Rewritten from outside" in s.screen(), s.screen()[:200])
s.keys("q")
s.wait_exit()
s.kill()

# ---------------------------------------------------------------------------
print("== the data file changes under the sheet and the annotate cursor")
with open(csv_path, "w") as f:
    f.write(body)
s = Session([deck, "--gfx", "cells", "--slide", "2"])
s.keys("e", "RIGHT", "RIGHT", "DOWN", "DOWN")
check("the sheet is on the last column", "costs · Mar" in s.screen(), s.screen()[-200:])
time.sleep(0.3)
with open(csv_path, "w") as f:
    f.write("month,revenue\nJan,1\n")
s.read(1.0)
check("the presenter survives the column going away", s.alive())
s.keys("ENTER", "9", "ENTER", "DOWN", "^D", "^X")
check("and the keys that follow", s.alive())
check("the cursor moved into what is left", "revenue" in s.screen() and "costs ·" not in s.screen(), s.screen()[-200:])
s.keys("ESC", "q")
check("the edit made before the reload still counts as unsaved", "unsaved" in s.screen().lower(), s.screen()[-300:])
s.keys("q")
st = s.wait_exit()
check("and quits cleanly", st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "status %r" % (st,))
s.kill()

with open(csv_path, "w") as f:
    f.write(body)
s = Session([deck, "--gfx", "cells", "--slide", "2"])
s.keys("a", "RIGHT", "RIGHT")
check("annotate mode is on", "enter note" in s.screen())
time.sleep(0.3)
with open(csv_path, "w") as f:
    f.write("garbage\n")
s.read(1.0)
s.keys("RIGHT", "LEFT", "UP", "ENTER")
check("annotate mode survives the file breaking", s.alive())
s.keys("ESC", "e")
check("a chart whose file broke cannot be edited", "no data under it" in s.screen(), s.screen()[-300:])
s.keys("q")
st = s.wait_exit()
check("and quits cleanly, nothing having been edited", st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0,
      "status %r" % (st,))
s.kill()
with open(csv_path, "w") as f:
    f.write(body)

# ---------------------------------------------------------------------------
print("== a bare data file")
q = os.path.join(d, "quarterly.csv")
s = Session(["-i", q, "--gfx", "cells"])
check("-i opens a csv", "north" in s.screen())
s.keys("a", "ENTER")
s.keys(*"first", "ENTER")
s.keys("ESC", "s")
made = os.path.join(d, "quarterly.deck.json")
check("saving an annotation makes quarterly.deck.json", os.path.exists(made), s.screen()[-200:])
if os.path.exists(made):
    with open(made) as f:
        j = json.load(f)
    check("which points at the csv beside it", j["slides"][0]["data"] == "quarterly.csv", json.dumps(j)[:200])
s.keys("q")
s.wait_exit()
s.kill()

# ---------------------------------------------------------------------------
print("== the terminal survives")
s = Session([deck, "--gfx", "cells"])
s.keys("q")
s.wait_exit()
check("the cursor is shown again on exit", b"\x1b[?25h" in s.raw)
s.kill()
s = Session([deck, "--gfx", "cells"])
os.kill(s.pid, signal.SIGTERM)
s.wait_exit()
s.read(0.3)
check("and after SIGTERM", b"\x1b[?25h" in s.raw)
s.kill()

s = JobSession([deck, "--gfx", "cells"])
cooked = lambda: bool(termios.tcgetattr(s.fd)[3] & termios.ICANON)
check("the presenter runs raw", not cooked())
mark = len(s.raw)
os.kill(s.job, signal.SIGTSTP)
s.read(0.5)
check("a stop from outside stops it", s.stopped())
check("with the terminal handed back cooked", cooked())
check("and the cursor shown", b"\x1b[?25h" in s.raw[mark:])
mark = len(s.raw)
os.kill(s.job, signal.SIGCONT)
s.read(1.0)
check("a continue takes the terminal back raw", not cooked())
check("and draws the slide again", b"\x1b[H" in s.raw[mark:] and len(s.screen().strip()) > 0)
s.keys("q")
check("and it still quits cleanly", s.wait_exit() is not None)
s.kill()

s = Session([deck, "--gfx", "cells"], cols=110, rows=32)
before = s.screen()
fcntl.ioctl(s.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
s.read(1.0)
after = s.screen()
lines = [l.rstrip("\r") for l in after.split("\n")]
check("a resize reflows the slide", after != before and len(lines) <= 24 and max(len(l) for l in lines) <= 80,
      "%d lines" % len(lines))
s.keys("q")
s.wait_exit()
s.kill()

# ---------------------------------------------------------------------------
print("== framebuffer")
fb = os.path.join(d, "fb.raw")
W, H = 1280, 720
with open(fb, "wb") as f:
    f.write(b"\0" * (W * H * 4))
s = Session([deck], env={"CHARTS_FB": fb, "CHARTS_FB_GEOM": "%dx%dx32" % (W, H), "TERM": "linux"})
with open(fb, "rb") as f:
    one = f.read()
check("a frame of the right size is written", len(one) == W * H * 4)
check("it is not blank", len(set(one[i:i + 4] for i in range(0, len(one), 4096 * 4 + 4))) > 2)
blue = bytes([0xAA, 0x00, 0x00])  # B G R of VGA blue
check("the desktop is VGA blue, in BGRX order", one[0:3] == blue, one[0:4].hex())
s.keys("RIGHT")
with open(fb, "rb") as f:
    two = f.read()
check("another slide is another picture", one != two)
s.keys("q")
s.wait_exit()
s.kill()

fb16 = os.path.join(d, "fb16.raw")
with open(fb16, "wb") as f:
    f.write(b"\0" * (800 * 600 * 2))
s = Session([deck], env={"CHARTS_FB": fb16, "CHARTS_FB_GEOM": "800x600x16", "TERM": "linux"})
with open(fb16, "rb") as f:
    px = struct.unpack("<H", f.read(2))[0]
check("16 bpp packs 5-6-5", px == (0xAA >> 3), hex(px))
s.keys("q")
s.wait_exit()
s.kill()

# ---------------------------------------------------------------------------
print("== kitty graphics")
s = Session([deck, "--gfx", "kitty"], cols=100, rows=30, xpix=1000, ypix=600)
chunks = re.findall(rb"\x1b_G([^;\x1b]*);([^\x1b]*)\x1b\\", s.raw)
payload = [c for c in chunks if c[1]]
check("an image is transmitted", len(payload) > 0)
check("no chunk is over 4096 bytes", all(len(c[1]) <= 4096 for c in payload))
check("the first chunk says PNG, quietly", payload and b"f=100" in payload[0][0] and b"q=2" in payload[0][0],
      payload[0][0].decode() if payload else "")
frames, cur = [], b""
for keys, data in payload:
    cur += data
    if b"m=0" in keys:
        frames.append(cur)
        cur = b""
png = base64.b64decode(frames[0]) if frames else b""
check("it decodes to a PNG", png[:8] == b"\x89PNG\r\n\x1a\n")
if png[:8] == b"\x89PNG\r\n\x1a\n":
    w, h = struct.unpack(">II", png[16:24])
    check("sized in whole 8x16 cells that fit the window", w % 8 == 0 and h % 16 == 0 and w <= 1000 and h <= 600,
          "%dx%d" % (w, h))
    i, idat, good = 8, b"", True
    while i < len(png):
        n = struct.unpack(">I", png[i:i + 4])[0]
        t, body = png[i + 4:i + 8], png[i + 8:i + 8 + n]
        good = good and zlib.crc32(t + body) == struct.unpack(">I", png[i + 8 + n:i + 12 + n])[0]
        if t == b"IDAT":
            idat += body
        i += 12 + n
    check("every chunk's CRC is right", good)
    check("the pixels inflate", len(zlib.decompress(idat)) == h * ((w + 1) // 2 + 1))
s.keys("q")
s.wait_exit()
check("the images are deleted on the way out", b"a=d,d=A" in s.raw)
s.kill()

print("== sixel")
s = Session([deck, "--gfx", "sixel"], cols=100, rows=30, xpix=1000, ypix=600)
m = re.search(rb"\x1bP[0-9;]*q(.*?)\x1b\\", s.raw, re.S)
check("a DCS sixel stream is sent and closed", m is not None)
if m:
    body = m.group(1)
    ra = re.match(rb'"1;1;(\d+);(\d+)', body)
    check("raster attributes give the size", ra is not None)
    check("sixteen colours are defined", all(b"#%d;2;" % i in body for i in range(16)))
    rest = re.sub(rb'"[0-9;]+|#\d+;2;\d+;\d+;\d+', b"", body)
    check("only sixel data follows", re.fullmatch(rb"[#0-9!$\-?-~]*", rest) is not None)
    if ra:
        check("it keeps off the last row, which would scroll", int(ra.group(2)) <= 29 * 20, ra.group(2).decode())
s.keys("q")
s.wait_exit()
s.kill()

cleanup(d)
print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
