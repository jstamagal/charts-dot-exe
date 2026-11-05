#!/usr/bin/env python3
"""Native TCP/CLI regressions. Mock peer; never submits physical print jobs."""
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading

if not sys.platform.startswith("linux"):
    print("native printer tests skipped: Linux-only transport")
    raise SystemExit(0)

binary = str(Path(sys.argv[1]).resolve())
MAGIC = b"CHPR0001"
TOKEN = b"a" * 64


def receive(sock, count):
    result = bytearray()
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            raise EOFError("short frame")
        result.extend(chunk)
    return bytes(result)


class Peer:
    def __init__(self):
        self.socket = socket.socket()
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen()
        self.socket.settimeout(0.1)
        self.target = f"supvan://127.0.0.1:{self.socket.getsockname()[1]}"
        self.jobs = []
        self.mode = "ok"
        self.stop = False
        self.errors = []
        self.thread = threading.Thread(target=self.serve)
        self.thread.start()

    def serve(self):
        while not self.stop:
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(5)
                try:
                    header = receive(connection, 80)
                    assert header[:8] == MAGIC and header[8:72] == TOKEN
                    op = header[72]
                    width, height = struct.unpack(">HH", header[76:])
                    pixels = receive(connection, width * height)
                    self.jobs.append((op, width, height, pixels))
                    if self.mode == "disconnect":
                        continue
                    if self.mode == "oversized":
                        connection.sendall(MAGIC + struct.pack(">II", 0, 0xFFFFFFFF))
                        continue
                    status = 0
                    if self.mode == "error":
                        status, body = 1, b"mock printer fault"
                    elif op == 3:
                        body = struct.pack(">HH", 384, 640) + bytes([15]) * (384 * 640)
                        if self.mode == "bad-preview":
                            body = struct.pack(">HH", 0, 640)
                        if self.mode == "bad-palette":
                            body = body[:-1] + b"\xff"
                    else:
                        body = b'{"remaining":42,"busy":false}\n'
                    connection.sendall(MAGIC + struct.pack(">II", status, len(body)) + body)
                except Exception as error:
                    self.errors.append(error)

    def close(self):
        self.stop = True
        self.thread.join(timeout=6)
        self.socket.close()
        assert not self.thread.is_alive()
        assert not self.errors, self.errors


with tempfile.TemporaryDirectory(prefix="charts-printer-test-") as directory:
    root = Path(directory)
    csv = root / "sample.csv"
    csv.write_text("item,value\nAlpha,10\nBeta,20\nGamma,15\n")
    token = root / "token"
    token.write_bytes(TOKEN + b"\n")
    token.chmod(0o600)
    peer = Peer()
    env = dict(os.environ, CHARTS_PRINTER=peer.target, CHARTS_PRINTER_TOKEN_FILE=str(token),
               CHARTS_SUPVAN_PRINT="/does/not/exist", PATH="/no/executables")
    checks = 0

    def invoke(options, expected=0, submitted=True, source=True, extra_env=None):
        global checks
        before = len(peer.jobs)
        command = [binary] + ([str(csv)] if source else []) + options
        result = subprocess.run(command, env=env | (extra_env or {}),
                                capture_output=True, timeout=15)
        assert result.returncode == expected, (options, result.returncode, result.stderr)
        assert len(peer.jobs) - before == int(submitted), (options, "unexpected network submission count")
        checks += 1
        return result

    try:
        invoke(["--printer", "supvan"])
        op, width, height, pixels = peer.jobs[-1]
        assert op == 2 and (width, height) == (608, 352)
        assert 0 in pixels and max(pixels) <= 15
        # Bars on white paper must contain actual black fill, not just outlines.
        assert pixels.count(0) > len(pixels) // 20
        preview = root / "it's a preview; not shell.png"
        invoke(["--printer", peer.target, "--printer-preview", str(preview)])
        assert peer.jobs[-1][0] == 3
        assert preview.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        assert struct.unpack(">II", preview.read_bytes()[16:24]) == (384, 640)
        invoke(["--printer", "supvan", "--size", "50x18"])
        assert peer.jobs[-1][1:3] == (400, 288)
        result = invoke(["--printer-status"], source=False)
        assert peer.jobs[-1][:3] == (1, 0, 0)
        assert json.loads(result.stdout)["remaining"] == 42
        for options in (["--printer", "other"], ["--printer-preview", str(preview)],
                        ["--printer", "supvan", "--print"], ["--printer", "supvan", "--check"],
                        ["--printer", "supvan", "--show"], ["--printer", "supvan", "--describe"],
                        ["--printer", "supvan", "--png", "ignored.png"],
                        ["--printer", "supvan", "--scale", "2"],
                        ["--printer-status"], ["--printer-listen", "127.0.0.1:9280"]):
            invoke(options, expected=2, submitted=False)
        for mode in ("error", "disconnect", "oversized", "bad-preview", "bad-palette"):
            peer.mode = mode
            invoke(["--printer", "supvan", "--printer-preview", str(preview)], expected=1)
        peer.mode = "ok"
        token.chmod(0o644)
        invoke(["--printer", "supvan"], expected=1, submitted=False)
        token.chmod(0o600)
        invoke(["--print"], submitted=False)
        csv2 = root / "other.csv"
        csv2.write_text(csv.read_text())
        invoke([str(csv2), "--printer", "supvan"], expected=2, submitted=False)
        invoke([str(csv2), "--printer", "supvan", "--slide", "2"])
        invoke(["--printer", "supvan", "--slide", "99"], expected=1, submitted=False)
        csv.write_text("item,value\nAlpha,not-a-number\n")
        invoke(["--printer", "supvan"], expected=1, submitted=False)
        invoke(["--printer-listen", "0.0.0.0:9280"], source=False, expected=1, submitted=False)
    finally:
        peer.close()

    # Test the actual native listener's authentication and input rejection.
    reservation = socket.socket()
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
    reservation.close()
    process = subprocess.Popen([binary, "--printer-listen", f"127.0.0.1:{port}"], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        assert select.select([process.stderr], [], [], 10)[0], "listener did not start"
        ready = process.stderr.readline()
        assert b"native printer listener" in ready, ready
        for secret, op, width, height in ((b"b" * 64, 1, 0, 0),
                                        (TOKEN, 99, 0, 0), (TOKEN, 2, 0, 0),
                                        (TOKEN, 2, 8192, 8192), (TOKEN, 1, 1, 1)):
            with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                connection.sendall(MAGIC + secret + bytes([op, 0, 0, 0]) + struct.pack(">HH", width, height))
                reply = receive(connection, 16)
                assert reply[:8] == MAGIC and struct.unpack(">I", reply[8:12])[0] == 1
                assert receive(connection, struct.unpack(">I", reply[12:])[0])
                checks += 1
    finally:
        process.terminate()
        _, errors = process.communicate(timeout=10)
        assert process.returncode == 0, errors
        assert b"Sanitizer" not in errors, errors
    print(f"{checks} native printer checks passed (empty PATH, no physical jobs)")
