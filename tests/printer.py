#!/usr/bin/env python3
"""Native printer dispatch regressions. Fake helper; never consumes labels."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="charts-printer-test-") as directory:
    root = Path(directory)
    csv = root / "sample.csv"
    csv.write_text("item,value\nAlpha,10\nBeta,20\nGamma,15\n")
    helper = root / "fake helper"
    helper.write_text("#!/usr/bin/env python3\n"
                      "import os,sys,json\n"
                      "from pathlib import Path\n"
                      "Path(os.environ['CAPTURE']).write_bytes(sys.stdin.buffer.read())\n"
                      "Path(os.environ['ARGUMENTS']).write_text(json.dumps(sys.argv[1:]))\n"
                      "sys.exit(int(os.environ.get('HELPER_EXIT','0')))\n")
    helper.chmod(0o755)
    capture, arguments = root / "captured.png", root / "args.json"
    env = dict(os.environ, CHARTS_SUPVAN_PRINT=str(helper),
               CAPTURE=str(capture), ARGUMENTS=str(arguments))
    checks = 0

    def invoke(options, expected=0, submitted=True, extra_env=None):
        global checks
        capture.unlink(missing_ok=True)
        result = subprocess.run([binary, str(csv), *options], env=env | (extra_env or {}),
                                capture_output=True, timeout=15)
        assert result.returncode == expected, (options, result.returncode, result.stderr)
        assert capture.exists() == submitted, (options, "unexpected helper execution")
        checks += 1
        return result

    invoke(["--printer", "supvan"])
    data = capture.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    assert struct.unpack(">II", data[16:24]) == (608, 352)
    assert json.loads(arguments.read_text()) == ["-"]
    preview = str(root / "it's a preview; not shell.png")
    invoke(["--printer=supvan", "--printer-preview", preview])
    assert json.loads(arguments.read_text()) == ["--preview", preview, "-"]
    invoke(["--printer", "supvan", "--size", "50x18"])
    assert struct.unpack(">II", capture.read_bytes()[16:24]) == (400, 288)
    for options in (["--printer", "other"], ["--printer-preview", preview],
                    ["--printer", "supvan", "--print"], ["--printer", "supvan", "--check"],
                    ["--printer", "supvan", "--show"], ["--printer", "supvan", "--describe"],
                    ["--printer", "supvan", "--png", "ignored.png"],
                    ["--printer", "supvan", "--scale", "2"]):
        invoke(options, expected=2, submitted=False)
    invoke(["--printer", "supvan"], expected=1, extra_env={"HELPER_EXIT": "7"})
    invoke(["--printer", "supvan"], expected=1, submitted=False,
           extra_env={"CHARTS_SUPVAN_PRINT": str(root / "missing-helper")})
    invoke(["--print"], submitted=False)
    csv2 = root / "other.csv"
    csv2.write_text(csv.read_text())
    invoke([str(csv2), "--printer", "supvan"], expected=2, submitted=False)
    invoke([str(csv2), "--printer", "supvan", "--slide", "2"])
    invoke(["--printer", "supvan", "--slide", "99"], expected=1, submitted=False)
    csv.write_text("item,value\nAlpha,not-a-number\n")
    invoke(["--printer", "supvan"], expected=1, submitted=False)
    print(f"{checks} printer dispatch checks passed (no physical jobs)")
