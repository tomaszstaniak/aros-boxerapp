#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Build the runtime replacements for the Apple system images Boxer uses by
name (Reveal, lock states) and the full-screen glyph, from Feather 4.29.2
(MIT), kept unmodified in assets/source/replacements/. Outputs go to
assets/runtime/replacements/.

History: until 2026-10-02 this also built Tango stand-ins for gamefolder,
import, prompt and Game; Boxer's originals are used again since then
(scripts/boxer-assets.py --restore-originals). The Tango sources stay in
assets/source/replacements/ as history, unused.

macOS host only: sips rasterises the SVGs. Usage: make-replacement-assets.py
Prints the sha256 of every output for assets/replacement-assets.json.
"""
import hashlib, os, struct, subprocess, sys, tempfile, zlib

sys.path.insert(0, os.path.dirname(__file__))
from png2inc import decode  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets/source/replacements")
OUT = os.path.join(ROOT, "assets/runtime/replacements")
FEATHER = os.path.join(SRC, "feather-icons-4.29.2")


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(k, d):
        return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def sips(svg, w, h, out):
    subprocess.run(["sips", "-s", "format", "png", "-z", str(h), str(w), svg, "--out", out],
                   check=True, stdout=subprocess.DEVNULL)
    # sips may write 16-bit or palette PNGs; normalise to 8-bit RGBA via decode.
    pw, ph, px = decode(out)
    write_png(out, pw, ph, px)


def feather(name, size, out, stroke="2.5"):
    # Template glyphs: only alpha counts. currentColor is not resolved by
    # sips, and the 2 px stroke of a 24 px design is too thin at 14 px.
    svg = open(os.path.join(FEATHER, "dist/icons/%s.svg" % name)).read()
    svg = svg.replace("currentColor", "#000000").replace('stroke-width="2"', 'stroke-width="%s"' % stroke)
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as t:
        t.write(svg)
    sips(t.name, size, size, out)
    os.unlink(t.name)


def main():
    os.makedirs(OUT, exist_ok=True)
    o = lambda n: os.path.join(OUT, n)
    feather("search", 14, o("RevealTemplate.png"))
    feather("maximize-2", 14, o("FullScreenTemplate.png"))
    feather("lock", 14, o("LockLockedTemplate.png"))
    feather("unlock", 14, o("LockUnlockedTemplate.png"))
    for n in sorted(os.listdir(OUT)):
        print(hashlib.sha256(open(o(n), "rb").read()).hexdigest(), "assets/runtime/replacements/" + n)


if __name__ == "__main__":
    main()
