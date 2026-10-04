#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Write a Workbench icon for a file of the package (tool, project or
drawer), with a real image from a PNG. Same file layout as
tools/mkgameboxicon.py (classic DiskObject + 2-plane fallback + OS 3.5
FORM ICON with one 8-bit IMAG), which both ABIs read identically.

A drawer icon carries the DrawerData that icon.library expects whenever
do_DrawerData is set (diskobjio.c: OldDrawerData after the DiskObject,
the 6-byte NewDrawerData after the ToolTypes for disk revision 1).

Usage: mkicon.py OUT.info tool|project|drawer [--image PNG|builtin:page] [--size N]
                 [--default-tool PATH] [--stack N] [--tooltype K=V]...
--size scales the PNG to N x N (area average) first; Wanderer shows the
image at its own size, so a 128 px source would make a huge icon.
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkgameboxicon import build, ROOT  # noqa: E402
from png2inc import decode  # noqa: E402

KIND = {"tool": 3, "project": 4, "drawer": 2}


def scale(w, h, rgba, n):
    """Area-average resample to n x n, alpha-weighted so transparent pixels
    do not darken the edges."""
    out = bytearray(n * n * 4)
    for oy in range(n):
        y0, y1 = oy * h / n, (oy + 1) * h / n
        for ox in range(n):
            x0, x1 = ox * w / n, (ox + 1) * w / n
            acc = [0.0, 0.0, 0.0, 0.0]
            tot = 0.0
            for sy in range(int(y0), min(h, int(y1 + 0.999999))):
                fy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(w, int(x1 + 0.999999))):
                    f = (min(x1, sx + 1) - max(x0, sx)) * fy
                    if f <= 0:
                        continue
                    r, g, b, a = rgba[(sy * w + sx) * 4:(sy * w + sx) * 4 + 4]
                    acc[0] += r * a * f; acc[1] += g * a * f; acc[2] += b * a * f
                    acc[3] += a * f
                    tot += f
            o = (oy * n + ox) * 4
            if acc[3] > 0:
                out[o:o + 3] = bytes(min(255, int(acc[i] / acc[3] + 0.5)) for i in range(3))
            out[o + 3] = min(255, int(acc[3] / tot + 0.5)) if tot else 0
    return n, n, bytes(out)


def page():
    """A plain text page (project-drawn), for documents: Boxer's own
    documentation art is a one-colour template that reads as a blot."""
    w, h = 32, 40
    px = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            if 2 <= x < 30 and 1 <= y < 39:
                edge = x in (2, 29) or y in (1, 38)
                line = 6 <= x < 26 and y >= 7 and y < 34 and (y - 7) % 4 == 0 and not (y == 31 and x > 18)
                c = (40, 40, 40) if edge else ((90, 90, 110) if line else (250, 250, 245))
                px[o:o + 4] = bytes(c + (255,))
    return w, h, bytes(px)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out")
    p.add_argument("kind", choices=sorted(KIND))
    p.add_argument("--image", default=os.path.join(ROOT, "assets/runtime/boxer/boxer-32.png"))
    p.add_argument("--size", type=int)
    p.add_argument("--default-tool")
    p.add_argument("--stack", type=int, default=0)
    p.add_argument("--tooltype", action="append", default=[])
    a = p.parse_args()
    w, h, rgba = page() if a.image == "builtin:page" else decode(a.image)
    if a.size and (a.size, a.size) != (w, h):
        w, h, rgba = scale(w, h, rgba, a.size)
    data, n = build(KIND[a.kind], a.default_tool, a.tooltype, a.stack, None, pixels=(w, h, rgba),
                    drawer=a.kind == "drawer")
    open(a.out, "wb").write(data)
    print("%s: %d bytes, %s, %dx%d, %d colours" % (a.out, len(data), a.kind, w, h, n))


if __name__ == "__main__":
    main()
