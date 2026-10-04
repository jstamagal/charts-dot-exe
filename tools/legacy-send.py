#!/usr/bin/env python3
"""Type DOS command lines through QEMU's local human monitor socket."""
import argparse
import socket
import time


KEYS = {" ": "spc", ":": "shift-semicolon", "\\": "backslash",
        ".": "dot", "-": "minus", "_": "shift-minus", "/": "slash", "=": "equal",
        "@": "shift-2", "*": "shift-8", "%": "shift-5",
        "(": "shift-9", ")": "shift-0", ">": "shift-dot",
        '"': "shift-apostrophe", "+": "shift-equal"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("monitor", help="QEMU monitor UNIX socket")
    parser.add_argument("commands", nargs="+", help="DOS commands, one per line")
    parser.add_argument("--delay", type=float, default=0.12,
                        help="pause between emulated key presses (default: 0.12s)")
    args = parser.parse_args()
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(args.monitor)
    sock.settimeout(0.1)
    try:
        try:
            sock.recv(4096)
        except (TimeoutError, socket.timeout):
            pass
        for command in args.commands:
            for char in command:
                key = KEYS.get(char, char.lower())
                sock.sendall(("sendkey " + key + "\n").encode("ascii"))
                time.sleep(args.delay)
            sock.sendall(b"sendkey ret\n")
            time.sleep(max(args.delay, 0.2))
    finally:
        sock.close()


if __name__ == "__main__":
    main()
