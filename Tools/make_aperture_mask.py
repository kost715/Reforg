"""Builds the radial aperture mask: PNG source plus an uncompressed EDDS with a full mip chain.

Every channel holds the normalised distance from the centre (0 centre, 255 at the edge and beyond).
"""
import struct
import sys

import numpy as np
from PIL import Image

SIZE = 512
out_png, out_edds = sys.argv[1], sys.argv[2]


def radial(n: int) -> np.ndarray:
    c = (np.arange(n) + 0.5 - n / 2) / (n / 2)
    r = np.sqrt(c[None, :] ** 2 + c[:, None] ** 2)
    v = np.clip(np.rint(np.clip(r, 0, 1) * 255), 0, 255).astype(np.uint8)
    return np.repeat(v[:, :, None], 4, axis=2)


top = radial(SIZE)
Image.fromarray(top, "RGBA").save(out_png, optimize=True)

mips = []
n = SIZE
while n >= 1:
    mips.append(radial(n).tobytes())
    n //= 2

DDSD = 0xA1007          # CAPS | HEIGHT | WIDTH | PIXELFORMAT | MIPMAPCOUNT | LINEARSIZE
CAPS = 0x401008         # COMPLEX | TEXTURE | MIPMAP
header = struct.pack("<4s7I", b"DDS ", 124, DDSD, SIZE, SIZE, len(mips[0]), 0, len(mips))
header += struct.pack("<11I", 0, 0x31464E45, 0, 0, 0, 0, 0, 0, 0, 0, 0)
header += struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
header += struct.pack("<5I", CAPS, 0, 0, 0, 0)
header += struct.pack("<5I", 28, 3, 0, 1, 0)   # DXGI_FORMAT_R8G8B8A8_UNORM, TEXTURE2D
assert len(header) == 148

# Enfusion block table: smallest mip first, then the blocks in the same order.
blocks = mips[::-1]
table = b"".join(struct.pack("<4sI", b"COPY", len(b)) for b in blocks)
with open(out_edds, "wb") as f:
    f.write(header + table + b"".join(blocks))
print("mips", len(mips), "edds bytes", 148 + len(table) + sum(map(len, blocks)))
