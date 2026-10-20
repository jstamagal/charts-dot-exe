"""tests/conpty.py -- drive the Windows presenter through a real console.

usage: python tests/conpty.py path\\to\\charts.exe

Runs `charts.exe --demo` in a ConPTY pseudo console (pywinpty), checks the
first slide is drawn in UTF-8 box drawing, that the right arrow pages to
slide 2, and that q quits with status 0.
"""
import sys
import threading
import time

from winpty import PtyProcess


def main():
    exe = sys.argv[1]
    proc = PtyProcess.spawn([exe, "--demo"], dimensions=(30, 100))
    out = []
    lock = threading.Lock()

    def reader():
        while True:
            try:
                chunk = proc.read(65536)
            except EOFError:
                return
            except Exception:
                return
            with lock:
                out.append(chunk)

    threading.Thread(target=reader, daemon=True).start()

    def seen():
        with lock:
            return "".join(out)

    def wait_for(text, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if text in seen():
                return True
            time.sleep(0.1)
        return False

    failures = []
    if not wait_for("1/6", 30):
        failures.append("the first slide's status line (1/6) never appeared")
    if "║" not in seen() and "═" not in seen():
        failures.append("no double-line frame: the console did not get UTF-8")
    proc.write("\x1b[C")
    if not wait_for("2/6", 15):
        failures.append("the right arrow did not page to slide 2/6")
    proc.write("q")
    end = time.time() + 15
    while proc.isalive() and time.time() < end:
        time.sleep(0.1)
    if proc.isalive():
        failures.append("q did not quit")
        proc.terminate(force=True)
    elif proc.exitstatus not in (0, None):
        failures.append("exit status %r after q" % proc.exitstatus)
    tail = seen()[-400:]
    for f in failures:
        print("FAIL", f)
    if failures:
        print("last output:", repr(tail))
        return 1
    print("ok: ConPTY presenter drew slide 1, paged to 2 and quit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
