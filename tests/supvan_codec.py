#!/usr/bin/env python3
"""Independent standard-library LZMA decoder oracle for the C89 encoder."""
import lzma
from pathlib import Path
import random
import struct
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())
random_source = random.Random(731)
fixtures = [b"", b"x", bytes(4096), bytes([255]) * 4096,
            b"abc" * 20000, bytes(range(256)) * 256,
            bytes(random_source.randrange(256) for _ in range(65536)), bytes(262144)]
for _ in range(24):
    # The native fallback uses at most 32 x 48 raster bytes, leaving a zero
    # tail in the fixed-size firmware buffer. Even random pixels must fit.
    buffer = bytearray(4096)
    buffer[2:14] = bytes([14, 80, 32, 0, 48, 0, 1, 0, 1, 0, 4, 0])
    buffer[14:1550] = bytes(random_source.randrange(256) for _ in range(1536))
    fixtures.append(bytes(buffer))

subprocess.run([binary], check=True)
for index, data in enumerate(fixtures):
    encoded = subprocess.run([binary, "encode"], input=data, capture_output=True,
                             timeout=10, check=True).stdout
    assert encoded[0] == 0x5D
    assert struct.unpack("<I", encoded[1:5])[0] == 8192
    assert struct.unpack("<Q", encoded[5:13])[0] == len(data)
    assert lzma.decompress(encoded, format=lzma.FORMAT_ALONE) == data
    if index >= 8:
        assert len(encoded) <= 4096, len(encoded)
print(f"{len(fixtures)} independent LZMA round trips passed")
