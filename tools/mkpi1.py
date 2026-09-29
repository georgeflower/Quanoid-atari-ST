#!/usr/bin/env python3
"""Build a 320x200 Degas .PI1 title image for Quanoid.

Source can be:
  - an Atari monochrome Degas file (.PI3, 640x400, 1 bitplane), halftone
    downsampled 2:1 into a 4-level greyscale PI1, or
  - any image PIL can open (PNG/JPG/...), resized to 320x200 and reduced
    to the same 4 grey levels.

Palette layout written into the PI1 (also what the game expects at runtime):
  0  background (dark navy, matches the game's usual COL_BG)
  1  UI highlight (red/orange, used by the game for "1st place" etc.)
  2  dark grey (art)
  3  mid grey (art)
  4  white (art)
  15 UI text white (used by the game for menu text)
  5-14 spare - free for a hand-drawn image to use for extra shading/colour;
       filled with background here.
Because this palette is loaded from the file at runtime, repainting the
picture (including recolouring the reserved slots) changes the title
screen with no code change - just overwrite assets/TITLE.PI1.
"""
import struct
import sys

PAL = [0x002, 0x700, 0x333, 0x555, 0x777, 0x002, 0x002, 0x002,
       0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002]


def load_mono_pi3(path):
    d = open(path, "rb").read()
    body = d[34:34 + 32000]
    w, h = 640, 400
    bpr = w // 8
    px = [[0] * w for _ in range(h)]
    for y in range(h):
        row = body[y * bpr:(y + 1) * bpr]
        for xb, b in enumerate(row):
            for bit in range(8):
                if b & (0x80 >> bit):
                    px[y][xb * 8 + bit] = 1
    return px, w, h


def halftone_to_levels(px, w, h):
    """2x2 box downsample to 320x200, then map dot density to 4 grey levels."""
    ow, oh = w // 2, h // 2
    out = [[0] * ow for _ in range(oh)]
    bucket = [0, 2, 3, 3, 4]      # count of white sub-pixels (0..4) -> palette index
    for y in range(oh):
        for x in range(ow):
            c = px[y * 2][x * 2] + px[y * 2][x * 2 + 1] + px[y * 2 + 1][x * 2] + px[y * 2 + 1][x * 2 + 1]
            out[y][x] = bucket[c]
    return out


def load_generic(path):
    from PIL import Image
    im = Image.open(path).convert("L").resize((320, 200))
    px = im.load()
    out = [[0] * 320 for _ in range(200)]
    for y in range(200):
        for x in range(320):
            v = px[x, y]
            out[y][x] = 0 if v < 48 else 2 if v < 112 else 3 if v < 176 else 4
    return out


def write_pi1(path, levels):
    out = bytearray(struct.pack(">H", 0))
    for c in PAL:
        out += struct.pack(">H", c)
    for row in levels:
        for g in range(20):
            planes = [0, 0, 0, 0]
            for b in range(16):
                v = row[g * 16 + b]
                for p in range(4):
                    if v & (1 << p):
                        planes[p] |= 0x8000 >> b
            for p in range(4):
                out += struct.pack(">H", planes[p])
    assert len(out) == 34 + 32000
    open(path, "wb").write(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if src.lower().endswith(".pi3"):
        px, w, h = load_mono_pi3(src)
        levels = halftone_to_levels(px, w, h)
    else:
        levels = load_generic(src)
    write_pi1(dst, levels)
    print("wrote", dst)


if __name__ == "__main__":
    main()
